#!/usr/bin/env bash
# Downloads, checks and builds the libraries OpenRCT2 needs for the PS5 into .deps/ps5, with the
# toolchain in cmake/ps5.cmake. Each library is built once; delete its stamp in build/deps to
# rebuild it. Runs inside the build image, after tools/build-runtime.sh.
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
downloads="$root/.deps/downloads"
sources="$root/.deps/src"
build="$root/build/deps"
prefix="$root/.deps/ps5"
toolchain="$root/cmake/ps5.cmake"
mkdir -p "$downloads" "$sources" "$build" "$prefix"

# fetch <file> <sha256> <url>...: downloads the first URL that gives the expected file.
fetch() {
    local file=$1 sha256=$2
    shift 2
    if [[ -f $downloads/$file ]] && echo "$sha256  $downloads/$file" | sha256sum --check --quiet 2>/dev/null; then
        return
    fi
    for url in "$@"; do
        if curl -fsSL -o "$downloads/$file.part" "$url" &&
            echo "$sha256  $downloads/$file.part" | sha256sum --check --quiet; then
            mv "$downloads/$file.part" "$downloads/$file"
            return
        fi
    done
    rm -f "$downloads/$file.part"
    echo "could not fetch $file" >&2
    exit 1
}

# unpack <file> <directory> [tar options]: a fresh copy of the source in .deps/src.
unpack() {
    local file=$1 directory=$2
    shift 2
    rm -rf "${sources:?}/$directory"
    case $file in
    *.zip) unzip -q "$downloads/$file" -d "$sources" ;;
    *) tar -xf "$downloads/$file" -C "$sources" "$@" ;;
    esac
}

built() { [[ -f $build/$1.stamp ]]; }
done_with() { touch "$build/$1.stamp"; }

# cmake_install <name> <source dir> [cmake options]
cmake_install() {
    local name=$1 source=$2
    shift 2
    rm -rf "${build:?}/$name"
    cmake -S "$source" -B "$build/$name" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" "$@" >"$build/$name.log"
    ninja -C "$build/$name" install >>"$build/$name.log"
}

if ! built zlib; then
    fetch zlib-1.3.2.tar.gz bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16 \
        https://github.com/madler/zlib/releases/download/v1.3.2/zlib-1.3.2.tar.gz \
        https://zlib.net/zlib-1.3.2.tar.gz
    unpack zlib-1.3.2.tar.gz zlib-1.3.2
    cmake_install zlib "$sources/zlib-1.3.2" -DZLIB_BUILD_TESTING=OFF -DZLIB_BUILD_SHARED=OFF
    done_with zlib
fi

if ! built libpng; then
    fetch libpng-1.6.59.tar.gz 2540302a1844ad2b2b501977abecfa850f265f97b78f065a712ab4074a89f5b5 \
        https://github.com/pnggroup/libpng/archive/refs/tags/v1.6.59.tar.gz
    unpack libpng-1.6.59.tar.gz libpng-1.6.59
    cmake_install libpng "$sources/libpng-1.6.59" -DPNG_SHARED=OFF -DPNG_TESTS=OFF -DPNG_TOOLS=OFF
    done_with libpng
fi

if ! built zstd; then
    fetch zstd-1.5.7.tar.gz eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3 \
        https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
    unpack zstd-1.5.7.tar.gz zstd-1.5.7 --exclude=zstd-1.5.7/tests
    # Tracing hooks are weak symbols left undefined, which a PS5 module cannot import.
    cmake_install zstd "$sources/zstd-1.5.7/build/cmake" -DZSTD_BUILD_SHARED=OFF \
        -DZSTD_BUILD_PROGRAMS=OFF -DZSTD_BUILD_TESTS=OFF -DZSTD_MULTITHREAD_SUPPORT=OFF \
        -DCMAKE_C_FLAGS=-DZSTD_TRACE=0
    done_with zstd
fi

if ! built libzip; then
    fetch libzip-1.11.4.tar.xz 8a247f57d1e3e6f6d11413b12a6f28a9d388de110adc0ec608d893180ed7097b \
        https://github.com/nih-at/libzip/releases/download/v1.11.4/libzip-1.11.4.tar.xz \
        https://libzip.org/download/libzip-1.11.4.tar.xz
    unpack libzip-1.11.4.tar.xz libzip-1.11.4
    # The console libc has the C11 Annex K functions, but the SDK headers do not declare them.
    cmake_install libzip "$sources/libzip-1.11.4" -DENABLE_BZIP2=OFF -DENABLE_LZMA=OFF \
        -DENABLE_ZSTD=ON -DENABLE_OPENSSL=OFF -DENABLE_GNUTLS=OFF -DENABLE_MBEDTLS=OFF \
        -DENABLE_COMMONCRYPTO=OFF -DENABLE_WINDOWS_CRYPTO=OFF -DBUILD_TOOLS=OFF \
        -DBUILD_REGRESS=OFF -DBUILD_OSSFUZZ=OFF -DBUILD_EXAMPLES=OFF -DBUILD_DOC=OFF \
        -DHAVE__CLOSE=OFF -DHAVE_MEMCPY_S=OFF -DHAVE_STRNCPY_S=OFF -DHAVE_STRERROR_S=OFF \
        -DHAVE_STRERRORLEN_S=OFF
    done_with libzip
fi

if ! built ogg; then
    fetch libogg-1.3.6.tar.xz 5c8253428e181840cd20d41f3ca16557a9cc04bad4a3d04cce84808677fa1061 \
        https://github.com/xiph/ogg/releases/download/v1.3.6/libogg-1.3.6.tar.xz \
        https://downloads.xiph.org/releases/ogg/libogg-1.3.6.tar.xz
    unpack libogg-1.3.6.tar.xz libogg-1.3.6
    cmake_install ogg "$sources/libogg-1.3.6" -DBUILD_TESTING=OFF -DINSTALL_DOCS=OFF
    done_with ogg
fi

if ! built vorbis; then
    fetch libvorbis-1.3.7.tar.xz b33cc4934322bcbf6efcbacf49e3ca01aadbea4114ec9589d1b1e9d20f72954b \
        https://github.com/xiph/vorbis/releases/download/v1.3.7/libvorbis-1.3.7.tar.xz \
        https://downloads.xiph.org/releases/vorbis/libvorbis-1.3.7.tar.xz
    unpack libvorbis-1.3.7.tar.xz libvorbis-1.3.7
    cmake_install vorbis "$sources/libvorbis-1.3.7" -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    done_with vorbis
fi

if ! built flac; then
    fetch flac-1.5.0.tar.xz f2c1c76592a82ffff8413ba3c4a1299b6c7ab06c734dee03fd88630485c2b920 \
        https://github.com/xiph/flac/releases/download/1.5.0/flac-1.5.0.tar.xz \
        https://downloads.xiph.org/releases/flac/flac-1.5.0.tar.xz
    unpack flac-1.5.0.tar.xz flac-1.5.0
    cmake_install flac "$sources/flac-1.5.0" -DBUILD_CXXLIBS=OFF -DBUILD_PROGRAMS=OFF \
        -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF -DBUILD_DOCS=OFF -DINSTALL_MANPAGES=OFF \
        -DWITH_OGG=ON -DENABLE_MULTITHREADING=OFF
    done_with flac
fi

# nlohmann/json 3.12 does not build with OpenRCT2's -fno-char8_t; 3.11.3 is what its CI uses.
if ! built json; then
    fetch json-3.11.3.tar.xz d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d \
        https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    unpack json-3.11.3.tar.xz json
    mkdir -p "$prefix/include"
    rm -rf "$prefix/include/nlohmann"
    cp -r "$sources/json/include/nlohmann" "$prefix/include/"
    done_with json
fi

# ICU's common library and a data library holding only the converters for the code pages
# OpenRCT2 reads (String::convertToUtf8: windows-1252, windows-932, GB2312, windows-949 and
# Big5; tools/icu-filter.json). Case mapping and bidi data are compiled into the code. ICU builds
# its data with tools made for the build machine, so a cross build needs a host build first.
# Every char string on the console is UTF-8, wchar_t has 16 bits (ICU assumes 32 on FreeBSD), and
# the libc has no nl_langinfo.
if ! built icu; then
    fetch icu4c-78.3-sources.tgz 3a2e7a47604ba702f345878308e6fefeca612ee895cf4a5f222e7955fabfe0c0 \
        https://github.com/unicode-org/icu/releases/download/release-78.3/icu4c-78.3-sources.tgz
    fetch icu4c-78.3-data.zip 9d8b3899096aeb83e4e21ef8a40fec9e03b28db18c48452efac882ce25a91e27 \
        https://github.com/unicode-org/icu/releases/download/release-78.3/icu4c-78.3-data.zip
    unpack icu4c-78.3-sources.tgz icu
    rm -rf "$sources/icu/source/data"
    unzip -q "$downloads/icu4c-78.3-data.zip" -d "$sources/icu/source"
    export ICU_DATA_FILTER_FILE="$root/tools/icu-filter.json"
    options=(--disable-shared --enable-static --disable-tests --disable-samples --disable-extras
        --disable-icuio --disable-layoutex)
    rm -rf "${build:?}/icu-host" "${build:?}/icu"
    mkdir -p "$build/icu-host" "$build/icu"
    (cd "$build/icu-host" &&
        CC=clang CXX=clang++ "$sources/icu/source/configure" "${options[@]}" &&
        make -j"$(nproc)") >"$build/icu-host.log" 2>&1
    flags="-O2 -ffunction-sections -fdata-sections -DU_STATIC_IMPLEMENTATION -DU_CHARSET_IS_UTF8=1"
    flags+=" -DU_HAVE_NL_LANGINFO_CODESET=0 -DU_SIZEOF_WCHAR_T=2"
    (cd "$build/icu" &&
        CC="$root/tools/ps5-cc" CXX="$root/tools/ps5-c++" AR=llvm-ar-18 RANLIB=llvm-ranlib-18 \
            CFLAGS="$flags" CXXFLAGS="$flags" \
            "$sources/icu/source/configure" --host=x86_64-unknown-freebsd \
            --build=x86_64-pc-linux-gnu --with-cross-build="$build/icu-host" --prefix="$prefix" \
            --with-data-packaging=static --disable-tools "${options[@]}" &&
        make -j"$(nproc)" &&
        make install) >"$build/icu.log" 2>&1
    done_with icu
fi

# SDL2 with ps5-payload-dev's PS5 drivers: VideoOut framebuffer, AudioOut, ScePad, the USB
# keyboard and the IME dialog, plus the changes in patches/sdl. No OpenGL (OSMesa).
if ! built sdl2; then
    fetch SDL-ee4c47dc.tar.gz 869cf9d26ae7ff9f73c05762f0a5cc988655d90bd7ce9167c4d4e0d76634dd31 \
        https://github.com/ps5-payload-dev/SDL/archive/ee4c47dc0d617b3bc8f35108f9956baf228a1322.tar.gz
    sdl="$sources/SDL-ee4c47dc0d617b3bc8f35108f9956baf228a1322"
    unpack SDL-ee4c47dc.tar.gz "${sdl##*/}" --exclude='*/android-project-ant'
    for patch in "$root"/patches/sdl/*.patch; do
        patch -d "$sdl" -p1 --quiet <"$patch"
    done
    cmake_install sdl2 "$sdl" -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF -DSDL_OPENGL=OFF \
        -DSDL_LIBSAMPLERATE=OFF -DSDL_LOADSO=OFF
    done_with sdl2
fi

# qrcodegen for the import screen's QR code; it is compiled with the game (cmake/ps5-openrct2.cmake).
if ! built qrcodegen; then
    fetch QR-Code-generator-1.8.0.tar.gz 2ec0a4d33d6f521c942eeaf473d42d5fe139abcfa57d2beffe10c5cf7d34ae60 \
        https://github.com/nayuki/QR-Code-generator/archive/refs/tags/v1.8.0.tar.gz
    unpack QR-Code-generator-1.8.0.tar.gz QR-Code-generator-1.8.0
    done_with qrcodegen
fi
