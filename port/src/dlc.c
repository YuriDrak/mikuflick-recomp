/*
 * DLC song table from the original save (Documents/MikuFlick2.dat). See dlc.h.
 *
 * The save is an NSKeyedArchiver bplist whose root "savedata" (StatusData) holds
 * m_MusicPackArray as NSData: a second keyed archive of an NSArray of MusicPack
 * {m_PackID, m_PackName, m_IsInstalled, m_MusicDataArray}. Each m_MusicDataArray is
 * again NSData wrapping a keyed archive of MusicData objects, and every NSString
 * field of a MusicData (m_Title, m_Artist, m_MovieDataFileName, ...) is itself
 * NSData wrapping a one-string keyed archive (MusicData encodeWithCoder:).
 */
#include "dlc.h"

#include "bplist.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct {
    BpNode *tree;
    const BpNode *objs;   /* $objects */
    const BpNode *root;   /* first $top entry, resolved */
} Arc;

static void seterr(char *err, size_t errlen, const char *fmt, ...) {
    if (!err || !errlen) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, errlen, fmt, ap);
    va_end(ap);
}

/* UID -> object; uid 0 is "$null". Non-UID values (inline ints/bools) are returned as is. */
static const BpNode *arc_obj(const Arc *a, const BpNode *v) {
    if (!v) return NULL;
    if (v->type != BP_UID) return v;
    if (v->i <= 0 || (size_t)v->i >= a->objs->n) return NULL;
    return a->objs->items[v->i];
}

static bool arc_open(Arc *a, const uint8_t *data, size_t len) {
    memset(a, 0, sizeof *a);
    a->tree = bp_parse(data, len);
    const BpNode *objs = bp_get(a->tree, "$objects"), *top = bp_get(a->tree, "$top");
    if (!objs || objs->type != BP_ARRAY || !top || top->type != BP_DICT || top->n == 0) {
        bp_free(a->tree);
        a->tree = NULL;
        return false;
    }
    a->objs = objs;
    a->root = arc_obj(a, top->items[0]);
    return true;
}

static void arc_close(Arc *a) {
    bp_free(a->tree);
    memset(a, 0, sizeof *a);
}

/* field holding NSData that is itself a keyed archive */
static bool arc_nested(const Arc *a, const BpNode *field, Arc *out) {
    const BpNode *v = arc_obj(a, field);
    if (v && v->type == BP_DICT) v = arc_obj(a, bp_get(v, "NS.data"));
    if (!v || v->type != BP_DATA) return false;
    return arc_open(out, (const uint8_t *)v->s, v->n);
}

/* NSArray / NSMutableArray -> its NS.objects (UIDs) */
static const BpNode *arc_array(const Arc *a, const BpNode *v) {
    v = arc_obj(a, v);
    const BpNode *o = v && v->type == BP_DICT ? bp_get(v, "NS.objects") : NULL;
    return o && o->type == BP_ARRAY ? o : NULL;
}

static bool copy_str(const BpNode *s, char *out, size_t cap) {
    if (!s || s->type != BP_STR || !s->s || !strcmp(s->s, "$null")) return false;
    snprintf(out, cap, "%s", s->s);
    return true;
}

/* NSString field, stored plain, as NSMutableString {NS.string}, or as NSData around a
   one-string archive (MusicData). Empty string if missing/nil. */
static void get_str(const Arc *a, const BpNode *dict, const char *key, char *out, size_t cap) {
    out[0] = 0;
    const BpNode *v = arc_obj(a, bp_get(dict, key));
    if (!v) return;
    if (copy_str(v, out, cap)) return;
    if (v->type != BP_DICT) return;
    if (copy_str(arc_obj(a, bp_get(v, "NS.string")), out, cap)) return;
    Arc in;
    if (!arc_nested(a, v, &in)) return;
    if (!copy_str(in.root, out, cap) && in.root && in.root->type == BP_DICT)
        copy_str(arc_obj(&in, bp_get(in.root, "NS.string")), out, cap);
    arc_close(&in);
}

static long get_int(const Arc *a, const BpNode *dict, const char *key, long def) {
    const BpNode *v = arc_obj(a, bp_get(dict, key));
    if (!v || (v->type != BP_INT && v->type != BP_BOOL && v->type != BP_REAL)) return def;
    return (long)bp_num(v);
}

/* file stem we are willing to put into a path */
static bool safe_name(const char *s) {
    if (!s[0] || s[0] == '.' || strlen(s) > 60) return false;
    for (const char *p = s; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_' ||
              *p == '-' || *p == '.'))
            return false;
    return true;
}

/* title/artist go into a tab-separated text line */
static void clean_text(char *s) {
    for (; *s; s++)
        if (*s == '\t' || *s == '\r' || *s == '\n') *s = ' ';
}

static int clampi(long v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : (int)v; }

#define MAX_DLC_ID 4096

typedef struct {
    DlcSong *v;
    int n, cap;
    unsigned char *seen;   /* m_Index already listed */
} List;

static void read_pack(const Arc *pa, const BpNode *pack, List *l) {
    long pack_id = get_int(pa, pack, "m_PackID", 0);
    if (pack_id <= 0 || pack_id > 9999 || !get_int(pa, pack, "m_IsInstalled", 0)) return;   /* 0 = DefaultPack (built-in songs) */
    char pack_name[128];
    get_str(pa, pack, "m_PackName", pack_name, sizeof pack_name);
    Arc ma;
    if (!arc_nested(pa, bp_get(pack, "m_MusicDataArray"), &ma)) return;
    const BpNode *songs = arc_array(&ma, ma.root);
    for (size_t i = 0; songs && i < songs->n; i++) {
        const BpNode *md = arc_obj(&ma, songs->items[i]);
        if (!md || md->type != BP_DICT || get_int(&ma, md, "m_PreInstall", 0)) continue;
        long id = get_int(&ma, md, "m_Index", -1);
        DlcSong s;
        memset(&s, 0, sizeof s);
        get_str(&ma, md, "m_MovieDataFileName", s.movie, sizeof s.movie);
        get_str(&ma, md, "m_PreSoundFileName", s.preview, sizeof s.preview);
        get_str(&ma, md, "m_ArtWorkFileName", s.atlas, sizeof s.atlas);
        get_str(&ma, md, "m_Title", s.title, sizeof s.title);
        get_str(&ma, md, "m_Artist", s.artist, sizeof s.artist);
        if (id < 0 || id >= MAX_DLC_ID || l->seen[id] || !safe_name(s.movie) || !safe_name(s.atlas)) continue;
        if (!safe_name(s.preview)) s.preview[0] = 0;
        clean_text(s.title);
        clean_text(s.artist);
        if (!s.title[0]) snprintf(s.title, sizeof s.title, "%s", s.movie);
        snprintf(s.pack_name, sizeof s.pack_name, "%s", pack_name);
        clean_text(s.pack_name);
        s.id = (int)id;
        s.pack = (int)pack_id;
        s.art_index = clampi(get_int(&ma, md, "m_ArtWorkIndex", 0), 0, 2);
        s.title_break = clampi(get_int(&ma, md, "m_TitleLineBreadIdx", 0), 0, 255);
        s.bpm = clampi(get_int(&ma, md, "m_BPM", 120), 1, 999);
        s.stars[0] = clampi(get_int(&ma, md, "m_DifficultNum0", 1), 0, 9);
        s.stars[1] = clampi(get_int(&ma, md, "m_DifficultNum1", 1), 0, 9);
        s.stars[2] = clampi(get_int(&ma, md, "m_DifficultNum2", 1), 0, 9);
        s.stars[3] = clampi(get_int(&ma, md, "m_DifficultNum3", 1), 0, 9);
        if (l->n == l->cap) {
            int cap = l->cap ? l->cap * 2 : 64;
            DlcSong *v = realloc(l->v, (size_t)cap * sizeof *v);
            if (!v) break;
            l->v = v;
            l->cap = cap;
        }
        l->seen[id] = 1;
        l->v[l->n++] = s;
    }
    arc_close(&ma);
}

int dlc_read_songs(const uint8_t *save, size_t len, DlcSong **out, char *err, size_t errlen) {
    *out = NULL;
    Arc sa, pa;
    if (!arc_open(&sa, save, len)) { seterr(err, errlen, "not a keyed archive"); return -1; }
    const BpNode *status = sa.root;
    if (!status || status->type != BP_DICT || !bp_get(status, "m_MusicPackArray")) {
        arc_close(&sa);
        seterr(err, errlen, "no music pack list in the save");
        return -1;
    }
    if (!arc_nested(&sa, bp_get(status, "m_MusicPackArray"), &pa)) {
        arc_close(&sa);
        seterr(err, errlen, "damaged music pack list");
        return -1;
    }
    const BpNode *packs = arc_array(&pa, pa.root);
    List l = {0};
    l.seen = calloc(MAX_DLC_ID, 1);
    for (size_t i = 0; packs && l.seen && i < packs->n; i++) {
        const BpNode *pk = arc_obj(&pa, packs->items[i]);
        if (pk && pk->type == BP_DICT) read_pack(&pa, pk, &l);
    }
    bool oom = !l.seen;
    free(l.seen);
    arc_close(&pa);
    arc_close(&sa);
    if (oom) { free(l.v); seterr(err, errlen, "out of memory"); return -1; }
    *out = l.v;
    return l.n;
}

void dlc_print_song(const DlcSong *s, FILE *out) {
    fprintf(out, "dlc %d %d %d %d %s %s %s %d %d %d %d %d\t%s\t%s\n", s->id, s->pack, s->art_index, s->title_break,
            s->preview[0] ? s->preview : "-", s->movie, s->atlas, s->bpm, s->stars[0], s->stars[1], s->stars[2], s->stars[3],
            s->title, s->artist);
}

bool dlc_song_files_present(const DlcSong *s, DlcHaveFile have, void *ud) {
    char file[80];
    snprintf(file, sizeof file, "%s.usm", s->movie);
    if (!have(ud, file)) return false;
    snprintf(file, sizeof file, "%s.png", s->atlas);
    return have(ud, file);
}

int dlc_write_songs(const uint8_t *save, size_t len, DlcHaveFile have, void *ud, FILE *out, char *err, size_t errlen) {
    DlcSong *v;
    int n = dlc_read_songs(save, len, &v, err, errlen);
    if (n < 0) return -1;
    int written = 0;
    for (int i = 0; i < n; i++)
        if (dlc_song_files_present(&v[i], have, ud)) { dlc_print_song(&v[i], out); written++; }
    free(v);
    return written;
}

const char *dlc_pack_file(const char *zip_path) {
    static const char pre[] = "Container/Library/InstallData/Mov_";
    if (strncmp(zip_path, pre, sizeof pre - 1)) return NULL;
    const char *p = zip_path + sizeof pre - 1;
    if (*p < '0' || *p > '9') return NULL;
    while (*p >= '0' && *p <= '9') p++;
    if (*p++ != '/' || !*p || *p == '.' || strchr(p, '/') || strchr(p, '\\')) return NULL;
    const char *dot = strrchr(p, '.');
    if (!dot || (strcasecmp(dot, ".usm") && strcasecmp(dot, ".adx") && strcasecmp(dot, ".png"))) return NULL;
    return p;
}
