/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: the games, kept the way prospero-win keeps them, and nothing else:
 *
 *   <root>/profiles/<id>.profile         what prospero-win starts
 *   <root>/profiles/profiles.lst         its index, when there is one
 *   <root>/prefixes/<id>/                the game's own Windows (its C: drive
 *                                        and registry, its saves and settings)
 *   <root>/prefixes/<id>/drive_c/Games/<folder>/   the one copy of the game
 *   <root>/prefixes/<id>/pc5/            PC5's notes on it and its cover
 *
 * <root> is /data/prospero-win. Installing builds the game's folder as
 * prefixes/.installing-<id> and renames it into place when everything is
 * copied and checked; uninstalling renames it to prefixes/.removing-<id>
 * first, so an install or uninstall cut short is finished (by deleting) the
 * next time PC5 starts. */
#ifndef PC5_LIBRARY_H
#define PC5_LIBRARY_H
#include "pc5.h"
#include "pc5_controls.h"
#include "pc5_pe.h"
#include "pc5_source.h"

/* PC5's notes on a game it installed (prefixes/<id>/pc5/info.txt). */
typedef struct Pc5Info {
    char title[PC5_NAME];
    char folder[PC5_NAME];       /* under C:\Games */
    char exe[PC5_PATH];          /* inside the folder, with '/' */
    int bits;
    Pc5Api api;
    uint64_t bytes;
    uint32_t files;
    char cover[64];              /* where the cover came from */
} Pc5Info;

typedef struct Pc5Game {
    char id[PC5_ID];
    char file[PC5_ID + 16];      /* its profile's file name in profiles/ */
    char title[PC5_NAME];
    char prefix[PC5_ID];         /* the profile's prefix */
    char executable[PC5_PATH];   /* the profile's program, C:\\... */
    char detail[128];            /* "64-bit · DirectX 11 · 23.4 GB" */
    int pc5;                     /* installed by PC5 (has its notes) */
    int bits;                    /* 32 or 64 */
    int playable;                /* the profile is valid and its prefix is there */
    char problem[128];           /* why not, when it isn't */
} Pc5Game;

typedef struct Pc5Library {
    Pc5Game games[PC5_MAX_GAMES];
    int count;                   /* sorted by title */
    int leftovers;               /* .installing- and .removing- folders to finish */
} Pc5Library;

/* Every game prospero-win would offer from root, plus PC5's notes. */
int pc5_library_load(const char *root, Pc5Library *library);
int pc5_library_find(const Pc5Library *library, const char *id);

int pc5_info_read(const char *root, const char *id, Pc5Info *info);
int pc5_info_text(const Pc5Info *info, char *out, size_t size);
/* The profile prospero-win reads for a PC5 game; arguments may be "". */
int pc5_profile_text(const char *id, const Pc5Info *info, const char *arguments, const Pc5Controls *controls, char *out,
                     size_t size);
/* Writes the game's profile again, with the launch options in launch.txt
 * beside its program (so options can be changed without reinstalling). */
int pc5_profile_refresh(const char *root, const char *id, char *error, size_t size);
/* The folder PC5 copied a game into (prefixes/<id>/drive_c/Games/<folder>). */
int pc5_game_folder(const char *root, const char *id, char *out, size_t size);
/* Makes a PC5 game start another of its programs (folder: its copy, read with
 * pc5_folder_inspect), with what that program draws with. */
int pc5_game_set_program(const char *root, const char *id, const Pc5Folder *folder, int exe, char *error, size_t size);
/* A new profile id from a name: lower-case letters, digits and '-', not used
 * by any profile or prefix under root. */
int pc5_new_id(const char *root, const char *name, char *id, size_t size);
/* "64-bit · DirectX 11 · 23.4 GB" */
void pc5_detail_text(int bits, Pc5Api api, uint64_t bytes, char *out, size_t size);

/* ---- installing ----------------------------------------------------------------- */

typedef enum Pc5InstallPhase {
    PC5_INSTALL_PREFIX,     /* the game's own Windows, from the app's template */
    PC5_INSTALL_LIST,       /* the game's files, listed */
    PC5_INSTALL_COPY,       /* copied, each file's SHA-256 taken as it is read */
    PC5_INSTALL_VERIFY,     /* each copy read back and its SHA-256 compared */
    PC5_INSTALL_FINISH,     /* notes, cover, renamed into place, profile */
    PC5_INSTALL_CLEANUP,    /* cancelled or failed: what was copied is deleted */
    PC5_INSTALL_DONE,
    PC5_INSTALL_FAILED,
    PC5_INSTALL_CANCELLED,
} Pc5InstallPhase;

typedef struct Pc5Remove Pc5Remove;

typedef struct Pc5Install {
    Pc5InstallPhase phase;
    char root[PC5_PATH], template_prefix[PC5_PATH], source[PC5_PATH];
    char id[PC5_ID], staging[PC5_PATH];
    Pc5Info info;
    Pc5Image cover;          /* owned once begun */
    uint32_t tint;
    Pc5Buf original;         /* the downloaded picture, kept with the game: set after begin */
    /* progress */
    uint64_t bytes_total, bytes_done;
    uint32_t files_total, files_done;
    char error[PC5_ERROR];
    int cancel;
    /* inside */
    Pc5Buf template_list, list;   /* NUL-separated relative paths, folders end in '/' */
    size_t at;                    /* the next path in the list being worked on */
    uint8_t (*hashes)[32];
    uint32_t file_index;
    int in_fd, out_fd;
    uint64_t file_left;
    Pc5Sha256 sha;
    uint8_t *chunk;
    Pc5Remove *cleanup;
    int cancelled;
} Pc5Install;

/* Checks the space and starts. -1 (with error set) when it can't begin. The
 * cover is taken over. */
int pc5_install_begin(Pc5Install *install, const char *root, const char *template_prefix,
                      const Pc5Folder *folder, int exe, Pc5Image *cover, uint32_t tint, const char *cover_source);
/* Works for about budget_ms. Returns the phase reached. */
Pc5InstallPhase pc5_install_step(Pc5Install *install, int budget_ms);
void pc5_install_cancel(Pc5Install *install);
void pc5_install_end(Pc5Install *install);
/* Bytes an install of folder needs: the game, its Windows, some room. */
uint64_t pc5_install_needs(const Pc5Folder *folder);

/* ---- uninstalling ------------------------------------------------------------------- */

struct Pc5Remove {
    char root[PC5_PATH], path[PC5_PATH];
    uint32_t removed;
    int done, failed;
    char error[PC5_ERROR];
    /* the folders still to empty: a stack of paths */
    char (*stack)[PC5_PATH];
    int top;
};

/* Takes the game out of the library at once (its profile, its line in the
 * index, its logs) and moves its folder aside; the folder is then deleted
 * step by step. Saves are in that folder, so they go with it. A game whose
 * prefix another profile shares, or the shared default prefix, keeps it. */
int pc5_remove_begin(Pc5Remove *remove, const char *root, const Pc5Game *game, const Pc5Library *library);
/* Deletes for about budget_ms; 1 while there is more. */
int pc5_remove_step(Pc5Remove *remove, int budget_ms);
void pc5_remove_end(Pc5Remove *remove);
/* Starts deleting a folder (and everything in it, links not followed). */
int pc5_tree_delete_begin(Pc5Remove *remove, const char *path);

/* Finishes what a cut-short install or uninstall left: 1 while there is more
 * (call again), 0 when nothing is left. Also writes back the profile of a
 * PC5 game that has its folder but lost its profile. */
int pc5_recover_step(const char *root, Pc5Remove *remove, int budget_ms);

/* Time since some fixed point, in milliseconds (the steps' clock). */
uint64_t pc5_now_ms(void);

#endif
