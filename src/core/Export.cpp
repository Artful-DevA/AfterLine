// SPDX-License-Identifier: MIT
#include "core/Export.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QLocale>

#include <cmath>

namespace afterline {

namespace {

QString tr(const char *s) { return QCoreApplication::translate("Export", s); }

QString subtitleTime(double seconds, char separator)
{
    const long long ms = std::llround(std::max(0.0, seconds) * 1000);
    return QString("%1:%2:%3%4%5")
        .arg(ms / 3600000, 2, 10, QChar('0'))
        .arg(ms / 60000 % 60, 2, 10, QChar('0'))
        .arg(ms / 1000 % 60, 2, 10, QChar('0'))
        .arg(QChar(separator))
        .arg(ms % 1000, 3, 10, QChar('0'));
}

}  // namespace

QString exportExtension(ExportFormat format)
{
    switch (format) {
    case ExportFormat::Markdown: return "md";
    case ExportFormat::PlainText: return "txt";
    case ExportFormat::Srt: return "srt";
    case ExportFormat::WebVtt: return "vtt";
    case ExportFormat::Json: return "json";
    }
    return "txt";
}

QString transcriptToText(const Transcript &t)
{
    QString out;
    for (const auto &s : t.segments)
        out += QString("[%1] %2: %3\n").arg(formatTime(s.start, true), speakerName(s.source), s.text);
    return out;
}

QString exportMeeting(const ExportInput &in, ExportFormat format)
{
    switch (format) {
    case ExportFormat::Markdown: {
        QString md = "# " + in.title + "\n\n";
        md += QLocale().toString(in.date, QLocale::LongFormat) + " · " + formatDuration(in.duration) + "\n\n";
        if (!in.notesMarkdown.isEmpty())
            md += in.notesMarkdown + "\n";
        if (!in.transcript.isEmpty()) {
            md += "## " + tr("Transcript") + "\n\n";
            for (const auto &s : in.transcript.segments)
                md += QString("**%1 · %2**  \n%3\n\n").arg(formatTime(s.start, true), speakerName(s.source), s.text);
        }
        return md;
    }
    case ExportFormat::PlainText: {
        QString txt = in.title + "\n" + QLocale().toString(in.date, QLocale::LongFormat) + "\n\n";
        if (!in.notesMarkdown.isEmpty())
            txt += in.notesMarkdown + "\n";
        return txt + transcriptToText(in.transcript);
    }
    case ExportFormat::Srt:
    case ExportFormat::WebVtt: {
        const bool vtt = format == ExportFormat::WebVtt;
        QString out = vtt ? "WEBVTT\n\n" : QString();
        int n = 1;
        for (const auto &s : in.transcript.segments) {
            const double end = std::max(s.end, s.start + 0.5);
            if (!vtt)
                out += QString::number(n++) + "\n";
            out += subtitleTime(s.start, vtt ? '.' : ',') + " --> " + subtitleTime(end, vtt ? '.' : ',') + "\n";
            out += (vtt ? "<v " + speakerName(s.source) + ">" : speakerName(s.source) + ": ") + s.text + "\n\n";
        }
        return out;
    }
    case ExportFormat::Json:
        return QString::fromUtf8(QJsonDocument(in.transcript.toJson()).toJson(QJsonDocument::Indented));
    }
    return {};
}

}  // namespace afterline
