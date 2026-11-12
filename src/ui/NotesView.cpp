// SPDX-License-Identifier: MIT
#include "ui/NotesView.h"


#include "app/ModelCatalog.h"
#include "core/Json.h"
#include "core/Notes.h"
#include "ui/Dialogs.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QMimeData>
#include <QTextDocument>
#include <QTimer>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QJsonArray>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScrollBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QTextBlock>
#include <QTextCursor>
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
    m_copy = new QPushButton(icon("copy"), tr("Copy"));
    m_copy->setToolTip(tr("Copy the notes, to paste into an email, a document or a chat"));
    m_edit = new QPushButton(icon("edit"), tr("Edit"));
    m_cancel = new QPushButton(tr("Cancel"));
    m_save = new QPushButton(icon("check", Theme::color(Token::OnAccent)), tr("Save"));
    m_save->setObjectName("Primary");
    for (auto *b : {m_revert, m_copy, m_edit, m_cancel, m_save})
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

    m_view->viewport()->installEventFilter(this);
    m_view->viewport()->setMouseTracking(true);
    connect(m_view, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        const double t = parseTimeLink(url.toString());
        if (t >= 0)
            Q_EMIT seekRequested(t);
    });
    connect(m_emptyAction, &QPushButton::clicked, this, &NotesView::actionRequested);
    connect(m_edit, &QPushButton::clicked, this, &NotesView::edit);
    connect(m_copy, &QPushButton::clicked, this, &NotesView::copy);
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
        m_markdown = notesToMarkdown(n, true, loadDoneTasks(meeting.path("tasks.json")));
        const QJsonObject checks = n.data["checks"].toObject();
        const int removed = checks["unsupportedItemsRemoved"].toInt() + checks["ownersCleared"].toInt() +
                            checks["deadlinesCleared"].toInt();
        info = tr("Written on this computer by %1. Every item links to the moment it was said; check anything "
                  "important against the transcript.")
                   .arg(friendlyModelName(n.data["model"].toString()));
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
    m_copy->setVisible(has);
    m_revert->setVisible(has && meeting.notesEdited() && QFile::exists(meeting.path("notes.json")));
    m_save->hide();
    m_cancel->hide();
}

// Action items are Markdown task-list entries: a click on the box ticks
// the item off (kept in tasks.json, or in your edited notes).
bool NotesView::eventFilter(QObject *o, QEvent *e)
{
    if (o != m_view->viewport() || (e->type() != QEvent::MouseButtonPress && e->type() != QEvent::MouseMove))
        return QWidget::eventFilter(o, e);
    const QPoint pos = static_cast<QMouseEvent *>(e)->position().toPoint();
    const QTextCursor c = m_view->cursorForPosition(pos);
    const QTextBlock block = c.block();
    int index = -1;
    if (block.blockFormat().marker() != QTextBlockFormat::MarkerType::NoMarker) {
        QTextCursor start(block);
        const int textLeft = m_view->cursorRect(start).left();
        const QRect line = m_view->cursorRect(start);
        if (pos.x() < textLeft && pos.x() > textLeft - 34 && pos.y() >= line.top() - 4 && pos.y() <= line.bottom() + 4) {
            index = 0;
            for (QTextBlock b = m_view->document()->begin(); b.isValid() && b != block; b = b.next())
                index += b.blockFormat().marker() != QTextBlockFormat::MarkerType::NoMarker;
        }
    }
    if (e->type() == QEvent::MouseMove) {
        if (index >= 0)
            m_view->viewport()->setCursor(Qt::PointingHandCursor);
        else if (m_view->anchorAt(pos).isEmpty())
            m_view->viewport()->unsetCursor();
        return false;
    }
    if (index < 0)
        return false;
    toggleTask(index);
    return true;
}

void NotesView::toggleTask(int index)
{
    const int scroll = m_view->verticalScrollBar()->value();
    if (m_meeting.notesEdited()) {
        static const QRegularExpression box(R"(^(\s*[-*+] )\[( |x|X)\])", QRegularExpression::MultilineOption);
        auto it = box.globalMatch(m_markdown);
        for (int i = 0; it.hasNext(); ++i) {
            const auto m = it.next();
            if (i != index)
                continue;
            const bool checked = m.captured(2) != " ";
            m_markdown.replace(m.capturedStart(2), 1, checked ? " " : "x");
            QString err;
            writeTextFile(m_meeting.path("notes.md"), m_markdown, &err);
            break;
        }
    } else {
        const QJsonArray items = loadNotes(m_meeting.path("notes.json")).data["action_items"].toArray();
        if (index >= items.size())
            return;
        const QString task = items[index].toObject()["task"].toString();
        QSet<QString> done = loadDoneTasks(m_meeting.path("tasks.json"));
        if (done.contains(task))
            done.remove(task);
        else
            done.insert(task);
        saveDoneTasks(m_meeting.path("tasks.json"), done);
    }
    load(m_meeting, m_emptyText->text(), m_emptyAction->text());
    m_view->verticalScrollBar()->setValue(scroll);
    Q_EMIT tasksChanged();
}

// Copies the notes both as formatted text (for email, Google Docs, Word…)
// and as Markdown (for chats, Obsidian, GitHub…). Timestamps become plain
// "(12:34)" instead of AfterLine's internal links.
void NotesView::copy()
{
    QString markdown = m_markdown;
    if (!m_meeting.notesEdited() && QFile::exists(m_meeting.path("notes.json")))
        markdown = notesToMarkdown(loadNotes(m_meeting.path("notes.json")), false,
                                   loadDoneTasks(m_meeting.path("tasks.json")));
    const QString title = m_meeting.title.trimmed();
    if (!title.isEmpty() && !markdown.startsWith("# "))
        markdown = "# " + title + "\n\n" + markdown;
    QTextDocument doc;
    doc.setMarkdown(markdown);
    auto *mime = new QMimeData;
    mime->setText(markdown);
    mime->setHtml(doc.toHtml());
    QGuiApplication::clipboard()->setMimeData(mime);
    m_copy->setText(tr("Copied"));
    m_copy->setIcon(icon("check", Theme::color(Token::Success)));
    QTimer::singleShot(2000, m_copy, [this] {
        m_copy->setText(tr("Copy"));
        m_copy->setIcon(icon("copy"));
    });
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
        inform(this, tr("Could not save"), err, Tone::Warning);
        return;
    }
    load(m_meeting, m_emptyText->text(), m_emptyAction->text());
    Q_EMIT changed();
}

void NotesView::revert()
{
    if (!confirm(this, tr("Restore generated notes?"),
                 tr("Your edits to these notes will be discarded and the notes AfterLine generated will be shown "
                    "again."),
                 tr("Restore"), Tone::Warning, "rewind"))
        return;
    QFile::remove(m_meeting.path("notes.md"));
    load(m_meeting, m_emptyText->text(), m_emptyAction->text());
    Q_EMIT changed();
}

}  // namespace afterline::ui
