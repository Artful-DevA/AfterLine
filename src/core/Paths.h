// SPDX-License-Identifier: MIT
#pragma once

#include <QString>

namespace afterline {

// ~/.local/share/AfterLine (or $XDG_DATA_HOME/AfterLine). AFTERLINE_DATA_DIR
// overrides it (tests, portable setups).
// Identifies the current user in names of per-user sockets and folders.
QString userTag();
QString dataDir();
QString meetingsDir();         // where recordings are kept (can be changed in Settings)
QString defaultMeetingsDir();
QString modelsDir();
QString logsDir();
QString databasePath();
// Directory holding afterline-transcriber/ and afterline-summarizer/.
// AFTERLINE_ENGINE_DIR overrides it.
QString engineDir();
QString enginePath(const QString &name);  // "transcriber" or "summarizer"

}  // namespace afterline
