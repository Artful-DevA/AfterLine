// SPDX-License-Identifier: MIT
#include "ui/AskPanel.h"

#include "app/MeetingStore.h"
#include "app/Processing.h"
#include "app/Tasks.h"
#include "core/Transcript.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace afterline::ui {

AskPanel::AskPanel(AppContext ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    setObjectName("Sidebar");
    setAttribute(Qt::WA_StyledBackground);
    setMinimumWidth(320);
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(16, 14, 12, 14);
    v->setSpacing(10);
    auto *head = new QHBoxLayout;
    auto *ic = new QLabel;
    ic->setPixmap(icon("ask", Theme::color(Token::Accent)).pixmap(20, 20));
    head->addWidget(ic);
    head->addWidget(label(tr("Ask AfterLine"), "SectionTitle"), 1);
    auto *close = iconButton("close", tr("Close"));
    connect(close, &QPushButton::clicked, this, &AskPanel::closeRequested);
    head->addWidget(close);
    v->addLayout(head);
    m_scope = new QComboBox;
    m_scope->setAccessibleName(tr("Search in"));
    v->addWidget(m_scope);

    m_scroll = new QScrollArea;
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet("QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }");
    auto *threadWidget = new QWidget;
    m_thread = new QVBoxLayout(threadWidget);
    m_thread->setContentsMargins(0, 4, 4, 4);
    m_thread->setSpacing(10);

    // Shown until the first question.
    m_suggestions = new QWidget;
    auto *sv = new QVBoxLayout(m_suggestions);
    sv->setContentsMargins(0, 8, 0, 0);
    sv->setSpacing(8);
    auto *intro = label(tr("Ask anything about your meetings. Answers come only from your transcripts and notes, on "
                           "this computer, with links to the moment it was said."),
                        "Muted");
    sv->addWidget(intro);
    sv->addSpacing(4);
    sv->addWidget(label(tr("TRY"), "Eyebrow"));
    for (const QString &q : {tr("What do I need to do?"), tr("What did we decide?"), tr("What is still undecided?"),
                             tr("Which dates and deadlines came up?")}) {
        auto *chip = new QPushButton(icon("chevron-right", Theme::color(Token::Accent)), q);
        chip->setObjectName("SuggestionChip");
        chip->setCursor(Qt::PointingHandCursor);
        chip->setLayoutDirection(Qt::LeftToRight);
        connect(chip, &QPushButton::clicked, this, [this, q] { ask(q); });
        sv->addWidget(chip);
    }
    m_thread->addWidget(m_suggestions);
    m_thread->addStretch();
    m_scroll->setWidget(threadWidget);
    v->addWidget(m_scroll, 1);

    auto *composer = new QWidget;
    composer->setObjectName("Composer");
    composer->setAttribute(Qt::WA_StyledBackground);
    auto *row = new QHBoxLayout(composer);
    row->setContentsMargins(12, 4, 4, 4);
    m_input = new QLineEdit;
    m_input->setObjectName("ComposerInput");
    m_input->setPlaceholderText(tr("Ask a question…"));
    m_input->setAccessibleName(tr("Question"));
    m_send = new QPushButton(icon("chevron-right", Theme::color(Token::OnAccent)), {});
    m_send->setObjectName("SendButton");
    m_send->setToolTip(tr("Ask (Enter)"));
    m_send->setAccessibleName(tr("Ask"));
    m_send->setCursor(Qt::PointingHandCursor);
    row->addWidget(m_input, 1);
    row->addWidget(m_send);
    v->addWidget(composer);

    m_dots.setInterval(400);
    connect(&m_dots, &QTimer::timeout, this, [this] {
        if (m_thinkingLabel)
            m_thinkingLabel->setText(tr("Looking through your meetings") + QString(".").repeated(1 + m_dotCount++ % 3));
    });
    connect(m_send, &QPushButton::clicked, this, [this] { ask(m_input->text()); });
    connect(m_input, &QLineEdit::returnPressed, this, [this] { ask(m_input->text()); });
    connect(ctx.processing, &Processing::answerReady, this, &AskPanel::onAnswer);
}

void AskPanel::setMeeting(const QString &meetingId, const QString &title)
{
    m_meetingId = meetingId;
    const int keep = m_scope->currentIndex();
    m_scope->clear();
    if (!meetingId.isEmpty())
        m_scope->addItem(icon("notes"), tr("This meeting: %1").arg(title), meetingId);
    m_scope->addItem(icon("folder"), tr("All meetings"), QString());
    m_scope->setCurrentIndex(keep > 0 && m_scope->count() > 1 ? 1 : 0);
}

void AskPanel::focusInput() { m_input->setFocus(); }

void AskPanel::scrollToEnd()
{
    QTimer::singleShot(0, this, [this] { m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->maximum()); });
}

void AskPanel::showSuggestions(bool show) { m_suggestions->setVisible(show); }

QWidget *AskPanel::addBubble(bool mine, const QString &html, const QVector<Source> &sources)
{
    auto *wrap = new QWidget;
    auto *h = new QHBoxLayout(wrap);
    h->setContentsMargins(0, 0, 0, 0);
    auto *bubble = new QWidget;
    bubble->setMinimumWidth(1);
    bubble->setObjectName(mine ? "UserBubble" : "AnswerBubble");
    bubble->setAttribute(Qt::WA_StyledBackground);
    auto *bv = new QVBoxLayout(bubble);
    bv->setContentsMargins(14, 10, 14, 11);
    bv->setSpacing(8);
    if (!mine) {
        auto *who = new QLabel(tr("AFTERLINE"));
        who->setObjectName("Eyebrow");
        bv->addWidget(who);
    }
    auto *text = new QLabel(html);
    text->setWordWrap(true);
    text->setTextFormat(Qt::RichText);
    text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    text->setObjectName(mine ? "UserBubbleText" : "AnswerBubbleText");
    text->setMinimumWidth(1);
    text->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    bv->addWidget(text);
    if (!sources.isEmpty()) {
        auto *flow = new QVBoxLayout;
        flow->setSpacing(4);
        auto *chips = new QHBoxLayout;
        chips->setSpacing(4);
        int inRow = 0;
        for (const auto &s : sources) {
            // In one meeting the time is enough; otherwise a short title too.
            QString title = s.title;
            if (title.size() > 26)
                title = title.left(24).trimmed() + "…";
            const QString text = s.meetingId == m_scope->currentData().toString()
                                     ? tr("Play %1").arg(formatTime(s.start))
                                     : QString("%1 · %2").arg(formatTime(s.start), title);
            auto *chip = new QPushButton(icon("play", Theme::color(Token::Accent)), text);
            chip->setToolTip(tr("Play this moment in “%1”").arg(s.title));
            chip->setObjectName("SourceChip");
            chip->setCursor(Qt::PointingHandCursor);
            const Source src = s;
            connect(chip, &QPushButton::clicked, this, [this, src] { Q_EMIT openMeeting(src.meetingId, src.start); });
            // Short chips share a row; long ones get their own.
            if (inRow == (text.size() > 12 ? 1 : 3) || (text.size() > 12 && inRow > 0)) {
                chips->addStretch();
                flow->addLayout(chips);
                chips = new QHBoxLayout;
                chips->setSpacing(4);
                inRow = 0;
            }
            chips->addWidget(chip);
            ++inRow;
        }
        chips->addStretch();
        flow->addLayout(chips);
        bv->addLayout(flow);
    }
    if (mine) {
        h->addStretch(1);
        h->addWidget(bubble, 6);
    } else {
        h->addWidget(bubble, 9);
        h->addStretch(1);
    }
    m_thread->insertWidget(m_thread->count() - 1, wrap);
    scrollToEnd();
    return wrap;
}

void AskPanel::ask(const QString &rawQuestion)
{
    const QString question = rawQuestion.trimmed();
    if (question.isEmpty() || !m_pending.isEmpty())
        return;
    showSuggestions(false);
    m_input->clear();
    addBubble(true, question.toHtmlEscaped());
    const QString scope = m_scope->currentData().toString();
    const QStringList scopeList = scope.isEmpty() ? QStringList() : QStringList{scope};
    // Questions about tasks are answered from the notes' action items
    // directly: exact, instant, and never confused by what a speaker said.
    if (isTaskQuestion(question)) {
        answerTasks(question, scopeList);
        return;
    }
    m_pending = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString error;
    if (!m_ctx.processing->ask(m_pending, question, scopeList, &error)) {
        m_pending.clear();
        addBubble(false, error.toHtmlEscaped());
        return;
    }
    m_thinking = addBubble(false, tr("Looking through your meetings."));
    m_thinkingLabel = m_thinking->findChild<QLabel *>("AnswerBubbleText");
    m_dotCount = 0;
    m_dots.start();
    m_send->setEnabled(false);
}

void AskPanel::answerTasks(const QString &, const QStringList &scope)
{
    const bool oneMeeting = !scope.isEmpty();
    // In one meeting: everyone's action items. Across meetings: yours, open.
    QVector<TaskItem> tasks = collectTasks(*m_ctx.store, !oneMeeting, scope);
    if (!oneMeeting)
        tasks.erase(std::remove_if(tasks.begin(), tasks.end(), [](const TaskItem &t) { return t.done; }), tasks.end());
    if (tasks.isEmpty()) {
        addBubble(false, oneMeeting ? tr("No action items were agreed in this meeting.")
                                    : tr("Nothing open for you. Action items assigned to you in your meetings' notes "
                                         "appear here and in <b>My tasks</b>."));
        return;
    }
    QString html = oneMeeting           ? tr("Action items from this meeting:")
                   : tasks.size() == 1 ? tr("You have 1 open task:")
                                       : tr("You have %1 open tasks:").arg(tasks.size());
    html += "<ul style='margin-left:-20px'>";
    QVector<Source> sources;
    const QString muted = Theme::hex(Token::Muted);
    for (const auto &t : tasks.mid(0, 12)) {
        QString line = (t.done ? "<s>" : QString()) + t.task.toHtmlEscaped() + (t.done ? "</s>" : QString());
        QStringList meta;
        if (oneMeeting && !t.owner.isEmpty())
            meta << t.owner.toHtmlEscaped();
        if (!t.due.isEmpty())
            meta << tr("due %1").arg(t.due.toHtmlEscaped());
        if (!oneMeeting)
            meta << t.meetingTitle.toHtmlEscaped();
        if (!meta.isEmpty())
            line += QString("<br><span style='color:%1'>%2</span>").arg(muted, meta.join(" · "));
        html += "<li style='margin-bottom:6px'>" + line + "</li>";
        if (t.start >= 0 && sources.size() < 4)
            sources << Source{t.meetingId, t.meetingTitle, t.start};
    }
    html += "</ul>";
    if (tasks.size() > 12)
        html += tr("…and %n more in <b>My tasks</b>.", nullptr, int(tasks.size() - 12));
    addBubble(false, html, sources);
}

void AskPanel::onAnswer(const QString &requestId, const QJsonObject &answer, const QString &error)
{
    if (requestId != m_pending)
        return;
    m_pending.clear();
    m_dots.stop();
    delete m_thinking;
    m_thinking = nullptr;
    m_thinkingLabel = nullptr;
    m_send->setEnabled(true);
    if (!error.isEmpty()) {
        addBubble(false, error.toHtmlEscaped());
        return;
    }
    const QJsonArray passages = m_ctx.processing->passagesOf(requestId);
    QVector<Source> sources;
    for (const auto s : answer["sources"].toArray()) {
        const QJsonObject p = passages.at(s.toInt() - 1).toObject();
        if (p.isEmpty())
            continue;
        const Source src{p["meetingId"].toString(), p["meetingTitle"].toString(), p["start"].toDouble()};
        const bool seen = std::any_of(sources.begin(), sources.end(), [&](const Source &o) {
            return o.meetingId == src.meetingId && std::abs(o.start - src.start) < 1.0;
        });
        if (!seen)
            sources << src;
    }
    addBubble(false, answer["answer"].toString().toHtmlEscaped(), sources);
}

}  // namespace afterline::ui
