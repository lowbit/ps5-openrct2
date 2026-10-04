#!/bin/sh
# Names the frames of a console crash report: tools/symbolize.sh ADDRESS...
# Takes the addresses as the report prints them (the eboot loads at 0x400000) and looks them up
# in build/ps5/openrct2.pie, the linked ELF that tools/ps5-link keeps. Runs in the build image.
elf=$(dirname -- "$0")/../build/ps5/openrct2.pie
symbols=$(mktemp)
llvm-nm-18 -n -C "$elf" >"$symbols"
for address in "$@"; do
    offset=$(printf "%016x" $((0x$address - 0x400000)))
    printf "%s  " "$address"
    awk -v offset="$offset" '$1 <= offset' "$symbols" | tail -1 | cut -d' ' -f3- | cut -c1-200
done
rm -f "$symbols"
