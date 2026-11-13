// SPDX-License-Identifier: MIT
#include "ui/TasksView.h"

#include "app/MeetingStore.h"
#include "app/Tasks.h"
#include "ui/Dialogs.h"
#include "core/Transcript.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace afterline::ui {

namespace {

// A round tick box.
class TickBox : public QAbstractButton {
public:
    explicit TickBox(QWidget *parent = nullptr) : QAbstractButton(parent)
    {
        setCheckable(true);
        setFixedSize(24, 24);
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r(2, 2, 20, 20);
        if (isChecked()) {
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::color(Token::Success));
            p.drawEllipse(r);
            ui::icon("check", QColor(Qt::white)).paint(&p, r.adjusted(3, 3, -3, -3).toRect());
        } else {
            p.setPen(QPen(underMouse() ? Theme::color(Token::Success) : Theme::color(Token::Muted), 1.6));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(r.adjusted(1, 1, -1, -1));
        }
    }
    void enterEvent(QEnterEvent *) override { update(); }
    void leaveEvent(QEvent *) override { update(); }
};

class TaskRow : public QWidget {
public:
    std::function<void()> onOpen;

protected:
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && onOpen)
            onOpen();
    }
    void enterEvent(QEnterEvent *) override { setProperty("hover", true); style()->polish(this); }
    void leaveEvent(QEvent *) override { setProperty("hover", false); style()->polish(this); }
};

}  // namespace

TasksView::TasksView(AppContext ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }");
    outer->addWidget(scroll);
    auto *page = new QWidget;
    scroll->setWidget(page);
    auto *center = new QHBoxLayout(page);
    auto *content = new QWidget;
    content->setMaximumWidth(760);
    center->addStretch();
    center->addWidget(content, 10);
    center->addStretch();
    auto *v = new QVBoxLayout(content);
    v->setContentsMargins(24, 28, 24, 40);
    v->setSpacing(12);
    v->addWidget(label(tr("FROM YOUR MEETINGS"), "Eyebrow"));
    auto *head = new QHBoxLayout;
    head->addWidget(label(tr("My tasks"), "PageTitle"), 1);
    m_everyone = new ToggleSwitch(tr("Everyone's tasks"));
    m_everyone->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    m_everyone->setMinimumWidth(QFontMetrics(QFont(font().family(), 11)).horizontalAdvance(m_everyone->text()) + 70);
    head->addWidget(m_everyone, 0, Qt::AlignVCenter);
    v->addLayout(head);
    m_summary = label({}, "Muted");
    v->addWidget(m_summary);
    v->addSpacing(6);
    m_list = new QVBoxLayout;
    m_list->setSpacing(8);
    v->addLayout(m_list);
    v->addStretch();
    connect(m_everyone, &QCheckBox::toggled, this, &TasksView::reload);
}

void TasksView::reload()
{
    while (QLayoutItem *item = m_list->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    const bool everyone = m_everyone->isChecked();
    const QVector<TaskItem> tasks = collectTasks(*m_ctx.store, !everyone);
    int open = 0;
    for (const auto &t : tasks)
        open += !t.done;
    m_summary->setText(tasks.isEmpty()
                           ? QString()
                           : everyone ? (open == 1 ? tr("1 open action item across your meetings.")
                                                   : tr("%1 open action items across your meetings.").arg(open))
                                      : (open == 0   ? tr("All done. Click a task to hear the moment it was agreed.")
                                         : open == 1 ? tr("1 open task. Tick it off when it's done; click it to hear "
                                                          "the moment it was agreed.")
                                                     : tr("%1 open tasks. Tick them off as you go; click one to hear "
                                                          "the moment it was agreed.")
                                                           .arg(open)));
    if (tasks.isEmpty()) {
        auto *empty = new QWidget;
        auto *ev = new QVBoxLayout(empty);
        ev->setContentsMargins(0, 40, 0, 0);
        auto *ic = new QLabel;
        ic->setPixmap(icon("tasks", Theme::color(Token::Muted)).pixmap(44, 44));
        ic->setAlignment(Qt::AlignCenter);
        ev->addWidget(ic);
        auto *t = label(everyone ? tr("No action items yet.") : tr("Nothing on your list."), "SectionTitle");
        t->setAlignment(Qt::AlignCenter);
        ev->addWidget(t);
        auto *d = label(tr("When someone says you'll do something in a meeting (“I'll send the slides by Friday”), "
                           "it shows up here once the notes are written."),
                        "Muted");
        d->setAlignment(Qt::AlignCenter);
        ev->addWidget(d);
        m_list->addWidget(empty);
        return;
    }
    bool doneHeader = false;
    // Open tasks first (newest meeting first), then the ones you ticked off.
    QVector<TaskItem> ordered;
    for (const auto &t : tasks)
        if (!t.done)
            ordered << t;
    for (const auto &t : tasks)
        if (t.done)
            ordered << t;
    for (const TaskItem &t : ordered) {
        if (t.done && !doneHeader) {
            auto *h = label(tr("DONE"), "Eyebrow");
            h->setContentsMargins(4, 14, 0, 2);
            m_list->addWidget(h);
            doneHeader = true;
        }
        auto *row = new TaskRow;
        row->setObjectName("TaskRow");
        row->setAttribute(Qt::WA_StyledBackground);
        row->setCursor(Qt::PointingHandCursor);
        row->setToolTip(tr("Open “%1” at %2").arg(t.meetingTitle, formatTime(std::max(0.0, t.start))));
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(14, 12, 16, 12);
        h->setSpacing(12);
        auto *tick = new TickBox;
        tick->setChecked(t.done);
        tick->setAccessibleName(tr("Done: %1").arg(t.task));
        h->addWidget(tick, 0, Qt::AlignTop);
        auto *tv = new QVBoxLayout;
        tv->setSpacing(3);
        auto *text = new QLabel(t.done ? QString("<s>%1</s>").arg(t.task.toHtmlEscaped()) : t.task.toHtmlEscaped());
        text->setWordWrap(true);
        if (t.done)
            text->setObjectName("Muted");
        QFont f = text->font();
        f.setPointSizeF(f.pointSizeF() * 1.05);
        text->setFont(f);
        tv->addWidget(text);
        QStringList meta;
        if (everyone && !t.owner.isEmpty())
            meta << (t.mine ? tr("You") : t.owner);
        meta << t.meetingTitle << QLocale().toString(t.meetingDate.date(), "d MMM");
        auto *m = label(meta.join("  ·  "), "Muted");
        tv->addWidget(m);
        h->addLayout(tv, 1);
        if (!t.due.isEmpty()) {
            auto *due = new QLabel(tr("Due %1").arg(t.due));
            due->setObjectName("Pill");
            h->addWidget(due, 0, Qt::AlignTop);
        }
        auto *play = new QLabel;
        play->setPixmap(icon("play", Theme::color(Token::Accent)).pixmap(14, 14));
        h->addWidget(play, 0, Qt::AlignTop);
        auto *remove = new QPushButton(icon("close", Theme::color(Token::Muted)), {});
        remove->setObjectName("TaskDelete");
        remove->setFlat(true);
        remove->setFixedSize(26, 26);
        remove->setIconSize(QSize(14, 14));
        remove->setCursor(Qt::PointingHandCursor);
        remove->setToolTip(tr("Delete this task"));
        remove->setAccessibleName(tr("Delete: %1").arg(t.task));
        h->addWidget(remove, 0, Qt::AlignTop);
        const QString meetingId = t.meetingId, task = t.task;
        connect(remove, &QPushButton::clicked, this, [this, meetingId, task] {
            if (!confirm(this, tr("Delete this task?"),
                         tr("“%1” is removed from My tasks and from the meeting's notes.").arg(task), tr("Delete"),
                         Tone::Danger, "trash"))
                return;
            deleteTask(*m_ctx.store, meetingId, task);
            Q_EMIT changed();
            reload();
        });
        const double start = t.start;
        row->onOpen = [this, meetingId, start] { Q_EMIT openMeeting(meetingId, start); };
        connect(tick, &QAbstractButton::toggled, this, [this, meetingId, task](bool on) {
            setTaskDone(*m_ctx.store, meetingId, task, on);
            Q_EMIT changed();
            // Let the tick show before the task moves.
            QTimer::singleShot(350, this, &TasksView::reload);
        });
        m_list->addWidget(row);
    }
}

}  // namespace afterline::ui
