/*
 * libavformat/libavcodec glue replacing CRI Mana / CRI Atom / AudioToolbox:
 *  - media_decode_audio(): decode every audio stream of a .usm/.adx/.caf to
 *    interleaved S16 stereo @44.1 kHz in one demux pass
 *  - VideoStream: streaming MPEG-1 decode of the USM video track into an
 *    SDL IYUV texture, presented by media time (the audio clock drives it)
 */
#include "mf.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- audio ---------------- */

typedef struct {
    int stream;
    AVCodecContext *dec;
    SwrContext *swr;
    int16_t *buf;
    size_t frames, cap;
} ADec;

static bool append_converted(ADec *d, const uint8_t **in, int in_count) {
    int want = swr_get_out_samples(d->swr, in_count);
    if (want < 0) return false;
    if (d->frames + (size_t)want > d->cap) {
        size_t nc = d->cap ? d->cap : 65536;
        while (nc < d->frames + (size_t)want) nc *= 2;
        int16_t *nb = realloc(d->buf, nc * 2 * sizeof(int16_t));
        if (!nb) return false;
        d->buf = nb;
        d->cap = nc;
    }
    uint8_t *out[1] = {(uint8_t *)(d->buf + d->frames * 2)};
    int got = swr_convert(d->swr, out, want, in, in_count);
    if (got < 0) return false;
    d->frames += (size_t)got;
    return true;
}

static void drain(ADec *d, AVFrame *fr) {
    while (avcodec_receive_frame(d->dec, fr) == 0) {
        append_converted(d, (const uint8_t **)fr->extended_data, fr->nb_samples);
        av_frame_unref(fr);
    }
}

int media_decode_audio(const char *path, Pcm *out, int max_streams) {
    AVFormatContext *fc = NULL;
    if (avformat_open_input(&fc, path, NULL, NULL) < 0) { log_msg("libav: cannot open %s", path); return 0; }
    if (avformat_find_stream_info(fc, NULL) < 0) { avformat_close_input(&fc); return 0; }

    ADec dec[4];
    int n = 0;
    if (max_streams > 4) max_streams = 4;
    for (unsigned i = 0; i < fc->nb_streams && n < max_streams; i++) {
        AVStream *st = fc->streams[i];
        if (st->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) { st->discard = AVDISCARD_ALL; continue; }
        const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!codec) continue;
        ADec *d = &dec[n];
        memset(d, 0, sizeof *d);
        d->stream = (int)i;
        d->dec = avcodec_alloc_context3(codec);
        if (!d->dec || avcodec_parameters_to_context(d->dec, st->codecpar) < 0 || avcodec_open2(d->dec, codec, NULL) < 0) {
            avcodec_free_context(&d->dec);
            continue;
        }
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        AVChannelLayout in_layout;
        if (d->dec->ch_layout.nb_channels > 0) av_channel_layout_copy(&in_layout, &d->dec->ch_layout);
        else av_channel_layout_default(&in_layout, 2);
        if (swr_alloc_set_opts2(&d->swr, &stereo, AV_SAMPLE_FMT_S16, AUDIO_RATE, &in_layout, d->dec->sample_fmt,
                                d->dec->sample_rate, 0, NULL) < 0 || swr_init(d->swr) < 0) {
            av_channel_layout_uninit(&in_layout);
            swr_free(&d->swr);
            avcodec_free_context(&d->dec);
            continue;
        }
        av_channel_layout_uninit(&in_layout);
        n++;
    }
    /* streams beyond max are discarded too */
    for (unsigned i = 0; i < fc->nb_streams; i++) {
        bool used = false;
        for (int k = 0; k < n; k++) used |= dec[k].stream == (int)i;
        if (!used) fc->streams[i]->discard = AVDISCARD_ALL;
    }

    AVPacket *pkt = av_packet_alloc();
    AVFrame *fr = av_frame_alloc();
    while (pkt && fr && av_read_frame(fc, pkt) >= 0) {
        for (int k = 0; k < n; k++) {
            if (dec[k].stream != pkt->stream_index) continue;
            if (avcodec_send_packet(dec[k].dec, pkt) == 0) drain(&dec[k], fr);
        }
        av_packet_unref(pkt);
    }
    for (int k = 0; k < n; k++) {
        avcodec_send_packet(dec[k].dec, NULL);
        drain(&dec[k], fr);
        append_converted(&dec[k], NULL, 0); /* flush resampler */
        out[k].pcm = dec[k].buf;
        out[k].frames = dec[k].frames;
        swr_free(&dec[k].swr);
        avcodec_free_context(&dec[k].dec);
    }
    av_frame_free(&fr);
    av_packet_free(&pkt);
    avformat_close_input(&fc);
    return n;
}

void pcm_free(Pcm *p) {
    free(p->pcm);
    p->pcm = NULL;
    p->frames = 0;
}

/* ---------------- video ---------------- */

struct VideoStream {
    AVFormatContext *fc;
    AVCodecContext *dec;
    int vidx;
    double tb;
    AVPacket *pkt;
    AVFrame *pending;       /* decoded, waiting for its presentation time */
    AVFrame *cur;           /* newest due frame (uploaded) */
    bool has_pending;
    double pending_pts;
    double frame_dur;
    long frame_no;
    bool eof;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    bool shown;
    struct SwsContext *sws;
    uint8_t *conv[4];
    int conv_ls[4];
};

VideoStream *video_open(const char *path, SDL_Renderer *ren) {
    VideoStream *v = calloc(1, sizeof *v);
    if (!v) return NULL;
    v->ren = ren;
    if (avformat_open_input(&v->fc, path, NULL, NULL) < 0 || avformat_find_stream_info(v->fc, NULL) < 0) goto fail;
    v->vidx = av_find_best_stream(v->fc, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (v->vidx < 0) goto fail;
    for (unsigned i = 0; i < v->fc->nb_streams; i++)
        if ((int)i != v->vidx) v->fc->streams[i]->discard = AVDISCARD_ALL;
    AVStream *st = v->fc->streams[v->vidx];
    const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) goto fail;
    v->dec = avcodec_alloc_context3(codec);
    if (!v->dec || avcodec_parameters_to_context(v->dec, st->codecpar) < 0 || avcodec_open2(v->dec, codec, NULL) < 0) goto fail;
    v->tb = av_q2d(st->time_base);
    AVRational fr = av_guess_frame_rate(v->fc, st, NULL);
    v->frame_dur = (fr.num > 0 && fr.den > 0) ? (double)fr.den / fr.num : 1.0 / 20.0;
    v->pkt = av_packet_alloc();
    v->pending = av_frame_alloc();
    v->cur = av_frame_alloc();
    v->tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, v->dec->width, v->dec->height);
    if (!v->pkt || !v->pending || !v->cur || !v->tex) goto fail;
    return v;
fail:
    log_msg("video: cannot open %s", path);
    video_close(v);
    return NULL;
}

static bool decode_next(VideoStream *v) {
    for (;;) {
        int r = avcodec_receive_frame(v->dec, v->pending);
        if (r == 0) {
            int64_t ts = v->pending->best_effort_timestamp;
            v->pending_pts = (ts != AV_NOPTS_VALUE) ? ts * v->tb : v->frame_no * v->frame_dur;
            v->frame_no++;
            v->has_pending = true;
            return true;
        }
        if (r == AVERROR_EOF) { v->eof = true; return false; }
        if (r != AVERROR(EAGAIN)) { v->eof = true; return false; }
        /* need input */
        for (;;) {
            int rr = av_read_frame(v->fc, v->pkt);
            if (rr < 0) { avcodec_send_packet(v->dec, NULL); break; }
            if (v->pkt->stream_index == v->vidx) {
                avcodec_send_packet(v->dec, v->pkt);
                av_packet_unref(v->pkt);
                break;
            }
            av_packet_unref(v->pkt);
        }
    }
}

static void upload(VideoStream *v, AVFrame *f) {
    if (f->format == AV_PIX_FMT_YUV420P || f->format == AV_PIX_FMT_YUVJ420P) {
        SDL_UpdateYUVTexture(v->tex, NULL, f->data[0], f->linesize[0], f->data[1], f->linesize[1], f->data[2], f->linesize[2]);
    } else {
        if (!v->sws) {
            v->sws = sws_getContext(f->width, f->height, f->format, f->width, f->height, AV_PIX_FMT_YUV420P,
                                    SWS_BILINEAR, NULL, NULL, NULL);
            if (!v->sws || av_image_alloc(v->conv, v->conv_ls, f->width, f->height, AV_PIX_FMT_YUV420P, 16) < 0) return;
        }
        sws_scale(v->sws, (const uint8_t *const *)f->data, f->linesize, 0, f->height, v->conv, v->conv_ls);
        SDL_UpdateYUVTexture(v->tex, NULL, v->conv[0], v->conv_ls[0], v->conv[1], v->conv_ls[1], v->conv[2], v->conv_ls[2]);
    }
    v->shown = true;
}

void video_update(VideoStream *v, double t) {
    if (!v) return;
    int decoded = 0;
    bool have_new = false;
    for (;;) {
        if (!v->has_pending && (v->eof || !decode_next(v))) break;
        if (v->pending_pts > t) break;
        /* frame is due: keep it as the newest candidate, look at the next one */
        av_frame_unref(v->cur);
        av_frame_move_ref(v->cur, v->pending);
        v->has_pending = false;
        have_new = true;
        if (++decoded > 240) break; /* never stall the main loop on a long catch-up */
    }
    if (have_new) upload(v, v->cur);
}

SDL_Texture *video_texture(VideoStream *v) { return (v && v->shown) ? v->tex : NULL; }

void video_close(VideoStream *v) {
    if (!v) return;
    if (v->tex) SDL_DestroyTexture(v->tex);
    av_frame_free(&v->pending);
    av_frame_free(&v->cur);
    av_packet_free(&v->pkt);
    avcodec_free_context(&v->dec);
    if (v->fc) avformat_close_input(&v->fc);
    sws_freeContext(v->sws);
    if (v->conv[0]) av_freep(&v->conv[0]);
    free(v);
}
