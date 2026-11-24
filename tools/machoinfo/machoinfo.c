/*
 * machoinfo - minimal 32-bit Mach-O inspector (armv7 iOS binaries).
 * Dumps header, load commands, segments/sections, LC_ENCRYPTION_INFO,
 * linked dylibs and entry point. Native replacement for otool -hlL.
 *
 * Build: cc -O2 -Wall -o machoinfo machoinfo.c
 * Usage: machoinfo <macho-file>
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MH_MAGIC 0xfeedfaceu
#define FAT_CIGAM 0xbebafecau

#define LC_SEGMENT 0x1
#define LC_SYMTAB 0x2
#define LC_UNIXTHREAD 0x5
#define LC_DYSYMTAB 0xb
#define LC_LOAD_DYLIB 0xc
#define LC_ID_DYLIB 0xd
#define LC_LOAD_DYLINKER 0xe
#define LC_LOAD_WEAK_DYLIB 0x80000018u
#define LC_UUID 0x1b
#define LC_CODE_SIGNATURE 0x1d
#define LC_ENCRYPTION_INFO 0x21
#define LC_DYLD_INFO 0x22
#define LC_DYLD_INFO_ONLY 0x80000022u
#define LC_VERSION_MIN_IPHONEOS 0x25
#define LC_FUNCTION_STARTS 0x26
#define LC_MAIN 0x80000028u
#define LC_DATA_IN_CODE 0x29
#define LC_SOURCE_VERSION 0x2a

static uint8_t *buf;
static size_t len;

static uint32_t rd32(size_t off) {
    if (off + 4 > len) { fprintf(stderr, "read past EOF at 0x%zx\n", off); exit(1); }
    uint32_t v; memcpy(&v, buf + off, 4); return v;
}

static const char *lcname(uint32_t c) {
    switch (c) {
    case LC_SEGMENT: return "LC_SEGMENT";
    case LC_SYMTAB: return "LC_SYMTAB";
    case LC_UNIXTHREAD: return "LC_UNIXTHREAD";
    case LC_DYSYMTAB: return "LC_DYSYMTAB";
    case LC_LOAD_DYLIB: return "LC_LOAD_DYLIB";
    case LC_ID_DYLIB: return "LC_ID_DYLIB";
    case LC_LOAD_DYLINKER: return "LC_LOAD_DYLINKER";
    case LC_LOAD_WEAK_DYLIB: return "LC_LOAD_WEAK_DYLIB";
    case LC_UUID: return "LC_UUID";
    case LC_CODE_SIGNATURE: return "LC_CODE_SIGNATURE";
    case LC_ENCRYPTION_INFO: return "LC_ENCRYPTION_INFO";
    case LC_DYLD_INFO: return "LC_DYLD_INFO";
    case LC_DYLD_INFO_ONLY: return "LC_DYLD_INFO_ONLY";
    case LC_VERSION_MIN_IPHONEOS: return "LC_VERSION_MIN_IPHONEOS";
    case LC_FUNCTION_STARTS: return "LC_FUNCTION_STARTS";
    case LC_MAIN: return "LC_MAIN";
    case LC_DATA_IN_CODE: return "LC_DATA_IN_CODE";
    case LC_SOURCE_VERSION: return "LC_SOURCE_VERSION";
    default: return "?";
    }
}

/* Shannon-ish quick check: fraction of zero bytes + crude entropy bucket count */
static double entropy(size_t off, size_t n) {
    if (off + n > len) n = (off < len) ? len - off : 0;
    if (!n) return 0;
    size_t hist[256] = {0};
    for (size_t i = 0; i < n; i++) hist[buf[off + i]]++;
    double h = 0;
    for (int i = 0; i < 256; i++) if (hist[i]) {
        double p = (double)hist[i] / n;
        h -= p * __builtin_log2(p);
    }
    return h;
}

/* ---- extra modes: -s (symbols), -w addr count (read words at vm address) ---- */
typedef struct { uint32_t vmaddr, vmsize, fileoff, filesize; } Seg;
static Seg segs[16];
static int nsegs;
static uint32_t symoff, nsyms, stroff, strsize;

static void scan_cmds(void) {
    uint32_t ncmds = rd32(16);
    size_t off = 28;
    for (uint32_t i = 0; i < ncmds; i++) {
        uint32_t cmd = rd32(off), cs = rd32(off + 4);
        if (cmd == LC_SEGMENT && nsegs < 16) {
            segs[nsegs].vmaddr = rd32(off + 24); segs[nsegs].vmsize = rd32(off + 28);
            segs[nsegs].fileoff = rd32(off + 32); segs[nsegs].filesize = rd32(off + 36);
            nsegs++;
        } else if (cmd == LC_SYMTAB) {
            symoff = rd32(off + 8); nsyms = rd32(off + 12); stroff = rd32(off + 16); strsize = rd32(off + 20);
        }
        off += cs;
    }
}

static long vm2off(uint32_t a) {
    for (int i = 0; i < nsegs; i++)
        if (a >= segs[i].vmaddr && a < segs[i].vmaddr + segs[i].filesize) return (long)(a - segs[i].vmaddr + segs[i].fileoff);
    return -1;
}

static const char *symname_at(uint32_t a) {
    a &= ~1u; /* Thumb bit */
    for (uint32_t i = 0; i < nsyms; i++) {
        size_t e = symoff + (size_t)i * 12;
        uint8_t type = buf[e + 4];
        if ((type & 0x0e) == 0x0e && (rd32(e + 8) & ~1u) == a) {
            uint32_t sx = rd32(e);
            if (sx < strsize) return (const char *)buf + stroff + sx;
        }
    }
    return NULL;
}

static int extra_modes(int argc, char **argv) {
    scan_cmds();
    if (!strcmp(argv[1], "-s")) {
        /* value, thumb flag, type, sect, name  (only defined N_SECT symbols) */
        for (uint32_t i = 0; i < nsyms; i++) {
            size_t e = symoff + (size_t)i * 12;
            uint32_t sx = rd32(e), val = rd32(e + 8);
            uint8_t type = buf[e + 4], sect = buf[e + 5];
            uint16_t desc = (uint16_t)(buf[e + 6] | buf[e + 7] << 8);
            if ((type & 0xe0) || (type & 0x0e) != 0x0e || sx >= strsize) continue; /* skip stabs/undef */
            printf("%08x %c %02x %2u %s\n", val, (desc & 0x0008) ? 'T' : '-', type, sect, (char *)buf + stroff + sx);
        }
        return 0;
    }
    if (!strcmp(argv[1], "-w") && argc >= 5) {
        uint32_t a = (uint32_t)strtoul(argv[3], NULL, 16);
        int n = atoi(argv[4]);
        for (int i = 0; i < n; i++, a += 4) {
            long o = vm2off(a);
            if (o < 0) { printf("%08x: unmapped\n", a); break; }
            uint32_t w = rd32((size_t)o);
            const char *s = symname_at(w);
            printf("%08x: %08x %s\n", a, w, s ? s : "");
        }
        return 0;
    }
    if (!strcmp(argv[1], "-x") && argc >= 5) { /* hexdump n bytes at vm address */
        uint32_t a = (uint32_t)strtoul(argv[3], NULL, 16);
        int n = atoi(argv[4]);
        long o = vm2off(a);
        if (o < 0) { printf("unmapped\n"); return 1; }
        for (int i = 0; i < n; i += 16) {
            printf("%08x:", a + i);
            for (int k = 0; k < 16 && i + k < n; k++) printf(" %02x", buf[o + i + k]);
            printf("  ");
            for (int k = 0; k < 16 && i + k < n; k++) { uint8_t ch = buf[o + i + k]; putchar(ch >= 0x20 && ch < 0x7f ? ch : '.'); }
            putchar('\n');
        }
        return 0;
    }
    fprintf(stderr, "usage: machoinfo <macho> | -s <macho> | -w <macho> <hexaddr> <nwords> | -x <macho> <hexaddr> <nbytes>\n");
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s [-s|-w|-x] <macho> ...\n", argv[0]); return 1; }
    const char *path = argv[1][0] == '-' ? argv[2] : argv[1];
    if (!path) { fprintf(stderr, "missing file\n"); return 1; }
    FILE *f = fopen(path, "rb");
    if (!f) { perror("open"); return 1; }
    fseek(f, 0, SEEK_END); len = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    buf = malloc(len);
    if (fread(buf, 1, len, f) != len) { perror("read"); return 1; }
    fclose(f);

    uint32_t magic = rd32(0);
    if (magic == FAT_CIGAM) { printf("FAT binary - not handled (thin it first)\n"); return 1; }
    if (magic != MH_MAGIC) { printf("not a 32-bit LE Mach-O (magic 0x%08x)\n", magic); return 1; }
    if (argv[1][0] == '-') return extra_modes(argc, argv);

    uint32_t cputype = rd32(4), cpusub = rd32(8), ftype = rd32(12);
    uint32_t ncmds = rd32(16), sizeofcmds = rd32(20), flags = rd32(24);
    printf("file size      : %zu\n", len);
    printf("cputype        : %u (12=ARM)  subtype %u (9=v7,11=v7s)\n", cputype, cpusub);
    printf("filetype       : %u  ncmds %u  sizeofcmds %u  flags 0x%08x\n", ftype, ncmds, sizeofcmds, flags);

    size_t off = 28;
    uint32_t text_vmaddr = 0, text_fileoff = 0;
    for (uint32_t i = 0; i < ncmds; i++) {
        uint32_t cmd = rd32(off), cs = rd32(off + 4);
        printf("\n[%2u] %-24s (0x%08x) size %u @0x%zx\n", i, lcname(cmd), cmd, cs, off);
        switch (cmd) {
        case LC_SEGMENT: {
            char name[17] = {0}; memcpy(name, buf + off + 8, 16);
            uint32_t vmaddr = rd32(off + 24), vmsize = rd32(off + 28);
            uint32_t fileoff = rd32(off + 32), filesize = rd32(off + 36);
            uint32_t maxprot = rd32(off + 40), initprot = rd32(off + 44);
            uint32_t nsects = rd32(off + 48);
            if (!strcmp(name, "__TEXT")) { text_vmaddr = vmaddr; text_fileoff = fileoff; }
            printf("     seg %-16s vm 0x%08x+0x%08x file 0x%08x+0x%08x prot %u/%u nsects %u\n",
                   name, vmaddr, vmsize, fileoff, filesize, initprot, maxprot, nsects);
            size_t so = off + 56;
            for (uint32_t s = 0; s < nsects; s++, so += 68) {
                char sn[17] = {0}, sg[17] = {0};
                memcpy(sn, buf + so, 16); memcpy(sg, buf + so + 16, 16);
                uint32_t addr = rd32(so + 32), size = rd32(so + 36), soff = rd32(so + 40);
                uint32_t sflags = rd32(so + 56);
                printf("       sect %-18s addr 0x%08x size 0x%08x off 0x%08x flags 0x%08x\n",
                       sn, addr, size, soff, sflags);
            }
            break;
        }
        case LC_ENCRYPTION_INFO: {
            uint32_t cryptoff = rd32(off + 8), cryptsize = rd32(off + 12), cryptid = rd32(off + 16);
            printf("     cryptoff 0x%08x cryptsize 0x%08x cryptid %u  => %s\n",
                   cryptoff, cryptsize, cryptid, cryptid ? "ENCRYPTED" : "decrypted (cryptid 0)");
            /* sample entropy of encrypted range: ARM code ~6.0-6.8 bits, AES ~7.99 */
            printf("     entropy of crypt range: %.3f bits/byte (first 64K: %.3f)\n",
                   entropy(cryptoff, cryptsize), entropy(cryptoff, 65536));
            break;
        }
        case LC_LOAD_DYLIB: case LC_LOAD_WEAK_DYLIB: case LC_ID_DYLIB: {
            uint32_t noff = rd32(off + 8);
            uint32_t cur = rd32(off + 16);
            printf("     %s (cur %u.%u.%u)\n", (char *)buf + off + noff,
                   cur >> 16, (cur >> 8) & 0xff, cur & 0xff);
            break;
        }
        case LC_LOAD_DYLINKER: printf("     %s\n", (char *)buf + off + rd32(off + 8)); break;
        case LC_UUID: {
            printf("     ");
            for (int k = 0; k < 16; k++) printf("%02X%s", buf[off + 8 + k], (k == 3 || k == 5 || k == 7 || k == 9) ? "-" : "");
            printf("\n");
            break;
        }
        case LC_SYMTAB:
            printf("     symoff 0x%x nsyms %u stroff 0x%x strsize %u\n",
                   rd32(off + 8), rd32(off + 12), rd32(off + 16), rd32(off + 20));
            break;
        case LC_DYSYMTAB:
            printf("     ilocal %u nlocal %u iext %u next %u iundef %u nundef %u nindirect %u\n",
                   rd32(off + 8), rd32(off + 12), rd32(off + 16), rd32(off + 20),
                   rd32(off + 24), rd32(off + 28), rd32(off + 60));
            break;
        case LC_CODE_SIGNATURE: case LC_FUNCTION_STARTS: case LC_DATA_IN_CODE:
            printf("     dataoff 0x%x datasize %u\n", rd32(off + 8), rd32(off + 12));
            break;
        case LC_DYLD_INFO: case LC_DYLD_INFO_ONLY:
            printf("     rebase 0x%x+%u bind 0x%x+%u weak 0x%x+%u lazy 0x%x+%u export 0x%x+%u\n",
                   rd32(off + 8), rd32(off + 12), rd32(off + 16), rd32(off + 20), rd32(off + 24),
                   rd32(off + 28), rd32(off + 32), rd32(off + 36), rd32(off + 40), rd32(off + 44));
            break;
        case LC_VERSION_MIN_IPHONEOS: {
            uint32_t v = rd32(off + 8), s = rd32(off + 12);
            printf("     min iOS %u.%u.%u  sdk %u.%u.%u\n", v >> 16, (v >> 8) & 0xff, v & 0xff,
                   s >> 16, (s >> 8) & 0xff, s & 0xff);
            break;
        }
        case LC_UNIXTHREAD: {
            /* ARM thread state: flavor, count, r0..r12, sp, lr, pc, cpsr */
            uint32_t pc = rd32(off + 16 + 15 * 4);
            printf("     entry pc 0x%08x (%s)\n", pc, (pc & 1) ? "Thumb" : "ARM");
            break;
        }
        case LC_MAIN: {
            uint32_t eo = rd32(off + 8);
            printf("     entryoff 0x%x -> vm 0x%08x\n", eo, text_vmaddr + eo - text_fileoff);
            break;
        }
        }
        off += cs;
    }
    free(buf);
    return 0;
}
