# Miku Flick/02: Linux port

A native C11/SDL2 reimplementation of SEGA's *Miku Flick/02* (iOS, v1.1.5) for Linux. The gameplay rules (note timing, judgement windows, scoring, combo bonus, tension gauge, ranks) are ported from the reverse-engineered original. See `../docs/re_notes.md`.

**No game data is included.** You need your own copy of `com.sega.MikuFlick02-iOS5.0-(Clutch-2.0.4).ipa` (v1.1.5, armv7, decrypted). At setup time the port extracts the IPA and reads its tables. At runtime it loads movies, audio and textures straight from the extracted app.

## Build

Debian/Ubuntu packages: `build-essential pkg-config libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev libavformat-dev libavcodec-dev libavutil-dev libswresample-dev libswscale-dev`

```sh
make -C port                                   # builds port/mikuflick
make -C port setup IPA="/path/to/com.sega.MikuFlick02-iOS5.0-(Clutch-2.0.4).ipa"
```
`setup` unpacks the IPA into `extracted/ipa/` (with an archive path-safety check) and writes `port/data/gamedata.txt`, which holds the song list, kana→key tables, judgement/score tables and the sound list read out of *your* `MikuFlick2` binary. It refuses any other build of the binary.

## Run (from the project root)

```sh
./port/mikuflick
```
Options: `--app DIR` (extracted `MikuFlick2.app`), `--data DIR` (save dir, default `~/.local/share/mikuflick-port`), `--song N --diff D` (jump straight into a song; diff 0 Easy … 3 Extreme, 4 Break the Limit), `--offset N` (input-timing calibration, same units as the original option), `--volume F`, `--se-volume F`, `--no-fail-sound`, `--font PATH` (menu font; `none` = built-in bitmap font), `--fullscreen`, `--window WxH`. F11 toggles fullscreen.

## Controls

| Action | Mouse / touch | Keyboard |
|---|---|---|
| Flick key あかさ/たなは/まやら/わ | press the key, drag up/right/down/left (or don't, for a tap), release | `Q W E` / `A S D` / `Z X C` / `Space`, or the numpad `7 8 9` / `4 5 6` / `1 2 3` / `0` |
| Flick direction | drag ≥ 16 pt (32 px at 640×960) | hold an **arrow key** while releasing the letter key |
| Interlude (♪ panel) | tap the panel | any board key |
| Pause / resume | pause button (bottom right) | `Esc` / `P`. While paused, `Enter` quits the song |

The note is judged **when you release**, as in the original. Timing is measured at both press and release, so press and release on the beat.

Menus: the SEGA / Crypton / CRI logos can be skipped with a click or `Enter`. On the title, click or press `Enter` at "Touch to Start" to open the menu (`Up`/`Down` + `Enter`, or click). Only **RHYTHM GAME** is functional; PV, OPTIONS and SHOP are shown but not ported. In music select, `Left`/`Right` (or click/swipe the cover flow) changes song, `Up`/`Down` or a click on the row changes difficulty, MODE CHANGE toggles Break the Limit, `Enter`/PLAY starts, and `Esc` goes back.

## Font

Menu and result text uses **M+ 1p Bold** (`port/fonts/MPLUS1p-Bold.ttf`, SIL Open Font License 1.1, see `port/fonts/OFL.txt`; downloaded from github.com/google/fonts, sha256 in `port/fonts/SHA256SUMS`) as a stand-in for the iOS system font. If it is missing the port tries common system fonts (Noto Sans CJK, Droid Sans Fallback, DejaVu Sans, Liberation Sans), then a built-in bitmap font.

## Not included (see ../docs/linux_port_plan.md §6)

Shop/DLC, Twitter, Game Center, ads, PV viewer, replays, the options screen (use the flags instead) and the help pages. All songs and difficulties are unlocked.

## Testing aids

`--autoplay` (perfect play through the game rules), `--autoplay-input` (perfect play through synthetic keyboard/mouse events), `--exit-after-song` (print a `RESULT …` line and exit), `--shot-dir DIR --shot-times 8,30` (save PNG frames), `--start-scene title|select|result` (result shows a sample score), `--shot-ui-times 0.5,3` (PNG frames at seconds since start, any screen), `--script "9:enter,12:right"` (inject key presses: enter/esc/left/right/up/down/space). With `SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy` these run headless.
