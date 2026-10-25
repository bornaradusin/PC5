/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: a game's controller (pc5_controls.h). */
#include "pc5_controls.h"

#include <stdio.h>
#include <string.h>

/* In the order the key picker shows them. */
const Pc5Key pc5_keys[] = {
    { "mouse_left", "Left click" }, { "mouse_right", "Right click" }, { "mouse_middle", "Middle click" },
    { "a", "A" }, { "b", "B" }, { "c", "C" }, { "d", "D" }, { "e", "E" }, { "f", "F" }, { "g", "G" }, { "h", "H" },
    { "i", "I" }, { "j", "J" }, { "k", "K" }, { "l", "L" }, { "m", "M" }, { "n", "N" }, { "o", "O" }, { "p", "P" },
    { "q", "Q" }, { "r", "R" }, { "s", "S" }, { "t", "T" }, { "u", "U" }, { "v", "V" }, { "w", "W" }, { "x", "X" },
    { "y", "Y" }, { "z", "Z" },
    { "1", "1" }, { "2", "2" }, { "3", "3" }, { "4", "4" }, { "5", "5" }, { "6", "6" }, { "7", "7" }, { "8", "8" },
    { "9", "9" }, { "0", "0" },
    { "f1", "F1" }, { "f2", "F2" }, { "f3", "F3" }, { "f4", "F4" }, { "f5", "F5" }, { "f6", "F6" }, { "f7", "F7" },
    { "f8", "F8" }, { "f9", "F9" }, { "f10", "F10" }, { "f11", "F11" }, { "f12", "F12" },
    { "space", "Space" }, { "enter", "Enter" }, { "escape", "Esc" }, { "tab", "Tab" }, { "backspace", "Backspace" },
    { "shift", "Shift" }, { "ctrl", "Ctrl" }, { "alt", "Alt" },
    { "up", "Up arrow" }, { "down", "Down arrow" }, { "left", "Left arrow" }, { "right", "Right arrow" },
    { "insert", "Insert" }, { "delete", "Delete" }, { "home", "Home" }, { "end", "End" }, { "pageup", "Page up" },
    { "pagedown", "Page down" }, { "pause", "Pause" },
    { "semicolon", ";" }, { "equals", "=" }, { "comma", "," }, { "minus", "-" }, { "period", "." }, { "slash", "/" },
    { "backquote", "`" }, { "lbracket", "[" }, { "backslash", "\\" }, { "rbracket", "]" }, { "quote", "'" },
    { "none", "Nothing" },
};
const int pc5_key_count = (int)(sizeof(pc5_keys) / sizeof(pc5_keys[0]));

/* prospero-win's names for the controls, and PC5's. */
static const char *const control_keys[PC5_CONTROLS] = {
    "cross", "circle", "square", "triangle", "l1", "r1", "l2", "r2", "l3", "r3", "up", "down", "left", "right",
    "options", "create", "touchpad",
    "left_stick_up", "left_stick_down", "left_stick_left", "left_stick_right",
    "right_stick_up", "right_stick_down", "right_stick_left", "right_stick_right",
};
static const char *const control_names[PC5_CONTROLS] = {
    "Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2", "L3 (left stick press)", "R3 (right stick press)",
    "D-pad up", "D-pad down", "D-pad left", "D-pad right", "OPTIONS", "Create", "Touchpad",
    "Left stick up", "Left stick down", "Left stick left", "Left stick right",
    "Right stick up", "Right stick down", "Right stick left", "Right stick right",
};
/* The default layout, by key name. */
static const char *const default_keys[PC5_CONTROLS] = {
    "space", "ctrl", "r", "e", "q", "f", "mouse_right", "mouse_left", "shift", "v", "up", "down", "left", "right",
    "escape", "tab", "enter",
    "w", "s", "a", "d",
    "up", "down", "left", "right",
};
static const char *const mode_keys[PC5_PAD_MODES] = { "xinput", "dualsense", "keyboard" };
static const char *const mode_names[PC5_PAD_MODES] = { "Xbox controller", "DualSense", "Keyboard & mouse" };
static const char *const stick_keys[PC5_STICK_USES] = { "keys", "pointer", "nothing" };
static const char *const stick_names[PC5_STICK_USES] = { "Keys", "Mouse pointer", "Nothing" };

static int key_index(const char *name)
{
    for (int i = 0; i < pc5_key_count; i++)
        if (!strcmp(pc5_keys[i].name, name)) return i;
    return -1;
}

void pc5_controls_default(Pc5Controls *controls)
{
    memset(controls, 0, sizeof(*controls));
    controls->mode = PC5_PAD_XBOX;
    controls->stick[0] = PC5_STICK_KEYS;
    controls->stick[1] = PC5_STICK_POINTER;
    controls->speed = 4;
    for (int i = 0; i < PC5_CONTROLS; i++) controls->key[i] = (uint8_t)key_index(default_keys[i]);
}

static int controls_path(const char *root, const char *id, char *out, size_t size)
{
    return pc5_format(out, size, "%s/prefixes/%s/pc5/controls.txt", root, id);
}

int pc5_controls_load(const char *root, const char *id, Pc5Controls *controls)
{
    char path[PC5_PATH];
    Pc5Buf text = { 0 };

    pc5_controls_default(controls);
    if (controls_path(root, id, path, sizeof(path)) || pc5_read_file(path, &text, 16 * 1024)) {
        pc5_buf_free(&text);
        return -1;
    }
    for (char *line = (char *)text.data, *next; line && *line; line = next) {
        char name[32], value[32];
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        if (line[0] == '#' || sscanf(line, " %31[a-z0-9_] = %31[a-z0-9_]", name, value) != 2) continue;
        if (!strcmp(name, "mode")) {
            for (int i = 0; i < PC5_PAD_MODES; i++)
                if (!strcmp(value, mode_keys[i])) controls->mode = (Pc5PadMode)i;
        } else if (!strcmp(name, "left_stick") || !strcmp(name, "right_stick")) {
            for (int i = 0; i < PC5_STICK_USES; i++)
                if (!strcmp(value, stick_keys[i])) controls->stick[name[0] == 'r'] = (Pc5StickUse)i;
        } else if (!strcmp(name, "frame_generation")) {
            if (!strcmp(value, "on")) controls->frame_generation = 1;
            else if (!strcmp(value, "off")) controls->frame_generation = 0;
        } else if (!strcmp(name, "resolution")) {
            if (!strcmp(value, "1080p")) controls->resolution = 0;
            else if (!strcmp(value, "1440p")) controls->resolution = 1;
            else if (!strcmp(value, "2160p")) controls->resolution = 2;
        } else if (!strcmp(name, "memory_32bit")) {
            if (!strcmp(value, "2gb")) controls->memory_2gb = 1;
            else if (!strcmp(value, "4gb")) controls->memory_2gb = 0;
        } else if (!strcmp(name, "pointer_speed")) {
            int speed = 0, digits = 0;
            for (const char *p = value; *p >= '0' && *p <= '9' && digits < 3; p++, digits++) speed = speed * 10 + *p - '0';
            if (digits && !value[digits] && speed >= 1 && speed <= PC5_SPEEDS) controls->speed = speed;
        } else {
            for (int i = 0; i < PC5_CONTROLS; i++) {
                int key;
                if (strcmp(name, control_keys[i])) continue;
                if ((key = key_index(value)) >= 0) controls->key[i] = (uint8_t)key;
                break;
            }
        }
    }
    /* One pointer: a file with two keeps the right stick's. */
    if (controls->stick[0] == PC5_STICK_POINTER && controls->stick[1] == PC5_STICK_POINTER)
        controls->stick[0] = PC5_STICK_KEYS;
    pc5_buf_free(&text);
    return 0;
}

int pc5_controls_save(const char *root, const char *id, const Pc5Controls *controls)
{
    char path[PC5_PATH];
    Pc5Buf text = { 0 };
    int status;

    if (pc5_format(path, sizeof(path), "%s/prefixes/%s/pc5", root, id) || pc5_mkdirs(path) ||
        controls_path(root, id, path, sizeof(path)))
        return -1;
    status = pc5_buf_printf(&text, "# This game's controller in PC5 (Triangle on the game, then Controller)\n"
                                   "mode = %s\nleft_stick = %s\nright_stick = %s\npointer_speed = %d\n"
                                   "# Triangle on the game, then Frame generation\nframe_generation = %s\n"
                                   "# Triangle on a 32-bit game, then Memory\nmemory_32bit = %s\n"
                                   "# Triangle on the game, then Resolution\nresolution = %s\n",
                            mode_keys[controls->mode % PC5_PAD_MODES], stick_keys[controls->stick[0] % PC5_STICK_USES],
                            stick_keys[controls->stick[1] % PC5_STICK_USES], controls->speed,
                            controls->frame_generation ? "on" : "off", controls->memory_2gb ? "2gb" : "4gb",
                            controls->resolution == 2 ? "2160p" : controls->resolution == 1 ? "1440p" : "1080p");
    for (int i = 0; i < PC5_CONTROLS && !status; i++)
        status = pc5_buf_printf(&text, "%s = %s\n", control_keys[i], pc5_keys[controls->key[i] % pc5_key_count].name);
    if (!status) status = pc5_write_file(path, text.data, text.len);
    pc5_buf_free(&text);
    return status;
}

int pc5_controls_input(const Pc5Controls *controls, char *out, size_t size)
{
    Pc5Buf text = { 0 };
    int status;
    const char *mouse = controls->stick[1] == PC5_STICK_POINTER ? "right_stick"
                        : controls->stick[0] == PC5_STICK_POINTER ? "left_stick" : "none";

    if (controls->mode != PC5_PAD_KEYBOARD)
        return pc5_format(out, size, "[input]\nmode = %s\n", controls->mode == PC5_PAD_DUALSENSE ? "dualsense" : "xinput");
    status = pc5_buf_printf(&text, "[input]\nmode = keyboard\nmouse = %s\nmouse_speed = %d\n", mouse,
                            pc5_controls_pixels(controls));
    for (int i = 0; i < PC5_CONTROLS && !status; i++) {
        /* A stick's directions only while it sends keys. */
        if (i >= PC5_STICK_FIRST && controls->stick[(i - PC5_STICK_FIRST) / 4] != PC5_STICK_KEYS) continue;
        status = pc5_buf_printf(&text, "%s = %s\n", control_keys[i], pc5_keys[controls->key[i] % pc5_key_count].name);
    }
    if (!status) status = pc5_format(out, size, "%.*s", (int)text.len, text.data ? (char *)text.data : "");
    pc5_buf_free(&text);
    return status;
}

const char *pc5_control_name(int control)
{
    return control >= 0 && control < PC5_CONTROLS ? control_names[control] : "";
}

const char *pc5_pad_mode_name(Pc5PadMode mode) { return mode_names[mode % PC5_PAD_MODES]; }
const char *pc5_stick_use_name(Pc5StickUse use) { return stick_names[use % PC5_STICK_USES]; }
const char *pc5_key_label(int key) { return key >= 0 && key < pc5_key_count ? pc5_keys[key].label : ""; }

void pc5_controls_next_stick(Pc5Controls *controls, int stick, int step)
{
    stick = stick ? 1 : 0;
    controls->stick[stick] = (Pc5StickUse)((controls->stick[stick] + PC5_STICK_USES + (step < 0 ? -1 : 1)) % PC5_STICK_USES);
    if (controls->stick[stick] == PC5_STICK_POINTER && controls->stick[!stick] == PC5_STICK_POINTER)
        controls->stick[!stick] = PC5_STICK_KEYS;
}

int pc5_controls_pixels(const Pc5Controls *controls)
{
    int speed = controls->speed < 1 ? 1 : controls->speed > PC5_SPEEDS ? PC5_SPEEDS : controls->speed;
    return speed * 300;
}
