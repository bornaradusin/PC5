/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5 on the console: the screens (pc5_ui.h) shown through prospero-win's
 * VideoOut presenter, worked by the DualSense and a USB keyboard, with the
 * console's own HTTPS for covers. wine64_main.c (patch 0001) calls
 * pc5_console_run instead of prospero-win's launcher. */
#ifndef PC5_CONSOLE_H
#define PC5_CONSOLE_H
#include <stddef.h>

typedef struct Pc5Console {
    const char *root;        /* the library: /data/prospero-win */
    const char *app;         /* where the app's files are seen (its app0) */
    unsigned cycle;          /* how many games ran since the title started */
    /* Restarts the title into the game with this profile id; returns only
     * when it could not. */
    void (*play)(const char *id);
    void (*log)(const char *line);
    /* The game a home screen tile started the title for ("--rom"), or NULL:
     * PC5 starts it at once. */
    const char *start;
} Pc5Console;

/* Shows PC5 until a game is started. Returns only when the screen can't be
 * opened (the caller falls back to prospero-win's own launcher). */
void pc5_console_run(const Pc5Console *console);

/* The PS5's language as a locale name for Wine ("fr_FR.UTF-8"), or NULL. */
const char *pc5_console_locale(void);
/* The console's language, SCE_SYSTEM_PARAM_LANG_* (1 English (US), 18 English (UK), ...), -1 unread. */
int pc5_console_language(void);

/* The log's line about the console: its firmware, the room on /data and PC5's
 * version ("PC5 system firmware=12.40 ..."), so a report needn't ask
 * (pc5_game_ps5.c; each session, PC5's and a game's, logs it once). */
void pc5_system_text(char *out, size_t size);

#endif
