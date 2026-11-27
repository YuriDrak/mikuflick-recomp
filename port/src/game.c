/*
 * Rhythm game scene (SceneGame / NoteManager / NoteNormal / NoteInterlude /
 * TouchFlickBoard / StatusData). Every rule below is ported from the
 * decompiled v1.1.5 binary; addresses refer to docs/re_notes.md.
 *
 * Coordinates are the original 640x960 render space. Gameplay time is the
 * 30 Hz PlayCnt derived from the song's audio clock.
 */
#include "mf.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPAWN_X 740.0f        /* NoteNormal m_Pos.x = 0x44390000 */
#define NOTE_Y 442.0f         /* m_Pos.y = 0x43dd0000 (+ GetBottomOfs = 0 on 640x960); glyph top edge */
#define LANE_CY 481.0f        /* visual lane centre: NOTE_Y + 78/2 */
#define JUDGE_CX 68.0f        /* insidewhirl centre (RenderTexRot 68,481) */
#define JUDGE_X (SPAWN_X - 642.0f)
#define MAX_POINTERS 8
#define MAX_FX 32
#define KEY_W 127
#define KEY_H 108
#define MIN_PRESS_MS 100   /* port: 3 ticks at 30 Hz, see Game.key_down_ms */

enum { NOTE_NORMAL = 0, NOTE_INTERLUDE = 1 };
enum { PTR_MOUSE = 0, PTR_KEYBOARD = 1, PTR_FINGER = 2 };

typedef struct {
    int type;
    int ch;                  /* kana index (char - U+3041) */
    bool active;             /* m_Active: playable and not yet judged */
    bool playable;           /* created enabled (TelopType_Enable) */
    bool small;              /* SmallTelop option */
    bool crimax;             /* m_CrimaxMode */
    int board, flick;        /* m_BoardType / m_FlickResult (expected key, direction) */
    float spdx;              /* m_SpdX px/s */
    int just, base, cnt;     /* m_JustFrame, m_BaseTime, m_Cnt */
    bool started, inside;    /* exec reached / m_InsideJudge */
    int down, up;            /* m_TouchDownFrame / m_TouchUpFrame */
    int grade;               /* last judgement (display) */
    float x;
    bool dead;
} Note;

typedef struct {
    bool down;
    int kind;
    SDL_FingerID fid;
    int key;                 /* board key under the initial press, -1 none */
    float x0, y0, x1, y1;    /* logical coords */
    int kb_dir;              /* keyboard: chosen direction */
    SDL_Keycode kb_key;
} Pointer;

/* effects, timed in 30 Hz ticks like the original EffectManager (EAGLView frame interval 2) */
enum { FX_NONE, FX_RESULT, FX_PULSE, FX_INTERLUDE };
typedef struct { int kind; double t0; int v; } Fx;

/* SoundManager Play: indices used by the game scene's buttons (same table as SND_*) */
enum {
    SE_BACK = 8,     /* -[TouchBack touchUp:] @0x6ee72 (pause menu RESUME) */
    SE_BUTTON = 19,  /* -[TouchPause touchUp:] @0x729c2, -[TouchRetry touchUp:] @0x6a418 (RETRY, MUSIC SELECT) */
};

struct Game {
    SDL_Renderer *ren;
    Assets *as;
    const GameData *gd;
    Options *opt;            /* writable: the pause menu INPUT TIMING bar stores input_timing */
    int song, diff;

    CueList cues;
    int next_cue;
    Pcm tracks[4];
    int ntracks;
    VideoStream *video;

    int bpm, delay_ms;
    Note *notes;
    int nnotes, cap;
    int play_cnt;            /* MikuFlickCriManager GetPlayCnt */
    double t;
    int evt_dcnt;            /* PlayCnt at the input event's own timestamp minus play_cnt */

    int counts[6];
    int combo, max_combo, stage_score, combo_score, interlude_cnt;
    int total_notes, total_interlude;
    int flick_total[NUM_KEYS][NUM_FLICK_DIRS], flick_success[NUM_KEYS][NUM_FLICK_DIRS];   /* GameResult */
    float gauge;            /* DAT_00163088 */
    bool game_over, interlude_mode, paused, finished, quit, retry;
    int pause_focus;
    int pause_press;         /* pause button held by the pointer, -1 none */
    /* port: a quick click keeps the held look until 100 ms after the press (as ui.c ui_event, pvview.c) */
    int pause_lg;
    Uint32 pause_dn_ms, pause_lg_until;
    bool pause_kbd;          /* keyboard focus shown (the original is touch-only) */
    bool timing_drag;        /* pause menu INPUT TIMING bar being dragged */
    float timing_x;          /* pointer x at the last timing step */
    char lyric[256];         /* PV mode: current lyric line (op 3 cue) */

    /* WindowManager exec @0x3e21c: UI fades, one step per 30 Hz tick, skipped while paused
       (driven by PlayCnt here, which stops while paused) */
    int ui_tick;             /* PlayCnt the fades have been stepped to */
    float ui_alpha, ui_rate; /* SetUIFade: keys, charactercircle, whirls, tension meter */
    float int_alpha, int_rate; /* SetInterludeUIFade: flickguide_int, charactercircle_int */
    int int_hold;            /* EndInterludeMode: ticks before the interlude panel fades out */
    bool int_panel;          /* TouchFlickBoard setInterludeMode (panel drawn); interlude_mode is the touch rect */

    /* WindowGameWindow exec @0x39b3c: every frame, also while paused (wall clock, 30 Hz) */
    Uint32 clock_last;
    double clock_ms;
    float fade;              /* m_Fade: full-screen black, 1 -> 0 at -0.03 per tick */
    int rail_pos;            /* rail_01 scroll, +32 px per tick mod 640 */
    int rot_a, rot_b, rot_star; /* m_ComboRot (+12), m_ComboRot2 (+13), m_ComboStarRot (+17) */
    bool sp_on;              /* StartSpScoreEffect: bonus score popup */
    int sp_cnt, sp_score;
    float sp_y, sp_alpha;

    /* TouchPause: held by a pointer (opens on release over the button) */
    bool pbtn_down;
    int pbtn_kind;
    SDL_FingerID pbtn_fid;
    Uint32 pbtn_ms;          /* port: wall time of its last touch-down (minimum visible press) */
    /* port: a mouse click / trackpad tap can deliver down and up inside one frame, so the original's
       pressed look (set on touchDown, drawn by the next Render) would never reach the screen: each key
       keeps its last press time and release direction and stays lit for at least MIN_PRESS_MS */
    Uint32 key_down_ms[NUM_KEYS];
    int key_up_dir[NUM_KEYS];

    Pointer ptr[MAX_POINTERS];
    Fx fx[MAX_FX];
    int fx_next;
    int last_grade;
    double last_grade_t;
    int highscore;
    bool loaded;
};

/* ---------------------------------------------------------------- tables */

/* +[NoteManager GetCenterTime:] @0xe57c: approach time in seconds by BPM */
static float center_time(int bpm) {
    if (bpm < 80) return 3.0f;
    if (bpm < 90) return 2.9f;
    if (bpm < 100) return 2.8f;
    if (bpm < 110) return 2.6f;
    if (bpm < 120) return 2.4f;
    if (bpm < 130) return 2.2f;
    if (bpm < 140) return 2.1f;
    if (bpm < 150) return 2.0f;
    if (bpm < 160) return 1.9f;
    if (bpm < 170) return 1.8f;
    if (bpm < 180) return 1.7f;
    if (bpm < 190) return 1.6f;
    if (bpm < 200) return 1.5f;
    if (bpm < 210) return 1.4f;
    if (bpm < 220) return 1.3f;
    if (bpm < 230) return 1.2f;
    if (bpm < 240) return 1.1f;
    return 1.0f;
}

static int kana_index(const char *s, int *bytes) {
    uint32_t cp;
    int n = utf8_decode(s, &cp);
    if (bytes) *bytes = n;
    if (n == 0) return -1;
    int idx = (int)cp - KANA_BASE;
    return (idx >= 0 && idx < NUM_KANA) ? idx : -1;
}

/* the digit at code-point position 'pos' of a cue parameter */
static int digit_at(const char *s, int pos) {
    uint32_t cp;
    for (int i = 0; *s; i++) {
        int n = utf8_decode(s, &cp);
        if (!n) break;
        if (i == pos) return (cp >= '0' && cp <= '9') ? (int)(cp - '0') : -1;
        s += n;
    }
    return -1;
}

/* the pause buttons' face pulse and press scaling (same as the menu buttons in ui.c) */
static Uint8 pause_pulse(unsigned counter) {
    int i = (int)(counter % 22);
    if (i > 10) i = 21 - i;
    return (Uint8)(255.0f * 0.85f * powf(0.98f, (float)i) + 0.5f);
}

static void pause_scale(float s, float *x, float *y, float *w, float *h) {
    *x -= *w * (s - 1) * 0.5f; *y -= *h * (s - 1) * 0.5f; *w *= s; *h *= s;
}

static void enter_pause(Game *g, bool on) {
    g->paused = on;
    g->pause_press = -1;
    g->pause_lg_until = 0;
    g->pause_kbd = false;
    g->timing_drag = false;
    g->pbtn_down = false;
    mixer_music_pause(on);
}

/* ---------------------------------------------------------------- effects */

static void fx_add(Game *g, int kind, int v) {
    g->fx[g->fx_next] = (Fx){kind, g->t, v};
    g->fx_next = (g->fx_next + 1) % MAX_FX;
}

/* m_StretchRate of EffectFlickResult exec @0x2ea26 and EffectInterlude exec @0x62cd4 after tick n
   (m_TimeCnt): while n < timer - 5 and n <= 17, bit n of 0x3c3c subtracts 0.38 and bit n of 0x3c3c0
   adds 0.38 (cumulative, so it swings 1 -> -0.52 -> 1 -> -0.52 -> 1: a horizontal flip-spin);
   any other tick resets it to 1. */
static float wobble_stretch(int n, int timer) {
    float s = 1.0f;
    for (int i = 1; i <= n; i++) {
        unsigned bit = i < 32 ? 1u << i : 0;
        if (i < timer - 5 && i <= 17 && (bit & 0x3c3cu)) s -= 0.38f;
        else if (i < timer - 5 && i <= 17 && (bit & 0x3c3c0u)) s += 0.38f;
        else s = 1.0f;
    }
    return s;
}

/* -[WindowManager SetUIFade:] / SetInterludeUIFade: on = +0.06, off = -0.02 per tick */
static void set_ui_fade(Game *g, bool on) { g->ui_rate = on ? 0.06f : -0.02f; }
static void set_int_fade(Game *g, bool on) { g->int_rate = on ? 0.06f : -0.02f; }

/* -[WindowManager StartInterludeMode] */
static void start_interlude_mode(Game *g) {
    if (g->interlude_mode) return;
    g->int_hold = 0;
    set_int_fade(g, true);
    set_ui_fade(g, false);
    g->interlude_mode = true;    /* TouchFlickBoard setInterludeMode:1 + setInterludeRect */
    if (getenv("MF_DEBUG")) log_msg("interlude start t=%.3f", g->t);
    g->int_panel = true;
}

/* -[WindowManager EndInterludeMode]: hold the panel for delay_ms/30 ticks, then fade it out;
   the touch rect goes back to the ten keys at once */
static void end_interlude_mode(Game *g) {
    g->int_hold = g->delay_ms / 30;
    set_int_fade(g, false);
    g->interlude_mode = false;
    if (getenv("MF_DEBUG")) log_msg("interlude end t=%.3f", g->t);
}

/* WindowManager exec @0x3e21c, UI part (one 30 Hz tick) */
static void ui_step(Game *g) {
    if (g->ui_rate != 0.0f) {
        g->ui_alpha += g->ui_rate;
        if (g->ui_alpha > 1.0f) { g->ui_alpha = 1.0f; g->ui_rate = 0.0f; }
        else if (g->ui_alpha < 0.0f) { g->ui_alpha = 0.0f; g->ui_rate = 0.0f; }
    }
    if (g->int_hold >= 1) { g->int_hold--; return; }
    if (g->int_rate != 0.0f) {
        g->int_alpha += g->int_rate;
        if (g->int_alpha > 1.0f) { g->int_alpha = 1.0f; g->int_rate = 0.0f; }
        else if (g->int_alpha < 0.0f) {
            g->int_alpha = 0.0f;
            g->int_rate = 0.0f;
            g->int_panel = false;                          /* setInterludeMode:0 */
            if (g->next_cue < g->cues.n - 10) set_ui_fade(g, true); /* keys back unless in the last 10 cues */
        }
    }
}

/* -[WindowGameWindow StartSpScoreEffect:] */
static void sp_start(Game *g, int score) {
    g->sp_on = true;
    g->sp_cnt = 0;
    g->sp_score = score;
    g->sp_y = 75.0f;
    g->sp_alpha = 1.0f;
}

/* WindowGameWindow exec @0x39b3c (one 30 Hz tick; runs while paused too) */
static void window_step(Game *g) {
    /* the fade-in starts with the first movie frame */
    if (g->fade > 0.0f && (!g->video || video_texture(g->video))) {
        g->fade -= 0.03f;
        if (g->fade < 0.0f) g->fade = 0.0f;
    }
    g->rail_pos = (g->rail_pos + 32) % 640;
    g->rot_a = (g->rot_a + 12) % 360;
    g->rot_b = (g->rot_b + 13) % 360;
    g->rot_star = (g->rot_star + 17) % 360;
    if (g->sp_on) {
        g->sp_cnt++;
        float y = 75.0f - 25.0f * sinf((float)g->sp_cnt / 42.0f * 1.57f);
        g->sp_y = y < 50.0f ? 50.0f : y;
        if (g->sp_cnt >= 43) g->sp_on = false;
        else if (g->sp_cnt >= 28) { g->sp_alpha -= 1.0f / 15.0f; if (g->sp_alpha < 0.0f) g->sp_alpha = 0.0f; }
    }
}

/* port: the original steps once per 30 fps frame; here whole 30 Hz ticks of wall time */
static int clock_steps(Game *g) {
    Uint32 now = SDL_GetTicks();
    g->clock_ms += (double)(Uint32)(now - g->clock_last);
    g->clock_last = now;
    int n = (int)(g->clock_ms / (1000.0 / 30.0));
    g->clock_ms -= n * (1000.0 / 30.0);
    return n > 8 ? 8 : n;
}

/* ---------------------------------------------------------------- status */

static void set_music_volume(Game *g) {
    /* NoteManager AddTensionGauge: main (vocal) volume = min(1, gauge/128 + 0.35) * BGM */
    float v = g->gauge * (1.0f / 128.0f) + 0.35f;
    if (v > 1.0f) v = 1.0f;
    /* PV view: no tension; KARAOKE (GetPVModeFlg:4) sets the main/vocal volume to 0 */
    if (g->opt->pv_mode) v = g->opt->pv_karaoke ? 0.0f : 1.0f;
    mixer_music_gain(v * g->opt->bgm_volume, g->opt->bgm_volume);
}

/* +[NoteManager AddTensionGauge:] @0xe040 */
static void add_tension(Game *g, int grade) {
    float total = (float)g->total_notes;
    if (total < 1) total = 1;
    float step = 128.0f / (total + total) + 0.01f;
    float v = g->gauge + step * (float)g->gd->tension[grade];
    g->gauge = v < 256.0f ? v : 256.0f;
    set_music_volume(g);
    int good = g->counts[3] + g->counts[4] + g->counts[5];
    int remaining = g->total_notes - (good + g->counts[2] + g->counts[1]);
    float ratio = (float)(good + remaining) / total;
    if (!(ratio < 0.5f) && g->gauge > 0.0f) return;
    g->gauge = 0.0f;
    g->game_over = true;           /* StatusData SetGameOver:1 + NextScene:8 */
}

/* ---------------------------------------------------------------- notes */

static Note *note_new(Game *g) {
    if (g->nnotes == g->cap) {
        int nc = g->cap ? g->cap * 2 : 256;
        Note *nn = realloc(g->notes, (size_t)nc * sizeof *nn);
        if (!nn) return NULL;
        g->notes = nn;
        g->cap = nc;
    }
    Note *n = &g->notes[g->nnotes++];
    memset(n, 0, sizeof *n);
    return n;
}

/* NoteNormal / NoteInterlude initWithType::: (shared timing part) */
static void note_timing(Game *g, Note *n, float speed_px) {
    float T = center_time(g->bpm);
    n->spdx = -speed_px / T;
    int just = (int)(T * 30.0f);
    float k = 39.0f / (fabsf(n->spdx) / (float)just);
    just += (int)(k / 10.0f * (float)g->opt->input_timing);
    n->just = just;
    float delay = floorf((float)g->delay_ms / 1000.0f * 30.0f);
    n->base = g->play_cnt + (int)(delay - T * 30.0f);
    n->x = SPAWN_X;
}

/* +[NoteManager AddNote::::] @0xdc3c */
static Note *add_note(Game *g, int type, int ch, bool enabled) {
    Note *n = note_new(g);
    if (!n) return NULL;
    n->type = type;
    n->ch = ch;
    n->active = n->playable = enabled;
    if (type == NOTE_NORMAL) {
        n->board = g->gd->kana[ch].key;
        n->flick = g->gd->kana[ch].dir;
        note_timing(g, n, 642.0f);
    } else {
        note_timing(g, n, 688.0f);
    }
    return n;
}

/* NoteManager GetLastCrimaxEnableNote: newest playable normal note */
static Note *last_crimax_note(Game *g) {
    for (int i = g->nnotes - 1; i >= 0; i--)
        if (g->notes[i].type == NOTE_NORMAL && g->notes[i].playable) return &g->notes[i];
    return NULL;
}

/* ---------------------------------------------------------------- cues */

/* CuePointCallback(NSString*) -> s_tblCuePointFunc[op] */
static void fire_cue(Game *g, const char *p) {
    if (!p[0]) return;
    int op = p[0] - '0';
    if (g->opt->pv_mode) {
        /* ScenePVView: CuePointFunc_Lyrics (op 3) only acts in scene 4 with the LYRICS flag;
           note cues (0/4/5/6) only act in the game scene. FadeOut (op 2) ends the section. */
        if (op == 3 && g->opt->pv_lyrics) snprintf(g->lyric, sizeof g->lyric, "%s", p + 1);
        else if (op == 2) g->lyric[0] = 0;
        else if (op == 7) g->delay_ms = atoi(p + 1);
        else if (op == 8) g->bpm = atoi(p + 1);
        return;
    }
    switch (op) {
    case 0: { /* Telop: s_tblTelopType[digit(diff+2)] -> Disable/Enable */
        int ch = kana_index(p + 1, NULL);
        int d = digit_at(p, g->diff + 2);
        if (ch < 0 || d < 0) break;
        add_note(g, NOTE_NORMAL, ch, d != 0);
        break;
    }
    case 1: set_ui_fade(g, true); break;    /* FadeIn: WindowManager SetUIFade:1 */
    case 2: set_ui_fade(g, false); break;   /* FadeOut: WindowManager SetUIFade:0 */
    case 3: break;                          /* Lyrics: only shown in PV mode (scene 4) */
    case 4: { /* SmallTelop: disabled note carrying the small kana */
        int ch = kana_index(p + 1, NULL);
        if (ch < 0) break;
        Note *n = add_note(g, NOTE_NORMAL, ch, false);
        if (n) n->small = true;
        break;
    }
    case 5: { /* Interlude: s_tblInterludeType[digit(diff+1)] */
        int d = digit_at(p, g->diff + 1);
        if (d == 1) {
            start_interlude_mode(g);        /* WindowManager StartInterludeMode */
            add_note(g, NOTE_INTERLUDE, 0, true);
        } else if (d == 2) {
            end_interlude_mode(g);          /* EndInterludeMode */
        }
        break;
    }
    case 6: { /* Crimax */
        int d = digit_at(p, g->diff + 1);
        if (d > 0) {
            Note *n = last_crimax_note(g);
            if (n) n->crimax = true;
        }
        break;
    }
    case 7: g->delay_ms = atoi(p + 1); break; /* SetDelay */
    case 8: g->bpm = atoi(p + 1); break;      /* SetBPM */
    default: break;
    }
}

/* MikuFlickCriManager CheckTotalNoteNum: playable notes / interlude notes for this difficulty */
/* the same pass's per-key x direction totals (e_FlickBoardTypeTbl / e_FlickBoardResultTbl of the
   note's kana -> MusicData AddFlickResultNum); successes are counted in check_normal */
static void count_flick_totals(Game *g) {
    memset(g->flick_total, 0, sizeof g->flick_total);
    memset(g->flick_success, 0, sizeof g->flick_success);
    for (int i = 0; i < g->cues.n; i++) {
        const char *p = g->cues.cues[i].param;
        int ch = p[0] == '0' && digit_at(p, g->diff + 2) > 0 ? kana_index(p + 1, NULL) : -1;
        if (ch < 0) continue;
        int k = g->gd->kana[ch].key, d = g->gd->kana[ch].dir;
        if (k >= 0 && k < NUM_KEYS && d >= 0 && d < NUM_FLICK_DIRS) g->flick_total[k][d]++;
    }
}

static void count_totals(Game *g) {
    g->total_notes = g->total_interlude = 0;
    for (int i = 0; i < g->cues.n; i++) {
        const char *p = g->cues.cues[i].param;
        if (p[0] == '0' && digit_at(p, g->diff + 2) > 0) g->total_notes++;
        if (p[0] == '5' && digit_at(p, g->diff + 1) == 1) g->total_interlude++;
    }
    count_flick_totals(g);
}

/* ---------------------------------------------------------------- judgement */

/* tick distance -> grade via s_tblNoteNormal_Front/Back; -1 when outside */
static int tick_grade(const GameData *gd, int d) {
    if (d >= 0) return d < 11 ? gd->front[d] : -1;
    return (d >= -11) ? gd->back[-d] : -1;
}

static void play(Game *g, int id) { play_sound(g->as, id); }

/* -[NoteInterlude checkResult::::] @0x2ffdc */
static int check_interlude(Game *g, Note *n) {
    int dd = n->just - n->down, du = n->just - n->up;
    int gu = tick_grade(g->gd, du);
    if (gu < 0) gu = 0;
    int grade = 0;
    if (dd < -11) {
        grade = 0;
    } else if (dd > 10) {
        grade = gu < 4 ? gu : 3;
    } else {
        int gd = tick_grade(g->gd, dd);
        if (gd == 0) grade = (dd > 0) ? (gu < 4 ? gu : 3) : 0;
        else if (gu != 0) {
            grade = gd <= gu ? gd : gu;
            if (!(grade > 2 || dd < 1)) grade = 3;
        } else grade = 0;
    }
    if (grade == 0) return 0;
    n->active = false;
    n->grade = grade;
    if (grade > 3) {
        play(g, SND_INTERLUDE);
        g->interlude_cnt++;
        g->stage_score += 1;
        if (g->interlude_cnt >= g->total_interlude) {
            g->stage_score += 39;
            play(g, SND_HIT);            /* SoundManager Play:1 @0x30216 */
            sp_start(g, 39);             /* StartSpScoreEffect:39 @0x30248 */
        }
        fx_add(g, FX_INTERLUDE, 0);      /* AddEffect2D type 7 (EffectInterlude) @0x30268, every success */
    }
    return grade;
}

/* -[NoteNormal checkResult::::] @0x22e10 */
static int check_normal(Game *g, Note *n, int board, int dir) {
    int grade;
    bool wrong;
    if (n->board != board) {
        wrong = true;
        grade = 2;
    } else {
        wrong = n->flick != dir;
        int cap = wrong ? 3 : 5;
        int du = n->just - n->up, dd = n->just - n->down;
        int gu = tick_grade(g->gd, du);
        if (gu < 0) gu = 0;
        int gdn = tick_grade(g->gd, dd);
        if (gdn >= 0) {
            if (gdn != 0) {
                grade = 0;
                if (gu != 0) {
                    grade = gu < gdn ? gu : gdn;
                    if (grade < 3 && dd > 0) grade = 3;
                }
            } else grade = (dd < 1) ? 0 : (gu > 3 ? 3 : gu);
        } else if (dd < 0) {
            grade = 0;
        } else {
            grade = gu > 3 ? 3 : gu;
        }
        if (grade > cap) grade = cap;
        if (grade == 0) return 0; /* outside every window: the press is ignored */
    }

    if (getenv("MF_DEBUG"))
        log_msg("judge t=%.3f pc=%d note(ch=%d key=%d dir=%d just=%d cnt=%d down=%d up=%d) input(key=%d dir=%d) -> %d%s",
                g->t, g->play_cnt, n->ch, n->board, n->flick, n->just, n->cnt, n->down, n->up, board, dir, grade,
                wrong ? " wrong" : "");
    if (g->diff == DIFF_BREAK) {
        if (grade == 2) grade = 0;
    } else {
        add_tension(g, grade);
    }
    n->active = false;
    n->grade = grade;
    g->counts[grade]++;
    /* StatusData AddTmpFlickSuccessNum (@0x232a8, grade >= 4: the right key and direction) */
    if (grade >= 4 && n->board >= 0 && n->board < NUM_KEYS && n->flick >= 0 && n->flick < NUM_FLICK_DIRS)
        g->flick_success[n->board][n->flick]++;

    bool sp = false;
    int bonus = 0;
    if (grade < 4 || wrong) {
        if (g->diff != DIFF_BREAK) g->combo = 0;
    } else {
        g->combo++;
        bonus = ((g->combo + 5) / 10) * 50 & 0xffff;
        if (bonus > 500) bonus = 500;
        g->combo_score += bonus;
        if (g->combo > g->max_combo) g->max_combo = g->combo;
        if (n->crimax && grade > 4 && g->combo >= 100) {
            g->stage_score += 200;
            sp = true;
            sp_start(g, g->gd->addscore[grade] + bonus + 200);
        }
    }
    g->stage_score += g->gd->addscore[grade + (wrong ? 6 : 0)];

    switch (grade) {
    case 0: return 0;
    case 2: if (g->opt->fail_sound) play(g, SND_SAD); break;
    case 3: play(g, SND_SAFE); break;
    case 4: play(g, SND_HIT); break;
    case 5: play(g, sp ? SND_COOL_SP : SND_HIT); break;
    default: break;
    }
    g->last_grade = grade;
    g->last_grade_t = g->t;
    fx_add(g, FX_RESULT, grade);
    return grade;
}

/* +[NoteManager checkResult:::]: oldest active note inside its judge frame */
static int judge(Game *g, int board, int dir) {
    for (int i = 0; i < g->nnotes; i++) {
        Note *n = &g->notes[i];
        if (n->dead || !n->active || !n->inside) continue;
        return n->type == NOTE_INTERLUDE ? check_interlude(g, n) : check_normal(g, n, board, dir);
    }
    return 0;
}

/* NoteManager setTouchDown/setTouchUp broadcast */
static void touch_down_all(Game *g) { for (int i = 0; i < g->nnotes; i++) g->notes[i].down = g->notes[i].cnt + g->evt_dcnt; }
static void touch_up_all(Game *g) { for (int i = 0; i < g->nnotes; i++) g->notes[i].up = g->notes[i].cnt + g->evt_dcnt; }

/* Inputs are judged at the moment they happened rather than at the last game_update
   (notes' cnt is refreshed once per frame; events arrive in between). */
static void stamp_event(Game *g, const SDL_Event *e) {
    double t = mixer_music_time();
    Uint32 now = SDL_GetTicks(), ts = e->common.timestamp;
    if (ts && now >= ts && now - ts < 200) t -= (now - ts) / 1000.0;
    int d = (int)ceil(t * 30.0) - g->play_cnt;
    g->evt_dcnt = d < -2 ? -2 : d > 2 ? 2 : d;
}

/* ---------------------------------------------------------------- exec */

/* -[NoteNormal exec] @0x22160 / -[NoteInterlude exec] @0x2fbf0 */
static void exec_notes(Game *g) {
    for (int i = 0; i < g->nnotes; i++) {
        Note *n = &g->notes[i];
        if (n->dead) continue;
        if (!n->started) {
            if (g->play_cnt < n->base) continue;
            n->started = true;
        }
        n->cnt = g->play_cnt - n->base;
        n->x = SPAWN_X + n->spdx * (float)n->cnt / 30.0f;
        n->inside = false;
        if (n->active && n->cnt >= n->just - 11) {
            int end = n->just + 12 + (n->type == NOTE_NORMAL ? g->gd->endofs[g->diff] : 0);
            if (n->cnt < end) {
                n->inside = true;
            } else {
                n->active = false;
                if (n->type == NOTE_NORMAL && g->diff != DIFF_BREAK) {
                    n->grade = 1;
                    g->counts[1]++;
                    add_tension(g, 1);
                    g->combo = 0;
                    g->last_grade = 1;
                    g->last_grade_t = g->t;
                    fx_add(g, FX_RESULT, 1);
                }
            }
        }
        if (n->x <= -200.0f) n->dead = true;
    }
    /* compact dead notes occasionally */
    int w = 0;
    for (int i = 0; i < g->nnotes; i++)
        if (!g->notes[i].dead) g->notes[w++] = g->notes[i];
    g->nnotes = w;
}

/* debug autoplay: perfect input at the just frame */
static void autoplay(Game *g) {
    for (int i = 0; i < g->nnotes; i++) {
        Note *n = &g->notes[i];
        if (!n->active || !n->started || n->cnt < n->just) continue;
        n->down = n->up = n->just;
        if (n->type == NOTE_INTERLUDE) check_interlude(g, n);
        else check_normal(g, n, n->board, n->flick);
        fx_add(g, FX_PULSE, 0);   /* port: the board release this stands for adds the EffectPulse */
    }
}

/* ---------------------------------------------------------------- input */

void game_event(Game *g, const SDL_Event *e);

/* debug: play through the real input path with synthetic events,
   alternating keyboard (board key + arrow) and mouse (press + drag) */
static void autoplay_input(Game *g) {
    static int alt = 0;
    static const SDL_Keycode keys[NUM_KEYS] = {SDLK_q, SDLK_w, SDLK_e, SDLK_a, SDLK_s, SDLK_d, SDLK_z, SDLK_x, SDLK_c, SDLK_SPACE};
    static const SDL_Keycode arrows[5] = {0, SDLK_UP, SDLK_RIGHT, SDLK_DOWN, SDLK_LEFT};
    static const float dxy[5][2] = {{0, 0}, {0, -60}, {60, 0}, {0, 60}, {-60, 0}};
    for (int i = 0; i < g->nnotes; i++) {
        Note *n = &g->notes[i];
        if (!n->active || !n->started || !n->inside) continue;
        if (n->cnt < n->just) return; /* oldest pending note not due yet */
        SDL_Event e;
        if (n->type == NOTE_INTERLUDE || (alt++ & 1) == 0) {
            int key = n->type == NOTE_INTERLUDE ? 4 : n->board;
            memset(&e, 0, sizeof e);
            e.type = SDL_KEYDOWN; e.key.keysym.sym = keys[key];
            game_event(g, &e);
            if (n->type == NOTE_NORMAL && n->flick) {
                e.key.keysym.sym = arrows[n->flick];
                game_event(g, &e);
            }
            memset(&e, 0, sizeof e);
            e.type = SDL_KEYUP; e.key.keysym.sym = keys[key];
            game_event(g, &e);
        } else {
            float x = g->gd->board[n->board].px + KEY_W / 2.0f, y = g->gd->board[n->board].py + KEY_H / 2.0f;
            memset(&e, 0, sizeof e);
            e.type = SDL_MOUSEBUTTONDOWN; e.button.button = SDL_BUTTON_LEFT; e.button.x = (int)x; e.button.y = (int)y;
            game_event(g, &e);
            memset(&e, 0, sizeof e);
            e.type = SDL_MOUSEMOTION; e.motion.x = (int)(x + dxy[n->flick][0]); e.motion.y = (int)(y + dxy[n->flick][1]);
            game_event(g, &e);
            memset(&e, 0, sizeof e);
            e.type = SDL_MOUSEBUTTONUP; e.button.button = SDL_BUTTON_LEFT;
            e.button.x = (int)(x + dxy[n->flick][0]); e.button.y = (int)(y + dxy[n->flick][1]);
            game_event(g, &e);
        }
        return; /* one note per frame, like a human */
    }
}

static int key_at(Game *g, float x, float y) {
    /* e_FlickBoardPictTbl positions are TOP-LEFT corners (UIKit rect origins), as the
       board is drawn: あ covers x 129..256, y 530..638 and the board spans 129..510 x 530..962. */
    /* -[TouchFlickBoard setInterludeRect:1] @0x6ff50 (board 0 only): collision rect = the
       flickguide_int_on frame (409x350 px) at ((640 - 409) / 2, 530) = x 115.5..524.5, y 530..880. Boards
       1-9 keep their own rects, so the part of わ below y 880 stays live (any board judges an interlude). */
    if (g->interlude_mode && x >= 115.5f && x < 524.5f && y >= 530 && y < 880) return 0;
    for (int k = 0; k < NUM_KEYS; k++) {
        const BoardKey *b = &g->gd->board[k];
        if (x >= b->px && x < b->px + KEY_W && y >= b->py && y < b->py + KEY_H) return k;
    }
    return -1;
}

/* -[TouchFlickBoard checkFlick]: < 16 points -> tap, else 90-degree sectors */
static int flick_dir(const Pointer *p) {
    float dx = (p->x1 - p->x0) * 0.5f, dy = (p->y1 - p->y0) * 0.5f; /* logical px -> UIKit points */
    if (dx * dx + dy * dy < 256.0f) return 0;
    if (fabsf(dx) > fabsf(dy)) return dx > 0 ? 2 : 4;
    return dy < 0 ? 1 : 3;
}

static Pointer *ptr_find(Game *g, int kind, SDL_FingerID fid) {
    for (int i = 0; i < MAX_POINTERS; i++)
        if (g->ptr[i].down && g->ptr[i].kind == kind && (kind != PTR_FINGER || g->ptr[i].fid == fid)) return &g->ptr[i];
    return NULL;
}

static void press(Game *g, int kind, SDL_FingerID fid, float x, float y, int key) {
    if (key < 0) return;
    Pointer *p = NULL;
    for (int i = 0; i < MAX_POINTERS; i++) if (!g->ptr[i].down) { p = &g->ptr[i]; break; }
    if (!p) return;
    *p = (Pointer){true, kind, fid, key, x, y, x, y, 0, 0};
    g->key_down_ms[key] = SDL_GetTicks() | 1;   /* port: minimum visible press */
    g->key_up_dir[key] = 0;
    touch_down_all(g); /* TouchFlickBoard touchDown -> NoteManager setTouchDown */
}

static void move(Game *g, Pointer *p, float x, float y) {
    p->x1 = x;
    p->y1 = y;
    touch_up_all(g);   /* touchMoved -> NoteManager setTouchUp + checkFlick */
}

static void release(Game *g, Pointer *p, int dir) {
    p->down = false;
    g->key_up_dir[p->key] = dir;   /* port: the flower of a short press shows its final direction */
    touch_up_all(g);
    judge(g, p->key, dir);
    /* -[TouchFlickBoard touchUp:] @0x6fec6: AddEffect2D type 3 (EffectPulse) after checkResult on
       every board release, hit or not, interlude panel included */
    fx_add(g, FX_PULSE, 0);
}

static int board_key_for(SDL_Keycode k) {
    switch (k) {
    case SDLK_KP_7: case SDLK_q: return 0;
    case SDLK_KP_8: case SDLK_w: return 1;
    case SDLK_KP_9: case SDLK_e: return 2;
    case SDLK_KP_4: case SDLK_a: return 3;
    case SDLK_KP_5: case SDLK_s: return 4;
    case SDLK_KP_6: case SDLK_d: return 5;
    case SDLK_KP_1: case SDLK_z: return 6;
    case SDLK_KP_2: case SDLK_x: return 7;
    case SDLK_KP_3: case SDLK_c: return 8;
    case SDLK_KP_0: case SDLK_SPACE: return 9;
    default: return -1;
    }
}

static int arrow_dir(SDL_Keycode k) {
    switch (k) {
    case SDLK_UP: return 1;
    case SDLK_RIGHT: return 2;
    case SDLK_DOWN: return 3;
    case SDLK_LEFT: return 4;
    default: return 0;
    }
}

static int held_arrow(void) {
    const Uint8 *ks = SDL_GetKeyboardState(NULL);
    if (ks[SDL_SCANCODE_UP]) return 1;
    if (ks[SDL_SCANCODE_RIGHT]) return 2;
    if (ks[SDL_SCANCODE_DOWN]) return 3;
    if (ks[SDL_SCANCODE_LEFT]) return 4;
    return 0;
}

/* WindowGameWindow pauseGame @0x39138: TouchBack(RESUME) y=574, TouchRetry(RETRY) y=656,
   TouchRetry No2 (MUSIC SELECT) y=740; menubutton_m at x=164 */
static const float k_pause_y[3] = {574, 656, 740};
/* TouchInputTiming (WindowGameWindow init @0x39528, type 0x3d in m_PauseTouch, shown with the pause
   menu): m_Pos (113, 895), touch box 440 wide from y 895. touchMoved @0x7da9c: once the pointer is more
   than 10 UIKit points (20 px) from the last step point, one step (right = -1, left = +1, clamped to -10..+10) and the
   step point moves to the pointer; a tap alone changes nothing. Stored per song (MusicData). */
static bool timing_hit(float mx, float my) { return mx >= 113 && mx < 553 && my >= 885; }

static void timing_step(Game *g, int d) {
    int t = g->opt->input_timing + d;
    g->opt->input_timing = t < -10 ? -10 : t > 10 ? 10 : t;
}

static int pause_hit(float mx, float my) {
    for (int i = 0; i < 3; i++)
        if (mx >= 164 && mx < 164 + 324 && my >= k_pause_y[i] && my < k_pause_y[i] + 82) return i;
    return -1;
}

/* chosen on touch up: TouchBack (RESUME) plays SE 8, TouchRetry (RETRY / MUSIC SELECT) SE 19 */
static void pause_choose(Game *g, int i) {
    play_sound(g->as, i == 0 ? SE_BACK : SE_BUTTON);
    if (i == 0) { g->paused = false; mixer_music_pause(false); }
    else if (i == 1) { g->retry = true; g->finished = true; }
    else { g->quit = true; g->finished = true; }
}

SDL_Texture *game_backdrop(const Game *g) { return g ? video_texture(g->video) : NULL; }

void game_pause(Game *g) {
    if (!g || !g->loaded || g->finished || g->paused) return;   /* PV view too: -[WindowPVView SetInit:] @0x47498 */
    enter_pause(g, true);
}

/* TouchPause: 80x80 box at (570,890), left-handed (19,890); pausebutton (51x51) drawn 5/9 px inside */
static float pbtn_x(const Game *g) { return g->opt->left_hand ? 19.0f : 570.0f; }
static bool pbtn_hit_g(const Game *g, float x, float y) {
    float bx = pbtn_x(g);
    return x >= bx && x < bx + 80 && y >= 890 && y < 970;
}

/* -[TouchPause touchUp:] @0x72910: on release over the button, once WindowManager GetFadeAlpha <= 0 and
   while CriManager GetMovieFrame < GetMovieTotalFrame: TouchCallback (pause) and SoundManager Play:19.
   port: "during fades" uses m_Fade, and the movie-end test is the last 2 s of the song (per the audit). */
static void pbtn_release(Game *g, float x, float y) {
    g->pbtn_down = false;
    if (!pbtn_hit_g(g, x, y) || g->fade > 0.0f || g->finished || g->paused) return;
    if (g->t >= mixer_music_length() - 2.0) return;
    play_sound(g->as, SE_BUTTON);
    enter_pause(g, true);
}

void game_event(Game *g, const SDL_Event *e) {
    if (!g->loaded || g->finished) return;
    g->evt_dcnt = 0;
    if (!g->paused && !g->opt->pv_mode &&
        (e->type == SDL_KEYDOWN || e->type == SDL_KEYUP || e->type == SDL_MOUSEBUTTONDOWN || e->type == SDL_MOUSEBUTTONUP ||
         e->type == SDL_MOUSEMOTION || e->type == SDL_FINGERDOWN || e->type == SDL_FINGERUP || e->type == SDL_FINGERMOTION))
        stamp_event(g, e);
    if (g->opt->pv_mode) {   /* PV view: WindowPVView swipes, pause button and menu (pvview.c) */
        int a = pvview_event(g->as, g->opt, e, g->paused, g->t, g->t >= mixer_music_length() - 2.0);
        if (a == PVV_PAUSE || a == PVV_RESUME) enter_pause(g, a == PVV_PAUSE);
        else if (a == PVV_RELOAD || a == PVV_LEAVE) { g->quit = a == PVV_LEAVE; g->finished = true; }
        return;
    }
    if (e->type == SDL_KEYDOWN && !e->key.repeat) {
        SDL_Keycode k = e->key.keysym.sym;
        if (k == SDLK_ESCAPE || k == SDLK_p) {
            /* port: keyboard toggle; sounds as the pause button (SE 19) and RESUME (SE 8) */
            play_sound(g->as, g->paused ? SE_BACK : SE_BUTTON);
            enter_pause(g, !g->paused);
            return;
        }
        if (g->paused) { /* pause menu: RESUME / RETRY / MUSIC SELECT */
            if (k == SDLK_UP || k == SDLK_DOWN) g->pause_kbd = true;
            if (k == SDLK_UP) g->pause_focus = (g->pause_focus + 2) % 3;
            if (k == SDLK_DOWN) g->pause_focus = (g->pause_focus + 1) % 3;
            if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE) pause_choose(g, g->pause_focus);
            if (k == SDLK_LEFT) timing_step(g, 1);     /* port: the keyboard moves the timing cursor too */
            if (k == SDLK_RIGHT) timing_step(g, -1);
            return;
        }
        int bk = board_key_for(k);
        if (bk >= 0) {
            Pointer *p = ptr_find(g, PTR_KEYBOARD, 0);
            if (p) release(g, p, p->kb_dir ? p->kb_dir : held_arrow());
            press(g, PTR_KEYBOARD, 0, 0, 0, g->interlude_mode ? 0 : bk);
            p = ptr_find(g, PTR_KEYBOARD, 0);
            if (p) p->kb_key = k;
            return;
        }
        int ad = arrow_dir(k);
        if (ad) {
            Pointer *p = ptr_find(g, PTR_KEYBOARD, 0);
            if (p) { p->kb_dir = ad; touch_up_all(g); }
        }
        return;
    }
    if (g->paused) {   /* TouchBack / TouchRetry: pressed on down, chosen on release over the same button */
        if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT &&
            timing_hit((float)e->button.x, (float)e->button.y)) {
            g->timing_drag = true;
            g->timing_x = (float)e->button.x;
            g->pause_kbd = false;
            return;
        }
        if (e->type == SDL_MOUSEMOTION && g->timing_drag) {
            float dx = (float)e->motion.x - g->timing_x;
            if (fabsf(dx) > 20.0f) { timing_step(g, dx > 0 ? -1 : 1); g->timing_x = (float)e->motion.x; }   /* 10 points @0x7dab0 */
            return;
        }
        if (e->type == SDL_MOUSEBUTTONUP && e->button.button == SDL_BUTTON_LEFT && g->timing_drag) {
            g->timing_drag = false;
            return;
        }
        if ((e->type == SDL_MOUSEBUTTONDOWN || e->type == SDL_MOUSEBUTTONUP) && e->button.button == SDL_BUTTON_LEFT) {
            float mx = (float)e->button.x, my = (float)e->button.y;
            int hit = pause_hit(mx, my);
            if (e->type == SDL_MOUSEBUTTONDOWN) {
                g->pause_press = hit; g->pause_kbd = false;
                g->pause_dn_ms = SDL_GetTicks(); g->pause_lg_until = 0;
            } else {
                int was = g->pause_press;
                if (was >= 0 && SDL_GetTicks() - g->pause_dn_ms < MIN_PRESS_MS) {   /* port: quick click */
                    g->pause_lg = was;
                    g->pause_lg_until = g->pause_dn_ms + MIN_PRESS_MS;
                }
                g->pause_press = -1;
                if (hit >= 0 && hit == was) pause_choose(g, hit);
            }
        }
        return;
    }
    if (e->type == SDL_KEYUP) {
        Pointer *p = ptr_find(g, PTR_KEYBOARD, 0);
        if (p && e->key.keysym.sym == p->kb_key) release(g, p, p->kb_dir ? p->kb_dir : held_arrow());
        return;
    }
    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT && e->button.which != SDL_TOUCH_MOUSEID) {
        float x = (float)e->button.x, y = (float)e->button.y;
        if (pbtn_hit_g(g, x, y)) { g->pbtn_down = true; g->pbtn_kind = PTR_MOUSE; g->pbtn_ms = SDL_GetTicks() | 1; return; } /* TouchPause touchDown */
        press(g, PTR_MOUSE, 0, x, y, key_at(g, x, y));
    } else if (e->type == SDL_MOUSEMOTION && e->motion.which != SDL_TOUCH_MOUSEID) {
        Pointer *p = ptr_find(g, PTR_MOUSE, 0);
        if (p) move(g, p, (float)e->motion.x, (float)e->motion.y);
    } else if (e->type == SDL_MOUSEBUTTONUP && e->button.button == SDL_BUTTON_LEFT && e->button.which != SDL_TOUCH_MOUSEID) {
        if (g->pbtn_down && g->pbtn_kind == PTR_MOUSE) { pbtn_release(g, (float)e->button.x, (float)e->button.y); return; }
        Pointer *p = ptr_find(g, PTR_MOUSE, 0);
        if (p) { p->x1 = (float)e->button.x; p->y1 = (float)e->button.y; release(g, p, flick_dir(p)); }
    } else if (e->type == SDL_FINGERDOWN || e->type == SDL_FINGERMOTION || e->type == SDL_FINGERUP) {
        /* port: macOS trackpads report fingers as SDL_TOUCH_DEVICE_INDIRECT_RELATIVE touches whose x/y are
           trackpad positions, not screen positions: they would press phantom keys next to the mouse's own */
        if (SDL_GetTouchDeviceType(e->tfinger.touchId) == SDL_TOUCH_DEVICE_INDIRECT_RELATIVE) return;
        int ww, wh;
        SDL_GetWindowSize(SDL_RenderGetWindow(g->ren), &ww, &wh);
        float lx, ly;
        view_window_to_logical(g->ren, e->tfinger.x * ww, e->tfinger.y * wh, &lx, &ly);
        if (e->type == SDL_FINGERDOWN) {
            if (pbtn_hit_g(g, lx, ly) && !g->pbtn_down) {   /* TouchPause touchDown */
                g->pbtn_down = true;
                g->pbtn_kind = PTR_FINGER;
                g->pbtn_fid = e->tfinger.fingerId;
                g->pbtn_ms = SDL_GetTicks() | 1;
                return;
            }
            press(g, PTR_FINGER, e->tfinger.fingerId, lx, ly, key_at(g, lx, ly));
        } else {
            if (g->pbtn_down && g->pbtn_kind == PTR_FINGER && g->pbtn_fid == e->tfinger.fingerId) {
                if (e->type == SDL_FINGERUP) pbtn_release(g, lx, ly);
                return;
            }
            Pointer *p = ptr_find(g, PTR_FINGER, e->tfinger.fingerId);
            if (!p) return;
            if (e->type == SDL_FINGERMOTION) move(g, p, lx, ly);
            else { p->x1 = lx; p->y1 = ly; release(g, p, flick_dir(p)); }
        }
    }
}

/* ---------------------------------------------------------------- lifecycle */

Game *game_create(SDL_Renderer *ren, Assets *as, const GameData *gd, const Options *opt, int song, int diff) {
    Game *g = calloc(1, sizeof *g);
    if (!g) return NULL;
    g->ren = ren;
    g->as = as;
    g->gd = gd;
    /* the caller's Options is a mutable object (main.c); const only in the shared prototype */
    g->opt = (Options *)opt;
    g->song = song;
    g->diff = diff;
    g->gauge = 128.0f;       /* NoteManager Reset */
    g->ui_alpha = 0.0f;      /* initGame: WindowManager SetUIAlpha:0, the FadeIn cue brings the UI in */
    g->fade = 1.0f;          /* WindowGameWindow setFade: start from black */
    g->clock_last = SDL_GetTicks();
    g->pause_press = -1;
    g->bpm = gd->songs[song].bpm;
    g->delay_ms = (int)(center_time(g->bpm) * 1000.0f);
    g->last_grade_t = -10;

    char path[1024];
    snprintf(path, sizeof path, "%s/%s.usm", as->appdir, gd->songs[song].movie);
    if (opt->pv_mode) pvview_begin(path, song, gd->nsongs);   /* WindowPVView init, or a ReLoad in place */
    if (!usm_read_cues(path, &g->cues) || g->cues.n == 0) {
        log_msg("no chart in %s", path);
        free(g);
        return NULL;
    }
    count_totals(g);
    g->ntracks = media_decode_audio(path, g->tracks, 2);
    if (g->ntracks < 1) { log_msg("no audio in %s", path); cuelist_free(&g->cues); free(g); return NULL; }
    g->video = video_open(path, ren);
    log_msg("song %d '%s' diff %d: %d cues, %d notes, %d interlude, %d audio tracks, %.1f s",
            song, gd->songs[song].title, diff, g->cues.n, g->total_notes, g->total_interlude, g->ntracks,
            (double)g->tracks[0].frames / AUDIO_RATE);
    set_music_volume(g);
    mixer_music_start(&g->tracks[0], g->ntracks > 1 ? &g->tracks[1] : NULL);
    g->loaded = true;
    return g;
}

void game_destroy(Game *g) {
    if (!g) return;
    mixer_music_stop();
    video_close(g->video);
    for (int i = 0; i < g->ntracks; i++) pcm_free(&g->tracks[i]);
    cuelist_free(&g->cues);
    free(g->notes);
    free(g);
}

/* testing aid: MF_TEST_HOLD="x,y,dx,dy,t0,t1" (logical px, song seconds) holds a synthetic finger pressed
   at (x,y) from t0, dragged by (dx,dy), released at t1, through the real press/move/release path, so
   --shot-times can capture the pressed key, its flower guide and the interlude panel */
static void test_hold(Game *g) {
    static const char *spec;
    static bool parsed;
    static float hx, hy, hdx, hdy, t0, t1;
    if (!parsed) {
        parsed = true;
        spec = getenv("MF_TEST_HOLD");
        if (spec && sscanf(spec, "%f,%f,%f,%f,%f,%f", &hx, &hy, &hdx, &hdy, &t0, &t1) != 6) spec = NULL;
    }
    if (!spec) return;
    const SDL_FingerID fid = 0x7e57;
    Pointer *p = ptr_find(g, PTR_FINGER, fid);
    if (!p && g->t >= t0 && g->t < t1) {
        press(g, PTR_FINGER, fid, hx, hy, key_at(g, hx, hy));
        p = ptr_find(g, PTR_FINGER, fid);
        if (p) move(g, p, hx + hdx, hy + hdy);
    } else if (p && g->t >= t1) {
        release(g, p, flick_dir(p));
        t0 = t1 = 1e9f;   /* once per run */
    }
}

bool game_update(Game *g) {
    if (g->finished) return false;
    for (int n = clock_steps(g); n > 0; n--) window_step(g);   /* WindowGameWindow exec, even while paused */
    if (g->paused) return true;
    g->evt_dcnt = 0;          /* autoplay presses below happen "now" */
    g->t = mixer_music_time();
    /* CriManager::Exec fires the cue callbacks first (handlers see the previous PlayCnt) */
    while (g->next_cue < g->cues.n && (double)g->cues.cues[g->next_cue].time_ms <= g->t * 1000.0)
        fire_cue(g, g->cues.cues[g->next_cue++].param);
    g->play_cnt = (int)ceil(g->t * 30.0);
    /* WindowManager exec: UI fades, one step per PlayCnt tick (port: at most 8 per frame) */
    if (g->play_cnt - g->ui_tick > 8) g->ui_tick = g->play_cnt - 8;
    while (g->ui_tick < g->play_cnt) { ui_step(g); g->ui_tick++; }
    exec_notes(g);
    if (g->opt->autoplay) autoplay(g);
    else if (g->opt->autoplay_input) autoplay_input(g);
    test_hold(g);
    video_update(g->video, g->t);
    if (g->game_over) { g->finished = true; mixer_music_stop(); return false; }
    if (!mixer_music_playing() && g->t >= mixer_music_length() - 0.05) { g->finished = true; return false; }
    return true;
}

/* ---------------------------------------------------------------- render */

/*
 * Layout below is transcribed from the original render code (positions are
 * TOP-LEFT like Cloud3D::RenderTex2DClip, except RenderTexRot which is centred):
 *   WindowGameWindow Render @0x39db4, NoteNormal render @0x22540,
 *   NoteManager render @0xd5e4, TouchFlickBoard Render/RenderGuide* @0x6f228..,
 *   EffectFlickResult @0x2e914/0x2eb34, EffectPulse @0x2e3d8, EffectInterlude @0x62de4.
 * See docs/re_notes.md §6.
 */

/* note glyph: RenderTex2DClip(tex, pos.x-39, pos.y, 78x78) */
static void draw_kana(Game *g, int ch, bool playable, float x, float y, Uint8 alpha) {
    const KanaInfo *k = &g->gd->kana[ch];
    int sheet = (k->tex >= 6 ? 2 : 0) + (playable ? 1 : 0); /* e_CharPictTbl tex 4/6 (+1 when enabled) */
    SDL_Rect src = {k->x, k->y, 78, 78};
    spr_tl_rect(g->ren, &g->as->kana_tex[sheet], &src, x, y, 78, 78, alpha);
}

/* s_tblKeyGuideTexIdx @0x139460 (guidepanel_off) / _Next @0x139474 (guidepanel_on), indexed by flick
   code (0 tap,1 up,2 right,3 down,4 left); the sprites are ordered tap, up, down, left, right */
static const char *guide_frame(int dir, bool next) {
    static const char *f[2][5] = {
        {"guidepanel_off_00", "guidepanel_off_01", "guidepanel_off_04", "guidepanel_off_02", "guidepanel_off_03"},
        {"guidepanel_on_00", "guidepanel_on_01", "guidepanel_on_04", "guidepanel_on_02", "guidepanel_on_03"}};
    return f[next ? 1 : 0][dir < 0 || dir > 4 ? 0 : dir];
}

/* s_tblRainbow @0x139310: 28 colours (floats / 255) for climax notes */
static const Uint8 k_rainbow[28][3] = {
    {134, 253, 135}, {164, 253, 135}, {194, 253, 135}, {224, 253, 135}, {255, 254, 136}, {255, 244, 121}, {255, 230, 106},
    {255, 218, 91},  {255, 202, 74},  {255, 187, 118}, {255, 172, 152}, {255, 157, 200}, {255, 136, 254}, {235, 144, 254},
    {215, 152, 254}, {190, 160, 254}, {168, 168, 255}, {143, 175, 255}, {118, 182, 255}, {93, 189, 255},  {78, 199, 254},
    {93, 211, 254},  {108, 225, 254}, {123, 239, 254}, {136, 254, 254}, {136, 254, 224}, {136, 254, 194}, {136, 254, 164}};

static double ticks_since(const Game *g, double t0) { return (g->t - t0) * 30.0; }

static Uint8 a255(float a) { return (Uint8)(a <= 0.0f ? 0 : a >= 1.0f ? 255 : a * 255.0f + 0.5f); }

/* Cloud3D RenderTex / RenderTexRot with a colour and blend (1 = alpha, 2 = additive), centred at (cx,cy),
   w x h px. A negative width (m_StretchRate < 0) draws the sprite mirrored, as GL does. */
static void fx_tex(SDL_Renderer *r, const Atlas *a, const char *name, float cx, float cy, float w, float h,
                   float deg, Uint8 cr, Uint8 cg, Uint8 cb, float alpha, int blend) {
    const Frame *f = atlas_find(a, name);
    Uint8 al = a255(alpha);
    if (!f || !a->tex || al == 0) return;
    SDL_RendererFlip fl = SDL_FLIP_NONE;
    if (w < 0) { w = -w; fl = SDL_FLIP_HORIZONTAL; }
    SDL_FRect d = {cx - w * 0.5f, cy - h * 0.5f, w, h};
    SDL_SetTextureColorMod(a->tex, cr, cg, cb);
    SDL_SetTextureAlphaMod(a->tex, al);
    if (blend == 2) SDL_SetTextureBlendMode(a->tex, SDL_BLENDMODE_ADD);
    SDL_RenderCopyExF(r, a->tex, &f->r, &d, deg, NULL, fl);
    if (blend == 2) SDL_SetTextureBlendMode(a->tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureColorMod(a->tex, 255, 255, 255);
}

static void frame_size(const Atlas *a, const char *name, float *w, float *h) {
    const Frame *f = atlas_find(a, name);
    *w = f ? (float)f->r.w : 0.0f;
    *h = f ? (float)f->r.h : 0.0f;
}

/* WindowGameWindow Render: combo whirls (all RenderTexRot, blend 2). Outer A at combo >= 25 (half alpha up
   to 34), outer B at combo >= 45 (half up to 54), both rgb (1,0.75,0) at (70,480); then the inner whirl,
   always, rgb (1,0.63,0) at (68,481), 0.75 alpha up to combo 14. Alphas scale with the UI alpha. */
static void draw_whirls(Game *g) {
    const Atlas *g1 = &g->as->game1;
    float w, h, ua = g->ui_alpha;
    frame_size(g1, "outsidewhirl", &w, &h);
    if (g->combo >= 25)
        fx_tex(g->ren, g1, "outsidewhirl", 70, 480, w, h, (float)(360 - g->rot_b), 255, 191, 0,
               ua * (g->combo <= 34 ? 0.5f : 1.0f), 2);
    if (g->combo >= 45)
        fx_tex(g->ren, g1, "outsidewhirl", 70, 480, w, h, (float)g->rot_a, 255, 191, 0,
               ua * (g->combo <= 54 ? 0.5f : 1.0f), 2);
    frame_size(g1, "insidewhirl", &w, &h);
    fx_tex(g->ren, g1, "insidewhirl", JUDGE_CX, LANE_CY, w, h, (float)(360 - g->rot_star), 255, 161, 0,
           ua * (g->combo <= 14 ? 0.75f : 1.0f), 2);
}

/* -[EffectPulse render] @0x2e3d8, tick n (exec @0x2e25c: m_NormalRate = 1 + 0.92 sin(n/10 * 1.57) + bonus; alpha
   1 - 0.2 (n - 5) over the last 5 ticks; m_Timer 10). ripple (TP 0x11d) 120*rate px centred (70,482), blend 2.
   Outside interlude, at combo >= 15 and n <= 7, the fireworks_mik rings: _01 (combo >= 15, half alpha up to
   24, x1.35 from 75, x1.4 from 95), _00 (combo >= 35, half up to 44, x1.15/1.25/1.35/1.4 from 55/65/75/95),
   _02 (combo >= 85, half up to 94, x1.25, x1.4 from 95), all 120*rate*k px at the same centre. */
static void draw_pulse(Game *g, int n) {
    const Atlas *ef = &g->as->effect1;
    if (n < 1 || n >= 10) return;
    /* + (int)(((combo - 50) / 5) * 0.02), at least 0: nonzero only from combo 300 */
    int bonus = (int)((float)((g->combo - 50) / 5) * 0.02f);
    float rate = 1.0f + 0.92f * sinf((float)n / 10.0f * 1.57f) + (float)(bonus > 0 ? bonus : 0);
    float alpha = n > 5 ? 1.0f - 0.2f * (float)(n - 5) : 1.0f;
    float s = 120.0f * rate;
    fx_tex(g->ren, ef, "ripple", 70, 482, s, s, 0, 255, 255, 255, alpha, 2);
    int c = g->combo;
    if (g->int_panel || c < 15 || n > 7) return;   /* WindowManager IsInterludeMode = s_IsInterlude (int_panel) */
    float k = c >= 95 ? 1.4f : c >= 75 ? 1.35f : 1.0f;
    fx_tex(g->ren, ef, "fireworks_mik_01", 70, 482, s * k, s * k, 0, 255, 255, 255, alpha * (c <= 24 ? 0.5f : 1.0f), 2);
    if (c < 35) return;
    k = c >= 95 ? 1.4f : c >= 75 ? 1.35f : c >= 65 ? 1.25f : c >= 55 ? 1.15f : 1.0f;
    fx_tex(g->ren, ef, "fireworks_mik_00", 70, 482, s * k, s * k, 0, 255, 255, 255, alpha * (c <= 44 ? 0.5f : 1.0f), 2);
    if (c < 85) return;
    k = c >= 95 ? 1.4f : 1.25f;
    fx_tex(g->ren, ef, "fireworks_mik_02", 70, 482, s * k, s * k, 0, 255, 255, 255, alpha * (c <= 94 ? 0.5f : 1.0f), 2);
}

/* WindowGameWindow Render: SpScore popup, 4 digits of score % 10000 (leading zeros skipped) as score_0d
   at x = 508 + 28 i, y = m_SpScorePosY, rgb (1,1,0.66) */
static void draw_spscore(Game *g) {
    if (!g->sp_on) return;
    int v = g->sp_score % 10000, div = 1000;
    bool started = false;
    for (int i = 0; i < 4; i++, div /= 10) {
        int d = (v / div) % 10;
        if (d > 0 || i == 3) started = true;
        if (!started) continue;
        char name[16];
        snprintf(name, sizeof name, "score_%02d", d);
        const Frame *f = atlas_find(&g->as->game1, name);
        if (!f) continue;
        SDL_SetTextureColorMod(g->as->game1.tex, 255, 255, 168);
        spr_tl(g->ren, &g->as->game1, name, 508.0f + 28.0f * (float)i, g->sp_y, a255(g->sp_alpha));
        SDL_SetTextureColorMod(g->as->game1.tex, 255, 255, 255);
    }
}

/* -[WindowGameWindow Render] @0x39ea0-0x3a034: score = TmpStageScore + TmpComboScore, 7 digits
   ((v % 10^(7-i)) / 10^(6-i)) as score_0d (TP 0x167+d) at s_tblScorePosX[i], y 8, z 4.5+0.1i, colour
   (1,1,1,1), blend 1; a digit is drawn only once a non-zero digit has been seen (no forced units digit,
   so a score of 0 draws nothing); score_10 comma (TP 0x171) at x+27 after digit 0 and 3 */
static void draw_score(Game *g, int value) {
    char name[32];
    int div = 1000000;
    bool started = false;
    for (int i = 0; i < 7; i++, div /= 10) {
        int d = (value / div) % 10;
        if (d > 0) started = true;
        if (!started) continue;
        snprintf(name, sizeof name, "score_%02d", d);
        spr_tl(g->ren, &g->as->game1, name, (float)g->gd->score_posx[i], 8, 255);
        if ((i == 0 || i == 3) && started) spr_tl(g->ren, &g->as->game1, "score_10", (float)g->gd->score_posx[i] + 27, 8, 255);
    }
}

/* -[WindowGameWindow Render] @0x3a038-0x3a26c: MusicData GetTopScore: of the playing song/difficulty;
   digits highscore_0d (TP 0x14b+d) left-aligned from x=129, 14 px stride, y 7, leading zeros skipped
   (no forced units digit), highscore_10 comma (TP 0x155) at x+13 after digit 0 and 3 (adds 6); the
   "High Score" label highscore_11 (TP 0x156) at (5,8) only when a digit was drawn (@0x3a206), so with no
   high score yet (0) nothing at all is drawn. Colour (1,1,1,1), blend 1. */
static void draw_highscore(Game *g, int value) {
    char name[32];
    if (value % 10000000 <= 0) return;
    spr_tl(g->ren, &g->as->game1, "highscore_11", 5, 8, 255);
    float x = 129;
    int div = 1000000;
    bool started = false;
    for (int i = 0; i < 7; i++, div /= 10) {
        int d = (value / div) % 10;
        if (d > 0) started = true;
        if (!started) continue;
        snprintf(name, sizeof name, "highscore_%02d", d);
        spr_tl(g->ren, &g->as->game1, name, x, 7, 255);
        if (i == 0 || i == 3) { spr_tl(g->ren, &g->as->game1, "highscore_10", x + 13, 7, 255); x += 6; }
        x += 14;
    }
}

/* -[WindowGameWindow Render] @0x3a808-0x3a9ce: only at StatusData GetCombo >= 5. 4 digits of combo % 10000
   (leading zeros skipped) as combonumber_0d (TP 0x125+d) from x=6, 18 px stride, y 517 + GetBottomOfs,
   z 4; then the label at x = 18 * digits + 8: combonumber_11 "combo" (TP 0x130), or combonumber_12 "hit"
   (TP 0x131) when StatusData GetDifficulty == 4 (Break the Limit, where misses don't reset the count).
   Colour GetColor(GetUIAlpha,1,1,1), blend 1: it fades out with the keys for interludes and is hidden
   until the FadeIn cue. No animation, timer or milestone effect (the combo's other uses: whirls,
   EffectPulse rings, rail_01 at 100, climax rainbow notes). */
static void draw_combo(Game *g, Uint8 alpha) {
    if (g->combo < 5 || alpha == 0) return;
    char name[32];
    int v = g->combo % 10000, div = 1000;
    bool started = false;
    float x = 6;
    for (int i = 0; i < 4; i++, div /= 10) {
        int d = (v / div) % 10;
        if (d > 0) started = true;
        if (!started) continue;
        snprintf(name, sizeof name, "combonumber_%02d", d);
        spr_tl(g->ren, &g->as->game1, name, x, 517, alpha);
        x += 18;
    }
    spr_tl(g->ren, &g->as->game1, g->diff == DIFF_BREAK ? "combonumber_12" : "combonumber_11", x + 2, 517, alpha);
}

/* KEYBOARD option: SystemTexture LoadBoardTex picks game_tex_02 / _fullroman / _romanhira */
static const Atlas *key_atlas(Game *g) {
    int k = g->opt->keyboard;
    if (k >= 1 && k <= 2 && g->as->game2_kb[k - 1].tex) return &g->as->game2_kb[k - 1];
    return &g->as->game2;
}

/* flick "flower" around a held key: -[TouchFlickBoard RenderGuide:] @0x6f860 with the current flick
   direction. Direction 0 (no flick yet): all petals unlit, only with the FLOWER option on. Once the
   finger has a direction: only that petal, lit, whatever the option (when the key has a character
   there). RenderGuideUp/Down/Left/Right @0x6f228..: flickguide_*_00..03 (TP 312..315, +4 lit) and the
   e_GuideCharPictIdx glyph (fgcharacter_ori, TP 370 + idx); the table is indexed by flick code.
   Petals (z -1.2) and glyphs (z -1.3) use GetColor(GetUIAlpha,1,1,1), blend 1: they fade with the keys.
   RenderGuide: returns at once while the board's m_InterludeMode is set (only board 0 gets it, from
   StartInterludeMode until the interlude panel has faded out: WindowManager exec @0x3e3ac). */
static void draw_flower(Game *g, int key, int dir, Uint8 alpha) {
    const BoardKey *b = &g->gd->board[key];
    float px = (float)b->px, py = (float)b->py;
    static const float petal[4][2] = {{7, -109}, {7, 82}, {-127, 3}, {99, 3}};  /* up, down, left, right */
    static const float glyph[4][2] = {{36, -85}, {36, 136}, {-90, 26}, {160, 26}};
    static const int dir_of[4] = {1, 3, 4, 2};      /* flick code of each petal */
    if (key == 0 && g->int_panel) return;           /* m_InterludeMode */
    if (dir == 0 && !g->opt->flower) return;         /* StatusData IsFlowerEnable */
    if (alpha == 0) return;
    const Atlas *a2 = key_atlas(g);
    for (int p = 0; p < 4; p++) {
        if (dir != 0 && dir != dir_of[p]) continue;
        int gi = g->gd->guide[key][dir_of[p]];
        if (gi < 0) continue;
        char name[32];
        bool on = dir != 0;
        snprintf(name, sizeof name, "flickguide_%s_%02d", on ? "on" : "off", p);
        spr_tl(g->ren, &g->as->game1, name, px + petal[p][0], py + petal[p][1], alpha);
        snprintf(name, sizeof name, "fgcharacter_ori_%02d", gi);
        spr_tl(g->ren, a2, name, px + glyph[p][0], py + glyph[p][1], alpha);
    }
}

void game_render(Game *g) {
    SDL_Renderer *r = g->ren;
    Assets *as = g->as;
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);

    SDL_Texture *vt = video_texture(g->video);
    if (vt) {
        SDL_FRect full = {0, 0, LOGICAL_W, LOGICAL_H};
        SDL_RenderCopyF(r, vt, NULL, &full);
    }

    if (g->opt->pv_mode) {
        /* ScenePVView: full-screen movie; lyrics, pause button/menu, fade and swipe arrows in pvview.c */
        pvview_render(r, as, g->opt, g->paused, vt != NULL, g->lyric);
        return;
    }
    const Atlas *g1 = &as->game1, *ef = &as->effect1;
    Uint8 ui_a = a255(g->ui_alpha);    /* WindowManager GetUIAlpha */
    Uint8 int_a = a255(g->int_alpha);  /* GetInterludeUIAlpha */

    /* --- WindowGameWindow Render: HUD --- */
    draw_score(g, g->stage_score + g->combo_score);
    draw_highscore(g, g->highscore);

    /* judge circle (@0x3a390-0x3a592): charactercircle_int (TP 0x124) at (-2,408) with the interlude alpha
       while it is > 0; then, only while GetUIAlpha > 0 (@0x3a474), charactercircle (TP 0x123) at (8,421)
       and outsidecircle (TP 0x15e) at (-9,404), both with the UI alpha */
    if (g->int_alpha > 0.0f) spr_tl(r, g1, "charactercircle_int", -2, 408, int_a);
    if (g->ui_alpha > 0.0f) {
        spr_tl(r, g1, "charactercircle", 8, 421, ui_a);
        spr_tl(r, g1, "outsidecircle", -9, 404, ui_a);
    }

    /* rail (@0x3a592-0x3a804): rail_00 (TP 0x165) at (0,424) stretched to 640 x frame height, z 5.2; at
       combo >= 100 rail_01 (TP 0x166) instead at x = -pos and 640 - pos (WindowGameWindow exec: pos =
       (pos + 32) % 640 per tick). Colour GetColor(GetUIAlpha,1,1,1): the rail is invisible until the FadeIn
       cue and fades out with the keys during interludes (not an emulator bug). */
    {
        const Frame *rf = atlas_find(g1, "rail_00");
        float rh = rf ? (float)rf->r.h : 116;
        if (g->combo >= 100) {
            float ofs = (float)g->rail_pos;
            spr_tl_wh(r, g1, "rail_01", -ofs, 424, 640, rh, ui_a);
            spr_tl_wh(r, g1, "rail_01", 640 - ofs, 424, 640, rh, ui_a);
        } else {
            spr_tl_wh(r, g1, "rail_00", 0, 424, 640, rh, ui_a);
        }
    }
    draw_combo(g, ui_a);

    draw_whirls(g);
    draw_spscore(g);

    /* tension (WindowGameWindow Render @0x3ad26): microphonemeter_00 (dark stand) and _01 (glowing
       outline) both whole at y 572, then the _02 fill clipped from the bottom: fill = 362*gauge/256 + 0.5
       px (362 from the stale e_SystemPictTbl entry), drawn at y = 943 - fill. Right-handed: x 4 / 4 / 15.05;
       the left-handed layout mirrors it with RenderTexClipReverse at x 490 / 521 / 534. */
    {
        bool mirror = g->opt->left_hand;
        SDL_RendererFlip fl = mirror ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
        static const char *mic[2] = {"microphonemeter_00", "microphonemeter_01"};
        static const float mic_x[2][2] = {{4, 4}, {490, 521}};
        for (int i = 0; i < 2; i++) {
            const Frame *f = atlas_find(g1, mic[i]);
            if (!f) continue;
            SDL_FRect d = {mic_x[mirror][i], 572, (float)f->r.w, (float)f->r.h};
            SDL_SetTextureAlphaMod(g1->tex, ui_a);
            SDL_RenderCopyExF(r, g1->tex, &f->r, &d, 0, NULL, fl);
        }
        const Frame *f = atlas_find(g1, "microphonemeter_02");
        float fill = 362.0f * (float)(int)g->gauge / 256.0f + 0.5f;
        int fh = (int)fill;
        if (f && fh > 0) {
            if (fh > f->r.h) fh = f->r.h;
            SDL_Rect src = {f->r.x, f->r.y + f->r.h - fh, f->r.w, fh};
            SDL_FRect d = {mirror ? 534.0f : 15.05f, 943.0f - fill, (float)f->r.w, (float)fh};
            SDL_SetTextureAlphaMod(g1->tex, ui_a);
            SDL_RenderCopyExF(r, g1->tex, &src, &d, 0, NULL, fl);
        }
    }

    /* --- NoteManager render: notes (newest first so the oldest is on top) --- */
    for (int i = g->nnotes - 1; i >= 0; i--) {
        const Note *n = &g->notes[i];
        if (!n->started || n->dead) continue;
        if (n->x > 680.0f || n->x < -40.0f) continue;          /* render culls outside [-40,680] */
        bool judged = !n->active && n->playable && n->grade > 0;
        Uint8 a = judged ? 110 : 255;
        if (n->type == NOTE_INTERLUDE) {
            const Frame *q = atlas_find(g1, n->active ? "quaver_00" : "quaver_01");
            if (q) spr_tl(r, g1, n->active ? "quaver_00" : "quaver_01", n->x - q->r.w * 0.5f, LANE_CY - q->r.h * 0.5f, a);
            continue;
        }
        /* option (small kana) notes are drawn 32 px right of the base glyph position */
        float gx = n->x - 39 + (n->small ? 32 : 0);
        if (n->crimax && n->active && g->combo >= 100) {
            /* NoteNormal render @0x2269e: a climax note at combo >= 100 is tinted with
               s_tblRainbow[(m_CrimaxCnt + 10) % 28] (m_CrimaxCnt counts ticks like m_Cnt) */
            const Uint8 *c = k_rainbow[((n->cnt + 10) % 28 + 28) % 28];
            SDL_Texture *kt = g->as->kana_tex[(g->gd->kana[n->ch].tex >= 6 ? 2 : 0) + (n->playable ? 1 : 0)].tex;
            if (kt) SDL_SetTextureColorMod(kt, c[0], c[1], c[2]);
            draw_kana(g, n->ch, n->playable, gx, NOTE_Y, a);
            if (kt) SDL_SetTextureColorMod(kt, 255, 255, 255);
        } else {
            draw_kana(g, n->ch, n->playable, gx, NOTE_Y, a);
        }
        /* NoteNormal render @0x2292a: every active note carries the emphasiscircle ring, centred at
           pos + (1,39), angle pi - m_Rot/100 rad; NoteNormal exec: m_Rot = (m_Rot + 21) % 360 per tick */
        if (n->active && !n->small) {
            int rot = (int)(((long)n->cnt * 21) % 360);
            if (rot < 0) rot += 360;
            float deg = (float)((M_PI - rot / 100.0) * 180.0 / M_PI);
            spr_rot(r, g1, "emphasiscircle", n->x + 1, NOTE_Y + 39, deg, 1.0f, 255);
        }
    }

    /* last-note direction (inputguide_0N at 26,440) and the timing ring (inputtarget/inputperfect at 69,482) */
    const Note *next = NULL;
    for (int i = 0; i < g->nnotes; i++)
        if (g->notes[i].active && g->notes[i].type == NOTE_NORMAL && g->notes[i].started) { next = &g->notes[i]; break; }
    if (next && next->cnt >= next->just - 11 && next->cnt <= next->just) {
        char name[32];
        /* s_tblLastNoteTexIdx @0x138f38 = TP frames {278, 279, 282, 280, 281}: the sprites are ordered
           tap, up, down, left, right while flick codes are tap, up, right, down, left */
        static const int guide_of_dir[5] = {0, 1, 4, 2, 3};
        snprintf(name, sizeof name, "inputguide_%02d", guide_of_dir[next->flick]);
        spr_tl(r, ef, name, 26, 440, ui_a);
        int dist = next->just - next->cnt + 1;
        if (dist < 2) spr_rot(r, ef, "inputperfect", 69, 482, 0, 1.0f, ui_a);
        else {
            float s = 0.48f + 0.52f * (dist > 12 ? 1.0f : dist / 12.0f);
            spr_rot(r, ef, "inputtarget", 69, 482, 0, s, ui_a);
        }
    }

    /* --- effects (EffectManager render, 30 Hz ticks). EffectManager exec (m_TimeCnt++) runs before render,
       so the first frame shown is tick n = 1 and an effect is gone at n = m_Timer. Every RenderTex passes a
       z: the pulse rings (-1 .. -1.3) lie under the results and quavers (-2), so two passes. --- */
    for (int i = 0; i < MAX_FX; i++) {
        const Fx *f = &g->fx[i];
        double age = ticks_since(g, f->t0);
        if (f->kind == FX_PULSE && age >= 0) draw_pulse(g, (int)floor(age) + 1);
    }
    for (int i = 0; i < MAX_FX; i++) {
        Fx *f = &g->fx[i];
        if (f->kind == FX_NONE || f->kind == FX_PULSE) continue;
        double age = ticks_since(g, f->t0);
        if (age < 0) continue;
        if (f->kind == FX_RESULT && age < 19) {
            /* -[EffectFlickResult exec] @0x2e914 / render @0x2eb34 (m_Timer 20): centre (66, 483 + m_OfsY);
               m_SizeRate min(1, 0.1 + 0.15 n); fade over the last 5 ticks. Back layer afterimage_*
               (s_tblResultEffect_Back @0x13ac3c = TP {286,290,289,288,287,286} by grade) at w*size*stretch
               (the m_StretchRate flip-spin, exec @0x2ea26) x h*size; then the front evaluation_*
               (s_tblResultEffect @0x13ac54) at w*size x h*size, not stretched. Both blend 1. */
            static const int back_idx[6] = {0, 4, 3, 2, 1, 0};
            int grade = f->v < 0 ? 0 : f->v > 5 ? 5 : f->v;
            int n = (int)floor(age) + 1;
            float ofs = -(float)g->gd->result_ofsy[grade] * sinf((float)n / 20.0f * 1.57f)
                        - 80.0f * (1.0f - powf(0.5f, (float)n));
            float size = fminf(1.0f, 0.1f + 0.15f * (float)n);
            float alpha = n > 15 ? 1.0f - 0.2f * (float)(n - 15) : 1.0f;
            char name[32];
            float fw, fh;
            snprintf(name, sizeof name, "afterimage_%02d", back_idx[grade]);
            frame_size(g1, name, &fw, &fh);
            fx_tex(r, g1, name, 66, 483 + ofs, fw * size * wobble_stretch(n, 20), fh * size, 0, 255, 255, 255, alpha, 1);
            snprintf(name, sizeof name, "evaluation_%02d", 5 - (grade < 1 ? 1 : grade));
            frame_size(g1, name, &fw, &fh);
            fx_tex(r, g1, name, 66, 483 + ofs, fw * size, fh * size, 0, 255, 255, 255, alpha, 1);
        } else if (f->kind == FX_INTERLUDE && age < 19) {
            /* -[EffectInterlude exec] @0x62be8 / render @0x62de4 (m_Timer 20): tick n = m_TimeCnt;
               m_SizeRate = min(1, 0.1 + 0.15 n); m_OfsY = -100 sin(n/20 * 1.57) + m_OfsAddTmpY, where
               m_OfsAddTmpY sums m_OfsAddY (-40, halved each tick) = -80 (1 - 0.5^n); alpha 1 - 0.2 (n - 15)
               after tick 15. quaver_01 (TP 0x162) w*size*stretch x h*size centred (70, 483 + m_OfsY), blend 1. */
            int n = (int)floor(age) + 1;
            float size = fminf(1.0f, 0.1f + 0.15f * (float)n);
            float ofs = -100.0f * sinf((float)n / 20.0f * 1.57f) - 80.0f * (1.0f - powf(0.5f, (float)n));
            float alpha = n > 15 ? 1.0f - 0.2f * (float)(n - 15) : 1.0f;
            float qw, qh;
            frame_size(g1, "quaver_01", &qw, &qh);
            fx_tex(r, g1, "quaver_01", 70, 483 + ofs, qw * size * wobble_stretch(n, 20), qh * size, 0,
                   255, 255, 255, alpha, 1);
        }
    }

    /* --- TouchFlickBoard Render: keys at their table position (top-left) --- */
    /* -[TouchFlickBoard Render] @0x6f9d4, per board: each board tracks its own touch (m_TouchDownIdx), so
       every held key lights up, not only the last one pressed. The pressed visual is only the texture swap
       flickpanel_ori_off_NN (TP 416+n) -> flickpanel_ori_on_NN (TP 426+n) @0x6fc14: same rect, no scale, no
       glow, z -1, GetColor(GetUIAlpha,1,1,1), blend 1, whatever the interlude state (the UI alpha hides the
       keys during an interlude). m_FlickResult (checkFlick on every touchMoved) picks the lit petal. */
    const Pointer *held[NUM_KEYS] = {0};
    for (int i = 0; i < MAX_POINTERS; i++) {
        const Pointer *p = &g->ptr[i];
        if (p->down && p->key >= 0 && p->key < NUM_KEYS && !held[p->key]) held[p->key] = p;
    }
    bool lit[NUM_KEYS];   /* held, or pressed less than MIN_PRESS_MS ago (port: short clicks) */
    Uint32 now_ms = SDL_GetTicks();
    for (int k = 0; k < NUM_KEYS; k++)
        lit[k] = held[k] || (g->key_down_ms[k] && now_ms - g->key_down_ms[k] < MIN_PRESS_MS);
    for (int k = 0; k < NUM_KEYS; k++) {
        const BoardKey *b = &g->gd->board[k];
        char name[32];
        snprintf(name, sizeof name, "flickpanel_ori_%s_%02d", lit[k] ? "on" : "off", k);
        spr_tl(r, key_atlas(g), name, (float)b->px, (float)b->py, ui_a);
    }
    if (g->int_panel) {
        /* board 0, while WindowManager IsInterludeMode (s_IsInterlude, cleared with the panel when the
           interlude alpha runs out): flickguide_int_on (TP 0x115) while board 0 is held, else _off (0x114),
           at (pos.x - 15, pos.y), z -1, GetInterludeUIAlpha, blend 1 @0x6fa76..0x6fb72. After
           EndInterludeMode board 0 is あ again, so holding あ lights the fading panel. */
        const BoardKey *b0 = &g->gd->board[0];
        spr_tl(r, ef, lit[0] ? "flickguide_int_on" : "flickguide_int_off", (float)b0->px - 15, (float)b0->py, int_a);
    }
    /* TouchFlickBoard Render tail @0x6fc44: RenderGuide:m_FlickResult while the board is touched
       (a released short press keeps the direction it was released with) */
    for (int k = 0; k < NUM_KEYS; k++) {
        const Pointer *p = held[k];
        if (p) draw_flower(g, k, p->kind == PTR_KEYBOARD ? (p->kb_dir ? p->kb_dir : held_arrow()) : flick_dir(p), ui_a);
        else if (lit[k]) draw_flower(g, k, g->key_up_dir[k], ui_a);
    }
    if (!g->interlude_mode) {
        /* NoteNormal render @0x22a38 (unless PANEL GUIDE is off): every active note up to its just frame
           shows its direction on its key, scaled by cnt/just (nothing below 0.25) about the panel's
           rect; the next target uses the _on panel plus guidecursor at key - (3,3), the others the
           _off panel. Older notes are in front, so draw newest first. The panels (z -1.5 / -4.0) are in
           front of the flower petals (TouchFlickBoard RenderGuideUp.. z about -1.2 / -1.3), so they go after it. */
        if (g->opt->guide_panel)
            for (int i = g->nnotes - 1; i >= 0; i--) {
                const Note *n = &g->notes[i];
                if (n->type != NOTE_NORMAL || !n->active || !n->started || n->dead || n->small || n->cnt > n->just)
                    continue;
                const BoardKey *b = &g->gd->board[n->board];
                bool is_next = n == next;
                if (is_next) spr_tl(r, g1, "guidecursor", (float)b->px - 3, (float)b->py - 3, ui_a);
                float s = n->just > 0 ? (float)n->cnt / (float)n->just : 1.0f;
                if (s < 0.25f) continue;
                const char *pn = guide_frame(n->flick, is_next);
                const Frame *pf = atlas_find(g1, pn);
                if (!pf) continue;
                float fw = (float)pf->r.w, fh = (float)pf->r.h;
                spr_tl_wh(r, g1, pn, (float)b->px + fw * (1 - s) * 0.5f, (float)b->py + fh * (1 - s) * 0.5f, fw * s, fh * s, ui_a);
            }
    }

    /* -[TouchPause Render] @0x72738: pausebutton (TP 0x160) colour (1,1,1,1), blend 1, rect scaled 1.05x
       about its centre (CoordinateScalingPoints) while a touch holds it; hidden by DISPLAY PAUSE: INVISIBLE
       (IsHidePauseBtn), the touch area stays */
    if (g->opt->pause_button) {
        float px = pbtn_x(g) + 5, py = 899, pw, ph;
        frame_size(g1, "pausebutton", &pw, &ph);
        bool pb_recent = g->pbtn_ms && SDL_GetTicks() - g->pbtn_ms < MIN_PRESS_MS;   /* port: short click */
        if (g->pbtn_down || pb_recent) pause_scale(1.05f, &px, &py, &pw, &ph);
        spr_tl_wh(r, g1, "pausebutton", px, py, pw, ph, 255);
    }

    /* WindowGameWindow Render: m_Fade black over the whole game screen (the song's fade-in) */
    if (g->fade > 0.0f) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0, 0, 0, a255(g->fade));
        SDL_RenderFillRect(r, NULL);
    }

    if (g->paused) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        /* PauseRenderFunc_Enable @0x38ff4: 30% black (GetColor 0.3,0,0,0 = alpha 77) + menugradation band from y=374 */
        SDL_SetRenderDrawColor(r, 0, 0, 0, 77);
        SDL_RenderFillRect(r, NULL);
        const Frame *mg = atlas_find(g1, "menugradation");
        spr_tl_wh(r, g1, "menugradation", 0, 374, 640, mg ? (float)mg->r.h : 586, 255);
        static const char *labels[3] = {"RESUME", "RETRY", "MUSIC SELECT"};
        const Atlas *u1 = &as->ui1;
        /* TouchBack / TouchRetry Render (@0x6e27e): the same m-size button as music select PLAY: base at
           pos; pulsing face (CoordinateColor, colour = alpha = v, blend 1) at (pos.x+1, pos.y+0.5, 310, 68.5),
           drawn 1 px left and 2 px wider; brackets on every button at (pos.x, pos.y+1) and
           (pos.x+1+271*s, pos.y+1); all rects scaled 1.05x about their centre while pressed. The face
           counter steps every 51 ms of wall time (15 Hz at 30 fps, the CFAbsoluteTime gate in TouchRetry Render)
           and keeps running while paused. */
        Uint8 v = pause_pulse(SDL_GetTicks() / 67u);
        for (int i = 0; i < 3; i++) {
            float y = k_pause_y[i];
            bool lg = g->pause_press < 0 && g->pause_lg_until && (Sint32)(g->pause_lg_until - SDL_GetTicks()) > 0 &&
                      i == g->pause_lg;   /* port: quick click (TouchRetry Render @0x69f0c: m_TouchDownIdx >= 0) */
            bool down = i == g->pause_press || lg || (g->pause_kbd && i == g->pause_focus);
            float s = down ? 1.05f : 1.0f;
            float bx = 164, by = y, bw = 324, bh = 82;
            float fx = 165, fy = y + 0.5f, fw = 310, fh = 68.5f;
            float px = 165, py = y + 1, pw = 39, ph = 68;
            pause_scale(s, &bx, &by, &bw, &bh);
            pause_scale(s, &fx, &fy, &fw, &fh);
            pause_scale(s, &px, &py, &pw, &ph);
            if (down) px -= 7;
            spr_tl_wh(r, u1, "menubutton_m_base", bx, by, bw, bh, 255);
            SDL_SetTextureColorMod(u1->tex, v, v, v);
            spr_tl_wh(r, u1, down ? "menubutton_m_on" : "menubutton_m_off", fx - 1, fy, fw + 2, fh, v);
            SDL_SetTextureColorMod(u1->tex, 255, 255, 255);
            font_draw(r, labels[i], 326, y + 40 - 18 * s, 3.25f * s, (SDL_Color){255, 255, 255, 255}, 0);
            spr_tl_wh(r, u1, "parenthesis_00", px - 1, py, pw, ph, 255);
            spr_tl_wh(r, u1, "parenthesis_01", px + 271 * s, py, pw, ph, 255);
        }
        /* TouchInputTiming Render @0x7d7a0: "INPUT TIMING" (28 pt, centred, m_Pos.y - 58; capitals ~6 px
           lower), inputbar at (100, m_Pos.y), inputcursor at (285 - 20.4 * timing, m_Pos.y - 19) */
        font_draw(r, "INPUT TIMING", 320, 843, 2.6f, (SDL_Color){255, 255, 255, 255}, 0);
        spr_tl(r, g1, "inputbar", 100, 895, 255);
        spr_tl(r, g1, "inputcursor", 285.0f - 20.4f * (float)g->opt->input_timing, 876, 255);
    }
}

void game_result(const Game *g, GameResult *o) {
    memset(o, 0, sizeof *o);
    o->song = g->song;
    o->diff = g->diff;
    memcpy(o->counts, g->counts, sizeof o->counts);
    o->total_notes = g->total_notes;
    o->max_combo = g->max_combo;
    o->stage_score = g->stage_score;
    o->combo_score = g->combo_score;
    o->interlude_hits = g->interlude_cnt;
    o->total_interlude = g->total_interlude;
    o->game_over = g->game_over;
    memcpy(o->flick_total, g->flick_total, sizeof o->flick_total);
    memcpy(o->flick_success, g->flick_success, sizeof o->flick_success);
    if (g->retry) { o->rank = -2; return; }   /* RETRY: restart the same song */
    if (g->opt->pv_mode) { o->rank = g->quit ? -1 : -3; return; }   /* PV view: no result screen */
    if (g->quit) { o->rank = -1; return; }
    /* WindowResult loadTexture @0x4ba80 */
    float total = (float)(g->total_notes > 0 ? g->total_notes : 1);
    if (g->game_over) o->rank = 6;
    else if ((float)(g->counts[3] + g->counts[4] + g->counts[5]) / total < 0.7f) o->rank = 5;
    else if (g->counts[5] >= g->total_notes) o->rank = 0;
    else {
        float r2 = (float)(g->counts[4] + g->counts[5]) / total;
        o->rank = r2 < 0.8f ? 4 : r2 < 0.95f ? 3 : r2 < 1.0f ? 2 : 1;
    }
}

void game_set_highscore(Game *g, int hs);
void game_set_highscore(Game *g, int hs) { g->highscore = hs; }
