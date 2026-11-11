// SPDX-License-Identifier: MIT
#include "audio/PipeWireGraph.h"

#include <pipewire/extensions/metadata.h>
#include <pipewire/pipewire.h>
#include <spa/utils/result.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <unistd.h>

namespace afterline::audio {

namespace {

std::string prop(const spa_dict *dict, const char *key)
{
    if (!dict)
        return {};
    const char *v = spa_dict_lookup(dict, key);
    return v ? std::string(v) : std::string();
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

// Extracts "name" from the JSON value of default.audio.sink ({"name":"..."}).
std::string jsonName(const char *value)
{
    if (!value)
        return {};
    const char *p = std::strstr(value, "\"name\"");
    if (!p)
        return {};
    p = std::strchr(p + 6, '"');
    if (!p)
        return {};
    const char *e = std::strchr(p + 1, '"');
    return e ? std::string(p + 1, e) : std::string();
}

void fillNode(NodeInfo &n, const spa_dict *props)
{
    auto set = [&](std::string &field, const char *key) {
        std::string v = prop(props, key);
        if (!v.empty())
            field = v;
    };
    set(n.mediaClass, PW_KEY_MEDIA_CLASS);
    set(n.name, PW_KEY_NODE_NAME);
    set(n.description, PW_KEY_NODE_DESCRIPTION);
    set(n.nick, PW_KEY_NODE_NICK);
    set(n.appName, PW_KEY_APP_NAME);
    set(n.appBinary, PW_KEY_APP_PROCESS_BINARY);
    set(n.appIcon, PW_KEY_APP_ICON_NAME);
    set(n.appId, "pipewire.access.portal.app_id");
    if (n.appId.empty())
        set(n.appId, "application.id");
    set(n.mediaName, PW_KEY_MEDIA_NAME);
    set(n.mediaRole, PW_KEY_MEDIA_ROLE);
    const std::string serial = prop(props, PW_KEY_OBJECT_SERIAL);
    if (!serial.empty())
        n.serial = std::strtoull(serial.c_str(), nullptr, 10);
    const std::string priority = prop(props, PW_KEY_PRIORITY_SESSION);
    if (!priority.empty())
        n.priority = std::atoi(priority.c_str());
    const std::string pid = prop(props, PW_KEY_APP_PROCESS_ID);
    if (!pid.empty()) {
        n.pid = std::atoi(pid.c_str());
        n.ours = n.pid == int(::getpid());
    }
    // Streams do not always carry the process id; AfterLine marks its own.
    const std::string own = prop(props, kOwnerKey);
    if (!own.empty() && std::atoi(own.c_str()) == int(::getpid()))
        n.ours = true;
}

}  // namespace

std::string NodeInfo::label() const
{
    if (!description.empty())
        return description;
    if (!nick.empty())
        return nick;
    return name;
}

std::string appKey(const NodeInfo &n)
{
    if (!n.appId.empty())
        return "app:" + n.appId;
    if (!n.appBinary.empty()) {
        std::string b = lower(n.appBinary);
        // Browsers and Electron apps run audio in helper processes whose
        // binary names differ from the main one.
        for (const char *suffix : {"-bin", "-wrapped", ".bin"}) {
            const size_t len = std::strlen(suffix);
            if (b.size() > len && b.compare(b.size() - len, len, suffix) == 0)
                b.erase(b.size() - len);
        }
        if (b == "chromium-browser")
            b = "chromium";
        if (b == "google-chrome" || b == "google-chrome-stable")
            b = "chrome";
        return "bin:" + b;
    }
    if (!n.appName.empty())
        return "name:" + lower(n.appName);
    return {};
}

std::string appDisplayName(const NodeInfo &n)
{
    const std::string key = appKey(n);
    struct Known { const char *needle; const char *name; };
    static const Known known[] = {
        {"firefox", "Firefox"}, {"librewolf", "LibreWolf"}, {"chrome", "Google Chrome"},
        {"chromium", "Chromium"}, {"brave", "Brave"}, {"msedge", "Microsoft Edge"},
        {"vivaldi", "Vivaldi"}, {"opera", "Opera"}, {"zoom", "Zoom"}, {"discord", "Discord"},
        {"teams", "Microsoft Teams"}, {"slack", "Slack"}, {"skype", "Skype"},
        {"signal", "Signal"}, {"telegram", "Telegram"}, {"webex", "Webex"},
        {"jitsi", "Jitsi Meet"}, {"element", "Element"}, {"mumble", "Mumble"},
        {"obs", "OBS Studio"}, {"spotify", "Spotify"}, {"vlc", "VLC"}, {"mpv", "mpv"},
    };
    const std::string lk = lower(key);
    for (const auto &k : known)
        if (lk.find(k.needle) != std::string::npos)
            return k.name;
    if (!n.appName.empty())
        return n.appName;
    if (!n.appBinary.empty())
        return n.appBinary;
    return n.label();
}

std::string appIconName(const NodeInfo &n)
{
    if (!n.appIcon.empty())
        return n.appIcon;
    if (!n.appId.empty())
        return n.appId;
    const std::string key = lower(appKey(n));
    struct Known { const char *needle; const char *icon; };
    static const Known known[] = {
        {"firefox", "firefox"}, {"chrome", "google-chrome"}, {"chromium", "chromium"},
        {"brave", "brave-browser"}, {"msedge", "microsoft-edge"}, {"zoom", "Zoom"},
        {"discord", "discord"}, {"teams", "teams-for-linux"}, {"slack", "slack"},
        {"spotify", "spotify"}, {"vlc", "vlc"}, {"signal", "signal-desktop"},
        {"telegram", "telegram"},
    };
    for (const auto &k : known)
        if (key.find(k.needle) != std::string::npos)
            return k.icon;
    return n.appBinary.empty() ? std::string("audio-x-generic") : lower(n.appBinary);
}

// ---------------------------------------------------------------------------

struct BoundNode {
    PipeWireGraph::Impl *impl;
    uint32_t id;
    pw_proxy *proxy;
    spa_hook listener;
};

struct PipeWireGraph::Impl {
    PipeWireGraph *q = nullptr;
    pw_thread_loop *loop = nullptr;
    pw_context *context = nullptr;
    pw_core *core = nullptr;
    pw_registry *registry = nullptr;
    spa_hook coreListener{};
    spa_hook registryListener{};
    pw_proxy *metadata = nullptr;
    spa_hook metadataListener{};
    uint32_t metadataId = SPA_ID_INVALID;
    std::map<uint32_t, NodeInfo> nodes;
    std::map<uint32_t, PortInfo> ports;
    std::map<uint32_t, std::unique_ptr<BoundNode>> bound;
    std::string defaultSink, defaultSource, version;
    std::map<int, Listener> listeners;
    int nextListener = 1;
    int pendingSeq = -1;
    bool syncDone = false;
    bool connected = false;

    void emit(GraphEvent::Type type, uint32_t id = 0)
    {
        const GraphEvent e{type, id};
        // Copy: a listener may remove itself.
        auto copy = listeners;
        for (auto &l : copy)
            l.second(e);
    }

    static void onCoreInfo(void *data, const pw_core_info *info)
    {
        auto *self = static_cast<Impl *>(data);
        if (info && info->version)
            self->version = info->version;
    }

    static void onCoreDone(void *data, uint32_t id, int seq)
    {
        auto *self = static_cast<Impl *>(data);
        if (id == PW_ID_CORE && seq == self->pendingSeq) {
            self->syncDone = true;
            pw_thread_loop_signal(self->loop, false);
        }
    }

    static void onCoreError(void *data, uint32_t id, int seq, int res, const char *message)
    {
        auto *self = static_cast<Impl *>(data);
        if (id == PW_ID_CORE && res == -EPIPE) {
            self->connected = false;
            self->syncDone = true;
            pw_thread_loop_signal(self->loop, false);
            self->emit(GraphEvent::Disconnected);
        }
    }

    static void onNodeInfo(void *data, const pw_node_info *info)
    {
        auto *b = static_cast<BoundNode *>(data);
        auto it = b->impl->nodes.find(b->id);
        if (it == b->impl->nodes.end() || !info || !(info->change_mask & PW_NODE_CHANGE_MASK_PROPS))
            return;
        fillNode(it->second, info->props);
        b->impl->emit(GraphEvent::NodeChanged, b->id);
    }

    static int onMetadataProperty(void *data, uint32_t subject, const char *key, const char *type,
                                  const char *value)
    {
        auto *self = static_cast<Impl *>(data);
        if (subject != PW_ID_CORE)
            return 0;
        bool changed = false;
        if (!key) {
            self->defaultSink.clear();
            self->defaultSource.clear();
            changed = true;
        } else if (std::strcmp(key, "default.audio.sink") == 0) {
            self->defaultSink = jsonName(value);
            changed = true;
        } else if (std::strcmp(key, "default.audio.source") == 0) {
            self->defaultSource = jsonName(value);
            changed = true;
        }
        if (changed)
            self->emit(GraphEvent::DefaultsChanged);
        return 0;
    }

    static void onGlobal(void *data, uint32_t id, uint32_t permissions, const char *type,
                         uint32_t version, const spa_dict *props)
    {
        auto *self = static_cast<Impl *>(data);
        if (std::strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
            const std::string mediaClass = prop(props, PW_KEY_MEDIA_CLASS);
            if (mediaClass.find("Audio") == std::string::npos)
                return;
            NodeInfo n;
            n.id = id;
            fillNode(n, props);
            self->nodes[id] = n;
            // The registry only carries a few properties; bind for the rest
            // (process binary, icon, pid).
            auto b = std::make_unique<BoundNode>();
            b->impl = self;
            b->id = id;
            b->proxy = static_cast<pw_proxy *>(pw_registry_bind(self->registry, id, type, PW_VERSION_NODE, 0));
            if (b->proxy) {
                static const pw_node_events events = [] { pw_node_events e{}; e.version = PW_VERSION_NODE_EVENTS; e.info = onNodeInfo; return e; }();
                pw_proxy_add_object_listener(b->proxy, &b->listener, &events, b.get());
                self->bound[id] = std::move(b);
            }
            self->emit(GraphEvent::NodeAdded, id);
        } else if (std::strcmp(type, PW_TYPE_INTERFACE_Port) == 0) {
            PortInfo p;
            p.id = id;
            p.nodeId = uint32_t(std::strtoul(prop(props, PW_KEY_NODE_ID).c_str(), nullptr, 10));
            p.output = prop(props, PW_KEY_PORT_DIRECTION) == "out";
            p.monitor = prop(props, PW_KEY_PORT_MONITOR) == "true";
            p.channel = prop(props, PW_KEY_AUDIO_CHANNEL);
            p.name = prop(props, PW_KEY_PORT_NAME);
            self->ports[id] = p;
            self->emit(GraphEvent::PortAdded, id);
        } else if (std::strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0) {
            if (prop(props, PW_KEY_METADATA_NAME) != "default" || self->metadata)
                return;
            self->metadata = static_cast<pw_proxy *>(
                pw_registry_bind(self->registry, id, type, PW_VERSION_METADATA, 0));
            if (self->metadata) {
                self->metadataId = id;
                static const pw_metadata_events events = [] { pw_metadata_events e{}; e.version = PW_VERSION_METADATA_EVENTS; e.property = onMetadataProperty; return e; }();
                pw_proxy_add_object_listener(self->metadata, &self->metadataListener, &events, self);
            }
        }
    }

    static void onGlobalRemove(void *data, uint32_t id)
    {
        auto *self = static_cast<Impl *>(data);
        if (self->nodes.erase(id)) {
            auto it = self->bound.find(id);
            if (it != self->bound.end()) {
                spa_hook_remove(&it->second->listener);
                pw_proxy_destroy(it->second->proxy);
                self->bound.erase(it);
            }
            self->emit(GraphEvent::NodeRemoved, id);
        } else if (self->ports.erase(id)) {
            self->emit(GraphEvent::PortRemoved, id);
        } else if (id == self->metadataId && self->metadata) {
            spa_hook_remove(&self->metadataListener);
            pw_proxy_destroy(self->metadata);
            self->metadata = nullptr;
            self->metadataId = SPA_ID_INVALID;
        }
    }
};

PipeWireGraph::PipeWireGraph() = default;

PipeWireGraph::~PipeWireGraph() { stop(); }

pw_core *PipeWireGraph::core() const { return d ? d->core : nullptr; }

bool PipeWireGraph::connected() const
{
    if (!d)
        return false;
    Lock lock(*this);
    return d->connected;
}

bool PipeWireGraph::start(std::string *error)
{
    if (m_loop)
        return true;
    pw_init(nullptr, nullptr);
    d = std::make_unique<Impl>();
    d->q = this;
    d->loop = pw_thread_loop_new("afterline-pw", nullptr);
    if (!d->loop) {
        if (error)
            *error = "Could not create the PipeWire thread";
        d.reset();
        return false;
    }
    d->context = pw_context_new(pw_thread_loop_get_loop(d->loop), nullptr, 0);
    if (!d->context || pw_thread_loop_start(d->loop) < 0) {
        if (error)
            *error = "Could not start PipeWire";
        if (d->context)
            pw_context_destroy(d->context);
        pw_thread_loop_destroy(d->loop);
        d.reset();
        return false;
    }
    m_loop = d->loop;

    pw_thread_loop_lock(d->loop);
    d->core = pw_context_connect(d->context,
                                 pw_properties_new(PW_KEY_APP_NAME, "AfterLine", nullptr), 0);
    if (!d->core) {
        const int err = errno;
        pw_thread_loop_unlock(d->loop);
        if (error)
            *error = std::string("Cannot connect to the PipeWire audio service (") + std::strerror(err) +
                     "). AfterLine needs PipeWire, which is standard on Ubuntu 22.10+, Fedora 34+, "
                     "Debian 12+ and current Arch.";
        stop();
        return false;
    }
    static const pw_core_events coreEvents = [] { pw_core_events e{}; e.version = PW_VERSION_CORE_EVENTS; e.info = Impl::onCoreInfo; e.done = Impl::onCoreDone; e.error = Impl::onCoreError; return e; }();
    pw_core_add_listener(d->core, &d->coreListener, &coreEvents, d.get());
    d->connected = true;
    d->registry = pw_core_get_registry(d->core, PW_VERSION_REGISTRY, 0);
    static const pw_registry_events registryEvents = [] { pw_registry_events e{}; e.version = PW_VERSION_REGISTRY_EVENTS; e.global = Impl::onGlobal; e.global_remove = Impl::onGlobalRemove; return e; }();
    pw_registry_add_listener(d->registry, &d->registryListener, &registryEvents, d.get());
    pw_thread_loop_unlock(d->loop);
    // Two round trips: the first delivers the globals, the second the info
    // of the nodes bound in response.
    sync();
    sync();
    return connected();
}

void PipeWireGraph::sync()
{
    if (!d || !d->core)
        return;
    pw_thread_loop_lock(d->loop);
    if (d->connected) {
        d->syncDone = false;
        d->pendingSeq = pw_core_sync(d->core, PW_ID_CORE, 0);
        while (!d->syncDone)
            pw_thread_loop_wait(d->loop);
    }
    pw_thread_loop_unlock(d->loop);
}

void PipeWireGraph::stop()
{
    if (!d)
        return;
    if (d->loop)
        pw_thread_loop_stop(d->loop);
    for (auto &b : d->bound) {
        spa_hook_remove(&b.second->listener);
        pw_proxy_destroy(b.second->proxy);
    }
    d->bound.clear();
    if (d->metadata) {
        spa_hook_remove(&d->metadataListener);
        pw_proxy_destroy(d->metadata);
    }
    if (d->registry) {
        spa_hook_remove(&d->registryListener);
        pw_proxy_destroy(reinterpret_cast<pw_proxy *>(d->registry));
    }
    if (d->core) {
        spa_hook_remove(&d->coreListener);
        pw_core_disconnect(d->core);
    }
    if (d->context)
        pw_context_destroy(d->context);
    if (d->loop)
        pw_thread_loop_destroy(d->loop);
    d.reset();
    m_loop = nullptr;
}

std::vector<NodeInfo> PipeWireGraph::nodes() const
{
    std::vector<NodeInfo> out;
    if (!d)
        return out;
    Lock lock(*this);
    for (const auto &n : d->nodes)
        out.push_back(n.second);
    return out;
}

std::vector<PortInfo> PipeWireGraph::ports() const
{
    std::vector<PortInfo> out;
    if (!d)
        return out;
    Lock lock(*this);
    for (const auto &p : d->ports)
        out.push_back(p.second);
    return out;
}

bool PipeWireGraph::node(uint32_t id, NodeInfo *out) const
{
    if (!d)
        return false;
    Lock lock(*this);
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
    Lock lock(*this);
    return d->defaultSink;
}

std::string PipeWireGraph::defaultSourceName() const
{
    if (!d)
        return {};
    Lock lock(*this);
    return d->defaultSource;
}

std::string PipeWireGraph::serverVersion() const
{
    if (!d)
        return {};
    Lock lock(*this);
    return d->version;
}

int PipeWireGraph::addListener(Listener listener)
{
    Lock lock(*this);
    const int handle = d->nextListener++;
    d->listeners[handle] = std::move(listener);
    return handle;
}

void PipeWireGraph::removeListener(int handle)
{
    if (!d)
        return;
    Lock lock(*this);
    d->listeners.erase(handle);
}

PipeWireGraph::Lock::Lock(const PipeWireGraph &g) : m_loop(g.m_loop)
{
    if (m_loop)
        pw_thread_loop_lock(m_loop);
}

PipeWireGraph::Lock::~Lock()
{
    if (m_loop)
        pw_thread_loop_unlock(m_loop);
}

}  // namespace afterline::audio
