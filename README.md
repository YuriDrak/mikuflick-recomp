# Miku Flick/02 — native Linux, Android and iOS port

A clean-room C11/SDL2 reimplementation of *Miku Flick/02* (SEGA, iOS v1.1.5) for Linux, Android (see [`android/README.md`](android/README.md)) and iOS (see [`ios/README.md`](ios/README.md)). The rhythm-game rules — note timing, judgement windows, scoring, combo bonus, tension gauge and ranks — are reproduced from the reverse-engineered original, as documented in [`docs/`](docs/).

> **This repository ships no game data.** It contains no SEGA/Crypton assets, music, video, artwork or charts. You need your own legally obtained, decrypted copy of `com.sega.MikuFlick02-iOS5.0-(Clutch-2.0.4).ipa` [ios3-6-ipas](https://archive.org/download/ios3-6-ipas) (v1.1.5, armv7). The port extracts the IPA locally and reads its tables, movies, audio and textures at runtime.

## Features

- Boot logos, title screen and music select with cover flow, artwork, star ratings and song previews.
- Full rhythm gameplay: flick notes, interludes, press/release timing judgement, combo and climax bonuses, tension gauge with vocal fade, game over and the pause menu (RESUME / RETRY / MUSIC SELECT, plus the original's per-song INPUT TIMING bar).
- OPTIONS screen (display guides, volumes, fail sound, handedness, keyboard style, the original help pages and licence text in English or Japanese, credits with the ENDING credits roll; saved).
- PV viewer (music videos with lyrics, KARAOKE, LOOP, SHUFFLE; swipe to the next/previous video; its own pause menu).
- Streaming MPEG-1 video and two ADX audio tracks (vocals + backing) decoded directly from the original `.usm` files by built-in decoders (no FFmpeg needed).
- Result screen with ranks and the per-key FLICK RESULT page; RESULTS / RECORDS (from the title's crown) with your best score per song and difficulty; a persistent local save file.
- Mouse and multi-touch flick input, keyboard equivalents for every screen, input-timing calibration, fullscreen and volume controls.
- Any screen shape: the original 2:3 layout stays centred (keys exactly where they were on iOS), and the bars on wide monitors show a blurred copy of the video or menu (on phones: black during a song, a dim blur of the menu elsewhere).
- Android app (APK) built from the same C sources: touch, Back button, pause when the app is left. See [`android/README.md`](android/README.md).
- iOS app built from the same C sources: native Files picker for IPA import, scene-based lifecycle, automatic signing. See [`ios/README.md`](ios/README.md).
- Headless modes for automated verification (`--autoplay`, `--autoplay-input`, screenshot and script modes).

All 11 bundled songs and all five difficulties (Easy, Normal, Hard, Extreme, Break the Limit) are unlocked.

## Requirements

Debian/Ubuntu package names:

```sh
sudo apt install build-essential pkg-config \
  libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev
```

`unzip` is used by the one-time `setup` step; the built-in importer (`--import-ipa`, or **Import IPA** in the Android app) does not need it. The Android build has its own requirements (JDK 17+, about 4 GB of disk for the SDK/NDK it downloads into the project); see [`android/README.md`](android/README.md).

## Build and set up

```sh
# 1. Build the port
make -C port

# 2. One-time extraction: unpacks your IPA and exports the game tables
make -C port setup IPA="/path/to/com.sega.MikuFlick02-iOS5.0-(Clutch-2.0.4).ipa"

# 3. Run from the repository root
./port/mikuflick
```

`setup` unpacks the IPA into `extracted/ipa/` (about 1 GB, path-safety checked) and writes `port/data/gamedata.txt` from your `MikuFlick2` binary. `mfextract` verifies the binary UUID and refuses any other build. Alternatively, `./port/mikuflick --import-ipa your.ipa --import-to DIR` runs the same importer the Android app uses (no `unzip` needed) and writes `DIR/MikuFlick2.app` and `DIR/gamedata.txt`; start the game with `--app DIR/MikuFlick2.app --gamedata DIR/gamedata.txt`.

If you already have an extracted app bundle, you can skip the IPA argument:

```sh
make -C port setup          # uses extracted/ipa/Payload/MikuFlick2.app
```

**Updating from an earlier version:** run `make -C port setup` (or re-import the IPA) once more. `gamedata.txt` now also holds the ending credits read from your binary; with an older file the game still runs, but the ENDING button on OPTIONS → CREDITS stays hidden.

### Runtime options

```text
--app DIR            extracted MikuFlick2.app directory
--gamedata FILE      tables exported by mfextract (default port/data/gamedata.txt)
--data DIR           save directory (default ~/.local/share/mikuflick-port)
--song N --diff D    jump straight into a song (diff 0 Easy … 3 Extreme, 4 Break the Limit)
--offset N           input-timing steps -10..10 for this session (overrides the per-song pause-menu value)
--volume F           music volume
--se-volume F        sound-effect volume
--no-fail-sound      disable the "sad" sound on a miss
--font PATH          menu font (none = built-in bitmap font)
--fullscreen         start fullscreen (F11 toggles)
--window WxH         window size
--import-ipa FILE    import your own IPA into --import-to DIR (default imported/) and exit
--import-to DIR      destination for --import-ipa
```

`--import-ipa` writes `DIR/MikuFlick2.app/` and `DIR/gamedata.txt`; start the game with `--app DIR/MikuFlick2.app --gamedata DIR/gamedata.txt`.

Environment variables:

```text
MIKUFLICK_AUDIO_BUFFER=N   audio buffer in frames (power of two, 256..8192; default 1024):
                           raise it if the sound crackles under system load
MIKUFLICK_LANG=ja|en       language of the help pages and option subtitles
                           (default: the system language, as in the original)
```

## Controls

| Action | Mouse / touch | Keyboard |
|---|---|---|
| Flick key あかさ / たなは / まやら / わ | press, drag in the flick direction, release | `Q W E` / `A S D` / `Z X C` / `Space`, or numpad `7 8 9` / `4 5 6` / `1 2 3` / `0` |
| Flick direction | drag ≥ 16 pt (32 px at 640×960) | hold an arrow key while releasing the letter key |
| Interlude (♪ panel) | tap the panel | any board key |
| Pause menu | pause button (bottom right) | `Esc` / `P`, then `Up`/`Down` + `Enter`: RESUME, RETRY or MUSIC SELECT |
| Input timing (pause menu, per song) | drag the INPUT TIMING bar | `Left` / `Right` while paused |
| PV playback | swipe left / right: next / previous PV (or restart); pause button | `Right` / `Left`; `Esc` pauses |

Notes are judged **when you release**, as in the original. Menus are navigated with the mouse or `Up`/`Down`/`Left`/`Right` + `Enter`; `Esc` goes back. On a touchscreen, a tap works wherever a click does, and the flick board takes several fingers at once.

See [`port/README.md`](port/README.md) for the full control and UI description.

## Android

The same sources build into an Android app (arm64-v8a, armeabi-v7a, x86_64; Android 7.0+). The `android/Makefile` downloads the SDK, NDK, Gradle and SDL sources into the project (gitignored). On the phone, the app imports your own IPA itself: copy the `.ipa` to the phone, tap **Import IPA** on the first start and pick it. No PC is needed for that step.

```sh
make -C android toolchain        # downloads into android/third_party/
make -C android licenses         # review and accept the Android SDK licences
make -C android sdk apk          # NDK, CMake, platform 35 → app-debug.apk
make -C android install run      # install and start, then import your IPA in the app
```

Details, controls and debugging: [`android/README.md`](android/README.md).

## iOS

The same sources build into an iOS app (arm64, iOS 26.0+). The `ios/Makefile` wraps CMake and fetches SDL dependencies. On first launch, the app presents a native Files picker to import your own IPA.

```sh
make -C ios sim                  # Simulator build (arm64)
make -C ios device               # Device build (unsigned, arm64)
```

For a signed installation on a physical device:

```sh
make -C ios device SIGNING=YES TEAM=ABCDE12345 BUNDLE_ID=com.example.yourname.mikuflick
open ios/build/iphoneos/MikuFlick.xcodeproj
```

Then select your device in Xcode and run. Details: [`ios/README.md`](ios/README.md).

## Repository layout

| Path | Contents |
|---|---|
| `port/` | The game: C sources shared by Linux, Android and iOS, Linux `Makefile`, bundled M+ font and the `pl_mpeg` (MIT) video decoder, detailed README. |
| `android/` | Android build: Gradle/CMake project, `Makefile` that fetches the toolchain into `android/third_party/` (ignored), README. |
| `ios/` | iOS build: CMake/Makefile project, Objective-C bridge, native Files picker for IPA import, README. |
| `docs/` | Reverse-engineering notes, the port design plan and the original IPA archive inventory. |
| `tools/` | Self-contained C utilities and scripts used during analysis (`machoinfo`, `usmdump`, `bplist2json`, `mfextract`, Ghidra helpers, IPA extractor). `mfextract` shares the table reader in `port/src/mfx.c`. |
| `CLAUDE.md` | Agent operational rules and the current project status. |
| `extracted/` | *Ignored.* Your extracted game bundle, created by `make setup`. |
| `work/` | *Ignored.* Reverse-engineering scratch space: chart dumps, decompiled functions and Ghidra projects. |
| `build/`, `port/build/` | *Ignored.* Compiled objects and helper binaries. |

## Documentation

- [`port/README.md`](port/README.md) — detailed build, controls, features and testing aids.
- [`android/README.md`](android/README.md) — Android toolchain, APK build, installing and adding your game data, debugging.
- [`ios/README.md`](ios/README.md) — iOS build, CMake/Makefile, native IPA import, signing, status.
- [`docs/linux_port_plan.md`](docs/linux_port_plan.md) — platform mapping, module layout, timing/sync design, deliberate deviations, verification, and the Android/decoder notes (§9).
- [`docs/re_notes.md`](docs/re_notes.md) — reverse-engineering notes: USM format, chart format, atlases, judgement/scoring formulas, scene state machine and rendering.
- [`docs/archive_layout.md`](docs/archive_layout.md) — inventory of the original IPA and its binary.

## Not included

Shop/DLC, Game Center, Twitter, ads, replays, the collection and PV playlist editing. The DLC servers are long gone; only the songs bundled in the IPA are playable.

## Testing aids

```sh
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./port/mikuflick --autoplay --exit-after-song
```

Additional flags: `--autoplay-input`, `--shot-dir DIR --shot-times 8,30`, `--shot-ui-times 0.5,3`, `--start-scene title|select|result|options|pv|credits|ending|movie|scores|records`, `--script "9:enter,12:right"`. See [`port/README.md`](port/README.md).

## Reverse-engineering tools

The utilities in `tools/` are optional and are only needed if you want to redo the analysis. Build them with a C compiler, e.g.:

```sh
cc -O2 -Wall -o build/machoinfo tools/machoinfo/machoinfo.c -lm
cc -O2 -Wall -o build/usmdump   tools/usmdump/usmdump.c
cc -O2 -Wall -o build/bplist2json tools/bplist2json/bplist2json.c
cc -O2 -Wall -Iport/src -o build/mfextract tools/mfextract/mfextract.c port/src/mfx.c
```

Ghidra is **not** vendored. To use `tools/ghidra_decompile.sh`, download Ghidra 11.2.1 and either place it at `tools/third_party/ghidra_11.2.1_PUBLIC` or set `GHIDRA_DIR`.

## Credits and licence

- The port source and documentation are released under the MIT License — see [`LICENSE`](LICENSE).
- MPEG-1 video decoding: [pl_mpeg](https://github.com/phoboslab/pl_mpeg) by Dominic Szablewski, MIT License (`port/third_party/pl_mpeg.h`, with a small patch for the 11-bit intra DC precision of the game's videos, marked `MIKUFLICK PATCH`).
- The Android build uses [SDL2, SDL2_image and SDL2_ttf](https://github.com/libsdl-org) (zlib License) and FreeType (FreeType License), downloaded at build time and not stored in this repository.
- Menu font: **M+ 1p Bold**, © The M+ Project Authors, licensed under the SIL Open Font License 1.1 (`port/fonts/OFL.txt`).
- *Miku Flick/02*, Hatsune Miku and all related artwork, music and game data are © SEGA / Crypton Future Media. No such material is included in this repository.

This is an unofficial fan preservation project and is not affiliated with SEGA or Crypton Future Media. Use it only with game data you legally own.
