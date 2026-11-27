/*
 * PV view overlay (WindowPVView @0x46624..0x47810, MikuFlickCriManager GotoNextPV/GotoPrevPV).
 *
 * The movie itself is played by the game scene in pv_mode (video, audio, lyrics cues); this file
 * adds what WindowPVView puts on top of it:
 *   - horizontal swipes (touchDown/touchMoved/touchUp @0x474fc/0x47574/0x475f0): previous / next PV,
 *     or restart the current one, with the arrow sprite that fades out over 75 frames;
 *   - the pause button (TouchPause, type 9) and the pause menu RESUME / RETRY / MUSIC SELECT
 *     (TouchBack type 7, TouchRetry types 10 and 11) over the menugradation band;
 *   - the window's own black fade-in (setFade @0x46624, exec @0x46dd8);
 *   - the choice of the next PV (GotoNextPV / GotoPrevPV, PLAYLIST off; every song is unlocked here).
 * There is one PV view at a time, so the state is a file-level singleton that survives the in-place
 * reloads (CriManager ReLoad: the port recreates the Game object for the new movie).
 */
#include "mf.h"

#include <math.h>
#include <stdlib.h>

enum {
    SE_BACK = 8,     /* -[TouchBack touchUp:] @0x6ee72: RESUME */
    SE_BUTTON = 19,  /* -[TouchPause touchUp:] @0x729c2, -[TouchRetry touchUp:] @0x6a418: pause, RETRY, MUSIC SELECT */
};

/* arrowDir values set by touchUp @0x475f0 */
enum { ARROW_NONE = 0, ARROW_PREV = 1, ARROW_NEXT = 2, ARROW_RELOAD = 3 };

#define SWIPE_MIN 320.0f      /* touchUp: CloudMath::Abs(posEd.x - posSt.x) > 160.0 (0x43200000 @0x477d0), raw
                                 touches in UIKit points (half the 320-point screen) = 320 scene px */
#define PREV_MAX_FRAME 90     /* touchUp @0x476fe: GetMovieFrame <= 0x5a goes to the previous PV, else restart */
#define ARROW_FRAMES 75       /* arrowCnt = 0x4b; Render @0x46ed0 draws alpha arrowCnt / 75 and then decrements */

static struct {
    int song, nsongs;
    double fps;               /* movie frame rate (USM VIDEO_HDRINFO) for CriManager::GetMovieFrame */
    bool reloading;           /* the next pvview_begin is an in-place ReLoad, not a new PV view */
    int pending;              /* song to ReLoad after the current Game finishes, -1 none */
    bool refade;              /* RETRY: setFade again after the ReLoad */

    float fade;               /* m_Fade: black over the movie, 1 -> 0 at -0.03 per frame */
    int arrow_dir, arrow_cnt; /* arrowDir / arrowCnt */

    bool swipe;               /* posSt valid (set on touchDown while not paused) */
    float st_x, ed_x;         /* posSt.x / posEd.x */

    bool was_paused;          /* to reset the press state when the pause state changes */
    bool pbtn_down;           /* TouchPause held */
    int menu_press;           /* pause menu button held, -1 none */
    Uint32 dn_ms, lg_until;   /* port: a quick click keeps the held look until 100 ms after the press (ui.c ui_event) */
    int lg_press;
    int focus;                /* port: keyboard focus in the pause menu */
    bool kbd;

    Uint32 clock_last;        /* 30 Hz frame clock for the fade and the arrow (0 = start on the next render) */
    double clock_ms;
} S = {.pending = -1, .menu_press = -1};

/* TouchPause (WindowBase AddNewTouch type 9, the same button as in the game): 80x80 box at (570,890), left-handed
   (19,890); pausebutton (51x51) drawn 5/9 px inside; -[TouchPause Render] @0x72738, z -1.2 */
static float pbtn_x(const Options *opt) { return opt->left_hand ? 19.0f : 570.0f; }
static bool pbtn_hit(const Options *opt, float x, float y) {
    float bx = pbtn_x(opt);
    return x >= bx && x < bx + 80 && y >= 890 && y < 970;
}

/* the pause menu: same buttons and positions as WindowGameWindow (init @0x39498 adds the same types 9, 10/0,
   11/2, 7/1): TouchBack RESUME y 574, TouchRetry RETRY y 656 and MUSIC SELECT y 740, 324x82 at x 164
   (TouchRetry initType:No:Pos: @0x69a80). WindowPVView adds no INPUT TIMING bar. */
static const float k_menu_y[3] = {574, 656, 740};
static int menu_hit(float x, float y) {
    for (int i = 0; i < 3; i++)
        if (x >= 164 && x < 164 + 324 && y >= k_menu_y[i] && y < k_menu_y[i] + 82) return i;
    return -1;
}

void pvview_begin(const char *movie_path, int song, int nsongs) {
    double fps = 0;
    int dcprec = 0;
    S.fps = usm_video_info(movie_path, &fps, &dcprec) && fps > 1.0 ? fps : 29.97;
    S.song = song;
    S.nsongs = nsongs > 0 ? nsongs : 1;
    S.pending = -1;
    S.swipe = false;
    S.pbtn_down = false;
    S.menu_press = -1;
    S.was_paused = false;
    if (S.reloading) {        /* CriManager ReLoad: WindowPVView stays; arrow and fade carry on */
        S.reloading = false;
        if (S.refade) S.fade = 1.0f;   /* TouchButton 10 (RETRY) @0x4739c: ReLoad, startGame, SetUIAlpha:0, setFade */
        S.refade = false;
        return;
    }
    /* -[WindowPVView init] @0x467b0: arrowDir 0, posSt (-1,-1), startGame, setFade (m_Fade = 1) */
    S.fade = 1.0f;
    S.arrow_dir = ARROW_NONE;
    S.arrow_cnt = 0;
    S.refade = false;
    S.focus = 0;
    S.kbd = false;
    S.clock_last = 0;
    S.clock_ms = 0;
}

/* +[MikuFlickCriManager GotoNextPV:1] @0x29678 with PLAYLIST off: the next unlocked song after GetIndex,
   (idx + i) % GetTotalMusicNum for i = 1..total-1 (___modsi3); fewer than 2 songs reloads the current one */
static int next_song(void) { return S.nsongs < 2 ? S.song : (S.song + 1) % S.nsongs; }
/* +[MikuFlickCriManager GotoPrevPV] @0x29458 with PLAYLIST off: (total + idx - i) % total for i = 1..total-1 */
static int prev_song(void) { return S.nsongs < 2 ? S.song : (S.song + S.nsongs - 1) % S.nsongs; }

/* -[WindowPVView touchUp:] @0x475f0 after the 160-point test; dx = posEd.x - posSt.x */
static int swipe(float dx, double t) {
    if (dx < 0) {                                 /* moved left: GotoNextPV:1, arrowDir 2 */
        S.pending = next_song();
        S.arrow_dir = ARROW_NEXT;
    } else if ((int)(t * S.fps) <= PREV_MAX_FRAME) { /* moved right early in the movie: GotoPrevPV, arrowDir 1 */
        S.pending = prev_song();
        S.arrow_dir = ARROW_PREV;
    } else {                                      /* moved right later: ReLoad the current PV, arrowDir 3 */
        S.pending = S.song;
        S.arrow_dir = ARROW_RELOAD;
    }
    S.arrow_cnt = ARROW_FRAMES;
    return PVV_RELOAD;
}

/* -[WindowPVView TouchButton::] @0x47364 for the pause menu buttons */
static int menu_choose(Assets *as, int i) {
    if (i == 0) {                                 /* type 7 TouchBack: startGame */
        play_sound(as, SE_BACK);
        return PVV_RESUME;
    }
    play_sound(as, SE_BUTTON);
    if (i == 1) {                                 /* type 10 RETRY: ReLoad the current PV + setFade */
        S.pending = S.song;
        S.refade = true;
        return PVV_RELOAD;
    }
    return PVV_LEAVE;                             /* type 11 MUSIC SELECT: pauseGame, NextScene:7 (PV select) */
}

/* -[TouchPause touchUp:] @0x72910: only once the fade is over and before the movie's end; SE 19, then
   TouchButton 9: pauseGame (startGame when already paused, but the button is hidden while paused) */
static int pause_now(Assets *as, bool movie_end) {
    if (S.fade > 0.0f || movie_end) return PVV_NONE;
    play_sound(as, SE_BUTTON);
    return PVV_PAUSE;
}

int pvview_event(Assets *as, const Options *opt, const SDL_Event *e, bool paused, double t, bool movie_end) {
    if (paused != S.was_paused) {   /* pauseGame / startGame: SetState on the buttons */
        S.was_paused = paused;
        S.menu_press = -1;
        S.lg_until = 0;
        S.pbtn_down = false;
        S.kbd = false;
        S.focus = 0;
    }
    if (e->type == SDL_KEYDOWN) {
        if (e->key.repeat) return PVV_NONE;
        SDL_Keycode k = e->key.keysym.sym;
        if (k == SDLK_ESCAPE || k == SDLK_p) {       /* port: the keyboard's pause button / RESUME */
            if (paused) return menu_choose(as, 0);
            return pause_now(as, movie_end);
        }
        if (paused) {                                /* port: keyboard focus over RESUME / RETRY / MUSIC SELECT */
            if (k == SDLK_UP || k == SDLK_DOWN) {   /* as the game's pause menu (game.c) */
                S.focus = (S.focus + (k == SDLK_UP ? 2 : 1)) % 3;
                S.kbd = true;
            }
            if ((k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE)) return menu_choose(as, S.focus);
            return PVV_NONE;
        }
        /* port: Left/Right stand for a swipe to the right/left (previous or restart / next) */
        if (k == SDLK_LEFT) return swipe(1.0f, t);
        if (k == SDLK_RIGHT) return swipe(-1.0f, t);
        return PVV_NONE;
    }
    if ((e->type == SDL_MOUSEBUTTONDOWN || e->type == SDL_MOUSEBUTTONUP) && e->button.button != SDL_BUTTON_LEFT)
        return PVV_NONE;
    float x, y;
    if (e->type == SDL_MOUSEBUTTONDOWN || e->type == SDL_MOUSEBUTTONUP) { x = (float)e->button.x; y = (float)e->button.y; }
    else if (e->type == SDL_MOUSEMOTION) { x = (float)e->motion.x; y = (float)e->motion.y; }
    else return PVV_NONE;

    if (paused) {   /* TouchBack / TouchRetry: pressed on down, chosen on release over the same button */
        if (e->type == SDL_MOUSEBUTTONDOWN) { S.menu_press = menu_hit(x, y); S.kbd = false; S.dn_ms = SDL_GetTicks(); S.lg_until = 0; }
        else if (e->type == SDL_MOUSEBUTTONUP) {
            int was = S.menu_press, hit = menu_hit(x, y);
            if (was >= 0 && SDL_GetTicks() - S.dn_ms < 100) { S.lg_press = was; S.lg_until = S.dn_ms + 100; }   /* port: quick click */
            S.menu_press = -1;
            if (hit >= 0 && hit == was) return menu_choose(as, hit);
        }
        return PVV_NONE;   /* touchDown/Moved/Up of the window ignore touches while m_Pause */
    }
    if (e->type == SDL_MOUSEBUTTONDOWN) {
        S.pbtn_down = pbtn_hit(opt, x, y);       /* TouchPause touchDown */
        S.swipe = true;                          /* touchDown: posSt = posEd = touch */
        S.st_x = S.ed_x = x;
    } else if (e->type == SDL_MOUSEMOTION) {
        if (S.swipe) S.ed_x = x;                 /* touchMoved: posEd = touch */
    } else {
        if (S.pbtn_down) {
            S.pbtn_down = false;
            if (pbtn_hit(opt, x, y)) {
                S.swipe = false;
                return pause_now(as, movie_end);
            }
        }
        if (!S.swipe) return PVV_NONE;
        S.swipe = false;
        S.ed_x = x;                              /* port: the release point counts as the last move */
        float dx = S.ed_x - S.st_x;
        if (fabsf(dx) > SWIPE_MIN) return swipe(dx, t);   /* a plain tap does nothing */
    }
    return PVV_NONE;
}

int pvview_next(const Options *opt, bool left) {
    int s = -1;
    if (left) s = -1;                            /* MUSIC SELECT: NextScene:7 */
    else if (S.pending >= 0) s = S.pending;      /* swipe or RETRY */
    /* movie end: GotoNextPV:0 @0x29678 with PLAYLIST off ReLoads the same PV with LOOP (GetPVModeFlg:2)
       and otherwise returns 1 (NextScene:7). SHUFFLE (GetPVModeFlg:3) is only read in PLAYLIST mode, where it
       starts from CloudMath::Rand(GetTotalMusicNum); without the playlist editor the port lets SHUFFLE pick
       Rand(total) over every song (may repeat the same one). */
    else if (opt->pv_shuffle) s = rand() % S.nsongs;
    else if (opt->pv_loop) s = S.song;
    S.pending = -1;
    S.reloading = s >= 0;
    if (s < 0) S.refade = false;
    return s;
}

static Uint8 a255(float a) { return (Uint8)(a <= 0 ? 0 : a >= 1 ? 255 : 255.0f * a + 0.5f); }

/* the pause buttons' face pulse and press scaling (as in game.c / the menu buttons in ui.c) */
static Uint8 menu_pulse(unsigned counter) {
    int i = (int)(counter % 22);
    if (i > 10) i = 21 - i;
    return (Uint8)(255.0f * 0.85f * powf(0.98f, (float)i) + 0.5f);
}
static void scale_rect(float s, float *x, float *y, float *w, float *h) {
    *x -= *w * (s - 1) * 0.5f; *y -= *h * (s - 1) * 0.5f; *w *= s; *h *= s;
}

static void draw_menu(SDL_Renderer *r, Assets *as) {
    static const char *labels[3] = {"RESUME", "RETRY", "MUSIC SELECT"};
    const Atlas *u1 = &as->ui1;
    /* TouchBack / TouchRetry Render (@0x6a144..): base z -9.6, pulsing face z -9.7, brackets z -9.8, label z -9.9
       (layout as in game.c's pause menu) */
    Uint8 v = menu_pulse(SDL_GetTicks() / 67u);
    for (int i = 0; i < 3; i++) {
        float y = k_menu_y[i];
        bool lg = S.menu_press < 0 && S.lg_until && (Sint32)(S.lg_until - SDL_GetTicks()) > 0 && i == S.lg_press;
        bool down = i == S.menu_press || lg || (S.kbd && i == S.focus);
        float s = down ? 1.05f : 1.0f;
        float bx = 164, by = y, bw = 324, bh = 82;
        float fx = 165, fy = y + 0.5f, fw = 310, fh = 68.5f;
        float px = 165, py = y + 1, pw = 39, ph = 68;
        scale_rect(s, &bx, &by, &bw, &bh);
        scale_rect(s, &fx, &fy, &fw, &fh);
        scale_rect(s, &px, &py, &pw, &ph);
        if (down) px -= 7;
        spr_tl_wh(r, u1, "menubutton_m_base", bx, by, bw, bh, 255);
        SDL_SetTextureColorMod(u1->tex, v, v, v);
        spr_tl_wh(r, u1, down ? "menubutton_m_on" : "menubutton_m_off", fx - 1, fy, fw + 2, fh, v);
        SDL_SetTextureColorMod(u1->tex, 255, 255, 255);
        font_draw(r, labels[i], 326, y + 40 - 18 * s, 3.25f * s, (SDL_Color){255, 255, 255, 255}, 0);
        spr_tl_wh(r, u1, "parenthesis_00", px - 1, py, pw, ph, 255);
        spr_tl_wh(r, u1, "parenthesis_01", px + 271 * s, py, pw, ph, 255);
    }
}

/* -[WindowPVView Render] @0x46ed0: arrow sprite from SystemTexture 2 (UI_tex_01) at y 370, z -9.99,
   colour (1,1,1, arrowCnt/75), blend 1: next = arrow_forward (TP 0) at x 453; previous = arrow_forward
   mirrored (RenderTex2DClip with a negative source width) at x 21; restart = arrow_head (TP 1) at x 21 */
static void draw_arrow(SDL_Renderer *r, Assets *as) {
    if (S.arrow_dir == ARROW_NONE) return;
    const Atlas *u1 = &as->ui1;
    Uint8 a = a255((float)S.arrow_cnt / (float)ARROW_FRAMES);
    if (S.arrow_dir == ARROW_NEXT) spr_tl(r, u1, "arrow_forward", 453, 370, a);
    else if (S.arrow_dir == ARROW_RELOAD) spr_tl(r, u1, "arrow_head", 21, 370, a);
    else {
        const Frame *f = atlas_find(u1, "arrow_forward");
        if (!f || !u1->tex || !a) return;
        SDL_FRect dst = {21, 370, (float)f->r.w, (float)f->r.h};
        SDL_SetTextureAlphaMod(u1->tex, a);
        SDL_RenderCopyExF(r, u1->tex, &f->r, &dst, 0, NULL, SDL_FLIP_HORIZONTAL);
    }
}

void pvview_render(SDL_Renderer *r, Assets *as, const Options *opt, bool paused, bool have_frame, const char *lyric) {
    /* frame clock: exec (m_Fade) and Render (arrowCnt) run once per 30 fps frame */
    Uint32 now = SDL_GetTicks();
    int steps = 0;
    if (S.clock_last) {
        S.clock_ms += (double)(Uint32)(now - S.clock_last);
        steps = (int)(S.clock_ms / (1000.0 / 30.0));
        S.clock_ms -= steps * (1000.0 / 30.0);
        if (steps > 8) steps = 8;
    }
    S.clock_last = now ? now : 1;
    /* -[WindowPVView exec] @0x46dd8: m_Fade -= 0.03 (port: from the movie's first frame, as the game's fade) */
    if (have_frame)
        for (int i = 0; i < steps && S.fade > 0.0f; i++) S.fade = S.fade - 0.03f < 0.0f ? 0.0f : S.fade - 0.03f;

    /* TouchPause (z -1.2): hidden while paused (pauseGame SetState:3) and by DISPLAY PAUSE: INVISIBLE */
    if (!paused && opt->pause_button) {
        float px = pbtn_x(opt) + 5, py = 899, pw = 51, ph = 51;
        const Frame *f = atlas_find(&as->game1, "pausebutton");
        if (f) { pw = (float)f->r.w; ph = (float)f->r.h; }
        if (S.pbtn_down) scale_rect(1.05f, &px, &py, &pw, &ph);
        spr_tl_wh(r, &as->game1, "pausebutton", px, py, pw, ph, 255);
    }
    /* NoteManager render_Lyrics @0xda64 (z -5.1): the text texture at y=896 (shadow 898), x 15; text textures put
       the glyph tops ~6 px below their y, font_draw takes the glyph top */
    if (lyric && lyric[0]) font_draw(r, lyric, 320, 902, 2.8f, (SDL_Color){255, 255, 255, 255}, 0);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    /* paused: menugradation (TP 0x15a, SystemTexture 1) at (0, 374 + GetBottomOfs), 640 wide, z -9.3; no dimming */
    if (paused) {
        const Frame *mg = atlas_find(&as->game1, "menugradation");
        spr_tl_wh(r, &as->game1, "menugradation", 0, 374, 640, mg ? (float)mg->r.h : 586, 255);
    }
    /* m_Fade: RenderRect2D(0, 0, z -9.5, 640, GetFullScreenHeight, black with alpha m_Fade) */
    if (S.fade > 0.0f) {
        SDL_SetRenderDrawColor(r, 0, 0, 0, a255(S.fade));
        SDL_RenderFillRect(r, NULL);
    }
    if (paused) draw_menu(r, as);
    draw_arrow(r, as);
    /* Render: arrowCnt-- after drawing; at 0 arrowDir = 0 */
    if (S.arrow_dir != ARROW_NONE) {
        S.arrow_cnt -= steps;
        if (S.arrow_cnt <= 0) { S.arrow_cnt = 0; S.arrow_dir = ARROW_NONE; }
    }
}
