/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5 in a game's process (pc5_game.h). */
#include "pc5_game.h"
#include "pc5_controls.h"
#include "pc5_pe.h"
#include "pw_pc5_hud.h"

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

/* ---- the runtime's copy ------------------------------------------------------------------- */

static uint64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}

int pc5_runtime_begin(Pc5Runtime *runtime, const char *root, const char *app, const char *program)
{
    char version[32], done[PC5_PATH], list[PC5_PATH];
    Pc5PeInfo pe;

    memset(runtime, 0, sizeof(*runtime));
    if (!program || pc5_pe_read(program, &pe) || !(pe.imports & PC5_IMPORT_DOTNET)) return 0;
    mono_version(app, version, sizeof(version));
    if (!version[0]) return 0;
    if (pc5_format(runtime->from, sizeof(runtime->from), "%s/pc5/mono", app) ||
        pc5_format(runtime->to, sizeof(runtime->to), "%s/pc5/mono-%s", root, version) ||
        pc5_format(done, sizeof(done), "%s/.complete", runtime->to) ||
        pc5_format(list, sizeof(list), "%s/pc5/mono.list", app)) {
        snprintf(runtime->error, sizeof(runtime->error), "The app's path is too long.");
        return -1;
    }
    if (pc5_exists(done)) return 0;
    if (pc5_read_file(list, &runtime->list, 16u << 20) || pc5_buf_terminate(&runtime->list) || pc5_mkdirs(runtime->to)) {
        snprintf(runtime->error, sizeof(runtime->error), "PC5 can't read the app's .NET runtime.");
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
            snprintf(runtime->error, sizeof(runtime->error), "A path of the .NET runtime is too long.");
            return -1;
        }
        if (line[n - 1] == '/') {
            if (pc5_mkdirs(to)) {
                snprintf(runtime->error, sizeof(runtime->error), "PC5 can't make the .NET runtime's folders: is the console full?");
                return -1;
            }
        } else {
            char folder[PC5_PATH];
            pc5_copy(folder, sizeof(folder), to);
            *strrchr(folder, '/') = 0;   /* its folder, should the list not name it */
            if (pc5_mkdirs(folder) || copy_file(from, part) || rename(part, to)) {
                unlink(part);
                snprintf(runtime->error, sizeof(runtime->error), "PC5 can't copy the .NET runtime: is the console full?");
                return -1;
            }
        }
        runtime->done++;
        if (now_ms() >= until) return 0;
    }
    if (pc5_format(to, sizeof(to), "%s/.complete", runtime->to) || pc5_write_file(to, "", 0)) {
        snprintf(runtime->error, sizeof(runtime->error), "PC5 can't finish the .NET runtime's copy.");
        return -1;
    }
    return 1;
}

void pc5_runtime_end(Pc5Runtime *runtime)
{
    pc5_buf_free(&runtime->list);
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
    game->light_target = bright(game->cover_tint);
    game->trigger_amplitude = 0;
    game->resistance = game->settings.triggers == PC5_TRIGGERS_RESISTANCE || game->settings.triggers == PC5_TRIGGERS_BOTH;
    if (prefix && app && app[0]) {
        int media = pc5_prefix_media(root, prefix, app);
        if (media) say(game, "PC5 prefix media %s", media > 0 ? "registered" : "registration failed");
        int dxvk = pc5_prefix_dxvk(root, prefix, app);
        if (dxvk) say(game, "PC5 prefix DXVK %s", dxvk > 0 ? "updated" : "update failed");
        int dotnet = pc5_prefix_dotnet(root, prefix, app);
        if (dotnet) say(game, "PC5 prefix .NET %s", dotnet > 0 ? "updated" : "update failed");
        int fonts = pc5_prefix_fonts(root, prefix, app);
        if (fonts) say(game, "PC5 prefix fonts %s", fonts > 0 ? "named" : "naming failed");
    }
    /* Frame generation, which libvulkan.prx reads from the environment, and a
     * 32-bit game's address space, which Wine does (patch 0921: 4 GB unless
     * PW_LARGE_ADDRESS_AWARE=0). */
    int memory_2gb = 0;
    if (prefix && prefix[0]) {
        Pc5Controls controls;
        pc5_controls_load(root, prefix, &controls);
        game->frame_generation = controls.frame_generation;
        memory_2gb = controls.memory_2gb;
    }
    if (game->frame_generation) setenv("PW_PC5_FRAMEGEN", "1", 1);
    else unsetenv("PW_PC5_FRAMEGEN");
    if (memory_2gb) setenv("PW_LARGE_ADDRESS_AWARE", "0", 1);
    else unsetenv("PW_LARGE_ADDRESS_AWARE");
    say(game, "PC5 game fps=%d overlay=%#x corner=%d vibration=%d light=%d triggers=%d trigger_rumble=%d "
        "trigger_resistance=%d tint=%06x frame_generation=%d memory_32bit=%s", game->settings.fps,
        pc5_game_overlay_lines(&game->settings), (int)game->settings.fps_corner, game->settings.vibration,
        (int)game->settings.light_bar, (int)game->settings.triggers, game->settings.trigger_rumble,
        game->settings.trigger_resistance, (unsigned)game->cover_tint, game->frame_generation, memory_2gb ? "2gb" : "4gb");
}

const uint32_t *pc5_game_loading(Pc5GameSession *game, uint64_t now_ms)
{
    if (game->matrix_failed) return NULL;
    if (!game->matrix) {
        Pc5Fonts fonts;
        char path[PC5_PATH];
        memset(&fonts, 0, sizeof(fonts));
        if (pc5_format(path, sizeof(path), "%s/pc5/fonts.bin", game->app) || pc5_fonts_load(&fonts, path))
            say(game, "PC5 loading screen without its fonts: %s", path);
        game->matrix = pc5_matrix_create(&fonts, game->title, (uint32_t)now_ms * 2654435761u);
        pc5_fonts_free(&fonts);
        if (!game->matrix) {
            game->matrix_failed = 1;
            say(game, "PC5 loading screen: out of memory");
            return NULL;
        }
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
    return (settings->fps ? PW_PC5_HUD_FPS : 0u) | (settings->frame_time ? PW_PC5_HUD_FRAME_TIME : 0u) |
           (settings->cpu_usage ? PW_PC5_HUD_CPU : 0u) | (settings->cpu_temperature ? PW_PC5_HUD_CPU_TEMP : 0u) |
           (settings->soc_temperature ? PW_PC5_HUD_SOC_TEMP : 0u) | (settings->memory ? PW_PC5_HUD_MEMORY : 0u);
}

/* A sensor's temperature, -1 when it can't be read; *rc what the console said. */
static int temperature(const Pc5GameSession *game, int sensor, int *rc)
{
    int celsius = -1;
    *rc = game->ops.temperature ? game->ops.temperature(sensor, &celsius) : -1;
    return !*rc && celsius > 0 && celsius < 150 ? celsius : -1;
}

int pc5_game_overlay_sample(Pc5GameSession *game, uint64_t now_ms, PwPc5HudStats *stats)
{
    unsigned lines = pc5_game_overlay_lines(&game->settings);
    PwPc5HudStats s = { .lines = lines, .cpu_percent = -1, .cpu_celsius = -1, .soc_celsius = -1 };
    int cpu_rc = 0, soc_rc = 0, memory_rc = 0;
    unsigned cpus = 0;

    if (!lines || (game->stats_at && now_ms - game->stats_at < 1000)) return 0;
    if ((lines & PW_PC5_HUD_CPU) && game->ops.cpu_time_us) {
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
        say(game, "PC5 overlay lines=%#x cpus=%u cpu_temp=%d rc=%#x soc_temp=%d rc=%#x memory=%lluM/%lluM rc=%#x",
            lines, cpus, s.cpu_celsius, (unsigned)cpu_rc, s.soc_celsius, (unsigned)soc_rc,
            (unsigned long long)(s.memory_used >> 20), (unsigned long long)(s.memory_total >> 20),
            (unsigned)memory_rc);
        game->stats_logged = 1;
    }
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
    game->hud_second_at = now_ms;
    game->hud_second_frames = frames;
    if (!(height = pw_pc5_hud_draw_stats(image, &stats))) {
        game->ops.cursor(videoout, NULL, 0, 0);   /* nothing to show: hidden */
        return;
    }
    pw_pc5_hud_place_box((int)game->settings.fps_corner, 1920, 1080, height, &x, &y);
    if (game->ops.cursor(videoout, image, x, y) && !game->hud_shown) say(game, "PC5 overlay refused");
    game->hud_shown = 1;
}

void pc5_game_end(Pc5GameSession *game, int pad)
{
    Pc5Trigger off = { .mode = PC5_TRIGGER_OFF };
    if (pad >= 0 && game->triggers_sent && game->ops.triggers) game->ops.triggers(pad, &off, &off);
    if (pad >= 0 && game->light_sent && game->ops.light_bar_reset) game->ops.light_bar_reset(pad);
    pc5_matrix_destroy(game->matrix);
    game->matrix = NULL;
}
