// SPDX-License-Identifier: MIT
#include "audio/Recorder.h"

#include <algorithm>

namespace afterline::audio {

Recorder::Recorder(PipeWireGraph &graph) : m_graph(graph) {}

Recorder::~Recorder()
{
    if (active())
        stop(nullptr);
}

bool Recorder::startTrack(Track &track, const SourceChoice &choice, const std::string &audio,
                          const std::string &peaks, int bitrate, std::string *error)
{
    track.choice = choice;
    track.writer = std::make_unique<TrackWriter>();
    TrackWriter::Options wo;
    wo.audioPath = audio;
    wo.peaksPath = peaks;
    wo.bitrate = bitrate;
    wo.title = m_config.title;
    if (!track.writer->start(wo, error))
        return false;
    CaptureStream::Config cc;
    cc.kind = choice.kind;
    cc.target = choice.target;
    cc.description = CaptureStream::isInput(choice.kind) ? "AfterLine (microphone)"
                                                          : "AfterLine (" + choice.label + ")";
    TrackWriter *writer = track.writer.get();
    track.capture = std::make_unique<CaptureStream>(
        m_graph, cc, [writer](const float *data, size_t n) { writer->push(data, n); });
    if (!track.capture->start(error)) {
        track.writer->stop();
        return false;
    }
    return true;
}

bool Recorder::start(const RecorderConfig &config, std::string *error)
{
    if (active()) {
        if (error)
            *error = "Already recording";
        return false;
    }
    if (!config.computer && !config.microphone) {
        if (error)
            *error = "Choose something to record";
        return false;
    }
    m_config = config;
    const std::string dir = config.directory + "/";
    if (config.computer) {
        m_computer = std::make_unique<Track>();
        // Computer audio may carry music and several voices: a little more
        // bitrate than the microphone.
        if (!startTrack(*m_computer, *config.computer, dir + kComputerFile, dir + kComputerPeaks, 48000,
                        error)) {
            m_computer.reset();
            return false;
        }
    }
    if (config.microphone) {
        m_microphone = std::make_unique<Track>();
        if (!startTrack(*m_microphone, *config.microphone, dir + kMicrophoneFile, dir + kMicrophonePeaks,
                        32000, error)) {
            m_microphone.reset();
            if (m_computer) {
                m_computer->capture->stop();
                m_computer->writer->stop();
                m_computer.reset();
            }
            return false;
        }
    }
    m_state = RecorderStatus::Recording;
    return true;
}

void Recorder::pause()
{
    if (m_state != RecorderStatus::Recording)
        return;
    for (Track *t : {m_computer.get(), m_microphone.get()})
        if (t)
            t->writer->setPaused(true);
    m_state = RecorderStatus::Paused;
}

void Recorder::resume()
{
    if (m_state != RecorderStatus::Paused)
        return;
    for (Track *t : {m_computer.get(), m_microphone.get()})
        if (t)
            t->writer->setPaused(false);
    m_state = RecorderStatus::Recording;
}

bool Recorder::stop(std::string *error)
{
    if (!active())
        return true;
    bool ok = true;
    std::string errors;
    // Stop capture first so no callback touches a closing writer.
    for (Track *t : {m_computer.get(), m_microphone.get()})
        if (t)
            t->capture->stop();
    for (Track *t : {m_computer.get(), m_microphone.get()}) {
        if (t && !t->writer->stop()) {
            ok = false;
            errors += t->writer->error() + "\n";
        }
    }
    m_computer.reset();
    m_microphone.reset();
    m_state = RecorderStatus::Idle;
    if (error)
        *error = errors;
    return ok;
}

TrackStatus Recorder::trackStatus(const Track *t)
{
    TrackStatus s;
    if (!t)
        return s;
    s.enabled = true;
    s.label = t->choice.label;
    s.state = t->capture->state();
    s.message = t->capture->message();
    s.level = t->writer->level();
    s.secondsSinceData = t->writer->secondsSinceData();
    s.linkedStreams = t->capture->linkedStreams();
    s.reconnects = t->capture->reconnects();
    s.seconds = t->writer->seconds();
    s.paddedSeconds = t->writer->paddedSeconds();
    s.writeFailed = t->writer->failed();
    s.writeError = t->writer->error();
    return s;
}

RecorderStatus Recorder::status() const
{
    RecorderStatus s;
    s.state = m_state;
    s.computer = trackStatus(m_computer.get());
    s.microphone = trackStatus(m_microphone.get());
    s.seconds = std::max(s.computer.seconds, s.microphone.seconds);
    return s;
}

}  // namespace afterline::audio
