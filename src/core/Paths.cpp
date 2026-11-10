// SPDX-License-Identifier: MIT
#include "core/Paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace afterline {

namespace {
QString ensure(const QString &path)
{
    QDir().mkpath(path);
    return path;
}
}  // namespace

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

QString meetingsDir() { return ensure(dataDir() + "/meetings"); }
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
    for (const QString &candidate : {dir + "/" + name + "/afterline-" + name,
                                     dir + "/" + name + "-build/stage/afterline-" + name})
        if (QFileInfo(candidate).isExecutable())
            return candidate;
    return dir + "/" + name + "/afterline-" + name;
}

}  // namespace afterline
