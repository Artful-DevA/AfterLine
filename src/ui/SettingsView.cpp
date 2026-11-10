// SPDX-License-Identifier: MIT
#include "ui/SettingsView.h"

#include "app/MeetingStore.h"
#include "app/ModelManager.h"
#include "app/Settings.h"
#include "core/Paths.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDirIterator>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStorageInfo>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

namespace afterline::ui {

ModelRow::ModelRow(AppContext ctx, const ModelSpec &spec, QButtonGroup *group, QWidget *parent)
    : QWidget(parent), m_ctx(ctx), m_spec(spec)
{
    setObjectName("Card");
    setAttribute(Qt::WA_StyledBackground);
    auto *h = new QHBoxLayout(this);
    h->setContentsMargins(14, 12, 14, 12);
    m_radio = new QRadioButton;
    m_radio->setAccessibleName(tr("Use %1").arg(spec.name));
    if (group)
        group->addButton(m_radio);
    else
        m_radio->hide();
    h->addWidget(m_radio, 0, Qt::AlignTop);
    auto *v = new QVBoxLayout;
    QString title = spec.name;
    if (spec.id == recommendedModel(spec.kind))
        title += "  ·  " + tr("Recommended for this computer");
    auto *name = label(title);
    QFont f = name->font();
    f.setWeight(QFont::DemiBold);
    name->setFont(f);
    v->addWidget(name);
    v->addWidget(label(spec.description, "Muted"));
    v->addWidget(label(tr("%1 download · about %2 of memory while working · %3 licence · %4")
                           .arg(formatBytes(spec.bytes), formatBytes(qint64(spec.memoryMB) * 1000 * 1000),
                                spec.license, spec.languages),
                       "Muted"));
    m_progress = new QProgressBar;
    m_progress->setRange(0, 1000);
    m_progress->setFixedHeight(6);
    v->addWidget(m_progress);
    m_status = label({}, "Muted");
    v->addWidget(m_status);
    h->addLayout(v, 1);
    m_download = new QPushButton(icon("download"), tr("Download"));
    m_cancel = new QPushButton(tr("Pause"));
    m_remove = iconButton("trash", tr("Remove %1").arg(spec.name));
    h->addWidget(m_download, 0, Qt::AlignTop);
    h->addWidget(m_cancel, 0, Qt::AlignTop);
    h->addWidget(m_remove, 0, Qt::AlignTop);

    connect(m_download, &QPushButton::clicked, this, [this] {
        QString err;
        if (!m_ctx.models->download(m_spec.id, &err))
            QMessageBox::warning(this, tr("Cannot download"), err);
        refresh();
    });
    connect(m_cancel, &QPushButton::clicked, this, [this] { m_ctx.models->cancel(m_spec.id); });
    connect(m_remove, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, tr("Remove model?"),
                                  tr("%1 (%2) will be deleted from this computer. Your meetings are not affected; "
                                     "you can download the model again later.")
                                      .arg(m_spec.name, formatBytes(m_spec.bytes))) == QMessageBox::Yes)
            m_ctx.models->remove(m_spec.id);
    });
    connect(ctx.models, &ModelManager::changed, this, &ModelRow::refresh);
    connect(ctx.models, &ModelManager::progress, this, [this](const QString &id, qint64 got, qint64 total) {
        if (id != m_spec.id)
            return;
        m_progress->setValue(total > 0 ? int(got * 1000 / total) : 0);
        m_status->setText(tr("Downloading… %1 of %2").arg(formatBytes(got), formatBytes(total)));
    });
    connect(ctx.models, &ModelManager::verifying, this, [this](const QString &id) {
        if (id == m_spec.id)
            m_status->setText(tr("Checking the download…"));
    });
    connect(ctx.models, &ModelManager::finished, this, [this](const QString &id, bool ok, const QString &err) {
        if (id != m_spec.id)
            return;
        refresh();
        if (!ok)
            m_status->setText(err);
    });
    refresh();
}

void ModelRow::refresh()
{
    const bool installed = m_spec.installed();
    const bool downloading = m_ctx.models->isDownloading(m_spec.id);
    m_download->setVisible(!installed && !downloading);
    m_cancel->setVisible(downloading);
    m_remove->setVisible(installed);
    m_progress->setVisible(downloading);
    m_radio->setEnabled(installed);
    if (installed)
        m_status->setText(tr("Installed"));
    else if (!downloading)
        m_status->setText(QFileInfo::exists(m_spec.path() + ".part") ? tr("Download paused") : tr("Not downloaded"));
}

QWidget *modelList(AppContext ctx, ModelKind kind, QWidget *parent)
{
    auto *w = new QWidget(parent);
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    auto *group = new QButtonGroup(w);
    const QString selected = kind == ModelKind::Speech ? ctx.settings->speechModel() : ctx.settings->notesModel();
    for (const auto &spec : modelsOfKind(kind)) {
        auto *row = new ModelRow(ctx, spec, group, w);
        row->radio()->setChecked(spec.id == selected);
        QObject::connect(row->radio(), &QRadioButton::toggled, w, [ctx, kind, id = spec.id](bool on) {
            if (!on)
                return;
            if (kind == ModelKind::Speech)
                ctx.settings->setSpeechModel(id);
            else
                ctx.settings->setNotesModel(id);
        });
        v->addWidget(row);
    }
    return w;
}

// ---------------------------------------------------------------------------

SettingsView::SettingsView(AppContext ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
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
    v->setSpacing(10);

    auto *head = new QHBoxLayout;
    head->addWidget(label(tr("Settings"), "PageTitle"), 1);
    auto *close = new QPushButton(tr("Done"));
    connect(close, &QPushButton::clicked, this, &SettingsView::closeRequested);
    head->addWidget(close);
    v->addLayout(head);

    auto section = [&](const QString &title, const QString &help = {}) {
        v->addSpacing(16);
        v->addWidget(label(title, "SectionTitle"));
        if (!help.isEmpty())
            v->addWidget(label(help, "Muted"));
    };

    // General
    section(tr("General"));
    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);
    auto *theme = new QComboBox;
    theme->addItem(tr("Follow the system"), "system");
    theme->addItem(tr("Light"), "light");
    theme->addItem(tr("Dark"), "dark");
    theme->setCurrentIndex(std::max(0, theme->findData(ctx.settings->theme())));
    form->addRow(tr("Appearance"), theme);
    connect(theme, &QComboBox::currentIndexChanged, this, [this, theme] {
        m_ctx.settings->setTheme(theme->currentData().toString());
        Q_EMIT themeChanged();
    });
    auto *tray = new QCheckBox(tr("Keep running in the system tray when the window is closed"));
    tray->setChecked(ctx.settings->closeToTray());
    connect(tray, &QCheckBox::toggled, this, [this](bool on) { m_ctx.settings->setCloseToTray(on); });
    form->addRow(QString(), tray);
    v->addLayout(form);

    // Transcription
    section(tr("Transcription"),
            tr("Speech recognition runs entirely on this computer. Models are downloaded once from Hugging Face "
               "and checked against a known checksum; after that, no internet connection is needed."));
    auto *tf = new QFormLayout;
    auto *lang = new QComboBox;
    for (const auto &code : Settings::supportedLanguages())
        lang->addItem(Settings::languageName(code), code);
    lang->setCurrentIndex(std::max(0, lang->findData(ctx.settings->language())));
    connect(lang, &QComboBox::currentIndexChanged, this,
            [this, lang] { m_ctx.settings->setLanguage(lang->currentData().toString()); });
    tf->addRow(tr("Meeting language"), lang);
    tf->addRow(QString(), label(tr("Choosing the language is more accurate than automatic detection."), "Muted"));
    auto *autoT = new QCheckBox(tr("Transcribe automatically when a recording ends"));
    autoT->setChecked(ctx.settings->autoTranscribe());
    connect(autoT, &QCheckBox::toggled, this, [this](bool on) { m_ctx.settings->setAutoTranscribe(on); });
    tf->addRow(QString(), autoT);
    auto *quality = new QComboBox;
    quality->addItem(tr("Fast"), "fast");
    quality->addItem(tr("Balanced"), "balanced");
    quality->addItem(tr("Accurate (slower)"), "accurate");
    quality->setCurrentIndex(std::max(0, quality->findData(ctx.settings->quality())));
    connect(quality, &QComboBox::currentIndexChanged, this,
            [this, quality] { m_ctx.settings->setQuality(quality->currentData().toString()); });
    tf->addRow(tr("Decoding"), quality);
    v->addLayout(tf);
    v->addWidget(modelList(ctx, ModelKind::Speech));

    // Notes
    section(tr("Notes"),
            tr("Summaries, decisions and action items are written by a language model on this computer. Every "
               "item is linked to the transcript, and details the transcript does not support are left out."));
    auto *autoN = new QCheckBox(tr("Write notes automatically after transcription"));
    autoN->setChecked(ctx.settings->autoNotes());
    connect(autoN, &QCheckBox::toggled, this, [this](bool on) { m_ctx.settings->setAutoNotes(on); });
    v->addWidget(autoN);
    v->addWidget(modelList(ctx, ModelKind::Notes));

    // Performance
    section(tr("Performance"),
            tr("Processing runs at low priority and pauses completely while you record, so your call keeps the "
               "computer's full attention."));
    auto *pf = new QFormLayout;
    auto *threads = new QSpinBox;
    threads->setRange(0, std::max(1, QThread::idealThreadCount()));
    threads->setSpecialValueText(tr("Automatic"));
    threads->setValue(ctx.settings->threads());
    connect(threads, &QSpinBox::valueChanged, this, [this](int n) { m_ctx.settings->setThreads(n); });
    pf->addRow(tr("Processor threads"), threads);
    v->addLayout(pf);

    // Offline models
    section(tr("Computers without internet"),
            tr("Download the model files on another computer (the links are on the AfterLine website), copy them "
               "over, and import them here. AfterLine checks each file before using it."));
    auto *import = new QPushButton(icon("file"), tr("Import a model file…"));
    import->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto *importStatus = label({}, "Muted");
    connect(import, &QPushButton::clicked, this, [this, importStatus] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Import a model file"), {},
                                                          tr("Model files (*.bin *.gguf)"));
        if (path.isEmpty())
            return;
        importStatus->setText(tr("Checking the file…"));
        m_ctx.models->importFile(path);
    });
    connect(ctx.models, &ModelManager::imported, this,
            [importStatus](const QString &, bool, const QString &msg) { importStatus->setText(msg); });
    v->addWidget(import);
    v->addWidget(importStatus);

    // Privacy
    section(tr("Privacy and storage"));
    qint64 used = 0;
    QDirIterator it(meetingsDir(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        used += it.fileInfo().size();
    }
    v->addWidget(label(
        tr("Your recordings, transcripts and notes are stored only in %1 (%2 used, %3 free). AfterLine has no "
           "account, sends no telemetry, and never uploads your meetings. The only network access is downloading "
           "models when you ask for them; you can disconnect from the internet and everything else keeps working.")
            .arg(meetingsDir(), formatBytes(used), formatBytes(QStorageInfo(meetingsDir()).bytesAvailable()))));
    auto *open = new QPushButton(icon("folder"), tr("Open the data folder"));
    open->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(open, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(dataDir())); });
    v->addWidget(open);
    v->addWidget(label(tr("Recordings are not encrypted by AfterLine; use your system's disk encryption to protect "
                          "them if this computer is shared or could be lost."),
                       "Muted"));

    section(tr("About"));
    v->addWidget(label(tr("AfterLine %1 · Conversations, kept.<br>Open source under the MIT licence. Built with "
                          "Qt, PipeWire, Opus, SQLite, whisper.cpp and llama.cpp; see the third-party notices "
                          "installed with the application.<br><a href='https://afterline-linux.netlify.app'>Website</a>"
                          " · <a href='https://github.com/Artful-DevA/AfterLine'>Source code and issues</a>")
                           .arg(AFTERLINE_VERSION)));
    for (auto *l : content->findChildren<QLabel *>())
        if (l->text().contains("<a ")) {
            l->setOpenExternalLinks(true);
            l->setTextFormat(Qt::RichText);
        }
    v->addStretch();
}

}  // namespace afterline::ui
