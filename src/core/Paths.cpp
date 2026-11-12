// SPDX-License-Identifier: MIT
#include "core/Paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include <QRegularExpression>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace afterline {

namespace {
QString ensure(const QString &path)
{
    QDir().mkpath(path);
    return path;
}
}  // namespace

QString userTag()
{
#ifdef _WIN32
    QString user = qEnvironmentVariable("USERNAME");
    user.remove(QRegularExpression("[^A-Za-z0-9_.-]"));
    return user.isEmpty() ? QString("user") : user;
#else
    return QString::number(::getuid());
#endif
}

QString dataDir()
{
    const QString env = qEnvironmentVariable("AFTERLINE_DATA_DIR");
    if (!env.isEmpty())
        return ensure(env);
    QString base = qEnvironmentVariable("XDG_DATA_HOME");
    if (base.isEmpty())
        base = QDir::homePath() + "/.local/share";
    return ensure(base + "/AfterLine");
}

QString defaultMeetingsDir() { return dataDir() + "/meetings"; }

QString meetingsDir()
{
    // Chosen in Settings → Storage; the library follows it.
    const QString chosen = QSettings("AfterLine", "AfterLine").value("storage/meetingsDir").toString();
    if (!chosen.isEmpty())
        return ensure(chosen);
    return ensure(defaultMeetingsDir());
}
QString modelsDir() { return ensure(dataDir() + "/models"); }
QString logsDir() { return ensure(dataDir() + "/logs"); }
QString databasePath() { return dataDir() + "/library.sqlite"; }

QString engineDir()
{
    const QString env = qEnvironmentVariable("AFTERLINE_ENGINE_DIR");
    if (!env.isEmpty())
        return env;
    const QString appDir = QCoreApplication::applicationDirPath();
    // Installed layout: <prefix>/bin/afterline + <prefix>/<libdir>/afterline/...
    const QString installed = QDir::cleanPath(appDir + "/" + AFTERLINE_BIN_TO_ENGINES);
    if (QFileInfo::exists(installed + "/transcriber"))
        return installed;
    // Build tree: <build>/afterline + <build>/engines/<name>-build/stage
    return appDir + "/engines";
}

QString enginePath(const QString &name)
{
    const QString dir = engineDir();
#ifdef _WIN32
    const QString exe = "afterline-" + name + ".exe";
#else
    const QString exe = "afterline-" + name;
#endif
    for (const QString &candidate : {dir + "/" + name + "/" + exe, dir + "/" + name + "-build/stage/" + exe})
        if (QFileInfo(candidate).isExecutable())
            return candidate;
    return dir + "/" + name + "/" + exe;
}

}  // namespace afterline
