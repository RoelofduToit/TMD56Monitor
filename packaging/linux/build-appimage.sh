#!/usr/bin/env bash
# Build a relocatable AppImage from the CMake install tree.
# GTK 4 and libserialport are bundled. The result does not need a compiler or -dev packages.
set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)
build=${TMD_BUILD_DIR:-"$root/build-appimage"}
tools="$build/tools"
appdir="$build/AppDir"
mkdir -p "$tools" "$root/dist"

cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$build" -j"$(nproc)"
rm -rf "$appdir"
cmake --install "$build" --prefix "$appdir/usr"

fetch() {
    local url=$1
    local dest=$2
    if [[ ! -x "$dest" ]]; then
        curl -L --fail --retry 3 -o "$dest" "$url"
        chmod +x "$dest"
    fi
}

fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" \
    "$tools/linuxdeploy"
fetch "https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh" \
    "$tools/linuxdeploy-plugin-gtk"

export PATH="$tools:$PATH"
export ARCH=x86_64
export DEPLOY_GTK_VERSION=4
export APPIMAGE_EXTRACT_AND_RUN=1
export LDAI_OUTPUT="$root/dist/TMD56Monitor-x86_64.AppImage"
# A pkg-config sysroot makes the GTK plugin look for modules inside the
# sysroot instead of the libraries it just copied. Native packaging should
# follow the real install prefixes.
unset PKG_CONFIG_SYSROOT_DIR

cd "$build"
linuxdeploy --appdir "$appdir" \
    --desktop-file "$appdir/usr/share/applications/tmd56-monitor.desktop" \
    --icon-file "$root/resources/icons/tmd56-monitor.svg" \
    --plugin gtk \
    --output appimage

if [[ ! -f "$root/dist/TMD56Monitor-x86_64.AppImage" ]]; then
    produced=$(find "$build" "$root" -maxdepth 2 -name '*.AppImage' -printf '%T@ %p\n' | sort -n | tail -1 | cut -d' ' -f2-)
    if [[ -z "$produced" ]]; then
        echo "tmd56: AppImage was not produced" >&2
        exit 1
    fi
    mv "$produced" "$root/dist/TMD56Monitor-x86_64.AppImage"
fi
chmod +x "$root/dist/TMD56Monitor-x86_64.AppImage"
echo "tmd56: $root/dist/TMD56Monitor-x86_64.AppImage"
