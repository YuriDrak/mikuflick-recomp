/*
 * dlc - list the downloaded (DLC) songs recorded in the original game's save,
 * Documents/MikuFlick2.dat, as "dlc" lines for gamedata.txt. The save is the only
 * copy of the DLC song table left (the shop list came from SEGA's server, now
 * gone); IPAs that bundle the packs (iMazing-style Container/) ship it next to
 * Library/InstallData/Mov_<pack>/. Shared by tools/mfextract and the in-app IPA
 * importer (ipa.c). Plain C, no SDL. Format notes: docs/re_notes.md §5.14.
 */
#ifndef DLC_H
#define DLC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* True if the flattened game data folder has this file (e.g. "gemini.usm"). */
typedef bool (*DlcHaveFile)(void *ud, const char *name);

/* One DLC song of the save (MusicData). File stems are checked to be plain names. */
typedef struct {
    int id, pack, art_index, title_break, bpm, stars[4];
    char preview[64];   /* "" = no preview */
    char movie[64], atlas[64];
    char title[160], artist[160];
    char pack_name[48]; /* MusicPack m_PackName, e.g. "Duet_Pack01" */
} DlcSong;

/* Every installed DLC song of the save (pack != 0, not pre-installed, unique m_Index), in pack order,
   whether or not its files are at hand. *out is malloc'd. Returns the count or -1 with err set. */
int dlc_read_songs(const uint8_t *save, size_t len, DlcSong **out, char *err, size_t errlen);

/* gamedata.txt line:
     dlc <id> <pack> <art_index> <title_break> <preview> <movie> <art_atlas> <bpm> <e> <n> <h> <x>\t<title>\t<artist> */
void dlc_print_song(const DlcSong *s, FILE *out);

/* movie (.usm) and art sheet (.png) present - what the game needs to list the song */
bool dlc_song_files_present(const DlcSong *s, DlcHaveFile have, void *ud);

/* Writes the line of every installed DLC song whose movie and art sheet are present.
   Returns the number of songs written, or -1 with a reason in err (not a MikuFlick2 save, damaged...). */
int dlc_write_songs(const uint8_t *save, size_t len, DlcHaveFile have, void *ud, FILE *out, char *err, size_t errlen);

/* Installed pack file the importer and setup flatten into the app folder:
   "Container/Library/InstallData/Mov_<n>/<name>.{usm,adx,png}". Returns <name> or NULL. */
const char *dlc_pack_file(const char *zip_path);

/* <n> of "Container/Library/InstallData/Mov_<n>/...", or -1 */
int dlc_pack_id(const char *zip_path);

/* A Latin-script name for a song title, for players who don't read Japanese: its English title
   (dlc_known_songs, by movie name), else the movie name the game itself uses, spaced and
   capitalised ("mikumiku_ni_siteyannyo" -> "Mikumiku ni Siteyannyo"). "" when the title is in
   Latin script already. */
void dlc_song_latin(const char *movie, const char *title, char *out, size_t cap);

/* The DLC packs the game offered (titles only), in pack order; en = English title or NULL */
typedef struct {
    int pack;
    const char *pack_name, *movie, *title, *en;
} DlcKnownSong;
const DlcKnownSong *dlc_known_songs(int *n);

#define DLC_SAVE_PATH "Container/Documents/MikuFlick2.dat"

#endif
