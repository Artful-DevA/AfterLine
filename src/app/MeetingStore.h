// SPDX-License-Identifier: MIT
//
// The meeting library. Every meeting is a folder:
//
//   meetings/2026-03-12_0930-ab12cd/
//     meeting.json        title, date, sources, status, tags, events
//     computer.opus       computer/application audio (Ogg Opus, written live)
//     microphone.opus     your microphone, when included
//     *.peaks             waveform overview
//     transcript.json     full transcript (corrections saved in place;
//                         transcript.original.json keeps the recogniser's)
//     notes.json          generated notes; notes.md once you edit them
//
// Folders are self-describing and portable; the SQLite index (titles, search)
// is rebuilt from them whenever needed.
#pragma once

#include "app/Database.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace afterline {

struct MeetingInfo {
    QString id;
    QString dir;
    QString title;
    bool titleIsAuto = true;
    QDateTime created;
    double duration = 0;
    QString state;  // "recording", "interrupted" (recovered after a crash), "saved"
    QString sourceLabel;
    QString microphoneLabel;  // empty: microphone not recorded
    QString language;
    QStringList tags;
    QString folder;
    bool folderAuto = false;      // filed by AfterLine (can be undone)
    QString folderSuggestion;     // a new folder AfterLine suggests for it
    // Processing: "" (not started), "queued", "running", "done", "failed", "cancelled"
    QString transcriptState;
    QString notesState;
    QString lastError;
    QJsonArray events;  // [{ "t": seconds, "type": "...", "message": "..." }]

    bool isValid() const { return !id.isEmpty(); }
    QString path(const QString &file) const { return dir + "/" + file; }
    bool hasComputerAudio() const;
    bool hasMicrophoneAudio() const;
    bool hasAudio() const { return hasComputerAudio() || hasMicrophoneAudio(); }
    bool hasTranscript() const;
    // Transcribed, but no speech was found (a silent recording).
    bool transcriptIsEmpty() const;
    bool hasNotes() const;
    bool notesEdited() const;
    QJsonObject toJson() const;
    static MeetingInfo fromJson(const QJsonObject &o, const QString &dir);
};

// A folder in the library: a name, an icon and a colour the person chose.
struct FolderInfo {
    QString name;
    QString icon = "folder";
    QString color;  // "#RRGGBB", empty: the accent colour
};

struct SearchHit {
    QString meetingId;
    QString meetingTitle;
    QDateTime created;
    int segmentId = -1;   // -1: the hit is in the notes
    double start = -1;
    QString source;
    QString snippet;      // HTML, matches in <b>
    QString text;         // plain text of the segment
};

class MeetingStore {
public:
    bool open(const QString &root, const QString &databaseFile, QString *error);
    QString root() const { return m_root; }

    MeetingInfo create(const QString &sourceLabel, const QString &microphoneLabel);
    bool save(MeetingInfo &meeting, QString *error = nullptr);
    MeetingInfo get(const QString &id) const;
    QVector<MeetingInfo> list() const;
    // Moves the meeting folder to the desktop trash (recoverable); falls back
    // to deletion where there is no trash.
    bool remove(const QString &id, QString *error);

    void addEvent(MeetingInfo &meeting, double t, const QString &type, const QString &message);

    // Search index.
    void reindex(const QString &id);
    void rebuildIndex();
    QVector<SearchHit> search(const QString &query, int limit = 50, const QStringList &meetingIds = {}) const;
    // Transcript passages most relevant to a question, with neighbouring lines
    // for context (for Ask AfterLine).
    QVector<SearchHit> passagesFor(const QString &question, int limit, const QStringList &meetingIds = {}) const;
    QStringList allTags() const;
    QStringList allFolders() const;
    // Folders with their icon and colour (folders.json in the library, plus
    // any folder a meeting names), sorted by name.
    QVector<FolderInfo> folders() const;
    FolderInfo folder(const QString &name) const;
    // Creates or updates a folder; with a different oldName, renames it and
    // moves its meetings along.
    bool saveFolder(const FolderInfo &folder, const QString &oldName = {});
    // Deletes a folder; its meetings stay in the library, without a folder.
    void removeFolder(const QString &name);
    void setFolder(const QString &meetingId, const QString &folder, bool automatic = false);

    // Meetings left in "recording" state by a crash or power loss, whose
    // recorder is no longer running. Their audio (written continuously) is
    // kept, the duration measured, and they are marked "interrupted".
    QStringList recoverInterrupted();

    static QString lockFile(const MeetingInfo &m) { return m.dir + "/.recording.lock"; }

private:
    bool ensureSchema(QString *error);
    void indexMeeting(const MeetingInfo &m);
    void syncFromDisk();

    QString m_root;
    mutable Database m_db;
};

// FTS5 query from free text: every word must match (prefix match), quotes
// and operators in user input are neutralised.
QString ftsQuery(const QString &text, bool any = false);

}  // namespace afterline
