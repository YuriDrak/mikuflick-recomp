/*
 * usmdump - CRI USM (Sofdec2) container walker and @UTF table dumper.
 *
 * USM chunk layout (all big-endian):
 *   +0  char  sig[4]        "CRID" "@SFV" "@SFA" "@CUE" "@SBT" "@ALP"
 *   +4  u32   size          bytes following this field
 *   +8  u8    unk
 *   +9  u8    payload_off   payload starts at chunk+8+payload_off
 *   +10 u16   padding       trailing pad bytes
 *   +12 u8    chno          stream channel
 *   +13 u8    unk, +14 u8 unk
 *   +15 u8    data_type     0 = stream data, 1 = header, 2 = section end/comment, 3 = seek/metadata
 *   +16 u32   frame_time, +20 u32 frame_rate
 *
 * @UTF table layout (big-endian, offsets relative to byte 8 of the table):
 *   +0 "@UTF"  +4 u32 table_size  +8 u16 version  +10 u16 rows_off
 *   +12 u32 strings_off  +16 u32 data_off  +20 u32 name_stroff
 *   +24 u16 ncols  +26 u16 row_width  +28 u32 nrows  +32 column descriptors
 *   column: u8 flags (hi nibble storage: 0x10 zero, 0x30 const, 0x50 per-row;
 *           lo nibble type), u32 name_stroff, [const value]
 *
 * Usage:
 *   usmdump [-a] file.usm        chunk statistics + every @UTF table (-a: all rows)
 *   usmdump -c file.usm          cue points as TSV: time, name, cue_type, parameter
 *
 * Build: cc -O2 -Wall -o usmdump usmdump.c
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint64_t be64(const uint8_t *p) { return (uint64_t)be32(p) << 32 | be32(p + 4); }

/* ---------------- @UTF ---------------- */

enum { T_U8, T_S8, T_U16, T_S16, T_U32, T_S32, T_U64, T_S64, T_F32, T_F64, T_STR, T_DATA };

static int type_size(int t) {
    switch (t) {
    case T_U8: case T_S8: return 1;
    case T_U16: case T_S16: return 2;
    case T_U32: case T_S32: case T_F32: case T_STR: return 4;
    case T_U64: case T_S64: case T_F64: case T_DATA: return 8;
    default: return -1;
    }
}

typedef struct {
    int type;          /* T_* */
    int kind;          /* 0 = string, 1 = signed int, 2 = unsigned int, 3 = float, 4 = data */
    int64_t i;
    uint64_t u;
    double f;
    const char *s;
    uint32_t doff, dlen;
} Val;

typedef struct {
    const uint8_t *base;   /* table start ("@UTF") */
    uint32_t size;         /* table_size + 8 */
    uint32_t rows_off, str_off, data_off;
    uint16_t ncols, row_width;
    uint32_t nrows;
    const char *name;
    struct Col { uint8_t flags; const char *name; Val cval; } *cols;
} Utf;

static const char *utf_str(const Utf *u, uint32_t off) {
    uint32_t p = u->str_off + off;
    if (p >= u->size) return "<bad-str>";
    /* ensure termination inside table */
    if (!memchr(u->base + p, 0, u->size - p)) return "<unterminated>";
    return (const char *)u->base + p;
}

/* read one value of type t at p; returns bytes consumed or -1 */
static int utf_read(const Utf *u, const uint8_t *p, const uint8_t *end, int t, Val *v) {
    int n = type_size(t);
    if (n < 0 || p + n > end) return -1;
    memset(v, 0, sizeof *v);
    v->type = t;
    switch (t) {
    case T_U8: v->kind = 2; v->u = p[0]; break;
    case T_S8: v->kind = 1; v->i = (int8_t)p[0]; break;
    case T_U16: v->kind = 2; v->u = be16(p); break;
    case T_S16: v->kind = 1; v->i = (int16_t)be16(p); break;
    case T_U32: v->kind = 2; v->u = be32(p); break;
    case T_S32: v->kind = 1; v->i = (int32_t)be32(p); break;
    case T_U64: v->kind = 2; v->u = be64(p); break;
    case T_S64: v->kind = 1; v->i = (int64_t)be64(p); break;
    case T_F32: { uint32_t x = be32(p); float f; memcpy(&f, &x, 4); v->kind = 3; v->f = f; break; }
    case T_F64: { uint64_t x = be64(p); double d; memcpy(&d, &x, 8); v->kind = 3; v->f = d; break; }
    case T_STR: v->kind = 0; v->s = utf_str(u, be32(p)); break;
    case T_DATA: v->kind = 4; v->doff = be32(p); v->dlen = be32(p + 4); break;
    }
    return n;
}

static int utf_open(Utf *u, const uint8_t *p, size_t avail) {
    memset(u, 0, sizeof *u);
    if (avail < 32 || memcmp(p, "@UTF", 4)) return -1;
    uint32_t tsz = be32(p + 4) + 8;
    if (tsz > avail) return -1;
    u->base = p; u->size = tsz;
    u->rows_off = be16(p + 10) + 8u;
    u->str_off = be32(p + 12) + 8u;
    u->data_off = be32(p + 16) + 8u;
    if (u->rows_off > tsz || u->str_off > tsz || u->data_off > tsz) return -1;
    u->name = utf_str(u, be32(p + 20));
    u->ncols = be16(p + 24);
    u->row_width = be16(p + 26);
    u->nrows = be32(p + 28);
    u->cols = calloc(u->ncols ? u->ncols : 1, sizeof *u->cols);
    const uint8_t *c = p + 32, *end = p + u->rows_off;
    for (int i = 0; i < u->ncols; i++) {
        if (c + 5 > end) return -1;
        u->cols[i].flags = c[0];
        u->cols[i].name = (c[0] & 0x10) ? utf_str(u, be32(c + 1)) : "";
        c += 5;
        if ((c[-5] & 0xf0) == 0x30 || (c[-5] & 0xf0) == 0x70) {
            int n = utf_read(u, c, end, c[-5] & 0x0f, &u->cols[i].cval);
            if (n < 0) return -1;
            c += n;
        }
    }
    return 0;
}

static void utf_close(Utf *u) { free(u->cols); u->cols = NULL; }

/* get value of column col for row r; returns 0 on success */
static int utf_get(const Utf *u, uint32_t r, int col, Val *v) {
    const uint8_t *p = u->base + u->rows_off + (size_t)r * u->row_width;
    const uint8_t *end = u->base + u->size;
    for (int i = 0; i < u->ncols; i++) {
        int st = u->cols[i].flags & 0xf0, t = u->cols[i].flags & 0x0f;
        if (st == 0x50) {
            if (i == col) return utf_read(u, p, end, t, v) < 0 ? -1 : 0;
            int n = type_size(t);
            if (n < 0) return -1;
            p += n;
        } else if (i == col) {
            if (st == 0x30 || st == 0x70) { *v = u->cols[i].cval; return 0; }
            memset(v, 0, sizeof *v); v->type = t; v->kind = (t == T_STR) ? 0 : 2; v->s = "";
            return 0;
        }
    }
    return -1;
}

static int utf_col(const Utf *u, const char *name) {
    for (int i = 0; i < u->ncols; i++) if (!strcmp(u->cols[i].name, name)) return i;
    return -1;
}

static void print_val(FILE *o, const Utf *u, const Val *v) {
    switch (v->kind) {
    case 0: fputs(v->s ? v->s : "", o); break;
    case 1: fprintf(o, "%" PRId64, v->i); break;
    case 2: fprintf(o, "%" PRIu64, v->u); break;
    case 3: fprintf(o, "%g", v->f); break;
    case 4:
        if (v->dlen >= 4 && u->data_off + v->doff + 4 <= u->size &&
            !memcmp(u->base + u->data_off + v->doff, "@UTF", 4))
            fprintf(o, "<@UTF %u bytes>", v->dlen);
        else if (u->data_off + v->doff + v->dlen <= u->size) {
            /* cue names/parameters are stored as raw text blobs: print them if printable */
            const uint8_t *d = u->base + u->data_off + v->doff;
            uint32_t n = v->dlen, k;
            while (n && d[n - 1] == 0) n--;
            for (k = 0; k < n; k++) if ((d[k] < 0x20 && d[k] != '\t') || d[k] == 0x7f) break;
            if (k == n) fwrite(d, 1, n, o);
            else {
                fprintf(o, "<hex:");
                for (k = 0; k < v->dlen && k < 64; k++) fprintf(o, "%02x", d[k]);
                fprintf(o, v->dlen > 64 ? "...>" : ">");
            }
        } else
            fprintf(o, "<data %u bytes, out of range>", v->dlen);
        break;
    }
}

static void utf_dump(FILE *o, const Utf *u, int allrows, int depth) {
    fprintf(o, "%*s@UTF table '%s': %u cols, %u rows, row_width %u\n", depth * 2, "",
            u->name, u->ncols, u->nrows, u->row_width);
    static const char *tn[] = {"u8","s8","u16","s16","u32","s32","u64","s64","f32","f64","str","data","?","?","?","?"};
    for (int i = 0; i < u->ncols; i++) {
        int st = u->cols[i].flags & 0xf0;
        fprintf(o, "%*s  col %-2d %-22s %-4s %s", depth * 2, "", i, u->cols[i].name,
                tn[u->cols[i].flags & 0x0f],
                st == 0x50 ? "per-row" : st == 0x30 || st == 0x70 ? "const=" : st == 0x10 ? "zero" : "?");
        if (st == 0x30 || st == 0x70) print_val(o, u, &u->cols[i].cval);
        fputc('\n', o);
    }
    uint32_t lim = allrows ? u->nrows : (u->nrows < 8 ? u->nrows : 8);
    for (uint32_t r = 0; r < lim; r++) {
        fprintf(o, "%*s  [%u]", depth * 2, "", r);
        for (int i = 0; i < u->ncols; i++) {
            Val v;
            if ((u->cols[i].flags & 0xf0) != 0x50) continue;
            if (utf_get(u, r, i, &v)) { fputs(" <err>", o); break; }
            fprintf(o, " %s=", u->cols[i].name);
            print_val(o, u, &v);
        }
        fputc('\n', o);
        /* recurse into nested @UTF blobs */
        for (int i = 0; i < u->ncols; i++) {
            Val v;
            if ((u->cols[i].flags & 0x0f) != T_DATA || utf_get(u, r, i, &v) || v.dlen < 32) continue;
            uint32_t at = u->data_off + v.doff;
            if (at + v.dlen > u->size) continue;
            Utf sub;
            if (utf_open(&sub, u->base + at, v.dlen) == 0) utf_dump(o, &sub, allrows, depth + 2);
            utf_close(&sub);
        }
    }
    if (lim < u->nrows) fprintf(o, "%*s  ... %u more rows (use -a)\n", depth * 2, "", u->nrows - lim);
}

/* ---------------- USM walk ---------------- */

typedef struct { char sig[5]; int chno, dtype; uint64_t count, bytes; } Stat;

static uint8_t *load(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)n);
    if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { perror("read"); fclose(f); free(b); return NULL; }
    fclose(f);
    *len = (size_t)n;
    return b;
}

static void emit_cues(const Utf *u, uint32_t time_unit) {
    int cn = utf_col(u, "name"), ct = utf_col(u, "time"), cy = utf_col(u, "cue_type"), cp = utf_col(u, "parameter");
    for (uint32_t r = 0; r < u->nrows; r++) {
        Val v;
        if (ct >= 0 && !utf_get(u, r, ct, &v)) {
            uint64_t t = v.kind == 1 ? (uint64_t)v.i : v.u;
            printf("%" PRIu64, t);
            if (time_unit) printf("\t%.3f", (double)t / time_unit);
            else printf("\t-");
        } else printf("-\t-");
        int cols[3] = {cn, cy, cp};
        for (int k = 0; k < 3; k++) {
            putchar('\t');
            if (cols[k] >= 0 && !utf_get(u, r, cols[k], &v)) print_val(stdout, u, &v);
        }
        putchar('\n');
    }
}

int main(int argc, char **argv) {
    int allrows = 0, cuemode = 0;
    int ai = 1;
    for (; ai < argc && argv[ai][0] == '-'; ai++) {
        if (!strcmp(argv[ai], "-a")) allrows = 1;
        else if (!strcmp(argv[ai], "-c")) cuemode = 1;
        else { fprintf(stderr, "unknown option %s\n", argv[ai]); return 1; }
    }
    if (ai >= argc) { fprintf(stderr, "usage: %s [-a|-c] file.usm\n", argv[0]); return 1; }

    size_t len;
    uint8_t *b = load(argv[ai], &len);
    if (!b) return 1;

    Stat st[64]; int nst = 0;
    uint32_t time_unit = 0;
    size_t off = 0;
    if (cuemode) printf("# %s\n# time_raw\ttime_sec\tname\tcue_type\tparameter\n", argv[ai]);

    while (off + 24 <= len) {
        const uint8_t *c = b + off;
        uint32_t csize = be32(c + 4);
        if (off + 8 + (size_t)csize > len || csize < 16) {
            fprintf(stderr, "truncated/garbled chunk at 0x%zx\n", off);
            break;
        }
        uint8_t poff = c[9];
        uint16_t pad = be16(c + 10);
        int chno = c[12], dtype = c[15] & 3;
        size_t pstart = off + 8 + poff;
        size_t pend = off + 8 + csize - pad;
        if (pstart > pend) pend = pstart;

        int k;
        for (k = 0; k < nst; k++)
            if (!memcmp(st[k].sig, c, 4) && st[k].chno == chno && st[k].dtype == dtype) break;
        if (k == nst && nst < 64) {
            memcpy(st[nst].sig, c, 4); st[nst].sig[4] = 0;
            st[nst].chno = chno; st[nst].dtype = dtype; st[nst].count = st[nst].bytes = 0;
            nst++;
        }
        if (k < 64) { st[k].count++; st[k].bytes += pend - pstart; }

        if (pend - pstart >= 32 && !memcmp(b + pstart, "@UTF", 4)) {
            Utf u;
            if (utf_open(&u, b + pstart, pend - pstart) == 0) {
                if (cuemode) {
                    if (!strcmp(u.name, "CUEPOINT_HDRINFO")) {
                        int c2 = utf_col(&u, "time_unit");
                        Val v;
                        if (c2 >= 0 && !utf_get(&u, 0, c2, &v)) time_unit = (uint32_t)(v.kind == 1 ? (uint64_t)v.i : v.u);
                        printf("# time_unit=%u\n", time_unit);
                    } else if (!strcmp(u.name, "CUEPOINT_INFO")) {
                        emit_cues(&u, time_unit);
                    }
                } else {
                    printf("\n-- chunk %.4s @0x%zx ch%d type%d payload %zu\n", (const char *)c, off, chno, dtype, pend - pstart);
                    utf_dump(stdout, &u, allrows, 1);
                }
            } else if (!cuemode) {
                printf("\n-- chunk %.4s @0x%zx: bad @UTF\n", (const char *)c, off);
            }
            utf_close(&u);
        }
        off += 8 + (size_t)csize;
    }

    if (!cuemode) {
        printf("\n== chunk statistics (sig, channel, data_type, count, payload bytes)\n");
        for (int k = 0; k < nst; k++)
            printf("  %s ch%-2d type%d  %8" PRIu64 "  %12" PRIu64 "\n", st[k].sig, st[k].chno, st[k].dtype, st[k].count, st[k].bytes);
        printf("  parsed up to 0x%zx of 0x%zx\n", off, len);
    }
    free(b);
    return 0;
}
