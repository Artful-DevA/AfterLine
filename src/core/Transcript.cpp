// SPDX-License-Identifier: MIT
#include "core/Transcript.h"

#include "core/Json.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>

#include <cmath>

namespace afterline {

QString speakerName(const QString &source)
{
    return source == "microphone" ? QCoreApplication::translate("Transcript", "You")
                                  : QCoreApplication::translate("Transcript", "Others");
}

QString formatTime(double seconds, bool alwaysHours)
{
    const int s = std::max(0, int(seconds));
    const int h = s / 3600, m = s / 60 % 60, sec = s % 60;
    if (h > 0 || alwaysHours)
        return QString("%1:%2:%3").arg(h, 2, 10, QChar('0')).arg(m, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0'));
    return QString("%1:%2").arg(m).arg(sec, 2, 10, QChar('0'));
}

QString formatDuration(double seconds)
{
    const int s = int(std::lround(seconds));
    if (s < 60)
        return QCoreApplication::translate("Transcript", "%n s", nullptr, s);
    const int m = s / 60;
    if (m < 60)
        return QCoreApplication::translate("Transcript", "%n min", nullptr, m);
    return QCoreApplication::translate("Transcript", "%1 h %2 min").arg(m / 60).arg(m % 60, 2, 10, QChar('0'));
}

Transcript Transcript::fromJson(const QJsonObject &o)
{
    Transcript t;
    t.language = o["language"].toString();
    t.model = o["model"].toString();
    t.created = o["created"].toString();
    t.extra = o;
    t.extra.remove("segments");
    int nextId = 0;
    for (const auto v : o["segments"].toArray()) {
        const QJsonObject s = v.toObject();
        TranscriptSegment seg;
        seg.id = s.contains("id") ? s["id"].toInt() : nextId;
        nextId = std::max(nextId, seg.id) + 1;
        seg.start = s["start"].toDouble();
        seg.end = s["end"].toDouble(seg.start);
        seg.source = s["source"].toString("computer");
        seg.text = s["text"].toString();
        seg.confidence = s["confidence"].toDouble(1);
        seg.edited = s["edited"].toBool();
        t.segments.push_back(seg);
    }
    return t;
}

QJsonObject Transcript::toJson() const
{
    QJsonObject o = extra;
    QJsonArray arr;
    for (const auto &s : segments) {
        QJsonObject j{{"id", s.id}, {"start", s.start}, {"end", s.end}, {"source", s.source},
                      {"text", s.text}, {"confidence", s.confidence}};
        if (s.edited)
            j["edited"] = true;
        arr.append(j);
    }
    o["segments"] = arr;
    o["language"] = language;
    o["model"] = model;
    o["created"] = created;
    return o;
}

bool Transcript::load(const QString &path, QString *error)
{
    const QJsonObject o = readJsonFile(path, error);
    if (o.isEmpty())
        return false;
    *this = fromJson(o);
    return true;
}

bool Transcript::save(const QString &path, QString *error) const
{
    const QString original = QFileInfo(path).path() + "/transcript.original.json";
    if (QFile::exists(path) && !QFile::exists(original))
        QFile::copy(path, original);
    return writeJsonFile(path, toJson(), error);
}

int Transcript::indexAt(double seconds) const
{
    int best = -1;
    for (int i = 0; i < segments.size(); ++i) {
        if (segments[i].start <= seconds + 0.05)
            best = i;
        else
            break;
    }
    return best;
}

const TranscriptSegment *Transcript::byId(int id) const
{
    for (const auto &s : segments)
        if (s.id == id)
            return &s;
    return nullptr;
}

bool Transcript::setText(int id, const QString &text)
{
    for (auto &s : segments)
        if (s.id == id) {
            if (s.text == text)
                return false;
            s.text = text;
            s.edited = true;
            return true;
        }
    return false;
}

}  // namespace afterline
