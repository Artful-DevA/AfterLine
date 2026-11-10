#!/bin/sh
# SPDX-License-Identifier: MIT
#
# Runs a command inside a private PipeWire + WirePlumber session (no sound
# hardware needed), so recording tests work in CI containers and never touch
# a developer's real audio setup.
#
#   tests/run-pipewire-tests.sh                    # ctest -L pipewire in ./build
#   tests/run-pipewire-tests.sh ctest --test-dir build-x -L pipewire
#   tests/run-pipewire-tests.sh sh -c 'your commands'
set -eu

for tool in pipewire wireplumber dbus-daemon pw-cli; do
    command -v "$tool" >/dev/null 2>&1 || { echo "missing $tool" >&2; exit 77; }
done

RUNTIME=$(mktemp -d "${TMPDIR:-/tmp}/afterline-pw-XXXXXX")
chmod 700 "$RUNTIME"
export XDG_RUNTIME_DIR="$RUNTIME"
export DBUS_SESSION_BUS_ADDRESS="unix:path=$RUNTIME/bus"
# Keep the private session away from any user configuration.
export XDG_CONFIG_HOME="$RUNTIME/config" XDG_STATE_HOME="$RUNTIME/state"
mkdir -p "$XDG_CONFIG_HOME" "$XDG_STATE_HOME"

cleanup() {
    [ -n "${WP_PID:-}" ] && kill "$WP_PID" 2>/dev/null || true
    [ -n "${PW_PID:-}" ] && kill "$PW_PID" 2>/dev/null || true
    [ -n "${DBUS_PID:-}" ] && kill "$DBUS_PID" 2>/dev/null || true
    rm -rf "$RUNTIME"
}
trap cleanup EXIT INT TERM

dbus-daemon --session --address="$DBUS_SESSION_BUS_ADDRESS" --nofork --nopidfile >"$RUNTIME/dbus.log" 2>&1 &
DBUS_PID=$!
sleep 0.3
pipewire >"$RUNTIME/pipewire.log" 2>&1 &
PW_PID=$!
i=0
until pw-cli info 0 >/dev/null 2>&1; do
    i=$((i + 1)); [ $i -gt 50 ] && { echo "pipewire did not start" >&2; cat "$RUNTIME/pipewire.log" >&2; exit 1; }
    sleep 0.1
done
wireplumber >"$RUNTIME/wireplumber.log" 2>&1 &
WP_PID=$!
sleep 1.5

if [ $# -eq 0 ]; then
    set -- ctest --test-dir build --output-on-failure -L pipewire
fi
"$@"
