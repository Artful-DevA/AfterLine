// SPDX-License-Identifier: MIT
//
// A preliminary transcript while recording: runs the transcriber in live
// mode on the growing track files. The careful transcript is still made from
// the whole recording once it stops; this one is only shown while recording.
#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QVector>

namespace afterline {

class Settings;
class MeetingStore;

struct LiveLine {
    double start = 0, end = 0;
    QString source;  // "computer" or "microphone"
    QString text;
};

class LiveTranscriber : public QObject {
    Q_OBJECT
public:
    LiveTranscriber(Settings &settings, MeetingStore &store, QObject *parent = nullptr);
    ~LiveTranscriber() override;

    // Empty when live transcription can run; otherwise why not.
    QString unavailableReason() const;
    void start(const QString &meetingId);
    void stop();
    bool running() const { return m_proc != nullptr; }
    bool ready() const { return m_ready; }
    QString meetingId() const { return m_meetingId; }
    const QVector<LiveLine> &lines() const { return m_lines; }

Q_SIGNALS:
    void lineAdded(const afterline::LiveLine &line);
    void partial(const QString &source, const QString &text);
    void linesChanged();  // lines were removed (echo of the call)
    void stateChanged();

private:
    void onLine(const QByteArray &line);
    Settings &m_settings;
    MeetingStore &m_store;
    QPointer<QProcess> m_proc;
    QByteArray m_buffer;
    QString m_meetingId, m_error;
    QVector<LiveLine> m_lines;
    bool m_ready = false;
};

}  // namespace afterline
