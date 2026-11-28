# Android build

The Android app runs the same C11 + SDL2 code as the Linux port (`../port/src`)
and the iOS port (`../ios/`),
built with the NDK through CMake and wrapped in SDL's Java activity. Like the
Linux build, it ships **no SEGA/Crypton data**: you import the game data from
your own decrypted IPA on the device (or copy an existing extraction from a PC).

## Requirements

- JDK 17 or newer, `curl`, `unzip`, `adb`.
- About 4 GB of free disk space. Everything goes into `android/third_party/`
  (gitignored): the SDK command-line tools, NDK r27c, CMake 3.22.1,
  platform/build-tools 35, Gradle 8.9 and its caches, the SDL2 / SDL2_image /
  SDL2_ttf sources, the debug keystore and temporary files. Nothing is written
  to `~/.gradle` or `~/.android`.
- Paths with spaces are fine. Google's `sdkmanager` launcher script breaks on
  them, so the Makefile calls its Java entry point directly, and the native
  code is built with CMake (`ndk-build` refuses such paths).

## Build

```sh
make -C android toolchain   # downloads into android/third_party/
make -C android licenses    # read and accept the Android SDK licences yourself
make -C android sdk         # installs NDK, CMake, platform 35, build-tools 35
make -C android apk         # -> android/app/build/outputs/apk/debug/app-debug.apk
```

GitHub Actions runs these same steps on every push and pull request (`.github/workflows/build.yml`, licences accepted with `yes`) and uploads the debug APK as a run artifact. It is signed with a debug key made on the runner for that run, so a CI APK only installs over another after an uninstall.

The debug APK contains `arm64-v8a`, `armeabi-v7a` and `x86_64` libraries
(16 KB page aligned), targets API 35 and needs Android 7.0 (API 24) or newer.

## Install and add your game data

Install the APK (`make -C android install`, or copy it to the phone and open
it). On first start the app shows **Game data needed**:

1. Copy your own decrypted `com.sega.MikuFlick02-iOS5.0-(Clutch-2.0.4).ipa`
   (v1.1.5) to the phone, e.g. into Downloads.
2. Tap **Import IPA** and pick it in the system file picker.
3. The app checks the game version first (only v1.1.5 armv7 is accepted), then
   extracts the files it uses (about 980 MB; it checks that there is enough
   free space) and builds `gamedata.txt`. It takes a minute or two.

Afterwards the IPA is no longer needed and can be deleted. The importer
(`port/src/ipa.c`) only reads the file you pick and only writes to the app's
own folder; a failed or interrupted import leaves no half-finished data behind.

### Alternative: copy the data from a PC

```sh
make -C port setup IPA=/path/to/your.ipa   # once, on the PC (as for Linux)
make -C android push                       # copies MikuFlick2.app (~980 MB) + gamedata.txt
```

`push` copies the files to the app's own storage folder,
`/sdcard/Android/data/io.github.yuridrak.mikuflick/files/`, which needs no
storage permission. If they are missing, the app shows that path on screen.
`adb push` creates the folders as the shell user with mode `2770`, which the
app cannot enter, so `push` also makes them `0775`. If you copy the data some
other way and the app still says it is missing, check that the app can open the
`MikuFlick2.app` folder.

Saves are kept in the app's internal storage and are deleted when the app is
uninstalled.

## Controls

- The flick board takes multi-touch, as on iOS. Menus, the pause button
  (bottom right), the pause menu and the PV viewer react to taps.
- Tall (or wide) screens keep the original 2:3 layout centred, so every key
  stays where it was on iOS; the bars around it are black during a song and
  show a dim, blurred copy of the menu elsewhere.
- The **Back** button works like Esc: cancel in menus, pause during a song,
  leave the PV viewer, and exit from the title screen.
- Leaving the app during a song opens the pause menu.
- Audio latency differs between phones. If notes feel early or late, change
  the input timing in OPTIONS.

## Debugging

```sh
make -C android logcat
adb shell am start -n io.github.yuridrak.mikuflick/.MikuFlickActivity --es args "--song 6 --diff 3 --autoplay"
```

The `args` extra accepts the Linux command-line flags.

## Tested

Debug APK on a real phone (arm64, Android 16, 1080×2392, gesture navigation):
magnet Extreme with `--autoplay` gives the same result as on Linux (115,616,
PERFECT, 172 COOL, 77/77 interludes); video, menus by touch, the Back button,
the filled bars and the touch areas of the flick keys were checked. The import
path was verified on Linux against `unzip` + `mfextract` (identical output);
the on-device **IMPORT IPA** flow still needs to be confirmed on real phones and
file providers. Not measured yet: audio latency on different phones.

## Layout

| Path | Purpose |
|---|---|
| `Makefile` | toolchain download, SDK setup, Gradle build, adb helpers |
| `SHA256SUMS` | checksums of the downloaded SDL source releases, command-line tools and Gradle |
| `app/build.gradle` | AGP 8.7 app module: SDK levels, ABIs, CMake, SDL Java sources, font assets |
| `app/jni/CMakeLists.txt` | builds SDL2, SDL2_image (stb PNG), SDL2_ttf (bundled FreeType) and `libmain.so` |
| `app/src/main/java/.../MikuFlickActivity.java` | `SDLActivity` subclass: library list, intent arguments, native pages over the game (`port/src/android.c`), system file picker for the IPA (fd passed to `port/src/ipa.c`) |
| `app/src/main/java/.../NativePage.java`, `ImportPage.java`, `DlcPage.java` | native screens of plain framework views (device theme, Android 7+): the IPA import screen and the DLC page, the counterparts of `ios/App/ImportViewController.m` and `DlcViewController.m` |
| `app/src/main/res/drawable/` | Material icons (Apache 2.0) for those screens |
| `app/src/main/AndroidManifest.xml` | portrait, fullscreen, no permissions |
| `../ios/` | iOS port: CMake/Makefile, native Files picker, scene-based lifecycle |

## Not done yet

- Confirming the in-app import on more phones and file providers (it needs a
  seekable file descriptor).
- A tap-to-calibrate latency screen.
- Release signing: `make -C android release` builds an unsigned APK. Sign it
  with your own key using `apksigner`.
- Recovering textures after a GL context loss (`SDL_RENDER_DEVICE_RESET`).
  This is only logged; it is rare on current devices.
