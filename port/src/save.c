/* Text save file: one line per song/difficulty record. Replaces MikuFlick2.dat (NSKeyedArchiver). */
#include "mf.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

void save_load(SaveData *s, const char *dir) {
    memset(s, 0, sizeof *s);
    for (int i = 0; i < MAX_SONGS; i++)
        for (int d = 0; d < NUM_DIFF; d++) s->rec[i][d].rank = -1;
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) log_msg("cannot create %s", dir);
    snprintf(s->path, sizeof s->path, "%s/save.txt", dir);
    FILE *f = fopen(s->path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        int song, diff, score, rank, combo, cleared;
        if (sscanf(line, "rec %d %d %d %d %d %d", &song, &diff, &score, &rank, &combo, &cleared) == 6 && song >= 0 &&
            song < MAX_SONGS && diff >= 0 && diff < NUM_DIFF)
            s->rec[song][diff] = (Record){score, rank, combo, cleared};
    }
    fclose(f);
}

void save_write(const SaveData *s) {
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tmp", s->path);
    FILE *f = fopen(tmp, "w");
    if (!f) { log_msg("cannot write %s", tmp); return; }
    fprintf(f, "# mikuflick port save: rec song diff score best_rank max_combo cleared\n");
    for (int i = 0; i < MAX_SONGS; i++)
        for (int d = 0; d < NUM_DIFF; d++) {
            const Record *r = &s->rec[i][d];
            if (r->score || r->rank >= 0)
                fprintf(f, "rec %d %d %d %d %d %d\n", i, d, r->score, r->rank, r->combo, r->cleared);
        }
    fclose(f);
    if (rename(tmp, s->path) != 0) log_msg("cannot replace %s", s->path);
}
