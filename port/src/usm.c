/*
 * CRI USM cue-point reader. Walks chunk headers with fseek (the movies are up
 * to ~100 MB) and parses the CUEPOINT_HDRINFO / CUEPOINT_INFO @UTF tables of
 * the @CUE chunks. Format notes: docs/re_notes.md §1-2.
 */
#include "mf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

typedef struct {
    const uint8_t *b;
    uint32_t size, rows_off, str_off, data_off;
    uint16_t ncols, row_width;
    uint32_t nrows;
    const char *name;
    uint8_t flags[32];
    const char *cname[32];
    uint32_t const_off[32]; /* offset of inline constant value */
} Utf;

static int tsize(int t) {
    static const int s[16] = {1, 1, 2, 2, 4, 4, 8, 8, 4, 8, 4, 8, -1, -1, -1, -1};
    return s[t & 15];
}

static const char *ustr(const Utf *u, uint32_t off) {
    uint32_t p = u->str_off + off;
    if (p >= u->size || !memchr(u->b + p, 0, u->size - p)) return "";
    return (const char *)u->b + p;
}

static bool utf_open(Utf *u, const uint8_t *p, size_t avail) {
    memset(u, 0, sizeof *u);
    if (avail < 32 || memcmp(p, "@UTF", 4)) return false;
    u->size = be32(p + 4) + 8;
    if (u->size > avail) return false;
    u->b = p;
    u->rows_off = be16(p + 10) + 8u;
    u->str_off = be32(p + 12) + 8u;
    u->data_off = be32(p + 16) + 8u;
    if (u->rows_off > u->size || u->str_off > u->size || u->data_off > u->size) return false;
    u->name = ustr(u, be32(p + 20));
    u->ncols = be16(p + 24);
    u->row_width = be16(p + 26);
    u->nrows = be32(p + 28);
    if (u->ncols > 32) return false;
    uint32_t c = 32;
    for (int i = 0; i < u->ncols; i++) {
        if (c + 5 > u->rows_off) return false;
        u->flags[i] = p[c];
        u->cname[i] = (p[c] & 0x10) ? ustr(u, be32(p + c + 1)) : "";
        c += 5;
        if ((u->flags[i] & 0xf0) == 0x30 || (u->flags[i] & 0xf0) == 0x70) {
            int n = tsize(u->flags[i] & 15);
            if (n < 0) return false;
            u->const_off[i] = c;
            c += (uint32_t)n;
        }
    }
    return (uint64_t)u->rows_off + (uint64_t)u->nrows * u->row_width <= u->size;
}

static int ucol(const Utf *u, const char *name) {
    for (int i = 0; i < u->ncols; i++) if (!strcmp(u->cname[i], name)) return i;
    return -1;
}

/* pointer to raw value bytes of (row, col), NULL if zero/absent */
static const uint8_t *ucell(const Utf *u, uint32_t row, int col) {
    if (col < 0) return NULL;
    uint32_t p = u->rows_off + row * u->row_width;
    for (int i = 0; i < u->ncols; i++) {
        int st = u->flags[i] & 0xf0, t = u->flags[i] & 15;
        if (st == 0x50) {
            if (i == col) return u->b + p;
            p += (uint32_t)tsize(t);
        } else if (i == col) {
            return (st == 0x30 || st == 0x70) ? u->b + u->const_off[i] : NULL;
        }
    }
    return NULL;
}

static uint64_t uint_of(const Utf *u, uint32_t row, int col) {
    const uint8_t *v = ucell(u, row, col);
    if (!v) return 0;
    switch (u->flags[col] & 15) {
    case 0: case 1: return v[0];
    case 2: case 3: return be16(v);
    case 4: case 5: return be32(v);
    case 6: case 7: return (uint64_t)be32(v) << 32 | be32(v + 4);
    default: return 0;
    }
}

/* text of a string or data column (cue parameters are stored as data blobs) */
static char *text_of(const Utf *u, uint32_t row, int col) {
    const uint8_t *v = ucell(u, row, col);
    if (!v) return strdup("");
    int t = u->flags[col] & 15;
    if (t == 10) return strdup(ustr(u, be32(v)));
    if (t == 11) {
        uint32_t off = u->data_off + be32(v), n = be32(v + 4);
        if (off > u->size || n > u->size - off) return strdup("");
        while (n && u->b[off + n - 1] == 0) n--;
        char *s = malloc(n + 1);
        if (!s) return NULL;
        memcpy(s, u->b + off, n);
        s[n] = 0;
        return s;
    }
    return strdup("");
}

bool usm_read_cues(const char *path, CueList *out) {
    memset(out, 0, sizeof *out);
    out->time_unit = 1000;
    FILE *f = fopen(path, "rb");
    if (!f) { log_msg("cannot open %s", path); return false; }
    uint8_t hdr[16];
    int cap = 0;
    bool found = false;
    long off = 0;
    while (fseek(f, off, SEEK_SET) == 0 && fread(hdr, 1, 16, f) == 16) {
        uint32_t csize = be32(hdr + 4);
        if (csize < 8) break;
        int dtype = hdr[15] & 3;
        if (!memcmp(hdr, "@CUE", 4) && (dtype == 1 || dtype == 3)) {
            uint8_t *chunk = malloc(csize);
            if (!chunk) break;
            if (fseek(f, off + 8, SEEK_SET) != 0 || fread(chunk, 1, csize, f) != csize) { free(chunk); break; }
            uint32_t poff = chunk[1], pad = be16(chunk + 2);
            if (poff < csize && pad <= csize - poff) {
                Utf u;
                if (utf_open(&u, chunk + poff, csize - poff - pad)) {
                    if (!strcmp(u.name, "CUEPOINT_HDRINFO")) {
                        uint64_t tu = uint_of(&u, 0, ucol(&u, "time_unit"));
                        if (tu) out->time_unit = (uint32_t)tu;
                    } else if (!strcmp(u.name, "CUEPOINT_INFO")) {
                        int ct = ucol(&u, "time"), cp = ucol(&u, "parameter");
                        for (uint32_t r = 0; r < u.nrows; r++) {
                            if (out->n == cap) {
                                cap = cap ? cap * 2 : 512;
                                Cue *nc = realloc(out->cues, (size_t)cap * sizeof *nc);
                                if (!nc) break;
                                out->cues = nc;
                            }
                            uint64_t t = uint_of(&u, r, ct);
                            out->cues[out->n].time_ms = (uint32_t)(t * 1000 / out->time_unit);
                            out->cues[out->n].param = text_of(&u, r, cp);
                            out->n++;
                        }
                        found = true;
                    }
                }
            }
            free(chunk);
        }
        off += 8 + (long)csize;
        if (found && !memcmp(hdr, "@SFV", 4) && dtype == 0) break; /* cue tables precede stream data */
    }
    fclose(f);
    /* Rows are already time-ordered in every chart; keep a *stable* insertion
       sort as a safety net because equal-time cues must stay in file order
       (e.g. a SmallTelop "4ょ" must follow its parent note "0じ"). */
    for (int i = 1; i < out->n; i++) {
        Cue c = out->cues[i];
        int j = i - 1;
        while (j >= 0 && out->cues[j].time_ms > c.time_ms) { out->cues[j + 1] = out->cues[j]; j--; }
        out->cues[j + 1] = c;
    }
    return true;
}

void cuelist_free(CueList *c) {
    for (int i = 0; i < c->n; i++) free(c->cues[i].param);
    free(c->cues);
    memset(c, 0, sizeof *c);
}

/* ---------------- stream payloads (replaces the libavformat USM demuxer) ----------------
 * Chunk header: sig[4] | u32be size | u8 ? | u8 payload_off | u16be padding | u8 chno | u8 ? | u8 ? | u8 data_type.
 * The payload is [chunk+8+payload_off, chunk+8+size-padding); data_type 0 is stream data,
 * 1 = header @UTF, 2 = "#HEADER END"/"#CONTENTS END" markers, 3 = metadata. */

struct UsmStream {
    FILE *f;
    long off;
    char sig[4];
    int chno;
    uint8_t *buf;
    size_t cap;
};

UsmStream *usm_stream_open(const char *path, const char *sig, int chno) {
    FILE *f = fopen(path, "rb");
    if (!f) { log_msg("cannot open %s", path); return NULL; }
    UsmStream *s = calloc(1, sizeof *s);
    if (!s) { fclose(f); return NULL; }
    s->f = f;
    memcpy(s->sig, sig, 4);
    s->chno = chno;
    return s;
}

const uint8_t *usm_stream_next(UsmStream *s, size_t *len, int *chno) {
    uint8_t hdr[16];
    while (s && fseek(s->f, s->off, SEEK_SET) == 0 && fread(hdr, 1, 16, s->f) == 16) {
        uint32_t csize = be32(hdr + 4);
        if (csize < 8) return NULL;
        long here = s->off;
        s->off += 8 + (long)csize;
        uint32_t poff = hdr[9], pad = be16(hdr + 10);
        int ch = hdr[12], dtype = hdr[15] & 3;
        if (memcmp(hdr, s->sig, 4) || dtype != 0 || (s->chno >= 0 && ch != s->chno)) continue;
        if (poff > csize || pad > csize - poff) continue;
        size_t n = csize - poff - pad;
        if (n > s->cap) {
            uint8_t *nb = realloc(s->buf, n);
            if (!nb) return NULL;
            s->buf = nb;
            s->cap = n;
        }
        if (fseek(s->f, here + 8 + (long)poff, SEEK_SET) != 0 || fread(s->buf, 1, n, s->f) != n) return NULL;
        if (len) *len = n;
        if (chno) *chno = ch;
        return s->buf;
    }
    return NULL;
}

void usm_stream_close(UsmStream *s) {
    if (!s) return;
    fclose(s->f);
    free(s->buf);
    free(s);
}

bool usm_video_info(const char *path, double *fps_out, int *dcprec_out) {
    *fps_out = 0;
    *dcprec_out = 8;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t hdr[16];
    long off = 0;
    double fps = 0;
    while (fps == 0 && fseek(f, off, SEEK_SET) == 0 && fread(hdr, 1, 16, f) == 16) {
        uint32_t csize = be32(hdr + 4);
        if (csize < 8) break;
        if (!memcmp(hdr, "@SFV", 4) && (hdr[15] & 3) == 0) break;   /* headers precede stream data */
        if (!memcmp(hdr, "@SFV", 4) && (hdr[15] & 3) == 1) {
            uint8_t *chunk = malloc(csize);
            if (chunk && fseek(f, off + 8, SEEK_SET) == 0 && fread(chunk, 1, csize, f) == csize) {
                uint32_t poff = chunk[1], pad = be16(chunk + 2);
                Utf u;
                if (poff < csize && pad <= csize - poff && utf_open(&u, chunk + poff, csize - poff - pad) &&
                    !strcmp(u.name, "VIDEO_HDRINFO")) {
                    uint64_t n = uint_of(&u, 0, ucol(&u, "framerate_n")), d = uint_of(&u, 0, ucol(&u, "framerate_d"));
                    if (n && d) fps = (double)n / (double)d;
                    uint64_t p = uint_of(&u, 0, ucol(&u, "mpeg_dcprec"));
                    if (p >= 8 && p <= 11) *dcprec_out = (int)p;
                }
            }
            free(chunk);
        }
        off += 8 + (long)csize;
    }
    fclose(f);
    *fps_out = fps;
    return fps > 0;
}
