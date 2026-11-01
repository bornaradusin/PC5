/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5's settings, kept in <root>/pc5/settings.txt as "name = value" lines.
 * PC5 writes them from its Settings; the game's process reads them when a
 * game starts (the overlay, the controller). A missing file, or a line
 * PC5 doesn't know, gives the default. */
#ifndef PC5_SETTINGS_H
#define PC5_SETTINGS_H
#include "pc5.h"

typedef enum Pc5Corner {
    PC5_CORNER_TOP_LEFT, PC5_CORNER_TOP_RIGHT, PC5_CORNER_BOTTOM_LEFT, PC5_CORNER_BOTTOM_RIGHT, PC5_CORNERS
} Pc5Corner;

/* The overlay's size (Settings › Overlay › Size). */
typedef enum Pc5OverlaySize {
    PC5_OVERLAY_NORMAL,       /* every line at once, in small text */
    PC5_OVERLAY_LARGE,        /* twice the size, two lines at a time, taking turns */
    PC5_OVERLAY_EXTRA_LARGE,  /* every line at once, shown at twice the size by the display (its cursor magnified) */
    PC5_OVERLAY_SIZES
} Pc5OverlaySize;

typedef enum Pc5Light {
    PC5_LIGHT_GAME,       /* the colour of the game's cover */
    PC5_LIGHT_SCREEN,     /* the colour of what is on screen */
    PC5_LIGHT_CONSOLE,    /* the console's own colour: PC5 leaves it */
    PC5_LIGHTS
} Pc5Light;

typedef enum Pc5Triggers {
    PC5_TRIGGERS_OFF,
    PC5_TRIGGERS_RUMBLE,      /* L2 and R2 vibrate with the game's rumble */
    PC5_TRIGGERS_RESISTANCE,  /* L2 and R2 push back */
    PC5_TRIGGERS_BOTH,        /* they push back, and vibrate instead while the game rumbles (a trigger
                               * runs one effect at a time) */
    PC5_TRIGGERS_COUNT
} Pc5Triggers;

typedef struct Pc5Settings {
    /* The overlay in games (Settings › Overlay), a line each: the frame
     * counter, the frames' average time, how busy the console's CPU and GPU are,
     * the CPU's and the chip's temperatures, the memory in use; its corner and size. */
    int fps;
    int frame_time;
    int cpu_usage;
    int gpu_usage;
    int cpu_temperature;
    int soc_temperature;
    int memory;
    Pc5Corner fps_corner;
    Pc5OverlaySize overlay_size;
    int vibration;            /* how strong the game's rumble is on the controller: 0 (off), 25, 50, 75, 100 % */
    Pc5Light light_bar;
    Pc5Triggers triggers;
    int trigger_rumble;       /* the triggers' rumble: 25, 50, 75, 100 % */
    int trigger_resistance;   /* how firmly they push back: 25, 50, 75, 100 % */
    int check_updates;        /* when PC5 opens */
    int keep_logs;            /* every session's log, not only those with a problem */
    int ftp_server;           /* the INSTALL folder's FTP server, while PC5 is open (pc5_ftp.h) */
    int web_page;             /* PC5's page for a browser, while PC5 is open (pc5_web.h) */
    /* The username and password the FTP server and the page ask for: "" any (no password). */
    char remote_user[64];
    char remote_password[64];
    /* The sound on the controller's speaker when PC5 is opened: a sound's name
     * (pc5_sound.h), or "off". */
    char startup_sound[64];
    int startup_volume;       /* how loud PC5 plays it: 10 to 100 %, in steps of 10 */
    /* The picture sources turned off (pc5_cover.h's PC5_PICTURES_*): "pictures_<key> = off". */
    uint32_t pictures_off;
    /* PC5's language (Settings › System › Language): "" for the console's, else a code of
     * pc5_i18n.h's ("de", "pt-BR"); "language = system" in the file for the console's. */
    char language[16];
    /* A USB keyboard (Settings › System › Keyboard layout): 0 Auto, the layout of PC5's language
     * (pc5_keyboard_layout); 1 always the US one, as before 2.1.0. "keyboard = auto|us". */
    int keyboard_us;
    /* 120 Hz output (Settings › In games): 1 asks for 119.88 Hz where the TV takes it (the console's
     * own 120 Hz setting on), for every game; 0 (the default) 60 Hz. "high_refresh = on|off". */
    int high_refresh;
} Pc5Settings;

void pc5_settings_default(Pc5Settings *settings);
/* Always fills settings (defaults where the file says nothing). 0 when the
 * file was read, -1 when there was none. */
int pc5_settings_load(const char *root, Pc5Settings *settings);
int pc5_settings_save(const char *root, const Pc5Settings *settings);

const char *pc5_corner_name(Pc5Corner corner);      /* "Top right" */
const char *pc5_light_name(Pc5Light light);         /* "Game's colour" */
const char *pc5_triggers_name(Pc5Triggers triggers);
const char *pc5_overlay_size_name(Pc5OverlaySize size);   /* "Extra large" */
const char *pc5_overlay_size_key(Pc5OverlaySize size);    /* "extra-large", as in the settings file */
/* The next step of a strength in percent: up (step 1) or down (-1), from
 * lowest (0 for Vibration's Off, else 25) to 100, round. */
int pc5_strength_next(int percent, int lowest, int step);
/* The API keys, in <root>/api_keys.txt as "NAME=value" lines (# starts a comment): the one
 * named, into out, or "" when it is missing, empty or not a key (only the signs ! to ~).
 * The file is made, explaining itself with its keys empty, when there is none; it is
 * the player's, never deleted. Read each time, so a key added counts at the next search.
 * 0, or -1 when there is no key. A key is never logged or shown. */
#define PC5_API_KEYS "api_keys.txt"
#define PC5_STEAMGRIDDB_KEY "STEAMGRIDDB_API_KEY"
int pc5_api_key(const char *root, const char *name, char *out, size_t size);
/* The startup sound's next volume: up (step 1) or down (-1) by 10, from 10 to 100 %, round. */
int pc5_volume_next(int percent, int step);

#endif
