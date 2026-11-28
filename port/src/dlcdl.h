/*
 * dlcdl - background DLC pack downloads (dlcdl.c) and the pack info they show.
 * Plain C, no SDL: also used by the native iOS DLC page (ios/App/DlcViewController.m).
 */
#ifndef DLCDL_H
#define DLCDL_H

#include <stdbool.h>
#include <stdint.h>

#define DLC_MAX_PACKS 64
#define DLC_PACK_SONGS 6   /* songs listed per pack (the IPA packs have 3) */
typedef struct {
    char title[96];        /* as in the game: "カンタレラ" */
    char latin[96];        /* English title, else the game's romanized name (dlc_song_latin); "" if title is Latin */
} DlcPackSong;
typedef struct {
    int pack;              /* Mov_<pack> */
    char name[48];         /* "Duet Pack" */
    int nsongs;            /* songs[0 .. min(nsongs, DLC_PACK_SONGS)) */
    uint64_t bytes;        /* what an install downloads */
    bool available;        /* in the linked IPA (can be downloaded); the other known packs are listed greyed */
    DlcPackSong songs[DLC_PACK_SONGS];
} DlcPackInfo;

/* One worker thread; the UI polls dlcdl_status. */
enum { DLD_IDLE, DLD_LISTING, DLD_READY, DLD_INSTALLING, DLD_FAILED };
typedef struct {
    int state;
    double frac;           /* DLD_INSTALLING */
    int busy_pack;         /* pack being installed */
    char msg[200];         /* error or last result */
    bool error;            /* msg is an error */
    int npacks;
    DlcPackInfo packs[DLC_MAX_PACKS];
} DlcDlStatus;
void dlcdl_init(const char *app_dir, const char *gamedata);
bool dlcdl_list(const char *url);      /* reads the pack list of the IPA at url */
bool dlcdl_install(int pack);          /* after DLD_READY */
void dlcdl_cancel(void);
void dlcdl_status(DlcDlStatus *out);
bool dlcdl_take_changed(void);         /* true once after an install changed gamedata.txt */
void dlcdl_quit(void);

/* an http(s) link the downloader accepts (net.c) */
bool net_url_ok(const char *url);

#endif
