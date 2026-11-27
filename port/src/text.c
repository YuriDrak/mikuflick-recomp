/*
 * TrueType text (replaces the iOS system-font rendering of TexTextManager).
 * Font search order: --font PATH, <exe dir>/fonts/MPLUS1p-Bold.ttf (shipped, SIL OFL),
 * ./port/fonts/MPLUS1p-Bold.ttf, then common system fonts. Falls back to the
 * built-in bitmap font when nothing can be opened.
 */
#include "mf.h"

#include <SDL_ttf.h>
#include <stdio.h>
#include <string.h>

#define MAX_SIZES 16
#define CACHE 160

typedef struct { int px; TTF_Font *f; } SizedFont;
typedef struct {
    char text[128];
    int px;
    Uint32 rgba;
    bool shadow;
    SDL_Texture *tex;
    int w, h;
    Uint32 used;
} Entry;

static struct {
    char path[1280];
    bool ok;
    SizedFont sizes[MAX_SIZES];
    int nsizes;
    Entry cache[CACHE];
    Uint32 clock;
} T;

static const char *k_system_fonts[] = {
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
    "/usr/share/fonts/noto-cjk/NotoSansCJK-Bold.ttc",
    "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
    NULL,
};

static bool try_font(const char *p) {
    if (!p || !*p) return false;
    TTF_Font *f = TTF_OpenFont(p, 24);
    if (!f) return false;
    TTF_CloseFont(f);
    snprintf(T.path, sizeof T.path, "%s", p);
    return true;
}

bool text_init(const char *font_path) {
    memset(&T, 0, sizeof T);
    if (TTF_Init() != 0) { log_msg("SDL_ttf: %s", TTF_GetError()); return false; }
    if (font_path && !strcmp(font_path, "none")) { log_msg("text: bitmap font (forced)"); return false; }
    bool found = false;
    if (font_path) {
        found = try_font(font_path);
        if (!found) log_msg("text: cannot open --font %s", font_path);
    }
    if (!found) {
        char *base = SDL_GetBasePath();
        if (base) {
            char p[1200];
            snprintf(p, sizeof p, "%sfonts/MPLUS1p-Bold.ttf", base);
            found = try_font(p);
            SDL_free(base);
        }
    }
    if (!found) found = try_font("port/fonts/MPLUS1p-Bold.ttf");
    for (int i = 0; !found && k_system_fonts[i]; i++) found = try_font(k_system_fonts[i]);
    T.ok = found;
    if (found) log_msg("text: %s", T.path);
    else log_msg("text: no TrueType font found, using the bitmap font");
    return found;
}

static TTF_Font *font_px(int px) {
    for (int i = 0; i < T.nsizes; i++) if (T.sizes[i].px == px) return T.sizes[i].f;
    TTF_Font *f = TTF_OpenFont(T.path, px);
    if (!f) return NULL;
    TTF_SetFontHinting(f, TTF_HINTING_LIGHT);
    if (T.nsizes == MAX_SIZES) { TTF_CloseFont(T.sizes[0].f); memmove(T.sizes, T.sizes + 1, sizeof T.sizes[0] * (MAX_SIZES - 1)); T.nsizes--; }
    T.sizes[T.nsizes++] = (SizedFont){px, f};
    return f;
}

static Entry *lookup(SDL_Renderer *ren, const char *text, int px, SDL_Color c, bool shadow) {
    Uint32 rgba = (Uint32)c.r << 24 | (Uint32)c.g << 16 | (Uint32)c.b << 8 | c.a;
    Entry *victim = &T.cache[0];
    T.clock++;
    for (int i = 0; i < CACHE; i++) {
        Entry *e = &T.cache[i];
        if (e->tex && e->px == px && e->rgba == rgba && e->shadow == shadow && !strcmp(e->text, text)) { e->used = T.clock; return e; }
        if (!e->tex || e->used < victim->used) victim = e;
    }
    TTF_Font *f = font_px(px);
    if (!f) return NULL;
    SDL_Color col = {c.r, c.g, c.b, 255};
    SDL_Surface *s = TTF_RenderUTF8_Blended(f, text, col);
    if (!s) return NULL;
    if (victim->tex) SDL_DestroyTexture(victim->tex);
    victim->tex = SDL_CreateTextureFromSurface(ren, s);
    victim->w = s->w;
    victim->h = s->h;
    SDL_FreeSurface(s);
    if (!victim->tex) return NULL;
    SDL_SetTextureBlendMode(victim->tex, SDL_BLENDMODE_BLEND);
    snprintf(victim->text, sizeof victim->text, "%s", text);
    victim->px = px;
    victim->rgba = rgba;
    victim->shadow = shadow;
    victim->used = T.clock;
    return victim;
}

/* size: legacy bitmap scale (7*scale px cap height); y is the top of the capitals; align -1/0/1 */
void font_draw(SDL_Renderer *ren, const char *text, float x, float y, float scale, SDL_Color col, int align) {
    if (!T.ok || !text[0]) { bitmap_font_draw(ren, text, x, y, scale, col, align); return; }
    int px = (int)(scale * 10.0f + 0.5f);
    Entry *e = lookup(ren, text, px, col, false);
    Entry *sh = lookup(ren, text, px, (SDL_Color){0, 0, 0, 255}, true);
    if (!e) { bitmap_font_draw(ren, text, x, y, scale, col, align); return; }
    /* the original's iOS UI font is narrower than M+ 1p Bold: measured against device screenshots
       (title menu, result labels) the same strings come out 7-11% wider, so condense to match */
    float w = (float)e->w * 0.93f, h = (float)e->h;
    float dx = align < 0 ? x : align == 0 ? x - w / 2 : x - w;
    float dy = y - h * 0.2f;   /* TTF line box starts above the cap height */
    if (sh) {
        SDL_FRect d2 = {dx + 2, dy + 2, w, h};
        SDL_SetTextureAlphaMod(sh->tex, (Uint8)(col.a * 0.6f));
        SDL_RenderCopyF(ren, sh->tex, NULL, &d2);
    }
    SDL_FRect d = {dx, dy, w, h};
    SDL_SetTextureAlphaMod(e->tex, col.a);
    SDL_RenderCopyF(ren, e->tex, NULL, &d);
}

bool text_has_ttf(void) { return T.ok; }

void text_quit(void) {
    for (int i = 0; i < CACHE; i++) if (T.cache[i].tex) SDL_DestroyTexture(T.cache[i].tex);
    for (int i = 0; i < T.nsizes; i++) TTF_CloseFont(T.sizes[i].f);
    memset(&T, 0, sizeof T);
    TTF_Quit();
}
