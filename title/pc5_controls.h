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

/* What can be bound: the 17 buttons, then the sticks' directions (left stick up,
 * down, left, right, then the right stick's), in prospero-win's order. */
enum {
    PC5_BUTTONS = 17,
    PC5_CONTROLS = 25,
    PC5_STICK_FIRST = 17,         /* the left stick's up; the right stick's is 21 */
    PC5_SPEEDS = 10,              /* pointer speed levels, 300 pixels a second each at full tilt */
};

typedef struct Pc5Controls {
    Pc5PadMode mode;
    Pc5StickUse stick[2];         /* left, right */
    int speed;                    /* 1..PC5_SPEEDS */
    uint8_t key[PC5_CONTROLS];    /* an index into pc5_keys */
    int frame_generation;         /* 1: the game held at 30 fps, shown at 60 (libvulkan.prx's frame generation) */
    int memory_2gb;               /* 1: a 32-bit game keeps 2 GB of address space, not the 4 GB it gets */
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
/* The profile's [input] section, from its header on. 0, or -1 when out is too
 * small. */
int pc5_controls_input(const Pc5Controls *controls, char *out, size_t size);

/* "Cross", "Left stick up": a control's name in PC5. */
const char *pc5_control_name(int control);
/* "Xbox controller", "DualSense", "Keyboard & mouse". */
const char *pc5_pad_mode_name(Pc5PadMode mode);
/* A stick's use: "Keys", "Mouse pointer", "Nothing". */
const char *pc5_stick_use_name(Pc5StickUse use);
/* The key a control sends, by index in pc5_keys; the label of one. */
const char *pc5_key_label(int key);
/* A stick's next use (step 1 or -1); the other stick stops being the pointer when
 * this one becomes it. */
void pc5_controls_next_stick(Pc5Controls *controls, int stick, int step);
/* The pointer speed in pixels a second at full tilt. */
int pc5_controls_pixels(const Pc5Controls *controls);

#endif
