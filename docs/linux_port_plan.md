# Linux (and Android) port plan: Miku Flick/02 → native C11 / SDL2

This is a clean-room **reimplementation** of the game logic in portable C, driven by the user's own extracted IPA. It is not a binary translation of the ARM code. The repository ships no SEGA/Crypton data. Every asset, and every data table read from the executable, comes from the user's `.ipa` at setup time.

## 1. Platform mapping

| iOS dependency (from load commands / symbols) | Use in the original | Linux replacement |
|---|---|---|
| UIKit (`UIApplication`, `UIView`, `UITouch`, nibs) | app lifecycle, touch events in UIKit *points* (320×480) | SDL2 window + event loop. Mouse/touch events are mapped to the same 320×480 point space |
| OpenGLES 2 (`EAGLView`, `ES2Renderer`, `ShaderManager`) | 2D sprite rendering in 640×960 "retina" space | `SDL_Renderer` (GPU-accelerated) with logical size 640×960, letterboxed and resizable |
| CoreGraphics / `TexTextManager` | rendering system-font text to textures | atlas sprites wherever the game has them; menu/result text via **SDL_ttf** with the bundled M+ 1p Bold (SIL OFL), bitmap-font fallback |
| CRI Mana (`criManaPlayer_*`) | USM video + 2 ADX tracks + cue points | **Own USM chunk reader** (`port/src/usm.c`) for cue points and the @SFV/@SFA stream payloads, **own ADX decoder** and **pl_mpeg** (vendored, MIT) for the MPEG-1 video (`port/src/media.c`). Until the Android port this used FFmpeg |
| CRI Atom / AudioToolbox / OpenAL / AVFoundation | SFX + BGM (`.caf` IMA4), preview loops (`.adx`) | own ADX and Apple IMA4 decoders to S16 PCM at load, mixed by an **SDL2 audio callback** (own mixer) |
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

In-app alternative (Android, or `mikuflick --import-ipa FILE --import-to DIR`): `src/ipa.c` reads the IPA zip directly (own central-directory reader and inflater with CRC-32 checks), runs the same table export on the binary in memory, and extracts only the top-level `.png .plist .usm .adx .caf` files of the app. On the user's IPA it produces 175 files byte-identical to `unzip`'s and the same `gamedata.txt`, in about 7 s on a desktop.

`mfextract` (whose logic now lives in `port/src/mfx.c`, shared with the importer) exports, by known VM address in the v1.1.5 binary (it checks the UUID `96B378F9-…` and refuses any other build):
`_sTblDefaultMusicPackData` (song list), `e_FlickBoardTypeTbl` / `ResultTbl` / `SubTypeTbl` (kana→key/dir), `e_CharPictTbl` (kana→glyph tile), `e_FlickBoardPictTbl` (key tiles and positions), `s_tblNoteNormal_Front/Back`, `s_tblAddScore`, `s_tblTensionGauge`, `s_tblDifficultEndTimeOfs`, `_s_szSoundFile` / `_s_bSoundLoop`.

## 3. Module layout (`port/`)

| File | Responsibility |
|---|---|
| `src/main.c` | args, SDL init, main loop, scene switching (ids follow `SceneManager`: 1 Title, 2 Game, 3 MusicSelect, 8 Result) |
| `src/gamedata.c` | parse `gamedata.txt` |
| `src/bplist.c` | bplist00 reader (atlases) |
| `src/atlas.c` | PNG textures (SDL_image) + atlas frame lookup + sprite drawing |
| `src/ipa.c` | in-app IPA importer: zip central directory, inflate + CRC-32, app-version check, selective extraction, progress callback |
| `src/mfx.c` | game tables read from the user's `MikuFlick2` binary (shared by `tools/mfextract` and `ipa.c`) |
| `src/usm.c` | USM chunk walker + `@UTF` reader → cue list |
| `src/media.c` | own ADX + IMA4 decoders: whole-file audio decode to S16 stereo 44.1 kHz (per stream), streaming MPEG-1 video (pl_mpeg, `src/pl_mpeg_impl.c`) |
| `src/mixer.c` | SDL audio callback: music (vocal + backing with separate gains), SFX voices, audio clock |
| `src/game.c` | **exact port** of `NoteManager` / `NoteNormal` / `NoteInterlude` / cue handlers / judgement / scoring / tension gauge, plus gameplay rendering |
| `src/ui.c` | title, music select, result screens, bitmap font |
| `src/save.c` | high scores / clear flags / unlocks |
| `src/view.c` | 640×960 render target shown centred and scaled to fit; bars of non-2:3 screens filled with a blurred backdrop (video in game); window/touch → 640×960 mapping |

## 4. Timing and sync (from RE §5.1–5.2)

* Master clock = **audio playback position** of the song (the samples the SDL callback has consumed, timestamped with `SDL_GetPerformanceCounter`, minus device latency). This matches the original, where `PlayCnt = ceil(CriPlayTime·30)` comes from CRI's audio-driven movie time.
* `PlayCnt = ceil(t·30)`. Cue points fire when `t·1000 ≥ cue.time`. The handlers see the PlayCnt from before this frame's update, which mirrors `MikuFlickCriManager Exec` calling `CriManager::Exec()` (cue callbacks) before refreshing PlayCnt.
* Video frames are shown once their pts ≤ clock (drop when behind, hold when ahead). Video never drives time.
* Render/update run at the display refresh rate (vsync). The gameplay state is a pure function of PlayCnt, as in the original.

## 5. Input mapping

| Original | Port |
|---|---|
| touch down/move/up on a flick key (12-key pad, top-left corners (129,530)…(256,854) in 640×960, cell 127×108) | mouse/touch press → drag → release, same 16-point tap radius and π/4 sector rule (`TouchFlickBoard checkFlick`) |
| — | keyboard: keys `7 8 9 / 4 5 6 / 1 2 3 / 0` on the numpad, or `Q W E / A S D / Z X C / Space`, act as the あかさ/たなは/まやら/わ keys. Holding an **arrow key** at release flicks in that direction, and no arrow means a tap |
| interlude panel | any key/click on the board area |
| pause button | `Esc` / `P` |

## 6. Deliberate deviations

* No network features (shop, Twitter, Game Center, ads, info feed) and no in-app purchases.
* Replay recording, the collection and PV playlist editing are not ported. The options screen stores its settings in the save file; command-line flags (`--offset`, `--volume`, `--se-volume`, `--no-fail-sound`) override them for a session.
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
| in-app importer (`--import-ipa`, same code as Android) | output identical to `unzip` + `mfextract` | 175 files byte-identical, same `gamedata.txt`, about 7 s; malformed/truncated IPAs refused with a message and no leftover data |
| magnet Extreme `--autoplay` and `--autoplay-input` after replacing FFmpeg with own ADX/IMA4 decoders and pl_mpeg (2026-11-24) | unchanged | both 115,616 PERFECT, 172 COOL, 77/77. Frames 0 and 600 match FFmpeg at PSNR 45.7 / 42.9 dB |
| Android debug APK on a real phone (A059P, arm64, Android 16), magnet Extreme `--autoplay` | same as Linux | 115,616 PERFECT, 172 COOL, 77/77; video correct; tap and Back key navigate the menus |

Caveat: if three instances share the CPU under the software renderer, frame times exceed the 33 ms tick and synthetic presses land late. The game samples input once per frame, as the original does (60 fps), so play quality depends on the frame rate. With a GPU renderer and vsync this is not an issue.

Touch input was checked on a phone (Android build, §9): menus, the flick keys' touch areas and the Back button. Not yet measured: audio output latency on real devices (PipeWire/Pulse on Linux, OpenSL ES/AAudio on phones; it is compensated only by one SDL buffer, and the input-timing option / `--offset` is the calibration knob), and long play sessions.

## 8. Build

Dependencies (Debian names): `libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev`, `gcc`, `make`. FFmpeg is no longer needed (see §9).
```
make -C port            # builds port/mikuflick
make -C port setup IPA=…  # one-time asset extraction + table export
./port/mikuflick [--app DIR] [--data DIR]
```

Without `unzip`, use the built-in importer instead of `setup`:
```
./port/mikuflick --import-ipa your.ipa --import-to imported
./port/mikuflick --app imported/MikuFlick2.app --gamedata imported/gamedata.txt
```

Android: see `android/README.md` (`make -C android toolchain licenses sdk apk install run`; the game data is imported in the app).

## 9. Android port and FFmpeg removal (2026-11-24)

The Android build (`android/`, see `android/README.md`) compiles the same `port/src` files with the NDK (CMake), next to SDL2/SDL2_image/SDL2_ttf built from source, inside SDL's Java activity. To make that build small and self-contained, FFmpeg was replaced on both platforms:

* `usm.c` also reads the stream payloads (`@SFV`/`@SFA`, data type 0). Concatenating the video payloads of `magnet.usm` gives 3,590 pictures and exactly the 71,945,020 bytes the CRID directory records for the source `.avi`.
* `media.c` decodes ADX (encoding 3, coefficients from the high-pass cutoff, as in FFmpeg's `adpcm_adx`) and Apple IMA4 in CAF, and feeds the MPEG-1 video to **pl_mpeg**.
* The USM video uses an **11-bit intra DC precision** (`VIDEO_HDRINFO.mpeg_dcprec = 11`; the stream's user data says `TMPGEXS … IDCPREC … 3`). That is an MPEG-2 feature inside an MPEG-1 stream. FFmpeg detects the TMPGEnc marker; stock pl_mpeg does not and produces block garbage, so the vendored copy is patched (DC size codes up to 11 bits, predictor reset `128 << p`, DC scale `8 >> p`) and `media.c` sets the precision from the USM header.
* Android specifics in `main.c`: data paths from `SDL_AndroidGetExternalStoragePath()` / internal storage, an on-screen message when data is missing, the first finger turned into pointer events for menus (the board keeps raw multi-touch), Back → Esc, pause and save on `SDL_APP_WILLENTERBACKGROUND`. `game.c` judges an input at the song time of its event timestamp rather than at the last frame update.
* Screen fill (`view.c`): scenes render into a 640×960 target that is shown centred. On tall phones the bars get a blurred, darkened copy of the video (two linear downscales to 40×60). The targets are RGBA32 because SDL 2.30's GLES2 renderer colour-swaps RGBA textures drawn into a BGRA (ARGB8888) target but not its YUV shader, which turned the movie R↔B. Mouse and touch coordinates are mapped back through the same rectangle.
* Touch areas fix: the `e_FlickBoardPictTbl` positions are the keys' top-left corners. The port had hit-tested them as centres, so every touch registered half a key below and to the right of the drawn key (reported on a phone as "the input lands lower than where I touch"). Keys and the interlude panel now use the drawn rectangles.
* In-app IPA import (`ipa.c`, `mfx.c`): Android has no `make setup`, so on first start the app shows an import screen. The system file picker (`MikuFlickActivity.pickIpa`) returns a file descriptor; the importer checks the game version on the binary before extracting about 980 MB (with a free-space check), writes into `MikuFlick2.app.import/` and swaps it in, writing `gamedata.txt` last. Verified on Linux against `unzip` + `mfextract` (identical output); bad inputs (not a zip, a zip without an iOS app, a truncated IPA) are refused with a message and leave nothing behind.
