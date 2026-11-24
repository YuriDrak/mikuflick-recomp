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
        char key[32];
        float fv;
        if (sscanf(line, "rec %d %d %d %d %d %d", &song, &diff, &score, &rank, &combo, &cleared) == 6 && song >= 0 &&
            song < MAX_SONGS && diff >= 0 && diff < NUM_DIFF)
            s->rec[song][diff] = (Record){score, rank, combo, cleared};
        else if (sscanf(line, "opt %31s %f", key, &fv) == 2) {
            Options *o = &s->opt;
            s->has_opt = true;
            if (!strcmp(key, "bgm_volume")) o->bgm_volume = fv;
            else if (!strcmp(key, "se_volume")) o->se_volume = fv;
            else if (!strcmp(key, "fail_sound")) o->fail_sound = fv != 0;
            else if (!strcmp(key, "input_timing")) o->input_timing = (int)fv;
            else if (!strcmp(key, "left_hand")) o->left_hand = fv != 0;
            else if (!strcmp(key, "keyboard")) o->keyboard = (int)fv;
            else if (!strcmp(key, "flower")) o->flower = fv != 0;
            else if (!strcmp(key, "guide_panel")) o->guide_panel = fv != 0;
        }
    }
    fclose(f);
}

void save_write(const SaveData *s) {
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tmp", s->path);
    FILE *f = fopen(tmp, "w");
    if (!f) { log_msg("cannot write %s", tmp); return; }
    fprintf(f, "# mikuflick port save: rec song diff score best_rank max_combo cleared\n");
    if (s->has_opt) {
        const Options *o = &s->opt;
        fprintf(f, "opt bgm_volume %.2f\nopt se_volume %.2f\nopt fail_sound %d\nopt input_timing %d\n", o->bgm_volume,
                o->se_volume, o->fail_sound, o->input_timing);
        fprintf(f, "opt left_hand %d\nopt keyboard %d\nopt flower %d\nopt guide_panel %d\n", o->left_hand, o->keyboard,
                o->flower, o->guide_panel);
    }
    for (int i = 0; i < MAX_SONGS; i++)
        for (int d = 0; d < NUM_DIFF; d++) {
            const Record *r = &s->rec[i][d];
            if (r->score || r->rank >= 0)
                fprintf(f, "rec %d %d %d %d %d %d\n", i, d, r->score, r->rank, r->combo, r->cleared);
        }
    fclose(f);
    if (rename(tmp, s->path) != 0) log_msg("cannot replace %s", s->path);
}
