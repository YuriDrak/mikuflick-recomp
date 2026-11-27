/*
 * In-game import of the user's own Miku Flick/02 IPA (Android has no PC-side
 * `make setup`). An IPA is a zip archive; this file holds a minimal reader for
 * it (central directory, zip64 sizes, stored and deflated entries, CRC-32) and
 * the import itself:
 *
 *   1. find Payload/<App>.app/<App>, inflate the game binary into memory and
 *      write gamedata.txt from it with mfx.c - this also rejects other builds
 *      before anything large is written;
 *   2. check free space, then extract the files the game loads (top-level
 *      .png .plist .usm .adx .caf of the app) into MikuFlick2.app.import/,
 *      verifying sizes and CRCs;
 *   3. swap the new folder in as MikuFlick2.app and gamedata.txt last, so a
 *      half-finished import never looks complete.
 *
 * Nothing is ever written outside `dir`. Archive paths with "..", absolute
 * paths or backslashes are refused.
 */
#define _FILE_OFFSET_BITS 64
#include "mf.h"
#include "mfx.h"

#include <dirent.h>
#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define APP_DIR "MikuFlick2.app"
#define MAX_BINARY (64u << 20)

static void seterr(char *err, size_t n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, n, fmt, ap);
    va_end(ap);
}

/* ---------------- CRC-32 ---------------- */

static uint32_t g_crc[256];

static void crc_init(void) {
    if (g_crc[1]) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
        g_crc[i] = c;
    }
}

static uint32_t crc_update(uint32_t crc, const uint8_t *p, size_t n) {
    crc = ~crc;
    while (n--) crc = g_crc[(crc ^ *p++) & 0xff] ^ (crc >> 8);
    return ~crc;
}

/* ---------------- buffered input over the archive fd ---------------- */

typedef struct {
    int fd;
    uint64_t pos, end;      /* next file offset to read, end of this entry's data */
    size_t n, i;
    uint8_t buf[1 << 16];
} In;

static int in_byte(In *in) {
    if (in->i == in->n) {
        if (in->pos >= in->end) return -1;
        uint64_t want = in->end - in->pos;
        if (want > sizeof in->buf) want = sizeof in->buf;
        ssize_t r = pread(in->fd, in->buf, (size_t)want, (off_t)in->pos);
        if (r <= 0) return -1;
        in->n = (size_t)r;
        in->i = 0;
        in->pos += (uint64_t)r;
    }
    return in->buf[in->i++];
}

static bool read_at(int fd, uint64_t off, void *dst, size_t n) {
    uint8_t *d = dst;
    while (n) {
        ssize_t r = pread(fd, d, n, (off_t)off);
        if (r <= 0) return false;
        d += r;
        off += (uint64_t)r;
        n -= (size_t)r;
    }
    return true;
}

/* ---------------- inflate (RFC 1951) ---------------- */

#define FASTBITS 10

typedef struct {
    uint16_t count[16];               /* codes per length (count[0] unused after build) */
    uint16_t symbol[320];             /* symbols ordered by code (canonical) */
    uint16_t fast[1 << FASTBITS];     /* len<<9 | sym for codes <= FASTBITS bits, 0 = longer */
} Huff;

typedef int (*Sink)(void *ud, const uint8_t *p, size_t n);   /* nonzero = abort */

typedef struct {
    In *in;
    uint64_t bitbuf;
    int bitcnt, pad;
    uint8_t win[1 << 16];             /* output ring; flushed every 32 KiB */
    uint64_t total, flushed, limit;
    uint32_t crc;
    Sink sink;
    void *ud;
    jmp_buf fail;
    const char *why;
    Huff lencode, distcode;
} Inf;

static void inf_fail(Inf *s, const char *why) {
    s->why = why;
    longjmp(s->fail, 1);
}

static void need(Inf *s, int n) {
    while (s->bitcnt < n) {
        int b = in_byte(s->in);
        if (b < 0) {   /* decoders may peek past the last byte; consuming it is an error caught below */
            if (++s->pad > 8) inf_fail(s, "archive truncated");
            b = 0;
        }
        s->bitbuf |= (uint64_t)b << s->bitcnt;
        s->bitcnt += 8;
    }
}

static unsigned getbits(Inf *s, int n) {
    if (!n) return 0;
    need(s, n);
    unsigned v = (unsigned)(s->bitbuf & ((1u << n) - 1));
    s->bitbuf >>= n;
    s->bitcnt -= n;
    return v;
}

static void flush(Inf *s) {
    uint64_t n = s->total - s->flushed;
    if (!n) return;
    const uint8_t *p = s->win + (s->flushed & 0xffff);   /* flushed is 32 KiB aligned: contiguous */
    s->crc = crc_update(s->crc, p, (size_t)n);
    if (s->sink && s->sink(s->ud, p, (size_t)n)) inf_fail(s, "could not write the extracted file");
    s->flushed = s->total;
}

static inline void put(Inf *s, uint8_t b) {
    s->win[s->total & 0xffff] = b;
    s->total++;
    if (!(s->total & 0x7fff)) {
        if (s->total > s->limit) inf_fail(s, "entry larger than recorded");
        flush(s);
    }
}

static unsigned rev(unsigned c, int len) {
    unsigned r = 0;
    while (len--) { r = r << 1 | (c & 1); c >>= 1; }
    return r;
}

/* returns 0 complete, >0 incomplete, <0 over-subscribed */
static int build(Huff *h, const uint8_t *length, int n) {
    memset(h->count, 0, sizeof h->count);
    memset(h->fast, 0, sizeof h->fast);
    for (int i = 0; i < n; i++) h->count[length[i]]++;
    if (h->count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len < 16; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;
    }
    uint16_t offs[16];
    offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = (uint16_t)(offs[len] + h->count[len]);
    for (int i = 0; i < n; i++)
        if (length[i]) h->symbol[offs[length[i]]++] = (uint16_t)i;
    unsigned next[16], code = 0;
    next[0] = 0;
    for (int len = 1; len < 16; len++) {
        code = (code + (len > 1 ? h->count[len - 1] : 0)) << 1;
        next[len] = code;
    }
    for (int i = 0; i < n; i++) {
        int L = length[i];
        if (!L) continue;
        unsigned c = next[L]++;
        if (L <= FASTBITS)
            for (unsigned j = rev(c, L); j < (1u << FASTBITS); j += 1u << L) h->fast[j] = (uint16_t)(L << 9 | i);
    }
    return left;
}

static int decode(Inf *s, const Huff *h) {
    need(s, FASTBITS);
    uint16_t e = h->fast[s->bitbuf & ((1u << FASTBITS) - 1)];
    if (e) {
        int L = e >> 9;
        s->bitbuf >>= L;
        s->bitcnt -= L;
        return e & 511;
    }
    int code = 0, first = 0, index = 0;   /* canonical decode, one bit at a time (puff) */
    for (int len = 1; len < 16; len++) {
        code |= (int)getbits(s, 1);
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    inf_fail(s, "corrupt compressed data");
    return -1;
}

static const uint16_t k_lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                     35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t k_lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t k_dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                     1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t k_dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static void codes(Inf *s, const Huff *lc, const Huff *dc) {
    for (;;) {
        int sym = decode(s, lc);
        if (sym < 256) { put(s, (uint8_t)sym); continue; }
        if (sym == 256) return;
        sym -= 257;
        if (sym >= 29) inf_fail(s, "corrupt compressed data");
        unsigned len = k_lbase[sym] + getbits(s, k_lext[sym]);
        int ds = decode(s, dc);
        if (ds >= 30) inf_fail(s, "corrupt compressed data");
        uint64_t dist = k_dbase[ds] + getbits(s, k_dext[ds]);
        if (dist > s->total) inf_fail(s, "corrupt compressed data");
        while (len--) put(s, s->win[(s->total - dist) & 0xffff]);
    }
}

static void block_fixed(Inf *s) {
    uint8_t l[288];
    for (int i = 0; i < 144; i++) l[i] = 8;
    for (int i = 144; i < 256; i++) l[i] = 9;
    for (int i = 256; i < 280; i++) l[i] = 7;
    for (int i = 280; i < 288; i++) l[i] = 8;
    build(&s->lencode, l, 288);
    for (int i = 0; i < 30; i++) l[i] = 5;
    build(&s->distcode, l, 30);
    codes(s, &s->lencode, &s->distcode);
}

static void block_dynamic(Inf *s) {
    static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    int nlen = (int)getbits(s, 5) + 257, ndist = (int)getbits(s, 5) + 1, ncode = (int)getbits(s, 4) + 4;
    if (nlen > 286 || ndist > 30) inf_fail(s, "corrupt compressed data");
    uint8_t lengths[320] = {0};
    for (int i = 0; i < ncode; i++) lengths[order[i]] = (uint8_t)getbits(s, 3);
    if (build(&s->lencode, lengths, 19) != 0) inf_fail(s, "corrupt compressed data");
    int idx = 0;
    while (idx < nlen + ndist) {
        int sym = decode(s, &s->lencode);
        if (sym < 16) { lengths[idx++] = (uint8_t)sym; continue; }
        int len = 0, rep;
        if (sym == 16) {
            if (!idx) inf_fail(s, "corrupt compressed data");
            len = lengths[idx - 1];
            rep = 3 + (int)getbits(s, 2);
        } else if (sym == 17) {
            rep = 3 + (int)getbits(s, 3);
        } else {
            rep = 11 + (int)getbits(s, 7);
        }
        if (idx + rep > nlen + ndist) inf_fail(s, "corrupt compressed data");
        while (rep--) lengths[idx++] = (uint8_t)len;
    }
    if (!lengths[256]) inf_fail(s, "corrupt compressed data");
    int e = build(&s->lencode, lengths, nlen);
    if (e < 0 || (e > 0 && nlen - s->lencode.count[0] != 1)) inf_fail(s, "corrupt compressed data");
    e = build(&s->distcode, lengths + nlen, ndist);
    if (e < 0 || (e > 0 && ndist - s->distcode.count[0] != 1)) inf_fail(s, "corrupt compressed data");
    codes(s, &s->lencode, &s->distcode);
}

static void block_stored(Inf *s) {
    s->bitbuf >>= s->bitcnt & 7;   /* to a byte boundary */
    s->bitcnt -= s->bitcnt & 7;
    unsigned len = getbits(s, 16), nlen = getbits(s, 16);
    if ((len ^ 0xffff) != nlen) inf_fail(s, "corrupt compressed data");
    while (len--) put(s, (uint8_t)getbits(s, 8));
}

/* Inflate one entry (method 8) or copy it (method 0). usize bounds the output. */
static bool unpack(In *in, int method, uint64_t usize, Sink sink, void *ud, uint32_t *crc, const char **why) {
    Inf *s = calloc(1, sizeof *s);
    if (!s) { *why = "out of memory"; return false; }
    s->in = in;
    s->limit = usize;
    s->sink = sink;
    s->ud = ud;
    bool ok = false;
    if (setjmp(s->fail)) {
        *why = s->why;
    } else {
        if (method == 0) {
            for (uint64_t i = 0; i < usize; i++) {
                int b = in_byte(in);
                if (b < 0) inf_fail(s, "archive truncated");
                put(s, (uint8_t)b);
            }
        } else {
            int last;
            do {
                last = (int)getbits(s, 1);
                int type = (int)getbits(s, 2);
                if (type == 0) block_stored(s);
                else if (type == 1) block_fixed(s);
                else if (type == 2) block_dynamic(s);
                else inf_fail(s, "corrupt compressed data");
            } while (!last);
        }
        flush(s);
        if (s->total != usize) inf_fail(s, "entry size does not match the archive");
        *crc = s->crc;
        ok = true;
    }
    free(s);
    return ok;
}

/* ---------------- zip directory ---------------- */

typedef struct {
    char name[256];
    int method;
    uint32_t crc;
    uint64_t csize, usize, lho;
} Entry;

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t le64(const uint8_t *p) { return (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32; }

static Entry *read_directory(int fd, uint64_t fsize, int *count, char *err, size_t errlen) {
    uint8_t tail[65557];
    size_t tn = fsize < sizeof tail ? (size_t)fsize : sizeof tail;
    if (tn < 22 || !read_at(fd, fsize - tn, tail, tn)) { seterr(err, errlen, "This file is not a zip/IPA archive (or the download is incomplete)."); return NULL; }
    long e = -1;
    for (long i = (long)tn - 22; i >= 0; i--)
        if (le32(tail + i) == 0x06054b50u) { e = i; break; }
    if (e < 0) { seterr(err, errlen, "This file is not a zip/IPA archive (or the download is incomplete)."); return NULL; }
    uint64_t n = le16(tail + e + 10), cdsize = le32(tail + e + 12), cdoff = le32(tail + e + 16);
    if (n == 0xffff || cdsize == 0xffffffffu || cdoff == 0xffffffffu) {   /* zip64 */
        uint8_t loc[20], rec[56];
        uint64_t eocd = fsize - tn + (uint64_t)e;
        if (eocd < 20 || !read_at(fd, eocd - 20, loc, 20) || le32(loc) != 0x07064b50u ||
            !read_at(fd, le64(loc + 8), rec, 56) || le32(rec) != 0x06064b50u) {
            seterr(err, errlen, "Unsupported zip archive.");
            return NULL;
        }
        n = le64(rec + 32);
        cdsize = le64(rec + 40);
        cdoff = le64(rec + 48);
    }
    if (n == 0 || n > 100000 || cdsize > (64u << 20) || cdoff + cdsize > fsize) {
        seterr(err, errlen, "Damaged zip archive (directory).");
        return NULL;
    }
    uint8_t *cd = malloc((size_t)cdsize);
    Entry *ents = calloc((size_t)n, sizeof *ents);
    if (!cd || !ents || !read_at(fd, cdoff, cd, (size_t)cdsize)) {
        free(cd);
        free(ents);
        seterr(err, errlen, "Could not read the archive.");
        return NULL;
    }
    int k = 0;
    size_t p = 0;
    for (uint64_t i = 0; i < n; i++) {
        if (p + 46 > cdsize || le32(cd + p) != 0x02014b50u) break;
        const uint8_t *h = cd + p;
        unsigned nl = le16(h + 28), xl = le16(h + 30), cl = le16(h + 32);
        if (p + 46 + nl + xl + cl > cdsize) break;
        Entry *en = &ents[k];
        en->method = le16(h + 10);
        en->crc = le32(h + 16);
        en->csize = le32(h + 20);
        en->usize = le32(h + 24);
        en->lho = le32(h + 42);
        if (le16(h + 8) & 1) en->method = -1;   /* encrypted */
        const uint8_t *x = h + 46 + nl, *xe = x + xl;
        while (x + 4 <= xe) {   /* zip64 extra: only the fields that overflowed, in this order */
            unsigned id = le16(x), sz = le16(x + 2);
            const uint8_t *v = x + 4, *ve = v + sz;
            if (ve > xe) break;
            if (id == 1) {
                if (en->usize == 0xffffffffu && v + 8 <= ve) { en->usize = le64(v); v += 8; }
                if (en->csize == 0xffffffffu && v + 8 <= ve) { en->csize = le64(v); v += 8; }
                if (en->lho == 0xffffffffu && v + 8 <= ve) { en->lho = le64(v); }
            }
            x = ve;
        }
        if (nl < sizeof en->name) {
            memcpy(en->name, h + 46, nl);
            en->name[nl] = 0;
            k++;
        }
        p += 46 + nl + xl + cl;
    }
    free(cd);
    *count = k;
    return ents;
}

/* where the entry's data starts (after its local header) */
static bool data_offset(int fd, const Entry *e, uint64_t fsize, uint64_t *off) {
    uint8_t lh[30];
    if (!read_at(fd, e->lho, lh, 30) || le32(lh) != 0x04034b50u) return false;
    *off = e->lho + 30 + le16(lh + 26) + le16(lh + 28);
    return *off + e->csize <= fsize;
}

/* ---------------- files ---------------- */

static void remove_tree(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (d) {
            struct dirent *de;
            while ((de = readdir(d))) {
                if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
                char sub[1280];
                snprintf(sub, sizeof sub, "%s/%s", path, de->d_name);
                remove_tree(sub);
            }
            closedir(d);
        }
        rmdir(path);
    } else {
        unlink(path);
    }
}

static bool wanted(const char *rest) {
    /* the help text and option subtitles (help.c) */
    if (!strcmp(rest, "en.lproj/Localizable.strings") || !strcmp(rest, "ja.lproj/Localizable.strings")) return true;
    if (!*rest || strchr(rest, '/') || strchr(rest, '\\') || rest[0] == '.') return false;   /* top level only */
    const char *dot = strrchr(rest, '.');
    if (!dot) return false;
    static const char *ext[] = {".png", ".plist", ".usm", ".adx", ".caf"};
    for (size_t i = 0; i < sizeof ext / sizeof *ext; i++)
        if (!strcasecmp(dot, ext[i])) return true;
    return false;
}

typedef struct {
    FILE *f;
    uint8_t *mem;   /* binary into memory */
    size_t mem_n;
    IpaProgress cb;
    void *ud;
    const char *label;
    uint64_t done_c, total_c, entry_c, entry_u, out;   /* progress in compressed bytes */
    uint64_t last_report;
} Job;

static int sink(void *ud, const uint8_t *p, size_t n) {
    Job *j = ud;
    if (j->mem) {
        memcpy(j->mem + j->mem_n, p, n);
        j->mem_n += n;
    } else if (fwrite(p, 1, n, j->f) != n) {
        return 1;
    }
    j->out += n;
    if (j->cb && j->out - j->last_report >= (2u << 20)) {
        j->last_report = j->out;
        double part = j->entry_u ? (double)j->entry_c * (double)j->out / (double)j->entry_u : 0;
        j->cb(j->ud, j->total_c ? (j->done_c + part) / (double)j->total_c : 0, j->label);
    }
    return 0;
}

static bool extract(int fd, uint64_t fsize, const Entry *e, Job *j, char *err, size_t errlen) {
    uint64_t off;
    if (e->method != 0 && e->method != 8) {
        seterr(err, errlen, "Unsupported compression in %s.", e->name);
        return false;
    }
    if (!data_offset(fd, e, fsize, &off)) {
        seterr(err, errlen, "Damaged archive (%s).", e->name);
        return false;
    }
    In *in = malloc(sizeof *in);
    if (!in) { seterr(err, errlen, "Out of memory."); return false; }
    in->fd = fd;
    in->pos = off;
    in->end = off + e->csize;
    in->n = in->i = 0;
    j->entry_c = e->csize;
    j->entry_u = e->usize;
    j->out = j->last_report = 0;
    uint32_t crc = 0;
    const char *why = NULL;
    bool ok = unpack(in, e->method, e->usize, sink, j, &crc, &why);
    free(in);
    if (!ok) {
        seterr(err, errlen, "Could not extract %s: %s.", e->name, why ? why : "error");
        return false;
    }
    if (crc != e->crc) {
        seterr(err, errlen, "Damaged archive: checksum error in %s.", e->name);
        return false;
    }
    j->done_c += e->csize;
    return true;
}

bool ipa_import(int fd, const char *dir, IpaProgress cb, void *ud, char *err, size_t errlen) {
    crc_init();
    if (strlen(dir) > 1000) { seterr(err, errlen, "Storage path too long."); return false; }
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) {
        seterr(err, errlen, "Cannot read this file directly. Copy the IPA to the device's storage (e.g. Downloads) and pick it there.");
        return false;
    }
    uint64_t fsize = (uint64_t)st.st_size;
    int n = 0;
    Entry *ents = read_directory(fd, fsize, &n, err, errlen);
    if (!ents) return false;

    /* locate Payload/<App>.app/<App> */
    char prefix[256] = "";
    const Entry *bin = NULL;
    for (int i = 0; i < n && !bin; i++) {
        const char *s = ents[i].name;
        if (strncmp(s, "Payload/", 8)) continue;
        const char *a = strstr(s + 8, ".app/");
        if (!a || memchr(s + 8, '/', (size_t)(a - (s + 8)))) continue;
        size_t stem = (size_t)(a - (s + 8));
        if (strlen(a + 5) == stem && !strncmp(a + 5, s + 8, stem)) {
            bin = &ents[i];
            snprintf(prefix, sizeof prefix, "%.*s", (int)(a + 5 - s), s);
        }
    }
    if (!bin) {
        free(ents);
        seterr(err, errlen, "No iOS app found in this file. Pick the Miku Flick/02 .ipa.");
        return false;
    }
    size_t plen = strlen(prefix);

    uint64_t need_u = 0, total_c = bin->csize;
    int nfiles = 0;
    for (int i = 0; i < n; i++)
        if (!strncmp(ents[i].name, prefix, plen) && wanted(ents[i].name + plen)) {
            need_u += ents[i].usize;
            total_c += ents[i].csize;
            nfiles++;
        }

    char path[1600], tmp[1280], gd_tmp[1280];
    Job j = {.cb = cb, .ud = ud, .total_c = total_c};
    bool ok = false;

    /* 1. game binary -> gamedata.txt (checks the version before the big extraction) */
    if (bin->usize == 0 || bin->usize > MAX_BINARY) { seterr(err, errlen, "Unexpected game binary size."); goto out; }
    j.mem = malloc((size_t)bin->usize);
    if (!j.mem) { seterr(err, errlen, "Out of memory."); goto out; }
    j.label = "game binary";
    if (cb) cb(ud, 0, j.label);
    if (!extract(fd, fsize, bin, &j, err, errlen)) goto out;
    mkdir(dir, 0755);
    snprintf(gd_tmp, sizeof gd_tmp, "%s/gamedata.txt.import", dir);
    FILE *g = fopen(gd_tmp, "w");
    if (!g) { seterr(err, errlen, "Cannot write to %s (%s).", dir, strerror(errno)); goto out; }
    char why[256];
    int nsongs = mfx_write_gamedata(j.mem, j.mem_n, g, why, sizeof why);
    if (fclose(g) != 0 && nsongs >= 0) { nsongs = -1; snprintf(why, sizeof why, "could not write gamedata.txt"); }
    free(j.mem);
    j.mem = NULL;
    if (nsongs < 0) {
        remove(gd_tmp);
        seterr(err, errlen, "This IPA cannot be used: %s.", why);
        goto out;
    }
    if (nfiles < 50) { remove(gd_tmp); seterr(err, errlen, "The game's data files are missing from this IPA."); goto out; }

    /* 2. space, then the files the game loads */
    struct statvfs vfs;
    if (statvfs(dir, &vfs) == 0) {
        uint64_t avail = (uint64_t)vfs.f_bavail * vfs.f_frsize, want = need_u + (64u << 20);
        if (avail < want) {
            remove(gd_tmp);
            seterr(err, errlen, "Not enough free space: the game needs %llu MB, %llu MB are free.",
                   (unsigned long long)(want >> 20), (unsigned long long)(avail >> 20));
            goto out;
        }
    }
    snprintf(tmp, sizeof tmp, "%s/%s.import", dir, APP_DIR);
    remove_tree(tmp);
    if (mkdir(tmp, 0755) != 0) { remove(gd_tmp); seterr(err, errlen, "Cannot create %s (%s).", tmp, strerror(errno)); goto out; }
    for (int i = 0; i < n; i++) {
        const Entry *e = &ents[i];
        if (strncmp(e->name, prefix, plen) || !wanted(e->name + plen)) continue;
        snprintf(path, sizeof path, "%s/%s", tmp, e->name + plen);
        char *slash = strrchr(path, '/');   /* <lang>.lproj/ (the only wanted subfolders) */
        if (slash > path + strlen(tmp)) { *slash = 0; mkdir(path, 0755); *slash = '/'; }
        j.f = fopen(path, "wb");
        if (!j.f) { seterr(err, errlen, "Cannot write %s (%s).", path, strerror(errno)); goto fail_tmp; }
        j.label = e->name + plen;
        bool good = extract(fd, fsize, e, &j, err, errlen);
        if (fclose(j.f) != 0 && good) { good = false; seterr(err, errlen, "Could not write %s (storage full?).", e->name + plen); }
        j.f = NULL;
        if (!good) goto fail_tmp;
    }

    /* 3. swap in: app folder first, gamedata.txt last */
    char old[1600];
    snprintf(path, sizeof path, "%s/%s", dir, APP_DIR);
    snprintf(old, sizeof old, "%s/%s.old", dir, APP_DIR);
    remove_tree(old);
    rename(path, old);   /* may not exist */
    if (rename(tmp, path) != 0) {
        rename(old, path);
        seterr(err, errlen, "Cannot replace %s (%s).", path, strerror(errno));
        goto fail_tmp;
    }
    remove_tree(old);
    snprintf(path, sizeof path, "%s/gamedata.txt", dir);
    if (rename(gd_tmp, path) != 0) { seterr(err, errlen, "Cannot write %s (%s).", path, strerror(errno)); goto out; }
    if (cb) cb(ud, 1.0, "done");
    log_msg("import: %d songs, %d files, %llu MB into %s", nsongs, nfiles, (unsigned long long)(need_u >> 20), dir);
    ok = true;
    goto out;

fail_tmp:
    remove_tree(tmp);
    remove(gd_tmp);
out:
    free(j.mem);
    free(ents);
    return ok;
}
