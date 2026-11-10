// SPDX-License-Identifier: MIT
//
// How the engines talk to the app: one JSON object per line on stdout.
//
//   {"type":"progress","fraction":0.42,"stage":"Transcribing meeting audio"}
//   {"type":"done"}
//   {"type":"error","message":"..."}
//
// Diagnostics go to stderr. SIGTERM/SIGINT request a clean, prompt stop:
// partial results already written stay valid and processing can resume.
#pragma once

#include <QJsonObject>
#include <QString>

#include <atomic>

namespace engine {

extern std::atomic<bool> g_abort;

void installSignalHandlers();
// Lowers CPU and I/O priority so a call running at the same time keeps
// priority over transcription.
void lowerPriority();
void emitEvent(const QJsonObject &event);
void progress(double fraction, const QString &stage);
void fail(const QString &message);  // emits an error event and exits(1)
QString executableDir();
// Writes JSON atomically (temporary file + rename).
bool writeJsonFile(const QString &path, const QJsonObject &object, QString *error);
// Physical cores minus one, at least one: leaves room for the call itself.
int defaultThreads();

}  // namespace engine
