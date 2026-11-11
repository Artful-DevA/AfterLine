// SPDX-License-Identifier: MIT
#include "ui/NotesView.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextList>

#include "core/Json.h"
#include "core/Notes.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QFile>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace afterline::ui {

NotesView::NotesView(QWidget *parent) : QWidget(parent)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 8, 0, 0);
    v->setSpacing(6);
    auto *bar = new QHBoxLayout;
    m_info = label({}, "Muted");
    bar->addWidget(m_info, 1);
    m_revert = new QPushButton(tr("Restore generated notes"));
    m_edit = new QPushButton(icon("edit"), tr("Edit"));
    m_cancel = new QPushButton(tr("Cancel"));
    m_save = new QPushButton(icon("check", Theme::color(Token::OnAccent)), tr("Save"));
    m_save->setObjectName("Primary");
    for (auto *b : {m_revert, m_edit, m_cancel, m_save})
        bar->addWidget(b);
    v->addLayout(bar);

    m_stack = new QStackedWidget;
    m_view = new QTextBrowser;
    m_view->setOpenLinks(false);
    m_view->setAccessibleName(tr("Meeting notes"));
    m_view->document()->setDocumentMargin(18);
    m_editor = new QPlainTextEdit;
    m_editor->setAccessibleName(tr("Edit notes (Markdown)"));
    QFont mono("monospace");
    mono.setStyleHint(QFont::Monospace);
    m_editor->setFont(mono);

    auto *empty = new QWidget;
    auto *ev = new QVBoxLayout(empty);
    ev->addStretch();
    auto *ic = new QLabel;
    ic->setPixmap(icon("notes", Theme::color(Token::Muted)).pixmap(40, 40));
    ic->setAlignment(Qt::AlignCenter);
    ev->addWidget(ic);
    m_emptyText = label({}, "Muted");
    m_emptyText->setAlignment(Qt::AlignCenter);
    ev->addWidget(m_emptyText);
    m_emptyAction = new QPushButton;
    m_emptyAction->setObjectName("Primary");
    auto *row = new QHBoxLayout;
    row->addStretch();
    row->addWidget(m_emptyAction);
    row->addStretch();
    ev->addLayout(row);
    ev->addStretch(2);

    m_stack->addWidget(m_view);
    m_stack->addWidget(m_editor);
    m_stack->addWidget(empty);
    v->addWidget(m_stack, 1);

    connect(m_view, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        const double t = parseTimeLink(url.toString());
        if (t >= 0)
            Q_EMIT seekRequested(t);
    });
    connect(m_emptyAction, &QPushButton::clicked, this, &NotesView::actionRequested);
    connect(m_edit, &QPushButton::clicked, this, &NotesView::edit);
    connect(m_save, &QPushButton::clicked, this, &NotesView::save);
    connect(m_cancel, &QPushButton::clicked, this, [this] { load(m_meeting, m_emptyText->text(), m_emptyAction->text()); });
    connect(m_revert, &QPushButton::clicked, this, &NotesView::revert);
}

void NotesView::load(const MeetingInfo &meeting, const QString &emptyMessage, const QString &actionText)
{
    m_meeting = meeting;
    m_markdown.clear();
    QString info;
    QFile md(meeting.path("notes.md"));
    if (md.open(QIODevice::ReadOnly)) {
        m_markdown = QString::fromUtf8(md.readAll());
        info = tr("Edited by you.");
    } else if (QFile::exists(meeting.path("notes.json"))) {
        const MeetingNotes n = loadNotes(meeting.path("notes.json"));
        m_markdown = notesToMarkdown(n);
        const QJsonObject checks = n.data["checks"].toObject();
        const int removed = checks["unsupportedItemsRemoved"].toInt() + checks["ownersCleared"].toInt() +
                            checks["deadlinesCleared"].toInt();
        info = tr("Written on this computer by %1. Every item links to the moment it was said; check anything "
                  "important against the transcript.")
                   .arg(n.data["model"].toString());
        if (removed > 0)
            info += " " + tr("%n detail(s) the transcript did not support were left out.", nullptr, removed);
    }
    m_info->setText(info);
    const bool has = !m_markdown.isEmpty();
    if (has) {
        m_view->document()->setDefaultStyleSheet(
            QString("a { color: %1; text-decoration: none; } h1, h2 { margin-top: 22px; margin-bottom: 8px; } "
                    "p { line-height: 145%; } li { margin-bottom: 6px; }")
                .arg(Theme::hex(Token::Accent)));
        m_view->document()->setDocumentMargin(20);
        m_view->setMarkdown(m_markdown);
        // Markdown import sets its own block formats: give headings and list
        // items room to breathe.
        QTextCursor c(m_view->document());
        c.beginEditBlock();
        for (QTextBlock b = m_view->document()->begin(); b.isValid(); b = b.next()) {
            QTextBlockFormat f = b.blockFormat();
            if (f.headingLevel() > 0) {
                f.setTopMargin(b == m_view->document()->begin() ? 0 : 20);
                f.setBottomMargin(6);
            } else {
                f.setLineHeight(b.textList() ? 118 : 135, QTextBlockFormat::ProportionalHeight);
                f.setBottomMargin(b.textList() ? 3 : 8);
            }
            c.setPosition(b.position());
            c.setBlockFormat(f);
        }
        c.endEditBlock();
        m_stack->setCurrentIndex(0);
    } else {
        m_emptyText->setText(emptyMessage);
        m_emptyAction->setText(actionText);
        m_emptyAction->setVisible(!actionText.isEmpty());
        m_stack->setCurrentIndex(2);
    }
    m_edit->setVisible(has);
    m_revert->setVisible(has && meeting.notesEdited() && QFile::exists(meeting.path("notes.json")));
    m_save->hide();
    m_cancel->hide();
}

void NotesView::edit()
{
    // Edit the version without internal links: plain timestamps read better.
    QString text = m_markdown;
    if (!m_meeting.notesEdited() && QFile::exists(m_meeting.path("notes.json")))
        text = notesToMarkdown(loadNotes(m_meeting.path("notes.json")), false);
    m_editor->setPlainText(text);
    m_stack->setCurrentIndex(1);
    m_editor->setFocus();
    m_edit->hide();
    m_revert->hide();
    m_save->show();
    m_cancel->show();
}

void NotesView::save()
{
    QString err;
    if (!writeTextFile(m_meeting.path("notes.md"), m_editor->toPlainText(), &err)) {
        QMessageBox::warning(this, tr("Could not save"), err);
        return;
    }
    load(m_meeting, m_emptyText->text(), m_emptyAction->text());
    Q_EMIT changed();
}

void NotesView::revert()
{
    if (QMessageBox::question(this, tr("Restore generated notes?"),
                              tr("Your edits to these notes will be discarded and the notes AfterLine generated "
                                 "will be shown again.")) != QMessageBox::Yes)
        return;
    QFile::remove(m_meeting.path("notes.md"));
    load(m_meeting, m_emptyText->text(), m_emptyAction->text());
    Q_EMIT changed();
}

}  // namespace afterline::ui
