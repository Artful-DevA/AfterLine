// SPDX-License-Identifier: MIT
#include "audio/CaptureStream.h"

#include <pipewire/pipewire.h>
#include "audio/PwCompat.h"
#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>

#include <map>
#include <vector>

namespace afterline::audio {

const char *toString(CaptureStream::State s)
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
    uint32_t targetNode = SPA_ID_INVALID;
    bool stopping = false;
    bool everConnected = false;
    std::set<uint32_t> appNodes;                               // tracked app streams
    std::map<std::pair<uint32_t, uint32_t>, pw_proxy *> links; // (out port, in port)
    std::vector<float> mono;

    PipeWireGraph &graph() { return q->m_graph; }
    const Config &config() { return q->m_config; }
    bool isApp() { return config().kind == Kind::Application; }

    static void onStateChanged(void *data, pw_stream_state old, pw_stream_state state, const char *error)
    {
        auto *self = static_cast<Impl *>(data);
        if (self->stopping)
            return;
        switch (state) {
        case PW_STREAM_STATE_PAUSED:
        case PW_STREAM_STATE_STREAMING:
            self->everConnected = true;
            self->ourNode = pw_stream_get_node_id(self->stream);
            if (self->isApp()) {
                self->relinkAll();
            } else if (state == PW_STREAM_STATE_STREAMING) {
                self->q->setState(State::Active, {});
            } else if (self->q->state() != State::Active) {
                self->q->setState(State::Waiting, "Waiting for audio");
            }
            break;
        case PW_STREAM_STATE_ERROR:
        case PW_STREAM_STATE_UNCONNECTED:
            if (self->everConnected || state == PW_STREAM_STATE_ERROR)
                self->q->setState(State::Lost, error ? error : "The audio source was disconnected");
            break;
        default:
            break;
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
                if (!self->isApp() && self->q->state() != State::Active)
                    self->q->setState(State::Active, {});
            }
        }
        pw_stream_queue_buffer(self->stream, b);
    }

    bool createStream(std::string *error)
    {
        const Kind kind = config().kind;
        const bool input = CaptureStream::isInput(kind);
        channels = input ? 1 : 2;
        pw_properties *props = pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture",
            PW_KEY_APP_NAME, "AfterLine", PW_KEY_APP_ICON_NAME, "io.github.AfterLine",
            PW_KEY_NODE_NAME, input ? "afterline-microphone" : "afterline-computer-audio",
            PW_KEY_NODE_DESCRIPTION,
            config().description.empty() ? "AfterLine recording" : config().description.c_str(),
            nullptr);
        if (kind == Kind::DefaultOutput || kind == Kind::OutputDevice)
            pw_properties_set(props, PW_KEY_STREAM_CAPTURE_SINK, "true");
        if (kind == Kind::OutputDevice || kind == Kind::InputDevice) {
            // node.target for PipeWire < 0.3.64 (Ubuntu 22.04), target.object after.
            pw_properties_set(props, PW_KEY_TARGET_OBJECT, config().target.c_str());
            pw_properties_set(props, "node.target", config().target.c_str());
            // If the chosen device goes away, do not silently record a
            // different one; AfterLine reports it and reconnects when it returns.
            pw_properties_set(props, PW_KEY_NODE_DONT_RECONNECT, "true");
        }
        if (kind == Kind::Application) {
            pw_properties_set(props, PW_KEY_NODE_AUTOCONNECT, "false");
            // Keep our node running so links made while the app is silent
            // start delivering the moment it plays.
            pw_properties_set(props, PW_KEY_NODE_WANT_DRIVER, "true");
        }

        stream = pw_stream_new(graph().core(), "AfterLine capture", props);
        if (!stream) {
            if (error)
                *error = "Could not create a PipeWire capture stream";
            return false;
        }
        static const pw_stream_events events = [] { pw_stream_events e{}; e.version = PW_VERSION_STREAM_EVENTS; e.state_changed = onStateChanged; e.process = onProcess; return e; }();
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
        auto flags = pw_stream_flags(PW_STREAM_FLAG_MAP_BUFFERS);
        if (kind != Kind::Application)
            flags = pw_stream_flags(flags | PW_STREAM_FLAG_AUTOCONNECT);
        if (pw_stream_connect(stream, PW_DIRECTION_INPUT, PW_ID_ANY, flags, params, 1) < 0) {
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
        everConnected = false;
    }

    bool matchesApp(const NodeInfo &n)
    {
        return n.isAppOutput() && !n.ours && appKey(n) == config().target;
    }

    void updateLinkedCount()
    {
        std::set<uint32_t> nodes;
        const auto ports = graph().ports();
        std::map<uint32_t, uint32_t> portNode;
        for (const auto &p : ports)
            portNode[p.id] = p.nodeId;
        for (const auto &l : links) {
            auto it = portNode.find(l.first.first);
            if (it != portNode.end())
                nodes.insert(it->second);
        }
        const int count = int(nodes.size());
        q->m_linkedStreams = count;
        if (count > 0)
            q->setState(State::Active, {});
        else
            q->setState(State::Waiting, "Waiting for the application to play audio");
    }

    void linkPort(const PortInfo &out, const std::map<std::string, uint32_t> &ourPorts)
    {
        for (const auto &ch : targetChannels(out.channel)) {
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
        std::map<std::string, uint32_t> ourPorts;
        const auto ports = graph().ports();
        for (const auto &p : ports)
            if (p.nodeId == ourNode && !p.output)
                ourPorts[p.channel] = p.id;
        if (ourPorts.empty())
            return;
        for (const auto &p : ports)
            if (p.output && !p.monitor && appNodes.count(p.nodeId))
                linkPort(p, ourPorts);
        updateLinkedCount();
    }

    void onGraphEvent(const GraphEvent &e)
    {
        if (stopping)
            return;
        if (isApp()) {
            switch (e.type) {
            case GraphEvent::NodeAdded:
            case GraphEvent::NodeChanged: {
                NodeInfo n;
                // A stream of the chosen application: the first one, or a new
                // one after the call or tab reconnected.
                if (graph().node(e.id, &n) && matchesApp(n) && !appNodes.count(e.id)) {
                    appNodes.insert(e.id);
                    relinkAll();
                }
                break;
            }
            case GraphEvent::NodeRemoved:
                if (appNodes.erase(e.id)) {
                    if (appNodes.empty())
                        q->m_reconnects++;  // counted as a gap; the next stream relinks
                    updateLinkedCount();
                }
                break;
            case GraphEvent::PortAdded: {
                const auto ports = graph().ports();
                for (const auto &p : ports) {
                    if (p.id != e.id)
                        continue;
                    if (p.nodeId == ourNode || (p.output && appNodes.count(p.nodeId)))
                        relinkAll();
                }
                break;
            }
            case GraphEvent::PortRemoved:
                for (auto it = links.begin(); it != links.end();) {
                    if (it->first.first == e.id || it->first.second == e.id) {
                        pw_proxy_destroy(it->second);
                        it = links.erase(it);
                    } else {
                        ++it;
                    }
                }
                updateLinkedCount();
                break;
            default:
                break;
            }
            return;
        }

        // Devices.
        const Kind kind = config().kind;
        const bool explicitDevice = kind == Kind::OutputDevice || kind == Kind::InputDevice;
        if (e.type == GraphEvent::NodeRemoved && explicitDevice && e.id == targetNode) {
            targetNode = SPA_ID_INVALID;
            destroyStream();
            q->setState(State::Lost, "The selected device was disconnected");
            return;
        }
        if (e.type == GraphEvent::NodeAdded || e.type == GraphEvent::NodeChanged ||
            e.type == GraphEvent::DefaultsChanged) {
            NodeInfo n;
            if (explicitDevice && e.type != GraphEvent::DefaultsChanged && graph().node(e.id, &n) &&
                n.name == config().target) {
                targetNode = e.id;
                if (!stream && q->state() == State::Lost) {
                    std::string err;
                    if (createStream(&err)) {
                        q->m_reconnects++;
                        q->setState(State::Waiting, "Device reconnected");
                    }
                }
            } else if (!explicitDevice && q->state() == State::Lost) {
                destroyStream();
                std::string err;
                if (createStream(&err)) {
                    q->m_reconnects++;
                    q->setState(State::Waiting, "Reconnected to the default device");
                }
            }
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
    const Kind kind = m_config.kind;
    if (kind == Kind::OutputDevice || kind == Kind::InputDevice) {
        for (const auto &n : m_graph.nodes())
            if (n.name == m_config.target)
                d->targetNode = n.id;
        if (d->targetNode == SPA_ID_INVALID) {
            if (error)
                *error = "The selected audio device is not available";
            return false;
        }
    }
    if (kind == Kind::Application) {
        if (m_config.target.empty()) {
            if (error)
                *error = "No application selected";
            return false;
        }
        for (const auto &n : m_graph.nodes())
            if (d->matchesApp(n))
                d->appNodes.insert(n.id);
    }
    d->graphListener = m_graph.addListener([this](const GraphEvent &e) { d->onGraphEvent(e); });
    setState(State::Waiting, kind == Kind::Application ? "Waiting for the application to play audio"
                                                       : "Connecting");
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
    d->appNodes.clear();
    m_state = State::Idle;
}

}  // namespace afterline::audio
