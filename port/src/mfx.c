/*
 * mfx - game data tables from the user's MikuFlick2 binary -> gamedata.txt.
 * All addresses are VM addresses documented in docs/re_notes.md. Formerly the
 * body of tools/mfextract; now shared with the in-game IPA importer, so errors
 * are returned instead of exiting.
 */
#include "mfx.h"

#include <setjmp.h>
#include <stdarg.h>
#include <string.h>

#define VM_BASE 0x4000u /* __TEXT vmaddr - fileoff; __DATA keeps the same delta */

/* table addresses (see docs/re_notes.md) */
#define A_SONGS         0x14094cu /* _sTblDefaultMusicPackData, 0x24 stride */
#define A_BOARD_TYPE    0x13a454u /* e_FlickBoardTypeTbl, 190 x s32 */
#define A_BOARD_RESULT  0x13a74cu /* e_FlickBoardResultTbl */
#define A_BOARD_SUB     0x139c80u /* e_FlickBoardSubTypeTbl */
#define A_CHARPICT      0x139fdeu /* e_CharPictTbl, 190 x {u16 tex,u16 x,u16 y} */
#define A_BOARDPICT     0x13c5b8u /* e_FlickBoardPictTbl, 10 x {u16 sx,sy,px,py} (px,py = top-left) */
#define A_GUIDECHAR     0x139f78u /* e_GuideCharPictIdx, 10 x s16 {centre,up,right,down,left} -> fgcharacter_ori_NN */
#define A_SCOREPOSX     0x13acd4u /* s_tblScorePosX, 7 x s16 */
#define A_RESULTOFSY    0x13ac24u /* s_tblEffectResultOfsY, 6 x s32 */
#define A_FRONT         0x139488u /* s_tblNoteNormal_Front, 11 x s32 */
#define A_BACK          0x1394b4u /* s_tblNoteNormal_Back, 12 x s32 */
#define A_ADDSCORE      0x1394e4u /* s_tblAddScore, 12 x s32 */
#define A_TENSION       0x138f4cu /* s_tblTensionGauge, 6 x s32 used */
#define A_ENDOFS        0x1392fcu /* s_tblDifficultEndTimeOfs, 5 x s32 */
#define A_SOUNDFILE     0x1618a8u /* _s_szSoundFile, 30 x CFString* */
#define A_SOUNDLOOP     0x161920u /* _s_bSoundLoop, 30 x u8 */
#define NUM_KANA 190
#define NUM_SOUNDS 30

static const uint8_t k_uuid[16] = {0x96,0xB3,0x78,0xF9,0x55,0x1D,0x39,0xDD,0xA2,0x56,0x14,0x00,0xE6,0x8E,0x03,0x9C};

typedef struct {
    const uint8_t *buf;
    size_t len;
    FILE *o;
    jmp_buf fail;
    char *err;
    size_t errlen;
} Ctx;

static void die(Ctx *c, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (c->err && c->errlen) vsnprintf(c->err, c->errlen, fmt, ap);
    va_end(ap);
    longjmp(c->fail, 1);
}

static size_t off_of(Ctx *c, uint32_t vm, size_t n) {
    if (vm < VM_BASE) die(c, "damaged game binary (address below image base)");
    size_t o = vm - VM_BASE;
    if (o + n > c->len) die(c, "damaged game binary (address beyond end of file)");
    return o;
}
static uint32_t u32(Ctx *c, uint32_t vm) { uint32_t v; memcpy(&v, c->buf + off_of(c, vm, 4), 4); return v; }
static int32_t s32(Ctx *c, uint32_t vm) { return (int32_t)u32(c, vm); }
static uint16_t u16(Ctx *c, uint32_t vm) { uint16_t v; memcpy(&v, c->buf + off_of(c, vm, 2), 2); return v; }
static uint8_t u8(Ctx *c, uint32_t vm) { return c->buf[off_of(c, vm, 1)]; }

static void utf8_put(FILE *o, uint32_t cp) {
    if (cp < 0x80) fputc((int)cp, o);
    else if (cp < 0x800) { fputc(0xc0 | cp >> 6, o); fputc(0x80 | (cp & 0x3f), o); }
    else if (cp < 0x10000) { fputc(0xe0 | cp >> 12, o); fputc(0x80 | ((cp >> 6) & 0x3f), o); fputc(0x80 | (cp & 0x3f), o); }
    else { fputc(0xf0 | cp >> 18, o); fputc(0x80 | ((cp >> 12) & 0x3f), o); fputc(0x80 | ((cp >> 6) & 0x3f), o); fputc(0x80 | (cp & 0x3f), o); }
}

/* Decode a constant CFString {isa, flags, ptr, len}. Returns 0 if it does not look like one. */
static int cfstring_x(Ctx *c, uint32_t vm, int nospace) {
    if (vm < VM_BASE || vm - VM_BASE + 16 > c->len) return 0;
    uint32_t flags = u32(c, vm + 4), p = u32(c, vm + 8), n = u32(c, vm + 12);
    if (n == 0 || n > 512) return 0;
    if (flags == 0x7d0) { /* UTF-16LE */
        for (uint32_t i = 0; i < n; i++) {
            uint32_t ch = u16(c, p + i * 2);
            if (ch >= 0xd800 && ch < 0xdc00 && i + 1 < n) {
                uint32_t lo = u16(c, p + (i + 1) * 2);
                ch = 0x10000 + ((ch - 0xd800) << 10) + (lo - 0xdc00);
                i++;
            }
            utf8_put(c->o, ch);
        }
        return 1;
    }
    if (flags == 0x7c8) { /* ASCII */
        for (uint32_t i = 0; i < n; i++) {
            uint8_t ch = u8(c, p + i);
            fputc(nospace && ch == ' ' ? '_' : ch, c->o);
        }
        return 1;
    }
    return 0;
}

/* s_tbl_Ending @0x1413b8: the WindowEnding credits roll, 76 rows of {CFString *text, s32 font size,
   s32 gap in px to the next line} (WindowEnding init loop @0x38a56-0x38b36, Render @0x38e6c).
   Optional: written only when every row decodes; without it the port hides the ENDING button.
   Line: "ending <i> <size> <gap> <text>" (text to the end of the line, leading spaces kept). */
#define A_ENDING 0x1413b8u
#define N_ENDING 76
static int cfstring_ok(const Ctx *c, uint32_t vm) {
    if (vm < VM_BASE || vm - VM_BASE + 16 > c->len) return 0;
    uint32_t w[4];
    memcpy(w, c->buf + (vm - VM_BASE), 16);
    uint32_t unit = w[1] == 0x7d0 ? 2 : w[1] == 0x7c8 ? 1 : 0;
    return unit && w[3] > 0 && w[3] <= 512 && w[2] >= VM_BASE && w[2] - VM_BASE + (size_t)w[3] * unit <= c->len;
}

static void write_ending(Ctx *c) {
    for (int i = 0; i < N_ENDING; i++) {
        uint32_t row = A_ENDING + (uint32_t)i * 12;
        int32_t size = s32(c, row + 4), gap = s32(c, row + 8);
        if (!cfstring_ok(c, u32(c, row)) || size < 4 || size > 128 || gap < 0 || gap > 4000) return;
    }
    for (int i = 0; i < N_ENDING; i++) {
        uint32_t row = A_ENDING + (uint32_t)i * 12;
        fprintf(c->o, "ending %d %d %d ", i, s32(c, row + 4), s32(c, row + 8));
        cfstring_x(c, u32(c, row), 0);
        fputc('\n', c->o);
    }
}

/* find the LC_UUID payload */
static const uint8_t *find_uuid(const Ctx *c) {
    if (c->len < 28) return NULL;
    uint32_t ncmds;
    memcpy(&ncmds, c->buf + 16, 4);
    size_t off = 28;
    for (uint32_t i = 0; i < ncmds && off + 8 <= c->len; i++) {
        uint32_t cmd, cs;
        memcpy(&cmd, c->buf + off, 4);
        memcpy(&cs, c->buf + off + 4, 4);
        if (cmd == 0x1b && off + 24 <= c->len) return c->buf + off + 8;
        if (cs < 8) break;
        off += cs;
    }
    return NULL;
}

int mfx_write_gamedata(const uint8_t *bin, size_t len, FILE *out, char *err, size_t errlen) {
    Ctx ctx = {.buf = bin, .len = len, .o = out, .err = err, .errlen = errlen};
    Ctx *c = &ctx;
    if (setjmp(c->fail)) return -1;

    uint32_t magic = 0;
    if (len >= 4) memcpy(&magic, bin, 4);
    if (magic == 0xcafebabeu || magic == 0xbebafecau)
        die(c, "the game binary is a fat (multi-architecture) Mach-O; only the decrypted armv7 v1.1.5 build is supported");
    if (magic != 0xfeedfaceu) die(c, "not a 32-bit Mach-O game binary");
    const uint8_t *uuid = find_uuid(c);
    if (!uuid || memcmp(uuid, k_uuid, 16))
        die(c, "unexpected game version (UUID mismatch); only Miku Flick/02 v1.1.5 (armv7, decrypted) is supported");

    FILE *o = out;
    fprintf(o, "# mikuflick port game data, extracted from MikuFlick2 v1.1.5\n");
    fprintf(o, "version 1\n");

    /* songs: title, artwork atlas, preview, movie, bpm, stars x4 */
    int nsongs = 0;
    for (int i = 0; i < 32; i++) {
        uint32_t row = A_SONGS + (uint32_t)i * 0x24;
        int32_t bpm = s32(c, row + 16);
        if (bpm < 30 || bpm > 400) break;
        fprintf(o, "song %d ", i);
        if (!cfstring_x(c, u32(c, row + 8), 1)) die(c, "damaged game binary (song preview name)");
        fputc(' ', o);
        if (!cfstring_x(c, u32(c, row + 12), 1)) die(c, "damaged game binary (song movie name)");
        fputc(' ', o);
        if (!cfstring_x(c, u32(c, row + 4), 1)) die(c, "damaged game binary (song artwork name)");
        fprintf(o, " %d %d %d %d %d ", bpm, s32(c, row + 20), s32(c, row + 24), s32(c, row + 28), s32(c, row + 32));
        if (!cfstring_x(c, u32(c, row), 0)) die(c, "damaged game binary (song title)");
        fputc('\n', o);
        nsongs++;
    }
    if (nsongs == 0) die(c, "song table not found in the game binary");

    for (int i = 0; i < NUM_KANA; i++) {
        uint32_t cp = A_CHARPICT + (uint32_t)i * 6;
        fprintf(o, "kana %d %d %d %d %u %u %u\n", i, s32(c, A_BOARD_TYPE + i * 4), s32(c, A_BOARD_RESULT + i * 4),
                s32(c, A_BOARD_SUB + i * 4), u16(c, cp), u16(c, cp + 2), u16(c, cp + 4));
    }
    for (int i = 0; i < 10; i++) {
        uint32_t b = A_BOARDPICT + (uint32_t)i * 8;
        fprintf(o, "board %d %u %u %u %u\n", i, u16(c, b), u16(c, b + 2), u16(c, b + 4), u16(c, b + 6));
    }
    for (int i = 0; i < 10; i++) {
        fprintf(o, "guide %d", i);
        for (int k = 0; k < 5; k++) fprintf(o, " %d", (int16_t)u16(c, A_GUIDECHAR + (uint32_t)(i * 5 + k) * 2));
        fputc('\n', o);
    }
    fprintf(o, "scoreposx"); for (int i = 0; i < 7; i++) fprintf(o, " %d", (int16_t)u16(c, A_SCOREPOSX + i * 2)); fputc('\n', o);
    fprintf(o, "resultofsy"); for (int i = 0; i < 6; i++) fprintf(o, " %d", s32(c, A_RESULTOFSY + i * 4)); fputc('\n', o);
    fprintf(o, "front"); for (int i = 0; i < 11; i++) fprintf(o, " %d", s32(c, A_FRONT + i * 4)); fputc('\n', o);
    fprintf(o, "back"); for (int i = 0; i < 12; i++) fprintf(o, " %d", s32(c, A_BACK + i * 4)); fputc('\n', o);
    fprintf(o, "addscore"); for (int i = 0; i < 12; i++) fprintf(o, " %d", s32(c, A_ADDSCORE + i * 4)); fputc('\n', o);
    fprintf(o, "tension"); for (int i = 0; i < 6; i++) fprintf(o, " %d", s32(c, A_TENSION + i * 4)); fputc('\n', o);
    fprintf(o, "endofs"); for (int i = 0; i < 5; i++) fprintf(o, " %d", s32(c, A_ENDOFS + i * 4)); fputc('\n', o);

    for (int i = 0; i < NUM_SOUNDS; i++) {
        fprintf(o, "sound %d ", i);
        if (!cfstring_x(c, u32(c, A_SOUNDFILE + i * 4), 1)) die(c, "damaged game binary (sound name)");
        fprintf(o, " %d\n", u8(c, A_SOUNDLOOP + i) ? 1 : 0);
    }
    write_ending(c);
    if (ferror(o)) die(c, "could not write gamedata.txt");
    return nsongs;
}
