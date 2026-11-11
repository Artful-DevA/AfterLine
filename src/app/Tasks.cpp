// SPDX-License-Identifier: MIT
#include "app/Tasks.h"

#include "app/MeetingStore.h"
#include "core/Notes.h"

#include <QFile>
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

bool isTaskQuestion(const QString &question)
{
    static const QRegularExpression re(
        R"(\b(tasks?|to-?dos?|action items?|owe|follow[- ]?ups?|assigned|responsib\w*|deadlines?|need to do|have to do|should i do|supposed to do|my (jobs?|work|homework)|do i need to (do|send|prepare|finish)|what('s| is) on my plate)\b)",
        QRegularExpression::CaseInsensitiveOption);
    return re.match(question).hasMatch();
}

}  // namespace afterline
