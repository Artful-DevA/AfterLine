// SPDX-License-Identifier: MIT
#include "audio/CaptureStream.h"

#include <unistd.h>

#include <pipewire/pipewire.h>
#include "audio/PwCompat.h"
#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>

#include <map>
#include <vector>

namespace afterline::audio {

const char *stateName(CaptureStream::State s)
{
    switch (s) {
    case CaptureStream::State::Idle: return "idle";
    case CaptureStream::State::Waiting: return "waiting";
    case CaptureStream::State::Active: return "active";
    case CaptureStream::State::Lost: return "lost";
    case CaptureStream::State::Failed: return "failed";
    }
    return "?";
}

namespace {

// Which of our two capture channels an application port feeds.
std::vector<std::string> targetChannels(const std::string &channel)
{
    if (channel == "FL" || channel == "RL" || channel == "SL" || channel == "FLC")
        return {"FL"};
    if (channel == "FR" || channel == "RR" || channel == "SR" || channel == "FRC")
        return {"FR"};
    if (channel == "LFE")
        return {};
    return {"FL", "FR"};  // MONO, FC, unknown/AUX
}

}  // namespace

struct CaptureStream::Impl {
    CaptureStream *q;
    DataFn onData;
    pw_stream *stream = nullptr;
    spa_hook streamListener{};
    int graphListener = 0;
    int channels = 2;
    uint32_t ourNode = SPA_ID_INVALID;
    bool stopping = false;
    bool targetSeen = false;                                   // explicit device was present
    std::set<uint32_t> sources;                                // nodes whose audio we link in
    std::map<std::pair<uint32_t, uint32_t>, pw_proxy *> links; // (their port, our port)
    std::vector<float> mono;

    PipeWireGraph &graph() { return q->m_graph; }
    const Config &config() { return q->m_config; }
    Kind kind() { return config().kind; }
    bool isApp() { return kind() == Kind::Application; }
    bool fromOutput() { return kind() == Kind::DefaultOutput || kind() == Kind::OutputDevice; }
    bool explicitDevice() { return kind() == Kind::OutputDevice || kind() == Kind::InputDevice; }

    static void onStateChanged(void *data, pw_stream_state, pw_stream_state state, const char *error)
    {
        auto *self = static_cast<Impl *>(data);
        if (self->stopping)
            return;
        if (state == PW_STREAM_STATE_PAUSED || state == PW_STREAM_STATE_STREAMING) {
            const uint32_t id = pw_stream_get_node_id(self->stream);
            if (id != self->ourNode) {
                self->ourNode = id;
                self->recompute();
            }
        } else if (state == PW_STREAM_STATE_ERROR) {
            self->q->setState(State::Failed, error ? error : "The capture stream failed");
        }
    }

    static void onProcess(void *data)
    {
        auto *self = static_cast<Impl *>(data);
        pw_buffer *b = pw_stream_dequeue_buffer(self->stream);
        if (!b)
            return;
        spa_buffer *buf = b->buffer;
        if (buf->datas[0].data && buf->datas[0].chunk) {
            const auto *in = reinterpret_cast<const float *>(
                static_cast<const uint8_t *>(buf->datas[0].data) + buf->datas[0].chunk->offset);
            const size_t frames = buf->datas[0].chunk->size / (sizeof(float) * size_t(self->channels));
            if (frames) {
                if (self->channels == 1) {
                    self->onData(in, frames);
                } else {
                    self->mono.resize(frames);
                    for (size_t i = 0; i < frames; ++i)
                        self->mono[i] = 0.5f * (in[2 * i] + in[2 * i + 1]);
                    self->onData(self->mono.data(), frames);
                }
            }
        }
        pw_stream_queue_buffer(self->stream, b);
    }

    // AfterLine links every source itself (devices as well as applications),
    // so recording does not depend on how a particular session manager
    // (WirePlumber, pipewire-media-session) and version interpret targets.
    bool createStream(std::string *error)
    {
        const bool input = CaptureStream::isInput(kind());
        channels = input ? 1 : 2;
        pw_properties *props = pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture",
            PW_KEY_APP_NAME, "AfterLine", PW_KEY_APP_ICON_NAME, "io.github.Artful_DevA.AfterLine",
            PW_KEY_NODE_NAME, input ? "afterline-microphone" : "afterline-computer-audio",
            PW_KEY_NODE_DESCRIPTION,
            config().description.empty() ? "AfterLine recording" : config().description.c_str(),
            PW_KEY_NODE_AUTOCONNECT, "false",
            // Keep our node scheduled so links made while a source is silent
            // deliver the moment it plays.
            PW_KEY_NODE_WANT_DRIVER, "true", nullptr);
        pw_properties_setf(props, kOwnerKey, "%d", int(::getpid()));
        stream = pw_stream_new(graph().core(), "AfterLine capture", props);
        if (!stream) {
            if (error)
                *error = "Could not create a PipeWire capture stream";
            return false;
        }
        static const pw_stream_events events = [] {
            pw_stream_events e{};
            e.version = PW_VERSION_STREAM_EVENTS;
            e.state_changed = onStateChanged;
            e.process = onProcess;
            return e;
        }();
        pw_stream_add_listener(stream, &streamListener, &events, this);

        spa_audio_info_raw info{};
        info.format = SPA_AUDIO_FORMAT_F32;
        info.rate = 48000;
        info.channels = uint32_t(channels);
        if (channels == 1) {
            info.position[0] = SPA_AUDIO_CHANNEL_MONO;
        } else {
            info.position[0] = SPA_AUDIO_CHANNEL_FL;
            info.position[1] = SPA_AUDIO_CHANNEL_FR;
        }
        uint8_t buffer[1024];
        spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
        const spa_pod *params[1] = {spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info)};
        if (pw_stream_connect(stream, PW_DIRECTION_INPUT, PW_ID_ANY, pw_stream_flags(PW_STREAM_FLAG_MAP_BUFFERS),
                              params, 1) < 0) {
            if (error)
                *error = "Could not connect the PipeWire capture stream";
            destroyStream();
            return false;
        }
        return true;
    }

    void destroyLinks()
    {
        for (auto &l : links)
            pw_proxy_destroy(l.second);
        links.clear();
        q->m_linkedStreams = 0;
    }

    void destroyStream()
    {
        destroyLinks();
        if (stream) {
            spa_hook_remove(&streamListener);
            pw_stream_disconnect(stream);
            pw_stream_destroy(stream);
            stream = nullptr;
        }
        ourNode = SPA_ID_INVALID;
    }

    static bool isRealSource(const NodeInfo &n) { return n.isSource() && n.mediaClass != "Audio/Sink"; }

    // The node name the "default" kinds should follow right now.
    std::string defaultName(const std::vector<NodeInfo> &nodes)
    {
        const bool out = fromOutput();
        const std::string name = out ? graph().defaultSinkName() : graph().defaultSourceName();
        const NodeInfo *first = nullptr;
        for (const auto &n : nodes) {
            if (n.ours || !(out ? n.isSink() : isRealSource(n)))
                continue;
            if (n.name == name)
                return name;
            if (!first || n.priority > first->priority || (n.priority == first->priority && n.id < first->id))
                first = &n;
        }
        // No default announced (or it vanished): the device a session manager
        // would choose, the one with the highest priority.
        return first ? first->name : std::string();
    }

    bool matches(const NodeInfo &n, const std::string &defaultTarget)
    {
        if (n.ours)
            return false;
        switch (kind()) {
        case Kind::Application: return n.isAppOutput() && appKey(n) == config().target;
        case Kind::OutputDevice: return n.isSink() && n.name == config().target;
        case Kind::InputDevice: return isRealSource(n) && n.name == config().target;
        case Kind::DefaultOutput: return n.isSink() && !defaultTarget.empty() && n.name == defaultTarget;
        case Kind::DefaultInput: return isRealSource(n) && !defaultTarget.empty() && n.name == defaultTarget;
        }
        return false;
    }

    bool eligiblePort(const PortInfo &p)
    {
        // A sink's audio is on its monitor outputs; sources and applications
        // on their ordinary outputs.
        return p.output && sources.count(p.nodeId) && (fromOutput() ? p.monitor : !p.monitor);
    }

    void linkPort(const PortInfo &out, const std::map<std::string, uint32_t> &ourPorts)
    {
        std::vector<std::string> targets;
        if (ourPorts.count("MONO"))
            targets = {"MONO"};
        else
            targets = targetChannels(out.channel);
        for (const auto &ch : targets) {
            auto in = ourPorts.find(ch);
            if (in == ourPorts.end())
                continue;
            const auto key = std::make_pair(out.id, in->second);
            if (links.count(key))
                continue;
            pw_properties *props = pw_properties_new(nullptr, nullptr);
            pw_properties_setf(props, PW_KEY_LINK_OUTPUT_NODE, "%u", out.nodeId);
            pw_properties_setf(props, PW_KEY_LINK_OUTPUT_PORT, "%u", out.id);
            pw_properties_setf(props, PW_KEY_LINK_INPUT_NODE, "%u", ourNode);
            pw_properties_setf(props, PW_KEY_LINK_INPUT_PORT, "%u", in->second);
            pw_properties_set(props, PW_KEY_OBJECT_LINGER, "false");
            auto *proxy = static_cast<pw_proxy *>(pw_core_create_object(
                graph().core(), "link-factory", PW_TYPE_INTERFACE_Link, PW_VERSION_LINK, &props->dict, 0));
            pw_properties_free(props);
            if (proxy)
                links[key] = proxy;
        }
    }

    void relinkAll()
    {
        if (ourNode == SPA_ID_INVALID)
            return;
        const auto ports = graph().ports();
        std::map<std::string, uint32_t> ourPorts;
        for (const auto &p : ports)
            if (p.nodeId == ourNode && !p.output)
                ourPorts[p.channel] = p.id;
        if (ourPorts.empty())
            return;
        for (const auto &p : ports)
            if (eligiblePort(p))
                linkPort(p, ourPorts);
    }

    // Brings links in line with the graph: which nodes should feed us now.
    void recompute()
    {
        const auto nodes = graph().nodes();
        const std::string def = isApp() || explicitDevice() ? std::string() : defaultName(nodes);
        std::set<uint32_t> wanted;
        bool targetPresent = false;
        for (const auto &n : nodes)
            if (matches(n, def)) {
                wanted.insert(n.id);
                targetPresent = true;
            }
        // Drop links from nodes that no longer qualify (default changed).
        const auto ports = graph().ports();
        std::map<uint32_t, uint32_t> portNode;
        for (const auto &p : ports)
            portNode[p.id] = p.nodeId;
        for (auto it = links.begin(); it != links.end();) {
            auto pn = portNode.find(it->first.first);
            if (pn == portNode.end() || !wanted.count(pn->second)) {
                pw_proxy_destroy(it->second);
                it = links.erase(it);
            } else {
                ++it;
            }
        }
        const bool hadSources = !sources.empty();
        sources = wanted;
        relinkAll();

        if (explicitDevice()) {
            if (targetPresent && !targetSeen) {
                targetSeen = true;
            } else if (targetPresent && q->state() == State::Lost) {
                q->m_reconnects++;
            }
            if (!targetPresent && targetSeen) {
                q->setState(State::Lost, "The selected device was disconnected");
                q->m_linkedStreams = 0;
                return;
            }
        } else if (isApp() && hadSources && sources.empty()) {
            q->m_reconnects++;  // the app closed its audio; its next stream relinks
        }
        updateState();
    }

    void updateState()
    {
        if (q->state() == State::Failed)
            return;
        std::set<uint32_t> linkedNodes;
        std::map<uint32_t, uint32_t> portNode;
        for (const auto &p : graph().ports())
            portNode[p.id] = p.nodeId;
        for (const auto &l : links) {
            auto it = portNode.find(l.first.first);
            if (it != portNode.end())
                linkedNodes.insert(it->second);
        }
        q->m_linkedStreams = int(linkedNodes.size());
        if (!linkedNodes.empty())
            q->setState(State::Active, {});
        else if (isApp())
            q->setState(State::Waiting, "Waiting for the application to play audio");
        else
            q->setState(State::Waiting, "Waiting for the audio device");
    }

    void onGraphEvent(const GraphEvent &e)
    {
        if (stopping)
            return;
        switch (e.type) {
        case GraphEvent::NodeAdded:
        case GraphEvent::NodeChanged:
        case GraphEvent::NodeRemoved:
        case GraphEvent::DefaultsChanged:
            recompute();
            break;
        case GraphEvent::PortAdded:
            relinkAll();
            updateState();
            break;
        case GraphEvent::PortRemoved:
            for (auto it = links.begin(); it != links.end();) {
                if (it->first.first == e.id || it->first.second == e.id) {
                    pw_proxy_destroy(it->second);
                    it = links.erase(it);
                } else {
                    ++it;
                }
            }
            if (q->state() != State::Lost)
                updateState();
            break;
        default:
            break;
        }
    }
};

CaptureStream::CaptureStream(PipeWireGraph &graph, Config config, DataFn onData)
    : m_graph(graph), m_config(std::move(config))
{
    d = std::make_unique<Impl>();
    d->q = this;
    d->onData = std::move(onData);
}

CaptureStream::~CaptureStream() { stop(); }

void CaptureStream::setState(State s, const std::string &message)
{
    m_state = s;
    std::lock_guard<std::mutex> lock(m_messageMutex);
    m_message = message;
}

std::string CaptureStream::message() const
{
    std::lock_guard<std::mutex> lock(m_messageMutex);
    return m_message;
}

bool CaptureStream::start(std::string *error)
{
    if (!m_graph.running()) {
        if (error)
            *error = "PipeWire is not running";
        return false;
    }
    PipeWireGraph::Lock lock(m_graph);
    d->stopping = false;
    d->targetSeen = false;
    d->sources.clear();
    if (d->explicitDevice()) {
        bool present = false;
        for (const auto &n : m_graph.nodes())
            present |= d->matches(n, {});
        if (!present) {
            if (error)
                *error = "The selected audio device is not available";
            return false;
        }
    }
    if (m_config.kind == Kind::Application && m_config.target.empty()) {
        if (error)
            *error = "No application selected";
        return false;
    }
    d->graphListener = m_graph.addListener([this](const GraphEvent &e) { d->onGraphEvent(e); });
    setState(State::Waiting, "Connecting");
    if (!d->createStream(error)) {
        m_graph.removeListener(d->graphListener);
        d->graphListener = 0;
        setState(State::Failed, error ? *error : std::string());
        return false;
    }
    return true;
}

void CaptureStream::stop()
{
    if (!d || !m_graph.running())
        return;
    PipeWireGraph::Lock lock(m_graph);
    d->stopping = true;
    if (d->graphListener) {
        m_graph.removeListener(d->graphListener);
        d->graphListener = 0;
    }
    d->destroyStream();
    d->sources.clear();
    m_state = State::Idle;
}

}  // namespace afterline::audio
