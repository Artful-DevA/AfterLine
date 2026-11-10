// SPDX-License-Identifier: MIT
//
// Writes one recording track on its own thread.
//
// Capture callbacks only append to a small buffer; encoding and disk writes
// happen here, so a slow disk can never stall the PipeWire graph (and with it
// the meeting application's audio).
//
// Every track is kept on the same wall-clock timeline: when a source delivers
// nothing (the application is silent and PipeWire stops scheduling it, a
// headset was unplugged, the output was suspended), silence is written so the
// microphone and computer tracks stay aligned and timestamps stay true.
#pragma once

#include "core/OggOpus.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace afterline::audio {

class TrackWriter {
public:
    struct Options {
        std::string audioPath;
        std::string peaksPath;     // 20 peak values per second, one byte each
        int bitrate = 40000;
        std::string title;
        double gapSeconds = 0.3;   // fill with silence after this long without data
    };

    TrackWriter() = default;
    ~TrackWriter();
    TrackWriter(const TrackWriter &) = delete;
    TrackWriter &operator=(const TrackWriter &) = delete;

    bool start(const Options &options, std::string *error);
    // Thread-safe; called from the PipeWire thread.
    void push(const float *samples, size_t count);
    void setPaused(bool paused);
    // Stops, writes the remaining audio and closes the file.
    bool stop();

    bool running() const { return m_thread.joinable(); }
    double seconds() const { return m_written.load() / double(kOpusRate); }
    // Peak level of roughly the last 100 ms (0..1).
    float level() const { return m_level.load(); }
    // Seconds since real audio last arrived (-1 if never).
    double secondsSinceData() const;
    // Seconds of silence inserted to bridge gaps.
    double paddedSeconds() const { return m_padded.load() / double(kOpusRate); }
    bool failed() const { return m_failed.load(); }
    std::string error() const;

    static constexpr int kPeaksPerSecond = 20;

private:
    void run();
    void writeBlock(const float *data, size_t n);

    Options m_options;
    OggOpusWriter m_writer;
    FILE *m_peaks = nullptr;
    std::thread m_thread;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<float> m_incoming;
    bool m_stop = false;
    bool m_paused = false;
    std::chrono::steady_clock::time_point m_lastData{};
    bool m_hadData = false;

    // Writer-thread state.
    std::chrono::steady_clock::duration m_activeTime{};
    std::chrono::steady_clock::time_point m_tickTime{};
    float m_peakAcc = 0.f;
    int m_peakCount = 0;

    std::atomic<uint64_t> m_written{0};
    std::atomic<uint64_t> m_padded{0};
    std::atomic<float> m_level{0.f};
    std::atomic<bool> m_failed{false};
    std::string m_error;
};

}  // namespace afterline::audio
