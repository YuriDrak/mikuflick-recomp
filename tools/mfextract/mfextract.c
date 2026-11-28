/*
 * mfextract - export game data tables from the user's own MikuFlick2 (v1.1.5,
 * armv7, UUID 96B378F9-551D-39DD-A256-1400E68E039C) into the plain text file the
 * port reads at startup. No game data is shipped with the port itself.
 *
 * With the original save (Container/Documents/MikuFlick2.dat from an IPA that
 * bundles the DLC), the DLC songs whose files are present next to the binary
 * are listed too (`make setup` links the pack files there).
 *
 * The extraction itself lives in port/src/mfx.c and port/src/dlc.c, which the
 * game's in-app IPA importer uses too.
 *
 * Build: cc -O2 -Wall -Iport/src -o mfextract tools/mfextract/mfextract.c port/src/mfx.c port/src/dlc.c port/src/bplist.c
 * Usage: mfextract <path/to/MikuFlick2> <out/gamedata.txt> [<MikuFlick2.dat>]
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "dlc.h"
#include "mfx.h"

static uint8_t *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = n > 0 ? malloc((size_t)n) : NULL;
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fprintf(stderr, "mfextract: cannot read %s\n", path);
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

static bool have_next_to(void *ud, const char *name) {
    char path[4096];
    struct stat st;
    snprintf(path, sizeof path, "%s/%s", (const char *)ud, name);
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <MikuFlick2> <gamedata.txt> [<MikuFlick2.dat>]\n", argv[0]); return 1; }
    size_t n = 0;
    uint8_t *buf = slurp(argv[1], &n);
    if (!buf) return 1;

    FILE *o = fopen(argv[2], "w");
    if (!o) { perror(argv[2]); return 1; }
    char err[256];
    int nsongs = mfx_write_gamedata(buf, n, o, err, sizeof err);
    free(buf);
    if (nsongs < 0) {
        fclose(o);
        fprintf(stderr, "mfextract: %s\n", err);
        remove(argv[2]);
        return 1;
    }
    int ndlc = 0;
    if (argc > 3) {
        char appdir[4096];
        snprintf(appdir, sizeof appdir, "%s", argv[1]);
        char *slash = strrchr(appdir, '/');
        if (slash) *slash = 0;
        else snprintf(appdir, sizeof appdir, ".");
        size_t sn = 0;
        uint8_t *save = slurp(argv[3], &sn);
        if (save) {
            ndlc = dlc_write_songs(save, sn, have_next_to, appdir, o, err, sizeof err);
            if (ndlc < 0) { fprintf(stderr, "mfextract: DLC songs skipped (%s): %s\n", argv[3], err); ndlc = 0; }
            free(save);
        }
    }
    if (fclose(o) != 0) { perror(argv[2]); remove(argv[2]); return 1; }
    fprintf(stderr, "mfextract: wrote %d songs + %d DLC songs to %s\n", nsongs, ndlc, argv[2]);
    return 0;
}
