// SPDX-License-Identifier: MIT
//
// Controlling the engine processes the same way on Linux and Windows.
#pragma once

class QProcess;

namespace afterline {

// Engines are started with --stdin-control: "stop" on their input (or their
// input closing) makes them stop cleanly, keeping partial results.
void requestEngineStop(QProcess *process);
// Pauses / resumes an engine completely (while a recording is in progress).
void suspendProcess(QProcess *process);
void resumeProcess(QProcess *process);

}  // namespace afterline
