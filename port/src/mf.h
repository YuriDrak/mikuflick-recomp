/*
 * Miku Flick/02 Linux/Android port - shared declarations.
 * Clean-room reimplementation; see docs/re_notes.md for the reverse-engineered
 * behaviour each module reproduces and docs/linux_port_plan.md for the design.
 */
#ifndef MF_H
#define MF_H

#include <SDL.h>
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
#define NUM_FLICK_DIRS 5          /* flick codes: 0 centre, 1 up, 2 right, 3 down, 4 left */
#define NUM_SOUNDS 30
#define AUDIO_RATE 44100

/* sound ids (SoundManager Play:) */
enum { SND_COOL_SP = 0, SND_HIT = 1, SND_SAD = 2, SND_INTERLUDE = 3, SND_DECIDE = 4, SND_CANCEL = 5,
       SND_SAFE = 18, SND_BGM01 = 22, SND_SEGA = 27, SND_TITLE = 28 };

/* scene ids (SceneManager) */
enum { SCENE_BOOT = 0, SCENE_TITLE = 1, SCENE_GAME = 2, SCENE_SELECT = 3, SCENE_PV = 4, SCENE_OPTION = 6,
       SCENE_RESULT = 8, SCENE_QUIT = 99 };
/* ending.c: SceneEnding (credits roll) and ScenePreEnding (ending movie) */
enum { SCENE_ENDING = 9, SCENE_PREENDING = 14 };
#define IS_ENDING_SCENE(s) ((s) == SCENE_ENDING || (s) == SCENE_PREENDING)
#define ENDING_LINES 76           /* s_tbl_Ending rows (WindowEnding init loop: 0x4c) */

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
    /* s_tbl_Ending: credits roll lines {text, font size, gap to the next line}; nending 0 when an
       older gamedata.txt lacks them (the OPTIONS -> CREDITS ENDING button is then hidden) */
    char ending_text[ENDING_LINES][96];
    int ending_size[ENDING_LINES], ending_gap[ENDING_LINES], nending;
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
/* stream payloads (data_type 0) of one chunk type, e.g. "@SFV" / "@SFA"; chno -1 = every channel */
typedef struct UsmStream UsmStream;
UsmStream *usm_stream_open(const char *path, const char *sig, int chno);
const uint8_t *usm_stream_next(UsmStream *s, size_t *len, int *chno); /* valid until the next call; NULL at end */
void usm_stream_close(UsmStream *s);
/* VIDEO_HDRINFO: frame rate (framerate_n/framerate_d) and intra DC precision in bits (mpeg_dcprec, 8..11) */
bool usm_video_info(const char *path, double *fps, int *dcprec);

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
int mixer_bgm_play(const Pcm *p, float gain, bool loop);   /* menu BGM / preview voice on the BGM master */
void mixer_set_bgm(float bgm);                              /* BGM master (Options.bgm_volume) */
void mixer_sfx_stop(int voice);
void mixer_sfx_stop_all(void);
void mixer_voice_gain(int voice, const Pcm *p, float gain);  /* if that voice still plays p */

/* ---------------- ipa.c ---------------- */
/* Import the user's own IPA (zip) open on fd: writes dir/MikuFlick2.app/ (the files the
   game loads) and dir/gamedata.txt. Progress frac 0..1 and the current file go to cb
   (called on the importing thread). On failure err holds a message for the player. */
typedef void (*IpaProgress)(void *ud, double frac, const char *file);
bool ipa_import(int fd, const char *dir, IpaProgress cb, void *ud, char *err, size_t errlen);

/* ---------------- view.c ---------------- */
/* Scenes draw into a 640x960 target between view_begin and view_present, which shows
   it centred and fills the bars of non-2:3 screens with a blurred backdrop (NULL = scene). */
bool view_init(SDL_Renderer *ren);
void view_quit(void);
bool view_active(void);              /* false: SDL logical-size fallback (no render targets) */
void view_begin(SDL_Renderer *ren);
void view_present(SDL_Renderer *ren, SDL_Texture *backdrop);
void view_window_to_logical(SDL_Renderer *ren, float wx, float wy, float *lx, float *ly); /* window points */
void view_capture_size(SDL_Renderer *ren, int *w, int *h);   /* size SDL_RenderReadPixels returns */

/* ---------------- assets ---------------- */
typedef struct {
    const char *appdir;
    Atlas game1, game2, effect1, ui1, ui2, ui3, ui4, ui5, bg_title, bg_select, bg_result, bg_options;
    Atlas game2_kb[2];      /* key faces for KEYBOARD option 1/2: game_tex_02_fullroman / _romanhira (frames of game_tex_02) */
    Atlas kana_tex[4];      /* mf_hiragana_blank, mf_hiragana_telop, mf_katakana_blank, mf_katakana_telop */
    Atlas kana_kb[2][2];    /* hiragana {blank, telop} for KEYBOARD option 1/2: mf_hiragana_*_fullroman / _romanhira (katakana has none) */
    Atlas logos[3];         /* logo_sega, logo_crypton, logo_cri */
    Atlas help[2], credits; /* help_01/02, credits_en */
    Atlas saisei;           /* PV select: play mark on the jacket */
    Atlas ui9;              /* UI_tex_09 (no plist): DISPLAY page aspect and pause previews */
    Atlas loading;          /* bg_tex_07: SceneManager RenderLoading (bg_loading, linemove, loadmiku_00/01) */
    Atlas launch;
    Pcm sounds[NUM_SOUNDS];
} Assets;

bool assets_load(Assets *as, SDL_Renderer *ren, const GameData *gd, const char *appdir);
void assets_free(Assets *as);
void play_sound(Assets *as, int id);

/* ---------------- options ---------------- */
typedef struct {
    int input_timing;        /* INPUT TIMING of the song being played (MusicData GetInputTiming, -10..+10) */
    float bgm_volume, se_volume;
    bool fail_sound;         /* StatusData IsDisableFailSound inverted */
    bool left_hand;          /* HANDEDNESS: tension gauge on the right */
    int keyboard;            /* KEYBOARD: 0 original, 1 full romaji, 2 romaji+hiragana */
    bool flower;             /* KEYBOARD: flick "flower" guide around a held key */
    bool guide_panel;        /* KEYBOARD: guide cursor/arrow on the next key */
    bool pause_button;       /* DISPLAY PAUSE: draw the in-game pause button (TouchPauseEnable) */
    /* PV viewer (ScenePVSelect / ScenePVView): set by the UI before starting playback */
    bool pv_mode, pv_lyrics, pv_karaoke, pv_loop, pv_shuffle;
    bool autoplay;           /* debug: perfect autoplay */
    bool autoplay_input;     /* debug: autoplay through synthetic keyboard/mouse events */
    int start_song, start_diff;   /* -1 = go through menus */
} Options;

/* ---------------- save.c ---------------- */
typedef struct { int score, rank, combo, cleared; } Record;
/* RECORDS (records.c): the rest of the original's per song x difficulty MusicData record, shown on the
   RECORDS screen. notes/flick_total are the chart's (every play), stage/combo_score/result/flick_success
   belong to the top score (replaced with it), interlude is a max, perfect a flag; stored = line present */
typedef struct {
    int notes, stage, combo_score, interlude, perfect, stored;
    int result[6];           /* TopNoteResult, index = grade as GameResult.counts (5 COOL .. 1 WORST) */
    int flick_total[NUM_KEYS][NUM_FLICK_DIRS];
    int flick_success[NUM_KEYS][NUM_FLICK_DIRS];
} RecordEx;
typedef struct {
    Record rec[MAX_SONGS][NUM_DIFF];
    RecordEx recx[MAX_SONGS][NUM_DIFF];   /* RECORDS */
    int timing[MAX_SONGS];   /* per-song INPUT TIMING (MusicData GetInputTiming), set in the pause menu */
    bool has_opt;            /* options stored in the save file */
    Options opt;             /* only the persistent fields are used */
    char path[1024];
} SaveData;
void save_load(SaveData *s, const char *dir);
void save_write(const SaveData *s);

/* ---------------- game.c ---------------- */
typedef struct Game Game;
typedef struct {
    int song, diff;
    int counts[6];           /* index = grade 1..5 (WORST..COOL) */
    int total_notes, max_combo, stage_score, combo_score, interlude_hits, total_interlude;
    bool game_over;
    int rank;                /* 0 PERFECT, 1..4 S..C, 5 D (not clear), 6 E (game over) */
    /* per key (0 あ .. 9 わ, KanaInfo.key) and flick direction (KanaInfo.dir: NUM_FLICK_DIRS codes
       0 centre/tap, 1 up, 2 right, 3 down, 4 left), for the FLICK RESULT page:
       flick_total   = playable notes of the chart (MikuFlickCriManager CheckTotalNoteNum @0x291dc ->
                       MusicData AddFlickResultNum), fixed when the song is loaded;
       flick_success = notes judged FINE or COOL (grade >= 4: NoteNormal checkResult @0x232a8 ->
                       StatusData AddTmpFlickSuccessNum) */
    int flick_total[NUM_KEYS][NUM_FLICK_DIRS];
    int flick_success[NUM_KEYS][NUM_FLICK_DIRS];
} GameResult;

/* ---------------- flickresult.c ---------------- */
/* result screen FLICK RESULT page: TouchResultChange toggle + TouchResultFlickBoard per key */
typedef struct {
    bool page;               /* WindowResult m_FlickResultMode: false score page, true flick page */
    bool held;               /* toggle button held by the touch that pressed it */
    float dn_x, dn_y, mv_x, mv_y;   /* that touch: press point, largest move per axis */
    int open;                /* board showing its direction guide (m_TouchDown), -1 none */
} FlickRes;
void flickres_reset(FlickRes *f);
bool flickres_event(FlickRes *f, Assets *as, const SDL_Event *e);   /* true: key used */
void flickres_render(SDL_Renderer *ren, const Assets *as, const GameData *gd, const Options *opt,
                     const GameResult *res, const FlickRes *f);
void flickres_render_button(SDL_Renderer *ren, const Assets *as, const FlickRes *f);
void flickres_demo(GameResult *res);     /* sample counters for --start-scene result */

Game *game_create(SDL_Renderer *ren, Assets *as, const GameData *gd, const Options *opt, int song, int diff);
void game_destroy(Game *g);
void game_event(Game *g, const SDL_Event *e);
void game_pause(Game *g);            /* open the pause menu (app sent to the background) */
SDL_Texture *game_backdrop(const Game *g);   /* song video for the screen bars, NULL before the first frame */
bool game_update(Game *g);           /* false when finished */
void game_render(Game *g);
void game_result(const Game *g, GameResult *out);

/* ---------------- pvview.c ---------------- */
/* WindowPVView over the game in pv_mode: swipes + arrow, pause button and menu, fade, next/previous PV */
enum { PVV_NONE, PVV_PAUSE, PVV_RESUME, PVV_RELOAD, PVV_LEAVE };
void pvview_begin(const char *movie_path, int song, int nsongs);   /* from game_create (new view or ReLoad) */
int pvview_event(Assets *as, const Options *opt, const SDL_Event *e, bool paused, double t, bool movie_end);
void pvview_render(SDL_Renderer *r, Assets *as, const Options *opt, bool paused, bool have_frame, const char *lyric);
/* after the PV game finished: song to ReLoad in place, or -1 for PV select (left = MUSIC SELECT) */
int pvview_next(const Options *opt, bool left);

/* ---------------- ui.c ---------------- */
void font_draw(SDL_Renderer *ren, const char *text, float x, float y, float scale, SDL_Color col, int align);
void font_draw_plain(SDL_Renderer *ren, const char *text, float x, float y, float scale, SDL_Color col, int align); /* no shadow */
void font_draw_plain_fit(SDL_Renderer *ren, const char *text, float x, float y, float scale, SDL_Color col, int align,
                         float max_w);   /* condensed to at most max_w px wide */
void bitmap_font_draw(SDL_Renderer *ren, const char *text, float x, float y, float scale, SDL_Color col, int align);
bool text_init(const char *font_path);   /* NULL = search; "none" = bitmap only */
bool text_has_glyph(Uint32 cp);           /* false: no TrueType font, or it lacks this code point */
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

/* ---------------- help.c (OPTIONS -> HELP, localized strings) ---------------- */
#define HELP_PAGES 10
const char *loc_string(const char *appdir, const char *key);   /* <lang>.lproj/Localizable.strings; NULL if absent */
void help_set_page(const char *appdir, int page);   /* WindowHelpText setPage: (scroll back to the top) */
bool help_event(const SDL_Event *e);                /* scroll touches; true for the keys/wheel it used */
void help_tick(void);                               /* 30 Hz exec: ScrollExec + clamp */
void help_render(SDL_Renderer *r, const Assets *as, int page, unsigned ticks);
void help_button_rect(int b, float *x, float *y, float *w, float *h);   /* TouchHelpPage 0 prev / 1 next */
bool help_button_shown(int page, int b);
void option_titles_render(SDL_Renderer *r, const char *appdir, const char *title, const char *desc_key,
                          const char *desc_fallback, const char *desc2_key);

/* ---------------- ending.c ---------------- */
typedef struct Ending Ending;
Ending *ending_create(SDL_Renderer *ren, Assets *as, const GameData *gd, const Options *opt);
void ending_destroy(Ending *e);
void ending_enter(Ending *e, int scene, int prev);   /* SCENE_ENDING / SCENE_PREENDING; prev = scene left */
void ending_leave(Ending *e);
/* frozen: a scene change is fading in (no exec). Returns the next scene or -1; SCENE_GAME sets song/diff */
int ending_update(Ending *e, bool frozen, int *song, int *diff);
void ending_render(Ending *e);
SDL_Texture *ending_backdrop(const Ending *e);       /* the movie, for the screen bars */

/* ---------------- records.c (RECORDS): title crown -> RESULTS (SceneScore) -> RECORDS (SceneRecord) */
#include <stdio.h>
enum { SCENE_SCORE = 5, SCENE_RECORD = 10 };
#define IS_RECORDS_SCENE(s) ((s) == SCENE_SCORE || (s) == SCENE_RECORD)
typedef struct Records Records;
Records *records_create(SDL_Renderer *ren, Assets *as, const GameData *gd, const Options *opt, const SaveData *save);
void records_destroy(Records *r);
void records_enter(Records *r, int scene, int prev);   /* SCENE_SCORE / SCENE_RECORD; prev = scene left */
void records_leave(Records *r, int next);
int records_event(Records *r, const SDL_Event *e);     /* next scene or -1 */
int records_update(Records *r);                        /* 30 Hz exec; -1 */
void records_render(Records *r);
int records_crown_event(Assets *as, const SDL_Event *e);   /* title menu: SCENE_SCORE or -1 */
void records_crown_render(SDL_Renderer *ren, const Atlas *ui1);
void records_store(SaveData *s, const GameResult *res);    /* before ui_enter(SCENE_RESULT) */
bool records_load_line(SaveData *s, const char *line);     /* save.c: recx / flk lines */
void records_save_lines(const SaveData *s, FILE *f);

#endif
