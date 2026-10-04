#!/usr/bin/env bash
# Configures and builds OpenRCT2 for the PS5 with the software renderer, against the libraries
# from tools/build-deps.sh. The result, build/ps5/openrct2, is the unsigned PS5 module.
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build="$root/build/ps5"

# Headers the import screen includes: its web page and its fonts.
python3 "$root/tools/embed-file.py" "$root/src/port/upload.html" upload_page "$root/build/gen/upload_page.h"
python3 "$root/tools/make-font.py" "$root/build/gen/font.h"

cmake -S "$root/OpenRCT2" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$root/cmake/ps5.cmake" \
    -DCMAKE_PROJECT_INCLUDE="$root/cmake/ps5-openrct2.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DDISABLE_OPENGL=ON -DDISABLE_NETWORK=ON -DDISABLE_HTTP=ON -DDISABLE_TTF=ON \
    -DDISABLE_DISCORD_RPC=ON -DDISABLE_VERSION_CHECKER=ON -DDISABLE_GOOGLE_BENCHMARK=ON \
    -DDOWNLOAD_TITLE_SEQUENCES=OFF -DDOWNLOAD_OBJECTS=OFF -DDOWNLOAD_OPENSFX=OFF \
    -DDOWNLOAD_OPENMUSIC=OFF -DOPENRCT2_USE_CCACHE=ON
ninja -C "$build" openrct2
