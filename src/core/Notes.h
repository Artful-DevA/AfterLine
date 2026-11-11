// SPDX-License-Identifier: MIT
//
// Meeting notes as produced by the notes engine (notes.json) and their
// Markdown form. Timestamps become links of the form "afterline:t=754.2" that
// the meeting view turns into "play from here".
#pragma once

#include <QJsonObject>
#include <QSet>
#include <QString>

namespace afterline {

struct MeetingNotes {
    QJsonObject data;  // notes.json
    bool isEmpty() const { return data.isEmpty(); }
    QString title() const { return data["title"].toString(); }
    QString summary() const { return data["summary"].toString(); }
    int actionItemCount() const;
};

MeetingNotes loadNotes(const QString &path);
// Markdown with clickable timestamps; `links` false gives plain timestamps
// (for export to other applications).
// `done`: action items (by their text) already ticked off.
QString notesToMarkdown(const MeetingNotes &notes, bool links = true, const QSet<QString> &done = {});
// Which action items have been ticked off, kept next to the notes in
// tasks.json so regenerating notes keeps them.
QSet<QString> loadDoneTasks(const QString &path);
bool saveDoneTasks(const QString &path, const QSet<QString> &done);
QString timeLink(double seconds);
// Parses "afterline:t=754.2"; returns -1 if it is not a time link.
double parseTimeLink(const QString &url);

}  // namespace afterline
