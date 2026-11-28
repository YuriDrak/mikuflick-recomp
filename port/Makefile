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
# DLC packs + original save: only in IPAs that carry the game's data container (iMazing style)
CONT = ../extracted/ipa/Container

all: mikuflick

mikuflick: $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDLIBS)

build/%.o: src/%.c src/mf.h src/bplist.h src/dlc.h src/dlcdl.h third_party/pl_mpeg.h | build
	$(CC) $(CFLAGS) -c -o $@ $<

build:
	mkdir -p build

../build/mfextract: ../tools/mfextract/mfextract.c src/mfx.c src/mfx.h src/dlc.c src/dlc.h src/bplist.c src/bplist.h
	mkdir -p ../build
	$(CC) -O2 -Wall -Isrc -o $@ ../tools/mfextract/mfextract.c src/mfx.c src/dlc.c src/bplist.c

setup: ../build/mfextract
	@if [ -n "$(IPA)" ]; then cd .. && sh tools/extract_ipa.sh "$(IPA)"; \
	elif [ ! -f "$(APP)/MikuFlick2" ]; then echo "pass IPA=/path/to/com.sega.MikuFlick02-....ipa"; exit 1; fi
	mkdir -p data
	@# DLC packs (Container/Library/InstallData/Mov_N/): link their files next to the app's own, never over them
	@if [ -d "$(CONT)/Library/InstallData" ]; then n=0; \
	  for f in "$(CONT)"/Library/InstallData/Mov_*/*.usm "$(CONT)"/Library/InstallData/Mov_*/*.adx \
	           "$(CONT)"/Library/InstallData/Mov_*/*.png; do \
	    [ -f "$$f" ] || continue; b=$${f##*/}; d=$${f%/*}; \
	    if [ ! -e "$(APP)/$$b" ] || [ -L "$(APP)/$$b" ]; then \
	      ln -sfn "../../Container/Library/InstallData/$${d##*/}/$$b" "$(APP)/$$b"; n=$$((n + 1)); fi; \
	  done; echo "linked $$n DLC files into $(APP)"; fi
	save="$(CONT)/Documents/MikuFlick2.dat"; [ -f "$$save" ] || save=; \
	../build/mfextract "$(APP)/MikuFlick2" data/gamedata.txt $$save

run: mikuflick
	cd .. && ./port/mikuflick

clean:
	rm -f build/*.o mikuflick

.PHONY: all setup run clean
