# Third-party notices

Everything in this project that came from someone else, what it is used for, and its license. The
release package carries this file and the license texts in `PPSA99728/licenses/`.

RollerCoaster Tycoon 2 itself is not part of this project or its package: the player sends the game
files of their own copy to the console.

## Shipped in the release

| Component | Used as | License |
| --- | --- | --- |
| [OpenRCT2](https://github.com/OpenRCT2/OpenRCT2) v0.5.5, the OpenRCT2 developers | The game, with the changes in `patches/openrct2` (listed in `DEVELOPMENT.md`); its data folder (languages, `g2.dat`, fonts, palettes, shaders, scenario patches) as `data/` | GNU GPL v3 (`licenses/OpenRCT2-GPL-3.0.txt`) |
| [OpenRCT2 objects](https://github.com/OpenRCT2/objects) v1.7.11 | `data/object` | CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/) |
| [OpenRCT2 title sequences](https://github.com/OpenRCT2/title-sequences) v0.4.26 | `data/sequence` | CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/) |
| [OpenMusic](https://github.com/OpenRCT2/OpenMusic) v1.6.1 | The music asset packs in `data/assetpack` | CC BY-SA 4.0 (https://creativecommons.org/licenses/by-sa/4.0/) |
| [OpenSoundEffects](https://github.com/OpenRCT2/OpenSoundEffects) v1.0.6 | The sound asset pack in `data/assetpack` | MIT (below) |
| [SDL](https://github.com/ps5-payload-dev/SDL) 2, ps5-payload-dev's PS5 fork at `ee4c47dc`, with the changes in `patches/sdl` | Video, audio, controller and on-screen keyboard | zlib (`licenses/SDL2.txt`) |
| [ICU](https://icu.unicode.org/) 78.3 | Text conversion and shaping | Unicode license (`licenses/ICU.txt`) |
| [zlib](https://zlib.net/) 1.3.2 | Compressed park files, PNG, ZIP and the GOG installer's data | zlib (`licenses/zlib.txt`) |
| [libpng](http://www.libpng.org/) 1.6.59 | Images | libpng license (`licenses/libpng.txt`) |
| [zstd](https://github.com/facebook/zstd) 1.5.7 | Park files | BSD (`licenses/zstd.txt`) |
| [libzip](https://libzip.org/) 1.11.4 | ZIP files of the game folder, OpenRCT2's own archives | BSD 3-Clause (`licenses/libzip.txt`) |
| [xz](https://tukaani.org/xz/) 5.8.1, liblzma | LZMA and LZMA2 decoding in the GOG installer reader | BSD Zero Clause (`licenses/liblzma.txt`) |
| [libogg](https://xiph.org/ogg/) 1.3.6, [libvorbis](https://xiph.org/vorbis/) 1.3.7, [FLAC](https://xiph.org/flac/) 1.5.0 | Music and sound | BSD (`licenses/libogg.txt`, `licenses/libvorbis.txt`, `licenses/FLAC.txt`) |
| [nlohmann/json](https://github.com/nlohmann/json) 3.11.3 | JSON | MIT (`licenses/nlohmann-json.txt`) |
| [QR Code generator](https://www.nayuki.io/page/qr-code-generator-library) 1.8.0, Project Nayuki | The QR code of the import screen's address | MIT (`licenses/qrcodegen.txt`) |
| [DejaVu fonts](https://dejavu-fonts.github.io/) | The import screen's text, rendered into bitmaps at build time | Bitstream Vera and public domain (`licenses/DejaVu-fonts.txt`) |
| `libc.prx` from [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate), BlackBearReloaded | Runtime module the PS5 loader requires next to a native title (`sce_module/libc.prx`), built from its source at commit `b1315a9` | GNU GPL v3 or later (`licenses/libc-prx-GPL-3.0.txt`) |

## Used to build

| Component | Used as | License |
| --- | --- | --- |
| [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate) `b1315a9` | `ps5-native-tool` (PIE to PS5 module conversion, FSELF signing), its PIE linker script and C runtime start file, fetched by `tools/fetch-native-tools.sh` | GNU GPL v3 or later |
| [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) v0.42, John Törnblom | Headers, system import stubs, `prospero-lld`, libc++, libc++abi and libunwind | GNU GPL v3, with the LLVM runtime libraries under Apache 2.0 with LLVM exception |
| [LLVM/Clang](https://llvm.org/) 18 | C and C++ compiler for the PS5 target | Apache 2.0 with LLVM exception |
| [innoextract](https://constexpr.org/innoextract/), Daniel Scharrer | Reference for the Inno Setup installer format and GOG Galaxy's file parts; `src/port/inno.c` follows its reading of the format but copies no code, and `tools/test-inno.sh` compares the two | zlib |

## OpenSoundEffects license

```
MIT License

Copyright (c) 2019 OpenRCT2

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
