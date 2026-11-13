// SPDX-License-Identifier: MIT
//
// Windows audio smoke test: the device list, and starting and stopping every
// kind of capture and playback without crashing or hanging. Build machines
// usually have no sound devices, so missing devices are not failures.
#include "audio/CaptureStream.h"
#include "audio/Player.h"
#include "audio/TrackWriter.h"

#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <cmath>
#include <chrono>
#include <thread>

using namespace afterline::audio;

class WasapiTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void listsDevices()
    {
        PipeWireGraph graph;
        std::string err;
        QVERIFY2(graph.start(&err), err.c_str());
        QVERIFY(graph.running());
        for (const auto &n : graph.nodes())
            qInfo("%s | %s | %s", n.mediaClass.c_str(), n.label().c_str(), appKey(n).c_str());
        qInfo("default output: %s", graph.defaultSinkName().c_str());
        qInfo("default input: %s", graph.defaultSourceName().c_str());
        graph.stop();
        QVERIFY(!graph.running());
    }

    void startsAndStopsEveryKindOfCapture()
    {
        PipeWireGraph graph;
        QVERIFY(graph.start(nullptr));
        for (auto kind : {CaptureStream::Kind::DefaultOutput, CaptureStream::Kind::DefaultInput,
                          CaptureStream::Kind::Application}) {
            std::atomic<size_t> frames{0};
            CaptureStream::Config c;
            c.kind = kind;
            c.target = kind == CaptureStream::Kind::Application ? "bin:explorer" : "";
            CaptureStream s(graph, c, [&](const float *, size_t n) { frames += n; });
            std::string err;
            QVERIFY2(s.start(&err), err.c_str());
            std::this_thread::sleep_for(std::chrono::milliseconds(700));
            qInfo("kind %d: %s (%s), %zu frames", int(kind), stateName(s.state()), s.message().c_str(),
                  frames.load());
            QVERIFY(s.state() != CaptureStream::State::Idle);
            s.stop();
            QCOMPARE(s.state(), CaptureStream::State::Idle);
        }
    }

    void missingDeviceIsReported()
    {
        PipeWireGraph graph;
        QVERIFY(graph.start(nullptr));
        CaptureStream s(graph, {CaptureStream::Kind::InputDevice, "{no-such-device}", ""}, [](const float *, size_t) {});
        std::string err;
        QVERIFY(!s.start(&err));
        QVERIFY(!err.empty());
    }

    void playsARecording()
    {
        QTemporaryDir dir;
        const std::string path = (dir.path() + "/t.opus").toStdString();
        {
            TrackWriter w;
            TrackWriter::Options o;
            o.audioPath = path;
            o.peaksPath = (dir.path() + "/t.peaks").toStdString();
            QVERIFY(w.start(o, nullptr));
            std::vector<float> tone(48000);
            for (size_t i = 0; i < tone.size(); ++i)
                tone[i] = 0.1f * float(std::sin(i * 0.05));
            w.push(tone.data(), tone.size());
            QVERIFY(w.stop());
        }
        PipeWireGraph graph;
        QVERIFY(graph.start(nullptr));
        Player p(graph);
        std::string err;
        QVERIFY2(p.open(path, "", &err), err.c_str());
        QVERIFY(p.duration() > 0.5);
        p.play();
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        p.seek(0.5);
        p.pause();
        p.close();
    }
};

QTEST_GUILESS_MAIN(WasapiTest)
#include "test_wasapi.moc"
