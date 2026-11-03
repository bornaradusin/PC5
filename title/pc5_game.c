/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5 in a game's process (pc5_game.h). */
#include "pc5_game.h"
#include "pc5_i18n.h"
#include "pc5_controls.h"
#include "pc5_library.h"
#include "pc5_pe.h"
#include "pw_pc5_hud.h"

#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum { LIGHT_EVERY_MS = 100 };

static void say(Pc5GameSession *game, const char *format, ...) __attribute__((format(printf, 2, 3)));
static void say(Pc5GameSession *game, const char *format, ...)
{
    char line[300];
    va_list args;
    if (!game->ops.log) return;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    game->ops.log(line);
}

/* A colour at a light bar's brightness: its strongest channel to 230. */
static uint32_t bright(uint32_t rgb)
{
    uint32_t r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255, hi = r > g ? (r > b ? r : b) : (g > b ? g : b);
    if (hi < 8) return 0;
    return (r * 230 / hi) << 16 | (g * 230 / hi) << 8 | (b * 230 / hi);
}

/* The tint in the cover file's header ("PC5D" or "PC5C", width, height, tint). */
static uint32_t cover_tint(const char *root, const char *prefix)
{
    char path[PC5_PATH];
    uint8_t head[16];
    int fd;

    if (!prefix[0] || pc5_format(path, sizeof(path), "%s/prefixes/%s/pc5/cover.bin", root, prefix) ||
        (fd = open(path, O_RDONLY)) < 0)
        return 0;
    int got = (int)read(fd, head, sizeof(head));
    close(fd);
    if (got != 16 || memcmp(head, "PC5", 3)) return 0;
    return ((uint32_t)head[12] | (uint32_t)head[13] << 8 | (uint32_t)head[14] << 16) & 0xffffffu;
}

/* 1 when the file holds needle, 0 when not, -1 when it can't be read. */
static int file_holds(const char *path, const char *needle)
{
    static char buffer[65536 + 64];   /* once, at a game's start */
    size_t length = strlen(needle), kept = 0, got;
    int found = 0;
    FILE *file;

    if (!length || length > 64 || !(file = fopen(path, "rb"))) return -1;
    while (!found && (got = fread(buffer + kept, 1, 65536, file)) > 0) {
        size_t total = kept + got;
        for (size_t at = 0; !found && at + length <= total; at++)
            found = buffer[at] == needle[0] && !memcmp(buffer + at, needle, length);
        /* The needle may span two reads: keep the tail. */
        kept = total < length - 1 ? total : length - 1;
        memmove(buffer, buffer + total - kept, kept);
    }
    int failed = ferror(file);
    fclose(file);
    return failed ? -1 : found;
}

int pc5_prefix_media(const char *root, const char *prefix, const char *app)
{
    char system[PC5_PATH], media[PC5_PATH], chunk[16384];
    FILE *in, *out;
    size_t got;
    int ok = 1;

    if (!prefix[0] || !strcmp(prefix, "default")
            ? pc5_format(system, sizeof(system), "%s/prefix/system.reg", root)
            : pc5_format(system, sizeof(system), "%s/prefixes/%s/system.reg", root, prefix))
        return -1;
    int held = file_holds(system, "winegstreamer.dll");
    if (held) return held > 0 ? 0 : -1;
    if (pc5_format(media, sizeof(media), "%s/pc5/prefix-media.reg", app) || !(in = fopen(media, "rb")))
        return -1;
    if (!(out = fopen(system, "ab"))) {
        fclose(in);
        return -1;
    }
    while (ok && (got = fread(chunk, 1, sizeof(chunk), in)) > 0)
        ok = fwrite(chunk, 1, got, out) == got;
    ok = ok && !ferror(in);
    fclose(in);
    ok = !fclose(out) && ok;
    return ok ? 1 : -1;
}

/* The first line of a small text file, or "" when there is none. */
static void first_line(const char *path, char *out, size_t size)
{
    Pc5Buf text = { 0 };
    out[0] = 0;
    if (pc5_read_file(path, &text, 4096) || pc5_buf_terminate(&text)) {
        pc5_buf_free(&text);
        return;
    }
    char *end = strchr((char *)text.data, '\n');
    if (end) *end = 0;
    pc5_copy(out, size, (char *)text.data);
    pc5_buf_free(&text);
}

/* A copy of from at to, in chunks (a title's heap is small): 0, or -1. */
static int copy_file(const char *from, const char *to)
{
    static char chunk[65536];   /* once, at a game's start */
    FILE *in = fopen(from, "rb"), *out = in ? fopen(to, "wb") : NULL;
    size_t got;
    int ok = in && out;

    while (ok && (got = fread(chunk, 1, sizeof(chunk), in)) > 0)
        ok = fwrite(chunk, 1, got, out) == got;
    ok = ok && in && !ferror(in);
    if (in) fclose(in);
    if (out) ok = !fclose(out) && ok;
    return ok ? 0 : -1;
}

int pc5_prefix_system(const char *root, const char *prefix, const char *app)
{
    char note[PC5_PATH], has[64], list[PC5_PATH];
    Pc5Buf text = { 0 };
    int copied = 0, failed = 0;

    if (!prefix || !prefix[0] || !strcmp(prefix, "default") || !app || !app[0]) return 0;
    if (pc5_format(note, sizeof(note), "%s/prefixes/%s/pc5/template.txt", root, prefix) ||
        pc5_format(list, sizeof(list), "%s/pc5/prefix.list", app))
        return -1;
    first_line(note, has, sizeof(has));
    if (!strcmp(has, PC5_VERSION)) return 0;
    if (!pc5_is_file(list)) return 0;   /* an app without its template's list */
    if (pc5_read_file(list, &text, 16u << 20) || pc5_buf_terminate(&text)) {
        pc5_buf_free(&text);
        return -1;
    }
    for (char *line = (char *)text.data, *next; line && *line; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        char from[PC5_PATH], to[PC5_PATH], part[PC5_PATH];
        if ((strncmp(line, "drive_c/windows/system32/", 25) && strncmp(line, "drive_c/windows/syswow64/", 25)) ||
            strchr(line + 25, '/') || !pc5_ends_with_ci(line, ".dll"))
            continue;
        if (pc5_format(from, sizeof(from), "%s/pc5/prefix/%s", app, line) ||
            pc5_format(to, sizeof(to), "%s/prefixes/%s/%s", root, prefix, line) ||
            pc5_format(part, sizeof(part), "%s.pc5-part", to)) {
            failed = 1;
            continue;
        }
        if (pc5_exists(to)) continue;
        char *slash = strrchr(to, '/');
        *slash = 0;
        int folder_made = !pc5_mkdirs(to);
        *slash = '/';
        /* Written beside and renamed into place: a cut-short copy leaves nothing half there. */
        if (!folder_made || copy_file(from, part) || rename(part, to)) {
            unlink(part);
            failed = 1;
            continue;
        }
        copied++;
    }
    pc5_buf_free(&text);
    if (failed) return -1;
    char folder[PC5_PATH], line[64];
    snprintf(line, sizeof(line), "%s\n", PC5_VERSION);
    if (pc5_format(folder, sizeof(folder), "%s/prefixes/%s/pc5", root, prefix) || pc5_mkdirs(folder) ||
        pc5_write_file(note, line, strlen(line)))
        return -1;
    return copied;
}

int pc5_prefix_dxvk(const char *root, const char *prefix, const char *app)
{
    static const char *const dlls[] = { "d3d8.dll", "d3d9.dll", "d3d10core.dll", "d3d11.dll", "dxgi.dll",
                                        "d3d12.dll", "d3d12core.dll" };   /* and VKD3D-Proton's */
    static const char *const folders[] = { "system32", "syswow64" };
    char wanted[128], has[128], note[PC5_PATH];

    if (!prefix || !prefix[0] || !strcmp(prefix, "default") || !app || !app[0]) return 0;
    if (pc5_format(note, sizeof(note), "%s/pc5/dxvk.txt", app)) return -1;
    first_line(note, wanted, sizeof(wanted));
    if (!wanted[0]) return 0;   /* an app that doesn't name its DXVK */
    if (pc5_format(note, sizeof(note), "%s/prefixes/%s/pc5/dxvk.txt", root, prefix)) return -1;
    first_line(note, has, sizeof(has));
    if (!strcmp(has, wanted)) return 0;
    for (size_t f = 0; f < 2; f++) {
        for (size_t d = 0; d < sizeof(dlls) / sizeof(dlls[0]); d++) {
            char from[PC5_PATH], to[PC5_PATH], part[PC5_PATH];
            if (pc5_format(from, sizeof(from), "%s/pc5/prefix/drive_c/windows/%s/%s", app, folders[f], dlls[d]) ||
                pc5_format(to, sizeof(to), "%s/prefixes/%s/drive_c/windows/%s/%s", root, prefix, folders[f], dlls[d]) ||
                pc5_format(part, sizeof(part), "%s.pc5-part", to))
                return -1;
            if (!pc5_is_file(to)) continue;   /* a Windows without this one: nothing to replace */
            /* Written beside the old one and renamed over it: a cut-short copy leaves the old DLL. */
            if (copy_file(from, part) || rename(part, to)) {
                unlink(part);
                return -1;
            }
        }
    }
    char text[160], folder[PC5_PATH];
    snprintf(text, sizeof(text), "%s\n", wanted);
    if (pc5_format(folder, sizeof(folder), "%s/prefixes/%s/pc5", root, prefix) || pc5_mkdirs(folder)) return -1;
    return pc5_write_file(note, text, strlen(text)) ? -1 : 1;
}

/* text appended to the file at path: 0, or -1 */
static int append_text(const char *path, const char *text)
{
    FILE *out = fopen(path, "ab");
    int ok = out && fwrite(text, 1, strlen(text), out) == strlen(text);
    if (out) ok = !fclose(out) && ok;
    return ok ? 0 : -1;
}

/* The app's file at from appended to the file at to: 0, or -1 */
static int append_file(const char *from, const char *to)
{
    Pc5Buf text = { 0 };
    int status = pc5_read_file(from, &text, 64u << 20) || pc5_buf_terminate(&text) ? -1
                                                                                    : append_text(to, (char *)text.data);
    pc5_buf_free(&text);
    return status;
}

/* The version of the app's Wine Mono (pc5/mono-version.txt), "" when it has none. */
static void mono_version(const char *app, char *out, size_t size)
{
    char note[PC5_PATH];
    out[0] = 0;
    if (!pc5_format(note, sizeof(note), "%s/pc5/mono-version.txt", app)) first_line(note, out, size);
    for (char *p = out; *p; p++)
        if (!((*p >= '0' && *p <= '9') || *p == '.')) { out[0] = 0; break; }
}

int pc5_prefix_dotnet(const char *root, const char *prefix, const char *app)
{
    char version[32], system[PC5_PATH], user[PC5_PATH], from[PC5_PATH], value[PC5_PATH * 2], needle[64], text[PC5_PATH * 2 + 96];
    int changed = 0, held;

    if (!prefix || !prefix[0] || !strcmp(prefix, "default") || !app || !app[0]) return 0;
    mono_version(app, version, sizeof(version));
    if (!version[0]) return 0;   /* an app without Wine Mono */
    if (pc5_format(system, sizeof(system), "%s/prefixes/%s/system.reg", root, prefix) ||
        pc5_format(user, sizeof(user), "%s/prefixes/%s/user.reg", root, prefix))
        return -1;
    /* What its installer registers: the template has it, a game installed before doesn't. */
    if ((held = file_holds(system, "NET Framework Setup\\\\NDP\\\\v4\\\\Full]")) < 0) return -1;
    if (!held) {
        Pc5Buf list = { 0 };
        int failed = 0;
        if (pc5_format(from, sizeof(from), "%s/pc5/prefix.list", app) || pc5_read_file(from, &list, 64u << 20) ||
            pc5_buf_terminate(&list)) {
            pc5_buf_free(&list);
            return -1;
        }
        /* C:\windows\Microsoft.NET, which it puts beside the runtime (fusion.dll, mscorlib.dll, ...) */
        static const char net[] = "drive_c/windows/Microsoft.NET/";
        for (char *line = (char *)list.data, *next; !failed && line && *line; line = next) {
            char to[PC5_PATH], source[PC5_PATH], part[PC5_PATH];
            size_t n;
            next = strchr(line, '\n');
            if (next) *next++ = 0;
            if (strncmp(line, net, sizeof(net) - 1)) continue;
            n = strlen(line);
            if (pc5_format(to, sizeof(to), "%s/prefixes/%s/%s", root, prefix, line) ||
                pc5_format(source, sizeof(source), "%s/pc5/prefix/%s", app, line)) { failed = 1; break; }
            if (line[n - 1] == '/') { failed = pc5_mkdirs(to) != 0; continue; }
            if (pc5_format(part, sizeof(part), "%s.pc5-part", to) || copy_file(source, part) || rename(part, to)) {
                unlink(part);
                failed = 1;
            }
        }
        pc5_buf_free(&list);
        if (failed || pc5_format(from, sizeof(from), "%s/pc5/prefix-dotnet.reg", app) || append_file(from, system))
            return -1;
        changed = 1;
    }
    /* Where the runtime is: root as a Windows path on Z:, with the registry's doubled
     * backslashes. Appended again when the version (and so the folder) changes: the last
     * value read wins. */
    size_t at = 0;
    at += (size_t)snprintf(value + at, sizeof(value) - at, "Z:");
    for (const char *p = root; *p && at + 3 < sizeof(value); p++) {
        if (*p == '/') { value[at++] = '\\'; value[at++] = '\\'; }
        else value[at++] = *p;
    }
    value[at] = 0;
    if ((size_t)snprintf(needle, sizeof(needle), "pc5\\\\mono-%s\\\\mono-2.0\"", version) >= sizeof(needle) ||
        (size_t)snprintf(text, sizeof(text), "\n[Software\\\\Wine\\\\Mono]\n\"RuntimePath\"=\"%s\\\\pc5\\\\mono-%s\\\\mono-2.0\"\n",
                         value, version) >= sizeof(text))
        return -1;
    if ((held = file_holds(user, needle)) < 0) return -1;
    if (!held) {
        if (append_text(user, text)) return -1;
        changed = 1;
    }
    return changed;
}

int pc5_prefix_fonts(const char *root, const char *prefix, const char *app)
{
    char user[PC5_PATH], from[PC5_PATH];

    if (!app || !app[0]) return 0;
    if (pc5_format(from, sizeof(from), "%s/pc5/prefix-fonts.reg", app)) return -1;
    if (!pc5_is_file(from)) return 0;   /* an app without the fonts */
    if (!prefix || !prefix[0] || !strcmp(prefix, "default")
            ? pc5_format(user, sizeof(user), "%s/prefix/user.reg", root)
            : pc5_format(user, sizeof(user), "%s/prefixes/%s/user.reg", root, prefix))
        return -1;
    int held = file_holds(user, "\"Arial\"=\"Liberation Sans\"");
    if (held) return held > 0 ? 0 : -1;
    return append_file(from, user) ? -1 : 1;
}

/* Arial's files, as games open them in C:\Windows\Fonts by name, and Liberation Sans's, which has
 * Arial's metrics: (Arial's file, Liberation's) pairs. */
static const char *const arial_files[][2] = {
    { "arial.ttf", "LiberationSans-Regular.ttf" }, { "arialbd.ttf", "LiberationSans-Bold.ttf" },
    { "ariali.ttf", "LiberationSans-Italic.ttf" }, { "arialbi.ttf", "LiberationSans-BoldItalic.ttf" },
};

int pc5_prefix_arial(const char *root, const char *prefix, const char *app)
{
    char windows[PC5_PATH], fonts[PC5_PATH], to[PC5_PATH], from[PC5_PATH];
    int added = 0;

    if (!app || !app[0]) return 0;
    if (!prefix || !prefix[0] || !strcmp(prefix, "default")
            ? pc5_format(windows, sizeof(windows), "%s/prefix/drive_c/windows", root)
            : pc5_format(windows, sizeof(windows), "%s/prefixes/%s/drive_c/windows", root, prefix))
        return -1;
    if (!pc5_exists(windows)) return 0;   /* no Windows yet: the next start adds them */
    if (pc5_format(fonts, sizeof(fonts), "%s/Fonts", windows) || pc5_mkdirs(fonts)) return -1;
    for (size_t i = 0; i < sizeof(arial_files) / sizeof(arial_files[0]); i++) {
        Pc5Buf data = { 0 };
        if (pc5_format(to, sizeof(to), "%s/%s", fonts, arial_files[i][0]) ||
            pc5_format(from, sizeof(from), "%s/win/wine/share/wine/fonts/%s", app, arial_files[i][1]))
            return -1;
        if (pc5_exists(to) || !pc5_is_file(from)) continue;   /* the player's own Arial stays */
        int failed = pc5_read_file(from, &data, 4u << 20) || pc5_write_file(to, data.data, data.len);
        pc5_buf_free(&data);
        if (failed) return -1;
        added++;
    }
    return added;
}

/* Programs whose own check refuses Windows 10, by name, and the Windows they were made for. */
static const struct { const char *program, *version; } program_versions[] = {
    { "Conviction_game.exe", "win7" },   /* Splinter Cell: Conviction's system check: "Operating System not supported" */
};

int pc5_prefix_versions(const char *root, const char *prefix)
{
    char user[PC5_PATH], needle[PC5_NAME + 64], text[PC5_NAME + 128];
    int added = 0;

    if (!prefix || !prefix[0] || !strcmp(prefix, "default")
            ? pc5_format(user, sizeof(user), "%s/prefix/user.reg", root)
            : pc5_format(user, sizeof(user), "%s/prefixes/%s/user.reg", root, prefix))
        return -1;
    if (!pc5_is_file(user)) return 0;   /* no Windows yet: Wine makes it, and the next start adds them */
    for (size_t i = 0; i < sizeof(program_versions) / sizeof(program_versions[0]); i++) {
        if (pc5_format(needle, sizeof(needle), "[Software\\\\Wine\\\\AppDefaults\\\\%s]", program_versions[i].program) ||
            pc5_format(text, sizeof(text), "\n%s\n\"Version\"=\"%s\"\n", needle, program_versions[i].version))
            return -1;
        int held = file_holds(user, needle);
        if (held < 0) return -1;
        if (held) continue;
        if (append_text(user, text)) return -1;
        added = 1;
    }
    return added;
}

/* 1 when the folder dir has a file of that name, in any case. */
static int folder_has(const char *dir, const char *name)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    int found = 0;

    if (!d) return 0;
    while (!found && (e = readdir(d))) found = !strcasecmp(e->d_name, name);
    closedir(d);
    return found;
}

/* Games whose program refuses to start without the keys its installer wrote ("The game is not
 * properly installed"), known by a file in the program's folder (so its settings program
 * gets them too). PC5 copies a game's folder and runs no installer, so it writes them itself:
 * the folder as the installer would have, and the languages whose files the game has.
 * The CD key the installer asked for is not among them: that is the player's. */
static const struct {
    const char *marker;      /* a file in the game's folder, any case */
    const char *key;         /* under HKLM\Software (and Wow6432Node), with the registry's doubled backslashes */
    const char *languages;   /* "lang_<c>" for each <c> whose dat/<c>_text.afs is there; the first when none is */
} installer_keys[] = {
    { "PES6.exe", "KONAMIPES6\\\\PES6", "efgisp" },   /* Pro Evolution Soccer 6 and its settings.exe */
};

int pc5_prefix_installs(const char *root, const char *prefix, const char *program)
{
    char system[PC5_PATH], drive[PC5_PATH], dir[PC5_PATH], value[PC5_PATH * 2], needle[160], text[PC5_PATH * 2 + 1024];
    int added = 0;

    if (!prefix || !prefix[0] || !strcmp(prefix, "default") || !program || !program[0]) return 0;
    if (pc5_format(system, sizeof(system), "%s/prefixes/%s/system.reg", root, prefix) ||
        pc5_format(drive, sizeof(drive), "%s/prefixes/%s/drive_c/", root, prefix))
        return -1;
    if (!pc5_is_file(system)) return 0;   /* no Windows yet: Wine makes it, and the next start adds them */
    /* The program's folder as Windows sees it, on C:. A game played from a USB drive or extended
     * storage is there too: its folder on the drive is C:\Games\<folder> to Windows. */
    const char *slash = strrchr(program, '/');
    size_t length = strlen(drive);
    char inside[PC5_PATH], top[PC5_PATH];
    Pc5Info info;
    if (!slash || (size_t)(slash - program) >= sizeof(dir)) return 0;
    memcpy(dir, program, (size_t)(slash - program));
    dir[slash - program] = 0;
    if (!strncmp(dir, drive, length)) {
        pc5_copy(inside, sizeof(inside), dir + length);
    } else {
        size_t n;
        if (pc5_info_read(root, prefix, &info) || pc5_game_folder(root, prefix, top, sizeof(top))) return 0;
        n = strlen(top);
        if (strncmp(dir, top, n) || (dir[n] && dir[n] != '/') ||
            pc5_format(inside, sizeof(inside), "Games/%s%s", info.folder, dir + n))
            return 0;   /* not in the game's folder */
    }
    size_t at = (size_t)snprintf(value, sizeof(value), "C:\\\\");
    for (const char *p = inside; *p && at + 3 < sizeof(value); p++) {
        if (*p == '/') { value[at++] = '\\'; value[at++] = '\\'; }
        else value[at++] = *p;
    }
    if (at + 3 >= sizeof(value)) return -1;
    value[at++] = '\\';
    value[at++] = '\\';
    value[at] = 0;
    for (size_t i = 0; i < sizeof(installer_keys) / sizeof(installer_keys[0]); i++) {
        if (!folder_has(dir, installer_keys[i].marker)) continue;
        if (pc5_format(needle, sizeof(needle), "[Software\\\\Wow6432Node\\\\%s]", installer_keys[i].key)) return -1;
        int held = file_holds(system, needle);
        if (held < 0) return -1;
        if (held) continue;
        char languages[160] = "", data[PC5_PATH], afs[16];
        int any = 0;
        size_t used = 0;
        for (const char *c = installer_keys[i].languages; *c; c++) {
            snprintf(afs, sizeof(afs), "%c_text.afs", *c);
            int has = !pc5_format(data, sizeof(data), "%s/dat", dir) && folder_has(data, afs);
            any |= has;
            used += (size_t)snprintf(languages + used, sizeof(languages) - used, "\"lang_%c\"=dword:0000000%d\n", *c, has);
        }
        if (!any) languages[strlen("\"lang_x\"=dword:0000000")] = '1';   /* the first language */
        used = 0;
        for (int wow = 0; wow < 2; wow++) {
            const char *node = wow ? "Wow6432Node\\\\" : "";
            int n = snprintf(text + used, sizeof(text) - used,
                             "\n[Software\\\\%s%s]\n\"installdir\"=\"%s\"\n%s\n[Software\\\\%s%s\\\\1.0]\n", node,
                             installer_keys[i].key, value, languages, node, installer_keys[i].key);
            if (n < 0 || (size_t)n >= sizeof(text) - used) return -1;
            used += (size_t)n;
        }
        if (append_text(system, text)) return -1;
        added = 1;
    }
    return added;
}

/* ---- the runtime's copy ------------------------------------------------------------------- */

static uint64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}

/* 1 when a DLL beside the program is a .NET one (a C++ library for .NET a native game loads,
 * as an Unreal Engine game's speech): it needs the runtime as a .NET program does. */
static int beside_dotnet(const char *program)
{
    char dir[PC5_PATH], path[PC5_PATH];
    const char *slash = strrchr(program, '/');
    int found = 0, looked = 0;
    DIR *d;
    struct dirent *e;

    if (!slash || (size_t)(slash - program) >= sizeof(dir)) return 0;
    memcpy(dir, program, (size_t)(slash - program));
    dir[slash - program] = 0;
    if (!(d = opendir(dir))) return 0;
    while (!found && looked < 1000 && (e = readdir(d))) {
        Pc5PeInfo pe;
        if (!pc5_ends_with_ci(e->d_name, ".dll")) continue;
        looked++;
        if (!pc5_format(path, sizeof(path), "%s/%s", dir, e->d_name) && !pc5_pe_read(path, &pe) &&
            (pe.imports & PC5_IMPORT_DOTNET))
            found = 1;
    }
    closedir(d);
    return found;
}

int pc5_runtime_begin(Pc5Runtime *runtime, const char *root, const char *app, const char *program)
{
    char version[32], done[PC5_PATH], list[PC5_PATH];
    Pc5PeInfo pe;
    int dotnet;

    memset(runtime, 0, sizeof(*runtime));
    if (!program || pc5_pe_read(program, &pe)) return 0;
    dotnet = (pe.imports & PC5_IMPORT_DOTNET) != 0;
    mono_version(app, version, sizeof(version));
    if (!version[0]) return 0;
    if (pc5_format(runtime->from, sizeof(runtime->from), "%s/pc5/mono", app) ||
        pc5_format(runtime->to, sizeof(runtime->to), "%s/pc5/mono-%s", root, version) ||
        pc5_format(done, sizeof(done), "%s/.complete", runtime->to) ||
        pc5_format(list, sizeof(list), "%s/pc5/mono.list", app)) {
        snprintf(runtime->error, sizeof(runtime->error), N_("The app's path is too long."));
        return -1;
    }
    if (pc5_exists(done)) return 0;
    if (!dotnet && !beside_dotnet(program)) return 0;
    if (pc5_read_file(list, &runtime->list, 16u << 20) || pc5_buf_terminate(&runtime->list) || pc5_mkdirs(runtime->to)) {
        snprintf(runtime->error, sizeof(runtime->error), N_("PC5 can't read the app's .NET runtime."));
        pc5_buf_free(&runtime->list);
        return -1;
    }
    for (size_t i = 0; i < runtime->list.len; i++) {
        if (runtime->list.data[i] == '\n') {
            runtime->list.data[i] = 0;
            runtime->total++;
        }
    }
    return 1;
}

int pc5_runtime_step(Pc5Runtime *runtime, int budget_ms)
{
    uint64_t until = now_ms() + (uint64_t)(budget_ms > 0 ? budget_ms : 1);
    char from[PC5_PATH], to[PC5_PATH], part[PC5_PATH];

    while (runtime->at < runtime->list.len) {
        const char *line = (const char *)runtime->list.data + runtime->at;
        size_t n = strlen(line);
        runtime->at += n + 1;
        if (!n) continue;
        if (pc5_format(from, sizeof(from), "%s/%s", runtime->from, line) ||
            pc5_format(to, sizeof(to), "%s/%s", runtime->to, line) || pc5_format(part, sizeof(part), "%s.pc5-part", to)) {
            snprintf(runtime->error, sizeof(runtime->error), N_("A path of the .NET runtime is too long."));
            return -1;
        }
        if (line[n - 1] == '/') {
            if (pc5_mkdirs(to)) {
                snprintf(runtime->error, sizeof(runtime->error), N_("PC5 can't make the .NET runtime's folders: is the console full?"));
                return -1;
            }
        } else {
            char folder[PC5_PATH];
            pc5_copy(folder, sizeof(folder), to);
            *strrchr(folder, '/') = 0;   /* its folder, should the list not name it */
            if (!pc5_is_file(from)) {
                /* Not a full disk: the app's own copy lacks it (copied off a PC without it). */
                const char *name = strrchr(line, '/');
                snprintf(runtime->error, sizeof(runtime->error),
                         N_("The app's .NET runtime has no %.80s. Copy the PPSA11018 folder to the console again."),
                         name ? name + 1 : line);
                return -1;
            }
            if (pc5_mkdirs(folder) || copy_file(from, part) || rename(part, to)) {
                unlink(part);
                snprintf(runtime->error, sizeof(runtime->error), N_("PC5 can't copy the .NET runtime: is the console full?"));
                return -1;
            }
        }
        runtime->done++;
        if (now_ms() >= until) return 0;
    }
    if (pc5_format(to, sizeof(to), "%s/.complete", runtime->to) || pc5_write_file(to, "", 0)) {
        snprintf(runtime->error, sizeof(runtime->error), N_("PC5 can't finish the .NET runtime's copy."));
        return -1;
    }
    return 1;
}

void pc5_runtime_end(Pc5Runtime *runtime)
{
    pc5_buf_free(&runtime->list);
}

int pc5_game_env(const char *root, const char *prefix, char *names, size_t size)
{
    char path[PC5_PATH];
    Pc5Buf text = { 0 };
    int set = 0;

    if (names && size) names[0] = 0;
    if (!prefix || !prefix[0] || pc5_format(path, sizeof(path), "%s/prefixes/%s/pc5/env.txt", root, prefix) ||
        pc5_read_file(path, &text, 64 * 1024) || pc5_buf_terminate(&text)) {
        pc5_buf_free(&text);
        return 0;
    }
    for (char *line = (char *)text.data, *next; line && *line && set < PC5_GAME_ENV_MAX; line = next) {
        char *equals, *end;
        size_t n;
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        while (*line == ' ' || *line == '\t') line++;
        for (end = line + strlen(line); end > line && (end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t'); end--) {}
        *end = 0;
        if (!line[0] || line[0] == '#' || !(equals = strchr(line, '='))) continue;
        *equals = 0;
        for (n = strlen(line); n && (line[n - 1] == ' ' || line[n - 1] == '\t'); n--) line[n - 1] = 0;
        char *value = equals + 1;
        while (*value == ' ' || *value == '\t') value++;
        if (!pc5_game_env_name_ok(line)) continue;
        if (setenv(line, value, 1)) continue;
        set++;
        if (names && size) {
            size_t at = strlen(names);
            snprintf(names + at, size - at, "%s%s", at ? "," : "", line);
        }
    }
    pc5_buf_free(&text);
    return set;
}

int pc5_game_env_name_ok(const char *name)
{
    /* What Wine, prospero-win and PC5 set themselves, and what finds programs and libraries. */
    static const char *const kept[] = { "WINE", "PW_", "LD_", "PS5", "PC5" };
    /* ... except prospero-win's per-frame profile, for finding what slows a game down
     * (docs/cs-thread-profile.md), whose switches only add lines to the log, and how long a
     * wait spins before it sleeps (Wine patch 0998; 0 off), and how long the pointer stays on
     * screen unused (Wine patch 0999; 0 always). */
    static const char *const profile[] = { "PW_NATIVE_PROFILE", "PW_VK_RADV_PROFILE", "PW_VK_RADV_EVENT_US",
                                           "PW_NATIVE_SLOW_SYSCALL_US", "WINE_PS5_SYNC_SPIN_US",
                                           "WINE_PS5_CURSOR_IDLE_MS" };
    for (size_t i = 0; i < sizeof(profile) / sizeof(profile[0]); i++)
        if (!strcmp(name, profile[i])) return 1;
    if (!name[0] || strlen(name) > 64 || !((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z') ||
                                           name[0] == '_'))
        return 0;
    for (const char *c = name; *c; c++)
        if (!((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_')) return 0;
    for (size_t i = 0; i < sizeof(kept) / sizeof(kept[0]); i++)
        if (!strncasecmp(name, kept[i], strlen(kept[i]))) return 0;
    return pc5_casecmp(name, "PATH") && pc5_casecmp(name, "HOME") && pc5_casecmp(name, "TMPDIR");
}

/* The folders in the game's pc5/path.txt (one a line, as Windows writes them), each once,
 * after what path has, ';' between. */
static void redist_path(const char *root, const char *prefix, char *path, size_t size)
{
    char file[PC5_PATH];
    Pc5Buf text = { 0 };
    if (!prefix || !prefix[0] || !strcmp(prefix, "default") ||
        pc5_format(file, sizeof(file), "%s/prefixes/%s/pc5/path.txt", root, prefix) || !pc5_is_file(file) ||
        pc5_read_file(file, &text, 65536) || pc5_buf_terminate(&text)) {
        pc5_buf_free(&text);
        return;
    }
    for (char *line = (char *)text.data, *next; line && *line; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = 0;
        if (!n || strchr(line, ';')) continue;
        /* Once: not when path has it already. */
        int had = 0;
        for (const char *at = path; *at && !had;) {
            const char *end = strchr(at, ';');
            size_t len = end ? (size_t)(end - at) : strlen(at);
            had = len == n && !strncasecmp(at, line, n);
            at = end ? end + 1 : at + len;
        }
        size_t used = strlen(path);
        if (!had && used + n + 2 < size) snprintf(path + used, size - used, "%s%s", used ? ";" : "", line);
    }
    pc5_buf_free(&text);
}

void pc5_game_begin(Pc5GameSession *game, const Pc5GameOps *ops, const char *root, const char *app,
                    const char *prefix, const char *title)
{
    memset(game, 0, sizeof(*game));
    game->ops = *ops;
    pc5_settings_load(root, &game->settings);
    pc5_copy(game->app, sizeof(game->app), app ? app : "");
    pc5_copy(game->title, sizeof(game->title), title ? title : "");
    game->cover_tint = cover_tint(root, prefix ? prefix : "");
    pc5_copy(game->root, sizeof(game->root), root ? root : "");
    pc5_copy(game->prefix, sizeof(game->prefix), prefix ? prefix : "");
    game->light_target = bright(game->cover_tint);
    game->trigger_amplitude = 0;
    game->resistance = game->settings.triggers == PC5_TRIGGERS_RESISTANCE || game->settings.triggers == PC5_TRIGGERS_BOTH;
    if (prefix && app && app[0]) {
        int media = pc5_prefix_media(root, prefix, app);
        if (media) say(game, "PC5 prefix media %s", media > 0 ? "registered" : "registration failed");
        int system = pc5_prefix_system(root, prefix, app);
        if (system) say(game, "PC5 prefix system files %s %d", system > 0 ? "added" : "failed", system > 0 ? system : 0);
        int dxvk = pc5_prefix_dxvk(root, prefix, app);
        if (dxvk) say(game, "PC5 prefix DXVK %s", dxvk > 0 ? "updated" : "update failed");
        int dotnet = pc5_prefix_dotnet(root, prefix, app);
        if (dotnet) say(game, "PC5 prefix .NET %s", dotnet > 0 ? "updated" : "update failed");
        int fonts = pc5_prefix_fonts(root, prefix, app);
        if (fonts) say(game, "PC5 prefix fonts %s", fonts > 0 ? "named" : "naming failed");
        int arial = pc5_prefix_arial(root, prefix, app);
        if (arial) say(game, "PC5 prefix Arial files %s %d", arial > 0 ? "added" : "failed", arial > 0 ? arial : 0);
        int versions = pc5_prefix_versions(root, prefix);
        if (versions) say(game, "PC5 prefix Windows versions %s", versions > 0 ? "added" : "failed");
        char started[PC5_PATH];
        if (!pc5_game_program(root, prefix, started, sizeof(started))) {
            int installs = pc5_prefix_installs(root, prefix, started);
            if (installs) say(game, "PC5 prefix installer keys %s", installs > 0 ? "added" : "failed");
        }
    }
    /* Frame generation and the aspect ratio, which libvulkan.prx reads from the
     * environment (the picture of a game drawn on the CPU follows its profile's
     * scaling), a 32-bit game's address space, which Wine does (patch 0921:
     * 4 GB for any program unless PW_LARGE_ADDRESS_AWARE=0, which Auto sets: then only for one
     * whose large-address flag asks for it, as on Windows), and CPU sharing, which Wine's server
     * reads (patch 0882, WINE_PS5_SCHED=1; the profile says it too, but PC5 writes
     * only the profiles of the games it installed). */
    int memory_4gb = 0, stretch = 0, cpu_sharing = 0, video_memory_full = 0, video_memory_mb = 0, pointer_hide = 0;
    int surround = 0;
    if (prefix && prefix[0]) {
        Pc5Controls controls;
        pc5_controls_load(root, prefix, &controls);
        game->frame_generation = controls.frame_generation;
        memory_4gb = controls.memory_4gb;
        stretch = controls.stretch;
        cpu_sharing = controls.cpu_sharing;
        video_memory_full = controls.video_memory_full;
        video_memory_mb = controls.video_memory_mb >= 512 && controls.video_memory_mb <= 6144 ? controls.video_memory_mb : 0;
        pointer_hide = controls.pointer_hide;
        surround = controls.surround;
        /* A two-finger click mapped to a key (Keyboard & mouse): the game's, not PC5's keyboard. */
        game->two_finger_key = controls.mode == PC5_PAD_KEYBOARD &&
                               strcmp(pc5_keys[controls.key[PC5_TWO_FINGER] % pc5_key_count].name, "none");
    }
    /* The mouse pointer as the game shows it, unless its Controller page says it hides when
     * unused (Wine patch 0999: 0 never hides). A game's env.txt, applied after, can say too. */
    setenv("WINE_PS5_CURSOR_IDLE_MS", pointer_hide ? "5000" : "0", 1);
    /* A USB keyboard types as the layout of PC5's language (Wine patch 0992), unless Settings ›
     * System › Keyboard layout says US. The controller's keys are the same under any layout. */
    {
        const char *layout = game->settings.keyboard_us ? "us"
                             : pc5_keyboard_layout(game->settings.language,
                                                   game->ops.console_language ? game->ops.console_language() : -1);
        setenv("WINE_PS5_KEYBOARD", layout, 1);
        say(game, "PC5 keyboard layout=%s", layout);
    }
    /* Video memory: the console's 12 GB are shared between the game and its graphics, but
     * DXGI said 8 GB were the graphics' own, and a Direct3D 11 or 12 game that sized its
     * textures to that ran the console out of memory. Auto tells it 4 GB (DXVK's dxgi, with
     * patches/dxvk 0005 for the budget), a size chosen that size (2 GB to 6 GB; 512 MB to 3 GB
     * for a 32-bit game); Full the whole heap.
     * A game's env.txt, applied after, can set DXVK_CONFIG itself. */
    {
        /* With the DXVK options of a game PC5 knows (pc5_known_dxvk: GTA IV's). */
        char dxvk_config[256] = "", program[PC5_PATH];
        const char *known = prefix && prefix[0] && !pc5_game_program(root, prefix, program, sizeof(program))
                                ? pc5_known_dxvk(program)
                                : NULL;
        if (!video_memory_full)
            snprintf(dxvk_config, sizeof(dxvk_config), "dxgi.maxDeviceMemory = %d",
                     video_memory_mb ? video_memory_mb : 4096);
        if (known) {
            size_t n = strlen(dxvk_config);
            snprintf(dxvk_config + n, sizeof(dxvk_config) - n, "%s%s", n ? "; " : "", known);
            say(game, "PC5 DXVK options for this game: %s", known);
        }
        if (dxvk_config[0]) setenv("DXVK_CONFIG", dxvk_config, 1);
        else unsetenv("DXVK_CONFIG");
    }
    /* One generator at most: PC5's own (PW_PC5_FRAMEGEN=1; 60 -> 120, or 30 -> 60 at 60 Hz) or
     * MihawkGen at three pictures a frame (PW_FRAMEGEN=3; 40 -> 120). That one only with 120 Hz
     * output: without, it would be 30 -> 60 as PC5's own is, and the menu shows it as that. */
    const int generator = game->frame_generation == 2 && !game->settings.high_refresh ? 1 : game->frame_generation;
    if (generator == 1) setenv("PW_PC5_FRAMEGEN", "1", 1);
    else unsetenv("PW_PC5_FRAMEGEN");
    if (generator == 2) setenv("PW_FRAMEGEN", "3", 1);
    else unsetenv("PW_FRAMEGEN");
    /* 120 Hz output: RADV opens the display at 119.88 Hz where the TV takes it (patches/radv 0004),
     * the title tells Wine (WINE_PS5_REFRESH); "0" keeps it at 60 Hz. */
    setenv("PW_PC5_HIGH_REFRESH", game->settings.high_refresh ? "1" : "0", 1);
    if (stretch) setenv("PW_PC5_STRETCH", "1", 1);
    else unsetenv("PW_PC5_STRETCH");
    /* A launcher that starts the game: Wine hands that program to PC5 (Wine patch 0923,
     * pc5_handoff_take), which starts it in the launcher's place. */
    char handoff[PC5_PATH], handoff_dir[PC5_PATH];
    if (prefix && prefix[0] && strcmp(prefix, "default") &&
        !pc5_handoff_env(root, prefix, handoff, sizeof(handoff), handoff_dir, sizeof(handoff_dir))) {
        unlink(handoff);   /* one left from before was never taken */
        setenv("PW_HANDOFF", handoff, 1);
        setenv("PW_HANDOFF_DIR", handoff_dir, 1);
    } else {
        unsetenv("PW_HANDOFF");
        unsetenv("PW_HANDOFF_DIR");
    }
    if (memory_4gb) unsetenv("PW_LARGE_ADDRESS_AWARE");
    else setenv("PW_LARGE_ADDRESS_AWARE", "0", 1);
    if (cpu_sharing) setenv("WINE_PS5_SCHED", "1", 1);
    else unsetenv("WINE_PS5_SCHED");
    /* Sound output: the speakers the game is offered (Wine's audio driver) and, for 5.1 or 7.1,
     * the console's port in 7.1; the title plays stereo if the console won't open that. */
    setenv("WINE_PS5_SPEAKERS", surround == 2 ? "8" : surround == 1 ? "6" : "2", 1);
    /* The game's own settings it reads from its environment (pc5/env.txt, pc5_game.h). */
    {
        char names[512];
        int set = pc5_game_env(root, prefix, names, sizeof(names));
        if (set) say(game, "PC5 env.txt set=%d %s", set, names);
        /* A per-frame profile writes so much that the log's usual chunk holds only its last
         * few seconds: larger chunks, so a slow stretch is in what's kept. */
        if (set && game->ops.log_chunk &&
            (strstr(names, "PW_NATIVE_PROFILE") || strstr(names, "PW_VK_RADV_PROFILE") || strstr(names, "DXVK_CS_PROFILE"))) {
            game->ops.log_chunk((size_t)16 << 20);
            say(game, "PC5 log chunks of 16 MB for the profile");
        }
    }
    /* A Java game's launcher made with jpackage: its app folder in PATH from the start, which
     * Wine takes from WINEPATH (pc5_game_winepath); and the folders the game's redistributables
     * put in PATH (pc5_msi.h, pc5/path.txt). */
    char winepath[PC5_PATH + 40], path[2048] = "";
    if (prefix && prefix[0] && strcmp(prefix, "default") && !pc5_game_winepath(root, prefix, winepath, sizeof(winepath))) {
        pc5_copy(path, sizeof(path), winepath);
        say(game, "PC5 Java launcher: %s in PATH", winepath);
    }
    redist_path(root, prefix, path, sizeof(path));
    if (path[0]) setenv("WINEPATH", path, 1);
    else unsetenv("WINEPATH");
    say(game, "PC5 game fps=%d overlay=%#x corner=%d vibration=%d light=%d triggers=%d trigger_rumble=%d "
        "trigger_resistance=%d tint=%06x frame_generation=%d memory_32bit=%s aspect_ratio=%s cpu_sharing=%d high_refresh=%d speakers=%d",
        game->settings.fps,
        pc5_game_overlay_lines(&game->settings), (int)game->settings.fps_corner, game->settings.vibration,
        (int)game->settings.light_bar, (int)game->settings.triggers, game->settings.trigger_rumble,
        game->settings.trigger_resistance, (unsigned)game->cover_tint, game->frame_generation, memory_4gb ? "4gb" : "auto",
        stretch ? "stretch" : "keep", cpu_sharing, game->settings.high_refresh,
        surround == 2 ? 8 : surround == 1 ? 6 : 2);
}

/* PC5's language from the app's folder: code as PC5 passed it. */
static void use_language(const char *app, const char *code)
{
    char path[PC5_PATH];
    if (code && code[0] && strcmp(code, "en") && !pc5_format(path, sizeof(path), "%s/pc5/lang", app))
        (void)pc5_i18n_use(path, code, NULL, NULL);
}

/* The fonts for the loading screen in the language in use: 0 when loaded. */
static int loading_fonts(const char *app, Pc5Fonts *fonts)
{
    char path[PC5_PATH];
    const Pc5Language *language;

    memset(fonts, 0, sizeof(*fonts));
    if (pc5_format(path, sizeof(path), "%s/pc5/fonts.bin", app) || pc5_fonts_load(fonts, path)) return -1;
    language = pc5_language(pc5_language_find(pc5_i18n_code()));
    if (language->cjk && !pc5_format(path, sizeof(path), "%s/pc5/fonts-%s.bin", app, pc5_i18n_code()))
        (void)pc5_fonts_extra(fonts, path);
    return 0;
}

int pc5_game_early_loading(const char *root, const char *app, const char *prefix, const char *title,
                           const char *language, const char *folder, uint32_t fade_ms,
                           void (*show)(const uint32_t *pixels, void *context), void *context,
                           void (*wait_ms)(uint32_t ms))
{
    Pc5Fonts fonts;
    Pc5Matrix *matrix;
    char own[PC5_PATH];
    int shown = -1;

    if (!app || !app[0] || !show) return -1;
    use_language(app, language);
    if (loading_fonts(app, &fonts)) memset(&fonts, 0, sizeof(fonts));
    if ((matrix = pc5_matrix_create(&fonts, title, 0))) {
        if (root && !pc5_loading_own(root, prefix, folder, title, prefix, own, sizeof(own)))
            (void)pc5_matrix_picture(matrix, own);
        /* from black: the matrix fades in over its first half second */
        for (uint32_t at = 0;; at += PC5_MATRIX_STEP_MS) {
            const uint32_t *pixels = pc5_matrix_step(matrix);
            if (!pixels) break;
            show(pixels, context);
            shown = 0;
            if (at >= fade_ms || !wait_ms) break;
            wait_ms(PC5_MATRIX_STEP_MS);
        }
        pc5_matrix_destroy(matrix);
    } else
        pc5_fonts_free(&fonts);
    pc5_i18n_end();   /* read again with the game's own loading screen */
    return shown;
}

void pc5_game_shown_before(Pc5GameSession *game)
{
    game->shown_before = 1;
}

void pc5_game_loading_setup(Pc5GameSession *game, const char *language, const char *folder)
{
    pc5_copy(game->language, sizeof(game->language), language ? language : "");
    pc5_copy(game->folder, sizeof(game->folder), folder ? folder : "");
}

const uint32_t *pc5_game_loading(Pc5GameSession *game, uint64_t now_ms)
{
    if (game->matrix_failed) return NULL;
    if (!game->matrix) {
        Pc5Fonts fonts;
        char own[PC5_PATH];
        if (game->language[0]) use_language(game->app, game->language);
        if (loading_fonts(game->app, &fonts)) say(game, "PC5 loading screen without its fonts: %s/pc5/fonts.bin", game->app);
        game->matrix = pc5_matrix_create(&fonts, game->title, (uint32_t)now_ms * 2654435761u);
        pc5_fonts_free(&fonts);
        if (!game->matrix) {
            game->matrix_failed = 1;
            say(game, "PC5 loading screen: out of memory");
            return NULL;
        }
        if (!pc5_loading_own(game->root, game->prefix, game->folder, game->title, game->prefix, own, sizeof(own)))
            say(game, "PC5 loading picture %s: %s", own, pc5_matrix_picture(game->matrix, own) ? "not readable" : "shown");
        if (game->shown_before) pc5_matrix_at_once(game->matrix);   /* it is already on the screen */
        game->matrix_at = 0;
    }
    if (game->matrix_at && now_ms < game->matrix_at) return NULL;
    game->matrix_at = now_ms + PC5_MATRIX_STEP_MS;
    return pc5_matrix_step(game->matrix);
}

void pc5_game_loading_done(Pc5GameSession *game)
{
    pc5_matrix_destroy(game->matrix);
    game->matrix = NULL;
    game->matrix_failed = 1;   /* not again */
}

/* A strength in percent as the triggers' 1-8: 25 % is 2, 100 % is 8. */
static uint8_t trigger_level(int percent)
{
    int level = percent * 8 / 100;
    return (uint8_t)(level < 1 ? 1 : level > 8 ? 8 : level);
}

static void set_trigger_pair(Pc5GameSession *game, int pad, const Pc5Trigger *l2, const Pc5Trigger *r2,
                             const char *why)
{
    if (!game->ops.triggers || pad < 0) return;
    int rc = game->ops.triggers(pad, l2, r2);
    if (game->triggers_sent++ && !rc) return;
    if (l2->mode == PC5_TRIGGER_GAME || r2->mode == PC5_TRIGGER_GAME)
        say(game, "PC5 triggers %s effects=%d,%d rc=%d", why, l2->effect, r2->effect, rc);
    else
        say(game, "PC5 triggers %s mode=%d rc=%d", why, (int)l2->mode, rc);
}

static void set_triggers(Pc5GameSession *game, int pad, const Pc5Trigger *both, const char *why)
{
    set_trigger_pair(game, pad, both, both, why);
}

void pc5_game_player_rumble(const Pc5GameSession *game, uint32_t left, uint32_t right, uint8_t *large,
                            uint8_t *small)
{
    *large = (uint8_t)((left >> 8) * (uint32_t)game->settings.vibration / 100);
    *small = (uint8_t)((right >> 8) * (uint32_t)game->settings.vibration / 100);
}

void pc5_game_rumble(Pc5GameSession *game, int pad, uint32_t left, uint32_t right, uint8_t *large, uint8_t *small)
{
    /* The motors at Vibration's strength (0 % is off). */
    *large = (uint8_t)((left >> 8) * (uint32_t)game->settings.vibration / 100);
    *small = (uint8_t)((right >> 8) * (uint32_t)game->settings.vibration / 100);
    if (!game->game_triggers &&
        (game->settings.triggers == PC5_TRIGGERS_RUMBLE || game->settings.triggers == PC5_TRIGGERS_BOTH)) {
        /* The stronger motor's speed as the triggers' amplitude: 1 up to 2, 4, 6
         * or 8 at the triggers' rumble strength (25 to 100 %), 0 when the game
         * stops. Then they are off again, or push back (both). */
        uint32_t speed = left > right ? left : right, most = (uint32_t)trigger_level(game->settings.trigger_rumble);
        uint8_t amplitude = (uint8_t)(speed ? 1 + (speed * (most - 1) + 32767) / 65535 : 0);
        if (amplitude != game->trigger_amplitude) {
            game->trigger_amplitude = amplitude;
            if (amplitude) {
                Pc5Trigger both = { .mode = PC5_TRIGGER_VIBRATION, .position = 2, .amplitude = amplitude,
                                     .frequency = (uint8_t)(left >= right ? 30 : 60) };
                game->resistance = 0;   /* not over the rumble */
                set_triggers(game, pad, &both, "rumble");
            } else if (game->settings.triggers == PC5_TRIGGERS_BOTH) {
                game->resistance = 1;   /* pc5_game_pad sends it */
            } else {
                Pc5Trigger off = { .mode = PC5_TRIGGER_OFF };
                set_triggers(game, pad, &off, "rumble");
            }
        }
    }
}

static int same_trigger(const Pc5Trigger *a, const Pc5Trigger *b)
{
    return a->mode == b->mode && a->effect == b->effect && !memcmp(a->data, b->data, sizeof(a->data));
}

void pc5_game_output(Pc5GameSession *game, int pad, const Pc5PadOutput *output, uint8_t *large, uint8_t *small)
{
    uint32_t light = output->light & 0xffffffu;

    if ((output->set & PC5_OUTPUT_LIGHT) && game->ops.light_bar && pad >= 0 &&
        (!game->game_light || light != game->light)) {
        int rc = game->ops.light_bar(pad, light);
        if (!game->game_light || rc) say(game, "PC5 light bar the game's %06x rc=%d", (unsigned)light, rc);
        game->game_light = 1;
        game->light = light;
        game->light_sent = 1;
    }
    if ((output->set & PC5_OUTPUT_TRIGGERS) &&
        (!game->game_triggers || !same_trigger(&output->trigger[0], &game->game_trigger[0]) ||
         !same_trigger(&output->trigger[1], &game->game_trigger[1]))) {
        game->game_triggers = 1;
        game->resistance = 0;
        game->trigger_amplitude = 0;
        game->game_trigger[0] = output->trigger[0];
        game->game_trigger[1] = output->trigger[1];
        set_trigger_pair(game, pad, &output->trigger[0], &output->trigger[1], "game");
    }
    pc5_game_rumble(game, pad, (uint32_t)output->large * 257u, (uint32_t)output->small * 257u, large, small);
}

void pc5_game_pad(Pc5GameSession *game, int pad, uint64_t now_ms, uint32_t screen)
{
    if (pad < 0) return;
    if (game->resistance && !game->game_triggers) {
        /* Pushing back from a third of the way in, 2 (25 %) to 8 (100 %) firm. */
        Pc5Trigger both = { .mode = PC5_TRIGGER_FEEDBACK, .position = 3,
                             .strength = trigger_level(game->settings.trigger_resistance) };
        game->resistance = 0;
        set_triggers(game, pad, &both, "resistance");
    }
    if (game->settings.light_bar == PC5_LIGHT_CONSOLE || !game->ops.light_bar || game->game_light) return;
    if (game->light_at && now_ms - game->light_at < LIGHT_EVERY_MS) return;
    game->light_at = now_ms;
    if (game->settings.light_bar == PC5_LIGHT_SCREEN && screen) game->light_target = bright(screen);
    else if (game->settings.light_bar == PC5_LIGHT_GAME) game->light_target = bright(game->cover_tint);
    if (!game->light_target && !game->light_sent) return;   /* nothing to show yet: the console's own */
    /* A quarter of the way each step: changes of picture fade in. */
    uint32_t next = 0;
    for (int shift = 0; shift < 24; shift += 8) {
        int now = (int)((game->light >> shift) & 255), target = (int)((game->light_target >> shift) & 255);
        int step = game->light_sent ? (target - now) / 4 : target - now;
        if (!step && now != target) step = target > now ? 1 : -1;
        next |= (uint32_t)(now + step) << shift;
    }
    if (game->light_sent && next == game->light) return;
    int rc = game->ops.light_bar(pad, next);
    if (!game->light_sent || rc) say(game, "PC5 light bar %06x rc=%d", (unsigned)next, rc);
    game->light = next;
    game->light_sent = 1;
}

unsigned pc5_game_overlay_lines(const Pc5Settings *settings)
{
    unsigned lines = (settings->fps ? PW_PC5_HUD_FPS : 0u) | (settings->frame_time ? PW_PC5_HUD_FRAME_TIME : 0u) |
                     (settings->cpu_usage ? PW_PC5_HUD_CPU : 0u) | (settings->gpu_usage ? PW_PC5_HUD_GPU : 0u) |
                     (settings->cpu_temperature ? PW_PC5_HUD_CPU_TEMP : 0u) |
                     (settings->soc_temperature ? PW_PC5_HUD_SOC_TEMP : 0u) | (settings->memory ? PW_PC5_HUD_MEMORY : 0u);
    /* Large: the same lines in glyphs twice the size, two at a time (pw_pc5_hud.h); Extra large:
     * every line at once, the whole overlay shown at twice its size by the display. Only with a
     * line to show. */
    if (!lines || settings->overlay_size == PC5_OVERLAY_NORMAL) return lines;
    return lines | (settings->overlay_size == PC5_OVERLAY_EXTRA_LARGE ? PW_PC5_HUD_DOUBLE : PW_PC5_HUD_LARGE);
}

/* A sensor's temperature, -1 when it can't be read; *rc what the console said. */
static int temperature(const Pc5GameSession *game, int sensor, int *rc)
{
    int celsius = -1;
    *rc = game->ops.temperature ? game->ops.temperature(sensor, &celsius) : -1;
    return !*rc && celsius > 0 && celsius < 150 ? celsius : -1;
}

/* The console's CPUs since the last sample, from its own counts (cpu_ticks): all their
 * work of all their time, and the busiest one's, in percent. 0 once it has two
 * samples; -1 when the console doesn't give them (or not yet the second). */
static int console_cpu(Pc5GameSession *game, int *percent, int *top)
{
    uint64_t busy[PC5_CPU_MOST], total[PC5_CPU_MOST], all_busy = 0, all_total = 0;
    unsigned count = game->ops.cpu_ticks ? game->ops.cpu_ticks(busy, total, PC5_CPU_MOST) : 0;
    int rc = -1;

    if (count > PC5_CPU_MOST) count = PC5_CPU_MOST;
    if (!count) {
        game->stats_cpus = 0;
        return -1;
    }
    if (count == game->stats_cpus) {
        int busiest = 0;
        for (unsigned i = 0; i < count; i++) {
            if (busy[i] < game->stats_cpu_busy[i] || total[i] < game->stats_cpu_total[i]) continue;
            uint64_t b = busy[i] - game->stats_cpu_busy[i], t = total[i] - game->stats_cpu_total[i];
            all_busy += b;
            all_total += t;
            if (t && b <= t && (int)((b * 100u + t / 2) / t) > busiest) busiest = (int)((b * 100u + t / 2) / t);
        }
        if (all_total) {
            *percent = (int)((all_busy * 100u + all_total / 2) / all_total);
            if (*percent > 100) *percent = 100;
            *top = busiest;
            rc = 0;
        }
    }
    memcpy(game->stats_cpu_busy, busy, count * sizeof(busy[0]));
    memcpy(game->stats_cpu_total, total, count * sizeof(total[0]));
    game->stats_cpus = count;
    return rc;
}

/* For the log, a line a second while the overlay is on: the three busiest threads of the
 * title's process (the game's, Wine's, the graphics' among them) since the last sample, each
 * as a share of one CPU, so a thread that holds the rest back shows near 100 even when the
 * console as a whole is mostly idle. */
static void busiest_threads(Pc5GameSession *game, uint64_t now_ms)
{
    static Pc5ThreadTime now[PC5_THREADS_MOST];
    unsigned count = game->ops.thread_times ? game->ops.thread_times(now, PC5_THREADS_MOST) : 0;
    if (count > PC5_THREADS_MOST) count = PC5_THREADS_MOST;
    if (count && game->stats_threads_at && now_ms > game->stats_threads_at) {
        uint64_t span_us = (now_ms - game->stats_threads_at) * 1000u;
        int top[3] = { -1, -1, -1 }, share[3] = { -1, -1, -1 };
        for (unsigned i = 0; i < count; i++) {
            const Pc5ThreadTime *was = NULL;
            for (unsigned j = 0; j < game->stats_thread_count && !was; j++)
                if (game->stats_threads[j].tid == now[i].tid) was = &game->stats_threads[j];
            if (!was || now[i].runtime_us < was->runtime_us) continue;
            int percent = (int)(((now[i].runtime_us - was->runtime_us) * 100u + span_us / 2) / span_us);
            for (int k = 0; k < 3; k++)
                if (percent > share[k]) {
                    for (int m = 2; m > k; m--) { share[m] = share[m - 1]; top[m] = top[m - 1]; }
                    share[k] = percent;
                    top[k] = (int)i;
                    break;
                }
        }
        char text[200] = "";
        size_t at = 0;
        for (int k = 0; k < 3 && top[k] >= 0 && at < sizeof(text); k++) {
            const Pc5ThreadTime *t = &now[top[k]];
            /* name/kernel thread id:share of one CPU%@the CPU it last ran on */
            int n = snprintf(text + at, sizeof(text) - at, "%s%s/%d:%d%%@%d", k ? " " : "", t->name[0] ? t->name : "?",
                             t->tid, share[k] > 100 ? 100 : share[k], t->cpu);
            if (n > 0) at += (size_t)n;
        }
        say(game, "PC5 threads=%u busiest %s", count, text);
    }
    memcpy(game->stats_threads, now, count * sizeof(now[0]));
    game->stats_thread_count = count;
    game->stats_threads_at = count ? now_ms : 0;
}

int pc5_game_overlay_sample(Pc5GameSession *game, uint64_t now_ms, PwPc5HudStats *stats)
{
    unsigned lines = pc5_game_overlay_lines(&game->settings);
    PwPc5HudStats s = { .lines = lines, .cpu_percent = -1, .cpu_top_percent = -1, .gpu_percent = -1,
                        .cpu_celsius = -1, .soc_celsius = -1 };
    int cpu_rc = 0, soc_rc = 0, memory_rc = 0, console = 0;
    unsigned cpus = 0;

    if (!lines || (game->stats_at && now_ms - game->stats_at < 1000)) return 0;
    /* The CPU: the console's own count of all its CPUs' work when it gives one, else the
     * game's process (its share of every CPU). The console's is kept for the log even
     * with the CPU line off. */
    if (game->ops.cpu_ticks) {
        (void)console_cpu(game, &s.cpu_percent, &s.cpu_top_percent);
        console = game->stats_cpus != 0;   /* the console gave its counts (two are needed for a number) */
        cpus = game->stats_cpus;
    }
    if (!console && (lines & PW_PC5_HUD_CPU) && game->ops.cpu_time_us) {
        uint64_t cpu = game->ops.cpu_time_us();
        cpus = game->ops.cpu_count ? game->ops.cpu_count() : 1;
        /* from the second time on: the CPU time since, of every CPU's */
        if (game->stats_at && now_ms > game->stats_at && cpu >= game->stats_cpu_us && cpus) {
            uint64_t whole = (now_ms - game->stats_at) * 1000u * cpus;
            uint64_t percent = ((cpu - game->stats_cpu_us) * 100u + whole / 2) / whole;
            s.cpu_percent = percent > 100 ? 100 : (int)percent;
        }
        game->stats_cpu_us = cpu;
    }
    if (lines & PW_PC5_HUD_CPU_TEMP) s.cpu_celsius = temperature(game, 0, &cpu_rc);
    if (lines & PW_PC5_HUD_SOC_TEMP) s.soc_celsius = temperature(game, 1, &soc_rc);
    if (lines & PW_PC5_HUD_MEMORY) {
        uint64_t used = 0, total = 0;
        memory_rc = game->ops.memory ? game->ops.memory(&used, &total) : -1;
        if (!memory_rc && total && used <= total) {
            s.memory_used = used;
            s.memory_total = total;
        }
    }
    /* once, for the log: what the console answered */
    if (!game->stats_logged) {
        say(game, "PC5 overlay lines=%#x cpus=%u cpu_from=%s cpu_temp=%d rc=%#x soc_temp=%d rc=%#x memory=%lluM/%lluM rc=%#x",
            lines, cpus, console ? "console" : "game", s.cpu_celsius, (unsigned)cpu_rc, s.soc_celsius, (unsigned)soc_rc,
            (unsigned long long)(s.memory_used >> 20), (unsigned long long)(s.memory_total >> 20),
            (unsigned)memory_rc);
        game->stats_logged = 1;
    }
    busiest_threads(game, now_ms);
    game->stats_at = now_ms;
    game->stats = s;
    *stats = s;
    return 1;
}

void pc5_game_hud(Pc5GameSession *game, int videoout, uint64_t frames, uint64_t now_ms)
{
    uint32_t image[PW_PC5_HUD_SIDE * PW_PC5_HUD_SIDE], x, y, height;
    unsigned lines = pc5_game_overlay_lines(&game->settings);
    PwPc5HudStats stats;

    if (!lines || !game->ops.cursor || videoout < 0) return;
    (void)pc5_game_overlay_sample(game, now_ms, &stats);
    if (!game->hud_second_at) {
        game->hud_second_at = now_ms;
        game->hud_second_frames = frames;
        return;
    }
    if (now_ms - game->hud_second_at < 1000) return;
    stats = game->stats;
    stats.lines = lines;
    uint64_t shown = frames - game->hud_second_frames, elapsed = now_ms - game->hud_second_at;
    stats.fps = (unsigned)((shown * 1000 + elapsed / 2) / elapsed);
    stats.frame_us = shown ? (uint32_t)(elapsed * 1000 / shown) : 0;
    stats.seconds = ++game->hud_seconds;
    game->hud_second_at = now_ms;
    game->hud_second_frames = frames;
    if (!(height = pw_pc5_hud_draw_stats(image, &stats))) {
        game->ops.cursor(videoout, NULL, 0, 0);   /* nothing to show: hidden */
        game->hud_magnified = -1;
        return;
    }
    int magnify = (lines & PW_PC5_HUD_DOUBLE) != 0;
    pw_pc5_hud_place_scaled((int)game->settings.fps_corner, 1920, 1080, height,
                            magnify && game->ops.cursor_magnify && !game->hud_magnify_refused ? 2 : 1, &x, &y);
    if (game->ops.cursor(videoout, image, x, y)) {
        if (!game->hud_shown) say(game, "PC5 overlay refused");
        game->hud_shown = 1;
        return;
    }
    game->hud_shown = 1;
    /* Extra large: the cursor at twice its size, asked as it changes (the console's answer
     * logged); refused, the overlay stays Large where it was placed for twice the size. */
    if (game->ops.cursor_magnify && magnify != game->hud_magnified && !(magnify && game->hud_magnify_refused)) {
        int rc = game->ops.cursor_magnify(videoout, magnify);
        say(game, "PC5 overlay magnified=%d rc=%d", magnify, rc);
        if (!rc) game->hud_magnified = magnify;
        else if (magnify) {
            game->hud_magnify_refused = 1;
            pw_pc5_hud_place_box((int)game->settings.fps_corner, 1920, 1080, height, &x, &y);
            game->ops.cursor(videoout, image, x, y);
        }
    }
}

/* ---- the on-screen keyboard -------------------------------------------------------------- */

enum { PAD_TOUCHPAD = 0x100000u };

uint32_t pc5_game_keyboard(Pc5GameSession *game, uint32_t held, uint32_t pressed, uint32_t released,
                           unsigned fingers)
{
    /* The touchpad is the game's again from the tick after the click's release; the
     * release is PC5's too. */
    uint32_t hidden = game->keyboard_taken;
    if (released & PAD_TOUCHPAD || !(held & PAD_TOUCHPAD)) game->keyboard_taken = 0;

    if (game->keyboard_open) {
        int state = game->ops.keyboard_state ? game->ops.keyboard_state() : -1;
        if (state == 1) return hidden;
        int accepted = game->ops.keyboard_close ? game->ops.keyboard_close() : -1;
        game->keyboard_open = 0;
        if (state == 2 && accepted == 1) {
            uint32_t length = 0;
            while (length < PC5_KEYBOARD_TEXT - 1 && game->keyboard_text[length]) length++;
            game->keyboard_length = length;
            game->keyboard_typed = 0;
            say(game, "PC5 keyboard text: %u characters to type", (unsigned)length);
        } else {
            say(game, "PC5 keyboard closed: %s (state %d, %d)",
                state == 2 && accepted == 0 ? "cancelled" : "failed", state, accepted);
        }
        return hidden;
    }
    if ((pressed & PAD_TOUCHPAD) && fingers >= 2 && game->ops.keyboard_open && !game->two_finger_key) {
        /* Taken from the game even when the keyboard can't open, so the game never sees it. */
        game->keyboard_taken = PAD_TOUCHPAD;
        hidden |= PAD_TOUCHPAD;
        memset(game->keyboard_text, 0, sizeof(game->keyboard_text));
        game->keyboard_length = game->keyboard_typed = 0;   /* text not typed yet goes */
        int rc = game->ops.keyboard_open(game->keyboard_text, PC5_KEYBOARD_TEXT);
        game->keyboard_open = rc == 0;
        say(game, "PC5 keyboard %s rc=%#x", rc ? "refused" : "open", (unsigned)rc);
    }
    return hidden;
}

const uint16_t *pc5_game_keyboard_text(const Pc5GameSession *game, size_t *count)
{
    *count = game->keyboard_typed < game->keyboard_length ? game->keyboard_length - game->keyboard_typed : 0;
    return game->keyboard_text + game->keyboard_typed;
}

void pc5_game_keyboard_typed(Pc5GameSession *game, size_t count)
{
    size_t left = game->keyboard_length - game->keyboard_typed;
    game->keyboard_typed += (uint32_t)(count < left ? count : left);
    if (game->keyboard_typed == game->keyboard_length && game->keyboard_length) {
        say(game, "PC5 keyboard typed %u characters", (unsigned)game->keyboard_length);
        game->keyboard_length = game->keyboard_typed = 0;
    }
}

void pc5_game_end(Pc5GameSession *game, int pad)
{
    Pc5Trigger off = { .mode = PC5_TRIGGER_OFF };
    if (game->keyboard_open && game->ops.keyboard_close) game->ops.keyboard_close();
    game->keyboard_open = 0;
    if (pad >= 0 && game->triggers_sent && game->ops.triggers) game->ops.triggers(pad, &off, &off);
    if (pad >= 0 && game->light_sent && game->ops.light_bar_reset) game->ops.light_bar_reset(pad);
    pc5_matrix_destroy(game->matrix);
    game->matrix = NULL;
}
