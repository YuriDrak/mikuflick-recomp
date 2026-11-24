/* Minimal Apple binary property list (bplist00) reader -> tree of BpNode. */
#include "mf.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const uint8_t *b;
    size_t len;
    unsigned off_size, ref_size;
    uint64_t nobj, table_off;
    int depth;
} Ctx;

static uint64_t ben(const uint8_t *p, unsigned n) {
    uint64_t v = 0;
    for (unsigned i = 0; i < n; i++) v = v << 8 | p[i];
    return v;
}

static bool obj_off(const Ctx *c, uint64_t ref, size_t *out) {
    if (ref >= c->nobj) return false;
    size_t e = (size_t)(c->table_off + ref * c->off_size);
    if (e + c->off_size > c->len) return false;
    size_t o = (size_t)ben(c->b + e, c->off_size);
    if (o >= c->len) return false;
    *out = o;
    return true;
}

static bool count(const Ctx *c, size_t *p, uint64_t *n) {
    uint8_t m = c->b[*p];
    *n = m & 0x0f;
    (*p)++;
    if (*n == 0x0f) {
        if (*p >= c->len) return false;
        uint8_t im = c->b[*p];
        if ((im & 0xf0) != 0x10) return false;
        unsigned k = 1u << (im & 0x0f);
        if (*p + 1 + k > c->len) return false;
        *n = ben(c->b + *p + 1, k);
        *p += 1 + k;
    }
    return true;
}

static void put8(char *o, size_t *k, uint32_t cp) {
    if (cp < 0x80) o[(*k)++] = (char)cp;
    else if (cp < 0x800) { o[(*k)++] = (char)(0xc0 | cp >> 6); o[(*k)++] = (char)(0x80 | (cp & 0x3f)); }
    else if (cp < 0x10000) {
        o[(*k)++] = (char)(0xe0 | cp >> 12); o[(*k)++] = (char)(0x80 | ((cp >> 6) & 0x3f));
        o[(*k)++] = (char)(0x80 | (cp & 0x3f));
    } else {
        o[(*k)++] = (char)(0xf0 | cp >> 18); o[(*k)++] = (char)(0x80 | ((cp >> 12) & 0x3f));
        o[(*k)++] = (char)(0x80 | ((cp >> 6) & 0x3f)); o[(*k)++] = (char)(0x80 | (cp & 0x3f));
    }
}

static BpNode *parse(Ctx *c, uint64_t ref) {
    size_t p;
    if (++c->depth > 64 || !obj_off(c, ref, &p)) { c->depth--; return NULL; }
    BpNode *n = calloc(1, sizeof *n);
    if (!n) { c->depth--; return NULL; }
    uint8_t m = c->b[p];
    uint64_t cnt;
    switch (m >> 4) {
    case 0x0:
        n->type = (m == 0x08 || m == 0x09) ? BP_BOOL : BP_NULL;
        n->i = (m == 0x09);
        break;
    case 0x1: {
        unsigned k = 1u << (m & 0x0f);
        if (p + 1 + k > c->len) goto fail;
        n->type = BP_INT;
        n->i = (int64_t)ben(c->b + p + 1, k);
        break;
    }
    case 0x2: {
        unsigned k = 1u << (m & 0x0f);
        if (p + 1 + k > c->len) goto fail;
        uint64_t v = ben(c->b + p + 1, k);
        n->type = BP_REAL;
        if (k == 4) { uint32_t x = (uint32_t)v; float f; memcpy(&f, &x, 4); n->r = f; }
        else { double d; memcpy(&d, &v, 8); n->r = d; }
        break;
    }
    case 0x4:
        if (!count(c, &p, &cnt) || p + cnt > c->len) goto fail;
        n->type = BP_DATA;
        n->s = malloc(cnt + 1);
        if (!n->s) goto fail;
        memcpy(n->s, c->b + p, cnt);
        n->s[cnt] = 0;
        n->n = cnt;
        break;
    case 0x5:
        if (!count(c, &p, &cnt) || p + cnt > c->len) goto fail;
        n->type = BP_STR;
        n->s = malloc(cnt * 2 + 1);
        if (!n->s) goto fail;
        { size_t k = 0; for (uint64_t i = 0; i < cnt; i++) put8(n->s, &k, c->b[p + i]); n->s[k] = 0; n->n = k; }
        break;
    case 0x6:
        if (!count(c, &p, &cnt) || p + cnt * 2 > c->len) goto fail;
        n->type = BP_STR;
        n->s = malloc(cnt * 4 + 1);
        if (!n->s) goto fail;
        {
            size_t k = 0;
            for (uint64_t i = 0; i < cnt; i++) {
                uint32_t u = (uint32_t)ben(c->b + p + i * 2, 2);
                if (u >= 0xd800 && u < 0xdc00 && i + 1 < cnt) {
                    uint32_t lo = (uint32_t)ben(c->b + p + (i + 1) * 2, 2);
                    if (lo >= 0xdc00 && lo < 0xe000) { u = 0x10000 + ((u - 0xd800) << 10) + (lo - 0xdc00); i++; }
                }
                put8(n->s, &k, u);
            }
            n->s[k] = 0;
            n->n = k;
        }
        break;
    case 0xA:
        if (!count(c, &p, &cnt) || p + cnt * c->ref_size > c->len) goto fail;
        n->type = BP_ARRAY;
        n->items = calloc(cnt ? cnt : 1, sizeof *n->items);
        if (!n->items) goto fail;
        n->n = cnt;
        for (uint64_t i = 0; i < cnt; i++) n->items[i] = parse(c, ben(c->b + p + i * c->ref_size, c->ref_size));
        break;
    case 0xD:
        if (!count(c, &p, &cnt) || p + cnt * 2 * c->ref_size > c->len) goto fail;
        n->type = BP_DICT;
        n->items = calloc(cnt ? cnt : 1, sizeof *n->items);
        n->keys = calloc(cnt ? cnt : 1, sizeof *n->keys);
        if (!n->items || !n->keys) goto fail;
        n->n = cnt;
        for (uint64_t i = 0; i < cnt; i++) {
            BpNode *k = parse(c, ben(c->b + p + i * c->ref_size, c->ref_size));
            if (k && k->type == BP_STR) { n->keys[i] = k->s; k->s = NULL; }
            else n->keys[i] = strdup("");
            bp_free(k);
            n->items[i] = parse(c, ben(c->b + p + (cnt + i) * c->ref_size, c->ref_size));
        }
        break;
    default:
        n->type = BP_NULL;
    }
    c->depth--;
    return n;
fail:
    c->depth--;
    bp_free(n);
    return NULL;
}

BpNode *bp_parse(const uint8_t *data, size_t len) {
    if (len < 40 || memcmp(data, "bplist00", 8)) return NULL;
    const uint8_t *t = data + len - 32;
    Ctx c = {data, len, t[6], t[7], ben(t + 8, 8), ben(t + 24, 8), 0};
    if (!c.off_size || c.off_size > 8 || !c.ref_size || c.ref_size > 8 || c.table_off >= len) return NULL;
    return parse(&c, ben(t + 16, 8));
}

void bp_free(BpNode *n) {
    if (!n) return;
    if (n->type == BP_ARRAY || n->type == BP_DICT) {
        for (size_t i = 0; i < n->n; i++) {
            bp_free(n->items[i]);
            if (n->keys) free(n->keys[i]);
        }
    }
    free(n->items);
    free(n->keys);
    free(n->s);
    free(n);
}

BpNode *bp_get(const BpNode *d, const char *key) {
    if (!d || d->type != BP_DICT) return NULL;
    for (size_t i = 0; i < d->n; i++)
        if (d->keys[i] && !strcmp(d->keys[i], key)) return d->items[i];
    return NULL;
}

double bp_num(const BpNode *n) {
    if (!n) return 0;
    if (n->type == BP_INT || n->type == BP_BOOL) return (double)n->i;
    if (n->type == BP_REAL) return n->r;
    if (n->type == BP_STR && n->s) return atof(n->s);
    return 0;
}
