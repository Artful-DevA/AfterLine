// SPDX-License-Identifier: MIT
//
// Everything about recording that the window needs: the list of things that
// can be recorded, starting/pausing/stopping, live levels and states, and
// plain-language warnings when something goes wrong mid-meeting (a source
// disappears, the disk fills up, the computer was suspended). A recording is
// always saved; problems are reported and logged in the meeting.
#pragma once

#include "app/MeetingStore.h"
#include "audio/PipeWireGraph.h"
#include "audio/Recorder.h"

#include <QElapsedTimer>
#include <QLockFile>
#include <QObject>
#include <QTimer>

#include <memory>

namespace afterline {

struct SourceOption {
    QString key;     // stable across sessions: "default", "app:bin:firefox", "sink:<node.name>", "mic:<node.name>"
    QString label;
    QString detail;  // "Playing audio", "Open, not playing audio yet", device description
    QString icon;    // icon theme name
    audio::CaptureStream::Kind kind = audio::CaptureStream::Kind::DefaultOutput;
    QString target;
    bool playing = false;
};

class RecordingController : public QObject {
    Q_OBJECT
public:
    RecordingController(MeetingStore &store, QObject *parent = nullptr);
    ~RecordingController() override;

    bool audioAvailable() const { return m_graph.running() && m_graph.connected(); }
    QString audioError() const { return m_audioError; }
    audio::PipeWireGraph &graph() { return m_graph; }

    QVector<SourceOption> computerSources() const;
    QVector<SourceOption> microphoneSources() const;

    bool start(const SourceOption &computer, const SourceOption *microphone, QString *error);
    void pause();
    void resume();
    // Stops and saves. Returns the meeting id.
    QString stop();
    bool recording() const { return m_recorder && m_recorder->active(); }
    bool paused() const;
    QString meetingId() const { return m_meeting.id; }
    audio::RecorderStatus status() const;
    double elapsed() const;

    // Live microphone level on the record page, before recording.
    void startMicrophonePreview(const SourceOption &mic);
    void stopMicrophonePreview();
    float microphonePreviewLevel() const { return m_previewLevel.load(); }

Q_SIGNALS:
    void sourcesChanged();
    void recordingStarted(const QString &meetingId);
    void recordingStopped(const QString &meetingId);
    void pausedChanged(bool paused);
    void statusChanged();
    // A problem the person should know about now.
    void warning(const QString &title, const QString &message);
    void warningCleared();

private:
    void poll();
    void event(const QString &type, const QString &message);
    void watchTrack(const audio::TrackStatus &now, audio::TrackStatus &before, bool microphone);

    MeetingStore &m_store;
    audio::PipeWireGraph m_graph;
    QString m_audioError;
    std::unique_ptr<audio::Recorder> m_recorder;
    MeetingInfo m_meeting;
    std::unique_ptr<QLockFile> m_lock;
    QTimer m_poll;
    QTimer m_sourcesDebounce;
    int m_graphListener = 0;
    audio::TrackStatus m_lastComputer, m_lastMicrophone;
    qint64 m_lastBootDelta = 0;
    bool m_lowDiskWarned = false;
    bool m_micSilentWarned = false;
    std::unique_ptr<audio::CaptureStream> m_preview;
    std::atomic<float> m_previewLevel{0.f};
};

}  // namespace afterline
