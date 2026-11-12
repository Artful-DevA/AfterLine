// SPDX-License-Identifier: MIT
#include "Protocol.h"

#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>

#include <QCoreApplication>
#include <QThread>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace engine {

std::atomic<bool> g_abort{false};

namespace {
void onSignal(int) { g_abort = true; }
}  // namespace

void installSignalHandlers()
{
#ifdef _WIN32
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
#else
    struct sigaction sa {};
    sa.sa_handler = onSignal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    std::signal(SIGPIPE, SIG_IGN);
#endif
    // With --stdin-control (as AfterLine starts its engines), a line "stop"
    // on standard input, or standard input closing because AfterLine went
    // away, asks for the same clean stop as a signal. This works the same on
    // every system, Windows included.
    for (const QString &a : QCoreApplication::arguments())
        if (a == QLatin1String("--stdin-control")) {
            std::thread([] {
                std::string line;
                while (std::getline(std::cin, line))
                    if (line.rfind("stop", 0) == 0)
                        break;
                g_abort = true;
            }).detach();
            break;
        }
}

void lowerPriority()
{
#ifdef _WIN32
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
#else
    setpriority(PRIO_PROCESS, 0, 10);
    // ioprio_set(IOPRIO_WHO_PROCESS, 0, IOPRIO_CLASS_IDLE): reading the
    // recording never competes with writing one.
#ifdef SYS_ioprio_set
    syscall(SYS_ioprio_set, 1, 0, (3 << 13));
#endif
#endif
}

void emitEvent(const QJsonObject &event)
{
    const QByteArray line = QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(line.constData(), 1, size_t(line.size()), stdout);
    std::fflush(stdout);
}

void progress(double fraction, const QString &stage)
{
    emitEvent({{"type", "progress"}, {"fraction", qBound(0.0, fraction, 1.0)}, {"stage", stage}});
}

void fail(const QString &message)
{
    emitEvent({{"type", "error"}, {"message", message}});
    std::exit(1);
}

QString executableDir() { return QCoreApplication::applicationDirPath(); }

bool writeJsonFile(const QString &path, const QJsonObject &object, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

int defaultThreads()
{
    // Count distinct (physical id, core id) pairs; fall back to logical CPUs.
    QFile f("/proc/cpuinfo");
    QSet<QString> cores;
    if (f.open(QIODevice::ReadOnly)) {
        QString physical;
        for (const QByteArray &raw : f.readAll().split('\n')) {
            const QString line = QString::fromLatin1(raw);
            if (line.startsWith("physical id"))
                physical = line.section(':', 1).trimmed();
            else if (line.startsWith("core id"))
                cores.insert(physical + "/" + line.section(':', 1).trimmed());
        }
    }
    // Elsewhere (Windows): logical processors, halved for hyper-threading.
    int n = cores.isEmpty() ? std::max(1, QThread::idealThreadCount() / (QFile::exists("/proc") ? 1 : 2))
                            : int(cores.size());
    if (n <= 0)
        n = 4;
    return std::max(1, std::min(n - 1, 8));
}

}  // namespace engine
