// SPDX-License-Identifier: MIT
#pragma once

#include "core/Transcript.h"

#include <QDateTime>
#include <QString>

namespace afterline {

struct ExportInput {
    QString title;
    QDateTime date;
    double duration = 0;
    QString notesMarkdown;  // may be empty
    Transcript transcript;
};

enum class ExportFormat { Markdown, PlainText, Srt, WebVtt, Json };

QString exportMeeting(const ExportInput &in, ExportFormat format);
QString exportExtension(ExportFormat format);
QString transcriptToText(const Transcript &t);

}  // namespace afterline
