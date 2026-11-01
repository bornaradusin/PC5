/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: its settings file (pc5_settings.h). */
#include "pc5_settings.h"
#include "pc5_cover.h"
#include "pc5_i18n.h"
#include "pc5_sound.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *const corner_keys[] = { "top-left", "top-right", "bottom-left", "bottom-right" };
static const char *const corner_names[] = { N_("Top left"), N_("Top right"), N_("Bottom left"), N_("Bottom right") };
static const char *const light_keys[] = { "game", "screen", "console" };
static const char *const light_names[] = { N_("Game's colour"), N_("Follows the screen"), N_("Console's colour") };
static const char *const overlay_size_keys[] = { "normal", "large", "extra-large" };
static const char *const overlay_size_names[] = { N_("Normal"), N_("Large"), N_("Extra large") };
static const char *const trigger_keys[] = { "off", "rumble", "resistance", "both" };
static const char *const trigger_names[] = { N_("Off"), N_("Rumble"), N_("Resistance"), N_("Rumble + resistance") };

void pc5_settings_default(Pc5Settings *settings)
{
    memset(settings, 0, sizeof(*settings));
    settings->fps = 0;
    settings->fps_corner = PC5_CORNER_TOP_RIGHT;
    settings->vibration = 100;
    settings->light_bar = PC5_LIGHT_GAME;
    settings->triggers = PC5_TRIGGERS_OFF;
    settings->trigger_rumble = 100;
    settings->trigger_resistance = 50;
    settings->check_updates = 1;
    settings->keep_logs = 0;
    pc5_copy(settings->startup_sound, sizeof(settings->startup_sound), PC5_SOUND_DEFAULT);
    settings->startup_volume = 100;
}

static int pick(const char *value, const char *const *keys, int count, int fallback)
{
    for (int i = 0; i < count; i++)
        if (!strcmp(value, keys[i])) return i;
    return fallback;
}

static int on_off(const char *value, int fallback)
{
    if (!strcmp(value, "on")) return 1;
    if (!strcmp(value, "off")) return 0;
    return fallback;
}

/* A strength in percent, in steps of 25 from lowest; "on" and "off" as 100 and 0. */
static int percent(const char *value, int lowest, int fallback)
{
    int n = 0, digits = 0;
    if (!strcmp(value, "on")) return 100;
    if (!strcmp(value, "off")) return lowest ? fallback : 0;
    for (const char *p = value; *p >= '0' && *p <= '9' && digits < 4; p++, digits++) n = n * 10 + (*p - '0');
    if (!digits || n > 100 || n < lowest || n % 25) return fallback;
    return n;
}

int pc5_strength_next(int value, int lowest, int step)
{
    value += step > 0 ? 25 : -25;
    if (value > 100) value = lowest;
    if (value < lowest) value = 100;
    return value;
}

/* The startup sound's volume in percent: 10 to 100 in steps of 10. */
static int volume(const char *value, int fallback)
{
    int n = 0, digits = 0;
    for (const char *p = value; *p >= '0' && *p <= '9' && digits < 4; p++, digits++) n = n * 10 + (*p - '0');
    if (!digits || value[digits] || n < 10 || n > 100 || n % 10) return fallback;
    return n;
}

int pc5_volume_next(int value, int step)
{
    value += step > 0 ? 10 : -10;
    if (value > 100) value = 10;
    if (value < 10) value = 100;
    return value;
}

int pc5_settings_load(const char *root, Pc5Settings *settings)
{
    char path[PC5_PATH];
    Pc5Buf text = { 0 };

    pc5_settings_default(settings);
    if (pc5_format(path, sizeof(path), "%s/pc5/settings.txt", root) || pc5_read_file(path, &text, 16 * 1024)) {
        pc5_buf_free(&text);
        return -1;
    }
    for (char *line = (char *)text.data, *next; line && *line; line = next) {
        char name[32], value[32];
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        if (line[0] == '#') continue;
        /* The startup sound's name may have capitals and spaces: the rest of the line. */
        if (!strncmp(line, "startup_sound", 13)) {
            const char *v = strchr(line, '=');
            char sound[64];
            if (!v) continue;
            for (v++; *v == ' ' || *v == '\t'; v++) {
            }
            pc5_copy(sound, sizeof(sound), v);
            for (size_t n = strlen(sound); n && (sound[n - 1] == ' ' || sound[n - 1] == '\r' || sound[n - 1] == '\t'); n--)
                sound[n - 1] = 0;
            /* 1.7.0's "on", and 1.8.0's own sound, which PC5 no longer has: the default now. */
            if (!pc5_casecmp(sound, "on") || !pc5_casecmp(sound, "PC5")) pc5_copy(sound, sizeof(sound), PC5_SOUND_DEFAULT);
            if (!pc5_casecmp(sound, "off")) pc5_copy(sound, sizeof(sound), "off");
            if (sound[0] && !strchr(sound, '/')) pc5_copy(settings->startup_sound, sizeof(settings->startup_sound), sound);
            continue;
        }
        /* The language: a code with capitals ("pt-BR"), or "system". */
        if (!strncmp(line, "language", 8) && (line[8] == ' ' || line[8] == '=' || line[8] == '\t')) {
            char code[16];
            if (sscanf(line, "language = %15[A-Za-z0-9-]", code) != 1) continue;
            int at = pc5_language_find(code);
            settings->language[0] = 0;
            if (at >= 0) pc5_copy(settings->language, sizeof(settings->language), pc5_language(at)->code);
            continue;
        }
        /* The username and password: the rest of the line, as typed. */
        if (!strncmp(line, "remote_user", 11) || !strncmp(line, "remote_password", 15)) {
            const char *v = strchr(line, '=');
            int user = line[7] == 'u';
            char *to = user ? settings->remote_user : settings->remote_password;
            size_t size = user ? sizeof(settings->remote_user) : sizeof(settings->remote_password);
            if (!v) continue;
            for (v++; *v == ' ' || *v == '\t'; v++) {
            }
            pc5_copy(to, size, v);
            for (size_t n = strlen(to); n && (to[n - 1] == '\r' || to[n - 1] == ' ' || to[n - 1] == '\t'); n--) to[n - 1] = 0;
            continue;
        }
        if (sscanf(line, " %31[a-z_] = %31[a-z0-9-]", name, value) != 2) continue;
        if (!strcmp(name, "fps")) settings->fps = on_off(value, settings->fps);
        else if (!strcmp(name, "frame_time")) settings->frame_time = on_off(value, settings->frame_time);
        else if (!strcmp(name, "cpu_usage")) settings->cpu_usage = on_off(value, settings->cpu_usage);
        else if (!strcmp(name, "gpu_usage")) settings->gpu_usage = on_off(value, settings->gpu_usage);
        else if (!strcmp(name, "cpu_temperature")) settings->cpu_temperature = on_off(value, settings->cpu_temperature);
        else if (!strcmp(name, "soc_temperature")) settings->soc_temperature = on_off(value, settings->soc_temperature);
        else if (!strcmp(name, "memory")) settings->memory = on_off(value, settings->memory);
        else if (!strcmp(name, "overlay_size")) {
            settings->overlay_size =
                (Pc5OverlaySize)pick(value, overlay_size_keys, PC5_OVERLAY_SIZES, (int)settings->overlay_size);
        } else if (!strcmp(name, "fps_corner"))
            settings->fps_corner = (Pc5Corner)pick(value, corner_keys, PC5_CORNERS, (int)settings->fps_corner);
        else if (!strcmp(name, "vibration")) settings->vibration = percent(value, 0, settings->vibration);
        else if (!strcmp(name, "trigger_rumble")) settings->trigger_rumble = percent(value, 25, settings->trigger_rumble);
        else if (!strcmp(name, "trigger_resistance"))
            settings->trigger_resistance = percent(value, 25, settings->trigger_resistance);
        else if (!strcmp(name, "light_bar"))
            settings->light_bar = (Pc5Light)pick(value, light_keys, PC5_LIGHTS, (int)settings->light_bar);
        else if (!strcmp(name, "triggers"))
            settings->triggers = (Pc5Triggers)pick(value, trigger_keys, PC5_TRIGGERS_COUNT, (int)settings->triggers);
        else if (!strcmp(name, "check_updates")) settings->check_updates = on_off(value, settings->check_updates);
        else if (!strcmp(name, "keep_logs")) settings->keep_logs = on_off(value, settings->keep_logs);
        else if (!strcmp(name, "high_refresh")) settings->high_refresh = on_off(value, settings->high_refresh);
        else if (!strcmp(name, "keyboard")) settings->keyboard_us = !strcmp(value, "us") ? 1 : !strcmp(value, "auto") ? 0 : settings->keyboard_us;
        else if (!strcmp(name, "ftp_server")) settings->ftp_server = on_off(value, settings->ftp_server);
        else if (!strcmp(name, "web_page")) settings->web_page = on_off(value, settings->web_page);
        else if (!strcmp(name, "startup_volume")) settings->startup_volume = volume(value, settings->startup_volume);
        else if (!strncmp(name, "pictures_", 9)) {
            for (int i = 0; i < PC5_PICTURE_SOURCES; i++) {
                if (strcmp(name + 9, pc5_picture_source_key(i))) continue;
                if (on_off(value, 1)) settings->pictures_off &= ~(1u << i);
                else settings->pictures_off |= 1u << i;
            }
        }
    }
    pc5_buf_free(&text);
    return 0;
}

int pc5_settings_save(const char *root, const Pc5Settings *settings)
{
    char path[PC5_PATH], text[1800];
    int length;

    if (pc5_format(path, sizeof(path), "%s/pc5", root) || pc5_mkdirs(path) ||
        pc5_format(path, sizeof(path), "%s/pc5/settings.txt", root))
        return -1;
    length = snprintf(text, sizeof(text),
                      "# PC5's settings (Settings in PC5 changes them)\n"
                      "fps = %s\nframe_time = %s\ncpu_usage = %s\ngpu_usage = %s\ncpu_temperature = %s\nsoc_temperature = %s\n"
                      "memory = %s\nfps_corner = %s\noverlay_size = %s\nvibration = %d\nlight_bar = %s\ntriggers = %s\n"
                      "trigger_rumble = %d\ntrigger_resistance = %d\ncheck_updates = %s\nkeep_logs = %s\nstartup_sound = %s\n"
                      "startup_volume = %d\nftp_server = %s\nweb_page = %s\nremote_user = %s\nremote_password = %s\nlanguage = %s\n"
                      "keyboard = %s\nhigh_refresh = %s\n",
                      settings->fps ? "on" : "off", settings->frame_time ? "on" : "off",
                      settings->cpu_usage ? "on" : "off", settings->gpu_usage ? "on" : "off",
                      settings->cpu_temperature ? "on" : "off",
                      settings->soc_temperature ? "on" : "off", settings->memory ? "on" : "off",
                      corner_keys[settings->fps_corner % PC5_CORNERS], overlay_size_keys[settings->overlay_size % PC5_OVERLAY_SIZES],
                      settings->vibration,
                      light_keys[settings->light_bar % PC5_LIGHTS], trigger_keys[settings->triggers % PC5_TRIGGERS_COUNT],
                      settings->trigger_rumble, settings->trigger_resistance, settings->check_updates ? "on" : "off",
                      settings->keep_logs ? "on" : "off", settings->startup_sound, settings->startup_volume,
                      settings->ftp_server ? "on" : "off", settings->web_page ? "on" : "off", settings->remote_user,
                      settings->remote_password, settings->language[0] ? settings->language : "system",
                      settings->keyboard_us ? "us" : "auto", settings->high_refresh ? "on" : "off");
    for (int i = 0; i < PC5_PICTURE_SOURCES && length > 0 && (size_t)length < sizeof(text); i++)
        length += snprintf(text + length, sizeof(text) - (size_t)length, "pictures_%s = %s\n", pc5_picture_source_key(i),
                           settings->pictures_off & (1u << i) ? "off" : "on");
    if (length < 0 || (size_t)length >= sizeof(text)) return -1;
    return pc5_write_file(path, text, (size_t)length);
}

const char *pc5_corner_name(Pc5Corner corner) { return corner_names[corner % PC5_CORNERS]; }
const char *pc5_light_name(Pc5Light light) { return light_names[light % PC5_LIGHTS]; }
const char *pc5_triggers_name(Pc5Triggers triggers) { return trigger_names[triggers % PC5_TRIGGERS_COUNT]; }
const char *pc5_overlay_size_name(Pc5OverlaySize size) { return overlay_size_names[size % PC5_OVERLAY_SIZES]; }
const char *pc5_overlay_size_key(Pc5OverlaySize size) { return overlay_size_keys[size % PC5_OVERLAY_SIZES]; }

static const char api_keys_text[] =
    "# PC5 API keys\n"
    "# Enter your API keys below, after the = sign, and save the file.\n"
    "# Leave unused keys empty: PC5 works without them.\n"
    "# PC5 reads this file when it opens and each time it looks for pictures.\n"
    "#\n"
    "# SteamGridDB: more covers and backgrounds. Make a free account at steamgriddb.com,\n"
    "# then make a key in its Preferences, under API.\n"
    "\n"
    PC5_STEAMGRIDDB_KEY "=\n";

int pc5_api_key(const char *root, const char *name, char *out, size_t size)
{
    char path[PC5_PATH];
    Pc5Buf text = { 0 };
    size_t name_length = strlen(name);
    int found = -1;

    out[0] = 0;
    if (pc5_format(path, sizeof(path), "%s/" PC5_API_KEYS, root)) return -1;
    if (!pc5_exists(path)) {
        /* Made for the player to fill in, readable by its owner only where that holds. */
        if (!pc5_write_file(path, api_keys_text, sizeof(api_keys_text) - 1)) (void)chmod(path, 0600);
        return -1;
    }
    if (pc5_read_file(path, &text, 64 * 1024) || pc5_buf_terminate(&text)) {
        pc5_buf_free(&text);
        return -1;
    }
    for (char *line = (char *)text.data, *next; line && found; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        while (*line == ' ' || *line == '\t') line++;
        if (!*line || *line == '#' || strncmp(line, name, name_length)) continue;
        char *value = line + name_length;
        while (*value == ' ' || *value == '\t') value++;
        if (*value != '=') continue;
        for (value++; *value == ' ' || *value == '\t'; value++) {
        }
        size_t length = strlen(value);
        while (length && (value[length - 1] == ' ' || value[length - 1] == '\t' || value[length - 1] == '\r')) length--;
        value[length] = 0;
        /* Only what can go in a request's header as it is: no spaces or line breaks. */
        int plain = length > 0 && length < size;
        for (size_t i = 0; i < length && plain; i++) plain = value[i] >= '!' && value[i] <= '~';
        if (plain) {
            memcpy(out, value, length + 1);
            found = 0;
        }
    }
    /* Not left in memory freed later. */
    if (text.data) memset(text.data, 0, text.len);
    pc5_buf_free(&text);
    return found;
}
