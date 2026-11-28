/*
 * Self-contained media decoding replacing CRI Mana / CRI Atom / AudioToolbox
 * (and, since the Android port, FFmpeg):
 *  - media_decode_audio(): CRI ADX (stand-alone .adx or the @SFA tracks of a
 *    .usm) and Apple IMA4 (.caf) to interleaved S16 stereo @44.1 kHz
 *  - VideoStream: the @SFV MPEG-1 elementary stream of a .usm, decoded with
 *    pl_mpeg into an SDL IYUV texture and presented by media time
 * Formats: docs/re_notes.md §1 and docs/archive_layout.md.
 */
#include "mf.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pl_mpeg.h"

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static int16_t clip16(int v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

/* ---------------- PCM output ---------------- */

/* native interleaved S16 (1 or 2 channels, any rate) -> stereo @AUDIO_RATE (linear resampling) */
static bool pcm_finish(const int16_t *src, size_t frames, int ch, int rate, Pcm *out) {
    memset(out, 0, sizeof *out);
    if (!frames || ch < 1 || rate <= 0) return false;
    size_t n = rate == AUDIO_RATE ? frames : (size_t)((double)frames * AUDIO_RATE / rate);
    if (!n) return false;
    int16_t *d = malloc(n * 2 * sizeof *d);
    if (!d) return false;
    int c1 = ch > 1 ? 1 : 0;
    if (rate == AUDIO_RATE) {
        for (size_t i = 0; i < n; i++) {
            d[i * 2] = src[i * ch];
            d[i * 2 + 1] = src[i * ch + c1];
        }
    } else {
        double step = (double)rate / AUDIO_RATE;
        for (size_t i = 0; i < n; i++) {
            double pos = i * step;
            size_t a = (size_t)pos, b = a + 1 < frames ? a + 1 : a;
            double fr = pos - a;
            for (int k = 0; k < 2; k++) {
                int c = k ? c1 : 0;
                d[i * 2 + k] = clip16((int)lrint(src[a * ch + c] * (1 - fr) + src[b * ch + c] * fr));
            }
        }
    }
    out->pcm = d;
    out->frames = n;
    return true;
}

/* ---------------- CRI ADX ---------------- */

/* Standard ADX (encoding 3): 18-byte blocks per channel, 16-bit scale + 32 4-bit samples,
   second-order prediction with coefficients derived from the high-pass cutoff. */
static bool adx_decode(const uint8_t *b, size_t len, Pcm *out) {
    if (len < 0x14 || b[0] != 0x80 || b[1] != 0x00) return false;
    size_t start = (size_t)rd16(b + 2) + 4;
    int enc = b[4], bs = b[5], bits = b[6], ch = b[7];
    uint32_t rate = rd32(b + 8), total = rd32(b + 12);
    int cutoff = rd16(b + 16);
    if (enc != 3 || bits != 4 || bs < 3 || ch < 1 || ch > 2 || !rate || start >= len) {
        log_msg("adx: unsupported stream (encoding %d, %d bits, block %d, %d ch)", enc, bits, bs, ch);
        return false;
    }
    int per_block = (bs - 2) * 8 / bits;
    size_t nblocks = (len - start) / ((size_t)bs * (size_t)ch);
    size_t cap = nblocks * (size_t)per_block;
    if (total && total < cap) cap = total;
    if (!cap) return false;

    double a = M_SQRT2 - cos(2.0 * M_PI * cutoff / rate), bb = M_SQRT2 - 1.0;
    double c = (a - sqrt((a + bb) * (a - bb))) / bb;
    int c1 = (int)lrint(c * 2.0 * 4096), c2 = (int)lrint(-(c * c) * 4096);

    int16_t *pcm = malloc(cap * (size_t)ch * sizeof *pcm);
    if (!pcm) return false;
    int s1[2] = {0, 0}, s2[2] = {0, 0};
    size_t frames = 0;
    for (size_t blk = 0; blk < nblocks && frames < cap; blk++) {
        int got = per_block;
        for (int k = 0; k < ch; k++) {
            const uint8_t *p = b + start + (blk * (size_t)ch + (size_t)k) * (size_t)bs;
            int scale = rd16(p);
            if (scale & 0x8000) { got = -1; break; }   /* end-of-stream block */
            for (int i = 0; i < per_block; i++) {
                if (frames + (size_t)i >= cap) { got = i < got ? i : got; break; }
                int byte = p[2 + i / 2];
                int d = (i & 1) ? (byte & 15) : (byte >> 4);
                if (d & 8) d -= 16;
                int s0 = d * scale + ((c1 * s1[k] + c2 * s2[k]) >> 12);
                s2[k] = s1[k];
                s1[k] = clip16(s0);
                pcm[(frames + (size_t)i) * (size_t)ch + (size_t)k] = (int16_t)s1[k];
            }
        }
        if (got < 0) break;
        frames += (size_t)got;
    }
    bool ok = pcm_finish(pcm, frames, ch, (int)rate, out);
    free(pcm);
    return ok;
}

/* ---------------- Apple IMA4 in CAF ---------------- */

static const int k_ima_index[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
static const int k_ima_step[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
    5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767};

static double rd_f64(const uint8_t *p) {
    uint64_t u = (uint64_t)rd32(p) << 32 | rd32(p + 4);
    double d;
    memcpy(&d, &u, sizeof d);
    return d;
}

static bool caf_decode(const uint8_t *b, size_t len, Pcm *out) {
    if (len < 8 || memcmp(b, "caff", 4)) return false;
    double rate = 0;
    uint32_t bpp = 0, fpp = 0, ch = 0;
    bool ima4 = false;
    const uint8_t *data = NULL;
    size_t dlen = 0;
    size_t pos = 8;
    while (pos + 12 <= len) {
        const uint8_t *c = b + pos;
        uint64_t sz = (uint64_t)rd32(c + 4) << 32 | rd32(c + 8);
        size_t body = pos + 12;
        size_t avail = len - body;
        if (!memcmp(c, "desc", 4) && avail >= 32) {
            rate = rd_f64(b + body);
            ima4 = !memcmp(b + body + 8, "ima4", 4);
            bpp = rd32(b + body + 16);
            fpp = rd32(b + body + 20);
            ch = rd32(b + body + 24);
        } else if (!memcmp(c, "data", 4)) {
            size_t n = (sz == UINT64_MAX || sz > avail) ? avail : (size_t)sz;
            if (n >= 4) { data = b + body + 4; dlen = n - 4; }   /* skip the edit count */
            break;
        }
        if (sz == UINT64_MAX || sz > avail) break;
        pos = body + (size_t)sz;
    }
    if (!ima4 || !data || ch < 1 || ch > 2 || fpp != 64 || bpp != 34 * ch || rate <= 0) {
        log_msg("caf: unsupported format (only IMA4 is handled)");
        return false;
    }
    size_t npk = dlen / bpp;
    int16_t *pcm = malloc(npk * 64 * ch * sizeof *pcm);
    if (!pcm) return false;
    int pred[2] = {0, 0}, idx[2] = {0, 0};
    for (size_t p = 0; p < npk; p++) {
        for (uint32_t k = 0; k < ch; k++) {
            const uint8_t *blk = data + p * bpp + k * 34;
            int pre = (int16_t)rd16(blk);
            int si = pre & 0x7f, pv = pre & ~0x7f;
            if (si > 88) si = 88;
            /* adpcm_ima_qt: keep the running predictor when the preamble only rounds it */
            if (!(p > 0 && idx[k] == si && abs(pv - pred[k]) < 0x80)) { pred[k] = pv; idx[k] = si; }
            for (int i = 0; i < 64; i++) {
                int nib = (i & 1) ? blk[2 + i / 2] >> 4 : blk[2 + i / 2] & 15;
                int step = k_ima_step[idx[k]];
                int diff = step >> 3;
                if (nib & 1) diff += step >> 2;
                if (nib & 2) diff += step >> 1;
                if (nib & 4) diff += step;
                if (nib & 8) diff = -diff;
                pred[k] = clip16(pred[k] + diff);
                idx[k] += k_ima_index[nib];
                if (idx[k] < 0) idx[k] = 0;
                if (idx[k] > 88) idx[k] = 88;
                pcm[(p * 64 + (size_t)i) * ch + k] = (int16_t)pred[k];
            }
        }
    }
    bool ok = pcm_finish(pcm, npk * 64, (int)ch, (int)lrint(rate), out);
    free(pcm);
    return ok;
}

/* ---------------- entry point ---------------- */

typedef struct { uint8_t *b; size_t n, cap; } Bytes;

static bool bytes_append(Bytes *d, const uint8_t *p, size_t n) {
    if (d->n + n > d->cap) {
        size_t nc = d->cap ? d->cap : 1 << 20;
        while (nc < d->n + n) nc *= 2;
        uint8_t *nb = realloc(d->b, nc);
        if (!nb) return false;
        d->b = nb;
        d->cap = nc;
    }
    memcpy(d->b + d->n, p, n);
    d->n += n;
    return true;
}

int media_decode_audio(const char *path, Pcm *out, int max_streams) {
    if (max_streams > 4) max_streams = 4;
    if (max_streams < 1) return 0;
    uint8_t magic[4] = {0};
    FILE *f = fopen(path, "rb");
    if (!f) { log_msg("cannot open %s", path); return 0; }
    size_t got = fread(magic, 1, 4, f);
    fclose(f);
    if (got < 4) return 0;

    if (!memcmp(magic, "CRID", 4)) {   /* USM: every @SFA channel is a complete ADX stream */
        Bytes tr[4] = {{0}};
        UsmStream *s = usm_stream_open(path, "@SFA", -1);
        size_t n;
        int chno;
        const uint8_t *p;
        while (s && (p = usm_stream_next(s, &n, &chno)))
            if (chno >= 0 && chno < max_streams) bytes_append(&tr[chno], p, n);
        usm_stream_close(s);
        int ntracks = 0;
        for (int k = 0; k < max_streams; k++) {
            if (tr[k].n && ntracks == k && adx_decode(tr[k].b, tr[k].n, &out[k])) ntracks++;
            free(tr[k].b);
        }
        return ntracks;
    }

    size_t len;
    uint8_t *data = read_file(path, &len);
    if (!data) return 0;
    bool ok = false;
    if (magic[0] == 0x80 && magic[1] == 0x00) ok = adx_decode(data, len, &out[0]);
    else if (!memcmp(magic, "caff", 4)) ok = caf_decode(data, len, &out[0]);
    else log_msg("%s: unknown audio format", path);
    free(data);
    return ok ? 1 : 0;
}

void pcm_free(Pcm *p) {
    free(p->pcm);
    p->pcm = NULL;
    p->frames = 0;
}

/* ---------------- video ---------------- */

struct VideoStream {
    UsmStream *usm;
    plm_buffer_t *buf;
    plm_video_t *dec;
    double fps;
    long frame_no;          /* frames decoded so far; frame k is due at k / fps */
    bool eof;
    SDL_Texture *tex;
    bool shown;
    uint8_t *pp;            /* deblocked copy of the shown frame (Y, Cb, Cr); NULL: filter off */
};

/* Deblocking post-filter. The movies are 320x480 MPEG-1 at a low bit rate (quantizer up to 31), drawn 2x
   (3x and more on phones), so the 8x8 blocks of smooth areas (skin, floors, sky) show as tiles; the
   original shows them too. Per line across a block edge (4 pixels each side): when both sides are smooth,
   the jump at the edge beyond the local slope is spread as a linear ramp over all 8 pixels, which removes
   the tile without blurring the content (only the jump is changed); otherwise only a small clipped
   correction of the two edge pixels. Thresholds grow with the picture's quantizer, so real edges stay.
   It works on a copy, since the decoder's frames are the reference pictures for the next ones.
   MF_NO_DEBLOCK=1 turns it off. */
static inline uint8_t clip255(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

static void deblock_edge(uint8_t *q, int step, int alpha, int beta, int tc) {
    int p[4], r[4];   /* p[0] = p0 next to the edge ... p[3]; r[] likewise on the far side */
    for (int i = 0; i < 4; i++) p[i] = q[-(i + 1) * step], r[i] = q[i * step];
    int slope2 = (p[0] - p[1]) + (r[1] - r[0]);   /* 2x the local gradient */
    int jump = r[0] - p[0];
    int s = (2 * jump - slope2) / 2;              /* the step the block edge adds */
    if (abs(s) >= alpha || !s) return;
    bool smooth = true;
    for (int i = 0; i < 3 && smooth; i++)
        smooth = abs(p[i] - p[i + 1]) < beta && abs(r[i] - r[i + 1]) < beta;
    if (smooth) {   /* ramp: weights 7,5,3,1 /16 on each side */
        for (int i = 0; i < 4; i++) {
            int c = (s * (7 - 2 * i) + (s < 0 ? -8 : 8)) / 16;
            q[-(i + 1) * step] = clip255(p[i] + c);
            q[i * step] = clip255(r[i] - c);
        }
    } else if (abs(p[0] - p[1]) < beta && abs(r[1] - r[0]) < beta) {
        int d = (jump * 4 + (p[1] - r[1]) + 4) >> 3;
        d = d < -tc ? -tc : d > tc ? tc : d;
        q[-step] = clip255(p[0] + d);
        q[0] = clip255(r[0] - d);
    }
}

static void deblock_plane(uint8_t *d, const plm_plane_t *s, int qp) {
    int w = (int)s->width, h = (int)s->height;
    memcpy(d, s->data, (size_t)w * (size_t)h);
    if (qp <= 0) return;
    int alpha = 3 * qp + 12, beta = qp / 2 + 4, tc = qp / 3 + 1;
    if (alpha > 80) alpha = 80;
    for (int y = 0; y < h; y++)   /* vertical edges, then horizontal ones */
        for (int x = 8; x + 4 <= w; x += 8) deblock_edge(d + y * w + x, 1, alpha, beta, tc);
    for (int y = 8; y + 4 <= h; y += 8)
        for (int x = 0; x < w; x++) deblock_edge(d + y * w + x, w, alpha, beta, tc);
}

/* pl_mpeg pulls more bytes: feed the next @SFV payloads */
static void video_feed(plm_buffer_t *b, void *user) {
    VideoStream *v = user;
    size_t fed = 0, n;
    const uint8_t *p;
    while (fed < 64 * 1024 && v->usm && (p = usm_stream_next(v->usm, &n, NULL))) {
        plm_buffer_write(b, (uint8_t *)p, n);
        fed += n;
    }
    if (!fed) {   /* end of stream: lets pl_mpeg flush the last reference frame */
        usm_stream_close(v->usm);
        v->usm = NULL;
        plm_buffer_signal_end(b);
    }
}

VideoStream *video_open(const char *path, SDL_Renderer *ren) {
    VideoStream *v = calloc(1, sizeof *v);
    if (!v) return NULL;
    v->usm = usm_stream_open(path, "@SFV", 0);
    if (!v->usm) goto fail;
    v->buf = plm_buffer_create_with_capacity(256 * 1024);
    if (!v->buf) goto fail;
    plm_buffer_set_load_callback(v->buf, video_feed, v);
    v->dec = plm_video_create_with_buffer(v->buf, 0);
    if (!v->dec || !plm_video_has_header(v->dec)) goto fail;
    /* VIDEO_HDRINFO: 20 fps (MPEG-1 cannot signal it) and mpeg_dcprec = 11, the TMPGEnc
       11-bit intra DC extension that plain MPEG-1 decoders get wrong (pl_mpeg is patched) */
    int dcprec = 8;
    usm_video_info(path, &v->fps, &dcprec);
    plm_video_set_intra_dc_precision(v->dec, dcprec - 8);
    if (v->fps <= 0) v->fps = plm_video_get_framerate(v->dec);
    if (v->fps <= 0) v->fps = 20.0;
    v->tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, plm_video_get_width(v->dec),
                               plm_video_get_height(v->dec));
    if (!(getenv("MF_NO_DEBLOCK") && atoi(getenv("MF_NO_DEBLOCK")))) {
        size_t lw = (size_t)(plm_video_get_width(v->dec) + 15) & ~(size_t)15, lh = (size_t)(plm_video_get_height(v->dec) + 15) & ~(size_t)15;
        v->pp = malloc(lw * lh * 3 / 2);   /* NULL: shown unfiltered */
    }
    if (!v->tex) goto fail;
    return v;
fail:
    log_msg("video: cannot open %s", path);
    video_close(v);
    return NULL;
}

void video_update(VideoStream *v, double t) {
    if (!v) return;
    plm_frame_t *last = NULL;
    int decoded = 0;
    while (!v->eof && (double)v->frame_no / v->fps <= t) {
        plm_frame_t *f = plm_video_decode(v->dec);
        if (!f) { v->eof = true; break; }
        v->frame_no++;
        last = f;
        if (++decoded > 240) break; /* never stall the main loop on a long catch-up */
    }
    if (last) {   /* the frame stays valid until the next plm_video_decode */
        SDL_Rect r = {0, 0, (int)last->width, (int)last->height};
        const uint8_t *y = last->y.data, *cb = last->cb.data, *cr = last->cr.data;
        if (v->pp) {
            int qp = plm_video_get_quantizer(v->dec);
            uint8_t *py = v->pp, *pcb = py + last->y.width * last->y.height, *pcr = pcb + last->cb.width * last->cb.height;
            deblock_plane(py, &last->y, qp);
            deblock_plane(pcb, &last->cb, qp);
            deblock_plane(pcr, &last->cr, qp);
            y = py, cb = pcb, cr = pcr;
        }
        SDL_UpdateYUVTexture(v->tex, &r, y, (int)last->y.width, cb, (int)last->cb.width, cr, (int)last->cr.width);
        v->shown = true;
    }
}

SDL_Texture *video_texture(VideoStream *v) { return (v && v->shown) ? v->tex : NULL; }

void video_close(VideoStream *v) {
    if (!v) return;
    free(v->pp);
    if (v->tex) SDL_DestroyTexture(v->tex);
    if (v->dec) plm_video_destroy(v->dec);
    if (v->buf) plm_buffer_destroy(v->buf);
    usm_stream_close(v->usm);
    free(v);
}
