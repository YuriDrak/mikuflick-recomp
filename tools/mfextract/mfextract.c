/*
 * mfextract - export game data tables from the user's own MikuFlick2 (v1.1.5,
 * armv7, UUID 96B378F9-551D-39DD-A256-1400E68E039C) into a plain text file the
 * Linux port reads at startup. No game data is shipped with the port itself.
 *
 * All addresses below are VM addresses documented in docs/re_notes.md.
 *
 * Build: cc -O2 -Wall -o mfextract mfextract.c
 * Usage: mfextract <path/to/MikuFlick2> <out/gamedata.txt>
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VM_BASE 0x4000u /* __TEXT vmaddr - fileoff; __DATA keeps the same delta */

/* table addresses (see docs/re_notes.md) */
#define A_SONGS         0x14094cu /* _sTblDefaultMusicPackData, 0x24 stride */
#define A_BOARD_TYPE    0x13a454u /* e_FlickBoardTypeTbl, 190 x s32 */
#define A_BOARD_RESULT  0x13a74cu /* e_FlickBoardResultTbl */
#define A_BOARD_SUB     0x139c80u /* e_FlickBoardSubTypeTbl */
#define A_CHARPICT      0x139fdeu /* e_CharPictTbl, 190 x {u16 tex,u16 x,u16 y} */
#define A_BOARDPICT     0x13c5b8u /* e_FlickBoardPictTbl, 10 x {u16 sx,sy,px,py} */
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

static uint8_t *buf;
static size_t len;

static void die(const char *m) { fprintf(stderr, "mfextract: %s\n", m); exit(1); }

static size_t off_of(uint32_t vm, size_t n) {
    if (vm < VM_BASE) die("address below image base");
    size_t o = vm - VM_BASE;
    if (o + n > len) die("address beyond end of file");
    return o;
}
static uint32_t u32(uint32_t vm) { uint32_t v; memcpy(&v, buf + off_of(vm, 4), 4); return v; }
static int32_t s32(uint32_t vm) { return (int32_t)u32(vm); }
static uint16_t u16(uint32_t vm) { uint16_t v; memcpy(&v, buf + off_of(vm, 2), 2); return v; }
static uint8_t u8(uint32_t vm) { return buf[off_of(vm, 1)]; }

static void utf8_put(FILE *o, uint32_t cp) {
    if (cp < 0x80) fputc((int)cp, o);
    else if (cp < 0x800) { fputc(0xc0 | cp >> 6, o); fputc(0x80 | (cp & 0x3f), o); }
    else if (cp < 0x10000) { fputc(0xe0 | cp >> 12, o); fputc(0x80 | ((cp >> 6) & 0x3f), o); fputc(0x80 | (cp & 0x3f), o); }
    else { fputc(0xf0 | cp >> 18, o); fputc(0x80 | ((cp >> 12) & 0x3f), o); fputc(0x80 | ((cp >> 6) & 0x3f), o); fputc(0x80 | (cp & 0x3f), o); }
}

/* Decode a constant CFString {isa, flags, ptr, len}. Returns 0 if it does not look like one. */
static int cfstring_x(FILE *o, uint32_t vm, int nospace) {
    if (vm < VM_BASE || vm - VM_BASE + 16 > len) return 0;
    uint32_t flags = u32(vm + 4), p = u32(vm + 8), n = u32(vm + 12);
    if (n == 0 || n > 512) return 0;
    if (flags == 0x7d0) { /* UTF-16LE */
        for (uint32_t i = 0; i < n; i++) {
            uint32_t c = u16(p + i * 2);
            if (c >= 0xd800 && c < 0xdc00 && i + 1 < n) {
                uint32_t lo = u16(p + (i + 1) * 2);
                c = 0x10000 + ((c - 0xd800) << 10) + (lo - 0xdc00);
                i++;
            }
            utf8_put(o, c);
        }
        return 1;
    }
    if (flags == 0x7c8) { /* ASCII */
        for (uint32_t i = 0; i < n; i++) {
            uint8_t c = u8(p + i);
            fputc(nospace && c == ' ' ? '_' : c, o);
        }
        return 1;
    }
    return 0;
}

static int cfstring(FILE *o, uint32_t vm) { return cfstring_x(o, vm, 1); }
static int cfstring_text(FILE *o, uint32_t vm) { return cfstring_x(o, vm, 0); }

/* find the LC_UUID payload */
static const uint8_t *find_uuid(void) {
    if (len < 28) return NULL;
    uint32_t ncmds; memcpy(&ncmds, buf + 16, 4);
    size_t off = 28;
    for (uint32_t i = 0; i < ncmds && off + 8 <= len; i++) {
        uint32_t cmd, cs;
        memcpy(&cmd, buf + off, 4); memcpy(&cs, buf + off + 4, 4);
        if (cmd == 0x1b && off + 24 <= len) return buf + off + 8;
        if (cs < 8) break;
        off += cs;
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <MikuFlick2> <gamedata.txt>\n", argv[0]); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); len = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    buf = malloc(len);
    if (!buf || fread(buf, 1, len, f) != len) die("read failed");
    fclose(f);

    uint32_t magic; memcpy(&magic, buf, 4);
    if (magic != 0xfeedfaceu) die("not a thin 32-bit Mach-O");
    const uint8_t *uuid = find_uuid();
    if (!uuid || memcmp(uuid, k_uuid, 16)) die("unexpected binary build (UUID mismatch); only v1.1.5 armv7 is supported");

    FILE *o = fopen(argv[2], "w");
    if (!o) { perror(argv[2]); return 1; }
    fprintf(o, "# mikuflick port game data, extracted by mfextract from MikuFlick2 v1.1.5\n");
    fprintf(o, "version 1\n");

    /* songs: title, artwork atlas, preview, movie, bpm, stars x4 */
    int nsongs = 0;
    for (int i = 0; i < 32; i++) {
        uint32_t row = A_SONGS + (uint32_t)i * 0x24;
        int32_t bpm = s32(row + 16);
        if (bpm < 30 || bpm > 400) break;
        fprintf(o, "song %d ", i);
        if (!cfstring(o, u32(row + 8))) die("bad preview string");
        fputc(' ', o);
        if (!cfstring(o, u32(row + 12))) die("bad movie string");
        fputc(' ', o);
        if (!cfstring(o, u32(row + 4))) die("bad artwork string");
        fprintf(o, " %d %d %d %d %d ", bpm, s32(row + 20), s32(row + 24), s32(row + 28), s32(row + 32));
        if (!cfstring_text(o, u32(row))) die("bad title string");
        fputc('\n', o);
        nsongs++;
    }
    if (nsongs == 0) die("song table not found");

    for (int i = 0; i < NUM_KANA; i++) {
        uint32_t cp = A_CHARPICT + (uint32_t)i * 6;
        fprintf(o, "kana %d %d %d %d %u %u %u\n", i,
                s32(A_BOARD_TYPE + i * 4), s32(A_BOARD_RESULT + i * 4), s32(A_BOARD_SUB + i * 4),
                u16(cp), u16(cp + 2), u16(cp + 4));
    }
    for (int i = 0; i < 10; i++) {
        uint32_t b = A_BOARDPICT + (uint32_t)i * 8;
        fprintf(o, "board %d %u %u %u %u\n", i, u16(b), u16(b + 2), u16(b + 4), u16(b + 6));
    }

    for (int i = 0; i < 10; i++) {
        fprintf(o, "guide %d", i);
        for (int k = 0; k < 5; k++) fprintf(o, " %d", (int16_t)u16(A_GUIDECHAR + (uint32_t)(i * 5 + k) * 2));
        fputc('\n', o);
    }
    fprintf(o, "scoreposx"); for (int i = 0; i < 7; i++) fprintf(o, " %d", (int16_t)u16(A_SCOREPOSX + i * 2)); fputc('\n', o);
    fprintf(o, "resultofsy"); for (int i = 0; i < 6; i++) fprintf(o, " %d", s32(A_RESULTOFSY + i * 4)); fputc('\n', o);
    fprintf(o, "front"); for (int i = 0; i < 11; i++) fprintf(o, " %d", s32(A_FRONT + i * 4)); fputc('\n', o);
    fprintf(o, "back"); for (int i = 0; i < 12; i++) fprintf(o, " %d", s32(A_BACK + i * 4)); fputc('\n', o);
    fprintf(o, "addscore"); for (int i = 0; i < 12; i++) fprintf(o, " %d", s32(A_ADDSCORE + i * 4)); fputc('\n', o);
    fprintf(o, "tension"); for (int i = 0; i < 6; i++) fprintf(o, " %d", s32(A_TENSION + i * 4)); fputc('\n', o);
    fprintf(o, "endofs"); for (int i = 0; i < 5; i++) fprintf(o, " %d", s32(A_ENDOFS + i * 4)); fputc('\n', o);

    for (int i = 0; i < NUM_SOUNDS; i++) {
        fprintf(o, "sound %d ", i);
        if (!cfstring(o, u32(A_SOUNDFILE + i * 4))) die("bad sound name");
        fprintf(o, " %d\n", u8(A_SOUNDLOOP + i) ? 1 : 0);
    }
    fclose(o);
    free(buf);
    fprintf(stderr, "mfextract: wrote %d songs, %d kana, 10 keys, %d sounds to %s\n", nsongs, NUM_KANA, NUM_SOUNDS, argv[2]);
    return 0;
}
