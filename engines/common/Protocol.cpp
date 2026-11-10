// SPDX-License-Identifier: MIT
#include "Protocol.h"

#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>

namespace engine {

std::atomic<bool> g_abort{false};

namespace {
void onSignal(int) { g_abort = true; }
}  // namespace

void installSignalHandlers()
{
    struct sigaction sa {};
    sa.sa_handler = onSignal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    std::signal(SIGPIPE, SIG_IGN);
}

void lowerPriority()
{
    setpriority(PRIO_PROCESS, 0, 10);
    // ioprio_set(IOPRIO_WHO_PROCESS, 0, IOPRIO_CLASS_IDLE): reading the
    // recording never competes with writing one.
#ifdef SYS_ioprio_set
    syscall(SYS_ioprio_set, 1, 0, (3 << 13));
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

QString executableDir()
{
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0)
        return QStringLiteral(".");
    buf[n] = 0;
    QString path = QString::fromLocal8Bit(buf);
    return path.left(path.lastIndexOf('/'));
}

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
    int n = cores.isEmpty() ? int(std::thread::hardware_concurrency()) : int(cores.size());
    if (n <= 0)
        n = 4;
    return std::max(1, std::min(n - 1, 8));
}

}  // namespace engine
