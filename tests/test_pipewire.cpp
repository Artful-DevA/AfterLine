// SPDX-License-Identifier: MIT
//
// Recording tests against a real PipeWire + WirePlumber session.
//
// tests/run-pipewire-tests.sh starts a private PipeWire session with a virtual
// speaker and microphone so these run anywhere (CI containers included). They
// can also run on a desktop session, where they create and remove their own
// virtual devices. The test exits with 77 (skipped) when no PipeWire is found.
//
// "Applications" are pw-play processes with distinct application identities
// playing distinct tones, so each test can check by frequency exactly whose
// audio ended up in the recording.
#include "audio/PipeWireGraph.h"
#include "audio/Recorder.h"
#include "core/OggOpus.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>

using namespace afterline;
using namespace afterline::audio;

// Readable QCOMPARE failures for capture states (also required by Qt 6.2).
namespace QTest {
template <>
inline char *toString(const CaptureStream::State &s)
{
    return qstrdup(afterline::audio::stateName(s));
}
}  // namespace QTest

namespace {

// Energy of one frequency (Goertzel), normalised by length.
double toneEnergy(const std::vector<float> &v, double freq, int rate, size_t from = 0, size_t to = SIZE_MAX)
{
    to = std::min(to, v.size());
    if (from >= to)
        return 0;
    const double w = 2 * M_PI * freq / rate, c = 2 * std::cos(w);
    double s1 = 0, s2 = 0;
    for (size_t i = from; i < to; ++i) {
        const double s = v[i] + c * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return (s1 * s1 + s2 * s2 - c * s1 * s2) / double(to - from) / double(to - from);
}

double rms(const std::vector<float> &v, size_t from = 0, size_t to = SIZE_MAX)
{
    to = std::min(to, v.size());
    double s = 0;
    for (size_t i = from; i < to; ++i)
        s += double(v[i]) * v[i];
    return to > from ? std::sqrt(s / double(to - from)) : 0;
}

// An application playing a tone ("freq:seconds", see PipeWireTest::wav) with
// its own identity, via tests/tools/fake_app.cpp.
class FakeApp {
public:
    FakeApp(const QString &binary, const QString &tone, const QString &target = {})
    {
        QStringList args{"--binary", binary, "--freq", tone.section(':', 0, 0), "--seconds", tone.section(':', 1, 1)};
        if (!target.isEmpty())
            args << "--target" << target;
        m_proc.start(QStringLiteral(AFTERLINE_FAKE_APP), args);
        m_proc.waitForStarted();
    }
    ~FakeApp() { stop(); }
    void stop()
    {
        if (m_proc.state() != QProcess::NotRunning) {
            m_proc.terminate();
            m_proc.waitForFinished(3000);
        }
    }

private:
    QProcess m_proc;
};

void wait(double seconds)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < seconds * 1000)
        QThread::msleep(20);
}

QString pwCli(const QStringList &args)
{
    QProcess p;
    p.start("pw-cli", args);
    p.waitForFinished(5000);
    return QString::fromUtf8(p.readAllStandardOutput());
}

}  // namespace

class PipeWireTest : public QObject {
    Q_OBJECT
    PipeWireGraph graph;
    QTemporaryDir tmp;
    QString speakers = "afterline-test-speakers";
    QString mic = "afterline-test-mic";

    uint32_t nodeId(const QString &name)
    {
        for (const auto &n : graph.nodes())
            if (QString::fromStdString(n.name) == name)
                return n.id;
        return 0;
    }

    // A microphone that behaves like hardware: audio played into "<name>-in"
    // comes out of the Audio/Source "<name>". Killing the process is the
    // equivalent of unplugging it.
    std::map<QString, std::unique_ptr<QProcess>> mics;

    void createMic(const QString &name)
    {
        auto p = std::make_unique<QProcess>();
        p->start("pw-loopback",
                 {"--channels", "1",
                  "--capture-props", QString("media.class=Audio/Sink node.name=%1-in audio.position=[MONO]").arg(name),
                  "--playback-props",
                  QString("media.class=Audio/Source node.name=%1 node.description=%1 audio.position=[MONO] priority.session=2500").arg(name)});
        p->waitForStarted();
        mics[name] = std::move(p);
        for (int i = 0; i < 150 && !(nodeId(name) && nodeId(name + "-in")); ++i)
            QThread::msleep(20);
    }

    void unplugMic(const QString &name)
    {
        auto it = mics.find(name);
        if (it == mics.end())
            return;
        it->second->terminate();
        it->second->waitForFinished(3000);
        mics.erase(it);
        for (int i = 0; i < 150 && nodeId(name); ++i)
            QThread::msleep(20);
    }

    void createDevice(const QString &name, bool sink)
    {
        pwCli({"create-node", "adapter",
               QString("{ factory.name=support.null-audio-sink node.name=%1 node.description=\"%1\" "
                       "media.class=%2 object.linger=true audio.position=[%3] priority.session=%4 }")
                   .arg(name, sink ? "Audio/Sink" : "Audio/Source/Virtual", sink ? "FL FR" : "MONO")
                   .arg(sink ? 3000 : 2000)});
        for (int i = 0; i < 100 && !nodeId(name); ++i)
            QThread::msleep(20);
    }

    void destroyDevice(const QString &name)
    {
        if (uint32_t id = nodeId(name))
            pwCli({"destroy", QString::number(id)});
        for (int i = 0; i < 100 && nodeId(name); ++i)
            QThread::msleep(20);
    }

    QString wav(double freq, double seconds = 8) { return QString("%1:%2").arg(freq).arg(seconds); }

    std::vector<float> decode(const QString &dir, const char *file)
    {
        std::string err;
        auto pcm = decodeOggOpus((dir + "/" + file).toStdString(), 16000, &err);
        if (pcm.empty())
            qWarning() << "decode" << file << QString::fromStdString(err);
        return pcm;
    }

    QString newDir(const char *name)
    {
        QDir(tmp.path()).mkpath(name);
        return tmp.filePath(name);
    }

private Q_SLOTS:
    void initTestCase()
    {
        std::string err;
        if (!graph.start(&err)) {
            qWarning("No PipeWire session (%s); skipping.", err.c_str());
            std::exit(77);
        }
        if (QStandardPaths::findExecutable("pw-loopback").isEmpty() ||
            QStandardPaths::findExecutable("pw-cli").isEmpty()) {
            qWarning("pw-loopback/pw-cli not installed; skipping.");
            std::exit(77);
        }
        createDevice(speakers, true);
        createMic(mic);
        QVERIFY(nodeId(speakers));
        QVERIFY(nodeId(mic));
        // Make the test speaker the default so "entire computer" means it.
        pwCli({"s", "0", "default.configured.audio.sink", QString("{ \"name\": \"%1\" }").arg(speakers)});
        QProcess::execute("pw-metadata", {"0", "default.configured.audio.sink",
                                          QString("{ \"name\": \"%1\" }").arg(speakers),
                                          "Spa:String:JSON"});
        for (int i = 0; i < 100 && graph.defaultSinkName() != speakers.toStdString(); ++i)
            QThread::msleep(20);
        qInfo() << "PipeWire" << QString::fromStdString(graph.serverVersion()) << "default sink"
                << QString::fromStdString(graph.defaultSinkName());
    }

    void cleanupTestCase()
    {
        destroyDevice(speakers);
        unplugMic(mic);
        unplugMic("afterline-test-usb-mic");
    }

    void discoversDevicesAndApplications()
    {
        FakeApp app("fakemeet", wav(440, 3), speakers);
        wait(0.6);
        graph.sync();
        bool sawSink = false, sawMic = false, sawApp = false;
        for (const auto &n : graph.nodes()) {
            sawSink |= n.isSink() && n.name == speakers.toStdString();
            sawMic |= n.isSource() && n.name == mic.toStdString();
            if (n.isAppOutput() && appKey(n) == "bin:fakemeet") {
                sawApp = true;
                QCOMPARE(QString::fromStdString(appDisplayName(n)), QString("fakemeet"));
            }
        }
        QVERIFY(sawSink);
        QVERIFY(sawMic);
        QVERIFY2(sawApp, "the test application's identity was not visible");
    }

    void recordsEntireComputer()
    {
        // PipeWire before 0.3.60's virtual null sink delivers silence on its
        // monitor ports in a headless session (stock pw-record too), so there
        // is nothing to measure. Real output devices are unaffected.
        const QStringList v = QString::fromStdString(graph.serverVersion()).split('.');
        if (v.size() >= 3 && v[0].toInt() == 0 && v[1].toInt() == 3 && v[2].toInt() < 60)
            QSKIP("virtual sink monitors are silent on this PipeWire version");
        const QString dir = newDir("system");
        Recorder rec(graph);
        RecorderConfig cfg;
        cfg.directory = dir.toStdString();
        cfg.computer = SourceChoice{CaptureStream::Kind::DefaultOutput, "", "Entire computer"};
        std::string err;
        QVERIFY2(rec.start(cfg, &err), err.c_str());
        FakeApp a("fakemeet", wav(440, 3), speakers);
        FakeApp b("fakemusic", wav(1700, 3), speakers);
        wait(2.5);
        QCOMPARE(rec.status().computer.state, CaptureStream::State::Active);
        QVERIFY(rec.status().computer.level > 0.05f);
        QVERIFY2(rec.stop(&err), err.c_str());
        auto pcm = decode(dir, Recorder::kComputerFile);
        QVERIFY(pcm.size() > 16000 * 2);
        // Both applications are audible in a whole-computer recording.
        QVERIFY(toneEnergy(pcm, 440, 16000) > 1e-3);
        QVERIFY(toneEnergy(pcm, 1700, 16000) > 1e-3);
        QVERIFY(QFile(dir + "/" + Recorder::kComputerPeaks).size() > 30);
    }

    void recordsOnlyTheChosenApplication()
    {
        const QString dir = newDir("app");
        FakeApp music("fakemusic", wav(1700, 8), speakers);
        FakeApp meeting("fakemeet", wav(440, 8), speakers);
        wait(0.5);
        Recorder rec(graph);
        RecorderConfig cfg;
        cfg.directory = dir.toStdString();
        cfg.computer = SourceChoice{CaptureStream::Kind::Application, "bin:fakemeet", "fakemeet"};
        std::string err;
        QVERIFY2(rec.start(cfg, &err), err.c_str());
        wait(2.5);
        const auto st = rec.status().computer;
        QCOMPARE(st.state, CaptureStream::State::Active);
        QCOMPARE(st.linkedStreams, 1);
        QVERIFY2(rec.stop(&err), err.c_str());
        auto pcm = decode(dir, Recorder::kComputerFile);
        QVERIFY(pcm.size() > 16000 * 2);
        const double meet = toneEnergy(pcm, 440, 16000), other = toneEnergy(pcm, 1700, 16000);
        qInfo() << "meeting tone" << meet << "other app" << other;
        QVERIFY(meet > 1e-3);
        QVERIFY2(other < meet / 1000, "audio from another application leaked into the recording");
    }

    void followsTheApplicationAcrossStreamRestarts()
    {
        // The meeting app plays, stops entirely (its stream is destroyed),
        // then comes back with a new stream, as when a call reconnects.
        const QString dir = newDir("restart");
        Recorder rec(graph);
        RecorderConfig cfg;
        cfg.directory = dir.toStdString();
        cfg.computer = SourceChoice{CaptureStream::Kind::Application, "bin:fakemeet", "fakemeet"};
        std::string err;
        QElapsedTimer wall;
        wall.start();
        QVERIFY2(rec.start(cfg, &err), err.c_str());
        QCOMPARE(rec.status().computer.state, CaptureStream::State::Waiting);  // not playing yet
        wait(0.5);
        {
            FakeApp first("fakemeet", wav(440, 8), speakers);
            wait(2.0);
            QCOMPARE(rec.status().computer.linkedStreams, 1);
        }
        wait(1.5);
        QCOMPARE(rec.status().computer.linkedStreams, 0);
        {
            FakeApp second("fakemeet", wav(880, 8), speakers);
            wait(2.0);
            QCOMPARE(rec.status().computer.linkedStreams, 1);
            QCOMPARE(rec.status().computer.state, CaptureStream::State::Active);
        }
        wait(0.4);
        QVERIFY2(rec.stop(&err), err.c_str());
        const double elapsed = wall.elapsed() / 1000.0;
        auto pcm = decode(dir, Recorder::kComputerFile);
        const double seconds = pcm.size() / 16000.0;
        qInfo() << "recorded" << seconds << "s over" << elapsed << "s of wall time";
        // Gaps are filled, so the recording keeps real time.
        QVERIFY(std::abs(seconds - elapsed) < 0.6);
        const size_t s = 16000;
        // First stream: 440 Hz around 1.5 s; gap around 3.3 s; second: 880 Hz near the end.
        QVERIFY(toneEnergy(pcm, 440, 16000, size_t(1.0 * s), size_t(2.0 * s)) > 1e-3);
        QVERIFY(rms(pcm, size_t(3.1 * s), size_t(3.6 * s)) < 0.01);
        const size_t end = pcm.size();
        QVERIFY(toneEnergy(pcm, 880, 16000, end - size_t(1.6 * s), end - size_t(0.6 * s)) > 1e-3);
    }

    void recordsMicrophoneOnItsOwnTrack()
    {
        const QString dir = newDir("mic");
        Recorder rec(graph);
        RecorderConfig cfg;
        cfg.directory = dir.toStdString();
        cfg.computer = SourceChoice{CaptureStream::Kind::DefaultOutput, "", "Entire computer"};
        cfg.microphone = SourceChoice{CaptureStream::Kind::InputDevice, mic.toStdString(), "Test mic"};
        std::string err;
        QVERIFY2(rec.start(cfg, &err), err.c_str());
        FakeApp speaker("fakemeet", wav(440, 4), speakers);
        FakeApp voice("fakevoice", wav(660, 4), mic + "-in");  // "speaking" into the mic
        wait(2.5);
        const auto st = rec.status();
        QCOMPARE(st.microphone.state, CaptureStream::State::Active);
        QVERIFY(st.microphone.level > 0.05f);
        QVERIFY2(rec.stop(&err), err.c_str());
        auto computer = decode(dir, Recorder::kComputerFile);
        auto micPcm = decode(dir, Recorder::kMicrophoneFile);
        QVERIFY(toneEnergy(micPcm, 660, 16000) > 1e-3);
        QVERIFY(toneEnergy(micPcm, 440, 16000) < toneEnergy(micPcm, 660, 16000) / 1000);
        QVERIFY(toneEnergy(computer, 660, 16000) < toneEnergy(computer, 440, 16000) / 1000);
        // Both tracks cover the same stretch of time.
        QVERIFY(std::abs(double(computer.size()) - double(micPcm.size())) < 16000 * 0.4);
    }

    void recordsTheDefaultMicrophone()
    {
        // "Default microphone" follows the system's choice (here the test mic,
        // the highest-priority source) without naming a device.
        const QString dir = newDir("defaultmic");
        Recorder rec(graph);
        RecorderConfig cfg;
        cfg.directory = dir.toStdString();
        cfg.microphone = SourceChoice{CaptureStream::Kind::DefaultInput, "", "Default microphone"};
        std::string err;
        QVERIFY2(rec.start(cfg, &err), err.c_str());
        FakeApp voice("fakevoice", wav(660, 4), mic + "-in");
        wait(2.5);
        QCOMPARE(rec.status().microphone.state, CaptureStream::State::Active);
        QVERIFY2(rec.stop(&err), err.c_str());
        auto pcm = decode(dir, Recorder::kMicrophoneFile);
        QVERIFY(toneEnergy(pcm, 660, 16000) > 1e-3);
    }

    void survivesMicrophoneUnplugAndReplug()
    {
        const QString usb = "afterline-test-usb-mic";
        createMic(usb);
        const QString dir = newDir("unplug");
        Recorder rec(graph);
        RecorderConfig cfg;
        cfg.directory = dir.toStdString();
        cfg.microphone = SourceChoice{CaptureStream::Kind::InputDevice, usb.toStdString(), "USB mic"};
        std::string err;
        QElapsedTimer wall;
        wall.start();
        QVERIFY2(rec.start(cfg, &err), err.c_str());
        {
            FakeApp voice("fakevoice", wav(660, 8), usb + "-in");
            wait(1.5);
            QCOMPARE(rec.status().microphone.state, CaptureStream::State::Active);
        }
        unplugMic(usb);
        wait(1.0);
        QCOMPARE(rec.status().microphone.state, CaptureStream::State::Lost);
        createMic(usb);
        wait(0.5);
        FakeApp voice("fakevoice", wav(330, 8), usb + "-in");
        wait(1.5);
        const auto st = rec.status().microphone;
        QCOMPARE(st.state, CaptureStream::State::Active);
        QCOMPARE(st.reconnects, 1);
        QVERIFY2(rec.stop(&err), err.c_str());
        auto pcm = decode(dir, Recorder::kMicrophoneFile);
        const double seconds = pcm.size() / 16000.0, elapsed = wall.elapsed() / 1000.0;
        qInfo() << "mic track" << seconds << "s over" << elapsed << "s";
        QVERIFY(std::abs(seconds - elapsed) < 0.7);
        QVERIFY(toneEnergy(pcm, 660, 16000, 0, 16000 * 1) > 1e-3);
        QVERIFY(toneEnergy(pcm, 330, 16000, pcm.size() - 16000, pcm.size()) > 1e-3);
    }

    void pauseLeavesNoGapInTheFile()
    {
        const QString dir = newDir("pause");
        Recorder rec(graph);
        RecorderConfig cfg;
        cfg.directory = dir.toStdString();
        cfg.computer = SourceChoice{CaptureStream::Kind::Application, "bin:fakemeet", "fakemeet"};
        FakeApp app("fakemeet", wav(440, 8), speakers);
        std::string err;
        QVERIFY2(rec.start(cfg, &err), err.c_str());
        wait(1.0);
        rec.pause();
        QCOMPARE(rec.status().state, RecorderStatus::Paused);
        wait(1.5);
        rec.resume();
        wait(1.0);
        QVERIFY2(rec.stop(&err), err.c_str());
        auto pcm = decode(dir, Recorder::kComputerFile);
        const double seconds = pcm.size() / 16000.0;
        qInfo() << "paused recording length" << seconds;
        QVERIFY(seconds > 1.6 && seconds < 2.5);
    }

    void reportsMissingDevices()
    {
        Recorder rec(graph);
        RecorderConfig cfg;
        cfg.directory = newDir("missing").toStdString();
        cfg.microphone = SourceChoice{CaptureStream::Kind::InputDevice, "no-such-device", "Ghost"};
        std::string err;
        QVERIFY(!rec.start(cfg, &err));
        QVERIFY(!err.empty());
        QVERIFY(!rec.active());
    }
};

QTEST_GUILESS_MAIN(PipeWireTest)
#include "test_pipewire.moc"
