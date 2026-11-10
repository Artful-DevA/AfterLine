// SPDX-License-Identifier: MIT
//
// One captured audio source, delivered as mono 48 kHz float.
//
// Application capture does not depend on a single stream id. AfterLine
// creates its own unconnected capture node and links into it every output
// stream that belongs to the chosen application, whenever such a stream
// appears: a browser opening a second stream for a call, Zoom restarting its
// audio engine, a tab being reloaded. Other applications are never linked, so
// music playing elsewhere is not recorded.
#pragma once

#include "audio/PipeWireGraph.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <set>
#include <string>

struct pw_stream;

namespace afterline::audio {

class CaptureStream {
public:
    enum class Kind {
        DefaultOutput,  // whatever plays through the default output (follows changes)
        OutputDevice,   // one output device, by node.name
        Application,    // every stream of one application, by appKey()
        DefaultInput,   // the default microphone (follows changes)
        InputDevice,    // one microphone, by node.name
    };
    enum class State { Idle, Waiting, Active, Lost, Failed };

    struct Config {
        Kind kind = Kind::DefaultOutput;
        std::string target;       // node.name or appKey, depending on kind
        std::string description;  // shown in pavucontrol and friends
    };

    using DataFn = std::function<void(const float *mono, size_t frames)>;

    CaptureStream(PipeWireGraph &graph, Config config, DataFn onData);
    ~CaptureStream();
    CaptureStream(const CaptureStream &) = delete;
    CaptureStream &operator=(const CaptureStream &) = delete;

    bool start(std::string *error);
    void stop();

    State state() const { return m_state.load(); }
    // Application capture: how many of the application's streams are linked.
    int linkedStreams() const { return m_linkedStreams.load(); }
    // Times the source vanished and came back (device replugged, app restarted).
    int reconnects() const { return m_reconnects.load(); }
    std::string message() const;
    const Config &config() const { return m_config; }
    static bool isInput(Kind k) { return k == Kind::DefaultInput || k == Kind::InputDevice; }

    struct Impl;

private:
    PipeWireGraph &m_graph;
    Config m_config;
    std::unique_ptr<Impl> d;
    std::atomic<State> m_state{State::Idle};
    std::atomic<int> m_linkedStreams{0};
    std::atomic<int> m_reconnects{0};
    mutable std::mutex m_messageMutex;
    std::string m_message;
    friend struct Impl;
    void setState(State s, const std::string &message);
};

const char *stateName(CaptureStream::State s);

}  // namespace afterline::audio
