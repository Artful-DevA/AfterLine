// SPDX-License-Identifier: MIT
//
// A live view of the PipeWire audio graph: devices, application streams,
// ports and the default devices, kept up to date from the registry.
//
// Everything PipeWire-related runs on one thread (a pw_thread_loop). Public
// methods take the loop lock themselves; listeners are called on the loop
// thread with the lock held and must not block.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct pw_thread_loop;
struct pw_core;
struct pw_context;

namespace afterline::audio {

// Property AfterLine puts on its own streams (value: its process id), so it
// never offers or records itself.
inline constexpr const char *kOwnerKey = "afterline.owner.pid";

struct NodeInfo {
    uint32_t id = 0;
    uint64_t serial = 0;
    std::string mediaClass;
    std::string name;         // node.name, stable across restarts for devices
    std::string description;
    std::string nick;
    std::string appName;      // application.name
    std::string appBinary;    // application.process.binary
    std::string appIcon;      // application.icon-name
    std::string appId;        // Flatpak/portal application id
    std::string mediaName;    // media.name (e.g. a browser tab title)
    std::string mediaRole;
    int pid = 0;
    int priority = 0;         // priority.session: how session managers rank defaults
    bool ours = false;        // created by this process

    bool isSink() const { return mediaClass == "Audio/Sink" || mediaClass == "Audio/Duplex"; }
    bool isSource() const
    {
        return mediaClass == "Audio/Source" || mediaClass == "Audio/Source/Virtual" ||
               mediaClass == "Audio/Duplex";
    }
    bool isAppOutput() const { return mediaClass == "Stream/Output/Audio"; }
    std::string label() const;
};

struct PortInfo {
    uint32_t id = 0;
    uint32_t nodeId = 0;
    bool output = false;
    bool monitor = false;
    std::string channel;  // FL, FR, MONO, ...
    std::string name;
};

// The identity an application keeps when it closes and reopens its audio
// streams (a browser tab reloading, a call reconnecting). Empty if unknown.
std::string appKey(const NodeInfo &node);
// A friendly display name for an application ("Firefox", "Zoom", ...).
std::string appDisplayName(const NodeInfo &node);
// An icon-theme name for an application, best effort.
std::string appIconName(const NodeInfo &node);

struct GraphEvent {
    enum Type { NodeAdded, NodeChanged, NodeRemoved, PortAdded, PortRemoved, DefaultsChanged, Disconnected };
    Type type;
    uint32_t id = 0;
};

class PipeWireGraph {
public:
    PipeWireGraph();
    ~PipeWireGraph();
    PipeWireGraph(const PipeWireGraph &) = delete;
    PipeWireGraph &operator=(const PipeWireGraph &) = delete;

    // Connects to the user's PipeWire daemon and waits for the initial graph.
    bool start(std::string *error);
    void stop();
    bool running() const;
    bool connected() const;

    std::vector<NodeInfo> nodes() const;
    std::vector<PortInfo> ports() const;
    bool node(uint32_t id, NodeInfo *out) const;
    std::string defaultSinkName() const;
    std::string defaultSourceName() const;
    std::string serverVersion() const;

    using Listener = std::function<void(const GraphEvent &)>;
    int addListener(Listener listener);
    void removeListener(int handle);

    // Waits until the daemon has processed everything sent so far.
    void sync();

    // Loop access for streams that live on the same thread.
    pw_thread_loop *loop() const { return m_loop; }
    pw_core *core() const;

    class Lock {
    public:
        explicit Lock(const PipeWireGraph &g);
        ~Lock();
        Lock(const Lock &) = delete;
        Lock &operator=(const Lock &) = delete;

    private:
        pw_thread_loop *m_loop;
    };

    // Called by the implementation on the loop thread.
    struct Impl;

private:
    pw_thread_loop *m_loop = nullptr;
    std::unique_ptr<Impl> d;
};

}  // namespace afterline::audio
