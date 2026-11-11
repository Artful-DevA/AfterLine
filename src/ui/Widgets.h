// SPDX-License-Identifier: MIT
#pragma once

#include <QCheckBox>
#include <QLabel>
#include <QStyledItemDelegate>
#include <QVariantAnimation>
#include <QTimer>
#include <QVector>
#include <QWidget>

class QHBoxLayout;
class QPushButton;
class QVBoxLayout;

namespace afterline::ui {

// Horizontal audio level, smoothed, with a peak hold.
class LevelMeter : public QWidget {
    Q_OBJECT
public:
    explicit LevelMeter(QWidget *parent = nullptr);
    void setLevel(float peak);  // 0..1 linear
    void setActive(bool active);
    QSize sizeHint() const override { return {160, 8}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    float m_level = 0, m_hold = 0;
    bool m_active = true;
};

// The unmistakable "recording" dot: pulses while recording, steady when paused.
class RecordingDot : public QWidget {
    Q_OBJECT
public:
    explicit RecordingDot(QWidget *parent = nullptr);
    void setPaused(bool paused);
    QSize sizeHint() const override { return {18, 18}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QTimer m_timer;
    int m_phase = 0;
    bool m_paused = false;
};

// Both tracks' waveforms (computer above the centre line, microphone below),
// the playhead, and markers for recording events. Click or drag to seek.
class Waveform : public QWidget {
    Q_OBJECT
public:
    explicit Waveform(QWidget *parent = nullptr);
    void load(const QString &computerPeaks, const QString &microphonePeaks, double duration);
    void setPosition(double seconds);
    void setMarkers(const QVector<QPair<double, QString>> &markers);
    void setCompact(bool compact) { m_compact = compact; update(); }
    QSize sizeHint() const override { return {400, m_compact ? 40 : 96}; }
    QSize minimumSizeHint() const override { return {120, 32}; }

Q_SIGNALS:
    void seekRequested(double seconds);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;

private:
    double timeAt(int x) const;
    QByteArray m_computer, m_microphone;
    double m_duration = 0, m_position = 0;
    QVector<QPair<double, QString>> m_markers;
    bool m_compact = false;
};

// An on/off switch. It is a QCheckBox (same signals), drawn as a switch with
// the text on the left and an optional description below.
class ToggleSwitch : public QCheckBox {
    Q_OBJECT
public:
    explicit ToggleSwitch(const QString &text, QWidget *parent = nullptr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return {120, 28}; }

protected:
    void paintEvent(QPaintEvent *) override;
    bool hitButton(const QPoint &) const override { return true; }
    void checkStateSet() override;
    void nextCheckState() override;

private:
    QVariantAnimation m_anim;
    double m_pos = 0;
};

// Combo box items with an icon, a title and a muted second line
// (Qt::UserRole + 1), with separators drawn as thin rules.
class TwoLineDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &index) const override;
};

QLabel *label(const QString &text, const char *objectName = nullptr, QWidget *parent = nullptr);
QWidget *hline(QWidget *parent = nullptr);
QPushButton *iconButton(const QString &iconName, const QString &tooltip, QWidget *parent = nullptr);

}  // namespace afterline::ui
