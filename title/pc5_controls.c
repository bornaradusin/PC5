/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: a game's controller (pc5_controls.h). */
#include "pc5_controls.h"
#include "pc5_pe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* In the order the key picker shows them. */
const Pc5Key pc5_keys[] = {
    { "mouse_left", "Left click" }, { "mouse_right", "Right click" }, { "mouse_middle", "Middle click" },
    { "mouse_wheel_up", "Wheel up" }, { "mouse_wheel_down", "Wheel down" },
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

/* controls.txt's names for a game's Graphics API (Pc5Graphics). */
static const char *const graphics_keys[PC5_GRAPHICS_COUNT] = { "auto", "wine", "wine_vulkan" };

int pc5_graphics_choices(int api, Pc5Graphics choices[PC5_GRAPHICS_COUNT])
{
    int n = 0;
    if (api == PC5_API_DDRAW || (api >= PC5_API_D3D8 && api <= PC5_API_D3D11)) {
        choices[n++] = PC5_GRAPHICS_AUTO;
        /* For DirectDraw, PC5's choice is already WineD3D on OpenGL. */
        if (api != PC5_API_DDRAW) choices[n++] = PC5_GRAPHICS_WINE;
        choices[n++] = PC5_GRAPHICS_WINE_VULKAN;
    } else if (api == PC5_API_D3D12) {
        choices[n++] = PC5_GRAPHICS_AUTO;
        choices[n++] = PC5_GRAPHICS_WINE;
    }
    return n;
}

const char *pc5_graphics_name(Pc5Graphics graphics, int api)
{
    if (graphics == PC5_GRAPHICS_AUTO) return "Auto";
    if (api == PC5_API_D3D12) return "Wine's VKD3D";
    return graphics == PC5_GRAPHICS_WINE_VULKAN ? "WineD3D on Vulkan" : "WineD3D";
}

void pc5_controls_default(Pc5Controls *controls)
{
    memset(controls, 0, sizeof(*controls));
    controls->mode = PC5_PAD_XBOX;
    controls->stick[0] = PC5_STICK_KEYS;
    controls->stick[1] = PC5_STICK_POINTER;
    controls->speed = 4;
    controls->slow_button = -1;
    controls->slow_percent = PC5_SLOW_DEFAULT;
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
        } else if (!strcmp(name, "aspect_ratio")) {
            if (!strcmp(value, "stretch")) controls->stretch = 1;
            else if (!strcmp(value, "keep")) controls->stretch = 0;
        } else if (!strcmp(name, "memory")) {
            /* "memory_32bit" before 1.9.8 said 4gb for every game left as it was (then the
             * default): it is left out, so such a game is Auto now. */
            if (!strcmp(value, "4gb")) controls->memory_4gb = 1;
            else if (!strcmp(value, "auto")) controls->memory_4gb = 0;
        } else if (!strcmp(name, "video_memory")) {
            if (!strcmp(value, "full")) controls->video_memory_full = 1;
            else if (!strcmp(value, "auto")) controls->video_memory_full = 0;
        } else if (!strcmp(name, "cpu_sharing")) {
            if (!strcmp(value, "on")) controls->cpu_sharing = 1;
            else if (!strcmp(value, "off")) controls->cpu_sharing = 0;
        } else if (!strcmp(name, "graphics_api")) {
            for (int i = 0; i < PC5_GRAPHICS_COUNT; i++)
                if (!strcmp(value, graphics_keys[i])) controls->graphics = (Pc5Graphics)i;
        } else if (!strcmp(name, "slow_button")) {
            controls->slow_button = -1;
            for (int i = 0; i < PC5_BUTTONS; i++)
                if (!strcmp(value, control_keys[i])) controls->slow_button = i;
        } else if (!strcmp(name, "slow_percent")) {
            int percent = atoi(value);
            if (percent >= PC5_SLOW_STEP && percent <= PC5_SLOW_MAX && percent % PC5_SLOW_STEP == 0)
                controls->slow_percent = percent;
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
                                   "# The button that slows the pointer while held, and by how much (%%)\n"
                                   "slow_button = %s\nslow_percent = %d\n"
                                   "# Triangle on the game, then Frame generation\nframe_generation = %s\n"
                                   "# Triangle on a 32-bit game, then Memory: auto (as Windows) or 4gb\nmemory = %s\n"
                                   "# Triangle on a 32-bit game, then Video memory: auto (2 GB at most) or full\n"
                                   "video_memory = %s\n"
                                   "# Triangle on the game, then Resolution\nresolution = %s\n"
                                   "# Triangle on the game, then Aspect ratio\naspect_ratio = %s\n"
                                   "# Triangle on the game, then CPU sharing\ncpu_sharing = %s\n"
                                   "# Triangle on the game, then Graphics API\ngraphics_api = %s\n",
                            mode_keys[controls->mode % PC5_PAD_MODES], stick_keys[controls->stick[0] % PC5_STICK_USES],
                            stick_keys[controls->stick[1] % PC5_STICK_USES], controls->speed,
                            controls->slow_button >= 0 && controls->slow_button < PC5_BUTTONS
                                ? control_keys[controls->slow_button] : "none",
                            controls->slow_percent,
                            controls->frame_generation ? "on" : "off", controls->memory_4gb ? "4gb" : "auto",
                            controls->video_memory_full ? "full" : "auto",
                            controls->resolution == 2 ? "2160p" : controls->resolution == 1 ? "1440p" : "1080p",
                            controls->stretch ? "stretch" : "keep", controls->cpu_sharing ? "on" : "off",
                            graphics_keys[controls->graphics % PC5_GRAPHICS_COUNT]);
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

    if (controls->mode != PC5_PAD_KEYBOARD) {
        status = pc5_buf_printf(&text, "[input]\nmode = %s\n", controls->mode == PC5_PAD_DUALSENSE ? "dualsense" : "xinput");
    } else {
        status = pc5_buf_printf(&text, "[input]\nmode = keyboard\nmouse = %s\nmouse_speed = %d\n", mouse,
                                pc5_controls_pixels(controls));
        /* The button that slows the pointer, while a stick moves it (prospero-win's mouse_slow). */
        if (!status && strcmp(mouse, "none") && controls->slow_button >= 0 && controls->slow_button < PC5_BUTTONS)
            status = pc5_buf_printf(&text, "mouse_slow = %s\nmouse_slow_percent = %d\n",
                                    control_keys[controls->slow_button], controls->slow_percent);
        for (int i = 0; i < PC5_CONTROLS && !status; i++) {
            /* A stick's directions only while it sends keys. */
            if (i >= PC5_STICK_FIRST && controls->stick[(i - PC5_STICK_FIRST) / 4] != PC5_STICK_KEYS) continue;
            status = pc5_buf_printf(&text, "%s = %s\n", control_keys[i], pc5_keys[controls->key[i] % pc5_key_count].name);
        }
    }
    /* The console runs threads of one priority each on a CPU of its own until it waits, where Windows
     * gives them turns: a game with more busy threads than its 13 CPUs can stop for seconds. prospero-win's
     * thread scheduling (Wine patch 0882) gives them turns. */
    if (!status && controls->cpu_sharing) status = pc5_buf_printf(&text, "\n[runtime]\nthread_scheduling = true\n");
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

void pc5_controls_next_slow(Pc5Controls *controls, int step)
{
    /* -1 (none), 0..PC5_BUTTONS - 1 in turn. */
    int next = controls->slow_button + 1 + (step < 0 ? -1 : 1);
    next = (next % (PC5_BUTTONS + 1) + PC5_BUTTONS + 1) % (PC5_BUTTONS + 1);
    controls->slow_button = next - 1;
}

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
