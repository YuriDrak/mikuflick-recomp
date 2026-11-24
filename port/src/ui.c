/*
 * Menu scenes: boot logos, title, music select, result.
 * Simplified layouts built from the original atlas sprites (see
 * docs/linux_port_plan.md §6). Text goes through font_draw (text.c, SDL_ttf); the 5x7 bitmap font below
 * is the fallback when no TrueType font can be opened.
 */
#include "mf.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- font */

typedef struct { char c; uint8_t rows[7]; } Glyph;
static const Glyph k_font[] = {
    {'A', {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}}, {'B', {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e}},
    {'C', {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e}}, {'D', {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e}},
    {'E', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f}}, {'F', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10}},
    {'G', {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f}}, {'H', {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}},
    {'I', {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e}}, {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0c}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}}, {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f}},
    {'M', {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11}}, {'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
    {'O', {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}}, {'P', {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10}},
    {'Q', {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d}}, {'R', {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11}},
    {'S', {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e}}, {'T', {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}}, {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a}}, {'X', {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04}}, {'Z', {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f}},
    {'0', {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e}}, {'1', {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e}},
    {'2', {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f}}, {'3', {0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e}},
    {'4', {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02}}, {'5', {0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e}},
    {'6', {0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e}}, {'7', {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e}}, {'9', {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c}}, {',', {0x00, 0x00, 0x00, 0x00, 0x0c, 0x04, 0x08}},
    {':', {0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00}}, {'!', {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}},
    {'?', {0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}}, {'-', {0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00}},
    {'+', {0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00}}, {'/', {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00}},
    {'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}}, {')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}},
    {'\'', {0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00}}, {'<', {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}},
    {'>', {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}}, {'%', {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}},
    {'*', {0x00, 0x04, 0x15, 0x0e, 0x15, 0x04, 0x00}}, {'=', {0x00, 0x00, 0x1f, 0x00, 0x1f, 0x00, 0x00}},
};

static const Glyph *glyph(char c) {
    c = (char)toupper((unsigned char)c);
    for (size_t i = 0; i < sizeof k_font / sizeof *k_font; i++)
        if (k_font[i].c == c) return &k_font[i];
    return NULL;
}

/* align: -1 left, 0 centre, 1 right; y is the top of the text */
void bitmap_font_draw(SDL_Renderer *ren, const char *text, float x, float y, float scale, SDL_Color col, int align) {
    float w = (float)strlen(text) * 6 * scale - scale;
    float px = align < 0 ? x : align == 0 ? x - w / 2 : x - w;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    for (int pass = 0; pass < 2; pass++) {
        /* pass 0: drop shadow */
        if (pass == 0) SDL_SetRenderDrawColor(ren, 0, 0, 0, (Uint8)(col.a * 0.7f));
        else SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, col.a);
        float off = pass == 0 ? scale * 0.6f : 0;
        float cx = px;
        for (const char *s = text; *s; s++, cx += 6 * scale) {
            const Glyph *gl = glyph(*s);
            if (!gl) continue;
            for (int r = 0; r < 7; r++)
                for (int c = 0; c < 5; c++)
                    if (gl->rows[r] & (0x10 >> c)) {
                        SDL_FRect rc = {cx + c * scale + off, y + r * scale + off, scale, scale};
                        SDL_RenderFillRectF(ren, &rc);
                    }
        }
    }
}

/* draw a number with digit sprites "<prefix>NN" right-aligned at right_x */
void digits_draw(SDL_Renderer *ren, const Atlas *a, const char *prefix, long value, float right_x, float cy, float scale,
                 bool commas, int comma_index) {
    char num[32], name[64];
    snprintf(num, sizeof num, "%ld", value < 0 ? 0 : value);
    int len = (int)strlen(num);
    float x = right_x;
    int since = 0;
    for (int i = len - 1; i >= 0; i--) {
        if (commas && since == 3) {
            snprintf(name, sizeof name, "%s%02d", prefix, comma_index);
            float w = spr_width(a, name) * scale;
            spr_draw(ren, a, name, x - w / 2, cy + 6 * scale, scale, 255);
            x -= w * 0.7f;
            since = 0;
        }
        snprintf(name, sizeof name, "%s%02d", prefix, num[i] - '0');
        float w = spr_width(a, name) * scale;
        spr_draw(ren, a, name, x - w / 2, cy, scale, 255);
        x -= w * 0.82f;
        since++;
    }
}

/* ---------------------------------------------------------------- state */

#define MAX_BGFX 48

/* EffectBGEffect: additive cube drifting up across the select screen */
typedef struct { bool on; int frame; float x, y, vx, vy, sx, sy, ang, angv; } BgFx;

/* WindowCover: one jacket of the cover flow */
typedef struct {
    float x, tx;                 /* centre x along the strip (m_Pos / m_TgtPos) */
    float lx, ly, rx, ry;        /* m_StretchLeft / m_StretchRight */
    float tlx, tly, trx, try_;   /* m_TargetLeft / m_TargetRight */
    int timer;                   /* m_StretchTimer */
} Cover;

struct UiState {
    SDL_Renderer *ren;
    Assets *as;
    const GameData *gd;
    SaveData *save;
    Options *opt;
    int scene;
    Uint32 enter_ms, last_ms;
    double tick_acc;
    unsigned ticks;              /* 30 Hz ticks since the scene was entered */
    /* title (WindowTitle m_State / m_Count / m_FadeAlpha) */
    int tstate, tcount;
    float fade;
    unsigned blink;
    float menu_x[4];
    int menu_focus, menu_press, menu_flash;
    /* select */
    int song, diff;
    Atlas music[8];
    bool music_loaded[8];
    Cover cov[MAX_SONGS];
    float scr, scr_vel;          /* m_ScrPos driven by CloudSimpleSpring */
    float csr_x;                 /* m_CsrX difficulty cursor */
    int logo_song, prev_logo;
    float logo_fade;
    unsigned pulse;              /* TouchMusicSelect m_Counter */
    BgFx bg[MAX_BGFX];
    bool play_pressed;
    float drag_x0;
    bool dragging;
    Pcm preview;
    int preview_song;
    int preview_voice;
    Uint32 song_changed_ms;
    int bgm_voice;
    /* result */
    GameResult res;
    bool new_record;
};

static const SDL_Color WHITE = {255, 255, 255, 255};

UiState *ui_create(SDL_Renderer *ren, Assets *as, const GameData *gd, SaveData *save, Options *opt) {
    UiState *u = calloc(1, sizeof *u);
    if (!u) return NULL;
    u->ren = ren;
    u->as = as;
    u->gd = gd;
    u->save = save;
    u->opt = opt;
    u->preview_song = -1;
    u->preview_voice = -1;
    u->bgm_voice = -1;
    u->diff = 1;
    u->menu_focus = 0;
    u->menu_press = -1;
    u->logo_song = u->prev_logo = -1;
    return u;
}

static void stop_preview(UiState *u) {
    if (u->preview_voice >= 0) mixer_sfx_stop(u->preview_voice);
    u->preview_voice = -1;
    pcm_free(&u->preview);
    u->preview_song = -1;
}

static void stop_bgm(UiState *u) {
    if (u->bgm_voice >= 0) mixer_sfx_stop(u->bgm_voice);
    u->bgm_voice = -1;
}

void ui_destroy(UiState *u) {
    if (!u) return;
    stop_preview(u);
    for (int i = 0; i < 8; i++) if (u->music_loaded[i]) atlas_free(&u->music[i]);
    free(u);
}

/* music_00_0N atlas index from "music_00_0N" */
static int art_slot(const char *atlas) {
    size_t l = strlen(atlas);
    int n = l ? atlas[l - 1] - '0' : 0;
    return (n >= 0 && n < 8) ? n : 0;
}

static Atlas *load_music_slot(UiState *u, int slot, const char *name) {
    if (!u->music_loaded[slot]) {
        u->music_loaded[slot] = atlas_load(&u->music[slot], u->ren, u->as->appdir, name);
        if (!u->music_loaded[slot]) return NULL;
    }
    return &u->music[slot];
}

static Atlas *music_atlas(UiState *u, int song) {
    const char *name = u->gd->songs[song].art_atlas;
    return load_music_slot(u, art_slot(name), name);
}

/* Song art/logo: only music_00_01 ships a .plist; the other music_00_0N sheets share its
   layout, so frame rects come from music_00_01. */
static const Frame *music_frame(UiState *u, int song, const char *kind) {
    Atlas *ref = load_music_slot(u, 1, "music_00_01");
    if (!ref) return NULL;
    char name[64];
    snprintf(name, sizeof name, "%s_00_01_%02d", kind, song % 3); /* SetArtWorkIndex: idx % 3 */
    return atlas_find(ref, name);
}

/* ------------------------------------------------------------ select: cover flow */

/* WindowMusicSelect updateTitle:: @0x4017c + WindowCover setDirection:: @0x36758 */
static void covers_retarget(UiState *u, int timer) {
    float x = 280.0f;
    for (int i = 0; i < u->gd->nsongs; i++) {
        Cover *c = &u->cov[i];
        c->tx = (i == 0 && u->song == 0) ? x + 40.0f : x;   /* original special-cases cover 0 when selected */
        x += (i == u->song || i == u->song - 1) ? 80.0f : 40.0f;
        if (i == u->song) { c->tlx = c->tly = c->trx = c->try_ = 0; }           /* dir 0: facing */
        else if (i < u->song) { c->tlx = 0; c->tly = 10; c->trx = -230; c->try_ = 60; }  /* dir 1 */
        else { c->tlx = 230; c->tly = 60; c->trx = 0; c->try_ = 10; }              /* dir 2 */
        c->timer = timer;
    }
}

static void covers_snap(UiState *u) {
    covers_retarget(u, 0);
    for (int i = 0; i < u->gd->nsongs; i++) {
        Cover *c = &u->cov[i];
        c->x = c->tx; c->lx = c->tlx; c->ly = c->tly; c->rx = c->trx; c->ry = c->try_;
    }
    u->scr = (float)u->song * 40.0f;
    u->scr_vel = 0;
    u->csr_x = 155.0f * (u->diff == DIFF_BREAK ? 1 : u->diff);
    u->logo_song = u->song;
    u->logo_fade = 1.0f;
}

static void spawn_bgfx(UiState *u) {
    for (int i = 0; i < MAX_BGFX; i++) {
        BgFx *b = &u->bg[i];
        if (b->on) continue;
        /* EffectBGEffect initWithType:: @0xbff4 */
        b->on = true;
        b->frame = rand() % 5;
        b->x = (float)(rand() % (LOGICAL_W - 100) + 50);
        b->y = (float)(LOGICAL_H + 100);
        b->vx = (float)(rand() % 100) / 100.0f - 0.5f;
        b->vy = -((float)(rand() % 50) / 100.0f + 0.5f);
        b->sx = ((float)(rand() % 50) / 100.0f + 0.5f) * 1.2f;
        b->sy = ((float)(rand() % 50) / 100.0f + 0.5f) * 1.2f;
        b->ang = (float)(rand() % 6);
        b->angv = (rand() & 1) ? 1.0f : -1.0f;
        return;
    }
}

/* one 30 Hz tick of WindowMusicSelect / WindowCover / WindowBGEffect exec */
static void select_tick(UiState *u) {
    /* CloudSimpleSpring toward index*40 */
    float target = (float)u->song * 40.0f;
    u->scr_vel = u->scr_vel * 0.55f + (target - u->scr) * 0.22f;
    u->scr += u->scr_vel;
    if (fabsf(target - u->scr) < 0.05f && fabsf(u->scr_vel) < 0.05f) { u->scr = target; u->scr_vel = 0; }
    /* WindowCover exec @0x36110: 15 % per tick toward the targets while the timer runs */
    for (int i = 0; i < u->gd->nsongs; i++) {
        Cover *c = &u->cov[i];
        if (c->timer <= 0) continue;
        if (--c->timer == 0) { c->x = c->tx; c->lx = c->tlx; c->ly = c->tly; c->rx = c->trx; c->ry = c->try_; continue; }
        c->x += (c->tx - c->x) * 0.15f;
        c->lx += (c->tlx - c->lx) * 0.15f; c->ly += (c->tly - c->ly) * 0.15f;
        c->rx += (c->trx - c->rx) * 0.15f; c->ry += (c->try_ - c->ry) * 0.15f;
    }
    /* difficulty cursor m_CsrX glides to its target (speed *0.9 per tick in the original) */
    float ctx = 155.0f * (u->diff == DIFF_BREAK ? 1 : u->diff);
    u->csr_x += (ctx - u->csr_x) * 0.35f;
    if (u->logo_fade < 1.0f) u->logo_fade = fminf(1.0f, u->logo_fade + 0.125f);
    u->pulse++;
    /* WindowBGEffect exec: 3 cubes every 100 ticks */
    if (u->ticks % 100 == 1) for (int k = 0; k < 3; k++) spawn_bgfx(u);
    for (int i = 0; i < MAX_BGFX; i++) {
        BgFx *b = &u->bg[i];
        if (!b->on) continue;
        b->x += b->vx * b->sx;
        b->y += b->vy * b->sy;
        b->ang += 0.005984f * b->angv; /* _RotSpeed */
        if (b->x < -50 || b->x > LOGICAL_W + 50 || b->y < -50) b->on = false;
    }
}

/* ------------------------------------------------------------ title state machine */

static const char *k_menu_label[4] = {"RHYTHM GAME", "PV", "OPTIONS", "SHOP"};

static void title_start_menu(UiState *u) {
    u->tstate = 10;
    for (int i = 0; i < 4; i++) u->menu_x[i] = (float)(LOGICAL_W + 128 + 20 * i); /* TouchTitleMenus init */
    u->menu_focus = 0;
    u->menu_press = -1;
    play_sound(u->as, 19); /* SE02_01 */
}

/* one 30 Hz tick of WindowTitle exec @0x50fb8 */
static void title_tick(UiState *u) {
    switch (u->tstate) {
    case 0:
        if (++u->tcount == 10) play_sound(u->as, SND_SEGA);
        if (u->tcount >= 86) { u->tcount = 0; u->tstate++; }
        break;
    case 1: case 4: case 7:
        u->fade += 0.2f;
        if (u->fade >= 1.0f) { u->fade = 1.0f; u->tstate++; }
        break;
    case 2: case 5:
        u->fade -= 0.2f;
        if (u->fade <= 0.0f) { u->fade = 0.0f; u->tstate++; }
        break;
    case 3: case 6:
        if (u->tcount++ >= 45) { u->tcount = 0; u->tstate++; }
        break;
    case 8:
        u->fade -= 0.2f;
        if (u->fade <= 0.0f) { u->fade = 0; u->tstate = 9; play_sound(u->as, SND_TITLE); u->tcount = 0; }
        break;
    case 9: case 10:
        u->tcount++;
        u->blink++;
        if (u->bgm_voice < 0 && u->tcount > 67) u->bgm_voice = mixer_sfx_play(&u->as->sounds[SND_BGM01], 0.8f, true);
        if (u->tstate == 10)
            for (int i = 0; i < 4; i++) { /* TouchTitleMenus exec: slide 100 px/tick to x = 128 */
                u->menu_x[i] -= 100.0f;
                if (u->menu_x[i] < 128.0f) u->menu_x[i] = 128.0f;
            }
        if (u->menu_flash > 0) u->menu_flash--;
        break;
    }
}

void ui_enter(UiState *u, int scene, const GameResult *res) {
    u->scene = scene;
    u->enter_ms = u->last_ms = SDL_GetTicks();
    u->tick_acc = 0;
    u->ticks = 0;
    if (scene == SCENE_BOOT) {           /* first boot: logos inside the title window */
        u->scene = SCENE_TITLE;
        u->tstate = 0; u->tcount = 0; u->fade = 0;
    } else if (scene == SCENE_TITLE) {
        stop_preview(u);
        if (u->tstate < 9) { u->tstate = 9; u->tcount = 60; u->fade = 0; }   /* --start-scene title */
        else title_start_menu(u);        /* back from music select: menu already open */
    }
    if (scene == SCENE_SELECT) {
        stop_bgm(u);
        u->song_changed_ms = SDL_GetTicks();
        covers_snap(u);
        memset(u->bg, 0, sizeof u->bg);
        for (int k = 0; k < 6; k++) { spawn_bgfx(u); }
        for (int i = 0; i < MAX_BGFX; i++) if (u->bg[i].on) u->bg[i].y = (float)(rand() % LOGICAL_H);
    }
    if (scene == SCENE_RESULT && res) {
        stop_preview(u);
        u->res = *res;
        u->new_record = false;
        if (res->rank >= 0) {
            Record *r = &u->save->rec[res->song][res->diff];
            int total = res->stage_score + res->combo_score;
            if (total > r->score) { r->score = total; u->new_record = true; }
            if (r->rank < 0 || res->rank < r->rank) r->rank = res->rank;
            if (res->max_combo > r->combo) r->combo = res->max_combo;
            if (res->rank <= 4) r->cleared = 1;
            save_write(u->save);
        }
        u->bgm_voice = mixer_sfx_play(&u->as->sounds[SND_BGM01 + 2], 0.7f, true);
    }
}

static void select_move(UiState *u, int ds, int dd) {
    if (ds) {
        int n = (u->song + ds + u->gd->nsongs) % u->gd->nsongs;
        if (n != u->song) {
            u->prev_logo = u->song;
            u->song = n;
            u->logo_song = n;
            u->logo_fade = 0.0f;
            covers_retarget(u, 10);
            u->song_changed_ms = SDL_GetTicks();
            play_sound(u->as, SND_DECIDE);
        }
    }
    if (dd) {
        int n = u->diff + dd;
        if (n >= 0 && n < NUM_DIFF) { u->diff = n; play_sound(u->as, SND_DECIDE); }
    }
}

static int start_game(UiState *u, int *song, int *diff) {
    stop_preview(u);
    stop_bgm(u);
    play_sound(u->as, SND_HIT);
    *song = u->song;
    *diff = u->diff;
    return SCENE_GAME;
}

/* title menu button hit test (menubutton_l_base 394x82 at (x, 538+82*i)) */
static int menu_hit(UiState *u, float mx, float my) {
    for (int i = 0; i < 4; i++) {
        float x = u->menu_x[i], y = 538.0f + 82.0f * i;
        if (mx >= x && mx < x + 394 && my >= y && my < y + 82) return i;
    }
    return -1;
}

static int menu_activate(UiState *u, int i) {
    if (i == 0) { play_sound(u->as, SND_HIT); return SCENE_SELECT; }   /* RHYTHM GAME */
    play_sound(u->as, SND_CANCEL);                                     /* PV / OPTIONS / SHOP: not ported */
    u->menu_flash = 20;
    u->menu_focus = i;
    return -1;
}

int ui_event(UiState *u, const SDL_Event *e, int *song, int *diff) {
    bool click = e->type == SDL_MOUSEBUTTONUP && e->button.button == SDL_BUTTON_LEFT;
    bool down = e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT;
    bool key = e->type == SDL_KEYDOWN && !e->key.repeat;
    SDL_Keycode k = key ? e->key.keysym.sym : 0;
    float mx = (click || down) ? (float)e->button.x : 0, my = (click || down) ? (float)e->button.y : 0;
    bool ok_key = key && (k == SDLK_RETURN || k == SDLK_SPACE || k == SDLK_KP_ENTER);
    switch (u->scene) {
    case SCENE_TITLE:
        if (u->tstate <= 6) {                       /* logos: a tap skips to the title (orig.: only on CRI) */
            if (click || ok_key || (key && k == SDLK_ESCAPE)) { u->tstate = 7; u->tcount = 0; }
            break;
        }
        if (u->tstate == 9) {
            if (key && k == SDLK_ESCAPE) return SCENE_QUIT;
            if (click || ok_key) title_start_menu(u);
            break;
        }
        if (u->tstate == 10) {
            if (key && (k == SDLK_ESCAPE || k == SDLK_BACKSPACE)) { u->tstate = 9; play_sound(u->as, SND_CANCEL); break; }
            if (key && k == SDLK_UP) u->menu_focus = (u->menu_focus + 3) % 4;
            if (key && k == SDLK_DOWN) u->menu_focus = (u->menu_focus + 1) % 4;
            if (ok_key) return menu_activate(u, u->menu_focus);
            if (down) { u->menu_press = menu_hit(u, mx, my); if (u->menu_press >= 0) u->menu_focus = u->menu_press; }
            if (click) {
                int h = menu_hit(u, mx, my), p = u->menu_press;
                u->menu_press = -1;
                if (h >= 0 && h == p) return menu_activate(u, h);
            }
        }
        break;
    case SCENE_SELECT:
        if (key) {
            if (k == SDLK_ESCAPE || k == SDLK_BACKSPACE) { stop_preview(u); play_sound(u->as, SND_CANCEL); return SCENE_TITLE; }
            if (k == SDLK_LEFT) select_move(u, -1, 0);
            if (k == SDLK_RIGHT) select_move(u, 1, 0);
            if (k == SDLK_UP) select_move(u, 0, -1);
            if (k == SDLK_DOWN) select_move(u, 0, 1);
            if (ok_key) return start_game(u, song, diff);
        }
        if (down) {
            u->drag_x0 = mx;
            u->dragging = my > 250 && my < 590;
            u->play_pressed = my > 870 && mx > 165 && mx < 475;
        }
        if (click) {
            bool was_play = u->play_pressed;
            u->play_pressed = false;
            if (u->dragging && fabsf(mx - u->drag_x0) > 50) {          /* swipe the cover flow */
                select_move(u, mx < u->drag_x0 ? 1 : -1, 0);
                u->dragging = false;
                break;
            }
            u->dragging = false;
            if (my > 250 && my < 590 && mx < 160) select_move(u, -1, 0);
            else if (my > 250 && my < 590 && mx > 480) select_move(u, 1, 0);
            else if (my > 694 && my < 764 && mx > 118 && mx < 522) {       /* MODE CHANGE: Break the Limit */
                u->diff = u->diff == DIFF_BREAK ? 3 : DIFF_BREAK;
                play_sound(u->as, SND_DECIDE);
            } else if (my > 777 && my < 843 && u->diff != DIFF_BREAK) {     /* difficulty row */
                int d = (int)(mx / 155);
                if (d > 3) d = 3;
                if (d != u->diff) { u->diff = d; play_sound(u->as, SND_DECIDE); }
            } else if (was_play && my > 870 && mx > 165 && mx < 475) return start_game(u, song, diff);
            else if (my > 880 && mx < 160) { stop_preview(u); play_sound(u->as, SND_CANCEL); return SCENE_TITLE; }
        }
        break;
    case SCENE_RESULT:
        if ((click || ok_key || (key && k == SDLK_ESCAPE)) && SDL_GetTicks() - u->enter_ms > 500) {
            stop_bgm(u);
            return SCENE_SELECT;
        }
        break;
    }
    return -1;
}

int ui_update(UiState *u, int *song, int *diff) {
    (void)song; (void)diff;
    Uint32 now = SDL_GetTicks();
    u->tick_acc += (double)(now - u->last_ms);
    u->last_ms = now;
    if (u->tick_acc > 500) u->tick_acc = 500;          /* don't fast-forward after a stall */
    while (u->tick_acc >= 1000.0 / 30.0) {
        u->tick_acc -= 1000.0 / 30.0;
        u->ticks++;
        if (u->scene == SCENE_TITLE) title_tick(u);
        else if (u->scene == SCENE_SELECT) select_tick(u);
    }
    if (u->scene == SCENE_SELECT && u->preview_song != u->song && now - u->song_changed_ms > 350) {
        stop_preview(u);
        char path[1024];
        snprintf(path, sizeof path, "%s/%s.adx", u->as->appdir, u->gd->songs[u->song].preview);
        if (media_decode_audio(path, &u->preview, 1) >= 1) u->preview_voice = mixer_sfx_play(&u->preview, 0.9f, true);
        u->preview_song = u->song;
    }
    return -1;
}

/* ---------------------------------------------------------------- render */

/* logos: 640x960 image in the top-left of a 1024^2 texture, drawn at (0,0) on white */
static void render_logos(UiState *u) {
    SDL_SetRenderDrawColor(u->ren, 255, 255, 255, 255);
    SDL_RenderClear(u->ren);
    int idx = u->tstate <= 1 ? 0 : u->tstate <= 4 ? 1 : 2;
    const Atlas *lg = &u->as->logos[idx];
    if (lg->tex) {
        SDL_Rect src = {0, 0, LOGICAL_W, LOGICAL_H};
        spr_tl_rect(u->ren, lg, &src, 0, 0, LOGICAL_W, LOGICAL_H, 255);
    }
}

static void render_white_fade(UiState *u, float a) {
    if (a <= 0) return;
    SDL_SetRenderDrawBlendMode(u->ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(u->ren, 255, 255, 255, (Uint8)(255 * fminf(1.0f, a)));
    SDL_RenderFillRect(u->ren, NULL);
}

/* WindowTitle RenderTitle @0x51678 / Render @0x512a8 (top-left positions, back to front by z) */
static void render_title(UiState *u) {
    SDL_Renderer *r = u->ren;
    if (u->tstate <= 7) { render_logos(u); render_white_fade(u, u->fade); return; }
    const Atlas *u5 = &u->as->ui5, *u1 = &u->as->ui1;
    spr_tl(r, &u->as->bg_title, "bg_title", 0, 0, 255);
    /* RenderTitle: bob = 5*sin(BlinkTime*113.04) (radians, aliases to a ~3.7 s cycle); eyeAction: blink every 200 ticks */
    float bob = (float)(5.0 * sin((double)u->blink * 113.04));
    spr_tl(r, u5, "miku", -10, bob - 3, 255);
    { unsigned b = u->blink % 200; if (u->tstate >= 9 && b >= 1 && b < 5) spr_tl(r, u5, b < 3 ? "eye_00" : "eye_01", 320, bob + 109, 255); }
    spr_tl(r, u5, "outerkey_b", 63, 411, 255);
    spr_tl(r, u5, "outerkey_p", 535, 148, 255);
    spr_tl(r, u5, "outerkey_r", 434, 533, 255);
    spr_tl(r, u5, "outerkey_t", 115, 76, 255);
    spr_tl(r, u5, "cubes_p", 472, 103, 255);
    spr_tl(r, u5, "cubes_y", 0, 48, 255);
    spr_tl(r, u5, "titlelogo_00", 12, 242, 255);
    spr_tl(r, u5, "titlelogo_01", 320, 242, 255);
    spr_tl(r, u5, "copyright", 99, 922, 255);
    if (u->tstate == 9) {
        float blink = 0.5f + 0.5f * sinf((float)u->blink * 0.4f * 0.333f);
        spr_tl(r, u1, "touchstart", 172, 662, (Uint8)(90 + 165 * blink));
    }
    if (u->tstate == 10) {
        /* TouchTitleMenus Render @0x6abb4: base, face (off/on), label, brackets on the focused button */
        for (int i = 0; i < 4; i++) {
            float x = u->menu_x[i], y = 538.0f + 82.0f * i;
            bool pressed = u->menu_press == i;
            spr_tl(r, u1, "menubutton_l_base", x, y, 255);
            spr_tl(r, u1, pressed ? "menubutton_l_on" : "menubutton_l_off", x + 7, y + 7, 255);
            SDL_Color col = WHITE;
            if (i > 0) col = (SDL_Color){200, 205, 215, 255};          /* not ported: dimmed label */
            if (u->menu_flash > 0 && i == u->menu_focus && i > 0) col = (SDL_Color){255, 150, 150, 255};
            font_draw(r, k_menu_label[i], x + 197, y + 25, 3.4f, col, 0);
            if (i == u->menu_focus) {
                spr_tl(r, u1, "parenthesis_00", x + 8, y + 7, 255);
                spr_tl(r, u1, "parenthesis_01", x + 394 - 8 - 39, y + 7, 255);
            }
        }
        spr_tl(r, u1, "crownicon_off", 14, 878, 255);
        spr_tl(r, u1, "infoicon_off", 559, 878, 255);
    }
    render_white_fade(u, u->fade);
}

/* textured quad (cover flow) via SDL_RenderGeometry */
static void draw_quad(SDL_Renderer *r, SDL_Texture *tex, const SDL_Rect *src, int tw, int th,
                      const float x[4], const float y[4], const float v[4], const Uint8 a[4]) {
    SDL_Vertex vt[4];
    float u0 = (float)src->x / tw, u1 = (float)(src->x + src->w) / tw;
    float uu[4] = {u0, u1, u1, u0};
    for (int i = 0; i < 4; i++) {
        vt[i].position = (SDL_FPoint){x[i], y[i]};
        vt[i].color = (SDL_Color){255, 255, 255, a[i]};
        vt[i].tex_coord = (SDL_FPoint){uu[i], ((float)src->y + v[i] * src->h) / th};
    }
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(r, tex, vt, 4, idx, 6);
}

/* WindowCover Render @0x36290: 316x316 quad around (pos.x - scroll, 420) with stretched edges,
   plus a 120-px reflection underneath */
static void render_cover(UiState *u, int i) {
    Cover *c = &u->cov[i];
    Atlas *m = music_atlas(u, i);
    const Frame *f = music_frame(u, i, "art");
    if (!m || !f || !m->tex) return;
    float cx = c->x - u->scr, cy = 420.0f, h = 158.0f;
    if (cx < -200 || cx > LOGICAL_W + 200) return;
    float xl = cx - h + c->lx, xr = cx + h + c->rx;
    float lt = cy - h + c->ly, lb = cy + h - c->ly, rt = cy - h + c->ry, rb = cy + h - c->ry;
    float X[4] = {xl, xr, xr, xl}, Y[4] = {lt, rt, rb, lb}, V[4] = {0, 0, 1, 1};
    Uint8 A[4] = {255, 255, 255, 255};
    draw_quad(u->ren, m->tex, &f->r, m->w, m->h, X, Y, V, A);
    /* reflection: flipped, fading out over 120 px */
    float lr = (lb - lt) * 120.0f / 316.0f, rr = (rb - rt) * 120.0f / 316.0f;
    float X2[4] = {xl, xr, xr, xl}, Y2[4] = {lb, rb, rb + rr, lb + lr}, V2[4] = {1, 1, 1 - 120.0f / 316.0f, 1 - 120.0f / 316.0f};
    Uint8 A2[4] = {79, 79, 0, 0};
    draw_quad(u->ren, m->tex, &f->r, m->w, m->h, X2, Y2, V2, A2);
}

/*
 * WindowMusicSelect Render @0x41a08 + children (WindowCover, TouchMusicSelect, WindowBGEffect).
 * bg (0,0); additive cubes; cover flow; logo; stars; "HIGH SCORE:/BPM:" at y=660; MODE CHANGE;
 * difficulty labels (155-px pitch, centre y=810) with the gliding difficultycursor at (CsrX,777);
 * baseline (0,824); PLAY button (menubutton_m) with brackets at x=165 / +271, y=874.
 */
static void render_select(UiState *u) {
    SDL_Renderer *r = u->ren;
    const Atlas *u1 = &u->as->ui1;
    const SongInfo *si = &u->gd->songs[u->song];
    spr_tl(r, &u->as->bg_select, "bg_misicselect", 0, 0, 255);

    /* EffectBGEffect render: RenderTexRot, additive */
    SDL_SetTextureBlendMode(u1->tex, SDL_BLENDMODE_ADD);
    for (int i = 0; i < MAX_BGFX; i++) {
        BgFx *b = &u->bg[i];
        if (!b->on) continue;
        char name[32];
        snprintf(name, sizeof name, "bgeffect_%02d", b->frame);
        spr_rot(r, u1, name, b->x, b->y, b->ang * 57.29578f, 1.0f, 160);
    }
    SDL_SetTextureBlendMode(u1->tex, SDL_BLENDMODE_BLEND);

    /* cover flow, far covers first */
    int n = u->gd->nsongs;
    for (int d = n; d >= 1; d--) {
        if (u->song - d >= 0) render_cover(u, u->song - d);
        if (u->song + d < n) render_cover(u, u->song + d);
    }
    render_cover(u, u->song);

    /* logo cross-fade (WindowCover renderLogo) */
    if (u->prev_logo >= 0 && u->logo_fade < 1.0f) {
        Atlas *pm = music_atlas(u, u->prev_logo);
        const Frame *pf = music_frame(u, u->prev_logo, "logo");
        if (pm && pf) spr_tl_rect(r, pm, &pf->r, 128, 58, 384, 192, (Uint8)(255 * (1.0f - u->logo_fade)));
    }
    {
        Atlas *lm = music_atlas(u, u->song);
        const Frame *lf = music_frame(u, u->song, "logo");
        if (lm && lf) spr_tl_rect(r, lm, &lf->r, 128, 58, 384, 192, (Uint8)(255 * u->logo_fade));
    }

    char buf[160];
    int stars = u->diff < 4 ? si->stars[u->diff] : 9;   /* Break the Limit is always 9 */
    for (int s = 0; s < 10; s++)
        spr_tl(r, u1, s < stars ? "difficultystar_01" : "difficultystar_00", 320 - 5 * 34 + s * 34, 600, 255);
    const Record *rec = &u->save->rec[u->song][u->diff];
    snprintf(buf, sizeof buf, "HIGH SCORE:%d  BPM:%d", rec->score, si->bpm);
    font_draw(r, buf, 320, 660, 2.6f, WHITE, 0);

    spr_tl(r, u1, "modechange_off", 118, 694, 255);

    if (u->diff == DIFF_BREAK) {
        spr_tl(r, u1, "difficultycursor_break", 185, 777, 255);
        spr_tl(r, u1, "difficulty_on_04", 206, 786, 255);
    } else {
        for (int d = 0; d < 4; d++) {
            char name[32];
            snprintf(name, sizeof name, "difficulty_%s_%02d", d == u->diff ? "on" : "off", d);
            spr_tl(r, u1, name, 13 + 155 * d, 786, 255);
        }
        spr_tl(r, u1, "difficultycursor", u->csr_x, 777, 255);
    }
    spr_tl(r, u1, "baseline", 0, 824, 255);

    spr_tl_wh(r, u1, "sgnbutton_off", 14, 890, 120, 62, 255);   /* small back button (help_01 panel 2) */
    spr_tl(r, u1, "back", 46, 902, 255);
    /* TouchMusicSelect Render @0x6d510: PLAY, 1.05x when pressed, pulsing label, brackets */
    float s = u->play_pressed ? 1.05f : 1.0f;
    const Frame *pb = atlas_find(u1, "menubutton_m_off");
    float pw = pb ? pb->r.w * s : 310, ph = pb ? pb->r.h * s : 68;
    spr_tl_wh(r, u1, u->play_pressed ? "menubutton_m_on" : "menubutton_m_off", 320 - pw / 2, 920 - ph / 2, pw, ph, 255);
    float glow = 0.5f + 0.5f * sinf((float)u->pulse * 6.2831853f / 22.0f);
    SDL_Color pc = {(Uint8)(200 + 55 * glow), 255, 255, 255};
    font_draw(r, "PLAY", 320, 908, 3.4f * s, pc, 0);
    spr_tl(r, u1, "parenthesis_00", 165, 886, 255);
    spr_tl(r, u1, "parenthesis_01", 165 + 271, 886, 255);
}

/* draw "art"/"logo" frame of a song top-left at (x,y) stretched to w x h */
static void draw_music_frame_tl(UiState *u, int song, const char *kind, float x, float y, float w, float h) {
    Atlas *m = music_atlas(u, song);
    Atlas *ref = load_music_slot(u, 1, "music_00_01");
    if (!m || !ref) return;
    char name[64];
    snprintf(name, sizeof name, "%s_00_01_%02d", kind, song % 3);
    const Frame *f = atlas_find(ref, name);
    if (f) spr_tl_rect(u->ren, m, &f->r, x, y, w, h, 255);
}

/*
 * WindowResult Render @0x4e8a8 / RenderScore @0x4dbdc (positions top-left):
 * bg (0,0); jacket 142x142; logo (279,67) 256x128; titlebar (104,238) with the difficulty;
 * rows y = 308 Total Notes, 346 Combo, 394..547 Cool..Worst, 597 interlude, 644 Stage Score,
 * 682 Combo Bonus, 720 Total Score; clearrankstamp (349,224); perfect (383,291).
 * Text is drawn by TexTextManager (system font) in the original; here the bitmap font.
 */
static void render_result(UiState *u) {
    SDL_Renderer *r = u->ren;
    const GameResult *res = &u->res;
    const Atlas *u2 = &u->as->ui2;
    spr_tl(r, &u->as->bg_result, "bg_result", 0, 0, 255);
    draw_music_frame_tl(u, res->song, "art", 46, 70, 142, 142);
    draw_music_frame_tl(u, res->song, "logo", 279, 67, 256, 128);

    /* RenderBgRect: translucent stats panel */
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 20, 40, 50, 150);
    SDL_FRect panel = {80, 228, 460, 562};
    SDL_RenderFillRectF(r, &panel);

    spr_tl(r, u2, "titlebar", 104, 238, 255);
    const Frame *tb = atlas_find(u2, "titlebar");
    float tbw = tb ? (float)tb->r.w : 432, tbh = tb ? (float)tb->r.h : 40;
    static const char *diffname[NUM_DIFF] = {"Easy", "Normal", "Hard", "Extreme", "Break the Limit"};
    font_draw(r, diffname[res->diff], 104 + tbw / 2, 238 + tbh / 2 - 9, 2.5f, WHITE, 0);

    struct { const char *label; int value; float lx, vx, y; } rows[] = {
        {"Total Notes", res->total_notes, 124, 350, 308},
        {"Combo", res->max_combo, 124, 350, 346},
        {"Cool", res->counts[5], 206, 460, 394},
        {"Fine", res->counts[4], 206, 460, 432},
        {"Safe", res->counts[3], 206, 460, 470},
        {"Sad", res->counts[2], 206, 460, 508},
        {"Worst", res->counts[1], 206, 460, 547},
        {"*", res->interlude_hits, 206, 460, 597},
        {"Stage Score", res->stage_score, 131, 530, 644},
        {"Combo Bonus", res->combo_score, 131, 530, 682},
        {"Total Score", res->stage_score + res->combo_score, 132, 530, 720},
    };
    char buf[32];
    for (size_t i = 0; i < sizeof rows / sizeof *rows; i++) {
        /* RenderUnderLine: textbar_00 under the upper block, textbar_01 under the score block */
        const char *bar = rows[i].y < 600 ? "textbar_00" : "textbar_01";
        spr_tl_wh(r, u2, bar, rows[i].lx - 10, rows[i].y + 26, rows[i].vx - rows[i].lx + 20, 4, 200);
        if (rows[i].label[0] == '*') spr_tl(r, &u->as->game1, "quaver_00", rows[i].lx - 6, rows[i].y - 26, 255);
        else font_draw(r, rows[i].label, rows[i].lx, rows[i].y, 2.6f, WHITE, -1);
        snprintf(buf, sizeof buf, "%d", rows[i].value);
        font_draw(r, buf, rows[i].vx, rows[i].y, 2.6f, WHITE, 1);
    }

    if (res->rank >= 0) {
        spr_tl(r, u2, "clearrankstamp", 349, 224, 255);
        char name[32];
        snprintf(name, sizeof name, "clearrank_%02d", res->rank > 0 ? res->rank - 1 : 0);
        const Frame *rk = atlas_find(u2, name);
        if (rk) spr_tl(r, u2, name, 349 + (210 - rk->r.w) / 2.0f, 224 + (235 - rk->r.h) / 2.0f - 10, 255);
        if (res->rank == 0) spr_tl(r, u2, "perfect", 383, 291, 255);
        if (res->game_over) {
            const Frame *ff = atlas_find(u2, "failed");
            if (ff) spr_tl(r, u2, "failed", 454 - ff->r.w / 2.0f, 341 - ff->r.h / 2.0f, 255);
        }
    }
    if (u->new_record) spr_tl(r, u2, "newrecord", 440, 300, 255);
}

void ui_render(UiState *u) {
    SDL_SetRenderDrawColor(u->ren, 0, 0, 0, 255);
    SDL_RenderClear(u->ren);
    switch (u->scene) {
    case SCENE_TITLE: render_title(u); break;
    case SCENE_SELECT: render_select(u); break;
    case SCENE_RESULT: render_result(u); break;
    default: break;
    }
}
