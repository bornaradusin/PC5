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
    /* Played from a USB drive, not copied: its folder on the drive, from the drive's top
     * ("Games/Neon Drift"), on whichever drive it is plugged into; "" for a game copied. */
    char usb[PC5_PATH];
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
    int vulkan;                  /* drawn through Vulkan (Direct3D, Vulkan, OpenGL), so Resolution applies */
    int api;                     /* what it draws with (Pc5Api), for its Graphics API; PC5_API_GDI when not PC5's */
    int playable;                /* the profile is valid and its prefix is there */
    int usb;                     /* played from a USB drive (Pc5Info.usb) */
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
/* The profile prospero-win reads for a PC5 game; arguments may be "", own_dlls NULL or
 * "" (pc5_own_dlls: the game's own Microsoft DLLs, which then go first). */
int pc5_profile_text(const char *id, const Pc5Info *info, const char *arguments, const Pc5Controls *controls,
                     const char *own_dlls, char *out, size_t size);
/* The same, with the working folder working_directory (a Windows path) in place of the
 * program's own when it isn't NULL. */
int pc5_profile_text_at(const char *id, const Pc5Info *info, const char *arguments, const Pc5Controls *controls,
                        const char *own_dlls, const char *working_directory, char *out, size_t size);
/* The Microsoft DLLs (Visual C++ runtimes, Direct3D compilers, D3DX) the game's program
 * folder holds, lower case and without .dll, "," between them, in name order, as many as
 * fit; how many were found. */
int pc5_own_dlls(const char *folder, char *out, size_t size);
/* Writes the game's profile again, with the launch options in launch.txt
 * beside its program (so options can be changed without reinstalling). */
int pc5_profile_refresh(const char *root, const char *id, char *error, size_t size);
/* A game's launcher starting the game, a second program, which a PS5 title can't have:
 * Wine writes that program to a file instead (its path, command line and working folder,
 * a line each) and ends (Wine patch 0923). pc5_handoff_env: the file
 * (root/prefixes/<id>/pc5/handoff.txt) and the game's folder as Windows names it
 * (C:\Games\<folder>\), which Wine gets as PW_HANDOFF and PW_HANDOFF_DIR; 0, or -1 when
 * they don't fit. pc5_handoff_take: that file, taken: the profile written for that program,
 * with its own bits and what it draws with, its arguments and its working folder, for the
 * game's next start (an ordinary start
 * writes it for the game's own program again). 1 when it was written, 0 when there was
 * none, -1 when it was refused (why in error): a program outside the game's folder or not
 * there, or a fourth hand-off in a row. */
/* A Java game made with jpackage: its app folder as Windows names it (C:\Games\<folder>\app
 * beside the program) for WINEPATH, so its program doesn't start itself again to put it in
 * PATH; 0, or -1 (out "") when the game's program isn't such a one. */
int pc5_game_winepath(const char *root, const char *id, char *out, size_t size);
int pc5_handoff_env(const char *root, const char *id, char *file, size_t file_size, char *dir, size_t dir_size);
int pc5_handoff_take(const char *root, const char *id, char *error, size_t size);
/* The program a PC5 game starts, on the console: its program, or the one its batch file
 * (.bat or .cmd) starts. 0, or -1 when there is none. */
int pc5_game_program(const char *root, const char *id, char *out, size_t size);
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

/* ---- games played from a USB drive ----------------------------------------------
 * A game installed to play from its USB drive keeps its files there: only its Windows is
 * on the console. Its folder in that Windows, C:\Games\<folder>, is a link to the folder on
 * the drive (a line of drive_c/Games/.pw-symlinks, which Wine on the console follows, as
 * it does dosdevices' links), written again before each start for the drive it is in now. */

/* The drives looked at, NULL-terminated: by default /mnt/usb0 to /mnt/usb7. NULL: those. */
void pc5_usb_roots(const char *const *roots);
/* A path on a USB drive from the drive's top ("Games/Neon Drift"). 0, or -1 when path isn't
 * on one. */
int pc5_usb_relative(const char *path, char *out, size_t size);
/* Where a game played from a USB drive is now: its folder on the drive plugged in that has
 * it, with its program. 0, or -1 when no drive has it (or the game isn't played from one). */
int pc5_usb_game_dir(const Pc5Info *info, char *out, size_t size);
/* The game's link to its folder on the drive, for the drive it is in now. 0, or -1 with a
 * sentence in error when no drive has it, or the link can't be written. */
int pc5_usb_link(const char *root, const char *id, const Pc5Info *info, char *error, size_t size);

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
    /* Where the chosen cover's background is (its source's, pc5_cover.h), kept with the game
     * for pc5_backgrounds.h: set after begin, "" and 0 for none. */
    char background[PC5_PATH];
    uint32_t background_appid;
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
    int tidying;                  /* done: deleting what is left over (CLEANUP, then DONE) */
    /* pc5_install_use_saves: the kept saves the game's Windows is made from, or "" */
    char kept[PC5_PATH];
    /* pc5_update_begin: a newer copy over an installed game's folder (game_dir); files of
     * the copy that couldn't be put in place (the first one's why in error) */
    int update, moving;
    char game_dir[PC5_PATH];
    uint32_t skipped;
    /* pc5_install_begin_usb: the game played from its USB drive, nothing copied */
    int in_place;
} Pc5Install;

/* Checks the space and starts. -1 (with error set) when it can't begin. The
 * cover is taken over. */
int pc5_install_begin(Pc5Install *install, const char *root, const char *template_prefix,
                      const Pc5Folder *folder, int exe, Pc5Image *cover, uint32_t tint, const char *cover_source);
/* The same for a game played from its USB drive (folder on one): only its Windows is made
 * on the console, and its folder there is a link to the drive's (pc5_usb_link). */
int pc5_install_begin_usb(Pc5Install *install, const char *root, const char *template_prefix,
                          const Pc5Folder *folder, int exe, Pc5Image *cover, uint32_t tint, const char *cover_source);
/* Bytes an install of folder to play from its USB drive needs on the console: its Windows
 * and some room. */
uint64_t pc5_install_needs_usb(const Pc5Folder *folder);
/* Works for about budget_ms. Returns the phase reached. */
Pc5InstallPhase pc5_install_step(Pc5Install *install, int budget_ms);
void pc5_install_cancel(Pc5Install *install);
void pc5_install_end(Pc5Install *install);
/* Bytes an install of folder needs: the game, its Windows, some room. */
uint64_t pc5_install_needs(const Pc5Folder *folder);

/* ---- kept saves ----------------------------------------------------------------------
 * Uninstalling a PC5 game can keep its saves: its own Windows (its saves and settings
 * there, its registry, its options in pc5/controls.txt) without the game, as
 * <root>/saves/<id>/, with the files the game made in its own folder (those not in
 * pc5/files.txt, the list of the files PC5 copied) as pc5-game/. Installing the game
 * again (the same folder name and program) can make its Windows from them instead of
 * the template, and put pc5-game/ back in its folder; they are deleted once it has. */
typedef struct Pc5Saves {
    char name[PC5_ID];           /* saves/<name> */
    char title[PC5_NAME], folder[PC5_NAME], exe[PC5_PATH];
    int64_t kept;                /* when (Unix time), or 0 */
} Pc5Saves;

/* The kept saves, by title, the newest first; how many (up to max). */
int pc5_saves_list(const char *root, Pc5Saves *saves, int max);
/* The kept saves of a game with this folder name and program (any case; exe NULL: any):
 * how many there are, and the newest one's path in out. 0 when there are none. */
int pc5_saves_find(const char *root, const char *folder, const char *exe, char *out, size_t size);
/* Deletes the kept saves saves/<name>: moved out of saves/ at once, then deleted step by step
 * (pc5_remove_step). */
int pc5_saves_delete_begin(Pc5Remove *remove, const char *root, const char *name);
/* Right after pc5_install_begin: the game's Windows is made from the kept saves at path
 * (pc5_saves_find) instead of the template. -1 (with install->error) when they can't be read. */
int pc5_install_use_saves(Pc5Install *install, const char *path);

/* ---- updating a game -----------------------------------------------------------------
 * A newer copy of a PC5 game (folder, on the USB drive) over its folder on the console:
 * the files the copy has replace the game's (in the case the game's have), the files only
 * the game's folder has stay (saves and settings some games keep there, launch.txt), and
 * its Windows, cover, tile and options are kept. The copy is made and checked in
 * prefixes/.updating-<id>/files first, as an install is; its notes written there mark it
 * whole, and its files are then moved into place. An update cut short before that is
 * deleted at the next start, one cut short after it is finished then (a file that can't be
 * put in place is left out, so it always finishes). The copy must have the game's program.
 * pc5/files.txt gets the copy's files too. Progress and the steps are an install's
 * (pc5_install_step). */
int pc5_update_begin(Pc5Install *install, const char *root, const char *id, const Pc5Folder *folder);
/* Bytes an update with folder needs: the copy and some room. */
uint64_t pc5_update_needs(const Pc5Folder *folder);

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
/* The same, keeping the game's saves (pc5_saves_list): its Windows goes to saves/, the
 * game's folder is deleted. Only for a PC5 game with a Windows of its own
 * (pc5_remove_can_keep). */
int pc5_remove_keep_saves(Pc5Remove *remove, const char *root, const Pc5Game *game, const Pc5Library *library);
int pc5_remove_can_keep(const Pc5Game *game, const Pc5Library *library);
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
