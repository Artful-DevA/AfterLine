#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION="${VERSION:-0.1.0}"
TARGET="${TARGET:-deb}"
DISTRO="${DISTRO:-$(. /etc/os-release; echo "$ID")}"
ARCH="$(uname -m)"
case "$ARCH" in x86_64) ;; *) echo "Only x86_64 packages configured" >&2; exit 1;; esac
export PIP_DISABLE_PIP_VERSION_CHECK=1
python3 -m venv .buildvenv
.buildvenv/bin/python -m pip install --upgrade pip wheel
.buildvenv/bin/python -m pip install -r requirements.txt pyinstaller
.buildvenv/bin/pyinstaller --noconfirm --clean --onedir --windowed --name afterline --collect-all faster_whisper --collect-all ctranslate2 --collect-all av --collect-all tokenizers --collect-all huggingface_hub main.py
PACKDIR="$(mktemp -d)"
trap 'rm -rf "$PACKDIR"' EXIT
mkdir -p "$PACKDIR/opt/afterline" "$PACKDIR/usr/bin" "$PACKDIR/usr/share/applications" "$PACKDIR/usr/share/icons/hicolor/scalable/apps" dist
cp -a dist/afterline/. "$PACKDIR/opt/afterline/"
printf '#!/bin/sh\nexec /opt/afterline/afterline "$@"\n' > "$PACKDIR/usr/bin/afterline"
chmod 755 "$PACKDIR/usr/bin/afterline"
cp packaging/afterline.desktop "$PACKDIR/usr/share/applications/"
cp assets/afterline.svg "$PACKDIR/usr/share/icons/hicolor/scalable/apps/afterline.svg"
case "$TARGET" in
deb)
  mkdir -p "$PACKDIR/DEBIAN"
  INSTALLED_SIZE="$(du -sk "$PACKDIR" | cut -f1)"
  cat > "$PACKDIR/DEBIAN/control" <<EOF
Package: afterline
Version: $VERSION
Section: sound
Priority: optional
Architecture: amd64
Maintainer: AfterLine contributors
Installed-Size: $INSTALLED_SIZE
Depends: pulseaudio-utils, libc6, libglib2.0-0t64 | libglib2.0-0
Description: AfterLine offline-first meeting recorder
 Record locally, transcribe speech and create private meeting notes.
EOF
  dpkg-deb --build --root-owner-group "$PACKDIR" "dist/afterline_${VERSION}_${DISTRO}_amd64.deb"
  ;;
rpm)
  command -v rpmbuild >/dev/null || { echo "rpm-build required" >&2; exit 1; }
  RPMTEMP="$(mktemp -d)"
  mkdir -p "$RPMTEMP"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}
  SPEC="$RPMTEMP/SPECS/afterline.spec"
  cat > "$SPEC" <<EOF
Name: afterline
Version: $VERSION
Release: 1
Summary: Offline-first Linux meeting recorder
License: MIT
BuildArch: x86_64
AutoReqProv: no
Requires: pulseaudio-utils

%description
Local computer audio recording, offline transcription and optional local notes.

%install
mkdir -p %{buildroot}
cp -a $PACKDIR/. %{buildroot}/

%files
/opt/afterline
/usr/bin/afterline
/usr/share/applications/afterline.desktop
/usr/share/icons/hicolor/scalable/apps/afterline.svg
EOF
  rpmbuild -bb --define "_topdir $RPMTEMP" "$SPEC"
  cp "$RPMTEMP"/RPMS/x86_64/*.rpm dist/
  rm -rf "$RPMTEMP"
  ;;
arch)
  # Use standard pacman .PKGINFO format, with package files at archive root.
  mkdir -p "$PACKDIR/usr/share/licenses/afterline"
  cp LICENSE "$PACKDIR/usr/share/licenses/afterline/LICENSE"
  cat > "$PACKDIR/.PKGINFO" <<EOF
pkgname = afterline
pkgbase = afterline
pkgver = $VERSION-1
pkgdesc = Offline-first meeting recorder and local notes
url = https://github.com/Artful-DevA/AfterLine
builddate = $(date +%s)
packager = AfterLine contributors
size = $(du -sb "$PACKDIR" | cut -f1)
arch = x86_64
license = MIT
depend = libpulse
EOF
  tar -C "$PACKDIR" --zstd -cf "dist/afterline-${VERSION}-1-x86_64.pkg.tar.zst" .
  ;;
appimage)
  APPDIR="$(pwd)/dist/AfterLine.AppDir"
  rm -rf "$APPDIR"
  mkdir -p "$APPDIR/usr/bin"
  cp -a "$PACKDIR/opt/afterline" "$APPDIR/usr/bin/"
  cp packaging/afterline.desktop "$APPDIR/afterline.desktop"
  cp assets/afterline.svg "$APPDIR/afterline.svg"
  cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/sh
HERE="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
exec "$HERE/usr/bin/afterline/afterline" "$@"
EOF
  chmod +x "$APPDIR/AppRun"
  command -v appimagetool >/dev/null || { echo "appimagetool is required" >&2; exit 1; }
  ARCH=x86_64 appimagetool "$APPDIR" "dist/AfterLine-${VERSION}-x86_64.AppImage"
  ;;
*) echo "Unknown packaging target $TARGET" >&2; exit 1;;
esac
