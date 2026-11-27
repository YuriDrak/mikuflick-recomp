/*
 * Result screen, FLICK RESULT page (WindowResult m_FlickResultMode).
 *
 * - TouchResultChange (touch type 0x20): the page toggle, created with PLAY AGAIN and Back in
 *   WindowResult exec state 5 (@0x4d9d4). WindowResult TouchButton:: 0x20 @0x4f30e flips the mode
 *   at once (no slide): the ten boards SetState 0/3 and the 11 WindowCounter children
 *   SetIsEnabled 0/1. Render @0x4e8a8 then skips RenderScore and everything after it (stamp,
 *   PERFECT, clear rank, FAILED, NEW RECORD, the counters); bg, jacket, logo, the window,
 *   difficulty stars, title bar and difficulty name stay.
 * - TouchResultFlickBoard (type 0x21), one per key, created hidden in loadTexture @0x4d3e2 at
 *   e_ResultFlickBoardPos. Each shows the key's success rate (FINE/COOL) over the chart's notes.
 *   A touch-down on a board toggles its direction guide (m_TouchDown; checkFlick is never called
 *   for it, so the finger's direction does not matter): the board then shows its centre (tap)
 *   numbers and four balloons show the flick directions; the other boards are dimmed and ignore
 *   touches until it is closed again (s_mBoardDown, WindowResult CheckFlickBoardIsDown @0x4db40).
 *
 * Positions are top-left, as TPManager RenderTex. The original depth-sorts by PosZ (more negative
 * is nearer); each element is drawn here in that order.
 */
#include "mf.h"

#include <stdio.h>
#include <string.h>

/* TouchResultChange initType:No:Pos: @0x63aac: m_Pos forced to (530,784) (0x44048000, 0x44440000);
   collision = the rect of frame 125 flickresult_off (67x67) */
#define CHG_X 530.0f
#define CHG_Y 784.0f
#define CHG_W 67.0f
#define CHG_H 67.0f

/* e_ResultFlickBoardPos @0x13c0a0 (u16 x, u16 y): board top-left per key, 0 あ .. 9 わ */
static const short k_board_pos[NUM_KEYS][2] = {
    {130, 338}, {257, 338}, {385, 338}, {130, 446}, {257, 446},
    {385, 446}, {130, 554}, {257, 554}, {385, 554}, {257, 662},
};
/* TouchResultFlickBoard collision: m_pColSize (63,54) in 320x480 points = 126x108 px at m_Pos */
#define BOARD_CW 126.0f
#define BOARD_CH 108.0f

/* s_VowelIndices @0x162350: the key's own kana on the board, TP 370 + n = fgcharacter_ori_NN */
static const int k_vowel[NUM_KEYS] = {0, 5, 10, 15, 20, 25, 30, 35, 38, 43};

static const SDL_Color WHITE = {255, 255, 255, 255};

void flickres_reset(FlickRes *f) {
    memset(f, 0, sizeof *f);
    f->open = -1;
}

static bool in_rect(float x, float y, float rx, float ry, float rw, float rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

/* port: a quick click keeps the held look until 100 ms after the press (see ui.c ui_event) */
static const FlickRes *s_lg_f;
static Uint32 s_dn_ms, s_lg_until;

bool flickres_event(FlickRes *f, Assets *as, const SDL_Event *e) {
    if (e->type == SDL_MOUSEBUTTONUP && e->button.button == SDL_BUTTON_LEFT && f->held && SDL_GetTicks() - s_dn_ms < 100) {
        s_lg_f = f;
        s_lg_until = s_dn_ms + 100;
    }
    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
        float mx = (float)e->button.x, my = (float)e->button.y;
        s_lg_until = 0;
        if (in_rect(mx, my, CHG_X, CHG_Y, CHG_W, CHG_H)) {   /* TouchBase touchDown: held */
            s_dn_ms = SDL_GetTicks();
            f->held = true;
            f->dn_x = mx; f->dn_y = my;
            f->mv_x = f->mv_y = 0;
        } else if (f->page) {
            /* TouchResultFlickBoard touchDown @0x6943c: m_TouchDown = !s_mBoardDown, so a touch on the
               open board closes it, a touch on another board does nothing while one is open */
            for (int k = 0; k < NUM_KEYS; k++)
                if (in_rect(mx, my, k_board_pos[k][0], k_board_pos[k][1], BOARD_CW, BOARD_CH)) {
                    if (f->open == k) f->open = -1;
                    else if (f->open < 0) f->open = k;
                    break;
                }
        }
        return false;
    }
    if (e->type == SDL_MOUSEMOTION && (e->motion.state & SDL_BUTTON_LMASK) && f->held) {
        float dx = (float)e->motion.x - f->dn_x, dy = (float)e->motion.y - f->dn_y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx > f->mv_x) f->mv_x = dx;
        if (dy > f->mv_y) f->mv_y = dy;
        return false;
    }
    if (e->type == SDL_MOUSEBUTTONUP && e->button.button == SDL_BUTTON_LEFT) {
        float mx = (float)e->button.x, my = (float)e->button.y;
        bool was = f->held;
        f->held = false;
        /* TouchResultChange touchUp @0x63e24: released inside by the touch that pressed it (and not
           dragged further than the button's size, TouchBase) -> TouchCallback, then SE 0x13 */
        if (was && in_rect(mx, my, CHG_X, CHG_Y, CHG_W, CHG_H) && f->mv_x <= CHG_W && f->mv_y <= CHG_H) {
            f->page = !f->page;
            play_sound(as, 19);
        }
        return false;
    }
    /* port: keyboard equivalents. Left/Right = the toggle button; Up/Down open the previous/next
       board's direction guide (one at a time, past the end closes it) */
    if (e->type == SDL_KEYDOWN && !e->key.repeat) {
        SDL_Keycode k = e->key.keysym.sym;
        if (k == SDLK_LEFT || k == SDLK_RIGHT || k == SDLK_TAB) {
            f->page = !f->page;
            play_sound(as, 19);
            return true;
        }
        if (f->page && (k == SDLK_UP || k == SDLK_DOWN)) {
            int n = f->open + (k == SDLK_DOWN ? 1 : -1);
            if (n >= NUM_KEYS) n = -1;
            if (n < -1) n = NUM_KEYS - 1;
            f->open = n;
            return true;
        }
    }
    return false;
}

/* frame drawn scaled by s about the centre of its top-left rect (TouchHelper CoordinateScalingPoints) */
static void spr_scaled_tl(SDL_Renderer *r, const Atlas *a, const char *name, float x, float y, float s) {
    const Frame *fr = atlas_find(a, name);
    if (!fr) return;
    float w = (float)fr->r.w, h = (float)fr->r.h;
    spr_tl_wh(r, a, name, x + (w - w * s) / 2, y + (h - h * s) / 2, w * s, h * s, 255);
}

/* TouchResultChange Render @0x63c4c (z -9.5): frame 125 flickresult_off on the score page, 160
   scoreresult_off on the flick page; while held the next frame (_on) at 1.05x */
void flickres_render_button(SDL_Renderer *ren, const Assets *as, const FlickRes *f) {
    char name[32];
    bool held = f->held || (s_lg_f == f && s_lg_until && (Sint32)(s_lg_until - SDL_GetTicks()) > 0);
    snprintf(name, sizeof name, "%s_%s", f->page ? "scoreresult" : "flickresult", held ? "on" : "off");
    spr_scaled_tl(ren, &as->ui2, name, CHG_X, CHG_Y, held ? 1.05f : 1.0f);
}

/* SystemTexture 3 = the key-face texture LoadBoardTex picked for the KEYBOARD option */
static const Atlas *key_atlas(const Assets *as, const Options *opt) {
    int k = opt ? opt->keyboard : 0;
    if (k >= 1 && k <= 2 && as->game2_kb[k - 1].tex) return &as->game2_kb[k - 1];
    return &as->game2;
}

/* TouchResultFlickBoard setColor: @0x67e1c: m_BaseRank by rate; a key with no notes keeps the
   initial 145 (resultpanel_04, dark) */
static const char *panel_frame(int succ, int total) {
    if (total < 1) return "resultpanel_04";
    int rate = (int)((float)(succ * 100) / (float)total);
    return rate < 50 ? "resultpanel_00" : rate < 65 ? "resultpanel_01" : rate < 80 ? "resultpanel_02"
         : rate < 101 ? "resultpanel_03" : "resultpanel_04";
}

/* initType:No:Pos: @0x677b4: "%d%%" (font 26) of (int)(succ * 100.0f / total), "Non" without notes;
   "%d/%d" (font 18) */
static void rate_text(char *buf, size_t n, int succ, int total) {
    if (total > 0) snprintf(buf, n, "%d%%", (int)((float)succ * 100.0f / (float)total));
    else snprintf(buf, n, "Non");
}

/* TexTextManager places a text texture by its top-left; for this font the line box is ~1.2x the
   point size and the capitals start ~0.24x the size below its top (ui.c labels: size 25, +6). */
#define BOX_H(pt) ((float)(int)((pt) * 1.2f + 0.5f))
#define CAP_OFS(pt) ((float)(int)((pt) * 0.24f + 0.5f))

static int sum_dirs(const int v[NUM_FLICK_DIRS]) {
    int s = 0;
    for (int d = 0; d < NUM_FLICK_DIRS; d++) s += v[d];
    return s;
}

/* RenderGuide: @0x68d40 draws a balloon for every direction the key has a character for (the
   argument, m_FlickResult, is ignored and always 0 = unlit petals). Per balloon (RenderGuideUp
   @0x67ea8 / Down @0x68248 / Left @0x68994 / Right @0x685f4): flickguide_off_NN at pos + petal,
   the direction's glyph (fgcharacter_ori, e_GuideCharPictIdx) at pos + glyph behind it, the rate
   text with its top-left at glyph + (15 - 57/2, 5 + 57/2) and "%d/%d" right-aligned at petal.x +
   right, top glyph.y - 5. z: Down/Left/Right glyph -1.1, petal -1.2, rate -1.3/-1.4; Up -2.1,
   -2.2, -2.3/-2.4; the "%d/%d" texts -2.9..-3.1. */
static const struct { int petal; float px, py, gx, gy, right; int dir; } k_guide[4] = {
    {1, 7, 82, 36, 136, 114 - 15, 3},        /* down: petal 114 wide, right = w - 10 - 5 */
    {2, -127, 3, -90, 26, 154 - 45, 4},      /* left: w - 40 - 5 */
    {3, 99, 3, 160, 26, 154 - 15, 2},        /* right */
    {0, 7, -109, 36, -85, 114 - 15, 1},      /* up (drawn last: nearer) */
};

static void render_guides(SDL_Renderer *ren, const Assets *as, const GameData *gd, const Options *opt,
                          const GameResult *res, int key) {
    const Atlas *ka = key_atlas(as, opt);
    float x = k_board_pos[key][0], y = k_board_pos[key][1];
    char name[32], buf[32];
    for (int i = 0; i < 4; i++) {
        int dir = k_guide[i].dir, gi = gd->guide[key][dir];
        if (gi < 0) continue;
        float gx = x + k_guide[i].gx, gy = y + k_guide[i].gy;
        snprintf(name, sizeof name, "fgcharacter_ori_%02d", gi);
        spr_tl(ren, ka, name, gx, gy, 255);
        snprintf(name, sizeof name, "flickguide_off_%02d", k_guide[i].petal);
        spr_tl(ren, &as->game1, name, x + k_guide[i].px, y + k_guide[i].py, 255);
        rate_text(buf, sizeof buf, res->flick_success[key][dir], res->flick_total[key][dir]);
        font_draw(ren, buf, gx + 15 - 28, gy + 5 + 28 + CAP_OFS(26), 2.6f, WHITE, -1);
    }
    for (int i = 0; i < 4; i++) {
        int dir = k_guide[i].dir;
        if (gd->guide[key][dir] < 0) continue;
        snprintf(buf, sizeof buf, "%d/%d", res->flick_success[key][dir], res->flick_total[key][dir]);
        font_draw(ren, buf, x + k_guide[i].px + k_guide[i].right, y + k_guide[i].gy - 5 + CAP_OFS(18), 1.8f, WHITE, 1);
    }
}

/* TouchResultFlickBoard Render @0x68e04 */
void flickres_render(SDL_Renderer *ren, const Assets *as, const GameData *gd, const Options *opt,
                     const GameResult *res, const FlickRes *f) {
    if (!f->page) return;
    const Atlas *u2 = &as->ui2, *ka = key_atlas(as, opt);
    const Frame *pf = atlas_find(u2, "resultpanel_00");
    float pw = pf ? (float)pf->r.w : 123, ph = pf ? (float)pf->r.h : 104;
    char name[32], buf[32];
    for (int k = 0; k < NUM_KEYS; k++) {
        float x = k_board_pos[k][0], y = k_board_pos[k][1];
        int tot = sum_dirs(res->flick_total[k]), suc = sum_dirs(res->flick_success[k]);
        /* the key's kana (SystemTexture 3, z -0.6) at pos + (31.75, 27), under the panel (z -0.7) */
        snprintf(name, sizeof name, "fgcharacter_ori_%02d", k_vowel[k]);
        spr_tl(ren, ka, name, x + 31.75f, y + 27, 255);
        spr_tl(ren, u2, panel_frame(suc, tot), x, y, 255);
        /* texts: m_TextRate/m_TextRateDetail entry 0 (all directions), entry 1 (centre) on the open
           board. Rate bottom-left at (pos.x + 10, pos.y + h - textH - 5); "%d/%d" top-right at
           (pos.x + w - textW - 10, pos.y + 5); white (z -0.9) over a 2 px shadow 0xFF0F0F0F (-0.8). */
        int s = k == f->open ? res->flick_success[k][0] : suc, t = k == f->open ? res->flick_total[k][0] : tot;
        rate_text(buf, sizeof buf, s, t);
        font_draw(ren, buf, x + 10, y + ph - BOX_H(26) - 5 + CAP_OFS(26), 2.6f, WHITE, -1);
        snprintf(buf, sizeof buf, "%d/%d", s, t);
        font_draw(ren, buf, x + pw - 10, y + 5 + CAP_OFS(18), 1.8f, WHITE, 1);
        /* another board is open: frame 145 over this one (z -1.0) */
        if (f->open >= 0 && f->open != k) spr_tl(ren, u2, "resultpanel_04", x, y, 255);
    }
    if (f->open >= 0 && f->open < NUM_KEYS) render_guides(ren, as, gd, opt, res, f->open);
}

/* --start-scene result: plausible per-key counters for the sample result (keys あ..わ, directions
   centre/up/right/down/left); every panel colour appears, わ has no notes ("Non", dark panel) */
void flickres_demo(GameResult *res) {
    static const int tot[NUM_KEYS][NUM_FLICK_DIRS] = {
        {4, 2, 1, 3, 2}, {3, 1, 2, 2, 1}, {2, 1, 0, 2, 1}, {5, 2, 2, 1, 2}, {2, 0, 1, 1, 0},
        {1, 1, 0, 1, 2}, {2, 1, 1, 0, 1}, {1, 1, 0, 1, 0}, {2, 1, 1, 1, 1}, {0, 0, 0, 0, 0},
    };
    static const int suc[NUM_KEYS][NUM_FLICK_DIRS] = {
        {4, 2, 1, 3, 2}, {2, 1, 1, 2, 1}, {1, 0, 0, 1, 1}, {5, 2, 1, 1, 2}, {1, 0, 0, 0, 0},
        {1, 1, 0, 1, 1}, {1, 1, 0, 0, 1}, {1, 0, 0, 1, 0}, {2, 1, 1, 1, 1}, {0, 0, 0, 0, 0},
    };
    memcpy(res->flick_total, tot, sizeof tot);
    memcpy(res->flick_success, suc, sizeof suc);
}
