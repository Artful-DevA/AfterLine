// SPDX-License-Identifier: MIT
#include "ui/MainWindow.h"

#include "app/MeetingStore.h"
#include "app/ModelManager.h"
#include "app/Processing.h"
#include "app/RecordingController.h"
#include "app/Settings.h"
#include "core/Transcript.h"
#include "ui/Dialogs.h"
#include "ui/Icons.h"
#include "ui/LibraryView.h"
#include "ui/MeetingView.h"
#include "ui/RecordView.h"
#include "ui/SettingsView.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QApplication>
#include <QCloseEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QVBoxLayout>

namespace afterline::ui {

MainWindow::MainWindow(AppContext ctx, QWidget *parent) : QMainWindow(parent), m_ctx(ctx)
{
    setWindowTitle("AfterLine");
    setWindowIcon(QIcon(":/icons/afterline.svg"));
    setMinimumSize(900, 600);
    resize(1200, 780);
    if (!ctx.settings->windowGeometry().isEmpty())
        restoreGeometry(ctx.settings->windowGeometry());

    auto *split = new QSplitter;
    split->setHandleWidth(1);
    split->setChildrenCollapsible(false);
    m_library = new LibraryView(ctx);
    split->addWidget(m_library);
    m_pages = new QStackedWidget;
    split->addWidget(m_pages);
    split->setStretchFactor(1, 1);
    split->setSizes({300, 900});
    setCentralWidget(split);

    // Home: empty state that invites a recording.
    m_home = new QWidget;
    auto *homeLayout = new QGridLayout(m_home);
    auto *box = new QWidget;
    box->setMaximumWidth(640);
    box->setMinimumWidth(540);
    auto *hv = new QVBoxLayout(box);
    hv->setSpacing(10);
    auto *logo = new QLabel;
    logo->setPixmap(QIcon(":/icons/afterline.svg").pixmap(68, 68));
    logo->setAlignment(Qt::AlignCenter);
    hv->addWidget(logo);
    hv->addSpacing(6);
    auto *t = label(tr("Conversations, kept."), "PageTitle");
    t->setAlignment(Qt::AlignCenter);
    hv->addWidget(t);
    auto *sub = label(tr("Record a meeting and AfterLine writes the transcript and notes, privately, on this computer."),
                      "Muted");
    sub->setAlignment(Qt::AlignCenter);
    hv->addWidget(sub);
    auto *start = new QPushButton(icon("record", Theme::color(Token::OnAccent)), tr("Start a recording"));
    start->setObjectName("RecordButton");
    start->setCursor(Qt::PointingHandCursor);
    auto *sr = new QHBoxLayout;
    sr->addStretch();
    sr->addWidget(start);
    sr->addStretch();
    hv->addSpacing(12);
    hv->addLayout(sr);
    auto *hint = label(tr("or press Ctrl+R"), "Muted");
    hint->setAlignment(Qt::AlignCenter);
    hv->addWidget(hint);
    hv->addSpacing(26);
    auto *steps = new QHBoxLayout;
    steps->setSpacing(12);
    const QList<std::tuple<QString, QString, QString>> stepList = {
        {"record", tr("Record"), tr("Any app or the whole computer, with your microphone on its own track.")},
        {"transcript", tr("Transcribe"), tr("Live while you talk, then a careful transcript when you stop.")},
        {"notes", tr("Notes"), tr("Summary, decisions and action items, each linked to the moment it was said.")},
    };
    for (const auto &[ic, title, text] : stepList) {
        auto *c = new QWidget;
        c->setObjectName("Card");
        c->setAttribute(Qt::WA_StyledBackground);
        auto *cv = new QVBoxLayout(c);
        cv->setContentsMargins(16, 14, 16, 16);
        cv->setSpacing(6);
        auto *i = new QLabel;
        i->setPixmap(icon(ic, Theme::color(Token::Accent)).pixmap(20, 20));
        cv->addWidget(i);
        cv->addWidget(label(title, "SectionTitle"));
        cv->addWidget(label(text, "Muted"));
        cv->addStretch();
        steps->addWidget(c, 1);
    }
    hv->addLayout(steps);
    homeLayout->addWidget(box, 0, 0, Qt::AlignCenter);
    connect(start, &QPushButton::clicked, this, &MainWindow::showRecord);

    m_record = new RecordView(ctx);
    m_meeting = new MeetingView(ctx);
    m_settings = new SettingsView(ctx);
    for (QWidget *w : {m_home, static_cast<QWidget *>(m_record), static_cast<QWidget *>(m_meeting),
                       static_cast<QWidget *>(m_settings)})
        m_pages->addWidget(w);

    connect(m_library, &LibraryView::recordClicked, this, &MainWindow::showRecord);
    connect(m_library, &LibraryView::settingsClicked, this, &MainWindow::showSettings);
    connect(m_library, &LibraryView::openMeeting, this, &MainWindow::showMeeting);
    connect(m_record, &RecordView::stopped, this, [this](const QString &id) {
        m_library->refresh();
        showMeeting(id);
    });
    connect(m_meeting, &MeetingView::deleted, this, [this] {
        m_library->refresh();
        showHome();
    });
    connect(m_meeting, &MeetingView::changed, m_library, &LibraryView::refresh);
    connect(m_meeting, &MeetingView::openMeeting, this, &MainWindow::showMeeting);
    connect(m_meeting, &MeetingView::showSettings, this, &MainWindow::showSettings);
    connect(m_settings, &SettingsView::themeChanged, this, &MainWindow::themeChanged);
    connect(m_settings, &SettingsView::closeRequested, this, [this] {
        if (m_previous && m_previous != m_settings) {
            m_pages->setCurrentWidget(m_previous);
            if (m_previous == m_meeting)
                m_meeting->reload();
        } else {
            showHome();
        }
    });
    connect(m_pages, &QStackedWidget::currentChanged, this, [this] {
        if (m_pages->currentWidget() == m_record)
            m_record->activate();
        else
            m_record->deactivate();
    });

    connect(ctx.recording, &RecordingController::recordingStarted, this, [this] {
        m_library->refresh();
        setWindowTitle(tr("● Recording — AfterLine"));
    });
    connect(ctx.recording, &RecordingController::recordingStopped, this, [this] {
        m_library->setRecording(false, {});
        setWindowTitle("AfterLine");
    });
    connect(ctx.recording, &RecordingController::statusChanged, this, [this] {
        const QString time = formatTime(m_ctx.recording->elapsed());
        m_library->setRecording(true, time);
        setWindowTitle(tr("● Recording %1 — AfterLine").arg(time));
    });
    connect(ctx.processing, &Processing::progress, this,
            [this](const QString &id, Processing::Kind kind, double f, const QString &) {
                if (kind == Processing::Kind::Ask)
                    return;
                m_library->setLiveStatus(id, (kind == Processing::Kind::Transcript ? tr("Transcribing %1%")
                                                                                   : tr("Notes %1%"))
                                                 .arg(int(f * 100)));
            });
    connect(ctx.processing, &Processing::finished, this,
            [this](const QString &id, Processing::Kind, bool, const QString &) {
                m_library->setLiveStatus(id, {});
                m_library->refresh();
            });
    connect(ctx.processing, &Processing::queueChanged, m_library, &LibraryView::refresh);

    // Downloads in the sidebar footer.
    auto updateDownloads = [this] {
        QString text;
        int percent = -1;
        for (const auto &spec : modelCatalog())
            if (m_ctx.models->isDownloading(spec.id) && spec.kind != ModelKind::SpeechDetector) {
                text = tr("Downloading %1…").arg(spec.name);
                percent = 0;
            }
        m_library->setDownloadStatus(text, percent);
    };
    connect(ctx.models, &ModelManager::changed, this, updateDownloads);
    connect(ctx.models, &ModelManager::progress, this, [this](const QString &id, qint64 got, qint64 total) {
        const ModelSpec *s = findModel(id);
        if (s && s->kind != ModelKind::SpeechDetector && total > 0)
            m_library->setDownloadStatus(tr("Downloading %1 · %2 of %3").arg(s->name, formatBytes(got), formatBytes(total)),
                                         int(got * 100 / total));
    });
    connect(ctx.models, &ModelManager::finished, this, [this, updateDownloads](const QString &id, bool ok, const QString &err) {
        updateDownloads();
        const ModelSpec *s = findModel(id);
        if (!ok && s && s->kind != ModelKind::SpeechDetector && !err.contains(tr("paused")))
            inform(this, tr("Download problem"), tr("%1: %2").arg(s->name, err), Tone::Warning);
        if (ok && m_pages->currentWidget() == m_meeting)
            m_meeting->reload();
    });

    setupShortcuts();
    showHome();
}

void MainWindow::setupShortcuts()
{
    auto add = [this](const QKeySequence &k, auto fn) {
        auto *s = new QShortcut(k, this);
        s->setContext(Qt::ApplicationShortcut);
        connect(s, &QShortcut::activated, this, fn);
    };
    add(QKeySequence(Qt::CTRL | Qt::Key_R), [this] {
        if (m_ctx.recording->recording()) {
            m_ctx.recording->stop();
        } else if (m_pages->currentWidget() == m_record) {
            m_record->startOrStop();
        } else {
            showRecord();
        }
    });
    add(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P), [this] { m_record->togglePause(); });
    add(QKeySequence(Qt::CTRL | Qt::Key_F), [this] {
        if (m_pages->currentWidget() == m_meeting)
            m_meeting->findInTranscript();
        else
            m_library->focusSearch();
    });
    add(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F), [this] { m_library->focusSearch(); });
    add(QKeySequence(Qt::CTRL | Qt::Key_Comma), [this] { showSettings(); });
    add(QKeySequence(Qt::CTRL | Qt::Key_E), [this] {
        if (m_pages->currentWidget() == m_meeting)
            m_meeting->exportMeeting();
    });
    add(QKeySequence(Qt::CTRL | Qt::Key_K), [this] {
        if (m_pages->currentWidget() == m_meeting)
            m_meeting->toggleAsk();
    });
    for (int i = 0; i < 3; ++i)
        add(QKeySequence(Qt::CTRL | Qt::Key(Qt::Key_1 + i)), [this, i] {
            if (m_pages->currentWidget() == m_meeting)
                m_meeting->showTab(i);
        });
    // Space plays/pauses unless typing.
    auto *space = new QShortcut(QKeySequence(Qt::Key_Space), m_meeting);
    space->setContext(Qt::WidgetWithChildrenShortcut);
    connect(space, &QShortcut::activated, this, [this] {
        QWidget *f = QApplication::focusWidget();
        if (qobject_cast<QLineEdit *>(f) || qobject_cast<QPlainTextEdit *>(f))
            return;
        m_meeting->togglePlay();
    });
    add(QKeySequence::Quit, [this] {
        if (confirmQuit()) {
            m_quitting = true;
            qApp->quit();
        }
    });
}

void MainWindow::showRecord()
{
    m_library->select({});
    m_pages->setCurrentWidget(m_record);
}

void MainWindow::showMeeting(const QString &id, double seekTo)
{
    if (id.isEmpty())
        return;
    if (m_ctx.recording->recording() && id == m_ctx.recording->meetingId()) {
        showRecord();
        return;
    }
    m_library->select(id);
    m_meeting->open(id, seekTo);
    m_pages->setCurrentWidget(m_meeting);
}

void MainWindow::showSettings()
{
    if (m_pages->currentWidget() != m_settings)
        m_previous = m_pages->currentWidget();
    m_pages->setCurrentWidget(m_settings);
}

void MainWindow::showHome()
{
    if (m_ctx.recording->recording()) {
        showRecord();
        return;
    }
    m_meeting->closeMeeting();
    m_library->select({});
    m_pages->setCurrentWidget(m_home);
}

QString MainWindow::currentMeetingId() const
{
    return m_pages->currentWidget() == m_meeting ? m_meeting->meetingId() : QString();
}

void MainWindow::bringToFront()
{
    show();
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    raise();
    activateWindow();
}

bool MainWindow::confirmQuit()
{
    if (!m_ctx.recording->recording())
        return true;
    if (!confirm(this, tr("A recording is in progress"), tr("Stop and save the recording before quitting?"),
                 tr("Stop, save and quit"), Tone::Warning, "record"))
        return false;
    m_ctx.recording->stop();
    return true;
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    m_ctx.settings->setWindowGeometry(saveGeometry());
    if (m_quitting) {
        e->accept();
        return;
    }
    // Closing the window must never silently drop a recording: keep running
    // in the tray, or ask.
    const bool tray = QSystemTrayIcon::isSystemTrayAvailable();
    if (tray && (m_ctx.settings->closeToTray() || m_ctx.recording->recording())) {
        hide();
        e->ignore();
        Q_EMIT hiddenToTray();
        return;
    }
    if (!confirmQuit()) {
        e->ignore();
        return;
    }
    m_quitting = true;
    e->accept();
    qApp->quit();
}

}  // namespace afterline::ui
