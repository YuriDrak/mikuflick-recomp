# Agent Operational Rules & Constraints

You MUST strictly adhere to the following rules at all times during this project:

## 1. Strict Workspace Sandboxing
- **DO NOT leave the current working directory (`./`) under any circumstances.**
- You are strictly prohibited from reading, listing, navigating to, creating, modifying, or deleting any files or directories outside the project root folder (no `cd ..`, no absolute paths outside the project workspace, no modifying system directories or home folders).
- All file creations, extractions, build artifacts, temporary files, and modifications **must** occur exclusively inside the project directory.

## 2. Minimal Python Usage
- Keep the use of **Python to an absolute minimum**.
- Do **not** use Python for the core game implementation, main tooling, or build orchestration unless strictly unavoidable for a tiny, isolated one-off inspection task. Prefer native compiled languages (such as C, C++, Rust, or Go) and standard shell/native build tools (`make`, `cmake`, `cargo`, etc.).

## 3. Testing Policy
- **Do NOT run tests, build verification runs, or execution checks incrementally while writing code.**
- Complete the full implementation of the codebase first.
- All compilation checks, test executions, and runtime validations **must be performed only after the code writing phase is completely finished**.

---

# Project Status

_Last updated: 2026-11-24._

## Overview

*Miku Flick/02* (SEGA, iOS v1.1.5, armv7) native Linux and Android port: a clean-room **C11 + SDL2** reimplementation. It is not a binary translation; game rules were reconstructed from the reverse-engineered `MikuFlick2` binary and documented in `docs/re_notes.md`.

The repository ships **no SEGA/Crypton game data**. A user-supplied, decrypted IPA is required. `make -C port setup IPA=…` extracts it to `extracted/ipa/` and writes `port/data/gamedata.txt`; both are gitignored.

## Implemented

- **Port** (`port/src`, ~4600 lines, excluding vendored pl_mpeg): `main`, `game`, `ui`, `media`, `mixer`, `usm`, `atlas`, `bplist`, `text`, `save`, `gamedata`, `assets`, `util`, `view` (screen composition), `pl_mpeg_impl` + `mf.h`. No FFmpeg: own USM payload reader, ADX and IMA4 decoders, MPEG-1 via vendored `port/third_party/pl_mpeg.h` (MIT, patched for the 11-bit intra DC precision of the Sofdec/TMPGEnc streams; see `MIKUFLICK PATCH`).
- **Android** (`android/`): Gradle (AGP 8.7) + CMake build of the same sources with SDL2 2.30.10, SDL2_image 2.8.2, SDL2_ttf 2.22.0; `make -C android toolchain|sdk|apk|install|push|run|logcat`. SDK, NDK r27c, Gradle and caches live in gitignored `android/third_party/` (~3 GB). The project path contains a space: the stock `sdkmanager` script and `ndk-build` break on it, so the Makefile calls sdkmanager via `java` and the native build uses CMake. Game data is `adb push`ed to the app's external files dir (folders chmod 0775 so the app can enter them).
- **Screens/features**: boot logos, title, music select (cover flow, artwork, stars, previews), full rhythm game (video, two ADX tracks, flick notes, interludes, judgement, scoring, combo/climax bonuses, tension gauge with vocal fade, game over, pause menu with RESUME/RETRY/MUSIC SELECT), result screen with ranks, OPTIONS (display, audio, handedness, keyboard, help, credits; saved), PV viewer (lyrics, KARAOKE, LOOP, SHUFFLE), persistent save.
- **Screen**: `view.c` renders the 640×960 scene to a target shown centred; non-2:3 screens get a blurred video/menu backdrop in the bars (RGBA32 targets: SDL GLES2 swaps YUV colours on BGRA targets).
- **Input**: mouse/multi-touch flick input, first finger mapped to the pointer for menus/pause/PV, Android Back = Esc, auto-pause on background, inputs judged at their event timestamp, keyboard mapping, input-timing calibration (`--offset`), fullscreen, volume controls.
- **Testing aids**: `--autoplay`, `--autoplay-input`, `--exit-after-song`, `--shot-dir/--shot-times`, `--shot-ui-times`, `--start-scene`, `--script`, headless via dummy SDL drivers.
- **RE tooling** (`tools/`): own C tools `machoinfo`, `usmdump`, `bplist2json`, `mfextract`, plus Ghidra scripts and IPA extractor. Ghidra is **not vendored**; `tools/ghidra_decompile.sh` uses `GHIDRA_DIR` or `tools/third_party/`.
- **Docs**: `docs/archive_layout.md` (IPA inventory), `docs/re_notes.md` (formats, formulas, scene/render analysis), `docs/linux_port_plan.md` (design, timing, deviations, verification).

## Verified (see `docs/linux_port_plan.md` §7)

- `magnet` Extreme `--autoplay`: 172 notes, 77/77 interludes, all COOL, PERFECT, combo bonus 61,500.
- Same via `--autoplay-input` (synthetic keyboard/mouse): identical result.
- `magnet` Hard with no input: game over after 26 misses, rank E.
- 恋は戦争 Break the Limit with no input: rank D, no game over.
- `mfextract` output matches the tables read by hand during RE.
- After removing FFmpeg (2026-11-24): both `magnet` Extreme autoplay modes still give 115,616 PERFECT on Linux; pl_mpeg frames match FFmpeg (PSNR 43-46 dB).
- Android, real phone (A059P, arm64, Android 16): APK installs and runs; `magnet` Extreme `--autoplay` gives the identical 115,616 PERFECT result; video, menus via tap, Back key verified.
- Touch areas: keys are hit-tested on their drawn rectangles (table positions are top-left corners; they were wrongly treated as centres, so touches landed one half-key low). Verified on the phone: a touch on the lower half of か selects か.
- Played by hand on the phone by the user (2026-11-24) after the touch-area and screen-fill fixes.

**Not yet measured:** audio output latency across devices (PipeWire/Pulse; OpenSL ES on phones), long sessions, phones other than the A059P. Software-renderer timing is frame-rate sensitive.

## Deliberately not implemented

Shop/DLC, Twitter, Game Center, ads/info feed, replays, collection and PV playlist editing. Only the 11 bundled songs are playable (DLC servers are gone). All songs/difficulties are unlocked.

## Repository / publication state

- Git repo on branch `main`, tracking `origin` (`https://github.com/YuriDrak/mikuflick-linux-port.git`); the Android port, FFmpeg removal, touch-area and screen-fill work is committed and pushed.
- Published to GitHub; keep local `main` and `origin/main` in sync.
- Publishable set: 54 files (~2.3 MB) — `port/` (incl. vendored, patched `third_party/pl_mpeg.h`, MIT), `android/` (Gradle/CMake project, Makefile, checksums; no toolchain), `docs/`, `tools/` (own sources/scripts), a bundled OFL M+ font, `README.md`, `LICENSE` (MIT), `.gitignore`, and this `CLAUDE.md` (tracked intentionally).
- `.gitignore` excludes: `*.ipa` (~924 MB), `extracted/` (~981 MB), `work/` (~132 MB RE artifacts), `tools/third_party/` (~1.1 GB Ghidra), `android/third_party/` (~3 GB SDK/NDK/Gradle/SDL sources), `*.apk`, keystores, all build output, and `port/data/gamedata.txt`.
- `README.md` is the public entry point; `port/README.md` has the detailed build/controls notes, `android/README.md` the Android build.

## Build / run

```sh
make -C port
make -C port setup IPA="/path/to/com.sega.MikuFlick02-iOS5.0-(Clutch-2.0.4).ipa"
./port/mikuflick
```

Dependencies: `build-essential pkg-config libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev`.

Android (toolchain inside the project, see `android/README.md`):

```sh
make -C android toolchain licenses sdk apk
make -C android install push run
```

## Next steps

1. Measure audio latency on phones and add a tap-to-calibrate screen; test more phones/aspect ratios.
2. Android extras: in-app IPA import (SAF + miniz + mfextract), release signing, GL context-loss texture reload.
3. Optional cleanups: confirm the licence choice, add CI build, and consider a small curated, non-copyrighted subset of RE outputs if ever needed.