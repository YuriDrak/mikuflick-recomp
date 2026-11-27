/*
 * OPTIONS -> HELP (WindowHelpText) and the localized OPTIONS titles (WindowOption setMode / Render).
 *
 * All text comes from the user's IPA at run time: <app>/<lang>.lproj/Localizable.strings, a binary
 * plist read with bplist.c. Like the original's IsJapanese() (first preferred language "ja"), the
 * first SDL_GetPreferredLocales() entry picks ja.lproj (and the Japanese layout table) or en.lproj;
 * MIKUFLICK_LANG=ja|en overrides it, and the other language is used if one file is missing. Without
 * the file the help pages show only their
 * images and the titles fall back to the strings in ui.c.
 *
 * Help layout (all from the binary, 640x960 coordinates, top-left positions):
 *   WindowOption Render HELP case @0x44bc6: helpline (0,60); the same frame turned 180 degrees from
 *     (0,825) to (640,837) (RenderTex2DFlex); a navy panel RenderRect2D (0,72) 640x753, GetColor(0.5,
 *     0,0.01,0.16) = (0,3,41) at alpha 128; ten page dots helpmeter_on/off at (190 + 25*i, 840).
 *   WindowHelpText setPage: @0x3b8a4 / Render @0x3bff4: title HELP_TITLE_%02d (font 36, bold) centred
 *     at x 320, top y 90; page counter "%d/%d" (font 36) right-aligned at x 590, centred on the Back
 *     button row (873 + 82/2 - 8); lines HELP_TEXT_%02d_%d (line, page; font 24, bold, white) at x 96;
 *     line i is s_tblHelpOffset_{Eng,Jpn}[page][0..i-1] below the first one (@0x13b650 / @0x13acf0);
 *     the first line is at 150 - scroll (+522 on pages whose screenshot sits above the text). The
 *     screenshots are 322x482 panels of help_01/help_02 drawn at x 159. Text and screenshots are
 *     clipped by glScissor(0, 145, 640, 683) = screen rows 132..815.
 *   Page 9 (RenderLicence @0x3c940 / RenderLicenceText: @0x3ca44): HELP_TEXT_LIC000..338, font 28,
 *     x 96, 30 px apart from 150 - scroll, from a 512 px wide text texture. It has no title
 *     (HELP_TITLE_09 is empty and the title texture is cleared).
 *   TouchHelpPage (type 0x16, initType @0x6c8c0, Render @0x6cee8, RenderButton::: @0x6cb74): small
 *     menu buttons like Back, prev at x 200 (arrow_00 at (224,891)), next at x 323 (arrow_01 at
 *     (351,891)), y 874; collision m_Pos*0.5 points + (60,37) points = (196|329, 874) 120x74 px. The
 *     one at an end is in state 3: not drawn, not touchable.
 * Scroll: WindowHelpText init @0x3bdb0: ScrollInit, ScrollSetTouchArea (0,0)-(640,960) (UIKit points,
 *   i.e. the whole screen), ScrollSetLimit (0,0)-(0,12000); exec @0x3bf20 runs WindowBase ScrollExec
 *   @0x341b0 then clamps m_ScrPos.y to 0..m_PageColumnCount (the sum of the page's offset table; 9600
 *   on the licence page). setPage resets m_ScrPos.y to 0 (m_ScrAdd is kept).
 */
#include "mf.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HELP_LINES 60
#define LIC_LINES 339
#define CLIP_Y 132        /* 960 - (145 + 683) */
#define CLIP_H 683

/* ------------------------------------------------------------ Localizable.strings */

static struct {
    bool tried, ja;
    BpNode *dict;
} L;

static void loc_load(const char *appdir) {
    if (L.tried) return;
    L.tried = true;
    /* IsJapanese(): [NSLocale preferredLanguages][0] is "ja" -> ja, anything else -> en.
       MIKUFLICK_LANG (ja / en) overrides the system locale. */
    const char *env = getenv("MIKUFLICK_LANG");
    bool want_ja = false;
    if (env && *env) {
        want_ja = !strncmp(env, "ja", 2);
    } else {
#if SDL_VERSION_ATLEAST(2, 0, 14)
        SDL_Locale *loc = SDL_GetPreferredLocales();
        want_ja = loc && loc[0].language && !strcmp(loc[0].language, "ja");
        SDL_free(loc);
#endif
    }
    const char *lang[2] = {want_ja ? "ja" : "en", want_ja ? "en" : "ja"};
    for (int i = 0; i < 2 && !L.dict; i++) {
        char path[1200];
        size_t len = 0;
        snprintf(path, sizeof path, "%s/%s.lproj/Localizable.strings", appdir ? appdir : ".", lang[i]);
        uint8_t *data = read_file(path, &len);
        if (!data) continue;
        BpNode *d = bp_parse(data, len);
        free(data);
        if (d && d->type == BP_DICT) { L.dict = d; L.ja = !strcmp(lang[i], "ja"); log_msg("help: %s", path); }
        else bp_free(d);
    }
    if (!L.dict) log_msg("help: no Localizable.strings in %s (help text and option subtitles unavailable)", appdir);
}

const char *loc_string(const char *appdir, const char *key) {
    loc_load(appdir);
    const BpNode *n = bp_get(L.dict, key);
    return n && n->type == BP_STR ? n->s : NULL;
}

/* ------------------------------------------------------------ layout tables */

/* s_tblHelpOffset_Eng @0x13b650 / s_tblHelpOffset_Jpn @0x13acf0: int[9][60], y step from line i to
   line i+1 (only the non-zero prefix is listed; the rest are 0, i.e. empty lines at the same y). The
   sum up to the first 0 is m_PageColumnCount, the scroll limit. */
static const short k_off_eng[9][HELP_LINES] = {
    {30,60,30,30,30,60,30,30,60,30,30,60,30,60,30,30,60},
    {30,60,30,30,30,30,60,30,60,30,30,60,30,30,30,30,30,30,30,30,30,30,30,60,30,60},
    {30,60,30,30,30,30,30,60,30,30,30,30,30,30,30,30,30,30,30,30,60,30,30,30,30,30,30,60,30,30,30,30,30,30,60},
    {30,30,30,30,30,30,30,30,30,30,30,60,30,60,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,
     30,30,60,30,30,30,30,30,30,30,30,30,60},
    {30,30,30,30,30,590,30,30,30,30,30,60},
    {60,30,30,60,30,30,30,30,30,30,30,30,60,30,60,30,60,30,60,30,30,30,60,30,60,30,30,60,30,60,30,30,30,30,60},
    {590,31,30,30,30,60,31,60,31,60,31,30,30,60,31,60,31,60,590,31,60,31,30,60,31,30,60,-590},
    {590,30,60,30,60,30,30,30,60,30,30,30,60,30,30,30,60,30,30,60,30,30,60,30,60,30,30,60,30,30,30,30,30,30,60,
     30,30,60,30,30,30,30,30,30,30,30,60,-590},
    {30,30,30,30,590,30,30,60},
};
static const short k_off_jpn[9][HELP_LINES] = {
    {30,60,30,30,30,60,30,60,30,30,60,30,60,30,30,30,30},
    {30,60,30,30,30,30,30,60,30,30,30,60,30,30,60,30,30,30,30,30,30,30,30,30,30,60,30,30,60,30,30},
    {30,60,30,30,30,30,30,60,30,60,30,30,30,30,30,30,30,30,30,30,30,30,60,30,30,30,30,30,30,30,30,30,60,30,30,
     30,30,30,30,30},
    {30,30,30,30,30,50,30,30,30,30,30,30,60,30,60,30,30,30,30,60,30,60,30,60,30,30,60,30,30,30,30,60,30,30,30,
     30,30,60,30,30,30,30,30,60,30,30},
    {30,30,30,30,590,30,30,30,30,30,30,30,30,60},
    {60,30,30,30,30,30,30,30,30,30,30,30,60,30,60,30,60,30,30,30,30,60,30,30,60,30,30,30,30,60},
    {590,30,30,30,30,60,30,30,60,30,30,60,30,30,30,60,30,60,30,60,590,30,30,60,30,30,30,60,30,30,60,-590},
    {590,30,60,30,30,60,30,30,30,30,60,30,30,30,60,30,30,30,30,60,30,30,60,30,30,60,30,30,60,30,30,60,30,30,30,
     30,30,30,30,30,30,60,30,30,30,30,30,30,30,60,30,30,30,60,-590},
    {30,30,30,30,590,30,30,30,30,60},
};

/* WindowHelpText Render @0x3c23c (jump table on m_Page): screenshots of help_01 (tex 0) / help_02
   (tex 1), source (u,v) 322x482, drawn at (159, 150 - scroll + y); y_ja is the IsJapanese() value.
   text = offset of the first text line (s16: 522 below one screenshot, 0 on pages 6 and 7). */
typedef struct { signed char tex; short u, v, y, y_ja; } HelpImage;
static const struct { HelpImage img[2]; short text; } k_page[9] = {
    {{{0, 0, 0, 10, 10}, {-1, 0, 0, 0, 0}}, 522},          /* TITLE SCREEN */
    {{{0, 322, 0, 10, 10}, {-1, 0, 0, 0, 0}}, 522},        /* MUSIC SELECT */
    {{{0, 644, 0, 10, 10}, {-1, 0, 0, 0, 0}}, 522},        /* RHYTHM GAME 1 */
    {{{0, 0, 482, 10, 10}, {-1, 0, 0, 0, 0}}, 522},        /* RHYTHM GAME 2 */
    {{{0, 322, 482, 10, 10}, {0, 644, 482, 740, 710}}, 522},   /* RHYTHM GAME 3: 10 + (Jpn ? 700 : 730) */
    {{{1, 0, 0, 10, 10}, {-1, 0, 0, 0, 0}}, 522},          /* RESULT */
    {{{1, 322, 0, 80, 80}, {1, 644, 0, 1366, 1410}}, 0},   /* OPTION MENU/RESULTS */
    {{{1, 0, 482, 70, 70}, {-1, 0, 0, 0, 0}}, 0},          /* PV/OTHER */
    {{{1, 322, 482, 10, 10}, {1, 644, 482, 710, 710}}, 522},   /* SHOP/SONG MANAGEMENT: 10 + 700 */
};

/* ------------------------------------------------------------ state */

static struct {
    int page;
    bool loaded;                 /* help_set_page ran */
    const char *title, *line[HELP_LINES];
    const char *lic[LIC_LINES];
    bool lic_loaded;
    int limit;                   /* m_PageColumnCount */
    /* WindowBase scroll, y axis only (the x limits are equal, so ScrollExec's x factor is 0) */
    float pos, add, start, sw_st, sw_ed;   /* m_ScrPos.y m_ScrAdd.y m_ScrStartPos.y m_ScrSwipStPos.y m_ScrSwipEdPos.y */
    bool swipe;                  /* m_IsScrSwipe */
    /* the touch held on a page button (TouchBase m_TouchDownIdx), for the pressed face */
    bool held;
    float dn_x, dn_y, dn_mx, dn_my;
    Uint32 dn_ms, lg_until;      /* port: a quick click keeps the pressed face 100 ms from the press */
} H;

static const short *page_offsets(int page) { return (L.ja ? k_off_jpn : k_off_eng)[page]; }

void help_set_page(const char *appdir, int page) {
    loc_load(appdir);
    if (page < 0) page = 0;
    if (page >= HELP_PAGES) page = HELP_PAGES - 1;
    H.page = page;
    H.loaded = true;
    H.pos = 0;                   /* setPage: m_ScrPos.y = 0 */
    char key[32];
    if (page == HELP_PAGES - 1) {
        H.limit = 9600;          /* m_PageColumnCount = 0x2580 */
        H.title = NULL;
        if (!H.lic_loaded) {
            for (int i = 0; i < LIC_LINES; i++) {
                snprintf(key, sizeof key, "HELP_TEXT_LIC%03d", i);   /* "%@%003d" */
                H.lic[i] = loc_string(appdir, key);
            }
            H.lic_loaded = true;
        }
        return;
    }
    snprintf(key, sizeof key, "HELP_TITLE_%02d", page);
    H.title = loc_string(appdir, key);
    for (int i = 0; i < HELP_LINES; i++) {
        snprintf(key, sizeof key, "HELP_TEXT_%02d_%d", i, page);
        H.line[i] = loc_string(appdir, key);
    }
    const short *off = page_offsets(page);
    H.limit = 0;
    for (int i = 0; i < HELP_LINES && off[i]; i++) H.limit += off[i];   /* stops at the first 0 */
}

static void impulse(float v) { H.add += v; }

/* WindowHelpText touchDown:/touchMoved:/touchUp: -> WindowBase ScrollTouch* (@0x344c8 / 0x345a4 /
   0x3469c). The touch area is the whole screen. Returns true for the keys/wheel it used (port only:
   UP/DOWN = 4 lines, PAGE UP/DOWN = 20 lines, HOME/END; they push m_ScrAdd so ScrollExec glides). */
bool help_event(const SDL_Event *e) {
    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
        H.swipe = true;
        H.start = H.pos;
        H.sw_st = H.sw_ed = (float)e->button.y;
        H.held = true;
        H.dn_x = (float)e->button.x; H.dn_y = (float)e->button.y;
        H.dn_mx = H.dn_my = 0;
        H.dn_ms = SDL_GetTicks();
        H.lg_until = 0;
        return false;
    }
    if (e->type == SDL_MOUSEMOTION && (e->motion.state & SDL_BUTTON_LMASK)) {
        if (!H.swipe) { H.start = H.pos; H.sw_st = (float)e->motion.y; H.swipe = true; }
        H.sw_ed = (float)e->motion.y;
        if (H.held) {
            H.dn_mx = fmaxf(H.dn_mx, fabsf((float)e->motion.x - H.dn_x));
            H.dn_my = fmaxf(H.dn_my, fabsf((float)e->motion.y - H.dn_y));
        }
        return false;
    }
    if (e->type == SDL_MOUSEBUTTONUP && e->button.button == SDL_BUTTON_LEFT) {
        H.swipe = false;
        H.sw_ed = (float)e->button.y;
        if (H.held && SDL_GetTicks() - H.dn_ms < 100) H.lg_until = H.dn_ms + 100;
        H.held = false;
        return false;
    }
    if (e->type == SDL_MOUSEWHEEL) { impulse(-40.0f * (float)e->wheel.y); return true; }
    if (e->type == SDL_KEYDOWN) {
        switch (e->key.keysym.sym) {
        case SDLK_UP: impulse(-40); return true;
        case SDLK_DOWN: impulse(40); return true;
        case SDLK_PAGEUP: impulse(-200); return true;
        case SDLK_PAGEDOWN: impulse(200); return true;
        case SDLK_HOME: H.pos = 0; H.add = 0; return true;
        case SDLK_END: H.pos = (float)H.limit; H.add = 0; return true;
        default: break;
        }
    }
    return false;
}

/* one 30 Hz WindowHelpText exec */
void help_tick(void) {
    /* ScrollExec: while swiping, pos = start + (st - ed) * 2 in UIKit points = 1:1 in pixels */
    if (H.swipe) {
        float old = H.pos;
        H.pos = H.start + (H.sw_st - H.sw_ed);
        H.add = H.pos - old;
    } else {
        H.pos += H.add;
        if (H.pos < 0) { H.pos += (0 - H.pos) * 0.5f; if (H.pos >= 0) H.pos = 0; }
        if (H.pos > 12000) { H.pos += (H.pos - 12000) * -0.5f; if (H.pos <= 12000) H.pos = 12000; }
    }
    H.add /= 1.5f;
    if (fabsf(H.add) < 0.1f) H.add = 0;
    /* exec @0x3bf74: clamp to the page */
    if (H.pos > (float)H.limit) H.pos = (float)H.limit;
    else if (H.pos < 0) H.pos = 0;
}

/* ------------------------------------------------------------ render */

static const SDL_Color HWHITE = {255, 255, 255, 255};

/* TexTextManager draws a text texture with its top-left at (x,y); font_draw_plain's y is the top of the
   capitals, which for M+ 1p lands within a pixel or two of the iOS box top, so the box top is passed */
static void text_at(SDL_Renderer *r, const char *s, float x, float y, int px, int align, float max_w) {
    if (s && *s) font_draw_plain_fit(r, s, x, y, (float)px / 10.0f, HWHITE, align, max_w);
}

/* M+ 1p Bold is wider than the iOS font the lines were broken for; condense (not wrap) each line to
   the 512 px the licence text texture has (x 96..608), which the help lines also stay inside */
#define TEXT_W 512.0f

/* TouchHelper CoordinateColor pulse, as the Back button (ui.c pulse_level) */
static Uint8 pulse(unsigned counter) {
    int i = (int)(counter % 22);
    if (i > 10) i = 21 - i;
    return (Uint8)(255.0f * 0.85f * powf(0.98f, (float)i) + 0.5f);
}

static void scaled(SDL_Renderer *r, const Atlas *a, const char *name, float x, float y, float s, Uint8 alpha) {
    const Frame *f = atlas_find(a, name);
    if (!f) return;
    float w = (float)f->r.w, h = (float)f->r.h;
    x -= w * (s - 1) * 0.5f; y -= h * (s - 1) * 0.5f;
    spr_tl_wh(r, a, name, x, y, w * s, h * s, alpha);
}

/* TouchHelpPage collision rects (No 0 prev, No 1 next) */
void help_button_rect(int b, float *x, float *y, float *w, float *h) {
    *x = b ? 329.0f : 196.0f;
    *y = 874.0f;
    *w = 120.0f;
    *h = 74.0f;
}

bool help_button_shown(int page, int b) { return b ? page < HELP_PAGES - 1 : page > 0; }

static bool button_pressed(int b) {
    float x, y, w, h;
    help_button_rect(b, &x, &y, &w, &h);
    bool lg = H.lg_until && (Sint32)(H.lg_until - SDL_GetTicks()) > 0;   /* port: quick click, see ui_event */
    return (H.held || lg) && H.dn_x >= x && H.dn_x < x + w && H.dn_y >= y && H.dn_y < y + h && H.dn_mx <= w && H.dn_my <= h;
}

/* TouchHelpPage Render / RenderButton:::: base menubutton_s_base at (bx,874), pulsing face at bx-1
   (2 px wider), brackets parenthesis_00/_01 at px-1 and px+75 (px-2.2 / +79.8 while pressed), all
   scaled 1.05 while pressed; then the arrow (not scaled) */
static void render_page_button(SDL_Renderer *r, const Assets *as, int b, unsigned ticks) {
    const Atlas *u1 = &as->ui1, *u3 = &as->ui3;
    const float bx = b ? 323.0f : 200.0f, px = b ? 324.0f : 202.0f;
    bool p = button_pressed(b);
    float s = p ? 1.05f : 1.0f;
    scaled(r, u1, "menubutton_s_base", bx, 874, s, 255);
    const Frame *f = atlas_find(u1, "menubutton_s_off");
    if (f) {
        Uint8 v = pulse(ticks / 2);
        float w = (f->r.w + 2) * s, h = f->r.h * s;
        float x = bx - 1 - (f->r.w + 2) * (s - 1) * 0.5f, y = 874 - f->r.h * (s - 1) * 0.5f;
        SDL_SetTextureColorMod(u1->tex, v, v, v);
        spr_tl_wh(r, u1, p ? "menubutton_s_on" : "menubutton_s_off", x, y, w, h, v);
        SDL_SetTextureColorMod(u1->tex, 255, 255, 255);
    }
    scaled(r, u1, "parenthesis_00", p ? px - 2.2f : px - 1, 874, s, 255);
    scaled(r, u1, "parenthesis_01", p ? px - 2.2f + 79.8f : px - 1 + 76, 874, s, 255);
    spr_tl(r, u3, b ? "arrow_01" : "arrow_00", b ? 351.0f : 224.0f, 891, 255);
}

void help_render(SDL_Renderer *r, const Assets *as, int page, unsigned ticks) {
    const Atlas *u3 = &as->ui3;
    if (!H.loaded || page != H.page) help_set_page(as->appdir, page);
    /* WindowOption Render HELP case: panel, lines, page dots */
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 3, 41, 128);
    SDL_FRect panel = {0, 72, 640, 753};
    SDL_RenderFillRectF(r, &panel);
    spr_tl(r, u3, "helpline", 0, 60, 255);
    spr_rot(r, u3, "helpline", 320, 831, 180, 1.0f, 255);
    for (int i = 0; i < HELP_PAGES; i++)
        spr_tl(r, u3, i == page ? "helpmeter_on" : "helpmeter_off", 190 + 25.0f * i, 840, 255);

    /* WindowHelpText Render: screenshots and text inside the scissor */
    float top = 150.0f - H.pos;
    SDL_Rect clip = {0, CLIP_Y, LOGICAL_W, CLIP_H};
    if (page == HELP_PAGES - 1) {
        clip.x = 96; clip.w = 512;   /* RenderLicence: 512 px of the text texture */
        SDL_RenderSetClipRect(r, &clip);
        for (int i = 0; i < LIC_LINES; i++) {
            float y = top + 30.0f * i;
            if (y < CLIP_Y - 40 || y > CLIP_Y + CLIP_H) continue;
            text_at(r, H.lic[i], 96, y, 28, -1, TEXT_W);
        }
    } else {
        SDL_RenderSetClipRect(r, &clip);
        for (int k = 0; k < 2; k++) {
            const HelpImage *im = &k_page[page].img[k];
            const Atlas *t = im->tex >= 0 ? &as->help[im->tex] : NULL;
            if (!t || !t->tex) continue;
            SDL_Rect src = {im->u, im->v, 322, 482};
            spr_tl_rect(r, t, &src, 159, top + (L.ja ? im->y_ja : im->y), 322, 482, 255);
        }
        const short *off = page_offsets(page);
        float y = top + k_page[page].text;
        for (int i = 0; i < HELP_LINES; i++) {
            if (y >= CLIP_Y - 40 && y <= CLIP_Y + CLIP_H) text_at(r, H.line[i], 96, y, 24, -1, TEXT_W);
            y += off[i];
        }
    }
    SDL_RenderSetClipRect(r, NULL);

    /* title (font 36, centred, top 90) and page counter (font 36, right edge 590, on the Back row) */
    text_at(r, H.title, 320, 90, 36, 0, 600.0f);
    char num[16];
    snprintf(num, sizeof num, "%d/%d", page + 1, HELP_PAGES);
    text_at(r, num, 590, 885, 36, 1, 0.0f);

    for (int b = 0; b < 2; b++)
        if (help_button_shown(page, b)) render_page_button(r, as, b, ticks);
}

/* ------------------------------------------------------------ OPTIONS titles */

/* WindowOption setMode @0x42cb4: title (literal, font 48) and description OPTION_TITLE_0N (font 24,
   bold only in Japanese); AUDIO adds OPTION_TITLE_01_OPT. Render @0x44504: both centred at x 320
   (title top 182, description top 234), except AUDIO, where the description is left-aligned at
   x 140 - (Japanese ? 0 : 22) and the second part at that + 22 + 136 (one sentence in two textures).
   Each is drawn over a navy copy at +2,+2: GetColor(1, 0.015, 0, 0.28) = (4,0,71). The port keeps
   ui.c's measured sizes/baselines (title scale 4.4 with capitals from y 189, description 2.4 at 235). */
void option_titles_render(SDL_Renderer *r, const char *appdir, const char *title, const char *desc_key,
                          const char *desc_fallback, const char *desc2_key) {
    static const SDL_Color navy = {4, 0, 71, 255};
    const char *desc = desc_key ? loc_string(appdir, desc_key) : NULL;
    const char *desc2 = desc && desc2_key ? loc_string(appdir, desc2_key) : NULL;
    if (!desc) desc = desc_fallback;
    if (title && *title) {
        font_draw_plain(r, title, 322, 191, 4.4f, navy, 0);
        font_draw_plain(r, title, 320, 189, 4.4f, HWHITE, 0);
    }
    if (!desc || !*desc) return;
    if (desc2) {
        /* @0x4469e / @0x447d6: OPTION_TITLE_01 left-aligned at x = 140 - (Japanese ? 0 : 22) and
           OPTION_TITLE_01_OPT at 140 + (Japanese ? 0 : 22) + 136, i.e. en 118 / 298, ja 140 / 276.
           M+ is wider than the iOS font, so the first half is condensed to end a space (6 px) before
           the second, and the second to end by x 620. */
        float x = L.ja ? 140.0f : 118.0f, x2 = L.ja ? 276.0f : 298.0f;
        font_draw_plain_fit(r, desc, x + 2, 237, 2.4f, navy, -1, x2 - x - 6);
        font_draw_plain_fit(r, desc, x, 235, 2.4f, HWHITE, -1, x2 - x - 6);
        font_draw_plain_fit(r, desc2, x2 + 2, 237, 2.4f, navy, -1, 620 - x2);
        font_draw_plain_fit(r, desc2, x2, 235, 2.4f, HWHITE, -1, 620 - x2);
    } else {
        font_draw_plain(r, desc, 322, 237, 2.4f, navy, 0);
        font_draw_plain(r, desc, 320, 235, 2.4f, HWHITE, 0);
    }
}
