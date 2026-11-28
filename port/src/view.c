/*
 * Screen composition. Every scene draws into a 640x960 render target (the
 * original's logical "retina" screen), which is shown centred and scaled to fit
 * the window, keeping the original layout and touch positions. On screens that
 * are taller (most phones) or wider than 2:3, the bars around it are filled
 * with a blurred, darkened copy of the backdrop - the song video in game, the
 * scene itself in the menus - instead of black. On phones (iOS, Android) the
 * bars stay black in game and the menu blur is dimmer.
 *
 * Window/touch coordinates are mapped back to the 640x960 space through the
 * same rectangle (view_window_to_logical). If render targets are unavailable
 * the renderer falls back to SDL's logical-size letterboxing.
 */
#include "mf.h"

#include <math.h>
#include <string.h>

#define BLUR1_W 160
#define BLUR1_H 240
#define BLUR2_W 40
#define BLUR2_H 60

static struct {
    SDL_Texture *scene, *blur1, *blur2;
    SDL_Rect dst;       /* where the 640x960 scene lands, in output pixels */
    int out_w, out_h;
    bool ok;
} V;

static SDL_Texture *target(SDL_Renderer *ren, int w, int h) {
    /* RGBA32 = GL's native order: with a BGRA (ARGB8888) target SDL's GLES2 renderer
       colour-swaps RGBA textures but not its YUV shader, so the movie came out R<->B */
    SDL_Texture *t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
    if (t) SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
    return t;
}

bool view_init(SDL_Renderer *ren) {
    view_quit();
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(ren, &info) == 0 && (info.flags & SDL_RENDERER_TARGETTEXTURE)) {
        V.scene = target(ren, LOGICAL_W, LOGICAL_H);
        V.blur1 = target(ren, BLUR1_W, BLUR1_H);
        V.blur2 = target(ren, BLUR2_W, BLUR2_H);
    }
    V.ok = V.scene && V.blur1 && V.blur2;
    if (!V.ok) {
        log_msg("view: no render targets, using plain letterboxing");
        SDL_RenderSetLogicalSize(ren, LOGICAL_W, LOGICAL_H);
    }
    return V.ok;
}

void view_quit(void) {
    if (V.scene) SDL_DestroyTexture(V.scene);
    if (V.blur1) SDL_DestroyTexture(V.blur1);
    if (V.blur2) SDL_DestroyTexture(V.blur2);
    memset(&V, 0, sizeof V);
}

static void update_layout(SDL_Renderer *ren) {
    /* SDL_GetRendererOutputSize reports the bound target's size, so ask for the window's */
    SDL_Texture *bound = SDL_GetRenderTarget(ren);
    if (bound) SDL_SetRenderTarget(ren, NULL);
    int r = SDL_GetRendererOutputSize(ren, &V.out_w, &V.out_h);
    if (bound) SDL_SetRenderTarget(ren, bound);
    if (r != 0 || V.out_w <= 0 || V.out_h <= 0) {
        V.out_w = LOGICAL_W;
        V.out_h = LOGICAL_H;
    }
    float s = fminf((float)V.out_w / LOGICAL_W, (float)V.out_h / LOGICAL_H);
    V.dst.w = (int)lroundf(LOGICAL_W * s);
    V.dst.h = (int)lroundf(LOGICAL_H * s);
    V.dst.x = (V.out_w - V.dst.w) / 2;
    V.dst.y = (V.out_h - V.dst.h) / 2;
}

bool view_active(void) { return V.ok; }

void view_begin(SDL_Renderer *ren) {
    if (V.ok) SDL_SetRenderTarget(ren, V.scene);
}

void view_present(SDL_Renderer *ren, SDL_Texture *backdrop) {
    if (!V.ok) { SDL_RenderPresent(ren); return; }
    update_layout(ren);
    bool bars = V.dst.w < V.out_w - 1 || V.dst.h < V.out_h - 1;
#if defined(MF_IOS) || defined(__ANDROID__)
    /* phones (mostly OLED): pure black bars during rhythm gameplay so moving blur never distracts,
       and a subtle dim blur only on menus */
    bool draw_blur = bars && !backdrop;
    const Uint8 blur_mod = 48;
#else
    bool draw_blur = bars;
    const Uint8 blur_mod = 120;
#endif
    if (draw_blur) {   /* two linear downscales make a cheap, stable blur */
        SDL_SetRenderTarget(ren, V.blur1);
        SDL_RenderCopy(ren, backdrop ? backdrop : V.scene, NULL, NULL);
        SDL_SetRenderTarget(ren, V.blur2);
        SDL_RenderCopy(ren, V.blur1, NULL, NULL);
    }
    SDL_SetRenderTarget(ren, NULL);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    if (draw_blur) {
        float c = fmaxf((float)V.out_w / LOGICAL_W, (float)V.out_h / LOGICAL_H);
        SDL_Rect cover = {0, 0, (int)lroundf(LOGICAL_W * c), (int)lroundf(LOGICAL_H * c)};
        cover.x = (V.out_w - cover.w) / 2;
        cover.y = (V.out_h - cover.h) / 2;
        SDL_SetTextureColorMod(V.blur2, blur_mod, blur_mod, blur_mod);
        SDL_RenderCopy(ren, V.blur2, NULL, &cover);
    }
    SDL_RenderCopy(ren, V.scene, NULL, &V.dst);
    SDL_RenderPresent(ren);
}

void view_window_to_logical(SDL_Renderer *ren, float wx, float wy, float *lx, float *ly) {
    if (!V.ok) {
        SDL_RenderWindowToLogical(ren, (int)wx, (int)wy, lx, ly);
        return;
    }
    update_layout(ren);
    int ww = 0, wh = 0;
    SDL_GetWindowSize(SDL_RenderGetWindow(ren), &ww, &wh);
    float px = ww > 0 ? wx * V.out_w / ww : wx, py = wh > 0 ? wy * V.out_h / wh : wy;   /* points -> pixels */
    *lx = (px - V.dst.x) * LOGICAL_W / (float)V.dst.w;
    *ly = (py - V.dst.y) * LOGICAL_H / (float)V.dst.h;
}

void view_capture_size(SDL_Renderer *ren, int *w, int *h) {
    if (V.ok) { *w = LOGICAL_W; *h = LOGICAL_H; return; }   /* reads the scene target */
    SDL_GetRendererOutputSize(ren, w, h);
}
