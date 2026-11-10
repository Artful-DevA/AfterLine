// SPDX-License-Identifier: MIT
#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace afterline {

struct TranscriptSegment {
    int id = 0;
    double start = 0;
    double end = 0;
    QString source;  // "computer" | "microphone"
    QString text;
    double confidence = 1;
    bool edited = false;
};

class Transcript {
public:
    bool load(const QString &path, QString *error = nullptr);
    // Saves corrections. The first save keeps the recogniser's original
    // output next to it (transcript.original.json).
    bool save(const QString &path, QString *error = nullptr) const;
    static Transcript fromJson(const QJsonObject &o);
    QJsonObject toJson() const;

    bool isEmpty() const { return segments.isEmpty(); }
    int indexAt(double seconds) const;  // segment playing at that time, or -1
    const TranscriptSegment *byId(int id) const;
    bool setText(int id, const QString &text);

    QVector<TranscriptSegment> segments;
    QString language;
    QString model;
    QString created;
    QJsonObject extra;  // fields we do not interpret, preserved on save
};

// Neutral labels: separate tracks identify the recording side, not a person.
QString speakerName(const QString &source);
QString formatTime(double seconds, bool alwaysHours = false);  // 1:05, 01:02:05
QString formatDuration(double seconds);                          // "1 h 05 min", "4 min", "35 s"

}  // namespace afterline
