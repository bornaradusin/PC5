/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: game folders on USB drives. A game folder is any folder at the top of
 * a drive (or in its Games folder) with a Windows program somewhere inside.
 * Drives are only read. */
#ifndef PC5_SOURCE_H
#define PC5_SOURCE_H
#include "pc5.h"
#include "pc5_pe.h"

enum { PC5_SOURCES = 64, PC5_EXES = 12, PC5_DEPTH = 6 };

typedef struct Pc5Source {
    char name[PC5_NAME];      /* the folder's name: the game's name in PC5 */
    char path[PC5_PATH];
    int drive;                /* /mnt/usb<drive> */
} Pc5Source;

/* Folders found on the drives under roots (NULL-terminated; the console's
 * are /mnt/usb0 to /mnt/usb7). Returns how many, sorted by name. */
int pc5_source_scan(const char *const roots[], Pc5Source *out, int max);

typedef struct Pc5Exe {
    char path[PC5_PATH];      /* inside the folder, with '/' */
    uint64_t size;
    int score;                /* how likely it is the game itself */
    Pc5PeInfo pe;
    uint32_t beside;          /* the imports of the libraries in its folder */
    int java;                 /* a Java runtime beside it (pc5_java_beside) */
    int gl_framework;         /* an OpenGL game framework beside it (pc5_gl_framework_beside) */
    int godot;                /* a Godot Engine game's (pc5_pe_godot): it draws with OpenGL */
} Pc5Exe;

typedef struct Pc5Folder {
    char path[PC5_PATH];
    char name[PC5_NAME];
    uint64_t bytes;
    uint32_t files, folders;
    Pc5Exe exes[PC5_EXES];    /* best first */
    int exe_count;
    uint32_t imports;         /* every program and library in the game's folders */
    uint32_t steam_appid;     /* the game's Steam app id (pc5_identity.h), or 0 */
    uint64_t gog_id;          /* its GOG product id (goggame-<id>.info), or 0 */
    char store_name[PC5_NAME];      /* GOG's name for it, or "" */
    char identity_source[64];       /* the file they came from, or "" */
    char picture[PC5_PATH];   /* the player's cover: cover.jpg, .jpeg or .png at the top, or "" */
    char problem[PC5_ERROR];  /* why it can't be installed, or "" */
    /* A file of the copy that isn't the game's as its store ships it (another Steam emulator's or
     * fix's: OnlineFix64.dll, steam_api_o.dll), which often closes a game on the console; "". */
    char not_store[64];
} Pc5Folder;

/* Walks the whole folder: its size, its programs (ranked), what they draw
 * with. A problem (no Windows program, a name Windows can't use, too deep)
 * is in problem. -1 only when the folder can't be read at all. */
int pc5_folder_inspect(const char *path, const char *name, Pc5Folder *folder);
/* The API of the chosen program, with the libraries beside it. A Java game (a program that
 * draws with nothing it or the libraries beside it import, with a Java runtime beside it)
 * draws with OpenGL: Java games' libraries (LWJGL, JOGL, libGDX) load it at run time. */
Pc5Api pc5_folder_api(const Pc5Folder *folder, int exe);
/* 1 when the folder dir (on disk) has a Java runtime in it, as a Java game carries one beside
 * its program: a folder (runtime, jre, jre64...) with bin/server/jvm.dll or
 * bin/client/jvm.dll, names in any case. */
int pc5_java_beside(const char *dir);
/* 1 when the folder dir (on disk) has a .NET game framework in it that draws with OpenGL, which
 * it loads at run time, so nothing imports it: FNA without FNA3D (FNA.dll alone draws with
 * OpenGL through SDL2; FNA3D picks for itself), or MonoGame's OpenGL build
 * (MonoGame.Framework.dll beside SDL2.dll, without the Direct3D build's SharpDX). Names in any
 * case. Such a game draws with OpenGL, as a Java game does. */
int pc5_gl_framework_beside(const char *dir);
/* What the libraries (.dll) in the folder dir import, together (PC5_IMPORT_*); not below it. */
uint32_t pc5_dll_imports_in(const char *dir);

/* ---- batch files ---------------------------------------------------------------------
 * A game that starts from a batch file (.bat or .cmd): the console runs one program, so
 * PC5 reads the batch file the way cmd.exe would and starts the program it starts, with
 * its options and in its folder. cd, chdir, pushd, popd and set are followed, and the
 * programs start, call or a line itself starts (another batch file isn't read); %~dp0, %CD%,
 * %% and variables set before are expanded; other commands are passed over. */
typedef struct Pc5Batch {
    char exe[PC5_PATH];       /* the program it starts: inside the game's folder, with '/' */
    char arguments[512];      /* what it starts it with */
    char cwd[PC5_PATH];       /* the folder it starts it in: inside the game's folder, with '/', "" for its top */
} Pc5Batch;

/* A .bat or .cmd file's name. */
int pc5_is_batch(const char *name);
/* The program batch (inside top, with '/') starts among the game's own: top is the game's
 * folder on disk, and C:\Games\<its name> on Windows. The first .exe in the game's folder
 * it starts that isn't a helper by its name (an installer, a crash reporter, a server),
 * else the first .exe at all. 0 when it starts one, -1 when it starts none. */
int pc5_batch_read(const char *top, const char *batch, Pc5Batch *out);

#endif
