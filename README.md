# Miku Flick/02 — native Linux and Android port

A clean-room C11/SDL2 reimplementation of *Miku Flick/02* (SEGA, iOS v1.1.5) for Linux and Android (see [`android/README.md`](android/README.md)). The rhythm-game rules — note timing, judgement windows, scoring, combo bonus, tension gauge and ranks — are reproduced from the reverse-engineered original, as documented in [`docs/`](docs/).

> **This repository ships no game data.** It contains no SEGA/Crypton assets, music, video, artwork or charts. You need your own legally obtained, decrypted copy of `com.sega.MikuFlick02-iOS5.0-(Clutch-2.0.4).ipa` (v1.1.5, armv7). The port extracts the IPA locally and reads its tables, movies, audio and textures at runtime.

## Features

- Boot logos, title screen and music select with cover flow, artwork, star ratings and song previews.
- Full rhythm gameplay: flick notes, interludes, press/release timing judgement, combo and climax bonuses, tension gauge with vocal fade, game over and the pause menu (RESUME / RETRY / MUSIC SELECT).
- OPTIONS screen (display guides, input timing, volumes, fail sound, handedness, keyboard style, help, credits; saved) and the PV viewer (music videos with lyrics, KARAOKE, LOOP, SHUFFLE).
- Streaming MPEG-1 video and two ADX audio tracks (vocals + backing) decoded directly from the original `.usm` files by built-in decoders (no FFmpeg needed).
- Result screen with ranks and a persistent local save file.
- Mouse and multi-touch flick input, a keyboard mapping, input-timing calibration, fullscreen and volume controls.
- Any screen shape: the original 2:3 layout stays centred (keys exactly where they were on iOS), and the bars on tall phones or wide monitors show a blurred copy of the video or menu.
- Android app (APK) built from the same C sources: touch, Back button, pause when the app is left. See [`android/README.md`](android/README.md).
- Headless modes for automated verification (`--autoplay`, `--autoplay-input`, screenshot and script modes).

All 11 bundled songs and all five difficulties (Easy, Normal, Hard, Extreme, Break the Limit) are unlocked.

## Requirements

Debian/Ubuntu package names:

```sh
sudo apt install build-essential pkg-config \
  libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev
```

`unzip` is also required for the one-time setup step. The Android build has its own requirements (JDK 17+, about 4 GB of disk for the SDK/NDK it downloads into the project); see [`android/README.md`](android/README.md).

## Build and set up

```sh
# 1. Build the port
make -C port

# 2. One-time extraction: unpacks your IPA and exports the game tables
make -C port setup IPA="/path/to/com.sega.MikuFlick02-iOS5.0-(Clutch-2.0.4).ipa"

# 3. Run from the repository root
./port/mikuflick
```

`setup` unpacks the IPA into `extracted/ipa/` (about 1 GB, path-safety checked) and writes `port/data/gamedata.txt` from your `MikuFlick2` binary. `mfextract` verifies the binary UUID and refuses any other build.

If you already have an extracted app bundle, you can skip the IPA argument:

```sh
make -C port setup          # uses extracted/ipa/Payload/MikuFlick2.app
```

### Runtime options

```text
--app DIR            extracted MikuFlick2.app directory
--data DIR           save directory (default ~/.local/share/mikuflick-port)
--song N --diff D    jump straight into a song (diff 0 Easy … 3 Extreme, 4 Break the Limit)
--offset N           input-timing calibration (same units as the original option)
--volume F           music volume
--se-volume F        sound-effect volume
--no-fail-sound      disable the "sad" sound on a miss
--font PATH          menu font (none = built-in bitmap font)
--fullscreen         start fullscreen (F11 toggles)
--window WxH         window size
```

## Controls

| Action | Mouse / touch | Keyboard |
|---|---|---|
| Flick key あかさ / たなは / まやら / わ | press, drag in the flick direction, release | `Q W E` / `A S D` / `Z X C` / `Space`, or numpad `7 8 9` / `4 5 6` / `1 2 3` / `0` |
| Flick direction | drag ≥ 16 pt (32 px at 640×960) | hold an arrow key while releasing the letter key |
| Interlude (♪ panel) | tap the panel | any board key |
| Pause menu | pause button (bottom right) | `Esc` / `P`, then `Up`/`Down` + `Enter`: RESUME, RETRY or MUSIC SELECT |

Notes are judged **when you release**, as in the original. Menus are navigated with the mouse or `Up`/`Down`/`Left`/`Right` + `Enter`; `Esc` goes back. On a touchscreen, a tap works wherever a click does, and the flick board takes several fingers at once.

See [`port/README.md`](port/README.md) for the full control and UI description.

## Android

The same sources build into an Android app (arm64-v8a, armeabi-v7a, x86_64; Android 7.0+). The `android/Makefile` downloads the SDK, NDK, Gradle and SDL sources into the project (gitignored), and the game data from your IPA is copied to the phone with `adb`:

```sh
make -C android toolchain        # downloads into android/third_party/
make -C android licenses         # review and accept the Android SDK licences
make -C android sdk apk          # NDK, CMake, platform 35 → app-debug.apk
make -C android install push run # install, copy MikuFlick2.app + gamedata.txt, start
```

Details, controls and debugging: [`android/README.md`](android/README.md).

## Repository layout

| Path | Contents |
|---|---|
| `port/` | The game: C sources shared by Linux and Android, Linux `Makefile`, bundled M+ font and the `pl_mpeg` (MIT) video decoder, detailed README. |
| `android/` | Android build: Gradle/CMake project, `Makefile` that fetches the toolchain into `android/third_party/` (ignored), README. |
| `docs/` | Reverse-engineering notes, the port design plan and the original IPA archive inventory. |
| `tools/` | Self-contained C utilities and scripts used during analysis (`machoinfo`, `usmdump`, `bplist2json`, `mfextract`, Ghidra helpers, IPA extractor). |
| `CLAUDE.md` | Agent operational rules and the current project status. |
| `extracted/` | *Ignored.* Your extracted game bundle, created by `make setup`. |
| `work/` | *Ignored.* Reverse-engineering scratch space: chart dumps, decompiled functions and Ghidra projects. |
| `build/`, `port/build/` | *Ignored.* Compiled objects and helper binaries. |

## Documentation

- [`port/README.md`](port/README.md) — detailed build, controls, features and testing aids.
- [`android/README.md`](android/README.md) — Android toolchain, APK build, installing and adding your game data, debugging.
- [`docs/linux_port_plan.md`](docs/linux_port_plan.md) — platform mapping, module layout, timing/sync design, deliberate deviations, verification, and the Android/decoder notes (§9).
- [`docs/re_notes.md`](docs/re_notes.md) — reverse-engineering notes: USM format, chart format, atlases, judgement/scoring formulas, scene state machine and rendering.
- [`docs/archive_layout.md`](docs/archive_layout.md) — inventory of the original IPA and its binary.

## Not included

Shop/DLC, Game Center, Twitter, ads, replays, the collection and PV playlist editing. The DLC servers are long gone; only the songs bundled in the IPA are playable.

## Testing aids

```sh
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./port/mikuflick --autoplay --exit-after-song
```

Additional flags: `--autoplay-input`, `--shot-dir DIR --shot-times 8,30`, `--shot-ui-times 0.5,3`, `--start-scene title|select|result|options|pv`, `--script "9:enter,12:right"`. See [`port/README.md`](port/README.md).

## Reverse-engineering tools

The utilities in `tools/` are optional and are only needed if you want to redo the analysis. Build them with a C compiler, e.g.:

```sh
cc -O2 -Wall -o build/machoinfo tools/machoinfo/machoinfo.c -lm
cc -O2 -Wall -o build/usmdump   tools/usmdump/usmdump.c
cc -O2 -Wall -o build/bplist2json tools/bplist2json/bplist2json.c
cc -O2 -Wall -o build/mfextract  tools/mfextract/mfextract.c
```

Ghidra is **not** vendored. To use `tools/ghidra_decompile.sh`, download Ghidra 11.2.1 and either place it at `tools/third_party/ghidra_11.2.1_PUBLIC` or set `GHIDRA_DIR`.

## Credits and licence

- The port source and documentation are released under the MIT License — see [`LICENSE`](LICENSE).
- MPEG-1 video decoding: [pl_mpeg](https://github.com/phoboslab/pl_mpeg) by Dominic Szablewski, MIT License (`port/third_party/pl_mpeg.h`, with a small patch for the 11-bit intra DC precision of the game's videos, marked `MIKUFLICK PATCH`).
- The Android build uses [SDL2, SDL2_image and SDL2_ttf](https://github.com/libsdl-org) (zlib License) and FreeType (FreeType License), downloaded at build time and not stored in this repository.
- Menu font: **M+ 1p Bold**, © The M+ Project Authors, licensed under the SIL Open Font License 1.1 (`port/fonts/OFL.txt`).
- *Miku Flick/02*, Hatsune Miku and all related artwork, music and game data are © SEGA / Crypton Future Media. No such material is included in this repository.

This is an unofficial fan preservation project and is not affiliated with SEGA or Crypton Future Media. Use it only with game data you legally own.
