/*
 * bplist2json - convert an Apple binary property list (bplist00) to JSON.
 * Also passes XML/text plists through unchanged (prints a note on stderr).
 *
 * Format: "bplist00" | objects | offset table | 32-byte trailer
 *   trailer: 6 pad, u8 offset_size, u8 ref_size, u64 nobjects, u64 top, u64 table_offset (BE)
 *   object marker: hi nibble type, lo nibble size (0xF => following int object holds size)
 *     0x00 null 0x08 false 0x09 true | 0x1n int (2^n bytes) | 0x2n real | 0x33 date
 *     0x4n data | 0x5n ascii | 0x6n utf16-be | 0x8n uid | 0xAn array | 0xDn dict
 *
 * Build: cc -O2 -Wall -o bplist2json bplist2json.c
 * Usage: bplist2json file.plist > file.json
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *buf;
static size_t len;
static unsigned off_size, ref_size;
static uint64_t nobj, top, table_off;
static int depth_guard;

static uint64_t ben(const uint8_t *p, unsigned n) {
    uint64_t v = 0;
    for (unsigned i = 0; i < n; i++) v = v << 8 | p[i];
    return v;
}

static void die(const char *m) { fprintf(stderr, "bplist2json: %s\n", m); exit(1); }

static size_t obj_off(uint64_t ref) {
    if (ref >= nobj) die("object ref out of range");
    size_t e = (size_t)(table_off + ref * off_size);
    if (e + off_size > len) die("offset table out of range");
    size_t o = (size_t)ben(buf + e, off_size);
    if (o >= len) die("object offset out of range");
    return o;
}

/* returns count and advances *p past the (possibly extended) length */
static uint64_t obj_count(size_t *p) {
    uint8_t m = buf[*p];
    uint64_t c = m & 0x0f;
    (*p)++;
    if (c == 0x0f) {
        uint8_t im = buf[*p];
        if ((im & 0xf0) != 0x10) die("bad extended count");
        unsigned n = 1u << (im & 0x0f);
        if (*p + 1 + n > len) die("count past EOF");
        c = ben(buf + *p + 1, n);
        *p += 1 + n;
    }
    return c;
}

static void put_utf8(uint32_t cp) {
    if (cp == '"' || cp == '\\') { putchar('\\'); putchar((int)cp); return; }
    if (cp < 0x20) { printf("\\u%04x", cp); return; }
    if (cp < 0x80) putchar((int)cp);
    else if (cp < 0x800) { putchar(0xc0 | cp >> 6); putchar(0x80 | (cp & 0x3f)); }
    else if (cp < 0x10000) { putchar(0xe0 | cp >> 12); putchar(0x80 | ((cp >> 6) & 0x3f)); putchar(0x80 | (cp & 0x3f)); }
    else { putchar(0xf0 | cp >> 18); putchar(0x80 | ((cp >> 12) & 0x3f)); putchar(0x80 | ((cp >> 6) & 0x3f)); putchar(0x80 | (cp & 0x3f)); }
}

static void indent(int d) { putchar('\n'); for (int i = 0; i < d; i++) fputs("  ", stdout); }

static void emit(uint64_t ref, int d) {
    if (++depth_guard > 512) die("nesting too deep (cycle?)");
    size_t p = obj_off(ref);
    uint8_t m = buf[p];
    switch (m >> 4) {
    case 0x0:
        fputs(m == 0x08 ? "false" : m == 0x09 ? "true" : "null", stdout);
        break;
    case 0x1: {
        unsigned n = 1u << (m & 0x0f);
        if (p + 1 + n > len) die("int past EOF");
        uint64_t v = ben(buf + p + 1, n);
        if (n == 8) printf("%" PRId64, (int64_t)v); else printf("%" PRIu64, v);
        break;
    }
    case 0x2: {
        unsigned n = 1u << (m & 0x0f);
        uint64_t v = ben(buf + p + 1, n);
        if (n == 4) { uint32_t x = (uint32_t)v; float f; memcpy(&f, &x, 4); printf("%.9g", f); }
        else { double f; memcpy(&f, &v, 8); printf("%.17g", f); }
        break;
    }
    case 0x3: {
        uint64_t v = ben(buf + p + 1, 8); double f; memcpy(&f, &v, 8);
        printf("{\"$date\": %.17g}", f); /* seconds since 2001-01-01 */
        break;
    }
    case 0x4: {
        uint64_t c = obj_count(&p);
        if (p + c > len) die("data past EOF");
        fputs("{\"$data\": \"", stdout);
        for (uint64_t i = 0; i < c; i++) printf("%02x", buf[p + i]);
        fputs("\"}", stdout);
        break;
    }
    case 0x5: {
        uint64_t c = obj_count(&p);
        if (p + c > len) die("string past EOF");
        putchar('"');
        for (uint64_t i = 0; i < c; i++) put_utf8(buf[p + i]);
        putchar('"');
        break;
    }
    case 0x6: {
        uint64_t c = obj_count(&p);
        if (p + c * 2 > len) die("utf16 past EOF");
        putchar('"');
        for (uint64_t i = 0; i < c; i++) {
            uint32_t u = (uint32_t)ben(buf + p + i * 2, 2);
            if (u >= 0xd800 && u < 0xdc00 && i + 1 < c) {
                uint32_t lo = (uint32_t)ben(buf + p + (i + 1) * 2, 2);
                if (lo >= 0xdc00 && lo < 0xe000) { u = 0x10000 + ((u - 0xd800) << 10) + (lo - 0xdc00); i++; }
            }
            put_utf8(u);
        }
        putchar('"');
        break;
    }
    case 0x8: {
        unsigned n = (m & 0x0f) + 1;
        printf("{\"$uid\": %" PRIu64 "}", ben(buf + p + 1, n));
        break;
    }
    case 0xA: case 0xC: {
        uint64_t c = obj_count(&p);
        if (p + c * ref_size > len) die("array past EOF");
        putchar('[');
        for (uint64_t i = 0; i < c; i++) {
            if (i) putchar(',');
            indent(d + 1);
            emit(ben(buf + p + i * ref_size, ref_size), d + 1);
        }
        if (c) indent(d);
        putchar(']');
        break;
    }
    case 0xD: {
        uint64_t c = obj_count(&p);
        if (p + c * 2 * ref_size > len) die("dict past EOF");
        putchar('{');
        for (uint64_t i = 0; i < c; i++) {
            if (i) putchar(',');
            indent(d + 1);
            emit(ben(buf + p + i * ref_size, ref_size), d + 1);
            fputs(": ", stdout);
            emit(ben(buf + p + (c + i) * ref_size, ref_size), d + 1);
        }
        if (c) indent(d);
        putchar('}');
        break;
    }
    default:
        printf("\"<unknown marker 0x%02x>\"", m);
    }
    depth_guard--;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s file.plist\n", argv[0]); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); len = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    buf = malloc(len + 1);
    if (!buf || fread(buf, 1, len, f) != len) die("read failed");
    fclose(f);
    if (len < 40 || memcmp(buf, "bplist00", 8)) {
        fprintf(stderr, "bplist2json: %s is not bplist00, copying through\n", argv[1]);
        fwrite(buf, 1, len, stdout);
        return 0;
    }
    const uint8_t *t = buf + len - 32;
    off_size = t[6]; ref_size = t[7];
    nobj = ben(t + 8, 8); top = ben(t + 16, 8); table_off = ben(t + 24, 8);
    if (!off_size || off_size > 8 || !ref_size || ref_size > 8 || table_off >= len) die("bad trailer");
    emit(top, 0);
    putchar('\n');
    free(buf);
    return 0;
}
