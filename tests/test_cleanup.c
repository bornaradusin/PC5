/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Game maintenance › Clean up (pc5_cleanup_scan) and the player's own pictures going with an
 * uninstalled game (pc5_own_pictures_delete): what goes, and that nothing of an installed game
 * does. tests/test_cleanup WORK */
#include "pc5.h"
#include "pc5_library.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures, checks;
static char root[512];

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

/* root/relative, its folders made, with text in it. */
static void put(const char *relative, const char *text)
{
    char path[PC5_PATH], dir[PC5_PATH];
    snprintf(path, sizeof(path), "%s/%s", root, relative);
    pc5_copy(dir, sizeof(dir), path);
    *strrchr(dir, '/') = 0;
    if (pc5_mkdirs(dir) || pc5_write_file(path, text, strlen(text))) { fprintf(stderr, "setup: %s\n", path); exit(2); }
}

static int there(const char *relative)
{
    char path[PC5_PATH];
    struct stat st;
    snprintf(path, sizeof(path), "%s/%s", root, relative);
    return !lstat(path, &st);
}

static Pc5Game *add_game(Pc5Library *library, const char *id, const char *title, const char *folder)
{
    Pc5Game *game = &library->games[library->count++];
    char info[512], relative[PC5_PATH];
    pc5_copy(game->id, sizeof(game->id), id);
    pc5_copy(game->prefix, sizeof(game->prefix), id);
    pc5_copy(game->title, sizeof(game->title), title);
    snprintf(game->file, sizeof(game->file), "%s.profile", id);
    if (folder) {
        game->pc5 = 1;
        snprintf(info, sizeof(info), "title = %s\nfolder = %s\nexe = game.exe\nbits = 64\n", title, folder);
        snprintf(relative, sizeof(relative), "prefixes/%s/pc5/info.txt", id);
        put(relative, info);
        snprintf(relative, sizeof(relative), "prefixes/%s/drive_c/Games/%s/game.exe", id, folder);
        put(relative, "MZ the game");
    }
    snprintf(relative, sizeof(relative), "profiles/%s.profile", id);
    put(relative, "profile");
    return game;
}

static void clean(Pc5Library *library, Pc5Cleanup *cleanup)
{
    int steps = 0;
    while (pc5_cleanup_step(cleanup, 5) && steps < 100000) steps++;
    CHECK(steps < 100000, "cleaning up ends");
    pc5_cleanup_end(cleanup);
    (void)library;
}

int main(int argc, char **argv)
{
    Pc5Library *library = pc5_alloc(sizeof(*library));
    Pc5Cleanup cleanup;
    Pc5Remove remove = { 0 };
    char link_path[PC5_PATH];

    if (argc != 2 || !library) { fprintf(stderr, "usage: test_cleanup WORK\n"); return 2; }
    snprintf(root, sizeof(root), "%s/cleanup/data/prospero-win", argv[1]);
    {
        char command[PC5_PATH + 16];
        snprintf(command, sizeof(command), "rm -rf '%s'", root);
        if (system(command)) return 2;
    }

    /* An installed game, with junk in its Temp folders and everything else that is its own. */
    add_game(library, "neon-drift", "Neon Drift", "Neon Drift");
    put("prefixes/neon-drift/drive_c/Games/Neon Drift/save.dat", "a save beside the game");
    put("prefixes/neon-drift/drive_c/users/prospero/AppData/Local/Temp/setup.tmp", "0123456789");
    put("prefixes/neon-drift/drive_c/users/prospero/AppData/Local/Temp/unpacked/deep/file.bin", "0123456789");
    put("prefixes/neon-drift/drive_c/users/prospero/Temp/old.tmp", "01234");
    put("prefixes/neon-drift/drive_c/windows/temp/vcredist.log", "01234");
    put("prefixes/neon-drift/drive_c/users/prospero/AppData/Local/Neon Drift/settings.ini", "the game's settings");
    put("prefixes/neon-drift/drive_c/users/prospero/Documents/My Games/Neon Drift/slot1.sav", "a save");
    put("prefixes/neon-drift/drive_c/windows/system32/kernel32.dll", "Windows");
    put("prefixes/neon-drift/pc5/cover.bin", "PC5's cover");
    put("prefixes/neon-drift/pc5-cache/radv/cache.bin", "shaders");
    put("prefixes/neon-drift/user.reg", "registry");
    /* A link in its Temp folder to the game's own folder: the link goes, never what it points to. */
    snprintf(link_path, sizeof(link_path), "%s/prefixes/neon-drift/drive_c/users/prospero/AppData/Local/Temp/to-game", root);
    CHECK(!symlink("../../../../../Games/Neon Drift", link_path), "a link made: %s", strerror(errno));
    /* A game with nothing in its Temp folder. */
    add_game(library, "tidy-game", "Tidy Game", "Tidy");
    {
        char path[PC5_PATH];
        snprintf(path, sizeof(path), "%s/prefixes/tidy-game/drive_c/users/prospero/AppData/Local/Temp", root);
        CHECK(!pc5_mkdirs(path), "an empty Temp folder");
    }
    /* An install that is being worked on, and kept saves: never touched. */
    put("prefixes/.installing-new-game/drive_c/users/prospero/AppData/Local/Temp/part.tmp", "being installed");
    put("prefixes/.taken-held-game/drive_c/Games/Held/game.exe", "a game held aside");
    put("saves/kept-game/drive_c/users/prospero/AppData/Local/Temp/kept.tmp", "in kept saves");
    put("saves/kept-game/drive_c/users/prospero/Documents/slot.sav", "kept save");
    put("saves/kept-game/pc5/info.txt", "title = Kept Game\nfolder = KeptGame\nexe = kept.exe\n");
    /* The player's own pictures: of installed games (stay), of a game that's gone (go), and the rest. */
    put("covers/Neon Drift.jpg", "cover");
    put("covers/TIDY.PNG", "cover by its folder's name, another case");
    put("covers/Gone Game.png", "gone");
    put("covers/kept-game.png", "a game whose saves are kept");
    put("covers/notes.txt", "not a picture");
    put("covers/.hidden.jpg", "hidden");
    put("backgrounds/neon-drift.png", "by id");
    put("backgrounds/Gone Game.jpg", "gone");
    put("loading/loading.jpg", "every game's");
    put("loading/Gone Game.jpeg", "gone");
    put("sounds/Gone Game.wav", "a startup sound");
    put("pc5/settings.txt", "settings");
    put("api_keys.txt", "STEAMGRIDDB_API_KEY=");

    CHECK(!pc5_cleanup_scan(root, library, &cleanup), "scanned");
    CHECK(cleanup.temp_games == 1, "one game with something in its Temp folders: %d", cleanup.temp_games);
    CHECK(cleanup.temp_bytes == 30, "its Temp folders' bytes, the link's target not counted: %llu",
          (unsigned long long)cleanup.temp_bytes);
    CHECK(cleanup.pictures == 3, "three own pictures of no installed game: %d (%s)", cleanup.pictures, cleanup.picture_names);
    CHECK(strstr(cleanup.picture_names, "Gone Game.png") && !strstr(cleanup.picture_names, "Neon"),
          "named for the question: %s", cleanup.picture_names);
    CHECK(cleanup.bytes == cleanup.temp_bytes + cleanup.picture_bytes && cleanup.picture_bytes == 12, "the total: %llu",
          (unsigned long long)cleanup.bytes);
    clean(library, &cleanup);
    CHECK(!cleanup.failed, "nothing failed");

    CHECK(!there("prefixes/neon-drift/drive_c/users/prospero/AppData/Local/Temp/setup.tmp") &&
          !there("prefixes/neon-drift/drive_c/users/prospero/AppData/Local/Temp/unpacked") &&
          !there("prefixes/neon-drift/drive_c/users/prospero/Temp/old.tmp") &&
          !there("prefixes/neon-drift/drive_c/windows/temp/vcredist.log") &&
          !there("prefixes/neon-drift/drive_c/users/prospero/AppData/Local/Temp/to-game"), "the Temp folders emptied");
    CHECK(there("prefixes/neon-drift/drive_c/users/prospero/AppData/Local/Temp") &&
          there("prefixes/neon-drift/drive_c/windows/temp"), "the Temp folders themselves kept");
    CHECK(there("prefixes/neon-drift/drive_c/Games/Neon Drift/game.exe") &&
          there("prefixes/neon-drift/drive_c/Games/Neon Drift/save.dat"), "the game's files and a save beside them kept");
    CHECK(there("prefixes/neon-drift/drive_c/users/prospero/AppData/Local/Neon Drift/settings.ini") &&
          there("prefixes/neon-drift/drive_c/users/prospero/Documents/My Games/Neon Drift/slot1.sav"),
          "its settings and saves kept");
    CHECK(there("prefixes/neon-drift/drive_c/windows/system32/kernel32.dll") && there("prefixes/neon-drift/user.reg") &&
          there("prefixes/neon-drift/pc5/cover.bin") && there("prefixes/neon-drift/pc5-cache/radv/cache.bin") &&
          there("prefixes/neon-drift/pc5/info.txt") && there("profiles/neon-drift.profile"),
          "its Windows, registry, PC5's notes and cover, shader cache and profile kept");
    CHECK(there("prefixes/.installing-new-game/drive_c/users/prospero/AppData/Local/Temp/part.tmp") &&
          there("prefixes/.taken-held-game/drive_c/Games/Held/game.exe"), "an install being worked on, a held game, kept");
    CHECK(there("saves/kept-game/drive_c/users/prospero/AppData/Local/Temp/kept.tmp") &&
          there("saves/kept-game/drive_c/users/prospero/Documents/slot.sav"), "kept saves untouched");
    CHECK(!there("covers/Gone Game.png") && !there("backgrounds/Gone Game.jpg") && !there("loading/Gone Game.jpeg"),
          "own pictures of a game that's gone deleted");
    CHECK(there("covers/Neon Drift.jpg") && there("covers/TIDY.PNG") && there("backgrounds/neon-drift.png"),
          "own pictures of installed games kept (by name, folder or id, any case)");
    CHECK(there("loading/loading.jpg") && there("covers/notes.txt") && there("covers/.hidden.jpg") &&
          there("sounds/Gone Game.wav") && there("pc5/settings.txt") && there("api_keys.txt") && there("covers/kept-game.png"),
          "every game's loading picture, other files, sounds, settings, keys and a kept game's picture kept");

    /* Run again: nothing left to clean. */
    CHECK(!pc5_cleanup_scan(root, library, &cleanup) && !cleanup.paths.len && !cleanup.bytes, "nothing the second time");
    pc5_cleanup_end(&cleanup);

    /* Uninstalling a game deletes its own pictures, but not one another installed game goes by. */
    Pc5Game *old = add_game(library, "old-game", "Old Game", "Old Game Folder");
    add_game(library, "twin", "Twin", "Old Game");   /* its folder has Old Game's name */
    put("covers/Old Game Folder.jpg", "by its folder");
    put("backgrounds/old-game.png", "by its id");
    put("covers/Old Game.png", "Twin's too, by its folder");
    put("covers/Twin.jpg", "Twin's");
    CHECK(!pc5_remove_begin(&remove, root, old, library), "uninstall begins");
    while (pc5_remove_step(&remove, 5)) {}
    pc5_remove_end(&remove);
    CHECK(!there("covers/Old Game Folder.jpg") && !there("backgrounds/old-game.png"), "its own pictures deleted with it");
    CHECK(there("covers/Old Game.png") && there("covers/Twin.jpg") && there("covers/Neon Drift.jpg"),
          "another installed game's kept, even one going by its name");
    CHECK(!there("prefixes/old-game"), "the game uninstalled");
    library->games[2] = library->games[--library->count];   /* out of the library */

    /* Keeping its saves: its own pictures stay with them, for when it is installed again (as
     * test_pc5's keep-saves case has it), and Clean up leaves them too. */
    Pc5Game *keeper = add_game(library, "keeper", "Keeper", "Keeper");
    put("prefixes/keeper/drive_c/users/prospero/Documents/keeper.sav", "a save");
    put("covers/Keeper.png", "cover");
    put("loading/keeper.jpg", "loading");
    CHECK(pc5_remove_can_keep(keeper, library), "its saves can be kept");
    CHECK(!pc5_remove_keep_saves(&remove, root, keeper, library), "uninstall keeping saves begins: %s", remove.error);
    while (pc5_remove_step(&remove, 5)) {}
    pc5_remove_end(&remove);
    CHECK(there("covers/Keeper.png") && there("loading/keeper.jpg"), "its own pictures kept with its saves");
    CHECK(there("saves/keeper/drive_c/users/prospero/Documents/keeper.sav"), "its saves kept");
    library->count--;
    CHECK(!pc5_cleanup_scan(root, library, &cleanup) && !cleanup.pictures, "not offered by Clean up: %d (%s)",
          cleanup.pictures, cleanup.picture_names);
    pc5_cleanup_end(&cleanup);
    CHECK(there("loading/loading.jpg") && there("covers/Neon Drift.jpg"), "the rest kept");

    pc5_free(library);
    printf("test_cleanup: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
