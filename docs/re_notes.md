# Reverse-engineering notes: Miku Flick/02 (v1.1.5, armv7)

Tags: **[V]** verified (evidence given), **[H]** hypothesis, **[D]** derived from decompilation (see `work/decomp/`).
Addresses are VM addresses in `MikuFlick2` (image base 0x4000 = file offset 0). A trailing `+1` on a pointer means Thumb.

## 0. Tooling used

| Tool | Version / source | Purpose |
|---|---|---|
| `tools/machoinfo` (own C) | — | load commands, `-s` symbols with addresses, `-w` read words at VM address, `-x` hexdump |
| `tools/usmdump` (own C) | — | CRI USM chunk walker, `@UTF` table dumper, `-c` cue-point TSV export |
| `tools/bplist2json` (own C) | — | bplist00 → JSON (atlases, Localizable.strings, nibs, Info.plist) |
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
| tension | `microphonemeter_00` / `_01` (clipped from the bottom by the gauge) | (4,572); the left-handed layout uses x = 490…534 |
| notes | 78×78 glyph from `mf_{hira,kata}gana_{blank,telop}` | (pos.x − 39, 442); option (small-kana) glyph at +32; culled outside x ∈ [−40,680] |
| direction hint | `inputguide_0N` (`s_tblLastNoteTexIdx`) | (26,440) while the next note is within 11 ticks |
| timing ring | `inputtarget` (scale 0.48…1 by ticks-to-just), `inputperfect` within 2 ticks | centre (69,482) |
| keys | `flickpanel_ori_off_k` idle / `flickpanel_ori_on_k` pressed (frame 416/426 + k) | `e_FlickBoardPictTbl` pos (top-left): あ(129,530)…わ(256,854) |
| next-key guide | `guidepanel_on_<dir>` + `guidecursor` | on the key of the next target note |
| flick flower | `flickguide_{off,on}_0p` + `fgcharacter_ori_N` (`e_GuideCharPictIdx` @0x139f78) | petal/glyph offsets from the key's top-left: up (7,−109)/(36,−85), down (7,82)/(36,136), left (−127,3)/(−90,26), right (99,3)/(160,26) |
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
* Static: bg (0,0), baseline (0,824), "HIGH SCORE:"+"BPM:" texts centred side by side at y = 660, difficulty labels with a 155-px pitch (row centre y = 810), `difficultycursor` 164×66 at (`m_CsrX`, 777). `m_CsrX` moves by `m_CsrSpdX`, which decays ×0.9 per tick until it reaches 155·diff.
* Cover flow (`updateTitle::` @0x4017c): cover i's centre x starts at 280 and advances 40 per cover, 80 on either side of the selection (cover 0 gets +40 when selected). It is drawn at `x − m_ScrPos`, and `m_ScrPos` follows a `CloudSimpleSpring` toward `sel·40` with swipe dragging and rubber-band limits. `setDirection::` @0x36758 sets the stretch targets: selected = flat; left covers = left edge (0,+10), right edge (−230,+60); right covers = left edge (+230,+60), right edge (0,+10) (x shift, top/bottom inset). `WindowCover exec` eases position and stretch 15 % per tick while `m_StretchTimer` runs. `Render` @0x36290 draws a 316×316 quad around (x, ~420) plus a 120-px reflection, nearer covers on top.
* PLAY (`TouchMusicSelect`): `menubutton_m_base`/`_off`/`_on`, scaled ×1.05 while pressed, the label colour pulsing on a 22-step counter (50 ms), `parenthesis_00/01` at (165,874) and (436,874).
* Background cubes (`WindowBGEffect exec`: 3 particles every 100 ticks; `EffectBGEffect initWithType::` @0xbff4): x = 50 + rand(540), y = 1060, v = (rand(100)/100 − 0.5, −(rand(50)/100 + 0.5)), speed = (rand(50)/100 + 0.5)·1.2 per axis, rotation ±`_RotSpeed` (0.005984 rad/tick), `bgeffect_00..04` drawn additive (blend 2) with RenderTexRot; removed once off-screen.


**Result (`WindowResult Render` @0x4e8a8 / `RenderScore` @0x4dbdc):** bg (0,0); logo (279,67) at 256×128; jacket 142²; titlebar (104,238); rows at y = 308, 346, 394, 432, 470, 508, 547, 597, 644, 682, 720 with labels at x ≈ 124 / 206 / 131; clearrankstamp (349,224); perfect (383,291).

**Title extras (`WindowTitle eyeAction:` @0x51940, `RenderTitle` @0x51678) [D]:** Miku is drawn at (−10, bob − 3) with bob = 5·sin(BlinkTime·113.04). BlinkTime counts ticks and 113.04 rad per tick aliases to about −0.057 rad/tick, a cycle of about 3.7 s. Once song 9 is unlocked she blinks every 200 ticks: ticks 1–2 show `eye_00`, 3–4 `eye_01`, at (320, bob + 109). The `outerkey_*` decorations are static.

**Pause (`WindowGameWindow pauseGame` @0x39138, `PauseRenderFunc_Enable` @0x38ff4) [D]:** a dimmed screen with `menugradation` stretched to 640 wide at y = 374, then three `menubutton_m` buttons at x = 164: RESUME (`TouchBack` No 1, y = 574), RETRY (`TouchRetry`, type 10, y = 656) and MUSIC SELECT (`TouchRetry` No 2, type 11, y = 740), with font 32.

**Options (`WindowOption setMode:` @0x42cb4, `init` @0x43eb4, `Render` @0x44504) [D]:** modes 0 OPTIONS, 1 AUDIO, 2 HANDEDNESS, 3 KEYBOARD, 4 HELP, 5 CREDITS, plus DISPLAY (`OPTION_TITLE_04`). The title is at y ≈ 182 and the subtitle (`Localizable OPTION_TITLE_0N`) at y ≈ 234. Touch types (table index − 1): 0x0e SoundVolume, 0x0f KeyBoard, 0x10 Help, 0x11 Credit, 0x12 Display, 0x13 SoundVolumeMeter ×2, 0x14 HandSide ×2, 0x16 HelpPage, 0x1d Handedness, 0x1e KeyBoardSelect ×3, 0x1f PanelSelect ×2, 0x30 FlowerUse ×2, 0x3a ChangeAspect, 0x3b PauseEnable, 0x3c FailSoundEnable. Art: AUDIO `volumewindow` at (81,303) with `speaker` and `volumemeter_on/off`; DISPLAY `panelguideline` at (35,319) and `flowerline`; input timing uses `inputbar`/`inputcursor` at x = 100. CREDITS is a 640×960 page in the top-left of the `credits_*` texture. The KEYBOARD option switches the key texture to `game_tex_02` / `_fullroman` / `_romanhira` (`SystemTexture LoadBoardTex`, same frame layout). In the port, the main-list button positions and the AUDIO/HAND/KEYBOARD control positions come from the screenshots **[H]**.

**PV viewer (`ScenePVSelect` / `ScenePVView`, `CuePointFunc_Lyrics`, `NoteManager render_Lyrics` @0xda64) [D]:** the lyric line (op 3) is only applied in scene 4 with `GetPVModeFlg:1` (LYRICS) and drawn at the bottom, y ≈ 896 (shadow 898), within x 15–623. KARAOKE (`GetPVModeFlg:4`) sets the CRI main (vocal) volume to 0. The select screen uses the music-select cover flow with the toggles `playlist`/`loop`/`shuffle`/`lyrics`/`karaoke_on/off` (109×93) and `saisei` on the jacket (help_02 panel 4). In the port, LOOP replays and SHUFFLE picks a random next PV; the playlist editor is not ported.

**Remaining visual differences [V by screenshot comparison]:** text is drawn with SDL_ttf and the bundled M+ 1p Bold (OFL) instead of the iOS system font (Hiragino/Helvetica) that `TexTextManager` used. Not reproduced: SHOP, the PV playlist editor, the original HELP text pages (the port shows the annotated help screenshots) and the `m_StretchRate` wobble of the popup. The cover-flow spring constants are tuned by eye **[H]**.

### 5.14 Still to analyse
`ReplayManager` format, the exact on-screen layout of the menu windows, PV-mode lyric rendering (`TexTextManager`), unlock progression rules, and the save format (`MikuFlick2.dat`). None of these is needed for core gameplay.
