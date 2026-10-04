#!/usr/bin/env bash
# Clones OpenRCT2 at the pinned release into OpenRCT2/ and applies the port's changes
# (patches/openrct2) as commits on a local ps5 branch. Work on that branch, then run
# tools/export-patches.sh to write the commits back to patches/openrct2.
set -euo pipefail

tag=v0.5.5
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
dest="$root/OpenRCT2"

if [[ -d $dest/.git ]]; then
    echo "$dest exists" >&2
    exit 1
fi
git -c core.autocrlf=false clone -q --depth 1 --branch "$tag" https://github.com/OpenRCT2/OpenRCT2.git "$dest"
git -C "$dest" config core.autocrlf false
git -C "$dest" config core.eol lf
git -C "$dest" switch -q -c ps5
name=$(git config user.name || echo build)
email=$(git config user.email || echo build@localhost)
git -C "$dest" -c user.name="$name" -c user.email="$email" am -q "$root"/patches/openrct2/*.patch
