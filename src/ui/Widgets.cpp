// SPDX-License-Identifier: MIT
#include "ui/Widgets.h"

#include "audio/TrackWriter.h"
#include "core/Transcript.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QFile>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace afterline::ui {

LevelMeter::LevelMeter(QWidget *parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(8);
    setAccessibleName(tr("Audio level"));
}

void LevelMeter::setLevel(float peak)
{
    // Perceptual scale: -60 dBFS .. 0 dBFS.
    const float db = peak > 1e-6f ? 20.f * std::log10(peak) : -100.f;
    const float v = std::clamp((db + 60.f) / 60.f, 0.f, 1.f);
    m_level = std::max(v, m_level * 0.7f);
    m_hold = std::max(v, m_hold - 0.015f);
    update();
}

void LevelMeter::setActive(bool active)
{
    m_active = active;
    if (!active)
        m_level = m_hold = 0;
    update();
}

void LevelMeter::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::color(Token::SurfaceAlt));
    p.drawRoundedRect(r, 4, 4);
    if (!m_active)
        return;
    QColor c = m_level > 0.95f ? Theme::color(Token::Warning) : Theme::color(Token::Success);
    p.setBrush(c);
    p.drawRoundedRect(QRectF(r.x(), r.y(), r.width() * m_level, r.height()), 4, 4);
    p.setBrush(Theme::color(Token::Muted));
    p.drawRect(QRectF(r.x() + r.width() * m_hold - 1, r.y(), 2, r.height()));
}

RecordingDot::RecordingDot(QWidget *parent) : QWidget(parent)
{
    setFixedSize(18, 18);
    m_timer.setInterval(60);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        m_phase = (m_phase + 1) % 40;
        update();
    });
    m_timer.start();
}

void RecordingDot::setPaused(bool paused)
{
    m_paused = paused;
    update();
}

void RecordingDot::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QColor c = m_paused ? Theme::color(Token::Muted) : Theme::color(Token::Recording);
    if (!m_paused) {
        QColor halo = c;
        const double t = std::sin(m_phase / 40.0 * 2 * M_PI) * 0.5 + 0.5;
        halo.setAlphaF(0.15 + 0.25 * t);
        p.setPen(Qt::NoPen);
        p.setBrush(halo);
        p.drawEllipse(QRectF(0, 0, 18, 18));
    }
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawEllipse(QRectF(4, 4, 10, 10));
}

Waveform::Waveform(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(96);
    setCursor(Qt::PointingHandCursor);
    setAccessibleName(tr("Recording waveform; click to play from a point"));
}

void Waveform::load(const QString &computerPeaks, const QString &microphonePeaks, double duration)
{
    auto read = [](const QString &path) {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    };
    m_computer = read(computerPeaks);
    m_microphone = read(microphonePeaks);
    m_duration = duration;
    setFixedHeight(m_compact ? 40 : 96);
    update();
}

void Waveform::setPosition(double seconds)
{
    m_position = seconds;
    update();
}

void Waveform::setMarkers(const QVector<QPair<double, QString>> &markers)
{
    m_markers = markers;
    update();
}

double Waveform::timeAt(int x) const
{
    return m_duration <= 0 ? 0 : std::clamp(double(x) / std::max(1, width()) * m_duration, 0.0, m_duration);
}

void Waveform::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int w = width(), h = height();
    const double mid = m_microphone.isEmpty() ? h - 2 : h / 2.0;
    const double playX = m_duration > 0 ? m_position / m_duration * w : 0;
    const int perSecond = audio::TrackWriter::kPeaksPerSecond;
    const int step = 3;  // a bar every 3 px

    auto drawTrack = [&](const QByteArray &peaks, QColor color, bool up) {
        if (peaks.isEmpty() || m_duration <= 0)
            return;
        const double span = up ? mid - 2 : h - mid - 2;
        for (int x = 0; x < w; x += step) {
            const int from = int(timeAt(x) * perSecond), to = std::max(from + 1, int(timeAt(x + step) * perSecond));
            int peak = 0;
            for (int i = from; i < to && i < peaks.size(); ++i)
                peak = std::max(peak, int(uchar(peaks[i])));
            const double len = std::max(1.0, peak / 255.0 * span);
            QColor c = color;
            if (x > playX)
                c.setAlphaF(0.38);
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawRoundedRect(QRectF(x, up ? mid - len : mid, step - 1, len), 1, 1);
        }
    };
    drawTrack(m_computer, Theme::color(Token::TrackComputer), true);
    drawTrack(m_microphone, Theme::color(Token::TrackMicrophone), false);

    // Event markers (disconnects, pauses).
    for (const auto &m : m_markers) {
        if (m_duration <= 0)
            break;
        const double x = m.first / m_duration * w;
        p.setPen(QPen(Theme::color(Token::Warning), 1, Qt::DashLine));
        p.drawLine(QPointF(x, 0), QPointF(x, h));
    }
    // Playhead.
    p.setPen(QPen(Theme::color(Token::Text), 1.5));
    p.drawLine(QPointF(playX, 0), QPointF(playX, h));
}

void Waveform::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && m_duration > 0)
        Q_EMIT seekRequested(timeAt(int(e->position().x())));
}

void Waveform::mouseMoveEvent(QMouseEvent *e)
{
    if (m_duration <= 0)
        return;
    const double t = timeAt(int(e->position().x()));
    if (e->buttons() & Qt::LeftButton)
        Q_EMIT seekRequested(t);
    QString tip = formatTime(t);
    for (const auto &m : m_markers)
        if (std::abs(m.first - t) < m_duration / std::max(1, width()) * 4)
            tip += "\n" + m.second;
    QToolTip::showText(e->globalPosition().toPoint(), tip, this);
}

QLabel *label(const QString &text, const char *objectName, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    if (objectName)
        l->setObjectName(objectName);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    l->setOpenExternalLinks(false);
    return l;
}

QWidget *hline(QWidget *parent)
{
    auto *w = new QWidget(parent);
    w->setFixedHeight(1);
    w->setAutoFillBackground(true);
    w->setStyleSheet(QString("background: %1;").arg(Theme::hex(Token::Border)));
    return w;
}

QPushButton *iconButton(const QString &iconName, const QString &tooltip, QWidget *parent)
{
    auto *b = new QPushButton(parent);
    b->setIcon(icon(iconName));
    b->setIconSize({18, 18});
    b->setToolTip(tooltip);
    b->setAccessibleName(tooltip);
    b->setFlat(true);
    b->setProperty("flat", true);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

}  // namespace afterline::ui
