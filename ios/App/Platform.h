#ifndef MF_IOS_PLATFORM_H
#define MF_IOS_PLATFORM_H

#include <SDL.h>
#include <stdbool.h>
#include <stddef.h>

bool ios_paths(char *files, size_t files_len, char *saves, size_t saves_len);
bool ios_attach_window(SDL_Window *window);
bool ios_import_screen(SDL_Window *window, const char *files_dir, bool update);   // update: outdated data, offers Not Now

#endif
