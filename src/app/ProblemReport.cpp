// SPDX-License-Identifier: MIT
#include "app/ProblemReport.h"

#include "app/Application.h"
#include "app/ModelCatalog.h"
#include "app/RecordingController.h"
#include "app/Settings.h"
#include "core/Paths.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QSysInfo>
#include <QThread>
#include <QUrlQuery>

namespace afterline::report {

namespace {
const char *kNewIssue = "https://github.com/Artful-DevA/AfterLine/issues/new";
constexpr int kMaxUrl = 7800;

QString redact(QString text)
{
    const QString home = QDir::homePath();
    if (home.size() > 1)
        text.replace(home, "~");
    return text;
}

QString tail(const QString &path, int maxChars)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const qint64 size = f.size();
    f.seek(std::max<qint64>(0, size - qint64(maxChars) * 2));
    QString text = QString::fromUtf8(f.readAll());
    if (text.size() > maxChars) {
        text = text.right(maxChars);
        const int nl = text.indexOf('\n');
        if (nl >= 0)
            text = text.mid(nl + 1);  // start on a whole line
    }
    return text.trimmed();
}

QString installType()
{
    if (!qEnvironmentVariableIsEmpty("FLATPAK_ID"))
        return "Flatpak";
    if (!qEnvironmentVariableIsEmpty("APPIMAGE"))
        return "AppImage";
    return "System package";
}

QString cpuName()
{
    QFile f("/proc/cpuinfo");
    if (f.open(QIODevice::ReadOnly))
        for (const QByteArray &line : f.readAll().split('\n'))
            if (line.startsWith("model name"))
                return QString::fromUtf8(line.mid(line.indexOf(':') + 1)).trimmed();
    return QSysInfo::currentCpuArchitecture();
}

QString memory()
{
    QFile f("/proc/meminfo");
    if (f.open(QIODevice::ReadOnly)) {
        const QRegularExpression re("MemTotal:\\s+(\\d+)");
        const auto m = re.match(QString::fromUtf8(f.readAll()));
        if (m.hasMatch())
            return QString("%1 GB").arg(m.captured(1).toDouble() / 1024 / 1024, 0, 'f', 1);
    }
    return "unknown";
}
}  // namespace

QString systemDetails(const AppContext &ctx)
{
    QStringList lines;
    lines << QString("AfterLine: %1 (%2)").arg(AFTERLINE_VERSION, installType());
    lines << QString("System: %1, kernel %2").arg(QSysInfo::prettyProductName(), QSysInfo::kernelVersion());
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP", "unknown");
    lines << QString("Desktop: %1 (%2)").arg(desktop, QGuiApplication::platformName());
    lines << QString("Qt: %1").arg(qVersion());
    if (ctx.recording) {
        const std::string pw = ctx.recording->graph().serverVersion();
        lines << QString("PipeWire: %1").arg(pw.empty() ? QString("not connected") : QString::fromStdString(pw));
    }
    lines << QString("CPU: %1 (%2 threads), RAM: %3").arg(cpuName()).arg(QThread::idealThreadCount()).arg(memory());
    if (ctx.settings) {
        const Settings &s = *ctx.settings;
        auto model = [](const QString &id) {
            const ModelSpec *m = findModel(id);
            return m ? QString("%1%2").arg(id, m->installed() ? "" : " (not downloaded)") : id;
        };
        lines << QString("Speech model: %1, notes model: %2").arg(model(s.speechModel()), model(s.notesModel()));
        lines << QString("Language: %1, quality: %2, threads: %3, live transcript: %4")
                     .arg(s.language(), s.quality(), s.threads() ? QString::number(s.threads()) : QString("auto"),
                          s.liveTranscript() ? "on" : "off");
    }
    return lines.join('\n');
}

QString recentLogs(int maxChars)
{
    QStringList parts;
    const QString engines = tail(logsDir() + "/engines.log", maxChars * 3 / 4);
    if (!engines.isEmpty())
        parts << "engines.log:\n" + engines;
    const QString live = tail(logsDir() + "/live-transcript.log", maxChars / 4);
    if (!live.isEmpty())
        parts << "live-transcript.log:\n" + live;
    return redact(parts.join("\n\n"));
}

QString body(Kind kind, const QString &description, const QString &details, const QString &logs)
{
    QString b;
    if (kind == Kind::Problem) {
        b += "### What happened\n\n" + (description.trimmed().isEmpty() ? QString("_Please describe the problem._")
                                                                           : description.trimmed());
        b += "\n\n### Steps to make it happen again (if you know them)\n\n1. \n";
    } else {
        b += "### The idea\n\n" + (description.trimmed().isEmpty() ? QString("_Please describe your idea._")
                                                                     : description.trimmed());
        b += "\n\n### Why it would help\n\n\n";
    }
    if (!details.isEmpty())
        b += "\n### System\n\n```\n" + details + "\n```\n";
    if (!logs.isEmpty())
        b += "\n<details><summary>Recent logs</summary>\n\n```\n" + logs + "\n```\n</details>\n";
    return b;
}

QUrl issueUrl(Kind kind, const QString &title, const QString &description, const QString &details, QString logs)
{
    auto build = [&](const QString &logText) {
        QUrl url(kNewIssue);
        QUrlQuery q;
        q.addQueryItem("title", title);
        q.addQueryItem("body", body(kind, description, details, logText));
        url.setQuery(q);
        return url;
    };
    QUrl url = build(logs);
    // Too long for a link: drop the oldest log lines until it fits.
    while (url.toEncoded().size() > kMaxUrl && !logs.isEmpty()) {
        const int cut = std::max(200, int(logs.size() / 4));
        logs = logs.size() > cut ? logs.mid(cut) : QString();
        const int nl = logs.indexOf('\n');
        if (nl >= 0)
            logs = logs.mid(nl + 1);
        url = build(logs.isEmpty() ? logs : "…\n" + logs);
    }
    return url;
}

}  // namespace afterline::report
