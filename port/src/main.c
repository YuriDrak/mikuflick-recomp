/*
 * Miku Flick/02 Linux/Android/iOS port - entry point and scene loop.
 *
 * Usage: mikuflick [--app DIR] [--gamedata FILE] [--data DIR] [--song N --diff D]
 *                  [--autoplay] [--exit-after-song] [--offset N] [--volume F]
 *                  [--se-volume F] [--no-fail-sound] [--fullscreen] [--window WxH]
 *
 * On Android the arguments come from the launch intent (extra "args"), the game
 * data is read from the app's external files directory and saves go to its
 * internal files directory (see android/README.md). On iOS both live in Application
 * Support (see ios/README.md).
 */
#include "mf.h"

#include <SDL_image.h>
#ifdef MF_IOS
#include "Platform.h"
#endif
#if defined(MF_IOS) || defined(__ANDROID__)
#define MF_NATIVE_DLC 1   /* the DLC page is a native screen (ios/App/DlcViewController.m, android/.../DlcPage.java) */
#else
#define MF_NATIVE_DLC 0
#endif
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
    if (IS_RECORDS_SCENE(to) || (to == SCENE_TITLE && from == SCENE_SCORE)) return true;   /* RECORDS: 5, 10; 1 from 5 */
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


/* import screen: first start, outdated data (NOT NOW), or opened from the DLC page (BACK) */
enum { IMPORT_NEEDED, IMPORT_UPDATE, IMPORT_DLC };

#if defined(__ANDROID__) || defined(MF_IOS)
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

/* an import made by an older version has no help text (Localizable.strings) or ending credits:
   both need the IPA again, as the game binary isn't kept */
static bool data_current(const char *appdir, const GameData *gd) {
    char p[1200];
    snprintf(p, sizeof p, "%s/en.lproj/Localizable.strings", appdir);
    FILE *f = fopen(p, "rb");
    if (!f) return false;
    fclose(f);
    return gd->nending > 0;
}
#endif

#ifdef MF_IOS
static bool import_screen(SDL_Renderer *ren, const char *files_dir, int mode) {
    return ios_import_screen(SDL_RenderGetWindow(ren), files_dir,
                             mode == IMPORT_UPDATE ? IOS_IMPORT_UPDATE : mode == IMPORT_DLC ? IOS_IMPORT_DLC : IOS_IMPORT_NEEDED);
}
#elif defined(__ANDROID__)
typedef struct {
    SDL_mutex *mu;
    double frac;
    char file[96];
    int state;          /* 0 running, 1 done, -1 failed */
    bool abandoned;     /* the screen was left mid-import: the worker frees the job */
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
    bool abandoned = j->abandoned;
    j->state = ok ? 1 : -1;
    snprintf(j->err, sizeof j->err, "%s", err);
    SDL_UnlockMutex(j->mu);
    if (abandoned) { SDL_DestroyMutex(j->mu); free(j); }
    return 0;
}

/* Shown while the user's game data is missing: imports their own IPA in the app.
   Returns true once an import has succeeded, false if the player quits. IMPORT_UPDATE: the data is
   there but outdated (data_current), NOT NOW keeps it; IMPORT_DLC: opened from the DLC page (an IPA
   with the DLC packs), Cancel. Both return false without importing.
   The page is native (ImportPage.java, through android.c): it picks the file and hands over its
   descriptor ("fd N"); the import runs here on a worker thread and the page shows its progress. */
static bool import_screen(SDL_Renderer *ren, const char *files_dir, int mode) {
    (void)ren;
    ImportJob *job = calloc(1, sizeof *job);   /* heap: a detached worker may outlive this screen */
    if (!job || !(job->mu = SDL_CreateMutex())) { free(job); return false; }
    job->dir = files_dir;
    job->fd = -1;
    SDL_Thread *th = NULL;
    bool result = false, importing = false;
    int shown_permille = -1;
    char shown_file[96] = "";
    if (!android_page_show(ANDROID_PAGE_IMPORT, mode == IMPORT_UPDATE ? 1 : mode == IMPORT_DLC ? 2 : 0, files_dir)) {
        log_msg("import: the import page could not be shown");
        goto done;
    }
    android_import_state(ANDROID_IMPORT_PROMPT, 0, NULL, NULL);

    for (;;) {
        SDL_Event e;
        while (SDL_PollEvent(&e))
            if (e.type == SDL_QUIT || e.type == SDL_APP_TERMINATING) goto done;
        char act[64];
        while (android_page_action(act, sizeof act)) {
            if (importing) continue;   /* the page shows no buttons meanwhile */
            if (!strcmp(act, "later")) goto done;   /* NOT NOW, or Back (on the first start: quit) */
            if (!strcmp(act, "fdfail")) {
                android_import_state(ANDROID_IMPORT_FAILED, 0, NULL, "Could not read the selected file. Try a copy in Downloads.");
            } else if (!strncmp(act, "fd ", 3)) {
                job->fd = atoi(act + 3);
                job->state = 0;
                job->frac = 0;
                job->file[0] = 0;
                th = SDL_CreateThread(import_thread, "ipa-import", job);
                if (th) {
                    importing = true;
                    shown_permille = -1;
                    shown_file[0] = 0;
                    android_import_state(ANDROID_IMPORT_RUNNING, 0, NULL, NULL);
                } else {
                    close(job->fd);
                    android_import_state(ANDROID_IMPORT_FAILED, 0, NULL, "Could not start the import.");
                }
            }
        }
        if (importing) {
            SDL_LockMutex(job->mu);
            int st = job->state, permille = (int)(job->frac * 1000.0 + 0.5);
            char file[96], err[320];
            snprintf(file, sizeof file, "%s", job->file);
            snprintf(err, sizeof err, "%s", job->err);
            SDL_UnlockMutex(job->mu);
            if (st != 0) {
                SDL_WaitThread(th, NULL);
                th = NULL;
                importing = false;
                if (st > 0) { result = true; goto done; }
                android_import_state(ANDROID_IMPORT_FAILED, 0, NULL, err[0] ? err : "The import failed.");
            } else if (permille != shown_permille || strcmp(file, shown_file)) {   /* only what changed */
                shown_permille = permille;
                snprintf(shown_file, sizeof shown_file, "%s", file);
                android_import_state(ANDROID_IMPORT_RUNNING, permille / 1000.0, file, NULL);
            }
        }
        SDL_Delay(16);   /* nothing to draw: the page is on top */
    }
done:
    android_page_hide();
    if (th) {   /* quitting mid-import: don't block on it; the next import starts from scratch */
        SDL_LockMutex(job->mu);
        bool running = job->state == 0;
        job->abandoned = running;
        SDL_UnlockMutex(job->mu);
        if (running) { SDL_DetachThread(th); return result; }
        SDL_WaitThread(th, NULL);
    }
    SDL_DestroyMutex(job->mu);
    free(job);
    return result;
}
#endif

static void cli_progress(void *ud, double frac, const char *file) {
    (void)ud;
    fprintf(stderr, "\r[%3d%%] %-48.48s", (int)(frac * 100 + 0.5), file ? file : "");
    if (frac >= 1.0) fputc('\n', stderr);
}

/* ---------------- song list changes (DLC installs and imports) ---------------- */
typedef struct {
    GameData *gd;
    SaveData *save;
    UiState *ui;
    Options *opt;
    int *song;
    const char *datadir, *gamedata;
} SongCtx;

/* the song list in gd changed: re-key the save records (options kept) and reset the menus */
static void songs_changed(SongCtx *c) {
    Options keep = c->save->opt;
    bool had_opt = c->save->has_opt;
    save_load(c->save, c->datadir, c->gd);
    c->save->opt = keep;
    c->save->has_opt = had_opt;
    ui_songs_changed(c->ui);
    *c->song = 0;
}

/* a DLC pack finished downloading (dlcdl.c): load the new song list (kept as is if it is broken) */
static void dlc_reload(SongCtx *c) {
    static GameData gd_new;
    if (!gamedata_load(&gd_new, c->gamedata)) {
        log_msg("dlc: the new song list could not be loaded, keeping the old one");
        return;
    }
    save_write(c->save);
    *c->gd = gd_new;
    songs_changed(c);
}

#if defined(__ANDROID__) || defined(MF_IOS)
/* DLC page: import a whole IPA with DLC. False if the player quit (broken data, import refused). */
static bool dlc_import(SDL_Renderer *ren, const char *files_dir, const char *appdir, SongCtx *c) {
    save_write(c->save);
    if (!import_screen(ren, files_dir, IMPORT_DLC)) return true;   /* cancelled */
    while (!(data_ready(appdir) && gamedata_load(c->gd, c->gamedata)))   /* broken result: full import */
        if (!import_screen(ren, files_dir, IMPORT_NEEDED)) return false;
    songs_changed(c);
    return true;
}
#endif

#ifdef MF_IOS
/* native DLC page (ios/App/DlcViewController.m) */
static int ios_pack_songs(void *ud, int pack) {
    const SongCtx *c = ud;
    int n = 0;
    for (int i = c->gd->nbase; i < c->gd->nsongs; i++) n += c->gd->songs[i].pack == pack;
    return n;
}

static void ios_dlc_tick(IosDlcPage *p) {
    SongCtx *c = p->ud;
    if (dlcdl_take_changed()) dlc_reload(c);
    int packs = 0;
    for (int i = c->gd->nbase; i < c->gd->nsongs; i++) {
        bool seen = false;
        for (int k = c->gd->nbase; k < i && !seen; k++) seen = c->gd->songs[k].pack == c->gd->songs[i].pack;
        packs += !seen;
    }
    p->installed_packs = packs;
    p->installed_songs = c->gd->nsongs - c->gd->nbase;
    bool dirty = false;
    if (p->show_dlc != c->opt->show_dlc) {
        c->opt->show_dlc = p->show_dlc;
        c->save->opt = *c->opt;
        c->save->has_opt = true;
        dirty = true;
    }
    if (strcmp(p->url, c->save->dlc_url)) {
        snprintf(c->save->dlc_url, sizeof c->save->dlc_url, "%s", p->url);
        dirty = true;
    }
    if (dirty) save_write(c->save);
}

/* true: the player asked to import a whole IPA with DLC */
static bool ios_dlc_page(SDL_Window *win, SongCtx *c) {
    IosDlcPage p = {.ud = c, .show_dlc = c->opt->show_dlc, .pack_songs = ios_pack_songs, .tick = ios_dlc_tick};
    snprintf(p.url, sizeof p.url, "%s", c->save->dlc_url);
    ios_dlc_tick(&p);
    return ios_dlc_screen(win, &p);
}
#endif

#ifdef __ANDROID__
/* Native DLC page (android/.../DlcPage.java, through android.c), the counterpart of the iOS one: the
   page shows the state below and sends the player's actions; the work runs here on the SDL thread.
   State: one line per record, fields separated by tabs (tabs and line breaks in text become spaces):
     S  state permille busy_pack error msg show_dlc installed_packs installed_songs url
     P  pack name nsongs megabytes available installed title latin [title latin ...]   (one per pack)
   Actions: later, import, show 0|1, url LINK, list, install PACK, cancel. */
typedef struct { char *p; size_t len, cap; } StateBuf;

static void sb_raw(StateBuf *b, const char *s) {
    for (; *s && b->len + 1 < b->cap; s++) b->p[b->len++] = *s;
    b->p[b->len] = 0;
}

static void sb_text(StateBuf *b, const char *s) {
    sb_raw(b, "\t");
    for (; s && *s && b->len + 1 < b->cap; s++) b->p[b->len++] = (*s == '\t' || *s == '\n' || *s == '\r') ? ' ' : *s;
    b->p[b->len] = 0;
}

static void sb_int(StateBuf *b, long long v) {
    char n[32];
    snprintf(n, sizeof n, "\t%lld", v);
    sb_raw(b, n);
}

static int pack_installed_songs(const SongCtx *c, int pack) {
    int n = 0;
    for (int i = c->gd->nbase; i < c->gd->nsongs; i++) n += c->gd->songs[i].pack == pack;
    return n;
}

static size_t dlc_page_state(const SongCtx *c, const DlcDlStatus *st, char *buf, size_t cap) {
    StateBuf b = {buf, 0, cap};
    buf[0] = 0;
    int packs = 0;
    for (int i = c->gd->nbase; i < c->gd->nsongs; i++) {
        bool seen = false;
        for (int k = c->gd->nbase; k < i && !seen; k++) seen = c->gd->songs[k].pack == c->gd->songs[i].pack;
        packs += !seen;
    }
    double f = st->frac < 0 ? 0 : st->frac > 1 ? 1 : st->frac;
    sb_raw(&b, "S");
    sb_int(&b, st->state);
    sb_int(&b, (long long)(f * 1000.0));
    sb_int(&b, st->busy_pack);
    sb_int(&b, st->error);
    sb_text(&b, st->msg);
    sb_int(&b, c->opt->show_dlc);
    sb_int(&b, packs);
    sb_int(&b, c->gd->nsongs - c->gd->nbase);
    sb_text(&b, c->save->dlc_url);
    sb_text(&b, DLC_DEFAULT_URL);   /* DlcPage: Restore Default Link */
    sb_raw(&b, "\n");
    for (int i = 0; i < st->npacks; i++) {
        const DlcPackInfo *p = &st->packs[i];
        size_t start = b.len;
        sb_raw(&b, "P");
        sb_int(&b, p->pack);
        sb_text(&b, p->name);
        sb_int(&b, p->nsongs);
        sb_int(&b, (long long)((p->bytes + (1u << 19)) >> 20));
        sb_int(&b, p->available);
        sb_int(&b, pack_installed_songs(c, p->pack) >= p->nsongs);
        for (int k = 0; k < p->nsongs && k < DLC_PACK_SONGS; k++) {
            sb_text(&b, p->songs[k].title);
            sb_text(&b, p->songs[k].latin);
        }
        sb_raw(&b, "\n");
        if (b.len + 1 >= b.cap) { b.len = start; buf[start] = 0; break; }   /* whole lines only */
    }
    return b.len;
}

static void dlc_set_show(SongCtx *c, bool on) {
    if (on && c->gd->nsongs <= c->gd->nbase) on = false;   /* nothing installed */
    if (on == c->opt->show_dlc) return;
    c->opt->show_dlc = on;
    c->save->opt = *c->opt;
    c->save->has_opt = true;
    save_write(c->save);
}

/* true: the player asked to import a whole IPA with DLC */
static bool android_dlc_page(SongCtx *c) {
    static char state[128 * 1024], sent[128 * 1024];
    static DlcDlStatus st;   /* ~80 KB: not on the SDL thread's stack */
    size_t sent_len = (size_t)-1;
    Uint32 sent_ms = 0;
    int sent_state = -1;
    bool import = false;
    if (!android_page_show(ANDROID_PAGE_DLC, 0, NULL)) {
        log_msg("dlc: the DLC page could not be shown");
        return false;
    }
    dlcdl_status(&st);
    if (st.state == DLD_IDLE && c->save->dlc_url[0]) dlcdl_list(c->save->dlc_url);   /* pack list of the saved link */
    for (;;) {
        SDL_Event e;
        bool quit = false;
        while (SDL_PollEvent(&e))
            if (e.type == SDL_QUIT || e.type == SDL_APP_TERMINATING) quit = true;
        if (quit) {   /* for the main loop */
            SDL_Event q = {.type = SDL_QUIT};
            SDL_PushEvent(&q);
            break;
        }
        dlcdl_status(&st);
        char act[640];
        bool done = false;
        while (!done && android_page_action(act, sizeof act)) {
            bool busy = st.state == DLD_LISTING || st.state == DLD_INSTALLING;
            if (!strcmp(act, "later")) done = true;
            else if (!strcmp(act, "import")) {   /* the importer replaces the folder packs are written to */
                if (!busy) import = done = true;
            } else if (!strncmp(act, "show ", 5)) dlc_set_show(c, act[5] == '1');
            else if (!strncmp(act, "url ", 4)) {
                const char *url = act + 4;
                if (!busy && strlen(url) < sizeof c->save->dlc_url && net_url_ok(url)) {
                    snprintf(c->save->dlc_url, sizeof c->save->dlc_url, "%s", url);
                    save_write(c->save);
                    dlcdl_list(c->save->dlc_url);
                } else log_msg("dlc: link not taken");
            } else if (!strcmp(act, "list")) {
                if (!busy && c->save->dlc_url[0]) dlcdl_list(c->save->dlc_url);
            } else if (!strncmp(act, "install ", 8)) {
                if (st.state == DLD_READY) dlcdl_install(atoi(act + 8));
            } else if (!strcmp(act, "cancel")) dlcdl_cancel();
            dlcdl_status(&st);
        }
        if (done) break;
        if (dlcdl_take_changed()) dlc_reload(c);   /* a pack finished: new song list (counts on the page) */
        size_t n = dlc_page_state(c, &st, state, sizeof state);
        Uint32 now = SDL_GetTicks();
        if (n != sent_len || memcmp(state, sent, n)) {
            /* while a pack downloads only its percentage changes: 10 updates a second are plenty */
            bool progress = st.state == DLD_INSTALLING && sent_state == DLD_INSTALLING;
            if (!progress || now - sent_ms >= 100) {
                android_dlc_state(state, n);
                memcpy(sent, state, n);
                sent_len = n;
                sent_ms = now;
                sent_state = st.state;
            }
        }
        SDL_Delay(16);   /* nothing to draw: the page is on top */
    }
    android_page_hide();
    return import;
}
#endif

/* --dlc-url [--dlc-get]: the DLC page downloader without the UI */
static int dlc_cli(const char *url, const char *get, const char *appdir, const char *gamedata) {
    char err[320];
    ZipSrc *src = net_open(url, NULL, err, sizeof err);
    if (!src) { fprintf(stderr, "%s\n", err); return 1; }
    DlcCatalog *cat = dlc_catalog_open(src, err, sizeof err);
    if (!cat) {
        const char *ne = net_error(src);
        fprintf(stderr, "%s%s%s\n", err, ne ? " / " : "", ne ? ne : "");
        net_close(src);
        return 1;
    }
    static DlcPackInfo packs[DLC_MAX_PACKS];
    int np = dlc_catalog_packs(cat, packs, DLC_MAX_PACKS), rc = 0;
    for (int i = 0; i < np; i++) {
        printf("pack %3d  %-20s %2d songs %5llu MB\n", packs[i].pack, packs[i].name, packs[i].nsongs,
               (unsigned long long)(packs[i].bytes >> 20));
        for (int k = 0; k < packs[i].nsongs && k < DLC_PACK_SONGS; k++) {
            const DlcPackSong *s = &packs[i].songs[k];
            if (s->latin[0]) printf("            %s (%s)\n", s->latin, s->title);
            else printf("            %s\n", s->title);
        }
    }
    for (const char *s = get; s && *s && !rc;) {
        bool all = !strncmp(s, "all", 3);
        int want = all ? -1 : atoi(s);
        for (int i = 0; i < np && !rc; i++) {
            if (!all && packs[i].pack != want) continue;
            fprintf(stderr, "pack %d (%s):\n", packs[i].pack, packs[i].name);
            if (!dlc_catalog_install(cat, packs[i].pack, appdir, gamedata, cli_progress, NULL, NULL, err, sizeof err)) {
                const char *ne = net_error(src);
                fprintf(stderr, "\ninstall failed: %s%s%s\n", err, ne ? " / " : "", ne ? ne : "");
                rc = 1;
            }
        }
        s = strchr(s, ',');
        if (s) s++;
    }
    dlc_catalog_free(cat);
    net_close(src);
    return rc;
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
         "  --dlc-url URL      list the DLC packs of the IPA at URL (needs curl and range requests)\n"
         "  --dlc-get LIST     with --dlc-url: install packs (e.g. 1,2,97 or all) into --app/--gamedata, exit\n"
         "  --fullscreen       start fullscreen (F11 toggles)   --window WxH  window size");
}

int main(int argc, char **argv) {
    const char *appdir = "extracted/ipa/Payload/MikuFlick2.app";
    const char *gamedata = "port/data/gamedata.txt";
    char datadir[1024] = "";
    Options opt = {.input_timing = 0, .bgm_volume = 1.0f, .se_volume = 1.0f, .fail_sound = true, .left_hand = false,
                   .keyboard = 0, .flower = true, .guide_panel = true, .pause_button = true, .show_dlc = true, .autoplay = false, .autoplay_input = false,
                   .start_song = -1, .start_diff = 1};
    bool cli_timing = false, cli_bgm = false, cli_se = false, cli_fail = false;
    bool exit_after = false, fullscreen = false;
    const char *shot_dir = NULL;      /* testing: PNG captures */
    const char *font_path = NULL;     /* --font PATH, or "none" for the bitmap font */
    const char *import_ipa = NULL, *import_to = "imported";   /* --import-ipa: in-app importer from the CLI */
    const char *dlc_url = NULL, *dlc_get = NULL;              /* --dlc-url / --dlc-get: DLC page downloader from the CLI */
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
#if defined(__ANDROID__) || defined(MF_IOS)
    int start_import = -1;   /* --start-scene import|import-update: the import screen as on a first start / an old install */
#endif
    int win_w = 480, win_h = 720;

#if defined(__ANDROID__) || defined(MF_IOS)
    static char mobile_app[1200], mobile_gd[1200], mobile_files[1024];
#ifdef __ANDROID__
    /* user-supplied data: /storage/emulated/0/Android/data/<package>/files (adb-writable, no permission needed) */
    const char *ext = SDL_AndroidGetExternalStoragePath();
    snprintf(mobile_files, sizeof mobile_files, "%s", ext ? ext : ".");
    const char *internal = SDL_AndroidGetInternalStoragePath();
    if (internal) snprintf(datadir, sizeof datadir, "%s", internal);
    font_path = "MPLUS1p-Bold.ttf";   /* APK asset (port/fonts) */
#else
    if (!ios_paths(mobile_files, sizeof mobile_files, datadir, sizeof datadir)) {
        log_msg("could not prepare iOS application storage");
        return 1;
    }
#endif
    snprintf(mobile_app, sizeof mobile_app, "%s/MikuFlick2.app", mobile_files);
    snprintf(mobile_gd, sizeof mobile_gd, "%s/gamedata.txt", mobile_files);
    appdir = mobile_app;
    gamedata = mobile_gd;
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
        else if (!strcmp(a, "--dlc-url") && v) { dlc_url = v; i++; }
        else if (!strcmp(a, "--dlc-get") && v) { dlc_get = v; i++; }
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
            if (!strcmp(v, "scores")) start_scene = SCENE_SCORE;   /* RECORDS: RESULTS menu / RECORDS screen */
            if (!strcmp(v, "records")) start_scene = SCENE_RECORD;
            if (!strcmp(v, "dlc")) start_scene = SCENE_DLC;   /* port DLC page */
#if defined(__ANDROID__) || defined(MF_IOS)
            if (!strcmp(v, "import")) start_import = 0;
            else if (!strcmp(v, "import-update")) start_import = 1;
#endif
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
    if (dlc_url) return dlc_cli(dlc_url, dlc_get, appdir, gamedata);
    if (!datadir[0]) {
        const char *x = getenv("XDG_DATA_HOME"), *h = getenv("HOME");
        if (x && *x) snprintf(datadir, sizeof datadir, "%s/mikuflick-port", x);
        else snprintf(datadir, sizeof datadir, "%s/.local/share/mikuflick-port", h ? h : ".");
    }

    static GameData gd;
#if !defined(__ANDROID__) && !defined(MF_IOS)   /* mobile checks after window creation */
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
    Uint32 win_flags = SDL_WINDOW_RESIZABLE | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
#ifdef MF_IOS
    win_flags |= SDL_WINDOW_ALLOW_HIGHDPI;
#endif
    SDL_Window *win = SDL_CreateWindow("Miku Flick/02", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       win_w, win_h, win_flags);
    if (!win) { log_msg("window: %s", SDL_GetError()); return 1; }
#ifdef MF_IOS
    if (!ios_attach_window(win)) { log_msg("could not attach the window to the iOS scene"); return 1; }
#endif
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    if (!ren) { log_msg("renderer: %s", SDL_GetError()); return 1; }
    view_init(ren);   /* 640x960 scene, centred; bars filled (view.c) */
    text_init(font_path);
#if defined(__ANDROID__) || defined(MF_IOS)
    /* no data yet (first start): import the player's own IPA inside the app */
    while (!(data_ready(appdir) && gamedata_load(&gd, gamedata)))
        if (!import_screen(ren, mobile_files, IMPORT_NEEDED)) return 1;
    if (!data_current(appdir, &gd) && import_screen(ren, mobile_files, IMPORT_UPDATE) && !gamedata_load(&gd, gamedata)) return 1;
    if (start_import >= 0 && import_screen(ren, mobile_files, start_import ? IMPORT_UPDATE : IMPORT_NEEDED) && !gamedata_load(&gd, gamedata)) return 1;   /* testing */
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
#if defined(__ANDROID__) || defined(MF_IOS)
        if (!import_screen(ren, mobile_files, IMPORT_NEEDED) || !gamedata_load(&gd, gamedata)) return 1;   /* incomplete data: re-import */
#else
        return 1;
#endif
    }
    static SaveData save;
    save_load(&save, datadir, &gd);
    dlcdl_init(appdir, gamedata);
    if (save.has_opt) {   /* options saved from the OPTIONS screen; command-line flags win */
        if (!cli_bgm) opt.bgm_volume = save.opt.bgm_volume;
        if (!cli_se) opt.se_volume = save.opt.se_volume;
        if (!cli_fail) opt.fail_sound = save.opt.fail_sound;
        opt.left_hand = save.opt.left_hand;
        opt.keyboard = save.opt.keyboard;
        opt.flower = save.opt.flower;
        opt.guide_panel = save.opt.guide_panel;
        opt.pause_button = save.opt.pause_button;
        opt.show_dlc = save.opt.show_dlc;
        mixer_set_master(1.0f, opt.se_volume);
        mixer_set_bgm(opt.bgm_volume);
    }
    UiState *ui = ui_create(ren, &as, &gd, &save, &opt);
    Ending *ending = ending_create(ren, &as, &gd, &opt);
    Records *records = records_create(ren, &as, &gd, &opt, &save);   /* RECORDS */
    Game *game = NULL;

    int scene = SCENE_BOOT, song = 0, diff = 1;
    int first_next = -1;   /* scene to switch to on the first frame */
    SongCtx songs = {&gd, &save, ui, &opt, &song, datadir, gamedata};
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
    } else if (MF_NATIVE_DLC && start_scene == SCENE_DLC) {   /* iOS, Android: the native page opens over the title */
        scene = SCENE_TITLE; ui_enter(ui, scene, NULL); first_next = SCENE_DLC;
    } else { scene = start_scene; ui_enter(ui, scene, NULL); if (scene == SCENE_BOOT) scene = SCENE_TITLE; }
    if (IS_RECORDS_SCENE(scene)) records_enter(records, scene, SCENE_TITLE);   /* RECORDS: --start-scene scores|records */
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
        int next = first_next;
        first_next = -1;
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
            } else if (IS_RECORDS_SCENE(scene)) {   /* RECORDS */
                int n = records_event(records, &e);
                if (n >= 0) next = n;
                if (has_m && next < 0 && (n = records_event(records, &m)) >= 0) next = n;
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
                if (IS_RECORDS_SCENE(scene)) records_leave(records, tr.target);   /* RECORDS */
                if (tr.target == SCENE_GAME) scene = SCENE_GAME;   /* created at the top of the loop */
                else {
                    opt.pv_mode = false;   /* leaving the PV view (or not in it) */
                    ui_enter(ui, tr.target, tr.has_res ? &tr.res : NULL);
                    if (IS_ENDING_SCENE(tr.target)) ending_enter(ending, tr.target, scene);
                    if (IS_RECORDS_SCENE(tr.target)) records_enter(records, tr.target, scene);   /* RECORDS */
                    scene = tr.target;
                }
                tr.switched = true;
                tr.start_ms = SDL_GetTicks() - FADE_FRAMES / 2 * 1000u / 30u;
                continue;
            }
            if (tr.switched && cnt >= FADE_FRAMES) tr.on = false;
            else if (tr.switched && IS_RECORDS_SCENE(scene)) records_update(records);   /* RECORDS */
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
                if (to == SCENE_RESULT) records_store(&save, &res);   /* RECORDS: written by ui_enter(SCENE_RESULT) */
                trans_start(&tr, scene, to, to == SCENE_RESULT ? &res : NULL);
            }
        } else if (IS_RECORDS_SCENE(scene) && next < 0) {   /* RECORDS */
            int n = records_update(records);
            if (n >= 0) next = n;
        } else if (IS_ENDING_SCENE(scene) && next < 0) {
            int n = ending_update(ending, false, &song, &diff);
            if (n >= 0) next = n;
        } else if (next < 0) {
            int n = ui_update(ui, &song, &diff);
            if (n >= 0) next = n;
        }

        /* a DLC pack finished downloading (dlcdl.c): new song list, outside play and the song lists */
        if (next < 0 && !tr.on && (scene == SCENE_OPTION || scene == SCENE_DLC || scene == SCENE_TITLE) && dlcdl_take_changed())
            dlc_reload(&songs);

#if MF_NATIVE_DLC
        while (next == SCENE_DLC) {   /* native DLC page; its IMPORT comes back to it */
#ifdef MF_IOS
            bool import = ios_dlc_page(win, &songs);
#else
            bool import = android_dlc_page(&songs);
#endif
            if (!import) next = scene == SCENE_TITLE ? -1 : SCENE_TITLE;   /* the title stays as it was */
            else if (!dlc_import(ren, mobile_files, appdir, &songs)) next = SCENE_QUIT;
        }
#endif
        if (next == SCENE_QUIT) { scene = SCENE_QUIT; continue; }
        if (next >= 0) trans_start(&tr, scene, next, NULL);   /* NextScene: fade, switch at the peak */

        view_begin(ren);
        /* a song that was just set up stays black until the scene change has faded in */
        bool game_held = tr.on && tr.switched && scene == SCENE_GAME;
        if (game_held) {
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderFillRect(ren, NULL);
        } else if (scene == SCENE_GAME && game) game_render(game);
        else if (IS_RECORDS_SCENE(scene)) records_render(records);   /* RECORDS */
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
#ifdef MF_IOS
        /* inactive or in the background SDL skips drawing and presenting; don't spin until iOS suspends us */
        if (SDL_GetWindowFlags(win) & SDL_WINDOW_MINIMIZED) SDL_Delay(16);
#endif
    }

    if (game) game_destroy(game);
    ending_destroy(ending);
    records_destroy(records);   /* RECORDS */
    dlcdl_quit();
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
