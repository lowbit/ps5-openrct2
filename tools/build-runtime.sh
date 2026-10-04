#!/usr/bin/env bash
# Builds what tools/ps5-link adds to every PS5 link: the CRT (the native-app boilerplate's
# app_crt.cpp), libps5runtime.a from src/runtime, and the system module stubs a game title may
# import (the SDK's, without the modules a game process does not load: anything bound to them
# would be null, so it should fail to link instead; plus stubs made from src/runtime/stubs for
# modules the SDK lacks).
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
native="$root/.deps/native-app"
sdk="$native/.deps/native/ps5-payload-sdk"
prefix="$root/.deps/ps5"
build="$root/build/runtime"
mkdir -p "$prefix/lib" "$prefix/stubs" "$build"

for stub in "$sdk"/target/lib/*.so; do
    case ${stub##*/} in
    libScePosixForWebKit.so | libkernel_sys.so | libkernel_web.so | libmonosgen-2.0.so) ;;
    *) cp "$stub" "$prefix/stubs/" ;;
    esac
done

# Modules the SDK has no stubs for: src/runtime/stubs/<module>.txt lists the functions used.
for list in "$root"/src/runtime/stubs/*.txt; do
    module=$(basename "$list" .txt)
    sed 's/.*/void &(void) {}/' "$list" >"$build/$module.c"
    "$root/tools/ps5-cc" -c "$build/$module.c" -o "$build/$module.o"
    "$sdk/bin/prospero-lld" --shared -soname "$module.prx" -o "$prefix/stubs/$module.so" "$build/$module.o"
done

# Outputs are replaced only when they change, so the game relinks only then.
install_if_changed() {
    if cmp -s "$1" "$2"; then rm -f "$1"; else mv "$1" "$2"; fi
}

"$root/tools/ps5-c++" -std=c++20 -O2 -fno-exceptions -fno-rtti \
    -c "$native/tooling/native/app_crt.cpp" -o "$build/ps5-crt.o"
install_if_changed "$build/ps5-crt.o" "$prefix/lib/ps5-crt.o"

objects=()
for source in "$root"/src/runtime/*.c; do
    object="$build/$(basename "$source" .c).o"
    "$root/tools/ps5-cc" -std=gnu11 -O2 -Wall -Wextra -ffunction-sections -fdata-sections \
        -c "$source" -o "$object"
    objects+=("$object")
done
rm -f "$build/libps5runtime.a"
llvm-ar-18 rcsD "$build/libps5runtime.a" "${objects[@]}"
install_if_changed "$build/libps5runtime.a" "$prefix/lib/libps5runtime.a"
