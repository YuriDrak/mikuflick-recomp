/* Loader for data/gamedata.txt produced by tools/mfextract from the user's binary. */
#include "mf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_ints(const char *s, int *out, int n) {
    int k = 0;
    char *end;
    while (k < n) {
        long v = strtol(s, &end, 10);
        if (end == s) break;
        out[k++] = (int)v;
        s = end;
    }
    return k;
}

bool gamedata_load(GameData *gd, const char *path) {
    memset(gd, 0, sizeof *gd);
    char *text = read_file(path, NULL);
    if (!text) { log_msg("cannot read %s (run `make setup IPA=...` first)", path); return false; }

    int nkana = 0, nboard = 0, nsound = 0, ok_tables = 0, nguide = 0;
    char *save = NULL;
    for (char *line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (line[0] == '#' || !line[0]) continue;
        if (!strncmp(line, "song ", 5)) {
            int idx, bpm, s0, s1, s2, s3, consumed = 0;
            char pv[64], mv[64], art[64];
            if (sscanf(line + 5, "%d %63s %63s %63s %d %d %d %d %d %n", &idx, pv, mv, art, &bpm, &s0, &s1, &s2, &s3,
                       &consumed) >= 9 && idx >= 0 && idx < MAX_SONGS) {
                SongInfo *si = &gd->songs[idx];
                snprintf(si->preview, sizeof si->preview, "%s", pv);
                snprintf(si->movie, sizeof si->movie, "%s", mv);
                snprintf(si->art_atlas, sizeof si->art_atlas, "%s", art);
                snprintf(si->title, sizeof si->title, "%s", line + 5 + consumed);
                si->bpm = bpm;
                si->stars[0] = s0; si->stars[1] = s1; si->stars[2] = s2; si->stars[3] = s3;
                if (idx + 1 > gd->nsongs) gd->nsongs = idx + 1;
            }
        } else if (!strncmp(line, "kana ", 5)) {
            int v[7];
            if (read_ints(line + 5, v, 7) == 7 && v[0] >= 0 && v[0] < NUM_KANA) {
                KanaInfo *k = &gd->kana[v[0]];
                k->key = v[1]; k->dir = v[2]; k->sub = v[3]; k->tex = v[4]; k->x = v[5]; k->y = v[6];
                nkana++;
            }
        } else if (!strncmp(line, "board ", 6)) {
            int v[5];
            if (read_ints(line + 6, v, 5) == 5 && v[0] >= 0 && v[0] < NUM_KEYS) {
                BoardKey *b = &gd->board[v[0]];
                b->sx = v[1]; b->sy = v[2]; b->px = v[3]; b->py = v[4];
                nboard++;
            }
        } else if (!strncmp(line, "guide ", 6)) {
            int v[6];
            if (read_ints(line + 6, v, 6) == 6 && v[0] >= 0 && v[0] < NUM_KEYS) {
                for (int k = 0; k < 5; k++) gd->guide[v[0]][k] = v[k + 1];
                nguide++;
            }
        } else if (!strncmp(line, "scoreposx ", 10)) {
            ok_tables += read_ints(line + 10, gd->score_posx, 7) == 7;
        } else if (!strncmp(line, "resultofsy ", 11)) {
            ok_tables += read_ints(line + 11, gd->result_ofsy, 6) == 6;
        } else if (!strncmp(line, "front ", 6)) {
            ok_tables += read_ints(line + 6, gd->front, 11) == 11;
        } else if (!strncmp(line, "back ", 5)) {
            ok_tables += read_ints(line + 5, gd->back, 12) == 12;
        } else if (!strncmp(line, "addscore ", 9)) {
            ok_tables += read_ints(line + 9, gd->addscore, 12) == 12;
        } else if (!strncmp(line, "tension ", 8)) {
            ok_tables += read_ints(line + 8, gd->tension, 6) == 6;
        } else if (!strncmp(line, "endofs ", 7)) {
            ok_tables += read_ints(line + 7, gd->endofs, 5) == 5;
        } else if (!strncmp(line, "sound ", 6)) {
            int idx, loop;
            char name[64];
            if (sscanf(line + 6, "%d %63s %d", &idx, name, &loop) == 3 && idx >= 0 && idx < NUM_SOUNDS) {
                snprintf(gd->sound[idx], sizeof gd->sound[idx], "%s", name);
                gd->sound_loop[idx] = loop;
                nsound++;
            }
        }
    }
    free(text);
    if (gd->nsongs == 0 || nkana != NUM_KANA || nboard != NUM_KEYS || nguide != NUM_KEYS || ok_tables != 7 || nsound != NUM_SOUNDS) {
        log_msg("%s is incomplete or outdated, rerun make setup (songs %d, kana %d, keys %d, tables %d, sounds %d)", path, gd->nsongs, nkana,
                nboard, ok_tables, nsound);
        return false;
    }
    return true;
}
