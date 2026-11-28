# Miku Flick for iOS

The shared C11/SDL2 port as an arm64 iPhone app. The deployment target is iOS 26.0, with iOS 26 and 27 as the primary runtime targets.

## Install

Each release has a ready-made, unsigned `MikuFlick-<version>-iOS.ipa`. To install it and get updates through AltStore, SideStore or Feather, add this source URL in the app's Sources screen:

```text
https://github.com/YuriDrak/mikuflick-recomp/releases/latest/download/altsource.json
```

Add this exact link, not the repository link (`https://github.com/YuriDrak/mikuflick-recomp`): the repository page is not a source. AltStore, SideStore or Feather re-signs the app on your device.

## Build

Install Xcode with the iOS SDK and CMake. From the repository root, run either command. Each target downloads and verifies the SDL source releases on first use.

```sh
make -C ios sim
make -C ios device
```

The Simulator app is in `ios/build/iphonesimulator/Debug-iphonesimulator/MikuFlick.app`. The unsigned device app is in `ios/build/iphoneos/Debug-iphoneos/MikuFlick.app`. Both generated Xcode projects, dependencies and build products live under the ignored `ios/build/` directory. The build uses the dependency checksums in [android/SHA256SUMS](../android/SHA256SUMS).

GitHub Actions builds the same unsigned device app in Release on every push and pull request (`.github/workflows/build.yml`) and uploads it as `MikuFlick-unsigned.ipa` (the app in a `Payload/` folder) under the run's artifacts. Sign it yourself before installing.

For a version tag (for example `v0.4.0`), the workflow also does three things:

- sets `CFBundleShortVersionString` from the tag (0.4.0) and `CFBundleVersion` from the run number;
- adds `MikuFlick-<version>-iOS.ipa` to the GitHub release it publishes;
- adds an AltStore/SideStore/Feather source, `altsource.json`, to the same release. [`altsource.sh`](altsource.sh) generates it from the packaged app (bundle id, version, size, minimum iOS).

The source URL to add is the one under [Install](#install).

For a signed installation, pass your development team ID and a bundle identifier registered to your team. Then open the generated project in Xcode.

```sh
make -C ios device SIGNING=YES TEAM=ABCDE12345 BUNDLE_ID=com.example.yourname.mikuflick
open ios/build/iphoneos/MikuFlick.xcodeproj
```

The target uses automatic signing. Select the MikuFlick scheme and your connected iPhone, then run the app. Set `CONFIG=Release` for an optimized build. `make -C ios clean` removes the generated projects and build products but keeps the verified SDL sources. `make -C ios distclean` removes all of `ios/build/`.

## First launch

Tap **Import IPA** and select your own legally obtained decrypted Miku Flick/02 v1.1.5 archive from Files. The picker supplies a temporary copy. Allow about 2 GB of free storage for the copy and extracted resources. Imported data stays in Application Support and is excluded from backups. Saves use a separate directory. If you close the picker, the prompt comes back. If the file cannot be read or is not a compatible IPA, the app shows the reason and offers **TRY AGAIN**. The port writes its diagnostics to the device log, which you can view in Console.app under the MikuFlick process. The app bundle contains the port and the redistributable font. See the [contribution and asset policy](../CONTRIBUTING.md).

If the imported data comes from an older build (it lacks the help text or the ending credits), the import screen comes back in update mode, with **Not Now** to keep the current data.

## DLC page

The title menu's **DLC** button opens a native page (`App/DlcViewController.m`):

- **Show DLC Songs**;
- the download link, which starts with the archive.org link. **Restore Default Link** appears when you change it;
- the 21 packs. Tap a pack to download it, and tap it again to cancel;
- **Import IPA with DLC**.

Downloads are HTTP range requests through `NSURLSession` (`ios_http_range` in `App/Platform.m`). A server that does not answer with partial content is refused at the response. See [DLC songs](../README.md#dlc-songs).

## How it fits together

| Location | Purpose |
| --- | --- |
| `ios/CMakeLists.txt` | Generates the Xcode app target and compiles the shared sources directly from `port/src/` with `MF_IOS` defined. |
| `ios/Makefile` | Wraps dependency setup and the `deps`, `sim` and `device` targets. |
| `ios/altsource.sh` | Writes the AltStore/SideStore/Feather source for a release IPA (used by CI for version tags). |
| `ios/App/fetch-deps.sh` | Downloads SDL2 2.30.10, SDL2_image 2.8.2 and SDL2_ttf 2.22.0 (bundled FreeType, static) and verifies them against the Android checksums. |
| `ios/App/` | App metadata, launch resources, the app icon (`AppIcon.svg`, the iOS version of the Android launcher icon, rendered into `Assets.xcassets`) and the Objective-C bridge: sandbox paths, the app lifecycle, the import screen with the Files picker (`ImportViewController`), the DLC page (`DlcViewController`) and range requests for pack downloads (`Platform.m`). |
| `ios/build/` | Ignored: dependency sources, generated projects and build products, in separate trees for device and Simulator. |

- **Lifecycle.** iOS 27 requires the scene-based UIKit lifecycle, and the stock `SDL2main` of SDL 2.30.10 uses the older app-delegate one. `App/Scene.m` therefore provides `main()`, `MFAppDelegate` and `MFSceneDelegate` (declared in `Info.plist` under `UIApplicationSceneManifest`). It calls `SDL_SetMainReady()` and `SDL_iPhoneSetEventPump()`, and attaches SDL's `UIWindow` to the active `UIWindowScene`. SDL's own observer still delivers the `SDL_APP_*` events, so the scene delegate does not forward them again. `SDL_main` only returns after a fatal startup error or an explicit quit, and UIKit cannot restart it, so the process then exits instead of leaving a frozen frame. While the app is inactive or in the background, SDL skips drawing and the main loop sleeps.
- **Shared code.** The platform differences in `port/src` are behind `MF_IOS`:
  - sandbox data and save paths, the import screen and the DLC page;
  - range requests for DLC pack downloads;
  - `SDL_WINDOW_ALLOW_HIGHDPI`;
  - a 512-frame audio buffer;
  - logging through `SDL_Log`;
  - black bars instead of the blurred backdrop during gameplay.
- **CMake 4** rejects the pinned FreeType project's old policy minimum, so the build passes `CMAKE_POLICY_VERSION_MINIMUM=3.5` instead of patching the archive.

## Status

- **Builds:** both the Simulator and device targets link (arm64, deployment target iOS 26.0, Xcode 27.2), in Debug and Release.
- **Simulator (iPhone 17, iOS 26.0 and 27.0):**
  - first-run screen and the Files picker (open, cancel, retry);
  - full IPA import, title, music select, and a full song with video;
  - auto-pause on background, and imported data kept after a cold relaunch;
  - DLC page: pack list and a pack download from the archive.org link, custom link and Restore Default Link, a server that ignores Range refused, HTTP 404 shown.
- **Physical iPhone 17 (iOS 27):** played by hand over many sessions (IPA import, menus, full songs with touch, all keyboard modes, options, help and credits).
- **CI:** an unsigned Release IPA on every push; v0.4.0 is the first release with an IPA and an AltSource.
- **Not yet verified:** audio latency and route changes on a phone, and other iPhone models.
