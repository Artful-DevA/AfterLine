// SPDX-License-Identifier: MIT
#include "app/MeetingStore.h"

#include "audio/Recorder.h"
#include "core/Json.h"
#include "core/Notes.h"
#include "core/OggOpus.h"
#include "core/Transcript.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocale>
#include <QLockFile>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace afterline {

namespace {

constexpr int kSchemaVersion = 1;

QString tr(const char *s) { return QCoreApplication::translate("MeetingStore", s); }

qint64 folderStamp(const QString &dir)
{
    // Changes to any of these mean the index entry is stale.
    qint64 stamp = 0;
    for (const char *f : {"meeting.json", "transcript.json", "notes.json", "notes.md"}) {
        const QFileInfo fi(dir + "/" + f);
        if (fi.exists())
            stamp = std::max(stamp, fi.lastModified().toMSecsSinceEpoch());
    }
    return stamp;
}

QString htmlSnippet(QString s)
{
    s = s.toHtmlEscaped();
    s.replace(QChar(1), "<b>");
    s.replace(QChar(2), "</b>");
    return s;
}

const QSet<QString> &stopWords()
{
    static const QSet<QString> words = {
        // English
        "the", "and", "for", "are", "but", "not", "you", "all", "any", "can", "had", "her", "was", "one", "our",
        "out", "has", "have", "what", "when", "where", "which", "who", "why", "how", "did", "does", "about",
        "with", "that", "this", "from", "they", "will", "would", "there", "their", "were", "been", "into",
        "we", "us", "do", "is", "it", "in", "on", "of", "to", "a", "an", "decide", "said", "say", "tell",
        // Romanian
        "care", "este", "sunt", "pentru", "despre", "unde", "cand", "când", "cine", "cum", "ceea", "ce",
        "din", "la", "pe", "si", "și", "sau", "nu", "da", "am", "ati", "ați", "au", "fost", "acest", "aceasta",
    };
    return words;
}

}  // namespace

// ---------------------------------------------------------------------------

bool MeetingInfo::hasComputerAudio() const { return QFileInfo(path(audio::Recorder::kComputerFile)).size() > 100; }
bool MeetingInfo::hasMicrophoneAudio() const { return QFileInfo(path(audio::Recorder::kMicrophoneFile)).size() > 100; }
bool MeetingInfo::hasTranscript() const { return QFileInfo::exists(path("transcript.json")); }

bool MeetingInfo::transcriptIsEmpty() const
{
    Transcript t;
    return hasTranscript() && t.load(path("transcript.json")) && t.isEmpty();
}
bool MeetingInfo::hasNotes() const { return QFileInfo::exists(path("notes.json")) || notesEdited(); }
bool MeetingInfo::notesEdited() const { return QFileInfo::exists(path("notes.md")); }

QJsonObject MeetingInfo::toJson() const
{
    return {{"version", 1},
            {"id", id},
            {"title", title},
            {"titleIsAuto", titleIsAuto},
            {"created", created.toUTC().toString(Qt::ISODateWithMs)},
            {"duration", duration},
            {"state", state},
            {"source", sourceLabel},
            {"microphone", microphoneLabel},
            {"language", language},
            {"tags", QJsonArray::fromStringList(tags)},
            {"folder", folder},
            {"folderAuto", folderAuto},
            {"folderSuggestion", folderSuggestion},
            {"transcriptState", transcriptState},
            {"notesState", notesState},
            {"lastError", lastError},
            {"events", events}};
}

MeetingInfo MeetingInfo::fromJson(const QJsonObject &o, const QString &dir)
{
    MeetingInfo m;
    m.dir = dir;
    m.id = o["id"].toString(QFileInfo(dir).fileName());
    m.title = o["title"].toString();
    m.titleIsAuto = o["titleIsAuto"].toBool(true);
    m.created = QDateTime::fromString(o["created"].toString(), Qt::ISODateWithMs).toLocalTime();
    if (!m.created.isValid())
        m.created = QFileInfo(dir).birthTime().isValid() ? QFileInfo(dir).birthTime() : QFileInfo(dir).lastModified();
    m.duration = o["duration"].toDouble();
    m.state = o["state"].toString("saved");
    m.sourceLabel = o["source"].toString();
    m.microphoneLabel = o["microphone"].toString();
    m.language = o["language"].toString();
    for (const auto v : o["tags"].toArray())
        m.tags << v.toString();
    m.folder = o["folder"].toString();
    m.folderAuto = o["folderAuto"].toBool();
    m.folderSuggestion = o["folderSuggestion"].toString();
    m.transcriptState = o["transcriptState"].toString();
    m.notesState = o["notesState"].toString();
    m.lastError = o["lastError"].toString();
    m.events = o["events"].toArray();
    return m;
}

// ---------------------------------------------------------------------------

QString ftsQuery(const QString &text, bool any)
{
    QStringList terms;
    static const QRegularExpression wordRe(R"([\p{L}\p{N}]+)");
    auto it = wordRe.globalMatch(text);
    while (it.hasNext()) {
        const QString w = it.next().captured(0).toLower();
        if (any && (w.size() < 3 || stopWords().contains(w)))
            continue;
        terms << "\"" + w + "\"*";
    }
    terms.removeDuplicates();
    return terms.join(any ? " OR " : " ");
}

bool MeetingStore::open(const QString &root, const QString &databaseFile, QString *error)
{
    m_root = root;
    QDir().mkpath(root);
    if (!m_db.open(databaseFile, error) || !ensureSchema(error)) {
        // A damaged index is not a reason to lose access to meetings: start
        // a fresh one (the folders hold everything).
        m_db.close();
        QFile::remove(databaseFile);
        QFile::remove(databaseFile + "-wal");
        QFile::remove(databaseFile + "-shm");
        if (!m_db.open(databaseFile, error) || !ensureSchema(error))
            return false;
    }
    syncFromDisk();
    return true;
}

bool MeetingStore::ensureSchema(QString *error)
{
    int version = 0;
    {
        Database::Query q(m_db, "PRAGMA user_version");
        if (q.step())
            version = int(q.integer(0));
    }
    if (version == kSchemaVersion)
        return true;
    // The index is disposable: recreate it on any schema change.
    const char *schema = R"(
        DROP TABLE IF EXISTS meetings;
        DROP TABLE IF EXISTS segments;
        DROP TABLE IF EXISTS notes;
        CREATE TABLE meetings(id TEXT PRIMARY KEY, dir TEXT NOT NULL, created INTEGER, stamp INTEGER, json TEXT);
        CREATE VIRTUAL TABLE segments USING fts5(meeting_id UNINDEXED, seg_id UNINDEXED, start UNINDEXED,
            source UNINDEXED, text, tokenize = 'unicode61 remove_diacritics 2');
        CREATE VIRTUAL TABLE notes USING fts5(meeting_id UNINDEXED, text,
            tokenize = 'unicode61 remove_diacritics 2');
    )";
    if (!m_db.exec(schema, error))
        return false;
    return m_db.exec(QString("PRAGMA user_version = %1").arg(kSchemaVersion), error);
}

void MeetingStore::syncFromDisk()
{
    QHash<QString, qint64> indexed;
    {
        Database::Query q(m_db, "SELECT id, stamp FROM meetings");
        while (q.step())
            indexed[q.text(0)] = q.integer(1);
    }
    QSet<QString> present;
    const auto dirs = QDir(m_root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    m_db.exec("BEGIN");
    for (const QFileInfo &fi : dirs) {
        const QString dir = fi.absoluteFilePath();
        if (!QFileInfo::exists(dir + "/meeting.json"))
            continue;
        const MeetingInfo m = MeetingInfo::fromJson(readJsonFile(dir + "/meeting.json"), dir);
        present.insert(m.id);
        if (indexed.value(m.id, -1) != folderStamp(dir))
            indexMeeting(m);
    }
    for (auto it = indexed.begin(); it != indexed.end(); ++it) {
        if (present.contains(it.key()))
            continue;
        for (const char *sql : {"DELETE FROM meetings WHERE id = ?", "DELETE FROM segments WHERE meeting_id = ?",
                                "DELETE FROM notes WHERE meeting_id = ?"})
            Database::Query(m_db, sql).bind(1, it.key()).run();
    }
    m_db.exec("COMMIT");
}

void MeetingStore::indexMeeting(const MeetingInfo &m)
{
    Database::Query(m_db, "INSERT OR REPLACE INTO meetings(id, dir, created, stamp, json) VALUES(?,?,?,?,?)")
        .bind(1, m.id)
        .bind(2, m.dir)
        .bind(3, m.created.toMSecsSinceEpoch())
        .bind(4, folderStamp(m.dir))
        .bind(5, QString::fromUtf8(QJsonDocument(m.toJson()).toJson(QJsonDocument::Compact)))
        .run();
    Database::Query(m_db, "DELETE FROM segments WHERE meeting_id = ?").bind(1, m.id).run();
    Database::Query(m_db, "DELETE FROM notes WHERE meeting_id = ?").bind(1, m.id).run();
    Transcript t;
    if (t.load(m.path("transcript.json"))) {
        for (const auto &s : t.segments) {
            Database::Query q(m_db, "INSERT INTO segments(meeting_id, seg_id, start, source, text) VALUES(?,?,?,?,?)");
            q.bind(1, m.id).bind(2, s.id).bind(3, s.start).bind(4, s.source).bind(5, s.text).run();
        }
    }
    QString notesText;
    QFile md(m.path("notes.md"));
    if (md.open(QIODevice::ReadOnly))
        notesText = QString::fromUtf8(md.readAll());
    else if (QFileInfo::exists(m.path("notes.json")))
        notesText = notesToMarkdown(loadNotes(m.path("notes.json")), false);
    if (!notesText.isEmpty())
        Database::Query(m_db, "INSERT INTO notes(meeting_id, text) VALUES(?,?)").bind(1, m.id).bind(2, notesText).run();
}

void MeetingStore::reindex(const QString &id)
{
    const MeetingInfo m = get(id);
    if (!m.isValid())
        return;
    m_db.exec("BEGIN");
    indexMeeting(m);
    m_db.exec("COMMIT");
}

void MeetingStore::rebuildIndex()
{
    m_db.exec("DELETE FROM meetings; DELETE FROM segments; DELETE FROM notes;");
    syncFromDisk();
}

MeetingInfo MeetingStore::create(const QString &sourceLabel, const QString &microphoneLabel)
{
    MeetingInfo m;
    m.created = QDateTime::currentDateTime();
    m.id = m.created.toString("yyyy-MM-dd_HHmmss") + "-" +
           QString::number(QRandomGenerator::global()->generate() & 0xffffff, 16).rightJustified(6, '0');
    m.dir = m_root + "/" + m.id;
    QDir().mkpath(m.dir);
    m.title = tr("Meeting, %1").arg(QLocale().toString(m.created, "d MMMM yyyy, HH:mm"));
    m.titleIsAuto = true;
    m.state = "recording";
    m.sourceLabel = sourceLabel;
    m.microphoneLabel = microphoneLabel;
    save(m);
    return m;
}

bool MeetingStore::save(MeetingInfo &meeting, QString *error)
{
    if (!writeJsonFile(meeting.path("meeting.json"), meeting.toJson(), error))
        return false;
    m_db.exec("BEGIN");
    indexMeeting(meeting);
    m_db.exec("COMMIT");
    return true;
}

MeetingInfo MeetingStore::get(const QString &id) const
{
    if (id.isEmpty() || id.contains('/') || id.startsWith('.'))
        return {};
    const QString dir = m_root + "/" + id;
    const QJsonObject o = readJsonFile(dir + "/meeting.json");
    if (o.isEmpty())
        return {};
    return MeetingInfo::fromJson(o, dir);
}

QVector<MeetingInfo> MeetingStore::list() const
{
    QVector<MeetingInfo> out;
    Database::Query q(m_db, "SELECT dir, json FROM meetings ORDER BY created DESC");
    while (q.step())
        out.push_back(MeetingInfo::fromJson(QJsonDocument::fromJson(q.text(1).toUtf8()).object(), q.text(0)));
    return out;
}

bool MeetingStore::remove(const QString &id, QString *error)
{
    const MeetingInfo m = get(id);
    if (!m.isValid()) {
        if (error)
            *error = tr("This meeting no longer exists.");
        return false;
    }
    if (!QFile::moveToTrash(m.dir) && !QDir(m.dir).removeRecursively()) {
        if (error)
            *error = tr("The meeting folder could not be removed: %1").arg(m.dir);
        return false;
    }
    for (const char *sql : {"DELETE FROM meetings WHERE id = ?", "DELETE FROM segments WHERE meeting_id = ?",
                            "DELETE FROM notes WHERE meeting_id = ?"})
        Database::Query(m_db, sql).bind(1, id).run();
    return true;
}

void MeetingStore::addEvent(MeetingInfo &meeting, double t, const QString &type, const QString &message)
{
    meeting.events.append(QJsonObject{{"t", std::round(t * 10) / 10}, {"type", type}, {"message", message},
                                      {"at", QDateTime::currentDateTime().toString(Qt::ISODate)}});
}

static QString inClause(const QStringList &ids, QVariantList &binds)
{
    if (ids.isEmpty())
        return {};
    QStringList marks;
    for (const auto &id : ids) {
        marks << "?";
        binds << id;
    }
    return " AND meeting_id IN (" + marks.join(',') + ")";
}

QVector<SearchHit> MeetingStore::search(const QString &query, int limit, const QStringList &meetingIds) const
{
    QVector<SearchHit> hits;
    const QString fts = ftsQuery(query);
    if (fts.isEmpty())
        return hits;
    QHash<QString, MeetingInfo> meetings;
    for (const auto &m : list())
        meetings[m.id] = m;

    QVariantList binds{fts};
    const QString where = inClause(meetingIds, binds);
    Database::Query q(m_db, "SELECT meeting_id, seg_id, start, source, snippet(segments, 4, char(1), char(2), '…', 14), "
                            "text FROM segments WHERE segments MATCH ?" + where +
                            " ORDER BY bm25(segments) LIMIT " + QString::number(limit));
    for (int i = 0; i < binds.size(); ++i)
        q.bind(i + 1, binds[i]);
    while (q.step()) {
        SearchHit h;
        h.meetingId = q.text(0);
        h.segmentId = int(q.integer(1));
        h.start = q.real(2);
        h.source = q.text(3);
        h.snippet = htmlSnippet(q.text(4));
        h.text = q.text(5);
        h.meetingTitle = meetings.value(h.meetingId).title;
        h.created = meetings.value(h.meetingId).created;
        hits.push_back(h);
    }
    Database::Query n(m_db, "SELECT meeting_id, snippet(notes, 1, char(1), char(2), '…', 14) FROM notes "
                            "WHERE notes MATCH ?" + where + " ORDER BY bm25(notes) LIMIT 20");
    for (int i = 0; i < binds.size(); ++i)
        n.bind(i + 1, binds[i]);
    while (n.step()) {
        SearchHit h;
        h.meetingId = n.text(0);
        h.snippet = htmlSnippet(n.text(1));
        h.meetingTitle = meetings.value(h.meetingId).title;
        h.created = meetings.value(h.meetingId).created;
        hits.push_back(h);
    }
    // Title matches.
    const QStringList words = query.toLower().split(' ', Qt::SkipEmptyParts);
    for (const auto &m : meetings) {
        if (!meetingIds.isEmpty() && !meetingIds.contains(m.id))
            continue;
        bool all = !words.isEmpty();
        for (const auto &w : words)
            all &= m.title.toLower().contains(w) || m.tags.join(' ').toLower().contains(w);
        if (all) {
            SearchHit h;
            h.meetingId = m.id;
            h.meetingTitle = m.title;
            h.created = m.created;
            h.snippet = m.title.toHtmlEscaped();
            hits.prepend(h);
        }
    }
    return hits;
}

QVector<SearchHit> MeetingStore::passagesFor(const QString &question, int limit, const QStringList &meetingIds) const
{
    QVector<SearchHit> out;
    const QString fts = ftsQuery(question, true);
    if (fts.isEmpty())
        return out;
    QVariantList binds{fts};
    const QString where = inClause(meetingIds, binds);
    Database::Query q(m_db, "SELECT meeting_id, seg_id FROM segments WHERE segments MATCH ?" + where +
                            " ORDER BY bm25(segments) LIMIT " + QString::number(limit));
    for (int i = 0; i < binds.size(); ++i)
        q.bind(i + 1, binds[i]);
    QVector<QPair<QString, int>> found;
    while (q.step())
        found.push_back({q.text(0), int(q.integer(1))});

    QHash<QString, Transcript> transcripts;
    QHash<QString, MeetingInfo> meetings;
    QSet<QString> used;
    for (const auto &f : found) {
        if (!transcripts.contains(f.first)) {
            const MeetingInfo m = get(f.first);
            meetings[f.first] = m;
            transcripts[f.first].load(m.path("transcript.json"));
        }
        const Transcript &t = transcripts[f.first];
        int index = -1;
        for (int i = 0; i < t.segments.size(); ++i)
            if (t.segments[i].id == f.second)
                index = i;
        if (index < 0)
            continue;
        // The line plus its neighbours, so the model sees the exchange.
        const int from = std::max(0, index - 1), to = std::min(int(t.segments.size()) - 1, index + 1);
        const QString key = f.first + "/" + QString::number(from);
        if (used.contains(key))
            continue;
        used.insert(key);
        SearchHit h;
        h.meetingId = f.first;
        h.meetingTitle = meetings[f.first].title;
        h.created = meetings[f.first].created;
        h.segmentId = t.segments[index].id;
        h.start = t.segments[from].start;
        QStringList parts;
        for (int i = from; i <= to; ++i)
            parts << speakerName(t.segments[i].source) + ": " + t.segments[i].text;
        h.text = parts.join(' ');
        out.push_back(h);
    }
    return out;
}

QStringList MeetingStore::allTags() const
{
    QStringList tags;
    for (const auto &m : list())
        tags += m.tags;
    tags.removeDuplicates();
    tags.sort(Qt::CaseInsensitive);
    return tags;
}

QVector<FolderInfo> MeetingStore::folders() const
{
    QVector<FolderInfo> out;
    QSet<QString> seen;
    for (const auto v : readJsonFile(m_root + "/folders.json")["folders"].toArray()) {
        const QJsonObject o = v.toObject();
        FolderInfo f{o["name"].toString(), o["icon"].toString("folder"), o["color"].toString()};
        if (!f.name.isEmpty() && !seen.contains(f.name.toLower())) {
            seen.insert(f.name.toLower());
            out << f;
        }
    }
    for (const auto &m : list())
        if (!m.folder.isEmpty() && !seen.contains(m.folder.toLower())) {
            seen.insert(m.folder.toLower());
            out << FolderInfo{m.folder, "folder", {}};
        }
    std::sort(out.begin(), out.end(), [](const FolderInfo &a, const FolderInfo &b) {
        return QString::localeAwareCompare(a.name.toLower(), b.name.toLower()) < 0;
    });
    return out;
}

FolderInfo MeetingStore::folder(const QString &name) const
{
    for (const auto &f : folders())
        if (f.name.compare(name, Qt::CaseInsensitive) == 0)
            return f;
    return FolderInfo{name, "folder", {}};
}

namespace {
void writeFolders(const QString &root, const QVector<FolderInfo> &folders)
{
    QJsonArray arr;
    for (const auto &f : folders)
        arr.append(QJsonObject{{"name", f.name}, {"icon", f.icon}, {"color", f.color}});
    writeJsonFile(root + "/folders.json", QJsonObject{{"version", 1}, {"folders", arr}});
}
}  // namespace

bool MeetingStore::saveFolder(const FolderInfo &folder, const QString &oldName)
{
    const QString name = folder.name.trimmed();
    if (name.isEmpty())
        return false;
    QVector<FolderInfo> all = folders();
    const QString key = (oldName.isEmpty() ? name : oldName).toLower();
    bool found = false;
    for (auto &f : all)
        if (f.name.toLower() == key) {
            f = folder;
            f.name = name;
            found = true;
        }
    if (!found)
        all << FolderInfo{name, folder.icon, folder.color};
    writeFolders(m_root, all);
    if (!oldName.isEmpty() && oldName != name)
        for (auto m : list())
            if (m.folder.compare(oldName, Qt::CaseInsensitive) == 0) {
                m.folder = name;
                save(m);
            }
    return true;
}

void MeetingStore::removeFolder(const QString &name)
{
    QVector<FolderInfo> all = folders();
    all.erase(std::remove_if(all.begin(), all.end(),
                             [&](const FolderInfo &f) { return f.name.compare(name, Qt::CaseInsensitive) == 0; }),
              all.end());
    writeFolders(m_root, all);
    for (auto m : list())
        if (m.folder.compare(name, Qt::CaseInsensitive) == 0) {
            m.folder.clear();
            m.folderAuto = false;
            save(m);
        }
}

void MeetingStore::setFolder(const QString &meetingId, const QString &folder, bool automatic)
{
    MeetingInfo m = get(meetingId);
    if (!m.isValid())
        return;
    m.folder = folder;
    m.folderAuto = automatic && !folder.isEmpty();
    if (!folder.isEmpty())
        m.folderSuggestion.clear();
    save(m);
    if (!folder.isEmpty() && !folders().isEmpty()) {
        // Make sure a folder created by moving a meeting is remembered.
        bool known = false;
        for (const auto v : readJsonFile(m_root + "/folders.json")["folders"].toArray())
            known |= v.toObject()["name"].toString().compare(folder, Qt::CaseInsensitive) == 0;
        if (!known)
            saveFolder(FolderInfo{folder, "folder", {}});
    }
}

QStringList MeetingStore::allFolders() const
{
    QStringList folders;
    for (const auto &m : list())
        if (!m.folder.isEmpty())
            folders << m.folder;
    folders.removeDuplicates();
    folders.sort(Qt::CaseInsensitive);
    return folders;
}

QStringList MeetingStore::recoverInterrupted()
{
    QStringList recovered;
    for (MeetingInfo m : list()) {
        if (m.state != "recording")
            continue;
        // A live recorder (another AfterLine window) holds this lock.
        QLockFile lock(lockFile(m));
        lock.setStaleLockTime(0);
        if (!lock.tryLock(0))
            continue;
        lock.unlock();
        double seconds = 0;
        for (const char *f : {audio::Recorder::kComputerFile, audio::Recorder::kMicrophoneFile}) {
            const OggOpusInfo info = probeOggOpus(m.path(f).toStdString());
            if (info.valid)
                seconds = std::max(seconds, info.seconds());
        }
        m.state = "interrupted";
        m.duration = seconds;
        addEvent(m, seconds, "recovered",
                 tr("AfterLine stopped unexpectedly during this recording. The audio recorded up to %1 was "
                    "recovered.").arg(formatTime(seconds)));
        save(m);
        recovered << m.id;
    }
    return recovered;
}

}  // namespace afterline
