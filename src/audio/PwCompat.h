// SPDX-License-Identifier: MIT
//
// Lets AfterLine build against every PipeWire it supports, from 0.3.48
// (Ubuntu 22.04, Linux Mint 21) to 1.x. Older headers lack a few property
// names; the daemon understands the strings regardless.
#pragma once

#include <pipewire/keys.h>
#include <pipewire/version.h>

#ifndef PW_KEY_TARGET_OBJECT
#define PW_KEY_TARGET_OBJECT "target.object"
#endif
#ifndef PW_KEY_NODE_DONT_RECONNECT
#define PW_KEY_NODE_DONT_RECONNECT "node.dont-reconnect"
#endif
#ifndef PW_KEY_NODE_WANT_DRIVER
#define PW_KEY_NODE_WANT_DRIVER "node.want-driver"
#endif
#ifndef PW_KEY_STREAM_CAPTURE_SINK
#define PW_KEY_STREAM_CAPTURE_SINK "stream.capture.sink"
#endif
#ifndef PW_KEY_NODE_AUTOCONNECT
#define PW_KEY_NODE_AUTOCONNECT "node.autoconnect"
#endif

// pw_buffer::requested appeared in 0.3.49.
#if PW_CHECK_VERSION(0, 3, 49)
#define AFTERLINE_PW_HAS_REQUESTED 1
#else
#define AFTERLINE_PW_HAS_REQUESTED 0
#endif
