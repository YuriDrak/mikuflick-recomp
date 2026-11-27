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
enum { FX_NONE, FX_RESULT, FX_PULSE, FX_INTERLUDE, FX_SPSCORE };
typedef struct { int kind; double t0; int v; } Fx;

struct Game {
    SDL_Renderer *ren;
    Assets *as;
    const GameData *gd;
    const Options *opt;
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
    float gauge;             /* DAT_00163088 */
    bool game_over, interlude_mode, paused, finished, quit, retry;
    int pause_focus;
    int pause_press;         /* pause button held by the pointer, -1 none */
    bool pause_kbd;          /* keyboard focus shown (the original is touch-only) */
    char lyric[256];         /* PV mode: current lyric line (op 3 cue) */
    float board_alpha;

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
    g->pause_kbd = false;
    mixer_music_pause(on);
}

/* ---------------------------------------------------------------- effects */

static void fx_add(Game *g, int kind, int v) {
    g->fx[g->fx_next] = (Fx){kind, g->t, v};
    g->fx_next = (g->fx_next + 1) % MAX_FX;
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
    case 1: g->board_alpha = 1.0f; break;  /* FadeIn: WindowManager SetUIFade:1 */
    case 2: break;                          /* FadeOut */
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
            g->interlude_mode = true;       /* WindowManager StartInterludeMode */
            add_note(g, NOTE_INTERLUDE, 0, true);
        } else if (d == 2) {
            g->interlude_mode = false;      /* EndInterludeMode */
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
static void count_totals(Game *g) {
    g->total_notes = g->total_interlude = 0;
    for (int i = 0; i < g->cues.n; i++) {
        const char *p = g->cues.cues[i].param;
        if (p[0] == '0' && digit_at(p, g->diff + 2) > 0) g->total_notes++;
        if (p[0] == '5' && digit_at(p, g->diff + 1) == 1) g->total_interlude++;
    }
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
            play(g, SND_HIT);
            fx_add(g, FX_SPSCORE, 39);
        }
        fx_add(g, FX_INTERLUDE, 0);
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
            fx_add(g, FX_SPSCORE, g->gd->addscore[grade] + bonus + 200);
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
    if (grade >= 4) fx_add(g, FX_PULSE, grade);
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
    if (g->interlude_mode) {
        /* TouchFlickBoard setInterludeRect: the whole board is one panel
           (flickguide_int_* is drawn at (114,530), 409x350; わ's row stays live too) */
        return (x >= 114 && x < 523 && y >= 530 && y < 962) ? 0 : -1;
    }
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
    touch_down_all(g); /* TouchFlickBoard touchDown -> NoteManager setTouchDown */
}

static void move(Game *g, Pointer *p, float x, float y) {
    p->x1 = x;
    p->y1 = y;
    touch_up_all(g);   /* touchMoved -> NoteManager setTouchUp + checkFlick */
}

static void release(Game *g, Pointer *p, int dir) {
    p->down = false;
    touch_up_all(g);
    judge(g, p->key, dir);

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
static int pause_hit(float mx, float my) {
    for (int i = 0; i < 3; i++)
        if (mx >= 164 && mx < 164 + 324 && my >= k_pause_y[i] && my < k_pause_y[i] + 82) return i;
    return -1;
}

static void pause_choose(Game *g, int i) {
    play_sound(g->as, SND_HIT);
    if (i == 0) { g->paused = false; mixer_music_pause(false); }
    else if (i == 1) { g->retry = true; g->finished = true; }
    else { g->quit = true; g->finished = true; }
}

SDL_Texture *game_backdrop(const Game *g) { return g ? video_texture(g->video) : NULL; }

void game_pause(Game *g) {
    if (!g || !g->loaded || g->finished || g->paused || g->opt->pv_mode) return;
    enter_pause(g, true);
}

void game_event(Game *g, const SDL_Event *e) {
    if (!g->loaded || g->finished) return;
    g->evt_dcnt = 0;
    if (!g->paused && !g->opt->pv_mode &&
        (e->type == SDL_KEYDOWN || e->type == SDL_KEYUP || e->type == SDL_MOUSEBUTTONDOWN || e->type == SDL_MOUSEBUTTONUP ||
         e->type == SDL_MOUSEMOTION || e->type == SDL_FINGERDOWN || e->type == SDL_FINGERUP || e->type == SDL_FINGERMOTION))
        stamp_event(g, e);
    if (g->opt->pv_mode) {   /* PV view: Esc / click returns to PV select, L/K toggle lyrics/karaoke */
        if ((e->type == SDL_KEYDOWN && !e->key.repeat &&
             (e->key.keysym.sym == SDLK_ESCAPE || e->key.keysym.sym == SDLK_BACKSPACE)) ||
            (e->type == SDL_MOUSEBUTTONUP && e->button.button == SDL_BUTTON_LEFT)) {
            g->quit = true;
            g->finished = true;
        }
        return;
    }
    if (e->type == SDL_KEYDOWN && !e->key.repeat) {
        SDL_Keycode k = e->key.keysym.sym;
        if (k == SDLK_ESCAPE || k == SDLK_p) {
            enter_pause(g, !g->paused);
            return;
        }
        if (g->paused) { /* pause menu: RESUME / RETRY / MUSIC SELECT */
            if (k == SDLK_UP || k == SDLK_DOWN) g->pause_kbd = true;
            if (k == SDLK_UP) g->pause_focus = (g->pause_focus + 2) % 3;
            if (k == SDLK_DOWN) g->pause_focus = (g->pause_focus + 1) % 3;
            if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE) pause_choose(g, g->pause_focus);
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
        if ((e->type == SDL_MOUSEBUTTONDOWN || e->type == SDL_MOUSEBUTTONUP) && e->button.button == SDL_BUTTON_LEFT) {
            float mx = (float)e->button.x, my = (float)e->button.y;
            int hit = pause_hit(mx, my);
            if (e->type == SDL_MOUSEBUTTONDOWN) { g->pause_press = hit; g->pause_kbd = false; }
            else {
                int was = g->pause_press;
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
        if (x > 570 && y > 890) { enter_pause(g, true); return; } /* pause button */
        press(g, PTR_MOUSE, 0, x, y, key_at(g, x, y));
    } else if (e->type == SDL_MOUSEMOTION && e->motion.which != SDL_TOUCH_MOUSEID) {
        Pointer *p = ptr_find(g, PTR_MOUSE, 0);
        if (p) move(g, p, (float)e->motion.x, (float)e->motion.y);
    } else if (e->type == SDL_MOUSEBUTTONUP && e->button.button == SDL_BUTTON_LEFT && e->button.which != SDL_TOUCH_MOUSEID) {
        Pointer *p = ptr_find(g, PTR_MOUSE, 0);
        if (p) { p->x1 = (float)e->button.x; p->y1 = (float)e->button.y; release(g, p, flick_dir(p)); }
    } else if (e->type == SDL_FINGERDOWN || e->type == SDL_FINGERMOTION || e->type == SDL_FINGERUP) {
        int ww, wh;
        SDL_GetWindowSize(SDL_RenderGetWindow(g->ren), &ww, &wh);
        float lx, ly;
        view_window_to_logical(g->ren, e->tfinger.x * ww, e->tfinger.y * wh, &lx, &ly);
        if (e->type == SDL_FINGERDOWN) {
            if (lx > 570 && ly > 890) { game_pause(g); return; } /* pause button */
            press(g, PTR_FINGER, e->tfinger.fingerId, lx, ly, key_at(g, lx, ly));
        } else {
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
    g->opt = opt;
    g->song = song;
    g->diff = diff;
    g->gauge = 128.0f;       /* NoteManager Reset */
    g->board_alpha = 1.0f;
    g->pause_press = -1;
    g->bpm = gd->songs[song].bpm;
    g->delay_ms = (int)(center_time(g->bpm) * 1000.0f);
    g->last_grade_t = -10;

    char path[1024];
    snprintf(path, sizeof path, "%s/%s.usm", as->appdir, gd->songs[song].movie);
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

bool game_update(Game *g) {
    if (g->finished) return false;
    if (g->paused) return true;
    g->evt_dcnt = 0;          /* autoplay presses below happen "now" */
    g->t = mixer_music_time();
    /* CriManager::Exec fires the cue callbacks first (handlers see the previous PlayCnt) */
    while (g->next_cue < g->cues.n && (double)g->cues.cues[g->next_cue].time_ms <= g->t * 1000.0)
        fire_cue(g, g->cues.cues[g->next_cue++].param);
    g->play_cnt = (int)ceil(g->t * 30.0);
    exec_notes(g);
    if (g->opt->autoplay) autoplay(g);
    else if (g->opt->autoplay_input) autoplay_input(g);
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

/* WindowGameWindow: 7-digit score at s_tblScorePosX, commas after digit 0 and 3 (x+27), y=8 */
static void draw_score(Game *g, int value) {
    char name[32];
    int div = 1000000;
    bool started = false;
    for (int i = 0; i < 7; i++, div /= 10) {
        int d = (value / div) % 10;
        if (d > 0 || i == 6) started = true;
        if (!started) continue;
        snprintf(name, sizeof name, "score_%02d", d);
        spr_tl(g->ren, &g->as->game1, name, (float)g->gd->score_posx[i], 8, 255);
        if ((i == 0 || i == 3) && started) spr_tl(g->ren, &g->as->game1, "score_10", (float)g->gd->score_posx[i] + 27, 8, 255);
    }
}

/* high score: label (5,8); digits left-aligned from x=129, 14 px stride, comma +13 (adds 6), y=7 */
static void draw_highscore(Game *g, int value) {
    char name[32];
    spr_tl(g->ren, &g->as->game1, "highscore_11", 5, 8, 255);
    float x = 129;
    int div = 1000000;
    bool started = false;
    for (int i = 0; i < 7; i++, div /= 10) {
        int d = (value / div) % 10;
        if (d > 0 || i == 6) started = true;
        if (!started) continue;
        snprintf(name, sizeof name, "highscore_%02d", d);
        spr_tl(g->ren, &g->as->game1, name, x, 7, 255);
        if (i == 0 || i == 3) { spr_tl(g->ren, &g->as->game1, "highscore_10", x + 13, 7, 255); x += 6; }
        x += 14;
    }
}

/* combo (>=5): combonumber digits from x=6, 18 px stride, y=517, then the "combo" label */
static void draw_combo(Game *g) {
    if (g->combo < 5) return;
    char num[16], name[32];
    snprintf(num, sizeof num, "%d", g->combo);
    float x = 6;
    for (const char *c = num; *c; c++) {
        snprintf(name, sizeof name, "combonumber_%02d", *c - '0');
        spr_tl(g->ren, &g->as->game1, name, x, 517, 255);
        x += 18;
    }
    spr_tl(g->ren, &g->as->game1, "combonumber_11", x + 2, 517, 255);
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
   e_GuideCharPictIdx glyph (fgcharacter_ori, TP 370 + idx); the table is indexed by flick code. */
static void draw_flower(Game *g, int key, int dir) {
    const BoardKey *b = &g->gd->board[key];
    float px = (float)b->px, py = (float)b->py;
    static const float petal[4][2] = {{7, -109}, {7, 82}, {-127, 3}, {99, 3}};  /* up, down, left, right */
    static const float glyph[4][2] = {{36, -85}, {36, 136}, {-90, 26}, {160, 26}};
    static const int dir_of[4] = {1, 3, 4, 2};      /* flick code of each petal */
    if (dir == 0 && !g->opt->flower) return;
    const Atlas *a2 = key_atlas(g);
    for (int p = 0; p < 4; p++) {
        if (dir != 0 && dir != dir_of[p]) continue;
        int gi = g->gd->guide[key][dir_of[p]];
        if (gi < 0) continue;
        char name[32];
        bool on = dir != 0;
        snprintf(name, sizeof name, "flickguide_%s_%02d", on ? "on" : "off", p);
        spr_tl(g->ren, &g->as->game1, name, px + petal[p][0], py + petal[p][1], 255);
        snprintf(name, sizeof name, "fgcharacter_ori_%02d", gi);
        spr_tl(g->ren, a2, name, px + glyph[p][0], py + glyph[p][1], 255);
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
        /* ScenePVView: full-screen movie; NoteManager render_Lyrics @0xda64 draws the text texture at y=896 (shadow 898,
           x 15, z -5.1); text textures put the glyph tops ~6 px below their y, font_draw takes the glyph top */
        if (g->lyric[0]) font_draw(r, g->lyric, 320, 902, 2.8f, (SDL_Color){255, 255, 255, 255}, 0);
        return;
    }
    const Atlas *g1 = &as->game1, *ef = &as->effect1;
    double tick = g->t * 30.0;  /* 30 Hz animation clock */
    Uint8 ui_a = (Uint8)(255 * g->board_alpha);

    /* --- WindowGameWindow Render: HUD --- */
    draw_score(g, g->stage_score + g->combo_score);
    draw_highscore(g, g->highscore);

    /* judge circle: charactercircle_int (-2,408) in interlude, charactercircle (8,421), outsidecircle (-9,404) */
    if (g->interlude_mode) spr_tl(r, g1, "charactercircle_int", -2, 408, 255);
    else spr_tl(r, g1, "charactercircle", 8, 421, ui_a);
    spr_tl(r, g1, "outsidecircle", -9, 404, 255);

    /* rail: rail_00 at (0,424) stretched to 640; at combo >= 100 the scrolling rail_01 */
    {
        const Frame *rf = atlas_find(g1, "rail_00");
        float rh = rf ? (float)rf->r.h : 116;
        if (g->combo >= 100) {
            float ofs = (float)fmod(tick * 4.0, 640.0);
            spr_tl_wh(r, g1, "rail_01", -ofs, 424, 640, rh, 255);
            spr_tl_wh(r, g1, "rail_01", 640 - ofs, 424, 640, rh, 255);
        } else {
            spr_tl_wh(r, g1, "rail_00", 0, 424, 640, rh, 255);
        }
    }
    draw_combo(g);

    /* insidewhirl: RenderTexRot centred (68,481) */
    spr_rot(r, g1, "insidewhirl", JUDGE_CX, LANE_CY, (float)fmod(tick * 3.0, 360.0), 1.0f, 200);

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
        spr_tl(r, ef, name, 26, 440, 255);
        int dist = next->just - next->cnt + 1;
        if (dist < 2) spr_rot(r, ef, "inputperfect", 69, 482, 0, 1.0f, 255);
        else {
            float s = 0.48f + 0.52f * (dist > 12 ? 1.0f : dist / 12.0f);
            spr_rot(r, ef, "inputtarget", 69, 482, 0, s, 255);
        }
    }

    /* --- effects (30 Hz ticks) --- */
    for (int i = 0; i < MAX_FX; i++) {
        Fx *f = &g->fx[i];
        if (f->kind == FX_NONE) continue;
        double age = ticks_since(g, f->t0);
        if (age < 0) continue;
        if (f->kind == FX_RESULT && age < 20) {
            /* EffectFlickResult: centred (66,483+ofsY); size 0.1 +0.15/tick (max 1); stretch wobble; fade last 5 ticks */
            int grade = f->v < 0 ? 0 : f->v > 5 ? 5 : f->v;
            double n = floor(age);
            double ofs = -g->gd->result_ofsy[grade] * sin(n / 20.0 * 1.57) - 80.0 * (1.0 - pow(0.5, n));
            double size = fmin(1.0, 0.1 + 0.15 * n);
            double stretch = 1.0; /* m_StretchRate wobble not reproduced (exact use in render unclear) */
            double alpha = n > 15 ? fmax(0.0, 1.0 - 0.2 * (n - 15)) : 1.0;
            char name[32];
            snprintf(name, sizeof name, "evaluation_%02d", 5 - (grade < 1 ? 1 : grade));
            const Frame *fr = atlas_find(g1, name);
            if (fr) {
                float w = (float)(fr->r.w * size * stretch), h = (float)(fr->r.h * size);
                spr_tl_wh(r, g1, name, 66 - w * 0.5f, (float)(483 + ofs) - h * 0.5f, w, h, (Uint8)(255 * alpha));
            }
        } else if (f->kind == FX_PULSE && age < 15) {
            /* EffectPulse ripple around the judge point */
            float s = 0.6f + (float)age * 0.06f;
            spr_rot(r, ef, "ripple", 70, 482, 0, s, (Uint8)(255 * (1.0 - age / 15.0)));
        } else if (f->kind == FX_INTERLUDE && age < 20) {
            /* EffectInterlude: quaver_01 rising from the judge point (70,483) */
            spr_rot(r, g1, "quaver_01", 70, (float)(483 - age * 4.0), 0, (float)fmin(1.0, 0.3 + age * 0.15),
                    (Uint8)(255 * (age > 15 ? (20 - age) / 5.0 : 1.0)));
        } else if (f->kind == FX_SPSCORE && age < 30) {
            char num[16], name[32];
            snprintf(num, sizeof num, "%d", f->v);
            float x = 90, y = (float)(380 - age * 2.0);
            for (const char *c = num; *c; c++, x += 18) {
                snprintf(name, sizeof name, "combonumber_%02d", *c - '0');
                spr_tl(r, g1, name, x, y, (Uint8)(255 * (age > 20 ? (30 - age) / 10.0 : 1.0)));
            }
        }
    }

    /* --- TouchFlickBoard Render: keys at their table position (top-left) --- */
    int held_key = -1, held_dir = 0;
    for (int i = 0; i < MAX_POINTERS; i++)
        if (g->ptr[i].down) {
            held_key = g->ptr[i].key;
            held_dir = g->ptr[i].kind == PTR_KEYBOARD ? g->ptr[i].kb_dir : flick_dir(&g->ptr[i]);
        }
    if (g->interlude_mode) {
        /* key 0 draws flickguide_int_off/on at (pos.x-15, pos.y) */
        const BoardKey *b0 = &g->gd->board[0];
        spr_tl(r, ef, held_key >= 0 ? "flickguide_int_on" : "flickguide_int_off", (float)b0->px - 15, (float)b0->py, 255);
    } else {
        for (int k = 0; k < NUM_KEYS; k++) {
            const BoardKey *b = &g->gd->board[k];
            char name[32];
            snprintf(name, sizeof name, "flickpanel_ori_%s_%02d", held_key == k ? "on" : "off", k);
            spr_tl(r, key_atlas(g), name, (float)b->px, (float)b->py, ui_a);
        }
        if (held_key >= 0) draw_flower(g, held_key, held_dir);
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
                if (is_next) spr_tl(r, g1, "guidecursor", (float)b->px - 3, (float)b->py - 3, 255);
                float s = n->just > 0 ? (float)n->cnt / (float)n->just : 1.0f;
                if (s < 0.25f) continue;
                const char *pn = guide_frame(n->flick, is_next);
                const Frame *pf = atlas_find(g1, pn);
                if (!pf) continue;
                float fw = (float)pf->r.w, fh = (float)pf->r.h;
                spr_tl_wh(r, g1, pn, (float)b->px + fw * (1 - s) * 0.5f, (float)b->py + fh * (1 - s) * 0.5f, fw * s, fh * s, 255);
            }
    }

    if (g->opt->pause_button) spr_tl(r, g1, "pausebutton", 575, 899, 220);   /* DISPLAY PAUSE: INVISIBLE hides it, the touch area stays */

    if (g->paused) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        /* PauseRenderFunc_Enable @0x38ff4: dim + menugradation band from y=374 */
        SDL_SetRenderDrawColor(r, 0, 0, 0, 110);
        SDL_RenderFillRect(r, NULL);
        const Frame *mg = atlas_find(g1, "menugradation");
        spr_tl_wh(r, g1, "menugradation", 0, 374, 640, mg ? (float)mg->r.h : 586, 255);
        static const char *labels[3] = {"RESUME", "RETRY", "MUSIC SELECT"};
        const Atlas *u1 = &as->ui1;
        /* TouchBack / TouchRetry Render (@0x6e27e): the same m-size button as music select PLAY: base at
           pos; pulsing face (CoordinateColor, colour = alpha = v, blend 1) at (pos.x+1, pos.y+0.5, 310, 68.5),
           drawn 1 px left and 2 px wider; brackets on every button at (pos.x, pos.y+1) and
           (pos.x+1+271*s, pos.y+1); all rects scaled 1.05x about their centre while pressed. The face
           counter steps at 30 Hz (like the menu ticks) and keeps running while paused. */
        Uint8 v = pause_pulse(SDL_GetTicks() / 33u);
        for (int i = 0; i < 3; i++) {
            float y = k_pause_y[i];
            bool down = i == g->pause_press || (g->pause_kbd && i == g->pause_focus);
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
