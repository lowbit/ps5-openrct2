# OpenRCT2 for PS5 (native title)

OpenRCT2 (RollerCoaster Tycoon 2) as an installable PS5 title, title ID `PPSA99728`. OpenRCT2
v0.5.5 with the software renderer, on SDL2 from ps5-payload-dev, linked into a PS5 module with the
native-app boilerplate's tools. The player brings the RCT2 data; none is in this repository.

Status (2026-10-04): the spike. Everything compiles and links into a signed `eboot.bin`; nothing
has run on the console yet.

## Layout

| Path | What |
| --- | --- |
| `OpenRCT2/` | OpenRCT2 at `v0.5.5` with `patches/openrct2` applied as commits on a local `ps5` branch (not tracked; `tools/fetch-openrct2.sh`) |
| `patches/openrct2/` | The port's changes to OpenRCT2 (`tools/export-patches.sh` writes them from the `ps5` branch) |
| `patches/sdl/` | Changes to ps5-payload-dev's SDL |
| `src/runtime/` | What every PS5 link gets (`libps5runtime.a`): malloc from a direct-memory mspace, libc gaps, sandbox-safe folder listing, the linker script |
| `src/port/` | PS5 code compiled into the game (`main`) |
| `cmake/ps5.cmake` | CMake toolchain for the console |
| `cmake/ps5-openrct2.cmake` | Hooked into OpenRCT2's project: adds `src/port` and the static-only libraries |
| `tools/ps5-cc`, `tools/ps5-c++`, `tools/ps5-link` | Compiler drivers for the console; any link produces a PS5 module |
| `tools/build-*.sh` | Native tools, runtime, dependencies, host build, PS5 build |
| `docker/` | The build image |

## Build

```bash
docker build -t ps5-openrct2-build docker/
MSYS_NO_PATHCONV=1 docker run --rm -v "$(cygpath -w "$PWD"):/src" -w /src ps5-openrct2-build make host ps5
```

`make host` builds OpenRCT2 for Linux in the container and installs it into `build/host-install`.
The PS5 build needs it for its data folder: OpenRCT2 makes `g2.dat`, `fonts.dat`, `palettes.dat`
and `tracks.dat` with its own `openrct2-cli` and skips that step when cross-compiling, and the
install step downloads the objects, title sequences, OpenSFX and OpenMusic from `assets.json`.
`build/host-install/share/openrct2` becomes `/app0/data` on the console.

`make ps5` builds, in order:

1. `tools/fetch-native-tools.sh`: ps5-native-app-boilerplate at `b1315a9` (as DOOM): the PS5
   payload SDK v0.42 with its static libc++, `ps5-native-tool` (PIE to PS5 module, FSELF signing)
   and the clean-room `libc.prx`.
2. `tools/build-runtime.sh`: the CRT (the boilerplate's `app_crt.cpp`), `libps5runtime.a` and the
   system module stubs into `.deps/ps5`.
3. `tools/build-deps.sh`: zlib, libpng, zstd, libzip, Ogg, Vorbis, FLAC, nlohmann/json, ICU and
   SDL2, pinned and checked by SHA-256, into `.deps/ps5`. Each has a stamp in `build/deps`.
4. `tools/build-ps5.sh`: OpenRCT2 with `DISABLE_OPENGL`, `DISABLE_NETWORK`, `DISABLE_HTTP`,
   `DISABLE_TTF`, scripting on, into `build/ps5/openrct2` (the unsigned module).

Sign it with `.deps/native-app/build/host/ps5-native-tool self --sign --in build/ps5/openrct2
--out eboot.bin --magic 0x1D3D154F`.

### The toolchain

`tools/ps5-cc` and `tools/ps5-c++` are clang 18 with the payload SDK's target flags
(`x86_64-sie-ps5`, `-fPIC -fno-plt -femulated-tls`). Asked to link, they call `tools/ps5-link`,
which links with `prospero-lld` and the boilerplate's PIE layout (`src/runtime/ps5.ld` adds the
`__eh_frame` bounds libunwind needs), the CRT, `libps5runtime.a`, libc++, libc++abi, libunwind,
compiler-rt's builtins and the system stubs, then converts the result with `ps5-native-tool link`.
Because every link is real, CMake and autotools configure checks see exactly what the console
offers, instead of guessing from compile-only checks.

- `--no-dependent-libraries`: libc++ asks for `-lpthread`; pthreads come from libkernel.
- The malloc family is wrapped (`--wrap`) into `src/runtime/heap.c`.
- The stubs leave out `libScePosixForWebKit` (a game process does not load it, so anything bound
  to it would be null; oot64-ps5 found this) and `libmonosgen`.
- `ps5-native-tool` rejects weak symbols nothing defines: zstd is built with `ZSTD_TRACE=0`, and the
  runtime defines `__cxa_thread_atexit_impl`.

### Choices made in the spike

- **SDL2:** ps5-payload-dev's fork (`ref/sdk/SDL`, pinned at `ee4c47dc`), built from source. Its
  video driver gives a VideoOut framebuffer, which is what the software renderer needs: OpenRCT2
  0.5.5 has no plain software engine any more, only `softwareWithHardwareDisplay`, which goes
  through SDL_Renderer, and SDL's software renderer draws into that framebuffer. The fork also has
  AudioOut, ScePad, the USB keyboard and the IME dialog. ps5-opengl's SDL2 bridge (used by
  oot64-ps5 and morrowind-ps5) offers only an OpenGL window: no window surface, no SDL_Renderer,
  no audio, no mouse; it is the route for OpenRCT2's OpenGL engine (GL 3.3 core) later.
  Patches: no dynamic API; no unconditional `-lsamplerate`.
- **ICU:** built (78.3), not patched out. OpenRCT2 uses it for UTF conversions, upper-casing, five
  code pages and Arabic/Hebrew shaping. A host build first (ICU builds its data with its own
  tools), then the cross build with `U_STATIC_IMPLEMENTATION` (the PS5 target supports
  `__declspec`, so ICU's headers would otherwise export and import), `U_CHARSET_IS_UTF8=1` and no
  `nl_langinfo`. `tools/icu-filter.json` keeps only the five converters OpenRCT2 reads, so the
  data library is 253 KB. OpenRCT2 must also be compiled with `U_STATIC_IMPLEMENTATION`.
- **nlohmann/json 3.11.3:** 3.12 does not compile with OpenRCT2's `-fno-char8_t`.

## The runtime (`src/runtime`)

- `heap.c`: malloc, calloc, realloc, free, posix_memalign, memalign, aligned_alloc and
  malloc_usable_size from an mspace (`sceLibcMspaceCreate`) in 2 GiB of direct memory (1 GiB, then
  512 MiB if that fails). A native title's libc heap is small (DOOM and oot64-ps5 found the same).
  Blocks allocated before or beyond it come from the libc heap; free routes by address.
- `dirent.c`: `opendir`, `readdir`, `scandir` and the rest on `sceKernelOpen(O_DIRECTORY)` and
  `sceKernelGetdents`, since the sandbox refuses the libc's `opendir` (DOOM's finding). OpenRCT2's
  file scanner uses `scandir`; libc++'s filesystem uses `opendir`.
- `libc.c`: what the console's libc lacks: the `*_l` locale functions (C locale only),
  `localtime_r`/`gmtime_r`, `mbsnrtowcs`/`wcsnrtombs`, `fnmatch`, message catalogue stubs,
  `getpwuid` (no user database), `arc4random`, `mkstemp`, `strcasestr`, `isatty`, `__assert`,
  `__cxa_thread_atexit_impl`. The C11 Annex K functions (`memcpy_s` and so on) exist in the libc
  but not in the SDK headers, so libzip is told it has none.

## Game data

RCT2 from the GOG Triple Thrill Pack installer, extracted with `innoextract --gog` (in the build
image) to `E:\scratch\rct2`: `Data` (with `g1.dat`), `ObjData` (2122 objects), `Scenarios` (57),
`Tracks`, `Landscapes`, about 680 MB. Never commit or publish any of it.

## External code (pinned)

- [OpenRCT2](https://github.com/OpenRCT2/OpenRCT2) v0.5.5 (GPL-3.0)
- [ps5-payload-dev/SDL](https://github.com/ps5-payload-dev/SDL) `ee4c47dc` (zlib)
- [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)
  `b1315a9` (GPL-3.0-or-later): `ps5-native-tool`, `libc.prx`, `app_crt.cpp`, the PIE linker
  script, and the PS5 payload SDK v0.42 it fetches (libc++, libc++abi, libunwind)
- zlib 1.3.2, libpng 1.6.59, zstd 1.5.7, libzip 1.11.4, libogg 1.3.6, libvorbis 1.3.7, FLAC 1.5.0,
  nlohmann/json 3.11.3, ICU 78.3
