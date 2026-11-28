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

int dlc_pack_id(const char *zip_path) {
    static const char pre[] = "Container/Library/InstallData/Mov_";
    if (strncmp(zip_path, pre, sizeof pre - 1)) return -1;
    const char *p = zip_path + sizeof pre - 1;
    int id = 0, digits = 0;
    while (*p >= '0' && *p <= '9' && digits < 5) { id = id * 10 + (*p++ - '0'); digits++; }
    return digits && *p == '/' ? id : -1;
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

/* Every DLC pack and song the game offered (as the "All DLC Included" IPA's save lists them: pack,
   pack name, movie, title), with the English name VocaDB (vocadb.net) lists for each original song
   that has a Japanese title, checked against the game's artist. Titles only, no game data: the DLC
   page shows these packs greyed out when no link offers them. */
static const DlcKnownSong k_known[] = {
    {1, "Duet Pack", "kantarera", "カンタレラ", "Cantarella"},
    {1, "Duet Pack", "gemini", "ジェミニ", "Gemini"},
    {1, "Duet Pack", "colorful_sexy", "カラフル×セクシィ", "Colorful×Sexy"},
    {2, "DIVA Pack", "kotti_muite_baby", "こっち向いて Baby", "Look this Way, Baby"},
    {2, "DIVA Pack", "Yellow", "Yellow", NULL},
    {2, "DIVA Pack", "colorful_melody", "カラフル×メロディ", "Colorful×Melody"},
    {3, "Summer Pack", "dear_cocoa_girls", "Dear cocoa girls", NULL},
    {3, "Summer Pack", "iyaiya_seijin", "イヤイヤ星人", "No-No Alien"},
    {3, "Summer Pack", "soiyassa", "そいやっさぁ!!", "Soiyassaa!!"},
    {4, "Dance Pack", "mikumiku_ni_siteyannyo", "みくみくにしてあげる♪【してやんよ】", "I'll Make You Do the Miku Miku♪ (You'll Do It)"},
    {4, "Dance Pack", "lukaluka_nightfever", "ルカルカ★ナイトフィーバー", "Luka Luka★Night Fever"},
    {4, "Dance Pack", "nightmare_partynight", "ナイトメア☆パーティーナイト", "Nightmare☆Party Night"},
    {5, "Tear Pack", "kokoro", "ココロ", "Heart"},
    {5, "Tear Pack", "VOiCE", "VOiCE -DIVA MIX-", NULL},
    {5, "Tear Pack", "hinekuremono", "ひねくれ者", "The Rebel"},
    {6, "Autumn Pack", "tsugaikogarashi", "番凩", "Pair of Wintry Winds"},
    {6, "Autumn Pack", "kyodai_syoujyo", "巨大少女", "Gigantic Girl"},
    {6, "Autumn Pack", "rinrin_signal", "リンリンシグナル", "Rin Rin Signal"},
    {7, "Guitar Pack", "puzzle", "パズル", "Puzzle"},
    {7, "Guitar Pack", "meiteki_cybernetics", "迷的サイバネティックス", "Depression of Cybernetics"},
    {7, "Guitar Pack", "sekiranunn_graffiti", "積乱雲グラフィティ", "Cumulonimbus Graffiti"},
    {8, "Dark Pack", "musunde_hiraite_rasetsuto_mukuro", "結んで開いて羅刹と骸", "Close and Open, Demons and the Dead"},
    {8, "Dark Pack", "houkai_utahime", "崩壊歌姫", "Disruptive Diva"},
    {8, "Dark Pack", "saa_dotti", "さあ、どっち？", "Now, Which?"},
    {9, "Winter Pack", "kogane_no_seiya_sousetsu_ni_kuchite", "金の聖夜霜雪に朽ちて", "Requiem for the Phantasma [25,Dec]"},
    {9, "Winter Pack", "hajimeteno_koiga_owarutoki", "初めての恋が終わる時", "When First Love Ends"},
    {9, "Winter Pack", "strobo_nights", "ストロボナイツ", "Strobo Nights"},
    {10, "Japan Pack", "jugemu_sequencer", "ジュゲムシーケンサー", "Jugemu Sequencer"},
    {10, "Japan Pack", "iroha_uta", "いろは唄", "Iroha Song"},
    {10, "Japan Pack", "amata_no_mai", "数多の舞 -Dance of many-", "Dance of Many"},
    {11, "Star Pack", "double_lariat", "ダブルラリアット", "Double Lariat"},
    {11, "Star Pack", "hello_planet", "＊ハロー、プラネット。", "*Hello, Planet."},
    {11, "Star Pack", "from_y_to_y", "from Y to Y", NULL},
    {12, "Lovely Pack", "mikumiku_kinn_ni_gotyuui", "みくみく菌にご注意♪", "Beware of the Miku Miku Germs♪"},
    {12, "Lovely Pack", "koiiro_byoutou", "恋色病棟", "Love-Colored Ward"},
    {12, "Lovely Pack", "nekomimi_switch", "ねこみみスイッチ", "Cat Ears Switch"},
    {13, "Farewell Pack", "hontoha_wakatteru", "ほんとは分かってる", "I Understand The Truth"},
    {13, "Farewell Pack", "utani_katachiha_naikeredo", "歌に形はないけれど", "Though My Song Has No Form"},
    {13, "Farewell Pack", "spica", "SPiCa", NULL},
    {14, "Anniversary Pack", "yumeyume", "ゆめゆめ", "DREAM DREAM"},
    {14, "Anniversary Pack", "innocence", "Innocence", NULL},
    {14, "Anniversary Pack", "worlds_end_dancehall", "ﾜｰﾙｽﾞｴﾝﾄﾞ･ﾀﾞﾝｽﾎｰﾙ", "World's End Dancehall"},
    {15, "mirai Pack", "on_the_rocks", "on the rocks", NULL},
    {15, "mirai Pack", "no_logic", "No Logic", NULL},
    {15, "mirai Pack", "mousou_sketch", "妄想スケッチ", "Delusion Sketch"},
    {16, "Thankyou Pack", "the_secretgarden", "The Secret garden", NULL},
    {16, "Thankyou Pack", "39", "39", NULL},
    {16, "Thankyou Pack", "sono_itibyou_suro-moshon", "その一秒スローモーション", "That One Second in Slow Motion"},
    {17, "Rock Pack", "kodokunohate", "孤独の果て -extend edition-", "End of Solitude -extend edition-"},
    {17, "Rock Pack", "rolling_girl", "ローリンガール", "Rollin' Girl"},
    {17, "Rock Pack", "toumei_suisai", "透明水彩", "Transparent Watercolors"},
    {96, "MikuFlick Pack 4", "popipo", "ぽっぴっぽー", "PoPiPo"},
    {96, "MikuFlick Pack 4", "koi_suru_vocaloid", "恋スルVOC@LOID", "VOC@LOID in Love"},
    {96, "MikuFlick Pack 4", "hatsunemiku_no_syoushitsu", "初音ミクの消失", "The Disappearance of Hatsune Miku"},
    {97, "MikuFlick Pack 2", "melt", "メルト", "Melt"},
    {97, "MikuFlick Pack 2", "stargazer", "StargazeR", NULL},
    {97, "MikuFlick Pack 2", "moon", "moon", NULL},
    {98, "MikuFlick Pack 3", "world_is_mine", "ワールドイズマイン", "World is Mine"},
    {98, "MikuFlick Pack 3", "anata_no_utahime", "あなたの歌姫", "Your Diva"},
    {98, "MikuFlick Pack 3", "timelimit", "タイムリミット", "Time Limit"},
    {99, "MikuFlick Pack 1", "romio_to_cinderella", "ロミオとシンデレラ", "Romeo and Cinderella"},
    {99, "MikuFlick Pack 1", "electric_angew", "えれくとりっく・えんじぇぅ", "Electric Angel"},
    {99, "MikuFlick Pack 1", "finder", "ファインダー(DSLR remix - re:edit)", "Finder (DSLR remix - re:edit)"},
};

const DlcKnownSong *dlc_known_songs(int *n) {
    *n = (int)(sizeof k_known / sizeof *k_known);
    return k_known;
}

void dlc_song_latin(const char *movie, const char *title, char *out, size_t cap) {
    static const char *particles[] = {"no", "ni", "wa", "ga", "wo", "to", "de", "he", "mo"};
    if (!cap) return;
    out[0] = 0;
    bool latin = true;
    for (const unsigned char *p = (const unsigned char *)title; *p && latin; p++) latin = *p < 0x80;
    if (latin || !movie[0]) return;
    for (size_t i = 0; i < sizeof k_known / sizeof *k_known; i++)
        if (k_known[i].en && !strcmp(k_known[i].movie, movie)) { snprintf(out, cap, "%s", k_known[i].en); return; }
    size_t n = 0;
    const char *p = movie;
    while (*p && n + 1 < cap) {
        while (*p == '_') p++;
        size_t len = strcspn(p, "_");
        if (!len) break;
        if (n && n + 1 < cap) out[n++] = ' ';
        bool small = false;
        for (size_t k = 0; n && k < sizeof particles / sizeof *particles; k++)
            small |= strlen(particles[k]) == len && !strncmp(p, particles[k], len);
        for (size_t k = 0; k < len && n + 1 < cap; k++) {
            char ch = p[k];
            if (k == 0 && !small && ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
            out[n++] = ch;
        }
        p += len;
    }
    out[n] = 0;
}
