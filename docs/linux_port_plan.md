# Linux, Android and iOS port plan: Miku Flick/02 → native C11 / SDL2

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
| GameKit, StoreKit, Twitter, Accounts, AdSupport, NoahSDK, Reachability, MessageUI | leaderboards, IAP DLC, tweeting, ads | **dropped**. The DLC servers are gone; the bundled 11 songs are always playable, and DLC songs only from an IPA that ships the game's data container (`dlc.c`, re_notes §5.14): imported whole, or single packs read out of it with HTTP range requests from the built-in archive.org link or one the player pastes (`net.c`, `dlcdl.c`, `ipa.c` `dlc_catalog_*`). The RESULTS screen's LEADERBOARD / ACHIEVEMENTS buttons show a port note instead |
| `NSLocale preferredLanguages` (`IsJapanese()`) | ja/en help and option text | `SDL_GetPreferredLocales()`; `MIKUFLICK_LANG=ja\|en` overrides it |
| `NSString` cue parsing, `NSArray` note lists | gameplay containers | C arrays |

## 2. Asset pipeline (user-supplied)

```
make -C port setup IPA="/path/com.sega.MikuFlick02-....ipa"
  └─ tools/extract_ipa.sh           unzip (path-checked) → extracted/ipa/Payload/MikuFlick2.app
  └─ build/mfextract                reads tables out of the user's MikuFlick2 binary → port/data/gamedata.txt
```
At runtime the port reads `.usm`, `.adx`, `.caf`, `.png` and atlas `.plist` **directly from the extracted .app directory** (`--app DIR`, default `extracted/ipa/Payload/MikuFlick2.app`). Nothing is converted or duplicated.

In-app alternative (Android, or `mikuflick --import-ipa FILE --import-to DIR`): `src/ipa.c` reads the IPA zip directly (own central-directory reader and inflater with CRC-32 checks), runs the same table export on the binary in memory, and extracts only the top-level `.png .plist .usm .adx .caf` files of the app plus `en.lproj/Localizable.strings` and `ja.lproj/Localizable.strings` (help text and option subtitles, read at runtime by `help.c`). On the user's IPA it produces 175 files byte-identical to `unzip`'s and the same `gamedata.txt`, in about 7 s on a desktop.

`mfextract` (whose logic now lives in `port/src/mfx.c`, shared with the importer) exports, by known VM address in the v1.1.5 binary (it checks the UUID `96B378F9-…` and refuses any other build):
`_sTblDefaultMusicPackData` (song list), `e_FlickBoardTypeTbl` / `ResultTbl` / `SubTypeTbl` (kana→key/dir), `e_CharPictTbl` (kana→glyph tile), `e_FlickBoardPictTbl` (key tiles and positions), `s_tblNoteNormal_Front/Back`, `s_tblAddScore`, `s_tblTensionGauge`, `s_tblDifficultEndTimeOfs`, `_s_szSoundFile` / `_s_bSoundLoop`, and (optional) `s_tbl_Ending` (the 76-line credits roll, `ending` lines).

The `ending` lines are optional: they are written only when all 76 rows decode, and a `gamedata.txt` without them still loads, with the OPTIONS → CREDITS → ENDING button hidden and a log line asking to rerun setup. **Existing installs must rerun `make -C port setup` (or re-import the IPA) once** to get them; a re-import also adds the two `Localizable.strings` files, without which the help pages show only their images and the option subtitles fall back to built-in English labels (`make setup` already extracts the whole app, strings included).

## 3. Module layout (`port/`)

| File | Responsibility |
|---|---|
| `src/main.c` | args, SDL init, main loop, scene switching with the original's sine fade and loading screen (ids follow `SceneManager`: 1 Title, 2 Game, 3 MusicSelect, 4 PV view, 5 RESULTS, 6 Options, 7 PV select, 8 Result, 9 Ending, 10 Records, 14 Ending movie) |
| `src/gamedata.c` | parse `gamedata.txt` |
| `src/bplist.c` | bplist00 reader (atlases) |
| `src/atlas.c` | PNG textures (SDL_image) + atlas frame lookup + sprite drawing |
| `src/ipa.c` | in-app IPA importer: zip central directory, inflate + CRC-32, app-version check, selective extraction, progress callback |
| `src/android.c` | Android only: JNI glue for the native pages over the game (import screen, DLC page: `android/.../ImportPage.java`, `DlcPage.java`) |
| `src/mfx.c` | game tables read from the user's `MikuFlick2` binary (shared by `tools/mfextract` and `ipa.c`) |
| `src/usm.c` | USM chunk walker + `@UTF` reader → cue list |
| `src/media.c` | own ADX + IMA4 decoders: whole-file audio decode to S16 stereo 44.1 kHz (per stream), streaming MPEG-1 video (pl_mpeg, `src/pl_mpeg_impl.c`) |
| `src/mixer.c` | SDL audio callback: music (vocal + backing with separate gains), SFX voices, audio clock |
| `src/game.c` | **exact port** of `NoteManager` / `NoteNormal` / `NoteInterlude` / cue handlers / judgement / scoring / tension gauge, plus gameplay rendering |
| `src/ui.c` | title, music select, PV select, result and options screens, bitmap font |
| `src/pvview.c` | PV playback overlay (`WindowPVView`): swipes, arrows, pause menu, lyrics, in-place reloads |
| `src/flickresult.c` | FLICK RESULT page of the result screen (per-key boards), shared with RECORDS |
| `src/help.c` | `Localizable.strings` lookup, OPTIONS → HELP pages (`WindowHelpText`) and localized option titles |
| `src/ending.c` | credits roll (`WindowEnding`) and the ending movie (`ScenePreEnding`) |
| `src/records.c` | title crown → RESULTS (`WindowScore`) → RECORDS (`WindowRecord`), extended record save lines |
| `src/text.c` | SDL_ttf text drawing (M+ font, read into memory once: a compressed APK asset is very slow to seek) with the bitmap-font fallback |
| `src/save.c` | high scores / clear flags, options, per-song input timing |
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
| pause menu INPUT TIMING (drag 10 points per step) | drag the bar (20 px per step), or `Left`/`Right` while paused |
| PV view swipe (> 160 points) | horizontal mouse/touch drag of more than 320 px, or `Left`/`Right` |
| — (touch only) | keyboard equivalents on every menu screen (see `port/README.md`) |

Touches in the original are UIKit points (320×480); the port converts its 640×960 logical coordinates (× 0.5) wherever the original compares distances or areas in points (re_notes §5.7).

## 6. Deliberate deviations

* No network features except the DLC page's pack downloads (built-in archive.org link, or one the player pastes) (no shop, Twitter, Game Center, ads, info feed) and no in-app purchases. On the RESULTS screen (title crown), LEADERBOARD and ACHIEVEMENTS switch to the screen's dormant "no function" mode with port text ("Game Center is not available."); the original's Japanese message is game text and is not copied.
* Replay recording (REPLAY on the result screen), Twitter, the collection, the PV playlist editor, the help tutorial-movie button and the help scroll handle are not ported. The options screen stores its settings in the save file; command-line flags (`--offset`, `--volume`, `--se-volume`, `--no-fail-sound`) override them for a session (`--offset` is then not saved).
* **Everything is unlocked** from the start. So the ENDING button on OPTIONS → CREDITS is always shown, while the original's first-clear trigger (all of songs 0–8 cleared on Normal while song 9 is locked → credits → ending movie → 初音ミクの激唱 Extreme) can never fire; the movie tail is implemented and reachable with `--start-scene ending|movie`. The credits roll and movie can't be skipped, as in the original.
* **INPUT TIMING** is in the pause menu, per song, −10…+10, as in the original (an earlier port version had one global value on the DISPLAY page; old saves are migrated to every song). `Left`/`Right` while paused also step it.
* **Fonts.** All text uses the bundled M+ 1p Bold instead of the iOS system font (Hiragino/Helvetica, regular weight in some places), condensed to 0.93 for Latin text; help and licence lines are condensed to fit 512 px rather than reflowed.
* **Help language** follows the system locale like the original (first preferred language "ja" → Japanese, else English); `MIKUFLICK_LANG=ja|en` overrides it. The rest of the port's own UI text stays English, so a Japanese system shows Japanese help and subtitles next to English menus.
* **SHUFFLE** (PV select): the original reads it only in PLAYLIST mode. Without the playlist editor the port keeps it available: at the end of a PV it picks a random song (`rand() % nsongs`, may repeat) and takes priority over LOOP. Swipes stay sequential.
* **PV view keys:** `Esc` / Android Back pause the PV (RESUME / RETRY / MUSIC SELECT) instead of quitting to PV select; `Left`/`Right` stand for swipes.
* **Quick clicks:** a mouse click or trackpad tap can press and release within one frame, so flick keys, the pause button and menu buttons keep their pressed look for at least 100 ms after the press. Judgement timing is unaffected.
* **macOS trackpad fingers** (`SDL_TOUCH_DEVICE_INDIRECT_RELATIVE`) report pad positions, not screen positions; they are ignored (the mouse events still work), so they no longer press phantom keys.
* **Scene fades:** menus keep ticking during a fade-in (the original freezes them); setup is synchronous behind a full-alpha loading frame.
* **Pause button cut-off:** the original refuses the pause once the movie's last frame is reached; the port refuses it in the last 2 s.
* **RECORDS:** lists every song in music-select order with all 5 difficulties; the touch slop before arrows hide is 4 px; the background cubes are not drawn; stars are skipped on a Break the Limit page (the original tests the difficulty last played).
* Keyboard equivalents exist for every touch gesture (see `port/README.md`); the original is touch only.
* **Movie deblocking:** the movies are 320×480 MPEG-1 at a low bit rate, drawn 2× or more, so the original shows 8×8 block tiles in smooth areas. The port runs a deblocking post-filter on each shown frame (`media.c`: at each block edge with smooth sides the jump is spread as a linear ramp over 8 pixels, thresholds from the picture's quantizer, which a small pl_mpeg patch reports). `MF_NO_DEBLOCK=1` shows the frames as decoded.
* Lyric-line telops (op 3) are only shown in PV mode in the original, so the core game does not need a text font.
* Menu layouts and timings are taken from the binary (re_notes §6); the remaining visual differences are listed in re_notes §6.9.

## 7. Status and verification (milestone 3)

Implemented in `port/`: boot logos, title, music select (art, logo, stars, preview loop), the full rhythm game (video, both audio tracks, cue-driven notes, judgement, scoring, combo, climax bonus, interludes, tension gauge with vocal fade, game over, pause with INPUT TIMING), result screen with rank and the FLICK RESULT page, RESULTS / RECORDS, OPTIONS with the original HELP text and the ENDING credits roll and movie, the PV viewer with swipes and its pause menu, and save file.

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
| magnet Extreme `--autoplay` and `--autoplay-input` after each of the 2026-11-27 UI changes (PV view, FLICK RESULT, ending, records, flick board, HUD; desktop build, dummy SDL drivers) | unchanged | every run 115,616, stage 54,116, combo bonus 61,500, max combo 172, 172 COOL, 77/77 |
| FLICK RESULT counters, magnet Extreme `--autoplay` | per-key totals sum to the 172 notes, all 100 % | 36+13+17+34+21+14+9+5+14+9 = 172, all boards green at 100 % |
| RECORDS save: autoplay from an old-format save (`rec` + `timing` lines only) | new `recx` / `flk` lines written, old lines kept | `recx 6 3 172 54116 61500 77 1 …`, 10 `flk` lines summing to 172; `timing`, `opt` and other `rec` lines kept; an old-format save loads with the new fields 0 |
| `mfextract` with the ending table | 76 `ending` lines, every other line unchanged | 76 lines; the rest identical to the previous `gamedata.txt`; with an old `gamedata.txt` the ENDING button is hidden and a log line asks to rerun setup |
| OPTIONS → CREDITS → ENDING round trip, headless | roll ≈ 100.7 s, hold, BGM fade, back to CREDITS through the loading screen | ran end to end in one ~117 s run; `--start-scene movie` starts 激唱 Extreme ≈ 9.5 s in |
| PV view, scripted mouse drags | tap does nothing, > 320 px drag changes PV, pause button, RESUME, RETRY fade-in, MUSIC SELECT | as expected (screenshots); a natural movie end with LOOP/SHUFFLE was not run headless (same code path as a swipe reload) |
| DLC (branch `dlc-support`, on upstream main, desktop build) | DLC records keyed by movie; built-in songs unchanged; lines of songs not installed kept | カンタレラ Extreme autoplay 117,005 PERFECT, saved as `rec kantarera …` / `recx kantarera …` and 10 `flk` lines; `rec 0` / `timing 0` and an unknown-movie line kept; magnet Extreme `--autoplay` / `--autoplay-input` still 115,616 |
| Movie deblocking, カンタレラ 170 s / 183 s frames with and without `MF_NO_DEBLOCK` | tiles in skin and floor gone, edges and texture kept | as expected (block steps of the face close-up removed; stair-stepped hair edges remain, that detail is not in the stream) |

Caveat: if three instances share the CPU under the software renderer, frame times exceed the 33 ms tick and synthetic presses land late. The game samples input once per frame (the original runs at 30 fps), so play quality depends on the frame rate. With a GPU renderer and vsync this is not an issue.

Touch input was checked on a phone (Android build, §9): menus, the flick keys' touch areas and the Back button. Not yet measured: audio output latency on real devices (PipeWire/Pulse on Linux, OpenSL ES/AAudio on phones; it is compensated only by one SDL buffer, and the pause menu's per-song INPUT TIMING / `--offset` is the calibration knob), and long play sessions. Not yet checked on a device: the new PV swipes, RECORDS swipe paging (the `--script` drag delivers down/move/up in one frame, so paging was tested with the arrows and keys), the quick-click pressed look of the pause menu, and the in-app import of the two `Localizable.strings` files on Android.

## 8. Build

Dependencies (Debian names): `libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev`, `gcc`, `make`. FFmpeg is no longer needed (see §9).
```
make -C port            # builds port/mikuflick
make -C port setup IPA=…  # one-time asset extraction + table export
./port/mikuflick [--app DIR] [--data DIR]
```

Environment variables: `MIKUFLICK_AUDIO_BUFFER=N` (audio buffer in frames, a power of two from 256 to 8192; default 1024 on desktop, 512 on Android; use a larger value if desktop output crackles under load, the audio clock accounts for the granted size) and `MIKUFLICK_LANG=ja|en` (help/subtitle language).

Without `unzip`, use the built-in importer instead of `setup`:
```
./port/mikuflick --import-ipa your.ipa --import-to imported
./port/mikuflick --app imported/MikuFlick2.app --gamedata imported/gamedata.txt
```

Android: see `android/README.md` (`make -C android toolchain licenses sdk apk install run`; the game data is imported in the app).

iOS: see `ios/README.md` (`make -C ios sim` for Simulator, `make -C ios device` for device; requires Xcode 27.2+ on macOS with iOS 26.0 SDK).

## 9. Android port and FFmpeg removal (2026-11-24)

The Android build (`android/`, see `android/README.md`) compiles the same `port/src` files with the NDK (CMake), next to SDL2/SDL2_image/SDL2_ttf built from source, inside SDL's Java activity. To make that build small and self-contained, FFmpeg was replaced on both platforms:

* `usm.c` also reads the stream payloads (`@SFV`/`@SFA`, data type 0). Concatenating the video payloads of `magnet.usm` gives 3,590 pictures and exactly the 71,945,020 bytes the CRID directory records for the source `.avi`.
* `media.c` decodes ADX (encoding 3, coefficients from the high-pass cutoff, as in FFmpeg's `adpcm_adx`) and Apple IMA4 in CAF, and feeds the MPEG-1 video to **pl_mpeg**.
* The USM video uses an **11-bit intra DC precision** (`VIDEO_HDRINFO.mpeg_dcprec = 11`; the stream's user data says `TMPGEXS … IDCPREC … 3`). That is an MPEG-2 feature inside an MPEG-1 stream. FFmpeg detects the TMPGEnc marker; stock pl_mpeg does not and produces block garbage, so the vendored copy is patched (DC size codes up to 11 bits, predictor reset `128 << p`, DC scale `8 >> p`) and `media.c` sets the precision from the USM header.
* Android specifics in `main.c`: data paths from `SDL_AndroidGetExternalStoragePath()` / internal storage, an on-screen message when data is missing, the first finger turned into pointer events for menus (the board keeps raw multi-touch), Back → Esc, pause and save on `SDL_APP_WILLENTERBACKGROUND`. `game.c` judges an input at the song time of its event timestamp rather than at the last frame update.
* Screen fill (`view.c`): scenes render into a 640×960 target that is shown centred. The bars get a blurred, darkened copy of the video (two linear downscales to 40×60); on phones (iOS, Android) they stay black during a song and the menu blur is dimmer. The targets are RGBA32 because SDL 2.30's GLES2 renderer colour-swaps RGBA textures drawn into a BGRA (ARGB8888) target but not its YUV shader, which turned the movie R↔B. Mouse and touch coordinates are mapped back through the same rectangle.
* Touch areas fix: the `e_FlickBoardPictTbl` positions are the keys' top-left corners. The port had hit-tested them as centres, so every touch registered half a key below and to the right of the drawn key (reported on a phone as "the input lands lower than where I touch"). Keys and the interlude panel now use the drawn rectangles.
* In-app IPA import (`ipa.c`, `mfx.c`): Android has no `make setup`, so on first start the app shows an import screen. It is a native page of Android views over the game (`android.c`, `ImportPage.java`), like the UIKit screen on iOS; while it is up the game loop draws nothing, so the page animates at the display rate. The system file picker (`MikuFlickActivity.pickIpa`) returns a file descriptor; the importer checks the game version on the binary before extracting about 980 MB (with a free-space check), writes into `MikuFlick2.app.import/` and swaps it in, writing `gamedata.txt` last. Verified on Linux against `unzip` + `mfextract` (identical output); bad inputs (not a zip, a zip without an iOS app, a truncated IPA) are refused with a message and leave nothing behind.
