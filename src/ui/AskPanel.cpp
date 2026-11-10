// SPDX-License-Identifier: MIT
#include "ui/AskPanel.h"

#include "app/Processing.h"
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
#include <QScrollBar>
#include <QTextBrowser>
#include <QUrlQuery>
#include <QUuid>
#include <QVBoxLayout>

namespace afterline::ui {

AskPanel::AskPanel(AppContext ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    setObjectName("Sidebar");
    setAttribute(Qt::WA_StyledBackground);
    setMinimumWidth(300);
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(16, 14, 12, 14);
    auto *head = new QHBoxLayout;
    head->addWidget(label(tr("Ask AfterLine"), "SectionTitle"), 1);
    auto *close = iconButton("close", tr("Close"));
    connect(close, &QPushButton::clicked, this, &AskPanel::closeRequested);
    head->addWidget(close);
    v->addLayout(head);
    v->addWidget(label(tr("Answers come only from your transcripts, on this computer, with links to where "
                          "it was said."),
                       "Muted"));
    m_scope = new QComboBox;
    m_scope->setAccessibleName(tr("Search in"));
    v->addWidget(m_scope);
    m_log = new QTextBrowser;
    m_log->setOpenLinks(false);
    m_log->setAccessibleName(tr("Questions and answers"));
    v->addWidget(m_log, 1);
    m_busy = label(tr("Looking through your meetings…"), "Muted");
    m_busy->hide();
    v->addWidget(m_busy);
    auto *row = new QHBoxLayout;
    m_input = new QLineEdit;
    m_input->setPlaceholderText(tr("What did we decide about…?"));
    m_input->setAccessibleName(tr("Question"));
    m_send = new QPushButton(icon("ask", Theme::color(Token::OnAccent)), {});
    m_send->setObjectName("Primary");
    m_send->setToolTip(tr("Ask"));
    m_send->setAccessibleName(tr("Ask"));
    row->addWidget(m_input, 1);
    row->addWidget(m_send);
    v->addLayout(row);

    connect(m_send, &QPushButton::clicked, this, &AskPanel::ask);
    connect(m_input, &QLineEdit::returnPressed, this, &AskPanel::ask);
    connect(ctx.processing, &Processing::answerReady, this, &AskPanel::onAnswer);
    connect(m_log, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        if (url.scheme() != "afterline")
            return;
        const QUrlQuery q(url);
        Q_EMIT openMeeting(q.queryItemValue("meeting"), q.queryItemValue("t").toDouble());
    });
}

void AskPanel::setMeeting(const QString &meetingId, const QString &title)
{
    m_meetingId = meetingId;
    const int keep = m_scope->currentIndex();
    m_scope->clear();
    if (!meetingId.isEmpty())
        m_scope->addItem(tr("This meeting: %1").arg(title), meetingId);
    m_scope->addItem(tr("All meetings"), QString());
    m_scope->setCurrentIndex(keep > 0 && m_scope->count() > 1 ? 1 : 0);
}

void AskPanel::focusInput() { m_input->setFocus(); }

void AskPanel::append(const QString &html)
{
    m_html += html;
    m_log->setHtml(QString("<style>a{color:%1;text-decoration:none} .q{font-weight:600;margin-top:12px} "
                           ".m{color:%2}</style>%3")
                       .arg(Theme::hex(Token::Accent), Theme::hex(Token::Muted), m_html));
    m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
}

void AskPanel::ask()
{
    const QString question = m_input->text().trimmed();
    if (question.isEmpty() || !m_pending.isEmpty())
        return;
    const QString scope = m_scope->currentData().toString();
    m_pending = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString error;
    if (!m_ctx.processing->ask(m_pending, question, scope.isEmpty() ? QStringList() : QStringList{scope}, &error)) {
        m_pending.clear();
        append(QString("<p class='q'>%1</p><p class='m'>%2</p>").arg(question.toHtmlEscaped(), error.toHtmlEscaped()));
        return;
    }
    append(QString("<p class='q'>%1</p>").arg(question.toHtmlEscaped()));
    m_input->clear();
    m_busy->show();
    m_send->setEnabled(false);
}

void AskPanel::onAnswer(const QString &requestId, const QJsonObject &answer, const QString &error)
{
    if (requestId != m_pending)
        return;
    m_pending.clear();
    m_busy->hide();
    m_send->setEnabled(true);
    if (!error.isEmpty()) {
        append(QString("<p class='m'>%1</p>").arg(error.toHtmlEscaped()));
        return;
    }
    QString html = QString("<p>%1</p>").arg(answer["answer"].toString().toHtmlEscaped());
    const QJsonArray passages = m_ctx.processing->passagesOf(requestId);
    QStringList links;
    for (const auto s : answer["sources"].toArray()) {
        const QJsonObject p = passages.at(s.toInt() - 1).toObject();
        if (p.isEmpty())
            continue;
        QUrl url;
        url.setScheme("afterline");
        url.setPath("open");
        QUrlQuery q;
        q.addQueryItem("meeting", p["meetingId"].toString());
        q.addQueryItem("t", QString::number(p["start"].toDouble()));
        url.setQuery(q);
        links << QString("<a href='%1'>%2 · %3</a>")
                     .arg(url.toString(), p["meetingTitle"].toString().toHtmlEscaped(), formatTime(p["start"].toDouble()));
    }
    if (!links.isEmpty())
        html += QString("<p class='m'>%1 %2</p>").arg(tr("Sources:"), links.join(" &nbsp; "));
    else if (!answer["found"].toBool())
        html = QString("<p class='m'>%1</p>").arg(answer["answer"].toString().toHtmlEscaped());
    append(html);
}

}  // namespace afterline::ui
