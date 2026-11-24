#include "mf.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)n + 1);
    if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
    fclose(f);
    b[n] = 0;
    if (len) *len = (size_t)n;
    return b;
}

void log_msg(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs("[mikuflick] ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

int utf8_decode(const char *s, uint32_t *cp) {
    const unsigned char *u = (const unsigned char *)s;
    if (!u[0]) return 0;
    if (u[0] < 0x80) { *cp = u[0]; return 1; }
    if ((u[0] & 0xe0) == 0xc0 && u[1]) { *cp = (uint32_t)(u[0] & 0x1f) << 6 | (u[1] & 0x3f); return 2; }
    if ((u[0] & 0xf0) == 0xe0 && u[1] && u[2]) {
        *cp = (uint32_t)(u[0] & 0x0f) << 12 | (uint32_t)(u[1] & 0x3f) << 6 | (u[2] & 0x3f);
        return 3;
    }
    if ((u[0] & 0xf8) == 0xf0 && u[1] && u[2] && u[3]) {
        *cp = (uint32_t)(u[0] & 0x07) << 18 | (uint32_t)(u[1] & 0x3f) << 12 | (uint32_t)(u[2] & 0x3f) << 6 | (u[3] & 0x3f);
        return 4;
    }
    *cp = 0xfffd;
    return 1;
}
