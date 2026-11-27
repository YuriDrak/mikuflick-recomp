/*
 * Miku Flick/02 Linux/Android port - entry point and scene loop.
 *
 * Usage: mikuflick [--app DIR] [--gamedata FILE] [--data DIR] [--song N --diff D]
 *                  [--autoplay] [--exit-after-song] [--offset N] [--volume F]
 *                  [--se-volume F] [--no-fail-sound] [--fullscreen] [--window WxH]
 *
 * On Android the arguments come from the launch intent (extra "args"), the game
 * data is read from the app's external files directory and saves go to its
 * internal files directory (see android/README.md).
 */
#include "mf.h"

#include <SDL_image.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>

void game_set_highscore(Game *g, int hs);

/* Scene changes (SceneManager NextScene: @0x10db8, SetFadeChange @0x11d08, Exec @0x10620, Rendering
   @0x10a94). Every change fades over 30 frames at 30 Hz with alpha = sin(pi*cnt/30): the old scene is
   frozen and fades out, the next scene is set up at the peak (cnt 15, ChangeScene) and fades in over
   frames 16..30. The overlay is a black rect, or the loading screen when s_DispLoading is set for the
   change. Touches are ignored meanwhile. The original also freezes the new scene during the fade-in;
   the port holds a new game (black, as its own m_Fade starts at 1) but lets the menus keep ticking,
   since ui.c would otherwise catch up the frozen half second at once. */
typedef struct {
    bool on, switched, loading, has_res;
    int target;
    Uint32 start_ms;         /* frame 0 of the fade (moved back to frame 15 after the switch) */
    GameResult res;          /* for the result screen */
} Transition;

#define FADE_FRAMES 30

/* s_DispLoading from NextScene: target mask 0x24e8 (music select 3, score 5, options 6, PV select 7,
   record 10, shop 13) shows it; the title (1) only when coming from one of 0x20e8 (3, 5, 6, 7, 13);
   the game (2), PV view (4), result (8), ending (9/14) and boot (0) fade through black. Port scenes:
   SCENE_SELECT = 3, SCENE_PV = PV select (7), SCENE_OPTION = 6; the PV view is the game in pv_mode. */
static bool shows_loading(int from, int to) {
    if (to == SCENE_SELECT || to == SCENE_PV || to == SCENE_OPTION) return true;
    if (to == SCENE_TITLE) return from == SCENE_SELECT || from == SCENE_PV || from == SCENE_OPTION;
    return false;
}

static void trans_start(Transition *t, int from, int to, const GameResult *res) {
    memset(t, 0, sizeof *t);
    t->on = true;
    t->target = to;
    t->loading = shows_loading(from, to);
    t->start_ms = SDL_GetTicks();
    if (res) { t->res = *res; t->has_res = true; }
}

static int trans_frame(const Transition *t) {
    int cnt = (int)((SDL_GetTicks() - t->start_ms) * 30u / 1000u);
    return cnt > FADE_FRAMES ? FADE_FRAMES : cnt;
}

/* SceneManager RenderLoading @0x10738 (bg_tex_07), drawn over the frozen scene with the fade alpha
   after the fade's black rect: bg_loading at (0,0); linemove (821x2) twice at y 840, x = c*w/45 - w and
   c*w/45 with c = (c + 1) % 45 per frame; loadmiku_00 at (385,422); loadmiku_01 at (185,507), 1 px
   taller. (Tall screens move these down; the port's scene is always 640x960.) */
static void render_loading(SDL_Renderer *ren, const Assets *as, float a) {
    Uint8 al = (Uint8)(255.0f * (a < 0 ? 0 : a > 1 ? 1 : a) + 0.5f);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, al);
    SDL_RenderFillRect(ren, NULL);
    const Atlas *l = &as->loading;
    if (!l->tex) return;
    spr_tl(ren, l, "bg_loading", 0, 0, al);
    float w = spr_width(l, "linemove");
    float x = (float)((SDL_GetTicks() * 30u / 1000u) % 45u) * w / 45.0f;
    spr_tl(ren, l, "linemove", x - w, 840, al);
    spr_tl(ren, l, "linemove", x, 840, al);
    spr_tl(ren, l, "loadmiku_00", 385, 422, al);
    const Frame *f = atlas_find(l, "loadmiku_01");
    if (f) spr_tl_wh(ren, l, "loadmiku_01", 185, 507, (float)f->r.w, (float)f->r.h + 1, al);
}

static void render_fade(SDL_Renderer *ren, const Assets *as, const Transition *t) {
    float a = sinf(3.14159265f * (float)trans_frame(t) / (float)FADE_FRAMES);
    if (a <= 0.0f) return;
    if (t->loading) { render_loading(ren, as, a); return; }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, (Uint8)(255.0f * a + 0.5f));
    SDL_RenderFillRect(ren, NULL);
}

/* The menus and the pause/PV controls react to the left mouse button. A touchscreen's
   first finger is turned into such mouse events (marked SDL_TOUCH_MOUSEID, in logical
   coordinates); the flick board itself keeps using the raw multi-touch events. */
static SDL_FingerID g_primary;
static bool g_primary_down;

static bool finger_to_mouse(SDL_Renderer *ren, const SDL_Event *e, SDL_Event *out) {
    if (e->type != SDL_FINGERDOWN && e->type != SDL_FINGERMOTION && e->type != SDL_FINGERUP) return false;
    /* macOS trackpad fingers: positions on the pad, not on the screen (the pad also sends real mouse events) */
    if (SDL_GetTouchDeviceType(e->tfinger.touchId) == SDL_TOUCH_DEVICE_INDIRECT_RELATIVE) return false;
    if (e->type == SDL_FINGERDOWN) {
        if (g_primary_down) return false;
        g_primary_down = true;
        g_primary = e->tfinger.fingerId;
    } else if (!g_primary_down || e->tfinger.fingerId != g_primary) {
        return false;
    }
    if (e->type == SDL_FINGERUP) g_primary_down = false;
    int ww, wh;
    SDL_GetWindowSize(SDL_RenderGetWindow(ren), &ww, &wh);
    float lx, ly;
    view_window_to_logical(ren, e->tfinger.x * ww, e->tfinger.y * wh, &lx, &ly);
    memset(out, 0, sizeof *out);
    if (e->type == SDL_FINGERMOTION) {
        out->type = SDL_MOUSEMOTION;
        out->motion.timestamp = e->tfinger.timestamp;
        out->motion.which = SDL_TOUCH_MOUSEID;
        out->motion.state = SDL_BUTTON_LMASK;
        out->motion.x = (int)lx;
        out->motion.y = (int)ly;
    } else {
        out->type = e->type == SDL_FINGERDOWN ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
        out->button.timestamp = e->tfinger.timestamp;
        out->button.which = SDL_TOUCH_MOUSEID;
        out->button.button = SDL_BUTTON_LEFT;
        out->button.state = e->type == SDL_FINGERDOWN ? SDL_PRESSED : SDL_RELEASED;
        out->button.clicks = 1;
        out->button.x = (int)lx;
        out->button.y = (int)ly;
    }
    return true;
}

/* Real mouse events arrive in window points; the scenes work in the 640x960 space. */
static void map_mouse(SDL_Renderer *ren, SDL_Event *e) {
    if (!view_active()) return;   /* SDL's logical-size fallback already scaled them */
    float lx, ly;
    if ((e->type == SDL_MOUSEBUTTONDOWN || e->type == SDL_MOUSEBUTTONUP) && e->button.which != SDL_TOUCH_MOUSEID) {
        view_window_to_logical(ren, (float)e->button.x, (float)e->button.y, &lx, &ly);
        e->button.x = (int)lx;
        e->button.y = (int)ly;
    } else if (e->type == SDL_MOUSEMOTION && e->motion.which != SDL_TOUCH_MOUSEID) {
        view_window_to_logical(ren, (float)e->motion.x, (float)e->motion.y, &lx, &ly);
        e->motion.x = (int)lx;
        e->motion.y = (int)ly;
    }
}

/* testing (--script "mX0-Y0-X1-Y1"): push what a desktop mouse sends, in window points with a non-touch
   mouse id, so it goes through map_mouse like the real thing. Logical -> window by inverting the
   (linear) view_window_to_logical. */
static void script_mouse(SDL_Renderer *ren, Uint32 type, int lx, int ly) {
    int ww = 0, wh = 0;
    SDL_GetWindowSize(SDL_RenderGetWindow(ren), &ww, &wh);
    float ax, ay, bx, by;
    view_window_to_logical(ren, 0, 0, &ax, &ay);
    view_window_to_logical(ren, (float)ww, (float)wh, &bx, &by);
    int wx = (int)lroundf(((float)lx - ax) * (float)ww / (bx - ax)), wy = (int)lroundf(((float)ly - ay) * (float)wh / (by - ay));
    SDL_Event ev = {0};
    ev.type = type;
    if (type == SDL_MOUSEMOTION) {
        ev.motion.timestamp = SDL_GetTicks();
        ev.motion.windowID = SDL_GetWindowID(SDL_RenderGetWindow(ren));
        ev.motion.which = 0;
        ev.motion.state = SDL_BUTTON_LMASK;
        ev.motion.x = wx; ev.motion.y = wy;
    } else {
        ev.button.timestamp = SDL_GetTicks();
        ev.button.windowID = SDL_GetWindowID(SDL_RenderGetWindow(ren));
        ev.button.which = 0;
        ev.button.button = SDL_BUTTON_LEFT;
        ev.button.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
        ev.button.clicks = 1;
        ev.button.x = wx; ev.button.y = wy;
    }
    SDL_PushEvent(&ev);
}


#ifdef __ANDROID__
/* the game's main files are there (a quick check before loading everything) */
static bool data_ready(const char *appdir) {
    static const char *probe[] = {"game_tex_01.png", "UI_tex_01.png", "bg_tex_01.png"};
    for (size_t i = 0; i < sizeof probe / sizeof *probe; i++) {
        char p[1200];
        snprintf(p, sizeof p, "%s/%s", appdir, probe[i]);
        FILE *f = fopen(p, "rb");
        if (!f) return false;
        fclose(f);
    }
    return true;
}

/* greedy word wrap into up to max lines of at most cols bytes (breaks after ' ' or '/') */
static int wrap_text(const char *s, int cols, char out[][96], int max) {
    int n = 0;
    while (*s && n < max) {
        while (*s == ' ') s++;
        int len = (int)strlen(s), cut = len;
        if (len > cols) {
            cut = cols;
            while (cut > cols / 3 && s[cut - 1] != ' ' && s[cut - 1] != '/') cut--;
            if (cut <= cols / 3) cut = cols;
            while (cut < len && ((unsigned char)s[cut] & 0xc0) == 0x80) cut++;   /* keep UTF-8 whole */
        }
        snprintf(out[n++], 96, "%.*s", cut, s);
        s += cut;
    }
    return n;
}

#include <jni.h>

/* MikuFlickActivity.pickIpa() opens the system file picker; takePickedFd() returns
   -1 while it is open, -2 if cancelled, else a readable fd for the chosen file. */
static bool jni_call_void(const char *name) {
    JNIEnv *env = SDL_AndroidGetJNIEnv();
    jobject act = SDL_AndroidGetActivity();
    if (!env || !act) return false;
    jclass cls = (*env)->GetObjectClass(env, act);
    jmethodID m = (*env)->GetMethodID(env, cls, name, "()V");
    bool ok = m != NULL;
    if (ok) (*env)->CallVoidMethod(env, act, m);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); ok = false; }
    (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, act);
    return ok;
}

static int jni_call_int(const char *name) {
    JNIEnv *env = SDL_AndroidGetJNIEnv();
    jobject act = SDL_AndroidGetActivity();
    if (!env || !act) return -2;
    jclass cls = (*env)->GetObjectClass(env, act);
    jmethodID m = (*env)->GetMethodID(env, cls, name, "()I");
    int r = m ? (*env)->CallIntMethod(env, act, m) : -2;
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); r = -2; }
    (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, act);
    return r;
}

typedef struct {
    SDL_mutex *mu;
    double frac;
    char file[96];
    int state;          /* 0 running, 1 done, -1 failed */
    char err[320];
    int fd;
    const char *dir;
} ImportJob;

static void import_progress(void *ud, double frac, const char *file) {
    ImportJob *j = ud;
    SDL_LockMutex(j->mu);
    j->frac = frac;
    snprintf(j->file, sizeof j->file, "%s", file ? file : "");
    SDL_UnlockMutex(j->mu);
}

static int SDLCALL import_thread(void *ud) {
    ImportJob *j = ud;
    char err[320] = "";
    bool ok = ipa_import(j->fd, j->dir, import_progress, j, err, sizeof err);
    close(j->fd);
    SDL_LockMutex(j->mu);
    j->state = ok ? 1 : -1;
    snprintf(j->err, sizeof j->err, "%s", err);
    SDL_UnlockMutex(j->mu);
    return 0;
}

typedef struct { float x, y, w, h; const char *label; } Button;

static void draw_button(SDL_Renderer *ren, const Button *b, bool primary) {
    SDL_FRect r = {b->x, b->y, b->w, b->h};
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    if (primary) SDL_SetRenderDrawColor(ren, 27, 181, 176, 255);
    else SDL_SetRenderDrawColor(ren, 55, 60, 72, 255);
    SDL_RenderFillRectF(ren, &r);
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 180);
    SDL_RenderDrawRectF(ren, &r);
    font_draw(ren, b->label, b->x + b->w / 2, b->y + b->h / 2 - 14, 2.6f, (SDL_Color){255, 255, 255, 255}, 0);
}

static bool hit(const Button *b, float x, float y) {
    return x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h;
}

/* Shown while the user's game data is missing: imports their own IPA in the app.
   Returns true once an import has succeeded, false if the player quits. */
static bool import_screen(SDL_Renderer *ren, const char *files_dir) {
    enum { PROMPT, PICKING, IMPORTING, FAILED } phase = PROMPT;
    ImportJob job = {.mu = SDL_CreateMutex(), .dir = files_dir, .fd = -1};
    SDL_Thread *th = NULL;
    const Button import_btn = {120, 600, 400, 96, "IMPORT IPA"}, retry_btn = {120, 600, 400, 96, "TRY AGAIN"};
    const Button quit_btn = {170, 730, 300, 80, "QUIT"};
    SDL_Color white = {255, 255, 255, 255}, dim = {170, 180, 190, 255}, teal = {120, 230, 220, 255};
    char lines[16][96], path_lines[6][96];
    int npath = wrap_text(files_dir, 40, path_lines, 6);
    char err_copy[320] = "";
    bool result = false;

    for (;;) {
        SDL_Event e;
        float tx = -1, ty = -1;
        bool back = false;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT || e.type == SDL_APP_TERMINATING) goto done;
            if (e.type == SDL_KEYDOWN && (e.key.keysym.sym == SDLK_AC_BACK || e.key.keysym.sym == SDLK_ESCAPE)) back = true;
            map_mouse(ren, &e);
            SDL_Event m;
            if (finger_to_mouse(ren, &e, &m)) e = m;
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                tx = (float)e.button.x;
                ty = (float)e.button.y;
            }
        }

        if (phase == PROMPT || phase == FAILED) {
            if (back || hit(&quit_btn, tx, ty)) goto done;
            if (hit(&import_btn, tx, ty)) {   /* same rectangle as TRY AGAIN */
                if (jni_call_void("pickIpa")) phase = PICKING;
                else { snprintf(err_copy, sizeof err_copy, "Could not open the file picker."); phase = FAILED; }
            }
        } else if (phase == PICKING) {
            int fd = jni_call_int("takePickedFd");
            if (fd == -2) phase = PROMPT;
            else if (fd >= 0) {
                job.fd = fd;
                job.state = 0;
                job.frac = 0;
                job.file[0] = 0;
                th = SDL_CreateThread(import_thread, "ipa-import", &job);
                if (th) phase = IMPORTING;
                else { close(fd); snprintf(err_copy, sizeof err_copy, "Could not start the import."); phase = FAILED; }
            }
        } else {
            SDL_LockMutex(job.mu);
            int st = job.state;
            if (st < 0) snprintf(err_copy, sizeof err_copy, "%s", job.err);
            SDL_UnlockMutex(job.mu);
            if (st != 0) {
                SDL_WaitThread(th, NULL);
                th = NULL;
                if (st > 0) { result = true; goto done; }
                phase = FAILED;
            }
        }

        view_begin(ren);
        SDL_SetRenderDrawColor(ren, 12, 16, 24, 255);
        SDL_RenderClear(ren);
        font_draw(ren, "Miku Flick/02", LOGICAL_W / 2, 110, 4.0f, teal, 0);
        if (phase == PROMPT || phase == PICKING) {
            font_draw(ren, "Game data needed", LOGICAL_W / 2, 220, 3.2f, white, 0);
            int n = wrap_text("This app contains no game data. Import your own decrypted Miku Flick/02 IPA "
                              "(iOS v1.1.5) from this device; the app extracts what it needs (about 1 GB of "
                              "free space). The IPA itself can be deleted afterwards.", 44, lines, 16);
            for (int i = 0; i < n; i++) font_draw(ren, lines[i], LOGICAL_W / 2, 300 + i * 34, 2.0f, white, 0);
            if (phase == PICKING) {
                font_draw(ren, "Choose the IPA file...", LOGICAL_W / 2, 630, 2.6f, teal, 0);
            } else {
                draw_button(ren, &import_btn, true);
                draw_button(ren, &quit_btn, false);
            }
            font_draw(ren, "Or copy MikuFlick2.app and gamedata.txt to:", LOGICAL_W / 2, 836, 1.6f, dim, 0);
            for (int i = 0; i < npath; i++) font_draw(ren, path_lines[i], LOGICAL_W / 2, 866 + i * 26, 1.6f, dim, 0);
        } else if (phase == IMPORTING) {
            SDL_LockMutex(job.mu);
            double frac = job.frac;
            char file[96];
            snprintf(file, sizeof file, "%s", job.file);
            SDL_UnlockMutex(job.mu);
            font_draw(ren, "Importing...", LOGICAL_W / 2, 300, 3.2f, white, 0);
            SDL_FRect bar = {80, 460, 480, 36}, fill = {80, 460, (float)(480 * (frac < 0 ? 0 : frac > 1 ? 1 : frac)), 36};
            SDL_SetRenderDrawColor(ren, 55, 60, 72, 255);
            SDL_RenderFillRectF(ren, &bar);
            SDL_SetRenderDrawColor(ren, 27, 181, 176, 255);
            SDL_RenderFillRectF(ren, &fill);
            char pct[16];
            snprintf(pct, sizeof pct, "%d%%", (int)(frac * 100 + 0.5));
            font_draw(ren, pct, LOGICAL_W / 2, 520, 2.6f, white, 0);
            font_draw(ren, file, LOGICAL_W / 2, 580, 1.8f, dim, 0);
            font_draw(ren, "Keep the app open. This takes a few minutes.", LOGICAL_W / 2, 700, 2.0f, white, 0);
        } else {
            font_draw(ren, "Import failed", LOGICAL_W / 2, 220, 3.2f, white, 0);
            int n = wrap_text(err_copy, 44, lines, 9);
            for (int i = 0; i < n; i++) font_draw(ren, lines[i], LOGICAL_W / 2, 300 + i * 34, 2.0f, white, 0);
            draw_button(ren, &retry_btn, true);
            draw_button(ren, &quit_btn, false);
        }
        view_present(ren, NULL);
        SDL_Delay(16);
    }
done:
    if (th) SDL_DetachThread(th);   /* quitting mid-import: the next import starts from scratch */
    else SDL_DestroyMutex(job.mu);
    return result;
}
#endif

static void cli_progress(void *ud, double frac, const char *file) {
    (void)ud;
    fprintf(stderr, "\r[%3d%%] %-48.48s", (int)(frac * 100 + 0.5), file ? file : "");
    if (frac >= 1.0) fputc('\n', stderr);
}

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
         "  --import-ipa FILE  extract your IPA into --import-to DIR (default imported/) and exit\n"
         "  --fullscreen       start fullscreen (F11 toggles)   --window WxH  window size");
}

int main(int argc, char **argv) {
    const char *appdir = "extracted/ipa/Payload/MikuFlick2.app";
    const char *gamedata = "port/data/gamedata.txt";
    char datadir[1024] = "";
    Options opt = {.input_timing = 0, .bgm_volume = 1.0f, .se_volume = 1.0f, .fail_sound = true, .left_hand = false,
                   .keyboard = 0, .flower = true, .guide_panel = true, .pause_button = true, .autoplay = false, .autoplay_input = false,
                   .start_song = -1, .start_diff = 1};
    bool cli_timing = false, cli_bgm = false, cli_se = false, cli_fail = false;
    bool exit_after = false, fullscreen = false;
    const char *shot_dir = NULL;      /* testing: PNG captures */
    const char *font_path = NULL;     /* --font PATH, or "none" for the bitmap font */
    const char *import_ipa = NULL, *import_to = "imported";   /* --import-ipa: in-app importer from the CLI */
    double shot_times[16];
    int nshots = 0, next_shot = 0, start_scene = SCENE_BOOT, ui_shot_scene = -1;
    double ui_shot_times[32];
    int nuishots = 0, next_uishot = 0, nscript = 0, next_script = 0;
    Uint32 script_ms[64];
    struct { bool on; int p[4]; } script_drag[64];   /* --script mouse drags (logical coords) */
    int drag_i = -1;
    Uint32 drag_t0 = 0;
    SDL_Keycode script_key[64];
    Uint32 prog_start = 0;
    bool start_credits = false;   /* --start-scene credits: OPTIONS -> CREDITS page */
    int win_w = 480, win_h = 720;

#ifdef __ANDROID__
    /* user-supplied data: /storage/emulated/0/Android/data/<package>/files (adb-writable, no permission needed) */
    static char android_app[1024], android_gd[1024], android_files[1024];
    const char *ext = SDL_AndroidGetExternalStoragePath();
    snprintf(android_files, sizeof android_files, "%s", ext ? ext : ".");
    snprintf(android_app, sizeof android_app, "%s/MikuFlick2.app", android_files);
    snprintf(android_gd, sizeof android_gd, "%s/gamedata.txt", android_files);
    appdir = android_app;
    gamedata = android_gd;
    const char *internal = SDL_AndroidGetInternalStoragePath();
    if (internal) snprintf(datadir, sizeof datadir, "%s", internal);
    font_path = "MPLUS1p-Bold.ttf";   /* APK asset (port/fonts) */
    fullscreen = true;
#endif

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--app") && v) { appdir = v; i++; }
        else if (!strcmp(a, "--gamedata") && v) { gamedata = v; i++; }
        else if (!strcmp(a, "--data") && v) { snprintf(datadir, sizeof datadir, "%s", v); i++; }
        else if (!strcmp(a, "--song") && v) { opt.start_song = atoi(v); i++; }
        else if (!strcmp(a, "--diff") && v) { opt.start_diff = atoi(v); i++; }
        else if (!strcmp(a, "--offset") && v) { opt.input_timing = atoi(v); cli_timing = true; i++; }
        else if (!strcmp(a, "--volume") && v) { opt.bgm_volume = (float)atof(v); cli_bgm = true; i++; }
        else if (!strcmp(a, "--se-volume") && v) { opt.se_volume = (float)atof(v); cli_se = true; i++; }
        else if (!strcmp(a, "--window") && v) { sscanf(v, "%dx%d", &win_w, &win_h); i++; }
        else if (!strcmp(a, "--no-fail-sound")) { opt.fail_sound = false; cli_fail = true; }
        else if (!strcmp(a, "--autoplay")) opt.autoplay = true;
        else if (!strcmp(a, "--autoplay-input")) opt.autoplay_input = true;
        else if (!strcmp(a, "--exit-after-song")) exit_after = true;
        else if (!strcmp(a, "--fullscreen")) fullscreen = true;
        else if (!strcmp(a, "--shot-dir") && v) { shot_dir = v; i++; }
        else if (!strcmp(a, "--font") && v) { font_path = v; i++; }
        else if (!strcmp(a, "--import-ipa") && v) { import_ipa = v; i++; }
        else if (!strcmp(a, "--import-to") && v) { import_to = v; i++; }
        else if (!strcmp(a, "--shot-ui-times") && v) {
            for (char *s = (char *)v; *s && nuishots < 32;) {
                ui_shot_times[nuishots++] = strtod(s, &s);
                if (*s == ',') s++; else break;
            }
            i++;
        }
        else if (!strcmp(a, "--script") && v) {   /* "t:key,t:key" seconds:enter/esc/left/right/up/down/space/dX0-X1-Y */
            for (char *s = (char *)v; *s && nscript < 64;) {
                double tt = strtod(s, &s);
                if (*s != ':') break;
                s++;
                char kn[32] = {0};
                int kl = 0;
                while (*s && *s != ',' && kl < 31) kn[kl++] = *s++;
                int m4[4];   /* "mX0-Y0-X1-Y1": real-mouse drag over 0.5 s (a click when both points match) */
                script_drag[nscript].on = sscanf(kn, "m%d-%d-%d-%d", &m4[0], &m4[1], &m4[2], &m4[3]) == 4;
                if (script_drag[nscript].on) memcpy(script_drag[nscript].p, m4, sizeof m4);
                SDL_Keycode kc = !strcmp(kn, "enter") ? SDLK_RETURN : !strcmp(kn, "esc") ? SDLK_ESCAPE : !strcmp(kn, "left") ? SDLK_LEFT
                    : !strcmp(kn, "right") ? SDLK_RIGHT : !strcmp(kn, "up") ? SDLK_UP : !strcmp(kn, "down") ? SDLK_DOWN : SDLK_SPACE;
                int dx0, dx1, dy;   /* "dX0-X1-Y": pointer drag (a tap when X0 == X1), logical coords, packed < 0 */
                if (sscanf(kn, "d%d-%d-%d", &dx0, &dx1, &dy) == 3)
                    kc = (SDL_Keycode)(0x80000000u | (Uint32)(dx0 & 1023) << 20 | (Uint32)(dx1 & 1023) << 10 | (Uint32)(dy & 1023));
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
            start_scene = !strcmp(v, "select") ? SCENE_SELECT : !strcmp(v, "title") ? SCENE_TITLE : !strcmp(v, "result") ? SCENE_RESULT : !strcmp(v, "options") ? SCENE_OPTION : !strcmp(v, "pv") ? SCENE_PV : SCENE_BOOT;
            /* ending.c: credits roll (then, as on a first clear, the movie and 激唱 Extreme), the movie alone */
            if (!strcmp(v, "ending")) start_scene = SCENE_ENDING;
            else if (!strcmp(v, "movie")) start_scene = SCENE_PREENDING;
            else if (!strcmp(v, "credits")) { start_scene = SCENE_OPTION; start_credits = true; }
            i++;
        }
        else if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(); return 0; }
        else { fprintf(stderr, "unknown argument %s\n", a); usage(); return 1; }
    }
    if (import_ipa) {   /* the same importer the Android app uses, for desktop setup and testing */
        int fd = open(import_ipa, O_RDONLY);
        if (fd < 0) { perror(import_ipa); return 1; }
        char err[320];
        bool ok = ipa_import(fd, import_to, cli_progress, NULL, err, sizeof err);
        close(fd);
        if (!ok) { fprintf(stderr, "\nimport failed: %s\n", err); return 1; }
        printf("imported into %s - run with --app %s/MikuFlick2.app --gamedata %s/gamedata.txt\n", import_to, import_to,
               import_to);
        return 0;
    }
    if (!datadir[0]) {
        const char *x = getenv("XDG_DATA_HOME"), *h = getenv("HOME");
        if (x && *x) snprintf(datadir, sizeof datadir, "%s/mikuflick-port", x);
        else snprintf(datadir, sizeof datadir, "%s/.local/share/mikuflick-port", h ? h : ".");
    }

    static GameData gd;
#ifndef __ANDROID__   /* Android reports missing data on screen, after the window exists */
    if (!gamedata_load(&gd, gamedata)) return 1;
    if (opt.start_song >= gd.nsongs) { fprintf(stderr, "song out of range (0-%d)\n", gd.nsongs - 1); return 1; }
#endif
    if (opt.start_diff < 0 || opt.start_diff >= NUM_DIFF) opt.start_diff = 1;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) { log_msg("SDL: %s", SDL_GetError()); return 1; }
    IMG_Init(IMG_INIT_PNG);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");   /* main loop maps the first finger itself */
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");   /* macOS: the click that focuses the window counts */
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "Portrait PortraitUpsideDown");
    SDL_Window *win = SDL_CreateWindow("Miku Flick/02 (Linux port)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       win_w, win_h, SDL_WINDOW_RESIZABLE | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    if (!win) { log_msg("window: %s", SDL_GetError()); return 1; }
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    if (!ren) { log_msg("renderer: %s", SDL_GetError()); return 1; }
    view_init(ren);   /* 640x960 scene, centred; bars filled (view.c) */
    text_init(font_path);
#ifdef __ANDROID__
    /* no data yet (first start): import the player's own IPA inside the app */
    while (!(data_ready(appdir) && gamedata_load(&gd, gamedata)))
        if (!import_screen(ren, android_files)) return 1;
    if (opt.start_song >= gd.nsongs) opt.start_song = -1;
#endif

    if (!mixer_init()) log_msg("continuing without audio device");
    mixer_set_master(1.0f, opt.se_volume);
    mixer_set_bgm(opt.bgm_volume);

    /* SceneBoot @0xf978: bg_tex_08 splash (640x960 region, on white) while everything loads */
    {
        Atlas splash;
        view_begin(ren);
        SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
        SDL_RenderClear(ren);
        if (atlas_load(&splash, ren, appdir, "bg_tex_08")) {
            SDL_Rect src = {0, 0, LOGICAL_W, LOGICAL_H};
            spr_tl_rect(ren, &splash, &src, 0, 0, LOGICAL_W, LOGICAL_H, 255);
            view_present(ren, NULL);
            atlas_free(&splash);
        }
    }
    static Assets as;
    while (!assets_load(&as, ren, &gd, appdir)) {
        log_msg("asset loading failed - is --app pointing at the extracted MikuFlick2.app?");
        assets_free(&as);
#ifdef __ANDROID__
        if (!import_screen(ren, android_files) || !gamedata_load(&gd, gamedata)) return 1;   /* incomplete data: re-import */
#else
        return 1;
#endif
    }
    static SaveData save;
    save_load(&save, datadir);
    if (save.has_opt) {   /* options saved from the OPTIONS screen; command-line flags win */
        if (!cli_bgm) opt.bgm_volume = save.opt.bgm_volume;
        if (!cli_se) opt.se_volume = save.opt.se_volume;
        if (!cli_fail) opt.fail_sound = save.opt.fail_sound;
        opt.left_hand = save.opt.left_hand;
        opt.keyboard = save.opt.keyboard;
        opt.flower = save.opt.flower;
        opt.guide_panel = save.opt.guide_panel;
        opt.pause_button = save.opt.pause_button;
        mixer_set_master(1.0f, opt.se_volume);
        mixer_set_bgm(opt.bgm_volume);
    }
    UiState *ui = ui_create(ren, &as, &gd, &save, &opt);
    Ending *ending = ending_create(ren, &as, &gd, &opt);
    Game *game = NULL;

    int scene = SCENE_BOOT, song = 0, diff = 1;
    if (opt.start_song >= 0) { scene = SCENE_GAME; song = opt.start_song; diff = opt.start_diff; }
    else if (start_scene == SCENE_RESULT) { /* testing: sample result screen */
        GameResult demo = {.song = 6, .diff = 1, .counts = {0, 0, 1, 0, 26, 56}, .total_notes = 83, .max_combo = 62,
                           .stage_score = 28245, .combo_score = 11000, .interlude_hits = 15, .total_interlude = 24, .rank = 3};
        flickres_demo(&demo);   /* per-key counters for the FLICK RESULT page */
        scene = SCENE_RESULT; ui_enter(ui, scene, &demo);
    } else if (IS_ENDING_SCENE(start_scene) || start_credits) {   /* testing: ending.c screens */
        scene = start_scene;
        ui_enter(ui, start_credits ? SCENE_ENDING : scene, NULL);
        if (start_credits) ui_enter(ui, SCENE_OPTION, NULL);   /* as if back from the ending: CREDITS */
        else ending_enter(ending, scene, SCENE_TITLE);
    } else { scene = start_scene; ui_enter(ui, scene, NULL); if (scene == SCENE_BOOT) scene = SCENE_TITLE; }
    Uint32 scene_ms = SDL_GetTicks();
    int last_scene = scene;
    int exit_code = 0;

    Transition tr = {0};
    prog_start = SDL_GetTicks();
    while (scene != SCENE_QUIT) {
        /* the game is set up behind a black screen: no loading screen on the way into a song (the
           fade to black is already at its peak; the song's own fade-in follows) */
        if (scene == SCENE_GAME && !game) {
            if (!cli_timing) opt.input_timing = save.timing[song];   /* per song, as MusicData */
            game = game_create(ren, &as, &gd, &opt, song, diff);
            if (!game) {
                if (exit_after) { exit_code = 2; break; }
                scene = SCENE_SELECT;
                ui_enter(ui, scene, NULL);
                continue;
            }
            game_set_highscore(game, save.rec[song][diff].score);
            if (tr.on) tr.start_ms = SDL_GetTicks() - FADE_FRAMES / 2 * 1000u / 30u;   /* resume at the peak */
        }

        /* testing: scripted key presses */
        while (next_script < nscript && SDL_GetTicks() - prog_start >= script_ms[next_script]) {
            SDL_Event ev = {0};
            Uint32 sk = (Uint32)script_key[next_script++];
            if (script_drag[next_script - 1].on) {   /* "mX0-Y0-X1-Y1": press now, moves each frame, release at 0.5 s */
                drag_i = next_script - 1;
                drag_t0 = SDL_GetTicks();
                script_mouse(ren, SDL_MOUSEBUTTONDOWN, script_drag[drag_i].p[0], script_drag[drag_i].p[1]);
                if (script_drag[drag_i].p[0] == script_drag[drag_i].p[2] && script_drag[drag_i].p[1] == script_drag[drag_i].p[3]) {
                    script_mouse(ren, SDL_MOUSEBUTTONUP, script_drag[drag_i].p[0], script_drag[drag_i].p[1]);
                    drag_i = -1;   /* a quick click: press and release in the same frame */
                }
                continue;
            }
            if (sk & 0x80000000u) {   /* "dX0-X1-Y": first-finger style mouse events in logical coords */
                int dx0 = (int)(sk >> 20 & 1023), dx1 = (int)(sk >> 10 & 1023), dy = (int)(sk & 1023);
                ev.button.which = SDL_TOUCH_MOUSEID; ev.button.button = SDL_BUTTON_LEFT;
                ev.type = SDL_MOUSEBUTTONDOWN; ev.button.x = dx0; ev.button.y = dy; SDL_PushEvent(&ev);
                SDL_Event mv = {0};
                mv.type = SDL_MOUSEMOTION; mv.motion.which = SDL_TOUCH_MOUSEID; mv.motion.state = SDL_BUTTON_LMASK;
                mv.motion.x = dx1; mv.motion.y = dy; SDL_PushEvent(&mv);
                ev.type = SDL_MOUSEBUTTONUP; ev.button.x = dx1; SDL_PushEvent(&ev);
                continue;
            }
            ev.type = SDL_KEYDOWN; ev.key.keysym.sym = (SDL_Keycode)sk; SDL_PushEvent(&ev);
            ev.type = SDL_KEYUP; SDL_PushEvent(&ev);
        }
        if (drag_i >= 0) {
            const int *p = script_drag[drag_i].p;
            float f = (float)(SDL_GetTicks() - drag_t0) / 500.0f;
            if (f > 1.0f) f = 1.0f;
            int x = p[0] + (int)lroundf((float)(p[2] - p[0]) * f), y = p[1] + (int)lroundf((float)(p[3] - p[1]) * f);
            script_mouse(ren, SDL_MOUSEMOTION, x, y);
            if (f >= 1.0f) { script_mouse(ren, SDL_MOUSEBUTTONUP, x, y); drag_i = -1; }
        }
        SDL_Event e;
        int next = -1;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT || e.type == SDL_APP_TERMINATING) { next = SCENE_QUIT; break; }
            if (e.type == SDL_APP_WILLENTERBACKGROUND) {   /* Android/iOS: the process may be killed while away */
                if (scene == SCENE_GAME && game) game_pause(game);
                if (scene == SCENE_GAME && game && !cli_timing) save.timing[song] = opt.input_timing;
                save_write(&save);
                continue;
            }
            if (e.type == SDL_RENDER_DEVICE_RESET) log_msg("render device reset: textures may be lost");
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F11) {
                Uint32 fl = SDL_GetWindowFlags(win);
                SDL_SetWindowFullscreen(win, (fl & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                continue;
            }
            /* Android back button behaves like Esc (cancel, pause, leave the PV) */
            if ((e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) && e.key.keysym.sym == SDLK_AC_BACK)
                e.key.keysym.sym = SDLK_ESCAPE;
            /* macOS trackpad contacts come as fingers with SDL_MOUSE_TOUCHID in trackpad coordinates (SDL
               cocoa isTouchFromTrackpad); the same gesture also sends the real mouse events, so drop them */
            if ((e.type == SDL_FINGERDOWN || e.type == SDL_FINGERMOTION || e.type == SDL_FINGERUP) &&
                e.tfinger.touchId == SDL_MOUSE_TOUCHID) continue;
            map_mouse(ren, &e);
            SDL_Event m;
            bool has_m = finger_to_mouse(ren, &e, &m);
            if (tr.on) continue;   /* SceneManager: no touches while a scene change fades */
            if (scene == SCENE_GAME) {
                game_event(game, &e);
                if (has_m) game_event(game, &m);
            } else if (!IS_ENDING_SCENE(scene)) {   /* WindowEnding / WindowPreEnding TouchButton:: are empty */
                int n = ui_event(ui, &e, &song, &diff);
                if (n >= 0) next = n;
                if (has_m && next < 0 && (n = ui_event(ui, &m, &song, &diff)) >= 0) next = n;
            }
        }

        if (tr.on && next != SCENE_QUIT) {
            int cnt = trans_frame(&tr);
            if (!tr.switched && cnt >= FADE_FRAMES / 2) {   /* ChangeScene at the peak of the fade */
                if (tr.loading) {   /* the next scene's setup blocks: show the loading screen meanwhile */
                    view_begin(ren);
                    render_loading(ren, &as, 1.0f);
                    view_present(ren, NULL);
                }
                if (game) { game_destroy(game); game = NULL; }
                if (IS_ENDING_SCENE(scene)) ending_leave(ending);
                if (tr.target == SCENE_GAME) scene = SCENE_GAME;   /* created at the top of the loop */
                else {
                    opt.pv_mode = false;   /* leaving the PV view (or not in it) */
                    ui_enter(ui, tr.target, tr.has_res ? &tr.res : NULL);
                    if (IS_ENDING_SCENE(tr.target)) ending_enter(ending, tr.target, scene);
                    scene = tr.target;
                }
                tr.switched = true;
                tr.start_ms = SDL_GetTicks() - FADE_FRAMES / 2 * 1000u / 30u;
                continue;
            }
            if (tr.switched && cnt >= FADE_FRAMES) tr.on = false;
            else if (tr.switched && IS_ENDING_SCENE(scene)) ending_update(ending, true, &song, &diff);
            else if (tr.switched && scene != SCENE_GAME) ui_update(ui, &song, &diff);   /* see Transition */
        } else if (scene == SCENE_GAME && next != SCENE_QUIT) {
            if (!game_update(game)) {
                GameResult res;
                game_result(game, &res);
                if (!cli_timing && save.timing[song] != opt.input_timing) {   /* changed in the pause menu */
                    save.timing[song] = opt.input_timing;
                    save_write(&save);
                }
                if (exit_after) {
                    game_destroy(game);
                    game = NULL;
                    printf("RESULT song=%d diff=%d rank=%d score=%d stage=%d combo_bonus=%d max_combo=%d notes=%d "
                           "cool=%d fine=%d safe=%d sad=%d worst=%d interlude=%d/%d game_over=%d\n",
                           res.song, res.diff, res.rank, res.stage_score + res.combo_score, res.stage_score,
                           res.combo_score, res.max_combo, res.total_notes, res.counts[5], res.counts[4],
                           res.counts[3], res.counts[2], res.counts[1], res.interlude_hits, res.total_interlude,
                           res.game_over);
                    fflush(stdout);
                    break;
                }
                /* the finished game stays on screen, frozen, while it fades out (destroyed at the switch) */
                int to;
                if (res.rank == -2) to = SCENE_GAME;   /* pause menu RETRY: NextScene to the game again */
                else if (opt.pv_mode) {                /* PV view left (-1), swiped/RETRY or finished (-3) */
                    int ns = pvview_next(&opt, res.rank == -1);   /* GotoNextPV / GotoPrevPV (pvview.c) */
                    if (ns >= 0) { game_destroy(game); game = NULL; song = ns; continue; }   /* CriManager ReLoad: in place */
                    to = SCENE_PV;
                } else to = res.rank < 0 ? SCENE_SELECT : SCENE_RESULT;
                trans_start(&tr, scene, to, to == SCENE_RESULT ? &res : NULL);
            }
        } else if (IS_ENDING_SCENE(scene) && next < 0) {
            int n = ending_update(ending, false, &song, &diff);
            if (n >= 0) next = n;
        } else if (next < 0) {
            int n = ui_update(ui, &song, &diff);
            if (n >= 0) next = n;
        }

        if (next == SCENE_QUIT) { scene = SCENE_QUIT; continue; }
        if (next >= 0) trans_start(&tr, scene, next, NULL);   /* NextScene: fade, switch at the peak */

        view_begin(ren);
        /* a song that was just set up stays black until the scene change has faded in */
        bool game_held = tr.on && tr.switched && scene == SCENE_GAME;
        if (game_held) {
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderFillRect(ren, NULL);
        } else if (scene == SCENE_GAME && game) game_render(game);
        else if (IS_ENDING_SCENE(scene)) ending_render(ending);
        else ui_render(ui);
        if (tr.on) render_fade(ren, &as, &tr);

        /* testing aid: capture frames (game: at song times; menus: 1.5 s after entering) */
        if (shot_dir) {
            if (scene != last_scene) { last_scene = scene; scene_ms = SDL_GetTicks(); }
            char shot[1200] = "";
            Uint32 run_ms = SDL_GetTicks() - prog_start;
            if (next_uishot < nuishots && run_ms >= (Uint32)(ui_shot_times[next_uishot] * 1000)) {
                snprintf(shot, sizeof shot, "%s/ui_%05u.png", shot_dir, (unsigned)(ui_shot_times[next_uishot] * 1000));
                next_uishot++;
            } else if (scene == SCENE_GAME && game && !tr.on && next_shot < nshots && mixer_music_time() >= shot_times[next_shot])
                snprintf(shot, sizeof shot, "%s/game_%03d.png", shot_dir, (int)shot_times[next_shot++]);
            else if (scene != SCENE_GAME && scene != ui_shot_scene && SDL_GetTicks() - scene_ms > 1500) {
                ui_shot_scene = scene;
                snprintf(shot, sizeof shot, "%s/scene_%d.png", shot_dir, scene);
            }
            if (shot[0]) {
                int ow, oh;
                view_capture_size(ren, &ow, &oh);
                SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, ow, oh, 32, SDL_PIXELFORMAT_ARGB8888);
                if (s && SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0) {
                    IMG_SavePNG(s, shot);
                    log_msg("saved %s", shot);
                }
                SDL_FreeSurface(s);
            }
        }
        view_present(ren, (scene == SCENE_GAME && game && !game_held) ? game_backdrop(game) : ending_backdrop(ending));
    }

    if (game) game_destroy(game);
    ending_destroy(ending);
    ui_destroy(ui);
    view_quit();
    text_quit();
    mixer_quit();
    assets_free(&as);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    IMG_Quit();
    SDL_Quit();
    return exit_code;
}
