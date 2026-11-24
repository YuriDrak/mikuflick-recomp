/*
 * Miku Flick/02 Linux port - entry point and scene loop.
 *
 * Usage: mikuflick [--app DIR] [--gamedata FILE] [--data DIR] [--song N --diff D]
 *                  [--autoplay] [--exit-after-song] [--offset N] [--volume F]
 *                  [--se-volume F] [--no-fail-sound] [--fullscreen] [--window WxH]
 */
#include "mf.h"

#include <SDL2/SDL_image.h>
#include <libavutil/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void game_set_highscore(Game *g, int hs);

static void usage(void) {
    puts("mikuflick - Linux port of Miku Flick/02 (requires your own extracted IPA)\n"
         "  --app DIR          extracted MikuFlick2.app (default extracted/ipa/Payload/MikuFlick2.app)\n"
         "  --gamedata FILE    tables exported by mfextract (default port/data/gamedata.txt)\n"
         "  --data DIR         save directory (default $XDG_DATA_HOME/mikuflick-port)\n"
         "  --song N --diff D  jump straight into a song (0-based; diff 0-4)\n"
         "  --offset N         input timing calibration steps (original option, default 0)\n"
         "  --volume F         BGM volume 0..1    --se-volume F   SFX volume 0..1\n"
         "  --no-fail-sound    mute the SAD sound\n"
         "  --autoplay         play perfectly by itself (testing)\n"
         "  --exit-after-song  print the result and exit after one song\n"
         "  --font PATH        TrueType font for menus (default: port/fonts/MPLUS1p-Bold.ttf; none = bitmap)\n"
         "  --fullscreen       start fullscreen (F11 toggles)   --window WxH  window size");
}

int main(int argc, char **argv) {
    const char *appdir = "extracted/ipa/Payload/MikuFlick2.app";
    const char *gamedata = "port/data/gamedata.txt";
    char datadir[1024] = "";
    Options opt = {.input_timing = 0, .bgm_volume = 1.0f, .se_volume = 1.0f, .fail_sound = true,
                   .autoplay = false, .autoplay_input = false, .start_song = -1, .start_diff = 1};
    bool exit_after = false, fullscreen = false;
    const char *shot_dir = NULL;      /* testing: PNG captures */
    const char *font_path = NULL;     /* --font PATH, or "none" for the bitmap font */
    double shot_times[16];
    int nshots = 0, next_shot = 0, start_scene = SCENE_BOOT, ui_shot_scene = -1;
    double ui_shot_times[32];
    int nuishots = 0, next_uishot = 0, nscript = 0, next_script = 0;
    Uint32 script_ms[64];
    SDL_Keycode script_key[64];
    Uint32 prog_start = 0;
    int win_w = 480, win_h = 720;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--app") && v) { appdir = v; i++; }
        else if (!strcmp(a, "--gamedata") && v) { gamedata = v; i++; }
        else if (!strcmp(a, "--data") && v) { snprintf(datadir, sizeof datadir, "%s", v); i++; }
        else if (!strcmp(a, "--song") && v) { opt.start_song = atoi(v); i++; }
        else if (!strcmp(a, "--diff") && v) { opt.start_diff = atoi(v); i++; }
        else if (!strcmp(a, "--offset") && v) { opt.input_timing = atoi(v); i++; }
        else if (!strcmp(a, "--volume") && v) { opt.bgm_volume = (float)atof(v); i++; }
        else if (!strcmp(a, "--se-volume") && v) { opt.se_volume = (float)atof(v); i++; }
        else if (!strcmp(a, "--window") && v) { sscanf(v, "%dx%d", &win_w, &win_h); i++; }
        else if (!strcmp(a, "--no-fail-sound")) opt.fail_sound = false;
        else if (!strcmp(a, "--autoplay")) opt.autoplay = true;
        else if (!strcmp(a, "--autoplay-input")) opt.autoplay_input = true;
        else if (!strcmp(a, "--exit-after-song")) exit_after = true;
        else if (!strcmp(a, "--fullscreen")) fullscreen = true;
        else if (!strcmp(a, "--shot-dir") && v) { shot_dir = v; i++; }
        else if (!strcmp(a, "--font") && v) { font_path = v; i++; }
        else if (!strcmp(a, "--shot-ui-times") && v) {
            for (char *s = (char *)v; *s && nuishots < 32;) {
                ui_shot_times[nuishots++] = strtod(s, &s);
                if (*s == ',') s++; else break;
            }
            i++;
        }
        else if (!strcmp(a, "--script") && v) {   /* "t:key,t:key" seconds:enter/esc/left/right/up/down/space */
            for (char *s = (char *)v; *s && nscript < 64;) {
                double tt = strtod(s, &s);
                if (*s != ':') break;
                s++;
                char kn[16] = {0};
                int kl = 0;
                while (*s && *s != ',' && kl < 15) kn[kl++] = *s++;
                SDL_Keycode kc = !strcmp(kn, "enter") ? SDLK_RETURN : !strcmp(kn, "esc") ? SDLK_ESCAPE : !strcmp(kn, "left") ? SDLK_LEFT
                    : !strcmp(kn, "right") ? SDLK_RIGHT : !strcmp(kn, "up") ? SDLK_UP : !strcmp(kn, "down") ? SDLK_DOWN : SDLK_SPACE;
                script_ms[nscript] = (Uint32)(tt * 1000); script_key[nscript++] = kc;
                if (*s == ',') s++;
            }
            i++;
        }
        else if (!strcmp(a, "--shot-times") && v) {
            for (char *s = (char *)v; *s && nshots < 16;) {
                shot_times[nshots++] = strtod(s, &s);
                if (*s == ',') s++; else break;
            }
            i++;
        }
        else if (!strcmp(a, "--start-scene") && v) {
            start_scene = !strcmp(v, "select") ? SCENE_SELECT : !strcmp(v, "title") ? SCENE_TITLE : !strcmp(v, "result") ? SCENE_RESULT : SCENE_BOOT;
            i++;
        }
        else if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(); return 0; }
        else { fprintf(stderr, "unknown argument %s\n", a); usage(); return 1; }
    }
    if (!datadir[0]) {
        const char *x = getenv("XDG_DATA_HOME"), *h = getenv("HOME");
        if (x && *x) snprintf(datadir, sizeof datadir, "%s/mikuflick-port", x);
        else snprintf(datadir, sizeof datadir, "%s/.local/share/mikuflick-port", h ? h : ".");
    }
    av_log_set_level(AV_LOG_ERROR);

    static GameData gd;
    if (!gamedata_load(&gd, gamedata)) return 1;
    if (opt.start_song >= gd.nsongs) { fprintf(stderr, "song out of range (0-%d)\n", gd.nsongs - 1); return 1; }
    if (opt.start_diff < 0 || opt.start_diff >= NUM_DIFF) opt.start_diff = 1;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) { log_msg("SDL: %s", SDL_GetError()); return 1; }
    IMG_Init(IMG_INIT_PNG);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_Window *win = SDL_CreateWindow("Miku Flick/02 (Linux port)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       win_w, win_h, SDL_WINDOW_RESIZABLE | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    if (!win) { log_msg("window: %s", SDL_GetError()); return 1; }
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    if (!ren) { log_msg("renderer: %s", SDL_GetError()); return 1; }
    SDL_RenderSetLogicalSize(ren, LOGICAL_W, LOGICAL_H);
    text_init(font_path);

    if (!mixer_init()) log_msg("continuing without audio device");
    mixer_set_master(1.0f, opt.se_volume);

    /* SceneBoot @0xf978: bg_tex_08 splash (640x960 region, on white) while everything loads */
    {
        Atlas splash;
        SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
        SDL_RenderClear(ren);
        if (atlas_load(&splash, ren, appdir, "bg_tex_08")) {
            SDL_Rect src = {0, 0, LOGICAL_W, LOGICAL_H};
            spr_tl_rect(ren, &splash, &src, 0, 0, LOGICAL_W, LOGICAL_H, 255);
            SDL_RenderPresent(ren);
            atlas_free(&splash);
        }
    }
    static Assets as;
    if (!assets_load(&as, ren, &gd, appdir)) {
        log_msg("asset loading failed - is --app pointing at the extracted MikuFlick2.app?");
        return 1;
    }
    static SaveData save;
    save_load(&save, datadir);
    UiState *ui = ui_create(ren, &as, &gd, &save, &opt);
    Game *game = NULL;

    int scene = SCENE_BOOT, song = 0, diff = 1;
    if (opt.start_song >= 0) { scene = SCENE_GAME; song = opt.start_song; diff = opt.start_diff; }
    else if (start_scene == SCENE_RESULT) { /* testing: sample result screen */
        GameResult demo = {.song = 6, .diff = 1, .counts = {0, 0, 1, 0, 26, 56}, .total_notes = 83, .max_combo = 62,
                           .stage_score = 28245, .combo_score = 11000, .interlude_hits = 15, .total_interlude = 24, .rank = 3};
        scene = SCENE_RESULT; ui_enter(ui, scene, &demo);
    } else { scene = start_scene; ui_enter(ui, scene, NULL); if (scene == SCENE_BOOT) scene = SCENE_TITLE; }
    Uint32 scene_ms = SDL_GetTicks();
    int last_scene = scene;
    int exit_code = 0;

    prog_start = SDL_GetTicks();
    while (scene != SCENE_QUIT) {
        if (scene == SCENE_GAME && !game) {
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderClear(ren);
            SDL_Color w = {255, 255, 255, 255};
            font_draw(ren, "LOADING", LOGICAL_W / 2, LOGICAL_H / 2 - 20, 6, w, 0);
            spr_draw(ren, &as.ui2, "loading", LOGICAL_W / 2, LOGICAL_H / 2 + 80, 1.0f, 255);
            SDL_RenderPresent(ren);
            game = game_create(ren, &as, &gd, &opt, song, diff);
            if (!game) {
                if (exit_after) { exit_code = 2; break; }
                scene = SCENE_SELECT;
                ui_enter(ui, scene, NULL);
                continue;
            }
            game_set_highscore(game, save.rec[song][diff].score);
        }

        /* testing: scripted key presses */
        while (next_script < nscript && SDL_GetTicks() - prog_start >= script_ms[next_script]) {
            SDL_Event ev = {0};
            ev.type = SDL_KEYDOWN; ev.key.keysym.sym = script_key[next_script]; SDL_PushEvent(&ev);
            ev.type = SDL_KEYUP; SDL_PushEvent(&ev);
            next_script++;
        }
        SDL_Event e;
        int next = -1;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { next = SCENE_QUIT; break; }
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F11) {
                Uint32 fl = SDL_GetWindowFlags(win);
                SDL_SetWindowFullscreen(win, (fl & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                continue;
            }
            if (scene == SCENE_GAME) game_event(game, &e);
            else {
                int n = ui_event(ui, &e, &song, &diff);
                if (n >= 0) next = n;
            }
        }

        if (scene == SCENE_GAME && next != SCENE_QUIT) {
            if (!game_update(game)) {
                GameResult res;
                game_result(game, &res);
                game_destroy(game);
                game = NULL;
                if (exit_after) {
                    printf("RESULT song=%d diff=%d rank=%d score=%d stage=%d combo_bonus=%d max_combo=%d notes=%d "
                           "cool=%d fine=%d safe=%d sad=%d worst=%d interlude=%d/%d game_over=%d\n",
                           res.song, res.diff, res.rank, res.stage_score + res.combo_score, res.stage_score,
                           res.combo_score, res.max_combo, res.total_notes, res.counts[5], res.counts[4],
                           res.counts[3], res.counts[2], res.counts[1], res.interlude_hits, res.total_interlude,
                           res.game_over);
                    fflush(stdout);
                    break;
                }
                if (res.rank == -2) { scene = SCENE_GAME; continue; }   /* pause menu RETRY */
                next = res.rank < 0 ? SCENE_SELECT : SCENE_RESULT;
                ui_enter(ui, next, res.rank < 0 ? NULL : &res);
                scene = next;
                next = -1;
            }
        } else if (next < 0) {
            int n = ui_update(ui, &song, &diff);
            if (n >= 0) next = n;
        }

        if (next >= 0) {
            if (next != SCENE_GAME && next != SCENE_QUIT) ui_enter(ui, next, NULL);
            scene = next;
            continue;
        }

        if (scene == SCENE_GAME && game) game_render(game);
        else ui_render(ui);

        /* testing aid: capture frames (game: at song times; menus: 1.5 s after entering) */
        if (shot_dir) {
            if (scene != last_scene) { last_scene = scene; scene_ms = SDL_GetTicks(); }
            char shot[1200] = "";
            Uint32 run_ms = SDL_GetTicks() - prog_start;
            if (next_uishot < nuishots && run_ms >= (Uint32)(ui_shot_times[next_uishot] * 1000)) {
                snprintf(shot, sizeof shot, "%s/ui_%05u.png", shot_dir, (unsigned)(ui_shot_times[next_uishot] * 1000));
                next_uishot++;
            } else if (scene == SCENE_GAME && game && next_shot < nshots && mixer_music_time() >= shot_times[next_shot])
                snprintf(shot, sizeof shot, "%s/game_%03d.png", shot_dir, (int)shot_times[next_shot++]);
            else if (scene != SCENE_GAME && scene != ui_shot_scene && SDL_GetTicks() - scene_ms > 1500) {
                ui_shot_scene = scene;
                snprintf(shot, sizeof shot, "%s/scene_%d.png", shot_dir, scene);
            }
            if (shot[0]) {
                int ow, oh;
                SDL_GetRendererOutputSize(ren, &ow, &oh);
                SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, ow, oh, 32, SDL_PIXELFORMAT_ARGB8888);
                if (s && SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0) {
                    IMG_SavePNG(s, shot);
                    log_msg("saved %s", shot);
                }
                SDL_FreeSurface(s);
            }
        }
        SDL_RenderPresent(ren);
    }

    if (game) game_destroy(game);
    ui_destroy(ui);
    text_quit();
    mixer_quit();
    assets_free(&as);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    IMG_Quit();
    SDL_Quit();
    return exit_code;
}
