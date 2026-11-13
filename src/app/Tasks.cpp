// SPDX-License-Identifier: MIT
#include "app/Tasks.h"

#include "app/MeetingStore.h"
#include "core/Notes.h"

#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QJsonArray>
#include <QRegularExpression>

#include <algorithm>

namespace afterline {

QVector<TaskItem> collectTasks(const MeetingStore &store, bool onlyMine, const QStringList &meetingIds)
{
    QVector<TaskItem> out;
    QVector<MeetingInfo> meetings = store.list();
    std::sort(meetings.begin(), meetings.end(),
              [](const MeetingInfo &a, const MeetingInfo &b) { return a.created > b.created; });
    for (const auto &m : meetings) {
        if (!meetingIds.isEmpty() && !meetingIds.contains(m.id))
            continue;
        if (!QFile::exists(m.path("notes.json")))
            continue;
        const MeetingNotes n = loadNotes(m.path("notes.json"));
        const QSet<QString> done = loadDoneTasks(m.path("tasks.json"));
        QVector<TaskItem> items;
        for (const auto v : n.data["action_items"].toArray()) {
            const QJsonObject o = v.toObject();
            TaskItem t;
            t.meetingId = m.id;
            t.meetingTitle = m.title;
            t.meetingDate = m.created;
            t.task = o["task"].toString();
            t.owner = o["owner"].toString();
            t.due = o["due"].toString();
            t.start = o["start"].toDouble(-1);
            t.done = done.contains(t.task);
            t.mine = t.owner.compare("You", Qt::CaseInsensitive) == 0;
            if (t.task.isEmpty() || (onlyMine && !t.mine))
                continue;
            items << t;
        }
        std::stable_sort(items.begin(), items.end(), [](const TaskItem &a, const TaskItem &b) { return !a.done && b.done; });
        out << items;
    }
    return out;
}

bool setTaskDone(const MeetingStore &store, const QString &meetingId, const QString &task, bool done)
{
    const MeetingInfo m = store.get(meetingId);
    if (!m.isValid())
        return false;
    QSet<QString> set = loadDoneTasks(m.path("tasks.json"));
    if (done)
        set.insert(task);
    else
        set.remove(task);
    return saveDoneTasks(m.path("tasks.json"), set);
}

bool deleteTask(MeetingStore &store, const QString &meetingId, const QString &task)
{
    const MeetingInfo m = store.get(meetingId);
    if (!m.isValid() || task.isEmpty())
        return false;
    bool removed = false;
    MeetingNotes n = loadNotes(m.path("notes.json"));
    if (!n.isEmpty()) {
        QJsonArray kept;
        for (const auto v : n.data["action_items"].toArray()) {
            if (!removed && v.toObject()["task"].toString() == task) {
                removed = true;
                continue;
            }
            kept << v;
        }
        if (removed) {
            n.data["action_items"] = kept;
            QSaveFile f(m.path("notes.json"));
            if (!f.open(QIODevice::WriteOnly))
                return false;
            f.write(QJsonDocument(n.data).toJson(QJsonDocument::Indented));
            if (!f.commit())
                return false;
        }
    }
    // Notes edited by hand: drop the list line that holds this task.
    QFile md(m.path("notes.md"));
    if (md.open(QIODevice::ReadOnly)) {
        QStringList lines = QString::fromUtf8(md.readAll()).split('\n');
        md.close();
        static const QRegularExpression item(R"(^\s*[-*]\s+(\[[ xX]\]\s+)?)");
        for (int i = 0; i < lines.size(); ++i) {
            if (item.match(lines[i]).hasMatch() && lines[i].contains(task)) {
                lines.removeAt(i);
                QSaveFile f(m.path("notes.md"));
                if (f.open(QIODevice::WriteOnly)) {
                    f.write(lines.join('\n').toUtf8());
                    removed = f.commit() || removed;
                }
                break;
            }
        }
    }
    QSet<QString> done = loadDoneTasks(m.path("tasks.json"));
    if (done.remove(task))
        saveDoneTasks(m.path("tasks.json"), done);
    if (removed)
        store.reindex(meetingId);
    return removed;
}

bool isTaskQuestion(const QString &question)
{
    static const QRegularExpression re(
        R"(\b(tasks?|to-?dos?|action items?|owe|follow[- ]?ups?|assigned|responsib\w*|deadlines?|need to do|have to do|should i do|supposed to do|my (jobs?|work|homework)|do i need to (do|send|prepare|finish)|what('s| is) on my plate)\b)",
        QRegularExpression::CaseInsensitiveOption);
    return re.match(question).hasMatch();
}

}  // namespace afterline
