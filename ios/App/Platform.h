#ifndef MF_IOS_PLATFORM_H
#define MF_IOS_PLATFORM_H

#include <SDL.h>
#include <stdbool.h>
#include <stddef.h>

bool ios_paths(char *files, size_t files_len, char *saves, size_t saves_len);
bool ios_attach_window(SDL_Window *window);
/* NEEDED: first start; UPDATE: outdated data, offers Not Now; DLC: from the DLC page (an IPA with DLC),
   offers Cancel. Not Now / Cancel return false. */
enum { IOS_IMPORT_NEEDED, IOS_IMPORT_UPDATE, IOS_IMPORT_DLC };
bool ios_import_screen(SDL_Window *window, const char *files_dir, int mode);

/* Native DLC page (DlcViewController.m) over the game. The page reads and writes show_dlc; tick
   runs every frame while it is shown (main.c: refreshes the counts, saves show_dlc). Returns true
   if the player asked to import a whole IPA with DLC. */
typedef struct IosDlcPage {
    void *ud;
    bool show_dlc;
    int installed_packs, installed_songs;
    void (*tick)(struct IosDlcPage *page);
} IosDlcPage;
bool ios_dlc_screen(SDL_Window *window, IosDlcPage *page);

#endif
