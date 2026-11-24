/*
 * SDL2 audio mixer. Replaces CRI Atom/AudioToolbox/OpenAL.
 *
 * Music = two tracks of the song USM played together, as in the original:
 *   main (track 0, "vo": vocals)  - gain follows the tension gauge
 *   sub  (track 1, "ok": backing) - BGM volume
 * The music playback position is the master clock for gameplay (the original
 * derives PlayCnt from CRI's audio-driven movie time).
 */
#include "mf.h"

#include <math.h>
#include <string.h>

#define MAX_VOICES 24

typedef struct {
    const Pcm *pcm;
    size_t pos;
    float gain;
    bool loop, active;
} Voice;

static struct {
    SDL_AudioDeviceID dev;
    int buf_frames;
    /* music */
    const Pcm *main, *sub;
    size_t pos, length;
    bool playing, paused;
    float main_gain, sub_gain;          /* targets */
    float cur_main, cur_sub;            /* ramped */
    float music_master, sfx_master;
    /* clock */
    size_t cb_pos;                      /* music pos at start of last callback */
    Uint64 cb_counter;
    bool clock_valid;
    double last_time;
    double paused_time;
    Voice voices[MAX_VOICES];
} M;

static inline int16_t sample_at(const Pcm *p, size_t frame, int ch) {
    if (!p || frame >= p->frames) return 0;
    return p->pcm[frame * 2 + (size_t)ch];
}

static void callback(void *ud, Uint8 *stream, int bytes) {
    (void)ud;
    int16_t *out = (int16_t *)stream;
    int frames = bytes / 4;
    static int32_t acc[8192 * 2];
    if (frames > 8192) frames = 8192;
    memset(acc, 0, (size_t)frames * 2 * sizeof(int32_t));

    M.cb_counter = SDL_GetPerformanceCounter();
    M.cb_pos = M.pos;
    M.clock_valid = M.playing && !M.paused;

    if (M.playing && !M.paused) {
        float tm = M.main_gain * M.music_master, ts = M.sub_gain * M.music_master;
        float dm = (tm - M.cur_main) / frames, ds = (ts - M.cur_sub) / frames;
        for (int i = 0; i < frames; i++) {
            size_t f = M.pos + (size_t)i;
            if (f >= M.length) break;
            M.cur_main += dm;
            M.cur_sub += ds;
            for (int ch = 0; ch < 2; ch++)
                acc[i * 2 + ch] += (int32_t)(sample_at(M.main, f, ch) * M.cur_main + sample_at(M.sub, f, ch) * M.cur_sub);
        }
        M.cur_main = tm;
        M.cur_sub = ts;
        M.pos += (size_t)frames;
        if (M.pos >= M.length) M.pos = M.length;
    }

    for (int v = 0; v < MAX_VOICES; v++) {
        Voice *vo = &M.voices[v];
        if (!vo->active || !vo->pcm || !vo->pcm->frames) continue;
        float g = vo->gain * M.sfx_master;
        for (int i = 0; i < frames; i++) {
            if (vo->pos >= vo->pcm->frames) {
                if (vo->loop) vo->pos = 0;
                else { vo->active = false; break; }
            }
            acc[i * 2] += (int32_t)(vo->pcm->pcm[vo->pos * 2] * g);
            acc[i * 2 + 1] += (int32_t)(vo->pcm->pcm[vo->pos * 2 + 1] * g);
            vo->pos++;
        }
    }
    for (int i = 0; i < frames * 2; i++) {
        int32_t s = acc[i];
        out[i] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
    }
    if (bytes / 4 > frames) memset(out + frames * 2, 0, (size_t)(bytes / 4 - frames) * 4);
}

bool mixer_init(void) {
    memset(&M, 0, sizeof M);
    M.music_master = M.sfx_master = 1.0f;
    SDL_AudioSpec want = {0}, have;
    want.freq = AUDIO_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = callback;
    M.dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!M.dev) { log_msg("audio: %s", SDL_GetError()); return false; }
    M.buf_frames = have.samples;
    SDL_PauseAudioDevice(M.dev, 0);
    log_msg("audio: %d Hz, %d ch, buffer %d frames", have.freq, have.channels, have.samples);
    return true;
}

void mixer_quit(void) {
    if (M.dev) SDL_CloseAudioDevice(M.dev);
    M.dev = 0;
}

void mixer_set_master(float music, float sfx) {
    SDL_LockAudioDevice(M.dev);
    M.music_master = music;
    M.sfx_master = sfx;
    SDL_UnlockAudioDevice(M.dev);
}

void mixer_music_start(const Pcm *main, const Pcm *sub) {
    SDL_LockAudioDevice(M.dev);
    M.main = main;
    M.sub = sub;
    M.length = 0;
    if (main && main->frames > M.length) M.length = main->frames;
    if (sub && sub->frames > M.length) M.length = sub->frames;
    M.pos = 0;
    M.cb_pos = 0;
    M.clock_valid = false;
    M.playing = true;
    M.paused = false;
    M.last_time = 0;
    M.cur_main = M.main_gain * M.music_master;
    M.cur_sub = M.sub_gain * M.music_master;
    SDL_UnlockAudioDevice(M.dev);
}

void mixer_music_stop(void) {
    SDL_LockAudioDevice(M.dev);
    M.playing = false;
    M.main = M.sub = NULL;
    M.length = M.pos = 0;
    SDL_UnlockAudioDevice(M.dev);
}

void mixer_music_pause(bool paused) {
    double t = mixer_music_time();
    SDL_LockAudioDevice(M.dev);
    if (paused && !M.paused) M.paused_time = t;
    if (!paused && M.paused) {
        /* rewind the read position to what was actually heard so nothing is skipped */
        size_t heard = (size_t)(M.paused_time * AUDIO_RATE);
        if (heard < M.pos) M.pos = heard;
        M.clock_valid = false;
        M.last_time = M.paused_time;
    }
    M.paused = paused;
    SDL_UnlockAudioDevice(M.dev);
}

void mixer_music_gain(float main_gain, float sub_gain) {
    SDL_LockAudioDevice(M.dev);
    M.main_gain = main_gain;
    M.sub_gain = sub_gain;
    SDL_UnlockAudioDevice(M.dev);
}

double mixer_music_time(void) {
    SDL_LockAudioDevice(M.dev);
    double t;
    if (!M.playing) t = 0;
    else if (M.paused) t = M.paused_time;
    else if (!M.clock_valid) t = M.last_time;
    else {
        /* The callback that started at cb_counter filled [cb_pos, cb_pos+B);
           at that moment the device began playing the previous buffer
           [cb_pos-B, cb_pos). Interpolate with the wall clock inside it. */
        double el = (double)(SDL_GetPerformanceCounter() - M.cb_counter) / (double)SDL_GetPerformanceFrequency();
        double heard = (double)M.cb_pos - M.buf_frames + el * AUDIO_RATE;
        if (heard > (double)M.cb_pos) heard = (double)M.cb_pos;
        if (heard < 0) heard = 0;
        t = heard / AUDIO_RATE;
        if (t < M.last_time) t = M.last_time; /* monotonic */
    }
    M.last_time = t;
    SDL_UnlockAudioDevice(M.dev);
    return t;
}

double mixer_music_length(void) { return (double)M.length / AUDIO_RATE; }

bool mixer_music_playing(void) {
    SDL_LockAudioDevice(M.dev);
    bool p = M.playing && M.pos < M.length;
    SDL_UnlockAudioDevice(M.dev);
    return p;
}

int mixer_sfx_play(const Pcm *p, float gain, bool loop) {
    if (!p || !p->frames) return -1;
    SDL_LockAudioDevice(M.dev);
    int slot = -1;
    for (int i = 0; i < MAX_VOICES; i++)
        if (!M.voices[i].active) { slot = i; break; }
    if (slot < 0) { /* steal the voice closest to its end */
        size_t best = 0;
        for (int i = 0; i < MAX_VOICES; i++)
            if (!M.voices[i].loop && M.voices[i].pos >= best) { best = M.voices[i].pos; slot = i; }
    }
    if (slot >= 0) M.voices[slot] = (Voice){p, 0, gain, loop, true};
    SDL_UnlockAudioDevice(M.dev);
    return slot;
}

void mixer_sfx_stop(int v) {
    if (v < 0 || v >= MAX_VOICES) return;
    SDL_LockAudioDevice(M.dev);
    M.voices[v].active = false;
    SDL_UnlockAudioDevice(M.dev);
}

void mixer_sfx_stop_all(void) {
    SDL_LockAudioDevice(M.dev);
    for (int i = 0; i < MAX_VOICES; i++) M.voices[i].active = false;
    SDL_UnlockAudioDevice(M.dev);
}
