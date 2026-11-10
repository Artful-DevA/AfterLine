// SPDX-License-Identifier: MIT
#include "ui/RecordView.h"

#include "app/MeetingStore.h"
#include "app/Settings.h"
#include "core/Transcript.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace afterline::ui {

using audio::CaptureStream;

namespace {
QWidget *card(QWidget *parent = nullptr)
{
    auto *w = new QWidget(parent);
    w->setObjectName("Card");
    w->setAttribute(Qt::WA_StyledBackground);
    return w;
}
}  // namespace

RecordView::RecordView(AppContext ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(40, 36, 40, 36);
    m_stack = new QStackedWidget;
    m_stack->addWidget(buildIdle());
    m_stack->addWidget(buildRecording());
    auto *center = new QHBoxLayout;
    center->addStretch();
    m_stack->setMaximumWidth(640);
    m_stack->setMinimumWidth(420);
    center->addWidget(m_stack, 4);
    center->addStretch();
    outer->addStretch(1);
    outer->addLayout(center);
    outer->addStretch(2);

    m_timerTick.setInterval(100);
    connect(&m_timerTick, &QTimer::timeout, this, &RecordView::tick);
    connect(ctx.recording, &RecordingController::sourcesChanged, this, &RecordView::reloadSources);
    connect(ctx.recording, &RecordingController::warning, this, &RecordView::showWarning);
    connect(ctx.recording, &RecordingController::warningCleared, this, [this] { m_warning->hide(); });
    connect(ctx.recording, &RecordingController::recordingStopped, this, [this](const QString &id) {
        showRecording(false);
        Q_EMIT stopped(id);
    });
    connect(ctx.recording, &RecordingController::recordingStarted, this, [this] { showRecording(true); });
    connect(ctx.recording, &RecordingController::pausedChanged, this, [this](bool paused) {
        m_pause->setText(paused ? tr("Resume") : tr("Pause"));
        m_pause->setIcon(icon(paused ? "record" : "pause"));
        m_dot->setPaused(paused);
        m_state->setText(paused ? tr("Paused. Nothing is being recorded.") : tr("Recording"));
    });
    reloadSources();
    showRecording(ctx.recording->recording());
}

QWidget *RecordView::buildIdle()
{
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(14);
    v->addWidget(label(tr("New recording"), "PageTitle"));
    v->addWidget(label(tr("Choose what to record. Your audio stays on this computer."), "Muted"));

    m_audioProblem = label({}, "ErrorText");
    m_audioProblem->hide();
    v->addWidget(m_audioProblem);

    auto *c = card();
    auto *g = new QVBoxLayout(c);
    g->setContentsMargins(22, 20, 22, 22);
    g->setSpacing(10);

    g->addWidget(label(tr("Record audio from"), "SectionTitle"));
    auto *row = new QHBoxLayout;
    m_source = new QComboBox;
    m_source->setIconSize({20, 20});
    m_source->setAccessibleName(tr("Record audio from"));
    m_source->setMinimumContentsLength(24);
    row->addWidget(m_source, 1);
    auto *refresh = iconButton("refresh", tr("Look for applications and devices again"));
    connect(refresh, &QPushButton::clicked, this, &RecordView::reloadSources);
    row->addWidget(refresh);
    g->addLayout(row);
    m_sourceDetail = label({}, "Muted");
    g->addWidget(m_sourceDetail);

    g->addSpacing(8);
    g->addWidget(hline());
    g->addSpacing(8);

    m_includeMic = new QCheckBox(tr("Include my microphone"));
    m_includeMic->setChecked(m_ctx.settings->includeMicrophone());
    QFont f = m_includeMic->font();
    f.setWeight(QFont::DemiBold);
    m_includeMic->setFont(f);
    g->addWidget(m_includeMic);
    auto *micHelp = label(tr("Recorded on its own track, so the transcript can tell your words from the call's. "
                             "When this is off, AfterLine does not open the microphone at all."),
                          "Muted");
    g->addWidget(micHelp);
    m_mic = new QComboBox;
    m_mic->setAccessibleName(tr("Microphone"));
    g->addWidget(m_mic);
    auto *meterRow = new QHBoxLayout;
    auto *micIcon = new QLabel;
    micIcon->setPixmap(icon("mic", Theme::color(Token::Muted)).pixmap(16, 16));
    meterRow->addWidget(micIcon);
    m_previewMeter = new LevelMeter;
    m_previewMeter->setToolTip(tr("Speak to check that the microphone works"));
    meterRow->addWidget(m_previewMeter, 1);
    g->addLayout(meterRow);
    v->addWidget(c);

    m_start = new QPushButton(icon("record", Theme::color(Token::OnAccent)), tr("Start recording"));
    m_start->setObjectName("RecordButton");
    m_start->setIconSize({18, 18});
    m_start->setCursor(Qt::PointingHandCursor);
    m_start->setToolTip(tr("Start recording (Ctrl+R)"));
    auto *startRow = new QHBoxLayout;
    startRow->addStretch();
    startRow->addWidget(m_start);
    startRow->addStretch();
    v->addSpacing(6);
    v->addLayout(startRow);
    auto *consent = label(tr("Make sure everyone in the conversation knows it is being recorded, and has agreed "
                             "where the law requires it."),
                          "Muted");
    consent->setAlignment(Qt::AlignCenter);
    v->addWidget(consent);

    connect(m_start, &QPushButton::clicked, this, &RecordView::start);
    connect(m_source, &QComboBox::currentIndexChanged, this, &RecordView::updateDetail);
    connect(m_includeMic, &QCheckBox::toggled, this, [this](bool on) {
        m_ctx.settings->setIncludeMicrophone(on);
        m_mic->setEnabled(on);
        updatePreview();
    });
    connect(m_mic, &QComboBox::currentIndexChanged, this, &RecordView::updatePreview);
    return page;
}

QWidget *RecordView::buildRecording()
{
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(14);

    auto *top = new QHBoxLayout;
    m_dot = new RecordingDot;
    top->addWidget(m_dot);
    m_state = label(tr("Recording"), "SectionTitle");
    m_state->setAccessibleName(tr("Recording status"));
    top->addWidget(m_state, 1);
    v->addLayout(top);

    m_title = new QLineEdit;
    m_title->setObjectName("TitleEdit");
    m_title->setAccessibleName(tr("Meeting title"));
    m_title->setPlaceholderText(tr("Meeting title"));
    v->addWidget(m_title);
    connect(m_title, &QLineEdit::editingFinished, this, [this] {
        MeetingInfo m = m_ctx.store->get(m_ctx.recording->meetingId());
        const QString t = m_title->text().trimmed();
        if (m.isValid() && !t.isEmpty() && t != m.title) {
            m.title = t;
            m.titleIsAuto = false;
            m_ctx.store->save(m);
        }
    });

    m_timer = label("0:00", "Timer");
    m_timer->setAccessibleName(tr("Recording duration"));
    v->addWidget(m_timer);

    auto *c = card();
    auto *g = new QGridLayout(c);
    g->setContentsMargins(20, 16, 20, 18);
    g->setHorizontalSpacing(12);
    g->setVerticalSpacing(6);
    auto addTrack = [&](int row, const QString &iconName, QLabel *&name, QLabel *&state, LevelMeter *&meter) {
        auto *ic = new QLabel;
        ic->setPixmap(icon(iconName, Theme::color(Token::Muted)).pixmap(18, 18));
        g->addWidget(ic, row * 2, 0);
        name = label({});
        QFont f = name->font();
        f.setWeight(QFont::DemiBold);
        name->setFont(f);
        g->addWidget(name, row * 2, 1);
        state = label({}, "Muted");
        state->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        g->addWidget(state, row * 2, 2);
        meter = new LevelMeter;
        g->addWidget(meter, row * 2 + 1, 1, 1, 2);
    };
    addTrack(0, "computer", m_computerLabel, m_computerState, m_computerMeter);
    addTrack(1, "mic", m_micLabel, m_micState, m_micMeter);
    m_micRow = c;
    g->setColumnStretch(1, 1);
    v->addWidget(c);

    m_warning = new QWidget;
    m_warning->setObjectName("WarningBanner");
    m_warning->setAttribute(Qt::WA_StyledBackground);
    auto *wl = new QHBoxLayout(m_warning);
    wl->setContentsMargins(14, 12, 10, 12);
    auto *wi = new QLabel;
    wi->setPixmap(icon("alert", Theme::color(Token::Warning)).pixmap(20, 20));
    wl->addWidget(wi, 0, Qt::AlignTop);
    auto *wt = new QVBoxLayout;
    m_warningTitle = label({}, "SectionTitle");
    m_warningText = label({});
    wt->addWidget(m_warningTitle);
    wt->addWidget(m_warningText);
    wl->addLayout(wt, 1);
    auto *close = iconButton("close", tr("Dismiss"));
    connect(close, &QPushButton::clicked, m_warning, &QWidget::hide);
    wl->addWidget(close, 0, Qt::AlignTop);
    m_warning->hide();
    v->addWidget(m_warning);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    m_pause = new QPushButton(icon("pause"), tr("Pause"));
    m_pause->setToolTip(tr("Pause (Ctrl+Shift+P)"));
    m_stop = new QPushButton(icon("stop", QColor(Qt::white)), tr("Stop and save"));
    m_stop->setObjectName("StopButton");
    m_stop->setToolTip(tr("Stop and save (Ctrl+R)"));
    for (auto *b : {m_pause, m_stop})
        b->setCursor(Qt::PointingHandCursor);
    buttons->addWidget(m_pause);
    buttons->addWidget(m_stop);
    buttons->addStretch();
    v->addLayout(buttons);
    auto *safe = label(tr("Audio is written to disk continuously. Closing the window keeps recording in the "
                          "background."),
                       "Muted");
    safe->setAlignment(Qt::AlignCenter);
    v->addWidget(safe);

    connect(m_pause, &QPushButton::clicked, this, &RecordView::togglePause);
    connect(m_stop, &QPushButton::clicked, this, [this] { m_ctx.recording->stop(); });
    return page;
}

void RecordView::reloadSources()
{
    const QString keepSource = m_source->currentData().toString().isEmpty() ? m_ctx.settings->lastSource()
                                                                             : m_source->currentData().toString();
    const QString keepMic = m_mic->currentData().toString().isEmpty() ? m_ctx.settings->lastMicrophone()
                                                                       : m_mic->currentData().toString();
    {
        QSignalBlocker b1(m_source), b2(m_mic);
        m_source->clear();
        bool separatorAdded = false, outputsAdded = false;
        for (const auto &o : m_ctx.recording->computerSources()) {
            if (o.kind == CaptureStream::Kind::Application && !separatorAdded) {
                m_source->insertSeparator(m_source->count());
                separatorAdded = true;
            }
            if (o.kind == CaptureStream::Kind::OutputDevice && !outputsAdded) {
                m_source->insertSeparator(m_source->count());
                outputsAdded = true;
            }
            QString text = o.label;
            if (o.kind == CaptureStream::Kind::OutputDevice)
                text = tr("Output: %1").arg(o.label);
            m_source->addItem(appIcon(o.icon), text, o.key);
            m_source->setItemData(m_source->count() - 1, o.detail, Qt::ToolTipRole);
            m_source->setItemData(m_source->count() - 1, o.detail, Qt::UserRole + 1);
        }
        int i = m_source->findData(keepSource);
        m_source->setCurrentIndex(i >= 0 ? i : 0);
        m_mic->clear();
        for (const auto &o : m_ctx.recording->microphoneSources())
            m_mic->addItem(o.label, o.key);
        i = m_mic->findData(keepMic);
        m_mic->setCurrentIndex(i >= 0 ? i : 0);
    }
    m_mic->setEnabled(m_includeMic->isChecked());
    const bool ok = m_ctx.recording->audioAvailable();
    m_audioProblem->setVisible(!ok);
    m_audioProblem->setText(m_ctx.recording->audioError());
    m_start->setEnabled(ok);
    updateDetail();
    updatePreview();
}

void RecordView::updateDetail()
{
    const QString key = m_source->currentData().toString();
    QString detail = m_source->currentData(Qt::UserRole + 1).toString();
    if (key == "default")
        detail = tr("Everything you hear, from every application. Notification sounds and music playing at the "
                    "same time will be recorded too; choose a single application to avoid that.");
    else if (key.startsWith("app:"))
        detail += (detail.isEmpty() ? QString() : QString(" ")) +
                  tr("Only this application's audio is recorded, even if it restarts its audio during the call.");
    m_sourceDetail->setText(detail);
}

void RecordView::updatePreview()
{
    if (!m_active || m_ctx.recording->recording() || !m_includeMic->isChecked()) {
        m_ctx.recording->stopMicrophonePreview();
        m_previewMeter->setActive(false);
        return;
    }
    const QString key = m_mic->currentData().toString();
    for (const auto &o : m_ctx.recording->microphoneSources())
        if (o.key == key) {
            m_ctx.recording->startMicrophonePreview(o);
            m_previewMeter->setActive(true);
            m_timerTick.start();
            return;
        }
}

void RecordView::activate()
{
    m_active = true;
    reloadSources();
    m_timerTick.start();
}

void RecordView::deactivate()
{
    m_active = false;
    m_ctx.recording->stopMicrophonePreview();
    if (!m_ctx.recording->recording())
        m_timerTick.stop();
}

void RecordView::start()
{
    const QString key = m_source->currentData().toString();
    SourceOption computer;
    bool found = false;
    for (const auto &o : m_ctx.recording->computerSources())
        if (o.key == key) {
            computer = o;
            found = true;
        }
    if (!found) {
        QMessageBox::warning(this, tr("Source not available"),
                             tr("The selected source is no longer available. Choose another one."));
        reloadSources();
        return;
    }
    SourceOption mic;
    bool withMic = false;
    if (m_includeMic->isChecked()) {
        for (const auto &o : m_ctx.recording->microphoneSources())
            if (o.key == m_mic->currentData().toString()) {
                mic = o;
                withMic = true;
            }
    }
    m_ctx.settings->setLastSource(computer.key);
    if (withMic)
        m_ctx.settings->setLastMicrophone(mic.key);
    QString error;
    if (!m_ctx.recording->start(computer, withMic ? &mic : nullptr, &error)) {
        QMessageBox::warning(this, tr("Could not start recording"), error);
        reloadSources();
    }
}

void RecordView::startOrStop()
{
    if (m_ctx.recording->recording())
        m_ctx.recording->stop();
    else if (m_start->isEnabled())
        start();
}

void RecordView::togglePause()
{
    if (!m_ctx.recording->recording())
        return;
    if (m_ctx.recording->paused())
        m_ctx.recording->resume();
    else
        m_ctx.recording->pause();
}

void RecordView::showRecording(bool recording)
{
    m_stack->setCurrentIndex(recording ? 1 : 0);
    if (recording) {
        const MeetingInfo m = m_ctx.store->get(m_ctx.recording->meetingId());
        m_title->setText(m.title);
        m_computerLabel->setText(m.sourceLabel);
        m_micLabel->setText(m.microphoneLabel.isEmpty() ? tr("Microphone off") : m.microphoneLabel);
        m_micMeter->setActive(!m.microphoneLabel.isEmpty());
        m_warning->hide();
        m_pause->setText(tr("Pause"));
        m_pause->setIcon(icon("pause"));
        m_dot->setPaused(false);
        m_state->setText(tr("Recording"));
        m_timerTick.start();
    } else {
        updatePreview();
    }
}

void RecordView::tick()
{
    if (!m_ctx.recording->recording()) {
        m_previewMeter->setLevel(m_ctx.recording->microphonePreviewLevel());
        return;
    }
    const auto st = m_ctx.recording->status();
    m_timer->setText(formatTime(st.seconds));
    auto describe = [](const audio::TrackStatus &t, bool mic) -> QString {
        using S = CaptureStream::State;
        switch (t.state) {
        case S::Active:
            if (!mic && t.linkedStreams > 1)
                return tr("%n audio streams", nullptr, t.linkedStreams);
            return tr("Receiving audio");
        case S::Waiting:
            return mic ? tr("Waiting for the microphone") : tr("Waiting for audio to play");
        case S::Lost:
            return mic ? tr("Disconnected") : tr("Source disconnected");
        case S::Failed:
            return tr("Not available");
        default:
            return {};
        }
    };
    m_computerState->setText(describe(st.computer, false));
    m_computerMeter->setLevel(st.computer.level);
    if (st.microphone.enabled) {
        m_micState->setText(describe(st.microphone, true));
        m_micMeter->setLevel(st.microphone.level);
    } else {
        m_micState->setText(tr("Not recorded"));
    }
}

void RecordView::showWarning(const QString &title, const QString &message)
{
    m_warningTitle->setText(title);
    m_warningText->setText(message);
    m_warning->show();
}

}  // namespace afterline::ui
