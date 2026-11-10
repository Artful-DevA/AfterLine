// SPDX-License-Identifier: MIT
//
// Runs the transcription and notes engines one job at a time, in the
// background, at low priority.
//
// While a recording is in progress, a running engine is paused (SIGSTOP) and
// queued jobs wait, so a call never competes with AI processing for the CPU.
// Engine failures never touch the recording; they are reported in plain words
// and the job can be retried. An interrupted transcription resumes where it
// stopped.
#pragma once

#include "app/MeetingStore.h"
#include "app/Settings.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QQueue>

namespace afterline {

class Processing : public QObject {
    Q_OBJECT
public:
    enum class Kind { Transcript, Notes, Ask };
    Q_ENUM(Kind)

    Processing(MeetingStore &store, Settings &settings, QObject *parent = nullptr);
    ~Processing() override;

    // Returns false with a reason (e.g. no model installed).
    bool transcribe(const QString &meetingId, QString *error = nullptr);
    bool writeNotes(const QString &meetingId, QString *error = nullptr);
    // Answers a question from transcript passages; the answer arrives via
    // answerReady(requestId, ...).
    bool ask(const QString &requestId, const QString &question, const QStringList &meetingIds,
             QString *error = nullptr);
    void cancel(const QString &meetingId);
    void cancelAsk(const QString &requestId);
    // The passages an Ask request was answered from (citations index into it).
    QJsonArray passagesOf(const QString &requestId) const;
    // Called when recording starts/stops.
    void setRecordingActive(bool active);
    // Re-queues work interrupted by a crash or by closing the app.
    void resumeInterrupted();

    bool busy() const { return m_proc != nullptr; }
    bool isQueuedOrRunning(const QString &meetingId) const;
    QString currentMeetingId() const { return m_current.meetingId; }
    Kind currentKind() const { return m_current.kind; }
    double currentProgress() const { return m_progress; }
    QString currentStage() const { return m_stage; }
    static bool engineAvailable(Kind kind);

Q_SIGNALS:
    void progress(const QString &meetingId, afterline::Processing::Kind kind, double fraction, const QString &stage);
    void started(const QString &meetingId, afterline::Processing::Kind kind);
    void finished(const QString &meetingId, afterline::Processing::Kind kind, bool ok, const QString &error);
    void answerReady(const QString &requestId, const QJsonObject &answer, const QString &error);
    void queueChanged();

private:
    struct Job {
        Kind kind = Kind::Transcript;
        QString meetingId;  // or the request id for Ask
        QStringList args;
        QString output;
        QString tempDir;
    };
    void enqueue(const Job &job);
    void startNext();
    void onLine(const QByteArray &line);
    void onExit(int code, QProcess::ExitStatus status);
    void setState(const QString &meetingId, Kind kind, const QString &state, const QString &error = {});
    int threads() const;

    MeetingStore &m_store;
    Settings &m_settings;
    QQueue<Job> m_queue;
    QHash<QString, QJsonArray> m_lastPassages;
    Job m_current;
    QPointer<QProcess> m_proc;
    QByteArray m_buffer;
    QString m_error;
    QString m_stage;
    double m_progress = 0;
    bool m_recording = false;
    bool m_paused = false;
    bool m_cancelled = false;
};

}  // namespace afterline

Q_DECLARE_METATYPE(afterline::Processing::Kind)
