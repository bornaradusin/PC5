/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: the games, installing and uninstalling (pc5_library.h). */
#include "pc5_library.h"
#include "pc5_cover.h"
#include "pc5_image.h"

#include "pw_wine_library.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum { CHUNK = 4 * 1024 * 1024, TEMPLATE_ROOM = 512u * 1024 * 1024, SPARE_ROOM = 1024u * 1024 * 1024,
       REMOVE_STACK = 96 };

uint64_t pc5_now_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static const char *api_key(Pc5Api api)
{
    static const char *const keys[] = { "gdi", "ddraw", "opengl", "vulkan", "d3d8", "d3d9", "d3d10", "d3d11", "d3d12" };
    return (unsigned)api < sizeof(keys) / sizeof(keys[0]) ? keys[api] : "gdi";
}

static Pc5Api api_from_key(const char *key)
{
    for (int api = PC5_API_GDI; api <= PC5_API_D3D12; api++)
        if (!strcmp(key, api_key((Pc5Api)api))) return (Pc5Api)api;
    return PC5_API_GDI;
}

void pc5_detail_text(int bits, Pc5Api api, uint64_t bytes, char *out, size_t size)
{
    char amount[32];

    if (bytes) {
        pc5_size_text(bytes, amount, sizeof(amount));
        snprintf(out, size, "%d-bit \xc2\xb7 %s \xc2\xb7 %s", bits, pc5_api_name(api), amount);
    } else {
        snprintf(out, size, "%d-bit \xc2\xb7 %s", bits, pc5_api_name(api));
    }
}

/* ---- PC5's notes ------------------------------------------------------------------- */

int pc5_info_text(const Pc5Info *info, char *out, size_t size)
{
    return pc5_format(out, size,
                      "title = %s\nfolder = %s\nexe = %s\nbits = %d\napi = %s\nbytes = %llu\nfiles = %u\ncover = %s\n",
                      info->title, info->folder, info->exe, info->bits, api_key(info->api),
                      (unsigned long long)info->bytes, (unsigned)info->files, info->cover);
}

static void trim(char **begin, char **end)
{
    while (*begin < *end && (**begin == ' ' || **begin == '\t')) (*begin)++;
    while (*end > *begin && ((*end)[-1] == ' ' || (*end)[-1] == '\t' || (*end)[-1] == '\r')) (*end)--;
}

int pc5_info_read(const char *root, const char *id, Pc5Info *info)
{
    char path[PC5_PATH];
    Pc5Buf text = { 0 };
    int fields = 0;

    memset(info, 0, sizeof(*info));
    if (pc5_format(path, sizeof(path), "%s/prefixes/%s/pc5/info.txt", root, id) || pc5_read_file(path, &text, 16384)) {
        pc5_buf_free(&text);
        return -1;
    }
    for (char *line = (char *)text.data, *next; line && *line; line = next) {
        char *equals, *key_end, *value, *end;
        next = strchr(line, '\n');
        end = next ? next : line + strlen(line);
        if (next) *next++ = 0;
        if (!(equals = memchr(line, '=', (size_t)(end - line)))) continue;
        key_end = equals;
        value = equals + 1;
        trim(&line, &key_end);
        trim(&value, &end);
        *key_end = 0;
        *end = 0;
        if (!strcmp(line, "title")) { pc5_copy(info->title, sizeof(info->title), value); fields |= 1; }
        else if (!strcmp(line, "folder")) { pc5_copy(info->folder, sizeof(info->folder), value); fields |= 2; }
        else if (!strcmp(line, "exe")) { pc5_copy(info->exe, sizeof(info->exe), value); fields |= 4; }
        else if (!strcmp(line, "bits")) info->bits = atoi(value);
        else if (!strcmp(line, "api")) info->api = api_from_key(value);
        else if (!strcmp(line, "bytes")) info->bytes = strtoull(value, NULL, 10);
        else if (!strcmp(line, "files")) info->files = (uint32_t)strtoul(value, NULL, 10);
        else if (!strcmp(line, "cover")) pc5_copy(info->cover, sizeof(info->cover), value);
    }
    pc5_buf_free(&text);
    if (info->bits != 32 && info->bits != 64) info->bits = 32;
    return fields == 7 ? 0 : -1;
}

/* ---- profiles ---------------------------------------------------------------------- */

static void windows_path(const char *folder, const char *relative, int keep_file, char *out, size_t size)
{
    char inside[PC5_PATH];
    char *slash;

    pc5_copy(inside, sizeof(inside), relative);
    if (!keep_file) {
        slash = strrchr(inside, '/');
        if (slash) *slash = 0;
        else inside[0] = 0;
    }
    for (char *p = inside; *p; p++) if (*p == '/') *p = '\\';
    if (inside[0]) snprintf(out, size, "C:\\Games\\%s\\%s", folder, inside);
    else snprintf(out, size, "C:\\Games\\%s", folder);
}

/* A Microsoft DLL a game may carry, which then goes first (native, then Wine's builtin):
 * the Visual C++ runtimes Proton sets so for every game (its wine.inf's ProtonOverrides;
 * not the *80 and *90 ones, which would need Wine's manifests gone), and the Direct3D
 * compilers and D3DX helpers, whose own copies games ship and Wine's lack parts of. name
 * is lower case, without .dll. */
static int own_dll(const char *name)
{
    static const char *const runtimes[] = {
        "atl100", "msvcp100", "msvcr100", "vcomp100", "atl110", "msvcp110", "msvcr110", "vcomp110", "atl120",
        "msvcp120", "msvcr120", "vcomp120", "api-ms-win-crt-conio-l1-1-0", "api-ms-win-crt-heap-l1-1-0",
        "api-ms-win-crt-locale-l1-1-0", "api-ms-win-crt-math-l1-1-0", "api-ms-win-crt-runtime-l1-1-0",
        "api-ms-win-crt-stdio-l1-1-0", "api-ms-win-crt-time-l1-1-0", "atl140", "concrt140", "msvcp140", "msvcp140_1",
        "msvcp140_2", "msvcp140_atomic_wait", "msvcp140_codecvt_ids", "msvcr140", "ucrtbase", "vcomp140",
        "vcruntime140", "vcruntime140_1",
    };
    static const char *const families[] = { "d3dcompiler_", "d3dx9_", "d3dx10_", "d3dx11_" };
    for (size_t i = 0; i < sizeof(runtimes) / sizeof(runtimes[0]); i++)
        if (!strcmp(name, runtimes[i])) return 1;
    for (size_t i = 0; i < sizeof(families) / sizeof(families[0]); i++) {
        size_t n = strlen(families[i]);
        if (!strncmp(name, families[i], n) && name[n] >= '0' && name[n] <= '9') return 1;
    }
    return 0;
}

static int by_name(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

int pc5_own_dlls(const char *folder, char *out, size_t size)
{
    char names[64][40], *sorted[64];
    size_t count = 0, used = 0;
    DIR *dir;
    struct dirent *entry;

    if (size) out[0] = 0;
    if (!size || !(dir = opendir(folder))) return 0;
    while ((entry = readdir(dir)) && count < 64) {
        size_t n = strlen(entry->d_name);
        if (n < 5 || n - 4 >= sizeof(names[0]) || !pc5_ends_with_ci(entry->d_name, ".dll")) continue;
        for (size_t i = 0; i < n - 4; i++)
            names[count][i] = (char)((entry->d_name[i] >= 'A' && entry->d_name[i] <= 'Z') ? entry->d_name[i] + 32
                                                                                          : entry->d_name[i]);
        names[count][n - 4] = 0;
        if (own_dll(names[count])) {
            sorted[count] = names[count];
            count++;
        }
    }
    closedir(dir);
    qsort(sorted, count, sizeof(sorted[0]), by_name);
    for (size_t i = 0; i < count; i++) {
        size_t n = strlen(sorted[i]);
        if (i && !strcmp(sorted[i], sorted[i - 1])) continue;
        if (used + n + 2 > size) break;
        if (used) out[used++] = ',';
        memcpy(out + used, sorted[i], n + 1);
        used += n;
    }
    return (int)count;
}

/* The desktop of a game drawn through Vulkan (Direct3D, Vulkan, OpenGL), as its
 * Resolution says; others draw on the CPU at 1080p. */
static const char *desktop_size(const Pc5Info *info, const Pc5Controls *controls)
{
    if (info->api <= PC5_API_DDRAW || !controls) return "1920x1080";
    return controls->resolution == 2 ? "3840x2160" : controls->resolution == 1 ? "2560x1440" : "1920x1080";
}

int pc5_profile_text(const char *id, const Pc5Info *info, const char *arguments, const Pc5Controls *controls,
                     const char *own_dlls, char *out, size_t size)
{
    char exe[PC5_PATH + 32], cwd[PC5_PATH + 32], options[600] = "", input[1600], dlls[260];
    Pc5Controls defaults;
    const char *graphics = info->api == PC5_API_OPENGL ? "opengl" : info->api <= PC5_API_DDRAW ? "gdi" : "dxvk";
    /* Direct3D 8 to 11 and DXGI through DXVK, as prospero-win's own profiles. Direct3D 12
     * through VKD3D-Proton only for a game whose program draws with it; any other game gets
     * Wine's own d3d12, as on prospero-win, so a library that merely tries Direct3D 12 (GTA
     * IV's 1911.dll, which then wants DirectComposition, which Wine lacks) fails there
     * quietly instead of getting further and crashing. */
    const char *overrides = info->api == PC5_API_D3D12 ? "d3d8,d3d9,d3d10core,d3d11,d3d12,d3d12core,dxgi=n"
                                                       : "d3d8,d3d9,d3d10core,d3d11,dxgi=n;d3d12,d3d12core=b";

    windows_path(info->folder, info->exe, 1, exe, sizeof(exe));
    windows_path(info->folder, info->exe, 0, cwd, sizeof(cwd));
    if (strlen(exe) >= 260) return -1;
    if (arguments && arguments[0]) snprintf(options, sizeof(options), "arguments = %s\n", arguments);
    if (!controls) {
        pc5_controls_default(&defaults);
        controls = &defaults;
    }
    if (pc5_controls_input(controls, input, sizeof(input))) return -1;
    /* prospero-win takes 256 characters of overrides: the game's own DLLs fill what is left. */
    if (pc5_format(dlls, sizeof(dlls), "%s", overrides)) return -1;
    if (own_dlls && own_dlls[0]) {
        size_t base = strlen(dlls), room = 256 > base + 5 ? 256 - base - 5 : 0, n = strlen(own_dlls);
        while (n > room) {   /* whole names only */
            do n--; while (n && own_dlls[n] != ',');
        }
        if (n) snprintf(dlls + base, sizeof(dlls) - base, ";%.*s=n,b", (int)n, own_dlls);
    }
    return pc5_format(out, size,
        "; %s, installed by PC5 from the folder \"%s\".\n"
        "; PC5 writes this file again each time the game starts: launch options go in\n"
        "; launch.txt beside the game's program.\n"
        "[application]\n"
        "id = %s\n"
        "name = %s\n"
        "executable = %s\n"
        "working_directory = %s\n"
        "%s"
        "dll_overrides = %s\n"
        "prefix = %s\n"
        "runtime = wine-wow64\n"
        "architecture = %s\n"
        "graphics = %s\n"
        "\n"
        "[display]\n"
        "desktop = %s\n"
        "show_fps = false\n"
        "\n"
        "%s",
        info->title, info->folder, id, info->title, exe, cwd, options, dlls, id,
        info->bits == 64 ? "pe64" : "pe32", graphics, desktop_size(info, controls), input);
}

/* The folder the game's program is in, on the console. */
static void program_folder(const char *root, const char *id, const Pc5Info *info, char *out, size_t size)
{
    char dir[PC5_PATH];
    char *slash;

    pc5_copy(dir, sizeof(dir), info->exe);
    slash = strrchr(dir, '/');
    if (slash) *slash = 0; else dir[0] = 0;
    if (pc5_format(out, size, "%s/prefixes/%s/drive_c/Games/%s%s%s", root, id, info->folder, dir[0] ? "/" : "", dir))
        out[0] = 0;
}

/* The first line of launch.txt that says something: launch options. */
static void launch_options(const char *root, const char *id, const Pc5Info *info, char *out, size_t size)
{
    char path[PC5_PATH], dir[PC5_PATH];
    Pc5Buf text = { 0 };
    char *slash;

    out[0] = 0;
    pc5_copy(dir, sizeof(dir), info->exe);
    slash = strrchr(dir, '/');
    if (slash) *slash = 0; else dir[0] = 0;
    if (pc5_format(path, sizeof(path), "%s/prefixes/%s/drive_c/Games/%s%s%s/launch.txt", root, id, info->folder,
                   dir[0] ? "/" : "", dir) ||
        pc5_read_file(path, &text, 4096)) {
        pc5_buf_free(&text);
        return;
    }
    for (char *line = (char *)text.data, *next; line && *line; line = next) {
        char *end;
        next = strchr(line, '\n');
        end = next ? next : line + strlen(line);
        if (next) next++;
        trim(&line, &end);
        if (line == end || *line == '#' || *line == ';') continue;
        size_t n = 0;
        for (char *p = line; p < end && n + 1 < size && n < 500; p++)
            if ((unsigned char)*p >= 0x20 && *p != 0x7f) out[n++] = *p;
        out[n] = 0;
        break;
    }
    pc5_buf_free(&text);
}

int pc5_game_folder(const char *root, const char *id, char *out, size_t size)
{
    Pc5Info info;

    if (pc5_info_read(root, id, &info)) return -1;
    return pc5_format(out, size, "%s/prefixes/%s/drive_c/Games/%s", root, id, info.folder);
}

int pc5_game_set_program(const char *root, const char *id, const Pc5Folder *folder, int exe, char *error, size_t size)
{
    Pc5Info info;
    char path[PC5_PATH], text[2048], windows[PC5_PATH + 32];

    if (pc5_info_read(root, id, &info)) {
        snprintf(error, size, "PC5's notes on this game are missing.");
        return -1;
    }
    if (exe < 0 || exe >= folder->exe_count) {
        snprintf(error, size, "Choose one of the game's programs.");
        return -1;
    }
    pc5_copy(info.exe, sizeof(info.exe), folder->exes[exe].path);
    info.bits = folder->exes[exe].pe.bits;
    info.api = pc5_folder_api(folder, exe);
    windows_path(info.folder, info.exe, 1, windows, sizeof(windows));
    if (strlen(windows) >= 260) {
        snprintf(error, size, "That program is too deep in the game's folder for Windows.");
        return -1;
    }
    if (pc5_info_text(&info, text, sizeof(text)) ||
        pc5_format(path, sizeof(path), "%s/prefixes/%s/pc5/info.txt", root, id) || pc5_write_file(path, text, strlen(text))) {
        snprintf(error, size, "PC5 can't write its notes: %s.", strerror(errno));
        return -1;
    }
    return pc5_profile_refresh(root, id, error, size);
}

/* profiles.lst, when there is one: the line for file added or taken out. */
static int index_update(const char *root, const char *file, int add)
{
    char path[PC5_PATH];
    Pc5Buf text = { 0 }, out = { 0 };
    int present = 0, status = 0;

    if (pc5_format(path, sizeof(path), "%s/profiles/profiles.lst", root) || !pc5_is_file(path)) return 0;
    if (pc5_read_file(path, &text, 64 * 1024)) { pc5_buf_free(&text); return -1; }
    for (char *line = (char *)text.data, *next; line && *line; line = next) {
        char *end;
        next = strchr(line, '\n');
        end = next ? next : line + strlen(line);
        if (next) next++;
        char *begin = line;
        trim(&begin, &end);
        int same = (size_t)(end - begin) == strlen(file) && !memcmp(begin, file, strlen(file));
        present |= same;
        if (same && !add) continue;
        if (end > begin) { pc5_buf_put(&out, begin, (size_t)(end - begin)); pc5_buf_puts(&out, "\n"); }
    }
    if (add && !present) { pc5_buf_puts(&out, file); pc5_buf_puts(&out, "\n"); }
    if ((add && !present) || (!add && present)) status = pc5_write_file(path, out.data ? out.data : (uint8_t *)"", out.len);
    pc5_buf_free(&text);
    pc5_buf_free(&out);
    return status;
}

int pc5_profile_refresh(const char *root, const char *id, char *error, size_t size)
{
    Pc5Info info;
    Pc5Controls controls;
    char arguments[512], path[PC5_PATH], file[PC5_ID + 16], folder[PC5_PATH], own[256];
    static char text[4096];
    Pc5Buf old = { 0 };
    int status = 0;

    if (pc5_info_read(root, id, &info)) {
        snprintf(error, size, "PC5's notes on this game are missing.");
        return -1;
    }
    launch_options(root, id, &info, arguments, sizeof(arguments));
    pc5_controls_load(root, id, &controls);
    program_folder(root, id, &info, folder, sizeof(folder));
    pc5_own_dlls(folder, own, sizeof(own));
    snprintf(file, sizeof(file), "%s.profile", id);
    if (pc5_profile_text(id, &info, arguments, &controls, own, text, sizeof(text)) ||
        pc5_format(path, sizeof(path), "%s/profiles/%s", root, file)) {
        snprintf(error, size, "The game's path is too long for Windows.");
        return -1;
    }
    if (pc5_read_file(path, &old, sizeof(text)) || old.len != strlen(text) || memcmp(old.data, text, old.len))
        status = pc5_write_file(path, text, strlen(text));
    pc5_buf_free(&old);
    if (status) snprintf(error, size, "PC5 can't write the game's profile: %s.", strerror(errno));
    else index_update(root, file, 1);
    return status;
}

/* ---- the library ------------------------------------------------------------------- */

static int by_title(const void *a, const void *b)
{
    const Pc5Game *x = a, *y = b;
    int order = pc5_casecmp(x->title, y->title);
    return order ? order : strcmp(x->id, y->id);
}

static int count_leftovers(const char *root)
{
    char path[PC5_PATH];
    DIR *dir;
    struct dirent *entry;
    int count = 0;

    if (pc5_format(path, sizeof(path), "%s/prefixes", root) || !(dir = opendir(path))) return 0;
    while ((entry = readdir(dir)))
        count += !strncmp(entry->d_name, ".installing-", 12) || !strncmp(entry->d_name, ".removing-", 10);
    closedir(dir);
    return count;
}

int pc5_library_load(const char *root, Pc5Library *library)
{
    PwWineLibrary *loaded = pc5_alloc(sizeof(*loaded));

    memset(library, 0, sizeof(*library));
    if (!loaded) return -1;
    pw_wine_library_load(loaded, root);
    for (uint32_t i = 0; i < loaded->count && library->count < PC5_MAX_GAMES; i++) {
        const PwWineLibraryEntry *entry = &loaded->entries[i];
        Pc5Game *game = &library->games[library->count++];
        char marker[PC5_PATH];
        Pc5Info info;

        memset(game, 0, sizeof(*game));
        pc5_copy(game->file, sizeof(game->file), entry->file);
        if (entry->status != PW_OK) {
            snprintf(game->id, sizeof(game->id), "%.*s", (int)(strlen(entry->file) > 8 ? strlen(entry->file) - 8 : 0),
                     entry->file);
            pc5_copy(game->title, sizeof(game->title), game->id);
            snprintf(game->problem, sizeof(game->problem), "Its profile has a mistake (%s).", pw_result_name(entry->status));
            /* A game PC5 installed has its own folder, named after it: uninstalling
             * it deletes that folder too, not only the profile. */
            if (!pc5_info_read(root, game->id, &info)) {
                pc5_copy(game->prefix, sizeof(game->prefix), game->id);
                pc5_copy(game->title, sizeof(game->title), info.title[0] ? info.title : game->id);
            }
            continue;
        }
        const PwAppProfile *app = &entry->profile.app;
        pc5_copy(game->id, sizeof(game->id), app->id);
        pc5_copy(game->title, sizeof(game->title), app->name);
        pc5_copy(game->prefix, sizeof(game->prefix), app->prefix);
        pc5_copy(game->executable, sizeof(game->executable), app->executable);
        if (strcmp(app->prefix, "default")) pc5_format(marker, sizeof(marker), "%s/prefixes/%s/system.reg", root, app->prefix);
        else pc5_format(marker, sizeof(marker), "%s/prefix/system.reg", root);
        game->playable = pc5_is_file(marker);
        if (!game->playable) snprintf(game->problem, sizeof(game->problem), "Its Windows folder is missing.");
        if (!strcmp(app->prefix, app->id) && !pc5_info_read(root, app->id, &info)) {
            game->pc5 = 1;
            game->bits = info.bits == 64 ? 64 : 32;
            game->vulkan = info.api > PC5_API_DDRAW;
            pc5_detail_text(info.bits, info.api, info.bytes, game->detail, sizeof(game->detail));
        } else {
            Pc5Api api = app->graphics == PW_APP_GRAPHICS_ZINK ? PC5_API_OPENGL :
                         app->graphics == PW_APP_GRAPHICS_DXVK ? PC5_API_D3D9 : PC5_API_GDI;
            game->bits = app->architecture == PW_APP_ARCH_PE64 ? 64 : 32;
            game->vulkan = 0;   /* not PC5's: its profile's desktop is its own */
            snprintf(game->detail, sizeof(game->detail), "%d-bit \xc2\xb7 %s",
                     game->bits,
                     api == PC5_API_D3D9 ? "DirectX" : pc5_api_name(api));
        }
    }
    pc5_free(loaded);
    qsort(library->games, (size_t)library->count, sizeof(library->games[0]), by_title);
    library->leftovers = count_leftovers(root);
    return 0;
}

int pc5_library_find(const Pc5Library *library, const char *id)
{
    for (int i = 0; i < library->count; i++)
        if (!strcmp(library->games[i].id, id)) return i;
    return -1;
}

int pc5_new_id(const char *root, const char *name, char *id, size_t size)
{
    char base[48], candidate[PC5_ID], path[PC5_PATH];
    size_t n = 0;
    PwWineLibrary *loaded = pc5_alloc(sizeof(*loaded));
    int status = -1;

    for (const unsigned char *p = (const unsigned char *)name; *p && n + 1 < sizeof(base); p++) {
        int c = pc5_lower(*p);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) base[n++] = (char)c;
        else if (n && base[n - 1] != '-') base[n++] = '-';
    }
    while (n && base[n - 1] == '-') n--;
    base[n] = 0;
    if (!n) snprintf(base, sizeof(base), "game");
    if (!loaded) return -1;
    pw_wine_library_load(loaded, root);
    for (int k = 1; k < 1000 && status; k++) {
        int taken = 0;
        if (k == 1) pc5_copy(candidate, sizeof(candidate), base);
        else snprintf(candidate, sizeof(candidate), "%s-%d", base, k);
        static const char *const places[] = { "profiles/%s.profile", "prefixes/%s", "prefixes/.installing-%s",
                                              "prefixes/.removing-%s" };
        for (size_t i = 0; i < 4 && !taken; i++) {
            char format[64];
            snprintf(format, sizeof(format), "%%s/%s", places[i]);
            if (!pc5_format(path, sizeof(path), format, root, candidate)) taken |= pc5_exists(path);
        }
        for (uint32_t i = 0; i < loaded->count && !taken; i++)
            taken |= loaded->entries[i].status == PW_OK && (!strcmp(loaded->entries[i].profile.app.id, candidate) ||
                                                             !strcmp(loaded->entries[i].profile.app.prefix, candidate));
        if (!taken) status = pc5_format(id, size, "%s", candidate);
    }
    pc5_free(loaded);
    return status;
}

/* ---- deleting a folder, step by step ------------------------------------------------- */

int pc5_tree_delete_begin(Pc5Remove *remove, const char *path)
{
    if (!remove->stack && !(remove->stack = pc5_alloc(sizeof(*remove->stack) * REMOVE_STACK))) return -1;
    remove->top = 0;
    remove->done = remove->failed = 0;
    if (!pc5_exists(path)) { remove->done = 1; return 0; }
    pc5_copy(remove->stack[remove->top++], PC5_PATH, path);
    pc5_copy(remove->path, sizeof(remove->path), path);
    return 0;
}

int pc5_remove_step(Pc5Remove *remove, int budget_ms)
{
    uint64_t until = pc5_now_ms() + (uint64_t)(budget_ms > 0 ? budget_ms : 1);

    while (remove->top > 0 && !remove->failed) {
        char *path = remove->stack[remove->top - 1], child[PC5_PATH], deeper[PC5_PATH] = "";
        struct stat st;
        DIR *dir;
        struct dirent *entry;

        if (lstat(path, &st)) { remove->top--; continue; }
        if (!S_ISDIR(st.st_mode)) {
            if (unlink(path) && errno != ENOENT) goto failed;
            remove->removed++;
            remove->top--;
            continue;
        }
        if (!(dir = opendir(path))) goto failed;
        while ((entry = readdir(dir))) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
            if (pc5_join(child, sizeof(child), path, entry->d_name)) { closedir(dir); errno = ENAMETOOLONG; goto failed; }
            if (lstat(child, &st)) continue;
            if (S_ISDIR(st.st_mode)) {
                if (!deeper[0]) memcpy(deeper, child, sizeof(deeper));
            } else {
                if (unlink(child) && errno != ENOENT) { closedir(dir); goto failed; }
                remove->removed++;
            }
        }
        closedir(dir);
        if (deeper[0]) {
            if (remove->top >= REMOVE_STACK) { errno = ENAMETOOLONG; goto failed; }
            memcpy(remove->stack[remove->top++], deeper, PC5_PATH);
        } else {
            if (rmdir(path) && errno != ENOENT) goto failed;
            remove->removed++;
            remove->top--;
        }
        if (pc5_now_ms() >= until) break;
        continue;
failed:
        remove->failed = 1;
        snprintf(remove->error, sizeof(remove->error), "PC5 can't delete %s: %s.", path, strerror(errno));
        return 0;
    }
    if (!remove->top && !remove->failed) remove->done = 1;
    return remove->top > 0 && !remove->failed;
}

void pc5_remove_end(Pc5Remove *remove)
{
    pc5_free(remove->stack);
    remove->stack = NULL;
    remove->top = 0;
}

/* Session logs (prospero-win's, shared) that a game wrote. */
static void delete_logs(const char *root, const char *id)
{
    char path[PC5_PATH], file[PC5_PATH], wanted[PC5_ID + 16], head[512];
    DIR *dir;
    struct dirent *entry;

    if (pc5_format(path, sizeof(path), "%s/logs", root) || !(dir = opendir(path))) return;
    snprintf(wanted, sizeof(wanted), " profile=%s ", id);
    while ((entry = readdir(dir))) {
        int fd;
        ssize_t got;
        if (strncmp(entry->d_name, "session-", 8) || !pc5_ends_with_ci(entry->d_name, ".log") ||
            pc5_join(file, sizeof(file), path, entry->d_name) || (fd = open(file, O_RDONLY)) < 0)
            continue;
        got = read(fd, head, sizeof(head) - 1);
        close(fd);
        if (got <= 0) continue;
        head[got] = 0;
        char *end = strchr(head, '\n');
        if (end) *end = 0;
        if (strstr(head, wanted)) unlink(file);
    }
    closedir(dir);
}

int pc5_remove_begin(Pc5Remove *remove, const char *root, const Pc5Game *game, const Pc5Library *library)
{
    char from[PC5_PATH], to[PC5_PATH], profile[PC5_PATH];
    int shared = 0;

    memset(remove, 0, sizeof(*remove));
    pc5_copy(remove->root, sizeof(remove->root), root);
    for (int i = 0; library && i < library->count; i++)
        shared |= &library->games[i] != game && library->games[i].prefix[0] && !strcmp(library->games[i].prefix, game->prefix);
    /* The folder first, out of the way: from here on the game is gone, and a
     * cut-short delete is finished at the next start. */
    if (game->prefix[0] && strcmp(game->prefix, "default") && !shared) {
        if (pc5_format(from, sizeof(from), "%s/prefixes/%s", root, game->prefix)) goto too_long;
        for (int k = 1; k < 100; k++) {
            if (k == 1 ? pc5_format(to, sizeof(to), "%s/prefixes/.removing-%s", root, game->prefix)
                       : pc5_format(to, sizeof(to), "%s/prefixes/.removing-%s-%d", root, game->prefix, k))
                goto too_long;
            if (!pc5_exists(to)) break;
        }
        if (pc5_exists(from) && rename(from, to)) {
            snprintf(remove->error, sizeof(remove->error), "PC5 can't move the game's folder: %s.", strerror(errno));
            remove->failed = 1;
            return -1;
        }
    } else {
        to[0] = 0;
    }
    if (pc5_format(profile, sizeof(profile), "%s/profiles/%s", root, game->file)) goto too_long;
    if (unlink(profile) && errno != ENOENT) {
        snprintf(remove->error, sizeof(remove->error), "PC5 can't delete the game's profile: %s.", strerror(errno));
        remove->failed = 1;
        return -1;
    }
    index_update(root, game->file, 0);
    delete_logs(root, game->id);
    if (to[0]) return pc5_tree_delete_begin(remove, to);
    remove->done = 1;
    return 0;
too_long:
    snprintf(remove->error, sizeof(remove->error), "A path is too long.");
    remove->failed = 1;
    return -1;
}

int pc5_recover_step(const char *root, Pc5Remove *remove, int budget_ms)
{
    char path[PC5_PATH], leftover[PC5_PATH] = "";
    DIR *dir;
    struct dirent *entry;

    if (remove->top > 0 && !remove->failed) return pc5_remove_step(remove, budget_ms) || 1;
    if (remove->failed) return 0;
    if (pc5_format(path, sizeof(path), "%s/prefixes", root) || !(dir = opendir(path))) return 0;
    while ((entry = readdir(dir))) {
        if (!strncmp(entry->d_name, ".installing-", 12) || !strncmp(entry->d_name, ".removing-", 10)) {
            pc5_join(leftover, sizeof(leftover), path, entry->d_name);
            break;
        }
    }
    if (!leftover[0]) {
        /* A PC5 game whose folder is in place but whose profile is not (the
         * install stopped between the two): its profile again. */
        rewinddir(dir);
        while ((entry = readdir(dir))) {
            char profile[PC5_PATH], error[PC5_ERROR];
            Pc5Info info;
            if (entry->d_name[0] == '.' || pc5_format(profile, sizeof(profile), "%s/profiles/%s.profile", root, entry->d_name))
                continue;
            if (!pc5_exists(profile) && !pc5_info_read(root, entry->d_name, &info))
                pc5_profile_refresh(root, entry->d_name, error, sizeof(error));
        }
    }
    closedir(dir);
    if (!leftover[0]) return 0;
    remove->failed = 0;
    if (pc5_tree_delete_begin(remove, leftover)) return 0;
    return 1;
}

/* ---- installing --------------------------------------------------------------------------- */

uint64_t pc5_install_needs(const Pc5Folder *folder)
{
    return folder->bytes + TEMPLATE_ROOM + SPARE_ROOM;
}

static int fail(Pc5Install *install, const char *format, ...) __attribute__((format(printf, 2, 3)));
static int fail(Pc5Install *install, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(install->error, sizeof(install->error), format, args);
    va_end(args);
    return -1;
}

static void close_files(Pc5Install *install)
{
    if (install->in_fd >= 0) close(install->in_fd);
    if (install->out_fd >= 0) close(install->out_fd);
    install->in_fd = install->out_fd = -1;
}

/* The template's paths in the order they are copied: folders and files,
 * then the registry, system.reg last (the marker of a whole prefix). */
static int template_paths(Pc5Install *install)
{
    char list_path[PC5_PATH];
    Pc5Buf text = { 0 };
    int has_system = 0;

    if (pc5_format(list_path, sizeof(list_path), "%s.list", install->template_prefix) ||
        pc5_read_file(list_path, &text, 4 * 1024 * 1024)) {
        pc5_buf_free(&text);
        return fail(install, "PC5's Windows template is missing from the app: copy the PC5 folder again.");
    }
    for (size_t i = 0; i < text.len; i++)
        if (text.data[i] == '\n' || text.data[i] == '\r') text.data[i] = 0;
    for (int pass = 0; pass < 3; pass++) {
        for (size_t at = 0; at < text.len; at += strlen((char *)text.data + at) + 1) {
            const char *line = (const char *)text.data + at;
            size_t length = strlen(line);
            if (!length || line[0] == '/' || strstr(line, "..")) continue;
            int registry = length > 4 && !strchr(line, '/') && !strcmp(line + length - 4, ".reg");
            int system = !strcmp(line, "system.reg");
            has_system |= system;
            if (pass == 0 ? registry : pass == 1 ? !registry || system : !system) continue;
            if (line[length - 1] != '/') {
                char source[PC5_PATH];
                struct stat st;
                if (!pc5_join(source, sizeof(source), install->template_prefix, line) && !stat(source, &st))
                    install->bytes_total += (uint64_t)st.st_size;
                install->files_total++;
            }
            pc5_buf_put(&install->template_list, line, length + 1);
        }
    }
    pc5_buf_free(&text);
    if (!has_system) return fail(install, "PC5's Windows template is damaged: copy the PC5 folder again.");
    return 0;
}

int pc5_install_begin(Pc5Install *install, const char *root, const char *template_prefix, const Pc5Folder *folder,
                      int exe, Pc5Image *cover, uint32_t tint, const char *cover_source)
{
    char path[PC5_PATH], exe_path[PC5_PATH + 32];
    uint64_t free_bytes = 0;

    memset(install, 0, sizeof(*install));
    install->in_fd = install->out_fd = -1;
    install->phase = PC5_INSTALL_FAILED;
    install->cover = *cover;
    *cover = (Pc5Image){ 0 };
    install->tint = tint;
    if (exe < 0 || exe >= folder->exe_count || folder->problem[0])
        return fail(install, "%s", folder->problem[0] ? folder->problem : "Choose the game's program first.");
    pc5_copy(install->root, sizeof(install->root), root);
    pc5_copy(install->template_prefix, sizeof(install->template_prefix), template_prefix);
    pc5_copy(install->source, sizeof(install->source), folder->path);
    Pc5Info *info = &install->info;
    pc5_copy(info->title, sizeof(info->title), folder->name);
    pc5_copy(info->folder, sizeof(info->folder), folder->name);
    pc5_copy(info->exe, sizeof(info->exe), folder->exes[exe].path);
    info->bits = folder->exes[exe].pe.bits;
    info->api = pc5_folder_api(folder, exe);
    info->bytes = folder->bytes;
    info->files = folder->files;
    pc5_copy(info->cover, sizeof(info->cover), cover_source && cover_source[0] ? cover_source : "icon");
    windows_path(info->folder, info->exe, 1, exe_path, sizeof(exe_path));
    if (strlen(exe_path) >= 260) return fail(install, "The game's program is too deep in its folder for Windows.");
    if (pc5_format(path, sizeof(path), "%s/profiles", root) || pc5_mkdirs(path) ||
        pc5_format(path, sizeof(path), "%s/prefixes", root) || pc5_mkdirs(path))
        return fail(install, "PC5 can't make its folders in %s: %s.", root, strerror(errno));
    if (pc5_free_bytes(path, &free_bytes) == 0 && free_bytes < pc5_install_needs(folder)) {
        char need[32], have[32];
        pc5_size_text(pc5_install_needs(folder), need, sizeof(need));
        pc5_size_text(free_bytes, have, sizeof(have));
        return fail(install, "Not enough space: the game needs %s and the console has %s free.", need, have);
    }
    if (pc5_new_id(root, folder->name, install->id, sizeof(install->id)))
        return fail(install, "PC5 can't name the game.");
    if (pc5_format(install->staging, sizeof(install->staging), "%s/prefixes/.installing-%s", root, install->id) ||
        mkdir(install->staging, 0777))
        return fail(install, "PC5 can't start the install: %s.", strerror(errno));
    if (template_paths(install)) {
        rmdir(install->staging);
        return -1;
    }
    if (!(install->chunk = pc5_alloc(CHUNK))) {
        rmdir(install->staging);
        return fail(install, "Out of memory.");
    }
    /* Step 1's progress: the template's bytes and files (template_paths). */
    install->phase = PC5_INSTALL_PREFIX;
    return 0;
}

/* One template entry: a folder made or a file copied whole. */
static int prefix_entry(Pc5Install *install, const char *line)
{
    char source[PC5_PATH], target[PC5_PATH];
    size_t length = strlen(line);
    int in, out, ok = 1;
    ssize_t got;

    if (pc5_join(source, sizeof(source), install->template_prefix, line) ||
        pc5_join(target, sizeof(target), install->staging, line))
        return fail(install, "A path in the Windows template is too long.");
    if (line[length - 1] == '/') {
        target[strlen(target) - 1] = 0;
        return pc5_mkdirs(target) ? fail(install, "PC5 can't make %s: %s.", line, strerror(errno)) : 0;
    }
    {
        char *slash = strrchr(target, '/');
        *slash = 0;
        if (pc5_mkdirs(target)) return fail(install, "PC5 can't make %s: %s.", target, strerror(errno));
        *slash = '/';
    }
    if ((in = open(source, O_RDONLY)) < 0) return fail(install, "PC5's Windows template is missing %s.", line);
    if ((out = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0) {
        close(in);
        return fail(install, "PC5 can't write %s: %s.", line, strerror(errno));
    }
    while (ok && (got = read(in, install->chunk, CHUNK)) != 0) {
        if (got < 0) { ok = 0; break; }
        for (ssize_t done = 0; ok && done < got;) {
            ssize_t put = write(out, install->chunk + done, (size_t)(got - done));
            if (put <= 0) ok = 0; else done += put;
        }
        install->bytes_done += (uint64_t)got;
    }
    close(in);
    if (close(out)) ok = 0;
    install->files_done++;
    return ok ? 0 : fail(install, "PC5 can't copy %s: %s.", line, errno == ENOSPC ? "the console is full" : strerror(errno));
}

/* Every folder and file of the game, in walking order. */
static int game_paths(Pc5Install *install)
{
    enum { STACK = 4096 };
    char (*stack)[PC5_PATH] = pc5_alloc(sizeof(*stack) * STACK);
    int top = 0;
    uint32_t files = 0;

    if (!stack) return fail(install, "Out of memory.");
    stack[top++][0] = 0;
    while (top > 0) {
        char relative[PC5_PATH], dir_path[PC5_PATH], child[PC5_PATH], full[PC5_PATH];
        DIR *dir;
        struct dirent *entry;
        struct stat st;

        memcpy(relative, stack[--top], PC5_PATH);
        if (relative[0] ? pc5_join(dir_path, sizeof(dir_path), install->source, relative)
                        : pc5_format(dir_path, sizeof(dir_path), "%s", install->source))
            continue;
        if (!(dir = opendir(dir_path))) {
            pc5_free(stack);
            return fail(install, "PC5 can't read %s on the USB drive: %s.", relative[0] ? relative : "the folder",
                        strerror(errno));
        }
        while ((entry = readdir(dir))) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
            if ((relative[0] ? pc5_format(child, sizeof(child), "%s/%s", relative, entry->d_name)
                             : pc5_format(child, sizeof(child), "%s", entry->d_name)) ||
                pc5_join(full, sizeof(full), install->source, child) || lstat(full, &st))
                continue;
            if (S_ISDIR(st.st_mode)) {
                if (top < STACK) memcpy(stack[top++], child, PC5_PATH);
                pc5_buf_puts(&install->list, child);
                pc5_buf_put(&install->list, "/", 2);
            } else if (S_ISREG(st.st_mode)) {
                pc5_buf_put(&install->list, child, strlen(child) + 1);
                files++;
            }
        }
        closedir(dir);
    }
    pc5_free(stack);
    install->files_total = files;
    if (!(install->hashes = pc5_alloc(32u * (files ? files : 1)))) return fail(install, "Out of memory.");
    return 0;
}

static int game_target(Pc5Install *install, const char *relative, char *out, size_t size)
{
    return pc5_format(out, size, "%s/drive_c/Games/%s%s%s", install->staging, install->info.folder,
                      relative[0] ? "/" : "", relative);
}

/* Copies (or, verifying, reads back) a piece of the current file. 1 when the
 * file is finished. */
static int copy_piece(Pc5Install *install, int verify, const char *relative)
{
    ssize_t got = read(install->in_fd, install->chunk, CHUNK);

    if (got < 0)
        return fail(install, verify ? "PC5 can't read back %s: %s." : "The USB drive stopped answering while PC5 copied %s (%s).",
                    relative, strerror(errno));
    if (got == 0) return 1;
    pc5_sha256_update(&install->sha, install->chunk, (size_t)got);
    if (!verify)
        for (ssize_t done = 0; done < got;) {
            ssize_t put = write(install->out_fd, install->chunk + done, (size_t)(got - done));
            if (put <= 0)
                return fail(install, "PC5 can't write %s: %s.", relative,
                            errno == ENOSPC ? "the console's storage is full" : strerror(errno));
            done += put;
        }
    install->bytes_done += (uint64_t)got;
    return 0;
}

static void start_cleanup(Pc5Install *install, int cancelled)
{
    close_files(install);
    if (cancelled) install->error[0] = 0;
    install->cancelled = cancelled;
    if (!install->cleanup) install->cleanup = pc5_alloc(sizeof(Pc5Remove));
    if (install->cleanup) {
        memset(install->cleanup, 0, sizeof(*install->cleanup));
        pc5_tree_delete_begin(install->cleanup, install->staging);
    }
    install->phase = PC5_INSTALL_CLEANUP;
}

static int finish(Pc5Install *install)
{
    char path[PC5_PATH], target[PC5_PATH], text[2048], error[PC5_ERROR];

    if (pc5_format(path, sizeof(path), "%s/pc5", install->staging) || pc5_mkdirs(path))
        return fail(install, "PC5 can't write its notes: %s.", strerror(errno));
    if (pc5_info_text(&install->info, text, sizeof(text)) ||
        pc5_format(path, sizeof(path), "%s/pc5/info.txt", install->staging) || pc5_write_file(path, text, strlen(text)))
        return fail(install, "PC5 can't write its notes: %s.", strerror(errno));
    if (install->cover.px && (pc5_format(path, sizeof(path), "%s/pc5/cover.bin", install->staging) ||
                              pc5_cover_save(path, &install->cover, install->tint)))
        return fail(install, "PC5 can't save the cover: %s.", strerror(errno));
    /* The picture as it was downloaded, for a cover made again later without the network. */
    if (install->original.len && !pc5_format(path, sizeof(path), "%s/pc5/%s", install->staging,
                                              pc5_picture_name(&install->original)))
        pc5_write_file(path, install->original.data, install->original.len);
    if (pc5_format(target, sizeof(target), "%s/prefixes/%s", install->root, install->id) || rename(install->staging, target))
        return fail(install, "PC5 can't put the game in place: %s.", strerror(errno));
    pc5_copy(install->staging, sizeof(install->staging), target);
    if (pc5_profile_refresh(install->root, install->id, error, sizeof(error))) {
        /* The folder is in place: the next start writes the profile again. */
        return fail(install, "%s", error);
    }
    return 0;
}

Pc5InstallPhase pc5_install_step(Pc5Install *install, int budget_ms)
{
    uint64_t until = pc5_now_ms() + (uint64_t)(budget_ms > 0 ? budget_ms : 1);

    if (install->cancel && install->phase < PC5_INSTALL_FINISH) start_cleanup(install, 1);
    while (pc5_now_ms() < until) {
        switch (install->phase) {
        case PC5_INSTALL_PREFIX:
            if (install->at >= install->template_list.len) {
                install->at = 0;
                /* Steps 2 and 3 count the game's bytes and files. */
                install->bytes_done = 0;
                install->bytes_total = install->info.bytes;
                install->files_done = 0;
                install->files_total = install->info.files;
                install->phase = PC5_INSTALL_LIST;
                break;
            }
            if (prefix_entry(install, (const char *)install->template_list.data + install->at)) {
                start_cleanup(install, 0);
                break;
            }
            install->at += strlen((const char *)install->template_list.data + install->at) + 1;
            break;
        case PC5_INSTALL_LIST: {
            char games[PC5_PATH];
            if (game_paths(install) || game_target(install, "", games, sizeof(games)) || pc5_mkdirs(games)) {
                if (!install->error[0]) fail(install, "PC5 can't make the game's folder: %s.", strerror(errno));
                start_cleanup(install, 0);
                break;
            }
            install->at = 0;
            install->file_index = 0;
            install->phase = PC5_INSTALL_COPY;
            return install->phase;   /* the listing may have taken a while: show it */
        }
        case PC5_INSTALL_COPY:
        case PC5_INSTALL_VERIFY: {
            int verify = install->phase == PC5_INSTALL_VERIFY;
            if (install->at >= install->list.len) {
                close_files(install);
                if (verify) {
                    install->phase = PC5_INSTALL_FINISH;
                } else {
                    install->phase = PC5_INSTALL_VERIFY;
                    install->at = 0;
                    install->file_index = 0;
                    install->bytes_done = 0;
                    install->bytes_total = install->info.bytes;
                    install->files_done = 0;
                }
                break;
            }
            const char *relative = (const char *)install->list.data + install->at;
            size_t length = strlen(relative);
            char source[PC5_PATH], target[PC5_PATH];
            if (game_target(install, relative, target, sizeof(target)) ||
                pc5_join(source, sizeof(source), install->source, relative)) {
                fail(install, "A path in the game is too long: %s.", relative);
                start_cleanup(install, 0);
                break;
            }
            if (relative[length - 1] == '/') {
                target[strlen(target) - 1] = 0;
                if (!verify && pc5_mkdirs(target)) {
                    fail(install, "PC5 can't make %s: %s.", relative, strerror(errno));
                    start_cleanup(install, 0);
                    break;
                }
                install->at += length + 1;
                break;
            }
            if (install->in_fd < 0) {
                install->in_fd = open(verify ? target : source, O_RDONLY);
                if (install->in_fd < 0) {
                    fail(install, verify ? "PC5 can't read back %s: %s." : "PC5 can't open %s on the USB drive: %s.",
                         relative, strerror(errno));
                    start_cleanup(install, 0);
                    break;
                }
                if (!verify && (install->out_fd = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0) {
                    fail(install, "PC5 can't write %s: %s.", relative, strerror(errno));
                    start_cleanup(install, 0);
                    break;
                }
                pc5_sha256_init(&install->sha);
            }
            int ended = copy_piece(install, verify, relative);
            if (ended < 0) { start_cleanup(install, 0); break; }
            if (ended) {
                uint8_t digest[32];
                pc5_sha256_final(&install->sha, digest);
                if (!verify && install->out_fd >= 0 && close(install->out_fd)) {
                    install->out_fd = -1;
                    fail(install, "PC5 can't write %s: %s.", relative, strerror(errno));
                    start_cleanup(install, 0);
                    break;
                }
                install->out_fd = -1;
                close(install->in_fd);
                install->in_fd = -1;
                if (install->file_index < install->files_total) {
                    if (!verify) {
                        memcpy(install->hashes[install->file_index], digest, 32);
                    } else if (memcmp(install->hashes[install->file_index], digest, 32)) {
                        fail(install, "The copy of %s doesn't match the USB drive. The drive or its cable may be "
                                      "failing; nothing was kept.", relative);
                        start_cleanup(install, 0);
                        break;
                    }
                }
                install->file_index++;
                install->files_done++;
                install->at += length + 1;
            }
            break;
        }
        case PC5_INSTALL_FINISH:
            if (finish(install)) {
                /* Before the rename the folder is deleted; after it the game
                 * is in place and only its profile is missing. */
                if (strstr(install->staging, "/.installing-")) start_cleanup(install, 0);
                else install->phase = PC5_INSTALL_FAILED;
                break;
            }
            install->phase = PC5_INSTALL_DONE;
            return install->phase;
        case PC5_INSTALL_CLEANUP:
            if (!install->cleanup || !pc5_remove_step(install->cleanup, (int)(until - pc5_now_ms()) + 1)) {
                if (install->cleanup && install->cleanup->failed && !install->error[0])
                    pc5_copy(install->error, sizeof(install->error), install->cleanup->error);
                install->phase = install->cancelled ? PC5_INSTALL_CANCELLED : PC5_INSTALL_FAILED;
            }
            break;
        default:
            return install->phase;
        }
    }
    return install->phase;
}

void pc5_install_cancel(Pc5Install *install)
{
    install->cancel = 1;
}

void pc5_install_end(Pc5Install *install)
{
    close_files(install);
    pc5_buf_free(&install->template_list);
    pc5_buf_free(&install->list);
    pc5_free(install->hashes);
    pc5_free(install->chunk);
    if (install->cleanup) {
        pc5_remove_end(install->cleanup);
        pc5_free(install->cleanup);
    }
    pc5_image_free(&install->cover);
    pc5_buf_free(&install->original);
    install->hashes = NULL;
    install->chunk = NULL;
    install->cleanup = NULL;
}
