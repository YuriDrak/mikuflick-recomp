/*
 * The ending: the credits roll (SceneEnding, scene 9, with WindowEnding) and the ending movie
 * (ScenePreEnding, scene 14, with WindowPreEnding).
 *
 * How the original gets here:
 *  - OPTIONS -> CREDITS -> ENDING button (TouchGotoEnding, type 0x26; WindowOption TouchButton
 *    @0x4536c: SE 0x13, NextScene 9). WindowEnding exec @0x38c94 then returns to OPTIONS (NextScene 6,
 *    because GetPrevScene == 6), and WindowOption init @0x443dc reopens the CREDITS page. The movie
 *    does not play on this path. This is the only way in the port (the button is always shown).
 *  - First clear (WindowResult loadTexture @0x4cbd4: !IsEnding, every song 0-8 cleared on Normal and
 *    song 9 still locked) -> credits -> ScenePreEnding (prev != 6): ending.usm, then song 9 (初音ミクの激唱)
 *    on Extreme. Everything is unlocked in the port, so this trigger never fires and is not ported;
 *    `--start-scene ending` / `--start-scene movie` show that path for testing.
 *
 * The credit lines are game data: s_tbl_Ending, exported into gamedata.txt by mfx.c.
 */
#include "mf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define END_BGM 25              /* BGM04 (WindowEnding init PlayBGM: 0x19 @0x38b88) */
#define END_SCROLL0 40.0f       /* m_ScrollY = 0x42200000 (init @0x38b54) */
#define END_SCROLL_STEP 3.5f    /* exec @0x38d00: m_ScrollY += -3.5 per tick */
#define END_SCROLL_MIN -10530.0f /* exec @0x38d08 (0xc6248800) */
#define END_HOLD 96             /* mode 1: 0x60 ticks, then Fade (exec @0x38c64) */
#define END_FADE 130            /* SoundManager Fade: 0x19, 130.0 (0x43020000), 0 (@0x38c86) */
#define END_OUT 171             /* mode 2: 0xab ticks, then NextScene + Stop 0x19 (@0x38c9e) */
#define END_SONG 9              /* ScenePreEnding Exec @0x79840: pack 0 song 9 */
#define END_DIFF 3              /* ... SetDifficulty: 3 (Extreme) @0x79868 */

struct Ending {
    SDL_Renderer *ren;
    Assets *as;
    const GameData *gd;
    const Options *opt;
    int scene;                  /* SCENE_ENDING / SCENE_PREENDING while active, else 0 */
    int prev;                   /* SceneManager GetPrevScene when the scene was set up */
    /* WindowEnding */
    Atlas bg;                   /* ending.png (+ ending.plist: bg_ending 640x960 at 0,0) */
    float scroll;               /* m_ScrollY */
    int mode, mode_cnt;         /* m_Mode, m_ModeCnt */
    int bgm_voice;
    bool fading;
    int fade_cnt;               /* SoundBase m_FadeCounter */
    /* WindowPreEnding */
    VideoStream *video;
    Pcm tracks[2];
    int ntracks;
    bool done;
    /* 30 Hz exec clock */
    Uint32 last_ms;
    double acc;
};

Ending *ending_create(SDL_Renderer *ren, Assets *as, const GameData *gd, const Options *opt) {
    Ending *e = calloc(1, sizeof *e);
    if (!e) return NULL;
    e->ren = ren;
    e->as = as;
    e->gd = gd;
    e->opt = opt;
    e->bgm_voice = -1;
    return e;
}

void ending_leave(Ending *e) {
    if (!e || !e->scene) return;
    if (e->bgm_voice >= 0) mixer_sfx_stop(e->bgm_voice);
    e->bgm_voice = -1;
    e->fading = false;
    if (e->bg.tex) atlas_free(&e->bg);
    if (e->video || e->ntracks) mixer_music_stop();
    video_close(e->video);
    e->video = NULL;
    for (int i = 0; i < e->ntracks; i++) pcm_free(&e->tracks[i]);
    e->ntracks = 0;
    e->scene = 0;
}

void ending_destroy(Ending *e) {
    if (!e) return;
    ending_leave(e);
    free(e);
}

void ending_enter(Ending *e, int scene, int prev) {
    if (!e) return;
    ending_leave(e);
    e->scene = scene;
    e->prev = prev;
    e->done = false;
    e->last_ms = SDL_GetTicks();
    e->acc = 0;
    if (scene == SCENE_ENDING) {
        /* WindowEnding loadTexture @0x38bec: CloudTexture Load "ending.png"; init @0x38940 */
        atlas_load(&e->bg, e->ren, e->as->appdir, "ending");
        e->scroll = END_SCROLL0;
        e->mode = e->mode_cnt = 0;
        e->fade_cnt = 0;
        e->fading = false;
        e->bgm_voice = mixer_bgm_play(&e->as->sounds[END_BGM], 1.0f, true);   /* PlayBGM: replaces BGM03 */
        /* init @0x38bae: StatusData SetEndingFlg:1 unless prev == 6; the port has no ending flag */
    } else {
        /* WindowPreEnding init @0x79d18: MikuFlickCriManager ReLoad "ending" (ending.usm, one audio track),
           CriManager SetMainAudioVolume(StatusData GetBGMVolume) */
        char path[1200];
        snprintf(path, sizeof path, "%s/ending.usm", e->as->appdir);
        e->ntracks = media_decode_audio(path, e->tracks, 2);
        if (e->ntracks < 0) e->ntracks = 0;
        e->video = video_open(path, e->ren);
        if (e->ntracks > 0) {
            mixer_music_gain(e->opt->bgm_volume, 0.0f);
            mixer_music_start(&e->tracks[0], NULL);
        } else log_msg("ending: no audio in %s", path);
    }
}

/* SoundBase FadeExec @0x19080: vol = st + (ed - st) * cnt / time, cnt++, stop once cnt > time (ed 0) */
static void fade_exec(Ending *e) {
    if (!e->fading) return;
    mixer_voice_gain(e->bgm_voice, &e->as->sounds[END_BGM], 1.0f - (float)e->fade_cnt / (float)END_FADE);
    if (++e->fade_cnt > END_FADE) {
        mixer_sfx_stop(e->bgm_voice);
        e->bgm_voice = -1;
        e->fading = false;
    }
}

/* WindowEnding exec @0x38c34, one 30 Hz tick; returns the next scene or -1 */
static int ending_tick(Ending *e) {
    int next = -1;
    e->mode_cnt++;
    if (e->mode == 0) {
        e->scroll -= END_SCROLL_STEP;
        if (e->scroll < END_SCROLL_MIN) { e->scroll = END_SCROLL_MIN; e->mode = 1; e->mode_cnt = 0; }
    } else if (e->mode == 1) {
        if (e->mode_cnt >= END_HOLD) { e->mode = 2; e->mode_cnt = 0; e->fading = e->bgm_voice >= 0; e->fade_cnt = 0; }
    } else if (e->mode == 2 && e->mode_cnt >= END_OUT && !e->done) {
        e->done = true;
        next = e->prev == SCENE_OPTION ? SCENE_OPTION : SCENE_PREENDING;   /* @0x38cc8 */
        if (e->bgm_voice >= 0) mixer_sfx_stop(e->bgm_voice);               /* Stop: 0x19 */
        e->bgm_voice = -1;
        e->fading = false;
    }
    fade_exec(e);   /* SoundManager runs its fades once per frame as well */
    return next;
}

int ending_update(Ending *e, bool frozen, int *song, int *diff) {
    if (!e || !e->scene) return -1;
    Uint32 now = SDL_GetTicks();
    if (frozen) e->acc = 0;   /* SceneManager does not exec the scene while a change fades */
    else {
        e->acc += (double)(now - e->last_ms);
        if (e->acc > 500) e->acc = 500;
    }
    e->last_ms = now;
    if (e->scene == SCENE_PREENDING) {
        /* the movie runs on its own (CRI) clock, fades included */
        video_update(e->video, mixer_music_time());
        if (frozen || e->done) return -1;
        bool over = e->ntracks == 0 || (!mixer_music_playing() && mixer_music_time() >= mixer_music_length() - 0.05);
        if (!over) return -1;
        /* ScenePreEnding Exec @0x7981a: CriManager state 5 -> SetPlayingMusicData(pack 0, 9), Extreme, NextScene 2 */
        e->done = true;
        if (e->gd->nsongs <= END_SONG) return SCENE_SELECT;
        *song = END_SONG;
        *diff = END_DIFF;
        return SCENE_GAME;
    }
    while (e->acc >= 1000.0 / 30.0) {
        e->acc -= 1000.0 / 30.0;
        int n = ending_tick(e);
        if (n >= 0) { e->acc = 0; return n; }
    }
    return -1;
}

void ending_render(Ending *e) {
    if (!e) return;
    SDL_Renderer *r = e->ren;
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    if (e->scene == SCENE_PREENDING) {
        /* WindowPreEnding Render -> MikuFlickCriManager Render: the movie over the whole screen */
        SDL_Texture *vt = video_texture(e->video);
        if (vt) {
            SDL_FRect full = {0, 0, LOGICAL_W, LOGICAL_H};
            SDL_RenderCopyF(r, vt, NULL, &full);
        }
        return;
    }
    if (e->scene != SCENE_ENDING) return;
    /* WindowEnding Render @0x38d4c: RenderTex2DClip(m_Tex, 0, 0, 640x960, src 0,0 640x960), white, blend 1 */
    if (e->bg.tex) {
        SDL_Rect src = {0, 0, LOGICAL_W, LOGICAL_H};
        spr_tl_rect(r, &e->bg, &src, 0, 0, LOGICAL_W, LOGICAL_H, 255);
    }
    if (e->mode > 1) return;   /* @0x38e02: the text goes once the BGM fade starts */
    /* @0x38e6c: line i at x = 320 - width/2, y = 960 + m_ScrollY + sum of the gaps of the lines above,
       white, z -6.1; TexTextManager draws the text texture top-left there and its capitals start about
       6 px lower at font size 24 (see the result labels); font_draw* take the capital top */
    float y = (float)LOGICAL_H + e->scroll;
    const SDL_Color white = {255, 255, 255, 255};
    for (int i = 0; i < e->gd->nending; i++) {
        float scale = (float)e->gd->ending_size[i] * 1.04f / 10.0f;   /* font 24 -> M+ 25 px (font 25 -> 26) */
        if (y > -60.0f && y < (float)LOGICAL_H + 10.0f && e->gd->ending_text[i][0])
            font_draw_plain(r, e->gd->ending_text[i], 320.0f, y + 6.0f, scale, white, 0);
        y += (float)e->gd->ending_gap[i];
    }
}

SDL_Texture *ending_backdrop(const Ending *e) {
    return (e && e->scene == SCENE_PREENDING) ? video_texture(e->video) : NULL;
}
