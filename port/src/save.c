/* Text save file: one line per song/difficulty record. Replaces MikuFlick2.dat (NSKeyedArchiver).
   The song field of the per-song lines (rec, recx, flk, timing) is the built-in song number (0..10, as
   in every earlier port save) or, for DLC songs, the movie name, so their records survive a re-import
   with another set of packs. Lines of songs that are not installed now are kept and written back. */
#include "mf.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* a number is a built-in song first; otherwise (or if no built-in song has it) a DLC movie name,
   since some of those are numbers too ("39", Thankyou Pack) */
static int song_by_key(const GameData *gd, const char *key) {
    if (isdigit((unsigned char)key[0])) {
        int id = atoi(key);
        for (int i = 0; gd && i < gd->nsongs; i++)
            if (gd->songs[i].pack == 0 && gd->songs[i].id == id) return i;
    }
    for (int i = 0; gd && i < gd->nsongs; i++)
        if (gd->songs[i].pack != 0 && !strcmp(gd->songs[i].movie, key)) return i;
    return -1;
}

const char *save_song_key(const SaveData *s, int i, char *buf, size_t cap) {
    if (!s->gd || i < 0 || i >= s->gd->nsongs) return NULL;
    const SongInfo *si = &s->gd->songs[i];
    if (si->pack == 0) snprintf(buf, cap, "%d", si->id);
    else snprintf(buf, cap, "%s", si->movie);
    return buf;
}

static void keep_line(SaveData *s, const char *line) {
    size_t n = strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) n--;
    char *k = realloc(s->keep, s->keep_len + n + 2);
    if (!k) return;
    s->keep = k;
    memcpy(k + s->keep_len, line, n);
    s->keep_len += n;
    k[s->keep_len++] = '\n';
    k[s->keep_len] = 0;
}

/* "<word> <song key> rest" with the key turned into the song index; false: not a per-song line,
   or the song is not installed (then the line is kept) */
static bool song_line(SaveData *s, const char *line, char *out, size_t cap) {
    static const char *words[] = {"rec", "recx", "flk", "timing"};
    char word[16], key[64];
    int used = 0;
    if (sscanf(line, "%15s %63s %n", word, key, &used) != 2 || !used) return false;
    bool known = false;
    for (size_t w = 0; w < sizeof words / sizeof *words; w++) known |= !strcmp(word, words[w]);
    if (!known) return false;
    int i = song_by_key(s->gd, key);
    if (i < 0) { keep_line(s, line); return false; }
    snprintf(out, cap, "%s %d %s", word, i, line + used);
    return true;
}

void save_load(SaveData *s, const char *dir, const GameData *gd) {
    free(s->keep);
    memset(s, 0, sizeof *s);
    s->gd = gd;
    s->opt.pause_button = true;   /* saves written before DISPLAY PAUSE existed */
    s->opt.show_dlc = true;
    for (int i = 0; i < MAX_SONGS; i++)
        for (int d = 0; d < NUM_DIFF; d++) s->rec[i][d].rank = -1;
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) log_msg("cannot create %s", dir);
    snprintf(s->path, sizeof s->path, "%s/save.txt", dir);
    FILE *f = fopen(s->path, "r");
    if (!f) return;
    char raw[640], line[700];
    int legacy_timing = 0;
    bool has_timing = false;
    while (fgets(raw, sizeof raw, f)) {
        int song, diff, score, rank, combo, cleared;
        char key[32];
        float fv;
        if (song_line(s, raw, line, sizeof line)) {
            if (sscanf(line, "rec %d %d %d %d %d %d", &song, &diff, &score, &rank, &combo, &cleared) == 6 &&
                song >= 0 && song < MAX_SONGS && diff >= 0 && diff < NUM_DIFF)
                s->rec[song][diff] = (Record){score, rank, combo, cleared};
            else if (sscanf(line, "timing %d %d", &song, &score) == 2 && song >= 0 && song < MAX_SONGS) {
                s->timing[song] = score < -10 ? -10 : score > 10 ? 10 : score;
                has_timing = true;
            } else records_load_line(s, line);   /* RECORDS: recx / flk lines */
        } else if (sscanf(raw, "opt %31s %f", key, &fv) == 2) {
            Options *o = &s->opt;
            s->has_opt = true;
            if (!strcmp(key, "bgm_volume")) o->bgm_volume = fv;
            else if (!strcmp(key, "se_volume")) o->se_volume = fv;
            else if (!strcmp(key, "fail_sound")) o->fail_sound = fv != 0;
            else if (!strcmp(key, "input_timing")) legacy_timing = (int)fv;   /* older saves: one value for all songs */
            else if (!strcmp(key, "left_hand")) o->left_hand = fv != 0;
            else if (!strcmp(key, "keyboard")) o->keyboard = fv >= 0 && fv <= 2 ? (int)fv : 0;
            else if (!strcmp(key, "flower")) o->flower = fv != 0;
            else if (!strcmp(key, "guide_panel")) o->guide_panel = fv != 0;
            else if (!strcmp(key, "pause_button")) o->pause_button = fv != 0;
            else if (!strcmp(key, "show_dlc")) o->show_dlc = fv != 0;
        }
    }
    fclose(f);
    if (!has_timing && legacy_timing)
        for (int i = 0; i < MAX_SONGS; i++) s->timing[i] = legacy_timing < -10 ? -10 : legacy_timing > 10 ? 10 : legacy_timing;
}

void save_write(const SaveData *s) {
    char tmp[1100], key[64];
    snprintf(tmp, sizeof tmp, "%s.tmp", s->path);
    FILE *f = fopen(tmp, "w");
    if (!f) { log_msg("cannot write %s", tmp); return; }
    fprintf(f, "# mikuflick port save: rec song diff score best_rank max_combo cleared (song: number, or DLC movie)\n");
    if (s->has_opt) {
        const Options *o = &s->opt;
        fprintf(f, "opt bgm_volume %.4f\nopt se_volume %.4f\nopt fail_sound %d\n", o->bgm_volume, o->se_volume,
                o->fail_sound);
        fprintf(f, "opt left_hand %d\nopt keyboard %d\nopt flower %d\nopt guide_panel %d\nopt pause_button %d\n",
                o->left_hand, o->keyboard, o->flower, o->guide_panel, o->pause_button);
        fprintf(f, "opt show_dlc %d\n", o->show_dlc);
    }
    for (int i = 0; i < MAX_SONGS; i++) {
        if (!save_song_key(s, i, key, sizeof key)) continue;
        for (int d = 0; d < NUM_DIFF; d++) {
            const Record *r = &s->rec[i][d];
            if (r->score || r->rank >= 0)
                fprintf(f, "rec %s %d %d %d %d %d\n", key, d, r->score, r->rank, r->combo, r->cleared);
        }
    }
    records_save_lines(s, f);   /* RECORDS */
    for (int i = 0; i < MAX_SONGS; i++)
        if (s->timing[i] && save_song_key(s, i, key, sizeof key)) fprintf(f, "timing %s %d\n", key, s->timing[i]);
    if (s->keep) fputs(s->keep, f);
    fclose(f);
    if (rename(tmp, s->path) != 0) log_msg("cannot replace %s", s->path);
}
