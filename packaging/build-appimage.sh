#!/bin/sh
# SPDX-License-Identifier: MIT
#
# Builds a portable AppImage in ./dist/. Run as root in the oldest Ubuntu you
# want to support (the AppImage needs a glibc at least as new as the build's):
#
#   docker run --rm -v "$PWD:/src" -w /src ubuntu:22.04 packaging/build-appimage.sh
#
# The AppImage still needs the system's PipeWire audio service (it talks to
# it like every other application); it bundles Qt and the AI engines.

set -eu

export DEBIAN_FRONTEND=noninteractive
apt-get update -q
apt-get install -y -q --no-install-recommends build-essential cmake pkg-config git ca-certificates \
    qt6-base-dev qt6-base-dev-tools libqt6svg6-dev qt6-wayland libgl-dev libpipewire-0.3-dev libopus-dev \
    libogg-dev libsqlite3-dev wget file patchelf libfuse2 squashfs-tools
git config --global --add safe.directory '*' 2>/dev/null || true

ARCH=$(uname -m)
fetch() {
    wget -q --tries=6 --waitretry=10 --retry-connrefused \
        --retry-on-http-error=404,429,500,502,503,504 -O "$1" "$2"
}
TOOLS=/tmp/appimage-tools
mkdir -p "$TOOLS"
for tool in linuxdeploy linuxdeploy-plugin-qt; do
    if [ ! -x "$TOOLS/$tool-$ARCH.AppImage" ]; then
        fetch "$TOOLS/$tool-$ARCH.AppImage" \
            "https://github.com/linuxdeploy/$tool/releases/download/continuous/$tool-$ARCH.AppImage"
        chmod +x "$TOOLS/$tool-$ARCH.AppImage"
    fi
done
if [ ! -f "$TOOLS/runtime-$ARCH" ]; then
    fetch "$TOOLS/runtime-$ARCH" "https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-$ARCH"
fi
export LDAI_RUNTIME_FILE="$TOOLS/runtime-$ARCH"
export APPIMAGE_EXTRACT_AND_RUN=1
export PATH="$TOOLS:$PATH"

BUILD=build-appimage
cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DAFTERLINE_BUILD_TESTS=OFF
cmake --build "$BUILD" -j"$(nproc)"
rm -rf "$BUILD/AppDir"
DESTDIR="$PWD/$BUILD/AppDir" cmake --install "$BUILD"

VERSION=$(sed -n 's/^project(AfterLine VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
export QMAKE=/usr/lib/qt6/bin/qmake
[ -x "$QMAKE" ] || QMAKE=$(command -v qmake6)
export EXTRA_QT_PLUGINS="svg;iconengines;imageformats"
export EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so"
export LDAI_OUTPUT="AfterLine-${VERSION}-${ARCH}.AppImage"
export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"

cd "$BUILD"
# The engines' private libraries (ggml, whisper, llama) sit next to them with
# RPATH $ORIGIN; listing the engines lets linuxdeploy bundle their system deps.
ENGINES=""
for e in AppDir/usr/lib*/afterline/*/afterline-*; do
    ENGINES="$ENGINES --executable $e"
done
# shellcheck disable=SC2086
"linuxdeploy-$ARCH.AppImage" --appdir AppDir $ENGINES \
    --desktop-file AppDir/usr/share/applications/io.github.AfterLine.desktop \
    --icon-file AppDir/usr/share/icons/hicolor/256x256/apps/io.github.AfterLine.png \
    --plugin qt --output appimage
cd ..
mkdir -p dist
mv "$BUILD/$LDAI_OUTPUT" dist/
echo "Built dist/$LDAI_OUTPUT"
