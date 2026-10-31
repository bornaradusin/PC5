/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: the launcher's screens. The library is a row of covers with the
 * chosen one large in the middle, each game with its menu (Triangle);
 * Settings holds Install games from USB (one, or several in a row),
 * Uninstall game, the settings for games, updates and logs. The screens only
 * see buttons and a clock; the console's side (pc5_console.c) or a test
 * drives them and shows what they draw. */
#ifndef PC5_UI_H
#define PC5_UI_H
#include "pc5.h"
#include "pc5_cover.h"
#include "pc5_draw.h"

typedef struct Pc5Platform {
    void *context;
    Pc5Http http;
    /* The time as the console shows it ("14:32" or "2:32 PM"), or "". */
    void (*clock)(void *context, char *out, size_t size);
    /* Starts the game with this profile id: the title is replaced by it.
     * Returns only when that could not be done. */
    void (*play)(void *context, const char *id);
    void (*log)(void *context, const char *line);
    /* Shows text in the console's own notification, or NULL. */
    void (*notify)(void *context, const char *text);
    /* Seconds since 1970 (UTC), and the console's offset from UTC in *offset_s. */
    int64_t (*unix_time)(void *context, int *offset_s);
    int circle_accepts;     /* the console confirms with Circle (its setting) */
    /* Starts the new PC5 in this one's place, after it updated itself; NULL where it
     * can't. */
    void (*restart)(void *context);
    /* Plays a startup sound by name now, at that volume (10 to 100 %), as Settings tries it;
     * NULL where it can't. */
    void (*sound)(void *context, const char *name, int volume);
    /* The console's on-screen keyboard over the screens, for typing a name to search for:
     * opened with its title and the text to start from (UTF-8), 0 or negative when it can't
     * be; its state, 1 while it is up and 2 once it is done; and closed, its text (UTF-8)
     * into out: 1 when it was typed (Done), 0 when cancelled, negative on an error. NULL
     * where there is none. */
    int (*keyboard_open)(void *context, const char *title, const char *text);
    int (*keyboard_state)(void *context);
    int (*keyboard_close)(void *context, char *out, size_t size);
} Pc5Platform;

enum {
    PC5_KEY_UP = 1u << 0, PC5_KEY_DOWN = 1u << 1, PC5_KEY_LEFT = 1u << 2, PC5_KEY_RIGHT = 1u << 3,
    PC5_KEY_ACCEPT = 1u << 4, PC5_KEY_BACK = 1u << 5, PC5_KEY_OPTIONS = 1u << 6, PC5_KEY_L1 = 1u << 7,
    PC5_KEY_R1 = 1u << 8, PC5_KEY_SQUARE = 1u << 9, PC5_KEY_TRIANGLE = 1u << 10, PC5_KEY_L2 = 1u << 11,
    PC5_KEY_R2 = 1u << 12, PC5_KEY_R3 = 1u << 13,
};

typedef struct Pc5Input {
    uint32_t held;      /* PC5_KEY_* down now */
    uint32_t pressed;   /* went down since the last frame */
} Pc5Input;

typedef struct Pc5UiConfig {
    const char *root;              /* /data/prospero-win */
    const char *template_prefix;   /* the app's empty prefix (with its .list) */
    const char *fonts;             /* the app's fonts.bin */
    const char *const *usb_roots;  /* NULL-terminated */
    /* Home screen tiles (pc5_home.h): where they go (PC5_HOME_ROOT), and the
     * app's forwarder program (its pc5/forwarder). NULL: no tiles. */
    const char *tiles_root;
    const char *tile_template;
    /* The game a home screen tile asked for (its "--rom"), started at once;
     * NULL or "" for none. */
    const char *start;
    /* PC5 updating itself (pc5_upgrade.h): the drives its folder may be on (NULL:
     * it doesn't offer to), and the console's copies of its sce_sys (/user). */
    const char *const *drives;
    const char *user;
    /* The version an update finished installing as PC5 opened (pc5_upgrade_tidy): this
     * PC5 is the old one, so the screens say so and restart it. NULL or "" for none. */
    const char *finished;
    /* 1: the games' backgrounds are looked for online and in the player's folders while
     * the library shows (pc5_backgrounds.h); 0: only those already kept are shown. */
    int fetch_backgrounds;
    /* 1: the player has just opened PC5 (not its screens coming back after a game, nor a
     * home screen tile's game): it asks GitHub for a newer PC5 now, whenever it last did. */
    int opened;
    /* 1: PC5's logo comes up as it opens (about two seconds, over whatever shows). */
    int boot;
} Pc5UiConfig;

typedef struct Pc5Ui Pc5Ui;

Pc5Ui *pc5_ui_create(const Pc5UiConfig *config, const Pc5Platform *platform);
void pc5_ui_destroy(Pc5Ui *ui);
/* One frame's input and work (an install step, a cover lookup, the
 * animation). 1 when the screen changed. */
int pc5_ui_update(Pc5Ui *ui, const Pc5Input *input, uint64_t now_ms);
/* Draws the whole screen (1920x1080). */
void pc5_ui_draw(Pc5Ui *ui, Pc5Canvas *canvas);

/* A cover made from a game's icon (or a plain screen when it has none) and
 * its name, for a game no source has a cover for. */
int pc5_make_card(const Pc5Fonts *fonts, const char *title, const Pc5Image *icon, Pc5Image *card, uint32_t *tint);

/* For tests and screenshots: the current screen's name. */
const char *pc5_ui_screen(const Pc5Ui *ui);

#endif
