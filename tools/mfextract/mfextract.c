/*
 * mfextract - export game data tables from the user's own MikuFlick2 (v1.1.5,
 * armv7, UUID 96B378F9-551D-39DD-A256-1400E68E039C) into the plain text file the
 * port reads at startup. No game data is shipped with the port itself.
 *
 * The extraction itself lives in port/src/mfx.c, which the game's in-app IPA
 * importer uses too.
 *
 * Build: cc -O2 -Wall -Iport/src -o mfextract tools/mfextract/mfextract.c port/src/mfx.c
 * Usage: mfextract <path/to/MikuFlick2> <out/gamedata.txt>
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "mfx.h"

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <MikuFlick2> <gamedata.txt>\n", argv[0]); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = n > 0 ? malloc((size_t)n) : NULL;
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) { fprintf(stderr, "mfextract: read failed\n"); return 1; }
    fclose(f);

    FILE *o = fopen(argv[2], "w");
    if (!o) { perror(argv[2]); return 1; }
    char err[256];
    int nsongs = mfx_write_gamedata(buf, (size_t)n, o, err, sizeof err);
    fclose(o);
    free(buf);
    if (nsongs < 0) {
        fprintf(stderr, "mfextract: %s\n", err);
        remove(argv[2]);
        return 1;
    }
    fprintf(stderr, "mfextract: wrote %d songs to %s\n", nsongs, argv[2]);
    return 0;
}
