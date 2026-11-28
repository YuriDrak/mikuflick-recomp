# Miku Flick/02: the game sources (Linux, Android, iOS build)

A native C11/SDL2 reimplementation of SEGA's *Miku Flick/02* (iOS, v1.1.5). This directory builds the Linux version; the same `src/` files are also compiled for Android (see `../android/README.md`) and iOS (see `../ios/README.md`). The gameplay rules (note timing, judgement windows, scoring, combo bonus, tension gauge, ranks) are ported from the reverse-engineered original. See `../docs/re_notes.md`.

**No game data is included.** You need your own copy of `com.sega.MikuFlick02-iOS5.0-(Clutch-2.0.4).ipa` (v1.1.5, armv7, decrypted). At setup time the port extracts the IPA and reads its tables. At runtime it loads movies, audio and textures straight from the extracted app.

## Build

Debian/Ubuntu packages: `build-essential pkg-config libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev`

```sh
make -C port                                   # builds port/mikuflick
make -C port setup IPA="/path/to/com.sega.MikuFlick02-iOS5.0-(Clutch-2.0.4).ipa"
```
`setup` unpacks the IPA into `extracted/ipa/` (with an archive path-safety check) and writes `port/data/gamedata.txt`, which holds the song list, kana→key tables, judgement/score tables and the sound list read out of *your* `MikuFlick2` binary. It refuses any other build of the binary.

Without `unzip`, the game can import the IPA itself: `./port/mikuflick --import-ipa your.ipa --import-to DIR` (the same code as the Android app's Import IPA button, `src/ipa.c`) writes only the files the game loads to `DIR/MikuFlick2.app` plus `DIR/gamedata.txt`, with CRC checks; run with `--app DIR/MikuFlick2.app --gamedata DIR/gamedata.txt`.

**Updating an existing install:** rerun `make -C port setup` (or re-import the IPA) once. `gamedata.txt` now also carries the ending credits read from the binary; with an older file the game still runs but hides the ENDING button on OPTIONS → CREDITS (and logs a hint). An older in-app import also lacks the `Localizable.strings` files that the help pages and option subtitles are read from.

## Run (from the project root)

```sh
./port/mikuflick
```
Options: `--app DIR` (extracted `MikuFlick2.app`), `--gamedata FILE` (tables from `mfextract`; default `port/data/gamedata.txt`), `--data DIR` (save dir, default `~/.local/share/mikuflick-port`), `--song N --diff D` (jump straight into a song; diff 0 Easy … 3 Extreme, 4 Break the Limit), `--offset N` (input-timing steps −10…+10, the original's INPUT TIMING units; overrides the per-song value for this session and is not saved), `--volume F`, `--se-volume F`, `--no-fail-sound`, `--font PATH` (menu font; `none` = built-in bitmap font), `--fullscreen`, `--window WxH`. F11 toggles fullscreen. `--import-ipa FILE [--import-to DIR]` imports your IPA in place of `setup` and exits (see above).

Environment variables:
- `MIKUFLICK_AUDIO_BUFFER=N`: audio buffer size in frames, a power of two from 256 to 8192 (default 1024; 512 on Android). Use a larger value if the desktop output crackles under system load; the audio clock accounts for the size.
- `MIKUFLICK_LANG=ja|en`: language of the help pages and option subtitles. By default it follows the system locale, as the original does (Japanese only when the first preferred language is Japanese). The port's other UI text is English either way.

## Controls

| Action | Mouse / touch | Keyboard |
|---|---|---|
| Flick key あかさ/たなは/まやら/わ | press the key, drag up/right/down/left (or don't, for a tap), release | `Q W E` / `A S D` / `Z X C` / `Space`, or the numpad `7 8 9` / `4 5 6` / `1 2 3` / `0` |
| Flick direction | drag ≥ 16 pt (32 px at 640×960) | hold an **arrow key** while releasing the letter key |
| Interlude (♪ panel) | tap the panel | any board key |
| Pause menu | pause button (bottom right) | `Esc` / `P`, then `Up`/`Down` + `Enter`: RESUME, RETRY or MUSIC SELECT (mouse: click the buttons) |
| INPUT TIMING (in the pause menu, saved per song) | drag the bar: 20 px per step, left = later (+), right = earlier (−) | `Left` / `Right` while paused |

The note is judged **when you release**, as in the original. Timing is measured at both press and release, so press and release on the beat.

Menus (on a touchscreen a tap works wherever a click does; the first finger drives the menus and the flick board takes every finger; Android Back = `Esc`):

| Screen | Mouse / touch | Keyboard |
|---|---|---|
| Logos / title | click to skip a logo, click "Touch to Start", then a menu button. RHYTHM GAME, PV and OPTIONS work; SHOP only plays its sound. The crown (bottom left) opens RESULTS | `Enter`; menu `Up`/`Down` + `Enter`; crown `Left` or `R` |
| Music select | drag the cover flow sideways (anywhere in the band from the song logo down to the difficulty labels; the covers themselves are not buttons), click a difficulty, MODE CHANGE toggles Break the Limit, PLAY starts, Back | `Left`/`Right` song, `Up`/`Down` difficulty, `Enter` play, `Esc` back |
| Result | click to skip the counters; Back, PLAY AGAIN; the FLICK RESULT button (right) toggles the per-key boards; click a board to show its directions, click it again to close | `Left`/`Right`/`Tab` toggle the page, `Up`/`Down` open the previous/next board, `Enter`/`Esc` back |
| PV select | as music select, plus the LOOP / SHUFFLE / LYRICS / KARAOKE icons | `Left`/`Right` song, `Up`/`Down` + `Space` toggle an icon, `Enter` play, `Esc` back |
| PV playback | swipe left (further than half the screen width: 160 points = 320 px) = next PV; swipe right = previous PV, or restart the current one after its first 3 s; a plain tap does nothing. Pause button: RESUME / RETRY / MUSIC SELECT | `Right` / `Left` as the swipes; `Esc`/`P` pause and resume, `Up`/`Down` + `Enter` in the menu |
| OPTIONS | click a page, then its controls; drag a volume bar | `Up`/`Down` + `Enter` open a page, `Left`/`Right` change a value, `Esc` back; settings are saved |
| HELP | drag the text up/down (or the mouse wheel); the arrows at the bottom change page (10 pages, the last is the licence) | `Up`/`Down` scroll, `PgUp`/`PgDn` scroll faster, `Home`/`End`, `Left`/`Right` page |
| CREDITS | ENDING (bottom right) plays the credits roll (about 110 s, not skippable, as in the original) and returns here | `Enter` |
| RESULTS | RECORDS; LEADERBOARD / ACHIEVEMENTS only show that Game Center is not available; Back | `Up`/`Down` + `Enter`, `Esc` back |
| RECORDS | swipe sideways or use the side arrows for the song, the arrows by the title for the difficulty, the FLICK RESULT button for the per-key page | `Left`/`Right` song, `Up`/`Down` difficulty, `Space`/`Tab` page, `Enter` next board on the flick page, `Esc` back |

The Android build is described in `../android/README.md`. The iOS build is described in `../ios/README.md`.

## Display

The game is laid out on the original 640×960 (2:3) screen. It is drawn into an off-screen 640×960 image that is shown centred and scaled to fit the window (`src/view.c`), so the keys and touch areas never move. On screens that are taller or wider than 2:3 (most phones, wide monitors in fullscreen), the bars around it show a blurred, darkened copy of the song video, or of the menu. On phones (iOS, Android) the bars stay black during a song and the menu blur is dimmer. Mouse and touch positions are mapped back through the same rectangle.

## Media decoding

No FFmpeg is needed. `src/usm.c` reads the CRI USM containers (cue points and the audio/video payloads), `src/media.c` decodes ADX and Apple IMA4 audio itself, and the MPEG-1 video goes through the vendored [pl_mpeg](https://github.com/phoboslab/pl_mpeg) (`third_party/pl_mpeg.h`, MIT). The game's videos use an 11-bit intra DC precision that stock pl_mpeg does not support, so the vendored copy carries a small patch (search for `MIKUFLICK PATCH`).

## Font

Menu and result text uses **M+ 1p Bold** (`port/fonts/MPLUS1p-Bold.ttf`, SIL Open Font License 1.1, see `port/fonts/OFL.txt`; downloaded from github.com/google/fonts, sha256 in `port/fonts/SHA256SUMS`) as a stand-in for the iOS system font. If it is missing the port tries common system fonts (Noto Sans CJK, Droid Sans Fallback, DejaVu Sans, Liberation Sans), then a built-in bitmap font.

## Not included (see ../docs/linux_port_plan.md §6)

Shop/DLC, Twitter, Game Center, ads, replays, the collection and PV playlist editing. All songs and difficulties are unlocked.

## Testing aids

`--autoplay` (perfect play through the game rules), `--autoplay-input` (perfect play through synthetic keyboard/mouse events), `--exit-after-song` (print a `RESULT …` line and exit), `--shot-dir DIR --shot-times 8,30` (save PNG frames), `--start-scene title|select|result|options|pv|credits|ending|movie|scores|records|import|import-update` (result shows a sample score; import / import-update = Android/iOS only: the import screen as on a first start / an outdated install, for screenshots; credits = OPTIONS → CREDITS; ending = the credits roll followed, as on a first clear, by the ending movie and 初音ミクの激唱 Extreme; movie = the ending movie alone; scores = RESULTS; records = RECORDS), `--shot-ui-times 0.5,3` (PNG frames at seconds since start, any screen), `--script "9:enter,12:right"` (inject key presses: enter/esc/left/right/up/down/space; `dX0-X1-Y` injects a pointer drag from X0 to X1 at height Y in 640×960 coordinates, a tap when X0 = X1; `mX0-Y0-X1-Y1` a real-mouse drag over 0.5 s). With `SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy` these run headless. The `--import-ipa` path is also used to test the Android importer on a desktop. Debug environment hooks: `MF_DEBUG=1` logs every judgement and the interlude start/end; `MF_NO_DEBLOCK=1` shows the movies without the deblocking filter; `MF_TEST_HOLD="x,y,dx,dy,t0,t1"` holds a synthetic finger at (x,y) from song time t0, dragged by (dx,dy), released at t1 (for `--shot-times` captures of a pressed key).
