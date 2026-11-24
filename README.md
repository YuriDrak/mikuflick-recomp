# Miku Flick/02 — native Linux port

A clean-room C11/SDL2 reimplementation of *Miku Flick/02* (SEGA, iOS v1.1.5) for Linux. The rhythm-game rules — note timing, judgement windows, scoring, combo bonus, tension gauge and ranks — are reproduced from the reverse-engineered original, as documented in [`docs/`](docs/).

> **This repository ships no game data.** It contains no SEGA/Crypton assets, music, video, artwork or charts. You need your own legally obtained, decrypted copy of `com.sega.MikuFlick02-iOS5.0-(Clutch-2.0.4).ipa` (v1.1.5, armv7). The port extracts the IPA locally and reads its tables, movies, audio and textures at runtime.

## Features

- Boot logos, title screen and music select with cover flow, artwork, star ratings and song previews.
- Full rhythm gameplay: flick notes, interludes, press/release timing judgement, combo and climax bonuses, tension gauge with vocal fade, game over and pause.
- Streaming MPEG-1 video and two ADX audio tracks (vocals + backing) decoded directly from the original `.usm` files.
- Result screen with ranks and a persistent local save file.
- Mouse/touch flick input and a keyboard mapping, input-timing calibration, fullscreen, volume controls.
- Headless modes for automated verification (`--autoplay`, `--autoplay-input`, screenshot and script modes).

All 11 bundled songs and all five difficulties (Easy, Normal, Hard, Extreme, Break the Limit) are unlocked.

## Requirements

Debian/Ubuntu package names:

```sh
sudo apt install build-essential pkg-config \
  libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev \
  libavformat-dev libavcodec-dev libavutil-dev libswresample-dev libswscale-dev
```

`unzip` is also required for the one-time setup step.

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
| Pause / resume | pause button (bottom right) | `Esc` / `P`; `Enter` quits while paused |

Notes are judged **when you release**, as in the original. Menus are navigated with the mouse or `Up`/`Down`/`Left`/`Right` + `Enter`; `Esc` goes back.

See [`port/README.md`](port/README.md) for the full control and UI description.

## Repository layout

| Path | Contents |
|---|---|
| `port/` | The Linux port: C sources, `Makefile`, bundled M+ font, detailed README. |
| `docs/` | Reverse-engineering notes, the port design plan and the original IPA archive inventory. |
| `tools/` | Self-contained C utilities and scripts used during analysis (`machoinfo`, `usmdump`, `bplist2json`, `mfextract`, Ghidra helpers, IPA extractor). |
| `extracted/` | *Ignored.* Your extracted game bundle, created by `make setup`. |
| `work/` | *Ignored.* Reverse-engineering scratch space: chart dumps, decompiled functions and Ghidra projects. |
| `build/`, `port/build/` | *Ignored.* Compiled objects and helper binaries. |

## Documentation

- [`port/README.md`](port/README.md) — detailed build, controls, features and testing aids.
- [`docs/linux_port_plan.md`](docs/linux_port_plan.md) — platform mapping, module layout, timing/sync design and deliberate deviations.
- [`docs/re_notes.md`](docs/re_notes.md) — reverse-engineering notes: USM format, chart format, atlases, judgement/scoring formulas, scene state machine and rendering.
- [`docs/archive_layout.md`](docs/archive_layout.md) — inventory of the original IPA and its binary.

## Not included

Shop/DLC, Game Center, Twitter, ads, the PV viewer, replays, the options screen (use command-line flags) and the help pages. The DLC servers are long gone; only the songs bundled in the IPA are playable.

## Testing aids

```sh
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./port/mikuflick --autoplay --exit-after-song
```

Additional flags: `--autoplay-input`, `--shot-dir DIR --shot-times 8,30`, `--shot-ui-times 0.5,3`, `--start-scene title|select|result`, `--script "9:enter,12:right"`. See [`port/README.md`](port/README.md).

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
- Menu font: **M+ 1p Bold**, © The M+ Project Authors, licensed under the SIL Open Font License 1.1 (`port/fonts/OFL.txt`).
- *Miku Flick/02*, Hatsune Miku and all related artwork, music and game data are © SEGA / Crypton Future Media. No such material is included in this repository.

This is an unofficial fan preservation project and is not affiliated with SEGA or Crypton Future Media. Use it only with game data you legally own.
