/*
 * Miku Flick/02 Linux port - shared declarations.
 * Clean-room reimplementation; see docs/re_notes.md for the reverse-engineered
 * behaviour each module reproduces and docs/linux_port_plan.md for the design.
 */
#ifndef MF_H
#define MF_H

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LOGICAL_W 640
#define LOGICAL_H 960
#define TICKS_PER_SEC 30          /* gameplay tick rate (PlayCnt) */
#define NUM_DIFF 5                /* Easy Normal Hard Extreme BreakTheLimit */
#define DIFF_BREAK 4
#define MAX_SONGS 32
#define NUM_KANA 190
#define KANA_BASE 0x3041          /* s_HiraganaFirstIdx */
#define NUM_KEYS 10
#define NUM_SOUNDS 30
#define AUDIO_RATE 44100

/* sound ids (SoundManager Play:) */
enum { SND_COOL_SP = 0, SND_HIT = 1, SND_SAD = 2, SND_INTERLUDE = 3, SND_DECIDE = 4, SND_CANCEL = 5,
       SND_SAFE = 18, SND_BGM01 = 22, SND_SEGA = 27, SND_TITLE = 28 };

/* scene ids (SceneManager) */
enum { SCENE_BOOT = 0, SCENE_TITLE = 1, SCENE_GAME = 2, SCENE_SELECT = 3, SCENE_RESULT = 8, SCENE_QUIT = 99 };

/* ---------------- util.c ---------------- */
void *read_file(const char *path, size_t *len);
void log_msg(const char *fmt, ...);
int utf8_decode(const char *s, uint32_t *cp); /* returns bytes consumed, 0 at end */

/* ---------------- gamedata.c ---------------- */
typedef struct {
    char preview[64], movie[64], art_atlas[64], title[160];
    int bpm, stars[4];
} SongInfo;

typedef struct { int key, dir, sub, tex, x, y; } KanaInfo;
typedef struct { int sx, sy, px, py; } BoardKey;

typedef struct {
    SongInfo songs[MAX_SONGS];
    int nsongs;
    KanaInfo kana[NUM_KANA];
    BoardKey board[NUM_KEYS];       /* src rect of key face + TOP-LEFT screen position */
    int guide[NUM_KEYS][5];         /* e_GuideCharPictIdx: fgcharacter index {centre,up,right,down,left}, -1 none */
    int score_posx[7];              /* s_tblScorePosX (score digit x, top-left) */
    int result_ofsy[6];             /* s_tblEffectResultOfsY (judgement popup rise per grade) */
    int front[11], back[12], addscore[12], tension[6], endofs[5];
    char sound[NUM_SOUNDS][64];
    int sound_loop[NUM_SOUNDS];
} GameData;

bool gamedata_load(GameData *gd, const char *path);

/* ---------------- bplist.c ---------------- */
typedef enum { BP_NULL, BP_BOOL, BP_INT, BP_REAL, BP_STR, BP_DATA, BP_ARRAY, BP_DICT } BpType;
typedef struct BpNode {
    BpType type;
    int64_t i;
    double r;
    char *s;                 /* BP_STR (UTF-8), BP_DATA (raw) */
    size_t n;                /* bytes for STR/DATA, items for ARRAY/DICT */
    struct BpNode **items;   /* ARRAY values, DICT values */
    char **keys;             /* DICT keys */
} BpNode;

BpNode *bp_parse(const uint8_t *data, size_t len);
void bp_free(BpNode *n);
BpNode *bp_get(const BpNode *dict, const char *key);
double bp_num(const BpNode *n);

/* ---------------- atlas.c ---------------- */
typedef struct { char name[64]; SDL_Rect r; } Frame;
typedef struct {
    SDL_Texture *tex;
    int w, h;
    Frame *frames;
    int nframes;
} Atlas;

bool atlas_load(Atlas *a, SDL_Renderer *ren, const char *appdir, const char *base);
void atlas_free(Atlas *a);
const Frame *atlas_find(const Atlas *a, const char *name);
/* draw a named frame centred at (cx,cy) in logical coords */
void spr_draw(SDL_Renderer *ren, const Atlas *a, const char *name, float cx, float cy, float scale, Uint8 alpha);
void spr_draw_rect(SDL_Renderer *ren, const Atlas *a, const SDL_Rect *src, float cx, float cy, float scale, Uint8 alpha);
/* draw a frame clipped vertically: only the bottom 'frac' (0..1) is shown */
void spr_draw_fill(SDL_Renderer *ren, const Atlas *a, const char *name, float cx, float cy, float frac, Uint8 alpha);
float spr_width(const Atlas *a, const char *name);
/* Original Cloud3D/TPManager semantics: (x,y) is the TOP-LEFT corner, optional width/height stretch */
void spr_tl(SDL_Renderer *ren, const Atlas *a, const char *name, float x, float y, Uint8 alpha);
void spr_tl_wh(SDL_Renderer *ren, const Atlas *a, const char *name, float x, float y, float w, float h, Uint8 alpha);
void spr_tl_rect(SDL_Renderer *ren, const Atlas *a, const SDL_Rect *src, float x, float y, float w, float h, Uint8 alpha);
/* RenderTexRot: centred, rotated (degrees), scaled */
void spr_rot(SDL_Renderer *ren, const Atlas *a, const char *name, float cx, float cy, float deg, float scale, Uint8 alpha);
/* find a frame in any of the given atlases (TPManager has one global frame namespace) */
const Atlas *atlas_owner(const Atlas *const *list, int n, const char *name);

/* ---------------- usm.c ---------------- */
typedef struct { uint32_t time_ms; char *param; } Cue;
typedef struct { Cue *cues; int n; uint32_t time_unit; } CueList;
bool usm_read_cues(const char *path, CueList *out);
void cuelist_free(CueList *c);

/* ---------------- media.c ---------------- */
typedef struct { int16_t *pcm; size_t frames; } Pcm; /* interleaved stereo S16 @ AUDIO_RATE */
/* decode every audio stream of a media file in one demux pass (max 4) */
int media_decode_audio(const char *path, Pcm *out, int max_streams);
void pcm_free(Pcm *p);

typedef struct VideoStream VideoStream;
VideoStream *video_open(const char *path, SDL_Renderer *ren);
void video_update(VideoStream *v, double t);   /* advance to the frame for media time t */
SDL_Texture *video_texture(VideoStream *v);    /* NULL until the first frame */
void video_close(VideoStream *v);

/* ---------------- mixer.c ---------------- */
bool mixer_init(void);
void mixer_quit(void);
void mixer_set_master(float music, float sfx);
/* song: main = vocal track (gain follows tension), sub = backing track */
void mixer_music_start(const Pcm *main, const Pcm *sub);
void mixer_music_stop(void);
void mixer_music_pause(bool paused);
void mixer_music_gain(float main_gain, float sub_gain);
double mixer_music_time(void);     /* seconds of song audio heard so far */
double mixer_music_length(void);
bool mixer_music_playing(void);
int mixer_sfx_play(const Pcm *p, float gain, bool loop);
void mixer_sfx_stop(int voice);
void mixer_sfx_stop_all(void);

/* ---------------- assets ---------------- */
typedef struct {
    const char *appdir;
    Atlas game1, game2, effect1, ui1, ui2, ui5, bg_title, bg_select, bg_result;
    Atlas kana_tex[4];      /* mf_hiragana_blank, mf_hiragana_telop, mf_katakana_blank, mf_katakana_telop */
    Atlas logos[3];         /* logo_sega, logo_crypton, logo_cri */
    Atlas launch;
    Pcm sounds[NUM_SOUNDS];
} Assets;

bool assets_load(Assets *as, SDL_Renderer *ren, const GameData *gd, const char *appdir);
void assets_free(Assets *as);
void play_sound(Assets *as, int id);

/* ---------------- save.c ---------------- */
typedef struct { int score, rank, combo, cleared; } Record;
typedef struct {
    Record rec[MAX_SONGS][NUM_DIFF];
    char path[1024];
} SaveData;
void save_load(SaveData *s, const char *dir);
void save_write(const SaveData *s);

/* ---------------- options ---------------- */
typedef struct {
    int input_timing;        /* MusicData GetInputTiming (calibration steps) */
    float bgm_volume, se_volume;
    bool fail_sound;         /* StatusData IsDisableFailSound inverted */
    bool autoplay;           /* debug: perfect autoplay */
    bool autoplay_input;     /* debug: autoplay through synthetic keyboard/mouse events */
    int start_song, start_diff;   /* -1 = go through menus */
} Options;

/* ---------------- game.c ---------------- */
typedef struct Game Game;
typedef struct {
    int song, diff;
    int counts[6];           /* index = grade 1..5 (WORST..COOL) */
    int total_notes, max_combo, stage_score, combo_score, interlude_hits, total_interlude;
    bool game_over;
    int rank;                /* 0 PERFECT .. 5 C(not clear), 6 game over */
} GameResult;

Game *game_create(SDL_Renderer *ren, Assets *as, const GameData *gd, const Options *opt, int song, int diff);
void game_destroy(Game *g);
void game_event(Game *g, const SDL_Event *e);
bool game_update(Game *g);           /* false when finished */
void game_render(Game *g);
void game_result(const Game *g, GameResult *out);

/* ---------------- ui.c ---------------- */
void font_draw(SDL_Renderer *ren, const char *text, float x, float y, float scale, SDL_Color col, int align);
void bitmap_font_draw(SDL_Renderer *ren, const char *text, float x, float y, float scale, SDL_Color col, int align);
bool text_init(const char *font_path);   /* NULL = search; "none" = bitmap only */
void text_quit(void);
void digits_draw(SDL_Renderer *ren, const Atlas *a, const char *prefix, long value, float right_x, float cy,
                 float scale, bool commas, int comma_index);

typedef struct UiState UiState;
UiState *ui_create(SDL_Renderer *ren, Assets *as, const GameData *gd, SaveData *save, Options *opt);
void ui_destroy(UiState *u);
void ui_enter(UiState *u, int scene, const GameResult *res);
/* returns next scene id or -1 to stay; on SCENE_GAME sets song and diff */
int ui_event(UiState *u, const SDL_Event *e, int *song, int *diff);
int ui_update(UiState *u, int *song, int *diff);
void ui_render(UiState *u);

#endif
