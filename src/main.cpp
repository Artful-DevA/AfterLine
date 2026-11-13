// SPDX-License-Identifier: MIT
#include "app/Application.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QSocketNotifier>

#ifdef Q_OS_UNIX
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

namespace {
int g_signalFds[2] = {-1, -1};
void onTerminate(int)
{
    const char c = 1;
    [[maybe_unused]] auto r = ::write(g_signalFds[0], &c, 1);
}
}  // namespace
#endif

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("AfterLine");
    QApplication::setOrganizationName("AfterLine");
    QApplication::setApplicationVersion(AFTERLINE_VERSION);
    QApplication::setDesktopFileName("io.github.Artful_DevA.AfterLine");
    QApplication::setWindowIcon(QIcon(":/icons/afterline.svg"));
    // The tray keeps a recording running after the window closes.
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser cli;
    cli.setApplicationDescription("AfterLine: private, offline meeting recorder, transcripts and notes.");
    cli.addHelpOption();
    cli.addVersionOption();
    QCommandLineOption hidden("minimized", "Start in the system tray.");
    cli.addOption(hidden);
    cli.process(app);

#ifdef Q_OS_UNIX
    // Logging out or shutting down sends SIGTERM: quit normally, so a
    // recording in progress is stopped and saved like a manual stop.
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, g_signalFds) == 0) {
        auto *notifier = new QSocketNotifier(g_signalFds[1], QSocketNotifier::Read, &app);
        QObject::connect(notifier, &QSocketNotifier::activated, &app, [] {
            char c;
            [[maybe_unused]] auto r = ::read(g_signalFds[1], &c, 1);
            QApplication::quit();
        });
        struct sigaction sa {};
        sa.sa_handler = onTerminate;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        sigaction(SIGTERM, &sa, nullptr);
        sigaction(SIGINT, &sa, nullptr);
        sigaction(SIGHUP, &sa, nullptr);
    }
#endif

    afterline::Application afterline;
    return afterline.run(cli.isSet(hidden));
}
