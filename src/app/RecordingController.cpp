// SPDX-License-Identifier: MIT
#include "app/RecordingController.h"

#include "core/Transcript.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QSet>
#include <QStorageInfo>

#include <algorithm>
#include <cmath>
#include <ctime>

namespace afterline {

using audio::CaptureStream;

namespace {

constexpr qint64 kWarnFreeBytes = 1024LL * 1024 * 1024;   // ~19 hours of audio left
constexpr qint64 kStopFreeBytes = 200LL * 1024 * 1024;

qint64 bootMinusMonotonicMs()
{
    timespec boot{}, mono{};
    clock_gettime(CLOCK_BOOTTIME, &boot);
    clock_gettime(CLOCK_MONOTONIC, &mono);
    return (qint64(boot.tv_sec) - mono.tv_sec) * 1000 + (boot.tv_nsec - mono.tv_nsec) / 1000000;
}

// Meeting applications that are running but not playing yet can still be
// chosen: AfterLine links their audio as soon as it starts.
struct KnownApp { const char *process; const char *key; const char *name; const char *icon; };
const KnownApp kKnownApps[] = {
    {"firefox", "bin:firefox", "Firefox", "firefox"},
    {"chrome", "bin:chrome", "Google Chrome", "google-chrome"},
    {"chromium", "bin:chromium", "Chromium", "chromium"},
    {"brave", "bin:brave", "Brave", "brave-browser"},
    {"msedge", "bin:msedge", "Microsoft Edge", "microsoft-edge"},
    {"zoom", "bin:zoom", "Zoom", "Zoom"},
    {"Discord", "bin:discord", "Discord", "discord"},
    {"teams-for-linux", "bin:teams-for-linux", "Microsoft Teams", "teams-for-linux"},
    {"slack", "bin:slack", "Slack", "slack"},
    {"signal-desktop", "bin:signal-desktop", "Signal", "signal-desktop"},
};

QSet<QString> runningProcesses()
{
    QSet<QString> names;
    for (const QString &pid : QDir("/proc").entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!pid.at(0).isDigit())
            continue;
        QFile f("/proc/" + pid + "/comm");
        if (f.open(QIODevice::ReadOnly))
            names.insert(QString::fromUtf8(f.readAll()).trimmed());
    }
    return names;
}

}  // namespace

RecordingController::RecordingController(MeetingStore &store, QObject *parent) : QObject(parent), m_store(store)
{
    std::string err;
    if (!m_graph.start(&err))
        m_audioError = QString::fromStdString(err);
    m_sourcesDebounce.setSingleShot(true);
    m_sourcesDebounce.setInterval(300);
    connect(&m_sourcesDebounce, &QTimer::timeout, this, &RecordingController::sourcesChanged);
    if (m_graph.running()) {
        m_graphListener = m_graph.addListener([this](const audio::GraphEvent &e) {
            // PipeWire thread: hop to the UI thread.
            const bool disconnected = e.type == audio::GraphEvent::Disconnected;
            QMetaObject::invokeMethod(this, [this, disconnected] {
                if (disconnected) {
                    m_audioError = tr("The PipeWire audio service stopped.");
                    if (recording()) {
                        event("audio-service", tr("The audio service stopped; the recording was saved."));
                        Q_EMIT warning(tr("Audio service stopped"),
                                       tr("The system's audio service (PipeWire) stopped, so AfterLine cannot "
                                          "capture audio any more. The recording up to now was saved."));
                        stop();
                    }
                }
                m_sourcesDebounce.start();
            }, Qt::QueuedConnection);
        });
    }
    m_poll.setInterval(200);
    connect(&m_poll, &QTimer::timeout, this, &RecordingController::poll);
    checkAudioSetup();
    connect(this, &RecordingController::sourcesChanged, this, &RecordingController::checkAudioSetup);
}

void RecordingController::checkAudioSetup()
{
    if (!m_graph.running() || !m_graph.connected())
        return;
    bool devices = false;
    for (const auto &n : m_graph.nodes())
        devices |= !n.ours && (n.isSink() || n.isSource());
    const QString pulse = tr("Your system plays sound through PulseAudio rather than PipeWire, so AfterLine cannot "
                             "see your speakers or microphone. Ubuntu 22.10 and later, Linux Mint 22, Fedora, "
                             "Debian 12+ and Arch use PipeWire by default; on Ubuntu 22.04 and Linux Mint 21 you can "
                             "switch to it (see “Ubuntu 22.04 and Linux Mint 21” on the AfterLine website).");
    if (!devices && runningProcesses().contains("pulseaudio"))
        m_audioError = pulse;
    else if (!devices)
        m_audioError = tr("PipeWire is running but reports no speakers or microphones.");
    else if (m_audioError == pulse || m_audioError.startsWith(tr("PipeWire is running but")))
        m_audioError.clear();
}

bool RecordingController::audioAvailable() const
{
    return m_graph.running() && m_graph.connected() && m_audioError.isEmpty();
}

RecordingController::~RecordingController()
{
    if (recording())
        stop();
    stopMicrophonePreview();
    if (m_graphListener)
        m_graph.removeListener(m_graphListener);
    m_graph.stop();
}

QVector<SourceOption> RecordingController::computerSources() const
{
    QVector<SourceOption> out;
    SourceOption all;
    all.key = "default";
    all.label = tr("Entire computer");
    all.detail = tr("Everything you hear, from any application");
    all.icon = "computer";
    all.kind = CaptureStream::Kind::DefaultOutput;
    out.push_back(all);
    if (!m_graph.running())
        return out;

    QHash<QString, SourceOption> apps;
    QVector<SourceOption> outputs;
    for (const auto &n : m_graph.nodes()) {
        if (n.ours)
            continue;
        if (n.isAppOutput()) {
            const QString key = QString::fromStdString(audio::appKey(n));
            if (key.isEmpty())
                continue;
            SourceOption &o = apps["app:" + key];
            o.key = "app:" + key;
            o.label = QString::fromStdString(audio::appDisplayName(n));
            o.icon = QString::fromStdString(audio::appIconName(n));
            o.kind = CaptureStream::Kind::Application;
            o.target = key;
            o.playing = true;
            o.detail = tr("Playing audio now");
        } else if (n.isSink()) {
            SourceOption o;
            o.key = "sink:" + QString::fromStdString(n.name);
            o.label = QString::fromStdString(n.label());
            o.detail = tr("Everything played through this output");
            o.icon = "audio-speakers";
            o.kind = CaptureStream::Kind::OutputDevice;
            o.target = QString::fromStdString(n.name);
            outputs.push_back(o);
        }
    }
    const QSet<QString> procs = runningProcesses();
    for (const auto &k : kKnownApps) {
        const QString key = QString("app:") + k.key;
        if (apps.contains(key) || !procs.contains(k.process))
            continue;
        SourceOption o;
        o.key = key;
        o.label = k.name;
        o.icon = k.icon;
        o.kind = CaptureStream::Kind::Application;
        o.target = k.key;
        o.detail = tr("Open, not playing audio yet. Recording starts as soon as it does.");
        apps[key] = o;
    }
    QVector<SourceOption> appList = apps.values().toVector();
    std::sort(appList.begin(), appList.end(), [](const SourceOption &a, const SourceOption &b) {
        if (a.playing != b.playing)
            return a.playing;
        return a.label.localeAwareCompare(b.label) < 0;
    });
    out += appList;
    out += outputs;
    return out;
}

QVector<SourceOption> RecordingController::microphoneSources() const
{
    QVector<SourceOption> out;
    SourceOption def;
    def.key = "mic:default";
    def.label = tr("Default microphone");
    def.detail = tr("Follows the microphone selected in system settings");
    def.icon = "audio-input-microphone";
    def.kind = CaptureStream::Kind::DefaultInput;
    out.push_back(def);
    if (!m_graph.running())
        return out;
    for (const auto &n : m_graph.nodes()) {
        if (!n.isSource() || n.ours || n.mediaClass == "Audio/Sink")
            continue;
        SourceOption o;
        o.key = "mic:" + QString::fromStdString(n.name);
        o.label = QString::fromStdString(n.label());
        o.icon = "audio-input-microphone";
        o.kind = CaptureStream::Kind::InputDevice;
        o.target = QString::fromStdString(n.name);
        out.push_back(o);
    }
    return out;
}

bool RecordingController::start(const SourceOption &computer, const SourceOption *microphone, QString *error)
{
    if (recording())
        return false;
    if (!m_graph.running()) {
        if (error)
            *error = m_audioError.isEmpty() ? tr("The audio service is not available.") : m_audioError;
        return false;
    }
    if (QStorageInfo(m_store.root()).bytesAvailable() < kStopFreeBytes * 2) {
        if (error)
            *error = tr("Your disk is almost full. Free some space before recording.");
        return false;
    }
    stopMicrophonePreview();
    m_meeting = m_store.create(computer.label, microphone ? microphone->label : QString());
    m_lock = std::make_unique<QLockFile>(MeetingStore::lockFile(m_meeting));
    m_lock->setStaleLockTime(0);
    m_lock->tryLock(0);

    audio::RecorderConfig cfg;
    cfg.directory = m_meeting.dir.toStdString();
    cfg.title = m_meeting.title.toStdString();
    cfg.computer = audio::SourceChoice{computer.kind, computer.target.toStdString(), computer.label.toStdString()};
    if (microphone)
        cfg.microphone = audio::SourceChoice{microphone->kind, microphone->target.toStdString(),
                                             microphone->label.toStdString()};
    m_recorder = std::make_unique<audio::Recorder>(m_graph);
    std::string err;
    if (!m_recorder->start(cfg, &err)) {
        m_recorder.reset();
        m_lock.reset();
        QString msg = QString::fromStdString(err);
        if (msg.contains("not available"))
            msg = tr("“%1” is no longer available. Choose another source.").arg(
                microphone && computer.kind != CaptureStream::Kind::OutputDevice ? microphone->label : computer.label);
        m_store.remove(m_meeting.id, nullptr);
        m_meeting = {};
        if (error)
            *error = msg;
        return false;
    }
    m_lastComputer = {};
    m_lastMicrophone = {};
    m_lastBootDelta = bootMinusMonotonicMs();
    m_lowDiskWarned = false;
    m_micSilentWarned = false;
    event("started", tr("Recording started: %1%2")
                         .arg(computer.label, microphone ? tr(" and microphone (%1)").arg(microphone->label) : QString()));
    m_poll.start();
    Q_EMIT recordingStarted(m_meeting.id);
    return true;
}

bool RecordingController::paused() const
{
    return m_recorder && m_recorder->status().state == audio::RecorderStatus::Paused;
}

void RecordingController::pause()
{
    if (!recording() || paused())
        return;
    m_recorder->pause();
    event("paused", tr("Paused"));
    Q_EMIT pausedChanged(true);
}

void RecordingController::resume()
{
    if (!paused())
        return;
    m_recorder->resume();
    event("resumed", tr("Resumed"));
    Q_EMIT pausedChanged(false);
}

QString RecordingController::stop()
{
    if (!m_recorder)
        return {};
    m_poll.stop();
    const auto st = m_recorder->status();
    std::string err;
    const bool ok = m_recorder->stop(&err);
    m_recorder.reset();
    m_meeting = m_store.get(m_meeting.id).isValid() ? m_store.get(m_meeting.id) : m_meeting;
    m_meeting.duration = st.seconds;
    m_meeting.state = "saved";
    if (!ok)
        m_store.addEvent(m_meeting, st.seconds, "write-error",
                         tr("Part of the recording could not be written: %1").arg(QString::fromStdString(err)));
    m_store.addEvent(m_meeting, st.seconds, "stopped", tr("Recording saved"));
    m_store.save(m_meeting);
    m_lock.reset();
    const QString id = m_meeting.id;
    m_meeting = {};
    Q_EMIT warningCleared();
    Q_EMIT recordingStopped(id);
    return id;
}

audio::RecorderStatus RecordingController::status() const
{
    return m_recorder ? m_recorder->status() : audio::RecorderStatus{};
}

double RecordingController::elapsed() const { return status().seconds; }

void RecordingController::event(const QString &type, const QString &message)
{
    if (!m_meeting.isValid())
        return;
    // Keep the meeting's own record of what happened (shown in the Audio tab).
    MeetingInfo m = m_store.get(m_meeting.id);
    if (!m.isValid())
        return;
    m_store.addEvent(m, elapsed(), type, message);
    m_store.save(m);
    m_meeting.events = m.events;
}

void RecordingController::watchTrack(const audio::TrackStatus &now, audio::TrackStatus &before, bool microphone)
{
    using S = CaptureStream::State;
    const QString label = QString::fromStdString(now.label);
    const QString name = microphone ? tr("microphone") : label.isEmpty() ? tr("computer audio") : label;
    if (now.state == S::Lost && before.state != S::Lost) {
        if (microphone) {
            event("source-lost", tr("Microphone disconnected"));
            Q_EMIT warning(tr("Microphone disconnected"),
                           tr("AfterLine can no longer hear “%1”. The meeting is still being recorded; your "
                              "microphone will be picked up again as soon as it is reconnected.")
                               .arg(label));
        } else {
            event("source-lost", tr("Audio source disconnected: %1").arg(label));
            Q_EMIT warning(tr("Audio source disconnected"),
                           tr("AfterLine can no longer capture audio from %1. Reconnect it, or stop and start a "
                              "new recording from another source or the entire computer.")
                               .arg(label));
        }
    }
    if (before.state == S::Lost && now.state != S::Lost) {
        event("source-back", tr("%1 reconnected").arg(name));
        Q_EMIT warningCleared();
    }
    if (!microphone && now.reconnects > before.reconnects && now.linkedStreams > 0 && before.state != S::Lost)
        event("stream-restarted", tr("%1 restarted its audio; recording continued").arg(label));
    if (now.writeFailed && !before.writeFailed) {
        event("write-error", QString::fromStdString(now.writeError));
        Q_EMIT warning(tr("Recording could not be written"),
                       tr("Writing the %1 track failed (%2). The audio recorded so far is safe; the recording "
                          "has been stopped.")
                           .arg(name, QString::fromStdString(now.writeError)));
        QMetaObject::invokeMethod(this, [this] { stop(); }, Qt::QueuedConnection);
    }
    before = now;
}

void RecordingController::poll()
{
    if (!m_recorder)
        return;
    const auto st = m_recorder->status();
    watchTrack(st.computer, m_lastComputer, false);
    watchTrack(st.microphone, m_lastMicrophone, true);

    // A microphone that delivers nothing at all (as opposed to silence) for
    // several seconds is usually muted in hardware or blocked.
    if (st.microphone.enabled && st.state == audio::RecorderStatus::Recording && st.seconds > 6 &&
        st.microphone.state != CaptureStream::State::Lost) {
        const bool silent = st.microphone.secondsSinceData < 0 || st.microphone.secondsSinceData > 5;
        if (silent && !m_micSilentWarned) {
            m_micSilentWarned = true;
            Q_EMIT warning(tr("No sound from the microphone"),
                           tr("AfterLine is not receiving anything from “%1”. Check that it is not muted and "
                              "that it is the microphone you use for the call.")
                               .arg(QString::fromStdString(st.microphone.label)));
        } else if (!silent && m_micSilentWarned) {
            m_micSilentWarned = false;
            Q_EMIT warningCleared();
        }
    }

    // Disk space.
    const qint64 free = QStorageInfo(m_store.root()).bytesAvailable();
    if (free >= 0 && free < kStopFreeBytes) {
        event("disk-full", tr("Stopped: the disk is almost full"));
        Q_EMIT warning(tr("Disk almost full"),
                       tr("The recording was stopped and saved because your disk is almost full."));
        stop();
        return;
    }
    if (free >= 0 && free < kWarnFreeBytes && !m_lowDiskWarned) {
        m_lowDiskWarned = true;
        event("disk-low", tr("Disk space is running low"));
        Q_EMIT warning(tr("Disk space is running low"),
                       tr("Less than 1 GB is free. AfterLine will stop and save the recording before the disk "
                          "fills up."));
    }

    // Suspend: CLOCK_BOOTTIME keeps counting while suspended, MONOTONIC not.
    const qint64 delta = bootMinusMonotonicMs();
    if (delta - m_lastBootDelta > 2000)
        event("suspended", tr("The computer was suspended for %1; nothing was recorded during that time")
                               .arg(formatDuration((delta - m_lastBootDelta) / 1000.0)));
    m_lastBootDelta = delta;
    Q_EMIT statusChanged();
}

void RecordingController::startMicrophonePreview(const SourceOption &mic)
{
    stopMicrophonePreview();
    if (!m_graph.running() || recording())
        return;
    CaptureStream::Config c;
    c.kind = mic.kind;
    c.target = mic.target.toStdString();
    c.description = "AfterLine (microphone test)";
    m_preview = std::make_unique<CaptureStream>(m_graph, c, [this](const float *d, size_t n) {
        float peak = 0;
        for (size_t i = 0; i < n; ++i)
            peak = std::max(peak, std::fabs(d[i]));
        const float old = m_previewLevel.load();
        m_previewLevel = std::max(peak, old * 0.85f);
    });
    std::string err;
    if (!m_preview->start(&err))
        m_preview.reset();
}

void RecordingController::stopMicrophonePreview()
{
    if (m_preview) {
        m_preview->stop();
        m_preview.reset();
    }
    m_previewLevel = 0;
}

}  // namespace afterline
