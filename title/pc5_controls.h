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
 *                for Direct3D 12, as WINE) */
typedef enum Pc5Graphics {
    PC5_GRAPHICS_AUTO,
    PC5_GRAPHICS_WINE,
    PC5_GRAPHICS_WINE_VULKAN,
    PC5_GRAPHICS_COUNT
} Pc5Graphics;

/* What can be bound: the 17 buttons, then the sticks' directions (left stick up,
 * down, left, right, then the right stick's), in prospero-win's order. */
enum {
    PC5_BUTTONS = 17,
    PC5_CONTROLS = 25,
    PC5_STICK_FIRST = 17,         /* the left stick's up; the right stick's is 21 */
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
    uint8_t key[PC5_CONTROLS];    /* an index into pc5_keys */
    int frame_generation;         /* 1: the game held at 30 fps, shown at 60 (libvulkan.prx's frame generation) */
    /* A 32-bit game's address space. 0, Auto (the default from 1.9.8): as on Windows, 4 GB for a
     * program made for it (its large-address flag), 2 GB for one that isn't, which may take an
     * address above 2 GB for a negative number or size what it asks for by the room it sees.
     * 1: 4 GB for any, room for big textures and mods (the default from 1.4.0 to 1.9.7). */
    int memory_4gb;
    /* A 32-bit game's video memory, as Wine's own Direct3D (its DirectDraw, its hardware check,
     * dxdiag) tells it. 0, Auto: at most 2 GB, a number an older game's check can take (the
     * console's 8 GB, past what a 32-bit number holds, reads to it as none or as negative).
     * 1, Full: all of it. A 64-bit game is always told all of it. */
    int video_memory_full;
    int resolution;               /* 0: 1920x1080, 1: 2560x1440, 2: 3840x2160 (games drawn through Vulkan) */
    int stretch;                  /* 1: the picture stretched to fill the 16:9 screen, 0: its own shape */
    int cpu_sharing;              /* 1: the game's threads take turns on the CPUs, as on a PC (prospero-win's
                                   * [runtime] thread_scheduling); 0: each keeps its CPU until it waits */
    Pc5Graphics graphics;         /* who turns the game's Direct3D or DirectDraw into Vulkan */
} Pc5Controls;

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
int pc5_controls_save(const char *root, const char *id, const Pc5Controls *controls);
/* The profile's [input] section, from its header on, and its [runtime] section
 * when CPU sharing is on. 0, or -1 when out is too small. */
int pc5_controls_input(const Pc5Controls *controls, char *out, size_t size);

/* "Cross", "Left stick up": a control's name in PC5. */
const char *pc5_control_name(int control);
/* The choices of Graphics API a game drawing with api (Pc5Api, pc5_pe.h) has, in order:
 * their count, 0 for a game that draws no other way (OpenGL, Vulkan, Windows' own drawing). */
int pc5_graphics_choices(int api, Pc5Graphics choices[PC5_GRAPHICS_COUNT]);
/* The choice as the game menu shows it: "Auto", "WineD3D", "WineD3D on Vulkan", "Wine's VKD3D". */
const char *pc5_graphics_name(Pc5Graphics graphics, int api);
/* "Xbox controller", "DualSense", "Keyboard & mouse". */
const char *pc5_pad_mode_name(Pc5PadMode mode);
/* A stick's use: "Keys", "Mouse pointer", "Nothing". */
const char *pc5_stick_use_name(Pc5StickUse use);
/* The key a control sends, by index in pc5_keys; the label of one. */
const char *pc5_key_label(int key);
/* A stick's next use (step 1 or -1); the other stick stops being the pointer when
 * this one becomes it. */
void pc5_controls_next_stick(Pc5Controls *controls, int stick, int step);
/* The slow button's next choice (step 1 or -1): none, then each button in turn. */
void pc5_controls_next_slow(Pc5Controls *controls, int step);
/* The pointer speed in pixels a second at full tilt. */
int pc5_controls_pixels(const Pc5Controls *controls);

#endif
