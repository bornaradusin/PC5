/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: games made for Steam, without Steam on the console. A game that uses Steam's library
 * (Valve's steam_api.dll or steam_api64.dll) quits when Steam doesn't answer; with the game's
 * Steam emulator on (its menu, Auto by default) PC5 puts gbe_fork's library in its place before
 * the game starts (the app's pc5/steam, unchanged), keeps Valve's beside it as
 * <name>.pc5-valve, and writes steam_settings/ for it: the game's app ID when the game names it
 * (steam_appid.txt), else the Steam ID PC5 knows for it (pc5_identity.h: its cover's, or the player's), the interfaces Valve's library has, a player called "Player", offline, no
 * DLC. Off puts Valve's back. A library that isn't Valve's (signed by Valve) is left alone.
 * Steam's DRM wrapper (SteamStub) is a different thing: a game's program wrapped in it needs
 * Steam itself, and PC5 says so rather than start it. */
#ifndef PC5_STEAM_H
#define PC5_STEAM_H
#include "pc5.h"

enum { PC5_STEAM_DLLS = 8 };

/* The game's steam_api DLLs (any case, a few folders deep), relative to game_dir with '/':
 * how many. Looked for once and kept in root/prefixes/<id>/pc5/steam.txt, and again when the
 * game's program (program) changes. */
int pc5_steam_find(const char *root, const char *id, const char *game_dir, const char *program,
                   char found[][PC5_PATH], int max);
/* Before the game starts: on (Auto), each of Valve's becomes the emulator; off, Valve's are
 * put back. How many changed, or -1 with why in error. app: the app's folder (pc5/steam). */
int pc5_steam_prepare(const char *root, const char *id, const char *game_dir, const char *program, const char *app,
                      int on, char *error, size_t size);
/* 1 when the program is wrapped in SteamStub: it starts in its section named .bind (one with the
 * wrapper taken off may keep the section, starting in its own code: not wrapped). */
int pc5_steam_stub(const char *program);
/* What a report needs to tell how the game meets Steam, as text into out (lines starting with
 * two spaces): the emulator's setting (on), whether the program is in SteamStub (stub), each
 * steam_api DLL and whose it is, the app ID the emulator has and where from, the DLC listed for
 * it, other Steam stand-ins' or fixes' files in the folder, and Steam's own install files. Taken
 * after pc5_steam_prepare, so it is what the game meets. 0, or -1 out of memory. */
int pc5_steam_check(const char *root, const char *id, const char *game_dir, const char *program, int on, int stub,
                    Pc5Buf *out);
/* The same written to root/prefixes/<id>/pc5/steam-check.txt (a report's about.txt shows it),
 * and its one-line summary into summary: 0, or -1. */
int pc5_steam_check_keep(const char *root, const char *id, const char *game_dir, const char *program, int on, int stub,
                         char *summary, size_t size);
#define PC5_STEAM_CHECK_FILE "steam-check.txt"
/* For the tests: the Steam interfaces named in a steam_api DLL's bytes, one a line, as
 * gbe_fork's generate_interfaces finds them. */
int pc5_steam_interfaces(const uint8_t *data, size_t size, Pc5Buf *out);

#endif
