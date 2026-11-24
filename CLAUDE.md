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

*Miku Flick/02* (SEGA, iOS v1.1.5, armv7) native Linux port: a clean-room **C11 + SDL2** reimplementation. It is not a binary translation; game rules were reconstructed from the reverse-engineered `MikuFlick2` binary and documented in `docs/re_notes.md`.

The repository ships **no SEGA/Crypton game data**. A user-supplied, decrypted IPA is required. `make -C port setup IPA=…` extracts it to `extracted/ipa/` and writes `port/data/gamedata.txt`; both are gitignored.

## Implemented

- **Port** (`port/`, ~3.7k lines): `main`, `game`, `ui`, `media`, `mixer`, `usm`, `atlas`, `bplist`, `text`, `save`, `gamedata`, `assets`, `util` + `mf.h`.
- **Screens/features**: boot logos, title, music select (cover flow, artwork, stars, previews), full rhythm game (video, two ADX tracks, flick notes, interludes, judgement, scoring, combo/climax bonuses, tension gauge with vocal fade, game over, pause), result screen with ranks, persistent save.
- **Input**: mouse/touch flick input and keyboard mapping, input-timing calibration (`--offset`), fullscreen, volume controls.
- **Testing aids**: `--autoplay`, `--autoplay-input`, `--exit-after-song`, `--shot-dir/--shot-times`, `--shot-ui-times`, `--start-scene`, `--script`, headless via dummy SDL drivers.
- **RE tooling** (`tools/`): own C tools `machoinfo`, `usmdump`, `bplist2json`, `mfextract`, plus Ghidra scripts and IPA extractor. Ghidra is **not vendored**; `tools/ghidra_decompile.sh` uses `GHIDRA_DIR` or `tools/third_party/`.
- **Docs**: `docs/archive_layout.md` (IPA inventory), `docs/re_notes.md` (formats, formulas, scene/render analysis), `docs/linux_port_plan.md` (design, timing, deviations, verification).

## Verified (see `docs/linux_port_plan.md` §7)

- `magnet` Extreme `--autoplay`: 172 notes, 77/77 interludes, all COOL, PERFECT, combo bonus 61,500.
- Same via `--autoplay-input` (synthetic keyboard/mouse): identical result.
- `magnet` Hard with no input: game over after 26 misses, rank E.
- 恋は戦争 Break the Limit with no input: rank D, no game over.
- `mfextract` output matches the tables read by hand during RE.

**Not yet verified:** real audio devices/latency (PipeWire/Pulse), touchscreens, and long sessions on real hardware. Software-renderer timing is frame-rate sensitive.

## Deliberately not implemented

Shop/DLC, Twitter, Game Center, ads/info feed, PV viewer, replays, collection, credits, options UI (use CLI flags) and help pages. Only the 11 bundled songs are playable (DLC servers are gone). All songs/difficulties are unlocked.

## Repository / publication state

- Git repo on branch `main`, tracking `origin` (`https://github.com/YuriDrak/mikuflick-linux-port.git`); working tree clean.
- Published to GitHub; keep local `main` and `origin/main` in sync.
- Publishable set: 37 files (~2.1 MB) — `port/`, `docs/`, `tools/` (own sources/scripts), a bundled OFL M+ font, `README.md`, `LICENSE` (MIT), `.gitignore`, and this `CLAUDE.md` (tracked intentionally).
- `.gitignore` excludes: `*.ipa` (~924 MB), `extracted/` (~981 MB), `work/` (~132 MB RE artifacts), `tools/third_party/` (~1.1 GB Ghidra), all build output, and `port/data/gamedata.txt`.
- `README.md` is the public entry point; `port/README.md` has the detailed build/controls notes.

## Build / run

```sh
make -C port
make -C port setup IPA="/path/to/com.sega.MikuFlick02-iOS5.0-(Clutch-2.0.4).ipa"
./port/mikuflick
```

Dependencies: `build-essential pkg-config libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev libavformat-dev libavcodec-dev libavutil-dev libswresample-dev libswscale-dev`.

## Next steps

1. Validate on real audio hardware and a touchscreen; tune `--offset` if needed.
2. Optional cleanups: confirm the licence choice, add CI build, and consider a small curated, non-copyrighted subset of RE outputs if ever needed.