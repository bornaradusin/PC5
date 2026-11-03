/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: a game's controller (pc5_controls.h). */
#include "pc5_controls.h"
#include "pc5_i18n.h"
#include "pc5_pe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* In the order the key picker shows them. */
const Pc5Key pc5_keys[] = {
    { "mouse_left", N_("Left click") }, { "mouse_right", N_("Right click") }, { "mouse_middle", N_("Middle click") },
    { "mouse_wheel_up", N_("Wheel up") }, { "mouse_wheel_down", N_("Wheel down") },
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
    { "up", N_("Up arrow") }, { "down", N_("Down arrow") }, { "left", N_("Left arrow") }, { "right", N_("Right arrow") },
    { "insert", "Insert" }, { "delete", "Delete" }, { "home", "Home" }, { "end", "End" }, { "pageup", "Page up" },
    { "pagedown", "Page down" }, { "pause", "Pause" },
    { "semicolon", ";" }, { "equals", "=" }, { "comma", "," }, { "minus", "-" }, { "period", "." }, { "slash", "/" },
    { "backquote", "`" }, { "lbracket", "[" }, { "backslash", "\\" }, { "rbracket", "]" }, { "quote", "'" },
    { "none", N_("Nothing") },
};
const int pc5_key_count = (int)(sizeof(pc5_keys) / sizeof(pc5_keys[0]));

/* prospero-win's names for the controls, and PC5's. */
static const char *const control_keys[PC5_CONTROLS] = {
    "cross", "circle", "square", "triangle", "l1", "r1", "l2", "r2", "l3", "r3", "up", "down", "left", "right",
    "options", "create", "touchpad",
    "left_stick_up", "left_stick_down", "left_stick_left", "left_stick_right",
    "right_stick_up", "right_stick_down", "right_stick_left", "right_stick_right",
    "touch_left", "touch_right", "swipe_up", "swipe_down", "swipe_left", "swipe_right", "two_finger",
};
static const char *const control_names[PC5_CONTROLS] = {
    N_("Cross"), N_("Circle"), N_("Square"), N_("Triangle"), "L1", "R1", "L2", "R2", N_("L3 (left stick press)"), N_("R3 (right stick press)"),
    N_("D-pad up"), N_("D-pad down"), N_("D-pad left"), N_("D-pad right"), "OPTIONS", N_("Create"), N_("Touchpad"),
    N_("Left stick up"), N_("Left stick down"), N_("Left stick left"), N_("Left stick right"),
    N_("Right stick up"), N_("Right stick down"), N_("Right stick left"), N_("Right stick right"),
    N_("Touchpad left half"), N_("Touchpad right half"), N_("Swipe up"), N_("Swipe down"), N_("Swipe left"),
    N_("Swipe right"), N_("Two-finger click"),
};
/* prospero-win's touchpad_mode values, and PC5's names for them. */
static const char *const touch_mode_keys[3] = { "button", "zones", "mouse" };
static const char *const touch_mode_names[3] = { N_("One button"), N_("Halves and swipes"), N_("Mouse") };
/* The default layout, by key name. */
static const char *const default_keys[PC5_CONTROLS] = {
    "space", "ctrl", "r", "e", "q", "f", "mouse_right", "mouse_left", "shift", "v", "up", "down", "left", "right",
    "escape", "tab", "enter",
    "w", "s", "a", "d",
    "up", "down", "left", "right",
    "enter", "escape", "none", "none", "none", "none", "none",
};
static const char *const mode_keys[PC5_PAD_MODES] = { "xinput", "dualsense", "wheel", "keyboard" };
static const char *const mode_names[PC5_PAD_MODES] = { N_("Xbox controller"), "DualSense", N_("G29 wheel"), N_("Keyboard & mouse") };
static const char *const stick_keys[PC5_STICK_USES] = { "keys", "pointer", "nothing" };
static const char *const stick_names[PC5_STICK_USES] = { N_("Keys"), N_("Mouse pointer"), N_("Nothing") };

static int key_index(const char *name)
{
    for (int i = 0; i < pc5_key_count; i++)
        if (!strcmp(pc5_keys[i].name, name)) return i;
    return -1;
}

/* controls.txt's names for a game's Graphics API (Pc5Graphics). */
static const char *const graphics_keys[PC5_GRAPHICS_COUNT] = { "auto", "wine", "wine_vulkan", "wine_2d" };

int pc5_graphics_choices(int api, Pc5Graphics choices[PC5_GRAPHICS_COUNT])
{
    int n = 0;
    if (api == PC5_API_DDRAW || (api >= PC5_API_D3D8 && api <= PC5_API_D3D11)) {
        /* Auto, Alternative (WineD3D on Vulkan), Compatibility (WineD3D on OpenGL, the slowest).
         * For DirectDraw, PC5's choice is already WineD3D on OpenGL; it can draw in 2D only. */
        choices[n++] = PC5_GRAPHICS_AUTO;
        choices[n++] = PC5_GRAPHICS_WINE_VULKAN;
        if (api != PC5_API_DDRAW) choices[n++] = PC5_GRAPHICS_WINE;
        if (api == PC5_API_DDRAW) choices[n++] = PC5_GRAPHICS_WINE_2D;
    } else if (api == PC5_API_D3D12) {
        choices[n++] = PC5_GRAPHICS_AUTO;
        choices[n++] = PC5_GRAPHICS_WINE;
    }
    return n;
}

/* The menu's names: plain words, as a player picks them. */
const char *pc5_graphics_name(Pc5Graphics graphics, int api)
{
    if (graphics == PC5_GRAPHICS_AUTO) return N_("Auto");
    if (api == PC5_API_D3D12) return N_("Alternative");
    if (graphics == PC5_GRAPHICS_WINE_2D) return N_("2D only");
    return graphics == PC5_GRAPHICS_WINE_VULKAN ? N_("Alternative") : N_("Compatibility (slow)");
}

/* What each one is, for logs, reports and the web page: the names a log is read by. */
const char *pc5_graphics_tech_name(Pc5Graphics graphics, int api)
{
    if (graphics == PC5_GRAPHICS_AUTO) return "Auto";
    if (api == PC5_API_D3D12) return "Wine's VKD3D";
    if (graphics == PC5_GRAPHICS_WINE_2D) return "WineD3D 2D only";
    return graphics == PC5_GRAPHICS_WINE_VULKAN ? "WineD3D on Vulkan" : "WineD3D";
}

/* Graphics memory's sizes as the file says them. */
static const struct { const char *key; int mb; } memory_keys[] = {
    { "512mb", 512 }, { "1gb", 1024 }, { "2gb", 2048 }, { "3gb", 3072 }, { "5gb", 5120 }, { "6gb", 6144 },
};

int pc5_video_memory_sizes(int bits32, const int **sizes)
{
    /* A 32-bit game: Auto's 2 GB is what an older game's check takes; 3 GB, past a signed 32-bit
     * number, for one that wants more. A 64-bit one: Auto's 4 GB leaves room for both. */
    static const int sizes32[] = { 512, 1024, 0, 3072, -1 }, sizes64[] = { 2048, 3072, 0, 5120, 6144, -1 };
    *sizes = bits32 ? sizes32 : sizes64;
    return bits32 ? (int)(sizeof(sizes32) / sizeof(sizes32[0])) : (int)(sizeof(sizes64) / sizeof(sizes64[0]));
}

int pc5_video_memory_level(const Pc5Controls *controls, int bits32)
{
    const int *sizes;
    int count = pc5_video_memory_sizes(bits32, &sizes), want = controls->video_memory_full ? -1 : controls->video_memory_mb;
    for (int i = 0; i < count; i++)
        if (sizes[i] == want) return i;
    for (int i = 0; i < count; i++)
        if (!sizes[i]) return i;
    return 0;
}

void pc5_video_memory_set(Pc5Controls *controls, int bits32, int level)
{
    const int *sizes;
    int count = pc5_video_memory_sizes(bits32, &sizes);
    level = level < 0 ? 0 : level >= count ? count - 1 : level;
    controls->video_memory_full = sizes[level] < 0;
    controls->video_memory_mb = sizes[level] > 0 ? sizes[level] : 0;
}

const char *pc5_video_memory_key(const Pc5Controls *controls)
{
    if (controls->video_memory_full) return "full";
    for (size_t i = 0; i < sizeof(memory_keys) / sizeof(memory_keys[0]); i++)
        if (memory_keys[i].mb == controls->video_memory_mb) return memory_keys[i].key;
    return "auto";
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

/* The player's own mapping (Button mapping › Save as my mapping), for every game:
 * <root>/pc5/mapping.txt, in controls.txt's words. */
static int mapping_path(const char *root, char *out, size_t size)
{
    return pc5_format(out, size, "%s/pc5/mapping.txt", root);
}

static void controls_parse(char *data, Pc5Controls *controls);

/* The mapping's own fields (the sticks, the pointer and the keys) from one controls into another. */
static void mapping_copy(Pc5Controls *to, const Pc5Controls *from)
{
    to->stick[0] = from->stick[0];
    to->stick[1] = from->stick[1];
    to->speed = from->speed;
    to->slow_button = from->slow_button;
    to->slow_percent = from->slow_percent;
    to->touch_mode = from->touch_mode;
    memcpy(to->key, from->key, sizeof(to->key));
}

int pc5_controls_mapping_load(const char *root, Pc5Controls *controls)
{
    char path[PC5_PATH];
    Pc5Buf text = { 0 };
    Pc5Controls saved;

    if (mapping_path(root, path, sizeof(path)) || pc5_read_file(path, &text, 16 * 1024) || pc5_buf_terminate(&text)) {
        pc5_buf_free(&text);
        return -1;
    }
    pc5_controls_default(&saved);
    controls_parse((char *)text.data, &saved);
    pc5_buf_free(&text);
    mapping_copy(controls, &saved);
    return 0;
}

int pc5_controls_mapping_save(const char *root, const Pc5Controls *controls)
{
    char path[PC5_PATH];
    Pc5Buf text = { 0 };
    int status;

    if (pc5_format(path, sizeof(path), "%s/pc5", root) || pc5_mkdirs(path) || mapping_path(root, path, sizeof(path)))
        return -1;
    status = pc5_buf_printf(&text, "# Your own button mapping in PC5 (a game's Button mapping, then Save as my mapping):\n"
                                   "# new games start with it, and Use my mapping puts it on a game.\n"
                                   "left_stick = %s\nright_stick = %s\npointer_speed = %d\nslow_button = %s\nslow_percent = %d\n"
                                   "touchpad_mode = %s\n",
                            stick_keys[controls->stick[0] % PC5_STICK_USES], stick_keys[controls->stick[1] % PC5_STICK_USES],
                            controls->speed,
                            controls->slow_button >= 0 && controls->slow_button < PC5_BUTTONS
                                ? control_keys[controls->slow_button] : "none",
                            controls->slow_percent,
                            touch_mode_keys[controls->touch_mode >= 0 && controls->touch_mode < 3 ? controls->touch_mode : 0]);
    for (int i = 0; i < PC5_CONTROLS && !status; i++)
        status = pc5_buf_printf(&text, "%s = %s\n", control_keys[i], pc5_keys[controls->key[i] % pc5_key_count].name);
    if (!status) status = pc5_write_file(path, text.data, text.len);
    pc5_buf_free(&text);
    return status;
}

int pc5_controls_mapping_exists(const char *root)
{
    char path[PC5_PATH];
    return !mapping_path(root, path, sizeof(path)) && pc5_is_file(path);
}

static void controls_parse(char *data, Pc5Controls *controls);

/* controls.txt's lines (a recipe's) over controls, as a load reads them. */
void pc5_controls_apply_text(const char *text, Pc5Controls *controls)
{
    char copy[1024];
    if ((size_t)snprintf(copy, sizeof(copy), "%s", text) >= sizeof(copy)) return;
    controls_parse(copy, controls);
}

int pc5_controls_load(const char *root, const char *id, Pc5Controls *controls)
{
    char path[PC5_PATH];
    Pc5Buf text = { 0 };

    pc5_controls_default(controls);
    if (controls_path(root, id, path, sizeof(path)) || pc5_read_file(path, &text, 16 * 1024) || pc5_buf_terminate(&text)) {
        pc5_buf_free(&text);
        /* A game set up for the first time starts with the player's own mapping. */
        (void)pc5_controls_mapping_load(root, controls);
        return -1;
    }
    controls_parse((char *)text.data, controls);
    pc5_buf_free(&text);
    return 0;
}

static void controls_parse(char *data, Pc5Controls *controls)
{
    for (char *line = data, *next; line && *line; line = next) {
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
            else if (!strcmp(value, "mihawkgen")) controls->frame_generation = 2;
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
            if (!strcmp(value, "full")) controls->video_memory_full = 1, controls->video_memory_mb = 0;
            else if (!strcmp(value, "auto")) controls->video_memory_full = 0, controls->video_memory_mb = 0;
            else
                for (size_t i = 0; i < sizeof(memory_keys) / sizeof(memory_keys[0]); i++)
                    if (!strcmp(value, memory_keys[i].key))
                        controls->video_memory_full = 0, controls->video_memory_mb = memory_keys[i].mb;
        } else if (!strcmp(name, "cpu_sharing")) {
            if (!strcmp(value, "on")) controls->cpu_sharing = 1;
            else if (!strcmp(value, "off")) controls->cpu_sharing = 0;
        } else if (!strcmp(name, "speakers")) {
            if (!strcmp(value, "2")) controls->surround = 0;
            else if (!strcmp(value, "6")) controls->surround = 1;
            else if (!strcmp(value, "8")) controls->surround = 2;
        } else if (!strcmp(name, "touchpad_mode")) {
            for (int i = 0; i < 3; i++)
                if (!strcmp(value, touch_mode_keys[i])) controls->touch_mode = i;
        } else if (!strcmp(name, "mouse_pointer")) {
            if (!strcmp(value, "hides")) controls->pointer_hide = 1;
            else if (!strcmp(value, "shown")) controls->pointer_hide = 0;
        } else if (!strcmp(name, "steam_emulator")) {
            if (!strcmp(value, "off")) controls->steam_off = 1;
            else if (!strcmp(value, "auto")) controls->steam_off = 0;
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
                                   "# Button mapping, then Touchpad: button (one button), zones (halves and swipes) or mouse\n"
                                   "touchpad_mode = %s\n"
                                   "# Triangle on the game, then Frame generation: off, on (60 -> 120, or 30 -> 60 at 60 Hz) or mihawkgen (40 -> 120)\nframe_generation = %s\n"
                                   "# Triangle on a 32-bit game, then Game memory: auto (as Windows) or 4gb\nmemory = %s\n"
                                   "# Triangle on the game, then Graphics memory: auto, full (Maximum), or 2gb, 3gb, 5gb, 6gb (a 64-bit game), 512mb, 1gb, 3gb (a 32-bit one)\n"
                                   "video_memory = %s\n"
                                   "# Triangle on the game, then Resolution\nresolution = %s\n"
                                   "# Triangle on the game, then Aspect ratio\naspect_ratio = %s\n"
                                   "# Triangle on the game, then Stutter fix\ncpu_sharing = %s\n"
                                   "# Triangle on the game, then Graphics mode: auto, wine_vulkan (Alternative), wine (Compatibility) or wine_2d (2D only)\ngraphics_api = %s\n"
                                   "# Triangle on the game, then Manage, then Steam emulator: auto or off\n"
                                   "steam_emulator = %s\n"
                                   "# Triangle on the game, then Controller: the mouse pointer shown, or hides when unused\n"
                                   "mouse_pointer = %s\n"
                                   "# Triangle on the game, then Sound output: 2 (stereo), 6 (5.1) or 8 (7.1)\nspeakers = %s\n",
                            mode_keys[controls->mode % PC5_PAD_MODES], stick_keys[controls->stick[0] % PC5_STICK_USES],
                            stick_keys[controls->stick[1] % PC5_STICK_USES], controls->speed,
                            controls->slow_button >= 0 && controls->slow_button < PC5_BUTTONS
                                ? control_keys[controls->slow_button] : "none",
                            controls->slow_percent, touch_mode_keys[controls->touch_mode >= 0 && controls->touch_mode < 3 ? controls->touch_mode : 0],
                            controls->frame_generation == 2 ? "mihawkgen" : controls->frame_generation ? "on" : "off",
                            controls->memory_4gb ? "4gb" : "auto",
                            pc5_video_memory_key(controls),
                            controls->resolution == 2 ? "2160p" : controls->resolution == 1 ? "1440p" : "1080p",
                            controls->stretch ? "stretch" : "keep", controls->cpu_sharing ? "on" : "off",
                            graphics_keys[controls->graphics % PC5_GRAPHICS_COUNT], controls->steam_off ? "off" : "auto",
                            controls->pointer_hide ? "hides" : "shown",
                            controls->surround == 2 ? "8" : controls->surround == 1 ? "6" : "2");
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
        status = pc5_buf_printf(&text, "[input]\nmode = %s\n", controls->mode == PC5_PAD_DUALSENSE ? "dualsense" :
                                controls->mode == PC5_PAD_WHEEL ? "wheel" : "xinput");
    } else {
        status = pc5_buf_printf(&text, "[input]\nmode = keyboard\nmouse = %s\nmouse_speed = %d\n", mouse,
                                pc5_controls_pixels(controls));
        /* The button that slows the pointer, while a stick moves it (prospero-win's mouse_slow). */
        if (!status && strcmp(mouse, "none") && controls->slow_button >= 0 && controls->slow_button < PC5_BUTTONS)
            status = pc5_buf_printf(&text, "mouse_slow = %s\nmouse_slow_percent = %d\n",
                                    control_keys[controls->slow_button], controls->slow_percent);
        /* The touchpad: one button (prospero-win's default, so said only when it is something else). */
        if (!status && controls->touch_mode > 0 && controls->touch_mode < 3)
            status = pc5_buf_printf(&text, "touchpad_mode = %s\n", touch_mode_keys[controls->touch_mode]);
        for (int i = 0; i < PC5_CONTROLS && !status; i++) {
            const char *key = pc5_keys[controls->key[i] % pc5_key_count].name;
            /* A stick's directions only while it sends keys. */
            if (i >= PC5_STICK_FIRST && i < PC5_STICK_FIRST + PC5_STICK_CONTROLS &&
                controls->stick[(i - PC5_STICK_FIRST) / 4] != PC5_STICK_KEYS)
                continue;
            /* The halves and swipes only with Halves and swipes; a two-finger click only when it is a
             * key (Nothing leaves it to PC5's on-screen keyboard). */
            if (i >= PC5_TOUCH_FIRST && i < PC5_TOUCH_FIRST + PC5_TOUCH_ZONES && controls->touch_mode != 1) continue;
            if (i == PC5_TWO_FINGER && !strcmp(key, "none")) continue;
            status = pc5_buf_printf(&text, "%s = %s\n", control_keys[i], key);
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
const char *pc5_touch_mode_name(int mode) { return touch_mode_names[mode >= 0 && mode < 3 ? mode : 0]; }
const char *pc5_stick_use_name(Pc5StickUse use) { return stick_names[use % PC5_STICK_USES]; }
const char *pc5_key_label(int key) { return key >= 0 && key < pc5_key_count ? pc5_keys[key].label : ""; }

const char *pc5_key_text(int key)
{
    /* Only the names marked for translation (the mouse's, the arrows, Nothing): a keyboard
     * key's name is the one on the key, and "Space" read through the catalogue would come
     * back as free space ("容量"). */
    const char *name = key >= 0 && key < pc5_key_count ? pc5_keys[key].name : "";
    if (!strncmp(name, "mouse_", 6) || !strcmp(name, "up") || !strcmp(name, "down") || !strcmp(name, "left") ||
        !strcmp(name, "right") || !strcmp(name, "none"))
        return T(pc5_key_label(key));
    return pc5_key_label(key);
}

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
