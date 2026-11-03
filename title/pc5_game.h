/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5 in a game's process (prospero-win's title in game mode, PW_PC5): its
 * settings applied while the game runs. The controller: the game's rumble
 * at the strength chosen, the light bar in the game's colour or the
 * screen's, the triggers' rumble, resistance or both; in DualSense mode the game's own light bar
 * and trigger effects, once it sets them. The loading screen until the game's first
 * picture (pc5_matrix.h). The overlay (pw_pc5_hud.h): what PC5 measures for
 * it, and the overlay itself over the frames the title shows itself (a game
 * drawing with Vulkan gets libvulkan's, pw_pc5_hud). The console's calls
 * come through Pc5GameOps (pc5_game_ps5.c), so the host tests can watch
 * them. */
#ifndef PC5_GAME_H
#define PC5_GAME_H
#include "pc5.h"
#include "pc5_matrix.h"
#include "pc5_settings.h"
#include "pw_pc5_hud.h"

typedef enum Pc5TriggerMode {
    PC5_TRIGGER_OFF, PC5_TRIGGER_FEEDBACK, PC5_TRIGGER_VIBRATION,
    PC5_TRIGGER_GAME,     /* the game's own effect (DualSense mode) */
} Pc5TriggerMode;

typedef struct Pc5Trigger {
    Pc5TriggerMode mode;
    uint8_t position;     /* where along the travel it starts, 0-9 */
    uint8_t strength;     /* feedback: 1-8 */
    uint8_t amplitude;    /* vibration: 1-8 */
    uint8_t frequency;    /* vibration: Hz */
    /* PC5_TRIGGER_GAME: libScePad's effect (its ScePadTriggerEffectMode: 1
     * feedback, 2 weapon, 3 vibration, 4 feedback by zone, 5 slope, 6
     * vibration by zone) and that effect's numbers, in its order. */
    uint8_t effect;
    uint8_t data[11];
} Pc5Trigger;

/* DualSense mode: what the game asked of its controller, as it would of a
 * DualSense on a PC. */
enum { PC5_OUTPUT_MOTORS = 1, PC5_OUTPUT_LIGHT = 2, PC5_OUTPUT_TRIGGERS = 4 };
typedef struct Pc5PadOutput {
    unsigned set;         /* PC5_OUTPUT_*: what the game has set so far */
    uint8_t large, small; /* the motors, 0-255 */
    uint32_t light;       /* the light bar, 0xRRGGBB */
    Pc5Trigger trigger[2];/* L2, R2: PC5_TRIGGER_GAME or off */
} Pc5PadOutput;

/* A thread's CPU time so far (Pc5GameOps.thread_times). */
typedef struct Pc5ThreadTime {
    int tid, cpu;
    uint64_t runtime_us;
    char name[24];
} Pc5ThreadTime;

typedef struct Pc5GameOps {
    int (*light_bar)(int handle, uint32_t rgb);         /* 0xRRGGBB */
    int (*light_bar_reset)(int handle);
    int (*triggers)(int handle, const Pc5Trigger *l2, const Pc5Trigger *r2);
    /* The second hardware cursor of the title's own VideoOut: a 64x64 image at
     * (x, y), or hidden (image NULL). */
    int (*cursor)(int videoout, const uint32_t *image, uint32_t x, uint32_t y);
    /* That cursor shown at twice its size by the display (on) or at its own
     * (sceVideoOutCursorSet2xMagnify): 0 or the console's error. NULL where there is none. */
    int (*cursor_magnify)(int videoout, int on);
    void (*log)(const char *line);
    /* The overlay's numbers: the process's CPU time (microseconds) and the
     * CPUs it has; a temperature in degrees (sensor 0 the CPU, 1 the chip)
     * and the memory in use of the whole a game has (bytes), 0 when read. */
    uint64_t (*cpu_time_us)(void);
    unsigned (*cpu_count)(void);
    /* The console's own count of each of its CPUs' time since it started, all
     * its work (busy) and in all (total), in its clock's ticks: how many CPUs
     * it gave, up to most, 0 when it can't be read (the overlay then shows the
     * game's process, cpu_time_us). */
    unsigned (*cpu_ticks)(uint64_t *busy, uint64_t *total, unsigned most);
    /* The title process's threads (the game's are among them): each one's id, CPU time
     * so far in microseconds, name, and the CPU it last ran on. How many, 0 when the
     * console doesn't say. */
    unsigned (*thread_times)(Pc5ThreadTime *threads, unsigned most);
    /* The log's chunk size from now on (pw_diagnostics_set_chunk), for a detailed profile. */
    void (*log_chunk)(size_t bytes);
    int (*temperature)(int sensor, int *celsius);
    int (*memory)(uint64_t *used, uint64_t *total);
    /* The console's on-screen keyboard over the game: open it for text written to
     * buffer (UTF-16, capacity units with the terminator), 0 or the console's error;
     * its state, 0 not open, 1 open, 2 done, negative on an error; and once done,
     * close it: 1 when the text was accepted, 0 when cancelled, negative on an error. */
    int (*keyboard_open)(uint16_t *buffer, uint32_t capacity);
    int (*keyboard_state)(void);
    int (*keyboard_close)(void);
    /* The console's language (pc5_console_language), for a USB keyboard's layout; NULL: US. */
    int (*console_language)(void);
} Pc5GameOps;

/* The on-screen keyboard's text, in UTF-16 units with the terminator. */
enum { PC5_KEYBOARD_TEXT = 256 };
/* The most CPUs the overlay counts the console's time of. */
enum { PC5_CPU_MOST = 32, PC5_THREADS_MOST = 256 };

typedef struct Pc5GameSession {
    Pc5GameOps ops;
    Pc5Settings settings;
    char app[PC5_PATH], title[PC5_NAME];
    char root[PC5_PATH];          /* /data/prospero-win */
    char prefix[PC5_ID];          /* the game's id */
    char language[16];            /* PC5's language, as PC5 passed it (pc5_game_loading_setup), or "" */
    char folder[PC5_NAME];        /* the game's folder's name, for its own loading picture, or "" */
    int shown_before;             /* the loading screen is already on the screen (pc5_game_early_loading) */
    uint32_t cover_tint;          /* the game's colour, 0 when it has no cover */
    uint32_t light, light_target; /* what the light bar shows and is going to */
    uint64_t light_at;
    int light_sent, triggers_sent, resistance;
    int game_light, game_triggers;   /* DualSense mode: the game set its own */
    Pc5Trigger game_trigger[2];
    uint8_t trigger_amplitude;
    Pc5Matrix *matrix;
    uint64_t matrix_at;
    int matrix_failed;
    int hud_shown;
    int hud_magnified;            /* the overlay's cursor at twice its size: 1, 0, -1 not asked yet */
    int hud_magnify_refused;      /* the console refused it once: not asked again */
    uint64_t hud_second_at, hud_second_frames;
    unsigned hud_seconds;         /* the overlay's updates, for the lines a large one shows */
    PwPc5HudStats stats;          /* what PC5 measured last for the overlay */
    uint64_t stats_at, stats_cpu_us;
    uint64_t stats_cpu_busy[PC5_CPU_MOST], stats_cpu_total[PC5_CPU_MOST];   /* cpu_ticks' last */
    unsigned stats_cpus;          /* how many cpu_ticks gave last, 0 none yet */
    Pc5ThreadTime stats_threads[PC5_THREADS_MOST];   /* thread_times' last */
    unsigned stats_thread_count;
    uint64_t stats_threads_at;    /* when, in ms (0 none yet) */
    int stats_logged;
    int frame_generation;         /* the game's Frame generation (PW_PC5_FRAMEGEN for libvulkan.prx) */
    /* The on-screen keyboard: open, its text, how much of the accepted text is typed,
     * and the buttons PC5 took from the game for it. */
    int two_finger_key;           /* a two-finger click is mapped to a key: no on-screen keyboard */
    int keyboard_open;
    uint16_t keyboard_text[PC5_KEYBOARD_TEXT];
    uint32_t keyboard_length, keyboard_typed, keyboard_taken;
} Pc5GameSession;

/* A game's Windows made before winegstreamer (the media decoders) was built
 * has none of its registrations, and wineboot can't run on the console: the
 * app's pc5/prefix-media.reg is appended to the prefix's system.reg, once
 * (Wine reads a key given twice as one). prefix is the game's prefix name
 * ("" or "default" for root/prefix). 1 appended, 0 already there, -1 when it
 * couldn't be done. */
int pc5_prefix_media(const char *root, const char *prefix, const char *app);
/* A game's Windows made by an older PC5 lacks the system files later ones added to the
 * template (winegstreamer.dll for videos and music, among them): a 32-bit game then can't
 * load them at all. The DLLs in the app's template (pc5/prefix.list, system32 and
 * syswow64) that the game's Windows hasn't got are copied in, once for each PC5 version
 * (the game's pc5/template.txt). prefix is the game's prefix name; the default prefix is
 * not PC5's. How many were copied, or -1 (tried again at the next start). */
int pc5_prefix_system(const char *root, const char *prefix, const char *app);
/* DXVK is in each game's Windows, copied from the app's prefix when the game
 * was installed. When the app's DXVK (named in its pc5/dxvk.txt) is not the
 * one the game's Windows has (its pc5/dxvk.txt), the app's DLLs replace the
 * game's, 64- and 32-bit, and the game's note is written. prefix is the
 * game's prefix name; the default prefix is not PC5's and is left as it is.
 * 1 replaced, 0 already the app's (or nothing to do), -1 when it couldn't be
 * done (tried again at the next start). */
int pc5_prefix_dxvk(const char *root, const char *prefix, const char *app);
/* .NET (Wine Mono, the app's pc5/mono): a prefix made before the app had it gets what its
 * installer adds to system.reg (pc5/prefix-dotnet.reg) and C:\windows\Microsoft.NET; and
 * every prefix's Wine is told where the runtime is (HKCU\Software\Wine\Mono RuntimePath:
 * root/pc5/mono-<version>/mono-2.0). 1 when it changed something, 0 when it had it all,
 * -1 on a failure (tried again next time). */
int pc5_prefix_dotnet(const char *root, const char *prefix, const char *app);
/* Fonts: a prefix made before the app had them gets the names Windows' fonts have for
 * Liberation and Droid Sans Fallback (HKCU\Software\Wine\Fonts\Replacements, the app's
 * pc5/prefix-fonts.reg, appended to user.reg). 1 when it added them, 0 when it had them or
 * the app has none, -1 on a failure (tried again next time). */
int pc5_prefix_fonts(const char *root, const char *prefix, const char *app);
/* Arial's files in the prefix's C:\Windows\Fonts (arial.ttf, arialbd.ttf, ariali.ttf,
 * arialbi.ttf), made from Liberation Sans in the app's Wine fonts: a Unity game opens them there
 * by name and drew no text without them (Magicraft, Flashing Lights), where the names above only
 * reach programs that ask Windows for a font. A file already there (the player's own Arial)
 * stays. The number added, 0 when none was missing (or there's no Windows yet), -1 on a failure. */
int pc5_prefix_arial(const char *root, const char *prefix, const char *app);
/* Windows versions for the programs that refuse the one Wine reports (Windows 10): each one's
 * HKCU\Software\Wine\AppDefaults\<program> "Version", appended to user.reg once (the
 * template's for the default prefix). 1 when it added some, 0 when it had them all, -1 on a
 * failure (tried again next time). */
int pc5_prefix_versions(const char *root, const char *prefix);
/* Installer keys for the games that refuse to start without them ("The game is not properly
 * installed": Pro Evolution Soccer 6), known by a file beside program (the game's program, on
 * the console or on the USB drive it plays from): HKLM\Software\<key> and its Wow6432Node
 * twin, with the program's folder as Windows sees it (C:\Games\...) as the install folder and the languages the game has, appended to system.reg once. Never a CD
 * key. 1 when it added some, 0 when it had them or the game needs none, -1 on a failure
 * (tried again next time). */
int pc5_prefix_installs(const char *root, const char *prefix, const char *program);

/* The runtime's copy on the console: the app can't be listed, and every game's prefix
 * would otherwise hold its own 230 MB. Made once, from pc5/mono.list, by the first .NET
 * game to start. */
typedef struct Pc5Runtime {
    char from[PC5_PATH], to[PC5_PATH];   /* app/pc5/mono, root/pc5/mono-<version> */
    Pc5Buf list;                         /* NUL-separated, folders end in '/' */
    size_t at;
    uint32_t total, done;
    char error[PC5_ERROR];
} Pc5Runtime;
/* program: the game's program on the console. 1 when it is a .NET program (or a DLL beside it
 * is a .NET one) and root has no
 * whole copy of the app's runtime: the copy begins (pc5_runtime_step); 0 when nothing is
 * needed; -1 when the copy can't begin (error set). */
int pc5_runtime_begin(Pc5Runtime *runtime, const char *root, const char *app, const char *program);
/* Copies for about budget_ms: 1 when the copy is whole, 0 to go on, -1 on a failure. */
int pc5_runtime_step(Pc5Runtime *runtime, int budget_ms);
void pc5_runtime_end(Pc5Runtime *runtime);
/* The game's own environment variables, from root/prefixes/<prefix>/pc5/env.txt: one
 * NAME=value a line ("#" for a comment), set before Wine starts, which passes them to the
 * game as Windows does its own. A game that reads a setting of its own from there (Yu-Gi-Oh!
 * Forbidden Memories Recompiled's MEMORIES_NO_MONITOR) gets it. Names only of letters, digits
 * and _, not starting with a digit; never one Wine, prospero-win or PC5 set (WINE..., PW_...,
 * LD_..., PS5..., PC5..., PATH, HOME, TMPDIR). At most PC5_GAME_ENV_MAX. How many were set,
 * their names in names (comma-separated). */
enum { PC5_GAME_ENV_MAX = 32 };
int pc5_game_env(const char *root, const char *prefix, char *names, size_t size);
/* 1 when name can be set from env.txt. */
int pc5_game_env_name_ok(const char *name);
/* At the game's start: PC5's settings, the game's cover colour, its
 * prefix's media registrations and its DXVK, from root (/data/prospero-win),
 * the game's prefix and the app's folder. */
void pc5_game_begin(Pc5GameSession *game, const Pc5GameOps *ops, const char *root, const char *app,
                    const char *prefix, const char *title);
/* The loading screen as soon as the game's process can draw, right after the /data grant and
 * before Wine is loaded: the game's own picture (pc5_loading_own) or PC5's, fading in from
 * black for fade_ms (shown with show, wait_ms between frames), then everything it took freed
 * again, so nothing of it is in the way of what Wine sets up next. The screen keeps the last
 * frame until the game's own loading screen carries on (pc5_game_shown_before). language as PC5
 * passed it. 0 when shown. */
int pc5_game_early_loading(const char *root, const char *app, const char *prefix, const char *title,
                           const char *language, const char *folder, uint32_t fade_ms,
                           void (*show)(const uint32_t *pixels, void *context), void *context,
                           void (*wait_ms)(uint32_t ms));
/* The loading screen was shown already (pc5_game_early_loading): the game's own comes at once. */
void pc5_game_shown_before(Pc5GameSession *game);
/* For the loading screen: PC5's language as PC5 passed it (NULL or "" English), and the name
 * of the game's folder (its own loading picture may be named after it; NULL or ""). */
void pc5_game_loading_setup(Pc5GameSession *game, const char *language, const char *folder);
/* The loading screen's next frame (PC5_MATRIX_WIDTH x HEIGHT, B,G,R,X), or
 * NULL when none is due yet (or it can't be drawn). */
const uint32_t *pc5_game_loading(Pc5GameSession *game, uint64_t now_ms);
/* The game showed its first picture: the loading screen is freed, and not shown again. */
void pc5_game_loading_done(Pc5GameSession *game);
/* The game's rumble (XInput's 0-65535 a motor): what the motors get, at
 * Vibration's strength, and the triggers when they follow it. */
void pc5_game_rumble(Pc5GameSession *game, int pad, uint32_t left, uint32_t right, uint8_t *large, uint8_t *small);
/* Players 2 to 4's rumble: their motors at Vibration's strength. The triggers
 * and the light bar stay player 1's. */
void pc5_game_player_rumble(const Pc5GameSession *game, uint32_t left, uint32_t right, uint8_t *large,
                            uint8_t *small);
/* DualSense mode, whenever the game's output changes: the motors at
 * Vibration's strength in large and small, which the triggers follow while
 * the game has none of its own; the game's light bar and trigger effects
 * once it sets them, which from then on replace PC5's Light bar and
 * Triggers settings for this game. */
void pc5_game_output(Pc5GameSession *game, int pad, const Pc5PadOutput *output, uint8_t *large, uint8_t *small);
/* Every tick of the title's loop: the light bar (screen: the colour of the
 * frame on screen, 0 when unknown), the triggers' resistance. */
void pc5_game_pad(Pc5GameSession *game, int pad, uint64_t now_ms, uint32_t screen);
/* The overlay's lines the settings turn on (PW_PC5_HUD_*), 0 for none. */
unsigned pc5_game_overlay_lines(const Pc5Settings *settings);
/* Every tick while the overlay is on: what PC5 measures for it, once a
 * second (the game's share of every CPU since the last time, the CPU's and
 * the chip's temperatures, the memory in use), with the lines on: 1 when
 * stats has new numbers. */
int pc5_game_overlay_sample(Pc5GameSession *game, uint64_t now_ms, PwPc5HudStats *stats);
/* Every tick while the title shows the frames itself: the overlay, once a
 * second; frames is how many it has shown. */
void pc5_game_hud(Pc5GameSession *game, int videoout, uint64_t frames, uint64_t now_ms);
/* The console's on-screen keyboard, every tick with the controller's held buttons,
 * new presses and releases (libScePad's bits) and the fingers on the touchpad: the
 * touchpad clicked with two fingers opens it, and its text, once accepted, is typed
 * into the game (pc5_game_keyboard_text). Returns the buttons the game shouldn't see
 * this tick: that click, from its press to its release. */
uint32_t pc5_game_keyboard(Pc5GameSession *game, uint32_t held, uint32_t pressed, uint32_t released,
                           unsigned fingers);
/* The accepted text not yet typed: its units, and how many in count (0: none). */
const uint16_t *pc5_game_keyboard_text(const Pc5GameSession *game, size_t *count);
/* count units of it were typed. */
void pc5_game_keyboard_typed(Pc5GameSession *game, size_t count);
/* The game has ended: the controller as the console had it. */
void pc5_game_end(Pc5GameSession *game, int pad);

#endif
