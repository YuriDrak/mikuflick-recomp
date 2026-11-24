# Miku Flick/02 Android build (see README.md).
#
#   make toolchain   download SDK command-line tools, Gradle and the SDL sources into third_party/
#   make licenses    review and accept the Android SDK licences (interactive, done by you)
#   make sdk         install NDK, CMake, platform and build-tools with sdkmanager
#   make apk         build app/build/outputs/apk/debug/app-debug.apk
#   make install     install the debug APK on the connected device (adb)
#   make push        copy your extracted MikuFlick2.app + gamedata.txt to the device
#   make run         start the app;  make logcat  follow its log
#
# Everything (SDK, NDK, Gradle caches, debug keystore, temp files) stays inside
# third_party/, which is gitignored. The project path may contain spaces, so
# paths are only ever used quoted in shell commands.

TP          := $(CURDIR)/third_party
SDK         := $(TP)/android-sdk
NDK_VER     := 27.2.12479018
CMAKE_VER   := 3.22.1
PLATFORM    := android-35
BUILD_TOOLS := 35.0.0
GRADLE_VER  := 8.9
CMDLINE_ZIP := commandlinetools-linux-11076708_latest.zip
SDL_VER     := 2.30.10
IMG_VER     := 2.8.2
TTF_VER     := 2.22.0

PKG     := io.github.yuridrak.mikuflick
DEVDIR  := /sdcard/Android/data/$(PKG)/files
APP     := ../extracted/ipa/Payload/MikuFlick2.app
GAMEDATA:= ../port/data/gamedata.txt
APK     := app/build/outputs/apk/debug/app-debug.apk

export ANDROID_HOME      := $(SDK)
export ANDROID_SDK_ROOT  := $(SDK)
export ANDROID_USER_HOME := $(TP)/android-user
export GRADLE_USER_HOME  := $(TP)/gradle-home

GRADLE     = "$(TP)/gradle-$(GRADLE_VER)/bin/gradle"
# The stock sdkmanager script breaks on paths with spaces, so call its entry point directly.
CMDLINE_HOME = $(SDK)/cmdline-tools/latest
SDKMANAGER = java "-Dcom.android.sdklib.toolsdir=$(CMDLINE_HOME)" "-Djava.io.tmpdir=$(TP)/tmp" \
    -classpath "$(CMDLINE_HOME)/lib/sdkmanager-classpath.jar" com.android.sdklib.tool.sdkmanager.SdkManagerCli \
    --sdk_root="$(SDK)"
GRADLE_ARGS = --no-daemon -p "$(CURDIR)" "-Dorg.gradle.jvmargs=-Xmx3g -Dfile.encoding=UTF-8 -Djava.io.tmpdir='$(TP)/tmp'"

all: apk

dirs:
	mkdir -p "$(TP)/dl" "$(TP)/tmp" "$(TP)/android-user" "$(TP)/gradle-home" "$(SDK)"

toolchain: dirs
	cd "$(TP)/dl" && [ -f "$(CMDLINE_ZIP)" ] || curl -fL -o "$(CMDLINE_ZIP)" "https://dl.google.com/android/repository/$(CMDLINE_ZIP)"
	[ -d "$(SDK)/cmdline-tools/latest" ] || { cd "$(TP)/tmp" && rm -rf cmdline-tools && unzip -q "$(TP)/dl/$(CMDLINE_ZIP)" && \
	    mkdir -p "$(SDK)/cmdline-tools" && mv cmdline-tools "$(SDK)/cmdline-tools/latest"; }
	cd "$(TP)/dl" && [ -f "gradle-$(GRADLE_VER)-bin.zip" ] || curl -fL -o "gradle-$(GRADLE_VER)-bin.zip" "https://services.gradle.org/distributions/gradle-$(GRADLE_VER)-bin.zip"
	[ -d "$(TP)/gradle-$(GRADLE_VER)" ] || { cd "$(TP)" && unzip -q "dl/gradle-$(GRADLE_VER)-bin.zip"; }
	cd "$(TP)/dl" && [ -f "SDL2-$(SDL_VER).tar.gz" ] || curl -fL -O "https://github.com/libsdl-org/SDL/releases/download/release-$(SDL_VER)/SDL2-$(SDL_VER).tar.gz"
	cd "$(TP)/dl" && [ -f "SDL2_image-$(IMG_VER).tar.gz" ] || curl -fL -O "https://github.com/libsdl-org/SDL_image/releases/download/release-$(IMG_VER)/SDL2_image-$(IMG_VER).tar.gz"
	cd "$(TP)/dl" && [ -f "SDL2_ttf-$(TTF_VER).tar.gz" ] || curl -fL -O "https://github.com/libsdl-org/SDL_ttf/releases/download/release-$(TTF_VER)/SDL2_ttf-$(TTF_VER).tar.gz"
	[ -d "$(TP)/SDL2" ] || { cd "$(TP)" && tar xzf "dl/SDL2-$(SDL_VER).tar.gz" && mv "SDL2-$(SDL_VER)" SDL2; }
	[ -d "$(TP)/SDL2_image" ] || { cd "$(TP)" && tar xzf "dl/SDL2_image-$(IMG_VER).tar.gz" && mv "SDL2_image-$(IMG_VER)" SDL2_image; }
	[ -d "$(TP)/SDL2_ttf" ] || { cd "$(TP)" && tar xzf "dl/SDL2_ttf-$(TTF_VER).tar.gz" && mv "SDL2_ttf-$(TTF_VER)" SDL2_ttf; }
	cd "$(TP)/dl" && sha256sum -c ../../SHA256SUMS

licenses:
	$(SDKMANAGER) --licenses

sdk:
	$(SDKMANAGER) "platforms;$(PLATFORM)" "build-tools;$(BUILD_TOOLS)" "ndk;$(NDK_VER)" "cmake;$(CMAKE_VER)"

local.properties:
	printf 'sdk.dir=%s\n' "$(SDK)" > local.properties

apk: local.properties
	$(GRADLE) $(GRADLE_ARGS) assembleDebug
	@ls -l "$(APK)"

release: local.properties
	$(GRADLE) $(GRADLE_ARGS) assembleRelease

install:
	adb install -r "$(APK)"

push:
	@[ -d "$(APP)" ] && [ -f "$(GAMEDATA)" ] || { echo "run 'make -C port setup IPA=...' first"; exit 1; }
	adb shell mkdir -p "$(DEVDIR)"
	adb push "$(APP)" "$(DEVDIR)/"
	adb push "$(GAMEDATA)" "$(DEVDIR)/gamedata.txt"
	@# folders created by adb belong to the shell user (mode 2770): let the app enter them
	adb shell find "$(DEVDIR)/MikuFlick2.app" -type d -exec chmod 0775 {} +

run:
	adb shell am start -n "$(PKG)/.MikuFlickActivity"

logcat:
	adb logcat -s mikuflick SDL SDL/APP SDL/ERROR AndroidRuntime DEBUG

clean:
	-$(GRADLE) $(GRADLE_ARGS) clean
	rm -rf app/build app/.cxx build .gradle

.PHONY: all dirs toolchain licenses sdk apk release install push run logcat clean
