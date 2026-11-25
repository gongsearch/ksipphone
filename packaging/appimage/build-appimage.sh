#!/bin/bash
# Build KSipPhone as AppImage on Ubuntu 22.04 (glibc 2.35): Qt from the official binaries (aqtinstall),
# ECM / KI18n / QtKeychain from source, baresip bundled (static), Qt-only desktop integration.
#   docker run --rm -v "$PWD":/src -w /src ubuntu:22.04 packaging/appimage/build-appimage.sh
set -euo pipefail
QT_VERSION=${QT_VERSION:-6.10.1}
KF_VERSION=${KF_VERSION:-v6.32.0}
QTKEYCHAIN_VERSION=${QTKEYCHAIN_VERSION:-0.18.0}
SRC=$(cd "$(dirname "$0")/../.." && pwd)
W=${WORKDIR:-/tmp/appimage-build}
OUT=${OUTDIR:-$SRC}
PREFIX=$W/prefix
mkdir -p "$W" "$PREFIX"

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends build-essential cmake ninja-build pkg-config git ca-certificates curl file \
  python3-pip gettext patchelf desktop-file-utils libfuse2 squashfs-tools \
  libgl1-mesa-dev libxkbcommon-dev libxkbcommon-x11-0 libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 \
  libxcb-randr0 libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 libegl1 libfontconfig1 libdbus-1-3 \
  libldap2-dev libsecret-1-dev libssl-dev zlib1g-dev libopus-dev libspandsp-dev libpipewire-0.3-dev \
  libpulse0 libgstreamer1.0-0 >/dev/null
# KF6 needs a newer CMake than Ubuntu 22.04 has
pip3 install -q cmake ninja aqtinstall
export PATH="/usr/local/bin:$PATH"
hash -r
cmake --version | head -1

# Qt
if [ ! -d "$W/qt/$QT_VERSION" ]; then
  python3 -m aqt install-qt -O "$W/qt" linux desktop "$QT_VERSION" linux_gcc_64 -m qtmultimedia qtshadertools >/dev/null
fi
QTDIR=$( (ls -d "$W/qt/$QT_VERSION"/gcc_64 "$W/qt/$QT_VERSION"/linux_gcc_64 2>/dev/null || true) | head -1)
echo "Qt: $QTDIR"
export CMAKE_PREFIX_PATH="$QTDIR;$PREFIX"
export LD_LIBRARY_PATH="$QTDIR/lib:$PREFIX/lib:${LD_LIBRARY_PATH:-}"

build() { # name url-or-git [cmake args]
  local name=$1 src=$2; shift 2
  [ -f "$W/$name.done" ] && return
  rm -rf "$W/$name"
  echo "--- $name"
  git clone -q --depth 1 -b "${src#*@}" "${src%@*}" "$W/$name"
  local log="$W/$name.log"
  { cmake -S "$W/$name" -B "$W/$name/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DBUILD_TESTING=OFF "$@" && cmake --build "$W/$name/build" && cmake --install "$W/$name/build"; } > "$log" 2>&1 \
    || { echo "=== $name failed:"; tail -40 "$log"; exit 1; }
  echo "built $name"
  touch "$W/$name.done"
}
build ecm https://invent.kde.org/frameworks/extra-cmake-modules.git@$KF_VERSION
build ki18n https://invent.kde.org/frameworks/ki18n.git@$KF_VERSION -DBUILD_WITH_QML=OFF -DBUILD_QCH=OFF
build qtkeychain https://github.com/frankosterfeld/qtkeychain.git@$QTKEYCHAIN_VERSION -DBUILD_WITH_QT6=ON \
      -DBUILD_TRANSLATIONS=OFF -DBUILD_TEST_APPLICATION=OFF

# KSipPhone (+ private static baresip)
rm -rf "$W/ksip"
cmake -S "$SRC" -B "$W/ksip" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
      -DKSIPPHONE_WITH_KDE=OFF -DKSIPPHONE_BUNDLE_BARESIP=ON -DECM_DIR="$PREFIX/share/ECM/cmake"
cmake --build "$W/ksip"
rm -rf "$W/AppDir"
DESTDIR="$W/AppDir" cmake --install "$W/ksip" >/dev/null

# The AppImage has no fixed /usr; AppRun sets KSIPPHONE_BARESIP* to the bundled baresip.
BARESIP_BIN=$( (ls "$W"/AppDir/usr/lib/*/ksipphone/baresip "$W"/AppDir/usr/lib/ksipphone/baresip 2>/dev/null || true) | head -1)
echo "bundled baresip: $BARESIP_BIN"
cd "$W"
for t in linuxdeploy-x86_64.AppImage linuxdeploy-plugin-qt-x86_64.AppImage; do
  [ -f "$t" ] || curl -sfLO "https://github.com/linuxdeploy/$(echo $t | sed 's/-x86_64.AppImage//')/releases/download/continuous/$t"
  chmod +x "$t"
done
export APPIMAGE_EXTRACT_AND_RUN=1 QMAKE="$QTDIR/bin/qmake" EXTRA_QT_MODULES="svg" EXTRA_PLATFORM_PLUGINS="libqwayland-generic.so"
VERSION=$(sed -n 's/^project(ksipphone VERSION \([0-9.]*\).*/\1/p' "$SRC/CMakeLists.txt")
export LDAI_OUTPUT="KSipPhone-$VERSION-x86_64.AppImage" VERSION
# baresip is an extra executable inside the AppDir: let linuxdeploy bundle its libraries too
./linuxdeploy-x86_64.AppImage --appdir AppDir \
    --executable "$BARESIP_BIN" \
    --desktop-file AppDir/usr/share/applications/io.github.gongsearch.KSipPhone.desktop \
    --icon-file AppDir/usr/share/icons/hicolor/scalable/apps/io.github.gongsearch.KSipPhone.svg \
    --custom-apprun "$SRC/packaging/appimage/AppRun" \
    --plugin qt --output appimage
cp "$LDAI_OUTPUT" "$OUT/"
echo "built $OUT/$LDAI_OUTPUT"
