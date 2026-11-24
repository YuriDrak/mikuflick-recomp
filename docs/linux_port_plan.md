# Linux port plan: Miku Flick/02 → native C11 / SDL2

This is a clean-room **reimplementation** of the game logic in portable C, driven by the user's own extracted IPA. It is not a binary translation of the ARM code. The repository ships no SEGA/Crypton data. Every asset, and every data table read from the executable, comes from the user's `.ipa` at setup time.

## 1. Platform mapping

| iOS dependency (from load commands / symbols) | Use in the original | Linux replacement |
|---|---|---|
| UIKit (`UIApplication`, `UIView`, `UITouch`, nibs) | app lifecycle, touch events in UIKit *points* (320×480) | SDL2 window + event loop. Mouse/touch events are mapped to the same 320×480 point space |
| OpenGLES 2 (`EAGLView`, `ES2Renderer`, `ShaderManager`) | 2D sprite rendering in 640×960 "retina" space | `SDL_Renderer` (GPU-accelerated) with logical size 640×960, letterboxed and resizable |
| CoreGraphics / `TexTextManager` | rendering system-font text to textures | atlas sprites wherever the game has them; menu/result text via **SDL_ttf** with the bundled M+ 1p Bold (SIL OFL), bitmap-font fallback |
| CRI Mana (`criManaPlayer_*`) | USM video + 2 ADX tracks + cue points | **libavformat/libavcodec** (FFmpeg has `usm` demuxer, `mpeg1video` and `adpcm_adx` decoders) for A/V. **Own `@UTF` parser** (`port/src/usm.c`) for cue points |
| CRI Atom / AudioToolbox / OpenAL / AVFoundation | SFX + BGM (`.caf` IMA4), preview loops (`.adx`) | libav decode to S16 PCM at load, mixed by an **SDL2 audio callback** (own mixer) |
| Foundation `NSUserDefaults` / `NSKeyedArchiver` (`MikuFlick2.dat`) | save data | plain text save file in `$XDG_DATA_HOME/mikuflick-port/save.txt` (or `--data DIR`) |
| GameKit, StoreKit, Twitter, Accounts, AdSupport, NoahSDK, Reachability, MessageUI | leaderboards, IAP DLC, tweeting, ads | **dropped**. The DLC servers are gone, and the bundled 11 songs are the playable set |
| `NSString` cue parsing, `NSArray` note lists | gameplay containers | C arrays |

## 2. Asset pipeline (user-supplied)

```
make -C port setup IPA="/path/com.sega.MikuFlick02-....ipa"
  └─ tools/extract_ipa.sh           unzip (path-checked) → extracted/ipa/Payload/MikuFlick2.app
  └─ build/mfextract                reads tables out of the user's MikuFlick2 binary → port/data/gamedata.txt
```
At runtime the port reads `.usm`, `.adx`, `.caf`, `.png` and atlas `.plist` **directly from the extracted .app directory** (`--app DIR`, default `extracted/ipa/Payload/MikuFlick2.app`). Nothing is converted or duplicated.

`mfextract` exports, by known VM address in the v1.1.5 binary (it checks the UUID `96B378F9-…` and refuses any other build):
`_sTblDefaultMusicPackData` (song list), `e_FlickBoardTypeTbl` / `ResultTbl` / `SubTypeTbl` (kana→key/dir), `e_CharPictTbl` (kana→glyph tile), `e_FlickBoardPictTbl` (key tiles and positions), `s_tblNoteNormal_Front/Back`, `s_tblAddScore`, `s_tblTensionGauge`, `s_tblDifficultEndTimeOfs`, `_s_szSoundFile` / `_s_bSoundLoop`.

## 3. Module layout (`port/`)

| File | Responsibility |
|---|---|
| `src/main.c` | args, SDL init, main loop, scene switching (ids follow `SceneManager`: 1 Title, 2 Game, 3 MusicSelect, 8 Result) |
| `src/gamedata.c` | parse `gamedata.txt` |
| `src/bplist.c` | bplist00 reader (atlases) |
| `src/atlas.c` | PNG textures (SDL_image) + atlas frame lookup + sprite drawing |
| `src/usm.c` | USM chunk walker + `@UTF` reader → cue list |
| `src/media.c` | libav: whole-file audio decode to S16 stereo 44.1 kHz (per stream), streaming MPEG-1 video decoder |
| `src/mixer.c` | SDL audio callback: music (vocal + backing with separate gains), SFX voices, audio clock |
| `src/game.c` | **exact port** of `NoteManager` / `NoteNormal` / `NoteInterlude` / cue handlers / judgement / scoring / tension gauge, plus gameplay rendering |
| `src/ui.c` | title, music select, result screens, bitmap font |
| `src/save.c` | high scores / clear flags / unlocks |

## 4. Timing and sync (from RE §5.1–5.2)

* Master clock = **audio playback position** of the song (the samples the SDL callback has consumed, timestamped with `SDL_GetPerformanceCounter`, minus device latency). This matches the original, where `PlayCnt = ceil(CriPlayTime·30)` comes from CRI's audio-driven movie time.
* `PlayCnt = ceil(t·30)`. Cue points fire when `t·1000 ≥ cue.time`. The handlers see the PlayCnt from before this frame's update, which mirrors `MikuFlickCriManager Exec` calling `CriManager::Exec()` (cue callbacks) before refreshing PlayCnt.
* Video frames are shown once their pts ≤ clock (drop when behind, hold when ahead). Video never drives time.
* Render/update run at the display refresh rate (vsync). The gameplay state is a pure function of PlayCnt, as in the original.

## 5. Input mapping

| Original | Port |
|---|---|
| touch down/move/up on a flick key (12-key pad, centres (129,530)…(256,854) in 640×960, cell 127×108) | mouse/touch press → drag → release, same 16-point tap radius and π/4 sector rule (`TouchFlickBoard checkFlick`) |
| — | keyboard: keys `7 8 9 / 4 5 6 / 1 2 3 / 0` on the numpad, or `Q W E / A S D / Z X C / Space`, act as the あかさ/たなは/まやら/わ keys. Holding an **arrow key** at release flicks in that direction, and no arrow means a tap |
| interlude panel | any key/click on the board area |
| pause button | `Esc` / `P` |

## 6. Deliberate deviations

* No network features (shop, Twitter, Game Center, ads, info feed) and no in-app purchases.
* The PV viewer, replay recording, collection, credits, options UI and help pages are deferred. Settings come from command-line flags (`--offset`, `--volume`, `--unlock-all`, `--no-fail-sound`).
* Menu screens are simplified re-layouts built from the original atlas sprites. They are not pixel copies of the iOS layouts, whose positions are scattered across hundreds of `Window*` methods.
* Lyric-line telops (op 3) are only shown in PV mode in the original, so the core game does not need a text font.

## 7. Status and verification (milestone 3)

Implemented in `port/`: boot logos, title, music select (art, logo, stars, preview loop), the full rhythm game (video, both audio tracks, cue-driven notes, judgement, scoring, combo, climax bonus, interludes, tension gauge with vocal fade, game over, pause), result screen with rank, and save file.

Headless checks (`SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy`, real-time audio clock):

| Test | Expected (derived independently from chart dump + RE formulas) | Observed |
|---|---|---|
| magnet Extreme, `--autoplay` (rules level) | 172 notes / 77 interlude (counted from `work/charts/magnet.tsv`), all COOL, PERFECT. Combo bonus Σmin(500,50⌊(c+5)/10⌋) = 61,500 | 172 COOL, 77/77, rank 0, combo bonus 61,500, stage 54,116 (= 51,600 + 77 + 39 + 12 × 200 climax) |
| same, `--autoplay-input` (synthetic keyboard+mouse through `game_event`) | identical to the above | identical (115,616). All 172 traced judgements are grade 5 |
| magnet Hard, no input | game over when the gauge reaches 0: ⌈128 / ((128/260+0.01)·10)⌉ = 26 misses | game over after 26 WORST, rank E |
| 恋は戦争 Break the Limit, no input | no penalties, no game over, r1 = 0 → rank D | rank 5 (D), game_over 0 |
| `mfextract` tables | equal to the values read by hand in RE | equal |

Caveat: if three instances share the CPU under the software renderer, frame times exceed the 33 ms tick and synthetic presses land late. The game samples input once per frame, as the original does (60 fps), so play quality depends on the frame rate. With a GPU renderer and vsync this is not an issue.

Not yet verified: real audio devices (PipeWire/Pulse latency is not compensated beyond one SDL buffer, and `--offset` is the calibration knob), touchscreens, and long play sessions on real hardware.

## 8. Build

Dependencies (Debian names): `libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev libavformat-dev libavcodec-dev libswresample-dev libavutil-dev`, `gcc`, `make`.
```
make -C port            # builds port/mikuflick
make -C port setup IPA=…  # one-time asset extraction + table export
./port/mikuflick [--app DIR] [--data DIR]
```
