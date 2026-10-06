#!/usr/bin/env bash
# Checks the console's Inno Setup reader (src/port/inno.c) against innoextract on the PC: builds it
# for Linux with liblzma from the same xz release (tools/build-deps.sh fetches it), unpacks the game
# folders from a GOG installer with both, and compares every file. Runs inside the build image:
#
#     MSYS_NO_PATHCONV=1 docker run --rm -v "$(cygpath -w "$PWD"):/src" -v "<installer folder>:/gog:ro" \
#         -w /src ps5-openrct2-build tools/test-inno.sh /gog/setup_....exe
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
setup=$1
work="$root/build/inno-test"
xz="$root/build/host-xz"
archive="$root/.deps/downloads/xz-5.8.1.tar.gz"

if [[ ! -f $xz/lib/liblzma.a ]]; then
    [[ -f $archive ]] || { echo "run tools/build-deps.sh first: $archive is missing" >&2; exit 1; }
    rm -rf "$xz" "$work-xz"
    mkdir -p "$work-xz"
    tar -xf "$archive" -C "$work-xz"
    cmake -S "$work-xz/xz-5.8.1" -B "$work-xz/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$xz" -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF -DXZ_NLS=OFF \
        -DXZ_THREADS=no -DXZ_ENCODERS= -DXZ_DECODERS="lzma1;lzma2" -DXZ_TOOL_XZ=OFF \
        -DXZ_TOOL_XZDEC=OFF -DXZ_TOOL_LZMADEC=OFF -DXZ_TOOL_LZMAINFO=OFF -DXZ_TOOL_SCRIPTS=OFF \
        -DXZ_LZIP_DECODER=OFF -DXZ_MICROLZMA_DECODER=OFF -DXZ_MICROLZMA_ENCODER=OFF -DXZ_DOC=OFF >/dev/null
    ninja -C "$work-xz/build" install >/dev/null
    rm -rf "$work-xz"
fi

rm -rf "$work"
mkdir -p "$work/ours" "$work/innoextract"
gcc -O2 -Wall -Wextra -o "$work/inno-unpack" -I"$root/src/port" -I"$xz/include" \
    "$root/tools/inno-unpack.c" "$root/src/port/inno.c" "$xz/lib/liblzma.a" -lz

start=$(date +%s)
"$work/inno-unpack" "$setup" "$work/ours"
echo "ours: $(($(date +%s) - start)) s"
start=$(date +%s)
innoextract -s -d "$work/innoextract" "$setup"
echo "innoextract: $(($(date +%s) - start)) s"

cd "$work/innoextract"
status=0
count=0
while IFS= read -r -d '' file; do
    count=$((count + 1))
    if ! cmp -s "$file" "$work/ours/$file"; then
        echo "differs or missing: $file"
        status=1
    fi
done < <(find Data ObjData Scenarios Tracks Landscapes -type f -print0 2>/dev/null)
extra=$(cd "$work/ours" && find . -type f | wc -l)
echo "compared $count files from innoextract; ours wrote $extra"
[[ $count -eq $extra ]] || status=1
exit $status
