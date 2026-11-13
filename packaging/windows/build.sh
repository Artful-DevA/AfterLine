#!/usr/bin/env bash
# Builds AfterLine for Windows in an MSYS2 UCRT64 shell and lays out a
# self-contained folder in dist/AfterLine (the installer packs it):
#   dist/AfterLine/bin/afterline.exe (+ Qt and runtime DLLs)
#   dist/AfterLine/lib/afterline/{transcriber,summarizer}/ (engines + DLLs)
# Packages: mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,pkgconf,qt6-base,
#   qt6-svg,qt6-tools,opus,libogg,sqlite3}
set -euo pipefail
cd "$(dirname "$0")/../.."
root=$PWD
build=${BUILD_DIR:-build-windows}
dist=$root/dist/AfterLine

cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$dist" \
  ${AFTERLINE_EXTRA_CMAKE:-}
cmake --build "$build"
if [ "${SKIP_TESTS:-0}" != 1 ]; then
  (cd "$build" && ctest --output-on-failure)
fi
rm -rf "$dist"
cmake --install "$build"

# Qt: plugins (platform, styles, image formats, SVG) and libraries.
windeployqt6 --release --no-translations --no-compiler-runtime --no-system-d3d-compiler \
  --no-opengl-sw "$dist/bin/afterline.exe"

# Every other DLL each program needs (compiler runtime, Opus, Ogg, SQLite,
# Qt Core for the engines), copied next to that program. Windows looks for a
# DLL's own dependencies next to the program, so the DLLs of Qt's plugins
# (bin/platforms/...) go into bin as well.
copy_deps() {  # file, destination directory
  ldd "$1" | awk '/=> \/ucrt64\// {print $3}' | sort -u | while read -r dll; do
    [ -f "$2/$(basename "$dll")" ] || cp "$dll" "$2/"
  done
}
for pass in 1 2 3; do  # DLLs pulled in by copied DLLs
  for f in "$dist"/bin/*.exe "$dist"/bin/*.dll "$dist"/bin/*/*.dll; do
    copy_deps "$f" "$dist/bin"
  done
  for dir in "$dist"/lib/afterline/*/; do
    for f in "$dir"*.exe "$dir"*.dll; do
      copy_deps "$f" "$dir"
    done
  done
done

cp LICENSE THIRD_PARTY_NOTICES.md "$dist/"
echo "Built $dist"
