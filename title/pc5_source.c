/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: game folders on USB drives (pc5_source.h). */
#include "pc5_source.h"
#include "pc5_i18n.h"
#include "pc5_cover.h"
#include "pc5_identity.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Folders a drive keeps for itself. */
static int system_folder(const char *name)
{
    static const char *const names[] = { "System Volume Information", "$RECYCLE.BIN", "RECYCLER", "PS5", "PS4",
                                         "LOST.DIR", "FOUND.000", ".Trashes", ".Spotlight-V100", ".fseventsd" };
    if (name[0] == '.') return 1;
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (!pc5_casecmp(name, names[i])) return 1;
    return 0;
}

/* A PS5 app or game (sce_sys/param.json in it): PC5 itself, other homebrew, a game's dump.
 * Never a PC game, and big: not looked through. */
static int ps5_app(const char *path)
{
    char param[PC5_PATH];
    return !pc5_format(param, sizeof(param), "%s/sce_sys/param.json", path) && pc5_is_file(param);
}

/* Is there a .exe somewhere in path, depth levels down at most? */
static int holds_program(const char *path, int depth)
{
    DIR *dir = opendir(path);
    struct dirent *entry;
    char inner[PC5_PATH];
    int found = 0;

    if (!dir) return 0;
    while (!found && (entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;
        if (pc5_join(inner, sizeof(inner), path, entry->d_name)) continue;
        if (pc5_ends_with_ci(entry->d_name, ".exe") && pc5_is_file(inner)) found = 1;
    }
    if (!found && depth > 0) {
        rewinddir(dir);
        while (!found && (entry = readdir(dir))) {
            if (entry->d_name[0] == '.' || pc5_join(inner, sizeof(inner), path, entry->d_name)) continue;
            if (pc5_is_dir(inner) && !ps5_app(inner)) found = holds_program(inner, depth - 1);
        }
    }
    closedir(dir);
    return found;
}

static int by_name(const void *a, const void *b)
{
    return pc5_casecmp(((const Pc5Source *)a)->name, ((const Pc5Source *)b)->name);
}

static void scan_folder(const char *path, int drive, Pc5Source *out, int max, int *count)
{
    DIR *dir = opendir(path);
    struct dirent *entry;
    char inner[PC5_PATH];

    if (!dir) return;
    while (*count < max && (entry = readdir(dir))) {
        if (system_folder(entry->d_name) || pc5_join(inner, sizeof(inner), path, entry->d_name) || !pc5_is_dir(inner))
            continue;
        if (!pc5_casecmp(entry->d_name, "Games")) continue;   /* gone into below */
        /* A USB drive's PC5 folder: the games PC5 installed there, and its INSTALL folder,
         * which is looked in on its own (pc5_usb_install_dir). */
        if (!pc5_casecmp(entry->d_name, "PC5")) continue;
        if (strlen(entry->d_name) >= PC5_NAME || ps5_app(inner) || !holds_program(inner, PC5_DEPTH)) continue;
        Pc5Source *source = &out[(*count)++];
        pc5_copy(source->name, sizeof(source->name), entry->d_name);
        pc5_copy(source->path, sizeof(source->path), inner);
        source->drive = drive;
    }
    closedir(dir);
}

int pc5_source_scan(const char *const roots[], Pc5Source *out, int max)
{
    int count = 0;

    for (int i = 0; roots[i]; i++) {
        char games[PC5_PATH];
        if (!pc5_is_dir(roots[i])) continue;
        scan_folder(roots[i], i, out, max, &count);
        /* A Games folder at the top holds game folders too. */
        DIR *dir = opendir(roots[i]);
        struct dirent *entry;
        if (!dir) continue;
        while ((entry = readdir(dir)))
            if (!pc5_casecmp(entry->d_name, "Games") && !pc5_join(games, sizeof(games), roots[i], entry->d_name) &&
                pc5_is_dir(games))
                scan_folder(games, i, out, max, &count);
        closedir(dir);
    }
    qsort(out, (size_t)count, sizeof(out[0]), by_name);
    return count;
}

/* ---- one folder ----------------------------------------------------------------------- */

static int contains_ci(const char *text, const char *word)
{
    size_t n = strlen(word);
    for (; *text; text++) {
        size_t k = 0;
        while (k < n && text[k] && pc5_lower((unsigned char)text[k]) == word[k]) k++;
        if (k == n) return 1;
    }
    return 0;
}

/* Folders with installers and runtimes, not the game. */
static int helper_folder(const char *relative)
{
    static const char *const words[] = { "_commonredist", "redist", "directx", "vcredist", "dotnetfx",
                                         "__installer", "installers", "easyanticheat", "battleye", "prereq",
                                         "thirdparty", "support" };
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++)
        if (contains_ci(relative, words[i])) return 1;
    return 0;
}

/* Programs that are never the game: installers, runtimes and archivers. */
static int installer_program(const char *name)
{
    static const char *const words[] = { "unins", "setup", "install", "redist", "dxsetup", "dxweb", "dotnet",
                                         "prereq", "physx", "oalinst", "quicksfv", "7z", "unrar" };
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++)
        if (contains_ci(name, words[i])) return 1;
    return 0;
}

/* Programs that are rarely the game: crash reporters, updaters, anti-cheat, servers,
 * editors and helpers. ("crash" alone would take Crashlands for a crash reporter.) */
static int helper_program(const char *name)
{
    static const char *const words[] = { "update", "patch", "crashreport", "crashhandler", "crashsender",
                                         "crashpad", "crash_report", "crash_handler", "crashupload", "crash_upload",
                                         "crashdump", "bugsplat", "report", "easyanticheat", "battleye", "beservice",
                                         "editor", "dedicated", "server", "uploader", "cefprocess", "subprocess",
                                         "notification", "touchup", "activation", "register", "cleanup", "repair",
                                         "diagnos", "benchmark", "helper" };
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++)
        if (contains_ci(name, words[i])) return 1;
    return 0;
}

/* Letters and digits only, lower case ("Crashlands 2" and "Crashlands2" alike). */
static void squeeze(const char *text, char *out, size_t size)
{
    size_t n = 0;
    for (; *text && n + 1 < size; text++)
        if ((*text >= 'a' && *text <= 'z') || (*text >= '0' && *text <= '9') || (*text >= 'A' && *text <= 'Z'))
            out[n++] = (char)pc5_lower((unsigned char)*text);
    out[n] = 0;
}

/* A program named after its folder (Crashlands2.exe in "Crashlands 2") is the game, whatever
 * else its name says. */
static int named_like(const char *folder, const char *base)
{
    char a[PC5_NAME * 2], b[PC5_NAME * 2];
    squeeze(folder, a, sizeof(a));
    squeeze(base, b, sizeof(b));
    return strlen(a) >= 4 && strlen(b) >= 4 && (strstr(a, b) || strstr(b, a));
}

/* A name Windows (or the profile format) can't take. */
static int bad_windows_name(const char *name)
{
    static const char *const reserved[] = { "con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5",
                                            "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4",
                                            "lpt5", "lpt6", "lpt7", "lpt8", "lpt9" };
    size_t n = strlen(name);
    char base[8];
    size_t k = 0;

    if (!n || name[n - 1] == '.' || name[n - 1] == ' ') return 1;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++)
        if (*p < 0x20 || strchr("<>:\"|?*\\", *p)) return 1;
    while (k < n && k + 1 < sizeof(base) && name[k] != '.') { base[k] = (char)pc5_lower((unsigned char)name[k]); k++; }
    base[k] = 0;
    if (k == n || name[k] == '.')
        for (size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++)
            if (!strcmp(base, reserved[i])) return 1;
    return 0;
}

static int by_score(const void *a, const void *b)
{
    const Pc5Exe *x = a, *y = b;
    if (x->score != y->score) return y->score - x->score;
    return strcmp(x->path, y->path);
}

/* Libraries beside the programs: what they draw with, by folder. */
typedef struct DllDir { char dir[PC5_PATH]; uint32_t imports; } DllDir;

typedef struct Walk {
    Pc5Folder *folder;
    Pc5Exe *all;               /* every program found, before ranking */
    int all_count, all_max;
    DllDir *dlls;
    int dll_count, dll_max;
} Walk;

static void add_dll_imports(Walk *walk, const char *dir, uint32_t imports)
{
    for (int i = 0; i < walk->dll_count; i++)
        if (!strcmp(walk->dlls[i].dir, dir)) { walk->dlls[i].imports |= imports; return; }
    if (walk->dll_count < walk->dll_max) {
        pc5_copy(walk->dlls[walk->dll_count].dir, PC5_PATH, dir);
        walk->dlls[walk->dll_count++].imports = imports;
    }
}

static int score_program(const Pc5Folder *folder, const Pc5Exe *exe, const char *relative)
{
    const char *slash = strrchr(relative, '/'), *name = slash ? slash + 1 : relative;
    char base[PC5_NAME];
    int score = 100, depth = 0;

    snprintf(base, sizeof(base), "%.*s", (int)(strlen(name) > 4 ? strlen(name) - 4 : strlen(name)), name);
    for (const char *p = relative; *p; p++) depth += *p == '/';
    if (exe->pe.dll) return -1000;
    if (helper_folder(relative) || installer_program(name) || (helper_program(name) && !named_like(folder->name, base)))
        score -= 1000;
    if (contains_ci(name, "launch") || contains_ci(name, "config") || contains_ci(name, "settings")) score -= 40;
    if (contains_ci(name, "-shipping")) score += 60;
    if (!exe->pe.gui) score -= 30;
    if (exe->pe.bits == 64) score += 10;
    score -= depth * 5;
    score += pc5_cover_similarity(folder->name, base) / 2;
    if (exe->pe.product[0]) score += pc5_cover_similarity(folder->name, exe->pe.product) / 4;
    if (exe->size > 1024 * 1024) score += (int)(log2((double)exe->size / (1024 * 1024)) * 6);
    else score -= 20;   /* a stub that starts the real program */
    return score;
}

/* 1 when program a starts program b: PlayX.exe, LaunchX.exe, StartX.exe or RunX.exe (with or
 * without a space, - or _) beside X.exe. Such a starter runs the game as a second process,
 * which Wine on the console doesn't do, so X.exe is the one to start. */
static int starter_of(const char *a, const char *b)
{
    static const char *const words[] = { "play", "launch", "launcher", "start", "run", "go" };
    const char *sa = strrchr(a, '/'), *sb = strrchr(b, '/');
    size_t da = sa ? (size_t)(sa - a) : 0, db = sb ? (size_t)(sb - b) : 0;
    const char *na = sa ? sa + 1 : a, *nb = sb ? sb + 1 : b;
    size_t la = strlen(na), lb = strlen(nb);

    if (da != db || strncmp(a, b, da) || la <= lb || la - lb > 10) return 0;
    if (pc5_casecmp(na + (la - lb), nb)) return 0;      /* ends with the other's whole name */
    size_t prefix = la - lb;
    while (prefix && (na[prefix - 1] == ' ' || na[prefix - 1] == '-' || na[prefix - 1] == '_')) prefix--;
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        size_t n = strlen(words[i]), k = 0;
        while (k < n && k < prefix && (na[k] | 0x20) == words[i][k]) k++;
        if (n == prefix && k == n) return 1;
    }
    return 0;
}

/* A batch file another way to start the game (a safe mode, windowed, another renderer, a
 * benchmark), not the way it is meant to start. */
static int variant_batch(const char *name)
{
    static const char *const words[] = { "safe", "window", "debug", "log", "test", "bench", "editor", "server",
                                         "config", "setting", "dx", "vulkan", "opengl", "nosound", "lowspec" };
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++)
        if (contains_ci(name, words[i])) return 1;
    return 0;
}

/* The program a choice starts, inside the folder at top: a batch file's program, or itself. */
static void program_of(const char *top, const char *choice, Pc5Batch *batch, char *out, size_t size)
{
    if (pc5_is_batch(choice) && !pc5_batch_read(top, choice, batch)) pc5_copy(out, size, batch->exe);
    else pc5_copy(out, size, choice);
}

int pc5_folder_inspect(const char *path, const char *name, Pc5Folder *folder)
{
    enum { BATCHES = 32 };
    enum { STACK = 4096 };
    char (*stack)[PC5_PATH] = pc5_alloc(sizeof(*stack) * STACK);
    Walk walk = { folder, pc5_alloc(sizeof(Pc5Exe) * 256), 0, 256, pc5_alloc(sizeof(DllDir) * 256), 0, 256 };
    char (*batches)[PC5_PATH] = pc5_alloc(sizeof(*batches) * BATCHES);
    Pc5Batch *batch = pc5_alloc(sizeof(*batch));
    int top = 0, status = 0, batch_count = 0;

    memset(folder, 0, sizeof(*folder));
    pc5_copy(folder->path, sizeof(folder->path), path);
    pc5_copy(folder->name, sizeof(folder->name), name);
    if (!stack || !walk.all || !walk.dlls || !batches || !batch || !pc5_is_dir(path)) { status = -1; goto out; }
    stack[top++][0] = 0;   /* "": the folder itself */
    while (top > 0) {
        char relative[PC5_PATH], dir_path[PC5_PATH], entry_path[PC5_PATH], child[PC5_PATH];
        DIR *dir;
        struct dirent *entry;
        int depth = 0;

        memcpy(relative, stack[--top], PC5_PATH);
        for (const char *p = relative; *p; p++) depth += *p == '/';
        if (relative[0] ? pc5_join(dir_path, sizeof(dir_path), path, relative)
                        : pc5_format(dir_path, sizeof(dir_path), "%s", path)) {
            snprintf(folder->problem, sizeof(folder->problem), N_("A path in the folder is too long."));
            continue;
        }
        if (!(dir = opendir(dir_path))) {
            snprintf(folder->problem, sizeof(folder->problem), N_("PC5 can't read %.200s."), relative[0] ? relative : name);
            continue;
        }
        while ((entry = readdir(dir))) {
            struct stat st;
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
            if ((relative[0] ? pc5_format(child, sizeof(child), "%s/%s", relative, entry->d_name)
                             : pc5_format(child, sizeof(child), "%s", entry->d_name)) ||
                pc5_join(entry_path, sizeof(entry_path), path, child)) {
                snprintf(folder->problem, sizeof(folder->problem), N_("A path in the folder is too long."));
                continue;
            }
            if (lstat(entry_path, &st)) continue;
            if (bad_windows_name(entry->d_name) && !folder->problem[0])
                snprintf(folder->problem, sizeof(folder->problem), N_("Windows can't use the name \"%.180s\"."), child);
            if (S_ISDIR(st.st_mode)) {
                folder->folders++;
                if (depth + 1 > 40 || top >= STACK) {
                    snprintf(folder->problem, sizeof(folder->problem), N_("The folder goes too many levels deep."));
                    continue;
                }
                memcpy(stack[top++], child, PC5_PATH);
            } else if (S_ISREG(st.st_mode)) {
                folder->files++;
                folder->bytes += (uint64_t)st.st_size;
                int is_exe = pc5_ends_with_ci(entry->d_name, ".exe"), is_dll = pc5_ends_with_ci(entry->d_name, ".dll");
                if ((is_exe || is_dll) && !helper_folder(child)) {
                    Pc5PeInfo pe;
                    if (pc5_pe_read(entry_path, &pe)) continue;
                    if (is_dll || pe.dll) {
                        add_dll_imports(&walk, relative, pe.imports);
                    } else if (depth <= PC5_DEPTH && walk.all_count < walk.all_max) {
                        Pc5Exe *exe = &walk.all[walk.all_count++];
                        pc5_copy(exe->path, sizeof(exe->path), child);
                        exe->size = (uint64_t)st.st_size;
                        exe->pe = pe;
                        exe->score = score_program(folder, exe, child);
                    }
                    folder->imports |= pe.imports;
                } else if (pc5_is_batch(entry->d_name)) {
                    if (depth <= PC5_DEPTH && !helper_folder(child) && batch_count < BATCHES)
                        memcpy(batches[batch_count++], child, PC5_PATH);
                } else if (!relative[0] && !pc5_casecmp(entry->d_name, "steam_appid.txt")) {
                    Pc5Buf text = { 0 };
                    if (!pc5_read_file(entry_path, &text, 64)) folder->steam_appid = (uint32_t)strtoul((char *)text.data, NULL, 10);
                    pc5_buf_free(&text);
                } else if (!relative[0] && !folder->picture[0] &&
                           (!pc5_casecmp(entry->d_name, "cover.jpg") || !pc5_casecmp(entry->d_name, "cover.jpeg") ||
                            !pc5_casecmp(entry->d_name, "cover.png"))) {
                    pc5_copy(folder->picture, sizeof(folder->picture), entry_path);
                }
            }
            /* links and devices are left out: exFAT and FAT32 have none */
        }
        closedir(dir);
    }
    for (int i = 0; i < walk.all_count; i++)
        for (int k = 0; k < walk.all_count; k++)
            if (i != k && starter_of(walk.all[i].path, walk.all[k].path)) { walk.all[i].score -= 80; break; }
    /* The folder's only program, unless it installs something, is the game: a helper's
     * name on it is a coincidence. */
    if (walk.all_count == 1 && walk.all[0].score <= -500 && !walk.all[0].pe.dll) {
        const char *slash = strrchr(walk.all[0].path, '/'), *name = slash ? slash + 1 : walk.all[0].path;
        if (!helper_folder(walk.all[0].path) && !installer_program(name)) walk.all[0].score += 1000;
    }
    /* Batch files that start one of the game's programs, offered with that program's bits and
     * drawing. A folder with one such file (Launch.bat, Start Game.bat) is meant to start from
     * it, so it comes first; others come after the program they start. */
    {
        int programs = walk.all_count, best = -100000, lone = -1, starting = 0;
        for (int i = 0; i < programs; i++)
            if (walk.all[i].score > best) best = walk.all[i].score;
        for (int i = 0; i < batch_count && walk.all_count < walk.all_max; i++) {
            const char *slash = strrchr(batches[i], '/'), *file = slash ? slash + 1 : batches[i];
            int k = 0;
            if (pc5_batch_read(path, batches[i], batch)) continue;
            while (k < programs && pc5_casecmp(walk.all[k].path, batch->exe)) k++;
            if (k == programs || walk.all[k].pe.dll) continue;
            Pc5Exe *exe = &walk.all[walk.all_count++];
            *exe = walk.all[k];
            pc5_copy(exe->path, sizeof(exe->path), batches[i]);
            if (installer_program(file) || helper_program(file) || walk.all[k].score <= -500) {
                exe->score = -1000;
            } else {
                exe->score = walk.all[k].score - 1;
                if (!variant_batch(file)) {
                    starting++;
                    lone = walk.all_count - 1;
                }
            }
        }
        if (starting == 1) walk.all[lone].score = best + 5;
    }
    /* Every program, best first: PC5 starts the first, and Change program offers the others,
     * the tools and installers last (a game's own settings tool can be named like one:
     * HardwareSetup.exe). */
    qsort(walk.all, (size_t)walk.all_count, sizeof(Pc5Exe), by_score);
    for (int i = 0; i < walk.all_count && folder->exe_count < PC5_EXES; i++) folder->exes[folder->exe_count++] = walk.all[i];
    /* Each program's own imports, joined by its folder's libraries. */
    for (int i = 0; i < folder->exe_count; i++) {
        char dir[PC5_PATH];
        char *slash;
        program_of(path, folder->exes[i].path, batch, dir, sizeof(dir));
        slash = strrchr(dir, '/');
        if (slash) *slash = 0; else dir[0] = 0;
        for (int k = 0; k < walk.dll_count; k++)
            if (!strcmp(walk.dlls[k].dir, dir)) folder->exes[i].beside |= walk.dlls[k].imports;
        char on_disk[PC5_PATH];
        if (dir[0] ? !pc5_join(on_disk, sizeof(on_disk), path, dir) : !pc5_format(on_disk, sizeof(on_disk), "%s", path))
            folder->exes[i].java = pc5_java_beside(on_disk);
    }
    /* A game whose programs and libraries import nothing they draw with loads it at run
     * time: what its likeliest programs name (pc5_pe_named_libraries), read only then. */
    if (!(folder->imports & PC5_IMPORT_DRAWING)) {
        for (int i = 0; i < folder->exe_count && i < 3; i++) {
            char file[PC5_PATH * 2], program[PC5_PATH];
            program_of(path, folder->exes[i].path, batch, program, sizeof(program));
            if (pc5_format(file, sizeof(file), "%s/%s", path, program)) continue;
            folder->exes[i].pe.imports |= pc5_pe_named_libraries(file);
        }
    }
    /* An Unreal Engine 5 game's program loads Direct3D 12 itself, its imports naming only
     * Direct3D 11 or DXGI: it draws with Direct3D 12 (VKD3D-Proton), not Wine's own. */
    for (int i = 0; i < folder->exe_count && i < 3; i++) {
        char file[PC5_PATH * 2], program[PC5_PATH];
        program_of(path, folder->exes[i].path, batch, program, sizeof(program));
        if (pc5_format(file, sizeof(file), "%s/%s", path, program)) continue;
        if (pc5_pe_unreal_d3d12(file, folder->exes[i].pe.imports)) folder->exes[i].pe.imports |= PC5_IMPORT_D3D12;
        /* A Godot game's program imports Direct3D, but the game draws with OpenGL (or Vulkan). */
        folder->exes[i].godot = pc5_pe_godot(file);
    }
    /* Which game it is, from what its store or a Steam stand-in left in it (pc5_identity.h). */
    {
        Pc5Identity id;
        if (!pc5_identity_scan(path, &id)) {
            if (id.steam_appid) folder->steam_appid = id.steam_appid;
            folder->gog_id = id.gog_id;
            pc5_copy(folder->store_name, sizeof(folder->store_name), id.name);
            pc5_copy(folder->identity_source, sizeof(folder->identity_source), id.source);
        }
    }
    if (!folder->exe_count && !folder->problem[0])
        snprintf(folder->problem, sizeof(folder->problem), N_("There is no Windows program in this folder."));
    else if (folder->exe_count && folder->exes[0].score <= -500 && !folder->problem[0])
        snprintf(folder->problem, sizeof(folder->problem), N_("This folder only has installers and tools, not a game."));
out:
    pc5_free(stack);
    pc5_free(walk.all);
    pc5_free(walk.dlls);
    pc5_free(batches);
    pc5_free(batch);
    return status;
}

Pc5Api pc5_folder_api(const Pc5Folder *folder, int index)
{
    const uint32_t drawing = PC5_IMPORT_DRAWING;
    uint32_t own, beside;

    if (index < 0 || index >= folder->exe_count) return PC5_API_GDI;
    if (folder->exes[index].godot) return PC5_API_OPENGL;
    own = folder->exes[index].pe.imports;
    beside = folder->exes[index].beside;
    if (own & drawing) return pc5_api_from_imports(own);
    if (beside & drawing) return pc5_api_from_imports(beside);
    if (folder->exes[index].java) return PC5_API_OPENGL;
    return pc5_api_from_imports(folder->imports);
}

/* name in dir in any case, joined to dir into out: 0, or -1 when there is none. */
static int join_ci(const char *dir, const char *name, char *out, size_t size)
{
    DIR *folder;
    struct dirent *entry;
    int found = -1;

    if (pc5_join(out, size, dir, name)) return -1;
    if (pc5_exists(out)) return 0;
    if (!(folder = opendir(dir))) return -1;
    while (found && (entry = readdir(folder)))
        if (!pc5_casecmp(entry->d_name, name) && !pc5_join(out, size, dir, entry->d_name)) found = 0;
    closedir(folder);
    return found;
}

int pc5_java_beside(const char *dir)
{
    static const char *const kinds[] = { "server", "client" };
    char top[PC5_PATH], bin[PC5_PATH], kind[PC5_PATH], jvm[PC5_PATH];
    DIR *folder = opendir(dir);
    struct dirent *entry;
    int found = 0;

    if (!folder) return 0;
    while (!found && (entry = readdir(folder))) {
        if (entry->d_name[0] == '.' || pc5_join(top, sizeof(top), dir, entry->d_name) || !pc5_is_dir(top) ||
            join_ci(top, "bin", bin, sizeof(bin)))
            continue;
        for (size_t k = 0; k < sizeof(kinds) / sizeof(kinds[0]) && !found; k++)
            found = !join_ci(bin, kinds[k], kind, sizeof(kind)) && !join_ci(kind, "jvm.dll", jvm, sizeof(jvm)) &&
                    pc5_is_file(jvm);
    }
    closedir(folder);
    return found;
}

uint32_t pc5_dll_imports_in(const char *dir)
{
    char file[PC5_PATH];
    DIR *folder = opendir(dir);
    struct dirent *entry;
    uint32_t imports = 0;

    if (!folder) return 0;
    while ((entry = readdir(folder))) {
        Pc5PeInfo pe;
        if (pc5_ends_with_ci(entry->d_name, ".dll") && !pc5_join(file, sizeof(file), dir, entry->d_name) &&
            pc5_is_file(file) && !pc5_pe_read(file, &pe))
            imports |= pe.imports;
    }
    closedir(folder);
    return imports;
}

/* ---- batch files ------------------------------------------------------------------- */

int pc5_is_batch(const char *name)
{
    return pc5_ends_with_ci(name, ".bat") || pc5_ends_with_ci(name, ".cmd");
}

enum { BATCH_VARS = 32, BATCH_PUSHES = 8, BATCH_LINE = 4096 };

typedef struct BatchVar {
    char name[64];
    char value[512];
} BatchVar;

typedef struct Batch {
    const char *top;                     /* the game's folder on disk */
    char windows[PC5_PATH];              /* the same on Windows: C:\Games\<its name> */
    char file[PC5_PATH];                 /* the batch file, inside top, with '/' */
    char cwd[PC5_PATH];                  /* the current folder, inside top, with '/' ("" for top) */
    char pushed[BATCH_PUSHES][PC5_PATH]; /* pushd's folders */
    int pushes;
    BatchVar vars[BATCH_VARS];           /* set's variables */
    int var_count;
    Pc5Batch first, game;                /* the first program it starts, and the first that isn't a helper */
    int have_first, have_game;
    char line[BATCH_LINE], expanded[BATCH_LINE], command[BATCH_LINE];
} Batch;

/* Windows' own variables in a game's Windows (the user "prospero"), as Wine sets them. */
static const struct { const char *name, *value; } batch_environment[] = {
    { "SystemDrive", "C:" },
    { "SystemRoot", "C:\\windows" },
    { "windir", "C:\\windows" },
    { "ComSpec", "C:\\windows\\system32\\cmd.exe" },
    { "ProgramFiles", "C:\\Program Files" },
    { "ProgramFiles(x86)", "C:\\Program Files (x86)" },
    { "ProgramW6432", "C:\\Program Files" },
    { "CommonProgramFiles", "C:\\Program Files\\Common Files" },
    { "ProgramData", "C:\\ProgramData" },
    { "ALLUSERSPROFILE", "C:\\ProgramData" },
    { "PUBLIC", "C:\\users\\Public" },
    { "USERNAME", "prospero" },
    { "USERPROFILE", "C:\\users\\prospero" },
    { "HOMEDRIVE", "C:" },
    { "HOMEPATH", "\\users\\prospero" },
    { "APPDATA", "C:\\users\\prospero\\AppData\\Roaming" },
    { "LOCALAPPDATA", "C:\\users\\prospero\\AppData\\Local" },
    { "TEMP", "C:\\users\\prospero\\AppData\\Local\\Temp" },
    { "TMP", "C:\\users\\prospero\\AppData\\Local\\Temp" },
    { "OS", "Windows_NT" },
};

/* relative (inside the game's folder, with '/') as Windows names it. */
static void batch_windows(const Batch *b, const char *relative, char *out, size_t size)
{
    if (pc5_format(out, size, "%s%s%s", b->windows, relative[0] ? "\\" : "", relative)) out[0] = 0;
    for (char *p = out; *p; p++) if (*p == '/') *p = '\\';
}

/* %0, %~dp0 and the like: the batch file on Windows, or the parts asked for (d the drive,
 * p the folder, n the name, x the extension; f or none the whole). */
static void batch_self(const Batch *b, const char *parts, char *out, size_t size)
{
    char full[PC5_PATH + 16];
    int d = 0, p = 0, n = 0, x = 0, f = !parts[0];

    for (const char *c = parts; *c; c++) {
        switch (pc5_lower((unsigned char)*c)) {
        case 'd': d = 1; break;
        case 'p': p = 1; break;
        case 'n': n = 1; break;
        case 'x': x = 1; break;
        case 'f': f = 1; break;
        default: break;   /* s: short names, which are the same here */
        }
    }
    batch_windows(b, b->file, full, sizeof(full));
    if (f || !(d || p || n || x)) {
        pc5_copy(out, size, full);
        return;
    }
    const char *slash = strrchr(full, '\\'), *base = slash ? slash + 1 : full, *dot = strrchr(base, '.');
    size_t path_len = slash ? (size_t)(slash + 1 - full) : 0, base_len = dot ? (size_t)(dot - base) : strlen(base);
    if (path_len < 2) path_len = 2;
    snprintf(out, size, "%.*s%.*s%.*s%s", d ? 2 : 0, full, p ? (int)(path_len - 2) : 0, full + 2, n ? (int)base_len : 0,
             base, x && dot ? dot : "");
}

/* A variable's value: %CD%, one the batch file set, or Windows' own; "" when there is none,
 * as cmd.exe makes it in a batch file. */
static const char *batch_variable(const Batch *b, const char *name, char *buf, size_t size)
{
    if (!pc5_casecmp(name, "CD")) {
        batch_windows(b, b->cwd, buf, size);
        return buf;
    }
    for (int i = b->var_count - 1; i >= 0; i--)
        if (!pc5_casecmp(name, b->vars[i].name)) return b->vars[i].value;
    for (size_t i = 0; i < sizeof(batch_environment) / sizeof(batch_environment[0]); i++)
        if (!pc5_casecmp(name, batch_environment[i].name)) return batch_environment[i].value;
    return "";
}

/* A line with its variables expanded, as cmd.exe does before it runs the line. */
static void batch_expand(const Batch *b, const char *in, char *out, size_t size)
{
    char piece[PC5_PATH + 16], name[128];
    size_t n = 0;

    for (const char *p = in; *p && n + 1 < size;) {
        const char *value;
        if (*p != '%') {
            out[n++] = *p++;
            continue;
        }
        if (p[1] == '%') {
            out[n++] = '%';
            p += 2;
            continue;
        }
        if (p[1] == '*' || (p[1] >= '1' && p[1] <= '9')) {   /* the batch file is given no arguments */
            p += 2;
            continue;
        }
        if (p[1] == '0' || p[1] == '~') {
            const char *q = p + 1, *parts = q;
            size_t count = 0;
            if (*q == '~') {
                parts = ++q;
                while (*q && strchr("dpnxfsDPNXFS", *q)) q++;
                count = (size_t)(q - parts);
            }
            if (*q == '*' || (*q >= '1' && *q <= '9')) {   /* the batch file's arguments: none */
                p = q + 1;
                continue;
            }
            if (*q != '0' || count >= sizeof(name)) {
                out[n++] = *p++;
                continue;
            }
            memcpy(name, parts, count);
            name[count] = 0;
            batch_self(b, name, piece, sizeof(piece));
            value = piece;
            p = q + 1;
        } else {
            const char *close = strchr(p + 1, '%');
            size_t len = close ? (size_t)(close - p - 1) : 0;
            if (len && memchr(p + 1, ':', len)) {   /* an edit (%x:~0,3%): kept whole, as it is */
                for (const char *c = p; c <= close && n + 1 < size; c++) out[n++] = *c;
                p = close + 1;
                continue;
            }
            if (!len || len >= sizeof(name) || memchr(p + 1, ' ', len)) {
                out[n++] = *p++;   /* a lone % */
                continue;
            }
            memcpy(name, p + 1, len);
            name[len] = 0;
            value = batch_variable(b, name, piece, sizeof(piece));
            p = close + 1;
        }
        for (; *value && n + 1 < size; value++) out[n++] = *value;
    }
    out[n] = 0;
}

/* The next word of text in out, its quotes taken off; returns where the text goes on. */
static const char *batch_token(const char *text, char *out, size_t size)
{
    size_t n = 0;
    int quoted = 0;

    while (*text == ' ' || *text == '\t') text++;
    for (; *text && (quoted || (*text != ' ' && *text != '\t')); text++) {
        if (*text == '"') {
            quoted = !quoted;
            continue;
        }
        if (n + 1 < size) out[n++] = *text;
    }
    out[n] = 0;
    return text;
}

/* 1 when text starts with prefix, letters of either case alike. */
static int starts_ci(const char *text, const char *prefix, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (!text[i] || pc5_lower((unsigned char)text[i]) != pc5_lower((unsigned char)prefix[i])) return 0;
    return 1;
}

/* path, as the batch file names it (in the game's folder on Windows, or from base), as a path
 * inside the game's folder with each name in its own case: a folder when want_dir, else a
 * file. -1 when it is outside the game's folder or not there. */
static int batch_resolve(const Batch *b, const char *base, const char *path, int want_dir, char *out, size_t size)
{
    char joined[PC5_PATH * 2], found[PC5_PATH], disk[PC5_PATH * 2];
    size_t w = strlen(b->windows), n = 0;
    const char *from = path;

    if (starts_ci(path, b->windows, w) && (path[w] == '\\' || path[w] == '/' || !path[w])) {
        from = path + w;
        base = "";
    } else if ((path[0] && path[1] == ':') || path[0] == '\\' || path[0] == '/') {
        return -1;   /* another drive, or C: outside the game */
    }
    if (pc5_format(joined, sizeof(joined), "%s/%s", base, from)) return -1;
    /* Its names one by one: "." and ".." taken, each found whatever its case. */
    found[0] = 0;
    for (char *name = joined, *next; name; name = next) {
        next = name + strcspn(name, "/\\");
        if (*next) *next++ = 0; else next = NULL;
        if (!name[0] || !strcmp(name, ".")) continue;
        if (!strcmp(name, "..")) {
            char *slash = strrchr(found, '/');
            if (!found[0]) return -1;
            if (slash) *slash = 0; else found[0] = 0;
            n = strlen(found);
            continue;
        }
        /* The name as the folder stores it: exFAT finds any case of it, so asking whether
         * the batch file's spelling exists isn't enough. The same name first, then any case. */
        {
            DIR *dir;
            struct dirent *entry;
            char stored[PC5_PATH] = "";
            if (pc5_format(disk, sizeof(disk), "%s%s%s", b->top, found[0] ? "/" : "", found)) return -1;
            if (!(dir = opendir(disk))) return -1;
            while ((entry = readdir(dir))) {
                if (!strcmp(entry->d_name, name)) {
                    pc5_copy(stored, sizeof(stored), entry->d_name);
                    break;
                }
                if (!stored[0] && !pc5_casecmp(entry->d_name, name)) pc5_copy(stored, sizeof(stored), entry->d_name);
            }
            closedir(dir);
            if (!stored[0] || strlen(stored) != strlen(name)) return -1;
            memcpy(name, stored, strlen(stored));
        }
        if (pc5_format(found + n, sizeof(found) - n, "%s%s", n ? "/" : "", name)) return -1;
        n = strlen(found);
    }
    if (pc5_format(disk, sizeof(disk), "%s%s%s", b->top, found[0] ? "/" : "", found)) return -1;
    if (want_dir ? !pc5_is_dir(disk) : !pc5_is_file(disk)) return -1;
    pc5_copy(out, size, found);
    return 0;
}

/* The arguments after a program: redirections (>nul, 2>&1) taken out, one space between. */
static void batch_arguments(const char *in, char *out, size_t size)
{
    size_t n = 0;
    int skip_next = 0;

    out[0] = 0;
    for (const char *p = in;;) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        const char *start = p;
        int quoted = 0;
        while (*p && (quoted || (*p != ' ' && *p != '\t'))) {
            if (*p == '"') quoted = !quoted;
            p++;
        }
        size_t len = (size_t)(p - start);
        if (skip_next) {
            skip_next = 0;
            continue;
        }
        if (start[0] == '>' || start[0] == '<' || (start[0] >= '0' && start[0] <= '9' && len > 1 && start[1] == '>')) {
            const char *q = start;
            while (q < p && (*q == '>' || *q == '<' || (*q >= '0' && *q <= '9'))) q++;
            skip_next = q == p;   /* the operator alone: its file is the next word */
            continue;
        }
        if (n + (n ? 1 : 0) + len + 1 > size) break;
        if (n) out[n++] = ' ';
        for (size_t i = 0; i < len; i++) {
            unsigned char c = (unsigned char)start[i];
            if (c == '\t') out[n++] = ' ';
            else if (c >= 0x20 && c != 0x7f) out[n++] = (char)c;   /* a profile line takes no others */
        }
        out[n] = 0;
    }
}

/* word, a program the batch file names, found from base: as it is, or with .exe after a name
 * without an extension, as cmd.exe finds it. */
static int batch_find(const Batch *b, const char *base, const char *word, char *exe, size_t size)
{
    char with_exe[PC5_PATH + 8];
    const char *slash = strrchr(word, '\\');

    if (!batch_resolve(b, base, word, 0, exe, size)) return 0;
    if (strchr(slash ? slash : word, '.') || pc5_format(with_exe, sizeof(with_exe), "%s.exe", word)) return -1;
    return batch_resolve(b, base, with_exe, 0, exe, size);
}

/* A program the batch file starts in cwd, found from there or from the batch file's own
 * current folder (start /d): kept when it is one of the game's. */
static void batch_program(Batch *b, const char *line, const char *cwd)
{
    char word[PC5_PATH], exe[PC5_PATH];
    const char *rest = batch_token(line, word, sizeof(word)), *slash, *name;

    if (!word[0]) return;
    if (batch_find(b, cwd, word, exe, sizeof(exe)) && (!strcmp(cwd, b->cwd) || batch_find(b, b->cwd, word, exe, sizeof(exe))))
        return;
    if (!pc5_ends_with_ci(exe, ".exe")) return;
    slash = strrchr(exe, '/');
    name = slash ? slash + 1 : exe;
    int helper = helper_folder(exe) || installer_program(name) || helper_program(name);
    for (int i = 0; i < 2; i++) {
        Pc5Batch *slot = i ? &b->game : &b->first;
        int *have = i ? &b->have_game : &b->have_first;
        if (*have || (i && helper)) continue;
        pc5_copy(slot->exe, sizeof(slot->exe), exe);
        pc5_copy(slot->cwd, sizeof(slot->cwd), cwd);
        batch_arguments(rest, slot->arguments, sizeof(slot->arguments));
        *have = 1;
    }
}

/* cd, chdir and pushd's folder: the rest of the line, /d and quotes taken off. */
static void batch_cd(Batch *b, const char *arg)
{
    char path[PC5_PATH], dir[PC5_PATH];
    size_t n = 0;
    const char *p;

    while (*arg == ' ' || *arg == '\t') arg++;
    if (arg[0] == '/' && pc5_lower((unsigned char)arg[1]) == 'd' && (arg[2] == ' ' || arg[2] == '\t' || arg[2] == '"'))
        arg += 2;
    for (; *arg && n + 1 < sizeof(path); arg++)
        if (*arg != '"') path[n++] = *arg;
    while (n && (path[n - 1] == ' ' || path[n - 1] == '\t')) n--;
    path[n] = 0;
    for (p = path; *p == ' ' || *p == '\t'; p++) {}
    if (*p && !batch_resolve(b, b->cwd, p, 1, dir, sizeof(dir))) pc5_copy(b->cwd, sizeof(b->cwd), dir);
}

/* set NAME=value and set "NAME=value"; set /a and set /p are passed over. */
static void batch_set(Batch *b, const char *arg)
{
    char text[sizeof(b->vars[0].name) + sizeof(b->vars[0].value)], *name, *value, *equals;
    size_t n;

    while (*arg == ' ' || *arg == '\t') arg++;
    if (*arg == '/' || !*arg) return;
    pc5_copy(text, sizeof(text), arg);
    name = text;
    if (*name == '"') {
        char *close = strrchr(++name, '"');
        if (close) *close = 0;
    }
    if (!(equals = strchr(name, '=')) || equals == name) return;
    *equals = 0;
    value = equals + 1;
    for (n = strlen(name); n && (name[n - 1] == ' ' || name[n - 1] == '\t'); n--) name[n - 1] = 0;
    for (n = strlen(value); n && (value[n - 1] == ' ' || value[n - 1] == '\t'); n--) value[n - 1] = 0;
    if (!name[0] || strlen(name) >= sizeof(b->vars[0].name)) return;
    int i = 0;
    while (i < b->var_count && pc5_casecmp(b->vars[i].name, name)) i++;
    if (i == b->var_count) {
        if (b->var_count == BATCH_VARS) return;
        b->var_count++;
    }
    pc5_copy(b->vars[i].name, sizeof(b->vars[i].name), name);
    pc5_copy(b->vars[i].value, sizeof(b->vars[i].value), value);
}

/* start ["title"] [/d folder] [/b /wait /min ...] program [arguments] */
static void batch_start(Batch *b, const char *arg)
{
    char word[PC5_PATH], cwd[PC5_PATH], dir[PC5_PATH];

    int titled = 0;

    pc5_copy(cwd, sizeof(cwd), b->cwd);
    for (;;) {
        while (*arg == ' ' || *arg == '\t') arg++;
        if (*arg == '"' && !titled) {   /* the window's title: the first quoted word, before or after switches */
            arg = batch_token(arg, word, sizeof(word));
            titled = 1;
            continue;
        }
        if (*arg != '/') break;
        arg = batch_token(arg, word, sizeof(word));
        if (pc5_lower((unsigned char)word[1]) == 'd') {
            const char *folder = word + 2;
            if (!*folder) {
                arg = batch_token(arg, word, sizeof(word));
                folder = word;
            }
            if (!batch_resolve(b, b->cwd, folder, 1, dir, sizeof(dir))) pc5_copy(cwd, sizeof(cwd), dir);
        } else if (!pc5_casecmp(word, "/affinity") || !pc5_casecmp(word, "/node")) {
            arg = batch_token(arg, word, sizeof(word));   /* its value */
        }
    }
    batch_program(b, arg, cwd);
}

static void batch_command(Batch *b, char *cmd)
{
    char word[PC5_PATH], arg[PC5_PATH * 2];
    const char *rest;
    size_t n;

    while (*cmd == ' ' || *cmd == '\t' || *cmd == '@' || *cmd == '(') cmd++;
    n = strlen(cmd);
    while (n && (cmd[n - 1] == ' ' || cmd[n - 1] == '\t' || cmd[n - 1] == '\r')) cmd[--n] = 0;
    if (n && cmd[n - 1] == ')' && !strchr(cmd, '(')) cmd[--n] = 0;   /* the end of a ( ) block */
    if (!*cmd || *cmd == ':') return;                                   /* labels and :: notes */
    rest = batch_token(cmd, word, sizeof(word));
    if (!pc5_casecmp(word, "rem")) return;
    if (!pc5_casecmp(word, "cd") || !pc5_casecmp(word, "chdir")) {
        batch_cd(b, rest);
    } else if (starts_ci(word, "cd", 2) && (word[2] == '.' || word[2] == '\\' || word[2] == '/')) {
        if (!pc5_format(arg, sizeof(arg), "%s%s", word + 2, rest)) batch_cd(b, arg);   /* cd.. */
    } else if (!pc5_casecmp(word, "pushd")) {
        if (b->pushes < BATCH_PUSHES) pc5_copy(b->pushed[b->pushes++], PC5_PATH, b->cwd);
        batch_cd(b, rest);
    } else if (!pc5_casecmp(word, "popd")) {
        if (b->pushes) pc5_copy(b->cwd, sizeof(b->cwd), b->pushed[--b->pushes]);
    } else if (!pc5_casecmp(word, "set")) {
        batch_set(b, rest);
    } else if (!pc5_casecmp(word, "start")) {
        batch_start(b, rest);
    } else if (!pc5_casecmp(word, "call")) {
        batch_program(b, rest, b->cwd);
    } else {
        batch_program(b, cmd, b->cwd);
    }
}

/* One line: its commands, split at &, &&, | and || outside quotes, ^ escaping the next sign. */
static void batch_line(Batch *b, const char *line)
{
    size_t n = 0;
    int quoted = 0;

    for (const char *p = line;; p++) {
        char c = *p;
        if (c == '"') quoted = !quoted;
        if (!c || (!quoted && (c == '&' || c == '|') && !(n && (b->command[n - 1] == '>' || b->command[n - 1] == '<')))) {
            b->command[n] = 0;
            batch_command(b, b->command);
            n = 0;
            if (!c) break;
            while (p[1] == '&' || p[1] == '|') p++;
            continue;
        }
        if (!quoted && c == '^' && p[1]) c = *++p;
        if (n + 1 < sizeof(b->command)) b->command[n++] = c;
    }
}

/* 1 when line ends in a ^ that carries it on to the next line. */
static int batch_continues(const char *line)
{
    int quoted = 0, carries = 0;

    for (const char *p = line; *p; p++) {
        carries = 0;
        if (*p == '"') quoted = !quoted;
        else if (*p == '^' && !quoted) {
            if (!p[1]) carries = 1;
            else p++;
        }
    }
    return carries;
}

int pc5_batch_read(const char *top, const char *batch, Pc5Batch *out)
{
    char path[PC5_PATH];
    const char *name, *slash;
    size_t top_len = strlen(top);
    Pc5Buf text = { 0 };
    Batch *b;
    int status = -1;

    memset(out, 0, sizeof(*out));
    while (top_len > 1 && top[top_len - 1] == '/') top_len--;
    for (name = top + top_len; name > top && name[-1] != '/'; name--) {}
    if (!(b = pc5_alloc(sizeof(*b)))) return -1;
    b->top = top;
    if (pc5_format(b->windows, sizeof(b->windows), "C:\\Games\\%.*s", (int)(top + top_len - name), name) ||
        pc5_format(b->file, sizeof(b->file), "%s", batch) || pc5_join(path, sizeof(path), top, batch) ||
        pc5_read_file(path, &text, 64 * 1024) || pc5_buf_terminate(&text))
        goto out;
    /* It starts in its own folder, as when it is opened on Windows. */
    pc5_copy(b->cwd, sizeof(b->cwd), batch);
    slash = strrchr(b->cwd, '/');
    if (slash) b->cwd[slash - b->cwd] = 0; else b->cwd[0] = 0;
    char *at = (char *)text.data, *end = memchr(text.data, 0x1a, text.len);
    if (end) *end = 0;                                /* Ctrl-Z: the end of the file, as DOS wrote it */
    if (!strncmp(at, "\xef\xbb\xbf", 3)) at += 3;   /* a UTF-8 mark */
    b->line[0] = 0;
    for (char *next; at; at = next) {
        size_t have = strlen(b->line), len;
        next = strchr(at, '\n');
        if (next) *next++ = 0;
        len = strlen(at);
        if (len && at[len - 1] == '\r') at[--len] = 0;
        pc5_copy(b->line + have, sizeof(b->line) - have, at);
        /* A ^ at the end, outside quotes and not itself escaped, carries the line on. */
        if (next && batch_continues(b->line)) {
            b->line[strlen(b->line) - 1] = 0;
            continue;
        }
        batch_expand(b, b->line, b->expanded, sizeof(b->expanded));
        batch_line(b, b->expanded);
        b->line[0] = 0;
    }
    if (b->have_game) *out = b->game;
    else if (b->have_first) *out = b->first;
    else goto out;
    status = 0;
out:
    pc5_buf_free(&text);
    pc5_free(b);
    return status;
}
