// SPDX-License-Identifier: MIT
#include "app/ProcessControl.h"

#include <QProcess>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <csignal>
#endif

namespace afterline {

#ifdef Q_OS_WIN
namespace {
// NtSuspendProcess / NtResumeProcess: whole-process pause, as SIGSTOP does.
using NtProcessFn = LONG(NTAPI *)(HANDLE);
void ntCall(QProcess *p, const char *name)
{
    static HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto fn = ntdll ? reinterpret_cast<NtProcessFn>(GetProcAddress(ntdll, name)) : nullptr;
    if (!fn)
        return;
    if (HANDLE h = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, DWORD(p->processId()))) {
        fn(h);
        CloseHandle(h);
    }
}
}  // namespace
#endif

void requestEngineStop(QProcess *process)
{
    if (!process || process->state() == QProcess::NotRunning)
        return;
    resumeProcess(process);  // a paused engine cannot stop
    process->write("stop\n");
    process->closeWriteChannel();
#ifndef Q_OS_WIN
    process->terminate();  // SIGTERM: the same clean stop
#endif
}

void suspendProcess(QProcess *process)
{
    if (!process || process->processId() <= 0)
        return;
#ifdef Q_OS_WIN
    ntCall(process, "NtSuspendProcess");
#else
    ::kill(pid_t(process->processId()), SIGSTOP);
#endif
}

void resumeProcess(QProcess *process)
{
    if (!process || process->processId() <= 0)
        return;
#ifdef Q_OS_WIN
    ntCall(process, "NtResumeProcess");
#else
    ::kill(pid_t(process->processId()), SIGCONT);
#endif
}

}  // namespace afterline
