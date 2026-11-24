/* PNG texture + cocos2d-style frame atlas (see docs/re_notes.md §3). */
#include "mf.h"

#include <SDL2/SDL_image.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void strip_png(char *dst, size_t n, const char *src) {
    snprintf(dst, n, "%s", src);
    size_t l = strlen(dst);
    if (l > 4 && !strcmp(dst + l - 4, ".png")) dst[l - 4] = 0;
}

bool atlas_load(Atlas *a, SDL_Renderer *ren, const char *appdir, const char *base) {
    memset(a, 0, sizeof *a);
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.png", appdir, base);
    SDL_Surface *s = IMG_Load(path);
    if (!s) { log_msg("cannot load %s: %s", path, IMG_GetError()); return false; }
    a->tex = SDL_CreateTextureFromSurface(ren, s);
    a->w = s->w;
    a->h = s->h;
    SDL_FreeSurface(s);
    if (!a->tex) { log_msg("texture %s: %s", path, SDL_GetError()); return false; }
    SDL_SetTextureBlendMode(a->tex, SDL_BLENDMODE_BLEND);

    snprintf(path, sizeof path, "%s/%s.plist", appdir, base);
    size_t len;
    uint8_t *data = read_file(path, &len);
    if (!data) return true; /* plain texture without atlas */
    BpNode *root = bp_parse(data, len);
    free(data);
    BpNode *frames = bp_get(root, "frames");
    if (frames && frames->type == BP_DICT) {
        a->frames = calloc(frames->n ? frames->n : 1, sizeof *a->frames);
        for (size_t i = 0; a->frames && i < frames->n; i++) {
            BpNode *f = frames->items[i];
            if (!f || f->type != BP_DICT) continue;
            Frame *fr = &a->frames[a->nframes++];
            strip_png(fr->name, sizeof fr->name, frames->keys[i]);
            fr->r.x = (int)bp_num(bp_get(f, "x"));
            fr->r.y = (int)bp_num(bp_get(f, "y"));
            fr->r.w = (int)bp_num(bp_get(f, "width"));
            fr->r.h = (int)bp_num(bp_get(f, "height"));
        }
    }
    bp_free(root);
    return true;
}

void atlas_free(Atlas *a) {
    if (a->tex) SDL_DestroyTexture(a->tex);
    free(a->frames);
    memset(a, 0, sizeof *a);
}

const Frame *atlas_find(const Atlas *a, const char *name) {
    char key[64];
    strip_png(key, sizeof key, name);
    for (int i = 0; i < a->nframes; i++)
        if (!strcmp(a->frames[i].name, key)) return &a->frames[i];
    return NULL;
}

void spr_draw_rect(SDL_Renderer *ren, const Atlas *a, const SDL_Rect *src, float cx, float cy, float scale, Uint8 alpha) {
    if (!a->tex || !src) return;
    SDL_FRect dst = {cx - src->w * scale * 0.5f, cy - src->h * scale * 0.5f, src->w * scale, src->h * scale};
    SDL_SetTextureAlphaMod(a->tex, alpha);
    SDL_RenderCopyF(ren, a->tex, src, &dst);
}

void spr_draw(SDL_Renderer *ren, const Atlas *a, const char *name, float cx, float cy, float scale, Uint8 alpha) {
    const Frame *f = atlas_find(a, name);
    if (f) spr_draw_rect(ren, a, &f->r, cx, cy, scale, alpha);
    else if (!a->nframes && a->tex) { /* whole texture */
        SDL_Rect r = {0, 0, a->w, a->h};
        spr_draw_rect(ren, a, &r, cx, cy, scale, alpha);
    }
}

void spr_draw_fill(SDL_Renderer *ren, const Atlas *a, const char *name, float cx, float cy, float frac, Uint8 alpha) {
    const Frame *f = atlas_find(a, name);
    if (!f || !a->tex) return;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    int h = (int)(f->r.h * frac + 0.5f);
    if (h <= 0) return;
    SDL_Rect src = {f->r.x, f->r.y + f->r.h - h, f->r.w, h};
    SDL_FRect dst = {cx - f->r.w * 0.5f, cy + f->r.h * 0.5f - h, (float)f->r.w, (float)h};
    SDL_SetTextureAlphaMod(a->tex, alpha);
    SDL_RenderCopyF(ren, a->tex, &src, &dst);
}

void spr_tl_rect(SDL_Renderer *ren, const Atlas *a, const SDL_Rect *src, float x, float y, float w, float h, Uint8 alpha) {
    if (!a->tex || !src || alpha == 0) return;
    SDL_FRect dst = {x, y, w, h};
    SDL_SetTextureAlphaMod(a->tex, alpha);
    SDL_RenderCopyF(ren, a->tex, src, &dst);
}

void spr_tl_wh(SDL_Renderer *ren, const Atlas *a, const char *name, float x, float y, float w, float h, Uint8 alpha) {
    const Frame *f = atlas_find(a, name);
    if (f) spr_tl_rect(ren, a, &f->r, x, y, w, h, alpha);
}

void spr_tl(SDL_Renderer *ren, const Atlas *a, const char *name, float x, float y, Uint8 alpha) {
    const Frame *f = atlas_find(a, name);
    if (f) spr_tl_rect(ren, a, &f->r, x, y, (float)f->r.w, (float)f->r.h, alpha);
    else if (!a->nframes && a->tex) {
        SDL_Rect r = {0, 0, a->w, a->h};
        spr_tl_rect(ren, a, &r, x, y, (float)a->w, (float)a->h, alpha);
    }
}

void spr_rot(SDL_Renderer *ren, const Atlas *a, const char *name, float cx, float cy, float deg, float scale, Uint8 alpha) {
    const Frame *f = atlas_find(a, name);
    if (!f || !a->tex || alpha == 0) return;
    SDL_FRect dst = {cx - f->r.w * scale * 0.5f, cy - f->r.h * scale * 0.5f, f->r.w * scale, f->r.h * scale};
    SDL_SetTextureAlphaMod(a->tex, alpha);
    SDL_RenderCopyExF(ren, a->tex, &f->r, &dst, deg, NULL, SDL_FLIP_NONE);
}

const Atlas *atlas_owner(const Atlas *const *list, int n, const char *name) {
    for (int i = 0; i < n; i++)
        if (list[i] && atlas_find(list[i], name)) return list[i];
    return NULL;
}

float spr_width(const Atlas *a, const char *name) {
    const Frame *f = atlas_find(a, name);
    return f ? (float)f->r.w : 0.0f;
}
