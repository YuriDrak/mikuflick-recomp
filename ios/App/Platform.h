#ifndef MF_IOS_PLATFORM_H
#define MF_IOS_PLATFORM_H

#include <SDL.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool ios_paths(char *files, size_t files_len, char *saves, size_t saves_len);
bool ios_attach_window(SDL_Window *window);
/* NEEDED: first start; UPDATE: outdated data, offers Not Now; DLC: from the DLC page (an IPA with DLC),
   offers Cancel. Not Now / Cancel return false. */
enum { IOS_IMPORT_NEEDED, IOS_IMPORT_UPDATE, IOS_IMPORT_DLC };
bool ios_import_screen(SDL_Window *window, const char *files_dir, int mode);

/* Native DLC page (DlcViewController.m) over the game. The page reads and writes show_dlc / url;
   tick runs every frame while it is shown (main.c: reloads the song list after a pack install,
   refreshes the counts, saves show_dlc / url). Returns true if the player asked to import a
   whole IPA with DLC. */
typedef struct IosDlcPage {
    void *ud;
    bool show_dlc;
    char url[512];
    int installed_packs, installed_songs;
    int (*pack_songs)(void *ud, int pack);   /* installed songs of a pack */
    void (*tick)(struct IosDlcPage *page);
} IosDlcPage;
bool ios_dlc_screen(SDL_Window *window, IosDlcPage *page);
int ios_http_range(const char *url, uint64_t off, size_t len, uint8_t *dst, uint64_t *total, char *final_url,
                   size_t final_cap, char *err, size_t errlen);   /* DLC downloads (net.c) */

#endif
