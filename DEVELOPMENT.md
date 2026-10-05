# OpenRCT2 for PS5 (native title)

OpenRCT2 (RollerCoaster Tycoon 2) as an installable PS5 title, title ID `PPSA99728`. OpenRCT2
v0.5.5 with the software renderer, on SDL2 from ps5-payload-dev, linked into a PS5 module with the
native-app boilerplate's tools. The player brings the RCT2 data; none is in this repository.

Status (2026-10-05): runs on the console (FW 13.00) at 1080p: title screen, starting a scenario,
playing at 56 to 58 frames per second, saving, and importing the game files into an empty title
from a PC (2457 files, 671 MB, in about 2.5 minutes), after which the game starts by itself. It is
played with the DualSense alone (Controls, below); console tests drove the menus, lists, tabs,
radial menus, track and path building and scenery placement with it. Not yet confirmed on
hardware: how the controls feel in a person's hands, typing in the IME dialog (it opens), a ZIP
import, and an import from a phone's browser. The sound works (Rijad heard it, 2026-10-05). The
controller is the only input: mice, USB keyboards and the TV remote are not read, and text comes
from the on-screen keyboard.

## Layout

| Path | What |
| --- | --- |
| `OpenRCT2/` | OpenRCT2 at `v0.5.5` with `patches/openrct2` applied as commits on a local `ps5` branch (not tracked; `tools/fetch-openrct2.sh`) |
| `patches/openrct2/` | The port's changes to OpenRCT2 (`tools/export-patches.sh` writes them from the `ps5` branch) |
| `patches/sdl/` | Changes to ps5-payload-dev's SDL (made in `.deps/sdl-dev`, a git copy, with `git format-patch`) |
| `src/runtime/` | What every PS5 link gets (`libps5runtime.a`), the linker script, and lists for module stubs the SDK lacks |
| `src/port/` | PS5 code compiled into the game: `main`, the import screen and its upload server and page, the console test harness |
| `sce_sys/` | `param.json` and the icon (the OpenRCT2 logo on a dark background) |
| `test/` | Console test plans |
| `cmake/ps5.cmake` | CMake toolchain for the console |
| `cmake/ps5-openrct2.cmake` | Hooked into OpenRCT2's project: adds `src/port`, qrcodegen, the generated headers and the static-only libraries |
| `tools/ps5-cc`, `tools/ps5-c++`, `tools/ps5-link` | Compiler drivers for the console; any link produces a PS5 module |
| `tools/build-*.sh`, `tools/package.sh` | Native tools, runtime, dependencies, host build, PS5 build, title folder |
| `tools/deploy.py`, `console_test.py`, `send.py`, `klog.py`, `symbolize.sh` | Console side: upload, tests, sending game files, kernel log, crash frames |
| `docker/` | The build image |

## Build

```bash
docker build -t ps5-openrct2-build docker/
MSYS_NO_PATHCONV=1 docker run --rm -v "$(cygpath -w "$PWD"):/src" -w /src ps5-openrct2-build make host package
```

`make host` builds OpenRCT2 for Linux in the container and installs it into `build/host-install`.
The PS5 build needs it for its data folder: OpenRCT2 makes `g2.dat`, `fonts.dat`, `palettes.dat`
and `tracks.dat` with its own `openrct2-cli` and skips that step when cross-compiling, and the
install step downloads the objects, title sequences, OpenSFX and OpenMusic from `assets.json`.
`build/host-install/share/openrct2`, plus the documentation (OpenRCT2 shows the changelog after an
update and looks for it in the data folder when there is no system documentation folder), becomes
`/app0/data` on the console. The host build is also handy for comparing behaviour: run
`build/host-install/bin/openrct2-cli` in the container with the same data.

`make package` builds, in order:

1. `tools/fetch-native-tools.sh`: ps5-native-app-boilerplate at `b1315a9` (as DOOM): the PS5
   payload SDK v0.42 with its static libc++, `ps5-native-tool` (PIE to PS5 module, FSELF signing)
   and the clean-room `libc.prx`.
2. `tools/build-runtime.sh`: the CRT (the boilerplate's `app_crt.cpp`), `libps5runtime.a` and the
   system module stubs into `.deps/ps5`. Outputs are replaced only when they change.
3. `tools/build-deps.sh`: zlib, libpng, zstd, libzip, Ogg, Vorbis, FLAC, nlohmann/json, ICU, SDL2
   and qrcodegen, pinned and checked by SHA-256, into `.deps/ps5`. Each has a stamp in
   `build/deps`; delete it to rebuild one (SDL after changing `patches/sdl`).
4. `tools/build-ps5.sh`: the import screen's headers (`build/gen`: the page from
   `tools/embed-file.py`, the fonts from `tools/make-font.py`), then OpenRCT2 with
   `DISABLE_OPENGL`, `DISABLE_NETWORK`, `DISABLE_HTTP`, `DISABLE_TTF`, scripting on, into
   `build/ps5/openrct2` (the unsigned module) and `build/ps5/openrct2.pie` (the linked ELF, for
   symbols).
5. `tools/package.sh`: `dist/PPSA99728`: the signed `eboot.bin`, `sce_module/libc.prx`,
   `sce_sys`, `data/` and the licences.

### The toolchain

`tools/ps5-cc` and `tools/ps5-c++` are clang 18 with the payload SDK's target flags
(`x86_64-sie-ps5`, `-fPIC -fno-plt -femulated-tls`). Asked to link, they call `tools/ps5-link`,
which links with `prospero-lld` and the boilerplate's PIE layout (`src/runtime/ps5.ld` adds the
`__eh_frame` bounds libunwind needs), the CRT, `libps5runtime.a`, libc++, libc++abi, libunwind,
compiler-rt's builtins and the system stubs, then converts the result with `ps5-native-tool link`.
Because every link is real, CMake and autotools configure checks see exactly what the console
offers, instead of guessing from compile-only checks.

- `--no-dependent-libraries`: libc++ asks for `-lpthread`; pthreads come from libkernel.
- `--wrap`: the malloc family into `src/runtime/heap.c`, `pthread_create` into `thread.c`, `exit`
  into `libc.c`, and the stdio output calls (`printf`, `puts`, `fprintf`, `fwrite`...) into
  `log.c`.
- The stubs leave out the modules a game process does not load: `libScePosixForWebKit`,
  `libkernel_sys`, `libkernel_web` (and `libmonosgen`). Anything bound to them would be null at
  run time, so such an import now fails to link instead (oot64-ps5 found this for WebKit; our first
  `popen` crash was the same thing). `src/runtime/stubs/<module>.txt` lists functions of modules the
  SDK has no stubs for (`libSceCommonDialog`).
- `ps5-native-tool` rejects weak symbols nothing defines: zstd is built with `ZSTD_TRACE=0`, and the
  runtime defines `__cxa_thread_atexit_impl`.

### Choices made in the spike

- **SDL2:** ps5-payload-dev's fork (`ref/sdk/SDL`, pinned at `ee4c47dc`), built from source. Its
  video driver gives a VideoOut framebuffer, which is what the software renderer needs: OpenRCT2
  0.5.5 has no plain software engine any more, only `softwareWithHardwareDisplay`, which goes
  through SDL_Renderer, and SDL's software renderer draws into that framebuffer. The fork also has
  AudioOut, ScePad and the IME dialog (and USB keyboard and TV remote input, which the port
  removes). ps5-opengl's SDL2 bridge (used by
  oot64-ps5 and morrowind-ps5) offers only an OpenGL window: no window surface, no SDL_Renderer,
  no audio, no mouse; it is the route for OpenRCT2's OpenGL engine (GL 3.3 core) later.
- **ICU:** built (78.3), not patched out. OpenRCT2 uses it for UTF conversions, upper-casing, five
  code pages and Arabic/Hebrew shaping. A host build first (ICU builds its data with its own
  tools), then the cross build with `U_STATIC_IMPLEMENTATION` (the PS5 target supports
  `__declspec`, so ICU's headers would otherwise export and import), `U_CHARSET_IS_UTF8=1`,
  `U_SIZEOF_WCHAR_T=2` and no `nl_langinfo`. `tools/icu-filter.json` keeps only the five
  converters OpenRCT2 reads, so the data library is 253 KB. OpenRCT2 is compiled with the same
  `U_STATIC_IMPLEMENTATION` and `U_SIZEOF_WCHAR_T=2`.
- **nlohmann/json 3.11.3:** 3.12 does not compile with OpenRCT2's `-fno-char8_t`.

## The runtime (`src/runtime`)

- `heap.c`: malloc, calloc, realloc, free, posix_memalign, memalign, aligned_alloc and
  malloc_usable_size from an mspace (`sceLibcMspaceCreate`) in 2 GiB of direct memory (1 GiB, then
  512 MiB if that fails). A native title's libc heap is small (DOOM and oot64-ps5 found the same).
  Blocks allocated before or beyond it come from the libc heap; free routes by address.
- `dirent.c`: `opendir`, `readdir`, `scandir` and the rest on `sceKernelOpen(O_DIRECTORY)` and
  `sceKernelGetdents` with a 64 KiB buffer, since the sandbox refuses the libc's `opendir` (DOOM's
  finding). Every inode reads as 0 on `/app0` (nullfs), so only nameless records are skipped.
- `path.c`: `realpath` resolved component by component on `stat` (the libc's fails with EPERM,
  which broke libc++'s `canonical` and `relative` and with them OpenRCT2's case-insensitive file
  lookups; `lstat` is refused too), and `openat`, `unlinkat`, `readlink`, `pathconf` (only in
  `libkernel_sys`), with folder descriptors mapped back to paths for libc++'s `remove_all`.
- `libc.c`: what the console's libc lacks: the `*_l` locale functions (C locale only),
  `localtime_r`/`gmtime_r`, `mbsnrtowcs`/`wcsnrtombs`, `fnmatch`, message catalogue stubs,
  `getpwuid` (no user database), `arc4random`, `mkstemp`, `strcasestr`, `isatty`, `fork` (fails),
  `__assert`, `__cxa_thread_atexit_impl`, and `exit`: a title ends with
  `sceSystemServiceLoadExec("exit")`, since the exit system call gets it killed with SIGSYS. The
  C11 Annex K functions (`memcpy_s` and so on) exist in the libc but not in the SDK headers, so
  libzip is told it has none.
- `log.c`: text for stdout and stderr goes to the log file and to the kernel log, one line per
  `sceKernelDebugOutText`. The console's `printf` writes to the libc's own stdout, which nothing
  shows, and pointing `stdout` at a pipe does not catch it.
- `thread.c`: every thread gets at least 2 MiB of stack; the console's default is 64 KiB.

## Changes to SDL (`patches/sdl`)

1. No dynamic API (linked statically).
2. No unconditional `-lsamplerate` on the PS5.
3. `PS5_FrameCallback`: the application sees each finished frame (console tests capture there).
4. The on-screen keyboard is the only keyboard: the fork's USB keyboard and TV remote (HDMI-CEC
   buttons reported as keys) input is gone, since the controller is the only input. The driver
   loads the IME module itself (a native title gets it only when it loads it; the first boot
   crashed calling the keyboard library before it was loaded), initialises the common dialog
   library before the IME, and decodes the IME's UTF-16 result (`wcstombs` only managed ASCII).
   `PS5_SetScreenKeyboardText`: the next IME dialog starts with the application's text and its
   result replaces it (End, then Backspace per character, then the text and Return).
5. A pointer the application moves (`SDL_WarpMouseInWindow`), with a cursor drawn in software into
   each frame and taken out again (the application's own cursors, or a built-in arrow or hand at
   TV size). It appears once the pointer has moved, so a game that does not use one shows none.
   Mice are not read. The window gets mouse and keyboard focus.
6. The DualSense touchpad through SDL's game controller touchpad API, and `PS5_PadCallback`, which
   sees each controller state before SDL does (console tests inject input there).

## Changes to OpenRCT2 (`patches/openrct2`)

1. The PS5 title supplies `main` and calls `NormalisedMain`.
2. The screen texture is 32-bit, the window's format when offered. With SDL's software renderer
   OpenRCT2 took RGB565, which `CopyBitsToTexture` cannot fill: the screen stayed black.
3. `/app0/eboot.bin` is the executable path (the sandbox refuses the sysctl).
4. The 16-bit `wchar_t` branch of `toWideChar` compiles and reads the right length.
5. and 6. A first pointer for the controller and joystick shortcut bindings; 10 replaces both.
7. Text input hands the IME the text being edited.
8. A PS5 platform UI context: no zenity or kdialog (the Linux context ran them through `popen`,
   which crashed saving a new game), OpenRCT2's own file browser, messages as notifications.
9. A quick menu at the pointer; 11 replaces it.
10. `input/Gamepad.cpp`: the controller layer (Controls, below), replacing `GamepadPointer` and the
    joystick bindings. It moves the real pointer and sends ordinary mouse button events, so every
    window works as with a mouse, and adds what a controller needs on top. It needs a few hooks:
    `WindowBase::onScrollGetItems` (the rows or cells of a list, overridden where they are not
    rows of `kScrollableRowHeight`: new rides, scenery, scenarios without their headings, news,
    guests), `DropdownItemRect`, `WindowFootpathInBridgeMode`, and `RotateConstructionObject` (the
    rotate shortcut, which says whether there was anything to rotate). The mouse's edge scrolling
    is off on the PS5: the pointer rests at the edge, and pushing the stick against it scrolls.
11. `windows/RadialMenu.cpp`: the build tools and the park windows in rings.
12. Button hints, drawn over the middle of the bottom toolbar by `Gamepad::drawHints` (from
    `UiContext::Draw`, marking their area dirty each frame as the FPS counter does), and
    `WindowSceneryRaise`/`WindowSceneryCanRaise`: the controller's stepped version of holding Shift
    while placing scenery.

## The port (`src/port`)

- `main.cpp`: opens the log (`/app0/openrct2.log`, readable from the PC even after a crash),
  starts the test harness, writes a first-run `config.ini` (full screen, interface at twice the
  size, no DPI guess, `uncap_fps` so a 60 Hz TV gets 60 frames rather than one per 40 Hz tick),
  selects SDL's software renderer (OpenRCT2 asks for an accelerated one), runs the import screen
  when the game files are missing, and runs OpenRCT2 with `/app0/data`, `/app0/rct2` and
  `/download0/user` (config, saves, caches; `downloadDataSize` 1024 MB).
- `import_screen.c`, `upload.c`, `upload.html`: importing the game files (below).
- `console_test.c`, `pad_script.c`: the test harness (below).

## Controls

The game is played with the DualSense alone. The rule is that buttons act on what is under the
pointer, and the middle of the bottom toolbar lists what they do right now (unless it shows news).

| Input | In the park | Over a window | Building track or a bridge path, pointer in the park |
| --- | --- | --- | --- |
| Left stick | The pointer: slow and precise near the centre, faster after a moment at full tilt, slower over buttons; pushed against the screen's edge it scrolls the park | | |
| Touchpad | The pointer, like a laptop's; a click is Cross | | |
| D-pad | Jump to the nearest button that way (up reaches the toolbar) | Button to button; in lists row by row or cell by cell, scrolling them; along sliders; through dropdowns | Left/right turn, up/down slope |
| Cross | Click, hold to drag | Click | Build the piece (held, keeps building) |
| Square | Right click (removes scenery and paths) | Right click | Take the last piece away |
| Circle | Leave the tool and close its window | Close the dropdown, or the window | Stop building |
| Triangle | Rotate what is being placed; otherwise the park windows | | Chain lift |
| Options | The build tools | | |
| L1 / R1 | Rotate the view | Previous/next tab | Bank left/right |
| L2 / R2 | Zoom out/in, towards the pointer | | |
| Right stick | Scroll the park | Scroll the list under the pointer | |
| R3 / L3 | Rotate the view / pause | | |

Options and Triangle show radial menus in the middle of the screen: the build tools (ride, paths,
scenery, land, water, clear scenery) and the park windows (rides, park, guests, staff, finances,
research, messages, map). Hold the button, point with the stick, let go; or tap it, then point and
press Cross. While placing scenery that can be raised, up and down on the d-pad raise and lower it.
A dropdown opened with Cross gets the pointer on its current item once Cross is let go. Turning or
sloping track from a special piece (a station platform) starts from plain track. Text boxes open the
PS5 on-screen keyboard with the current text. The controller ticks lightly as the pointer jumps and
thumps when a piece is built.

Settings: the game's Controls options, *Gamepad deadzone* and *Gamepad sensitivity*, apply to the
pointer too (the sensitivity scales its speed: 1.5 is the default).

## Importing the game files

When `/app0/rct2/Data/g1.dat` is missing, the title shows a screen with an address
(`http://<console>:9666/`, or the next free port) and its QR code. The page there takes the
RollerCoaster Tycoon 2 folder (chosen or dropped; only files below Data, ObjData, Scenarios,
Tracks and Landscapes are sent) or a ZIP file of it (for phones, which cannot send folders), and
ends with `POST /done`. The server stores files below those folders only, each path part a plain
name, as `<path>.upload` renamed when complete; the screen unpacks ZIP files with libzip under the
same rules. The game starts once `Data/g1.dat` is there and the sender has finished. Circle closes
the title. The screen draws with SDL and fonts rendered from DejaVu Sans at build time
(`tools/make-font.py`), since OpenRCT2 cannot draw before the game's own files exist.

`uv run --no-project python tools/send.py http://192.168.0.90:9666/ E:/scratch/rct2` sends a folder
the way the page does (`--only` sends some files, a `.zip` path sends that file).

## On the console

`uv run --no-project python tools/deploy.py` uploads `dist/PPSA99728` to `/data/homebrew/PPSA99728`,
where ShadowMountPlus registers it; `--eboot` uploads only the executable (what most iterations
need), `--rct2 E:/scratch/rct2` also uploads the game folders as `rct2/`. `tools/deploy.py cat
PATH` prints a console file, e.g. `/data/homebrew/PPSA99728/openrct2.log`.

`uv run --no-project --with pillow python tools/console_test.py test/console-boot.cfg` writes the
plan as `test.cfg` into the title folder, launches the title with the `rct2launch` payload
(`tools/launcher`, installed into `/data/pldmgr/payloads/rct2launch/`), pulls captured frames over
TCP port 9119 into `build/test/run/` as PNG, then the log when the plan's frame limit ends the
game, and deletes `test.cfg`. Frames come from SDL's window surface before it is tiled for scanout,
so they need no de-tiling. Plan lines:

| Line | Effect |
| --- | --- |
| `reset` | delete `/download0/user` first, for a first-run state |
| `capture N,N,...` | frames to capture, counted from the first one shown |
| `frames N` | exit after N frames |
| `input SCRIPT` | controller input: `START:INPUTS:COUNT` steps separated by `;`, inputs joined by `+` (button names, `LX=`/`LY=`/`RX=`/`RY=` tilts from -127 to 127, `TX=`/`TY=` a touchpad finger) |
| `point N X Y` | put the pointer at window pixel X, Y at frame N |
| `game PATH`, `remove PATH` | use another game folder (an empty one shows the import screen); delete a test folder |
| `list PATH`, `stat PATH`, `realpath PATH` | log what the sandbox says |
| `arg ARGUMENT` | add to OpenRCT2's command line, e.g. `--verbose` |

The harness also logs the renderer in use, how much of each captured frame is lit, and the frame
rate every 300 frames. Plans: `console-boot`, `console-newgame`, `console-ingame` (rotate, zoom,
scrolling), `console-save`, `console-filemenu`, `console-perf`, `console-import`, `console-list`
(probes), and for the controller `console-stick`, `console-dpad` (title buttons, Load Game, Circle),
`console-dpad-game` (toolbar, tabs, closing), `console-radial`, `console-ime` (a text box asks for
the on-screen keyboard), `console-track` (a miniature railway from the radial menu to built
pieces), `console-path` (a bridge path) and `console-scenery`. Most
start the first scenario by pointing at its button (`point`), and assume the title screen is up by
frame 700, which holds when the object index exists (the first start after `reset` takes longer).
`reset` deletes the player's own saves and settings on the console: use it only for first-run tests.

Crashes: start klogsrv (`GET :8084/repository_install?filename=klogsrv_v0.9.elf`, then
`GET :8084/loadpayload:klogsrv_v0.9.elf`; not autoloaded, gone after a reboot) and record it with
`uv run --no-project python tools/klog.py build/klog.txt`. A crash report there lists the frames;
`tools/symbolize.sh ADDRESS...` (in the build image) names them from `build/ps5/openrct2.pie`. The
report's list of loaded modules shows which imports can be null.

## PS5 facts learned on hardware (FW 13.00)

- Modules a game process loads on its own: those of the title's imports that are standard game
  libraries. libSceKeyboard, libSceImeDialog and libSceMouse must be loaded with
  `sceSysmoduleLoadModule` (0x0106, 0x0096, 0x00a9); `libkernel_sys` and the WebKit POSIX module are
  never there.
- The sandbox refuses `opendir`, `realpath`, `lstat` and `sysctl(KERN_PROC_PATHNAME)`; `stat`,
  `open`, `mkdir`, `rename`, `unlink` and `sceKernelGetdents` work. `/app0` (nullfs) reports every
  inode as 0.
- `wchar_t` has 16 bits; the default thread stack is 64 KiB; the exit system call ends in SIGSYS.
- Uploading to the title over HTTP: about 10 MB/s for large files (ps5upload does about 11) and
  about 30 ms per small file, once the server sets `TCP_NODELAY`. Without it each reply's body
  waited for Windows' delayed acknowledgement of the head (up to 200 ms), about 105 ms per file,
  and the whole folder took 7 minutes instead of 2.5 (that run also shared the network with
  another upload).
- Rest mode (the TV turned off) stops a running test; the payloads' listening sockets die with it.

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
  nlohmann/json 3.11.3, ICU 78.3, qrcodegen 1.8.0, DejaVu Sans (rendered into the import screen's
  fonts)
- The upload server and page are the DOOM port's, reworked
