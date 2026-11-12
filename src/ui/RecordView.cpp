// SPDX-License-Identifier: MIT
#include "ui/RecordView.h"

#include "app/LiveTranscriber.h"
#include "app/MeetingStore.h"
#include "app/Settings.h"
#include "core/Transcript.h"
#include "ui/Dialogs.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextBrowser>
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
    outer->setContentsMargins(36, 28, 36, 28);
    m_stack = new QStackedWidget;
    // The setup page is a centred column; the recording page uses the width.
    auto *idleHolder = new QWidget;
    auto *ih = new QHBoxLayout(idleHolder);
    ih->setContentsMargins(0, 0, 0, 0);
    auto *idle = buildIdle();
    idle->setMaximumWidth(660);
    ih->addStretch(1);
    ih->addWidget(idle, 6);
    ih->addStretch(1);
    auto *idleScroll = new QScrollArea;
    idleScroll->setWidget(idleHolder);
    idleScroll->setWidgetResizable(true);
    idleScroll->setFrameShape(QFrame::NoFrame);
    idleScroll->setStyleSheet("QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }");
    m_stack->addWidget(idleScroll);
    m_stack->addWidget(buildRecording());
    outer->addWidget(m_stack);

    m_timerTick.setInterval(100);
    connect(&m_timerTick, &QTimer::timeout, this, &RecordView::tick);
    m_testTimeout.setSingleShot(true);
    m_testTimeout.setInterval(30000);
    connect(&m_testTimeout, &QTimer::timeout, this, [this] { setTesting(false); });
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
        m_state->setText(paused ? tr("PAUSED · NOTHING IS BEING RECORDED") : tr("RECORDING"));
    });
    connect(ctx.live, &LiveTranscriber::lineAdded, this, [this](const LiveLine &l) {
        if (l.source == m_partialSource)
            m_partial.clear();
        renderLive();
    });
    connect(ctx.live, &LiveTranscriber::linesChanged, this, &RecordView::renderLive);
    connect(ctx.live, &LiveTranscriber::partial, this, [this](const QString &source, const QString &text) {
        m_partial = text;
        m_partialSource = source;
        renderLive();
    });
    connect(ctx.live, &LiveTranscriber::stateChanged, this, &RecordView::updateLiveState);
    reloadSources();
    showRecording(ctx.recording->recording());
}

namespace {
// A section header inside a card: tinted icon tile, title and a muted line.
QWidget *sectionHeader(const QString &iconName, const QString &title, const QString &subtitle, QWidget *trailing = nullptr)
{
    auto *w = new QWidget;
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(12);
    auto *tile = new QLabel;
    tile->setFixedSize(38, 38);
    tile->setAlignment(Qt::AlignCenter);
    tile->setPixmap(icon(iconName, Theme::color(Token::Accent)).pixmap(20, 20));
    tile->setStyleSheet(QString("background: %1; border-radius: 10px;").arg(Theme::hex(Token::AccentSoft)));
    h->addWidget(tile, 0, Qt::AlignTop);
    auto *t = new QVBoxLayout;
    t->setSpacing(1);
    t->addWidget(label(title, "SectionTitle"));
    t->addWidget(label(subtitle, "Muted"));
    h->addLayout(t, 1);
    if (trailing)
        h->addWidget(trailing, 0, Qt::AlignVCenter);
    return w;
}
}  // namespace

QWidget *RecordView::buildIdle()
{
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 12, 0, 12);
    v->setSpacing(14);
    v->addStretch(1);
    v->addWidget(label(tr("NEW RECORDING"), "Eyebrow"));
    v->addWidget(label(tr("What should AfterLine record?"), "PageTitle"));
    v->addWidget(label(tr("Everything stays on this computer: the audio, the transcript and the notes."), "Muted"));
    v->addSpacing(4);

    m_audioProblem = label({}, "ErrorText");
    m_audioProblem->hide();
    v->addWidget(m_audioProblem);

    // Call audio
    auto *c = card();
    auto *g = new QVBoxLayout(c);
    g->setContentsMargins(20, 18, 20, 18);
    g->setSpacing(12);
    g->addWidget(sectionHeader("computer", tr("Call audio"), tr("What the other people in the meeting say.")));
    auto *row = new QHBoxLayout;
    row->setSpacing(8);
    m_source = new QComboBox;
    m_source->setIconSize({20, 20});
    m_source->setItemDelegate(new TwoLineDelegate(m_source));
    m_source->setAccessibleName(tr("Record audio from"));
    m_source->setMinimumContentsLength(20);
    m_source->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_source->setMinimumHeight(40);
    m_source->setMaxVisibleItems(12);
    row->addWidget(m_source, 1);
    auto *refresh = iconButton("refresh", tr("Look for applications and devices again"));
    refresh->setFixedSize(40, 40);
    connect(refresh, &QPushButton::clicked, this, &RecordView::reloadSources);
    row->addWidget(refresh);
    g->addLayout(row);
    m_sourceDetail = label({}, "Muted");
    g->addWidget(m_sourceDetail);
    v->addWidget(c);

    // Microphone
    auto *mc = card();
    auto *mg = new QVBoxLayout(mc);
    mg->setContentsMargins(20, 18, 20, 18);
    mg->setSpacing(12);
    m_includeMic = new ToggleSwitch({});
    m_includeMic->setAccessibleName(tr("Include my microphone"));
    m_includeMic->setChecked(m_ctx.settings->includeMicrophone());
    m_includeMic->setFixedWidth(44);
    mg->addWidget(sectionHeader("mic", tr("Your microphone"),
                                tr("Recorded on its own track, so the transcript can tell your words from the "
                                   "call's. When this is off, AfterLine never opens the microphone."),
                                m_includeMic));
    m_mic = new QComboBox;
    m_mic->setAccessibleName(tr("Microphone"));
    m_mic->setItemDelegate(new TwoLineDelegate(m_mic));
    m_mic->setMinimumHeight(40);
    mg->addWidget(m_mic);
    auto *meterRow = new QHBoxLayout;
    meterRow->setSpacing(12);
    m_test = new QPushButton(icon("mic"), tr("Test"));
    m_test->setToolTip(tr("Listen to the microphone for a moment, to check that it works"));
    m_test->setCursor(Qt::PointingHandCursor);
    meterRow->addWidget(m_test);
    m_previewMeter = new LevelMeter;
    m_previewMeter->setToolTip(tr("Speak to check that the microphone works"));
    m_previewMeter->setActive(false);
    meterRow->addWidget(m_previewMeter, 1);
    mg->addLayout(meterRow);
    v->addWidget(mc);

    // Live transcript
    auto *lc = card();
    auto *lg = new QVBoxLayout(lc);
    lg->setContentsMargins(20, 16, 20, 16);
    m_liveToggle = new ToggleSwitch({});
    m_liveToggle->setAccessibleName(tr("Show a live transcript while recording"));
    m_liveToggle->setChecked(m_ctx.settings->liveTranscript());
    m_liveToggle->setFixedWidth(44);
    lg->addWidget(sectionHeader("transcript", tr("Live transcript"),
                                tr("See what is said while you record. A careful transcript and notes are made "
                                   "when you stop."),
                                m_liveToggle));
    v->addWidget(lc);

    m_start = new QPushButton(icon("record", Theme::color(Token::OnAccent)), tr("Start recording"));
    m_start->setObjectName("RecordButton");
    m_start->setIconSize({18, 18});
    m_start->setCursor(Qt::PointingHandCursor);
    m_start->setToolTip(tr("Start recording (Ctrl+R)"));
    auto *startRow = new QHBoxLayout;
    startRow->addStretch();
    startRow->addWidget(m_start);
    startRow->addStretch();
    v->addSpacing(8);
    v->addLayout(startRow);
    auto *consent = label(tr("Make sure everyone in the conversation knows it is being recorded, and has agreed "
                             "where the law requires it."),
                          "Muted");
    consent->setAlignment(Qt::AlignCenter);
    v->addWidget(consent);
    v->addStretch(2);

    connect(m_start, &QPushButton::clicked, this, &RecordView::start);
    connect(m_source, &QComboBox::currentIndexChanged, this, &RecordView::updateDetail);
    connect(m_includeMic, &QCheckBox::toggled, this, [this](bool on) {
        m_ctx.settings->setIncludeMicrophone(on);
        m_mic->setEnabled(on);
        m_test->setEnabled(on);
        if (!on)
            setTesting(false);
    });
    connect(m_liveToggle, &QCheckBox::toggled, this, [this](bool on) { m_ctx.settings->setLiveTranscript(on); });
    connect(m_test, &QPushButton::clicked, this, [this] { setTesting(!m_testing); });
    connect(m_mic, &QComboBox::currentIndexChanged, this, &RecordView::updatePreview);
    return page;
}

QWidget *RecordView::buildRecording()
{
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(16);

    // Header: status, title, controls.
    auto *top = new QHBoxLayout;
    top->setSpacing(10);
    m_dot = new RecordingDot;
    top->addWidget(m_dot);
    m_state = label(tr("RECORDING"), "Eyebrow");
    m_state->setAccessibleName(tr("Recording status"));
    top->addWidget(m_state, 1);
    m_pause = new QPushButton(icon("pause"), tr("Pause"));
    m_pause->setObjectName("PauseButton");
    m_pause->setToolTip(tr("Pause (Ctrl+Shift+P)"));
    m_stop = new QPushButton(icon("stop", QColor(Qt::white)), tr("Stop and save"));
    m_stop->setObjectName("StopButton");
    m_stop->setToolTip(tr("Stop and save (Ctrl+R)"));
    for (auto *b : {m_pause, m_stop})
        b->setCursor(Qt::PointingHandCursor);
    top->addWidget(m_pause);
    top->addWidget(m_stop);
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

    auto *columns = new QHBoxLayout;
    columns->setSpacing(16);

    // Left: time and the two tracks.
    auto *left = new QVBoxLayout;
    left->setSpacing(12);
    auto *c = card();
    // A fixed width: status texts of different lengths must not shift the
    // layout or squeeze the device names.
    c->setFixedWidth(330);
    auto *g = new QGridLayout(c);
    g->setContentsMargins(20, 16, 20, 20);
    g->setHorizontalSpacing(10);
    g->setVerticalSpacing(6);
    m_timer = label("0:00", "Timer");
    m_timer->setAccessibleName(tr("Recording duration"));
    g->addWidget(m_timer, 0, 0, 1, 3);
    g->addWidget(hline(), 1, 0, 1, 3);
    g->setRowMinimumHeight(1, 14);
    // Each track: icon, name, and its state on the line below; then the meter.
    auto addTrack = [&](int row, const QString &iconName, QLabel *&name, QLabel *&state, LevelMeter *&meter) {
        auto *ic = new QLabel;
        ic->setPixmap(icon(iconName, Theme::color(Token::Muted)).pixmap(18, 18));
        g->addWidget(ic, row, 0, Qt::AlignTop);
        auto *text = new QVBoxLayout;
        text->setSpacing(0);
        name = new QLabel;
        QFont f = name->font();
        f.setWeight(QFont::DemiBold);
        name->setFont(f);
        name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        text->addWidget(name);
        state = label({}, "Muted");
        state->setWordWrap(false);
        state->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        text->addWidget(state);
        g->addLayout(text, row, 1, 1, 2);
        meter = new LevelMeter;
        g->addWidget(meter, row + 1, 0, 1, 3);
        g->setRowMinimumHeight(row + 2, 10);
    };
    addTrack(2, "computer", m_computerLabel, m_computerState, m_computerMeter);
    addTrack(5, "mic", m_micLabel, m_micState, m_micMeter);
    m_micRow = c;
    g->setColumnStretch(1, 1);
    left->addWidget(c);

    m_warning = new QWidget;
    m_warning->setObjectName("WarningBanner");
    m_warning->setAttribute(Qt::WA_StyledBackground);
    m_warning->setFixedWidth(330);
    auto *wl = new QHBoxLayout(m_warning);
    wl->setContentsMargins(14, 12, 8, 12);
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
    left->addWidget(m_warning);
    auto *safe = label(tr("Audio is written to disk continuously. Closing the window keeps recording in the "
                          "background."),
                       "Muted");
    safe->setFixedWidth(330);
    left->addWidget(safe);
    left->addStretch();
    columns->addLayout(left);

    // Right: live transcript.
    auto *lc = card();
    auto *lv = new QVBoxLayout(lc);
    lv->setContentsMargins(20, 16, 8, 12);
    lv->setSpacing(8);
    auto *lh = new QHBoxLayout;
    lh->addWidget(label(tr("Live transcript"), "SectionTitle"));
    auto *pill = new QLabel(tr("PRELIMINARY"));
    pill->setObjectName("Pill");
    pill->setToolTip(tr("Written quickly while you record. The final transcript is made from the whole recording "
                        "when you stop, and is more accurate."));
    lh->addWidget(pill);
    lh->addStretch();
    lh->addSpacing(12);
    lv->addLayout(lh);
    m_live = new QTextBrowser;
    m_live->setOpenLinks(false);
    m_live->setFrameShape(QFrame::NoFrame);
    m_live->setStyleSheet("QTextBrowser { background: transparent; }");
    lv->addWidget(m_live, 1);
    m_liveStatus = label({}, "Muted");
    lv->addWidget(m_liveStatus);
    columns->addWidget(lc, 1);
    v->addLayout(columns, 1);

    connect(m_pause, &QPushButton::clicked, this, &RecordView::togglePause);
    connect(m_stop, &QPushButton::clicked, this, [this] { m_ctx.recording->stop(); });
    return page;
}

void RecordView::updateLiveState()
{
    const QString why = m_ctx.live->unavailableReason();
    if (m_ctx.live->ready())
        m_liveStatus->setText(m_ctx.live->lines().isEmpty() ? tr("Listening. Sentences appear a few seconds after "
                                                                 "they are spoken.")
                                                            : QString());
    else if (m_ctx.live->running())
        m_liveStatus->setText(tr("Loading the speech recognition model…"));
    else if (!why.isEmpty())
        m_liveStatus->setText(why);
    else
        m_liveStatus->setText(tr("Live transcript stopped. The full transcript is made when you stop recording."));
    m_liveStatus->setVisible(!m_liveStatus->text().isEmpty());
}

void RecordView::renderLive()
{
    auto *bar = m_live->verticalScrollBar();
    const bool atEnd = bar->value() >= bar->maximum() - 8;
    const QString muted = Theme::hex(Token::Muted);
    QString html = "<style>p { margin: 0 0 12px 0; line-height: 140%; }</style>";
    // Consecutive sentences from the same side form one paragraph.
    const auto &lines = m_ctx.live->lines();
    for (int i = 0; i < lines.size(); ++i) {
        const LiveLine &l = lines[i];
        const bool mic = l.source == "microphone";
        const QString who = speakerName(l.source);
        const QString color = Theme::hex(mic ? Token::TrackMicrophone : Token::TrackComputer);
        QString text = l.text.toHtmlEscaped();
        while (i + 1 < lines.size() && lines[i + 1].source == l.source && lines[i + 1].start - lines[i].end < 4.0)
            text += " " + lines[++i].text.toHtmlEscaped();
        html += QString("<p><span style='color:%1; font-weight:600'>%2</span>"
                        "&nbsp;&nbsp;<span style='color:%3'>%4</span><br>%5</p>")
                    .arg(color, who.toHtmlEscaped(), muted, formatTime(l.start), text);
    }
    if (!m_partial.isEmpty())
        html += QString("<p style='color:%1'><i>%2…</i></p>").arg(muted, m_partial.toHtmlEscaped());
    m_live->setHtml(html);
    if (atEnd)
        bar->setValue(bar->maximum());
    updateLiveState();
}

void RecordView::setTesting(bool on)
{
    m_testing = on && m_includeMic->isChecked();
    m_test->setText(m_testing ? tr("Stop test") : tr("Test"));
    if (m_testing)
        m_testTimeout.start();
    else
        m_testTimeout.stop();
    updatePreview();
}

void RecordView::preselectSource(const QString &key)
{
    m_preselect = key;
    reloadSources();
}

void RecordView::reloadSources()
{
    QString keepSource = m_source->currentData().toString().isEmpty() ? m_ctx.settings->lastSource()
                                                                       : m_source->currentData().toString();
    if (!m_preselect.isEmpty()) {
        keepSource = m_preselect;
        m_preselect.clear();
    }
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
        for (const auto &o : m_ctx.recording->microphoneSources()) {
            m_mic->addItem(icon("mic"), o.label, o.key);
            if (o.detail != o.label)
                m_mic->setItemData(m_mic->count() - 1, o.detail, Qt::UserRole + 1);
        }
        i = m_mic->findData(keepMic);
        m_mic->setCurrentIndex(i >= 0 ? i : 0);
    }
    m_mic->setEnabled(m_includeMic->isChecked());
    m_test->setEnabled(m_includeMic->isChecked());
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
    if (!m_active || !m_testing || m_ctx.recording->recording() || !m_includeMic->isChecked()) {
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
    m_testing = false;
    m_test->setText(tr("Test"));
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
        inform(this, tr("Source not available"), tr("The selected source is no longer available. Choose another one."),
               Tone::Warning);
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
        inform(this, tr("Could not start recording"), error, Tone::Warning);
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
        m_state->setText(tr("RECORDING"));
        m_timerTick.start();
        m_partial.clear();
        renderLive();
    } else {
        m_testing = false;
        m_test->setText(tr("Test"));
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
