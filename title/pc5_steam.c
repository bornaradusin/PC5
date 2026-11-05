/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: Steam's library swapped for gbe_fork's, and Steam's DRM wrapper recognised (pc5_steam.h). */
#include "pc5_steam.h"
#include "pc5_identity.h"
#include "pc5_pe.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

enum { FIND_DEPTH = 6, FIND_ENTRIES = 40000 };

static void find_in(const char *dir, const char *relative, int depth, int *visited, char found[][PC5_PATH], int max,
                    int *count)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    if (!d) return;
    while (*count < max && (e = readdir(d)) && ++*visited < FIND_ENTRIES) {
        char path[PC5_PATH], rel[PC5_PATH];
        struct stat st;
        if (e->d_name[0] == '.' || !strcasecmp(e->d_name, "steam_settings")) continue;
        if (pc5_join(path, sizeof(path), dir, e->d_name) ||
            pc5_format(rel, sizeof(rel), "%s%s%s", relative, relative[0] ? "/" : "", e->d_name) || lstat(path, &st))
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < FIND_DEPTH) find_in(path, rel, depth + 1, visited, found, max, count);
        } else if (S_ISREG(st.st_mode) &&
                   (!strcasecmp(e->d_name, "steam_api.dll") || !strcasecmp(e->d_name, "steam_api64.dll"))) {
            pc5_copy(found[(*count)++], PC5_PATH, rel);
        }
    }
    closedir(d);
}

int pc5_steam_find(const char *root, const char *id, const char *game_dir, const char *program,
                   char found[][PC5_PATH], int max)
{
    char note[PC5_PATH], stamp[96] = "program - -";
    struct stat st;
    Pc5Buf text = { 0 };
    int count = 0, visited = 0;

    if (program && !stat(program, &st)) snprintf(stamp, sizeof(stamp), "program %lld %lld", (long long)st.st_size, (long long)st.st_mtime);
    if (pc5_format(note, sizeof(note), "%s/prefixes/%s/pc5/steam.txt", root, id)) return 0;
    /* What was found before, while the program is the same. */
    if (pc5_is_file(note) && !pc5_read_file(note, &text, 65536) && !pc5_buf_terminate(&text)) {
        char *line = (char *)text.data, *next = strchr(line, '\n');
        if (next) *next++ = 0;
        if (!strcmp(line, stamp)) {
            /* ... and while every library it named is still there (one deleted since: looked for again). */
            int gone = 0;
            for (line = next; line && *line && count < max; line = next) {
                char full[PC5_PATH];
                next = strchr(line, '\n');
                if (next) *next++ = 0;
                if (!line[0]) continue;
                if (pc5_join(full, sizeof(full), game_dir, line) || !pc5_is_file(full)) { gone = 1; break; }
                pc5_copy(found[count++], PC5_PATH, line);
            }
            if (!gone) {
                pc5_buf_free(&text);
                return count;
            }
            count = 0;
        }
    }
    pc5_buf_free(&text);
    find_in(game_dir, "", 0, &visited, found, max, &count);
    Pc5Buf out = { 0 };
    pc5_buf_printf(&out, "%s\n", stamp);
    for (int i = 0; i < count; i++) pc5_buf_printf(&out, "%s\n", found[i]);
    char folder[PC5_PATH];
    if (!pc5_format(folder, sizeof(folder), "%s/prefixes/%s/pc5", root, id) && !pc5_mkdirs(folder) && out.data)
        pc5_write_file(note, out.data, out.len);
    pc5_buf_free(&out);
    return count;
}

int pc5_steam_stub(const char *program)
{
    return program && pc5_pe_entry_in(program, ".bind");
}

/* ---- the interfaces Valve's library has (steam_interfaces.txt) ------------------------------- */
static const char *const interface_names[] = {
    "STEAMAPPS_INTERFACE_VERSION", "SteamApps", "STEAMAPPLIST_INTERFACE_VERSION", "STEAMAPPTICKET_INTERFACE_VERSION",
    "SteamClient", "STEAMCONTROLLER_INTERFACE_VERSION", "SteamController", "SteamFriends", "SteamGameServerStats",
    "SteamGameCoordinator", "SteamGameServer", "STEAMHTMLSURFACE_INTERFACE_VERSION_", "STEAMHTTP_INTERFACE_VERSION",
    "SteamInput", "STEAMINVENTORY_INTERFACE_V", "SteamMatchMakingServers", "SteamMatchMaking", "SteamMatchGameSearch",
    "SteamParties", "STEAMMUSIC_INTERFACE_VERSION", "STEAMMUSICREMOTE_INTERFACE_VERSION", "SteamNetworkingMessages",
    "SteamNetworkingSockets", "SteamNetworkingUtils", "SteamNetworking", "STEAMPARENTALSETTINGS_INTERFACE_VERSION",
    "STEAMREMOTEPLAY_INTERFACE_VERSION", "STEAMREMOTESTORAGE_INTERFACE_VERSION", "STEAMSCREENSHOTS_INTERFACE_VERSION",
    "STEAMTIMELINE_INTERFACE_V", "STEAMUGC_INTERFACE_VERSION", "SteamUser", "STEAMUSERSTATS_INTERFACE_VERSION",
    "SteamUtils", "STEAMVIDEO_INTERFACE_V", "STEAMUNIFIEDMESSAGES_INTERFACE_VERSION", "SteamMasterServerUpdater",
};

static int listed(const Pc5Buf *out, const char *line, size_t n)
{
    if (!out->data) return 0;
    for (const char *at = (const char *)out->data; at < (const char *)out->data + out->len;) {
        const char *end = memchr(at, '\n', (size_t)((const char *)out->data + out->len - at));
        size_t len = end ? (size_t)(end - at) : strlen(at);
        if (len == n && !memcmp(at, line, n)) return 1;
        if (!end) break;
        at = end + 1;
    }
    return 0;
}

int pc5_steam_interfaces(const uint8_t *data, size_t size, Pc5Buf *out)
{
    for (size_t p = 0; p < sizeof(interface_names) / sizeof(interface_names[0]); p++) {
        const char *name = interface_names[p];
        size_t n = strlen(name);
        int digits_needed = strcmp(name, "STEAMCONTROLLER_INTERFACE_VERSION") != 0;
        char matches[16][64];
        int count = 0, has_017 = 0;
        for (size_t i = 0; i + n <= size;) {
            if (data[i] != (uint8_t)name[0] || memcmp(data + i, name, n)) {
                i++;
                continue;
            }
            size_t end = i + n;
            if (digits_needed)
                while (end < size && data[end] >= '0' && data[end] <= '9' && end - i < 60) end++;
            if (digits_needed && end == i + n) {
                i++;
                continue;
            }
            if (count < 16) {
                memcpy(matches[count], data + i, end - i);
                matches[count][end - i] = 0;
                has_017 |= !strcmp(matches[count], "SteamClient017");
                count++;
            }
            i = end;
        }
        /* Newer SDKs keep only SteamClient() among the old exports, which returns SteamClient017. */
        int only_017 = !strcmp(name, "SteamClient") && count > 1 && has_017;
        for (int m = 0; m < count; m++) {
            if (only_017 && strcmp(matches[m], "SteamClient017")) continue;
            if (!listed(out, matches[m], strlen(matches[m])) && pc5_buf_printf(out, "%s\n", matches[m])) return -1;
        }
    }
    return 0;
}

/* ---- the swap ------------------------------------------------------------------------------ */
static int copy_file(const char *from, const char *to)
{
    char part[PC5_PATH];
    uint8_t *chunk = pc5_alloc(1 << 16);
    int in = open(from, O_RDONLY), out = -1, ok = chunk && in >= 0;
    ssize_t got = 0;

    if (ok && pc5_format(part, sizeof(part), "%s.part", to)) ok = 0;
    if (ok) ok = (out = open(part, O_WRONLY | O_CREAT | O_TRUNC, 0644)) >= 0;
    while (ok && (got = read(in, chunk, 1 << 16)) > 0) ok = write(out, chunk, (size_t)got) == got;
    if (got < 0) ok = 0;
    if (out >= 0 && close(out)) ok = 0;
    if (in >= 0) close(in);
    pc5_free(chunk);
    if (ok && rename(part, to)) ok = 0;
    if (!ok && out >= 0) unlink(part);
    return ok ? 0 : -1;
}

/* 1 when a and b hold the same bytes at their start and end and are as long (the emulator). */
static int same_file(const char *a, const char *b)
{
    struct stat sa, sb;
    if (stat(a, &sa) || stat(b, &sb) || sa.st_size != sb.st_size) return 0;
    int fa = open(a, O_RDONLY), fb = open(b, O_RDONLY), same = fa >= 0 && fb >= 0;
    uint8_t *x = pc5_alloc(65536), *y = pc5_alloc(65536);
    same = same && x && y;
    off_t at[2] = { 0, sa.st_size > 65536 ? sa.st_size - 65536 : 0 };
    for (int k = 0; same && k < 2; k++) {
        ssize_t n = pread(fa, x, 65536, at[k]), m = pread(fb, y, 65536, at[k]);
        same = n >= 0 && n == m && !memcmp(x, y, (size_t)n);
    }
    if (fa >= 0) close(fa);
    if (fb >= 0) close(fb);
    pc5_free(x);
    pc5_free(y);
    return same;
}

/* The digits of a steam_appid.txt's first line, into out ("" when there are none). */
static void app_id_from(const char *file, char *out, size_t size)
{
    Pc5Buf text = { 0 };
    size_t n = 0;
    out[0] = 0;
    if (!pc5_is_file(file) || pc5_read_file(file, &text, 4096) || pc5_buf_terminate(&text)) {
        pc5_buf_free(&text);
        return;
    }
    const char *at = (const char *)text.data;
    while (*at == ' ' || *at == '\t' || *at == '\xef' || *at == '\xbb' || *at == '\xbf') at++;
    while (at[n] >= '0' && at[n] <= '9' && n + 1 < size && n < 10) n++;
    memcpy(out, at, n);
    out[n] = 0;
    pc5_buf_free(&text);
}

static int write_if_missing(const char *dir, const char *name, const void *data, size_t size)
{
    char path[PC5_PATH];
    if (pc5_join(path, sizeof(path), dir, name)) return -1;
    if (pc5_exists(path)) return 0;
    return pc5_write_file(path, data, size);
}

/* The app ID PC5 knows for the game when its folder has no steam_appid.txt: the one the player
 * gave on the web page, or the one Steam's own files in the folder name (pc5_identity.h, as for
 * its cover). 0: none. Where it came from into source. */
static uint32_t known_app_id(const char *root, const char *id, const char *game_dir, char *source, size_t size)
{
    Pc5Identity ident;
    memset(&ident, 0, sizeof(ident));
    if ((pc5_identity_read(root, id, &ident) || !ident.steam_appid) &&
        (!game_dir || pc5_identity_scan(game_dir, &ident) || !ident.steam_appid))
        return 0;
    if (source && size) pc5_copy(source, size, ident.source[0] ? ident.source : "PC5's look at the folder");
    return ident.steam_appid;
}

/* The game's note of the app ID PC5 gave the emulator from known_app_id (prefixes/<id>/pc5/steam-appid.txt):
 * a steam_settings/steam_appid.txt that still says it is PC5's to change when the player gives
 * another; one that says anything else is the player's own and stays. */
static int app_id_note(const char *root, const char *id, char *path, size_t size)
{
    return pc5_format(path, size, "%s/prefixes/%s/pc5/steam-appid.txt", root, id);
}

/* steam_settings/ beside the emulator in dll's folder; valve: Valve's library, for its interfaces.
 * known: the app ID for a game whose folder has none (known_app_id), 0 for none; note: where PC5
 * keeps what it gave (app_id_note). */
static int write_settings(const char *dll, const char *valve, const char *game_dir, const char *program, uint32_t known,
                          const char *note)
{
    char dir[PC5_PATH], settings[PC5_PATH], file[PC5_PATH], app_id[16] = "";
    static const char user[] = "# PC5: who plays, for the Steam emulator (gbe_fork)\n[user::general]\naccount_name=Player\n"
                               "language=english\n";
    static const char main_ini[] = "# PC5: no Steam network on the console\n[main::connectivity]\ndisable_lan_only=0\n"
                                   "offline=1\ndisable_networking=1\n";
    static const char app_ini[] = "# PC5: no DLC unless the game has it\n[app::dlcs]\nunlock_all=0\n";

    pc5_copy(dir, sizeof(dir), dll);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = 0;
    if (pc5_join(settings, sizeof(settings), dir, "steam_settings") || pc5_mkdirs(settings)) return -1;
    /* The game's app ID: beside the library, beside the program, or at the top of its folder. */
    const char *places[3] = { dir, NULL, game_dir };
    char program_dir[PC5_PATH] = "";
    if (program) {
        pc5_copy(program_dir, sizeof(program_dir), program);
        char *s = strrchr(program_dir, '/');
        if (s) *s = 0;
        places[1] = program_dir;
    }
    for (int i = 0; i < 3 && !app_id[0]; i++)
        if (places[i] && !pc5_join(file, sizeof(file), places[i], "steam_appid.txt")) app_id_from(file, app_id, sizeof(app_id));
    if (app_id[0]) {
        char line[24];
        int n = snprintf(line, sizeof(line), "%s\n", app_id);
        if (write_if_missing(settings, "steam_appid.txt", line, (size_t)n)) return -1;
    } else if (known && note) {
        /* None in the folder (a copy from the Steam app has none): the one PC5 knows, written when
         * there is none yet or the one there is PC5's from before (the player gave another since). */
        char line[24], here[16] = "", before[16] = "";
        int n = snprintf(line, sizeof(line), "%u\n", (unsigned)known);
        if (pc5_join(file, sizeof(file), settings, "steam_appid.txt")) return -1;
        app_id_from(file, here, sizeof(here));
        app_id_from(note, before, sizeof(before));
        if (!pc5_exists(file) || (here[0] && !strcmp(here, before) && strtoul(here, NULL, 10) != known)) {
            if (pc5_write_file(file, line, (size_t)n)) return -1;
        }
        app_id_from(file, here, sizeof(here));
        if (strtoul(here, NULL, 10) == known) pc5_write_file(note, line, (size_t)n);
    }
    if (write_if_missing(settings, "configs.user.ini", user, sizeof(user) - 1) ||
        write_if_missing(settings, "configs.main.ini", main_ini, sizeof(main_ini) - 1) ||
        write_if_missing(settings, "configs.app.ini", app_ini, sizeof(app_ini) - 1))
        return -1;
    Pc5Buf original = { 0 }, interfaces = { 0 };
    int status = 0;
    if (!pc5_read_file(valve, &original, 64u << 20) && !pc5_steam_interfaces(original.data, original.len, &interfaces) &&
        interfaces.len)
        status = write_if_missing(settings, "steam_interfaces.txt", interfaces.data, interfaces.len);
    pc5_buf_free(&original);
    pc5_buf_free(&interfaces);
    return status;
}

/* The files another Steam emulator or fix keeps beside the game's Steam library, in dir. */
static int emulator_marker_in(const char *dir)
{
    static const char *const markers[] = { "steam_emu.ini", "OnlineFix.ini", "OnlineFix64.dll", "OnlineFix.dll",
                                           "ColdClientLoader.ini", "SmartSteamEmu.ini", "SmartSteamEmu", "steam_api64_o.dll",
                                           "steam_api_o.dll", "cream_api.ini", "ALI213.ini", "valve.ini", "CPY.ini" };
    char path[PC5_PATH];
    for (size_t i = 0; i < sizeof(markers) / sizeof(markers[0]); i++)
        if (!pc5_join(path, sizeof(path), dir, markers[i]) && pc5_exists(path)) return 1;
    return 0;
}

/* 1 when the game already has a Steam emulator or fix of its own: a Steam library that is
 * neither Valve's nor PC5's, or another emulator's files beside one. Then it is left as it is,
 * its settings and saves with it (Auto changes nothing for it). */
static int own_emulator(const char *game_dir, char found[][PC5_PATH], int count)
{
    if (emulator_marker_in(game_dir)) return 1;
    for (int i = 0; i < count; i++) {
        char dll[PC5_PATH], valve[PC5_PATH], dir[PC5_PATH];
        if (pc5_join(dll, sizeof(dll), game_dir, found[i]) || pc5_format(valve, sizeof(valve), "%s.pc5-valve", dll) ||
            !pc5_is_file(dll))
            continue;
        if (!pc5_pe_signed_by(dll, "Valve") && !pc5_is_file(valve)) return 1;
        pc5_copy(dir, sizeof(dir), dll);
        char *slash = strrchr(dir, '/');
        if (slash) *slash = 0;
        if (emulator_marker_in(dir)) return 1;
    }
    return 0;
}

int pc5_steam_prepare(const char *root, const char *id, const char *game_dir, const char *program, const char *app,
                      int on, char *error, size_t size)
{
    char (*found)[PC5_PATH] = pc5_alloc(sizeof(*found) * PC5_STEAM_DLLS);
    int count, changed = 0, status = 0;

    error[0] = 0;
    if (!found) return -1;
    count = pc5_steam_find(root, id, game_dir, program, found, PC5_STEAM_DLLS);
    /* A game with an emulator of its own keeps it: PC5 puts none beside it (SuperLeiyan: Auto
     * gave such a game another user and other settings, and its saves were lost to it). */
    if (on && own_emulator(game_dir, found, count)) {
        pc5_free(found);
        return 0;
    }
    char note[PC5_PATH];
    uint32_t known = on && count ? known_app_id(root, id, game_dir, NULL, 0) : 0;
    if (app_id_note(root, id, note, sizeof(note))) known = 0;
    for (int i = 0; i < count && !status; i++) {
        char dll[PC5_PATH], valve[PC5_PATH], emulator[PC5_PATH];
        const char *name = strrchr(found[i], '/');
        name = name ? name + 1 : found[i];
        int bits64 = !strcasecmp(name, "steam_api64.dll");
        if (pc5_join(dll, sizeof(dll), game_dir, found[i]) || pc5_format(valve, sizeof(valve), "%s.pc5-valve", dll) ||
            pc5_format(emulator, sizeof(emulator), "%s/pc5/steam/%s", app, bits64 ? "steam_api64.dll" : "steam_api.dll"))
            continue;
        /* Valve's (signed by Valve: a new one after an update too), or PC5's emulator with Valve's
         * kept beside it (an earlier PC5's emulator among them), or another one, left alone. */
        int valve_signed = pc5_pe_signed_by(dll, "Valve"), valve_kept = pc5_is_file(valve);
        if (!on) {
            if (!valve_signed && valve_kept) {
                if (rename(valve, dll)) {
                    snprintf(error, size, "PC5 can't put Steam's %s back.", name);
                    status = -1;
                } else {
                    changed++;
                }
            }
            continue;
        }
        if ((valve_signed || valve_kept) && !pc5_is_file(emulator)) {
            pc5_copy(error, size, "This PC5 has no Steam emulator (pc5/steam).");
            status = -1;
            break;
        }
        if (valve_signed) {
            if (copy_file(dll, valve) || copy_file(emulator, dll)) {
                snprintf(error, size, "PC5 can't put the Steam emulator in place of %s.", name);
                status = -1;
                continue;
            }
            changed++;
        } else if (!valve_kept) {
            continue;
        } else if (!same_file(dll, emulator)) {   /* an earlier PC5's emulator */
            if (copy_file(emulator, dll)) {
                snprintf(error, size, "PC5 can't update the Steam emulator in place of %s.", name);
                status = -1;
                continue;
            }
            changed++;
        }
        if (write_settings(dll, valve, game_dir, program, known, note)) {
            snprintf(error, size, "PC5 can't write the Steam emulator's settings beside %s.", name);
            status = -1;
        }
    }
    pc5_free(found);
    return status ? -1 : changed;
}

/* ---- what a report says about Steam (pc5_steam_check) ------------------------------------------ */

enum { CHECK_DEPTH = 4, CHECK_ENTRIES = 20000, CHECK_SHOWN = 8 };

/* Files other Steam stand-ins and fixes keep in a game's folder; and Steam's own install file. */
static const char *const stand_in_files[] = {
    "steam_emu.ini", "OnlineFix.ini", "OnlineFix64.dll", "OnlineFix.dll", "ColdClientLoader.ini", "coldloader.dll",
    "SmartSteamEmu.ini", "steam_api64_o.dll", "steam_api_o.dll", "cream_api.ini", "ALI213.ini", "valve.ini", "CPY.ini",
    "CODEX.ini", "steamclient.dll", "steamclient64.dll",
};

typedef struct CheckWalk {
    Pc5Buf *stand_ins, *steam_files;
    int stand_in_count, steam_count, visited;
} CheckWalk;

static void check_walk(CheckWalk *w, const char *dir, const char *relative, int depth)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    if (!d) return;
    while ((e = readdir(d)) && ++w->visited < CHECK_ENTRIES) {
        char path[PC5_PATH], rel[PC5_PATH];
        struct stat st;
        if (e->d_name[0] == '.' || !strcasecmp(e->d_name, "steam_settings")) continue;
        if (pc5_join(path, sizeof(path), dir, e->d_name) ||
            pc5_format(rel, sizeof(rel), "%s%s%s", relative, relative[0] ? "/" : "", e->d_name) || lstat(path, &st))
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < CHECK_DEPTH) check_walk(w, path, rel, depth + 1);
            continue;
        }
        if (!S_ISREG(st.st_mode)) continue;
        for (size_t i = 0; i < sizeof(stand_in_files) / sizeof(stand_in_files[0]); i++) {
            if (strcasecmp(e->d_name, stand_in_files[i])) continue;
            if (w->stand_in_count++ < CHECK_SHOWN)
                pc5_buf_printf(w->stand_ins, "%s%s", w->stand_in_count > 1 ? ", " : "", rel);
            break;
        }
        if (!strcasecmp(e->d_name, "installscript.vdf") ||
            (!strncasecmp(e->d_name, "appmanifest_", 12) && pc5_ends_with_ci(e->d_name, ".acf"))) {
            if (w->steam_count++ < CHECK_SHOWN)
                pc5_buf_printf(w->steam_files, "%s%s", w->steam_count > 1 ? ", " : "", rel);
        }
    }
    closedir(d);
}

/* The DLC lines and unlock_all of a configs.app.ini's [app::dlcs]. */
static void dlc_listed(const char *ini, int *listed_count, int *unlock_all)
{
    Pc5Buf text = { 0 };
    int in_dlcs = 0;
    *listed_count = 0;
    *unlock_all = -1;
    if (!pc5_is_file(ini) || pc5_read_file(ini, &text, 256 * 1024) || pc5_buf_terminate(&text)) {
        pc5_buf_free(&text);
        return;
    }
    for (char *line = (char *)text.data, *next; line && *line; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        while (*line == ' ' || *line == '\t') line++;
        if (*line == '[') { in_dlcs = !strncasecmp(line, "[app::dlcs]", 11); continue; }
        if (!in_dlcs || *line == '#' || *line == ';') continue;
        if (!strncasecmp(line, "unlock_all=", 11)) *unlock_all = atoi(line + 11);
        else if (*line >= '0' && *line <= '9' && strchr(line, '=')) ++*listed_count;
    }
    pc5_buf_free(&text);
}

int pc5_steam_check(const char *root, const char *id, const char *game_dir, const char *program, int on, int stub,
                    Pc5Buf *out)
{
    char (*found)[PC5_PATH] = pc5_alloc(sizeof(*found) * PC5_STEAM_DLLS);
    Pc5Buf stand_ins = { 0 }, steam_files = { 0 };
    int failed = 0;

    if (!found) return -1;
    int count = pc5_steam_find(root, id, game_dir, program, found, PC5_STEAM_DLLS);
    CheckWalk w = { &stand_ins, &steam_files, 0, 0, 0 };
    check_walk(&w, game_dir, "", 0);

    failed |= pc5_buf_printf(out, "  Steam emulator setting: %s\n", on ? "Auto" : "Off");
    failed |= pc5_buf_printf(out, "  Steam's DRM (SteamStub) in the program: %s\n", stub ? "yes, so PC5 didn't start it" : "no");
    if (!count) failed |= pc5_buf_puts(out, "  Steam's library (steam_api.dll, steam_api64.dll): none in the game's folder\n");
    char app_id[16] = "", app_id_from_where[PC5_PATH + 64] = "";
    for (int i = 0; i < count; i++) {
        char dll[PC5_PATH], valve[PC5_PATH], dir[PC5_PATH], file[PC5_PATH];
        if (pc5_join(dll, sizeof(dll), game_dir, found[i]) || pc5_format(valve, sizeof(valve), "%s.pc5-valve", dll)) continue;
        int signed_valve = pc5_is_file(dll) && pc5_pe_signed_by(dll, "Valve"), kept = pc5_is_file(valve);
        const char *whose = !pc5_is_file(dll) ? "missing"
                            : signed_valve    ? (on ? "Valve's: the emulator isn't in its place (another stand-in in the folder, or PC5 couldn't put it there)"
                                                    : "Valve's (emulator Off)")
                            : kept            ? "PC5's Steam emulator, Valve's kept beside it"
                                              : "not Valve's and not PC5's: another Steam stand-in";
        failed |= pc5_buf_printf(out, "  %s: %s\n", found[i], whose);
        /* The app ID as the emulator finds it: its steam_settings first, then beside it. */
        pc5_copy(dir, sizeof(dir), dll);
        char *slash = strrchr(dir, '/');
        if (slash) *slash = 0;
        if (!app_id[0] && !pc5_format(file, sizeof(file), "%s/steam_settings/steam_appid.txt", dir)) {
            app_id_from(file, app_id, sizeof(app_id));
            if (app_id[0]) snprintf(app_id_from_where, sizeof(app_id_from_where), "%s's steam_settings", found[i]);
        }
        if (!app_id[0] && !pc5_join(file, sizeof(file), dir, "steam_appid.txt")) {
            app_id_from(file, app_id, sizeof(app_id));
            if (app_id[0]) snprintf(app_id_from_where, sizeof(app_id_from_where), "steam_appid.txt beside %s", found[i]);
        }
        int listed_count, unlock_all;
        if (!pc5_format(file, sizeof(file), "%s/steam_settings/configs.app.ini", dir)) {
            dlc_listed(file, &listed_count, &unlock_all);
            if (unlock_all >= 0 || listed_count)
                failed |= pc5_buf_printf(out, "  DLC for %s: %d listed, unlock_all=%d\n", found[i], listed_count, unlock_all);
        }
    }
    if (count) {
        if (!app_id[0] && program) {
            char dir[PC5_PATH], file[PC5_PATH];
            pc5_copy(dir, sizeof(dir), program);
            char *slash = strrchr(dir, '/');
            if (slash) *slash = 0;
            if (!pc5_join(file, sizeof(file), dir, "steam_appid.txt")) app_id_from(file, app_id, sizeof(app_id));
            if (app_id[0]) pc5_copy(app_id_from_where, sizeof(app_id_from_where), "steam_appid.txt beside the program");
        }
        if (!app_id[0] && game_dir) {
            char file[PC5_PATH];
            if (!pc5_join(file, sizeof(file), game_dir, "steam_appid.txt")) app_id_from(file, app_id, sizeof(app_id));
            if (app_id[0]) pc5_copy(app_id_from_where, sizeof(app_id_from_where), "steam_appid.txt at the top of the game's folder");
        }
        char note[PC5_PATH], given[16] = "", source[64] = "";
        if (app_id[0] && !app_id_note(root, id, note, sizeof(note))) app_id_from(note, given, sizeof(given));
        if (app_id[0] && given[0] && !strcmp(given, app_id) && known_app_id(root, id, game_dir, source, sizeof(source)))
            failed |= pc5_buf_printf(out, "  App ID the emulator has: %s (PC5 gave it: no steam_appid.txt in the game's "
                                          "folder; found in %s)\n", app_id, source);
        else if (app_id[0]) failed |= pc5_buf_printf(out, "  App ID the emulator has: %s (%s)\n", app_id, app_id_from_where);
        else failed |= pc5_buf_puts(out, "  App ID the emulator has: none (no steam_appid.txt in the game's folder)\n");
    }
    failed |= pc5_buf_printf(out, "  Other Steam stand-ins' or fixes' files: %s%s\n", w.stand_in_count ? (char *)stand_ins.data : "none",
                             w.stand_in_count > CHECK_SHOWN ? ", and more" : "");
    failed |= pc5_buf_printf(out, "  Steam's own install files: %s%s\n", w.steam_count ? (char *)steam_files.data : "none",
                             w.steam_count > CHECK_SHOWN ? ", and more" : "");
    pc5_buf_free(&stand_ins);
    pc5_buf_free(&steam_files);
    pc5_free(found);
    return failed ? -1 : 0;
}

int pc5_steam_check_keep(const char *root, const char *id, const char *game_dir, const char *program, int on, int stub,
                         char *summary, size_t size)
{
    char path[PC5_PATH];
    Pc5Buf text = { 0 };
    if (summary && size) summary[0] = 0;
    if (pc5_steam_check(root, id, game_dir, program, on, stub, &text) || pc5_buf_terminate(&text)) {
        pc5_buf_free(&text);
        return -1;
    }
    /* The summary: the lines joined with " | ", their leading spaces left out. */
    if (summary && size) {
        size_t n = 0;
        for (const char *p = (const char *)text.data; *p && n + 4 < size; p++) {
            if (*p == '\n') {
                if (p[1]) { summary[n++] = ' '; summary[n++] = '|'; summary[n++] = ' '; }
                while (p[1] == ' ') p++;
            } else if (!(n == 0 && *p == ' ')) {
                summary[n++] = *p;
            }
        }
        summary[n] = 0;
    }
    int status = pc5_format(path, sizeof(path), "%s/prefixes/%s/pc5/%s", root, id, PC5_STEAM_CHECK_FILE) ||
                 pc5_write_file(path, text.data, text.len);
    pc5_buf_free(&text);
    return status ? -1 : 0;
}
