// SPDX-License-Identifier: MIT
#include "ui/LibraryView.h"

#include "app/Processing.h"
#include "core/Transcript.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QLocale>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>

namespace afterline::ui {

namespace {

QString dayGroup(const QDate &d)
{
    const QDate today = QDate::currentDate();
    if (d == today)
        return QObject::tr("Today");
    if (d == today.addDays(-1))
        return QObject::tr("Yesterday");
    if (d > today.addDays(-7))
        return QObject::tr("This week");
    if (d.year() == today.year() && d.month() == today.month())
        return QObject::tr("Earlier this month");
    return QLocale().toString(d, "MMMM yyyy");
}

class Delegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
    {
        const int kind = idx.data(LibraryModel::KindRole).toInt();
        const QFontMetrics fm(opt.font);
        if (kind == LibraryModel::Header)
            return {opt.rect.width(), fm.height() + 18};
        if (kind == LibraryModel::Hit)
            return {opt.rect.width(), fm.height() * 3 + 22};
        return {opt.rect.width(), fm.height() * 2 + 22};
    }

    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const int kind = idx.data(LibraryModel::KindRole).toInt();
        const QRect r = opt.rect.adjusted(8, 2, -8, -2);
        QFont f = opt.font;
        if (kind == LibraryModel::Header) {
            f.setPointSizeF(f.pointSizeF() * 0.85);
            f.setBold(true);
            f.setLetterSpacing(QFont::PercentageSpacing, 106);
            p->setFont(f);
            p->setPen(Theme::color(Token::Muted));
            p->drawText(r.adjusted(8, 0, 0, -3), Qt::AlignLeft | Qt::AlignBottom,
                        idx.data(Qt::DisplayRole).toString().toUpper());
            p->restore();
            return;
        }
        const bool selected = opt.state & QStyle::State_Selected;
        const bool hover = opt.state & QStyle::State_MouseOver;
        if (selected || hover) {
            QColor bg = selected ? Theme::color(Token::Surface) : Theme::color(Token::Surface);
            if (!selected)
                bg.setAlpha(140);
            p->setPen(selected ? QPen(Theme::color(Token::Border)) : Qt::NoPen);
            p->setBrush(bg);
            p->drawRoundedRect(r, 9, 9);
            if (selected) {
                p->setPen(Qt::NoPen);
                p->setBrush(Theme::color(Token::Accent));
                p->drawRoundedRect(QRect(r.left() + 1, r.top() + 10, 3, r.height() - 20), 1.5, 1.5);
            }
        }
        const QRect content = r.adjusted(14, 8, -12, -8);
        const QFontMetrics fm(f);
        // Title
        QFont tf = f;
        tf.setWeight(QFont::DemiBold);
        p->setFont(tf);
        p->setPen(Theme::color(Token::Text));
        const QString status = idx.data(LibraryModel::StatusRole).toString();
        QRect titleRect(content.left(), content.top(), content.width(), fm.height());
        p->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                    QFontMetrics(tf).elidedText(idx.data(Qt::DisplayRole).toString(), Qt::ElideRight, titleRect.width()));
        // Subtitle + status chip
        QFont sf = f;
        sf.setPointSizeF(f.pointSizeF() * 0.9);
        p->setFont(sf);
        const QFontMetrics sfm(sf);
        QRect sub(content.left(), titleRect.bottom() + 4, content.width(), sfm.height());
        int chipWidth = 0;
        if (!status.isEmpty()) {
            chipWidth = sfm.horizontalAdvance(status) + 14;
            const QRect chip(sub.right() - chipWidth, sub.top() - 1, chipWidth, sfm.height() + 2);
            const bool warn = status.startsWith(QObject::tr("Interrupted")) || status.startsWith(QObject::tr("Failed"));
            const bool rec = status.startsWith(QObject::tr("Recording"));
            QColor bg = rec ? Theme::color(Token::Recording) : warn ? Theme::color(Token::Warning) : Theme::color(Token::Accent);
            QColor fg = rec ? QColor(Qt::white) : bg;
            if (!rec)
                bg.setAlpha(36);
            p->setPen(Qt::NoPen);
            p->setBrush(bg);
            p->drawRoundedRect(chip, chip.height() / 2.0, chip.height() / 2.0);
            p->setPen(fg);
            p->drawText(chip, Qt::AlignCenter, status);
        }
        p->setPen(Theme::color(Token::Muted));
        p->drawText(sub.adjusted(0, 0, -chipWidth - 6, 0), Qt::AlignLeft | Qt::AlignVCenter,
                    sfm.elidedText(idx.data(LibraryModel::SubtitleRole).toString(), Qt::ElideRight,
                                   sub.width() - chipWidth - 6));
        if (kind == LibraryModel::Hit) {
            QTextDocument doc;
            doc.setDefaultFont(sf);
            doc.setDocumentMargin(0);
            doc.setTextWidth(content.width());
            doc.setHtml(QString("<span style='color:%1'>%2</span>")
                            .arg(Theme::hex(Token::Text), idx.data(LibraryModel::SnippetRole).toString()));
            p->translate(content.left(), sub.bottom() + 4);
            doc.drawContents(p, QRectF(0, 0, content.width(), sfm.height() * 1.3));
        }
        p->restore();
    }
};

}  // namespace

LibraryModel::LibraryModel(AppContext ctx, QObject *parent) : QAbstractListModel(parent), m_ctx(ctx) {}

int LibraryModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(m_rows.size()); }

Qt::ItemFlags LibraryModel::flags(const QModelIndex &index) const
{
    if (!index.isValid() || m_rows[index.row()].kind == Header)
        return Qt::ItemIsEnabled;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

QVariant LibraryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};
    const Row &r = m_rows[index.row()];
    switch (role) {
    case Qt::DisplayRole: return r.title;
    case Qt::AccessibleTextRole: return r.title + ". " + r.subtitle + ". " + r.status;
    case KindRole: return int(r.kind);
    case MeetingIdRole: return r.id;
    case SubtitleRole: return r.subtitle;
    case StatusRole: return m_live.value(r.id, r.status);
    case StartRole: return r.start;
    case SnippetRole: return r.snippet;
    default: return {};
    }
}

QString LibraryModel::statusOf(const MeetingInfo &m) const
{
    if (m.state == "recording")
        return tr("Recording");
    if (m.transcriptState == "running" || m.transcriptState == "queued")
        return tr("Transcribing");
    if (m.notesState == "running" || m.notesState == "queued")
        return tr("Writing notes");
    if (m.transcriptState == "failed" || m.notesState == "failed")
        return tr("Failed");
    if (m.state == "interrupted" && !m.hasTranscript())
        return tr("Interrupted");
    if (!m.hasTranscript())
        return tr("Not transcribed");
    return {};
}

void LibraryModel::reload(const QString &filter)
{
    beginResetModel();
    m_rows.clear();
    QString lastGroup;
    for (const auto &m : m_ctx.store->list()) {
        if (filter.startsWith("folder:") && m.folder != filter.mid(7))
            continue;
        if (filter.startsWith("tag:") && !m.tags.contains(filter.mid(4)))
            continue;
        const QString group = dayGroup(m.created.date());
        if (group != lastGroup) {
            m_rows.push_back({Header, {}, group, {}, {}, {}});
            lastGroup = group;
        }
        QString sub = QLocale().toString(m.created, m.created.date() == QDate::currentDate() ? "HH:mm" : "d MMM, HH:mm");
        if (m.duration > 0)
            sub += " · " + formatDuration(m.duration);
        if (!m.sourceLabel.isEmpty())
            sub += " · " + m.sourceLabel;
        m_rows.push_back({Meeting, m.id, m.title, sub, statusOf(m), {}});
    }
    endResetModel();
}

void LibraryModel::showSearch(const QString &query)
{
    beginResetModel();
    m_rows.clear();
    const auto hits = m_ctx.store->search(query, 80);
    m_rows.push_back({Header, {}, hits.isEmpty() ? tr("No results") : tr("%n result(s)", nullptr, int(hits.size())),
                      {}, {}, {}});
    for (const auto &h : hits) {
        QString sub = QLocale().toString(h.created.date(), QLocale::ShortFormat);
        if (h.start >= 0)
            sub += " · " + formatTime(h.start) + " · " + speakerName(h.source);
        else if (h.segmentId < 0 && h.snippet != h.meetingTitle.toHtmlEscaped())
            sub += " · " + tr("Notes");
        Row r{Hit, h.meetingId, h.meetingTitle, sub, {}, h.snippet};
        r.start = h.start;
        m_rows.push_back(r);
    }
    endResetModel();
}

int LibraryModel::rowOf(const QString &meetingId) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].kind == Meeting && m_rows[i].id == meetingId)
            return i;
    return -1;
}

void LibraryModel::setLiveStatus(const QString &meetingId, const QString &status)
{
    if (status.isEmpty())
        m_live.remove(meetingId);
    else
        m_live[meetingId] = status;
    const int row = rowOf(meetingId);
    if (row >= 0)
        Q_EMIT dataChanged(index(row), index(row), {StatusRole});
}

// ---------------------------------------------------------------------------

LibraryView::LibraryView(AppContext ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    setObjectName("Sidebar");
    setAttribute(Qt::WA_StyledBackground);
    setMinimumWidth(270);
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(14, 16, 14, 12);
    v->setSpacing(10);

    auto *brand = new QHBoxLayout;
    auto *logo = new QLabel;
    logo->setPixmap(QIcon(":/icons/afterline.svg").pixmap(28, 28));
    brand->addWidget(logo);
    brand->addWidget(label("AfterLine", "BrandName"));
    brand->addStretch();
    auto *settings = iconButton("settings", tr("Settings (Ctrl+,)"));
    connect(settings, &QPushButton::clicked, this, &LibraryView::settingsClicked);
    brand->addWidget(settings);
    v->addLayout(brand);

    m_record = new QPushButton(icon("record", Theme::color(Token::OnAccent)), tr("New recording"));
    m_record->setObjectName("Primary");
    m_record->setCursor(Qt::PointingHandCursor);
    m_record->setToolTip(tr("Start a new recording (Ctrl+R)"));
    connect(m_record, &QPushButton::clicked, this, &LibraryView::recordClicked);
    v->addWidget(m_record);

    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Search meetings and transcripts"));
    m_search->addAction(icon("search", Theme::color(Token::Muted)), QLineEdit::LeadingPosition);
    m_search->setClearButtonEnabled(true);
    v->addWidget(m_search);

    m_filter = new QComboBox;
    m_filter->setAccessibleName(tr("Show"));
    v->addWidget(m_filter);

    m_model = new LibraryModel(ctx, this);
    m_list = new QListView;
    m_list->setModel(m_model);
    m_list->setItemDelegate(new Delegate(m_list));
    m_list->setMouseTracking(true);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setUniformItemSizes(false);
    m_list->setAccessibleName(tr("Meetings"));
    v->addWidget(m_list, 1);

    m_download = new QWidget;
    auto *dl = new QVBoxLayout(m_download);
    dl->setContentsMargins(4, 4, 4, 0);
    m_downloadLabel = label({}, "Muted");
    m_downloadBar = new QProgressBar;
    m_downloadBar->setFixedHeight(6);
    dl->addWidget(m_downloadLabel);
    dl->addWidget(m_downloadBar);
    m_download->hide();
    v->addWidget(m_download);

    auto *debounce = new QTimer(this);
    debounce->setSingleShot(true);
    debounce->setInterval(250);
    connect(m_search, &QLineEdit::textChanged, debounce, qOverload<>(&QTimer::start));
    connect(debounce, &QTimer::timeout, this, &LibraryView::refresh);
    connect(m_filter, &QComboBox::currentIndexChanged, this, &LibraryView::refresh);
    connect(m_list->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &idx) {
        if (!idx.isValid() || idx.data(LibraryModel::KindRole).toInt() == LibraryModel::Header)
            return;
        m_selected = idx.data(LibraryModel::MeetingIdRole).toString();
        Q_EMIT openMeeting(m_selected, idx.data(LibraryModel::StartRole).toDouble());
    });
    connect(m_list, &QListView::clicked, this, [this](const QModelIndex &idx) {
        // Clicking the selected search hit again jumps to it again.
        if (idx.data(LibraryModel::KindRole).toInt() == LibraryModel::Hit)
            Q_EMIT openMeeting(idx.data(LibraryModel::MeetingIdRole).toString(),
                               idx.data(LibraryModel::StartRole).toDouble());
    });
    reloadFilters();
    refresh();
}

void LibraryView::reloadFilters()
{
    const QString current = m_filter->currentData().toString();
    QSignalBlocker block(m_filter);
    m_filter->clear();
    m_filter->addItem(icon("notes"), tr("All meetings"), QString());
    for (const auto &f : m_ctx.store->allFolders())
        m_filter->addItem(icon("folder"), f, "folder:" + f);
    for (const auto &t : m_ctx.store->allTags())
        m_filter->addItem(icon("tag"), t, "tag:" + t);
    const int i = m_filter->findData(current);
    m_filter->setCurrentIndex(i >= 0 ? i : 0);
    m_filter->setVisible(m_filter->count() > 1);
}

void LibraryView::refresh()
{
    const QString q = m_search->text().trimmed();
    reloadFilters();
    if (q.isEmpty())
        m_model->reload(m_filter->currentData().toString());
    else
        m_model->showSearch(q);
    if (q.isEmpty() && !m_selected.isEmpty())
        select(m_selected);
}

void LibraryView::select(const QString &meetingId)
{
    m_selected = meetingId;
    const int row = m_model->rowOf(meetingId);
    QSignalBlocker block(m_list->selectionModel());
    if (row >= 0) {
        m_list->setCurrentIndex(m_model->index(row));
        m_list->scrollTo(m_model->index(row));
    } else {
        m_list->clearSelection();
    }
}

void LibraryView::focusSearch()
{
    m_search->setFocus();
    m_search->selectAll();
}

void LibraryView::setRecording(bool recording, const QString &time)
{
    if (recording) {
        m_record->setText(tr("Recording · %1").arg(time));
        m_record->setIcon(icon("record", Theme::color(Token::OnAccent)));
        m_record->setStyleSheet(QString("background:%1;").arg(Theme::hex(Token::Recording)));
        m_record->setToolTip(tr("Show the recording"));
    } else {
        m_record->setText(tr("New recording"));
        m_record->setStyleSheet({});
        m_record->setToolTip(tr("Start a new recording (Ctrl+R)"));
    }
}

void LibraryView::setLiveStatus(const QString &meetingId, const QString &status)
{
    m_model->setLiveStatus(meetingId, status);
}

void LibraryView::setDownloadStatus(const QString &text, int percent)
{
    m_download->setVisible(percent >= 0);
    m_downloadLabel->setText(text);
    m_downloadBar->setValue(percent);
}

}  // namespace afterline::ui
