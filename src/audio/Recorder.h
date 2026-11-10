// SPDX-License-Identifier: MIT
//
// A recording session: up to two independent tracks (computer audio and
// microphone), each captured by a CaptureStream and written by a TrackWriter.
// Separate tracks mean the transcript can tell "you" from "the call", and
// either track can be replayed or reprocessed on its own.
#pragma once

#include "audio/CaptureStream.h"
#include "audio/TrackWriter.h"

#include <memory>
#include <optional>
#include <string>

namespace afterline::audio {

struct SourceChoice {
    CaptureStream::Kind kind = CaptureStream::Kind::DefaultOutput;
    std::string target;  // node.name or appKey
    std::string label;   // for people: "Firefox", "Built-in Audio Speakers"
};

struct RecorderConfig {
    std::string directory;
    std::optional<SourceChoice> computer;
    std::optional<SourceChoice> microphone;
    std::string title;
};

struct TrackStatus {
    bool enabled = false;
    std::string label;
    CaptureStream::State state = CaptureStream::State::Idle;
    std::string message;
    float level = 0;
    double secondsSinceData = -1;
    int linkedStreams = 0;
    int reconnects = 0;
    double seconds = 0;
    double paddedSeconds = 0;
    bool writeFailed = false;
    std::string writeError;
};

struct RecorderStatus {
    enum State { Idle, Recording, Paused } state = Idle;
    double seconds = 0;
    TrackStatus computer;
    TrackStatus microphone;
};

class Recorder {
public:
    explicit Recorder(PipeWireGraph &graph);
    ~Recorder();

    bool start(const RecorderConfig &config, std::string *error);
    void pause();
    void resume();
    // Stops capture and closes the files. Returns false if a track could not
    // be finalised (its audio up to the failure is still on disk).
    bool stop(std::string *error);

    bool active() const { return m_state != RecorderStatus::Idle; }
    RecorderStatus status() const;

    static constexpr const char *kComputerFile = "computer.opus";
    static constexpr const char *kMicrophoneFile = "microphone.opus";
    static constexpr const char *kComputerPeaks = "computer.peaks";
    static constexpr const char *kMicrophonePeaks = "microphone.peaks";

private:
    struct Track {
        SourceChoice choice;
        std::unique_ptr<TrackWriter> writer;
        std::unique_ptr<CaptureStream> capture;
    };
    bool startTrack(Track &track, const SourceChoice &choice, const std::string &audio,
                    const std::string &peaks, int bitrate, std::string *error);
    static TrackStatus trackStatus(const Track *t);

    PipeWireGraph &m_graph;
    RecorderConfig m_config;
    std::unique_ptr<Track> m_computer;
    std::unique_ptr<Track> m_microphone;
    RecorderStatus::State m_state = RecorderStatus::Idle;
};

}  // namespace afterline::audio
