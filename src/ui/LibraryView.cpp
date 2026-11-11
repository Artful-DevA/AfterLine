// SPDX-License-Identifier: MIT
#include "ui/LibraryView.h"

#include "app/Processing.h"
#include "app/Tasks.h"
#include "core/Transcript.h"
#include "ui/Dialogs.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QButtonGroup>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QMenu>
#include <QMimeData>
#include <QScrollArea>
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

const char *kMeetingMime = "application/x-afterline-meeting";

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
    const Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    return m_rows[index.row()].kind == Meeting ? f | Qt::ItemIsDragEnabled : f;
}

QStringList LibraryModel::mimeTypes() const { return {kMeetingMime}; }

QMimeData *LibraryModel::mimeData(const QModelIndexList &indexes) const
{
    if (indexes.isEmpty())
        return nullptr;
    auto *m = new QMimeData;
    m->setData(kMeetingMime, indexes.first().data(MeetingIdRole).toString().toUtf8());
    return m;
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
    if (m.notesState == "failed" && m.transcriptState == "done" && m.transcriptIsEmpty())
        return tr("No speech");
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
        if (filter.startsWith("folder:") && m.folder.compare(filter.mid(7), Qt::CaseInsensitive) != 0)
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

NavButton::NavButton(const QString &key, const QString &text, const QString &iconName, const QColor &tint,
                     QWidget *parent)
    : QAbstractButton(parent), m_key(key), m_icon(iconName), m_tint(tint)
{
    setText(text);
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(34);
    setAccessibleName(text);
    setFocusPolicy(Qt::TabFocus);
}

void NavButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 1, -0.5, -1);
    if (m_dropping) {
        p.setPen(QPen(m_tint, 1.5));
        QColor fill = m_tint;
        fill.setAlpha(40);
        p.setBrush(fill);
        p.drawRoundedRect(r, 8, 8);
    } else if (isChecked()) {
        p.setPen(QPen(Theme::color(Token::Border), 1));
        p.setBrush(Theme::color(Token::Surface));
        p.drawRoundedRect(r, 8, 8);
    } else if (m_hover || hasFocus()) {
        QColor h = Theme::color(Token::Text);
        h.setAlpha(14);
        p.setPen(Qt::NoPen);
        p.setBrush(h);
        p.drawRoundedRect(r, 8, 8);
    }
    // Tinted icon tile.
    const QRectF tile(8, (height() - 24) / 2.0, 24, 24);
    QColor soft = m_tint;
    soft.setAlpha(Theme::isDark() ? 60 : 38);
    p.setPen(Qt::NoPen);
    p.setBrush(soft);
    p.drawRoundedRect(tile, 7, 7);
    ui::icon(m_icon, m_tint).paint(&p, tile.adjusted(4, 4, -4, -4).toRect());
    QFont f = font();
    if (isChecked())
        f.setWeight(QFont::DemiBold);
    p.setFont(f);
    const QFontMetrics fm(f);
    int right = width() - 10;
    if (!m_count.isEmpty()) {
        p.setPen(Theme::color(Token::Muted));
        const int w = fm.horizontalAdvance(m_count);
        p.drawText(QRect(right - w, 0, w, height()), Qt::AlignVCenter, m_count);
        right -= w + 10;
    }
    p.setPen(Theme::color(Token::Text));
    p.drawText(QRect(42, 0, right - 42, height()), Qt::AlignVCenter | Qt::AlignLeft,
               fm.elidedText(text(), Qt::ElideRight, right - 42));
}

void NavButton::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasFormat(kMeetingMime)) {
        e->acceptProposedAction();
        m_dropping = true;
        update();
    }
}

void NavButton::dropEvent(QDropEvent *e)
{
    m_dropping = false;
    update();
    const QString id = QString::fromUtf8(e->mimeData()->data(kMeetingMime));
    if (!id.isEmpty()) {
        e->acceptProposedAction();
        Q_EMIT meetingDropped(id);
    }
}

void NavButton::contextMenuEvent(QContextMenuEvent *e) { Q_EMIT contextMenuWanted(e->globalPos()); }

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
    m_search->setObjectName("Search");
    m_search->setPlaceholderText(tr("Search meetings and transcripts"));
    m_search->addAction(icon("search", Theme::color(Token::Muted)), QLineEdit::LeadingPosition);
    m_search->setClearButtonEnabled(true);
    v->addWidget(m_search);

    // Navigation: everything, your tasks, folders and tags.
    auto *navWidget = new QWidget;
    m_navLayout = new QVBoxLayout(navWidget);
    m_navLayout->setContentsMargins(0, 0, 0, 0);
    m_navLayout->setSpacing(1);
    m_navScroll = new QScrollArea;
    m_navScroll->setWidget(navWidget);
    m_navScroll->setWidgetResizable(true);
    m_navScroll->setFrameShape(QFrame::NoFrame);
    m_navScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_navScroll->setStyleSheet("QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }");
    m_navGroup = new QButtonGroup(this);
    m_navGroup->setExclusive(true);
    v->addWidget(m_navScroll);

    m_model = new LibraryModel(ctx, this);
    m_list = new QListView;
    m_list->setModel(m_model);
    m_list->setItemDelegate(new Delegate(m_list));
    m_list->setMouseTracking(true);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setUniformItemSizes(false);
    m_list->setAccessibleName(tr("Meetings"));
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setDragEnabled(true);
    m_list->setDragDropMode(QAbstractItemView::DragOnly);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QListView::customContextMenuRequested, this, &LibraryView::meetingMenu);
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
    // Rebuilt on every refresh: counts change and folders come and go.
    for (auto *b : m_navGroup->buttons()) {
        m_navGroup->removeButton(b);
        b->deleteLater();
    }
    while (QLayoutItem *item = m_navLayout->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    const auto meetings = m_ctx.store->list();
    const QColor accent = Theme::color(Token::Accent);
    auto addNav = [&](const QString &key, const QString &text, const QString &iconName, const QColor &tint,
                      const QString &count) {
        auto *b = new NavButton(key, text, iconName, tint);
        b->setCount(count);
        b->setChecked(key == m_section);
        m_navGroup->addButton(b);
        m_navLayout->addWidget(b);
        connect(b, &NavButton::clicked, this, [this, key] { showSection(key); });
        return b;
    };
    addNav({}, tr("All meetings"), "inbox", Theme::color(Token::Muted), QString::number(meetings.size()));
    int open = 0;
    for (const auto &t : collectTasks(*m_ctx.store, true))
        open += !t.done;
    addNav("tasks", tr("My tasks"), "tasks", Theme::color(Token::Success), open ? QString::number(open) : QString());

    auto *head = new QWidget;
    auto *hh = new QHBoxLayout(head);
    hh->setContentsMargins(10, 10, 0, 2);
    hh->addWidget(label(tr("FOLDERS"), "Eyebrow"), 1);
    auto *organize = iconButton("sparkles", tr("Organize meetings into folders with AI"));
    organize->setFixedSize(26, 26);
    organize->setIconSize({16, 16});
    connect(organize, &QPushButton::clicked, this, &LibraryView::organizeRequested);
    hh->addWidget(organize);
    auto *add = iconButton("plus", tr("New folder"));
    add->setFixedSize(26, 26);
    add->setIconSize({16, 16});
    connect(add, &QPushButton::clicked, this, [this] { newFolder(); });
    hh->addWidget(add);
    m_navLayout->addWidget(head);

    const auto folders = m_ctx.store->folders();
    if (folders.isEmpty()) {
        auto *hint = label(tr("Create folders, then drag meetings onto them, or let AfterLine file them for you."),
                           "Muted");
        hint->setContentsMargins(10, 0, 6, 4);
        m_navLayout->addWidget(hint);
    }
    for (const auto &f : folders) {
        int n = 0;
        for (const auto &m : meetings)
            n += m.folder.compare(f.name, Qt::CaseInsensitive) == 0;
        auto *b = addNav("folder:" + f.name, f.name, f.icon, folderColor(f.color), n ? QString::number(n) : QString());
        b->setAcceptsMeetings(true);
        b->setToolTip(tr("Drop a meeting here to move it into “%1”").arg(f.name));
        const QString name = f.name;
        connect(b, &NavButton::meetingDropped, this, [this, name](const QString &id) {
            m_ctx.store->setFolder(id, name);
            refresh();
            Q_EMIT foldersChanged();
        });
        connect(b, &NavButton::contextMenuWanted, this, [this, name](const QPoint &pos) { folderMenu(name, pos); });
    }
    const QStringList tags = m_ctx.store->allTags();
    if (!tags.isEmpty()) {
        auto *th = label(tr("TAGS"), "Eyebrow");
        th->setContentsMargins(10, 10, 0, 2);
        m_navLayout->addWidget(th);
        for (const auto &t : tags)
            addNav("tag:" + t, t, "tag", Theme::color(Token::Muted), {});
    }
    // The navigation takes what it needs, up to about half the sidebar.
    navHeightChanged();
    Q_UNUSED(accent);
}

void LibraryView::navHeightChanged()
{
    int h = 0;
    for (int i = 0; i < m_navLayout->count(); ++i)
        if (QWidget *w = m_navLayout->itemAt(i)->widget())
            h += w->sizeHint().height() + m_navLayout->spacing();
    m_navScroll->setFixedHeight(std::min(h + 4, std::max(160, height() / 2 - 60)));
}

void LibraryView::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    navHeightChanged();
}

void LibraryView::showSection(const QString &key)
{
    if (key == "tasks") {
        Q_EMIT tasksClicked();
        m_section = key;
        for (auto *b : m_navGroup->buttons())
            b->setChecked(static_cast<NavButton *>(b)->key() == key);
        return;
    }
    m_section = key;
    m_search->clear();
    refresh();
}

void LibraryView::newFolder(const QString &moveMeetingId)
{
    FolderInfo f;
    QStringList taken;
    for (const auto &e : m_ctx.store->folders())
        taken << e.name;
    if (!editFolder(this, &f, true, taken))
        return;
    m_ctx.store->saveFolder(f);
    if (!moveMeetingId.isEmpty())
        m_ctx.store->setFolder(moveMeetingId, f.name);
    refresh();
    Q_EMIT foldersChanged();
}

void LibraryView::folderMenu(const QString &name, const QPoint &globalPos)
{
    QMenu menu(this);
    menu.addAction(icon("edit"), tr("Edit name, icon and colour…"), this, [this, name] {
        FolderInfo f = m_ctx.store->folder(name);
        QStringList taken;
        for (const auto &e : m_ctx.store->folders())
            taken << e.name;
        if (!editFolder(this, &f, false, taken))
            return;
        m_ctx.store->saveFolder(f, name);
        if (m_section == "folder:" + name)
            m_section = "folder:" + f.name;
        refresh();
        Q_EMIT foldersChanged();
    });
    menu.addSeparator();
    menu.addAction(icon("trash", Theme::color(Token::Danger)), tr("Delete folder"), this, [this, name] {
        if (!confirm(this, tr("Delete the folder “%1”?").arg(name),
                     tr("Only the folder is deleted. Its meetings stay in your library, under All meetings."),
                     tr("Delete folder"), Tone::Danger, "folder"))
            return;
        m_ctx.store->removeFolder(name);
        if (m_section == "folder:" + name)
            m_section.clear();
        refresh();
        Q_EMIT foldersChanged();
    });
    menu.exec(globalPos);
}

void LibraryView::meetingMenu(const QPoint &pos)
{
    const QModelIndex idx = m_list->indexAt(pos);
    if (!idx.isValid() || idx.data(LibraryModel::KindRole).toInt() != LibraryModel::Meeting)
        return;
    const QString id = idx.data(LibraryModel::MeetingIdRole).toString();
    const MeetingInfo m = m_ctx.store->get(id);
    QMenu menu(this);
    QMenu *move = menu.addMenu(icon("folder"), tr("Move to folder"));
    for (const auto &f : m_ctx.store->folders()) {
        QAction *a = move->addAction(icon(f.icon, folderColor(f.color)), f.name, this, [this, id, name = f.name] {
            m_ctx.store->setFolder(id, name);
            refresh();
            Q_EMIT foldersChanged();
        });
        a->setCheckable(true);
        a->setChecked(m.folder.compare(f.name, Qt::CaseInsensitive) == 0);
    }
    if (!m_ctx.store->folders().isEmpty())
        move->addSeparator();
    move->addAction(icon("plus"), tr("New folder…"), this, [this, id] { newFolder(id); });
    if (!m.folder.isEmpty())
        menu.addAction(icon("close"), tr("Remove from “%1”").arg(m.folder), this, [this, id] {
            m_ctx.store->setFolder(id, {});
            refresh();
            Q_EMIT foldersChanged();
        });
    menu.exec(m_list->viewport()->mapToGlobal(pos));
}

void LibraryView::refresh()
{
    const QString q = m_search->text().trimmed();
    reloadFilters();
    if (q.isEmpty())
        m_model->reload(m_section == "tasks" ? QString() : m_section);
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
