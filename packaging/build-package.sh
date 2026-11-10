#!/bin/sh
# SPDX-License-Identifier: MIT
#
# Builds a .deb or .rpm for the distribution this runs on (normally a clean
# container of that distribution, as root). Tests, including recording tests
# against a private PipeWire session, must pass before a package is made.
# The package lands in ./dist/ with the distribution in its name, e.g.
#   afterline_0.2.0_ubuntu-24.04_amd64.deb
#   afterline-0.2.0-fedora-43.x86_64.rpm
#
#   docker run --rm -v "$PWD:/src" -w /src ubuntu:24.04 packaging/build-package.sh
#   docker run --rm -v "$PWD:/src" -w /src fedora:43    packaging/build-package.sh

set -eu

. /etc/os-release
TAG="${ID}-${VERSION_ID:-rolling}"
BUILD="build-pkg-${TAG}"

if command -v apt-get >/dev/null 2>&1; then
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -q
    apt-get install -y -q --no-install-recommends build-essential cmake pkg-config git ca-certificates \
        qt6-base-dev qt6-base-dev-tools libqt6svg6-dev libgl-dev libpipewire-0.3-dev libopus-dev libogg-dev \
        libsqlite3-dev dpkg-dev file \
        pipewire pipewire-bin dbus && { apt-get install -y -q --no-install-recommends wireplumber || true; } && { apt-get install -y -q --no-install-recommends pipewire-media-session 2>/dev/null || true; }
    GEN=DEB
elif command -v dnf >/dev/null 2>&1; then
    dnf install -y -q gcc-c++ cmake pkgconf git qt6-qtbase-devel qt6-qtsvg-devel pipewire-devel opus-devel \
        libogg-devel sqlite-devel rpm-build pipewire pipewire-utils wireplumber dbus-daemon
    GEN=RPM
else
    echo "Unsupported distribution: $ID" >&2
    exit 1
fi
git config --global --add safe.directory '*' 2>/dev/null || true

cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DAFTERLINE_BUILD_TESTS=ON
cmake --build "$BUILD" -j"$(nproc)"
ctest --test-dir "$BUILD" --output-on-failure -LE pipewire
tests/run-pipewire-tests.sh ctest --test-dir "$BUILD" --output-on-failure -L pipewire
(cd "$BUILD" && cpack -G "$GEN")

mkdir -p dist
VERSION=$(sed -n 's/^project(AfterLine VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
for f in "$BUILD"/afterline*."$(echo "$GEN" | tr 'A-Z' 'a-z')"; do
    case "$GEN" in
        DEB) arch=$(dpkg --print-architecture); out="afterline_${VERSION}_${TAG}_${arch}.deb" ;;
        RPM) arch=$(uname -m);                  out="afterline-${VERSION}-${TAG}.${arch}.rpm" ;;
    esac
    cp "$f" "dist/$out"
    echo "Built dist/$out"
done
