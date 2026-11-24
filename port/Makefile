# Miku Flick/02 Linux port (the Android build lives in ../android)
#   make                      build ./mikuflick
#   make setup IPA=/path/x.ipa  extract your IPA and export game tables (one time)
#   make run                  run from the project root with default paths

CC      ?= cc
PKGS     = sdl2 SDL2_image SDL2_ttf
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wno-unused-parameter -Ithird_party $(shell pkg-config --cflags $(PKGS))
LDLIBS   = $(shell pkg-config --libs $(PKGS)) -lm

SRC = $(wildcard src/*.c)
OBJ = $(SRC:src/%.c=build/%.o)
APP = ../extracted/ipa/Payload/MikuFlick2.app

all: mikuflick

mikuflick: $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDLIBS)

build/%.o: src/%.c src/mf.h third_party/pl_mpeg.h | build
	$(CC) $(CFLAGS) -c -o $@ $<

build:
	mkdir -p build

../build/mfextract: ../tools/mfextract/mfextract.c
	mkdir -p ../build
	$(CC) -O2 -Wall -o $@ $<

setup: ../build/mfextract
	@if [ -n "$(IPA)" ]; then cd .. && sh tools/extract_ipa.sh "$(IPA)"; \
	elif [ ! -f "$(APP)/MikuFlick2" ]; then echo "pass IPA=/path/to/com.sega.MikuFlick02-....ipa"; exit 1; fi
	mkdir -p data
	../build/mfextract "$(APP)/MikuFlick2" data/gamedata.txt

run: mikuflick
	cd .. && ./port/mikuflick

clean:
	rm -f build/*.o mikuflick

.PHONY: all setup run clean
