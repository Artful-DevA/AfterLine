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

#include <algorithm>

namespace afterline::ui {

namespace {
QWidget *card(QVBoxLayout *&inner)
{
    auto *w = new QWidget;
    w->setObjectName("Card");
    w->setAttribute(Qt::WA_StyledBackground);
    inner = new QVBoxLayout(w);
    inner->setContentsMargins(18, 14, 18, 16);
    inner->setSpacing(10);
    return w;
}
}  // namespace

AudioView::AudioView(QWidget *parent) : QWidget(parent)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 14, 0, 0);
    v->setSpacing(12);

    // Both tracks.
    QVBoxLayout *wv;
    v->addWidget(card(wv));
    m_wave = new Waveform;
    wv->addWidget(m_wave);
    auto *legend = new QHBoxLayout;
    auto dot = [](Token t) {
        auto *l = new QLabel;
        l->setFixedSize(10, 10);
        l->setStyleSheet(QString("background:%1;border-radius:5px;").arg(Theme::hex(t)));
        return l;
    };
    legend->addWidget(dot(Token::TrackComputer));
    legend->addWidget(label(tr("Others in the call (top)"), "Muted"));
    legend->addSpacing(14);
    legend->addWidget(dot(Token::TrackMicrophone));
    legend->addWidget(label(tr("You (bottom)"), "Muted"));
    legend->addStretch();
    legend->addWidget(label(tr("Click to play from there"), "Muted"));
    wv->addLayout(legend);
    for (int i = 0; i < legend->count(); ++i)
        if (auto *l = qobject_cast<QLabel *>(legend->itemAt(i)->widget()))
            l->setWordWrap(false);

    // Playback.
    QVBoxLayout *pv;
    v->addWidget(card(pv));
    pv->addWidget(label(tr("Playback"), "SectionTitle"));
    auto *row = new QHBoxLayout;
    row->setSpacing(24);
    m_computerLabel = label(tr("Others' volume"));
    m_microphoneLabel = label(tr("Your volume"));
    m_computer = new QSlider(Qt::Horizontal);
    m_microphone = new QSlider(Qt::Horizontal);
    for (auto *s : {m_computer, m_microphone}) {
        s->setRange(0, 200);
        s->setValue(100);
        s->setMinimumWidth(120);
        connect(s, &QSlider::valueChanged, this,
                [this] { Q_EMIT volumesChanged(m_computer->value() / 100.f, m_microphone->value() / 100.f); });
    }
    m_computer->setAccessibleName(tr("Others' volume"));
    m_microphone->setAccessibleName(tr("Your volume"));
    auto column = [&](QLabel *l, QWidget *w) {
        auto *c = new QVBoxLayout;
        c->setSpacing(6);
        l->setWordWrap(false);
        l->setObjectName("Muted");
        c->addWidget(l);
        c->addWidget(w);
        row->addLayout(c, 1);
    };
    column(m_computerLabel, m_computer);
    column(m_microphoneLabel, m_microphone);
    m_rate = new QComboBox;
    for (double r : {0.75, 1.0, 1.25, 1.5, 1.75, 2.0})
        m_rate->addItem(QString("%1×").arg(r), r);
    m_rate->setCurrentIndex(1);
    m_rate->setAccessibleName(tr("Playback speed"));
    column(label(tr("Speed")), m_rate);
    pv->addLayout(row);
    connect(m_rate, &QComboBox::currentIndexChanged, this, [this] { Q_EMIT rateChanged(m_rate->currentData().toDouble()); });

    // What happened while recording.
    QVBoxLayout *ev;
    v->addWidget(card(ev));
    ev->addWidget(label(tr("During the recording"), "SectionTitle"));
    m_events = new QListWidget;
    m_events->setAccessibleName(tr("Recording events"));
    m_events->setStyleSheet("QListWidget { background: transparent; } QListWidget::item { padding: 4px 0; }");
    m_events->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    ev->addWidget(m_events);
    auto *files = new QHBoxLayout;
    m_files = label({}, "Muted");
    files->addWidget(m_files, 1);
    auto *show = new QPushButton(icon("folder"), tr("Show files"));
    connect(show, &QPushButton::clicked, this, &AudioView::showFilesRequested);
    files->addWidget(show);
    ev->addLayout(files);
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
    // As tall as its entries, up to a point.
    const int rowH = std::max(28, m_events->sizeHintForRow(0));
    m_events->setFixedHeight(std::min(8, m_events->count()) * rowH + 6);
    m_wave->setMarkers(markers);
    const bool comp = m.hasComputerAudio(), mic = m.hasMicrophoneAudio();
    m_computer->setEnabled(comp);
    m_microphone->setEnabled(mic);
    qint64 bytes = 0;
    for (const char *f : {audio::Recorder::kComputerFile, audio::Recorder::kMicrophoneFile})
        bytes += QFileInfo(m.path(f)).size();
    QStringList tracks;
    if (comp)
        tracks << tr("others");
    if (mic)
        tracks << tr("you");
    m_files->setText((tracks.size() == 1 ? tr("1 track") : tr("%1 tracks").arg(tracks.size())) +
                     QString(" (%1) · %2 · ").arg(tracks.join(tr(" and ")), formatBytes(bytes)) +
                     tr("stored only on this computer"));
}

void AudioView::setPosition(double seconds) { m_wave->setPosition(seconds); }

}  // namespace afterline::ui
