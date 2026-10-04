#!/usr/bin/env bash
# Builds OpenRCT2 for Linux inside the build image and installs it into build/host-install.
# The PS5 build takes its data folder from there: the sprite files (g2, fonts, palettes, tracks)
# are made by the host openrct2-cli, and the install step downloads the objects, title sequences,
# OpenSFX and OpenMusic listed in assets.json.
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build="$root/build/host"

cmake -S "$root/OpenRCT2" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_INSTALL_PREFIX="$root/build/host-install" \
    -DDISABLE_DISCORD_RPC=ON -DOPENRCT2_USE_CCACHE=OFF
ninja -C "$build"
ninja -C "$build" install
