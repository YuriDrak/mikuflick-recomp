# Archive layout: `com.sega.MikuFlick02-iOS5.0-(Clutch-2.0.4).ipa`

Status: **milestone 1 (extraction + inventory)**. Tags: **[V]** = verified with the evidence shown, **[H]** = hypothesis.

## How this was produced

| Step | Command | Output |
|---|---|---|
| Path-safety check + listing | `unzip -l` / `unzip -Z1` + grep for `/` and `..` | `work/ipa_listing.txt` (193 entries, no unsafe paths) |
| Extraction | `unzip -q -o <ipa> -d extracted/ipa` | `extracted/ipa/` (981 MB) |
| Mach-O load commands | `build/machoinfo` (own C tool, `tools/machoinfo/machoinfo.c`) | `work/macho_loadcmds.txt` |
| Symbol names | LC_SYMTAB string table sliced with `tail`/`head` | `work/symbols.txt` (15,109 unique names) |
| Codec probing | `ffprobe` (system ffmpeg) | inline below |

You can rerun it all with `sh tools/extract_ipa.sh "<ipa>"` from the project root.

Host tools: `unzip`, `7z`, `file`, `ffmpeg/ffprobe`, `gcc`, `make`, `cargo`. **Not installed:** clang, cmake, otool/llvm-objdump, rizin/radare2, Ghidra, xxd.

## Top level

```
iTunesArtwork                     PNG store artwork (174 KB)
Payload/MikuFlick2.app/           the app bundle (192 files)
```
There's no `iTunesMetadata.plist` and no `SC_Info/`. Clutch left only the re-signed `_CodeSignature/` and `archived-expanded-entitlements.xcent`.

## Info.plist (binary plist) [V]

| Key | Value |
|---|---|
| CFBundleIdentifier | `com.sega.MikuFlick02` |
| CFBundleExecutable | `MikuFlick2` |
| CFBundleShortVersionString | `1.1.5` |
| MinimumOSVersion | `5.0` |
| DTSDKName / DTXcode | `iphoneos8.0` / `0600` (Xcode 6.0, build 6A317) |
| UIDeviceFamily | 1, 2 (iPhone + iPad) |
| UIRequiredDeviceCapabilities | `gamekit`, `opengles-2`, `armv7` |
| Orientation | Portrait, PortraitUpsideDown |
| URL scheme | present (`Editor` role); name not decoded yet |

## Main binary `MikuFlick2` [V]

* `Mach-O armv7 executable`, thin (not FAT), 2,421,392 bytes, flags `PIE|TWOLEVEL|DYLDLINK`.
* Entry point: LC_UNIXTHREAD, pc `0x0000bfc0` (= start of `__text`, ARM mode `start` stub).
* `LC_ENCRYPTION_INFO`: cryptoff `0x4000`, cryptsize `0x138000`, **cryptid 0**. The range's entropy is 6.89 bits/byte, which is typical of ARM/Thumb code. AES-encrypted pages would be about 7.99. So **Clutch fully decrypted `__TEXT`**, and the binary is ready for static analysis.
* UUID `96B378F9-551D-39DD-A256-1400E68E039C`.
* **Not stripped:** 27,637 nlist entries (24,949 local, 2,205 external defined, 483 undefined). Every ObjC method, C++ function (Itanium-mangled) and CRI middleware symbol has a name.
* Has LC_FUNCTION_STARTS (7,712 bytes) and LC_DATA_IN_CODE (2,800 bytes), which helps disassemblers split code from literal pools.

Segment map (vmaddr = file offset + 0x4000):

| Segment | VM range | Notes |
|---|---|---|
| `__TEXT` | 0x4000–0x140000 | `__text` 0xbfc0 (1.04 MB code), `__objc_methname`, `__cstring` 0x126e30 (+0x12075), `__const`, `__ustring` (UTF-16 Japanese text) |
| `__DATA` | 0x140000–0x1a4000 | full ObjC2 metadata: `__objc_classlist` (191 classes), `__objc_selrefs`, `__objc_ivar`, `__cfstring`, `__mod_init_func` (27 C++ static initializers) |
| `__LINKEDIT` | 0x1a4000– | dyld info, symtab, code signature |

Linked frameworks: UIKit, Foundation, CoreFoundation, CoreGraphics, QuartzCore, **OpenGLES**, **OpenAL**, **AudioToolbox**, **AVFoundation**, CoreMedia, GameKit, StoreKit, Twitter, Accounts, AdSupport (weak), MessageUI, CFNetwork, Security, SystemConfiguration, CoreLocation, libxml2, libz, libstdc++, libobjc.

Statically linked third-party code (from symbol names): **CRI Mana** (USM video), **CRI Atom/ADX2** (audio), the NoahSDK 1.5.1 ad/offer SDK, MGTwitterEngine and OAuthConsumer (Twitter), ZipArchive (DLC packs), and Reachability.

## Bundle inventory

### Song movies: CRI USM (`*.usm`) [V]
12 files, 3–106 MB each: `cloverclub`, `ending`, `hajimete_no_oto`, `hatsune_miku_no_gekisyou`, `Just_be_friends`, `koi_wa_sensou`, `magnet`, `migikata_no_tyou`, `promise`, `roshin_yuukai`, `tajyuumirai_no_quartet`, `ura_omote_lovers`.

`magnet.usm` according to ffprobe: **MPEG-1 video 320×480 @ 20 fps** plus **2 ADX streams** (44.1 kHz stereo), 186.0 s long. The CRID directory names the original source files as `magnet_320x480_20f.avi`, `pv_044vo2.aif` and `pv_044ok2.aif`. These are probably **vocal** and **instrumental** tracks **[H]**. The "fail sound" option (`TouchFailSoundEnable`) may switch between them.

Chunk counts in `magnet.usm`: `CRID`×1, `@SFV`×1590, `@SFA`×4756, `@CUE`×5, `@UTF`×7. `ending.usm` has **no `@CUE`**: it is a plain movie with no chart.

**The beatmap lives inside the USM** [V]. A `@CUE` chunk (file offset 8384 in `magnet.usm`) holds a CRI `@UTF` table:
```
CUEPOINT_HDRINFO { time_unit, total_points, metadata_count, metadata_size, ... }
CUEPOINT_INFO    { name, time, cue_type, parameter }
rows: name="note" parameter="501111" | "500111" | "500011" | "501000" | ...
      plus non-note rows with values such as "72600", "8108"
```
The binary consumes these through `criManaPlayer_SetCuePointCallback` → `CuePointCallback(void*, CriManaPlayerTag*, CriManaEventPoint*)` → `s_tblCuePointFunc`. That table dispatches to handlers named `CuePointFunc_{Lyrics, Telop, SmallTelop, Crimax, Interlude, SetBPM, SetDelay, FadeIn, FadeOut}`, and those feed `+[NoteManager AddNote::::]`. **[H]** The digits in `parameter` encode the note and character/flick type.

### Preview loops: CRI ADX (`pv_NNN_lp.adx`) [V]
11 files: `pv_001, 042, 044, 054, 056, 061, 062, 065, 066, 082, 102`. They are ADX v3, 44.1 kHz stereo, looping, about 26 s each. The `pv_NNN` IDs match the Project DIVA song numbering (magnet = pv_044). These are the song-select previews (`-[MusicData GetPreSoundFileName]`).

### SFX / BGM: Core Audio Format (`*.caf`) [V]
All of them use `adpcm_ima_qt` (IMA4) at 44.1 kHz stereo, which ffmpeg decodes.
* BGM: `BGM01`–`BGM05`, `MusicSelect`
* Voice / jingles: `Na_SEGA_09_keep` (SEGA logo voice), `Na_Title_A_03_keep` (title call), `J01_01`
* SE: `SE01`–`SE16` plus the variants `SE01_01`, `SE01_03`, `SE02_01`, `SE04_01`, `SE04v2`, `SE05_01`

### Textures: PNG + atlas plist [V]
98 PNGs, mostly 1024×1024 RGBA (no PVRTC). The atlas descriptors are binary plists in a Zwoptex/cocos-style frame dictionary **[H]**, still to be decoded:
* UI: `UI_tex_01..09` (`.plist` for 01–07)
* Gameplay: `game_tex_01/02` (+ `_fullroman`, `_romanhira` variants for romaji display modes), `game_effect_01..05`
* Flick keyboards: `mf_hiragana_{blank,telop}{,_fullroman,_romanhira}`, `mf_katakana_{blank,telop}`
* Backgrounds: `bg_tex_01..09`. Song select: `music_00_01..04`, `music_01_01`, `music_02_01`, `thum_01_01`, `thum_02_01`
* Misc: `credits_{en,ja}`, `ending`, `help_01/02`, `logo_{cri,crypton,sega}`, `saisei`, `twitter_load`, `ipad_top`, plus icons and launch images

### UI / localisation / misc
* `*.nib` (binary plist): `MikuFlick2ViewController{,-4inch,-iPad}`, `DebugViewController`. These are the UIKit host views; the game itself is drawn with GLES2 through `EAGLView`/`ES2Renderer`.
* `en.lproj/`, `ja.lproj/`: `Localizable.strings` (binary plist, about 41 KB each) and `InfoPlist.strings`
* `jQueryInject*.txt` (web-view helper, NoahSDK), `NoahSDK1.5.1.txt` (empty marker), `ResourceRules.plist`, `PkgInfo`

## Not in the bundle
* **Downloadable song packs** (`ExPackManager`, `FileDownloader`, `%@%d.zip`, `%@ShopList_enc.txt`). These were fetched from SEGA servers and unzipped into Documents at runtime. Only the songs bundled in the IPA can be ported.
* Save data: `MikuFlick2.dat` (NSKeyedArchiver, keys such as `m_TopScore%d`, `m_TotalNotes%d`) under the app's Documents folder.

## Next steps
1. Write a native (C) USM demuxer plus a `@UTF` table parser, so every `CUEPOINT_INFO` row of each song can be dumped to text (`work/charts/*.tsv`).
2. Install a disassembler inside the project (rizin static build or Ghidra under `tools/`) and decompile the `CuePointFunc_*` handlers, `NoteManager AddNote/checkResult` (timing windows, scoring) and `SceneManager` (state machine).
3. Decode the atlas plists and `Localizable.strings`.
4. Start `docs/re_notes.md` and `docs/linux_port_plan.md` from those results.
