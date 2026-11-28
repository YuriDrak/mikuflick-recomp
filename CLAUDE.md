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

_Last updated: 2026-11-27._

## Overview

*Miku Flick/02* (SEGA, iOS v1.1.5, armv7) native Linux and Android port: a clean-room **C11 + SDL2** reimplementation. It is not a binary translation; game rules were reconstructed from the reverse-engineered `MikuFlick2` binary and documented in `docs/re_notes.md`.

The repository ships **no SEGA/Crypton game data**. A user-supplied, decrypted IPA is required. `make -C port setup IPA=…` extracts it to `extracted/ipa/` and writes `port/data/gamedata.txt`; both are gitignored. Alternatively `./port/mikuflick --import-ipa FILE --import-to DIR` (the same importer the Android app uses) writes `DIR/MikuFlick2.app/` and `DIR/gamedata.txt` without `unzip`.

## Implemented

- **Port** (`port/src`, ~5700 lines, excluding vendored pl_mpeg): `main`, `game`, `ui`, `media`, `mixer`, `usm`, `atlas`, `bplist`, `text`, `save`, `gamedata`, `assets`, `util`, `view` (screen composition), `ipa` (in-app IPA importer: zip + inflate, read through a `ZipSrc`; also the per-pack DLC catalog/installer), `dlc` (DLC song table from the original save), `net` (http(s) URL as a `ZipSrc` via range requests: curl / Android Java / iOS NSURLSession), `dlcdl` (DLC page download worker), `mfx` (game tables from the binary, shared with `tools/mfextract`), `pl_mpeg_impl` + `mf.h`. No FFmpeg: own USM payload reader, ADX and IMA4 decoders, MPEG-1 via vendored `port/third_party/pl_mpeg.h` (MIT, patched for the 11-bit intra DC precision of the Sofdec/TMPGEnc streams; see `MIKUFLICK PATCH`).
- **Android** (`android/`): Gradle (AGP 8.7) + CMake build of the same sources with SDL2 2.30.10, SDL2_image 2.8.2, SDL2_ttf 2.22.0; `make -C android toolchain|sdk|apk|install|push|run|logcat`. SDK, NDK r27c, Gradle and caches live in gitignored `android/third_party/` (~3 GB). The project path contains a space: the stock `sdkmanager` script and `ndk-build` break on it, so the Makefile calls sdkmanager via `java` and the native build uses CMake. On first start the app imports the user's own IPA (system file picker -> fd -> `ipa.c`) into its external files dir; `make -C android push` (adb, folders chmod 0775) remains as an alternative.
- **Screens/features**: boot logos, title, music select (cover flow, artwork, stars, previews), full rhythm game (video, two ADX tracks, flick notes, interludes, judgement, scoring, combo/climax bonuses, tension gauge with vocal fade, game over, pause menu with RESUME/RETRY/MUSIC SELECT), result screen with ranks, OPTIONS (display, audio, handedness, keyboard, help, credits; saved), PV viewer (lyrics, KARAOKE, LOOP, SHUFFLE), persistent save.
- **Screen**: `view.c` renders the 640×960 scene to a target shown centred; non-2:3 screens get a blurred video/menu backdrop in the bars (RGBA32 targets: SDL GLES2 swaps YUV colours on BGRA targets).
- **Input**: mouse/multi-touch flick input, first finger mapped to the pointer for menus/pause/PV, Android Back = Esc, auto-pause on background, inputs judged at their event timestamp, keyboard mapping, input-timing calibration (`--offset`), fullscreen, volume controls.
- **Testing aids**: `--autoplay`, `--autoplay-input`, `--exit-after-song`, `--shot-dir/--shot-times`, `--shot-ui-times`, `--start-scene`, `--script`, headless via dummy SDL drivers.
- **RE tooling** (`tools/`): own C tools `machoinfo`, `usmdump`, `bplist2json`, `mfextract`, plus Ghidra scripts and IPA extractor. Ghidra is **not vendored**; `tools/ghidra_decompile.sh` uses `GHIDRA_DIR` or `tools/third_party/`.
- **Docs**: `docs/archive_layout.md` (IPA inventory), `docs/re_notes.md` (formats, formulas, scene/render analysis), `docs/linux_port_plan.md` (design, timing, deviations, verification).
- **CI** (`.github/workflows/build.yml`, branch `android-fixes`): every push/PR builds the Linux port, an unsigned Release iOS IPA (macos-26 runner) and the Android debug APK (runner-made debug key), uploaded as run artifacts; no game data in them. Published releases also get `MikuFlick.ipa` (version from the tag) and `altsource.json` (`ios/altsource.sh`), so `releases/latest/download/altsource.json` is the AltStore/SideStore/Feather source.

- **DLC** (issue #4; stacked PRs `android-fixes` → `dlc-import` (Container/ import, keyed saves, show/hide page) → `dlc-download` (this: per-pack downloads, INTERNET permission); the older WIP on `ios-port-analysis` is backed up in `refs/backup/dlc-wip-2026-11-27`): IPAs with the iMazing-style `Container/` (archive.org "All DLC Included", 7.6 GB) give 63 DLC songs in 21 packs (1–17, 96–99) via `make setup`, `--import-ipa` or IMPORT IPA; the song table comes from the original save `Container/Documents/MikuFlick2.dat` (`dlc.c`, re_notes §5.14). The title SHOP slot is the DLC page (SDL page on Linux, native screens on iOS (UIKit `DlcViewController`) and Android (`DlcPage.java` of plain framework views, driven through `android.c`; the game loop draws nothing meanwhile)): SHOW DLC SONGS toggle, a built-in archive.org link (`DLC_DEFAULT_URL`, mf.h; the maintainer agreed to a pre-filled link) or PASTE LINK (saved as `url` in save.txt only when it differs), pack list, tap to download one pack (~230–360 MB) out of the remote IPA, IMPORT IPA. All 21 known packs are always listed (`dlc.c` `k_known`: pack names and song titles only); packs not in the linked IPA, or all of them with no link, are greyed unless installed. Songs show the English title (VocaDB, checked against the artist; else the romanized movie name) next to the Japanese one. Per-song save lines (`rec`/`recx`/`flk`/`timing`) key DLC songs by movie name, built-in songs by number; lines of songs not installed are kept.
- **Movie deblocking** (`media.c`, pl_mpeg `plm_video_get_quantizer` patch): the 320×480 low-bit-rate movies get a post-filter that ramps out 8×8 block steps in smooth areas; `MF_NO_DEBLOCK=1` turns it off.

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
- In-app importer (`--import-ipa` on Linux, same code as Android): 175 files byte-identical to `unzip`, `gamedata.txt` identical to `mfextract`, ~7 s; bad/truncated files refused cleanly. Device test of the Android IMPORT IPA flow is pending (to be done by the user).

- DLC (2026-11-27, macOS build): full DLC IPA (md5 matches archive.org) imports 63 DLC songs in 32 s; per-pack downloads from the archive.org link: pack list in ~2.6 s, pack 1 in 70 s, pack 97 (zip64 offsets) works, re-install skips present files, cancel leaves no `.part` files; downloaded packs give `dlc` lines identical to the full import; DLC page installs reload the song list live; autoplay Extreme all-COOL on DLC songs from packs 1, 96, 97, 99. iOS simulator: native DLC page lists packs and downloaded the Summer Pack through `ios_http_range`. After the move onto upstream main: magnet Extreme still 115,616 (both autoplay modes), カンタレラ records saved by movie, greyed packs and the two-title rows checked on the SDL page and the iOS simulator. Not yet built/tested: Android download path (Java `httpRange`, INTERNET permission), `make setup` with the DLC IPA.

**Not yet measured:** audio output latency across devices (PipeWire/Pulse; OpenSL ES on phones), long sessions, phones other than the A059P. Software-renderer timing is frame-rate sensitive.

## Deliberately not implemented

Shop/DLC purchases, Twitter, Game Center, ads/info feed, replays, collection and PV playlist editing. The 11 bundled songs are always playable; DLC songs only from an IPA that contains them (DLC servers are gone). All songs/difficulties are unlocked.

## Repository / publication state

- Git repo on branch `main`, tracking `origin` (`https://github.com/YuriDrak/mikuflick-linux-port.git`); the Android port, FFmpeg removal, touch-area/screen-fill and in-app IPA importer work is committed and pushed.
- Published to GitHub; keep local `main` and `origin/main` in sync.
- Publishable set: 58 files (~2.3 MB) — `port/` (incl. vendored, patched `third_party/pl_mpeg.h`, MIT), `android/` (Gradle/CMake project, Makefile, checksums; no toolchain), `docs/`, `tools/` (own sources/scripts), a bundled OFL M+ font, `README.md`, `LICENSE` (MIT), `.gitignore`, and this `CLAUDE.md` (tracked intentionally).
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
make -C android install run       # then IMPORT IPA in the app (or: make -C android push)
```

## Next steps

1. Measure audio latency on phones and add a tap-to-calibrate screen; test more phones/aspect ratios.
2. Android extras: release signing, GL context-loss texture reload; confirm the in-app import on more phones/file providers (it needs a seekable file descriptor).
3. DLC: land the three stacked PRs (android-fixes, dlc-import, dlc-download); build/test the Android and iOS download paths on devices; the default link (archive.org) was OKed by the maintainer. Custom songs are planned (parked: an own `.flick` chart format with kana inline, not `.osu`): songs are keyed by movie name in the save, new song sources should get their own `gamedata.txt` line type/pack ids and, ideally, their own folder instead of the flattened app folder; charts live in the `.usm` cue points (re_notes §2).
4. Optional cleanups: confirm the licence choice and consider a small curated, non-copyrighted subset of RE outputs if ever needed.
