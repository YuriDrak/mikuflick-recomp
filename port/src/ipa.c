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
 *   3. DLC: IPAs that carry the game's data container (iMazing style,
 *      Container/Library/InstallData/Mov_<pack>/ + Container/Documents/MikuFlick2.dat)
 *      get the pack files flattened into the same folder, and the DLC song
 *      table is read from that original save (dlc.c) and appended to gamedata.txt.
 *      Earlier packs the IPA does not bring are kept (keep_dlc_songs);
 *   4. swap the new folder in as MikuFlick2.app and gamedata.txt last, so a
 *      half-finished import never looks complete.
 *
 * Nothing is ever written outside `dir`. Archive paths with "..", absolute
 * paths or backslashes are refused.
 *
 * The archive is read through a ZipSrc (a local fd here, HTTP range requests in
 * net.c), so the same reader serves dlc_catalog_* at the end of this file: the pack
 * list of a remote IPA with DLC and the install of single packs (DLC page).
 */
#define _FILE_OFFSET_BITS 64
#include "mf.h"
#include "dlc.h"
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
#define MAX_SAVE (64u << 20)   /* original MikuFlick2.dat (1.5 MB with all DLC) */

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

/* ---------------- buffered input over the archive (ZipSrc: local file or HTTP ranges) ---------------- */

typedef struct {
    const ZipSrc *src;
    uint64_t pos, end;      /* next file offset to read, end of this entry's data */
    size_t n, i;
    uint8_t buf[1 << 16];
} In;

static int in_byte(In *in) {
    if (in->i == in->n) {
        if (in->pos >= in->end) return -1;
        uint64_t want = in->end - in->pos;
        if (want > sizeof in->buf) want = sizeof in->buf;
        if (!in->src->read(in->src->ud, in->pos, in->buf, (size_t)want)) return -1;
        in->n = (size_t)want;
        in->i = 0;
        in->pos += want;
    }
    return in->buf[in->i++];
}

static bool read_at(const ZipSrc *src, uint64_t off, void *dst, size_t n) {
    return off <= src->size && n <= src->size - off && src->read(src->ud, off, dst, n);
}

/* local archive file */
static bool fd_read(void *ud, uint64_t off, void *dst, size_t n) {
    int fd = *(const int *)ud;
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

static Entry *read_directory(const ZipSrc *src, int *count, char *err, size_t errlen) {
    uint64_t fsize = src->size;
    uint8_t tail[65557];
    size_t tn = fsize < sizeof tail ? (size_t)fsize : sizeof tail;
    if (tn < 22 || !read_at(src, fsize - tn, tail, tn)) { seterr(err, errlen, "This file is not a zip/IPA archive (or the download is incomplete)."); return NULL; }
    long e = -1;
    for (long i = (long)tn - 22; i >= 0; i--)
        if (le32(tail + i) == 0x06054b50u) { e = i; break; }
    if (e < 0) { seterr(err, errlen, "This file is not a zip/IPA archive (or the download is incomplete)."); return NULL; }
    uint64_t n = le16(tail + e + 10), cdsize = le32(tail + e + 12), cdoff = le32(tail + e + 16);
    if (n == 0xffff || cdsize == 0xffffffffu || cdoff == 0xffffffffu) {   /* zip64 */
        uint8_t loc[20], rec[56];
        uint64_t eocd = fsize - tn + (uint64_t)e;
        if (eocd < 20 || !read_at(src, eocd - 20, loc, 20) || le32(loc) != 0x07064b50u ||
            !read_at(src, le64(loc + 8), rec, 56) || le32(rec) != 0x06064b50u) {
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
    if (!cd || !ents || !read_at(src, cdoff, cd, (size_t)cdsize)) {
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
static bool data_offset(const ZipSrc *src, const Entry *e, uint64_t *off) {
    uint64_t fsize = src->size;
    uint8_t lh[30];
    if (!read_at(src, e->lho, lh, 30) || le32(lh) != 0x04034b50u) return false;
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
    size_t mem_n, mem_cap;
    IpaProgress cb;
    void *ud;
    const char *label;
    uint64_t done_c, total_c, entry_c, entry_u, out;   /* progress in compressed bytes */
    uint64_t last_report;
} Job;

static int sink(void *ud, const uint8_t *p, size_t n) {
    Job *j = ud;
    if (j->mem) {
        if (n > j->mem_cap - j->mem_n) return 1;   /* more than the directory said */
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

static bool extract(const ZipSrc *src, const Entry *e, Job *j, char *err, size_t errlen) {
    uint64_t off;
    if (e->method != 0 && e->method != 8) {
        seterr(err, errlen, "Unsupported compression in %s.", e->name);
        return false;
    }
    if (!data_offset(src, e, &off)) {
        seterr(err, errlen, "Damaged archive (%s).", e->name);
        return false;
    }
    In *in = malloc(sizeof *in);
    if (!in) { seterr(err, errlen, "Out of memory."); return false; }
    in->src = src;
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

static bool have_in_dir(void *ud, const char *name) {
    char path[1600];
    struct stat st;
    snprintf(path, sizeof path, "%s/%s", (const char *)ud, name);
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

/* Inflates the original save into memory and appends its DLC songs (those whose files are now in
   app_tmp) to gamedata. Returns the number of DLC songs; failures are logged, not fatal. */
static int import_dlc_songs(const ZipSrc *src, const Entry *save_e, Job *j, const char *app_tmp, const char *gamedata) {
    char why[256] = "";
    int n = -1;
    j->mem = malloc((size_t)save_e->usize);
    j->mem_n = 0;
    j->mem_cap = (size_t)save_e->usize;
    j->label = "DLC song list";
    if (!j->mem) snprintf(why, sizeof why, "out of memory");
    else if (extract(src, save_e, j, why, sizeof why)) {
        FILE *g = fopen(gamedata, "a");
        if (!g) snprintf(why, sizeof why, "cannot append to %s", gamedata);
        else {
            n = dlc_write_songs(j->mem, j->mem_n, have_in_dir, (void *)app_tmp, g, why, sizeof why);
            if (fclose(g) != 0 && n >= 0) { n = -1; snprintf(why, sizeof why, "could not write %s", gamedata); }
        }
    }
    free(j->mem);
    j->mem = NULL;
    if (n < 0) { log_msg("import: DLC songs skipped (%s): %s", DLC_SAVE_PATH, why); return 0; }
    return n;
}

/* move one file from the previous app folder into the new one (same file system, so a rename);
   true if the new folder has it afterwards */
static bool move_over(const char *old_app, const char *new_app, const char *name) {
    char from[1600], to[1600];
    snprintf(to, sizeof to, "%s/%s", new_app, name);
    if (have_in_dir((void *)new_app, name)) return true;
    snprintf(from, sizeof from, "%s/%s", old_app, name);
    return rename(from, to) == 0;
}

/* gamedata (the new one, being written) already has a "dlc" line for this movie */
static bool dlc_listed(const char *gamedata, const char *movie) {
    FILE *f = fopen(gamedata, "r");
    if (!f) return false;
    char line[1024], mv[64];
    bool found = false;
    while (!found && fgets(line, sizeof line, f))
        found = !strncmp(line, "dlc ", 4) && sscanf(line + 4, "%*d %*d %*d %*d %*s %63s", mv) == 1 && !strcmp(mv, movie);
    fclose(f);
    return found;
}

/* Carries the previous DLC songs over (their "dlc" lines from the old gamedata.txt, their files from
   the old app folder, which is still in place at this point) instead of dropping them: after an IPA
   without DLC (e.g. the plain one), and for packs an IPA with DLC does not have (e.g. ones downloaded
   on the DLC page). Songs the new IPA brings are left to it. Returns the count. */
static int keep_dlc_songs(const char *gd_old, const char *old_app, const char *new_app, const char *gd_new) {
    FILE *in = fopen(gd_old, "r");
    if (!in) return 0;
    char line[1024], pv[64], mv[64], atlas[64], name[80];
    int n = 0;
    while (fgets(line, sizeof line, in)) {
        if (strncmp(line, "dlc ", 4) || !strchr(line, '\n') ||
            sscanf(line + 4, "%*d %*d %*d %*d %63s %63s %63s", pv, mv, atlas) != 3)
            continue;
        if (strchr(pv, '/') || strchr(mv, '/') || strchr(atlas, '/') || dlc_listed(gd_new, mv)) continue;
        FILE *out = fopen(gd_new, "a");
        if (!out) break;
        snprintf(name, sizeof name, "%s.usm", mv);
        bool ok = move_over(old_app, new_app, name);
        snprintf(name, sizeof name, "%s.png", atlas);
        ok = move_over(old_app, new_app, name) && ok;
        if (strcmp(pv, "-")) { snprintf(name, sizeof name, "%s.adx", pv); move_over(old_app, new_app, name); }
        bool put = ok && fputs(line, out) >= 0;
        if (fclose(out) != 0) put = false;
        n += put;
    }
    fclose(in);
    if (n) log_msg("import: kept %d DLC songs from the previous import", n);
    return n;
}

bool ipa_import(int fd, const char *dir, IpaProgress cb, void *ud, char *err, size_t errlen) {
    crc_init();
    if (strlen(dir) > 1000) { seterr(err, errlen, "Storage path too long."); return false; }
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) {
        seterr(err, errlen, "Cannot read this file directly. Copy the IPA to the device's storage (e.g. Downloads) and pick it there.");
        return false;
    }
    ZipSrc local = {fd_read, &fd, (uint64_t)st.st_size}, *src = &local;
    int n = 0;
    Entry *ents = read_directory(src, &n, err, errlen);
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

    /* file name in MikuFlick2.app for each entry (NULL = skip): the app's own files, then the DLC
       pack files flattened next to them unless the name is taken */
    const char **out_name = calloc((size_t)n + 1, sizeof *out_name);
    if (!out_name) { free(ents); seterr(err, errlen, "Out of memory."); return false; }
    const Entry *save_e = NULL;
    int nfiles = 0, ndlc_files = 0;
    for (int i = 0; i < n; i++)
        if (!strncmp(ents[i].name, prefix, plen) && wanted(ents[i].name + plen)) {
            out_name[i] = ents[i].name + plen;
            nfiles++;
        }
    for (int i = 0; i < n; i++) {
        if (!strcmp(ents[i].name, DLC_SAVE_PATH) && ents[i].usize > 0 && ents[i].usize <= MAX_SAVE) save_e = &ents[i];
        const char *dn = out_name[i] ? NULL : dlc_pack_file(ents[i].name);
        bool taken = false;
        for (int k = 0; dn && k < n && !taken; k++) taken = out_name[k] && !strcasecmp(out_name[k], dn);
        if (dn && !taken) { out_name[i] = dn; ndlc_files++; }
    }
    if (ndlc_files && !save_e) {   /* no song table for them: leave the packs out */
        log_msg("import: DLC packs found but no %s, skipping them", DLC_SAVE_PATH);
        for (int i = 0; i < n; i++)
            if (out_name[i] && dlc_pack_file(ents[i].name)) out_name[i] = NULL;
        ndlc_files = 0;
    }
    if (!ndlc_files) save_e = NULL;

    uint64_t need_u = 0, total_c = bin->csize + (save_e ? save_e->csize : 0);
    for (int i = 0; i < n; i++)
        if (out_name[i]) {
            need_u += ents[i].usize;
            total_c += ents[i].csize;
        }

    char path[1600], tmp[1280], gd_tmp[1280];
    Job j = {.cb = cb, .ud = ud, .total_c = total_c};
    bool ok = false;
    int ndlc = 0;

    /* 1. game binary -> gamedata.txt (checks the version before the big extraction) */
    if (bin->usize == 0 || bin->usize > MAX_BINARY) { seterr(err, errlen, "Unexpected game binary size."); goto out; }
    j.mem = malloc((size_t)bin->usize);
    j.mem_cap = (size_t)bin->usize;
    if (!j.mem) { seterr(err, errlen, "Out of memory."); goto out; }
    j.label = "game binary";
    if (cb) cb(ud, 0, j.label);
    if (!extract(src, bin, &j, err, errlen)) goto out;
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
        if (!out_name[i]) continue;
        snprintf(path, sizeof path, "%s/%s", tmp, out_name[i]);
        char *slash = strrchr(path, '/');   /* <lang>.lproj/ (the only wanted subfolders) */
        if (slash > path + strlen(tmp)) { *slash = 0; mkdir(path, 0755); *slash = '/'; }
        j.f = fopen(path, "wb");
        if (!j.f) { seterr(err, errlen, "Cannot write %s (%s).", path, strerror(errno)); goto fail_tmp; }
        j.label = out_name[i];
        bool good = extract(src, e, &j, err, errlen);
        if (fclose(j.f) != 0 && good) { good = false; seterr(err, errlen, "Could not write %s (storage full?).", out_name[i]); }
        j.f = NULL;
        if (!good) goto fail_tmp;
    }

    /* 3. DLC song table from the original save; a bad save only loses the DLC, not the import */
    if (save_e) ndlc = import_dlc_songs(src, save_e, &j, tmp, gd_tmp);

    /* 4. swap in: app folder first, gamedata.txt last */
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
    {   /* earlier packs this IPA does not bring (all of them for an IPA without DLC) stay */
        char gd_old[1280];
        snprintf(gd_old, sizeof gd_old, "%s/gamedata.txt", dir);
        ndlc += keep_dlc_songs(gd_old, old, path, gd_tmp);
    }
    remove_tree(old);
    snprintf(path, sizeof path, "%s/gamedata.txt", dir);
    if (rename(gd_tmp, path) != 0) { seterr(err, errlen, "Cannot write %s (%s).", path, strerror(errno)); goto out; }
    if (cb) cb(ud, 1.0, "done");
    log_msg("import: %d songs + %d DLC songs, %d files + %d DLC files, %llu MB into %s", nsongs, ndlc, nfiles, ndlc_files,
            (unsigned long long)(need_u >> 20), dir);
    ok = true;
    goto out;

fail_tmp:
    remove_tree(tmp);
    remove(gd_tmp);
out:
    free(j.mem);
    free(out_name);
    free(ents);
    return ok;
}

/* ---------------- per-pack DLC install (DLC page downloads, --dlc-get) ----------------
   Works on any ZipSrc, but is meant for net.c: an IPA with the DLC container is ~7.6 GB, a pack
   ~280 MB. Reads the central directory and the original save (the song table), then only the
   files of the packs the player picks. */

struct DlcCatalog {
    const ZipSrc *src;
    Entry *ents;
    int n;
    DlcSong *songs;
    int nsongs;
};

#define PACK_FILES 64

static const Entry *pack_entry(const DlcCatalog *c, int pack, const char *name) {
    for (int i = 0; i < c->n; i++) {
        if (dlc_pack_id(c->ents[i].name) != pack) continue;
        const char *dn = dlc_pack_file(c->ents[i].name);
        if (dn && !strcmp(dn, name)) return &c->ents[i];
    }
    return NULL;
}

/* archive entries of a song: movie, art sheet, preview (optional). False if movie or art is missing. */
static bool song_entries(const DlcCatalog *c, const DlcSong *s, const Entry *out[3]) {
    char name[80];
    snprintf(name, sizeof name, "%s.usm", s->movie);
    out[0] = pack_entry(c, s->pack, name);
    snprintf(name, sizeof name, "%s.png", s->atlas);
    out[1] = pack_entry(c, s->pack, name);
    out[2] = NULL;
    if (s->preview[0]) {
        snprintf(name, sizeof name, "%s.adx", s->preview);
        out[2] = pack_entry(c, s->pack, name);
    }
    return out[0] && out[1];
}

/* distinct files the songs of a pack need (several songs share one art sheet) */
static int pack_files(const DlcCatalog *c, int pack, const Entry **out, int max) {
    int n = 0;
    for (int i = 0; i < c->nsongs; i++) {
        const Entry *f[3];
        if (c->songs[i].pack != pack || !song_entries(c, &c->songs[i], f)) continue;
        for (int k = 0; k < 3; k++) {
            bool dup = !f[k];
            for (int m = 0; m < n && !dup; m++) dup = out[m] == f[k];
            if (!dup && n < max) out[n++] = f[k];
        }
    }
    return n;
}

/* "Duet_Pack01" -> "Duet Pack" (*num = 1) */
static void pack_title(const char *raw, int pack, char *out, size_t cap, int *num) {
    snprintf(out, cap, "%s", raw);
    size_t n = strlen(out);
    while (n && out[n - 1] >= '0' && out[n - 1] <= '9') n--;
    *num = atoi(out + n);
    while (n && (out[n - 1] == '_' || out[n - 1] == ' ')) n--;
    out[n] = 0;
    for (char *p = out; *p; p++)
        if (*p == '_') *p = ' ';
    if (!out[0]) snprintf(out, cap, "Pack %d", pack);
}

DlcCatalog *dlc_catalog_open(const ZipSrc *src, char *err, size_t errlen) {
    crc_init();
    DlcCatalog *c = calloc(1, sizeof *c);
    if (!c) { seterr(err, errlen, "Out of memory."); return NULL; }
    c->src = src;
    c->ents = read_directory(src, &c->n, err, errlen);
    if (!c->ents) { free(c); return NULL; }
    const Entry *save_e = NULL;
    int npack = 0;
    for (int i = 0; i < c->n; i++) {
        if (!strcmp(c->ents[i].name, DLC_SAVE_PATH)) save_e = &c->ents[i];
        npack += dlc_pack_file(c->ents[i].name) != NULL;
    }
    if (!save_e || !npack) {
        seterr(err, errlen, "This IPA has no DLC packs. The link must point to an IPA that includes them (Container/ folder).");
        goto fail;
    }
    if (save_e->usize == 0 || save_e->usize > MAX_SAVE) { seterr(err, errlen, "Damaged archive (%s).", DLC_SAVE_PATH); goto fail; }
    Job j = {.label = "DLC song list"};
    j.mem = malloc((size_t)save_e->usize);
    j.mem_cap = (size_t)save_e->usize;
    if (!j.mem) { seterr(err, errlen, "Out of memory."); goto fail; }
    bool got = extract(src, save_e, &j, err, errlen);
    char why[200] = "";
    if (got) c->nsongs = dlc_read_songs(j.mem, j.mem_n, &c->songs, why, sizeof why);
    free(j.mem);
    if (!got) goto fail;
    if (c->nsongs < 0) { seterr(err, errlen, "The DLC song list in this IPA cannot be read: %s.", why); goto fail; }
    return c;
fail:
    dlc_catalog_free(c);
    return NULL;
}

void dlc_catalog_free(DlcCatalog *c) {
    if (!c) return;
    free(c->songs);
    free(c->ents);
    free(c);
}

int dlc_catalog_packs(const DlcCatalog *c, DlcPackInfo *out, int max) {
    int np = 0, num[256];
    if (max > 256) max = 256;
    for (int i = 0; i < c->nsongs; i++) {
        const DlcSong *s = &c->songs[i];
        const Entry *f[3];
        if (!song_entries(c, s, f)) continue;
        int k = 0;
        while (k < np && out[k].pack != s->pack) k++;
        if (k == np) {
            if (np == max) continue;
            memset(&out[np], 0, sizeof out[np]);
            out[np].pack = s->pack;
            pack_title(s->pack_name, s->pack, out[np].name, sizeof out[np].name, &num[np]);
            np++;
        }
        DlcPackInfo *p = &out[k];
        if (p->nsongs < DLC_PACK_SONGS) {
            DlcPackSong *ps = &p->songs[p->nsongs];
            snprintf(ps->title, sizeof ps->title, "%s", s->title);
            dlc_song_latin(s->movie, s->title, ps->latin, sizeof ps->latin);
        }
        p->nsongs++;
    }
    for (int k = 0; k < np; k++) {
        const Entry *f[PACK_FILES];
        int nf = pack_files(c, out[k].pack, f, PACK_FILES);
        for (int m = 0; m < nf; m++) out[k].bytes += f[m]->csize;
    }
    bool shared[256] = {false};   /* "MikuFlick_Pack01".."04": keep the number where names repeat */
    for (int k = 0; k < np; k++)
        for (int m = 0; m < np && !shared[k]; m++) shared[k] = m != k && !strcmp(out[m].name, out[k].name);
    for (int k = 0; k < np; k++) {
        if (shared[k] && num[k] > 0) {
            size_t l = strlen(out[k].name);
            snprintf(out[k].name + l, sizeof out[k].name - l, " %d", num[k]);
        }
    }
    for (int a = 1; a < np; a++)   /* by pack number */
        for (int b = a; b > 0 && out[b - 1].pack > out[b].pack; b--) {
            DlcPackInfo t = out[b];
            out[b] = out[b - 1];
            out[b - 1] = t;
        }
    return np;
}

/* a built-in song of gamedata uses this file name (never replaced by a pack file) */
static bool base_uses(const char *gd_text, const char *name) {
    const char *dot = strrchr(name, '.');
    if (!gd_text || !dot) return false;
    size_t stem = (size_t)(dot - name);
    for (const char *l = gd_text; l && *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL) {
        if (strncmp(l, "song ", 5)) continue;
        char pv[64], mv[64], art[64];
        if (sscanf(l + 5, "%*d %63s %63s %63s", pv, mv, art) != 3) continue;
        if ((strlen(pv) == stem && !strncmp(pv, name, stem)) || (strlen(mv) == stem && !strncmp(mv, name, stem)) ||
            (strlen(art) == stem && !strncmp(art, name, stem)))
            return true;
    }
    return false;
}

typedef struct {
    int pack;
    char movie[64];
    const char *line;   /* in the old gamedata text, '\n' terminated */
} OldDlc;

/* gamedata.txt with fresh "dlc" lines: the catalog's songs whose files are in app_dir, plus earlier
   DLC songs (other IPAs or imports) that are still installed, in pack order */
static bool rewrite_gamedata(const DlcCatalog *c, const char *app_dir, const char *gamedata, char *gd_text, char *err,
                             size_t errlen) {
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.dlc.tmp", gamedata);
    FILE *f = fopen(tmp, "w");
    if (!f) { seterr(err, errlen, "Cannot write %s (%s).", tmp, strerror(errno)); return false; }
    int nold = 0, cap = 0;
    OldDlc *old = NULL;
    for (char *l = gd_text; l && *l;) {
        char *nl = strchr(l, '\n');
        size_t len = nl ? (size_t)(nl - l) : strlen(l);
        if (strncmp(l, "dlc ", 4)) {
            fwrite(l, 1, len, f);
            fputc('\n', f);
        } else {
            OldDlc o = {.line = l};
            char pv[64], atlas[64], name[80];
            if (sscanf(l + 4, "%*d %d %*d %*d %63s %63s %63s", &o.pack, pv, o.movie, atlas) == 4 && !strchr(o.movie, '/') &&
                !strchr(atlas, '/')) {
                snprintf(name, sizeof name, "%s.usm", o.movie);
                bool ok = have_in_dir((void *)app_dir, name);
                snprintf(name, sizeof name, "%s.png", atlas);
                if (ok && have_in_dir((void *)app_dir, name)) {
                    if (nold == cap) {
                        OldDlc *v = realloc(old, (size_t)(cap ? cap * 2 : 64) * sizeof *v);
                        if (v) { old = v; cap = cap ? cap * 2 : 64; }
                    }
                    if (nold < cap) old[nold++] = o;   /* out of memory: the song is dropped, not the file */
                }
            }
        }
        l = nl ? nl + 1 : NULL;
    }
    /* pack ids in use, ascending */
    int packs[256], np = 0;
    for (int i = 0; i < c->nsongs + nold; i++) {
        int p = i < c->nsongs ? c->songs[i].pack : old[i - c->nsongs].pack;
        if (i < c->nsongs && !dlc_song_files_present(&c->songs[i], have_in_dir, (void *)app_dir)) continue;
        bool dup = false;
        for (int k = 0; k < np && !dup; k++) dup = packs[k] == p;
        if (dup || np == 256) continue;
        int k = np++;
        while (k > 0 && packs[k - 1] > p) { packs[k] = packs[k - 1]; k--; }
        packs[k] = p;
    }
    int written = 0;
    for (int k = 0; k < np; k++) {
        for (int i = 0; i < c->nsongs; i++)
            if (c->songs[i].pack == packs[k] && dlc_song_files_present(&c->songs[i], have_in_dir, (void *)app_dir)) {
                dlc_print_song(&c->songs[i], f);
                written++;
            }
        for (int i = 0; i < nold; i++) {
            if (old[i].pack != packs[k]) continue;
            bool fresh = false;
            for (int s = 0; s < c->nsongs && !fresh; s++)
                fresh = !strcmp(c->songs[s].movie, old[i].movie) &&
                        dlc_song_files_present(&c->songs[s], have_in_dir, (void *)app_dir);
            if (fresh) continue;
            const char *e = strchr(old[i].line, '\n');
            fwrite(old[i].line, 1, e ? (size_t)(e - old[i].line) : strlen(old[i].line), f);
            fputc('\n', f);
            written++;
        }
    }
    free(old);
    if (fclose(f) != 0) { remove(tmp); seterr(err, errlen, "Could not write %s (storage full?).", tmp); return false; }
    if (rename(tmp, gamedata) != 0) { remove(tmp); seterr(err, errlen, "Cannot replace %s (%s).", gamedata, strerror(errno)); return false; }
    log_msg("dlc: gamedata now lists %d DLC songs", written);
    return true;
}

bool dlc_catalog_install(DlcCatalog *c, int pack, const char *app_dir, const char *gamedata, IpaProgress cb, void *ud,
                         const volatile int *cancel, char *err, size_t errlen) {
    if (strlen(app_dir) > 1000 || strlen(gamedata) > 1000) { seterr(err, errlen, "Storage path too long."); return false; }
    const Entry *files[PACK_FILES];
    int nf = pack_files(c, pack, files, PACK_FILES);
    if (!nf) { seterr(err, errlen, "Pack %d is not in this IPA.", pack); return false; }
    struct stat st;
    char *gd_text = read_file(gamedata, NULL);
    if (stat(app_dir, &st) != 0 || !S_ISDIR(st.st_mode) || !gd_text ||
        (strncmp(gd_text, "song ", 5) && !strstr(gd_text, "\nsong "))) {
        free(gd_text);
        seterr(err, errlen, "Import the game first (no game data in %s).", app_dir);
        return false;
    }
    bool todo[PACK_FILES];
    uint64_t need_u = 0, total_c = 0;
    char path[1200], part[1210];
    for (int i = 0; i < nf; i++) {
        const char *name = dlc_pack_file(files[i]->name);
        snprintf(path, sizeof path, "%s/%s", app_dir, name);
        todo[i] = !(stat(path, &st) == 0 && S_ISREG(st.st_mode) && (uint64_t)st.st_size == files[i]->usize);
        if (todo[i] && base_uses(gd_text, name)) {   /* never over a file of the base game */
            log_msg("dlc: %s is a file of the base game, not replaced", name);
            todo[i] = false;
        }
        if (todo[i]) { need_u += files[i]->usize; total_c += files[i]->csize; }
    }
    struct statvfs vfs;
    if (statvfs(app_dir, &vfs) == 0) {
        uint64_t avail = (uint64_t)vfs.f_bavail * vfs.f_frsize, want = need_u + (16u << 20);
        if (avail < want) {
            free(gd_text);
            seterr(err, errlen, "Not enough free space: this pack needs %llu MB, %llu MB are free.",
                   (unsigned long long)(want >> 20), (unsigned long long)(avail >> 20));
            return false;
        }
    }
    Job j = {.cb = cb, .ud = ud, .total_c = total_c};
    for (int i = 0; i < nf; i++) {
        if (!todo[i]) continue;
        const char *name = dlc_pack_file(files[i]->name);
        if (cancel && *cancel) { free(gd_text); seterr(err, errlen, "Cancelled."); return false; }
        snprintf(path, sizeof path, "%s/%s", app_dir, name);
        snprintf(part, sizeof part, "%s.part", path);
        j.f = fopen(part, "wb");
        if (!j.f) { free(gd_text); seterr(err, errlen, "Cannot write %s (%s).", part, strerror(errno)); return false; }
        j.label = name;
        if (cb) cb(ud, total_c ? (double)j.done_c / (double)total_c : 0, name);
        bool good = extract(c->src, files[i], &j, err, errlen);
        if (fclose(j.f) != 0 && good) { good = false; seterr(err, errlen, "Could not write %s (storage full?).", name); }
        j.f = NULL;
        if (good && rename(part, path) != 0) { good = false; seterr(err, errlen, "Cannot write %s (%s).", path, strerror(errno)); }
        if (!good) {
            remove(part);
            if (cancel && *cancel) seterr(err, errlen, "Cancelled.");
            free(gd_text);
            return false;
        }
    }
    bool ok = rewrite_gamedata(c, app_dir, gamedata, gd_text, err, errlen);
    free(gd_text);
    if (!ok) return false;
    if (cb) cb(ud, 1.0, "done");
    log_msg("dlc: pack %d installed (%d files, %llu MB downloaded) into %s", pack, nf, (unsigned long long)(total_c >> 20), app_dir);
    return true;
}
