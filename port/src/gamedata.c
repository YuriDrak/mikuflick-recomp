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

/* "dlc <id> <pack> <art_index> <title_break> <preview> <movie> <art_atlas> <bpm> <e> <n> <h> <x>\t<title>\t<artist>"
   (dlc.c). title_break (m_TitleLineBreadIdx) is read but unused: the select screen shows the logo art. */
static bool parse_dlc(const char *s, SongInfo *si) {
    int id, pack, art, brk, bpm, st[4], used = 0;
    char pv[64], mv[64], atlas[64];
    if (sscanf(s, "%d %d %d %d %63s %63s %63s %d %d %d %d %d%n", &id, &pack, &art, &brk, pv, mv, atlas, &bpm, &st[0], &st[1],
               &st[2], &st[3], &used) != 12 || s[used] != '\t' || id < 0 || pack <= 0)
        return false;
    memset(si, 0, sizeof *si);
    si->id = id;
    si->pack = pack;
    si->art_index = art >= 0 && art < 3 ? art : 0;
    snprintf(si->preview, sizeof si->preview, "%s", strcmp(pv, "-") ? pv : "");
    snprintf(si->movie, sizeof si->movie, "%s", mv);
    snprintf(si->art_atlas, sizeof si->art_atlas, "%s", atlas);
    si->bpm = bpm;
    for (int d = 0; d < 4; d++) si->stars[d] = st[d] < 0 ? 0 : st[d] > 9 ? 9 : st[d];
    const char *t = s + used + 1, *tab = strchr(t, '\t');
    size_t tl = tab ? (size_t)(tab - t) : strlen(t);
    snprintf(si->title, sizeof si->title, "%.*s", (int)(tl < sizeof si->title ? tl : sizeof si->title - 1), t);
    if (tab) snprintf(si->artist, sizeof si->artist, "%s", tab + 1);
    return true;
}

bool gamedata_load(GameData *gd, const char *path) {
    memset(gd, 0, sizeof *gd);
    char *text = read_file(path, NULL);
    if (!text) { log_msg("cannot read %s (run `make setup IPA=...` first)", path); return false; }

    int nkana = 0, nboard = 0, nsound = 0, ok_tables = 0, nguide = 0, nending = 0, ndlc = 0;
    SongInfo *dlc = NULL;
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
                si->id = idx;
                si->art_index = idx % 3;   /* _sTblDefaultMusicPackData: ArtWorkIndex = idx % 3 */
                if (idx + 1 > gd->nsongs) gd->nsongs = idx + 1;
            }
        } else if (!strncmp(line, "dlc ", 4)) {
            if (!dlc) dlc = calloc(MAX_SONGS, sizeof *dlc);
            if (dlc && ndlc < MAX_SONGS && parse_dlc(line + 4, &dlc[ndlc])) ndlc++;
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
        } else if (!strncmp(line, "ending ", 7)) {   /* optional: s_tbl_Ending (ending.c) */
            int idx, size, gap, n = 0;
            if (sscanf(line + 7, "%d %d %d%n", &idx, &size, &gap, &n) == 3 && idx >= 0 && idx < ENDING_LINES &&
                line[7 + n] == ' ') {
                snprintf(gd->ending_text[idx], sizeof gd->ending_text[idx], "%s", line + 8 + n);
                gd->ending_size[idx] = size;
                gd->ending_gap[idx] = gap;
                nending++;
            }
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
    gd->nending = nending == ENDING_LINES ? ENDING_LINES : 0;   /* all rows or none (older gamedata.txt) */
    if (gd->nending == 0) log_msg("%s has no ending credits (rerun make setup for the OPTIONS ENDING button)", path);
    /* DLC songs (dlc.c, from the original save) after the built-in ones, in pack order */
    int nbase = gd->nsongs;
    for (int k = 0; k < ndlc && nbase > 0; k++) {
        bool dup = false;
        for (int i = 0; i < gd->nsongs && !dup; i++) dup = gd->songs[i].id == dlc[k].id;
        if (dup || gd->nsongs >= MAX_SONGS) { log_msg("gamedata: skipping DLC song %d (%s)", dlc[k].id, dlc[k].movie); continue; }
        gd->songs[gd->nsongs++] = dlc[k];
    }
    free(dlc);
    gd->nbase = nbase;
    if (gd->nsongs > nbase) log_msg("gamedata: %d built-in + %d DLC songs", nbase, gd->nsongs - nbase);
    if (gd->nsongs == 0 || nkana != NUM_KANA || nboard != NUM_KEYS || nguide != NUM_KEYS || ok_tables != 7 || nsound != NUM_SOUNDS) {
        log_msg("%s is incomplete or outdated, rerun make setup (songs %d, kana %d, keys %d, tables %d, sounds %d)", path, gd->nsongs, nkana,
                nboard, ok_tables, nsound);
        return false;
    }
    return true;
}
