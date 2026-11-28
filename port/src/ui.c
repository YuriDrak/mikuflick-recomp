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

#define HOLD_MIN_MS 100   /* port: shortest time a clicked button shows its held look (3 ticks) */
#define MAX_BGFX 128  /* ~50 alive at steady state with the double exec */

/* EffectBGEffect: additive cube drifting up across the select screen */
typedef struct { bool on; int frame; float x, y, vx, vy, sx, sy, ang, angv; } BgFx;

#define MAX_TITLEFX 192
#define MUSIC_CACHE 12           /* art sheets kept loaded (4 built-in + 1 per DLC pack, 1024x1024 each) */

/* EffectTitleFlower: additive key sprite thrown out from Miku's hands on the title screen */
typedef struct { bool on; int tex; unsigned timer; float x, y, vx, vy, sx, sy, ang, angv; } TitleFx;

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
    int menu_focus, menu_press;
    /* select */
    int song, diff;
    Atlas music[MUSIC_CACHE];    /* song art sheets (music_NN_0M), least recently used evicted */
    char music_name[MUSIC_CACHE][64];   /* "" = free slot */
    bool music_ok[MUSIC_CACHE];  /* false: load failed (kept so it is not retried every frame) */
    unsigned music_used[MUSIC_CACHE], music_clock;
    Cover cov[MAX_SONGS];
    /* WindowBase scroll state (x only: the y limits are equal, so the y factor is 0) */
    float scr;                   /* m_ScrPos.x */
    float scr_add;               /* m_ScrAdd.x: momentum, /1.5 per tick */
    float scr_start;             /* m_ScrStartPos.x */
    float sw_st, sw_ed;          /* m_ScrSwipStPos.x / m_ScrSwipEdPos.x */
    bool swiping, last_swiping;  /* m_IsScrSwipe / m_LastSwipeFlag */
    float spr_pos, spr_tgt;      /* CloudSimpleSpring position / target */
    int snd_wait;                /* WindowMusicSelect m_FadeSoundWait */
    float csr_x, csr_spd;        /* m_CsrX / m_CsrSpdX difficulty cursor */
    unsigned pulse;              /* TouchMusicSelect m_Counter */
    BgFx bg[MAX_BGFX];
    unsigned bg_timer;           /* WindowBGEffect m_Timer */
    TitleFx tfx[MAX_TITLEFX];
    unsigned tfx_timer;          /* WindowTitleFlower m_Timer */
    bool play_pressed, back_pressed, mode_pressed;
    /* TouchBase touchDown/touchMoved: where the current touch went down and how far it has moved */
    float dn_x, dn_y, dn_mx, dn_my;
    bool dn_on;
    Uint32 dn_ms;                /* SDL ticks of the last press */
    /* port: a quick click keeps the held looks it had at release until dn_ms + HOLD_MIN_MS (see ui_event) */
    Uint32 lg_until;
    bool lg_play, lg_back, lg_mode;
    int lg_menu, lg_opt;
    int opt_press;               /* OPTIONS main list: the TouchCommonButton held, -1 none */
    bool menu_kbd;              /* title menu focus brackets: only after arrow-key navigation */
    Pcm preview;
    int preview_song;
    int preview_voice;
    int bgm_voice;
    /* options */
    int opt_mode, opt_row, help_page;
    unsigned credits_t;
    /* DLC page */
    int dlc_page;                /* pack list page */
    char dlc_note[160];          /* page-local message (clipboard) */
    /* PV viewer */
    int pv_focus;
    bool pv_lyrics, pv_karaoke, pv_loop, pv_shuffle;
    /* AUDIO page: volume meter being dragged (0 BGM, 1 SE, -1 none) and the last pointer x */
    int vol_drag;
    float vol_x;
    /* result (WindowResult m_State / m_ModeCnt / m_NowCounterIndex, WindowCounter) */
    GameResult res;
    bool new_record;
    int res_state, res_cnt, res_idx;
    int res_val[11];             /* counter values in CheckCounter order */
    int res_ctr[11];             /* ticks since each counter was activated */
    float res_fail_rate, res_rec_rate;   /* m_FailSizeRate / m_HighScoreSizeRate */
    int res_roll_voice;          /* looped SE04v2 while a counter rolls */
    FlickRes fres;               /* FLICK RESULT page (flickresult.c) */
};

static const SDL_Color WHITE = {255, 255, 255, 255};

/* sound table indices (Sound.plist order, see docs/re_notes.md): the menus only use these */
enum {
    SE_BUTTON = 19,   /* SE02_01: every Touch* touchUp released inside the button */
    SE_BACK = 8,      /* SE09: TouchBack touchUp @0x6ee74 */
    SE_COVER = 15,    /* MusicSelect: WindowMusicSelect updateTitle:: @0x401da */
    SE_RECORD = 21,   /* SE05_01: WindowResult setHighScoreMode */
    SE_COUNT = 29,    /* SE04v2 (loop): WindowCounter Active @0x35db0 */
};

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
    u->pv_lyrics = false;    /* StatusData init zeroes every m_PVModeFlg: all PV toggles start off */
    u->pv_focus = 1;
    u->menu_focus = 0;
    u->menu_press = -1;
    u->opt_press = -1;
    u->vol_drag = -1;
    u->res_roll_voice = -1;
    return u;
}

static void stop_roll(UiState *u) {
    if (u->res_roll_voice >= 0) mixer_sfx_stop(u->res_roll_voice);
    u->res_roll_voice = -1;
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

/* Menu BGM (looped Sound.plist entry, replacing whatever BGM plays), on the mixer's BGM master
   (Options.bgm_volume) like the original's PlayBGM */
static void play_bgm(UiState *u, int id) {
    stop_bgm(u);
    u->bgm_voice = mixer_bgm_play(&u->as->sounds[id], 1.0f, true);
}

void ui_destroy(UiState *u) {
    if (!u) return;
    stop_preview(u);
    for (int i = 0; i < MUSIC_CACHE; i++) if (u->music_ok[i]) atlas_free(&u->music[i]);
    free(u);
}

#define MUSIC_REF "music_00_01"   /* the only art sheet with a .plist; never evicted */

void ui_songs_changed(UiState *u) {
    stop_preview(u);
    for (int i = 0; i < MUSIC_CACHE; i++) {
        if (u->music_ok[i]) atlas_free(&u->music[i]);
        u->music_ok[i] = false;
        u->music_name[i][0] = 0;
    }
    u->song = 0;
    u->preview_song = -1;
}

/* art sheet by name (music_00_01..04 built-in, music_NN_01 per DLC pack). With 70+ songs the
   sheets no longer all fit, so the least recently drawn one is replaced. */
static Atlas *music_sheet(UiState *u, const char *name) {
    int slot = -1, lru = -1;
    for (int i = 0; i < MUSIC_CACHE; i++) {
        if (!strcmp(u->music_name[i], name)) {
            u->music_used[i] = ++u->music_clock;
            return u->music_ok[i] ? &u->music[i] : NULL;
        }
        if (!u->music_name[i][0]) { if (slot < 0) slot = i; }
        else if (strcmp(u->music_name[i], MUSIC_REF) && (lru < 0 || u->music_used[i] < u->music_used[lru])) lru = i;
    }
    if (slot < 0) slot = lru;
    if (slot < 0) return NULL;
    if (u->music_ok[slot]) atlas_free(&u->music[slot]);
    snprintf(u->music_name[slot], sizeof u->music_name[slot], "%s", name);
    u->music_ok[slot] = atlas_load(&u->music[slot], u->ren, u->as->appdir, name);
    if (!u->music_ok[slot]) log_msg("song art %s not found", name);
    u->music_used[slot] = ++u->music_clock;
    return u->music_ok[slot] ? &u->music[slot] : NULL;
}

static Atlas *music_atlas(UiState *u, int song) {
    return music_sheet(u, u->gd->songs[song].art_atlas);
}

/* Song art/logo: only music_00_01 ships a .plist; the other sheets (music_00_02..04 and the DLC
   music_NN_01) share its layout, so frame rects come from music_00_01. */
static const Frame *music_frame(UiState *u, int song, const char *kind) {
    Atlas *ref = music_sheet(u, MUSIC_REF);
    if (!ref) return NULL;
    char name[64];
    snprintf(name, sizeof name, "%s_00_01_%02d", kind, u->gd->songs[song].art_index); /* SetArtWorkIndex */
    return atlas_find(ref, name);
}

/* songs in the cover flow: the DLC ones only while SHOW DLC SONGS is on */
static int nvis(const UiState *u) { return visible_songs(u->gd, u->opt); }

/* ------------------------------------------------------------ select: cover flow */

/* WindowMusicSelect updateTitle:: @0x4017c + WindowCover setDirection:: @0x36758 */
static void covers_retarget(UiState *u, int timer) {
    float x = 280.0f;
    for (int i = 0; i < nvis(u); i++) {
        Cover *c = &u->cov[i];
        c->tx = (i == 0 && u->song == 0) ? x + 40.0f : x;   /* original special-cases cover 0 when selected */
        x += (i == u->song || i == u->song - 1) ? 80.0f : 40.0f;
        if (i == u->song) { c->tlx = c->tly = c->trx = c->try_ = 0; }           /* dir 0: facing */
        else if (i < u->song) { c->tlx = 0; c->tly = 10; c->trx = -230; c->try_ = 60; }  /* dir 1 */
        else { c->tlx = 230; c->tly = 60; c->trx = 0; c->try_ = 10; }              /* dir 2 */
        c->timer = timer;
    }
}

/* WindowMusicSelect setDifficulty:: @0x3fd3c: difficultycursor x targets (table @0x13bfb0, all four
   difficulties open); the cursor is drawn at y 777 (WindowMusicSelect Render @0x41dd8: movw 0x4000 / movt 0x4442) */
static float csr_target(int diff) {
    static const float k_x[4] = {7, 161, 315, 469};
    return k_x[diff >= 0 && diff < 4 ? diff : 1];
}

static void covers_snap(UiState *u) {
    covers_retarget(u, 0);
    for (int i = 0; i < nvis(u); i++) {
        Cover *c = &u->cov[i];
        c->x = c->tx; c->lx = c->tlx; c->ly = c->tly; c->rx = c->trx; c->ry = c->try_;
    }
    u->scr = u->spr_pos = u->spr_tgt = (float)u->song * 40.0f;
    u->scr_add = 0;
    u->swiping = u->last_swiping = false;
    u->csr_x = csr_target(u->diff);
    u->csr_spd = 0;
}

/* WindowMusicSelect setDifficulty:: @0x3fd62: the cursor starts at a third of the distance per tick and
   slows by 0.9 each tick (exec @0x413f6), stopping on the target */
static void set_difficulty(UiState *u, int d) {
    u->diff = d;
    /* Break the Limit draws its own wide cursor; m_CsrX stays where it was */
    u->csr_spd = d == DIFF_BREAK ? 0.0f : (csr_target(d) - u->csr_x) / 3.0f;
}

/* TouchHelper CoordinateColor mode 0 @0x6c7e0: the button faces pulse with colour = alpha = v, v going
   0.85 * 0.98^i down for i = 0..10 and back up (22 steps); m_Counter wraps after 21 and only advances
   when >= 51 ms have passed since its last step (TouchTitleMenus / TouchMusicSelect exec), i.e. every
   second 30 Hz tick: callers pass ticks / 2 */
static Uint8 pulse_level(unsigned counter) {
    int i = (int)(counter % 22);
    if (i > 10) i = 21 - i;
    return (Uint8)(255.0f * 0.85f * powf(0.98f, (float)i) + 0.5f);
}

/* scale a rect by s about its own centre (TouchHelper CoordinateScalingPoints) */
static void scale_rect(float s, float *x, float *y, float *w, float *h) {
    *x -= *w * (s - 1) * 0.5f; *y -= *h * (s - 1) * 0.5f; *w *= s; *h *= s;
}

static void spawn_bgfx(UiState *u) {
    for (int i = 0; i < MAX_BGFX; i++) {
        BgFx *b = &u->bg[i];
        if (b->on) continue;
        /* EffectBGEffect initWithType:: @0xbff4 */
        b->on = true;
        b->frame = 3 + rand() % 2;   /* pattern {7, 8} -> bgeffect_03 / bgeffect_04 */
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

/* one WindowBGEffect exec: 3 new cubes whenever ++m_Timer is a multiple of 100, then move and rotate.
   WindowBGEffect is a WindowBase child window, so it is exec'd twice per frame on the music select,
   PV select and options screens (WindowManager AddObject @0x33210 plus the parent's [super exec]
   child loop); WindowResult does not call [super exec], so it runs once there. */
static void bgfx_exec(UiState *u) {
    if (++u->bg_timer % 100 == 0) for (int k = 0; k < 3; k++) spawn_bgfx(u);
    for (int i = 0; i < MAX_BGFX; i++) {
        BgFx *b = &u->bg[i];
        if (!b->on) continue;
        b->x += b->vx * b->sx;
        b->y += b->vy * b->sy;
        b->ang += 0.005984f * b->angv; /* _RotSpeed */
        if (b->x < -50 || b->x > LOGICAL_W + 50 || b->y < -50) b->on = false;
    }
}

/* fresh cube field for a scene: empty, as in the original (the first wave comes at exec 100) */
static void bgfx_reset(UiState *u) {
    memset(u->bg, 0, sizeof u->bg);
    u->bg_timer = 0;
}

/* EffectBGEffect render: RenderTexRot, additive, colour GetColor(0.1, 1, 1, 1) (alpha 0.1 = 26/255),
   drawn at half size (half width/height passed to the same call the title flowers use) */
static void render_bgfx(UiState *u) {
    const Atlas *u1 = &u->as->ui1;
    SDL_SetTextureBlendMode(u1->tex, SDL_BLENDMODE_ADD);
    for (int i = 0; i < MAX_BGFX; i++) {
        BgFx *b = &u->bg[i];
        if (!b->on) continue;
        char name[32];
        snprintf(name, sizeof name, "bgeffect_%02d", b->frame);
        spr_rot(u->ren, u1, name, b->x, b->y, b->ang * 57.29578f, 0.5f, 26);
    }
    SDL_SetTextureBlendMode(u1->tex, SDL_BLENDMODE_BLEND);
}

/* WindowMusicSelect updateTitle:: @0x4017c: new centre cover. SE 15, the preview stops at once and a
   new one starts after m_FadeSoundWait = 10 ticks; a running wait is not restarted, so scrolling on
   plays whatever is centred when it runs out. */
static void update_title(UiState *u, int idx) {
    u->song = idx;
    covers_retarget(u, 35);
    play_sound(u->as, SE_COVER);
    stop_preview(u);
    if (u->snd_wait == 0) u->snd_wait = 10;
}

/* WindowMusicSelect updateSound @0x400b4: play the centred song's preview when the wait runs out, at
   the BGM volume, looped, without a fade */
static void update_sound(UiState *u) {
    if (u->snd_wait <= 0 || --u->snd_wait > 0) return;
    stop_preview(u);
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.adx", u->as->appdir, u->gd->songs[u->song].preview);
    if (media_decode_audio(path, &u->preview, 1) >= 1) u->preview_voice = mixer_bgm_play(&u->preview, 1.0f, true);
    u->preview_song = u->song;
}

/* WindowBase ScrollSetTouchArea:::: min (0,140) max (640,400) (r2,r3 / [sp],[sp+4] @0x34174), set in
   WindowMusicSelect loadTexture @0x41128 and WindowPVSelect loadTexture @0x45fea. ScrollTouchDown/Moved
   compare it with the raw touch, which is in UIKit points (320x480: locationInView + CloudMath::AdjustPoint
   @0x578a8, which only halves on iPad HD; every TouchBase collision rect is pos * 0.5 / frame size / 2,
   e.g. TouchBack init @0x6deb4, TouchSoundVolume @0x76f72). In the 640x960 scene the band is y 280..800
   across the whole width: the jackets (y 293..609), their reflection, the strip below and the top of the
   difficulty labels. The port passes touches here in points (logical px * 0.5). */
static bool in_scroll_area(float x, float y) { return x >= 0 && x <= 640 && y >= 140 && y <= 400; }

/* WindowBase ScrollTouchDown: / ScrollTouchMoved: / ScrollTouchUp: @0x344c8 / 0x345a4 / 0x3469c;
   sw_st / sw_ed are in points like the original's m_ScrSwipStPos / m_ScrSwipEdPos */
static void scroll_touch(UiState *u, const SDL_Event *e) {
    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
        float x = (float)e->button.x * 0.5f, y = (float)e->button.y * 0.5f;
        if (!in_scroll_area(x, y)) { u->swiping = false; return; }
        u->swiping = true;
        u->scr_start = u->scr;
        u->sw_st = u->sw_ed = x;
    } else if (e->type == SDL_MOUSEMOTION && (e->motion.state & SDL_BUTTON_LMASK)) {
        float x = (float)e->motion.x * 0.5f, y = (float)e->motion.y * 0.5f;
        if (!in_scroll_area(x, y)) { u->swiping = false; return; }
        if (!u->swiping) { u->scr_start = u->scr; u->sw_st = x; }
        u->swiping = true;
        u->sw_ed = x;
    } else if ((e->type == SDL_MOUSEBUTTONUP && e->button.button == SDL_BUTTON_LEFT) ||
               (e->type == SDL_MOUSEMOTION && !(e->motion.state & SDL_BUTTON_LMASK))) {
        u->swiping = false;   /* port: a hover without the button also ends it (release lost outside the window) */
    }
}

/* one 30 Hz tick of WindowMusicSelect exec @0x41466..0x41880 (+ its WindowCover / WindowBGEffect
   children, which run twice per frame, see bgfx_exec) */
static void select_tick(UiState *u) {
    int n = nvis(u);   /* DLC songs only while SHOW DLC SONGS is on */
    /* CloudSimpleSpring::Compute first */
    u->spr_pos += (u->spr_tgt - u->spr_pos) * 0.5f;
    if (n >= 2) {
        const float lim_min = 0.0f, lim_max = (float)(n * 40);   /* loadTexture @0x41152 */
        if (u->swiping) {
            /* exec @0x41530: ScrPos = ScrStartPos + (St - Ed) * 1.0 (table @0x419fc) * 0.9 (@0x41788), touch in
               points, so 0.45 scroll px per scene px: about 89 px of drag per cover (40). No dead zone: the
               strip moves from the first motion. The centre cover changes during the drag. */
            float prev = u->scr;
            u->scr = u->scr_start + (u->sw_st - u->sw_ed) * 0.9f;
            u->scr_add = u->scr - prev;
            int idx = (int)((u->scr + 20.0f) / 40.0f);
            if (idx < 0) idx = 0;
            if (idx > n - 1) idx = n - 1;
            if (idx != u->song) update_title(u, idx);
        } else {
            if (u->last_swiping) {   /* released: aim the spring at the nearest cover */
                int idx = (int)((u->scr + 20.0f) / 40.0f);
                if (idx < 0) idx = 0;
                if (idx > n - 1) idx = n - 1;
                u->spr_tgt = (float)idx * 40.0f;
                u->spr_pos = u->scr;
            }
            float x = u->spr_pos + u->scr_add;   /* the release momentum carries on */
            if (x < lim_min) { x += (lim_min - x) / 1.5f; if (x >= lim_min) x = lim_min; }
            if (x > lim_max) { x += (x - lim_max) / -1.5f; if (x <= lim_max) x = lim_max; }
            u->scr = u->spr_pos = x;
        }
        u->scr_add /= 1.2f;   /* exec @0x417e0: Vector2D / 1.2 (0x3f99999a); ScrollExec's 1.5 is not used here */
        if (fabsf(u->scr_add) < 0.1f) u->scr_add = 0;
    }
    u->last_swiping = u->swiping;
    update_sound(u);
    /* difficulty cursor (exec @0x413f6) */
    if (u->csr_spd != 0.0f) {
        float tgt = csr_target(u->diff);
        u->csr_x += u->csr_spd;
        u->csr_spd *= 0.9f;
        if ((u->csr_spd > 0 && u->csr_x >= tgt) || (u->csr_spd < 0 && u->csr_x <= tgt)) { u->csr_x = tgt; u->csr_spd = 0; }
    }
    u->pulse++;
    /* WindowCover exec @0x36110 (twice per frame: WindowBase init -> AddObject, and AddChildWindow in
       loadTexture @0x40d24): 15 % toward the targets per exec while m_StretchTimer runs */
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < n; i++) {
            Cover *c = &u->cov[i];
            if (c->timer <= 0) continue;
            if (--c->timer == 0) { c->x = c->tx; c->lx = c->tlx; c->ly = c->tly; c->rx = c->trx; c->ry = c->try_; continue; }
            c->x += (c->tx - c->x) * 0.15f;
            c->lx += (c->tlx - c->lx) * 0.15f; c->ly += (c->tly - c->ly) * 0.15f;
            c->rx += (c->trx - c->rx) * 0.15f; c->ry += (c->try_ - c->ry) * 0.15f;
        }
    bgfx_exec(u);
    bgfx_exec(u);
}

/* ------------------------------------------------------------ title particles */

/* EffectTitleFlower (initWithType:: @0x2f240, exec @0x2f480, render @0x2f690), spawned by
   WindowTitleFlower exec @0x5222c: 12 at a time whenever its timer reaches a multiple of 100.
   In the title scene the sprite is titleeffect_{b,p,r,y}_0{0,1,2} (_pictIndex[2..13] @0x161de8);
   _rotSpeed @0x161e20 cycles 0.2992 / pi/10 / 0.3696. */
static const char *const k_titlefx_name[12] = {
    "titleeffect_b_00", "titleeffect_b_01", "titleeffect_b_02", "titleeffect_p_00", "titleeffect_p_01", "titleeffect_p_02",
    "titleeffect_r_00", "titleeffect_r_01", "titleeffect_r_02", "titleeffect_y_00", "titleeffect_y_01", "titleeffect_y_02"};
static const float k_titlefx_rot[3] = {0.2991993f, 0.3141593f, 0.3695991f};

static void spawn_titlefx(UiState *u) {
    for (int i = 0; i < MAX_TITLEFX; i++) {
        TitleFx *t = &u->tfx[i];
        if (t->on) continue;
        memset(t, 0, sizeof *t);
        t->on = true;
        /* start at one of Miku's hands (__MergedGlobals @0x164358 set by the initializer @0x2f814)
           and head away from the centre */
        bool right = rand() % 2;
        t->x = right ? 510.0f : 200.0f;
        t->y = right ? 385.0f : 370.0f;
        t->vx = (right ? 1.0f : -1.0f) * (float)(rand() % 100) / 100.0f;
        t->vy = (float)(rand() % 200) / 100.0f - 1.0f;
        t->sx = ((float)(rand() % 50) / 100.0f + 0.5f) * 3.0f;
        t->sy = ((float)(rand() % 50) / 100.0f + 0.5f) * 2.0f;
        t->ang = (float)(rand() % 6);
        t->angv = ((int)t->ang & 1) ? 1.0f : -1.0f;
        t->tex = rand() % 12;   /* title scene: Rand(12) + 2 into _pictIndex */
        return;
    }
}

static void titlefx_tick(UiState *u) {
    if (++u->tfx_timer % 100 == 0) for (int k = 0; k < 12; k++) spawn_titlefx(u);
    const float half = LOGICAL_H * 0.5f;
    for (int i = 0; i < MAX_TITLEFX; i++) {
        TitleFx *t = &u->tfx[i];
        if (!t->on) continue;
        t->timer++;
        /* sideways push away from the screen's horizontal midline, up to 2.5 px/tick at the edges */
        float dy = t->y < half ? half - t->y : t->y - half;
        float push = (t->y < half ? 1.0f : -1.0f) * (dy / half) * 2.5f;
        t->x += t->vx * t->sx + push;
        t->y += t->vy * t->sy;
        if (t->x < -20 || t->x > LOGICAL_W + 20 || t->y < -20 || t->y > LOGICAL_H + 20) { t->on = false; continue; }
        t->ang += k_titlefx_rot[(t->tex + 2) % 3] * t->angv * 0.08f;
        if (t->ang > 6.2831853f) t->ang = 0;
        else if (t->ang < 0) t->ang = 6.2831853f;
    }
}

/* additive, untinted, scale = timer/1000 clamped to 0.5..1.1, centred (RenderTexRot, blend 2) */
static void render_titlefx(UiState *u) {
    const Atlas *u1 = &u->as->ui1;
    SDL_SetTextureBlendMode(u1->tex, SDL_BLENDMODE_ADD);
    for (int i = 0; i < MAX_TITLEFX; i++) {
        const TitleFx *t = &u->tfx[i];
        if (!t->on) continue;
        float s = fminf(fmaxf((float)t->timer / 1000.0f, 0.5f), 1.1f);
        spr_rot(u->ren, u1, k_titlefx_name[t->tex], t->x, t->y, t->ang * 57.29578f, s, 255);
    }
    SDL_SetTextureBlendMode(u1->tex, SDL_BLENDMODE_BLEND);
}

/* ------------------------------------------------------------ title state machine */

static const char *k_menu_label[4] = {"RHYTHM GAME", "PV", "OPTIONS", "DLC"};   /* port: DLC page in the SHOP slot */

static void title_start_menu(UiState *u) {
    u->tstate = 10;
    for (int i = 0; i < 4; i++) u->menu_x[i] = (float)(LOGICAL_W + 128 + 20 * i); /* TouchTitleMenus init */
    u->menu_focus = 0;
    u->menu_press = -1;
}

/* one 30 Hz tick of WindowTitle exec @0x50fb8 */
static void title_tick(UiState *u) {
    if (u->tstate >= 8) titlefx_tick(u);   /* WindowTitleFlower belongs to the title, not the boot logos */
    switch (u->tstate) {
    case 0:
        /* Na_SEGA_09_keep: the original plays this voice with PlayBGM (stops other BGM, BGM volume) */
        if (++u->tcount == 10) { stop_bgm(u); mixer_bgm_play(&u->as->sounds[SND_SEGA], 1.0f, false); }
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
        if (u->bgm_voice < 0 && u->tcount > 67) play_bgm(u, SND_BGM01);
        if (u->tstate == 10)
            for (int i = 0; i < 4; i++) { /* TouchTitleMenus exec: slide 100 px/tick to x = 128 */
                u->menu_x[i] -= 100.0f;
                if (u->menu_x[i] < 128.0f) u->menu_x[i] = 128.0f;
            }
        break;
    }
}

static void opt_open(UiState *u, int mode);
static void render_back_button(UiState *u);

static bool in_rect(float x, float y, float rx, float ry, float rw, float rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

/* TouchBase touchDown/touchMoved/touchUp: a button fires only when the same touch went down inside it
   and is released inside it, and a touch that moved further than the button's own size on either
   axis is cancelled (so a swipe that ends on a button does nothing). press_track records the touch;
   tapped() is the test on release at (mx, my). */
static void press_track(UiState *u, const SDL_Event *e) {
    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
        u->dn_on = true;
        u->dn_x = (float)e->button.x; u->dn_y = (float)e->button.y;
        u->dn_mx = u->dn_my = 0;
        u->dn_ms = SDL_GetTicks();
    } else if (e->type == SDL_MOUSEMOTION && (e->motion.state & SDL_BUTTON_LMASK) && u->dn_on) {
        u->dn_mx = fmaxf(u->dn_mx, fabsf((float)e->motion.x - u->dn_x));
        u->dn_my = fmaxf(u->dn_my, fabsf((float)e->motion.y - u->dn_y));
    }
}

static bool tapped(const UiState *u, float rx, float ry, float rw, float rh, float mx, float my) {
    return u->dn_on && in_rect(u->dn_x, u->dn_y, rx, ry, rw, rh) && in_rect(mx, my, rx, ry, rw, rh) &&
           u->dn_mx <= rw && u->dn_my <= rh;
}
/* hit boxes of the bottom buttons: TouchBack type 4 (17,873) 129x82, TouchMusicSelect PLAY (164,873) 324x82 */
static bool hit_back(float mx, float my) { return mx >= 17 && mx < 146 && my >= 873 && my < 955; }
static bool hit_play(float mx, float my) { return mx >= 164 && mx < 488 && my >= 873 && my < 955; }
/* TouchModeChange: modechange 404x70 drawn at (118,698) */
static bool hit_mode(float mx, float my) { return mx >= 118 && mx < 522 && my >= 698 && my < 768; }

/* TouchBack: pressed while the touch that began on it is held; a release over it fires (SE 8 after
   the callback, TouchBack touchUp @0x6ee74). Returns true when it fires. */
static bool back_event(UiState *u, const SDL_Event *e) {
    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
        u->back_pressed = hit_back((float)e->button.x, (float)e->button.y);
        return false;
    }
    if (e->type == SDL_MOUSEBUTTONUP && e->button.button == SDL_BUTTON_LEFT) {
        u->back_pressed = false;
        if (tapped(u, 17, 873, 129, 82, (float)e->button.x, (float)e->button.y)) { play_sound(u->as, SE_BACK); return true; }
    }
    return false;
}

/* WindowCounter SetValue: m_Digit = floor(log10 v) + 2 (1 for 0); digit k (k = 1 the units) rolls
   while the counter's tick count is below 12k, so a counter with d digits settles after 12d ticks */
static int counter_digits(int v) {
    int d = 0;
    for (v = v < 0 ? 0 : v; v > 0; v /= 10) d++;
    return d;
}

static int result_rank_se(int rank) {
    static const int k_se[7] = {13, 12, 12, 11, 11, 11, 10};   /* SE14 / SE13 / SE12 / SE11 */
    return rank >= 0 && rank <= 6 ? k_se[rank] : -1;
}

/* WindowResult touchDown: @0x4f07c: a touch while the counters run settles them all */
static void result_skip(UiState *u) {
    if (u->res_idx >= 11) return;
    stop_roll(u);
    u->res_idx = 11;
}

/* one 30 Hz tick of WindowResult exec @0x4d6ac with its WindowCounter children (CheckCounter @0x4d5fc) */
static void result_tick(UiState *u) {
    bgfx_exec(u);
    u->res_cnt++;
    if (u->res_idx < 11) {
        int k = u->res_idx;
        bool done = ++u->res_ctr[k] >= 12 * counter_digits(u->res_val[k]);
        if (done) { stop_roll(u); u->res_idx++; }
        else if (u->res_roll_voice < 0) u->res_roll_voice = mixer_sfx_play(&u->as->sounds[SE_COUNT], 1.0f, true);
    }
    bool failed = u->res.rank >= 5 && u->res.diff != DIFF_BREAK;   /* m_ContentOpen == 0 */
    switch (u->res_state) {
    case 0:   /* counters; the rank stamp lands once they are done and 46 ticks have passed */
        if (u->res_cnt >= 46 && u->res_idx >= 11) {
            int se = result_rank_se(u->res.rank);
            if (se >= 0) play_sound(u->as, se);
            u->res_state = 1;
            u->res_cnt = 0;
        }
        break;
    case 1:   /* FAILED grows in, then a pause */
        if (failed) u->res_fail_rate = fminf(1.0f, u->res_fail_rate + 0.2f);
        if (u->res_cnt >= 66) { u->res_state = 2; u->res_cnt = 0; }
        break;
    case 2:
        if (u->new_record) { u->res_state = 4; u->res_cnt = 0; play_sound(u->as, SE_RECORD); }   /* setHighScoreMode */
        else u->res_state = 5;
        break;
    case 4:   /* NEW RECORD grows in */
        u->res_rec_rate = fminf(1.0f, u->res_rec_rate + 0.2f);
        if (u->res_cnt >= 46) u->res_state = 5;
        break;
    case 5:   /* Back / PLAY AGAIN appear */
        u->res_state = 6;
        break;
    default: break;
    }
}

void ui_enter(UiState *u, int scene, const GameResult *res) {
    int prev_scene = u->scene;
    u->scene = scene;
    if (IS_ENDING_SCENE(scene)) { stop_bgm(u); stop_preview(u); }   /* ending.c plays BGM04 / the movie */
    if (IS_RECORDS_SCENE(scene)) { stop_bgm(u); stop_preview(u); }  /* RECORDS: records.c plays BGM03 */
    u->enter_ms = u->last_ms = SDL_GetTicks();
    u->tick_acc = 0;
    u->ticks = 0;
    u->play_pressed = u->back_pressed = u->mode_pressed = false;
    u->opt_press = -1;
    u->lg_until = 0;
    stop_roll(u);
    if (scene == SCENE_BOOT || scene == SCENE_TITLE) {   /* new title scene: fresh WindowTitleFlower */
        memset(u->tfx, 0, sizeof u->tfx);
        u->tfx_timer = 0;
    }
    if (scene == SCENE_BOOT) {           /* first boot: logos inside the title window */
        u->scene = SCENE_TITLE;
        u->tstate = 0; u->tcount = 0; u->fade = 0;
    } else if (scene == SCENE_TITLE) {
        stop_preview(u);
        if (u->tstate < 9) { u->tstate = 9; u->tcount = 60; u->fade = 0; }   /* --start-scene title */
        else {                           /* back from another screen: menu already open, no SE 19 */
            title_start_menu(u);
            play_bgm(u, SND_BGM01);      /* the title restarts BGM01 at once (@0x50e6e) */
        }
    }
    if (scene == SCENE_OPTION) {
        /* WindowOption init @0x443dc: setMode (GetPrevScene == 1 ? 0 : 5): back from the ending -> CREDITS */
        opt_open(u, prev_scene == SCENE_ENDING ? 5 /* OPT_CREDITS */ : 0 /* OPT_MAIN */);
        bgfx_reset(u);
        play_bgm(u, SND_BGM01 + 2);      /* OPTIONS switches to BGM03, sound #24 (@0x444be) */
    }
    if (scene == SCENE_DLC) {
        u->scene = SCENE_OPTION;
        opt_open(u, 7 /* OPT_DLC */);
        u->dlc_note[0] = 0;
        DlcDlStatus st;
        dlcdl_status(&st);
        if (st.state == DLD_IDLE && u->save->dlc_url[0]) dlcdl_list(u->save->dlc_url);   /* pack list of the saved link */
    }
    if (scene == SCENE_SELECT || scene == SCENE_PV) {
        if (u->song >= nvis(u)) u->song = 0;   /* DLC songs hidden, or another import */
        stop_bgm(u);
        stop_preview(u);
        covers_snap(u);
        u->snd_wait = 10;   /* first preview after the same wait as a cover change */
        bgfx_reset(u);
    }
    if (scene == SCENE_RESULT && res) {
        stop_preview(u);
        u->res = *res;
        u->new_record = false;
        u->res_state = u->res_cnt = u->res_idx = 0;
        u->res_fail_rate = u->res_rec_rate = 0;
        flickres_reset(&u->fres);
        /* CheckCounter order: Total Notes, Combo, COOL, FINE, SAFE, SAD, WORST, ♪, Stage, Combo Bonus, Total */
        const int vals[11] = {res->total_notes, res->max_combo, res->counts[5], res->counts[4], res->counts[3],
                              res->counts[2], res->counts[1], res->interlude_hits, res->stage_score,
                              res->combo_score, res->stage_score + res->combo_score};
        memcpy(u->res_val, vals, sizeof vals);
        memset(u->res_ctr, 0, sizeof u->res_ctr);
        bgfx_reset(u);
        if (res->rank >= 0) {
            Record *r = &u->save->rec[res->song][res->diff];
            int total = res->stage_score + res->combo_score;
            if (total > r->score) { r->score = total; u->new_record = true; }
            if (r->rank < 0 || res->rank < r->rank) r->rank = res->rank;
            if (res->max_combo > r->combo) r->combo = res->max_combo;
            if (res->rank <= 4) r->cleared = 1;
            save_write(u->save);
        }
        play_bgm(u, SND_BGM01 + 4);      /* result BGM is BGM05, sound #26 (@0x4bad2) */
    }
}

/* keyboard: ←/→ move the spring target one cover (no wrap, like the swipe limits); ↑/↓ difficulty */
static void select_move(UiState *u, int ds, int dd) {
    if (ds && !u->swiping) {
        int n = u->song + ds;
        if (n >= 0 && n < nvis(u)) {
            update_title(u, n);
            u->spr_tgt = (float)n * 40.0f;
        }
    }
    if (dd) {
        int n = u->diff + dd;
        if (n >= 0 && n < NUM_DIFF) { set_difficulty(u, n); play_sound(u->as, SE_BUTTON); }
    }
}

static int start_game(UiState *u, int *song, int *diff) {
    stop_preview(u);
    stop_bgm(u);
    stop_roll(u);
    play_sound(u->as, SE_BUTTON);
    *song = u->song;
    *diff = u->diff;
    /* PV viewer playback goes through the game engine in "PV mode" */
    u->opt->pv_mode = u->scene == SCENE_PV;
    u->opt->pv_lyrics = u->pv_lyrics;
    u->opt->pv_karaoke = u->pv_karaoke;
    u->opt->pv_loop = u->pv_loop;
    u->opt->pv_shuffle = u->pv_shuffle;
    return SCENE_GAME;
}

/* PV select (ScenePVSelect, help_02 panel 4): PLAYLIST / LOOP / SHUFFLE / LYRICS / KARAOKE toggles */
static const char *k_pv_icon[5] = {"playlist", "loop", "shuffle", "lyrics", "karaoke"};
static bool *pv_flag(UiState *u, int i) {
    return i == 1 ? &u->pv_loop : i == 2 ? &u->pv_shuffle : i == 3 ? &u->pv_lyrics : i == 4 ? &u->pv_karaoke : NULL;
}

static int pv_event(UiState *u, const SDL_Event *e, int *song, int *diff) {
    scroll_touch(u, e);
    if (back_event(u, e)) { stop_preview(u); return SCENE_TITLE; }
    bool click = e->type == SDL_MOUSEBUTTONUP && e->button.button == SDL_BUTTON_LEFT;
    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT)
        u->play_pressed = hit_play((float)e->button.x, (float)e->button.y);
    bool key = e->type == SDL_KEYDOWN && !e->key.repeat;
    SDL_Keycode k = key ? e->key.keysym.sym : 0;
    float mx = click ? (float)e->button.x : 0, my = click ? (float)e->button.y : 0;
    int toggle = -1;
    if (key) {
        if (k == SDLK_ESCAPE || k == SDLK_BACKSPACE) { stop_preview(u); play_sound(u->as, SE_BACK); return SCENE_TITLE; }
        if (k == SDLK_LEFT) select_move(u, -1, 0);
        if (k == SDLK_RIGHT) select_move(u, 1, 0);
        /* focus cycles LOOP, SHUFFLE, LYRICS, KARAOKE */
        if (k == SDLK_UP) { u->pv_focus = u->pv_focus <= 1 ? 4 : u->pv_focus - 1; u->menu_kbd = true; }
        if (k == SDLK_DOWN) { u->pv_focus = u->pv_focus >= 4 ? 1 : u->pv_focus + 1; u->menu_kbd = true; }
        if (k == SDLK_SPACE) { toggle = u->pv_focus; u->menu_kbd = true; }
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER) return start_game(u, song, diff);
    }
    bool was_play = u->play_pressed;
    if (click) u->play_pressed = false;
    if (click) {
        /* the covers only scroll (WindowCover TouchButton:: is empty): no taps on them */
        u->menu_kbd = false;
        for (int i = 0; i < 5; i++) if (tapped(u, 25 + 120.0f * i, 742, 109, 93, mx, my)) toggle = i;
        if (was_play && tapped(u, 164, 873, 324, 82, mx, my)) return start_game(u, song, diff);
    }
    if (toggle >= 0) {
        /* TouchPVLoopMode touchUp: SE 19; PLAYLIST editing is not ported, so that one only sounds */
        bool *f = pv_flag(u, toggle);
        if (f) { *f = !*f; u->pv_focus = toggle; }
        play_sound(u->as, SE_BUTTON);
    }
    return -1;
}

/* title menu button hit test (menubutton_l_base 394x82 at (x, 538+82*i)) */
/* TouchTitleMenus exec: a button only takes touches once it has slid in to x = 128 */
static int menu_hit(UiState *u, float mx, float my) {
    for (int i = 0; i < 4; i++) {
        float x = u->menu_x[i], y = 538.0f + 82.0f * i;
        if (x > 128.0f) continue;
        if (mx >= x && mx < x + 394 && my >= y && my < y + 82) return i;
    }
    return -1;
}

/* TouchTitleMenus touchUp: SE 19 for every button, SHOP included */
static int menu_activate(UiState *u, int i) {
    play_sound(u->as, SE_BUTTON);
    if (i == 0) return SCENE_SELECT;   /* RHYTHM GAME */
    if (i == 1) return SCENE_PV;       /* PV */
    if (i == 2) return SCENE_OPTION;   /* OPTIONS */
    return SCENE_DLC;                  /* SHOP slot: the store is gone, port DLC page instead */
}

/* ------------------------------------------------------------ options (SceneOption / WindowOption) */

enum { OPT_MAIN, OPT_AUDIO, OPT_HAND, OPT_KEYBOARD, OPT_HELP, OPT_CREDITS, OPT_DISPLAY, OPT_DLC };
static const char *k_opt_title[8] = {"OPTIONS", "AUDIO", "HANDEDNESS", "KEYBOARD", "HELP", "CREDITS", "DISPLAY", "DLC"};
/* Localizable.strings OPTION_TITLE_0N (English) */
static const char *k_opt_sub[8] = {"Please select a category to change.", "MUSIC and SFX", "Handedness Settings",
                                   "Keyboard Settings", "", "", "Configure Display Settings", "Downloadable Songs"};
/* port-only DLC page (title SHOP slot) on Linux; iOS and Android show a native screen instead
   (ios/App/DlcViewController.m, android/.../DlcPage.java): SHOW DLC SONGS ON / OFF and the download
   link (PASTE LINK) with the pack list of the IPA behind it (dlcdl.c) */
static bool dlc_installed(const UiState *u) { return u->gd->nsongs > u->gd->nbase; }
/* main list order as on the iOS screen (help_02 panel 2) -> mode */
static const int k_opt_list[6] = {OPT_DISPLAY, OPT_AUDIO, OPT_HAND, OPT_KEYBOARD, OPT_HELP, OPT_CREDITS};
/* TouchCommonButton initType:No:Pos: @0x63ee0: list buttons (menubutton_l_base 394x82) at x = 129 */
static const float k_opt_list_y[6] = {384, 466, 548, 630, 712, 794};   /* 384 + 82i (emulator capture) */
/* TouchSoundVolume init @0x76d18: meter rows (BGM, SE); 16 cells of 25x34 at x = 113 + 26i */
static const float k_vol_y[2] = {583, 664};

/* KEYBOARD page: TouchKeyBoardSelect slots left to right are ORIGINAL, ROMAN SUPPORT, FULL ROMAN;
   Options.keyboard is 0 original, 1 full roman, 2 roman+hiragana */
static const int k_kb_slot[3] = {0, 2, 1}, k_kb_of_slot[3] = {0, 2, 1};
static const char *k_kb_name[3] = {"keyboard_ori", "keyboard_fro", "keyboard_rah"};
/* DISPLAY page: TouchPauseEnable s_Pos @0x13c668 / s_TexPos @0x13c688 (UI_tex_09), index 2*No + !selected */
static const int k_pause_pos[4][2] = {{322, 696}, {336, 711}, {459, 696}, {473, 711}};
static const int k_pause_tex[4][4] = {{125, 311, 135, 115}, {395, 386, 108, 93}, {260, 311, 135, 115}, {395, 293, 108, 93}};
/* TouchSoundVolume Render @0x77020: lit cells = (int)(vol*15 + 1), none at 0. The volume itself is
   continuous (dragged, see option_event); the small bias keeps a 1/15 step read back from the save
   (4 decimals, e.g. 0.1333) on the same cell count */
static int vol_cells(float v) {
    if (v <= 0.0f) return 0;
    int n = (int)(v * 15.0f + 1.0f + 1e-3f);
    return n > 16 ? 16 : n;
}

static void opt_store(UiState *u) {
    u->save->opt = *u->opt;
    u->save->has_opt = true;
    save_write(u->save);
    mixer_set_master(1.0f, u->opt->se_volume);
    mixer_set_bgm(u->opt->bgm_volume);
}

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

/* change the focused row of a sub page by d (-1/+1); toggles for on/off rows */
static void opt_adjust(UiState *u, int d) {
    Options *o = u->opt;
    switch (u->opt_mode) {
    case OPT_AUDIO:
        if (u->opt_row == 0) o->bgm_volume = clamp01(roundf(o->bgm_volume * 15 + d) / 15);
        else if (u->opt_row == 1) { o->se_volume = clamp01(roundf(o->se_volume * 15 + d) / 15); }
        else o->fail_sound = !o->fail_sound;
        break;
    case OPT_HAND: o->left_hand = d < 0; break;
    case OPT_KEYBOARD:
        if (u->opt_row == 0) o->keyboard = k_kb_of_slot[(k_kb_slot[o->keyboard % 3] + d + 3) % 3];
        else if (u->opt_row == 1) o->guide_panel = d < 0;
        else o->flower = d < 0;
        break;
    case OPT_DISPLAY: o->pause_button = d < 0; break;
    case OPT_HELP:   /* TouchButton HelpPage @0x45200: page +-1 clamped 0..9, setPage (the arrow at an end is hidden) */
        if (u->help_page + d >= 0 && u->help_page + d < HELP_PAGES) {
            u->help_page += d;
            help_set_page(u->as->appdir, u->help_page);
            play_sound(u->as, SE_BUTTON);   /* TouchHelpPage touchUp: SE 0x13 */
        }
        return;
    case OPT_DLC:
        if (u->opt_row != 0 || !dlc_installed(u)) { play_sound(u->as, SND_CANCEL); return; }
        o->show_dlc = d < 0;
        break;
    default: return;
    }
    opt_store(u);
    play_sound(u->as, SE_BUTTON);
}

static int opt_rows(int mode) {
    if (mode == OPT_DLC) return 1;
    return mode == OPT_AUDIO || mode == OPT_KEYBOARD ? 3 : 1;
}


/* TouchSoundVolume touchDown/touchMoved/touchUp @0x77284/0x77364/0x772e8: a touch on a meter (box
   440x84 px = ColSize (220,42) points @0x76f64) only records its x; each move then adds dx/440 to that
   volume, dx in UIKit points (0x43dc0000 @0x774c4), i.e. scene px / 880 (clamped 0..1; the original
   keeps SE in 0..0.75 and scales dx by 0.75, the same thing normalised) and moves the start point. A
   full-width drag changes the volume by half. A tap without movement changes nothing. Release stores
   the options. */
static bool volume_drag_event(UiState *u, const SDL_Event *e) {
    if (u->opt_mode != OPT_AUDIO) { u->vol_drag = -1; return false; }
    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
        float mx = (float)e->button.x, my = (float)e->button.y;
        for (int r = 0; r < 2; r++)
            if (my >= k_vol_y[r] - 10 && my < k_vol_y[r] + 74 && mx >= 113 && mx < 553) {
                u->vol_drag = r;
                u->vol_x = mx;
                u->opt_row = r;
                u->menu_kbd = false;
                return true;
            }
        return false;
    }
    if (u->vol_drag < 0) return false;
    if (e->type == SDL_MOUSEMOTION) {
        float *v = u->vol_drag == 0 ? &u->opt->bgm_volume : &u->opt->se_volume;
        *v = clamp01(*v + ((float)e->motion.x - u->vol_x) / 880.0f);
        u->vol_x = (float)e->motion.x;
        mixer_set_master(1.0f, u->opt->se_volume);
        mixer_set_bgm(u->opt->bgm_volume);
        return true;
    }
    if (e->type == SDL_MOUSEBUTTONUP && e->button.button == SDL_BUTTON_LEFT) {
        u->vol_drag = -1;
        opt_store(u);   /* TouchSoundVolume touchUp: no sound */
        return true;
    }
    return false;
}

static void opt_open(UiState *u, int mode) {
    u->opt_mode = mode;
    u->opt_row = 0;
    u->help_page = 0;
    u->credits_t = 0;
    if (mode == OPT_HELP) help_set_page(u->as->appdir, 0);   /* setMode HELP: m_HelpPage 0, setPage:0 */
}

/* ---- DLC page: SHOW DLC SONGS toggle, download link + pack list (dlcdl.c), Linux ---- */
#define DLC_ROWS 3   /* packs per page, rows at DLC_ROW_Y(i), DLC_ROW_H high: the name and a line per song */
#define DLC_ROW_Y(i) (538.0f + 112.0f * (float)(i))
#define DLC_ROW_H 104.0f
#define DLC_TOGGLE_X 380.0f   /* ON | OFF segments */
#define DLC_TOGGLE_Y 152.0f
#define DLC_TOGGLE_W 110.0f
#define DLC_TOGGLE_H 56.0f
#define DLC_STATUS_Y 446.0f
static const SDL_FRect k_dlc_paste = {170, 358, 300, 62};

static bool in_frect(const SDL_FRect *r, float x, float y) { return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h; }

static int dlc_pack_songs(const UiState *u, int pack) {
    int n = 0;
    for (int i = u->gd->nbase; i < u->gd->nsongs; i++) n += u->gd->songs[i].pack == pack;
    return n;
}

static int dlc_pages(int npacks) { return npacks > 0 ? (npacks + DLC_ROWS - 1) / DLC_ROWS : 1; }

static bool dlc_busy(const DlcDlStatus *st) { return st->state == DLD_LISTING || st->state == DLD_INSTALLING; }

/* PASTE LINK: the clipboard's http(s) link becomes the saved download link and its pack list is read */
static void dlc_paste(UiState *u) {
    DlcDlStatus st;
    dlcdl_status(&st);
    if (dlc_busy(&st)) {
        snprintf(u->dlc_note, sizeof u->dlc_note, "Wait for the current download to finish (or cancel it).");
        play_sound(u->as, SND_CANCEL);
        return;
    }
    char url[sizeof u->save->dlc_url] = "";
    char *t = SDL_HasClipboardText() ? SDL_GetClipboardText() : NULL;
    if (t) {
        const char *s = t;
        while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
        size_t n = strlen(s);
        while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) n--;
        if (n < sizeof url) snprintf(url, sizeof url, "%.*s", (int)n, s);
        SDL_free(t);
    }
    if (!net_url_ok(url)) {
        snprintf(u->dlc_note, sizeof u->dlc_note, "Copy the http(s) download link of the .ipa first, then tap PASTE LINK.");
        play_sound(u->as, SND_CANCEL);
        return;
    }
    u->dlc_note[0] = 0;
    snprintf(u->save->dlc_url, sizeof u->save->dlc_url, "%s", url);
    save_write(u->save);
    u->dlc_page = 0;
    play_sound(u->as, dlcdl_list(url) ? SND_HIT : SND_CANCEL);
}

/* a pack row: download it, or cancel its running download */
static void dlc_pick(UiState *u, const DlcDlStatus *st, int idx) {
    if (idx < 0 || idx >= st->npacks) return;
    const DlcPackInfo *p = &st->packs[idx];
    u->dlc_note[0] = 0;
    if (st->state == DLD_INSTALLING && st->busy_pack == p->pack) {
        dlcdl_cancel();
        play_sound(u->as, SND_CANCEL);
    } else if (!p->available) {   /* greyed: not in the linked IPA (or no link yet) */
        if (dlc_pack_songs(u, p->pack) < p->nsongs)
            snprintf(u->dlc_note, sizeof u->dlc_note, "%s", u->save->dlc_url[0] ? "This pack is not in the linked IPA." : "Paste a link first.");
        play_sound(u->as, SND_CANCEL);
    } else if (st->state == DLD_READY && dlcdl_install(p->pack)) {   /* installed packs: repairs / re-lists */
        play_sound(u->as, SND_HIT);
    } else {
        play_sound(u->as, SND_CANCEL);
    }
}

static int dlc_event(UiState *u, bool key, SDL_Keycode k, bool click, float mx, float my, bool ok) {
    DlcDlStatus st;
    dlcdl_status(&st);
    int pages = dlc_pages(st.npacks);
    if (u->dlc_page >= pages) u->dlc_page = pages - 1;
    int first = u->dlc_page * DLC_ROWS, shown = st.npacks - first;
    if (shown > DLC_ROWS) shown = DLC_ROWS;
    if (shown < 0) shown = 0;
    int fixed = 2, rows = fixed + shown;
    if (key && (k == SDLK_UP || k == SDLK_DOWN || k == SDLK_LEFT || k == SDLK_RIGHT)) u->menu_kbd = true;
    if (click) u->menu_kbd = false;
    if (u->opt_row >= rows) u->opt_row = rows - 1;
    if (key && k == SDLK_UP) u->opt_row = (u->opt_row + rows - 1) % rows;
    if (key && k == SDLK_DOWN) u->opt_row = (u->opt_row + 1) % rows;
    if (key && k == SDLK_v && (SDL_GetModState() & (KMOD_CTRL | KMOD_GUI))) { u->opt_row = 1; dlc_paste(u); return -1; }
    int flip = 0;
    if (key && (k == SDLK_PAGEUP || ((k == SDLK_LEFT) && u->opt_row >= fixed))) flip = -1;
    if (key && (k == SDLK_PAGEDOWN || ((k == SDLK_RIGHT) && u->opt_row >= fixed))) flip = 1;
    if (click && pages > 1 && my >= 880 && my < 950 && mx >= 380) flip = mx < 470 ? -1 : mx >= 560 ? 1 : 0;
    if (flip) {
        int np = u->dlc_page + flip;
        if (np >= 0 && np < pages) { u->dlc_page = np; play_sound(u->as, SND_DECIDE); }
        int nshown = st.npacks - u->dlc_page * DLC_ROWS;
        if (u->opt_row >= fixed + (nshown > DLC_ROWS ? DLC_ROWS : nshown)) u->opt_row = fixed;
        return -1;
    }
    if (key && (k == SDLK_LEFT || k == SDLK_RIGHT) && u->opt_row == 0) opt_adjust(u, k == SDLK_LEFT ? -1 : 1);
    int act = -1;   /* row to activate */
    if (ok) act = u->opt_row;
    if (click) {
        if (my >= DLC_TOGGLE_Y && my < DLC_TOGGLE_Y + DLC_TOGGLE_H && mx >= DLC_TOGGLE_X && mx < DLC_TOGGLE_X + 2 * DLC_TOGGLE_W) {
            u->opt_row = 0;
            bool on = mx < DLC_TOGGLE_X + DLC_TOGGLE_W;
            if (u->opt->show_dlc != on) opt_adjust(u, on ? -1 : 1);
        } else if (in_frect(&k_dlc_paste, mx, my)) {
            act = u->opt_row = 1;
        } else if (my >= DLC_STATUS_Y - 10 && my < DLC_STATUS_Y + 66 && st.state == DLD_FAILED && u->save->dlc_url[0]) {
            u->dlc_note[0] = 0;
            play_sound(u->as, dlcdl_list(u->save->dlc_url) ? SND_HIT : SND_CANCEL);   /* retry */
        } else {
            for (int i = 0; i < shown; i++)
                if (my >= DLC_ROW_Y(i) && my < DLC_ROW_Y(i) + DLC_ROW_H && mx >= 30 && mx < 610) act = u->opt_row = fixed + i;
        }
    }
    if (act == 0 && ok) opt_adjust(u, u->opt->show_dlc ? 1 : -1);
    else if (act == 1) dlc_paste(u);
    else if (act >= fixed) dlc_pick(u, &st, first + act - fixed);
    return -1;
}

/* s cut to about `units` columns (ASCII 1, other characters 2) with "..." */
static void fit_text(const char *s, int units, char *out, size_t cap) {
    int total = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        if ((*p & 0xc0) != 0x80) total += *p < 0x80 ? 1 : 2;
    int limit = total <= units ? units : units - 2, used = 0;
    size_t o = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        size_t len = *p < 0x80 ? 1 : (*p >> 5) == 6 ? 2 : (*p >> 4) == 14 ? 3 : 4;
        int w = *p < 0x80 ? 1 : 2;
        bool whole = true;
        for (size_t k = 1; k < len; k++) whole = whole && p[k];
        if (!whole || used + w > limit || o + len + 4 > cap) break;
        memcpy(out + o, p, len);
        o += len;
        p += len;
        used += w;
    }
    if (*p && o + 4 <= cap) { memcpy(out + o, "...", 3); o += 3; }
    out[o] = 0;
}

/* word wrap (ASCII messages) into up to max lines of cols bytes */
static int wrap_msg(const char *s, int cols, char out[][128], int max) {
    int n = 0;
    while (*s && n < max) {
        while (*s == ' ') s++;
        int len = (int)strlen(s), cut = len < cols ? len : cols;
        if (len > cols) {
            while (cut > cols / 2 && s[cut] != ' ') cut--;
            if (cut <= cols / 2) cut = cols;
        }
        snprintf(out[n++], 128, "%.*s", cut, s);
        s += cut;
    }
    return n;
}

static void dlc_button(SDL_Renderer *r, const SDL_FRect *b, const char *label, bool focus, bool enabled) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    if (enabled) SDL_SetRenderDrawColor(r, 27, 181, 176, 235);
    else SDL_SetRenderDrawColor(r, 70, 80, 92, 235);
    SDL_RenderFillRectF(r, b);
    if (focus) SDL_SetRenderDrawColor(r, 255, 240, 120, 255);
    else SDL_SetRenderDrawColor(r, 255, 255, 255, 200);
    SDL_RenderDrawRectF(r, b);
    if (focus) {
        SDL_FRect in = {b->x + 2, b->y + 2, b->w - 4, b->h - 4};
        SDL_RenderDrawRectF(r, &in);
    }
    font_draw(r, label, b->x + b->w / 2, b->y + b->h / 2 - 9, 2.4f, WHITE, 0);
}

/* Port-only look (not the original game's OPTIONS art), like the import screen: navy, teal, plain
   rectangles, so the page does not pass for a feature of the original app. */
static void dlc_card(SDL_Renderer *r, float x, float y, float w, float h, Uint8 a) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 255, 255, 255, a);
    SDL_FRect c = {x, y, w, h};
    SDL_RenderFillRectF(r, &c);
}

static void render_dlc(UiState *u) {
    SDL_Renderer *r = u->ren;
    const Options *o = u->opt;
    SDL_Color dim = {170, 180, 190, 255}, focus = {255, 240, 120, 255}, bad = {255, 160, 150, 255}, teal = {120, 230, 220, 255};
    bool have = dlc_installed(u), kbd = u->menu_kbd;
    DlcDlStatus st;
    dlcdl_status(&st);
    int fixed = 2;

    SDL_SetRenderDrawColor(r, 12, 16, 24, 255);
    SDL_RenderClear(r);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 27, 181, 176, 255);
    SDL_FRect band = {0, 0, LOGICAL_W, 6};
    SDL_RenderFillRectF(r, &band);
    font_draw(r, "DLC Packs", 320, 60, 4.0f, WHITE, 0);

    /* Show DLC songs: segmented ON / OFF */
    dlc_card(r, 24, 132, 592, 96, 14);
    font_draw(r, "Show DLC songs", 48, 156, 2.4f, have ? WHITE : dim, -1);
    font_draw(r, "in music select and PV", 48, 192, 1.5f, dim, -1);
    static const char *lbl[2] = {"ON", "OFF"};
    for (int i = 0; i < 2; i++) {
        bool sel = o->show_dlc == (i == 0);
        SDL_FRect seg = {DLC_TOGGLE_X + DLC_TOGGLE_W * i, DLC_TOGGLE_Y, DLC_TOGGLE_W, DLC_TOGGLE_H};
        if (sel) SDL_SetRenderDrawColor(r, 27, 181, 176, have ? 255 : 120);
        else SDL_SetRenderDrawColor(r, 55, 60, 72, 255);
        SDL_RenderFillRectF(r, &seg);
        font_draw(r, lbl[i], seg.x + seg.w / 2, seg.y + seg.h / 2 - 9, 2.4f, sel || have ? WHITE : dim, 0);
    }
    if (kbd && u->opt_row == 0) {
        SDL_SetRenderDrawColor(r, focus.r, focus.g, focus.b, 255);
        SDL_FRect fr = {DLC_TOGGLE_X - 4, DLC_TOGGLE_Y - 4, DLC_TOGGLE_W * 2 + 8, DLC_TOGGLE_H + 8};
        SDL_RenderDrawRectF(r, &fr);
    }
    char buf[256];
    int packs = 0;
    for (int i = u->gd->nbase; i < u->gd->nsongs; i++) {
        bool seen = false;
        for (int k = u->gd->nbase; k < i && !seen; k++) seen = u->gd->songs[k].pack == u->gd->songs[i].pack;
        packs += !seen;
    }
    if (have) snprintf(buf, sizeof buf, "%d pack%s, %d songs installed", packs, packs == 1 ? "" : "s", u->gd->nsongs - u->gd->nbase);
    else snprintf(buf, sizeof buf, "No DLC songs installed");
    font_draw(r, buf, 320, 248, 2.0f, WHITE, 0);

    /* download link and buttons */
    font_draw(r, "Download link", 48, 294, 1.8f, teal, -1);
    if (u->save->dlc_url[0]) {
        const char *s = u->save->dlc_url;
        s += !strncmp(s, "https://", 8) ? 8 : !strncmp(s, "http://", 7) ? 7 : 0;
        size_t n = strlen(s);
        if (n > 46) snprintf(buf, sizeof buf, "%.18s...%s", s, s + n - 25);
        else snprintf(buf, sizeof buf, "%s", s);
    } else {
        snprintf(buf, sizeof buf, "none yet");
    }
    font_draw(r, buf, 48, 324, 1.6f, dim, -1);
    dlc_button(r, &k_dlc_paste, "PASTE LINK", kbd && u->opt_row == 1, !dlc_busy(&st));

    /* status */
    SDL_Color sc = WHITE;
    buf[0] = 0;
    if (u->dlc_note[0]) {
        snprintf(buf, sizeof buf, "%s", u->dlc_note);
        sc = bad;
    } else if (st.state == DLD_IDLE) {
        snprintf(buf, sizeof buf, "Paste the link of an IPA with the DLC packs to download them.");
    } else if (st.state == DLD_LISTING) {
        snprintf(buf, sizeof buf, "Reading the pack list...");
    } else if (st.state == DLD_INSTALLING) {
        const char *name = "pack";
        for (int i = 0; i < st.npacks; i++) if (st.packs[i].pack == st.busy_pack) name = st.packs[i].name;
        snprintf(buf, sizeof buf, "Downloading %s: %d%%. Tap it again to cancel.", name, (int)(st.frac * 100));
    } else if (st.state == DLD_FAILED) {
        snprintf(buf, sizeof buf, "%s Tap here to try again.", st.msg);
        sc = bad;
    } else if (st.state == DLD_READY) {
        if (st.msg[0]) snprintf(buf, sizeof buf, "%s", st.msg);
        else snprintf(buf, sizeof buf, "Tap a pack to download it.");
        if (st.error) sc = bad;
        else if (st.msg[0]) sc = teal;
    }
    char lines[3][128];
    int nl = wrap_msg(buf, 50, lines, 2);
    for (int i = 0; i < nl; i++) font_draw(r, lines[i], 320, DLC_STATUS_Y + 26.0f * i, 1.8f, sc, 0);
    if (st.state == DLD_INSTALLING) {
        float f = (float)(st.frac < 0 ? 0 : st.frac > 1 ? 1 : st.frac);
        SDL_FRect bar = {24, DLC_STATUS_Y + 56, 592, 8}, fill = {24, DLC_STATUS_Y + 56, 592 * f, 8};
        SDL_SetRenderDrawColor(r, 55, 60, 72, 255);
        SDL_RenderFillRectF(r, &bar);
        SDL_SetRenderDrawColor(r, 27, 181, 176, 255);
        SDL_RenderFillRectF(r, &fill);
    }

    /* back (same hit area as the game's back button) */
    SDL_FRect back = {17, 880, 129, 62};
    SDL_SetRenderDrawColor(r, 55, 60, 72, 255);
    SDL_RenderFillRectF(r, &back);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 200);
    SDL_RenderDrawRectF(r, &back);
    font_draw(r, "BACK", back.x + back.w / 2, back.y + back.h / 2 - 9, 2.4f, WHITE, 0);

    /* packs */
    if (st.npacks == 0) {
        if (st.state == DLD_LISTING) return;
        /* one sentence: what the link is for */
        font_draw(r, "Paste the link of an IPA with the DLC packs", 320, DLC_ROW_Y(0) + 8, 1.8f, dim, 0);
        font_draw(r, "to download them one by one.", 320, DLC_ROW_Y(0) + 40, 1.8f, dim, 0);
        return;
    }
    int pages = dlc_pages(st.npacks), page = u->dlc_page < pages ? u->dlc_page : pages - 1, first = page * DLC_ROWS;
    for (int i = 0; i < DLC_ROWS && first + i < st.npacks; i++) {
        const DlcPackInfo *p = &st.packs[first + i];
        float y = DLC_ROW_Y(i);
        dlc_card(r, 24, y, 592, DLC_ROW_H, 16);
        if (kbd && u->opt_row == fixed + i) {
            SDL_SetRenderDrawColor(r, focus.r, focus.g, focus.b, 255);
            SDL_FRect fr = {24, y, 592, DLC_ROW_H};
            SDL_RenderDrawRectF(r, &fr);
        }
        char name[96], right[48];
        fit_text(p->name, 26, name, sizeof name);
        int inst = dlc_pack_songs(u, p->pack);
        bool grey = !p->available && inst < p->nsongs;   /* not offered by the link, not installed */
        SDL_Color rc = WHITE, nc = grey ? dim : WHITE, sc1 = grey ? (SDL_Color){110, 118, 128, 255} : WHITE,
                  sc2 = grey ? (SDL_Color){90, 96, 106, 255} : dim;
        if (st.state == DLD_INSTALLING && st.busy_pack == p->pack) {
            snprintf(right, sizeof right, "%d%%", (int)(st.frac * 100));
            rc = focus;
        } else if (inst >= p->nsongs) {
            snprintf(right, sizeof right, "INSTALLED");
            rc = teal;
        } else if (p->available) {
            snprintf(right, sizeof right, "%llu MB", (unsigned long long)((p->bytes + (1u << 19)) >> 20));
        } else {
            right[0] = 0;
        }
        font_draw(r, name, 40, y + 7, 2.2f, nc, -1);
        if (right[0]) font_draw(r, right, 600, y + 9, 2.0f, rc, 1);
        /* a line per song: English (or romanized) title, then the Japanese one dimmer; condensed to fit */
        int ns = p->nsongs < DLC_PACK_SONGS ? p->nsongs : DLC_PACK_SONGS;
        for (int k = 0; k < ns && k < 3; k++) {
            const DlcPackSong *s = &p->songs[k];
            float sy = y + 38 + 21.0f * (float)k;
            const char *t1 = s->latin[0] ? s->latin : s->title, *t2 = s->latin[0] ? s->title : "";
            float w1 = font_plain_width(r, t1, 1.4f, sc1), w2 = t2[0] ? font_plain_width(r, t2, 1.4f, sc2) : 0.0f;
            float gap = t2[0] ? 14.0f : 0.0f, f = w1 + gap + w2 > 560.0f ? 560.0f / (w1 + gap + w2) : 1.0f;
            font_draw_plain_fit(r, t1, 40, sy, 1.4f, sc1, -1, w1 * f);
            if (t2[0]) font_draw_plain_fit(r, t2, 40 + (w1 + gap) * f, sy, 1.4f, sc2, -1, w2 * f);
        }
    }
    if (pages > 1) {
        font_draw(r, "<", 425, 897, 2.8f, page > 0 ? WHITE : dim, 0);
        snprintf(buf, sizeof buf, "%d/%d", page + 1, pages);
        font_draw(r, buf, 515, 899, 2.2f, WHITE, 0);
        font_draw(r, ">", 605, 897, 2.8f, page + 1 < pages ? WHITE : dim, 0);
    }
}

static int option_event(UiState *u, const SDL_Event *e) {
    if (volume_drag_event(u, e)) return -1;
    bool click = e->type == SDL_MOUSEBUTTONUP && e->button.button == SDL_BUTTON_LEFT;
    bool key = e->type == SDL_KEYDOWN && !e->key.repeat;
    SDL_Keycode k = key ? e->key.keysym.sym : 0;
    float mx = click ? (float)e->button.x : 0, my = click ? (float)e->button.y : 0;
    bool ok = key && (k == SDLK_RETURN || k == SDLK_SPACE || k == SDLK_KP_ENTER);
    bool back = back_event(u, e);   /* plays SE 8 itself */
    if (key && (k == SDLK_ESCAPE || k == SDLK_BACKSPACE)) { back = true; play_sound(u->as, SE_BACK); }
    if (back) {
        if (u->opt_mode == OPT_MAIN || u->opt_mode == OPT_DLC) return SCENE_TITLE;
        int from = u->opt_mode;
        opt_open(u, OPT_MAIN);
        for (int i = 0; i < 6; i++) if (k_opt_list[i] == from) u->opt_row = i;
        return -1;
    }
    if (u->opt_mode == OPT_MAIN) {
        /* TouchCommonButton: held (m_TouchDownIdx >= 0) from the press on the button until the release */
        if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
            u->opt_press = -1;
            for (int i = 0; i < 6; i++)
                if (in_rect((float)e->button.x, (float)e->button.y, 129, k_opt_list_y[i], 394, 82)) u->opt_press = i;
        }
        if (click) u->opt_press = -1;
        if (key && k == SDLK_UP) { u->opt_row = (u->opt_row + 5) % 6; u->menu_kbd = true; }
        if (key && k == SDLK_DOWN) { u->opt_row = (u->opt_row + 1) % 6; u->menu_kbd = true; }
        if (click) {
            u->menu_kbd = false;
            for (int i = 0; i < 6; i++)
                if (tapped(u, 129, k_opt_list_y[i], 394, 82, mx, my)) { u->opt_row = i; ok = true; }
        }
        if (ok) { play_sound(u->as, SE_BUTTON); opt_open(u, k_opt_list[u->opt_row]); }   /* TouchCommonButton */
        return -1;
    }
    if (u->opt_mode == OPT_DLC) return dlc_event(u, key, k, click, mx, my, ok);
    if (u->opt_mode == OPT_HELP && help_event(e)) return -1;   /* WindowHelpText scroll (+ keys) */
    int rows = opt_rows(u->opt_mode);
    if (key && k == SDLK_UP) u->opt_row = (u->opt_row + rows - 1) % rows;
    if (key && k == SDLK_DOWN) u->opt_row = (u->opt_row + 1) % rows;
    if (key && (k == SDLK_UP || k == SDLK_DOWN || k == SDLK_LEFT || k == SDLK_RIGHT)) u->menu_kbd = true;
    if (click) u->menu_kbd = false;
    if (key && k == SDLK_LEFT) opt_adjust(u, -1);
    if (key && k == SDLK_RIGHT) opt_adjust(u, 1);
    if (ok && (u->opt_mode == OPT_AUDIO && u->opt_row == 2)) opt_adjust(u, 1);
    /* CREDITS: TouchGotoEnding (type 0x26, ending 112x32 at (487,891)); WindowOption TouchButton @0x4536c:
       (song 9 unlocked, always in the port) SE 0x13, NextScene 9. Port: Enter presses it too. */
    if (u->opt_mode == OPT_CREDITS && u->gd->nending > 0 && (ok || (click && tapped(u, 487, 891, 112, 32, mx, my)))) {
        play_sound(u->as, SE_BUTTON);
        return SCENE_ENDING;
    }
    if (click) {
        switch (u->opt_mode) {
        case OPT_AUDIO:   /* TouchFailSoundEnable ON (120,740) / OFF (320,740) */
            for (int b = 0; b < 2; b++)
                if (tapped(u, 120 + 200.0f * b, 730, 200, 70, mx, my)) {
                    u->opt_row = 2;
                    if (u->opt->fail_sound != (b == 0)) opt_adjust(u, 1);
                    else play_sound(u->as, SE_BUTTON);   /* TouchFailSoundEnable touchUp sounds either way */
                }
            break;
        case OPT_HAND:    /* TouchHandSide: left / right halves */
            for (int b = 0; b < 2; b++)
                if (tapped(u, 320.0f * b, 320, 320, 400, mx, my)) { u->opt->left_hand = b == 0; opt_store(u); play_sound(u->as, SE_BUTTON); }
            break;
        case OPT_KEYBOARD: {
            /* KeyBoardSelect panels 196x255 at x 20/222/424, y 304; PanelSelect ON (322,601) / OFF (449,601)
               129x110; FlowerUse ON (300,722) / OFF (428,722) 170x145 */
            static const float kb_x[4] = {14, 219, 421, 626};
            for (int s = 0; s < 3; s++)
                if (tapped(u, kb_x[s], 290, kb_x[s + 1] - kb_x[s], 290, mx, my)) {
                    u->opt_row = 0;
                    u->opt->keyboard = k_kb_of_slot[s];
                    opt_store(u);
                    play_sound(u->as, SE_BUTTON);
                }
            for (int b = 0; b < 2; b++)
                if (tapped(u, b ? 449 : 322, 601, b ? 129 : 127, 110, mx, my)) {
                    u->opt_row = 1;
                    u->opt->guide_panel = b == 0;
                    opt_store(u);
                    play_sound(u->as, SE_BUTTON);
                }
            for (int b = 0; b < 2; b++)
                if (tapped(u, b ? 472 : 300, 722, b ? 126 : 172, 145, mx, my)) {
                    u->opt_row = 2;
                    u->opt->flower = b == 0;
                    opt_store(u);
                    play_sound(u->as, SE_BUTTON);
                }
            break;
        }
        case OPT_DISPLAY: /* TouchPauseEnable VISIBLE / INVISIBLE */
            for (int b = 0; b < 2; b++)
                if (tapped(u, b ? 458 : 322, 690, 136, 160, mx, my)) {
                    u->opt_row = 0;
                    u->opt->pause_button = b == 0;
                    opt_store(u);
                    play_sound(u->as, SE_BUTTON);
                }
            break;
        case OPT_HELP:    /* TouchHelpPage arrows; the one at an end is hidden and inert */
            for (int b = 0; b < 2; b++) {
                float ax, ay, aw, ah;
                help_button_rect(b, &ax, &ay, &aw, &ah);
                if (help_button_shown(u->help_page, b) && tapped(u, ax, ay, aw, ah, mx, my)) opt_adjust(u, b ? 1 : -1);
            }
            break;
        default: break;
        }
    }
    return -1;
}

/* WindowOption Render @0x44504: bg_tex_04, title y=182, subtitle y=234, page contents, back button */
static void render_option(UiState *u) {
    SDL_Renderer *r = u->ren;
    const Options *o = u->opt;
    const Atlas *u1 = &u->as->ui1, *u3 = &u->as->ui3, *u4 = &u->as->ui4;
    if (u->opt_mode == OPT_DLC) { render_dlc(u); return; }   /* port page, own look and back button */
    spr_tl(r, &u->as->bg_options, "bg_options", 0, 0, 255);
    render_bgfx(u);   /* WindowOption has a WindowBGEffect child too */
    if (u->opt_mode != OPT_HELP && u->opt_mode != OPT_CREDITS) {
        /* WindowOption Render @0x445d2: m_TextTitle at y 182, m_TextDesc at y 234 (capitals from 192 / 239) */
        /* setMode @0x42cb4: description OPTION_TITLE_0N from the IPA (k_opt_sub when absent), AUDIO + _01_OPT */
        static const char *desc_key[7] = {"OPTION_TITLE_00", "OPTION_TITLE_01", "OPTION_TITLE_02", "OPTION_TITLE_03",
                                          NULL, NULL, "OPTION_TITLE_04"};
        option_titles_render(r, u->as->appdir, k_opt_title[u->opt_mode], desc_key[u->opt_mode], k_opt_sub[u->opt_mode],
                             u->opt_mode == OPT_AUDIO ? "OPTION_TITLE_01_OPT" : NULL);
    }
    switch (u->opt_mode) {
    case OPT_MAIN:
        for (int i = 0; i < 6; i++) {
            float x = 129, y = k_opt_list_y[i];
            /* TouchCommonButton Render @0x646d0: like the title menu - base at pos, additive pulsing face
               at (pos.x, pos.y + 1), corner brackets at pos + (1,1) / (342,1) on every button. While held
               (m_TouchDownIdx @0x6470a) the face is the next frame, menubutton_l_on (0x35 + 1 @0x6472a), and
               the button is scaled 1.05 (@0x64988); the scaled geometry follows TouchTitleMenus (render_title).
               The port also marks the keyboard row with the "on" face. */
            bool held = u->opt_press == i;
            float s = held ? 1.05f : 1.0f;
            float bx = x, by = y, bw = 394, bh = 82, fx = x, fy = y, fw = 380.2f, fh = 68, pw = 39, ph = 68, pdx = 0, pdy = 0;
            scale_rect(s, &bx, &by, &bw, &bh);
            scale_rect(s, &fx, &fy, &fw, &fh);
            scale_rect(s, &pdx, &pdy, &pw, &ph);
            Uint8 v = pulse_level(u->ticks / 2);
            spr_tl_wh(r, u1, "menubutton_l_base", bx, by, bw, bh, 255);
            SDL_SetTextureBlendMode(u1->tex, SDL_BLENDMODE_ADD);
            SDL_SetTextureColorMod(u1->tex, v, v, v);
            spr_tl_wh(r, u1, held || (i == u->opt_row && u->menu_kbd) ? "menubutton_l_on" : "menubutton_l_off", fx, fy + 1, fw, fh, v);
            SDL_SetTextureColorMod(u1->tex, 255, 255, 255);
            SDL_SetTextureBlendMode(u1->tex, SDL_BLENDMODE_BLEND);
            spr_tl_wh(r, u1, "parenthesis_00", bx + 1, by + 1, pw, ph, 255);
            spr_tl_wh(r, u1, "parenthesis_01", bx + (held ? 358.05f : 341.0f) + 1, by + 1, pw, ph, 255);
            font_draw(r, k_opt_title[k_opt_list[i]], x + 191, y + 22, 3.2f * s, WHITE, 0);
        }
        break;
    case OPT_AUDIO: {
        spr_tl(r, u3, "volumewindow", 81, 303, 255);
        spr_tl(r, u3, "speaker", 203, 318, 255);
        static const char *rowname[2] = {"MUSIC", "SFX"};
        for (int row = 0; row < 2; row++) {
            float y = k_vol_y[row];
            int lit = vol_cells(row == 0 ? o->bgm_volume : o->se_volume);
            SDL_Color c = u->opt_row == row ? (SDL_Color){255, 240, 120, 255} : (SDL_Color){40, 60, 80, 255};
            font_draw(r, rowname[row], 320, y - 42, 2.4f, c, 0);   /* label centred at y - 42 */
            for (int i = 0; i < 16; i++)
                spr_tl(r, u3, i < lit ? "volumemeter_on" : "volumemeter_off", 113 + 26.0f * i, y, 255);
        }
        /* TouchFailSoundEnable @0x7cef8: ON (No0) and OFF (No1) buttons; the selected one uses the
           light _w face (TP_FAILEDSOUND = {on_w, on_b, off_w, off_b}) */
        spr_tl(r, u1, o->fail_sound ? "failedsound_on_w" : "failedsound_on_b", 120, 740, 255);
        spr_tl(r, u1, o->fail_sound ? "failedsound_off_b" : "failedsound_off_w", 320, 740, 255);
        if (u->opt_row == 2) {
            SDL_SetRenderDrawColor(r, 255, 240, 120, 255);
            SDL_FRect fr = {116, 736, 407, 53};
            SDL_RenderDrawRectF(r, &fr);
        }
        break;
    }
    case OPT_HAND:
        /* TouchHandSide Render @0x73100: m_Pos (37,345) left / (338,345) right; the selected (_on) face is
           drawn at m_Pos - (14,35), the unselected right one at x - 6 */
        if (o->left_hand) { spr_tl(r, u3, "lefthand_on", 23, 310, 255); spr_tl(r, u3, "righthand_off", 332, 345, 255); }
        else { spr_tl(r, u3, "lefthand_off", 37, 345, 255); spr_tl(r, u3, "righthand_on", 324, 310, 255); }
        break;
    case OPT_KEYBOARD: {
        /* positions measured on the original (emulator capture): three KeyBoardSelect panels, the
           selected one with its larger _on face at slot - (26,28); PanelSelect and FlowerUse rows below.
           The row labels are part of panelguideline / flowerline. */
        int sel = k_kb_slot[o->keyboard % 3];
        for (int slot = 0; slot < 3; slot++) {
            if (slot == sel) continue;
            char name[32];
            snprintf(name, sizeof name, "%s_off", k_kb_name[k_kb_of_slot[slot]]);
            spr_tl(r, u4, name, 20 + 202.0f * slot, 304, 255);
        }
        char on_name[32];
        snprintf(on_name, sizeof on_name, "%s_on", k_kb_name[o->keyboard % 3]);
        spr_tl(r, u4, on_name, 20 + 202.0f * sel - 26, 304 - 28, 255);
        spr_tl(r, u4, "panelguideline", 35, 643, 255);
        spr_tl(r, u4, o->guide_panel ? "panelguide_on_on" : "panelguide_on_off", 322, 601, 255);
        spr_tl(r, u4, o->guide_panel ? "panelguide_off_off" : "panelguide_off_on", 449, 601, 255);
        spr_tl(r, u4, "flowerline", 38, 779, 255);
        spr_tl(r, u4, o->flower ? "flowet_off_off" : "flowet_off_on", 428, 722, 255);
        spr_tl(r, u4, o->flower ? "flowet_on_on" : "flowet_on_off", 300, 722, 255);
        if (u->menu_kbd) {   /* port: keyboard focus */
            static const SDL_FRect focus[3] = {{14, 270, 612, 322}, {318, 597, 264, 118}, {296, 718, 306, 153}};
            SDL_SetRenderDrawColor(r, 255, 240, 120, 255);
            SDL_RenderDrawRectF(r, &focus[u->opt_row % 3]);
        }
        break;
    }
    case OPT_DISPLAY: {
        /* TouchChangeAspect @0x7bfd8: 3:2 device, so only the 3:2 phone (UI_tex_09 s_TexPos[0]) shows,
           centred; its label sits under it at y 610 */
        const Atlas *u9 = &u->as->ui9;
        if (u9->tex) {
            SDL_Rect ph = {290, 0, 165, 293};
            spr_tl_rect(r, u9, &ph, 240, 310, 165, 293, 255);
        }
        font_draw(r, "3:2 ASPECT", 320, 616, 2.6f, WHITE, 0);
        /* TouchPauseEnable Render @0x7c6e0: divider (31,783) 578x3 behind the previews (only the gap
           between them shows), "DISPLAY PAUSE" at (60,744), VISIBLE (No0) / INVISIBLE (No1) previews;
           the selected one is the large 135x115 face, labels centred under each (grey when not selected) */
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 175, 195, 215, 128);
        SDL_FRect line = {440, 783, 40, 3};
        SDL_RenderFillRectF(r, &line);
        font_draw(r, "DISPLAY PAUSE", 63, 750, 2.6f, WHITE, -1);
        static const char *plabel[2] = {"VISIBLE", "INVISIBLE"};
        for (int no = 0; no < 2; no++) {
            bool selected = o->pause_button == (no == 0);
            int idx = 2 * no + !selected;
            const int *p = k_pause_pos[idx], *t = k_pause_tex[idx];
            if (u9->tex) {
                SDL_Rect src = {t[0], t[1], t[2], t[3]};
                spr_tl_rect(r, u9, &src, (float)p[0], (float)p[1], (float)t[2], (float)t[3], 255);
            }
            font_draw(r, plabel[no], p[0] + t[2] * 0.5f, (float)(t[3] + 711), selected ? 2.6f : 2.1f,
                      selected ? WHITE : (SDL_Color){204, 204, 204, 255}, 0);
        }
        if (u->menu_kbd && u->opt_row == 0) {
            SDL_SetRenderDrawColor(r, 255, 240, 120, 255);
            SDL_FRect fr = {318, 692, 280, 158};
            SDL_RenderDrawRectF(r, &fr);
        }
        break;
    }
    case OPT_HELP:   /* WindowOption Render HELP case @0x44bc6 + WindowHelpText + TouchHelpPage (help.c) */
        help_render(r, u->as, u->help_page, u->ticks);
        break;
    case OPT_CREDITS: {
        /* WindowOption m_CreditTex: a 640x960 page in the top-left of a 1024^2 texture (credits_ja.plist) */
        const Atlas *c = &u->as->credits;
        if (c->tex) {
            SDL_Rect src = {0, 0, LOGICAL_W, LOGICAL_H};
            spr_tl_rect(r, c, &src, 0, 0, LOGICAL_W, LOGICAL_H, 255);
        }
        /* TouchGotoEnding Render @0x778e4: `ending` (TP 35) at m_Pos (487,891), z -9.2; no pressed look
           (only state 3 hides it); hidden in the port when gamedata.txt has no credits */
        if (u->gd->nending > 0) spr_tl(r, u1, "ending", 487, 891, 255);
        break;
    }
    }
    render_back_button(u);
}

static int ui_event_inner(UiState *u, const SDL_Event *e, int *song, int *diff) {
    bool click = e->type == SDL_MOUSEBUTTONUP && e->button.button == SDL_BUTTON_LEFT;
    bool down = e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT;
    bool key = e->type == SDL_KEYDOWN && !e->key.repeat;
    SDL_Keycode k = key ? e->key.keysym.sym : 0;
    float mx = (click || down) ? (float)e->button.x : 0, my = (click || down) ? (float)e->button.y : 0;
    bool ok_key = key && (k == SDLK_RETURN || k == SDLK_SPACE || k == SDLK_KP_ENTER);
    press_track(u, e);
    switch (u->scene) {
    case SCENE_TITLE:
        if (u->tstate <= 6) {                       /* logos: a tap skips to the title (orig.: only on CRI) */
            if (click || ok_key || (key && k == SDLK_ESCAPE)) { u->tstate = 7; u->tcount = 0; }
            break;
        }
        if (u->tstate == 9) {
            if (key && k == SDLK_ESCAPE) return SCENE_QUIT;
            if (click || ok_key) { play_sound(u->as, SE_BUTTON); title_start_menu(u); }   /* WindowTitle touchUp: SE 19 */
            break;
        }
        if (u->tstate == 10) {
            { int n = records_crown_event(u->as, e); if (n >= 0) return n; }   /* RECORDS: TouchGotoScore */
            if (key && (k == SDLK_ESCAPE || k == SDLK_BACKSPACE)) { u->tstate = 9; play_sound(u->as, SE_BACK); break; }   /* port: keyboard only */
            if (key && (k == SDLK_UP || k == SDLK_DOWN)) u->menu_kbd = true;
            if (key && k == SDLK_UP) u->menu_focus = (u->menu_focus + 3) % 4;
            if (key && k == SDLK_DOWN) u->menu_focus = (u->menu_focus + 1) % 4;
            if (ok_key) return menu_activate(u, u->menu_focus);
            if (down) { u->menu_kbd = false; u->menu_press = menu_hit(u, mx, my); if (u->menu_press >= 0) u->menu_focus = u->menu_press; }
            if (click) {
                int h = menu_hit(u, mx, my), p = u->menu_press;
                u->menu_press = -1;
                if (h >= 0 && h == p && tapped(u, u->menu_x[h], 538.0f + 82.0f * h, 394, 82, mx, my)) return menu_activate(u, h);
            }
        }
        break;
    case SCENE_OPTION:
        return option_event(u, e);
    case SCENE_PV:
        return pv_event(u, e, song, diff);
    case SCENE_SELECT:
        scroll_touch(u, e);
        if (back_event(u, e)) { stop_preview(u); return SCENE_TITLE; }
        if (key) {
            if (k == SDLK_ESCAPE || k == SDLK_BACKSPACE) { stop_preview(u); play_sound(u->as, SE_BACK); return SCENE_TITLE; }
            if (k == SDLK_LEFT) select_move(u, -1, 0);
            if (k == SDLK_RIGHT) select_move(u, 1, 0);
            if (k == SDLK_UP) select_move(u, 0, -1);
            if (k == SDLK_DOWN) select_move(u, 0, 1);
            if (ok_key) return start_game(u, song, diff);
        }
        if (down) {
            u->play_pressed = hit_play(mx, my);
            u->mode_pressed = hit_mode(mx, my);
        }
        if (click) {
            /* the covers only scroll (WindowCover TouchButton:: is empty): no taps on them */
            bool was_play = u->play_pressed, was_mode = u->mode_pressed;
            u->play_pressed = u->mode_pressed = false;
            if (was_mode && tapped(u, 118, 698, 404, 70, mx, my)) {           /* MODE CHANGE: Break the Limit */
                set_difficulty(u, u->diff == DIFF_BREAK ? 3 : DIFF_BREAK);
                play_sound(u->as, SE_BUTTON);
            } else if (was_play && tapped(u, 164, 873, 324, 82, mx, my)) return start_game(u, song, diff);
            else if (u->diff != DIFF_BREAK)                                 /* TouchDifficulty: labels at x 20 + 154d */
                for (int d = 0; d < 4; d++)
                    if (tapped(u, 13 + 154.0f * d, 776, 154, 72, mx, my)) {
                        if (d != u->diff) set_difficulty(u, d);
                        play_sound(u->as, SE_BUTTON);
                    }
        }
        break;
    case SCENE_RESULT:
        /* WindowResult: a touch or key while the counters run settles them; the buttons (Back -> music
           select, PLAY AGAIN -> same song) exist only from state 6. Keyboard: Enter/Esc -> music select */
        if (u->res_state < 6) {
            if (down || key) result_skip(u);
            break;
        }
        if (flickres_event(&u->fres, u->as, e)) break;   /* FLICK RESULT toggle + boards (Left/Right keys) */
        if (back_event(u, e)) { stop_bgm(u); stop_roll(u); return SCENE_SELECT; }
        if (down) u->play_pressed = hit_play(mx, my);
        if (ok_key || (key && (k == SDLK_ESCAPE || k == SDLK_BACKSPACE))) {
            stop_bgm(u); stop_roll(u); play_sound(u->as, SE_BACK); return SCENE_SELECT;
        }
        if (click) {
            bool was_play = u->play_pressed;
            u->play_pressed = false;
            if (was_play && tapped(u, 164, 873, 324, 82, mx, my)) {
                u->song = u->res.song;
                u->diff = u->res.diff;
                return start_game(u, song, diff);
            }
        }
        break;
    }
    return -1;
}

/* Port aid for mouse clicks: on iOS a button shows its held look (_on face, 1.05x) for as long as the
   finger is down; a mouse click (or a trackpad tap, whose press and release can arrive in the same
   frame) is often over before a frame shows it. A release less than HOLD_MIN_MS after the press keeps
   the held looks it ends until dn_ms + HOLD_MIN_MS (ui_render). The release itself is handled at once,
   as before (the action fires, a scene change starts its fade). */
int ui_event(UiState *u, const SDL_Event *e, int *song, int *diff) {
    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) u->lg_until = 0;
    if (e->type == SDL_MOUSEBUTTONUP && e->button.button == SDL_BUTTON_LEFT && SDL_GetTicks() - u->dn_ms < HOLD_MIN_MS) {
        u->lg_play = u->play_pressed; u->lg_back = u->back_pressed; u->lg_mode = u->mode_pressed;
        u->lg_menu = u->menu_press; u->lg_opt = u->opt_press;
        u->lg_until = u->dn_ms + HOLD_MIN_MS;
    }
    return ui_event_inner(u, e, song, diff);
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
        else if (u->scene == SCENE_SELECT || u->scene == SCENE_PV) select_tick(u);
        else if (u->scene == SCENE_OPTION) {
            u->credits_t++; bgfx_exec(u); bgfx_exec(u);
            if (u->opt_mode == OPT_HELP) help_tick();   /* WindowHelpText exec */
        }
        else if (u->scene == SCENE_RESULT) result_tick(u);
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
    { unsigned b = u->blink % 200;
      const Frame *ef = atlas_find(u5, b < 3 ? "eye_00" : "eye_01");
      if (u->tstate >= 9 && b >= 1 && b < 5 && ef) {
          /* port: inset by one texel; at the fractional bob y the linear filter would blend in the empty
             atlas row above the eyes and draw a dark edge around the patch (the patch matches the face) */
          SDL_Rect in = {ef->r.x + 1, ef->r.y + 1, ef->r.w - 2, ef->r.h - 2};
          spr_tl_rect(r, u5, &in, 321, bob + 110, (float)in.w, (float)in.h, 255);
      } }
    spr_tl(r, u5, "outerkey_b", 63, 411, 255);
    spr_tl(r, u5, "outerkey_p", 535, 148, 255);
    spr_tl(r, u5, "outerkey_r", 434, 533, 255);
    spr_tl(r, u5, "outerkey_t", 115, 76, 255);
    spr_tl(r, u5, "cubes_p", 472, 103, 255);
    spr_tl(r, u5, "cubes_y", 0, 48, 255);
    render_titlefx(u);   /* z -4.8: over Miku, keys and cubes (0..-2.1), under logo/copyright (-5) */
    spr_tl(r, u5, "titlelogo_00", 12, 242, 255);
    spr_tl(r, u5, "titlelogo_01", 320, 242, 255);
    spr_tl(r, u5, "copyright", 99, 922, 255);
    if (u->tstate == 9) {
        float blink = 0.5f + 0.5f * sinf((float)u->blink * 0.4f * 0.333f);
        spr_tl(r, u1, "touchstart", 172, 662, (Uint8)(90 + 165 * blink));
    }
    if (u->tstate == 10) {
        /* TouchTitleMenus Render @0x6abb4 (positions top-left, as everywhere): base at pos (blend 1);
           face off/on additive (blend 2 @0x6aed8) at (pos.x, pos.y + 1.8), 0.2 px larger - the face is the
           size of the base's frame, not inset; corner brackets parenthesis_00/_01 (white, @0x6af54 /
           @0x6afcc) at pos + (1,1) and pos + (342,1), tracing the frame corners. While touched every
           rect is scaled 1.05x about its own centre, the face moves (+0.5,-1.5) and the right bracket
           sits 358.05 px from the left one. The port also shows keyboard focus with the "on" face. */
        bool sliding = false;
        for (int i = 0; i < 4; i++) if (u->menu_x[i] > 128.0f) sliding = true;
        for (int i = 0; i < 4; i++) {
            float x = u->menu_x[i], y = 538.0f + 82.0f * i;
            bool touched = u->menu_press == i;
            bool lit = touched || (u->menu_kbd && i == u->menu_focus);
            float s = touched ? 1.05f : 1.0f;
            float bx = x, by = y, bw = 394, bh = 82, fx = x, fy = y, fw = 380, fh = 68, pw = 39, ph = 68, pdx = 0, pdy = 0;
            scale_rect(s, &bx, &by, &bw, &bh);
            scale_rect(s, &fx, &fy, &fw, &fh);
            scale_rect(s, &pdx, &pdy, &pw, &ph);
            if (touched) { fx += 0.5f; fy -= 1.5f; }
            spr_tl_wh(r, u1, "menubutton_l_base", bx, by, bw, bh, 255);
            /* colour and alpha (v,v,v,v), additive: adds tex * v^2; m_Counter is held at 11 while the
               buttons slide in */
            Uint8 v = pulse_level(sliding ? 11 : u->blink / 2);
            SDL_SetTextureBlendMode(u1->tex, SDL_BLENDMODE_ADD);
            SDL_SetTextureColorMod(u1->tex, v, v, v);
            spr_tl_wh(r, u1, lit ? "menubutton_l_on" : "menubutton_l_off", fx, fy + 1.8f, fw + 0.2f, fh + 0.2f, v);
            SDL_SetTextureColorMod(u1->tex, 255, 255, 255);
            SDL_SetTextureBlendMode(u1->tex, SDL_BLENDMODE_BLEND);
            spr_tl_wh(r, u1, "parenthesis_00", bx + 1, by + 1, pw, ph, 255);
            spr_tl_wh(r, u1, "parenthesis_01", bx + (touched ? 358.05f : 341.0f) + 1, by + 1, pw, ph, 255);
            SDL_Color col = WHITE;   /* SHOP is white too on the device even though it is not ported */
            /* label: 24-px capitals from y + 24, centred (device screenshot) */
            font_draw(r, k_menu_label[i], x + 193, y + 22, 3.25f * s, col, 0);
        }
        records_crown_render(r, u1);   /* RECORDS: TouchGotoScore at (14,893) (was crownicon_off at (14,878)) */
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

/* WindowCover Render @0x36290: 316x316 quad, x centred on pos.x - scroll, top at pos.y = 293
   (WindowMusicSelect loadTexture: Vector2D(x, 293) @0x40f14), with stretched edges,
   plus a 120-px reflection underneath */
static void render_cover(UiState *u, int i) {
    Cover *c = &u->cov[i];
    float cx = c->x - u->scr, cy = 293.0f + 158.0f, h = 158.0f;
    if (cx < -200 || cx > LOGICAL_W + 200) return;   /* before loading: off-screen covers need no art sheet */
    const Frame *f = music_frame(u, i, "art");
    Atlas *m = music_atlas(u, i);
    if (!m || !f || !m->tex) return;
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

/* TouchBack initType:No:Pos: case 4 @0x6de3e (pos (17,873): movw 0x4000 / movt 0x445a) / Render @0x6e270
   (music select, PV select, options, result): menubutton_s_base and the face both at pos (the face
   sprite is the size of the base's frame, the base adds the shadow; it pulses like the PLAY face),
   "back" at pos + (34,18),
   corner brackets parenthesis_00/_01 at pos + (1,1) and pos + (77,1).
   While pressed every rect is scaled 1.05x about its own centre, the face becomes menubutton_s_on and
   the right bracket moves from pos.x + 76 to pos.x + 79.8 (the left bracket's (-2.5, +1.2) shift is
   inferred from the same block). */
static void spr_scaled(SDL_Renderer *r, const Atlas *a, const char *name, float x, float y, float s, Uint8 alpha) {
    const Frame *f = atlas_find(a, name);
    if (!f) return;
    float w = (float)f->r.w, h = (float)f->r.h;
    scale_rect(s, &x, &y, &w, &h);
    spr_tl_wh(r, a, name, x, y, w, h, alpha);
}

static void render_back_button(UiState *u) {
    const Atlas *u1 = &u->as->ui1;
    bool p = u->back_pressed;
    float s = p ? 1.05f : 1.0f;
    spr_scaled(u->ren, u1, "menubutton_s_base", 17, 873, s, 255);
    Uint8 v = pulse_level(u->ticks / 2);   /* face colour = alpha = v (CoordinateColor, like the PLAY face) */
    SDL_SetTextureColorMod(u1->tex, v, v, v);
    spr_scaled(u->ren, u1, p ? "menubutton_s_on" : "menubutton_s_off", 17, 873, s, v);
    SDL_SetTextureColorMod(u1->tex, 255, 255, 255);
    spr_scaled(u->ren, u1, "back", 51, 891, s, 255);
    spr_scaled(u->ren, u1, "parenthesis_00", p ? 18 - 2.5f : 18, p ? 874 + 1.2f : 874, s, 255);
    spr_scaled(u->ren, u1, "parenthesis_01", p ? 17 + 79.8f + 1 : 94, 874, s, 255);
}

/* TouchMusicSelect initType:No:Pos: pos (164,873) / Render @0x6d510 (music select PLAY; the result
   screen's PLAY AGAIN uses the same button). Every rect is scaled 1.05x about its own centre while
   pressed. base at pos; face rect (pos.x+1, pos.y+0.5, 310, 68.5) drawn at x-1 and 2 px wider; brackets
   from the fixed rect (165,874,39,68): parenthesis_00 at x-1, parenthesis_01 at x + 271*scale (x - 7
   first when pressed). The label keeps the device-screenshot position (24-px capitals from y 896). */
static void render_play_button(UiState *u, const char *label, bool pulse) {
    SDL_Renderer *r = u->ren;
    const Atlas *u1 = &u->as->ui1;
    float s = u->play_pressed ? 1.05f : 1.0f;
    float bx = 164, by = 873, bw = 324, bh = 82;
    float fx = 165, fy = 873.5f, fw = 310, fh = 68.5f;
    float px = 165, py = 874, pw = 39, ph = 68;
    scale_rect(s, &bx, &by, &bw, &bh);
    scale_rect(s, &fx, &fy, &fw, &fh);
    scale_rect(s, &px, &py, &pw, &ph);
    if (u->play_pressed) px -= 7;
    spr_tl_wh(r, u1, "menubutton_m_base", bx, by, bw, bh, 255);
    /* the face pulses (CoordinateColor on m_Counter, colour = alpha = v, blend 1); the label stays white */
    Uint8 v = pulse_level(pulse ? u->pulse / 2 : 0);
    SDL_SetTextureColorMod(u1->tex, v, v, v);
    spr_tl_wh(r, u1, u->play_pressed ? "menubutton_m_on" : "menubutton_m_off", fx - 1, fy, fw + 2, fh, v);
    SDL_SetTextureColorMod(u1->tex, 255, 255, 255);
    font_draw(r, label, 326, 913 - 18 * s, 3.25f * s, WHITE, 0);
    spr_tl_wh(r, u1, "parenthesis_00", px - 1, py, pw, ph, 255);
    spr_tl_wh(r, u1, "parenthesis_01", px + 271 * s, py, pw, ph, 255);
}

/* rank mark of a cleared difficulty (WindowMusicSelect RenderRankMarkByRank:texIdx:posX:posY:
   @0x4204c): rank 0 PERFECT star, 1 S, 2 A, 3 B, 4 C; 35x35, top-left. Full size on the selected
   difficulty (texIdx 0xe), 0.9 (0x3f666666) otherwise, scaled about the centre of the 35x35 rect. */
static void render_rank_mark(UiState *u, const Record *rec, float x, float y, bool selected) {
    static const char *k_mark[5] = {"clearrank_star", "clearrank_s", "clearrank_a", "clearrank_b", "clearrank_c"};
    if (rec->cleared && rec->rank >= 0 && rec->rank <= 4)
        spr_scaled(u->ren, &u->as->ui1, k_mark[rec->rank], x, y, selected ? 1.0f : 0.9f, 255);
}

/*
 * WindowMusicSelect Render @0x41a08 + children (WindowCover, TouchMusicSelect, TouchDifficulty,
 * TouchModeChange, WindowBGEffect). bg (0,0); additive cubes; cover flow; logo (64,43) 512x256
 * (WindowCover renderLogo @0x36a0c, z -3, no fade); 9 difficultyemblem stars at (166+34i, 614)
 * (none on Break the Limit); "HIGH SCORE:/BPM:" at y=660; MODE CHANGE (118,698); difficulty labels
 * at x 20/174/328/482, y 799 (TouchDifficulty updateText: @0x700f8) with the gliding
 * difficultycursor at (CsrX, 777); clear-rank marks (RenderClearStar @0x41e94) at y 783;
 * baseline (0,824); PLAY = menubutton_m_base at (164,873) + face + brackets at (164 / 165+271, 874).
 */
static void render_select(UiState *u) {
    SDL_Renderer *r = u->ren;
    const Atlas *u1 = &u->as->ui1;
    const SongInfo *si = &u->gd->songs[u->song];
    spr_tl(r, &u->as->bg_select, "bg_misicselect", 0, 0, 255);
    render_bgfx(u);

    /* cover flow, far covers first */
    int n = nvis(u);
    for (int d = n; d >= 1; d--) {
        if (u->song - d >= 0) render_cover(u, u->song - d);
        if (u->song + d < n) render_cover(u, u->song + d);
    }
    render_cover(u, u->song);

    /* WindowCover renderLogo @0x36a0c: native-size logo, in front of the covers */
    {
        Atlas *lm = music_atlas(u, u->song);
        const Frame *lf = music_frame(u, u->song, "logo");
        if (lm && lf) spr_tl_rect(r, lm, &lf->r, 64, 43, (float)lf->r.w, (float)lf->r.h, 255);
    }

    char buf[160];
    if (u->scene == SCENE_PV) {
        /* ScenePVSelect with PLAYLIST off (the playlist editor is not ported, and off is the original's
           default): WindowPVSelect Render draws the "N SONG SELECTED" text (y 700), updateTouchPVProgram
           shows the TouchPVProgram play mark (112x104 at (265,612)) and checkRandomEnable enables
           SHUFFLE only while GetPVModeFlg:0 (PLAYLIST) is set. The text and play mark are left out; SHUFFLE stays
           available (port deviation: without the playlist editor it would otherwise be unreachable).
           TouchPVLoopMode update: @0x717e0: 109x93 toggles at x = 25 + 120i, y = 742 */
        for (int i = 0; i < 5; i++) {
            char name[32];
            bool *f = pv_flag(u, i);
            snprintf(name, sizeof name, "%s_%s", k_pv_icon[i], f && *f ? "on" : "off");
            spr_tl(r, u1, name, 25 + 120.0f * i, 742, 255);
            if (i == u->pv_focus && u->menu_kbd) {   /* port: keyboard focus */
                SDL_SetRenderDrawColor(r, 120, 240, 230, 255);
                SDL_FRect fr = {25 + 120.0f * i - 2, 740, 113, 97};
                SDL_RenderDrawRectF(r, &fr);
            }
        }
    } else {
    if (u->diff != DIFF_BREAK)   /* Break the Limit: RenderClearStarBTL instead of the stars */
        for (int s = 0; s < 9; s++)
            spr_tl(r, u1, s < si->stars[u->diff] ? "difficultyemblem_00" : "difficultyemblem_01", 166 + 34.0f * s, 614, 255);
    const Record *rec = &u->save->rec[u->song][u->diff];
    snprintf(buf, sizeof buf, "HIGH SCORE:%d  BPM:%d", rec->score, si->bpm);
    font_draw(r, buf, 320, 660, 2.6f, WHITE, 0);

    /* TouchModeChange m_Pos (118,678) + 20; the _on face while held */
    spr_tl(r, u1, u->mode_pressed ? "modechange_on" : "modechange_off", 118, 698, 255);

    if (u->diff == DIFF_BREAK) {
        spr_tl_wh(r, u1, "difficultycursor", 155, 777, 328, 66, 255);
        spr_tl(r, u1, "difficulty_on_04", 208, 799, 255);
        render_rank_mark(u, &u->save->rec[u->song][DIFF_BREAK], 430, 783, true);
    } else {
        static const float k_label_x[4] = {20, 174, 328, 482}, k_mark_x[4] = {116, 300, 430, 602};
        spr_tl(r, u1, "difficultycursor", u->csr_x, 777, 255);
        for (int d = 0; d < 4; d++) {
            char name[32];
            snprintf(name, sizeof name, "difficulty_%s_%02d", d == u->diff ? "on" : "off", d);
            spr_tl(r, u1, name, k_label_x[d], 799, 255);
        }
        for (int d = 0; d < 4; d++) render_rank_mark(u, &u->save->rec[u->song][d], k_mark_x[d], 783, d == u->diff);
    }
    spr_tl(r, u1, "baseline", 0, 824, 255);
    }

    render_back_button(u);
    render_play_button(u, "PLAY", true);
}

/* draw "art"/"logo" frame of a song top-left at (x,y) stretched to w x h */
static void draw_music_frame_tl(UiState *u, int song, const char *kind, float x, float y, float w, float h) {
    const Frame *f = music_frame(u, song, kind);
    Atlas *m = music_atlas(u, song);
    if (m && f) spr_tl_rect(u->ren, m, &f->r, x, y, w, h, 255);
}

/* sprite scaled by rate about the centre of its full-size top-left rect (WindowResult m_FailSizeRate /
   m_HighScoreSizeRate draws: PosX = x + (1-rate)*w/2, Width = rate*w) */
static void spr_pop(SDL_Renderer *r, const Atlas *a, const char *name, float x, float y, float rate) {
    const Frame *f = atlas_find(a, name);
    if (!f || rate <= 0.0f) return;
    float w = (float)f->r.w, h = (float)f->r.h;
    spr_tl_wh(r, a, name, x + (1.0f - rate) * w / 2, y + (1.0f - rate) * h / 2, rate * w, rate * h, 255);
}

/* WindowResult RenderBgRect @0x4e610: 9-slice resultwindow_00..07 spanning (81,223)-(576,841); the
   centre is a 338x441 stretch of a 2x2 texel block inside resultwindow_05 (RenderTexClip) */
static void render_result_panel(SDL_Renderer *r, const Atlas *u2) {
    const Frame *mid = atlas_find(u2, "resultwindow_05");
    if (mid) {
        SDL_Rect src = {mid->r.x + 1, mid->r.y + 1, 2, 2};
        spr_tl_rect(r, u2, &src, 144, 298, 338, 441, 255);
    }
    for (int x = 167; x < 482; x += 63) {
        spr_tl(r, u2, "resultwindow_04", (float)x, 223, 255);
        spr_tl(r, u2, "resultwindow_05", (float)x, 739, 255);
    }
    for (int y = 298; y < 739; y += 63) {
        spr_tl(r, u2, "resultwindow_06", 81, (float)y, 255);
        spr_tl(r, u2, "resultwindow_07", 482, (float)y, 255);
    }
    spr_tl(r, u2, "resultwindow_00", 81, 223, 255);
    spr_tl(r, u2, "resultwindow_01", 482, 223, 255);
    spr_tl(r, u2, "resultwindow_02", 81, 739, 255);
    spr_tl(r, u2, "resultwindow_03", 482, 739, 255);
}

/*
 * WindowResult Render @0x4e8a8 / RenderScore @0x4dbdc / loadTexture @0x4ba80 (positions top-left).
 * The original depth-sorts by PosZ; this draws back to front in that order:
 *   bg (z 2) -> panel (1.2) -> clearrankstamp (349,224) and perfect (383,291) (-0.1) -> jacket
 *   (90,67) 142x142 (-0.2) -> logo (279,67) 256x128 (-0.3) -> titlebar (104,238) (-0.5) -> text bars
 *   (-0.9) -> clearrank_NN (394,298), empty stars, difficulty text (-1.0) -> filled stars, labels
 *   (-1.1) -> failed (-3.5) -> newrecord (-3.6).
 * Labels are TexTextManager slots at x 124/206/131/132; the values are WindowCounter children
 * right-aligned at x 340/435/510 (table @0x13c0c8, top = y - 6). Text is SDL_ttf here.
 */
static void render_result(UiState *u) {
    SDL_Renderer *r = u->ren;
    const GameResult *res = &u->res;
    const Atlas *u2 = &u->as->ui2;
    spr_tl(r, &u->as->bg_result, "bg_result", 0, 0, 255);
    render_bgfx(u);
    render_result_panel(r, u2);

    /* rank stamp: lands with the rank SE once the counters are done (state 1 on) */
    /* flick page (TouchButton 0x20): Render @0x4ec06 skips RenderScore and all after it */
    bool score_page = !u->fres.page;
    bool stamped = u->res_state >= 1 && score_page;
    if (stamped && res->rank >= 0) {
        spr_tl(r, u2, "clearrankstamp", 349, 224, 255);
        if (res->rank == 0) spr_tl(r, u2, "perfect", 383, 291, 255);
    }
    draw_music_frame_tl(u, res->song, "art", 90, 67, 142, 142);
    draw_music_frame_tl(u, res->song, "logo", 279, 67, 256, 128);

    spr_tl(r, u2, "titlebar", 104, 238, 255);
    const Frame *tb = atlas_find(u2, "titlebar");
    float tbh = tb ? (float)tb->r.h : 53;
    static const char *diffname[NUM_DIFF] = {"Easy", "Normal", "Hard", "Extreme", "Break The Limit"};

    /* RenderScore: RenderUnderLine stretches the 4x23 textbar sprite into a 23-px strip behind
       each row: textbar_00 x 114 (w 233 / 412), textbar_01 x 181 (w 278) */
    static const struct { const char *bar; float x, y, w; } bars[] = {
        {"textbar_00", 114, 316, 233}, {"textbar_00", 114, 354, 233},
        {"textbar_01", 181, 403, 278}, {"textbar_01", 181, 441, 278}, {"textbar_01", 181, 479, 278},
        {"textbar_01", 181, 517, 278}, {"textbar_01", 181, 555, 278}, {"textbar_01", 181, 606, 278},
        {"textbar_00", 114, 652, 412}, {"textbar_00", 114, 690, 412}, {"textbar_00", 114, 728, 412},
    };
    for (size_t i = 0; score_page && i < sizeof bars / sizeof *bars; i++)
        spr_tl_wh(r, u2, bars[i].bar, bars[i].x, bars[i].y, bars[i].w, 23, 255);

    if (stamped && res->rank > 0) {
        char name[32];
        snprintf(name, sizeof name, "clearrank_%02d", res->rank - 1);   /* frame 110 + m_Rank */
        spr_tl(r, u2, name, 394, 298, 255);
    }
    /* difficulty stars (GetDifficultNum): 9 empty resultstat_00 then the filled resultstat_01 at
       (290 + 26*i, 191); none for Break the Limit */
    if (res->diff != DIFF_BREAK) {
        int stars = u->gd->songs[res->song].stars[res->diff];
        for (int i = 0; i < 9; i++) spr_tl(r, u2, "resultstat_00", 290 + 26.0f * i, 191, 255);
        for (int i = 0; i < stars && i < 9; i++) spr_tl(r, u2, "resultstat_01", 290 + 26.0f * i, 191, 255);
    }
    /* TexTextManager draws a text texture top-left at (x, y); its capitals start ~0.3 cap-heights
       lower. Measured on a device screenshot: title capitals 29 px from y 249 (Render y 240), labels
       20 px from y + 6, counter digits from table y + 6. font_draw's y is the top of the capitals. */
    (void)tbh;
    font_draw(r, diffname[res->diff], 320, 247, 3.9f, WHITE, 0);

    /* labels: RenderScore @0x4dbdc (x, y); values: WindowCounter right-top at e_ResultCounterPos
       (0x13c0c8) with y - 6 */
    /* rows in CheckCounter order (values in u->res_val) */
    static const struct { const char *label; float lx, ly, vx, vy; } rows[11] = {
        {"Total Notes", 124, 308, 340, 308},
        {"Combo", 124, 346, 340, 346},
        {"Cool", 206, 394, 435, 394},
        {"Fine", 206, 432, 435, 432},
        {"Safe", 206, 470, 435, 470},
        {"Sad", 206, 508, 435, 508},
        {"Worst", 206, 547, 435, 547},
        {"\xe2\x99\xaa", 206, 597, 435, 597},   /* "♪" */
        {"Stage Score", 131, 644, 510, 644},
        {"Combo Bonus", 131, 682, 510, 682},
        {"Total Score", 132, 720, 510, 720},
    };
    for (int i = 0; score_page && i < 11; i++) {
        /* the "♪" glyph sits 4 px further right in the original; the bitmap fallback font has no ♪,
           so draw the quaver sprite then */
        if (i == 7 && !text_has_glyph(0x266A)) spr_tl(r, &u->as->game1, "quaver_00", rows[i].lx - 6, rows[i].ly - 26, 255);
        else font_draw(r, rows[i].label, rows[i].lx + (i == 7 ? 4 : 0), rows[i].ly + 6, 2.6f, WHITE, -1);
        if (i > u->res_idx) continue;   /* WindowCounter not activated yet */
        /* WindowCounter: while counter i rolls, digit k (k = 1 the units) shows (ticks % 10) until
           12k ticks have passed; then the real digit. Thousands are grouped with a comma ("18,572"). */
        char d[16], buf[32];
        snprintf(d, sizeof d, "%d", u->res_val[i] < 0 ? 0 : u->res_val[i]);
        int len = (int)strlen(d), t = u->res_ctr[i];
        if (i == u->res_idx)
            for (int k = 1; k <= len; k++) if (t < 12 * k) d[len - k] = (char)('0' + t % 10);
        int o = 0;
        for (int j = 0; j < len; j++) {
            if (j > 0 && (len - j) % 3 == 0) buf[o++] = ',';
            buf[o++] = d[j];
        }
        buf[o] = 0;
        font_draw(r, buf, rows[i].vx, rows[i].vy + 6, 2.6f, WHITE, 1);
    }

    /* bottom buttons (device screenshot): TouchBack at (17,873) and PLAY AGAIN on the music select
       PLAY button, created in state 5 -> 6 with the FLICK RESULT toggle. REPLAY and Twitter are not ported. */
    flickres_render(r, u->as, u->gd, u->opt, res, &u->fres);   /* boards (flick page only) */
    if (u->res_state >= 6) {
        render_back_button(u);
        render_play_button(u, "PLAY AGAIN", false);
        flickres_render_button(r, u->as, &u->fres);   /* TouchResultChange (530,784), z -9.5 */
    }

    /* WindowResult exec @0x4d6ac: FAILED (m_ContentOpen == 0, i.e. rank D or E, never on Break the
       Limit) grows by 0.2/tick in state 1, NEW RECORD the same way in state 4. Both scale about the
       centre of their full-size rect. */
    if (score_page && res->rank >= 5 && res->diff != DIFF_BREAK) spr_pop(r, u2, "failed", 359, 319, u->res_fail_rate);
    if (score_page && u->new_record) spr_pop(r, u2, "newrecord", 461, 328, u->res_rec_rate);
}

void ui_render(UiState *u) {
    /* a quick click's held looks (ui_event) stay up to HOLD_MIN_MS after the press */
    bool lg = u->lg_until && (Sint32)(u->lg_until - SDL_GetTicks()) > 0;
    bool s_play = u->play_pressed, s_back = u->back_pressed, s_mode = u->mode_pressed;
    int s_menu = u->menu_press, s_opt = u->opt_press;
    if (lg) {
        u->play_pressed |= u->lg_play; u->back_pressed |= u->lg_back; u->mode_pressed |= u->lg_mode;
        if (u->menu_press < 0) u->menu_press = u->lg_menu;
        if (u->opt_press < 0) u->opt_press = u->lg_opt;
    }
    SDL_SetRenderDrawColor(u->ren, 0, 0, 0, 255);
    SDL_RenderClear(u->ren);
    switch (u->scene) {
    case SCENE_TITLE: render_title(u); break;
    case SCENE_SELECT: case SCENE_PV: render_select(u); break;
    case SCENE_OPTION: render_option(u); break;
    case SCENE_RESULT: render_result(u); break;
    default: break;
    }
    if (lg) {
        u->play_pressed = s_play; u->back_pressed = s_back; u->mode_pressed = s_mode;
        u->menu_press = s_menu; u->opt_press = s_opt;
    }
}
