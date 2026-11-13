// SPDX-License-Identifier: MIT
#include "ui/MeetingView.h"

#include "app/ModelCatalog.h"
#include "app/RecordingController.h"
#include "app/Settings.h"
#include "audio/Player.h"
#include "audio/Recorder.h"
#include "core/Export.h"
#include "core/Notes.h"
#include "core/OggOpus.h"
#include "ui/AskPanel.h"
#include "ui/AudioView.h"
#include "ui/Dialogs.h"
#include "ui/Icons.h"
#include "ui/NotesView.h"
#include "ui/Theme.h"
#include "ui/TranscriptView.h"
#include "ui/Widgets.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "core/Json.h"

namespace afterline::ui {

MeetingView::MeetingView(AppContext ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    m_split = new QSplitter;
    m_split->setChildrenCollapsible(false);
    m_split->setHandleWidth(1);
    outer->addWidget(m_split);

    auto *main = new QWidget;
    auto *v = new QVBoxLayout(main);
    v->setContentsMargins(32, 22, 32, 14);
    v->setSpacing(8);

    // Header
    auto *head = new QHBoxLayout;
    m_title = new QLineEdit;
    m_title->setObjectName("TitleEdit");
    // Not focused automatically when the page opens: it would look like an
    // input box waiting for text.
    m_title->setFocusPolicy(Qt::ClickFocus);
    m_title->setAccessibleName(tr("Meeting title"));
    head->addWidget(m_title, 1);
    auto *ask = new QPushButton(icon("ask"), tr("Ask"));
    ask->setToolTip(tr("Ask a question about this meeting (Ctrl+K)"));
    connect(ask, &QPushButton::clicked, this, &MeetingView::toggleAsk);
    head->addWidget(ask);
    auto *exportBtn = new QPushButton(icon("export"), tr("Export"));
    exportBtn->setToolTip(tr("Export or copy (Ctrl+E)"));
    connect(exportBtn, &QPushButton::clicked, this, &MeetingView::exportMeeting);
    head->addWidget(exportBtn);
    auto *more = iconButton("more", tr("More actions"));
    auto *menu = new QMenu(more);
    QMenu *folderMenu = menu->addMenu(icon("folder"), tr("Move to folder"));
    connect(folderMenu, &QMenu::aboutToShow, this, [this, folderMenu] { fillFolderMenu(folderMenu); });
    menu->addAction(icon("tag"), tr("Edit tags…"), this, &MeetingView::editTags);
    menu->addSeparator();
    menu->addAction(icon("transcript"), tr("Transcribe again"), this, [this] {
        if (m_meeting.hasTranscript() &&
            !confirm(this, tr("Transcribe again?"),
                     tr("The transcript, including your corrections, will be replaced by a new one made with the "
                        "current speech model and language (%1). The audio is not changed.")
                         .arg(Settings::languageName(m_ctx.settings->language())),
                     tr("Transcribe again"), Tone::Warning, "transcript"))
            return;
        QFile::remove(m_meeting.path("transcript.json.partial"));
        startTranscription();
    });
    menu->addAction(icon("notes"), tr("Write notes again"), this, [this] {
        if (m_meeting.notesEdited() &&
            !confirm(this, tr("Write notes again?"),
                     tr("New notes will be generated. Your edited notes will be kept until you choose “Restore "
                        "generated notes”."),
                     tr("Write again"), Tone::Neutral, "notes"))
            return;
        startNotes();
    });
    menu->addAction(icon("folder"), tr("Show files"), this,
                    [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(m_meeting.dir)); });
    menu->addSeparator();
    menu->addAction(icon("trash", Theme::color(Token::Danger)), tr("Delete meeting…"), this, &MeetingView::deleteMeeting);
    more->setMenu(menu);
    connect(more, &QPushButton::clicked, more, &QPushButton::showMenu);
    head->addWidget(more);
    v->addLayout(head);
    auto *metaRow = new QHBoxLayout;
    metaRow->setSpacing(10);
    m_folderChip = new QPushButton;
    m_folderChip->setObjectName("FolderChip");
    m_folderChip->setCursor(Qt::PointingHandCursor);
    m_folderChip->setToolTip(tr("Move to another folder"));
    auto *chipMenu = new QMenu(m_folderChip);
    connect(chipMenu, &QMenu::aboutToShow, this, [this, chipMenu] { fillFolderMenu(chipMenu); });
    m_folderChip->setMenu(chipMenu);
    metaRow->addWidget(m_folderChip);
    m_meta = label({}, "Muted");
    metaRow->addWidget(m_meta, 1);
    v->addLayout(metaRow);

    // "Filed in Work by AfterLine · Undo", or a suggested new folder.
    m_folderNote = new QWidget;
    m_folderNote->setObjectName("FolderNote");
    m_folderNote->setAttribute(Qt::WA_StyledBackground);
    auto *fl = new QHBoxLayout(m_folderNote);
    fl->setContentsMargins(12, 6, 6, 6);
    auto *spark = new QLabel;
    spark->setPixmap(icon("sparkles", Theme::color(Token::Accent)).pixmap(16, 16));
    fl->addWidget(spark);
    m_folderNoteText = label({});
    m_folderNoteText->setTextFormat(Qt::RichText);
    fl->addWidget(m_folderNoteText, 1);
    m_folderNoteYes = new QPushButton;
    m_folderNoteNo = new QPushButton;
    m_folderNoteYes->setObjectName("SmallButton");
    m_folderNoteNo->setObjectName("SmallButton");
    fl->addWidget(m_folderNoteYes);
    fl->addWidget(m_folderNoteNo);
    m_folderNote->hide();
    v->addWidget(m_folderNote);

    // Status banner
    m_banner = new QWidget;
    m_banner->setObjectName("Banner");
    m_banner->setAttribute(Qt::WA_StyledBackground);
    auto *bl = new QHBoxLayout(m_banner);
    bl->setContentsMargins(14, 10, 10, 10);
    auto *bt = new QVBoxLayout;
    m_bannerText = label({});
    m_bannerProgress = new QProgressBar;
    m_bannerProgress->setRange(0, 1000);
    m_bannerProgress->setFixedHeight(6);
    bt->addWidget(m_bannerText);
    bt->addWidget(m_bannerProgress);
    bl->addLayout(bt, 1);
    m_bannerAction = new QPushButton;
    m_bannerAction->setObjectName("Primary");
    m_bannerCancel = new QPushButton(tr("Cancel"));
    m_bannerReport = new QPushButton(tr("Report this"));
    m_bannerReport->setToolTip(tr("Tell the developer, with the error and recent logs"));
    connect(m_bannerReport, &QPushButton::clicked, this, [this] { reportProblem(this, m_ctx, false, m_meeting.lastError); });
    bl->addWidget(m_bannerReport);
    bl->addWidget(m_bannerAction);
    bl->addWidget(m_bannerCancel);
    v->addWidget(m_banner);

    // Tabs
    m_tabs = new QTabWidget;
    m_tabs->setDocumentMode(true);
    m_notes = new NotesView;
    m_transcript = new TranscriptView;
    m_audio = new AudioView;
    m_tabs->addTab(m_notes, tr("Notes"));
    m_tabs->addTab(m_transcript, tr("Transcript"));
    m_tabs->addTab(m_audio, tr("Audio"));
    v->addWidget(m_tabs, 1);

    // Player bar
    m_playerBar = new QWidget;
    auto *pb = new QHBoxLayout(m_playerBar);
    pb->setContentsMargins(0, 6, 0, 0);
    auto *back = iconButton("rewind", tr("Back 15 seconds"));
    m_play = iconButton("play", tr("Play (Space)"));
    m_play->setIconSize({26, 26});
    auto *fwd = iconButton("forward", tr("Forward 15 seconds"));
    pb->addWidget(back);
    pb->addWidget(m_play);
    pb->addWidget(fwd);
    m_time = label("0:00 / 0:00", "Muted");
    m_time->setMinimumWidth(110);
    pb->addWidget(m_time);
    m_mini = new Waveform;
    m_mini->setCompact(true);
    pb->addWidget(m_mini, 1);
    v->addWidget(m_playerBar);
    m_split->addWidget(main);

    m_ask = new AskPanel(ctx);
    m_ask->hide();
    m_split->addWidget(m_ask);
    m_split->setStretchFactor(0, 3);
    m_split->setStretchFactor(1, 1);

    m_player = std::make_unique<audio::Player>(ctx.recording->graph());
    m_tick.setInterval(100);
    connect(&m_tick, &QTimer::timeout, this, &MeetingView::updatePlayer);

    connect(m_title, &QLineEdit::editingFinished, this, [this] {
        const QString t = m_title->text().trimmed();
        if (!m_meeting.isValid() || t.isEmpty() || t == m_meeting.title)
            return;
        m_meeting = m_ctx.store->get(m_meeting.id);
        m_meeting.title = t;
        m_meeting.titleIsAuto = false;
        m_ctx.store->save(m_meeting);
        Q_EMIT changed(m_meeting.id);
    });
    connect(m_play, &QPushButton::clicked, this, &MeetingView::togglePlay);
    connect(back, &QPushButton::clicked, this, [this] { seek(m_player->position() - 15, m_player->playing()); });
    connect(fwd, &QPushButton::clicked, this, [this] { seek(m_player->position() + 15, m_player->playing()); });
    connect(m_mini, &Waveform::seekRequested, this, [this](double t) { seek(t, m_player->playing()); });
    connect(m_audio, &AudioView::seekRequested, this, [this](double t) { seek(t); });
    connect(m_audio, &AudioView::volumesChanged, this, [this](float c, float m) { m_player->setVolumes(c, m); });
    connect(m_audio, &AudioView::rateChanged, this, [this](double r) { m_player->setRate(r); });
    connect(m_audio, &AudioView::showFilesRequested, this,
            [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(m_meeting.dir)); });
    connect(m_transcript, &TranscriptView::seekRequested, this, [this](double t) { seek(t); });
    connect(m_transcript, &TranscriptView::changed, this, [this] {
        m_ctx.store->reindex(m_meeting.id);
    });
    connect(m_notes, &NotesView::seekRequested, this, [this](double t) {
        seek(t);
    });
    connect(m_notes, &NotesView::changed, this, [this] { m_ctx.store->reindex(m_meeting.id); });
    connect(m_notes, &NotesView::actionRequested, this, [this] {
        const ModelSpec *notes = findModel(m_ctx.settings->notesModel());
        if (!m_meeting.hasTranscript())
            startTranscription();
        else if (!notes || !notes->installed())
            Q_EMIT showSettings();
        else
            startNotes();
    });
    connect(m_ask, &AskPanel::closeRequested, m_ask, &QWidget::hide);
    connect(m_ask, &AskPanel::openMeeting, this, &MeetingView::openMeeting);
    connect(m_bannerCancel, &QPushButton::clicked, this, [this] { m_ctx.processing->cancel(m_meeting.id); });
    connect(ctx.processing, &Processing::progress, this, &MeetingView::onProgress);
    connect(ctx.processing, &Processing::queueChanged, this, &MeetingView::updateBanner);
    connect(ctx.processing, &Processing::finished, this,
            [this](const QString &id, Processing::Kind, bool, const QString &) {
                if (id == m_meeting.id)
                    reload();
            });
}

MeetingView::~MeetingView() = default;

void MeetingView::open(const QString &meetingId, double seekTo)
{
    if (meetingId != m_meeting.id) {
        m_player->close();
        m_meeting = m_ctx.store->get(meetingId);
        reload();
        m_tabs->setCurrentIndex(m_notes->hasNotes() || !m_meeting.hasTranscript() ? 0 : 1);
    }
    if (seekTo >= 0) {
        m_tabs->setCurrentWidget(m_transcript);
        m_transcript->revealTime(seekTo);
        seek(seekTo, false);
    }
}

void MeetingView::closeMeeting()
{
    m_player->close();
    m_tick.stop();
    m_meeting = {};
}

void MeetingView::reload()
{
    if (!m_meeting.id.isEmpty())
        m_meeting = m_ctx.store->get(m_meeting.id);
    if (!m_meeting.isValid())
        return;
    m_title->setText(m_meeting.title);
    m_title->setCursorPosition(0);
    QStringList meta{QLocale().toString(m_meeting.created, "dddd, d MMMM yyyy · HH:mm")};
    if (m_meeting.duration > 0)
        meta << formatDuration(m_meeting.duration);
    if (!m_meeting.sourceLabel.isEmpty())
        meta << m_meeting.sourceLabel + (m_meeting.microphoneLabel.isEmpty() ? QString() : tr(" + microphone"));
    if (m_meeting.folder.isEmpty()) {
        m_folderChip->setText(tr("No folder"));
        m_folderChip->setIcon(icon("folder", Theme::color(Token::Muted)));
    } else {
        const FolderInfo f = m_ctx.store->folder(m_meeting.folder);
        m_folderChip->setText(f.name);
        m_folderChip->setIcon(icon(f.icon, folderColor(f.color)));
    }
    updateFolderNote();
    if (!m_meeting.tags.isEmpty())
        meta << "#" + m_meeting.tags.join(" #");
    m_meta->setText(meta.join("  ·  "));

    // Player
    const bool playing = m_player->playing();
    const double pos = m_player->position();
    std::string err;
    if (m_meeting.state != "recording" && m_meeting.hasAudio()) {
        m_player->open(m_meeting.hasComputerAudio() ? m_meeting.path(audio::Recorder::kComputerFile).toStdString() : "",
                       m_meeting.hasMicrophoneAudio() ? m_meeting.path(audio::Recorder::kMicrophoneFile).toStdString() : "",
                       &err);
        m_player->seek(pos);
        if (playing)
            m_player->play();
    }
    const double duration = m_player->duration() > 0 ? m_player->duration() : m_meeting.duration;
    m_mini->load(m_meeting.path(audio::Recorder::kComputerPeaks), m_meeting.path(audio::Recorder::kMicrophonePeaks),
                 duration);
    m_audio->load(m_meeting, duration);
    m_playerBar->setEnabled(m_player->duration() > 0);

    if (!m_meeting.hasTranscript() && QFile::exists(m_meeting.path("live-transcript.json")))
        m_transcript->load(m_meeting.path("live-transcript.json"), true);
    else
        m_transcript->load(m_meeting.path("transcript.json"));
    const ModelSpec *notesModel = findModel(m_ctx.settings->notesModel());
    QString empty, action;
    if (!m_meeting.hasTranscript()) {
        empty = tr("Notes are written from the transcript, once the meeting has been transcribed.");
    } else if (m_meeting.transcriptIsEmpty()) {
        empty = tr("No speech was found in this recording, so there are no notes. If people did speak, check in "
                   "the Audio tab that the right source was recorded.");
    } else if (!notesModel || !notesModel->installed()) {
        empty = tr("To write notes, AfterLine needs its notes model (%1, downloaded once).")
                    .arg(notesModel ? formatBytes(notesModel->bytes) : QString());
        action = tr("Set up notes");
    } else if (m_meeting.notesState == "failed") {
        empty = tr("Notes could not be written: %1").arg(m_meeting.lastError);
        action = tr("Try again");
    } else if (m_meeting.notesState == "running" || m_meeting.notesState == "queued") {
        empty = tr("Writing notes…");
    } else {
        empty = tr("No notes yet.");
        action = tr("Write notes");
    }
    m_notes->load(m_meeting, empty, action);
    m_ask->setMeeting(m_meeting.id, m_meeting.title);
    updateBanner();
    updatePlayer();
}

void MeetingView::updateBanner()
{
    if (!m_meeting.isValid())
        return;
    disconnect(m_bannerAction, &QPushButton::clicked, nullptr, nullptr);
    m_bannerCancel->hide();
    m_bannerReport->hide();
    m_bannerProgress->hide();
    m_bannerAction->hide();
    QString text;
    const bool running = m_ctx.processing->busy() && m_ctx.processing->currentMeetingId() == m_meeting.id;
    const bool queued = !running && m_ctx.processing->isQueuedOrRunning(m_meeting.id);
    const ModelSpec *speech = findModel(m_ctx.settings->speechModel());
    if (m_meeting.state == "recording") {
        text = tr("This meeting is being recorded.");
    } else if (running) {
        const bool transcript = m_ctx.processing->currentKind() == Processing::Kind::Transcript;
        text = m_ctx.processing->currentStage();
        if (text.isEmpty())
            text = transcript ? tr("Transcribing…") : tr("Writing notes…");
        text += QString(" · %1%").arg(int(m_ctx.processing->currentProgress() * 100));
        m_bannerProgress->setValue(int(m_ctx.processing->currentProgress() * 1000));
        m_bannerProgress->show();
        m_bannerCancel->show();
    } else if (queued) {
        text = m_ctx.recording->recording() ? tr("Waiting: processing starts when the current recording ends.")
                                            : tr("Waiting for another meeting to finish processing.");
        m_bannerCancel->show();
    } else if (m_meeting.transcriptState == "failed" || m_meeting.notesState == "failed") {
        text = m_meeting.lastError;
        m_bannerAction->setText(tr("Try again"));
        m_bannerAction->show();
        m_bannerReport->show();
        const bool t = m_meeting.transcriptState == "failed";
        connect(m_bannerAction, &QPushButton::clicked, this, [this, t] { t ? startTranscription() : startNotes(); });
    } else if (!m_meeting.hasTranscript() && m_meeting.hasAudio()) {
        if (m_meeting.state == "interrupted")
            text = tr("AfterLine closed unexpectedly during this recording. The audio up to %1 was recovered.")
                       .arg(formatTime(m_meeting.duration)) + " ";
        if (speech && speech->installed()) {
            text += tr("This meeting has not been transcribed yet.");
            m_bannerAction->setText(tr("Transcribe"));
            connect(m_bannerAction, &QPushButton::clicked, this, &MeetingView::startTranscription);
        } else {
            text += tr("To transcribe meetings, AfterLine needs a speech recognition model (downloaded once, then "
                       "everything works offline).");
            m_bannerAction->setText(tr("Set up transcription"));
            connect(m_bannerAction, &QPushButton::clicked, this, &MeetingView::showSettings);
        }
        m_bannerAction->show();
    } else if (!m_meeting.hasAudio()) {
        text = tr("This meeting has no audio. Nothing was captured, or the audio files were removed.");
    }
    m_bannerText->setText(text);
    m_banner->setVisible(!text.isEmpty());
}

void MeetingView::onProgress(const QString &id, Processing::Kind, double, const QString &)
{
    if (id == m_meeting.id)
        updateBanner();
}

void MeetingView::startTranscription()
{
    QString error;
    if (!m_ctx.processing->transcribe(m_meeting.id, &error))
        inform(this, tr("Cannot transcribe yet"), error, Tone::Info, "transcript");
    reload();
}

void MeetingView::startNotes()
{
    QString error;
    if (!m_ctx.processing->writeNotes(m_meeting.id, &error))
        inform(this, tr("Cannot write notes yet"), error, Tone::Info, "notes");
    reload();
}

void MeetingView::seek(double seconds, bool play)
{
    if (m_player->duration() <= 0)
        return;
    m_player->seek(std::clamp(seconds, 0.0, m_player->duration()));
    if (play)
        m_player->play();
    updatePlayer();
}

void MeetingView::togglePlay()
{
    if (m_player->duration() <= 0)
        return;
    if (m_player->playing())
        m_player->pause();
    else
        m_player->play();
    updatePlayer();
}

void MeetingView::updatePlayer()
{
    const bool playing = m_player->playing();
    m_play->setIcon(icon(playing ? "pause" : "play"));
    m_play->setToolTip(playing ? tr("Pause (Space)") : tr("Play (Space)"));
    const double pos = m_player->position();
    m_time->setText(formatTime(pos) + " / " + formatTime(m_player->duration()));
    m_mini->setPosition(pos);
    m_audio->setPosition(pos);
    m_transcript->setPlaybackPosition(playing ? pos : -1);
    if (playing && !m_tick.isActive())
        m_tick.start();
    else if (!playing)
        m_tick.stop();
}

void MeetingView::showTab(int index) { m_tabs->setCurrentIndex(index); }
void MeetingView::findInTranscript()
{
    m_tabs->setCurrentWidget(m_transcript);
    m_transcript->focusFind();
}

void MeetingView::toggleAsk()
{
    m_ask->setVisible(!m_ask->isVisible());
    if (m_ask->isVisible())
        m_ask->focusInput();
}

void MeetingView::fillFolderMenu(QMenu *menu)
{
    menu->clear();
    const QString id = m_meeting.id;
    for (const auto &f : m_ctx.store->folders()) {
        QAction *a = menu->addAction(icon(f.icon, folderColor(f.color)), f.name, this, [this, id, name = f.name] {
            m_ctx.store->setFolder(id, name);
            reload();
            Q_EMIT changed(id);
        });
        a->setCheckable(true);
        a->setChecked(m_meeting.folder.compare(f.name, Qt::CaseInsensitive) == 0);
    }
    if (!menu->isEmpty())
        menu->addSeparator();
    menu->addAction(icon("plus"), tr("New folder…"), this, [this, id] {
        FolderInfo f;
        QStringList taken;
        for (const auto &e : m_ctx.store->folders())
            taken << e.name;
        if (!editFolder(this, &f, true, taken))
            return;
        m_ctx.store->saveFolder(f);
        m_ctx.store->setFolder(id, f.name);
        reload();
        Q_EMIT changed(id);
    });
    if (!m_meeting.folder.isEmpty())
        menu->addAction(icon("close"), tr("Remove from folder"), this, [this, id] {
            m_ctx.store->setFolder(id, {});
            reload();
            Q_EMIT changed(id);
        });
}

void MeetingView::updateFolderNote()
{
    m_folderNoteYes->disconnect(this);
    m_folderNoteNo->disconnect(this);
    const QString id = m_meeting.id;
    if (m_meeting.folderAuto && !m_meeting.folder.isEmpty()) {
        m_folderNoteText->setText(tr("AfterLine filed this meeting in <b>%1</b>.").arg(m_meeting.folder.toHtmlEscaped()));
        m_folderNoteYes->setText(tr("Keep"));
        m_folderNoteNo->setText(tr("Undo"));
        connect(m_folderNoteYes, &QPushButton::clicked, this, [this, id] {
            MeetingInfo m = m_ctx.store->get(id);
            m.folderAuto = false;
            m_ctx.store->save(m);
            reload();
        });
        connect(m_folderNoteNo, &QPushButton::clicked, this, [this, id] {
            m_ctx.store->setFolder(id, {});
            reload();
            Q_EMIT changed(id);
        });
        m_folderNote->show();
    } else if (m_meeting.folder.isEmpty() && !m_meeting.folderSuggestion.isEmpty()) {
        m_folderNoteText->setText(tr("Put this meeting in a new folder, <b>%1</b>?")
                                      .arg(m_meeting.folderSuggestion.toHtmlEscaped()));
        m_folderNoteYes->setText(tr("Create folder"));
        m_folderNoteNo->setText(tr("Not now"));
        connect(m_folderNoteYes, &QPushButton::clicked, this, [this, id] {
            const MeetingInfo m = m_ctx.store->get(id);
            FolderInfo f{m.folderSuggestion, "folder", {}};
            // A colour of its own: the next one not used yet.
            QStringList used;
            for (const auto &e : m_ctx.store->folders())
                used << e.color;
            for (const auto &c : folderColors())
                if (!used.contains(c)) {
                    f.color = c;
                    break;
                }
            m_ctx.store->saveFolder(f);
            m_ctx.store->setFolder(id, f.name);
            reload();
            Q_EMIT changed(id);
        });
        connect(m_folderNoteNo, &QPushButton::clicked, this, [this, id] {
            MeetingInfo m = m_ctx.store->get(id);
            m.folderSuggestion.clear();
            m_ctx.store->save(m);
            reload();
        });
        m_folderNote->show();
    } else {
        m_folderNote->hide();
    }
}

void MeetingView::editTags()
{
    QString tags = m_meeting.tags.join(", ");
    if (!askText(this, tr("Tags"), tr("Separate tags with commas. Tags appear in the sidebar."), &tags,
                 tr("for example: planning, client"), tr("Save"), true))
        return;
    m_meeting = m_ctx.store->get(m_meeting.id);
    m_meeting.tags.clear();
    for (const QString &t : tags.split(',', Qt::SkipEmptyParts))
        if (!t.trimmed().isEmpty())
            m_meeting.tags << t.trimmed();
    m_meeting.tags.removeDuplicates();
    m_ctx.store->save(m_meeting);
    reload();
    Q_EMIT changed(m_meeting.id);
}

void MeetingView::exportMeeting()
{
    if (!m_meeting.isValid())
        return;
    QMenu menu(this);
    ExportInput in;
    in.title = m_meeting.title;
    in.date = m_meeting.created;
    in.duration = m_meeting.duration;
    in.transcript = m_transcript->transcript();
    if (QFile::exists(m_meeting.path("notes.md")))
        in.notesMarkdown = m_notes->markdown();
    else if (QFile::exists(m_meeting.path("notes.json")))
        in.notesMarkdown = notesToMarkdown(loadNotes(m_meeting.path("notes.json")), false);
    auto save = [this, in](ExportFormat f, const QString &filter) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        QString name = in.title;
        name.replace(QRegularExpression(R"([/\\:*?"<>|])"), "-");
        const QString path = QFileDialog::getSaveFileName(this, tr("Export meeting"),
                                                          dir + "/" + name + "." + exportExtension(f), filter);
        if (path.isEmpty())
            return;
        QString err;
        if (!writeTextFile(path, afterline::exportMeeting(in, f), &err))
            inform(this, tr("Export failed"), err, Tone::Warning);
    };
    const bool hasT = !in.transcript.isEmpty();
    menu.addAction(icon("notes"), tr("Notes and transcript (Markdown)…"),
                   [=] { save(ExportFormat::Markdown, tr("Markdown (*.md)")); });
    menu.addAction(icon("file"), tr("Notes and transcript (plain text)…"),
                   [=] { save(ExportFormat::PlainText, tr("Text (*.txt)")); });
    auto *srt = menu.addAction(tr("Subtitles (SRT)…"), [=] { save(ExportFormat::Srt, tr("SubRip (*.srt)")); });
    auto *vtt = menu.addAction(tr("Subtitles (WebVTT)…"), [=] { save(ExportFormat::WebVtt, tr("WebVTT (*.vtt)")); });
    auto *json = menu.addAction(tr("Transcript data (JSON)…"), [=] { save(ExportFormat::Json, tr("JSON (*.json)")); });
    for (auto *a : {srt, vtt, json})
        a->setEnabled(hasT);
    menu.addSeparator();
    auto *copyNotes = menu.addAction(icon("copy"), tr("Copy notes"),
                                     [in] { QApplication::clipboard()->setText(in.notesMarkdown); });
    copyNotes->setEnabled(!in.notesMarkdown.isEmpty());
    auto *copyT = menu.addAction(icon("copy"), tr("Copy transcript"),
                                 [in] { QApplication::clipboard()->setText(transcriptToText(in.transcript)); });
    copyT->setEnabled(hasT);
    menu.addSeparator();
    auto *audio = menu.addAction(icon("waveform"), tr("Save audio files…"), [this] {
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Save the audio to"), QStandardPaths::writableLocation(QStandardPaths::MusicLocation));
        if (dir.isEmpty())
            return;
        QString base = m_meeting.title;
        base.replace(QRegularExpression(R"([/\\:*?"<>|])"), "-");
        int copied = 0;
        for (const auto &[file, suffix] : {std::pair{audio::Recorder::kComputerFile, tr("meeting")},
                                           std::pair{audio::Recorder::kMicrophoneFile, tr("microphone")}})
            if (QFile::exists(m_meeting.path(file)))
                copied += QFile::copy(m_meeting.path(file), dir + "/" + base + " (" + suffix + ").opus");
        if (!copied)
            inform(this, tr("Nothing saved"),
                   tr("The audio could not be copied (a file with that name may already exist)."), Tone::Warning);
    });
    audio->setEnabled(m_meeting.hasAudio());
    menu.exec(QCursor::pos());
}

void MeetingView::deleteMeeting()
{
    if (!m_meeting.isValid() || m_meeting.state == "recording")
        return;
    QStringList parts;
    if (m_meeting.hasAudio())
        parts << tr("the audio recording");
    if (m_meeting.hasTranscript())
        parts << tr("the transcript (and your corrections)");
    if (m_meeting.hasNotes())
        parts << tr("the notes");
    if (!confirm(this, tr("Delete “%1”?").arg(m_meeting.title),
                 tr("%1 will be moved to the trash. You can restore it from there until the trash is emptied.")
                     .arg(parts.isEmpty() ? tr("Its details") : [&] {
                         QString l = QLocale().createSeparatedList(parts);
                         l[0] = l[0].toUpper();
                         return l;
                     }()),
                 tr("Move to trash"), Tone::Danger))
        return;
    m_ctx.processing->cancel(m_meeting.id);
    const QString id = m_meeting.id;
    closeMeeting();
    QString err;
    if (!m_ctx.store->remove(id, &err)) {
        inform(this, tr("Could not delete"), err, Tone::Warning);
        return;
    }
    Q_EMIT deleted(id);
}

}  // namespace afterline::ui
