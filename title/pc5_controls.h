/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: how a game is played, chosen in its menu and kept with PC5's notes on it
 * (prefixes/<game>/pc5/controls.txt, "name = value" lines).
 *
 * The controller is the game's Xbox controller (XInput), a DualSense as Windows sees
 * one plugged in (for games that read controllers another way, or know the
 * DualSense), or it plays the keyboard and mouse: then each button sends a key or a
 * mouse button, and each stick moves the pointer, sends four keys (its directions)
 * or does nothing. PC5 writes the
 * choice into the game's profile each time it starts ([input], which prospero-win
 * reads), so a change takes effect at the next start. Holding the touchpad and
 * OPTIONS still leaves the game in every mode. */
#ifndef PC5_CONTROLS_H
#define PC5_CONTROLS_H
#include "pc5.h"

typedef enum Pc5PadMode {
    PC5_PAD_XBOX,         /* the game's Xbox controller */
    PC5_PAD_DUALSENSE,    /* a DualSense, as on a PC */
    PC5_PAD_WHEEL,        /* the console's steering wheel, as a Logitech G29 on a PC */
    PC5_PAD_KEYBOARD,     /* keys and the mouse */
    PC5_PAD_MODES
} Pc5PadMode;

typedef enum Pc5StickUse {
    PC5_STICK_KEYS,       /* four keys, one for each direction */
    PC5_STICK_POINTER,    /* moves the mouse pointer (one stick at a time) */
    PC5_STICK_NOTHING,
    PC5_STICK_USES
} Pc5StickUse;

/* A game's Graphics API (Triangle › Graphics): PC5's choice, or Wine's own Direct3D for
 * a game that doesn't run that way (pc5_profile_text_at in pc5_library.c).
 *   AUTO         DXVK for Direct3D 8 to 11, VKD3D-Proton for Direct3D 12, Wine's
 *                own Direct3D (WineD3D) on OpenGL for DirectDraw
 *   WINE         Wine's own: WineD3D on OpenGL (Mesa Zink) for Direct3D 8 to 11 and
 *                DirectDraw, Wine's VKD3D for Direct3D 12
 *   WINE_VULKAN  WineD3D drawing with Vulkan itself, without OpenGL (Wine's VKD3D
 *                for Direct3D 12, as WINE)
 *   WINE_2D      DirectDraw only: WineD3D without 3D (renderer=no3d), every picture
 *                made on the CPU and shown the way plain Windows drawing is, so a
 *                game that mixes DirectDraw with plain Windows drawing on one window
 *                (Heroes of Might and Magic IV) shows both in one picture instead of
 *                flickering between them. A DirectDraw game's Direct3D (7 and older)
 *                can't start with it. */
typedef enum Pc5Graphics {
    PC5_GRAPHICS_AUTO,
    PC5_GRAPHICS_WINE,
    PC5_GRAPHICS_WINE_VULKAN,
    PC5_GRAPHICS_WINE_2D,
    PC5_GRAPHICS_COUNT
} Pc5Graphics;

/* What can be bound: the 17 buttons, then the sticks' directions (left stick up,
 * down, left, right, then the right stick's), in prospero-win's order, then the
 * touchpad's own (its left and right halves, swipes up, down, left and right,
 * and a two-finger click). */
enum {
    PC5_BUTTONS = 17,
    PC5_CONTROLS = 32,
    PC5_STICK_FIRST = 17,         /* the left stick's up; the right stick's is 21 */
    PC5_STICK_CONTROLS = 8,
    PC5_TOUCH_FIRST = 25,         /* the touchpad's left half, then its right, then the swipes */
    PC5_TOUCH_ZONES = 6,          /* bound with Touchpad: Halves and swipes */
    PC5_TWO_FINGER = 31,          /* a two-finger click: Nothing is PC5's on-screen keyboard */
    PC5_TOUCHPAD = 16,            /* the touchpad's click, as one button */
    PC5_SPEEDS = 10,              /* pointer speed levels, 300 pixels a second each at full tilt */
    PC5_SLOW_STEP = 10, PC5_SLOW_MAX = 90, PC5_SLOW_DEFAULT = 50,
};

typedef struct Pc5Controls {
    Pc5PadMode mode;
    Pc5StickUse stick[2];         /* left, right */
    int speed;                    /* 1..PC5_SPEEDS */
    /* The button that slows the pointer while held (it sends nothing else), -1 for none, and by
     * how much: PC5_SLOW_STEP..PC5_SLOW_MAX percent (prospero-win's mouse_slow, Wine patch 0942). */
    int slow_button;
    int slow_percent;
    /* 1: the mouse pointer hides when it hasn't been moved or clicked for 5 seconds (Wine patch
     * 0999, WINE_PS5_CURSOR_IDLE_MS), for a game that leaves it in a corner; 0 (the default): it
     * stays as the game shows it, as before 2.0.8. */
    int pointer_hide;
    uint8_t key[PC5_CONTROLS];    /* an index into pc5_keys */
    /* What the touchpad is with Keyboard & mouse (prospero-win's touchpad_mode): 0 one button
     * (its click is Touchpad's), 1 its halves and swipes, 2 a mouse, a finger moving the pointer. */
    int touch_mode;
    /* Frame generation (libvulkan.prx): 0 off; 1 PC5's own (FSR 3), the game held at half the
     * display's rate and shown at the whole, 30 -> 60 or, with 120 Hz output, 60 -> 120;
     * 2 MihawkGen, three pictures a frame, 40 -> 120 with 120 Hz output (30 -> 60 without).
     * "frame_generation = off|on|mihawkgen". */
    int frame_generation;
    /* A 32-bit game's address space. 0, Auto (the default from 1.9.8): as on Windows, 4 GB for a
     * program made for it (its large-address flag), 2 GB for one that isn't, which may take an
     * address above 2 GB for a negative number or size what it asks for by the room it sees.
     * 1: 4 GB for any, room for big textures and mods (the default from 1.4.0 to 1.9.7). */
    int memory_4gb;
    /* A 32-bit game's video memory, as Wine's own Direct3D (its DirectDraw, its hardware check,
     * dxdiag) tells it. 0, Auto: at most 2 GB, a number an older game's check can take (the
     * console's 8 GB, past what a 32-bit number holds, reads to it as none or as negative).
     * 1, Full: all of it. A 64-bit Direct3D 10 to 12 game is told 4 GB by DXVK's dxgi on Auto
     * (pc5_game.c), all 8 GB on Full. */
    int video_memory_full;
    /* A size between those when video_memory_full is 0, in MB (pc5_video_memory_sizes); 0, Auto:
     * 2 GB for a 32-bit game, 4 GB for a 64-bit one. Less leaves more of the shared memory for the
     * game itself, for one that runs out of it; more is for a game that warns there is too little
     * video memory on Auto (Gears of War: E-Day). "video_memory = auto|full|512mb|1gb|2gb|3gb|5gb|6gb". */
    int video_memory_mb;
    int resolution;               /* 0: 1920x1080, 1: 2560x1440, 2: 3840x2160 (games drawn through Vulkan) */
    int stretch;                  /* 1: the picture stretched to fill the 16:9 screen, 0: its own shape */
    int cpu_sharing;              /* 1: the game's threads take turns on the CPUs, as on a PC (prospero-win's
                                   * [runtime] thread_scheduling); 0: each keeps its CPU until it waits */
    Pc5Graphics graphics;         /* who turns the game's Direct3D or DirectDraw into Vulkan */
    int steam_off;                /* 1: Valve's steam_api DLL as the game has it; 0, Auto: the Steam emulator (pc5_steam.h) */
    /* Sound output: 0 stereo; 1 5.1, 2 7.1, the speakers the game is offered (WINE_PS5_SPEAKERS
     * 6 or 8 for the title and Wine's audio driver, the console's port then in 7.1).
     * "speakers = 2|6|8". */
    int surround;
} Pc5Controls;

/* Graphics memory's choices, from least to most, for a 32-bit game (bits32) or a 64-bit one: each
 * size in MB, 0 for Auto (2 or 4 GB), -1 for Maximum. Their count. */
int pc5_video_memory_sizes(int bits32, const int **sizes);
/* Where controls is in those choices (Auto when its size isn't one of them). */
int pc5_video_memory_level(const Pc5Controls *controls, int bits32);
/* Sets controls to choice level of those. */
void pc5_video_memory_set(Pc5Controls *controls, int bits32, int level);
/* The size as the file says it ("auto", "full", "512mb", "3gb"). */
const char *pc5_video_memory_key(const Pc5Controls *controls);

/* A key, mouse button or nothing, as prospero-win names it in a profile, and as
 * PC5 shows it. */
typedef struct Pc5Key {
    const char *name;             /* "space", "mouse_left", "none" */
    const char *label;            /* "Space", "Left click", "Nothing" */
} Pc5Key;

extern const Pc5Key pc5_keys[];
extern const int pc5_key_count;

/* The default layout: the Xbox controller, and for keyboard and mouse W A S D on
 * the left stick, the pointer on the right, the triggers as the mouse buttons. */
void pc5_controls_default(Pc5Controls *controls);
/* Always fills controls (the default where the file says nothing). 0 when the
 * file was read, -1 when there was none. */
int pc5_controls_load(const char *root, const char *id, Pc5Controls *controls);
/* The player's own mapping, for every game (<root>/pc5/mapping.txt): put on controls (its
 * sticks, pointer and keys only), saved from them, and whether there is one. A game without
 * controls of its own yet starts with it (pc5_controls_load). 0, or -1. */
int pc5_controls_mapping_load(const char *root, Pc5Controls *controls);
int pc5_controls_mapping_save(const char *root, const Pc5Controls *controls);
int pc5_controls_mapping_exists(const char *root);
int pc5_controls_save(const char *root, const char *id, const Pc5Controls *controls);
/* controls.txt's lines ("graphics_api = wine_2d\n", a recipe's) over controls, as a load reads them. */
void pc5_controls_apply_text(const char *text, Pc5Controls *controls);
/* The profile's [input] section, from its header on, and its [runtime] section
 * when CPU sharing is on. 0, or -1 when out is too small. */
int pc5_controls_input(const Pc5Controls *controls, char *out, size_t size);

/* "Cross", "Left stick up": a control's name in PC5. */
const char *pc5_control_name(int control);
/* "One button", "Halves and swipes", "Mouse": a touch_mode's name in PC5. */
const char *pc5_touch_mode_name(int mode);
/* The choices of Graphics API a game drawing with api (Pc5Api, pc5_pe.h) has, in order:
 * their count, 0 for a game that draws no other way (OpenGL, Vulkan, Windows' own drawing). */
int pc5_graphics_choices(int api, Pc5Graphics choices[PC5_GRAPHICS_COUNT]);
/* The choice as the game menu shows it: "Auto", "WineD3D", "WineD3D on Vulkan", "Wine's VKD3D". */
const char *pc5_graphics_name(Pc5Graphics graphics, int api);
/* Its technical name (WineD3D on Vulkan, ...), untranslated: for logs, reports and the web page. */
const char *pc5_graphics_tech_name(Pc5Graphics graphics, int api);
/* "Xbox controller", "DualSense", "Keyboard & mouse". */
const char *pc5_pad_mode_name(Pc5PadMode mode);
/* A stick's use: "Keys", "Mouse pointer", "Nothing". */
const char *pc5_stick_use_name(Pc5StickUse use);
/* The key a control sends, by index in pc5_keys; the label of one. */
const char *pc5_key_label(int key);
/* The same, in the language in use where it is a word (the mouse's, the arrows, Nothing). */
const char *pc5_key_text(int key);
/* A stick's next use (step 1 or -1); the other stick stops being the pointer when
 * this one becomes it. */
void pc5_controls_next_stick(Pc5Controls *controls, int stick, int step);
/* The slow button's next choice (step 1 or -1): none, then each button in turn. */
void pc5_controls_next_slow(Pc5Controls *controls, int step);
/* The pointer speed in pixels a second at full tilt. */
int pc5_controls_pixels(const Pc5Controls *controls);

#endif
