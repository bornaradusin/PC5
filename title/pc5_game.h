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

typedef struct Pc5GameOps {
    int (*light_bar)(int handle, uint32_t rgb);         /* 0xRRGGBB */
    int (*light_bar_reset)(int handle);
    int (*triggers)(int handle, const Pc5Trigger *l2, const Pc5Trigger *r2);
    /* The second hardware cursor of the title's own VideoOut: a 64x64 image at
     * (x, y), or hidden (image NULL). */
    int (*cursor)(int videoout, const uint32_t *image, uint32_t x, uint32_t y);
    void (*log)(const char *line);
    /* The overlay's numbers: the process's CPU time (microseconds) and the
     * CPUs it has; a temperature in degrees (sensor 0 the CPU, 1 the chip)
     * and the memory in use of the whole a game has (bytes), 0 when read. */
    uint64_t (*cpu_time_us)(void);
    unsigned (*cpu_count)(void);
    int (*temperature)(int sensor, int *celsius);
    int (*memory)(uint64_t *used, uint64_t *total);
} Pc5GameOps;

typedef struct Pc5GameSession {
    Pc5GameOps ops;
    Pc5Settings settings;
    char app[PC5_PATH], title[PC5_NAME];
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
    uint64_t hud_second_at, hud_second_frames;
    PwPc5HudStats stats;          /* what PC5 measured last for the overlay */
    uint64_t stats_at, stats_cpu_us;
    int stats_logged;
    int frame_generation;         /* the game's Frame generation (PW_PC5_FRAMEGEN for libvulkan.prx) */
} Pc5GameSession;

/* A game's Windows made before winegstreamer (the media decoders) was built
 * has none of its registrations, and wineboot can't run on the console: the
 * app's pc5/prefix-media.reg is appended to the prefix's system.reg, once
 * (Wine reads a key given twice as one). prefix is the game's prefix name
 * ("" or "default" for root/prefix). 1 appended, 0 already there, -1 when it
 * couldn't be done. */
int pc5_prefix_media(const char *root, const char *prefix, const char *app);
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
/* program: the game's program on the console. 1 when it is a .NET program and root has no
 * whole copy of the app's runtime: the copy begins (pc5_runtime_step); 0 when nothing is
 * needed; -1 when the copy can't begin (error set). */
int pc5_runtime_begin(Pc5Runtime *runtime, const char *root, const char *app, const char *program);
/* Copies for about budget_ms: 1 when the copy is whole, 0 to go on, -1 on a failure. */
int pc5_runtime_step(Pc5Runtime *runtime, int budget_ms);
void pc5_runtime_end(Pc5Runtime *runtime);
/* At the game's start: PC5's settings, the game's cover colour, its
 * prefix's media registrations and its DXVK, from root (/data/prospero-win),
 * the game's prefix and the app's folder. */
void pc5_game_begin(Pc5GameSession *game, const Pc5GameOps *ops, const char *root, const char *app,
                    const char *prefix, const char *title);
/* The loading screen's next frame (PC5_MATRIX_WIDTH x HEIGHT, B,G,R,X), or
 * NULL when none is due yet (or it can't be drawn). */
const uint32_t *pc5_game_loading(Pc5GameSession *game, uint64_t now_ms);
/* The game showed its first picture. */
void pc5_game_loading_done(Pc5GameSession *game);
/* The game's rumble (XInput's 0-65535 a motor): what the motors get, at
 * Vibration's strength, and the triggers when they follow it. */
void pc5_game_rumble(Pc5GameSession *game, int pad, uint32_t left, uint32_t right, uint8_t *large, uint8_t *small);
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
/* The game has ended: the controller as the console had it. */
void pc5_game_end(Pc5GameSession *game, int pad);

#endif
