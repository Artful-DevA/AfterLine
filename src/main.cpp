// SPDX-License-Identifier: MIT
#include "app/Application.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("AfterLine");
    QApplication::setOrganizationName("AfterLine");
    QApplication::setApplicationVersion(AFTERLINE_VERSION);
    QApplication::setDesktopFileName("io.github.AfterLine");
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

    afterline::Application afterline;
    return afterline.run(cli.isSet(hidden));
}
