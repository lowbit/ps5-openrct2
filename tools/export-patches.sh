#!/usr/bin/env bash
# Writes the commits on OpenRCT2/'s ps5 branch since the pinned release to patches/openrct2.
set -euo pipefail

tag=v0.5.5
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)

rm -f "$root"/patches/openrct2/*.patch
git -C "$root/OpenRCT2" format-patch -q --no-signature --zero-commit -N "$tag..ps5" \
    -o "$root/patches/openrct2"
ls "$root/patches/openrct2"
