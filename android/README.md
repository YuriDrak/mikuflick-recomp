# Android build

The Android app runs the same C11 + SDL2 code as the Linux port (`../port/src`),
built with the NDK through CMake and wrapped in SDL's Java activity. Like the
Linux build, it ships **no SEGA/Crypton data**: you copy the data from your own
decrypted IPA onto the device.

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

The debug APK contains `arm64-v8a`, `armeabi-v7a` and `x86_64` libraries
(16 KB page aligned), targets API 35 and needs Android 7.0 (API 24) or newer.

## Install and add your game data

```sh
make -C port setup IPA=/path/to/your.ipa   # once, on the PC (as for Linux)
make -C android install                    # adb install -r
make -C android push                       # copies MikuFlick2.app (~980 MB) + gamedata.txt
make -C android run
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
  stays where it was on iOS; the bars around it show a blurred copy of the
  song video (or of the menu).
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
the filled bars and the touch areas of the flick keys were checked. Not measured
yet: audio latency on different phones.

## Layout

| Path | Purpose |
|---|---|
| `Makefile` | toolchain download, SDK setup, Gradle build, adb helpers |
| `SHA256SUMS` | checksums of the downloaded SDL source releases, command-line tools and Gradle |
| `app/build.gradle` | AGP 8.7 app module: SDK levels, ABIs, CMake, SDL Java sources, font assets |
| `app/jni/CMakeLists.txt` | builds SDL2, SDL2_image (stb PNG), SDL2_ttf (bundled FreeType) and `libmain.so` |
| `app/src/main/java/.../MikuFlickActivity.java` | `SDLActivity` subclass: library list, intent arguments |
| `app/src/main/AndroidManifest.xml` | portrait, fullscreen, no permissions |

## Not done yet

- Importing an IPA inside the app (for now the data is copied with `adb push`).
- A tap-to-calibrate latency screen.
- Release signing: `make -C android release` builds an unsigned APK. Sign it
  with your own key using `apksigner`.
- Recovering textures after a GL context loss (`SDL_RENDER_DEVICE_RESET`).
  This is only logged; it is rare on current devices.
