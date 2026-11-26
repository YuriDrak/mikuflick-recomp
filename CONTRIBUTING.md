# Contributing to Miku Flick/02 Port

Thank you for your interest in contributing to **mikuflick-recomp**, a clean-room C11/SDL2 reimplementation of *Miku Flick/02* (SEGA, iOS v1.1.5) for Linux and Android!

Whether you are fixing bugs, improving performance, enhancing documentation, or adding new features (within scope), your contributions are welcome.

---

## Legal & Asset Policy

- **No Game Data Included:** This repository ships **no game data**, copyrighted artwork, music, video, audio, charts, or binaries belonging to SEGA or Crypton Future Media.
- **User-Supplied IPA:** You must provide your own legally obtained, decrypted copy of `com.sega.MikuFlick02-iOS5.0-(Clutch-2.0.4).ipa` (v1.1.5, armv7) to extract game assets and tables at setup time.
- **Clean-Room Reimplementation:** All game logic, rule engines, and decoders are clean-room implementations. Do not copy decompiled proprietary source code directly into the codebase.

---

## Getting Started

### Prerequisites (Linux)
Install build essentials and SDL2 development libraries (Debian/Ubuntu example):
```sh
sudo apt install build-essential pkg-config \
  libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev
```

### Building and Setting Up Game Data
1. **Build the port:**
   ```sh
   make -C port
   ```
2. **Set up game data (one-time extraction):**
   Unpacks your IPA and exports game tables (`port/data/gamedata.txt`):
   ```sh
   make -C port setup IPA="/path/to/com.sega.MikuFlick02-iOS5.0-(Clutch-2.0.4).ipa"
   ```
   *(Alternatively, use `--import-ipa FILE --import-to DIR` to test the built-in IPA importer without `unzip`).*
3. **Run the game:**
   ```sh
   ./port/mikuflick
   ```

### Prerequisites (Android)
To build the Android APK, see [`android/README.md`](android/README.md). The Android build automatically fetches the required SDK, NDK, Gradle, and SDL dependencies into gitignored directories (`android/third_party/`).

---

## Development Workflow

### Code Standards & Conventions
- **Language & Standard:** Written in clean, portable **C11**.
- **Minimal Dependencies:** Avoid external heavy dependencies. The project implements its own USM container reader, ADX/IMA4 audio decoders, and uses vendored [pl_mpeg](https://github.com/phoboslab/pl_mpeg) (MIT) for MPEG-1 video.
- **Python Policy:** Keep Python usage to an absolute minimum (prefer native C, shell scripts, and standard makefiles).
- **Sandboxing:** All file creations, extractions, and build outputs must remain strictly within the project directory.

### Testing and Verification
The repository includes robust automated testing and headless verification aids. Before submitting changes, ensure that automated test runs pass successfully:

- **Headless Autoplay Verification:**
  ```sh
  SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./port/mikuflick --autoplay --exit-after-song
  ```
- **Synthetic Input Autoplay:**
  ```sh
  SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./port/mikuflick --autoplay-input --exit-after-song
  ```
- **Screenshot / Diagnostic Dumps:**
  Use `--shot-dir DIR --shot-times N` or `--shot-ui-times N` to generate inspection frames.

---

## Reporting Issues

When filing an issue, please include:
- Operating system and version (Linux distribution, Android version/device).
- SDL2 version and package details.
- Steps to reproduce the bug or unexpected behavior.
- Relevant logs, stack traces, or console output.

---

## Submitting Pull Requests

1. **Fork and Clone:** Create a feature branch from `main`.
2. **Implement Changes:** Keep commits focused, atomic, and well-described.
3. **Verify:** Run headless autoplay tests and manual spot-checks on Linux (and Android if applicable) to ensure no regressions.
4. **Open a Pull Request:** Describe your changes clearly in the PR description, referencing any related issues.

Thank you for helping preserve and enjoy *Miku Flick/02*!
