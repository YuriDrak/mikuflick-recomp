# Reverse-engineering notes: Miku Flick/02 (v1.1.5, armv7) — for Linux, Android, iOS

Tags: **[V]** verified (evidence given), **[H]** hypothesis, **[D]** derived from decompilation (see `work/decomp/`).
Addresses are VM addresses in `MikuFlick2` (image base 0x4000 = file offset 0). A trailing `+1` on a pointer means Thumb.

## 0. Tooling used

| Tool | Version / source | Purpose |
|---|---|---|
| `tools/machoinfo` (own C) | — | load commands, `-s` symbols with addresses, `-w` read words at VM address, `-x` hexdump |
| `tools/usmdump` (own C) | — | CRI USM chunk walker, `@UTF` table dumper, `-c` cue-point TSV export |
| `tools/bplist2json` (own C) | — | bplist00 → JSON (atlases, Localizable.strings, nibs, Info.plist) |
| `tools/mfextract` (own C) | — | game tables from `MikuFlick2` → `gamedata.txt` (the same reader as `port/src/mfx.c`) |
| Ghidra | 11.2.1 PUBLIC 20241105, sha256 `ce4db511…b1bc461c`, OpenJDK 21.0.13 | headless analysis and decompile (`tools/ghidra_decompile.sh`, `tools/ghidra_scripts/DecompileMatching.java`) |
| ffmpeg/ffprobe | system | codec identification, later asset conversion |

Outputs: `work/symbols_addr.txt` (7,949 defined symbols with addresses), `work/charts/*.tsv`, `work/plists/*.json`, `work/decomp/*.c`.

## 1. CRI USM container [V]

Every chunk is `sig[4] | u32be size | u8 ? | u8 payload_off | u16be padding | u8 chno | u8 ? | u8 ? | u8 data_type | u32 frame_time | u32 frame_rate`. The payload is `[chunk+8+payload_off, chunk+8+size-padding)`. `usmdump` walks all 0x56d57c0 bytes of `magnet.usm` with no resync errors.

`magnet.usm` chunk inventory (`build/usmdump magnet.usm`):

| sig | ch | type | count | meaning |
|---|---|---|---|---|
| CRID | 1 | 1 | 1 | `CRIUSF_DIR_STREAM` directory (source file names) |
| @SFV | 0 | 1 / 3 / 0 | 1 / 1 / 3590 | `VIDEO_HDRINFO`, `VIDEO_SEEKINFO` (296 rows), MPEG-1 data |
| @SFA | 0, 1 | 1 / 0 | 1 / 5577 each | `AUDIO_HDRINFO`, ADX data (two identical-size channels) |
| @CUE | 0 | 1 / 3 | 1 / 1 | `CUEPOINT_HDRINFO` (`time_unit`=1000), `CUEPOINT_INFO` (462 rows) |

Audio channel 0/1 correspond to the source files `pv_NNNvo2.aif` / `pv_NNNok2.aif` from the CRID directory. Which channel is the vocal mix and which is the backing track is **[H]** until `MikuFlickCriManager` / the fail-sound option has been decompiled.

`@UTF` tables are big-endian. `rows_off` is a u16 at +10, `strings_off`/`data_off`/`name` are u32 at +12/+16/+20, then `ncols` u16, `row_width` u16, `nrows` u32, followed by column descriptors `u8 flags, u32 name`. Storage is in the high nibble (0x10 zero, 0x30 constant, 0x50 per-row) and the type in the low nibble (0–7 ints, 8 f32, 9 f64, 0xA string, 0xB data). All offsets are relative to table+8.

## 2. Chart (beatmap) format [V]

**Location.** Each song's `.usm` has a `CUEPOINT_INFO` table `{name:data, time:u64, cue_type:s32, parameter:data}`, with `time` in units of 1/`time_unit` s (1000 → ms). `name` is always `"note"` and `cue_type` is always 0. All semantics live in `parameter`, a UTF-8 string. Dumps: `work/charts/<song>.tsv` (`build/usmdump -c`).

| Song file | pv id | cues | Notes (opcode 0) |
|---|---|---|---|
| cloverclub | 061 | 624 | 466 |
| hajimete_no_oto | 065 | 430 | 357 |
| hatsune_miku_no_gekisyou | 042 | 824 | 697 |
| Just_be_friends | 066 | 734 | 589 |
| koi_wa_sensou | 001 | 313 | 241 |
| magnet | 044 | 462 | 333 |
| migikata_no_tyou | 056 | 660 | 530 |
| promise | 062 | 457 | 312 |
| roshin_yuukai | 054 | 546 | 461 |
| tajyuumirai_no_quartet | 102 | 472 | 273 |
| ura_omote_lovers | 082 | 1296 | 1021 |
| ending | — | 0 | (movie only) |

**Dispatch.** The first character of `parameter` is a decimal opcode used as an index into `s_tblCuePointFunc` at `0x140c14` (`__DATA,__const`). The table, read with `machoinfo -w MikuFlick2 140c14 9`:

| op | entry | handler | payload |
|---|---|---|---|
| 0 | 0x29b55 | `CuePointFunc_Telop(NSString*)` | `<kana><digits>`: one flick note (see below) |
| 1 | 0x29bd5 | `CuePointFunc_FadeIn` | none. Starts a lyric section |
| 2 | 0x29bfd | `CuePointFunc_FadeOut` | none. Ends a lyric section |
| 3 | 0x29c25 | `CuePointFunc_Lyrics` | full lyric line text (displayed) |
| 4 | 0x29ca5 | `CuePointFunc_SmallTelop` | small kana (ゃ ゅ ょ っ ッ ェ …) shown with the preceding note |
| 5 | 0x29d39 | `CuePointFunc_Interlude` | `<digits>`: instrumental-section note |
| 6 | 0x29d95 | `CuePointFunc_Crimax` | `<digits>`: climax ("Crimax") marker |
| 7 | 0x29e11 | `CuePointFunc_SetDelay` | integer, e.g. `2600` (at t=0) |
| 8 | 0x29e85 | `CuePointFunc_SetBPM` | integer BPM, e.g. `108` (at t=0) |

Evidence for op 8 = BPM: magnet's note spacing is 277.8 ms, which is an eighth note at 108 BPM, and its header has `8108`. The other songs' BPM values (100–200) are in the same musical range. The unit and meaning of the SetDelay value (approach time in ms?) are **[H]**.

**Note digits (op 0 / 5 / 6) [D].** The handlers read exactly one digit, at index `GetDifficulty()+2` (op 0) or `GetDifficulty()+1` (ops 5/6). Difficulties are 0 Easy, 1 Normal, 2 Hard, 3 Extreme, 4 "Break the Limit" (help text `HELP_TEXT_29..37_7`). Digits past index 4 are never read, so the 6–10-digit payloads in four older charts are inert padding.
* op 0 → `s_tblTelopType[digit]` @0x140c44 = `[TelopType_Disable, TelopType_Enable ×5]`. Both handlers call `+[NoteManager AddNote:0 :GetNoteCharIdx(kana) :enabled :0]`, so digit 0 still spawns the character as a **non-playable (display-only) note**. Digits 1–5 all behave the same at runtime. The expected key and direction come from the kana tables (§5.3), not from the digit.
* op 5 → `s_tblInterludeType[digit]` @0x140c38 = `[None, Normal, FadeOut]`. Normal calls `WindowManager StartInterludeMode` + `AddNote:1 :0 :1 :0` (a `NoteInterlude` object). FadeOut calls `EndInterludeMode`.
* op 6: a non-zero digit calls `[[NoteManager GetLastCrimaxEnableNote] setCrimaxMode:1]`, marking the latest note as a climax note.
* op 4 adds a disabled note for the small kana and calls `addOption:` on it.

As a data observation, the non-zero digit in op 0 notes always equals the kana's flick direction + 1. This is redundant authoring data that matches the binary's tables exactly:

| digit | direction | vowel | examples from magnet |
|---|---|---|---|
| 1 | tap (center) | a | か が ま は た わ |
| 2 | up | u | う る つ ん (ん is "up" on the わ key) |
| 3 | right | e | え ね て せ |
| 4 | down | o | こ の と も ろ |
| 5 | left | i | い し に ひ じ, を (を is "left" on the わ key) |

Overall digit counts across all 5-digit notes: 1→2442, 2→1949, 3→1498, 4→2061, 5→2587.

**Delay [D].** `SetDelay` stores the value via `MusicData SetNoteDelay:` (ms). The note spawn logic (§5.2) makes the judge instant **cue_time + NoteDelay**. In every chart NoteDelay equals `GetCenterTime(BPM)` in ms (magnet: 108 BPM → 2.6 s, delay 2600), so cues are authored one approach time ahead of the beat. SetDelay values range 1400–2600 and BPM values 100–200.

## 3. Texture atlases [V]

`*.plist` atlases are cocos2d/Zwoptex "format 0"-style dictionaries: `frames{ "<name>.png": {x, y, width, height, offsetX, offsetY, originalWidth, originalHeight} }` plus `metadata`. None are rotated or trimmed so far (offset 0). Examples: `game_tex_01` (86 frames: `combonumber_00..12`, `evaluation_00..04`, `flickguide_*`, `guidepanel_*`, `charactercircle`), `game_tex_02` (68 `fgcharacter_ori_NN` glyph tiles), `game_effect_01` (`inputguide_*`, `inputperfect`, `ripple`, `fireworks_mik_*`), `UI_tex_02` (result screen, `clearrank_*`, `newrecord`).

## 4. Localisation [V]

`{en,ja}.lproj/Localizable.strings` are bplists with about 1,000 keys each (`HELP_TEXT_*`, licence text `HELP_TEXT_LIC*`, UI messages such as `SAVE_CURRENT_DATA`). They are converted in `work/plists/`.

* en has 540 `HELP_TEXT_*` keys (`HELP_TEXT_%02d_%d`, page then line) and 339 licence lines `HELP_TEXT_LIC000..338`. `HELP_TITLE_00..08` are the page titles; `HELP_TITLE_09` is empty. The English help lines are indented with U+3000.
* Option subtitles: `OPTION_TITLE_00..04` and `OPTION_TITLE_01_OPT` (second half of the AUDIO line); the RESULTS screen uses `SCORE_MSG`.
* The language follows `IsJapanese()`: the first entry of `[NSLocale preferredLanguages]` is "ja" → ja.lproj, anything else → en.lproj.
* The ending credits are **not** in the strings files; they are a CFString table in the binary (`s_tbl_Ending`, §6.6).

## 5. Code subsystems [D]

Decompiled output: `work/decomp/` (about 950 functions, `_index.tsv` lists address/size/name). The Ghidra project in `work/ghidra_proj/` can be reopened in the GUI. Ghidra mis-renders ARM VFP `vmov.f32 #imm` as `SIMDExpandImmediate(0,0xf,imm8)`. Decoded by hand: `0x3e`=30.0, `0x24`=10.0, `0xe0`=−0.5, `0x08`=3.0, `0x78`=1.5, `0x00`=2.0 (Ghidra prints the last one as `0.0`).

### 5.1 Clock and cue delivery
* `+[MikuFlickCriManager Exec]` @0x28eb4: `PlayCnt = (int)ceilf(CriManager::GetPlayTime() * 30.0f)`. **All gameplay timing is in 30 Hz ticks derived from the CRI movie/audio playback time.** There is no separate wall clock, so sync is inherent.
* CRI calls `CuePointCallback(void*, CriManaPlayerTag*, CriManaEventPoint*)` @0x21b0c as playback passes each cue. It wraps the cue's parameter string in an NSString and forwards it to `CuePointCallback(NSString*)` @0x28e70, which does `s_tblCuePointFunc[[[s substringToIndex:1] intValue]](s)`.

### 5.2 Note lifecycle: `NoteNormal` (`initWithType:::` @0x21dc0, `exec` @0x22160)
With `T = +[NoteManager GetCenterTime:BPM]` in seconds (@0xe57c):

| BPM | <80 | <90 | <100 | <110 | <120 | <130 | <140 | <150 | <160 | <170 | <180 | <190 | <200 | <210 | <220 | <230 | <240 | ≥240 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| T (s) | 3.0 | 2.9 | 2.8 | 2.6 | 2.4 | 2.2 | 2.1 | 2.0 | 1.9 | 1.8 | 1.7 | 1.6 | 1.5 | 1.4 | 1.3 | 1.2 | 1.1 | 1.0 |

* `m_SpdX = -642.0 / T` (px/s). Start position is (740, `DisplayManager.GetBottomOfs + 442`) in 640-wide (retina) space. The note travels left and reaches x = 98 after T seconds.
* `m_JustFrame = (int)(T*30)`, then `+= (int)((39.0 / (|m_SpdX| / m_JustFrame)) / 10.0 * MusicData.GetInputTiming)`. This is the user's calibration offset: **INPUT TIMING** in the pause menu (`TouchInputTiming`, §6.2), stored **per song** in `MusicData`, range −10…+10.
* `m_BaseTime = PlayCnt_at_cue + (int)(floorf(NoteDelay/1000*30) - T*30)`, and `m_Cnt = PlayCnt - m_BaseTime` each tick. The note is "just" when `m_Cnt == m_JustFrame`, i.e. at cue_time + NoteDelay.
* Inside the judge frame: `JustFrame-11 <= m_Cnt < JustFrame+12+s_tblDifficultEndTimeOfs[diff]`, with `s_tblDifficultEndTimeOfs` @0x1392fc = `{0,0,0,0,-2}`. So the window is −11…+11 ticks (about −367…+367 ms), and −11…+9 on Break the Limit.
* Past the window with no hit, the note deactivates as a miss: `AddTmpNoteResult:1`, `AddTensionGauge:1`, `ResetCombo`, effect 4. On difficulty 4 nothing happens.

### 5.3 Flick board model (`e_FlickBoard*Tbl`, indexed by `char - U+3041`, 190 entries, exported to `work/kana_board.tsv`)
* `e_FlickBoardTypeTbl` @0x13a454 = **key** 0–9 (あ か さ た な は ま や ら わ rows of the 12-key pad)
* `e_FlickBoardResultTbl` @0x13a74c = **direction** 0 tap, 1 up, 2 right, 3 down, 4 left
* `e_FlickBoardSubTypeTbl` @0x139c80 = board sub-mode (11 = small kana, 12 = dakuten, 17 = small ya-row, …) passed to `NoteManager SetFlickBoardSubType:` so the on-screen keyboard shows the right variant **[H]** on exact meaning
* `e_CharPictTbl` @0x139fde (6-byte stride) gives the glyph texture index. `s_HiraganaFirstIdx` (bss @0x163070) is inferred to be 0x3041 because the tables are coherent with that base (あ→key 0/dir 0, か→1/0, を→9/4, ん→9/1).

### 5.4 Judgement: `-[NoteNormal checkResult::::]` @0x22e10
Inputs: `board` = key the player pressed, `dir` = flick direction, plus the note's `m_TouchDownFrame` and `m_TouchUpFrame`.
1. Wrong key (`board != m_BoardType`) → grade 2, `wrong=1`.
2. Otherwise `cap = (dir == m_FlickResult) ? 5 : 3` and `wrong = (dir != m_FlickResult)`. For both `dd = JustFrame - TouchDownFrame` and `du = JustFrame - TouchUpFrame`, the tick grade is `Front[d]` for 0≤d≤10, `Back[-d]` for −11≤d<0, and 0 outside. The tables are at @0x139488 and @0x1394b4:
   `Front = {5,5,5,4,4,4,4,3,3,2,2}`, `Back = {5,5,5,5,4,4,4,4,3,3,2,2}`.
   The combination rule (exact port of the branch structure): if `g(dd)!=0`, take `min(g(dd), g(du))` (0 if `g(du)==0`), raising it to 3 if it is <3 and the press was early (`dd>0`). If `g(dd)==0` and `dd>0`, use `min(g(du),3)`. Otherwise 0. Finally `grade = min(grade, cap)`.
3. On difficulty 4, grade 2 becomes 0 and the tension gauge is untouched. Otherwise `AddTensionGauge:grade` (`s_tblTensionGauge` @0x138f4c: `{0,-10,-5,0,+2,+2}`).
4. A grade ≥4 with the right direction gives `AddCombo` and `AddTmpComboScore: min(500, 50*((combo+5)/10))`. A climax note with grade 5 and combo ≥100 adds +200. Anything else resets combo (except on diff 4).
5. `AddTmpStageScore: s_tblAddScore[grade + 6*wrong]`, with `s_tblAddScore` @0x1394e4 = `{0,0,30,50,150,300, 0,0,30,50,150,250}`.
6. SFX (switch @0x231ee, on the **grade only**): grade 2 → sound 2 (unless "fail sound" is off), 3 → 0x12, 4 → 1, 5 → 1 (0 with the climax bonus); grades 0/1 play nothing. The effect is `AddEffect2D(4, &grade)` (@0x232a2). A wrong flick therefore looks and sounds exactly like its capped grade (SAFE, or SAD on the wrong key); only the score-table row (+6) differs.
7. A grade ≥4 also counts a per-key/per-direction success (`StatusData GetTmpFlickSuccessNum`, @0x232a8) for the FLICK RESULT page (§6.5).

Grade names [V]: 5 Cool!!, 4 Fine!, 3 Safe, 2 Sad, 1 Worst. These come from the `evaluation_00..04` artwork in `game_tex_01.png` and the help text "You can still get a SAFE if you don't flick". The final rank is in §5.10.

### 5.5 Scene state machine: `+[SceneManager ChangeScene]` @0x10f10
The scene id lives in `DAT_001632b4` (next), `…b8` (current) and `…bc` (previous). Each id maps to a singleton `+[SceneX GetInstance]`:

| id | scene | id | scene |
|---|---|---|---|
| 0 | SceneBoot **[H]** (initial) | 7 | ScenePVSelect |
| 1 | SceneTitle | 8 | SceneResult |
| 2 | **SceneGame** | 9 | SceneEnding |
| 3 | SceneMusicSelect | 10 | SceneRecord |
| 4 | ScenePVView | 0xc | SceneCollection |
| 5 | SceneScore (RESULTS menu) | 0xd | SceneAdditionalPackage (shop) |
| 6 | SceneOption | 0xe | ScenePreEnding (ending movie) |

Leaving a scene clears `TexTextManager` slots, touches and windows, then `EffectManager Clear` and `SystemTexture ReleaseTelopTex`. Touches are routed `SceneManager touches{Began,Moved,Ended,Cancelled}::::` → current scene.

**Scene changes and the loading screen [D]:**

* `NextScene:` @0x10db8 starts a fade for every target except boot (0) and 11.
* `SetFadeChange` @0x11d08 (mode 2): 30 frames, starting at cnt 0 (or 15 if the screen is already black). `Exec` @0x10620: `alpha = sin(π·cnt/frames)`, `cnt++` per frame. In mode 2 the scene's `exec` is **not** called during either half; at `cnt == frames/2` the alpha is forced to 1 and `ChangeScene` runs; at `cnt == frames` `s_FadeModeValid` is cleared. While `s_Loading` is set (`WindowBase fncLoadTexture` on a background thread) nothing runs.
* `SetFade:0` (mode 0) starts at cnt 30 of 60: a 1 s fade-in during which the scene does run.
* `Rendering` @0x10a94 draws the scene, then a full-screen rect `GetColor(alpha,0,0,0)` (white for boot → title), then, if `s_DispLoading`, `RenderLoading` with the same alpha.
* `s_DispLoading` is set in `NextScene:` for targets in mask 0x24e8 (3 music select, 5 RESULTS, 6 options, 7 PV select, 10 records, 13 shop); for the title (1) only when the previous scene is in 0x20e8 (3, 5, 6, 7, 13). Game 2, PV view 4, result 8, ending 9/14 and boot get a plain black fade. Going into a song is a fade to black, then the game's own `m_Fade` (§6.2) fades the video in.
* `RenderLoading` @0x10738 (bg_tex_07), in draw order: a black band 88 px tall (tall-screen letterbox); `bg_loading` at (0,0); `linemove` (821×2) at y 840 (928 tall) drawn twice, at x = c·w/45 − w (z −1) and c·w/45 (z −1.2), c = (c+1) % 45 per frame; `loadmiku_00` at (385,422) (510 tall), z −5; `loadmiku_01` at (185,507) (595 tall), 1 px taller than its frame, z −5.1.
* Port (`main.c` transition): the same 30-frame sine fade on a wall-clock 30 Hz counter, with no input meanwhile. Setup is synchronous, so a full-alpha loading frame is presented just before the blocking scene setup. A new game is held black until the fade-in ends. **Deviation:** menus keep ticking during the fade-in (the original freezes them), because `ui.c`'s clock would otherwise catch up the half second at once.

### 5.6 Audio tracks [D]
`Load(NSString*)` @0x2176c calls `criManaPlayer_SetSubAudioTrack(player, 1)`, so **both** ADX tracks play at once: track 0 (`pv_NNNvo2`, vocals) is the CRI *main* audio and track 1 (`pv_NNNok2`, backing) is *sub* audio. `AddTensionGauge` drives only `SetMainAudioVolume`, which means the vocals fade as the tension drops. PV mode with KARAOKE (`GetPVModeFlg:4`) mutes main.

### 5.7 Touch → key/direction (`TouchFlickBoard`, `WindowGameWindow init` @0x3939c) [D]
* **Touch space.** Every touch the game sees is in **UIKit points (320×480)**: `locationInView` then `CloudMath::AdjustPoint` @0x578a8, which only halves on iPad HD. Every `TouchBase` collision rect is `pos × 0.5` with size `frame / 2` (e.g. `TouchBack init` @0x6deb4, `TouchSoundVolume` @0x76f72), so hit boxes line up with the 640×960 art, but **every distance and scroll constant is in points** (×2 in 640×960 px): flick tap radius 16 pt = 32 px, PV swipe 160 pt = 320 px, INPUT TIMING step 10 pt = 20 px, volume drag dx/440 pt, music-select scroll band y 140–400 pt = 280–800 px. The port feeds those code paths logical px × 0.5.
* The keys are created by `WindowGameWindow init` → `WindowBase AddNewTouch:0 :k :pos` for k = 0…9. The per-key data come from `e_FlickBoardPictTbl` @0x13c5b8 = `{u16 srcX, srcY, posX, posY}`.
  Positions in 640×960 are the keys' **top-left corners** (UIKit rect origins; the board is drawn and hit-tested from them): あ(129,530) か(256,530) さ(383,530) / た(129,638) な(256,638) は(383,638) / ま(129,746) や(256,746) ら(383,746) / わ(256,854). Cells are 127×108, so the board covers x 129–510, y 530–962. (An earlier reading took them as centres, which shifted every touch area half a key up and left.)
  Key-face tiles are in `game_tex_02.png`. src = idle face. The lit face of key k is at tile index 9−k in a 4-wide grid of 127×108 cells.
* Each of the ten boards tracks its own touch (`m_TouchDownIdx`), so several keys can be held at once. `touchDown:` @0x6fcb8 stores the begin point, sets `m_TouchDown = 1`, `m_FlickResult = 0`, and broadcasts `NoteManager setTouchDown` (each live note sets `m_TouchDownFrame = m_Cnt`). `touchMoved:` @0x6fd70 stores the end point, broadcasts `setTouchUp` and calls `checkFlick` on every move (no latch). `touchUp:` @0x6fddc does the same, then calls `NoteManager checkResult: board :flick :0` (**judgement happens on release**), `EffectManager AddEffect2D:3` (EffectPulse, @0x6fec6) and `ReplayManager AddUnit…`.
* `checkFlick` @0x6ef60: `d = end − begin` in points. A distance below 16 pt (|d|² < 256) is a tap (0). Otherwise `CloudMath::Atan(−dy, dx) + π` (@0x6efc4) is compared with 0.785 / 2.355 / 3.925 / 5.495: four 90° sectors, each centred on an axis, giving 4 left, 3 down, 2 right, 1 up in increasing order. The codes match the kana tables: 1 up, 2 right, 3 down, 4 left. It **always returns 1**, so a release over a board always spawns the ripple, even while the keys are invisible (before cue 1, during interludes). The boards are never locked (`setLockStatus:` is only called by WindowPackMusic, WindowMusicSelect and TouchChangeAspect).
* Interlude mode (`setInterludeMode:` @0x6ff30 → `setInterludeRect:`): `StartInterludeMode` @0x3f4a0 sets it **only on board 0** (`GetFlickBoardTouch:0`). `setInterludeRect:1` @0x6ff50 makes board 0's collision rect the `flickguide_int_on` frame, 409×350 px with top-left ((640−409)/2, 265 pt + bottomOfs/2), i.e. x 115.5–524.5, y 530–880 px, which does not cover わ below y 880; boards 1–9 keep their rects (any board judges an interlude note). `EndInterludeMode` @0x3f53c only calls `setInterludeRect:0` (back to m_Pos/2, 63×54 pt). `m_InterludeMode` (ivar 0x8c) and `s_IsInterlude` are cleared together in `WindowManager exec` @0x3e34a–0x3e3b0, when the interlude alpha falls below 0.

### 5.8 Interlude notes (`NoteInterlude`) [D]
They spawn like normal notes but move at −688/T px/s, and their judge window is a plain `[just−11, just+12)` with no difficulty offset. A miss has no penalty. `checkResult` ignores the key and direction and uses the same Front/Back tables with its own combination rule (port: `check_interlude`). A grade ≥4 plays SE04, adds +1 stage score and counts the hit. When every interlude note of the song has been hit, it adds +39 and plays SE02 (@0x30216), with the SpScore popup (@0x30248). Every successful interlude spawns EffectInterlude (@0x30268, §6.2). During interlude mode board 0 becomes a single panel (`flickguide_int_*`, 409×350, drawn at (114,530), touch rect in §5.7). `-[NoteInterlude render]` @0x2fe28 draws `quaver_00` (active) or `quaver_03` and has no touch state.

### 5.9 Tension gauge and game over (`+[NoteManager AddTensionGauge:]` @0xe040) [D]
`gauge₀ = 128` (`NoteManager Reset`). `gauge = min(256, gauge + (128/(2·total) + 0.01)·tbl[grade])`. Main (vocal) volume is `min(1, gauge/128 + 0.35)·BGM`. Game over (→ `NextScene:8`) when `gauge ≤ 0` or `(COOL+FINE+SAFE+unplayed)/total < 0.5`. The NEON immediates decode as `vmov.i32 #0x43000000` = 128.0 and `#0x3c000000` = 1/128.

### 5.10 Final rank (`WindowResult loadTexture` @0x4ba80, `Render` @0x4e8a8) [D]
`r1 = (COOL+FINE+SAFE)/total`, `r2 = (COOL+FINE)/total`:
game over → 6 (E). r1 < 0.7 → 5 (D). all COOL → 0 (PERFECT). Otherwise r2 = 1 → 1 (S), r2 ≥ 0.95 → 2 (A), r2 ≥ 0.8 → 3 (B), else 4 (C).
Ranks 0–4 are "Clear" and 5–6 "Not Clear" (help text `HELP_TEXT_05..14_5`). Sprites: `clearrank_00..05` = S, A, B, C, D, E (`rank−1`), and rank 0 uses the special "Perfect!" art.

### 5.11 Built-in song table `_sTblDefaultMusicPackData` @0x14094c (0x24-byte rows) [D]
`{CFString* title, artworkAtlas, previewAdx, movieUsm; s32 bpm; s32 stars[4]}`. It holds 11 rows, read by `StatusData AddBlankMusicDataByMusicID:toMusicPack:`. ArtWorkIndex = idx % 3. Songs 0–2 start unlocked on Easy/Normal, only song 0's PV starts unlocked, and Break the Limit is always rated 9 stars. Only `music_00_01` has a `.plist`; `music_00_02..04` reuse its frame layout.

| # | title | movie | preview | BPM | ★ E/N/H/X |
|---|---|---|---|---|---|
| 0 | 恋は戦争 | koi_wa_sensou | pv_001_lp | 175 | 1/2/4/5 |
| 1 | ハジメテノオト | hajimete_no_oto | pv_065_lp | 100 | 1/2/4/6 |
| 2 | 炉心融解 | roshin_yuukai | pv_054_lp | 165 | 1/2/4/6 |
| 3 | 右肩の蝶 | migikata_no_tyou | pv_056_lp | 140 | 1/3/5/7 |
| 4 | Just Be Friends | Just_be_friends | pv_066_lp | 128 | 2/3/5/7 |
| 5 | クローバー・クラブ | cloverclub | pv_061_lp | 150 | 2/3/5/7 |
| 6 | magnet | magnet | pv_044_lp | 108 | 2/4/6/7 |
| 7 | Promise | promise | pv_062_lp | 138 | 2/4/6/7 |
| 8 | 裏表ラバーズ | ura_omote_lovers | pv_082_lp | 159 | 3/4/7/8 |
| 9 | 初音ミクの激唱 | hatsune_miku_no_gekisyou | pv_042_lp | 200 | 3/5/8/9 |
| 10 | 多重未来のカルテット | tajyuumirai_no_quartet | pv_102_lp | 195 | 1/2/4/5 |

### 5.12 Sounds (`_s_szSoundFile` @0x1618a8, `_s_bSoundLoop` @0x161920) [D]
IDs 0–14 = SE01–SE15, 15 MusicSelect, 16 J01_01, 17 SE01_01, 18 SE01_03, 19 SE02_01, 20 SE04_01, 21 SE05_01, 22–26 BGM01–05 (loop), 27 Na_SEGA_09_keep, 28 Na_Title_A_03_keep, 29 SE04v2 (loop). Calls are `SoundManager Play:` with this index. Every sound is played on touch **up**, never on touch down.

| ID | file | used by |
|---|---|---|
| 0 | SE01 | COOL on a climax bonus |
| 1 | SE02 | COOL/FINE (`NoteNormal checkResult` @0x23200); also after the last interlude (@0x30216) |
| 2 | SE03 | SAD, unless fail sound is off |
| 3 | SE04 | interlude hit (@0x3014a) |
| 5 | SE06 | `CreateDialog` (dialogs; not used by any ported screen) |
| 8 | SE09 | `TouchBack touchUp` @0x6ee74 (after `WindowManager TouchCallback`, only when released inside): Back buttons and the pause menu's RESUME; `WindowRecord` difficulty arrows when the value is already at an end |
| 10–13 | SE11–SE14 | `WindowResult exec` rank stamp: PERFECT → 13, S/A → 12, B/C/D → 11, E → 10 |
| 15 | MusicSelect | `WindowMusicSelect updateTitle::` @0x401da (every change of the centred cover); `WindowRecord` page swipe |
| 16 | J01_01 | `WindowUnlock` |
| 18 | SE01_03 | SAFE |
| 19 | SE02_01 | every `Touch*` `touchUp` whose super `touchUp` returns ≥ 0 (released inside): title menus (SHOP included) and touch-to-start, `TouchDifficulty`, `TouchModeChange`, `TouchMusicSelect` (PLAY / PLAY AGAIN), `TouchPVLoopMode`, `TouchPVProgram`, all option toggles and pages (`TouchCommonButton`, `TouchHandSide`, `TouchKeyBoardSelect`, `TouchPanelSelect`, `TouchFlowerType`, `TouchPauseEnable`, `TouchFailSoundEnable`, `TouchChangeAspect`, `TouchHelpPage`, `TouchHelpScroll`), `TouchGotoShop`/`GotoOption`/`GotoScore`, `TouchResultChange`, `TouchRetry` (RETRY / MUSIC SELECT, @0x6a418), `TouchPause`, `TouchReturn`; ENDING via `WindowOption TouchButton` @0x453cc |
| 21 | SE05_01 | `WindowResult setHighScoreMode` (NEW RECORD) |
| 22 | BGM01 | title, restarted at once on return from another screen (@0x50e6e) |
| 24 | BGM03 | options (@0x444be), RESULTS (restarted on every entry) and RECORDS (only if not playing) |
| 25 | BGM04 | ending credits roll |
| 26 | BGM05 | result screen (@0x4bad2) |
| 27 | Na_SEGA_09_keep | logo voice, played as BGM (stops the other BGM, BGM volume) |
| 28 | Na_Title_A_03_keep | `WindowTitle exec`: the title call |
| 29 | SE04v2 (loop) | `WindowCounter Active` @0x35db0: loops while a result counter rolls |

`TouchSoundVolume` and `TouchInputTiming` play nothing, nor do the game-over path and the combo milestones. Music select and PV select stop the BGM and play the previews instead. The ported menus never use 1, 4 or 5 (an older port used them as "hit", "decide" and "cancel"). The original has one OpenAL source per sound, so playing a sound again restarts it instead of layering a copy (the port's mixer does the same). SE volume is kept in 0..0.75 (slider 1.0 = 0.75).

### 5.13 Glyphs (`e_CharPictTbl` @0x139fde, 6-byte rows `{u16 tex, x, y}`) [D]
These are 78×78 tiles. tex 4 = `mf_hiragana_blank`, 6 = `mf_katakana_blank`. The note adds `+enabled`, so a playable note uses the `_telop` sheet (5/7) and a display-only note the `_blank` sheet. With the KEYBOARD option, `SystemTexture LoadBoardTex` also swaps the two hiragana sheets for `mf_hiragana_{blank,telop}_fullroman` (FULL ROMAN) or `_romanhira` (ROMAN SUPPORT), same tile layout; the katakana sheets have no such versions. `evaluation_00..04` = Cool!!, Fine!, Safe, Sad, Worst (index = 5 − grade).

### 5.14 DLC packs (`ExPackManager`, `MusicPack`, `StatusData`) [D, V on the archive.org "All DLC Included" IPA]
**Download/install [D]:** `+[ExPackManager Load]` fetched `ShopList_enc.txt` (base64 + encryption, `dataFromUrlWithBase64Encrypt:`) from `http://dl.sega-pc.com/mikuf2/MusicPack/14_honban_13_djikdsa_1101/` with keys `%03d_PackID`, `%03d_MusicNum`, `%03d_%03d_MusicName`, `_ProducerName`, `_MusicBPM`, `_DifficultyEasy..Extreme`, `_ArtWorkFileName`, `_PreSoundFileName`, `_MovieFileName`, `%03d_ZipSplitNum`. Packs were zips (`%@%@.zip`, split parts via `genSplitFileName::`) unzipped by `DownLoadExPackData::` (`UnzipFile`) into `GetInstallPath()` = `NSSearchPathForDirectoriesInDomains(NSLibraryDirectory)/InstallData` (skip-backup attribute set): movies, previews and the art sheet in `InstallData/Mov_<packID>/` (`GetMovieFolderPath:`), the shop thumbnail in `InstallData/Thum_<id>/thum_<id>.png` (`GetThumFilePath:`). Buying a pack copies its `MusicData` into a `MusicPack` in the save (`MovePackageToMusic:`, `+[StatusData AddMusicPack:]`). The server is gone, so the save is the only remaining copy of the DLC song table.

**IPA with the DLC bundled [V]:** the archive.org upload (7.6 GB, 435 zip entries) is an iMazing-style package: `Payload/MikuFlick2.app` is identical to the plain v1.1.5 app, and `Container/` is the app's data folder from a device that owned every pack: `Container/Library/InstallData/Mov_{1..17,96..99}/` (21 packs, each three `<movie>.usm`, three preview `.adx`, one art sheet `music_<NN>_01.png`, `verificationFile.dat`; `Mov_8` also has `thum_8.png`), `Thum_*/`, and `Container/Documents/MikuFlick2.dat`. All DLC entries are stored (method 0). No file name occurs twice across packs except `verificationFile.dat`. Preview names: `pv_NNN_lp` for packs 1–17, `pvNNN` for 96–99.

**Save `Documents/MikuFlick2.dat` [V]:** NSKeyedArchiver bplist, root key `savedata` (`StatusData`, `version` 27). `m_MusicPackArray` is NSData holding a second keyed archive (root `root`) of an NSArray of `MusicPack {m_PackID, m_PackName ("DefaultPack", "Duet_Pack01", ...), m_IsBuy, m_IsInstalled, m_Date, m_MusicDataArray}`. Each `m_MusicDataArray` is NSData around a third archive: NSArray of `MusicData` with plain number fields `m_Index` (global song id: 0–10 built-in, 11–73 DLC in this save), `m_BPM`, `m_NoteDelay`, `m_ArtWorkIndex` (0–2), `m_TitleLineBreadIdx` (sic), `m_DifficultNum0..4` (★ Easy..Extreme, Break the Limit 9), `m_PreInstall`, the records (`m_TopScore0..4`, `m_ClearRank0..4`, ...), and NSString fields stored as NSData around a one-string archive: `m_Title`, `m_Artist` (empty for built-in songs), `m_MovieDataFileName`, `m_PreSoundFileName`, `m_ArtWorkFileName`, `m_LogoFileName` (nil). Pack 0 (`DefaultPack`) repeats the 11 built-in rows of §5.11. The DLC art sheets use the `music_00_01` frame layout (`art_/logo_00_01_0<ArtWorkIndex>`), checked on `music_01_01.png`.

**Port [V after import]:** `port/src/dlc.c` reads this save (`bplist.c` with UIDs) and writes `dlc <id> <pack> <art_index> <title_break> <preview> <movie> <atlas> <bpm> <★×4>\t<title>\t<artist>` lines into `gamedata.txt` for installed, non-preinstalled songs whose `.usm` and art sheet are present. The importer (`ipa.c`) flattens `Container/Library/InstallData/Mov_*/*.{usm,adx,png}` into `MikuFlick2.app/` (never over an app file); `make setup` symlinks them there. DLC songs follow the built-in ones in pack order; the port save keys their records (`rec`, `recx`, `flk`, `timing`) by movie name.

## 6. Rendering and screens (verified against the iOS screenshots in help_01.png / help_02.png)

### 6.1 Method, primitives and frame rate

**Method.** Ghidra's C output drops the float arguments of draws: iOS armv7 passes floats in core registers (softfp) and the decompiler models them as VFP. Positions were therefore recovered from annotated disassembly (`tools/ghidra_scripts/DumpDisasm.java` → `work/disasm/*.s`). `tools/disasm_calls.awk` propagates constants through movw/movt, VFP immediates, literal pools, s/d aliasing and float add/sub/mul, with `GetBottomOfs` = 0 at 640×960. `tools/calls_named.sh` resolves frame IDs to names. A `movw rX, __mh_execute_header` in the annotated output means the low half is 0x4000 (so 628/792 read earlier were 630/794).

**Draw primitives [D].** `TPManager RenderTex:<frameId> pTex:<texture> PosX: PosY: PosZ: …`. `frameId` indexes `s_tblTPFrameName` @0x140cb0 (451 names, exported to `work/tpframes.txt`), and the rects come from the plists named in `s_tblTPPlistName` @0x140c5c. Textures come from `SystemTexture GetTex:` / `s_FileName` @0x140ad8: 0 bg_tex_07 (loading), 1 game_tex_01, 2 UI_tex_01, 3 keyboard (game_tex_02, or its `_fullroman`/`_romanhira` variants chosen by `GetKeyBoardType`).
`Cloud3D::RenderTex2DClip` builds its quad from **(x, y) to (x+w, y+h)**, so every position is a **top-left corner**. The exception is `RenderTexRot`, which is centred. A negative source width flips the frame horizontally (`RenderTex2DClip` @0x558f8: u0 = (u + |uw|)/W); a negative drawn width mirrors the sprite as GL does. `e_SystemPictTbl` @0x139514 (190 × `{sx,sy,w,h,tex}`) is a **stale** legacy table whose rects don't match the shipped textures; the code uses the plists. `GetColor(a, r, g, b)` takes the alpha first. Draws are depth-sorted by PosZ (smaller = in front).

**Frame rate.** The display runs at **30 fps** (`EAGLView animationFrameInterval = 2`); exec and Render run once per frame, so effect timers advance at the same 30 Hz as PlayCnt. `WindowBase` child windows, however, are exec'd **twice per frame**: once through `WindowManager AddObject` (`WindowBase init` @0x33210) and once from the parent's `[super exec]` child loop. That applies to `WindowBGEffect` on the music select, PV select and options screens, and to `WindowCover` (also added with `AddChildWindow` in `WindowMusicSelect loadTexture` @0x40d24). `WindowTitle`, `WindowResult`, `WindowRecord` and `WindowScore` do not call `[super exec]`, so their children (the result cubes and `WindowCounter`s) run once.

**Buttons [D].** `TouchBase touchDown`/`touchUp`: a button fires only when the same touch pressed it and is released inside it, and a touch that moves further than the button's own size on either axis is cancelled (a swipe that ends on a button does nothing). While held (`m_TouchDownIdx ≥ 0`) most buttons show their `_on` face and scale ×1.05 about their centre (`CoordinateScalingPoints`); exceptions are noted per button. The label/face pulse: `TouchHelper CoordinateColor` steps a 22-step counter only when ≥ 51 ms have passed since its last step, i.e. every second 30 Hz frame: **15 Hz** (not 30 Hz). While the title menu buttons slide in, the counter is held at 11. Port: a mouse click can deliver down and up inside one frame, so the port keeps a button's (and a flick key's) pressed look for at least 100 ms after the press; the original needs no such aid.

### 6.2 Game screen

**Layout (`WindowGameWindow Render` @0x39db4 and friends), top-left 640×960 [D]:**

| element | frames | position |
|---|---|---|
| score | `score_0d`, comma `score_10` | value `TmpStageScore + TmpComboScore`; x = `s_tblScorePosX` {393,435,465,495,537,567,597}, y = 8, z 4.5+0.1i; commas after digits 0 and 3 at x+27; 7 digits, white; leading zeros are skipped and the units digit is not forced, so **a score of 0 draws nothing** (the score appears with the first hit) |
| high score | `highscore_11` label / `highscore_0d` | `MusicData GetTopScore:` of the song/difficulty; digits from x = 129, 14 px stride, y = 7; comma `highscore_10` at +13 (+6 advance); the label at (5,8) is drawn only when a digit was (`tst r8,#1` @0x3a206), so **no record = nothing at all**; white, no fade |
| judge | `charactercircle` / `charactercircle_int` (interlude) / `outsidecircle` / `insidewhirl` (rot) | (8,421) / (−2,408) / (−9,404) / centre (68,481). `charactercircle` and `outsidecircle` only while UI alpha > 0 (@0x3a474), with the UI alpha; `charactercircle_int` only while the interlude alpha > 0 |
| lane | `rail_00` stretched to 640 wide, z 5.2; at combo ≥ 100 `rail_01` instead, at x = −pos and 640 − pos | (0,424); pos = (int(pos) + 32) % 640 per tick; colour `GetColor(GetUIAlpha,1,1,1)` (@0x3a7be/0x3a63e), so the rail is invisible before the FadeIn cue and during interludes |
| combo (≥5) | `combonumber_0d` (29×39), then `combonumber_11` "combo", or `combonumber_12` "hit" on difficulty 4 | 4 digits of combo % 10000, leading zeros skipped, x from 6, 18 px stride, y = 517, z 4; label at x = 18·digits + 8; UI alpha; no animation, scale or milestone effect (@0x3a808–0x3a9ce) |
| tension | `microphonemeter_00` (dark stand) and `_01` (glowing outline) whole, then `_02` (91×362 coloured fill) clipped from the bottom: fill = 362·gauge/256 + 0.5, drawn at y = 943 − fill (@0x3ad26) | right-handed x = 4 / 4 / 15.05, y 572; left-handed mirrored with `RenderTexClipReverse` at x = 490 / 521 / 534 |
| notes | 78×78 glyph from `mf_{hira,kata}gana_{blank,telop}` | (pos.x − 39, 442); option (small-kana) glyph at +32; culled outside x ∈ [−40,680] |
| direction hint | `inputguide_0N` via `s_tblLastNoteTexIdx` @0x138f38 = TP {278,279,282,280,281}: flick code (tap, up, right, down, left) → sprite 00/01/04/02/03 (the sprites are ordered tap, up, down, left, right) | (26,440) while the next note is within 11 ticks |
| timing ring | `inputtarget` (scale 0.48…1 by ticks-to-just), `inputperfect` within 2 ticks | centre (69,482) |
| keys | `flickpanel_ori_off_k` idle / `flickpanel_ori_on_k` pressed (TP 416/426 + k, `SystemTexture GetTex:3`), z −1, UI alpha | `e_FlickBoardPictTbl` pos (top-left): あ(129,530)…わ(256,854). The **only** pressed look of a key is this texture swap (`TouchFlickBoard Render` @0x6f9d4, @0x6fc14): no scale, glow or release animation; the release feedback is the EffectPulse ripple |
| key guides | `NoteNormal render` @0x22a38, every active note with cnt ≤ just (unless PANEL GUIDE is off): `guidepanel_off_*` (`s_tblKeyGuideTexIdx` @0x139460) or, for the next target, `guidepanel_on_*` (`_Next` @0x139474) plus `guidecursor` at key − (3,3); same 00/01/04/02/03 mapping as the hint | on the note's key, scaled by cnt/just about the panel rect (0 below 0.25); older notes in front |
| note ring | `emphasiscircle` on every active note (not only climax ones) | centre (pos.x + 1, 481), angle π − m_Rot/100 rad, m_Rot = (m_Rot + 21) % 360 per tick (`NoteNormal exec`) |
| climax note | glyph tinted `s_tblRainbow` @0x139310 [(m_CrimaxCnt + 10) % 28] (28 RGB floats/255) | only while combo ≥ 100 |
| flick flower | `-[TouchFlickBoard RenderGuide:]` @0x6f860, drawn for every held board (`m_TouchDown`): with no flick yet, all petals that have a glyph, `flickguide_off_0p` (only with FLOWER on); once the finger has a direction, only that petal, `flickguide_on_0p`, whatever the option; petal p = up/down/left/right → TP 312..315 (+4 lit), glyph `fgcharacter_ori_N` from `e_GuideCharPictIdx` @0x139f78 [key][flick code]. Petals z −1.2, glyphs z −1.3, **both with the UI alpha**. It **returns at once while the board is in interlude mode**: there is no interlude flower | petal/glyph offsets from the key's top-left: up (7,−109)/(36,−85), down (7,82)/(36,136), left (−127,3)/(−90,26), right (99,3)/(160,26) |
| interlude panel | `flickguide_int_off/on` (TP 0x114/0x115), only while `s_IsInterlude` and on board 0; `_on` while board 0 is held, also after `EndInterludeMode` while the panel fades | (key0.x − 15, key0.y) = (114,530), z −1, interlude alpha (@0x6fa76–0x6fb72) |
| judgement popup | EffectFlickResult (below) | centred (66, 483 + ofsY) |
| ripple / interlude ♪ | EffectPulse / EffectInterlude (below) | centred near (70,482) |
| whirls | `outsidewhirl` A/B, `insidewhirl` (below) | (70,480) / (68,481) |

**UI fades (`WindowManager exec` @0x3e21c) [D].** One step per 30 Hz frame, skipped while paused. `ui_alpha += ui_rate`, clamped to 0..1 with the rate zeroed at either end. If the interlude hold is ≥ 1 it is decremented and the rest skipped; otherwise `int_alpha += int_rate` with the same clamping; when it falls below 0 the interlude panel is hidden (`setInterludeMode:0`), and if `next_cue < ncues − 10` the keys fade back in (`SetUIFade:1`; within the last 10 cues they stay hidden). `SetUIFade:1/0` and `SetInterludeUIFade:1/0` set the rate to +0.06 / −0.02 per tick. `initGame` calls `SetUIAlpha:0`, so the board, keys, guides, judge ring, input target, tension meter, rail, combo and charactercircle stay hidden until cue 1 (FadeIn); cue 2 (FadeOut) is `SetUIFade:0`. `StartInterludeMode` does nothing if an interlude is already running; otherwise hold = 0, interlude fade +0.06, UI fade −0.02, and board 0's interlude mode is set. `EndInterludeMode` sets hold = delay_ms/30 ticks and interlude fade −0.02; the touch rect goes back to the ten keys at once.

**`WindowGameWindow exec` @0x39b3c [D]** runs every frame, also while paused. `setFade` sets `m_Fade` = 1; it drops by 0.03 per tick and a full-screen black rect with alpha `m_Fade` is drawn in front of the game (port: the countdown starts with the first movie frame). Rotations: `m_ComboRot += 12`, `m_ComboRot2 += 13`, `m_ComboStarRot += 17` (mod 360). Whirls (`RenderTexRot`, additive, alpha × UI alpha): `outsidewhirl` A at combo ≥ 25, (70,480), rgb (1,0.75,0), angle 360 − ComboRot2, half alpha while combo ≤ 34; B at combo ≥ 45, same place and colour, angle ComboRot, half alpha while ≤ 54; `insidewhirl` always, (68,481), rgb (1,0.63,0), angle 360 − ComboStarRot, ×0.75 alpha while combo ≤ 14. SpScore popup (`StartSpScoreEffect:`): each tick cnt++, y = max(50, 75 − 25 sin(cnt/42·1.57)); from cnt 28 alpha −= 1/15; off at cnt 43; 4 digits of score % 10000 (leading zeros skipped) with `score_0d` at x = 508 + 28i, rgb (1,1,0.66). Callers: the +39 after the last interlude (@0x30248) and the climax bonus.

**Effects [D].** The first frame shown is TimeCnt 1 (exec runs before render). Draw order: pulse rings z −1…−1.3, results and quavers z −2.

* **EffectPulse** (init @0x2e1c4, exec @0x2e27c, render @0x2e3d8): `AddEffect2D` type 3 on **every** board release (hit, miss or interlude) by `touchUp` @0x6fec6 (replays @0x6ef1e). Timer 10, alpha 1. At tick n: `m_NormalRate = 1 + 0.92 sin(n/10·1.57)` + max(0, (int)(((combo−50)/5)·0.02)) (0 below combo 300); once n > 5, alpha = 1 − 0.2(n − 5). Render centred at (70, 482 + GetBottomOfs), size 120·rate·k px, additive, **never using the UI alpha**: always `ripple` (TP 0x11d); only when not in interlude mode (`IsInterludeMode`, @0x2e512), combo ≥ 15 and n ≤ 7, also the rings below.

  | ring | shown from combo | half alpha up to combo | k |
  |---|---|---|---|
  | fireworks_mik_01 (0x112) | 15 | 24 | 1.35 at ≥ 75, 1.4 at ≥ 95 |
  | fireworks_mik_00 (0x111) | 35 | 44 | 1.15 / 1.25 / 1.35 / 1.4 at ≥ 55 / 65 / 75 / 95 |
  | fireworks_mik_02 (0x113) | 85 | 94 | 1.25; 1.4 at ≥ 95 |

  TP indices of game_effect_01 are alphabetical from 273: fireworks_mik_00..02 (273–275), flickguide_int_off/on, inputguide_00..04 (278–282), inputperfect, inputtarget, ripple (285); game_tex_01 follows from 286 (afterimage_00).
* **EffectInterlude** (init @0x62af4, exec @0x62be8, render @0x62de4): type 7 on every successful interlude. Timer 20, size 0.1, stretch 1, alpha 1, `m_OfsAddY` −40. Per tick: `m_OfsAddTmpY += m_OfsAddY; m_OfsAddY *= 0.5` (−80·(1 − 0.5ⁿ)); `m_OfsY = −100 sin(n/20·1.57) + m_OfsAddTmpY`; size = min(1, size + 0.15); once n > 15 alpha = 1 − 0.2(n − 15). `quaver_01` (TP 0x162) at w·size·stretch × h·size, centred on (70, 483 + m_OfsY), blend 1.
* **`m_StretchRate` flip-spin** (EffectInterlude, and EffectFlickResult exec @0x2ea26): while n < Timer − 5 and n ≤ 17, bit n of 0x3c3c adds −0.38 and bit n of 0x3c3c0 adds +0.38, otherwise it resets to 1. The steps are cumulative: 1, .62, .24, −.14, −.52, −.14, .24, .62, 1, … A negative width mirrors the sprite.
* **EffectFlickResult** (judgement popup, type 4, Timer 20; render @0x2eb34): ofsY = −`s_tblEffectResultOfsY`[g]·sin(t/20·π/2) − 80·(1−0.5ᵗ); size 0.1 + 0.15/tick (max 1); fades over the last 5 ticks. Two layers, blend 1: the back `afterimage_*` (`s_tblResultEffect_Back` @0x13ac3c = TP {286,290,289,288,287,286} by grade 0–5) at w·size·**stretch** × h·size, then the front `evaluation_*` (table @0x13ac54) at w·size × h·size, **not** stretched.

**Pause (`WindowGameWindow pauseGame` @0x39138, `PauseRenderFunc_Enable` @0x38ff4) [D].**
* Screen: 30 % black (`GetColor(0.3,0,0,0)`), `menugradation` stretched to 640 wide at y = 374, then three `menubutton_m` buttons at x = 164: RESUME (`TouchBack` No 1, y = 574), RETRY (`TouchRetry`, type 10, y = 656) and MUSIC SELECT (`TouchRetry` No 2, type 11, y = 740), font 32. `TouchBack Render` @0x6e27e / `TouchRetry Render` @0x69e40 draw them like music-select PLAY: `menubutton_m_base`, the pulsing `menubutton_m_off` face (colour = alpha, blend 1), `menubutton_m_on` (TP 0x39) and ×1.05 while held (@0x69f0c), and `parenthesis_00/01` on every button. RESUME plays SE 8, RETRY / MUSIC SELECT SE 19.
* **Pause button** (`TouchPause`, Render @0x72738, touchUp @0x72910): `pausebutton` (TP 0x160), fully opaque, blend 1, hidden when `IsHidePauseBtn` (DISPLAY PAUSE off) and while paused, ×1.05 while held. Hit box 80×80 at (570,890), or (19,890) left-handed. `touchDown` only calls super; `touchUp` opens the pause through `WindowManager TouchCallback` only when `GetFadeAlpha ≤ 0` and `GetMovieFrame < GetMovieTotalFrame`, then plays SE 19. Port: "before the movie end" is approximated as "not in the last 2 s".
* **INPUT TIMING** (`TouchInputTiming`, type 0x3d, created in `WindowGameWindow init` @0x39528 in `m_PauseTouch`, so it is shown with the pause menu). m_Pos (113,895), touch box 440 px wide from y 895. Render @0x7d788: "INPUT TIMING" at 28 pt centred at m_Pos.y − 58, `inputbar` at (100, m_Pos.y), `inputcursor` at (285 − 20.4·timing, m_Pos.y − 19), fixed colour, **no pressed look**. touchDown @0x7d990 records the point; touchMoved @0x7da78/@0x7da9c: once the pointer is more than **10 points** (20 px) from the last step point, one step (right = −1, left = +1, clamped to −10…+10) and the step point moves to the pointer; a tap alone changes nothing; touchUp @0x7d9fc plays nothing. The value is stored per song (`MusicData GetInputTiming`, used in §5.2). This is the original's only input-timing control: an earlier version of these notes wrongly said there was none, and the port had put one on the OPTIONS → DISPLAY page; it is now in the pause menu as in the original (port extra: `Left`/`Right` in the pause menu step it too; `--offset` overrides it for a session).

### 6.3 Boot, logos and title

**Boot (`SceneBoot` @0xf978/0xfb50) [D]:** `bg_tex_08` ("LOADING" Miku, the 640×960 region of a 1024² texture) is shown on white while every manager and texture loads, then the game goes straight to scene 1. The port shows it during asset loading.

**Logos and title state machine (`WindowTitle exec` @0x50fb8, `touchUp:` @0x51b8c, 30 Hz) [D]:** the logos are the top-left 640×960 regions of `logo_sega`/`logo_crypton`/`logo_cri` (1024² textures) on a white background (`RenderRect2D` 640×1136 at (0,−88)). Transitions use a full-screen **white** overlay (`GetColor(1,1,1,m_FadeAlpha)`).

| state | behaviour |
|---|---|
| 0 | SEGA for 86 ticks; SE 27 (Na_SEGA) at tick 10 |
| 1/2 | fade ±0.2 per tick, switch to Crypton |
| 3 | hold 45 |
| 4/5 | fade, switch to CRI |
| 6 | hold 45 (a tap skips to 7) |
| 7 | fade in; background goes black |
| 8 | fade out to the title; SE 28 (Na_Title) |
| 9 | Touch to Start; BGM01 (22) once 67 ticks have passed |
| 10 | menu |

A tap in state 9 activates touch slots 0, 1, 3, 5 (mask 0x2b), plays SE 19 (SE02_01) and enters state 10.

**Title menu (`TouchTitleMenus` @0x6a428.., Render @0x6abb4) [D]:** labels RHYTHM GAME / PV / OPTIONS / SHOP (types 2, 3, 5, 0x2c → index 0–3, font 34 px). `menubutton_l_base` (394×82) sits at (x, 538 + 82·i). Buttons start at x = 640 + 128 + 20·i and slide −100 px/tick to x = 128. The face `menubutton_l_off/on` (380×68) is inset by 7 and drawn additive (blend 2); `parenthesis_00/01` (39×68) are drawn on every button. Info (`infoicon`) sits in the bottom-right corner.

**Crown (`TouchGotoScore`, title type 4) [D]:** m_Pos (14,893) (0x41600000 / 0x445f4000, initType @0x63440; not y ≈ 878 as read earlier: the 67×67 frame touches the bottom edge), hit box 67×67; `crownicon_off`, `crownicon_on` ×1.05 while held (Render @0x635ac); touchUp @0x63710: SE 19, then `NextScene 5` (RESULTS, §6.7). It does not open Game Center directly.

**Title (`WindowTitle RenderTitle` @0x51678):** bg_title (0,0); miku (−10, −3 + bob); outerkey_b (63,411), _p (535,148), _r (434,533), _t (115,76); cubes_p (472,103), cubes_y (0,48); titlelogo_00 (12,242), _01 (320,242); copyright (99,922); touchstart (172,662), blinking.

**Title extras (`WindowTitle eyeAction:` @0x51940) [D]:** bob = 5·sin(BlinkTime·113.04). BlinkTime counts ticks and 113.04 rad per tick aliases to about −0.057 rad/tick, a cycle of about 3.7 s. Once song 9 is unlocked she blinks every 200 ticks: ticks 1–2 show `eye_00`, 3–4 `eye_01`, at (320, bob + 109). The `outerkey_*` decorations are static.

### 6.4 Music select and PV select

**Music select (`WindowMusicSelect` @0x405cc.., `WindowCover` @0x36070.., `TouchMusicSelect` @0x6d160..) [D]:**
* Static: bg (0,0), baseline (0,824), "HIGH SCORE:"+"BPM:" texts centred side by side at y = 660. Logo (`WindowCover renderLogo` @0x36a0c) at its native 512×256 at (64,43), z −3, no cross-fade. Stars (`Render` @0x41cd4): 9 × `difficultyemblem_00` (filled, i < stars) / `_01` (40×40) at (166 + 34i, 614); Break the Limit draws none (`RenderClearStarBTL`).
* Difficulty labels (`TouchDifficulty updateText:` @0x700f8) at x = 20/174/328/482, y = 799 (99/247/398 while Extreme is locked); Break the Limit shows `difficulty_on_04` at (208,799) under `difficultycursor` stretched to 328×66 at (155,776.25). `difficultycursor` 164×66 at (`m_CsrX`, 776.25); `setDifficulty::` @0x3fd3c/@0x3fd62 targets {7,161,315,469} (table @0x13bfb0; {86,234,386} locked) with `m_CsrSpdX = (target − x)/3`; each exec `m_CsrX += spd`, then `spd *= 0.9` (0x3f666666 @0x413f6), clamped at the target.
* Clear-rank marks (`RenderClearStar` @0x41e94 → `RenderRankMarkByRank:texIdx:posX:posY:` @0x4204c, 35×35 top-left): rank 0 `clearrank_star`, 1 `_s`, 2 `_a`, 3 `_b`, 4 `_c` at (116/300/430/602, 783) (table @0x13bfcc); Break the Limit one mark at (430,783). Scale 1.0 when `texIdx == 0xe` (the selected difficulty), otherwise 0.9, drawn at (x + (1 − s)·w/2, y + (1 − s)·h/2).
* MODE CHANGE (`TouchModeChange`, m_Pos (118,678)) is drawn at (118,698), `modechange_on` while pressed.
* Cover flow (`updateTitle::` @0x4017c): cover i's centre x starts at 280 and advances 40 per cover, 80 on either side of the selection (cover 0 gets +40 when selected). It is drawn at `x − m_ScrPos`. `setDirection::` @0x36758 sets the stretch targets: selected = flat; left covers = left edge (0,+10), right edge (−230,+60); right covers = left edge (+230,+60), right edge (0,+10) (x shift, top/bottom inset). `WindowCover exec` @0x36110 eases position and stretch 0.15 toward the targets while `m_StretchTimer` > 0 (twice per frame, §6.1). `Render` @0x36290 draws a 316×316 quad from y = `m_Pos.y` = 293 (`Vector2D(x, 293)` @0x40f14, so the jacket spans y 293–609) plus a 120-px reflection, nearer covers on top. **Taps on a cover do nothing** (`WindowCover TouchButton::` is `bx lr`).
* `updateTitle::` also plays SE 15, stops the preview at once (`stopLoopSound`, no fade), sets `m_FadeSoundWait = 10` if it is 0 (a running wait is not restarted) and sets `m_StretchTimer = 35`. `updateSound` @0x400b4 counts the wait down; at 0 the centred song's preview plays, looped, at the BGM volume.
* **Swipe scroll.** Touch area `ScrollSetTouchArea:::: (0,140)-(640,400)` (call @0x41128; the same in `WindowPVSelect loadTexture` @0x45fea), compared with the touch in **points**, so in 640×960 px it is the band **y 280–800** across the whole width (jackets, reflection, the strip below and the top of the difficulty labels). `ScrollTouchDown:` @0x344c8: inside → `m_IsScrSwipe = 1`, `StartPos = ScrPos`, `SwipSt = SwipEd = point`; outside → 0. `ScrollTouchMoved:` @0x345a4: outside → 0; inside, if not yet swiping, `StartPos = ScrPos`, `SwipSt = point`; then `m_IsScrSwipe = 1`, `SwipEd = point`. `ScrollTouchUp:` @0x3469c: 0.
* `WindowMusicSelect exec` @0x41466–0x41880: (1) `CloudSimpleSpring::Compute`; (2) skip the rest if `m_CoverNum < 2`; (3) axis factor 1 where `ScrLimMin != ScrLimMax` (only x); (4) while swiping: `ScrPos = StartPos + factor·(SwipSt − SwipEd)·0.9` (@0x41788) in points, i.e. **0.45 scroll units per scene px, about 89 px of drag per cover**, `ScrAdd = ScrPos − prev`, and `m_ScrolIndex = clamp((int)((x + 20)/40), 0, N − 1)` with `updateTitle` on change, so the centre cover changes during the drag; (5) not swiping: on the first tick after a release the spring is aimed at `idx·40` from x; then `x = spring.GetPos + ScrAdd` (release momentum), pulled back past a limit by (lim − x)/1.5 and snapped once it crosses; (6) momentum decay **`ScrAdd /= 1.2`** (0x3f99999a @0x417e0), zeroed below 0.1; (7) limits (`loadTexture` @0x41152): x 0…max(0, CoverNum·40). The generic `WindowBase ScrollExec` @0x341b0 (factor 2, ×0.5 edge pull, /1.5 decay) is not used here. The `CloudSimpleSpring::Compute` constant has not been re-read: the port uses 0.5 **[H]**.
* PLAY (`TouchMusicSelect` init @0x6d160, Render @0x6d510): m_Pos (164,872.25); `menubutton_m_base` 324×82 there, face `menubutton_m_off`/`_on` inset 7, ×1.05 while pressed, the label colour pulsing (15 Hz, §6.1), `parenthesis_00/01` at (165,874) and (165 + 271·scale, 874).
* Back (`TouchBack initType:No:Pos:` type 4 @0x6de3e, Render @0x6e270; also PV select, options, RESULTS, RECORDS): m_Pos (17,872.25), `menubutton_s_base` 129×82, face `menubutton_s_off/on` inset 7, `back` at ≈ (51,890). While pressed every rect is ×1.05 about its centre and the right bracket moves from `pos.x + 76` to `pos.x + 79.8`; the port's left-bracket shift of (−2.5, +1.2) is inferred from the same block, not read exactly.
* The right-hand bottom button on music select is `playlist` (not ported).

**Background cubes (`WindowBGEffect`, `EffectBGEffect initWithType::` @0xbff4) [D]:** `if (++m_Timer % 100 == 0)` spawns 3 cubes, so a scene starts empty and the first wave comes at exec 100; with the double exec about 50 are alive at steady state. x = 50 + rand(540), y = 1060, v = (rand(100)/100 − 0.5, −(rand(50)/100 + 0.5)), speed = (rand(50)/100 + 0.5)·1.2 per axis, rotation ±`_RotSpeed` (0.005984 rad/tick); removed once off-screen. Frame from the pattern {7, 8}, i.e. **only `bgeffect_03` / `bgeffect_04`**, drawn at half size with `RenderTexRot`, additive, colour `GetColor(0.1,1,1,1)` (alpha 26/255). Screens: music select, PV select and options (2 execs per frame) and result (1 exec); not RESULTS/RECORDS.

**PV select (`TouchPVLoopMode update:` @0x717e0, `WindowPVSelect`) [D]:** the music-select cover flow with the 109×93 toggles PLAYLIST/LOOP/SHUFFLE/LYRICS/KARAOKE (`playlist`/`loop`/`shuffle`/`lyrics`/`karaoke_on/off`) at x = 25 + 120i, y = 742, and `saisei` on the jacket (help_02 panel 4). "SONG SELECTED", the `TouchPVProgram` play mark (112×104 at (265,612), @0x7388c) and SHUFFLE are only shown with `GetPVModeFlg:0` (PLAYLIST). `StatusData` init zeroes all PV flags, so LYRICS starts off. Port: the playlist editor is not ported, so the text and play mark are hidden and SHUFFLE stays available (it would otherwise be unreachable).

### 6.5 Result screen and FLICK RESULT

**Result (`WindowResult Render` @0x4e8a8 / `RenderScore` @0x4dbdc / `RenderBgRect` @0x4e610 / `exec` @0x4d6ac) [D]:**
* bg (0,0) z 2. Panel: 9-slice `resultwindow_00..03` corners at (81,223), (482,223), (81,739), (482,739); `_04`/`_05` top/bottom edges at x = 167…419 (63 px), `_06`/`_07` left/right edges at y = 298…676; centre = `RenderTexClip` of a 2×2 texel block inside `_05` stretched to 338×441 at (144,298). Spans (81,223)–(576,841).
* jacket (90,67) 142² z −0.2; logo (279,67) 256×128 z −0.3; titlebar (104,238) z −0.5 with the difficulty text ("Easy" … "Break The Limit") centred on x = 320, y = 240.
* difficulty stars (not for Break the Limit): `resultstat_00` ×9 at (290 + 26i, 191) z −1.0, then `resultstat_01` (`_02` if GetDifficultNum < 0) for the star count z −1.1.
* labels (TexTextManager, z −1.1, plus a (+2,+2) shadow at z −1.0): Total Notes (124,308), Combo (124,346), Cool/Fine/Safe/Sad/Worst (206, 394/432/470/508/547), "♪" (206,597), Stage Score (131,644), Combo Bonus (131,682), Total Score (132,720) (movw/movt pairs, e.g. 0x4408c000 = 547). Values are `WindowCounter` children with `SetRightTopPos` from the table @0x13c0c8 (y − 6): right edge 340 (rows 1–2), 435 (judgements, interlude), 510 (scores); tops 308, 346, 394, 432, 470, 508, 547, 597, 644, 682, 720.
* `RenderUnderLine:x:y:xw:yh:` stretches the 4×23 `textbar` sprite into 23-px strips behind the text (z −0.9): `textbar_00` x 114 w 233 at y 316, 354; `textbar_01` x 181 w 278 at y 403, 441, 479, 517, 555, 606; `textbar_00` x 114 w 412 at y 652, 690, 728.
* After the counters: `clearrankstamp` (349,224) z −0.1 (behind the titlebar and text); `clearrank_NN` (frame 110 + rank) at (394,298) z −1.0; `perfect` (383,291) z −0.1 for rank 0.
* `m_ContentOpen` (set in `loadTexture` for ranks 0–4) = cleared. In state ≥ 1, if not cleared and the difficulty is not Break the Limit, `failed` (256×160) is drawn with `m_FailSizeRate` (0 → 1, +0.2/tick) scaled about the centre of (359,319)–(615,479), z −3.5. So FAILED shows for rank D as well as E. In state ≥ 4 with a new high score, `newrecord` (152²) pops in the same way about the centre of (461,328), z −3.6.

**Counters (`CheckCounter` @0x4d5fc).** Counter `m_NowCounterIndex` is activated (SE 29 looped), SE 29 stops when it settles, then the next starts. Order: Total Notes, Combo, COOL, FINE, SAFE, SAD, WORST, ♪, Stage Score, Combo Bonus, Total Score. `WindowCounter SetValue`: `m_Digit = floor(log10 v) + 2` (1 for 0). Digit k (k = 1 the units) shows its own tick counter mod 10 while that counter is below 12k, so a counter with d digits settles after 12d ticks. Values are grouped with commas ("18,572"). `touchDown:` @0x4f07c settles every counter. Whether an inactive counter shows blanks or zeros is unverified; the port draws nothing until it activates **[H]**.

**States (`exec`).**

| state | what happens |
|---|---|
| 0 | when `ModeCnt ≥ 46` and the counter index is ≥ 11: rank SE (§5.12), then the stamp (`clearrankstamp` + `clearrank_NN`, or `perfect`) |
| 1 | if failed (`m_ContentOpen == 0`): `m_FailSizeRate += 0.2` per tick; wait until `ModeCnt ≥ 66` |
| 2 | new record → 4, otherwise → 5 |
| 4 | `setHighScoreMode`: SE 21, `m_HighScoreSizeRate += 0.2` per tick for 46 ticks |
| 5 → 6 | Back, PLAY AGAIN and the FLICK RESULT toggle are created (@0x4d9d4); only from here do they draw and take input |

The device screen also has REPLAY ("RECORD THIS GAME", 180–465 × 765–808) and Twitter (507,872); neither is ported.

**FLICK RESULT toggle (`TouchResultChange`, type 0x20) [D]:** position forced to (530,784) in `initType:No:Pos:` @0x63aac (0x44048000/0x44440000); collision = frame 125 (67×67). Render @0x63c4c: `flickresult_off` (125) on the score page, `scoreresult_off` (160) on the flick page; while held the next frame (`_on`) ×1.05 (0x3f866666), z −9.5. touchUp @0x63e24: `TouchCallback`, then SE 19. `WindowResult TouchButton` 0x20 @0x4f30e flips `m_FlickResultMode` instantly (boards SetState 0/3, counters SetIsEnabled). In flick mode `Render` @0x4ec06 skips `RenderScore` and every later draw (stamp, PERFECT, clear rank, FAILED, NEW RECORD, counters); bg, jacket, logo, window, stars, title bar and difficulty name stay.

**Per-key boards (`TouchResultFlickBoard`, type 0x21) [D]:**
* Created hidden in `loadTexture` @0x4d3e2, No = key, Pos = `e_ResultFlickBoardPos` @0x13c0a0: (130|257|385, 338|446|554), わ (257,662); collision 63×54 pt = 126×108 px.
* Data: totals from `MusicData GetFlickResultNum`, filled by `CheckTotalNoteNum` @0x291dc/@0x29234 over the op-0 cues whose digit (diff+2) is non-zero (key/dir from `e_FlickBoardTypeTbl`/`ResultTbl`); successes from §5.4 step 7 (grade ≥ 4). Direction codes 0 centre, 1 up, 2 right, 3 down, 4 left. Init @0x677b4: text slot 0 = all directions, slots 1..5 = dirs 0,1,4,3,2.
* Text: rate `"%d%%"` = (int)(succ·100.0f/total) at font 26 (the `SetFontSize:36` at @0x67908 is overwritten before any text), "Non" when total = 0; `"%d/%d"` at font 18.
* Colour (`setColor` @0x67e1c): < 50 → `resultpanel_00` (red), < 65 `_01` (orange), < 80 `_02` (yellow), < 101 `_03` (green, so 100 % is green); no notes keeps the initial `m_BaseRank` 145 = `_04` (dark navy).
* Render @0x68e04: kana `fgcharacter_ori` [`s_VowelIndices`[key]] (@0x162350: 0,5,…,38,43) from SystemTexture 3 (the KEYBOARD option's key texture) at +(31.75,27) z −0.6, under the ~75 %-alpha panel (z −0.7); rate bottom-left at (x+10, y+h−textH−5), detail top-right at (x+w−textW−10, y+5); white text z −0.9 over a 0xFF0F0F0F shadow at +2,+2 (z −0.8). An open board shows slot 1 (centre); the other boards get frame 145 on top (z −1.0).
* `touchDown` @0x6943c sets `m_TouchDown = !s_mBoardDown` (never cleared elsewhere): a touch-down on a board opens it, on the open board closes it, on another board does nothing. `touchMoved` returns −1 and `checkFlick` is never called for it, so the finger direction is irrelevant. `s_mBoardDown` is read only by the boards: the other buttons stay active.
* `RenderGuide` @0x68d40 draws every direction that has a guide glyph, unlit (`flickguide_off_NN`), whatever its argument, with the in-game petal/glyph offsets (§6.2). Rate top-left at glyph + (15 − 57/2, 5 + 57/2); `"%d/%d"` right edge petal.x + w − 15 (left petal: w − 45), top glyph.y − 5. Glyphs behind petals (z −1.1/−1.2; up −2.1/−2.2), texts in front, the up balloon nearest.

### 6.6 Options, help, credits and ending

**Options (`WindowOption setMode:` @0x42cb4, `init` @0x43eb4, `Render` @0x44504) [D]:**
* Modes 0 OPTIONS, 1 AUDIO, 2 HANDEDNESS, 3 KEYBOARD, 4 HELP, 5 CREDITS, plus DISPLAY (`OPTION_TITLE_04`). `init` ends with `setMode(GetPrevScene == 1 ? 0 : 5)` (@0x443dc): the main list when coming from the title, CREDITS otherwise (in practice: back from the ending).
* Titles (`setMode`): the title literal at font 48, y 182; the description `OPTION_TITLE_0N` at font 24 (bold only for Japanese), y 234; both with a navy (4,0,71) shadow at +2,+2. AUDIO splits its description: `OPTION_TITLE_01` at x 118 (ja 140) and `OPTION_TITLE_01_OPT` at x 298 (ja 276), both left-aligned (@0x4469e / @0x447d6). HELP has no title. The iPhone-5 y adjust (+44/+88) is ignored by the port (fixed 640×960 scene).
* Touch types (table index − 1): 0x0e SoundVolume, 0x0f KeyBoard, 0x10 Help, 0x11 Credit, 0x12 Display, 0x13 SoundVolumeMeter ×2, 0x14 HandSide ×2, 0x16 HelpPage, 0x1d Handedness, 0x1e KeyBoardSelect ×3, 0x1f PanelSelect ×2, 0x26 GotoEnding, 0x30 FlowerUse ×2, 0x3a ChangeAspect, 0x3b PauseEnable, 0x3c FailSoundEnable. `TouchGotoTutorialMovie` (0x25) is created in the HELP layer (@0x441ac) but `WindowOption TouchButton` has no case for it; not ported.
* Main list (`TouchCommonButton` @0x63ee0): `menubutton_l_base` at x = 129, y = 384 + 82i.
* AUDIO (`TouchSoundVolume` init @0x76d18, Render @0x77020): `volumewindow` at (81,303) with `speaker`; 16 × `volumemeter_on/off` (25×34) at x = 113 + 26i, rows y = 583 (BGM) and 664 (SE); lit cells = (int)(vol·15 + 1), none at 0 (SE vol is divided by 0.75 first); row label centred at (320, y − 42). Hit box 440×84 px = ColSize (220,42) pt @0x76f64. touchDown @0x77284 only records the x; each touchMoved @0x77364 adds **dx/440 with dx in points** (0x43dc0000 @0x774c4), i.e. scene px/880, and moves the start point, so a full-width drag changes the volume by half and a tap changes nothing; touchUp @0x772e8 stores the value and plays no sound. The port keeps SE volume normalised to 0..1, so both rows use the same formula.
* FAILED SOUND (`TouchFailSoundEnable` @0x7cef8): ON (No0) at (120,740) and OFF (No1) at (320,740), 199×45; `TP_FAILEDSOUND` = {on_w, on_b, off_w, off_b}: the selected button uses the light `_w` face.
* KEYBOARD (emulator capture): `keyboard_{ori,rah,fro}_off` (196×255) at x = 20/222/424, y = 304, in that slot order (port value 0 ori, 1 fro, 2 rah); the selected panel uses its `_on` face (247×315) at slot − (26,28). It switches the key texture to `game_tex_02` / `_fullroman` / `_romanhira` (`SystemTexture LoadBoardTex`, same frame layout). `panelguideline` at (35,643) with PanelSelect ON `panelguide_on_{on,off}` (322,601) / OFF `panelguide_off_{off,on}` (449,601), 129×110; `flowerline` at (38,779) with FlowerUse ON `flowet_on_*` (300,722) / OFF `flowet_off_*` (428,722), 170×145. FlowerUse is ON/OFF only (`SetFlowerEnable`); `TouchEnabledFlower` is never instantiated.
* DISPLAY (`UI_tex_09`, no plist; rects in code): TouchChangeAspect s_Pos @0x13c608 = (136,310),(156,333),(339,301),(359,324), s_TexPos @0x13c628 = {290,0,165,293},{0,266,125,247},{125,0,165,311},{0,0,125,265}; on a 3:2 device only the 3:2 phone shows, at (240,310), label "3:2 ASPECT" at y 610, and touchUp acts only when `m_Enable` (non-2:3 screens), so the port's static preview behaves like a 2:3 device. TouchPauseEnable (Render @0x7c6e0) s_Pos @0x13c668 = (322,696),(336,711),(459,696),(473,711), s_TexPos @0x13c688 = {125,311,135,115},{395,386,108,93},{260,311,135,115},{395,293,108,93}, index 2·No + !selected (No0 VISIBLE, No1 INVISIBLE); label centred under each at y = h + 705 (grey 0.8 when not selected); "DISPLAY PAUSE" at (60,744); a 578×3 divider at (31,783) behind the previews. Text textures put the capitals about 6 px below their y. DISPLAY has **no** input-timing control: that is in the pause menu (§6.2).
* HANDEDNESS `TouchHandSide` @0x72f1c: m_Pos y = 345, x = 37 / 338; Render @0x73100 draws the selected `_on` face at m_Pos − (14,35) and the unselected right hand at x − 6 (verified on the emulator).

**HELP (`WindowHelpText`, `WindowOption Render` HELP @0x44bc6) [D]:**
* Frame: `helpline` (640×12) at (0,60) and the same image rotated 180° spanning (0,825)–(640,837); panel (0,72) 640×753, colour (0,3,41) at alpha 128; `helpmeter_on/off` dots at (190 + 25i, 840).
* `setPage` @0x3b8a4: fonts 36 (counter), 36 bold (title), 24 bold (lines); keys `HELP_TITLE_%02d` and `HELP_TEXT_%02d_%d`; `m_PageColumnCount` = Σ `s_tblHelpOffset`[page][i] up to the first 0 (en @0x13b650, ja @0x13acf0; 9 pages × 60 shorts); page 9 (the licence) uses 9600; ScrPos.y = 0 (the momentum `ScrAdd` is kept across a page change).
* Render @0x3bff4: `glScissor(0,145,640,683)` (screen rows 132..815); images 322×482 at x 159 (help_01 pages 0–4, help_02 pages 5–8); text at x 96, y = 150 − scroll + cumulative offsets; title centred at (320,90); counter `"%d/%d"` right-aligned at x 590, y 873 + 41 − th/2 − 8. Licence: `HELP_TEXT_LIC000..338`, font 28, x 96, 30-px pitch, 512 px wide, rendered in 7-line chunks (`IsNextTextArea` / `RenderLicenceText`).
* Scroll: full-screen touch area, limit 12000; `ScrollExec` @0x341b0 then `exec` @0x3bf20 hard-clamps ScrPos.y to [0, m_PageColumnCount]. `ScrollExec`: while swiping `pos = start + (St − Ed)·factor·2` (points, so 1:1 in px) and `add = pos − prev`; released: `pos += add`, then a soft 0.5·overshoot bounce into [min, max]; each tick `add /= 1.5`, zeroed below 0.1.
* Page buttons (`TouchHelpPage`, `TouchButton` @0x45200: ±1 clamped to 0..9, no wrap, SE 19 on touch-up); the arrow at an end is hidden (state 3):

  | button | collision rect | art x (brackets) | arrow |
  |---|---|---|---|
  | prev | (196,874) 120×74 | 200 (202) | `arrow_00` at (224,891) |
  | next | (329,874) 120×74 | 323 (324) | `arrow_01` at (351,891) |

* Not ported: the `TouchHelpScroll` handle (the text scrolls by dragging anywhere) and the tutorial-movie button.

**CREDITS.** A 640×960 page in the top-left of the `credits_*` texture, plus the ENDING button: `TouchGotoEnding` (type 0x26, on `m_TouchCreditLayer`, `WindowOption init` @0x4438e). m_Pos is hard-coded in `initType` @0x777dc/0x77818: 0x43f38000 / 0x445ec000 = **(487,891)**; `Render` @0x778e4 draws `ending` (TP 35, 112×32, UI_tex_01) top-left at z −9.2 (0xc1133333), with **no pressed look**, only when not hidden and song 9 is unlocked. `touchUp` @0x779e4 only calls `TouchCallback`; `WindowOption TouchButton` case 0x26 (jump table @0x44fb0, @0x4536c) checks `IsUnlock(pack 0, song 9)`, plays SE 19 (@0x453cc) and `NextScene: 9`.

**Ending credits (`WindowEnding`, scene 9) [D]:**
* `s_tbl_Ending` @0x1413b8: 76 rows of 12 bytes {CFString*, s32 font size, s32 gap px}. Every size is 24; the gaps of rows 0–74 sum to 10000. Rows 7, 26, 31, 36, 41, 46 and 51 (mask 0x02108421) reuse row 2's text texture ("Songwritten by"): texture de-duplication only.
* `init` @0x38940: the 76 texts white (0xffffffff), not bold; `m_ScrollY = 40`, mode/cnt 0, `PlayBGM: 0x19` (BGM04), and `SetEndingFlg:1` unless prev == 6. `loadTexture` @0x38bec: `ending.png` (frame `bg_ending`).
* `exec` @0x38c34 (`ModeCnt++` first): mode 0 `ScrollY −= 3.5` until below −10530 (0xc6248800; clamped; 3021 ticks ≈ 100.7 s), then mode 1; mode 1 at cnt ≥ 96 → mode 2 and `SoundManager Fade: 0x19 : 130.0 : 0`; mode 2 at cnt ≥ 171 → `NextScene (GetPrevScene == 6 ? 6 : 14)` and `Stop: 0x19`.
* `SoundBase Fade::` @0x19010 / `FadeExec` @0x19080 (only caller: WindowEnding): linear, vol = st + (ed − st)·cnt/time, cnt++, and the source stops once cnt > time when ed == 0, so BGM04 is silent 40 ticks before the scene change.
* `Render` @0x38d4c: background 640×960 at (0,0); the text **only while mode ≤ 1**; line i centred on x 320 at y = 960 + ScrollY + Σ gap[0..i−1], z −6.1, white, no shadow. The last line rests at y = 960 − 10530 + 10000 = 430.
* **No skip:** `WindowEnding TouchButton::` @0x38fd0 and the `SceneEnding` touch handlers are empty.
* From OPTIONS (prev == 6) the roll returns to scene 6, which reopens CREDITS; the movie does not play. Ending → Options shows the loading screen; Options → Ending and Ending → PreEnding fade through black.

**First clear and the ending movie [D].** `WindowResult loadTexture` @0x4cbd4 sets `m_GotoEnding` when the ending has not been seen, the song was just cleared, songs 0–8 are cleared on Normal and song 9 is locked; Back/Retry then go to scene 9, and the roll (prev ≠ 6) leads to `ScenePreEnding` (14). `WindowPreEnding init` @0x79d18: `StageManager EnableGame`, `MikuFlickCriManager ReLoad "ending"` (`ending.usm`, one audio track), `SetMainAudioVolume(GetBGMVolume)`. `ScenePreEnding Exec` @0x7981a: when the CRI state is 5 (movie ended), `SetPlayingMusicData(pack 0, song 9)` (初音ミクの激唱), `SetDifficulty 3` (Extreme), `NextScene 2`. No skip. Port: the movie and its tail are implemented, but the trigger is unreachable (everything is unlocked, so "song 9 locked" never holds) and the ENDING button is always shown.

### 6.7 RESULTS and RECORDS

**RESULTS (`WindowScore`, scene 5) [D]:** bg_tex_05 frame `bg_results` (loadTexture @0x500c0, which also restarts BGM03 on every entry); "RESULTS" (48) at y 182 and `SCORE_MSG` (24) at y 234, both with a dark +2,+2 shadow (Render @0x50178); `TouchCommonButton`s RECORDS (130,568), LEADERBOARD (130,648), ACHIEVEMENTS (130,728) (init @0x63ec0); `TouchButton` @0x504f8: RECORDS → NextScene 10, Back → NextScene 1 (or setMode 0 in mode 1). The Game Center buttons call `showLeaderboard` @0x53140 / `showAchievement` @0x531cc (GameKit, no sign-in check). `setMode` @0x4fc0c modes 1/2 (Game Center buttons hidden, `m_TextNoFunc` at (320,696)) have no callers. Port: the two Game Center buttons use that dormant mode 1 with port text ("Game Center is not available."; the original's Japanese `m_TextNoFunc` is not copied).

**RECORDS (`WindowRecord`, scene 10) [D]:**
* Entry (init @0x47840): Easy for every song, the score page, song 0. BGM03 starts only if not already playing (loadTexture @0x4853c). Back → NextScene 5.
* Pages are 534 px apart (`ox = (int)(i·534 − scr)`); the selected page and its neighbours are drawn (`RenderResult` @0x4a32c): `RenderBgRect` @0x496e0 (9-slice + titlebar), jacket (ox+118,67) and logo (ox+279,67) (`RenderArtwork` @0x4a0b4), stars (290 + 26j, 191), difficulty name at (ox+320,240) (@0x4a754); on the score page `clearrankstamp` (349,224) if cleared, `clearrank_NN` or `perfect` at (394,298) if played, then `RenderScore` @0x49a80 (labels at x 124/206/131/132, values right-aligned at 340/435/510 from the table @0x13c03c, "#,##0", text bars as on the result screen but `textbar_01` at y 556). `GetScore` @0x48880 picks the values. Quirk: the star check uses `StatusData GetDifficulty != 4`, the difficulty last played, not the page's (@0x4a430); the port tests the page.
* Page arrows (`TouchRecordPage` init @0x6379c, Render @0x63920): frames 133/134 at (0,480) / (580,480), hit boxes (−140|580, 480) 200×100; `EnabledMoveArrow` @0x4acc0 hides the left arrow on song 0 and the right one on the last song; `TouchButton` 0x19 @0x4adb8 jumps instantly, SE 19 only if the song changed.
* Difficulty arrows (`TouchRecordDifficult` init @0x64d78, Render @0x64f5c): frames 120/121 at (112,251) / (498,251), hit box 20 px larger on every side; `UpdateDifficultButton` @0x4a83c / `EnableDifficultButton` @0x4a9e0: SE 19 on a change, SE 8 when clamped, the arrow at an end hidden.
* Swipe: touch area (50,100)–(W−50,H−100) against touch points **[H]** on the exact px extent; `ScrollExec` (1:1 drag, glide /1.5, edge pull 0.5, limits 0…(n−1)·534); `IsNextTextArea` @0x494b0: once per touch, ≥ 67 px from the page selects the neighbour, SE 15 (even when clamped); `MoveNearResultPosition` @0x49278 moves 80 % per tick and snaps once the **signed** `(int)(target − scr) ≤ 5` (so leftward moves snap at once); `touchMoved` @0x4b0a8 hides the difficulty arrows and the toggle until the snap; `exec` @0x490b8: the flick page does not scroll.
* Flick page: the same toggle (530,784) and boards as §6.5 (`TouchRecordFlickBoard` is a copy of `TouchResultFlickBoard`).

**When stored records update (`WindowResult loadTexture` @0x4c2d0..0x4cbbe, every result including game over) [D]:**

| value | rule | address |
|---|---|---|
| rank | min | @0x4c4b8 |
| clear | set if cleared | |
| perfect | set when max combo == notes | @0x4c5b8 |
| TopScore, TopStageScore, TopComboScore, TopNoteResult[0..5], FlickSuccessNum[10][5] | replaced together, only when `TopScore < stage + combo` (strictly) | @0x4c6a2 |
| TopCombo | max | @0x4cb16 |
| TopInterludeCnt | max | @0x4cb88 |
| FlickResultNum, TotalNotes | chart constants, overwritten every play | `CheckTotalNoteNum` @0x29234 |

### 6.8 PV viewer

**`ScenePVView` / `WindowPVView` (@0x46624–0x47810), `CuePointFunc_Lyrics`, `NoteManager render_Lyrics` @0xda64 [D]:**
* Lyrics: the lyric line (op 3) is only applied in scene 4 with `GetPVModeFlg:1` (LYRICS) and drawn at the bottom, y ≈ 896 (shadow 898), within x 15–623. KARAOKE (`GetPVModeFlg:4`) sets the CRI main (vocal) volume to 0.
* Touch: `touchDown` stores posSt = posEd (not while paused); `touchMoved` updates posEd (and posSt if posSt.x < 0; init sets (−1,−1)). `touchUp` @0x475f0 (ignored while paused): |posEd.x − posSt.x| > **160 points** (0x43200000 @0x477d0; 320 px) is a swipe. Left: `GotoNextPV:1`, arrowDir 2. Right with `GetMovieFrame` ≤ 90 (@0x476fe): `GotoPrevPV`, arrowDir 1. Right later: `ReLoad:` the current movie, arrowDir 3. arrowCnt = 75 in every case. A plain tap does nothing.
* `GotoNextPV:` @0x29678 / `GotoPrevPV` @0x29458 without PLAYLIST: the next/previous `IsPVUnlock` song, (idx ± i) mod total, then `ReLoad`; arg 0 (the movie end) reloads the same movie with LOOP (flag 2), else returns 1 (NextScene 7). **SHUFFLE is not read on this path.** With PLAYLIST (`GetPVModeFlg:0`) the remaining count is decremented and the entry cleared; at 0 it returns 1 for arg 0 without LOOP, otherwise `SetPVStartMusic`, `ReLoad`, `CreatePlayListTbl`; the start is `Rand(total)` with SHUFFLE, else `GetIndex`, and the next pending playlist entry (idx + i) % total is taken.
* `MikuFlickCriManager ReLoad:` @0x28f08: `LoadMovie`, `NoteManager Reset`, `EffectManager Clear`, `SceneManager CancelFade` + `SetFade:0`, `ReplayManager Prepare:0`, main/sub volumes (KARAOKE mutes main in scene 4). The movie restarts in place, with no scene fade.
* `init` @0x467b0 adds the same touch types as `WindowGameWindow init` @0x39498 (9/0 Pause, 10/0 RETRY, 11/2 MUSIC SELECT, 7/1 RESUME), so the pause menu has the game's layout; the "PAUSE" `m_TextPause` is created but never drawn (in the game too). `TouchButton` @0x47364: 7 startGame (SE 8); 9 toggles pauseGame/startGame; 10 `ReLoad` + startGame + `SetUIAlpha:0` + `setFade` (in-place restart with a black fade-in); 11 pauseGame + NextScene 7. `pauseGame`/`startGame` @0x46638/0x466f4: `CriManager::PausePlayer`, SetState 0/3 on the menu buttons and the pause button, `StageManager StopGame/EnableGame`. `SetInit:` @0x47498 pauses when the app goes to the background.
* Render: super, then while `m_Fade` > 0 a black rect (z −9.5, alpha m_Fade, −0.03 per frame, `setFade` @0x46624 / `exec` @0x46dd8); while paused only `menugradation` at (0, 374 + BottomOfs), 640 wide, z −9.3, **with no dimming** (unlike the game); the arrow (SystemTexture 2, y 370, z −9.99, alpha arrowCnt/75, arrowCnt−− after drawing): dir 2 `arrow_forward` at x 453, dir 1 the same sprite mirrored at x 21, dir 3 `arrow_head` (⏮) at x 21. Z order: pause button −1.2 < lyrics −5.1 < band −9.3 < fade −9.5 < menu buttons −9.6..−9.9 < arrow −9.99.
* Port: the playlist editor is not ported. SHUFFLE (only meaningful in PLAYLIST mode in the original) makes the movie end pick `rand() % nsongs` (may repeat) and takes priority over LOOP; swipes stay sequential. `Esc` / Android Back pause (they no longer quit to PV select); `Left`/`Right` act as right/left swipes.

### 6.9 Text and remaining differences

**Device screenshot check (iPhone, 640×960) [V]:** `TexTextManager` draws the text texture top-left at (x, y); capitals start about 0.3 cap-heights lower: result labels (font 25, 20-px capitals) at y + 6, counter digits at table y + 6, the titlebar difficulty (29-px capitals) at 249 for y = 240; title menu labels are 24-px capitals from button y + 24; PLAY / PLAY AGAIN 24-px capitals from y 896, centred. The iOS font is about 7–11 % narrower than M+ 1p Bold (the port condenses Latin text to 0.93, and fits help/licence lines to 512 px).

**Remaining visual differences [V by screenshot comparison]:** text is drawn with SDL_ttf and the bundled M+ 1p Bold (OFL) instead of the iOS system font (Hiragino/Helvetica, regular weight in the licence, counters and credits) that `TexTextManager` used; help text y is placed from the M+ ascent (within about 2 px). Not reproduced: SHOP, the PV playlist editor, REPLAY and Twitter, the Game Center sheets, the tutorial-movie and help-scroll-handle buttons, and the cubes on RESULTS/RECORDS. After a cancelling move, the pressed look of PLAY, Back and MODE CHANGE stays until release (only firing is cancelled). The cover-flow spring constant is tuned by eye **[H]**.

## 7. Still to analyse
`ReplayManager` format, PV-mode lyric rendering details (`TexTextManager`), unlock progression rules, and the rest of the save format (`MikuFlick2.dat`; the music pack part is in §5.14). None of these is needed for core gameplay.
