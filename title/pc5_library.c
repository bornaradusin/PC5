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
                      "title = %s\nfolder = %s\nexe = %s\nbits = %d\napi = %s\nbytes = %llu\nfiles = %u\ncover = %s\n%s%s%s",
                      info->title, info->folder, info->exe, info->bits, api_key(info->api),
                      (unsigned long long)info->bytes, (unsigned)info->files, info->cover,
                      info->usb[0] ? "usb = " : "", info->usb, info->usb[0] ? "\n" : "");
}

static void trim(char **begin, char **end)
{
    while (*begin < *end && (**begin == ' ' || **begin == '\t')) (*begin)++;
    while (*end > *begin && ((*end)[-1] == ' ' || (*end)[-1] == '\t' || (*end)[-1] == '\r')) (*end)--;
}

static int info_read_file(const char *path, Pc5Info *info)
{
    Pc5Buf text = { 0 };
    int fields = 0;

    memset(info, 0, sizeof(*info));
    if (pc5_read_file(path, &text, 16384)) {
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
        else if (!strcmp(line, "usb") && !strstr(value, "..") && value[0] != '/') pc5_copy(info->usb, sizeof(info->usb), value);
    }
    pc5_buf_free(&text);
    if (info->bits != 32 && info->bits != 64) info->bits = 32;
    return fields == 7 ? 0 : -1;
}

int pc5_info_read(const char *root, const char *id, Pc5Info *info)
{
    char path[PC5_PATH];

    if (pc5_format(path, sizeof(path), "%s/prefixes/%s/pc5/info.txt", root, id)) {
        memset(info, 0, sizeof(*info));
        return -1;
    }
    return info_read_file(path, info);
}

/* ---- games played from a USB drive --------------------------------------------------- */

static const char *const default_usb_roots[] = { "/mnt/usb0", "/mnt/usb1", "/mnt/usb2", "/mnt/usb3", "/mnt/usb4",
                                                 "/mnt/usb5", "/mnt/usb6", "/mnt/usb7", NULL };
static const char *const *usb_roots = default_usb_roots;

/* The link table Wine on the console reads in a directory (wine/ps5/pw_wine_cwd.h,
 * PW_CWD_LINK_TABLE): a line "name<TAB>target" for each link in it. */
#define LINK_TABLE ".pw-symlinks"

void pc5_usb_roots(const char *const *roots)
{
    usb_roots = roots ? roots : default_usb_roots;
}

int pc5_usb_relative(const char *path, char *out, size_t size)
{
    for (int i = 0; usb_roots[i]; i++) {
        size_t n = strlen(usb_roots[i]);
        if (!strncmp(path, usb_roots[i], n) && path[n] == '/' && path[n + 1] && !strstr(path + n, "/.."))
            return pc5_format(out, size, "%s", path + n + 1);
    }
    return -1;
}

int pc5_usb_game_dir(const Pc5Info *info, char *out, size_t size)
{
    char program[PC5_PATH];

    out[0] = 0;
    if (!info->usb[0]) return -1;
    for (int i = 0; usb_roots[i]; i++)
        if (!pc5_format(out, size, "%s/%s", usb_roots[i], info->usb) && pc5_is_dir(out) &&
            !pc5_format(program, sizeof(program), "%s/%s", out, info->exe) && pc5_exists(program))
            return 0;
    out[0] = 0;
    return -1;
}

int pc5_usb_link(const char *root, const char *id, const Pc5Info *info, char *error, size_t size)
{
    char dir[PC5_PATH], games[PC5_PATH], table[PC5_PATH];
    Pc5Buf old = { 0 }, text = { 0 };
    int status = 0;

    if (pc5_usb_game_dir(info, dir, sizeof(dir))) {
        snprintf(error, size, "Plug in the USB drive with %.100s: it plays from the folder %.200s on it.", info->title,
                 info->usb);
        return -1;
    }
    if (pc5_format(games, sizeof(games), "%s/prefixes/%s/drive_c/Games", root, id) || pc5_mkdirs(games) ||
        pc5_format(table, sizeof(table), "%s/" LINK_TABLE, games)) {
        snprintf(error, size, "PC5 can't make the game's folder: %s.", strerror(errno));
        return -1;
    }
    /* The other links kept (Wine's own, made by the game), this one for the drive it is in now. */
    if (!pc5_read_file(table, &old, 65536) && !pc5_buf_terminate(&old)) {
        size_t n = strlen(info->folder);
        for (char *line = (char *)old.data, *next; line && *line; line = next) {
            next = strchr(line, '\n');
            if (next) *next++ = 0;
            if (!strncmp(line, info->folder, n) && line[n] == '\t') continue;
            status |= pc5_buf_puts(&text, line) || pc5_buf_puts(&text, "\n");
        }
    }
    status |= pc5_buf_puts(&text, info->folder) || pc5_buf_puts(&text, "\t") || pc5_buf_puts(&text, dir) ||
              pc5_buf_puts(&text, "\n");
    if (!status && !(old.len == text.len && !memcmp(old.data, text.data, text.len)))
        status = pc5_write_file(table, text.data, text.len);
    pc5_buf_free(&old);
    pc5_buf_free(&text);
    if (status) {
        snprintf(error, size, "PC5 can't link the game's folder on the USB drive: %s.", strerror(errno));
        return -1;
    }
    return 0;
}

/* The game's folder, where PC5 reads it: in its Windows, or on its USB drive. -1 when the
 * drive isn't there. */
static int game_dir(const char *root, const char *id, const Pc5Info *info, char *out, size_t size)
{
    if (info->usb[0]) return pc5_usb_game_dir(info, out, size);
    return pc5_format(out, size, "%s/prefixes/%s/drive_c/Games/%s", root, id, info->folder);
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
 * Resolution says; DirectDraw games and plain Windows drawing stay at 1080p. */
static const char *desktop_size(const Pc5Info *info, const Pc5Controls *controls)
{
    if (info->api <= PC5_API_DDRAW || !controls) return "1920x1080";
    return controls->resolution == 2 ? "3840x2160" : controls->resolution == 1 ? "2560x1440" : "1920x1080";
}

int pc5_profile_text(const char *id, const Pc5Info *info, const char *arguments, const Pc5Controls *controls,
                     const char *own_dlls, char *out, size_t size)
{
    return pc5_profile_text_at(id, info, arguments, controls, own_dlls, NULL, out, size);
}

int pc5_profile_text_at(const char *id, const Pc5Info *info, const char *arguments, const Pc5Controls *controls,
                        const char *own_dlls, const char *working_directory, char *out, size_t size)
{
    char exe[PC5_PATH + 32], cwd[PC5_PATH + 32], options[600] = "", input[1600], dlls[260];
    Pc5Controls defaults;
    /* DirectDraw through Wine's own Direct3D (wined3d), which draws with OpenGL: Mesa Zink, as
     * for OpenGL games. Without OpenGL a DirectDraw game's Direct3D (7 and older) can't start
     * and its 2D is drawn on the CPU. Plain Windows drawing stays with Wine itself. */
    const char *graphics = info->api == PC5_API_OPENGL || info->api == PC5_API_DDRAW ? "opengl"
                         : info->api == PC5_API_GDI                                  ? "gdi"
                                                                                     : "dxvk";
    /* Direct3D 8 to 11 and DXGI through DXVK, as prospero-win's own profiles. Direct3D 12
     * through VKD3D-Proton only for a game whose program draws with it; any other game gets
     * Wine's own d3d12, as on prospero-win, so a library that merely tries Direct3D 12 (GTA
     * IV's 1911.dll, which then wants DirectComposition, which Wine lacks) fails there
     * quietly instead of getting further and crashing. */
    const char *overrides = info->api == PC5_API_D3D12 ? "d3d8,d3d9,d3d10core,d3d11,d3d12,d3d12core,dxgi=n"
                                                       : "d3d8,d3d9,d3d10core,d3d11,dxgi=n;d3d12,d3d12core=b";
    /* Graphics API (pc5_controls.h): Wine's own Direct3D, all of it Wine's (its dxgi with
     * its d3d11 or d3d12), on OpenGL through Zink or, WineD3D on Vulkan, drawing with
     * Vulkan itself (WINE_D3D_CONFIG, which prospero-win's [debug] env passes on). A
     * DirectDraw game is WineD3D's already: on Vulkan it only gets that setting. */
    const char *wine_d3d = "";
    Pc5Graphics chosen = controls ? controls->graphics : PC5_GRAPHICS_AUTO;
    {
        Pc5Graphics choices[PC5_GRAPHICS_COUNT];
        int n = pc5_graphics_choices((int)info->api, choices), offered = 0;
        for (int i = 0; i < n; i++) offered |= choices[i] == chosen;
        if (!offered) chosen = PC5_GRAPHICS_AUTO;
    }
    if (chosen != PC5_GRAPHICS_AUTO && info->api != PC5_API_DDRAW) {
        overrides = "d3d8,d3d9,d3d10core,d3d11,dxgi,d3d12,d3d12core=b";
        graphics = chosen == PC5_GRAPHICS_WINE && info->api != PC5_API_D3D12 ? "opengl" : "dxvk";
    }
    /* Without OpenGL (graphics = dxvk), Wine's own Direct3D draws with Vulkan too: a DirectDraw
     * part of a DXVK game, or a hardware check asking Direct3D 9 or DirectDraw for the video
     * memory, otherwise finds no graphics at all ("Failed to get a GL context"). */
    if (chosen == PC5_GRAPHICS_WINE_VULKAN || !strcmp(graphics, "dxvk"))
        wine_d3d = "\n[debug]\nenv = WINE_D3D_CONFIG=renderer=vulkan\n";

    windows_path(info->folder, info->exe, 1, exe, sizeof(exe));
    if (working_directory) pc5_copy(cwd, sizeof(cwd), working_directory);
    else windows_path(info->folder, info->exe, 0, cwd, sizeof(cwd));
    if (strlen(exe) >= 260 || strlen(cwd) >= 260) return -1;
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
        "scaling = %s\n"
        "show_fps = false\n"
        "\n"
        "%s%s",
        info->title, info->folder, id, info->title, exe, cwd, options, dlls, id,
        info->bits == 64 ? "pe64" : "pe32", graphics, desktop_size(info, controls),
        controls->stretch ? "stretch" : "fit", input, wine_d3d);
}

/* The folder the game's program is in, on the console. */
static void program_folder(const char *root, const char *id, const Pc5Info *info, char *out, size_t size)
{
    char dir[PC5_PATH];
    char *slash;

    pc5_copy(dir, sizeof(dir), info->exe);
    slash = strrchr(dir, '/');
    if (slash) *slash = 0; else dir[0] = 0;
    char top[PC5_PATH];
    if (game_dir(root, id, info, top, sizeof(top)) || pc5_format(out, size, "%s%s%s", top, dir[0] ? "/" : "", dir))
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
    char top[PC5_PATH];
    if (game_dir(root, id, info, top, sizeof(top)) ||
        pc5_format(path, sizeof(path), "%s%s%s/launch.txt", top, dir[0] ? "/" : "", dir) ||
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
    return game_dir(root, id, &info, out, size);
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

/* What Wine starts for a PC5 game: its program, or the one its batch file starts, with the
 * batch file's arguments and its working folder (a Windows path, "" for the program's own). */
static int start_program(const char *root, const char *id, const Pc5Info *info, Pc5Info *program, char *arguments,
                         size_t arguments_size, char *work, size_t work_size, char *error, size_t size)
{
    char top[PC5_PATH], own[PC5_PATH];
    Pc5Batch *batch;
    char *slash;

    *program = *info;
    arguments[0] = work[0] = 0;
    if (!pc5_is_batch(info->exe)) return 0;
    if (!(batch = pc5_alloc(sizeof(*batch)))) {
        snprintf(error, size, "PC5 is out of memory.");
        return -1;
    }
    if (game_dir(root, id, info, top, sizeof(top)) || pc5_batch_read(top, info->exe, batch)) {
        snprintf(error, size, "%.200s doesn't start any of the game's programs.", info->exe);
        pc5_free(batch);
        return -1;
    }
    pc5_copy(program->exe, sizeof(program->exe), batch->exe);
    pc5_copy(arguments, arguments_size, batch->arguments);
    /* Its bits are the program's own, whatever the notes say (a batch file chosen as the game's
     * program before 1.8.2 kept those of the program chosen before it). */
    {
        char exe[PC5_PATH];
        Pc5PeInfo pe;
        if (!pc5_join(exe, sizeof(exe), top, batch->exe) && !pc5_pe_read(exe, &pe) && (pe.bits == 32 || pe.bits == 64))
            program->bits = pe.bits;
    }
    pc5_copy(own, sizeof(own), batch->exe);
    slash = strrchr(own, '/');
    if (slash) *slash = 0; else own[0] = 0;
    if (strcmp(own, batch->cwd)) windows_path(info->folder, batch->cwd, 1, work, work_size);
    pc5_free(batch);
    return 0;
}

int pc5_game_program(const char *root, const char *id, char *out, size_t size)
{
    Pc5Info info, program;
    char arguments[512], work[PC5_PATH + 32], error[PC5_ERROR];

    if (pc5_info_read(root, id, &info) ||
        start_program(root, id, &info, &program, arguments, sizeof(arguments), work, sizeof(work), error, sizeof(error)))
        return -1;
    char top[PC5_PATH];
    return game_dir(root, id, &info, top, sizeof(top)) || pc5_format(out, size, "%s/%s", top, program.exe);
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

/* The player's Wine log channels for one game, for a report that needs more than PC5 logs:
 * the first line of prefixes/<id>/pc5/winedebug.txt that isn't empty or a comment (# or ;),
 * in place of the title's (err+all,+loaddll,+process). "" when there is none, or when it
 * has more than prospero-win's [debug] winedebug takes: letters, digits and _ + - , = ., 127
 * at most. */
static void game_winedebug(const char *root, const char *id, char *out, size_t size)
{
    char path[PC5_PATH];
    Pc5Buf text = { 0 };

    out[0] = 0;
    if (pc5_format(path, sizeof(path), "%s/prefixes/%s/pc5/winedebug.txt", root, id) ||
        pc5_read_file(path, &text, 4096) || pc5_buf_terminate(&text)) {
        pc5_buf_free(&text);
        return;
    }
    for (char *line = (char *)text.data, *next; line; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        while (*line == ' ' || *line == '\t') line++;
        size_t n = strlen(line);
        while (n && (line[n - 1] == ' ' || line[n - 1] == '\t' || line[n - 1] == '\r')) line[--n] = 0;
        if (!n || line[0] == '#' || line[0] == ';') continue;
        int ok = n < 128 && n < size;
        for (size_t i = 0; ok && i < n; i++) {
            char c = line[i];
            ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '+' ||
                 c == '-' || c == ',' || c == '=' || c == '.';
        }
        if (ok) memcpy(out, line, n + 1);
        break;
    }
    pc5_buf_free(&text);
}

int pc5_profile_refresh(const char *root, const char *id, char *error, size_t size)
{
    Pc5Info info, program;
    Pc5Controls controls;
    char arguments[512], options[512], work[PC5_PATH + 32], path[PC5_PATH], file[PC5_ID + 16], folder[PC5_PATH],
         own[256];
    static char text[4096];
    Pc5Buf old = { 0 };
    int status = 0;

    if (pc5_info_read(root, id, &info)) {
        snprintf(error, size, "PC5's notes on this game are missing.");
        return -1;
    }
    /* A game played from its USB drive: its folder linked to the drive it is in now. */
    if (info.usb[0] && pc5_usb_link(root, id, &info, error, size)) return -1;
    char top[PC5_PATH];
    if (game_dir(root, id, &info, top, sizeof(top))) top[0] = 0;
    /* A game started from a batch file: the program it starts, with its arguments and folder. */
    if (start_program(root, id, &info, &program, arguments, sizeof(arguments), work, sizeof(work), error, size))
        return -1;
    /* A game installed as drawing with Windows itself may load Direct3D or Vulkan at run
     * time (before 1.6.1 PC5 didn't look): what its program names, kept in its notes. */
    if (info.api == PC5_API_GDI &&
        top[0] && !pc5_format(path, sizeof(path), "%s/%s", top, program.exe)) {
        uint32_t named = pc5_pe_named_libraries(path) & PC5_IMPORT_DRAWING;
        if (named) {
            info.api = program.api = pc5_api_from_imports(named);
            if (!pc5_info_text(&info, text, sizeof(text)) &&
                !pc5_format(path, sizeof(path), "%s/prefixes/%s/pc5/info.txt", root, id))
                pc5_write_file(path, text, strlen(text));
        }
    }
    /* A Java game installed before 1.8.2 as drawing with Windows itself (or what its runtime's
     * libraries import): neither its program nor the libraries beside it import anything to
     * draw with, and a Java runtime is beside it, so it draws with OpenGL (pc5_folder_api),
     * kept in its notes. */
    if (info.api != PC5_API_OPENGL) {
        char dir[PC5_PATH];
        Pc5PeInfo pe;
        program_folder(root, id, &program, dir, sizeof(dir));
        if (dir[0] && pc5_java_beside(dir) &&
            top[0] && !pc5_format(path, sizeof(path), "%s/%s", top, program.exe) &&
            !pc5_pe_read(path, &pe) && !(pe.imports & PC5_IMPORT_DRAWING) && !(pc5_dll_imports_in(dir) & PC5_IMPORT_DRAWING)) {
            info.api = program.api = PC5_API_OPENGL;
            if (!pc5_info_text(&info, text, sizeof(text)) &&
                !pc5_format(path, sizeof(path), "%s/prefixes/%s/pc5/info.txt", root, id))
                pc5_write_file(path, text, strlen(text));
        }
    }
    /* launch.txt beside the program chosen (a batch file's options come first). */
    launch_options(root, id, &info, options, sizeof(options));
    if (options[0]) {
        size_t n = strlen(arguments);
        if (pc5_format(arguments + n, sizeof(arguments) - n, "%s%s", n ? " " : "", options)) {
            snprintf(error, size, "The game's launch options are too long: 511 characters at most.");
            return -1;
        }
    }
    pc5_controls_load(root, id, &controls);
    program_folder(root, id, &program, folder, sizeof(folder));
    pc5_own_dlls(folder, own, sizeof(own));
    snprintf(file, sizeof(file), "%s.profile", id);
    if (pc5_profile_text_at(id, &program, arguments, &controls, own, work[0] ? work : NULL, text, sizeof(text)) ||
        pc5_format(path, sizeof(path), "%s/profiles/%s", root, file)) {
        snprintf(error, size, "The game's path is too long for Windows.");
        return -1;
    }
    {
        char channels[128];
        size_t n = strlen(text);
        game_winedebug(root, id, channels, sizeof(channels));
        /* In the profile's [debug] when it has one (it is the last section: WineD3D on Vulkan). */
        if (channels[0] && pc5_format(text + n, sizeof(text) - n, strstr(text, "\n[debug]\n") ? "winedebug = %s\n"
                                                                                               : "\n[debug]\nwinedebug = %s\n",
                                      channels))
            text[n] = 0;
    }
    if (pc5_read_file(path, &old, sizeof(text)) || old.len != strlen(text) || memcmp(old.data, text, old.len))
        status = pc5_write_file(path, text, strlen(text));
    pc5_buf_free(&old);
    if (status) snprintf(error, size, "PC5 can't write the game's profile: %s.", strerror(errno));
    else index_update(root, file, 1);
    /* A start of the game's own program: any hand-offs before it are done with. */
    if (!pc5_format(path, sizeof(path), "%s/prefixes/%s/pc5/handoff-count.txt", root, id)) unlink(path);
    return status;
}

/* ---- a Java game's launcher ---------------------------------------------------------- */

static int resolve_ci(const char *base, const char *relative, char *out, size_t size);

/* A program made with Java's jpackage finds its app folder (app\<program>.cfg beside it)
 * through PATH: when PATH lacks it, the program adds it and starts itself again, which a
 * program on the console can't. With it in PATH from the start (WINEPATH, which Wine puts
 * first in PATH), it runs its game in its own process. */
int pc5_game_winepath(const char *root, const char *id, char *out, size_t size)
{
    Pc5Info info, program;
    char arguments[512], work[PC5_PATH + 32], error[PC5_ERROR], dir[PC5_PATH], cfg[PC5_PATH], found[PC5_PATH],
         windows[PC5_PATH + 32];
    const char *slash, *name;
    size_t base;

    out[0] = 0;
    if (pc5_info_read(root, id, &info) ||
        start_program(root, id, &info, &program, arguments, sizeof(arguments), work, sizeof(work), error, sizeof(error)))
        return -1;
    program_folder(root, id, &program, dir, sizeof(dir));
    slash = strrchr(program.exe, '/');
    name = slash ? slash + 1 : program.exe;
    base = strlen(name) > 4 && pc5_ends_with_ci(name, ".exe") ? strlen(name) - 4 : 0;
    if (!dir[0] || !base || pc5_format(cfg, sizeof(cfg), "app/%.*s.cfg", (int)base, name) ||
        resolve_ci(dir, cfg, found, sizeof(found)) || !pc5_is_file(found))
        return -1;
    windows_path(info.folder, program.exe, 0, windows, sizeof(windows));
    if (strchr(windows, ';')) return -1;   /* PATH's separator: it can't be in PATH */
    return pc5_format(out, size, "%s\\app", windows);
}

/* ---- a launcher's hand-off ------------------------------------------------------------ */

enum { HANDOFFS_IN_A_ROW = 3 };

/* 1 when s starts with prefix, letters of either case alike (ASCII). */
static int starts_with_case(const char *s, const char *prefix)
{
    for (; *prefix; s++, prefix++) {
        char a = *s, b = *prefix;
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (!a || a != b) return 0;
    }
    return 1;
}

int pc5_handoff_env(const char *root, const char *id, char *file, size_t file_size, char *dir, size_t dir_size)
{
    Pc5Info info;
    if (pc5_info_read(root, id, &info) ||
        pc5_format(file, file_size, "%s/prefixes/%s/pc5/handoff.txt", root, id) ||
        pc5_format(dir, dir_size, "C:\\Games\\%s\\", info.folder))
        return -1;
    for (char *p = dir; *p; p++) if (*p == '/') *p = '\\';
    return 0;
}

/* The bits of a program a launcher hands off to, and what it draws with, as installing notes
 * them for a game's program (pc5_folder_api): its own imports, then the libraries it names to
 * load at run time, then the libraries beside it, then a Java runtime beside it (OpenGL). What
 * it can't tell stays as the game's notes say: a launcher and its game may differ in both
 * (a 32-bit launcher for a 64-bit game). */
static void handed_program(const char *path, const char *dir, Pc5Info *program)
{
    Pc5PeInfo pe;
    uint32_t drawing;

    if (pc5_pe_read(path, &pe) || pe.dll) return;
    if (pe.bits == 32 || pe.bits == 64) program->bits = pe.bits;
    if ((drawing = pe.imports & PC5_IMPORT_DRAWING) || (drawing = pc5_pe_named_libraries(path) & PC5_IMPORT_DRAWING) ||
        (dir[0] && (drawing = pc5_dll_imports_in(dir) & PC5_IMPORT_DRAWING)))
        program->api = pc5_api_from_imports(drawing);
    else if (dir[0] && pc5_java_beside(dir))
        program->api = PC5_API_OPENGL;
}

int pc5_handoff_take(const char *root, const char *id, char *error, size_t size)
{
    char file[PC5_PATH], dir[PC5_PATH], path[PC5_PATH], relative[PC5_PATH], cwd[PC5_PATH], arguments[512],
         folder[PC5_PATH], own[256], count_file[PC5_PATH], note[16];
    static char text[4096];
    const char *line[3] = { "", "", "" };
    Pc5Buf handed = { 0 }, count_text = { 0 };
    Pc5Info info;
    Pc5Controls controls;
    int count = 0;

    error[0] = 0;
    if (pc5_handoff_env(root, id, file, sizeof(file), dir, sizeof(dir))) {
        snprintf(error, size, "PC5's notes on this game are missing.");
        return -1;
    }
    if (pc5_read_file(file, &handed, 16384) || pc5_buf_terminate(&handed)) {
        pc5_buf_free(&handed);
        return 0;   /* none */
    }
    unlink(file);   /* taken, whatever comes of it */
    char *at = (char *)handed.data;
    for (int i = 0; i < 3 && at; i++) {
        char *end = strchr(at, '\n');
        if (end) *end++ = 0;
        line[i] = at;
        at = end;
    }
    pc5_info_read(root, id, &info);
    /* Its program: in the game's folder, there, a Windows program. */
    const char *inside = line[0] + strlen(dir), *dot = strrchr(line[0], '.');
    int ok = starts_with_case(line[0], dir) && *inside && !strstr(inside, "..") && dot && !pc5_casecmp(dot, ".exe") &&
             !pc5_format(relative, sizeof(relative), "%s", inside);
    for (char *p = relative; ok && *p; p++) if (*p == '\\') *p = '/';
    char top[PC5_PATH];
    ok = ok && !game_dir(root, id, &info, top, sizeof(top)) && !pc5_format(path, sizeof(path), "%s/%s", top, relative) &&
         pc5_is_file(path);
    if (!ok) {
        snprintf(error, size, "The program its launcher started isn't one of the game's: %.200s", line[0]);
        pc5_buf_free(&handed);
        return -1;
    }
    /* Its arguments: the command line after the program, quoted or not. */
    const char *args = line[1];
    if (*args == '"') {
        const char *close = strchr(args + 1, '"');
        args = close ? close + 1 : args + strlen(args);
    } else {
        while (*args && *args != ' ' && *args != '\t') args++;
    }
    while (*args == ' ' || *args == '\t') args++;
    if (pc5_format(arguments, sizeof(arguments), "%s", args)) {
        snprintf(error, size, "The arguments its launcher gave are too long.");
        pc5_buf_free(&handed);
        return -1;
    }
    for (size_t n = strlen(arguments); n && (arguments[n - 1] == ' ' || arguments[n - 1] == '\t'); n--) arguments[n - 1] = 0;
    /* Its working folder, when it is in the game's folder; the program's own otherwise. */
    const char *work = NULL;
    if (starts_with_case(line[2], dir) || (strlen(line[2]) + 1 == strlen(dir) && starts_with_case(dir, line[2]))) {
        if (!pc5_format(cwd, sizeof(cwd), "%s", line[2]) && !strstr(cwd, "..")) {
            size_t n = strlen(cwd);
            if (n > 3 && cwd[n - 1] == '\\') cwd[n - 1] = 0;
            work = cwd;
        }
    }
    pc5_buf_free(&handed);
    /* Not a fourth in a row: a launcher and its game that keep starting each other. */
    if (!pc5_format(count_file, sizeof(count_file), "%s/prefixes/%s/pc5/handoff-count.txt", root, id) &&
        !pc5_read_file(count_file, &count_text, 64) && !pc5_buf_terminate(&count_text))
        count = atoi((char *)count_text.data);
    pc5_buf_free(&count_text);
    if (count >= HANDOFFS_IN_A_ROW) {
        unlink(count_file);
        snprintf(error, size, "Its programs kept starting each other: %d hand-offs in a row.", count);
        return -1;
    }
    snprintf(note, sizeof(note), "%d\n", count + 1);
    pc5_write_file(count_file, note, strlen(note));
    /* The profile for it, for this start, with its own bits and drawing. */
    Pc5Info program = info;
    pc5_copy(program.exe, sizeof(program.exe), relative);
    pc5_controls_load(root, id, &controls);
    program_folder(root, id, &program, folder, sizeof(folder));
    handed_program(path, folder, &program);
    pc5_own_dlls(folder, own, sizeof(own));
    if (pc5_profile_text_at(id, &program, arguments, &controls, own, work, text, sizeof(text)) ||
        pc5_format(path, sizeof(path), "%s/profiles/%s.profile", root, id)) {
        snprintf(error, size, "The program's path is too long for Windows.");
        return -1;
    }
    if (pc5_write_file(path, text, strlen(text))) {
        snprintf(error, size, "PC5 can't write the game's profile: %s.", strerror(errno));
        return -1;
    }
    snprintf(path, sizeof(path), "%s.profile", id);
    index_update(root, path, 1);
    return 1;
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
        count += !strncmp(entry->d_name, ".installing-", 12) || !strncmp(entry->d_name, ".removing-", 10) ||
                 !strncmp(entry->d_name, ".updating-", 10);
    closedir(dir);
    if (!pc5_format(path, sizeof(path), "%s/saves", root) && (dir = opendir(path))) {
        while ((entry = readdir(dir))) count += !strncmp(entry->d_name, ".keeping-", 9);
        closedir(dir);
    }
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
            game->api = (int)info.api;
            pc5_detail_text(info.bits, info.api, info.bytes, game->detail, sizeof(game->detail));
            /* Played from its USB drive: said first ("USB · 64-bit · ..."). */
            if ((game->usb = info.usb[0] != 0)) {
                char detail[sizeof(game->detail)];
                snprintf(detail, sizeof(detail), "USB \xc2\xb7 %s", game->detail);
                pc5_copy(game->detail, sizeof(game->detail), detail);
            }
        } else {
            Pc5Api api = app->graphics == PW_APP_GRAPHICS_ZINK ? PC5_API_OPENGL :
                         app->graphics == PW_APP_GRAPHICS_DXVK ? PC5_API_D3D9 : PC5_API_GDI;
            game->bits = app->architecture == PW_APP_ARCH_PE64 ? 64 : 32;
            game->vulkan = 0;   /* not PC5's: its profile's desktop is its own */
            game->api = PC5_API_GDI;   /* nor its graphics */
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
                                              "prefixes/.removing-%s", "prefixes/.updating-%s", "saves/.keeping-%s" };
        for (size_t i = 0; i < sizeof(places) / sizeof(places[0]) && !taken; i++) {
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

static int free_name(const char *dir, const char *base, char *out, size_t size);

/* An unfinished update of the game (prefixes/.updating-<prefix>) goes with it: moved aside,
 * deleted at the next start. */
static void drop_update(const char *root, const char *prefix)
{
    char from[PC5_PATH], prefixes[PC5_PATH], base[PC5_ID + 24], leaf[PC5_ID + 40], to[PC5_PATH];

    snprintf(base, sizeof(base), ".removing-update-%s", prefix);
    if (!pc5_format(from, sizeof(from), "%s/prefixes/.updating-%s", root, prefix) && pc5_exists(from) &&
        !pc5_format(prefixes, sizeof(prefixes), "%s/prefixes", root) && !free_name(prefixes, base, leaf, sizeof(leaf)) &&
        !pc5_join(to, sizeof(to), prefixes, leaf))
        rename(from, to);
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
        drop_update(root, game->prefix);
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

/* ---- folders, a name at a time -------------------------------------------------------- */

/* name in dir in any case: 0 and the name as the folder has it, -1 when there is none. */
static int name_in(const char *dir, const char *name, char *out, size_t size)
{
    DIR *folder = opendir(dir);
    struct dirent *entry;
    int found = -1;

    if (!folder) return -1;
    while (found && (entry = readdir(folder)))
        if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..") && !pc5_casecmp(entry->d_name, name) &&
            strlen(entry->d_name) < size) {
            pc5_copy(out, size, entry->d_name);
            found = 0;
        }
    closedir(folder);
    return found;
}

/* relative (parts between '/') in base, a part at a time in any case: 0 and the path as
 * the folders have it, -1 when a part isn't there. */
static int resolve_ci(const char *base, const char *relative, char *out, size_t size)
{
    char rest[PC5_PATH], name[PC5_PATH], next[PC5_PATH];

    pc5_copy(out, size, base);
    pc5_copy(rest, sizeof(rest), relative);
    for (char *part = rest, *slash; part && *part; part = slash) {
        if ((slash = strchr(part, '/'))) *slash++ = 0;
        if (!*part) continue;
        if (pc5_join(next, sizeof(next), out, part)) return -1;
        if (!pc5_exists(next) && (name_in(out, part, name, sizeof(name)) || pc5_join(next, sizeof(next), out, name)))
            return -1;
        pc5_copy(out, size, next);
    }
    return 0;
}

/* A name in dir that isn't taken: base, then base-2, base-3 and so on. */
static int free_name(const char *dir, const char *base, char *out, size_t size)
{
    char path[PC5_PATH];

    for (int k = 1; k < 100; k++) {
        if (k == 1 ? pc5_format(out, size, "%s", base) : pc5_format(out, size, "%s-%d", base, k)) return -1;
        if (pc5_join(path, sizeof(path), dir, out)) return -1;
        if (!pc5_exists(path)) return 0;
    }
    return -1;
}

/* Everything in base (links left out) as relative paths, a folder ending in '/' and before
 * what it holds, NUL after each, in list; keep(relative, is_folder) chooses (NULL: all). */
static int walk_tree(const char *base, Pc5Buf *list, int (*keep)(const char *relative, int folder))
{
    enum { STACK = 4096 };
    char (*stack)[PC5_PATH] = pc5_alloc(sizeof(*stack) * STACK);
    int top = 0, status = 0;

    if (!stack) return -1;
    stack[top++][0] = 0;
    while (top > 0 && !status) {
        char relative[PC5_PATH], dir_path[PC5_PATH], child[PC5_PATH], full[PC5_PATH];
        DIR *dir;
        struct dirent *entry;
        struct stat st;

        memcpy(relative, stack[--top], PC5_PATH);
        if (relative[0] ? pc5_join(dir_path, sizeof(dir_path), base, relative)
                        : pc5_format(dir_path, sizeof(dir_path), "%s", base))
            continue;
        if (!(dir = opendir(dir_path))) { status = -1; break; }
        while ((entry = readdir(dir))) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
            if ((relative[0] ? pc5_format(child, sizeof(child), "%s/%s", relative, entry->d_name)
                             : pc5_format(child, sizeof(child), "%s", entry->d_name)) ||
                pc5_join(full, sizeof(full), base, child) || lstat(full, &st))
                continue;
            if (S_ISDIR(st.st_mode)) {
                if (keep && !keep(child, 1)) continue;
                if (top >= STACK) { status = -1; break; }
                memcpy(stack[top++], child, PC5_PATH);
                if (pc5_buf_puts(list, child) || pc5_buf_put(list, "/", 2)) status = -1;
            } else if (S_ISREG(st.st_mode)) {
                if (keep && !keep(child, 0)) continue;
                if (pc5_buf_put(list, child, strlen(child) + 1)) status = -1;
            }
        }
        closedir(dir);
    }
    pc5_free(stack);
    return status;
}

/* ---- the game's files as PC5 copied them (pc5/files.txt, a relative path a line) ---------- */

typedef struct Manifest {
    Pc5Buf text;
    char **names;                 /* into text, in any-case order */
    size_t count;
} Manifest;

static int name_order_ci(const void *a, const void *b)
{
    return pc5_casecmp(*(char *const *)a, *(char *const *)b);
}

static void manifest_free(Manifest *m)
{
    pc5_buf_free(&m->text);
    pc5_free(m->names);
    memset(m, 0, sizeof(*m));
}

/* The lines of text (taken over) as a manifest. */
static int manifest_from(Pc5Buf *text, Manifest *m)
{
    size_t lines = 0, at = 0;

    memset(m, 0, sizeof(*m));
    m->text = *text;
    *text = (Pc5Buf){ 0 };
    if (pc5_buf_terminate(&m->text)) return -1;
    for (size_t i = 0; i < m->text.len; i++) lines += m->text.data[i] == '\n';
    if (!(m->names = pc5_alloc(sizeof(*m->names) * (lines + 1)))) return -1;
    for (char *line = (char *)m->text.data, *next; line && at < m->text.len; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        at = next ? (size_t)(next - (char *)m->text.data) : m->text.len;
        size_t length = strlen(line);
        if (length && line[length - 1] == '\r') line[--length] = 0;
        if (length) m->names[m->count++] = line;
    }
    qsort(m->names, m->count, sizeof(*m->names), name_order_ci);
    return 0;
}

static int manifest_load(const char *path, Manifest *m)
{
    Pc5Buf text = { 0 };
    memset(m, 0, sizeof(*m));
    if (pc5_read_file(path, &text, 256u * 1024 * 1024)) {
        pc5_buf_free(&text);
        return -1;
    }
    return manifest_from(&text, m);
}

static int manifest_has(const Manifest *m, const char *relative)
{
    return m->count && bsearch(&relative, m->names, m->count, sizeof(*m->names), name_order_ci) != NULL;
}

/* The files of list (NUL after each, folders ending in '/'), a line each, added to out. */
static int manifest_add_list(Pc5Buf *out, const Pc5Buf *list)
{
    for (size_t at = 0; at < list->len; at += strlen((const char *)list->data + at) + 1) {
        const char *line = (const char *)list->data + at;
        size_t length = strlen(line);
        if (!length || line[length - 1] == '/') continue;
        if (pc5_buf_put(out, line, length) || pc5_buf_put(out, "\n", 1)) return -1;
    }
    return 0;
}

/* ---- kept saves ------------------------------------------------------------------------ */

static int saves_dir(const char *root, char *out, size_t size)
{
    return pc5_format(out, size, "%s/saves", root);
}

int pc5_remove_can_keep(const Pc5Game *game, const Pc5Library *library)
{
    if (!game->pc5 || !game->prefix[0] || strcmp(game->prefix, game->id) || !strcmp(game->prefix, "default")) return 0;
    for (int i = 0; library && i < library->count; i++)
        if (&library->games[i] != game && !strcmp(library->games[i].prefix, game->prefix)) return 0;
    return 1;
}

/* The files the game made in its own folder, which pc5/files.txt doesn't list (its saves and
 * settings kept beside its program, launch.txt), moved from the kept Windows' game folder to
 * its pc5-game/. Without the list (a game installed before PC5 1.8.0) nothing is. A file
 * moved already is gone from the game's folder, so this can run again. */
static int keep_game_files(const char *keeping, char *error, size_t error_size)
{
    char path[PC5_PATH], game[PC5_PATH], from[PC5_PATH], to[PC5_PATH];
    Pc5Info info;
    Manifest manifest;
    Pc5Buf list = { 0 };
    int status = 0;

    if (pc5_format(path, sizeof(path), "%s/pc5/info.txt", keeping) || info_read_file(path, &info) ||
        pc5_format(game, sizeof(game), "%s/drive_c/Games/%s", keeping, info.folder) || !pc5_is_dir(game) ||
        pc5_format(path, sizeof(path), "%s/pc5/files.txt", keeping) || manifest_load(path, &manifest))
        return 0;
    if (walk_tree(game, &list, NULL)) {
        snprintf(error, error_size, "PC5 can't read the game's folder.");
        status = -1;
    }
    for (size_t at = 0; !status && at < list.len; at += strlen((char *)list.data + at) + 1) {
        const char *relative = (const char *)list.data + at;
        size_t length = strlen(relative);
        if (relative[length - 1] == '/' || manifest_has(&manifest, relative)) continue;
        if (pc5_join(from, sizeof(from), game, relative) || pc5_format(to, sizeof(to), "%s/pc5-game/%s", keeping, relative)) {
            snprintf(error, error_size, "A path in the game is too long: %s.", relative);
            status = -1;
            break;
        }
        char *slash = strrchr(to, '/');
        *slash = 0;
        int made = pc5_mkdirs(to);
        *slash = '/';
        if (made || rename(from, to)) {
            snprintf(error, error_size, "PC5 can't keep %s: %s.", relative, strerror(errno));
            status = -1;
        }
    }
    pc5_buf_free(&list);
    manifest_free(&manifest);
    return status;
}

/* The rest of keeping a game's saves, its Windows already moved to saves/.keeping-<id>: the
 * game's folders (C:\Games) out of it to prefixes/.removing-<id> (removing, deleted after;
 * "" when there were none), the game out of the library, the saves under their own name.
 * Run again, it finishes one cut short. */
static int keep_finish(const char *root, const char *id, const char *profile_file, char *removing, size_t removing_size,
                       char *error, size_t error_size)
{
    char saves[PC5_PATH], keeping[PC5_PATH], games[PC5_PATH], prefixes[PC5_PATH], profile[PC5_PATH];
    char leaf[PC5_ID + 32], base[PC5_ID + 16], name[PC5_ID];

    removing[0] = 0;
    if (saves_dir(root, saves, sizeof(saves)) || pc5_format(keeping, sizeof(keeping), "%s/.keeping-%s", saves, id) ||
        pc5_format(games, sizeof(games), "%s/drive_c/Games", keeping) ||
        pc5_format(prefixes, sizeof(prefixes), "%s/prefixes", root) ||
        pc5_format(profile, sizeof(profile), "%s/profiles/%s", root, profile_file)) {
        snprintf(error, error_size, "A path is too long.");
        return -1;
    }
    snprintf(base, sizeof(base), ".removing-%s", id);
    if (pc5_is_dir(games) && keep_game_files(keeping, error, error_size)) return -1;
    if (pc5_is_dir(games) &&
        (free_name(prefixes, base, leaf, sizeof(leaf)) || pc5_join(removing, removing_size, prefixes, leaf) ||
         rename(games, removing))) {
        snprintf(error, error_size, "PC5 can't move the game's folder: %s.", strerror(errno));
        removing[0] = 0;
        return -1;
    }
    if (unlink(profile) && errno != ENOENT) {
        snprintf(error, error_size, "PC5 can't delete the game's profile: %s.", strerror(errno));
        return -1;
    }
    index_update(root, profile_file, 0);
    delete_logs(root, id);
    /* Its pictures go with the game: the saves don't need them, and an install puts back new ones. */
    {
        char pictures[PC5_PATH];
        if (!pc5_format(pictures, sizeof(pictures), "%s/pc5", keeping)) pc5_cover_delete_all(pictures);
    }
    {
        /* When they were kept: the newest of a game's kept saves is the one put back. */
        char when[PC5_PATH], text[32];
        if (!pc5_format(when, sizeof(when), "%s/pc5/kept.txt", keeping) && !pc5_exists(when)) {
            snprintf(text, sizeof(text), "%lld\n", (long long)time(NULL));
            pc5_write_file(when, text, strlen(text));
        }
    }
    if (free_name(saves, id, name, sizeof(name)) || pc5_join(games, sizeof(games), saves, name) || rename(keeping, games)) {
        snprintf(error, error_size, "PC5 can't put the saves in place: %s.", strerror(errno));
        return -1;
    }
    return 0;
}

int pc5_remove_keep_saves(Pc5Remove *remove, const char *root, const Pc5Game *game, const Pc5Library *library)
{
    char saves[PC5_PATH], from[PC5_PATH], keeping[PC5_PATH], removing[PC5_PATH];

    memset(remove, 0, sizeof(*remove));
    pc5_copy(remove->root, sizeof(remove->root), root);
    remove->failed = 1;
    if (!pc5_remove_can_keep(game, library)) {
        snprintf(remove->error, sizeof(remove->error), "This game's saves can't be kept apart from it.");
        return -1;
    }
    if (saves_dir(root, saves, sizeof(saves)) || pc5_mkdirs(saves) ||
        pc5_format(from, sizeof(from), "%s/prefixes/%s", root, game->prefix) ||
        pc5_format(keeping, sizeof(keeping), "%s/.keeping-%s", saves, game->prefix)) {
        snprintf(remove->error, sizeof(remove->error), "PC5 can't make its saves folder: %s.", strerror(errno));
        return -1;
    }
    if (pc5_exists(keeping)) {
        snprintf(remove->error, sizeof(remove->error), "PC5 is still keeping this game's saves from before: start PC5 again.");
        return -1;
    }
    drop_update(root, game->prefix);
    if (rename(from, keeping)) {
        snprintf(remove->error, sizeof(remove->error), "PC5 can't move the game's folder: %s.", strerror(errno));
        return -1;
    }
    /* From here on a cut-short uninstall is finished at the next start. */
    if (keep_finish(root, game->prefix, game->file, removing, sizeof(removing), remove->error, sizeof(remove->error)))
        return -1;
    remove->failed = 0;
    if (removing[0]) return pc5_tree_delete_begin(remove, removing);
    remove->done = 1;
    return 0;
}

int pc5_saves_delete_begin(Pc5Remove *remove, const char *root, const char *name)
{
    char saves[PC5_PATH], from[PC5_PATH], prefixes[PC5_PATH], base[PC5_ID + 16], leaf[PC5_ID + 32], to[PC5_PATH];

    memset(remove, 0, sizeof(*remove));
    pc5_copy(remove->root, sizeof(remove->root), root);
    snprintf(base, sizeof(base), ".removing-saves-%s", name);
    if (!name[0] || name[0] == '.' || strchr(name, '/') || saves_dir(root, saves, sizeof(saves)) ||
        pc5_join(from, sizeof(from), saves, name) || pc5_format(prefixes, sizeof(prefixes), "%s/prefixes", root) ||
        free_name(prefixes, base, leaf, sizeof(leaf)) || pc5_join(to, sizeof(to), prefixes, leaf) || rename(from, to)) {
        snprintf(remove->error, sizeof(remove->error), "PC5 can't move the saves: %s.", strerror(errno));
        remove->failed = 1;
        return -1;
    }
    return pc5_tree_delete_begin(remove, to);
}

static int saves_by_title(const void *a, const void *b)
{
    const Pc5Saves *x = a, *y = b;
    int order = pc5_casecmp(x->title, y->title);
    if (order) return order;
    if (x->kept != y->kept) return x->kept > y->kept ? -1 : 1;   /* the newest first */
    return strcmp(x->name, y->name);
}

int pc5_saves_list(const char *root, Pc5Saves *saves, int max)
{
    char dir[PC5_PATH], path[PC5_PATH];
    DIR *folder;
    struct dirent *entry;
    int count = 0;

    if (saves_dir(root, dir, sizeof(dir)) || !(folder = opendir(dir))) return 0;
    while (count < max && (entry = readdir(folder))) {
        Pc5Info info;
        if (entry->d_name[0] == '.' || strlen(entry->d_name) >= PC5_ID ||
            pc5_format(path, sizeof(path), "%s/%s/pc5/info.txt", dir, entry->d_name) || info_read_file(path, &info))
            continue;
        Pc5Saves *one = &saves[count++];
        pc5_copy(one->name, sizeof(one->name), entry->d_name);
        pc5_copy(one->title, sizeof(one->title), info.title);
        pc5_copy(one->folder, sizeof(one->folder), info.folder);
        pc5_copy(one->exe, sizeof(one->exe), info.exe);
        one->kept = 0;
        {
            Pc5Buf text = { 0 };
            struct stat st;
            if (!pc5_format(path, sizeof(path), "%s/%s/pc5/kept.txt", dir, entry->d_name) && !pc5_read_file(path, &text, 64) &&
                !pc5_buf_terminate(&text))
                one->kept = strtoll((char *)text.data, NULL, 10);
            else if (!pc5_format(path, sizeof(path), "%s/%s", dir, entry->d_name) && !stat(path, &st))
                one->kept = (int64_t)st.st_mtime;
            pc5_buf_free(&text);
        }
    }
    closedir(folder);
    qsort(saves, (size_t)count, sizeof(saves[0]), saves_by_title);
    return count;
}

int pc5_saves_find(const char *root, const char *folder, const char *exe, char *out, size_t size)
{
    enum { MAX = 256 };
    Pc5Saves *saves = pc5_alloc(sizeof(*saves) * MAX);
    char dir[PC5_PATH];
    int found = 0, newest = -1;

    if (!saves || !folder || !folder[0] || saves_dir(root, dir, sizeof(dir))) {
        pc5_free(saves);
        return 0;
    }
    int count = pc5_saves_list(root, saves, MAX);
    for (int i = 0; i < count; i++)
        if (!pc5_casecmp(saves[i].folder, folder) && (!exe || !pc5_casecmp(saves[i].exe, exe))) {
            found++;
            if (newest < 0 || saves[i].kept > saves[newest].kept) newest = i;
        }
    if (newest >= 0 && pc5_format(out, size, "%s/%s", dir, saves[newest].name)) found = 0;
    pc5_free(saves);
    return found;
}

/* ---- updates, put in place ---------------------------------------------------------------- */

/* One path of an update's copy (staging/files/<relative>) put in the game's folder: its
 * folders made, or the game's own used in whatever case they have; a file of the game's
 * where the copy has a folder deleted, a folder of the game's where the copy has a file
 * kept beside it as <name>.pc5-old; the file renamed over the game's own (in the case the
 * game's has). A path already moved is left as it is, so this can run again. */
static int update_place(const char *staging, const char *game_dir, const char *relative, char *error, size_t size)
{
    char from[PC5_PATH], at[PC5_PATH], part[PC5_PATH], name[PC5_PATH], next[PC5_PATH];
    size_t length = strlen(relative);
    int is_dir = length && relative[length - 1] == '/';

    if (!length || pc5_format(from, sizeof(from), "%s/files/%s", staging, relative)) {
        snprintf(error, size, "A path in the update is too long: %s.", relative);
        return -1;
    }
    pc5_copy(at, sizeof(at), game_dir);
    pc5_copy(part, sizeof(part), relative);
    if (is_dir) part[length - 1] = 0;
    for (char *piece = part, *slash; piece; piece = slash) {
        struct stat st;
        int exists, folder;
        if ((slash = strchr(piece, '/'))) *slash++ = 0;
        folder = slash || is_dir;
        if (pc5_join(next, sizeof(next), at, piece)) goto too_long;
        exists = !lstat(next, &st);
        if (!exists && !name_in(at, piece, name, sizeof(name))) {
            if (pc5_join(next, sizeof(next), at, name)) goto too_long;
            exists = !lstat(next, &st);
        }
        if (folder) {
            if (exists && !S_ISDIR(st.st_mode)) {
                if (unlink(next)) goto failed;
                exists = 0;
            }
            if (!exists && (pc5_join(next, sizeof(next), at, piece) || (mkdir(next, 0777) && errno != EEXIST)))
                goto failed;
            pc5_copy(at, sizeof(at), next);
            continue;
        }
        if (!pc5_exists(from)) return 0;   /* moved already */
        if (exists && S_ISDIR(st.st_mode)) {
            char base[PC5_PATH], leaf[PC5_PATH], aside[PC5_PATH];
            const char *own = strrchr(next, '/') + 1;
            if (pc5_format(base, sizeof(base), "%s.pc5-old", own) || free_name(at, base, leaf, sizeof(leaf)) ||
                pc5_join(aside, sizeof(aside), at, leaf) || rename(next, aside))
                goto failed;
            exists = 0;
        }
        if (!exists && pc5_join(next, sizeof(next), at, piece)) goto too_long;
        if (rename(from, next)) goto failed;
        return 0;
    }
    return 0;
too_long:
    snprintf(error, size, "A path in the game is too long: %s.", relative);
    return -1;
failed:
    snprintf(error, size, "PC5 can't put %s in place: %s.", relative, strerror(errno));
    return -1;
}

/* The update's notes (staging/info.txt) made the game's, the copy's files (staging/files.txt)
 * added to the game's list, and its profile written again (or, when it can't be, at the
 * game's next start). */
static int update_complete(const char *root, const char *id, const char *staging, char *error, size_t size)
{
    char from[PC5_PATH], to[PC5_PATH], why[PC5_ERROR];
    Pc5Buf text = { 0 };
    int failed = pc5_format(from, sizeof(from), "%s/info.txt", staging) ||
                 pc5_format(to, sizeof(to), "%s/prefixes/%s/pc5/info.txt", root, id) ||
                 pc5_read_file(from, &text, 16384) || pc5_write_file(to, text.data, text.len);

    pc5_buf_free(&text);
    if (failed) {
        snprintf(error, size, "PC5 can't write its notes: %s.", strerror(errno));
        return -1;
    }
    /* The game's files: what it had and what the copy brought, once each. */
    if (!pc5_format(from, sizeof(from), "%s/files.txt", staging) && pc5_is_file(from) &&
        !pc5_format(to, sizeof(to), "%s/prefixes/%s/pc5/files.txt", root, id)) {
        Manifest old = { 0 }, copy = { 0 };
        Pc5Buf merged = { 0 };
        manifest_load(to, &old);   /* none for a game installed before PC5 1.8.0 */
        if (!manifest_load(from, &copy)) {
            size_t i = 0, j = 0;
            while (i < old.count || j < copy.count) {
                int order = i >= old.count ? 1 : j >= copy.count ? -1 : pc5_casecmp(old.names[i], copy.names[j]);
                const char *name = order <= 0 ? old.names[i] : copy.names[j];
                if (order <= 0) i++;
                if (order >= 0) j++;
                pc5_buf_puts(&merged, name);
                pc5_buf_put(&merged, "\n", 1);
            }
            if (pc5_write_file(to, merged.data, merged.len)) {
                snprintf(error, size, "PC5 can't write the game's list of files: %s.", strerror(errno));
                failed = 1;
            }
        }
        manifest_free(&old);
        manifest_free(&copy);
        pc5_buf_free(&merged);
        if (failed) return -1;
    }
    pc5_profile_refresh(root, id, why, sizeof(why));
    return 0;
}

/* An update left at prefixes/<name> (".updating-<id>"): 1 when it is to be deleted (its copy
 * wasn't whole, or it is now in place), -1 when it couldn't be put in place (it stays, for
 * the next start). */
static int update_recover(const char *root, const char *name)
{
    char staging[PC5_PATH], notes[PC5_PATH], files[PC5_PATH], game_dir[PC5_PATH], error[PC5_ERROR];
    const char *id = name + 10;
    Pc5Buf list = { 0 };
    Pc5Info info;

    if (pc5_format(staging, sizeof(staging), "%s/prefixes/%s", root, name) ||
        pc5_format(notes, sizeof(notes), "%s/info.txt", staging) ||
        pc5_format(files, sizeof(files), "%s/files", staging))
        return -1;
    if (info_read_file(notes, &info)) return 1;   /* not whole: the game wasn't changed */
    if (pc5_format(game_dir, sizeof(game_dir), "%s/prefixes/%s/drive_c/Games/%s", root, id, info.folder) ||
        !pc5_is_dir(game_dir))
        return 1;   /* the game is gone */
    if (pc5_is_dir(files) && walk_tree(files, &list, NULL)) {
        pc5_buf_free(&list);
        return -1;
    }
    /* A file that can't be put in place is left out, so the update always finishes. */
    for (size_t at = 0; at < list.len; at += strlen((char *)list.data + at) + 1)
        update_place(staging, game_dir, (char *)list.data + at, error, sizeof(error));
    pc5_buf_free(&list);
    return update_complete(root, id, staging, error, sizeof(error)) ? -1 : 1;
}

int pc5_recover_step(const char *root, Pc5Remove *remove, int budget_ms)
{
    char path[PC5_PATH], leftover[PC5_PATH] = "";
    DIR *dir;
    struct dirent *entry;

    if (remove->top > 0 && !remove->failed) return pc5_remove_step(remove, budget_ms) || 1;
    if (remove->failed) return 0;
    /* Saves being kept when the uninstall stopped: kept, and the game's folder deleted. */
    if (!saves_dir(root, path, sizeof(path)) && (dir = opendir(path))) {
        char id[PC5_ID], profile[PC5_ID + 16], error[PC5_ERROR];
        while ((entry = readdir(dir)) && !leftover[0]) {
            if (strncmp(entry->d_name, ".keeping-", 9) || strlen(entry->d_name + 9) >= sizeof(id)) continue;
            pc5_copy(id, sizeof(id), entry->d_name + 9);
            snprintf(profile, sizeof(profile), "%s.profile", id);
            keep_finish(root, id, profile, leftover, sizeof(leftover), error, sizeof(error));
        }
        closedir(dir);
        if (leftover[0]) {
            remove->failed = 0;
            return pc5_tree_delete_begin(remove, leftover) ? 0 : 1;
        }
    }
    if (pc5_format(path, sizeof(path), "%s/prefixes", root) || !(dir = opendir(path))) return 0;
    /* An update the console stopped, before anything is deleted: put in place when its copy
     * was whole, else deleted. */
    while (!leftover[0] && (entry = readdir(dir)))
        if (!strncmp(entry->d_name, ".updating-", 10) && entry->d_name[10] && update_recover(root, entry->d_name) > 0)
            pc5_join(leftover, sizeof(leftover), path, entry->d_name);
    if (!leftover[0]) {
        rewinddir(dir);
        while ((entry = readdir(dir))) {
            if (!strncmp(entry->d_name, ".installing-", 12) || !strncmp(entry->d_name, ".removing-", 10)) {
                pc5_join(leftover, sizeof(leftover), path, entry->d_name);
                break;
            }
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
            /* Its pictures that nothing uses, before anything saves one (the library isn't shown yet). */
            char pictures[PC5_PATH];
            if (strcmp(entry->d_name, "default") && !pc5_format(pictures, sizeof(pictures), "%s/%s/pc5", path, entry->d_name))
                pc5_cover_tidy(pictures);
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

uint64_t pc5_install_needs_usb(const Pc5Folder *folder)
{
    (void)folder;
    return TEMPLATE_ROOM + SPARE_ROOM;
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

static int install_begin(Pc5Install *install, const char *root, const char *template_prefix, const Pc5Folder *folder,
                         int exe, Pc5Image *cover, uint32_t tint, const char *cover_source, int in_place)
{
    char path[PC5_PATH], exe_path[PC5_PATH + 32];
    uint64_t free_bytes = 0, needs = in_place ? pc5_install_needs_usb(folder) : pc5_install_needs(folder);

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
    install->in_place = in_place;
    if (in_place && pc5_usb_relative(folder->path, info->usb, sizeof(info->usb)))
        return fail(install, "Only a game on a USB drive can play from it: this folder isn't on one.");
    windows_path(info->folder, info->exe, 1, exe_path, sizeof(exe_path));
    if (strlen(exe_path) >= 260) return fail(install, "The game's program is too deep in its folder for Windows.");
    if (pc5_format(path, sizeof(path), "%s/profiles", root) || pc5_mkdirs(path) ||
        pc5_format(path, sizeof(path), "%s/prefixes", root) || pc5_mkdirs(path))
        return fail(install, "PC5 can't make its folders in %s: %s.", root, strerror(errno));
    if (pc5_free_bytes(path, &free_bytes) == 0 && free_bytes < needs) {
        char need[32], have[32];
        pc5_size_text(needs, need, sizeof(need));
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

int pc5_install_begin(Pc5Install *install, const char *root, const char *template_prefix, const Pc5Folder *folder,
                      int exe, Pc5Image *cover, uint32_t tint, const char *cover_source)
{
    return install_begin(install, root, template_prefix, folder, exe, cover, tint, cover_source, 0);
}

int pc5_install_begin_usb(Pc5Install *install, const char *root, const char *template_prefix, const Pc5Folder *folder,
                          int exe, Pc5Image *cover, uint32_t tint, const char *cover_source)
{
    return install_begin(install, root, template_prefix, folder, exe, cover, tint, cover_source, 1);
}

/* One template entry: a folder made or a file copied whole. */
static int prefix_entry(Pc5Install *install, const char *line)
{
    char source[PC5_PATH], target[PC5_PATH];
    size_t length = strlen(line);
    int in, out, ok = 1;
    ssize_t got;

    if (pc5_join(source, sizeof(source), install->template_prefix, line) ||
        (!strncmp(line, "pc5-game/", 9)
             /* kept saves: what the game made in its own folder goes back there */
             ? pc5_format(target, sizeof(target), "%s/drive_c/Games/%s/%s", install->staging, install->info.folder, line + 9)
             : pc5_join(target, sizeof(target), install->staging, line)))
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
    /* Kept saves are deleted once the game is in place: their copy must be on the disk first. */
    if (ok && install->kept[0] && fsync(out)) ok = 0;
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
    if (install->update) return pc5_format(out, size, "%s/files%s%s", install->staging, relative[0] ? "/" : "", relative);
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
    /* The game's files as copied: what else is in its folder later, the game made (kept saves).
     * None for a game played from its USB drive: its folder isn't the console's. */
    if (!install->in_place) {
        Pc5Buf files = { 0 };
        int failed = manifest_add_list(&files, &install->list) ||
                     pc5_format(path, sizeof(path), "%s/pc5/files.txt", install->staging) ||
                     pc5_write_file(path, files.data, files.len);
        pc5_buf_free(&files);
        if (failed) return fail(install, "PC5 can't write its notes: %s.", strerror(errno));
    }
    if (install->cover.px && (pc5_format(path, sizeof(path), "%s/pc5/cover.bin", install->staging) ||
                              pc5_cover_save(path, &install->cover, install->tint)))
        return fail(install, "PC5 can't save the cover: %s.", strerror(errno));
    /* Where the cover's background is, for the background fetched after the install. */
    if ((install->background[0] || install->background_appid) &&
        !pc5_format(path, sizeof(path), "%s/pc5/background.txt", install->staging) &&
        !pc5_format(text, sizeof(text), "%s\n%u\n", install->background, (unsigned)install->background_appid))
        pc5_write_file(path, text, strlen(text));
    if (pc5_format(target, sizeof(target), "%s/prefixes/%s", install->root, install->id) || rename(install->staging, target))
        return fail(install, "PC5 can't put the game in place: %s.", strerror(errno));
    pc5_copy(install->staging, sizeof(install->staging), target);
    if (pc5_profile_refresh(install->root, install->id, error, sizeof(error))) {
        /* The folder is in place: the next start writes the profile again. */
        return fail(install, "%s", error);
    }
    /* The kept saves are the game's now: out of saves/ at once, deleted step by step. */
    if (install->kept[0]) {
        char prefixes[PC5_PATH], base[PC5_ID + 16], leaf[PC5_ID + 32];
        snprintf(base, sizeof(base), ".removing-saves-%s", install->id);
        if (!pc5_format(prefixes, sizeof(prefixes), "%s/prefixes", install->root) &&
            !free_name(prefixes, base, leaf, sizeof(leaf)) && !pc5_join(path, sizeof(path), prefixes, leaf) &&
            !rename(install->kept, path)) {
            if (!install->cleanup) install->cleanup = pc5_alloc(sizeof(Pc5Remove));
            if (install->cleanup) {
                memset(install->cleanup, 0, sizeof(*install->cleanup));
                if (!pc5_tree_delete_begin(install->cleanup, path)) install->tidying = 1;
            }
        }
    }
    return 0;
}

/* ---- updates, the steps -------------------------------------------------------------------- */

/* The update's next piece of putting the copy in place: its notes first, which mark the copy
 * whole, then file after file until about until; 1 when everything is in place. */
static int update_finish_step(Pc5Install *install, uint64_t until)
{
    char path[PC5_PATH], text[2048];

    if (!install->moving) {
        Pc5Buf files = { 0 };
        int failed = manifest_add_list(&files, &install->list) ||
                     pc5_format(path, sizeof(path), "%s/files.txt", install->staging) ||
                     pc5_write_file(path, files.data, files.len);
        pc5_buf_free(&files);
        /* The notes last: they mark the copy whole. */
        if (failed || pc5_info_text(&install->info, text, sizeof(text)) ||
            pc5_format(path, sizeof(path), "%s/info.txt", install->staging) || pc5_write_file(path, text, strlen(text)))
            return fail(install, "PC5 can't write its notes: %s.", strerror(errno));
        install->moving = 1;
        install->at = 0;
        install->files_done = 0;
    }
    while (install->at < install->list.len) {
        const char *relative = (const char *)install->list.data + install->at;
        size_t length = strlen(relative);
        char why[PC5_ERROR];
        /* A file that can't be put in place is left out (the first one's why kept), so the
         * update finishes either way. */
        if (update_place(install->staging, install->game_dir, relative, why, sizeof(why)) && !install->skipped++)
            pc5_copy(install->error, sizeof(install->error), why);
        if (relative[length - 1] != '/') install->files_done++;
        install->at += length + 1;
        if (pc5_now_ms() >= until) return 0;
    }
    if (update_complete(install->root, install->id, install->staging, install->error, sizeof(install->error)))
        return -1;
    /* What is left of the copy (its emptied folders and its notes) goes. */
    if (!install->cleanup) install->cleanup = pc5_alloc(sizeof(Pc5Remove));
    if (install->cleanup) {
        memset(install->cleanup, 0, sizeof(*install->cleanup));
        if (!pc5_tree_delete_begin(install->cleanup, install->staging)) install->tidying = 1;
    }
    return 1;
}

uint64_t pc5_update_needs(const Pc5Folder *folder)
{
    return folder->bytes + SPARE_ROOM;
}

int pc5_update_begin(Pc5Install *install, const char *root, const char *id, const Pc5Folder *folder)
{
    char path[PC5_PATH], program[PC5_PATH];
    uint64_t free_bytes = 0;
    Pc5Info old;

    memset(install, 0, sizeof(*install));
    install->in_fd = install->out_fd = -1;
    install->phase = PC5_INSTALL_FAILED;
    install->update = 1;
    pc5_copy(install->root, sizeof(install->root), root);
    pc5_copy(install->id, sizeof(install->id), id);
    pc5_copy(install->source, sizeof(install->source), folder->path);
    if (pc5_info_read(root, id, &old)) return fail(install, "PC5 didn't install this game, so it can't update it.");
    if (old.usb[0])
        return fail(install, "%s plays from its USB drive: copy the newer version over its folder %s there.", old.title,
                    old.usb);
    install->info = old;
    if (pc5_format(install->game_dir, sizeof(install->game_dir), "%s/prefixes/%s/drive_c/Games/%s", root, id, old.folder) ||
        !pc5_is_dir(install->game_dir))
        return fail(install, "The game's folder is missing: uninstall it and install it again.");
    /* The copy of the same game: it has the game's program. */
    if (resolve_ci(folder->path, old.exe, program, sizeof(program)) || !pc5_is_file(program))
        return fail(install, "This folder doesn't have %s, the program %s starts. Choose the folder of a newer copy of %s.",
                    old.exe, old.title, old.title);
    install->info.bytes = folder->bytes;
    install->info.files = folder->files;
    for (int i = 0; i < folder->exe_count; i++)
        if (!pc5_casecmp(folder->exes[i].path, old.exe) && folder->exes[i].pe.bits) {
            install->info.bits = folder->exes[i].pe.bits;
            install->info.api = pc5_folder_api(folder, i);
        }
    if (pc5_format(path, sizeof(path), "%s/prefixes", root)) return fail(install, "A path is too long.");
    if (pc5_free_bytes(path, &free_bytes) == 0 && free_bytes < pc5_update_needs(folder)) {
        char need[32], have[32];
        pc5_size_text(pc5_update_needs(folder), need, sizeof(need));
        pc5_size_text(free_bytes, have, sizeof(have));
        return fail(install, "Not enough space: the update needs %s and the console has %s free.", need, have);
    }
    if (pc5_format(install->staging, sizeof(install->staging), "%s/prefixes/.updating-%s", root, id))
        return fail(install, "A path is too long.");
    if (pc5_exists(install->staging)) {
        install->staging[0] = 0;   /* not this update's: left alone */
        return fail(install, "An earlier update of this game isn't finished: close PC5 and open it again to finish it.");
    }
    if (mkdir(install->staging, 0777)) return fail(install, "PC5 can't start the update: %s.", strerror(errno));
    if (!(install->chunk = pc5_alloc(CHUNK))) {
        rmdir(install->staging);
        return fail(install, "Out of memory.");
    }
    install->bytes_total = install->info.bytes;
    install->files_total = install->info.files;
    install->phase = PC5_INSTALL_LIST;
    return 0;
}

/* ---- kept saves, installed ------------------------------------------------------------------- */

/* What of the kept saves makes the game's Windows: all but the game's folders and PC5's
 * notes, of which the options and the marks of what the Windows has (its DXVK, its
 * template's version) stay. */
static int saves_keep(const char *relative, int folder)
{
    if (!pc5_casecmp(relative, "drive_c/Games")) return 0;
    if (!strcmp(relative, "pc5")) return 1;
    if (!strncmp(relative, "pc5/", 4))
        return !folder && (!strcmp(relative, "pc5/controls.txt") || !strcmp(relative, "pc5/dxvk.txt") ||
                           !strcmp(relative, "pc5/template.txt"));
    return 1;
}

int pc5_install_use_saves(Pc5Install *install, const char *path)
{
    /* A game played from its USB drive: what it made in its own folder would go onto the drive. */
    if (install->in_place) return fail(install, "Kept saves can't be put back for a game that plays from its USB drive.");
    Pc5Buf found = { 0 }, ordered = { 0 };
    int has_system = 0, failed;

    if (install->phase != PC5_INSTALL_PREFIX || install->at) return fail(install, "The install has begun already.");
    failed = walk_tree(path, &found, saves_keep);
    /* As template_paths orders the template's: the registry after the rest, system.reg last. */
    for (int pass = 0; pass < 3 && !failed; pass++)
        for (size_t at = 0; at < found.len; at += strlen((char *)found.data + at) + 1) {
            const char *line = (const char *)found.data + at;
            size_t length = strlen(line);
            int registry = length > 4 && !strchr(line, '/') && !strcmp(line + length - 4, ".reg");
            int system = !strcmp(line, "system.reg");
            has_system |= system;
            if (pass == 0 ? registry : pass == 1 ? !registry || system : !system) continue;
            if (pc5_buf_put(&ordered, line, length + 1)) failed = 1;
        }
    pc5_buf_free(&found);
    if (failed || !has_system) {
        pc5_buf_free(&ordered);
        return fail(install, failed ? "PC5 can't read the kept saves." : "The kept saves are damaged: they have no registry.");
    }
    pc5_buf_free(&install->template_list);
    install->template_list = ordered;
    install->bytes_total = 0;
    install->files_total = 0;
    for (size_t at = 0; at < ordered.len; at += strlen((char *)ordered.data + at) + 1) {
        const char *line = (const char *)ordered.data + at;
        char source[PC5_PATH];
        struct stat st;
        if (line[strlen(line) - 1] == '/') continue;
        if (!pc5_join(source, sizeof(source), path, line) && !stat(source, &st)) install->bytes_total += (uint64_t)st.st_size;
        install->files_total++;
    }
    pc5_copy(install->template_prefix, sizeof(install->template_prefix), path);
    pc5_copy(install->kept, sizeof(install->kept), path);
    return 0;
}

Pc5InstallPhase pc5_install_step(Pc5Install *install, int budget_ms)
{
    uint64_t until = pc5_now_ms() + (uint64_t)(budget_ms > 0 ? budget_ms : 1);

    if (install->cancel && install->phase < PC5_INSTALL_FINISH) start_cleanup(install, 1);
    while (pc5_now_ms() < until) {
        switch (install->phase) {
        case PC5_INSTALL_PREFIX:
            if (install->at >= install->template_list.len && install->in_place) {
                /* Played from its USB drive: nothing to copy, its folder linked as it finishes. */
                install->at = 0;
                install->phase = PC5_INSTALL_FINISH;
                break;
            }
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
                /* An update's copy goes over the game's files: on the disk before it does. */
                if (!verify && install->out_fd >= 0 && install->update && fsync(install->out_fd)) {
                    close(install->out_fd);
                    install->out_fd = -1;
                    fail(install, "PC5 can't write %s: %s.", relative, strerror(errno));
                    start_cleanup(install, 0);
                    break;
                }
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
            if (install->update) {
                int placed = update_finish_step(install, until);
                if (placed < 0) {
                    /* Not whole yet: deleted, the game unchanged. Whole: finished at the next start. */
                    if (!install->moving) start_cleanup(install, 0);
                    else install->phase = PC5_INSTALL_FAILED;
                } else if (placed) {
                    install->phase = install->tidying ? PC5_INSTALL_CLEANUP : PC5_INSTALL_DONE;
                }
                break;
            }
            if (finish(install)) {
                /* Before the rename the folder is deleted; after it the game
                 * is in place and only its profile is missing. */
                if (strstr(install->staging, "/.installing-")) start_cleanup(install, 0);
                else install->phase = PC5_INSTALL_FAILED;
                break;
            }
            install->phase = install->tidying ? PC5_INSTALL_CLEANUP : PC5_INSTALL_DONE;
            if (!install->tidying) return install->phase;
            break;
        case PC5_INSTALL_CLEANUP:
            if (!install->cleanup || !pc5_remove_step(install->cleanup, (int)(until - pc5_now_ms()) + 1)) {
                /* Tidying after the game is in place: what is left is deleted at the next start. */
                if (install->tidying) {
                    install->phase = PC5_INSTALL_DONE;
                    return install->phase;
                }
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
    install->hashes = NULL;
    install->chunk = NULL;
    install->cleanup = NULL;
}
