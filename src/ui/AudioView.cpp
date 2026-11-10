// SPDX-License-Identifier: MIT
#include "ui/AudioView.h"

#include "app/ModelCatalog.h"
#include "audio/Recorder.h"
#include "core/Transcript.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QComboBox>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

namespace afterline::ui {

AudioView::AudioView(QWidget *parent) : QWidget(parent)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 14, 0, 0);
    v->setSpacing(12);
    m_wave = new Waveform;
    v->addWidget(m_wave);
    auto *legend = new QHBoxLayout;
    auto dot = [](Token t) {
        auto *l = new QLabel;
        l->setFixedSize(10, 10);
        l->setStyleSheet(QString("background:%1;border-radius:5px;").arg(Theme::hex(t)));
        return l;
    };
    legend->addWidget(dot(Token::TrackComputer));
    legend->addWidget(label(tr("Meeting audio (top)"), "Muted"));
    legend->addSpacing(14);
    legend->addWidget(dot(Token::TrackMicrophone));
    legend->addWidget(label(tr("Your microphone (bottom)"), "Muted"));
    legend->addStretch();
    v->addLayout(legend);
    for (auto *l : findChildren<QLabel *>())
        l->setWordWrap(false);

    auto *g = new QGridLayout;
    g->setHorizontalSpacing(14);
    m_computerLabel = label(tr("Meeting audio volume"));
    m_microphoneLabel = label(tr("Microphone volume"));
    m_computer = new QSlider(Qt::Horizontal);
    m_microphone = new QSlider(Qt::Horizontal);
    for (auto *s : {m_computer, m_microphone}) {
        s->setRange(0, 200);
        s->setValue(100);
        s->setMaximumWidth(260);
        connect(s, &QSlider::valueChanged, this,
                [this] { Q_EMIT volumesChanged(m_computer->value() / 100.f, m_microphone->value() / 100.f); });
    }
    m_computer->setAccessibleName(tr("Meeting audio volume"));
    m_microphone->setAccessibleName(tr("Microphone volume"));
    for (auto *l : {m_computerLabel, m_microphoneLabel})
        l->setWordWrap(false);
    g->addWidget(m_computerLabel, 0, 0);
    g->addWidget(m_computer, 0, 1);
    g->addWidget(m_microphoneLabel, 1, 0);
    g->addWidget(m_microphone, 1, 1);
    g->addWidget(label(tr("Speed")), 2, 0);
    m_rate = new QComboBox;
    for (double r : {0.75, 1.0, 1.25, 1.5, 1.75, 2.0})
        m_rate->addItem(QString("%1×").arg(r), r);
    m_rate->setCurrentIndex(1);
    m_rate->setAccessibleName(tr("Playback speed"));
    g->addWidget(m_rate, 2, 1, Qt::AlignLeft);
    g->setColumnStretch(2, 1);
    v->addLayout(g);
    connect(m_rate, &QComboBox::currentIndexChanged, this, [this] { Q_EMIT rateChanged(m_rate->currentData().toDouble()); });

    v->addWidget(label(tr("What happened during the recording"), "SectionTitle"));
    m_events = new QListWidget;
    m_events->setAccessibleName(tr("Recording events"));
    m_events->setMaximumHeight(180);
    v->addWidget(m_events);

    auto *files = new QHBoxLayout;
    m_files = label({}, "Muted");
    files->addWidget(m_files, 1);
    auto *show = new QPushButton(icon("folder"), tr("Show files"));
    connect(show, &QPushButton::clicked, this, &AudioView::showFilesRequested);
    files->addWidget(show);
    v->addLayout(files);
    v->addStretch();

    connect(m_wave, &Waveform::seekRequested, this, &AudioView::seekRequested);
    connect(m_events, &QListWidget::itemActivated, this,
            [this](QListWidgetItem *i) { Q_EMIT seekRequested(i->data(Qt::UserRole).toDouble()); });
}

void AudioView::load(const MeetingInfo &m, double duration)
{
    m_wave->load(m.path(audio::Recorder::kComputerPeaks), m.path(audio::Recorder::kMicrophonePeaks), duration);
    QVector<QPair<double, QString>> markers;
    m_events->clear();
    for (const auto v : m.events) {
        const QJsonObject e = v.toObject();
        const double t = e["t"].toDouble();
        auto *item = new QListWidgetItem(QString("%1   %2").arg(formatTime(t), e["message"].toString()));
        item->setData(Qt::UserRole, t);
        const QString type = e["type"].toString();
        const bool problem = type == "source-lost" || type == "write-error" || type == "disk-full" ||
                             type == "recovered" || type == "audio-service" || type == "suspended";
        item->setIcon(icon(problem ? "alert" : "clock", problem ? Theme::color(Token::Warning) : Theme::color(Token::Muted)));
        m_events->addItem(item);
        if (problem || type == "paused")
            markers.push_back({t, e["message"].toString()});
    }
    if (m_events->count() == 0)
        m_events->addItem(tr("Nothing unusual."));
    m_wave->setMarkers(markers);
    const bool comp = m.hasComputerAudio(), mic = m.hasMicrophoneAudio();
    m_computer->setEnabled(comp);
    m_microphone->setEnabled(mic);
    qint64 bytes = 0;
    for (const char *f : {audio::Recorder::kComputerFile, audio::Recorder::kMicrophoneFile})
        bytes += QFileInfo(m.path(f)).size();
    QStringList tracks;
    if (comp)
        tracks << tr("meeting audio");
    if (mic)
        tracks << tr("microphone");
    m_files->setText(tr("%1 · %2 · stored only on this computer").arg(tracks.join(tr(" and ")), formatBytes(bytes)));
}

void AudioView::setPosition(double seconds) { m_wave->setPosition(seconds); }

}  // namespace afterline::ui
