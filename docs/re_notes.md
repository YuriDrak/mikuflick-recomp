# Reverse-engineering notes: Miku Flick/02 (v1.1.5, armv7)

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
* `m_JustFrame = (int)(T*30)`, then `+= (int)((39.0 / (|m_SpdX| / m_JustFrame)) / 10.0 * MusicData.GetInputTiming)`. This is the user's calibration offset (option `TouchInputTiming`).
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
6. SFX: grade 2 → sound 2 (unless "fail sound" is off), 3 → 0x12, 4 → 1, 5 → 1 (0 with the climax bonus).

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
| 5 | SceneScore | 0xd | SceneAdditionalPackage (shop) |
| 6 | SceneOption | 0xe | ScenePreEnding |

Leaving a scene clears `TexTextManager` slots, touches and windows, then `EffectManager Clear` and `SystemTexture ReleaseTelopTex`. Touches are routed `SceneManager touches{Began,Moved,Ended,Cancelled}::::` → current scene.

### 5.6 Audio tracks [D]
`Load(NSString*)` @0x2176c calls `criManaPlayer_SetSubAudioTrack(player, 1)`, so **both** ADX tracks play at once: track 0 (`pv_NNNvo2`, vocals) is the CRI *main* audio and track 1 (`pv_NNNok2`, backing) is *sub* audio. `AddTensionGauge` drives only `SetMainAudioVolume`, which means the vocals fade as the tension drops. PV mode with KARAOKE (`GetPVModeFlg:4`) mutes main.

### 5.7 Touch → key/direction (`TouchFlickBoard`, `WindowGameWindow init` @0x3939c) [D]
* The keys are created by `WindowGameWindow init` → `WindowBase AddNewTouch:0 :k :pos` for k = 0…9. The per-key data come from `e_FlickBoardPictTbl` @0x13c5b8 = `{u16 srcX, srcY, posX, posY}`.
  Positions in 640×960 are the keys' **top-left corners** (UIKit rect origins; the board is drawn and hit-tested from them): あ(129,530) か(256,530) さ(383,530) / た(129,638) な(256,638) は(383,638) / ま(129,746) や(256,746) ら(383,746) / わ(256,854). Cells are 127×108, so the board covers x 129–510, y 530–962. (An earlier reading took them as centres, which shifted every touch area half a key up and left.)
  Key-face tiles are in `game_tex_02.png`. src = idle face. The lit face of key k is at tile index 9−k in a 4-wide grid of 127×108 cells.
* `touchDown:` stores the begin point and broadcasts `NoteManager setTouchDown` (each live note sets `m_TouchDownFrame = m_Cnt`). `touchMoved:` stores the end point, broadcasts `setTouchUp` and calls `checkFlick`. `touchUp:` does the same, then calls `NoteManager checkResult: board :flick :0` (**judgement happens on release**), `EffectManager AddEffect2D:3` and `ReplayManager AddUnit…`.
* `checkFlick` @0x6ef60: `d = end − begin` in UIKit points (`CloudMath::AdjustPoint` only rescales on iPad HD). A distance below 16 pt (|d|² < 256) is a tap (0). Otherwise `atan2(dx,dy)+π` falls in one of four 90° sectors bounded by π/4·(1,3,5,7), each centred on an axis. The codes match the kana tables: 1 up, 2 right, 3 down, 4 left.

### 5.8 Interlude notes (`NoteInterlude`) [D]
They spawn like normal notes but move at −688/T px/s, and their judge window is a plain `[just−11, just+12)` with no difficulty offset. A miss has no penalty. `checkResult` ignores the key and direction and uses the same Front/Back tables with its own combination rule (port: `check_interlude`). A grade ≥4 plays SE04, adds +1 stage score and counts the hit. When every interlude note of the song has been hit, it adds +39 and plays SE02. During interlude mode the board becomes a single panel (`flickguide_int_*`, 409×350, centred at (256,692)).

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
IDs 0–14 = SE01–SE15, 15 MusicSelect, 16 J01_01, 17 SE01_01, 18 SE01_03, 19 SE02_01, 20 SE04_01, 21 SE05_01, 22–26 BGM01–05 (loop), 27 Na_SEGA_09_keep, 28 Na_Title_A_03_keep, 29 SE04v2 (loop). Judgement SFX: COOL/FINE → 1, COOL with climax bonus → 0, SAFE → 18, SAD → 2 (unless fail-sound is off), interlude hit → 3.

### 5.13 Glyphs (`e_CharPictTbl` @0x139fde, 6-byte rows `{u16 tex, x, y}`) [D]
These are 78×78 tiles. tex 4 = `mf_hiragana_blank`, 6 = `mf_katakana_blank`. The note adds `+enabled`, so a playable note uses the `_telop` sheet (5/7) and a display-only note the `_blank` sheet. `evaluation_00..04` = Cool!!, Fine!, Safe, Sad, Worst (index = 5 − grade).

## 6. Rendering (verified against the iOS screenshots in help_01.png / help_02.png)

**Method.** Ghidra's C output drops the float arguments of draws: iOS armv7 passes floats in core registers (softfp) and the decompiler models them as VFP. Positions were therefore recovered from annotated disassembly (`tools/ghidra_scripts/DumpDisasm.java` → `work/disasm/*.s`). `tools/disasm_calls.awk` propagates constants through movw/movt, VFP immediates, literal pools, s/d aliasing and float add/sub/mul, with `GetBottomOfs` = 0 at 640×960. `tools/calls_named.sh` resolves frame IDs to names.

**Draw primitives [D].** `TPManager RenderTex:<frameId> pTex:<texture> PosX: PosY: PosZ: …`. `frameId` indexes `s_tblTPFrameName` @0x140cb0 (451 names, exported to `work/tpframes.txt`), and the rects come from the plists named in `s_tblTPPlistName` @0x140c5c. Textures come from `SystemTexture GetTex:` / `s_FileName` @0x140ad8: 0 bg_tex_07 (loading), 1 game_tex_01, 2 UI_tex_01, 3 keyboard (game_tex_02, or its `_fullroman`/`_romanhira` variants chosen by `GetKeyBoardType`).
`Cloud3D::RenderTex2DClip` builds its quad from **(x, y) to (x+w, y+h)**, so every position is a **top-left corner**. The exception is `RenderTexRot`, which is centred. `e_SystemPictTbl` @0x139514 (190 × `{sx,sy,w,h,tex}`) is a **stale** legacy table whose rects don't match the shipped textures; the code uses the plists.
The display runs at **30 fps** (`EAGLView animationFrameInterval = 2`), so effect timers advance at the same 30 Hz as PlayCnt.

**Game screen (`WindowGameWindow Render` @0x39db4 and friends), top-left 640×960:**

| element | frames | position |
|---|---|---|
| score | `score_0d`, comma `score_10` | x = `s_tblScorePosX` {393,435,465,495,537,567,597}, y = 8; commas after digits 0 and 3 at x+27; 7 digits, leading zeros hidden |
| high score | `highscore_11` label / `highscore_0d` | label (5,8); digits from x = 129, 14 px stride, y = 7; comma at +13 (+6 advance) |
| judge | `charactercircle` / `charactercircle_int` (interlude) / `outsidecircle` / `insidewhirl` (rot) | (8,421) / (−2,408) / (−9,404) / centre (68,481) |
| lane | `rail_00` stretched to 640 wide; `rail_01` scrolling when combo ≥ 100 | (0,424) |
| combo (≥5) | `combonumber_0d` then `combonumber_11` ("combo") | x from 6, 18 px stride, y = 517 |
| tension | `microphonemeter_00` (dark stand) and `_01` (glowing outline) whole, then `_02` (91×362 coloured fill) clipped from the bottom: fill = 362·gauge/256 + 0.5, drawn at y = 943 − fill (`WindowGameWindow Render` @0x3ad26) | right-handed x = 4 / 4 / 15.05, y 572; left-handed mirrored with `RenderTexClipReverse` at x = 490 / 521 / 534 |
| notes | 78×78 glyph from `mf_{hira,kata}gana_{blank,telop}` | (pos.x − 39, 442); option (small-kana) glyph at +32; culled outside x ∈ [−40,680] |
| direction hint | `inputguide_0N` via `s_tblLastNoteTexIdx` @0x138f38 = TP {278,279,282,280,281}: flick code (tap, up, right, down, left) → sprite 00/01/04/02/03 (the sprites are ordered tap, up, down, left, right) | (26,440) while the next note is within 11 ticks |
| timing ring | `inputtarget` (scale 0.48…1 by ticks-to-just), `inputperfect` within 2 ticks | centre (69,482) |
| keys | `flickpanel_ori_off_k` idle / `flickpanel_ori_on_k` pressed (frame 416/426 + k) | `e_FlickBoardPictTbl` pos (top-left): あ(129,530)…わ(256,854) |
| key guides | `NoteNormal render` @0x22a38, every active note with cnt ≤ just (unless PANEL GUIDE is off): `guidepanel_off_*` (`s_tblKeyGuideTexIdx` @0x139460) or, for the next target, `guidepanel_on_*` (`_Next` @0x139474) plus `guidecursor` at key − (3,3); same 00/01/04/02/03 mapping as the hint | on the note's key, scaled by cnt/just about the panel rect (0 below 0.25); older notes in front |
| note ring | `emphasiscircle` on every active note (not only climax ones) | centre (pos.x + 1, 481), angle π − m_Rot/100 rad, m_Rot = (m_Rot + 21) % 360 per tick (`NoteNormal exec`) |
| climax note | glyph tinted `s_tblRainbow` @0x139310 [(m_CrimaxCnt + 10) % 28] (28 RGB floats/255) | only while combo ≥ 100 |
| flick flower | `-[TouchFlickBoard RenderGuide:]` @0x6f860: with no flick yet, all petals `flickguide_off_0p` (only with FLOWER on); once the finger has a direction, only that petal, `flickguide_on_0p`, whatever the option; petal p = up/down/left/right → TP 312..315 (+4 lit), glyph `fgcharacter_ori_N` from `e_GuideCharPictIdx` @0x139f78 [key][flick code] | petal/glyph offsets from the key's top-left: up (7,−109)/(36,−85), down (7,82)/(36,136), left (−127,3)/(−90,26), right (99,3)/(160,26) |
| interlude panel | `flickguide_int_off/on` | (key0.x − 15, key0.y) = (114,530) |
| judgement popup | `evaluation_(5−g)` | centred (66, 483 + ofsY); ofsY = −`s_tblEffectResultOfsY`[g]·sin(t/20·π/2) − 80·(1−0.5ᵗ); size 0.1 + 0.15/tick (max 1); 20 ticks, fades over the last 5 |
| ripple / interlude ♪ | `ripple`, `quaver_01` | centred near (70,482) |

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

**Title menu (`TouchTitleMenus` @0x6a428..) [D]:** labels RHYTHM GAME / PV / OPTIONS / SHOP (types 2, 3, 5, 0x2c → index 0–3, font 34 px). `menubutton_l_base` (394×82) sits at (x, 538 + 82·i). Buttons start at x = 640 + 128 + 20·i and slide −100 px/tick to x = 128. The face `menubutton_l_off/on` (380×68) is inset by 7, and `parenthesis_00/01` (39×68) bracket the focused button. The crown (`crownicon`, Game Center) sits at (14,~878) and info (`infoicon`) in the opposite corner.

**Title (`WindowTitle RenderTitle` @0x51678):** bg_title (0,0); miku (−10, −3 + bob); outerkey_b (63,411), _p (535,148), _r (434,533), _t (115,76); cubes_p (472,103), cubes_y (0,48); titlelogo_00 (12,242), _01 (320,242); copyright (99,922); touchstart (172,662), blinking.

**Music select (`WindowMusicSelect` @0x405cc.., `WindowCover` @0x36070.., `TouchMusicSelect` @0x6d160.., `WindowBGEffect`/`EffectBGEffect`) [D]:**
* Static: bg (0,0), baseline (0,824), "HIGH SCORE:"+"BPM:" texts centred side by side at y = 660. Logo (`WindowCover renderLogo` @0x36a0c) at its native 512×256 at (64,43), z −3, no cross-fade. Stars (`Render` @0x41cd4): 9 × `difficultyemblem_00` (filled, i < stars) / `_01` (40×40) at (166 + 34i, 614); Break the Limit draws none (`RenderClearStarBTL`). Difficulty labels (`TouchDifficulty updateText:` @0x700f8) at x = 20/174/328/482, y = 799 (99/247/398 while Extreme is locked); Break the Limit shows `difficulty_on_04` at (208,799) under `difficultycursor` stretched to 328×66 at (155,776.25). `difficultycursor` 164×66 at (`m_CsrX`, 776.25); `setDifficulty::` @0x3fd3c targets {7,161,315,469} (table @0x13bfb0; {86,234,386} locked), `m_CsrSpdX` = (target − x)/3 decaying ×0.9 per tick. Clear-rank marks (`RenderClearStar` @0x41e94 → `RenderRankMarkByRank:texIdx:posX:posY:` @0x4204c, 35×35 top-left): rank 0 `clearrank_star`, 1 `_s`, 2 `_a`, 3 `_b`, 4 `_c` at (116/300/430/602, 783) (table @0x13bfcc); Break the Limit one mark at (430,783). MODE CHANGE (`TouchModeChange`, m_Pos (118,678)) is drawn at (118,698), `modechange_on` while pressed.
* Cover flow (`updateTitle::` @0x4017c): cover i's centre x starts at 280 and advances 40 per cover, 80 on either side of the selection (cover 0 gets +40 when selected). It is drawn at `x − m_ScrPos`, and `m_ScrPos` follows a `CloudSimpleSpring` toward `sel·40` with swipe dragging and rubber-band limits. `setDirection::` @0x36758 sets the stretch targets: selected = flat; left covers = left edge (0,+10), right edge (−230,+60); right covers = left edge (+230,+60), right edge (0,+10) (x shift, top/bottom inset). `WindowCover exec` eases position and stretch 15 % per tick while `m_StretchTimer` runs. `Render` @0x36290 draws a 316×316 quad around (x, ~420) plus a 120-px reflection, nearer covers on top.
* PLAY (`TouchMusicSelect` init @0x6d160, Render @0x6d510): m_Pos (164,872.25); `menubutton_m_base` 324×82 there, face `menubutton_m_off`/`_on` inset 7, scaled ×1.05 while pressed, the label colour pulsing on a 22-step counter (50 ms), `parenthesis_00/01` at (165,874) and (165 + 271·scale, 874).
* Back (`TouchBack initType:No:Pos:` type 4 @0x6de3e, Render @0x6e270; also PV select and options): m_Pos (17,872.25), `menubutton_s_base` 129×82, face `menubutton_s_off/on` inset 7, `back` at ≈ (51,890).
* PV select (`TouchPVLoopMode update:` @0x717e0): the 109×93 toggles PLAYLIST/LOOP/SHUFFLE/LYRICS/KARAOKE at x = 25 + 120i, y = 742. `TouchPVProgram` @0x7388c adds a 112×104 button at (265,612) (not ported).
* Background cubes (`WindowBGEffect exec`: 3 particles every 100 ticks; `EffectBGEffect initWithType::` @0xbff4): x = 50 + rand(540), y = 1060, v = (rand(100)/100 − 0.5, −(rand(50)/100 + 0.5)), speed = (rand(50)/100 + 0.5)·1.2 per axis, rotation ±`_RotSpeed` (0.005984 rad/tick), `bgeffect_00..04` drawn additive (blend 2) with RenderTexRot; removed once off-screen.


**Result (`WindowResult Render` @0x4e8a8 / `RenderScore` @0x4dbdc / `RenderBgRect` @0x4e610 / `exec` @0x4d6ac) [D]:** depth-sorted by PosZ (smaller = in front).
* bg (0,0) z 2. Panel: 9-slice `resultwindow_00..03` corners at (81,223), (482,223), (81,739), (482,739); `_04`/`_05` top/bottom edges at x = 167…419 (63 px), `_06`/`_07` left/right edges at y = 298…676; centre = `RenderTexClip` of a 2×2 texel block inside `_05` stretched to 338×441 at (144,298). Spans (81,223)–(576,841).
* jacket (90,67) 142² z −0.2; logo (279,67) 256×128 z −0.3; titlebar (104,238) z −0.5 with the difficulty text ("Easy" … "Break The Limit") centred on x = 320, y = 240.
* difficulty stars (not for Break the Limit): `resultstat_00` ×9 at (290 + 26i, 191) z −1.0, then `resultstat_01` (`_02` if GetDifficultNum < 0) for the star count z −1.1.
* labels (TexTextManager, z −1.1, plus a (+2,+2) shadow at z −1.0): Total Notes (124,308), Combo (124,346), Cool/Fine/Safe/Sad/Worst (206, 394/432/470/508/547), "♪" (206,597), Stage Score (131,644), Combo Bonus (131,682), Total Score (132,720) (movw/movt pairs, e.g. 0x4408c000 = 547). Values are `WindowCounter` children with `SetRightTopPos` from the table @0x13c0c8 (y − 6): right edge 340 (rows 1–2), 435 (judgements, interlude), 510 (scores); tops 308, 346, 394, 432, 470, 508, 547, 597, 644, 682, 720. They count up; `m_NowCounterIndex` reaches 11 when all are done.
* `RenderUnderLine:x:y:xw:yh:` stretches the 4×23 `textbar` sprite into 23-px strips behind the text (z −0.9): `textbar_00` x 114 w 233 at y 316, 354; `textbar_01` x 181 w 278 at y 403, 441, 479, 517, 555, 606; `textbar_00` x 114 w 412 at y 652, 690, 728.
* After the counters: `clearrankstamp` (349,224) z −0.1 (behind the titlebar and text); `clearrank_NN` (frame 110 + rank) at (394,298) z −1.0; `perfect` (383,291) z −0.1 for rank 0.
* `exec` state 0 → 1 after 46 ticks with the counters done (rank SE). `m_ContentOpen` (set in `loadTexture` for ranks 0–4) = cleared. In state ≥ 1, if not cleared and the difficulty is not Break the Limit, `failed` (256×160) is drawn with `m_FailSizeRate` (0 → 1, +0.2/tick) scaled about the centre of (359,319)–(615,479), z −3.5. So FAILED shows for rank D as well as E. In state ≥ 4 with a new high score, `newrecord` (152²) pops in the same way about the centre of (461,328), z −3.6.

**Title extras (`WindowTitle eyeAction:` @0x51940, `RenderTitle` @0x51678) [D]:** Miku is drawn at (−10, bob − 3) with bob = 5·sin(BlinkTime·113.04). BlinkTime counts ticks and 113.04 rad per tick aliases to about −0.057 rad/tick, a cycle of about 3.7 s. Once song 9 is unlocked she blinks every 200 ticks: ticks 1–2 show `eye_00`, 3–4 `eye_01`, at (320, bob + 109). The `outerkey_*` decorations are static.

**Pause (`WindowGameWindow pauseGame` @0x39138, `PauseRenderFunc_Enable` @0x38ff4) [D]:** a dimmed screen with `menugradation` stretched to 640 wide at y = 374, then three `menubutton_m` buttons at x = 164: RESUME (`TouchBack` No 1, y = 574), RETRY (`TouchRetry`, type 10, y = 656) and MUSIC SELECT (`TouchRetry` No 2, type 11, y = 740), with font 32. `TouchBack Render` @0x6e27e draws them like music select PLAY: `menubutton_m_base`, the pulsing additive `menubutton_m_off` face (`_on` while pressed), and `parenthesis_00/01` on every button (not only a focused one); all rects scale 1.05× about their centre while pressed.

**Options (`WindowOption setMode:` @0x42cb4, `init` @0x43eb4, `Render` @0x44504) [D]:** modes 0 OPTIONS, 1 AUDIO, 2 HANDEDNESS, 3 KEYBOARD, 4 HELP, 5 CREDITS, plus DISPLAY (`OPTION_TITLE_04`). The title is at y ≈ 182 and the subtitle (`Localizable OPTION_TITLE_0N`) at y ≈ 234. Touch types (table index − 1): 0x0e SoundVolume, 0x0f KeyBoard, 0x10 Help, 0x11 Credit, 0x12 Display, 0x13 SoundVolumeMeter ×2, 0x14 HandSide ×2, 0x16 HelpPage, 0x1d Handedness, 0x1e KeyBoardSelect ×3, 0x1f PanelSelect ×2, 0x30 FlowerUse ×2, 0x3a ChangeAspect, 0x3b PauseEnable, 0x3c FailSoundEnable. Art: AUDIO `volumewindow` at (81,303) with `speaker` and `volumemeter_on/off`; KEYBOARD/DISPLAY layout below. CREDITS is a 640×960 page in the top-left of the `credits_*` texture. The KEYBOARD option switches the key texture to `game_tex_02` / `_fullroman` / `_romanhira` (`SystemTexture LoadBoardTex`, same frame layout). Main list (`TouchCommonButton` @0x63ee0): `menubutton_l_base` at x = 129, y = 384 + 82i (the movw @__mh_execute_header low half is 0x4000, so 628/792 read earlier were 630/794). AUDIO (`TouchSoundVolume` init @0x76d18, Render @0x77020): 16 × `volumemeter` (25×34) at x = 113 + 26i, rows y = 583 (BGM) and 664 (SE); lit cells = (int)(vol·15 + 1), none at 0 (SE vol is divided by 0.75 first); row label centred at (320, y − 42); hit box 440×84. FAILED SOUND (`TouchFailSoundEnable` @0x7cef8): ON (No0) at (120,740) and OFF (No1) at (320,740), 199×45; `TP_FAILEDSOUND` = {on_w, on_b, off_w, off_b}: the selected button uses the light `_w` face. HELP draws `helpline` (640×12) at (0,60). KEYBOARD (emulator capture): `keyboard_{ori,rah,fro}_off` (196×255) at x = 20/222/424, y = 304, in that slot order (port value 0 ori, 1 fro, 2 rah); the selected panel uses its `_on` face (247×315) at slot − (26,28). `panelguideline` at (35,643) with PanelSelect ON `panelguide_on_{on,off}` (322,601) / OFF `panelguide_off_{off,on}` (449,601), 129×110; `flowerline` at (38,779) with FlowerUse ON `flowet_on_*` (300,722) / OFF `flowet_off_*` (428,722), 170×145. DISPLAY (`UI_tex_09`, no plist; rects in code): TouchChangeAspect s_Pos @0x13c608 = (136,310),(156,333),(339,301),(359,324), s_TexPos @0x13c628 = {290,0,165,293},{0,266,125,247},{125,0,165,311},{0,0,125,265}; on a 3:2 device only the 3:2 phone shows, at (240,310), label "3:2 ASPECT" at y 610. TouchPauseEnable (Render @0x7c6e0) s_Pos @0x13c668 = (322,696),(336,711),(459,696),(473,711), s_TexPos @0x13c688 = {125,311,135,115},{395,386,108,93},{260,311,135,115},{395,293,108,93}, index 2·No + !selected (No0 VISIBLE, No1 INVISIBLE); label centred under each at y = h + 705 (grey 0.8 when not selected); "DISPLAY PAUSE" at (60,744); a 578×3 divider at (31,783) behind the previews. Text textures put the capitals about 6 px below their y. The original has no input-timing control: the port's (inputbar/inputcursor) sits at the bottom of DISPLAY. HANDEDNESS `TouchHandSide` @0x72f1c: m_Pos y = 345, x = 37 / 338; Render @0x73100 draws the selected `_on` face at m_Pos − (14,35) and the unselected right hand at x − 6 (verified on the emulator).

**PV viewer (`ScenePVSelect` / `ScenePVView`, `CuePointFunc_Lyrics`, `NoteManager render_Lyrics` @0xda64) [D]:** the lyric line (op 3) is only applied in scene 4 with `GetPVModeFlg:1` (LYRICS) and drawn at the bottom, y ≈ 896 (shadow 898), within x 15–623. KARAOKE (`GetPVModeFlg:4`) sets the CRI main (vocal) volume to 0. The select screen uses the music-select cover flow with the toggles `playlist`/`loop`/`shuffle`/`lyrics`/`karaoke_on/off` (109×93) and `saisei` on the jacket (help_02 panel 4). In the port, LOOP replays and SHUFFLE picks a random next PV; the playlist editor is not ported. `WindowPVSelect`: "SONG SELECTED", the `TouchPVProgram` play mark (112×104 at (265,612)) and SHUFFLE are only shown with `GetPVModeFlg:0` (PLAYLIST), which the port does not implement, so they are hidden; `StatusData` init zeroes all PV flags, so LYRICS starts off.

**Device screenshot check (iPhone, 640×960) [V]:** `TexTextManager` draws the text texture top-left at (x, y); capitals start about 0.3 cap-heights lower: result labels (font 25, 20-px capitals) at y + 6, counter digits at table y + 6, the titlebar difficulty (29-px capitals) at 249 for y = 240; title menu labels are 24-px capitals from button y + 24; PLAY / PLAY AGAIN 24-px capitals from y 896, centred. The iOS font is about 7–11 % narrower than M+ 1p Bold (the port condenses text to 0.93). Counters group thousands with a comma ("18,572"). The result screen has `TouchBack` (17,872), PLAY AGAIN on the music-select PLAY button (164,872), REPLAY ("RECORD THIS GAME", 180–465 × 765–808), the flick-result board button (≈531,785) and Twitter (507,872); the port has Back and PLAY AGAIN only. Title menu (`TouchTitleMenus Render` @0x6abb4): the face is drawn additive (blend 2) and both brackets are drawn on every button. Music select: the cover top is `m_Pos.y` = 293 (`Vector2D(x, 293)` @0x40f14), so the 316-px jacket spans y 293–609. The right-hand bottom button on music select is `playlist` (not ported).

**Remaining visual differences [V by screenshot comparison]:** text is drawn with SDL_ttf and the bundled M+ 1p Bold (OFL) instead of the iOS system font (Hiragino/Helvetica) that `TexTextManager` used. Not reproduced: SHOP, the PV playlist editor, the original HELP text pages (the port shows the annotated help screenshots) and the `m_StretchRate` wobble of the popup. The cover-flow spring constants are tuned by eye **[H]**.

### 5.14 Still to analyse
`ReplayManager` format, the exact on-screen layout of the menu windows, PV-mode lyric rendering (`TexTextManager`), unlock progression rules, and the save format (`MikuFlick2.dat`). None of these is needed for core gameplay.
