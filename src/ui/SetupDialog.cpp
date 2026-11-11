// SPDX-License-Identifier: MIT
#include "ui/SetupDialog.h"

#include "app/ModelCatalog.h"
#include "app/ModelManager.h"
#include "app/Settings.h"
#include "ui/Dialogs.h"
#include "ui/Icons.h"
#include "ui/SettingsView.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace afterline::ui {

namespace {
QWidget *page(const QString &title, const QString &text, QVBoxLayout **body)
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(10);
    v->addWidget(label(title, "PageTitle"));
    if (!text.isEmpty())
        v->addWidget(label(text, "Muted"));
    v->addSpacing(6);
    *body = v;
    return w;
}
}  // namespace

SetupDialog::SetupDialog(AppContext ctx, QWidget *parent) : QDialog(parent), m_ctx(ctx)
{
    setWindowTitle(tr("Welcome to AfterLine"));
    setMinimumSize(640, 600);
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(32, 28, 32, 22);
    m_pages = new QStackedWidget;
    v->addWidget(m_pages, 1);

    QVBoxLayout *b = nullptr;
    // 1. Welcome
    auto *welcome = page(tr("Conversations, kept."),
                         tr("AfterLine records your meetings, writes a full transcript and turns it into clear "
                            "notes, all on this computer."),
                         &b);
    auto point = [&](const QString &iconName, const QString &text) {
        auto *row = new QHBoxLayout;
        auto *ic = new QLabel;
        ic->setPixmap(icon(iconName, Theme::color(Token::Accent)).pixmap(22, 22));
        row->addWidget(ic, 0, Qt::AlignTop);
        row->addWidget(label(text), 1);
        b->addLayout(row);
    };
    point("app", tr("Record a whole computer or just your meeting app (Firefox, Chrome, Zoom, Teams, Discord…), "
                    "with or without your microphone."));
    point("transcript", tr("Get a timestamped transcript. Click any line to hear that moment."));
    point("notes", tr("Get a summary, decisions, action items and dates, each linked to where it was said."));
    point("lock", tr("No account, no cloud, no telemetry. Your meetings never leave this computer."));
    b->addStretch();
    b->addWidget(label(tr("Please make sure the people you record know about it and agree where required."), "Muted"));
    m_pages->addWidget(welcome);

    // 2. Transcription
    auto *speech = page(tr("Set up offline transcription"),
                        tr("AfterLine needs a speech recognition model, downloaded once from Hugging Face and "
                           "checked before use. After that, transcription works without internet."),
                        &b);
    auto *langRow = new QHBoxLayout;
    langRow->addWidget(label(tr("Your meetings are mostly in")));
    auto *lang = new QComboBox;
    for (const auto &code : Settings::supportedLanguages())
        lang->addItem(Settings::languageName(code), code);
    lang->setCurrentIndex(std::max(0, lang->findData(ctx.settings->language())));
    connect(lang, &QComboBox::currentIndexChanged, this, [this, lang] {
        m_ctx.settings->setLanguage(lang->currentData().toString());
    });
    langRow->addWidget(lang, 1);
    b->addLayout(langRow);
    b->addWidget(label(tr("For meetings in Romanian and other languages besides English, “Most accurate” gives "
                          "clearly better transcripts if your computer has 16 GB of memory."),
                       "Muted"));
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(modelList(ctx, ModelKind::Speech));
    b->addWidget(scroll, 1);
    b->addWidget(label(tr("Free disk space: %1").arg(formatBytes(ctx.models->freeSpace())), "Muted"));
    m_pages->addWidget(speech);

    // 3. Notes
    auto *notes = page(tr("Meeting notes (optional)"),
                       tr("To write summaries and action items, AfterLine uses a local language model. It is a "
                          "larger download; you can add it now or later in Settings."),
                       &b);
    auto *scroll2 = new QScrollArea;
    scroll2->setWidgetResizable(true);
    scroll2->setFrameShape(QFrame::NoFrame);
    scroll2->setWidget(modelList(ctx, ModelKind::Notes));
    b->addWidget(scroll2, 1);
    b->addWidget(label(tr("Downloads continue in the background. You can start recording right away; meetings are "
                          "transcribed as soon as the model is ready."),
                       "Muted"));
    m_pages->addWidget(notes);

    auto *nav = new QHBoxLayout;
    m_step = label({}, "Muted");
    nav->addWidget(m_step, 1);
    m_back = new QPushButton(tr("Back"));
    m_next = new QPushButton;
    m_next->setObjectName("Primary");
    m_next->setDefault(true);
    nav->addWidget(m_back);
    nav->addWidget(m_next);
    v->addLayout(nav);
    connect(m_back, &QPushButton::clicked, this, [this] { go(m_pages->currentIndex() - 1); });
    connect(m_next, &QPushButton::clicked, this, [this] {
        const int i = m_pages->currentIndex();
        // The recommended model is one click away: "Download and continue".
        if (i == 1 || i == 2) {
            const ModelKind kind = i == 1 ? ModelKind::Speech : ModelKind::Notes;
            const QString id = kind == ModelKind::Speech ? m_ctx.settings->speechModel() : m_ctx.settings->notesModel();
            const ModelSpec *spec = findModel(id);
            if (spec && !spec->installed() && !m_ctx.models->isDownloading(id)) {
                QString err;
                if (!m_ctx.models->download(id, &err)) {
                    inform(this, tr("Cannot download"), err, Tone::Warning);
                    return;
                }
            }
        }
        if (i + 1 < m_pages->count()) {
            go(i + 1);
        } else {
            m_ctx.settings->setSetupDone(true);
            accept();
        }
    });
    auto *skip = new QPushButton(tr("Skip"));
    skip->setFlat(true);
    skip->setProperty("flat", true);
    nav->insertWidget(1, skip);
    connect(skip, &QPushButton::clicked, this, [this] {
        if (m_pages->currentIndex() + 1 < m_pages->count()) {
            go(m_pages->currentIndex() + 1);
        } else {
            m_ctx.settings->setSetupDone(true);
            accept();
        }
    });
    connect(m_pages, &QStackedWidget::currentChanged, skip, [this, skip](int i) { skip->setVisible(i > 0); });
    skip->hide();
    go(0);
}

void SetupDialog::go(int index)
{
    index = std::clamp(index, 0, m_pages->count() - 1);
    m_pages->setCurrentIndex(index);
    m_back->setVisible(index > 0);
    m_step->setText(tr("Step %1 of %2").arg(index + 1).arg(m_pages->count()));
    auto label = [this](ModelKind kind) {
        const ModelSpec *s = findModel(kind == ModelKind::Speech ? m_ctx.settings->speechModel()
                                                                 : m_ctx.settings->notesModel());
        if (!s || s->installed() || m_ctx.models->isDownloading(s->id))
            return tr("Continue");
        return tr("Download %1 and continue").arg(formatBytes(s->bytes));
    };
    if (index == 0)
        m_next->setText(tr("Get started"));
    else if (index == 1)
        m_next->setText(label(ModelKind::Speech));
    else
        m_next->setText(m_ctx.models->isDownloading(m_ctx.settings->notesModel()) ||
                                (findModel(m_ctx.settings->notesModel()) && findModel(m_ctx.settings->notesModel())->installed())
                            ? tr("Finish")
                            : tr("Download %1 and finish").arg(formatBytes(findModel(m_ctx.settings->notesModel())->bytes)));
}

}  // namespace afterline::ui
