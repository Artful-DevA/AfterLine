// SPDX-License-Identifier: MIT
//
// Plays a meeting: the computer and microphone tracks mixed together (each
// with its own volume) through PipeWire, with sample-accurate seeking so a
// click on a transcript line plays exactly that moment.
#pragma once

#include "audio/PipeWireGraph.h"
#include "core/OggOpus.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct pw_stream;

namespace afterline::audio {

class Player {
public:
    explicit Player(PipeWireGraph &graph);
    ~Player();

    // Paths may be empty (track absent).
    bool open(const std::string &computer, const std::string &microphone, std::string *error);
    void close();
    bool play();
    void pause();
    bool playing() const { return m_playing.load(); }
    void seek(double seconds);
    double position() const;
    double duration() const { return m_duration; }
    void setVolumes(float computer, float microphone);
    void setRate(double rate);  // 0.5 .. 2.0, pitch is not preserved
    bool finished() const { return m_finished.load(); }

    struct Impl;

private:
    PipeWireGraph &m_graph;
    std::unique_ptr<Impl> d;
    std::atomic<bool> m_playing{false};
    std::atomic<bool> m_finished{false};
    double m_duration = 0;
};

}  // namespace afterline::audio
