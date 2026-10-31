/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: which game a folder is, from the files its store, its installer or a Steam stand-in left
 * in it, as scrapers mark ROMs by their database id: a Steam app id (steam_appid.txt, an
 * emulator's or crack's .ini, Steam's appmanifest_<id>.acf or installscript.vdf) and a GOG
 * product id with its name (goggame-<id>.info). With an id, covers, backgrounds and details
 * are the game's own, not a guess from its folder's name.
 *
 * What is found is kept with the game (prefixes/<prefix>/pc5/identity.txt), and the player can
 * give the Steam app id by hand (the web page), which then goes first. */
#ifndef PC5_IDENTITY_H
#define PC5_IDENTITY_H
#include "pc5.h"

typedef struct Pc5Identity {
    uint32_t steam_appid;      /* 0: none known */
    uint64_t gog_id;           /* 0: none known */
    char name[PC5_NAME];       /* the store's name for it (GOG's), or "" */
    char source[64];           /* the file it came from ("steam_appid.txt", "codex.ini"), or "you" */
} Pc5Identity;

/* The game's folder walked (down to 3 folders deep, names in any case) for the files above.
 * A steam_appid.txt goes before an .ini, an .ini before Steam's own manifests, the one
 * nearest the top first. 0 when something was found, -1 when nothing was. */
int pc5_identity_scan(const char *game_dir, Pc5Identity *id);

/* identity.txt: steam_appid=, gog_id=, name=, source= lines. read: 0, or -1 when there is none. */
int pc5_identity_read(const char *root, const char *prefix, Pc5Identity *id);
int pc5_identity_write(const char *root, const char *prefix, const Pc5Identity *id);
/* The player's Steam app id for the game, kept with source "you" (0: identity.txt goes, and
 * the folder is read again); the pictures and details PC5 fetched for the game are deleted, so they are
 * fetched again for the right game. 0, or -1. */
int pc5_identity_set_steam(const char *root, const char *prefix, uint32_t appid);

#endif
