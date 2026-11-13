// SPDX-License-Identifier: MIT
//
// Windows implementation of the audio graph view (see PipeWireGraph.h): the
// active playback and recording devices, the defaults, and which
// applications are playing or recording sound right now (audio sessions).
// Windows has no single change feed covering all of these, so a background
// thread looks every 1.5 s and reports what changed.
#include "audio/PipeWireGraph.h"
#include "audio/win/WasapiCommon.h"

#include <audiopolicy.h>

#include <atomic>
#include <condition_variable>
#include <map>
#include <set>
#include <thread>

namespace afterline::audio {

using namespace win;

struct PipeWireGraph::Impl {
    std::thread thread;
    std::mutex wakeMutex;
    std::condition_variable wake;
    bool quit = false;
    std::atomic<bool> connected{false};

    mutable std::mutex mutex;  // guards the state below
    std::map<uint32_t, NodeInfo> nodes;
    std::map<std::string, uint32_t> ids;  // stable key -> node id
    uint32_t nextId = 1;
    std::string defaultSink, defaultSource;

    std::mutex listenerMutex;
    std::map<int, Listener> listeners;
    int nextListener = 1;

    uint32_t idFor(const std::string &key)
    {
        auto it = ids.find(key);
        if (it != ids.end())
            return it->second;
        ids[key] = nextId;
        return nextId++;
    }

    void emit(GraphEvent::Type type, uint32_t id = 0)
    {
        std::map<int, Listener> copy;
        {
            std::lock_guard<std::mutex> lock(listenerMutex);
            copy = listeners;
        }
        for (auto &[h, l] : copy)
            l(GraphEvent{type, id});
    }

    static void addSessions(IMMDevice *device, bool render, std::map<std::string, NodeInfo> &out)
    {
        Com<IAudioSessionManager2> manager;
        if (FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                    reinterpret_cast<void **>(manager.put()))))
            return;
        Com<IAudioSessionEnumerator> sessions;
        if (FAILED(manager->GetSessionEnumerator(sessions.put())))
            return;
        int count = 0;
        sessions->GetCount(&count);
        for (int i = 0; i < count; ++i) {
            Com<IAudioSessionControl> control;
            if (FAILED(sessions->GetSession(i, control.put())))
                continue;
            Com<IAudioSessionControl2> control2;
            if (FAILED(control->QueryInterface(__uuidof(IAudioSessionControl2),
                                               reinterpret_cast<void **>(control2.put()))))
                continue;
            AudioSessionState state = AudioSessionStateInactive;
            control2->GetState(&state);
            if (state != AudioSessionStateActive || control2->IsSystemSoundsSession() == S_OK)
                continue;
            DWORD pid = 0;
            control2->GetProcessId(&pid);
            if (!pid)
                continue;
            NodeInfo n;
            n.mediaClass = render ? "Stream/Output/Audio" : "Stream/Input/Audio";
            n.pid = int(pid);
            n.ours = pid == GetCurrentProcessId();
            n.appBinary = processExe(pid);
            if (n.appBinary.empty())
                continue;
            n.appName = n.appBinary;
            n.appName[0] = char(std::toupper(static_cast<unsigned char>(n.appName[0])));
            n.name = n.appBinary + "." + std::to_string(pid);
            n.description = n.appName;
            out[std::string(render ? "out:" : "in:") + n.name] = n;
        }
    }

    // One look at the system. Returns false if Windows audio is unavailable.
    bool scan(std::map<std::string, NodeInfo> &found, std::string &sink, std::string &source)
    {
        Com<IMMDeviceEnumerator> e = deviceEnumerator();
        if (!e)
            return false;
        for (EDataFlow flow : {eRender, eCapture}) {
            Com<IMMDeviceCollection> devices;
            if (FAILED(e->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, devices.put())))
                continue;
            UINT count = 0;
            devices->GetCount(&count);
            for (UINT i = 0; i < count; ++i) {
                Com<IMMDevice> d;
                if (FAILED(devices->Item(i, d.put())))
                    continue;
                NodeInfo n;
                n.mediaClass = flow == eRender ? "Audio/Sink" : "Audio/Source";
                n.name = endpointId(d.get());
                n.description = friendlyName(d.get());
                found["dev:" + n.name] = n;
                addSessions(d.get(), flow == eRender, found);
            }
            Com<IMMDevice> def;
            if (SUCCEEDED(e->GetDefaultAudioEndpoint(flow, eConsole, def.put())))
                (flow == eRender ? sink : source) = endpointId(def.get());
        }
        return true;
    }

    void run()
    {
        ComThread com;
        while (true) {
            std::map<std::string, NodeInfo> found;
            std::string sink, source;
            const bool ok = scan(found, sink, source);
            std::vector<std::pair<GraphEvent::Type, uint32_t>> events;
            {
                std::lock_guard<std::mutex> lock(mutex);
                std::map<uint32_t, NodeInfo> next;
                for (auto &[key, n] : found) {
                    n.id = idFor(key);
                    n.serial = n.id;
                    auto old = nodes.find(n.id);
                    if (old == nodes.end())
                        events.push_back({GraphEvent::NodeAdded, n.id});
                    else if (old->second.description != n.description)
                        events.push_back({GraphEvent::NodeChanged, n.id});
                    next[n.id] = n;
                }
                for (const auto &[id, n] : nodes)
                    if (!next.count(id))
                        events.push_back({GraphEvent::NodeRemoved, id});
                nodes.swap(next);
                if (sink != defaultSink || source != defaultSource)
                    events.push_back({GraphEvent::DefaultsChanged, 0});
                defaultSink = sink;
                defaultSource = source;
            }
            const bool was = connected.exchange(ok);
            if (was && !ok)
                events.push_back({GraphEvent::Disconnected, 0});
            for (const auto &[type, id] : events)
                emit(type, id);
            std::unique_lock<std::mutex> lock(wakeMutex);
            if (wake.wait_for(lock, std::chrono::milliseconds(1500), [this] { return quit; }))
                return;
        }
    }
};

PipeWireGraph::PipeWireGraph() = default;
PipeWireGraph::~PipeWireGraph() { stop(); }

bool PipeWireGraph::start(std::string *error)
{
    if (d)
        return true;
    {
        ComThread com;
        if (!deviceEnumerator()) {
            if (error)
                *error = "Windows audio is not available";
            return false;
        }
    }
    d = std::make_unique<Impl>();
    // The first scan happens here, so callers see devices right away.
    {
        ComThread com;
        std::map<std::string, NodeInfo> found;
        std::string sink, source;
        if (d->scan(found, sink, source)) {
            for (auto &[key, n] : found) {
                n.id = d->idFor(key);
                n.serial = n.id;
                d->nodes[n.id] = n;
            }
            d->defaultSink = sink;
            d->defaultSource = source;
            d->connected = true;
        }
    }
    d->thread = std::thread([impl = d.get()] { impl->run(); });
    return true;
}

void PipeWireGraph::stop()
{
    if (!d)
        return;
    {
        std::lock_guard<std::mutex> lock(d->wakeMutex);
        d->quit = true;
    }
    d->wake.notify_all();
    if (d->thread.joinable())
        d->thread.join();
    d.reset();
}

bool PipeWireGraph::running() const { return d != nullptr; }
bool PipeWireGraph::connected() const { return d && d->connected.load(); }
pw_core *PipeWireGraph::core() const { return nullptr; }

void PipeWireGraph::sync()
{
    if (!d)
        return;
    d->wake.notify_all();
}

std::vector<NodeInfo> PipeWireGraph::nodes() const
{
    std::vector<NodeInfo> out;
    if (!d)
        return out;
    std::lock_guard<std::mutex> lock(d->mutex);
    for (const auto &[id, n] : d->nodes)
        out.push_back(n);
    return out;
}

std::vector<PortInfo> PipeWireGraph::ports() const { return {}; }

bool PipeWireGraph::node(uint32_t id, NodeInfo *out) const
{
    if (!d)
        return false;
    std::lock_guard<std::mutex> lock(d->mutex);
    auto it = d->nodes.find(id);
    if (it == d->nodes.end())
        return false;
    if (out)
        *out = it->second;
    return true;
}

std::string PipeWireGraph::defaultSinkName() const
{
    if (!d)
        return {};
    std::lock_guard<std::mutex> lock(d->mutex);
    return d->defaultSink;
}

std::string PipeWireGraph::defaultSourceName() const
{
    if (!d)
        return {};
    std::lock_guard<std::mutex> lock(d->mutex);
    return d->defaultSource;
}

std::string PipeWireGraph::serverVersion() const { return d ? "Windows audio (WASAPI)" : std::string(); }

int PipeWireGraph::addListener(Listener listener)
{
    if (!d)
        return 0;
    std::lock_guard<std::mutex> lock(d->listenerMutex);
    const int h = d->nextListener++;
    d->listeners[h] = std::move(listener);
    return h;
}

void PipeWireGraph::removeListener(int handle)
{
    if (!d)
        return;
    std::lock_guard<std::mutex> lock(d->listenerMutex);
    d->listeners.erase(handle);
}

PipeWireGraph::Lock::Lock(const PipeWireGraph &) : m_loop(nullptr) {}
PipeWireGraph::Lock::~Lock() = default;

}  // namespace afterline::audio
