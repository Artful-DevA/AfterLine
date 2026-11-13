// SPDX-License-Identifier: MIT
//
// "Report a problem" / "Suggest an idea": a GitHub issue filled in with what
// the person wrote and, if they agree, system details and the end of the
// logs. Nothing is sent by AfterLine: the browser opens the pre-filled issue
// and the person reviews it there before submitting.
#pragma once

#include <QString>
#include <QUrl>

namespace afterline {

struct AppContext;

namespace report {

enum class Kind { Problem, Idea };

// Version, install type, system, desktop, audio server, models and settings
// that matter for bug reports. One "Key: value" per line.
QString systemDetails(const AppContext &ctx);
// The end of the engine logs, with the home folder shown as "~".
QString recentLogs(int maxChars = 3000);
// The issue text as Markdown.
QString body(Kind kind, const QString &description, const QString &details, const QString &logs);
// A new-issue URL, trimmed (logs first) to fit what browsers and GitHub accept.
QUrl issueUrl(Kind kind, const QString &title, const QString &description, const QString &details, QString logs);

}  // namespace report
}  // namespace afterline
