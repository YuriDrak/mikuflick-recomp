# Miku Flick/02: the game sources (Linux build)

A native C11/SDL2 reimplementation of SEGA's *Miku Flick/02* (iOS, v1.1.5). This directory builds the Linux version; the same `src/` files are also compiled for Android (see `../android/README.md`). The gameplay rules (note timing, judgement windows, scoring, combo bonus, tension gauge, ranks) are ported from the reverse-engineered original. See `../docs/re_notes.md`.

**No game data is included.** You need your own copy of `com.sega.MikuFlick02-iOS5.0-(Clutch-2.0.4).ipa` (v1.1.5, armv7, decrypted). At setup time the port extracts the IPA and reads its tables. At runtime it loads movies, audio and textures straight from the extracted app.

## Build

Debian/Ubuntu packages: `build-essential pkg-config libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev`

```sh
make -C port                                   # builds port/mikuflick
make -C port setup IPA="/path/to/com.sega.MikuFlick02-iOS5.0-(Clutch-2.0.4).ipa"
```
`setup` unpacks the IPA into `extracted/ipa/` (with an archive path-safety check) and writes `port/data/gamedata.txt`, which holds the song list, kana→key tables, judgement/score tables and the sound list read out of *your* `MikuFlick2` binary. It refuses any other build of the binary.

Without `unzip`, the game can import the IPA itself: `./port/mikuflick --import-ipa your.ipa --import-to DIR` (the same code as the Android app's IMPORT IPA button, `src/ipa.c`) writes only the files the game loads to `DIR/MikuFlick2.app` plus `DIR/gamedata.txt`, with CRC checks; run with `--app DIR/MikuFlick2.app --gamedata DIR/gamedata.txt`.

## Run (from the project root)

```sh
./port/mikuflick
```
Options: `--app DIR` (extracted `MikuFlick2.app`), `--gamedata FILE` (tables from `mfextract`; default `port/data/gamedata.txt`), `--data DIR` (save dir, default `~/.local/share/mikuflick-port`), `--song N --diff D` (jump straight into a song; diff 0 Easy … 3 Extreme, 4 Break the Limit), `--offset N` (input-timing calibration, same units as the original option), `--volume F`, `--se-volume F`, `--no-fail-sound`, `--font PATH` (menu font; `none` = built-in bitmap font), `--fullscreen`, `--window WxH`. F11 toggles fullscreen. `--import-ipa FILE [--import-to DIR]` imports your IPA in place of `setup` and exits (see above).

## Controls

| Action | Mouse / touch | Keyboard |
|---|---|---|
| Flick key あかさ/たなは/まやら/わ | press the key, drag up/right/down/left (or don't, for a tap), release | `Q W E` / `A S D` / `Z X C` / `Space`, or the numpad `7 8 9` / `4 5 6` / `1 2 3` / `0` |
| Flick direction | drag ≥ 16 pt (32 px at 640×960) | hold an **arrow key** while releasing the letter key |
| Interlude (♪ panel) | tap the panel | any board key |
| Pause menu | pause button (bottom right) | `Esc` / `P`, then `Up`/`Down` + `Enter`: RESUME, RETRY or MUSIC SELECT (mouse: click the buttons) |

The note is judged **when you release**, as in the original. Timing is measured at both press and release, so press and release on the beat.

Menus: the SEGA / Crypton / CRI logos can be skipped with a click or `Enter`. On the title, click or press `Enter` at "Touch to Start" to open the menu (`Up`/`Down` + `Enter`, or click). RHYTHM GAME, PV and OPTIONS work; SHOP is shown but not ported. OPTIONS: `Up`/`Down` + `Enter` to open a page, `Left`/`Right` to change a value, `Esc` back; settings are saved. PV select: `Left`/`Right` song, `Up`/`Down` + `Space` toggle LOOP/SHUFFLE/LYRICS/KARAOKE (or click the icons), `Enter` plays, `Esc` or a click during playback returns. In music select, `Left`/`Right` (or click/swipe the cover flow) changes song, `Up`/`Down` or a click on the row changes difficulty, MODE CHANGE toggles Break the Limit, `Enter`/PLAY starts, and `Esc` goes back. On a touchscreen a tap works wherever a click does (the first finger drives the menus; the flick board takes every finger). The Android build is described in `../android/README.md`.

## Display

The game is laid out on the original 640×960 (2:3) screen. It is drawn into an off-screen 640×960 image that is shown centred and scaled to fit the window (`src/view.c`), so the keys and touch areas never move. On screens that are taller or wider than 2:3 (most phones, wide monitors in fullscreen), the bars around it show a blurred, darkened copy of the song video, or of the menu. Mouse and touch positions are mapped back through the same rectangle.

## Media decoding

No FFmpeg is needed. `src/usm.c` reads the CRI USM containers (cue points and the audio/video payloads), `src/media.c` decodes ADX and Apple IMA4 audio itself, and the MPEG-1 video goes through the vendored [pl_mpeg](https://github.com/phoboslab/pl_mpeg) (`third_party/pl_mpeg.h`, MIT). The game's videos use an 11-bit intra DC precision that stock pl_mpeg does not support, so the vendored copy carries a small patch (search for `MIKUFLICK PATCH`).

## Font

Menu and result text uses **M+ 1p Bold** (`port/fonts/MPLUS1p-Bold.ttf`, SIL Open Font License 1.1, see `port/fonts/OFL.txt`; downloaded from github.com/google/fonts, sha256 in `port/fonts/SHA256SUMS`) as a stand-in for the iOS system font. If it is missing the port tries common system fonts (Noto Sans CJK, Droid Sans Fallback, DejaVu Sans, Liberation Sans), then a built-in bitmap font.

## Not included (see ../docs/linux_port_plan.md §6)

Shop/DLC, Twitter, Game Center, ads, replays, the collection and PV playlist editing. All songs and difficulties are unlocked.

## Testing aids

`--autoplay` (perfect play through the game rules), `--autoplay-input` (perfect play through synthetic keyboard/mouse events), `--exit-after-song` (print a `RESULT …` line and exit), `--shot-dir DIR --shot-times 8,30` (save PNG frames), `--start-scene title|select|result|options|pv` (result shows a sample score), `--shot-ui-times 0.5,3` (PNG frames at seconds since start, any screen), `--script "9:enter,12:right"` (inject key presses: enter/esc/left/right/up/down/space). With `SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy` these run headless. The `--import-ipa` path is also used to test the Android importer on a desktop.
