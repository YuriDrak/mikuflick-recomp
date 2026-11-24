/*
 * mfx - export the game data tables from the user's own MikuFlick2 binary
 * (v1.1.5, armv7, UUID 96B378F9-551D-39DD-A256-1400E68E039C) as the text file
 * the port reads at startup (gamedata.txt). Shared by tools/mfextract (PC
 * setup) and the in-game IPA importer (ipa.c). Plain C, no SDL.
 */
#ifndef MFX_H
#define MFX_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Writes gamedata.txt for the Mach-O image bin[0..len) to out. Returns the number
   of songs, or -1 with a user-readable reason in err (wrong build, damaged file...). */
int mfx_write_gamedata(const uint8_t *bin, size_t len, FILE *out, char *err, size_t errlen);

#endif
