#!/usr/bin/env bash
# Assembles the title folder dist/PPSA99728 from the PS5 build and the host build's data folder:
# the signed eboot.bin, the boilerplate's libc.prx, sce_sys, OpenRCT2's data as data/ and the
# licences. The player's RCT2 data is not part of it.
set -euo pipefail

title=PPSA99728
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
native="$root/.deps/native-app"
tool="$native/build/host/ps5-native-tool"
sources="$root/.deps/src"
app="$root/dist/$title"

rm -rf "$app"
mkdir -p "$app/sce_sys" "$app/sce_module" "$app/licenses"
"$tool" self --sign --in "$root/build/ps5/openrct2" --out "$app/eboot.bin" --magic 0x1D3D154F >/dev/null
cp "$native/runtime/libc.prx" "$app/sce_module/libc.prx"
cp "$root/sce_sys/param.json" "$root/sce_sys/icon0.png" "$app/sce_sys/"
cp -r "$root/build/host-install/share/openrct2" "$app/data"
# OpenRCT2 looks for its documentation (the changelog it shows after an update) in the data
# folder when no system documentation folder exists.
cp "$root"/build/host-install/share/doc/openrct2/* "$app/data/"

cp "$root/LICENSE" "$app/licenses/OpenRCT2-GPL-3.0.txt"
cp "$native/LICENSE" "$app/licenses/libc-prx-GPL-3.0.txt"
cp "$sources"/SDL-*/LICENSE.txt "$app/licenses/SDL2.txt"
cp "$sources/icu/LICENSE" "$app/licenses/ICU.txt"
cp "$sources/zlib-1.3.2/LICENSE" "$app/licenses/zlib.txt"
cp "$sources/libpng-1.6.59/LICENSE" "$app/licenses/libpng.txt"
cp "$sources/zstd-1.5.7/LICENSE" "$app/licenses/zstd.txt"
cp "$sources/libzip-1.11.4/LICENSE" "$app/licenses/libzip.txt"
cp "$sources/libogg-1.3.6/COPYING" "$app/licenses/libogg.txt"
cp "$sources/libvorbis-1.3.7/COPYING" "$app/licenses/libvorbis.txt"
cp "$sources/flac-1.5.0/COPYING.Xiph" "$app/licenses/FLAC.txt"
cp "$sources/json/LICENSE.MIT" "$app/licenses/nlohmann-json.txt"
"$tool" self --inspect --file "$app/eboot.bin" | grep -E "integrity|sha256"
du -sh "$app"
