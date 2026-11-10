// SPDX-License-Identifier: MIT
#include "ui/TranscriptView.h"

#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace afterline::ui {

int TranscriptModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_t.segments.size());
}

Qt::ItemFlags TranscriptModel::flags(const QModelIndex &index) const
{
    return QAbstractListModel::flags(index) | Qt::ItemIsEditable;
}

QVariant TranscriptModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};
    const auto &s = m_t.segments[index.row()];
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole: return s.text;
    case Qt::AccessibleTextRole: return formatTime(s.start) + ", " + speakerName(s.source) + ": " + s.text;
    case StartRole: return s.start;
    case SourceRole: return s.source;
    case ConfidenceRole: return s.confidence;
    case EditedRole: return s.edited;
    case CurrentRole: return index.row() == m_current;
    case MatchRole: return !m_query.isEmpty() && s.text.contains(m_query, Qt::CaseInsensitive);
    default: return {};
    }
}

bool TranscriptModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole || !index.isValid())
        return false;
    const QString text = value.toString().simplified();
    if (text.isEmpty() || !m_t.setText(m_t.segments[index.row()].id, text))
        return false;
    Q_EMIT dataChanged(index, index);
    Q_EMIT edited();
    return true;
}

void TranscriptModel::setTranscript(const Transcript &t)
{
    beginResetModel();
    m_t = t;
    m_current = -1;
    endResetModel();
}

void TranscriptModel::setCurrent(int row)
{
    if (row == m_current)
        return;
    const int old = m_current;
    m_current = row;
    if (old >= 0 && old < rowCount())
        Q_EMIT dataChanged(index(old), index(old), {CurrentRole});
    if (row >= 0)
        Q_EMIT dataChanged(index(row), index(row), {CurrentRole});
}

void TranscriptModel::setQuery(const QString &q)
{
    m_query = q;
    if (rowCount())
        Q_EMIT dataChanged(index(0), index(rowCount() - 1), {MatchRole});
}

namespace {

class Delegate : public QStyledItemDelegate {
public:
    Delegate(QListView *view) : QStyledItemDelegate(view), m_view(view) {}

    static constexpr int kPad = 14;

    int textWidth() const { return std::max(100, m_view->viewport()->width() - 2 * kPad - 8); }

    QFont headerFont(const QFont &base) const
    {
        QFont f = base;
        f.setPointSizeF(base.pointSizeF() * 0.88);
        f.setWeight(QFont::DemiBold);
        return f;
    }

    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
    {
        const QFontMetrics fm(opt.font), hm(headerFont(opt.font));
        const QRect br = fm.boundingRect(QRect(0, 0, textWidth(), 100000), Qt::TextWordWrap,
                                         idx.data(Qt::DisplayRole).toString());
        return {m_view->viewport()->width(), hm.height() + br.height() + 22};
    }

    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QRect r = opt.rect.adjusted(4, 2, -4, -2);
        const bool current = idx.data(TranscriptModel::CurrentRole).toBool();
        const bool match = idx.data(TranscriptModel::MatchRole).toBool();
        const bool hover = opt.state & QStyle::State_MouseOver;
        if (current || match || hover) {
            QColor bg = match ? Theme::color(Token::Warning) : Theme::color(Token::AccentSoft);
            bg.setAlpha(match ? 40 : current ? 255 : 90);
            p->setPen(Qt::NoPen);
            p->setBrush(bg);
            p->drawRoundedRect(r, 8, 8);
        }
        const QString source = idx.data(TranscriptModel::SourceRole).toString();
        const double start = idx.data(TranscriptModel::StartRole).toDouble();
        const QFont hf = headerFont(opt.font);
        const QFontMetrics hm(hf);
        p->setFont(hf);
        const QRect head(r.left() + kPad - 4, r.top() + 7, r.width(), hm.height());
        const QString time = formatTime(start, true);
        p->setPen(Theme::color(Token::Accent));
        p->drawText(head, Qt::AlignLeft | Qt::AlignVCenter, time);
        const int tw = hm.horizontalAdvance(time);
        p->setPen(source == "microphone" ? Theme::color(Token::TrackMicrophone) : Theme::color(Token::Muted));
        QString who = " · " + speakerName(source);
        const double conf = idx.data(TranscriptModel::ConfidenceRole).toDouble();
        if (idx.data(TranscriptModel::EditedRole).toBool())
            who += " · " + QObject::tr("corrected");
        else if (conf > 0 && conf < 0.45)
            who += " · " + QObject::tr("uncertain");
        p->drawText(head.adjusted(tw, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, who);

        p->setFont(opt.font);
        p->setPen(conf > 0 && conf < 0.45 && !idx.data(TranscriptModel::EditedRole).toBool()
                      ? Theme::color(Token::Muted)
                      : Theme::color(Token::Text));
        const QRect body(r.left() + kPad - 4, head.bottom() + 4, textWidth(), r.bottom() - head.bottom() - 6);
        p->drawText(body, Qt::TextWordWrap, idx.data(Qt::DisplayRole).toString());
        p->restore();
    }

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        auto *e = new QPlainTextEdit(parent);
        e->setTabChangesFocus(true);
        return e;
    }
    void setEditorData(QWidget *editor, const QModelIndex &idx) const override
    {
        static_cast<QPlainTextEdit *>(editor)->setPlainText(idx.data(Qt::EditRole).toString());
    }
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &idx) const override
    {
        model->setData(idx, static_cast<QPlainTextEdit *>(editor)->toPlainText(), Qt::EditRole);
    }
    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &opt, const QModelIndex &) const override
    {
        const QFontMetrics hm(headerFont(opt.font));
        editor->setGeometry(opt.rect.adjusted(kPad - 8, hm.height() + 8, -8, -2));
    }
    bool eventFilter(QObject *obj, QEvent *ev) override
    {
        // Enter saves, Shift+Enter is a new line, Escape cancels.
        if (ev->type() == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent *>(ev);
            if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) && !(ke->modifiers() & Qt::ShiftModifier)) {
                auto *w = qobject_cast<QWidget *>(obj);
                Q_EMIT const_cast<Delegate *>(this)->commitData(w);
                Q_EMIT const_cast<Delegate *>(this)->closeEditor(w);
                return true;
            }
        }
        return QStyledItemDelegate::eventFilter(obj, ev);
    }
    bool editorEvent(QEvent *ev, QAbstractItemModel *model, const QStyleOptionViewItem &opt,
                     const QModelIndex &idx) override
    {
        // A click on the timestamp plays from there.
        if (ev->type() == QEvent::MouseButtonRelease) {
            auto *me = static_cast<QMouseEvent *>(ev);
            const QFontMetrics hm(headerFont(opt.font));
            const QRect head(opt.rect.left() + kPad, opt.rect.top() + 7,
                             hm.horizontalAdvance(formatTime(idx.data(TranscriptModel::StartRole).toDouble(), true)) + 4,
                             hm.height());
            if (head.contains(me->position().toPoint())) {
                Q_EMIT timeClicked(idx.data(TranscriptModel::StartRole).toDouble());
                return true;
            }
        }
        return QStyledItemDelegate::editorEvent(ev, model, opt, idx);
    }
    std::function<void(double)> onTime;
    void timeClicked(double t) const
    {
        if (onTime)
            onTime(t);
    }

private:
    QListView *m_view;
};

}  // namespace

TranscriptView::TranscriptView(QWidget *parent) : QWidget(parent)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 8, 0, 0);
    v->setSpacing(6);
    auto *bar = new QHBoxLayout;
    m_find = new QLineEdit;
    m_find->setPlaceholderText(tr("Find in transcript"));
    m_find->addAction(icon("search", Theme::color(Token::Muted)), QLineEdit::LeadingPosition);
    m_find->setClearButtonEnabled(true);
    m_find->setMaximumWidth(320);
    bar->addWidget(m_find);
    m_findCount = label({}, "Muted");
    bar->addWidget(m_findCount);
    bar->addStretch();
    m_info = label(tr("Double-click a line to correct it. Click a time to play from there."), "Muted");
    bar->addWidget(m_info);
    v->addLayout(bar);

    m_model = new TranscriptModel(this);
    m_list = new QListView;
    m_list->setModel(m_model);
    auto *delegate = new Delegate(m_list);
    delegate->onTime = [this](double t) { Q_EMIT seekRequested(t); };
    m_list->setItemDelegate(delegate);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setUniformItemSizes(false);
    m_list->setWordWrap(true);
    m_list->setMouseTracking(true);
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_list->setAccessibleName(tr("Transcript"));
    v->addWidget(m_list, 1);

    connect(m_model, &TranscriptModel::edited, this, [this] {
        QString err;
        if (!m_model->transcript().save(m_path, &err))
            QMessageBox::warning(this, tr("Could not save"), tr("The correction could not be saved: %1").arg(err));
        Q_EMIT changed();
    });
    connect(m_find, &QLineEdit::textChanged, this, [this](const QString &q) {
        m_model->setQuery(q.trimmed());
        int n = 0;
        for (const auto &s : m_model->transcript().segments)
            n += !q.trimmed().isEmpty() && s.text.contains(q.trimmed(), Qt::CaseInsensitive);
        m_findCount->setText(q.trimmed().isEmpty() ? QString() : tr("%n match(es)", nullptr, n));
        if (n)
            findNext(1);
    });
    connect(m_find, &QLineEdit::returnPressed, this, [this] {
        findNext(QApplication::keyboardModifiers() & Qt::ShiftModifier ? -1 : 1);
    });
    // Stop following playback while the person scrolls on their own.
    connect(m_list->verticalScrollBar(), &QScrollBar::sliderPressed, this, [this] { m_follow = false; });
}

void TranscriptView::load(const QString &path)
{
    m_path = path;
    Transcript t;
    t.load(path);
    m_model->setTranscript(t);
    m_find->clear();
    m_follow = true;
}

void TranscriptView::setPlaybackPosition(double seconds)
{
    const int row = m_model->transcript().indexAt(seconds);
    if (row != m_model->current()) {
        m_model->setCurrent(row);
        if (row >= 0 && m_follow && !m_list->isPersistentEditorOpen(m_model->index(row)))
            m_list->scrollTo(m_model->index(row), QAbstractItemView::EnsureVisible);
    }
}

void TranscriptView::revealTime(double seconds)
{
    m_follow = true;
    const int row = m_model->transcript().indexAt(seconds);
    if (row >= 0) {
        m_model->setCurrent(row);
        m_list->scrollTo(m_model->index(row), QAbstractItemView::PositionAtCenter);
    }
}

void TranscriptView::focusFind()
{
    m_find->setFocus();
    m_find->selectAll();
}

void TranscriptView::findNext(int direction)
{
    const QString q = m_find->text().trimmed();
    const auto &segs = m_model->transcript().segments;
    if (q.isEmpty() || segs.isEmpty())
        return;
    const int n = int(segs.size());
    const int startRow = m_list->currentIndex().isValid() ? m_list->currentIndex().row() : -1;
    for (int k = 1; k <= n; ++k) {
        const int row = ((startRow + direction * k) % n + n) % n;
        if (segs[row].text.contains(q, Qt::CaseInsensitive)) {
            m_list->setCurrentIndex(m_model->index(row));
            m_list->scrollTo(m_model->index(row), QAbstractItemView::PositionAtCenter);
            return;
        }
    }
}

}  // namespace afterline::ui
