// SPDX-License-Identifier: MIT
//
// Action items from every meeting's notes, in one list: what you (and
// others) said you would do, with the moment it was said.
#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

namespace afterline {

class MeetingStore;

struct TaskItem {
    QString meetingId, meetingTitle;
    QDateTime meetingDate;
    QString task, owner, due;
    double start = -1;
    bool done = false;
    bool mine = false;  // assigned to "You"
};

// Newest meetings first; open tasks before done ones within a meeting.
// Only meetings in `meetingIds` when it is not empty.
QVector<TaskItem> collectTasks(const MeetingStore &store, bool onlyMine, const QStringList &meetingIds = {});
bool setTaskDone(const MeetingStore &store, const QString &meetingId, const QString &task, bool done);
// Removes the action item from the meeting's notes (and from edited notes).
bool deleteTask(MeetingStore &store, const QString &meetingId, const QString &task);
// True for questions about tasks ("what do I need to do?", "my action items").
bool isTaskQuestion(const QString &question);

}  // namespace afterline
