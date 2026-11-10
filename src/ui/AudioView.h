// SPDX-License-Identifier: MIT
#pragma once

#include "app/MeetingStore.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QListWidget;
class QSlider;

namespace afterline::ui {

class Waveform;

class AudioView : public QWidget {
    Q_OBJECT
public:
    explicit AudioView(QWidget *parent = nullptr);
    void load(const MeetingInfo &meeting, double duration);
    void setPosition(double seconds);

Q_SIGNALS:
    void seekRequested(double seconds);
    void volumesChanged(float computer, float microphone);
    void rateChanged(double rate);
    void showFilesRequested();

private:
    Waveform *m_wave;
    QSlider *m_computer, *m_microphone;
    QLabel *m_computerLabel, *m_microphoneLabel;
    QComboBox *m_rate;
    QListWidget *m_events;
    QLabel *m_files;
};

}  // namespace afterline::ui
