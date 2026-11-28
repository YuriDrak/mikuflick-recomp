/*
 * RECORDS: the title's crown icon -> RESULTS menu (SceneScore / WindowScore, scene 5) -> RECORDS
 * (SceneRecord / WindowRecord, scene 10), and the per song x difficulty records they show.
 *
 * - Crown: TouchGotoScore (WindowTitle touch type 4). initType @0x63440: m_Pos (14,893), collision =
 *   the 67x67 rect of frame 19 crownicon_on. Render @0x635ac: crownicon_off (18), crownicon_on (19)
 *   scaled 1.05x while touched (z -8.2). touchUp @0x63710: callback, then SE 0x13; WindowTitle
 *   TouchButton (menu open, state 10) -> NextScene 5.
 * - WindowScore (init @0x4fd28, loadTexture @0x500c0, Render @0x50178, TouchButton @0x504f8):
 *   bg_tex_05 full screen, "RESULTS" (font 48) at y 182 and the localized SCORE_MSG (font 24) at
 *   y 234, both centred with a dark 2 px shadow; TouchCommonButton (initType @0x63ec0) RECORDS
 *   (130,568), LEADERBOARD (130,648), ACHIEVEMENTS (130,728); TouchBack; PlayBGM 0x18 (BGM03).
 *   RECORDS -> NextScene 10. LEADERBOARD / ACHIEVEMENTS call MikuFlick2ViewController
 *   showLeaderboard @0x53140 / showAchievement @0x531cc (a GameKit sheet, no sign-in check). There is
 *   no Game Center here: the port uses WindowScore's own "no function" mode (setMode @0x4fc0c,
 *   mode 1: both Game Center buttons SetState 3, m_TextNoFunc at (320,696) z -1.2; Back returns to
 *   mode 0). The original never enters that mode (no caller of WindowScore setMode: 1/2); its
 *   Japanese m_TextNoFunc string is game text and is not copied: the port shows its own English note.
 * - WindowRecord (init @0x47840, loadTexture @0x481d4, exec @0x490b8, Render @0x49628,
 *   TouchButton @0x4adb8): one page per song, 534 px apart, scrolled by WindowBase ScrollExec; each
 *   song keeps its own difficulty (m_Difficultlys, Easy on entry). See the functions below.
 * - Stored values and when they change: records_store() (WindowResult loadTexture @0x4c2d0..0x4cbbe).
 *
 * Positions are top-left (TPManager RenderTex). The original depth-sorts by PosZ; this draws in that
 * order. Hooks into the shared files are marked RECORDS.
 */
#include "mf.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SE_BUTTON 0x13            /* SE02_01: Touch* touchUp / page and difficulty changes */
#define SE_BACK 8                 /* SE09: TouchBack touchUp; UpdateDifficultButton at an end */
#define SE_SLIDE 15               /* IsNextTextArea @0x494b0: Play 0xf (as the music select cover change) */
#define BGM03 (SND_BGM01 + 2)     /* SoundManager 0x18 */
#define PAGE_W 534                /* WindowRecord init: page width 0x216 */

/* TouchGotoScore: m_Pos (14,893) (0x41600000, 0x445f4000), 67x67 */
#define CROWN_X 14.0f
#define CROWN_Y 893.0f
#define CROWN_S 67.0f

/* TouchResultChange initType:No:Pos: @0x63aac (same class on this screen): forced to (530,784), 67x67 */
#define CHG_X 530.0f
#define CHG_Y 784.0f
#define CHG_S 67.0f

/* WindowScore TouchCommonButton positions (initType @0x63ec0 jump table, type - 6) */
#define LIST_X 130.0f
static const float k_list_y[3] = {568, 648, 728};
static const char *k_list_label[3] = {"RECORDS", "LEADERBOARD", "ACHIEVEMENTS"};

static const SDL_Color WHITE = {255, 255, 255, 255};
static const char *k_diffname[NUM_DIFF] = {"Easy", "Normal", "Hard", "Extreme", "Break The Limit"};

/* record screen buttons */
enum { B_PAGE_L, B_PAGE_R, B_DIFF_L, B_DIFF_R, B_BACK, NB_REC };
/* RESULTS buttons: list rows 0..2, then Back */
enum { S_BACK = 3, NB_SCORE };

struct Records {
    SDL_Renderer *ren;
    Assets *as;
    const GameData *gd;
    const Options *opt;
    const SaveData *save;
    int scene;
    unsigned ticks;
    Uint32 last_ms;
    double tick_acc;
    Atlas bg;                      /* bg_tex_05: bg_results */
    bool bg_loaded;
    Atlas music[8];                /* song art sheets by name (as ui.c), least recently used replaced */
    bool music_loaded[8];
    char music_name[8][64];
    unsigned music_used[8], music_clock;
    int bgm_voice;
    /* the touch that pressed a button (TouchBase m_TouchDownIdx): press point, largest move */
    int press;
    bool dn_on;
    float dn_x, dn_y, dn_mx, dn_my;
    Uint32 dn_ms, lg_until;        /* port: a quick click keeps the pressed look 100 ms from the press (ui.c ui_event) */
    int lg_press;
    /* WindowScore */
    int mode;                      /* m_Mode: 0 normal, 1 "no function" (Game Center buttons hidden) */
    int focus;                     /* port: keyboard focus row */
    bool kbd;
    /* WindowRecord */
    int sel;                       /* m_SelectMusic */
    int diff[MAX_SONGS];           /* m_Difficultlys */
    float scr, add, start, sw_st, sw_ed;   /* m_ScrPos.x m_ScrAdd.x m_ScrStartPos.x m_ScrSwipStPos.x m_ScrSwipEdPos.x */
    bool swipe;                    /* m_IsScrSwipe */
    bool touch_up;                 /* m_IsTouchUp */
    bool change;                   /* m_IsChange */
    int target;                    /* m_Target */
    bool shown[NB_REC];            /* TouchBase state 0 (true) / 3 (false) */
    bool toggle_shown;             /* m_hButtonHandle (TouchResultChange) */
    FlickRes fres;                 /* m_FlickResultMode (fres.page = flick page) and the boards */
};

static bool in_rect(float x, float y, float rx, float ry, float rw, float rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

/* scale a rect by s about its own centre (TouchHelper CoordinateScalingPoints) */
static void spr_scaled(SDL_Renderer *r, const Atlas *a, const char *name, float x, float y, float s, Uint8 alpha) {
    const Frame *f = atlas_find(a, name);
    if (!f) return;
    float w = (float)f->r.w, h = (float)f->r.h;
    x -= w * (s - 1) * 0.5f; y -= h * (s - 1) * 0.5f;
    spr_tl_wh(r, a, name, x, y, w * s, h * s, alpha);
}

/* the menu buttons' pulsing face level (as ui.c) */
static Uint8 pulse_level(unsigned counter) {
    int i = (int)(counter % 22);
    if (i > 10) i = 21 - i;
    return (Uint8)(255.0f * 0.85f * powf(0.98f, (float)i) + 0.5f);
}

/* ------------------------------------------------------------ title crown */

static struct { bool held; float dn_x, dn_y, mx, my; Uint32 dn_ms, lg_until; } C;   /* lg_until: quick-click look */

int records_crown_event(Assets *as, const SDL_Event *e) {
    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
        C.dn_x = (float)e->button.x; C.dn_y = (float)e->button.y;
        C.held = in_rect(C.dn_x, C.dn_y, CROWN_X, CROWN_Y, CROWN_S, CROWN_S);
        C.mx = C.my = 0;
        C.dn_ms = SDL_GetTicks();
        C.lg_until = 0;
        return -1;
    }
    if (e->type == SDL_MOUSEMOTION && (e->motion.state & SDL_BUTTON_LMASK) && C.held) {
        C.mx = fmaxf(C.mx, fabsf((float)e->motion.x - C.dn_x));
        C.my = fmaxf(C.my, fabsf((float)e->motion.y - C.dn_y));
        return -1;
    }
    if (e->type == SDL_MOUSEBUTTONUP && e->button.button == SDL_BUTTON_LEFT) {
        bool was = C.held;
        C.held = false;
        if (was && SDL_GetTicks() - C.dn_ms < 100) C.lg_until = C.dn_ms + 100;
        if (was && in_rect((float)e->button.x, (float)e->button.y, CROWN_X, CROWN_Y, CROWN_S, CROWN_S) &&
            C.mx <= CROWN_S && C.my <= CROWN_S) {
            play_sound(as, SE_BUTTON);   /* TouchGotoScore touchUp @0x63710: SE 0x13 */
            return SCENE_SCORE;
        }
        return -1;
    }
    /* port: keyboard equivalent (the crown sits bottom left): Left or R */
    if (e->type == SDL_KEYDOWN && !e->key.repeat && (e->key.keysym.sym == SDLK_LEFT || e->key.keysym.sym == SDLK_r)) {
        play_sound(as, SE_BUTTON);
        return SCENE_SCORE;
    }
    return -1;
}

void records_crown_render(SDL_Renderer *ren, const Atlas *ui1) {
    if (C.held || (C.lg_until && (Sint32)(C.lg_until - SDL_GetTicks()) > 0))
        spr_scaled(ren, ui1, "crownicon_on", CROWN_X, CROWN_Y, 1.05f, 255);
    else spr_tl(ren, ui1, "crownicon_off", CROWN_X, CROWN_Y, 255);
}

/* ------------------------------------------------------------ save data */

/*
 * WindowResult loadTexture @0x4c2d0..0x4cbbe, on every result (game over included), against the
 * MusicData of the song and difficulty:
 *   ClearRank      if the new rank is lower (better) (@0x4c4b8)          -> Record.rank (ui.c)
 *   SetClear       when cleared (m_ContentOpen)                         -> Record.cleared (ui.c)
 *   PerfectClear   when GetMaxCombo == the chart's notes (@0x4c5b8)       -> RecordEx.perfect
 *   if GetTopScore < stage + combo (strictly, @0x4c6a2): SetTopScore (Record.score, ui.c),
 *     SetTopStageScore, SetTopComboScore, SetTopNoteResult(0..5), SetFlickSuccessNum(key, dir):
 *     replaced together, only on a new top score
 *   TopCombo       per-field max (@0x4cb16)                             -> Record.combo (ui.c)
 *   TopInterludeCnt per-field max (@0x4cb88)                             -> RecordEx.interlude
 * FlickResultNum (the per key/direction note totals) and the total notes are chart constants, set
 * every time the song is loaded (MikuFlickCriManager CheckTotalNoteNum @0x29234/0x292b8).
 * Called before ui_enter(SCENE_RESULT), which updates Record and writes the file; Record.score is
 * still the old top score here.
 */
void records_store(SaveData *s, const GameResult *res) {
    if (!s || !res || res->rank < 0 || res->song < 0 || res->song >= MAX_SONGS || res->diff < 0 || res->diff >= NUM_DIFF)
        return;
    RecordEx *x = &s->recx[res->song][res->diff];
    const Record *r = &s->rec[res->song][res->diff];
    int total = res->stage_score + res->combo_score;
    x->notes = res->total_notes;
    memcpy(x->flick_total, res->flick_total, sizeof x->flick_total);
    if (res->total_notes > 0 && res->max_combo == res->total_notes) x->perfect = 1;
    if (r->score < total) {
        x->stage = res->stage_score;
        x->combo_score = res->combo_score;
        memcpy(x->result, res->counts, sizeof x->result);
        memcpy(x->flick_success, res->flick_success, sizeof x->flick_success);
    }
    if (res->interlude_hits > x->interlude) x->interlude = res->interlude_hits;
    x->stored = 1;
}

/* "recx song diff notes stage combo_score interlude perfect r0..r5" and per key
   "flk song diff key total0..4 success0..4" (direction codes 0 centre, 1 up, 2 right, 3 down, 4 left) */
bool records_load_line(SaveData *s, const char *line) {
    int song, diff, v[11];
    if (sscanf(line, "recx %d %d %d %d %d %d %d %d %d %d %d %d %d", &song, &diff, &v[0], &v[1], &v[2], &v[3], &v[4],
               &v[5], &v[6], &v[7], &v[8], &v[9], &v[10]) == 13) {
        if (song < 0 || song >= MAX_SONGS || diff < 0 || diff >= NUM_DIFF) return true;
        RecordEx *x = &s->recx[song][diff];
        x->notes = v[0]; x->stage = v[1]; x->combo_score = v[2]; x->interlude = v[3]; x->perfect = v[4] != 0;
        for (int i = 0; i < 6; i++) x->result[i] = v[5 + i];
        x->stored = 1;
        return true;
    }
    int key, t[NUM_FLICK_DIRS], c[NUM_FLICK_DIRS];
    if (sscanf(line, "flk %d %d %d %d %d %d %d %d %d %d %d %d %d", &song, &diff, &key, &t[0], &t[1], &t[2], &t[3], &t[4],
               &c[0], &c[1], &c[2], &c[3], &c[4]) == 13) {
        if (song < 0 || song >= MAX_SONGS || diff < 0 || diff >= NUM_DIFF || key < 0 || key >= NUM_KEYS) return true;
        RecordEx *x = &s->recx[song][diff];
        for (int d = 0; d < NUM_FLICK_DIRS; d++) {
            x->flick_total[key][d] = t[d] < 0 ? 0 : t[d];
            x->flick_success[key][d] = c[d] < 0 ? 0 : c[d];
        }
        return true;
    }
    return false;
}

void records_save_lines(const SaveData *s, FILE *f) {   /* song field: save_song_key (save.c) */
    char key[64];
    for (int i = 0; i < MAX_SONGS; i++)
        for (int d = 0; d < NUM_DIFF; d++) {
            const RecordEx *x = &s->recx[i][d];
            if (!x->stored || !save_song_key(s, i, key, sizeof key)) continue;
            fprintf(f, "recx %s %d %d %d %d %d %d %d %d %d %d %d %d\n", key, d, x->notes, x->stage, x->combo_score,
                    x->interlude, x->perfect, x->result[0], x->result[1], x->result[2], x->result[3], x->result[4],
                    x->result[5]);
            for (int k = 0; k < NUM_KEYS; k++) {
                const int *t = x->flick_total[k], *c = x->flick_success[k];
                if (!(t[0] | t[1] | t[2] | t[3] | t[4] | c[0] | c[1] | c[2] | c[3] | c[4])) continue;
                fprintf(f, "flk %s %d %d %d %d %d %d %d %d %d %d %d %d\n", key, d, k, t[0], t[1], t[2], t[3], t[4],
                        c[0], c[1], c[2], c[3], c[4]);
            }
        }
}

/* ------------------------------------------------------------ setup */

Records *records_create(SDL_Renderer *ren, Assets *as, const GameData *gd, const Options *opt, const SaveData *save) {
    Records *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    r->ren = ren; r->as = as; r->gd = gd; r->opt = opt; r->save = save;
    r->bgm_voice = -1;
    r->press = -1;
    flickres_reset(&r->fres);
    return r;
}

static void free_textures(Records *r) {
    if (r->bg_loaded) atlas_free(&r->bg);
    r->bg_loaded = false;
    for (int i = 0; i < 8; i++) {
        if (r->music_loaded[i]) atlas_free(&r->music[i]);
        r->music_loaded[i] = false;
        r->music_name[i][0] = 0;
    }
}

void records_destroy(Records *r) {
    if (!r) return;
    if (r->bgm_voice >= 0) mixer_sfx_stop(r->bgm_voice);
    free_textures(r);
    free(r);
}

static void play_bgm03(Records *r) {
    if (r->bgm_voice >= 0) mixer_sfx_stop(r->bgm_voice);
    r->bgm_voice = mixer_bgm_play(&r->as->sounds[BGM03], 1.0f, true);
}

static int nsongs(const Records *r) {
    int n = visible_songs(r->gd, r->opt);   /* DLC songs only while SHOW DLC SONGS is on */
    return n < 1 ? 1 : n > MAX_SONGS ? MAX_SONGS : n;
}

/* WindowRecord EnabledMoveArrow @0x4acc0: page arrows; at the first song only the right one */
static void enabled_move_arrow(Records *r) {
    r->shown[B_PAGE_L] = r->sel != 0;
    r->shown[B_PAGE_R] = r->sel == 0 || r->sel != nsongs(r) - 1;
}

/* WindowRecord EnableDifficultButton @0x4a9e0: both shown, then the one at an end hidden (the count
   is the unlocked difficulties: all five in the port) */
static void enable_difficult(Records *r) {
    int d = r->diff[r->sel];
    r->shown[B_DIFF_L] = r->shown[B_DIFF_R] = true;
    if (d == 0) r->shown[B_DIFF_L] = false;
    else if (d == NUM_DIFF - 1) r->shown[B_DIFF_R] = false;
}

/* WindowRecord Update @0x49220: ReRenderText, ReLoadCoverTexture, UpdateFlickBoard (drawn live
   here), EnabledMoveArrow */
static void update(Records *r) { enabled_move_arrow(r); }

void records_enter(Records *r, int scene, int prev) {
    (void)prev;
    r->scene = scene;
    r->ticks = 0;
    r->last_ms = SDL_GetTicks();
    r->tick_acc = 0;
    r->press = -1;
    r->dn_on = false;
    C.held = false;
    if (!r->bg_loaded) r->bg_loaded = atlas_load(&r->bg, r->ren, r->as->appdir, "bg_tex_05");
    if (scene == SCENE_SCORE) {
        r->mode = 0;          /* init: setMode:0 */
        r->focus = 0;
        r->kbd = false;
        play_bgm03(r);        /* loadTexture @0x500c0: PlayBGM 0x18 (restarts it, also back from RECORDS) */
    } else if (scene == SCENE_RECORD) {
        /* init @0x47840: m_SelectMusic 0, m_IsTouchUp 1, m_FlickResultMode 1 (score page), every song on
           Easy, m_ScrPos.x = sel * 534 (the replay path, previous scene 8/2, does not exist here) */
        r->sel = 0;
        memset(r->diff, 0, sizeof r->diff);
        r->scr = r->add = r->start = r->sw_st = r->sw_ed = 0;
        r->swipe = false;
        r->touch_up = true;
        r->change = false;
        r->target = 0;
        flickres_reset(&r->fres);
        for (int i = 0; i < NB_REC; i++) r->shown[i] = true;
        r->toggle_shown = true;
        update(r);
        enable_difficult(r);
        /* loadTexture @0x4853c: PlayBGM 0x18 only when it is not playing (BGM03 goes on from RESULTS) */
        if (r->bgm_voice < 0) play_bgm03(r);
    }
}

void records_leave(Records *r, int next) {
    if (IS_RECORDS_SCENE(next)) return;
    /* the title restarts BGM01 itself (ui.c) */
    if (r->bgm_voice >= 0) mixer_sfx_stop(r->bgm_voice);
    r->bgm_voice = -1;
    free_textures(r);
}

/* ------------------------------------------------------------ RESULTS (WindowScore) */

static bool score_shown(const Records *r, int b) { return b == 0 || b == S_BACK || r->mode == 0; }

static void score_rect(int b, float *x, float *y, float *w, float *h) {
    if (b == S_BACK) { *x = 17; *y = 873; *w = 129; *h = 82; return; }   /* TouchBack (17,873) */
    *x = LIST_X; *y = k_list_y[b]; *w = 394; *h = 82;                    /* menubutton_l_base */
}

/* WindowScore TouchButton @0x504f8 */
static int score_activate(Records *r, int b) {
    if (b == S_BACK) {
        play_sound(r->as, SE_BACK);
        if (r->mode != 0) { r->mode = 0; r->focus = 0; return -1; }   /* setMode 0 */
        return SCENE_TITLE;
    }
    play_sound(r->as, SE_BUTTON);
    if (b == 0) return SCENE_RECORD;
    /* 0xc / 0xd: Game Center (not ported) -> the dormant "no function" mode */
    r->mode = 1;
    r->focus = 0;
    return -1;
}

static void press_track(Records *r, const SDL_Event *e) {
    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
        r->dn_on = true;
        r->dn_x = (float)e->button.x; r->dn_y = (float)e->button.y;
        r->dn_mx = r->dn_my = 0;
    } else if (e->type == SDL_MOUSEMOTION && (e->motion.state & SDL_BUTTON_LMASK) && r->dn_on) {
        r->dn_mx = fmaxf(r->dn_mx, fabsf((float)e->motion.x - r->dn_x));
        r->dn_my = fmaxf(r->dn_my, fabsf((float)e->motion.y - r->dn_y));
    }
}

/* released inside by the touch that pressed it, not dragged further than the button's size (TouchBase) */
static bool tapped(const Records *r, float rx, float ry, float rw, float rh, float mx, float my) {
    return r->dn_on && in_rect(r->dn_x, r->dn_y, rx, ry, rw, rh) && in_rect(mx, my, rx, ry, rw, rh) &&
           r->dn_mx <= rw && r->dn_my <= rh;
}

static int score_event(Records *r, const SDL_Event *e) {
    press_track(r, e);
    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
        r->kbd = false;
        r->press = -1;
        for (int b = 0; b < NB_SCORE; b++) {
            float x, y, w, h;
            score_rect(b, &x, &y, &w, &h);
            if (score_shown(r, b) && in_rect(r->dn_x, r->dn_y, x, y, w, h)) r->press = b;
        }
        return -1;
    }
    if (e->type == SDL_MOUSEBUTTONUP && e->button.button == SDL_BUTTON_LEFT) {
        int b = r->press;
        r->press = -1;
        if (b < 0 || !score_shown(r, b)) { r->dn_on = false; return -1; }
        float x, y, w, h;
        score_rect(b, &x, &y, &w, &h);
        bool hit = tapped(r, x, y, w, h, (float)e->button.x, (float)e->button.y);
        r->dn_on = false;
        return hit ? score_activate(r, b) : -1;
    }
    if (e->type == SDL_KEYDOWN && !e->key.repeat) {   /* port: keyboard */
        SDL_Keycode k = e->key.keysym.sym;
        int rows = r->mode == 0 ? 3 : 1;
        if (k == SDLK_ESCAPE || k == SDLK_BACKSPACE) return score_activate(r, S_BACK);
        if (k == SDLK_UP) { r->focus = (r->focus + rows - 1) % rows; r->kbd = true; }
        if (k == SDLK_DOWN) { r->focus = (r->focus + 1) % rows; r->kbd = true; }
        if (k == SDLK_RETURN || k == SDLK_SPACE || k == SDLK_KP_ENTER) return score_activate(r, r->focus);
    }
    return -1;
}

/* TouchBack Render @0x6e270 (as ui.c render_back_button) */
static void render_back(Records *r, bool pressed) {
    const Atlas *u1 = &r->as->ui1;
    float s = pressed ? 1.05f : 1.0f;
    spr_scaled(r->ren, u1, "menubutton_s_base", 17, 873, s, 255);
    Uint8 v = pulse_level(r->ticks / 2);
    SDL_SetTextureColorMod(u1->tex, v, v, v);
    spr_scaled(r->ren, u1, pressed ? "menubutton_s_on" : "menubutton_s_off", 17, 873, s, v);
    SDL_SetTextureColorMod(u1->tex, 255, 255, 255);
    spr_scaled(r->ren, u1, "back", 51, 891, s, 255);
    spr_scaled(r->ren, u1, "parenthesis_00", pressed ? 18 - 2.5f : 18, pressed ? 874 + 1.2f : 874, s, 255);
    spr_scaled(r->ren, u1, "parenthesis_01", pressed ? 17 + 79.8f + 1 : 94, 874, s, 255);
}

/* TouchCommonButton Render @0x646d0 (as the OPTIONS list in ui.c): base at pos, additive pulsing face
   at (pos.x, pos.y + 1), brackets at pos + (1,1) / (342,1), label centred; 1.05x with the _on face
   while touched (CoordinateScalingPoints 0x3f866666) */
static void render_list_button(Records *r, float x, float y, const char *label, bool touched, bool lit) {
    SDL_Renderer *ren = r->ren;
    const Atlas *u1 = &r->as->ui1;
    float s = touched ? 1.05f : 1.0f;
    float cx = x + 197, cy = y + 41;   /* centre of the 394x82 base */
#define SX(px) (cx + ((px) - cx) * s)
#define SY(py) (cy + ((py) - cy) * s)
    const Frame *bf = atlas_find(u1, "menubutton_l_base");
    float bw = bf ? (float)bf->r.w : 394, bh = bf ? (float)bf->r.h : 82;
    spr_tl_wh(ren, u1, "menubutton_l_base", SX(x), SY(y), bw * s, bh * s, 255);
    Uint8 v = pulse_level(r->ticks / 2);
    SDL_SetTextureBlendMode(u1->tex, SDL_BLENDMODE_ADD);
    SDL_SetTextureColorMod(u1->tex, v, v, v);
    spr_tl_wh(ren, u1, touched || lit ? "menubutton_l_on" : "menubutton_l_off", SX(x), SY(y + 1), 380.2f * s, 68 * s, v);
    SDL_SetTextureColorMod(u1->tex, 255, 255, 255);
    SDL_SetTextureBlendMode(u1->tex, SDL_BLENDMODE_BLEND);
    const Frame *pf = atlas_find(u1, "parenthesis_00");
    float pw = pf ? (float)pf->r.w : 39, ph = pf ? (float)pf->r.h : 68;
    spr_tl_wh(ren, u1, "parenthesis_00", SX(x + 1), SY(y + 1), pw * s, ph * s, 255);
    spr_tl_wh(ren, u1, "parenthesis_01", SX(x + 342), SY(y + 1), pw * s, ph * s, 255);
    font_draw(ren, label, SX(x + 191), SY(y + 22), 3.2f * s, WHITE, 0);
#undef SX
#undef SY
}

static void render_bg(Records *r) {
    if (r->bg_loaded) spr_tl(r->ren, &r->bg, "bg_results", 0, 0, 255);
    else { SDL_SetRenderDrawColor(r->ren, 0, 0, 0, 255); SDL_RenderFillRect(r->ren, NULL); }
}

/* WindowScore Render @0x50178: bg (z 5), title y 182 (-4.1) and subtitle y 234 (-4.2) with their
   shadows at +2,+2; in mode 1 m_TextNoFunc (font 32) centred at (320, 696) (-1.2) */
static void score_render(Records *r) {
    render_bg(r);
    option_titles_render(r->ren, r->as->appdir, "RESULTS", "SCORE_MSG", NULL, NULL);
    for (int b = 0; b < 3; b++)
        if (score_shown(r, b))
            render_list_button(r, LIST_X, k_list_y[b], k_list_label[b], r->press == b, r->kbd && r->focus == b);
    if (r->mode != 0) font_draw(r->ren, "Game Center is not available.", 320, 703, 3.4f, WHITE, 0);   /* port text */
    render_back(r, r->press == S_BACK);
}

/* ------------------------------------------------------------ RECORDS (WindowRecord) */

/* WindowBase ScrollTouchDown/Moved @0x344c8 / 0x345a4: a swipe only inside the touch area. WindowRecord
   init: ScrollSetTouchArea (50,100)-(ScreenWidth - 50, ScreenHeight - 100) = (50,100)-(590,860), compared
   with the touch in UIKit points, i.e. x >= 100, y >= 200 px (the far corner is off screen) */
static bool in_scroll_area(float x, float y) { return x >= 100 && y >= 200 && x <= 1180 && y <= 1720; }

static void scroll_down(Records *r, float x, float y) {
    if (!in_scroll_area(x, y)) { r->swipe = false; return; }
    r->swipe = true;
    r->start = r->scr;
    r->sw_st = r->sw_ed = x;
}

static void scroll_moved(Records *r, float x, float y) {
    if (!in_scroll_area(x, y)) { r->swipe = false; return; }
    if (!r->swipe) { r->start = r->scr; r->sw_st = x; r->swipe = true; }
    r->sw_ed = x;
}

/* WindowBase ScrollExec @0x341b0: swiping 1:1 (points * 2), else glide with m_ScrAdd / 1.5 per tick
   and a 0.5 pull back inside ScrollSetLimit (0 .. (count - 1) * 534) */
static void scroll_exec(Records *r) {
    float hi = (float)((nsongs(r) - 1) * PAGE_W);
    if (r->swipe) {
        float old = r->scr;
        r->scr = r->start + (r->sw_st - r->sw_ed);
        r->add = r->scr - old;
    } else {
        r->scr += r->add;
        if (r->scr < 0) { r->scr += (0 - r->scr) * 0.5f; if (r->scr >= 0) r->scr = 0; }
        if (r->scr > hi) { r->scr += (r->scr - hi) * -0.5f; if (r->scr <= hi) r->scr = hi; }
    }
    r->add /= 1.5f;
    if (fabsf(r->add) < 0.1f) r->add = 0;
}

/* WindowRecord MoveNearResultPosition @0x49278: 80 % of the way per tick; once the (signed, integer)
   distance is <= 5 it snaps and shows the difficulty arrows and the toggle again. A page to the left
   (negative distance) therefore snaps at once. */
static void move_near(Records *r) {
    int d = (int)((float)r->target - r->scr);
    r->scr = (float)((double)r->scr + (double)d * 0.8);
    if (d <= 5) {
        r->scr = (float)r->target;
        enable_difficult(r);
        r->toggle_shown = true;
    }
}

/* WindowRecord IsNextTextArea @0x494b0: once per touch, a drag of 67 px or more from the selected
   page selects the neighbour (clamped), with SE 15 even when clamped */
static bool next_text_area(Records *r) {
    if (r->change) return false;
    int d = (int)((float)(r->sel * PAGE_W) - r->scr);
    if (abs(d) < 67) return false;
    r->change = true;
    if (d < 0) { if (r->sel + 1 < nsongs(r)) r->sel++; }
    else if (r->sel > 0) r->sel--;
    play_sound(r->as, SE_SLIDE);
    return true;
}

/* WindowRecord UpdateDifficultButton @0x4a83c */
static void update_difficult(Records *r, int delta) {
    int *d = &r->diff[r->sel];
    bool ok = true;
    *d += delta;
    if (*d < 0) { *d = 0; play_sound(r->as, SE_BACK); ok = false; }
    if (*d >= NUM_DIFF) { *d = NUM_DIFF - 1; play_sound(r->as, SE_BACK); return; }
    if (!ok) return;
    play_sound(r->as, SE_BUTTON);
    enable_difficult(r);   /* after ReRenderText / ReLoadCoverTexture / UpdateFlickBoard */
}

/* WindowRecord TouchButton 0x19 (TouchRecordPage No0 / No1): instant jump, SE only if it moved */
static void page_button(Records *r, int dir) {
    int n = nsongs(r);
    if (dir < 0) {
        if (r->sel - 1 >= 0) { r->sel--; play_sound(r->as, SE_BUTTON); }
        else r->sel = 0;
    } else {
        if (r->sel + 1 < n) { r->sel++; play_sound(r->as, SE_BUTTON); }
        else r->sel = n - 1;
    }
    update(r);
    r->target = r->sel * PAGE_W;
    r->scr = (float)r->target;
}

/* TouchRecordPage initType @0x6379c: m_Pos (-140|580, 480), collision m_Pos * 0.5 points, (100,50)
   points = 200x100 px; drawn at x 0 / 580 (Render @0x63920 adds 140 for No0), frames 133/134.
   TouchRecordDifficult initType @0x64d78: m_Pos (112|498, 251), frames 120/121, collision 20 px
   bigger on every side (pos * 0.5 - 10 points, frame / 2 + 20 points). */
static void rec_rect(const Records *r, int b, float *x, float *y, float *w, float *h) {
    const Atlas *u2 = &r->as->ui2;
    switch (b) {
    case B_PAGE_L: *x = -140; *y = 480; *w = 200; *h = 100; return;
    case B_PAGE_R: *x = 580; *y = 480; *w = 200; *h = 100; return;
    case B_DIFF_L:
    case B_DIFF_R: {
        const Frame *f = atlas_find(u2, b == B_DIFF_L ? "difficultyarrow_00" : "difficultyarrow_01");
        float fw = f ? (float)f->r.w : 30, fh = f ? (float)f->r.h : 30;
        *x = (b == B_DIFF_L ? 112 : 498) - 20.0f; *y = 251 - 20.0f; *w = fw + 40; *h = fh + 40;
        return;
    }
    default: *x = 17; *y = 873; *w = 129; *h = 82; return;   /* TouchBack */
    }
}

/* WindowRecord TouchButton @0x4adb8 */
static int rec_activate(Records *r, int b) {
    switch (b) {
    case B_PAGE_L: page_button(r, -1); return -1;
    case B_PAGE_R: page_button(r, 1); return -1;
    case B_DIFF_L: update_difficult(r, -1); return -1;
    case B_DIFF_R: update_difficult(r, 1); return -1;
    default: play_sound(r->as, SE_BACK); return SCENE_SCORE;   /* 7 -> NextScene 5 */
    }
}

static void toggle_page(Records *r) {
    /* 0x20: m_FlickResultMode ^= 1 (boards SetState 0/3); TouchResultChange touchUp: SE 0x13 */
    r->fres.page = !r->fres.page;
    play_sound(r->as, SE_BUTTON);
}

static int record_event(Records *r, const SDL_Event *e) {
    press_track(r, e);
    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
        float x = (float)e->button.x, y = (float)e->button.y;
        /* touchDown: m_IsTouchUp = 0, ScrollTouchDown */
        r->touch_up = false;
        scroll_down(r, x, y);
        r->press = -1;
        for (int b = 0; b < NB_REC; b++) {
            float bx, by, bw, bh;
            rec_rect(r, b, &bx, &by, &bw, &bh);
            if (r->shown[b] && in_rect(x, y, bx, by, bw, bh)) r->press = b;
        }
        /* toggle and boards (flickresult.c); a hidden toggle takes no touch */
        if (r->toggle_shown || !in_rect(x, y, CHG_X, CHG_Y, CHG_S, CHG_S)) flickres_event(&r->fres, r->as, e);
        return -1;
    }
    if (e->type == SDL_MOUSEMOTION && (e->motion.state & SDL_BUTTON_LMASK)) {
        scroll_moved(r, (float)e->motion.x, (float)e->motion.y);
        flickres_event(&r->fres, r->as, e);
        /* touchMoved @0x4b0a8: both difficulty arrows, the toggle (and replay) SetState 3. Port: only once
           the touch has moved more than 4 px, so a click with a little mouse jitter still works */
        if (r->dn_on && (r->dn_mx > 4 || r->dn_my > 4)) {
            r->shown[B_DIFF_L] = r->shown[B_DIFF_R] = false;
            r->toggle_shown = false;
            r->fres.held = false;
        }
        return -1;
    }
    if (e->type == SDL_MOUSEBUTTONUP && e->button.button == SDL_BUTTON_LEFT) {
        float x = (float)e->button.x, y = (float)e->button.y;
        /* touchUp: m_IsTouchUp = 1, m_Target = sel * 534, m_IsChange = 0, ScrollTouchUp */
        r->touch_up = true;
        r->target = r->sel * PAGE_W;
        r->change = false;
        r->swipe = false;
        r->sw_ed = x;
        int b = r->press, next = -1;
        r->press = -1;
        if (b >= 0 && r->shown[b]) {
            float bx, by, bw, bh;
            rec_rect(r, b, &bx, &by, &bw, &bh);
            if (tapped(r, bx, by, bw, bh, x, y)) next = rec_activate(r, b);
        }
        r->dn_on = false;
        flickres_event(&r->fres, r->as, e);   /* toggle: page flip + SE 0x13 (flickresult.c) */
        return next;
    }
    if (e->type == SDL_KEYDOWN && !e->key.repeat) {   /* port: keyboard, through the visible buttons */
        SDL_Keycode k = e->key.keysym.sym;
        if (k == SDLK_ESCAPE || k == SDLK_BACKSPACE) return rec_activate(r, B_BACK);
        if (k == SDLK_LEFT && r->shown[B_PAGE_L]) return rec_activate(r, B_PAGE_L);
        if (k == SDLK_RIGHT && r->shown[B_PAGE_R]) return rec_activate(r, B_PAGE_R);
        if (k == SDLK_UP && r->shown[B_DIFF_L]) return rec_activate(r, B_DIFF_L);
        if (k == SDLK_DOWN && r->shown[B_DIFF_R]) return rec_activate(r, B_DIFF_R);
        if ((k == SDLK_SPACE || k == SDLK_TAB) && r->toggle_shown) { toggle_page(r); return -1; }
        if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && r->fres.page) {   /* next board's direction guide */
            int n = r->fres.open + 1;
            r->fres.open = n >= NUM_KEYS ? -1 : n;
        }
    }
    return -1;
}

/* WindowRecord exec @0x490b8, one 30 Hz tick: score page ScrollExec, then MoveNearResultPosition
   after the touch (or IsNextTextArea -> Update while it is held); the flick page only
   MoveNearResultPosition */
static void record_tick(Records *r) {
    if (!r->fres.page) {
        scroll_exec(r);
        if (r->touch_up) move_near(r);
        else if (next_text_area(r)) update(r);
    } else move_near(r);
}

/* ---- render */

#define MUSIC_REF "music_00_01"   /* the only art sheet with a .plist; never replaced */

/* art sheet by name (music_00_01..04 built-in, music_NN_01 per DLC pack), as ui.c music_sheet */
static Atlas *music_sheet(Records *r, const char *name) {
    int slot = -1, lru = -1;
    for (int i = 0; i < 8; i++) {
        if (!strcmp(r->music_name[i], name)) {
            r->music_used[i] = ++r->music_clock;
            return r->music_loaded[i] ? &r->music[i] : NULL;
        }
        if (!r->music_name[i][0]) { if (slot < 0) slot = i; }
        else if (strcmp(r->music_name[i], MUSIC_REF) && (lru < 0 || r->music_used[i] < r->music_used[lru])) lru = i;
    }
    if (slot < 0) slot = lru;
    if (slot < 0) return NULL;
    if (r->music_loaded[slot]) atlas_free(&r->music[slot]);
    snprintf(r->music_name[slot], sizeof r->music_name[slot], "%s", name);
    r->music_loaded[slot] = atlas_load(&r->music[slot], r->ren, r->as->appdir, name);
    r->music_used[slot] = ++r->music_clock;
    return r->music_loaded[slot] ? &r->music[slot] : NULL;
}

/* song art/logo; only music_00_01 has a plist, the other sheets share its layout (as ui.c) */
static void draw_music(Records *r, int song, const char *kind, float x, float y, float w, float h) {
    Atlas *ref = music_sheet(r, MUSIC_REF);
    Atlas *m = music_sheet(r, r->gd->songs[song].art_atlas);
    if (!m || !ref) return;
    char fn[64];
    snprintf(fn, sizeof fn, "%s_00_01_%02d", kind, r->gd->songs[song].art_index);   /* SetArtWorkIndex */
    const Frame *f = atlas_find(ref, fn);
    if (f) spr_tl_rect(r->ren, m, &f->r, x, y, w, h, 255);
}

/* WindowRecord RenderBgRect @0x496e0: WindowResult's 9-slice shifted by ox, titlebar (104,238) */
static void render_panel(Records *r, float ox) {
    SDL_Renderer *ren = r->ren;
    const Atlas *u2 = &r->as->ui2;
    const Frame *mid = atlas_find(u2, "resultwindow_05");
    if (mid) {
        SDL_Rect src = {mid->r.x + 1, mid->r.y + 1, 2, 2};
        spr_tl_rect(ren, u2, &src, ox + 144, 298, 338, 441, 255);
    }
    for (int x = 167; x < 482; x += 63) {
        spr_tl(ren, u2, "resultwindow_04", ox + (float)x, 223, 255);
        spr_tl(ren, u2, "resultwindow_05", ox + (float)x, 739, 255);
    }
    for (int y = 298; y < 739; y += 63) {
        spr_tl(ren, u2, "resultwindow_06", ox + 81, (float)y, 255);
        spr_tl(ren, u2, "resultwindow_07", ox + 482, (float)y, 255);
    }
    spr_tl(ren, u2, "resultwindow_00", ox + 81, 223, 255);
    spr_tl(ren, u2, "resultwindow_01", ox + 482, 223, 255);
    spr_tl(ren, u2, "resultwindow_02", ox + 81, 739, 255);
    spr_tl(ren, u2, "resultwindow_03", ox + 482, 739, 255);
}

/* "#,##0" */
static void fmt_commas(char *buf, size_t n, int v) {
    char d[16];
    snprintf(d, sizeof d, "%d", v < 0 ? 0 : v);
    int len = (int)strlen(d), o = 0;
    for (int j = 0; j < len && o + 2 < (int)n; j++) {
        if (j > 0 && (len - j) % 3 == 0) buf[o++] = ',';
        buf[o++] = d[j];
    }
    buf[o] = 0;
}

/* WindowRecord RenderScore @0x49a80: RenderUnderLine bars, 11 labels (font 25, z -1.1) and values
   right-aligned at the x of table @0x13c03c (z -1.2), same rows as the result screen. Values from
   GetScore @0x48880: TotalNotes, TopCombo, TopNoteResult 5..1, TopInterludeCnt, TopStageScore,
   TopComboScore, TopScore. */
static void render_score(Records *r, float ox, int song, int diff) {
    SDL_Renderer *ren = r->ren;
    const Atlas *u2 = &r->as->ui2;
    static const struct { const char *bar; float x, y, w; } bars[] = {
        {"textbar_00", 114, 316, 233}, {"textbar_00", 114, 354, 233},
        {"textbar_01", 181, 403, 278}, {"textbar_01", 181, 441, 278}, {"textbar_01", 181, 479, 278},
        {"textbar_01", 181, 517, 278}, {"textbar_01", 181, 556, 278}, {"textbar_01", 181, 606, 278},
        {"textbar_00", 114, 652, 412}, {"textbar_00", 114, 690, 412}, {"textbar_00", 114, 728, 412},
    };
    for (size_t i = 0; i < sizeof bars / sizeof *bars; i++)
        spr_tl_wh(ren, u2, bars[i].bar, ox + bars[i].x, bars[i].y, bars[i].w, 23, 255);
    static const struct { const char *label; float lx, vx, y; } rows[11] = {
        {"Total Notes", 124, 340, 308}, {"Combo", 124, 340, 346},
        {"Cool", 206, 435, 394}, {"Fine", 206, 435, 432}, {"Safe", 206, 435, 470}, {"Sad", 206, 435, 508},
        {"Worst", 206, 435, 547}, {"\xe2\x99\xaa", 206, 435, 597},
        {"Stage Score", 131, 510, 644}, {"Combo Bonus", 131, 510, 682}, {"Total Score", 132, 510, 720},
    };
    const Record *rc = &r->save->rec[song][diff];
    const RecordEx *x = &r->save->recx[song][diff];
    const int vals[11] = {x->notes, rc->combo, x->result[5], x->result[4], x->result[3], x->result[2], x->result[1],
                          x->interlude, x->stage, x->combo_score, rc->score};
    char buf[32];
    for (int i = 0; i < 11; i++) {
        /* "♪": the quaver sprite when the font lacks the glyph (as ui.c) */
        if (i == 7 && !text_has_glyph(0x266A)) spr_tl(ren, &r->as->game1, "quaver_00", ox + rows[i].lx - 6, rows[i].y - 26, 255);
        else font_draw(ren, rows[i].label, ox + rows[i].lx + (i == 7 ? 4 : 0), rows[i].y + 6, 2.6f, WHITE, -1);
        fmt_commas(buf, sizeof buf, vals[i]);
        font_draw(ren, buf, ox + rows[i].vx, rows[i].y + 6, 2.6f, WHITE, 1);
    }
}

/* WindowRecord RenderResult @0x4a32c for page i at ox = (int)(i * 534 - scr) */
static void render_page(Records *r, int song) {
    SDL_Renderer *ren = r->ren;
    const Atlas *u2 = &r->as->ui2;
    int diff = r->diff[song];
    float ox = (float)(int)((float)(song * PAGE_W) - r->scr);
    const Record *rc = &r->save->rec[song][diff];
    bool score_page = !r->fres.page;
    render_panel(r, ox);
    /* clearrankstamp (349,224) z -0.1 when IsClear, score page only */
    if (score_page && rc->cleared) spr_tl(ren, u2, "clearrankstamp", ox + 349, 224, 255);
    /* RenderArtwork @0x4a0b4: jacket (118,67) 142x142 (-0.2), logo (279,67) 256x128 (-0.3) */
    draw_music(r, song, "art", ox + 118, 67, 142, 142);
    draw_music(r, song, "logo", ox + 279, 67, 256, 128);
    spr_tl(ren, u2, "titlebar", ox + 104, 238, 255);
    /* rank (IsPlayed): clearrank_NN = frame 110 + rank, PERFECT (132) for rank 0, at (394,298) z -1 */
    if (score_page && rc->rank >= 0) {
        char name[32];
        if (rc->rank > 0) snprintf(name, sizeof name, "clearrank_%02d", rc->rank - 1);
        else snprintf(name, sizeof name, "perfect");
        spr_tl(ren, u2, name, ox + 394, 298, 255);
    }
    /* stars (GetDifficultNum): 9 resultstat_00 then |n| resultstat_01 (_02 when negative) at
       (290 + 26j, 191). The original skips them when StatusData GetDifficulty (the difficulty last
       played) is Break the Limit; the port skips them on a Break the Limit page */
    if (diff != DIFF_BREAK) {
        int n = r->gd->songs[song].stars[diff];
        const char *lit = n < 0 ? "resultstat_02" : "resultstat_01";
        if (n < 0) n = -n;
        for (int j = 0; j < 9; j++) spr_tl(ren, u2, "resultstat_00", ox + 290 + 26.0f * j, 191, 255);
        for (int j = 0; j < n && j < 9; j++) spr_tl(ren, u2, lit, ox + 290 + 26.0f * j, 191, 255);
    }
    /* RenderDifficult:: @0x4a754: name (font 36) centred at x 320, y 240 (as the result screen) */
    font_draw(ren, k_diffname[diff], ox + 320, 247, 3.9f, WHITE, 0);
    if (score_page) render_score(r, ox, song, diff);
}

/* WindowRecord Render @0x49628: bg (z 3), the selected page and its neighbours; then the touches:
   page arrows (-9.3), difficulty arrows (-9.4), the boards, the toggle (-9.5), Back */
static void record_render(Records *r) {
    SDL_Renderer *ren = r->ren;
    const Atlas *u2 = &r->as->ui2;
    int n = nsongs(r);
    render_bg(r);
    if (r->sel > 0) render_page(r, r->sel - 1);
    render_page(r, r->sel);
    if (r->sel < n - 1) render_page(r, r->sel + 1);
    if (r->shown[B_PAGE_L]) spr_tl(ren, u2, "resultarrow_00", 0, 480, 255);
    if (r->shown[B_PAGE_R]) spr_tl(ren, u2, "resultarrow_01", 580, 480, 255);
    if (r->shown[B_DIFF_L]) spr_tl(ren, u2, "difficultyarrow_00", 112, 251, 255);
    if (r->shown[B_DIFF_R]) spr_tl(ren, u2, "difficultyarrow_01", 498, 251, 255);
    /* TouchRecordFlickBoard: the boards of the selected song and difficulty (UpdateFlickBoard @0x49140) */
    if (r->fres.page) {
        const RecordEx *x = &r->save->recx[r->sel][r->diff[r->sel]];
        GameResult tmp;
        memset(&tmp, 0, sizeof tmp);
        tmp.song = r->sel;
        tmp.diff = r->diff[r->sel];
        memcpy(tmp.flick_total, x->flick_total, sizeof tmp.flick_total);
        memcpy(tmp.flick_success, x->flick_success, sizeof tmp.flick_success);
        flickres_render(ren, r->as, r->gd, r->opt, &tmp, &r->fres);
    }
    if (r->toggle_shown) flickres_render_button(ren, r->as, &r->fres);
    render_back(r, r->press == B_BACK);
}

/* ------------------------------------------------------------ dispatch */

int records_event(Records *r, const SDL_Event *e) {
    if (!r) return -1;
    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) { r->dn_ms = SDL_GetTicks(); r->lg_until = 0; }
    if (e->type == SDL_MOUSEBUTTONUP && e->button.button == SDL_BUTTON_LEFT && r->press >= 0 && SDL_GetTicks() - r->dn_ms < 100) {
        r->lg_press = r->press;
        r->lg_until = r->dn_ms + 100;
    }
    return r->scene == SCENE_SCORE ? score_event(r, e) : record_event(r, e);
}

int records_update(Records *r) {
    if (!r) return -1;
    Uint32 now = SDL_GetTicks();
    r->tick_acc += (double)(now - r->last_ms);
    r->last_ms = now;
    if (r->tick_acc > 500) r->tick_acc = 500;
    while (r->tick_acc >= 1000.0 / 30.0) {
        r->tick_acc -= 1000.0 / 30.0;
        r->ticks++;
        if (r->scene == SCENE_RECORD) record_tick(r);
    }
    return -1;
}

void records_render(Records *r) {
    if (!r) return;
    int press = r->press;
    if (press < 0 && r->lg_until && (Sint32)(r->lg_until - SDL_GetTicks()) > 0) r->press = r->lg_press;
    if (r->scene == SCENE_SCORE) score_render(r);
    else record_render(r);
    r->press = press;
}
