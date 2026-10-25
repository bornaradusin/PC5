/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: the launcher's screens (pc5_ui.h). */
#include "pc5_ui.h"
#include "pc5_home.h"
#include "pc5_image.h"
#include "pc5_library.h"
#include "pc5_logs.h"
#include "pc5_settings.h"
#include "pc5_source.h"
#include "pc5_update.h"

#include <dirent.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum { W = 1920, H = 1080, COVER_W = 400, COVER_H = 600, FLOOR_Y = 712, MARGIN = 72 };

#define C_TEXT 0xffffffffu
#define C_DIM 0xff9aa3afu
#define C_FAINT 0xff6b7280u
#define C_ICON 0xffd7dce3u
#define C_PANEL 0xff121419u
#define C_EDGE 0xff262a33u
#define C_ROW 0xff1d222bu
#define C_ACCENT 0xff7c9cffu
#define C_WARN 0xfff5b342u
#define C_BAD 0xffff6b6bu
#define C_RED 0xffe5484du
#define C_DEFAULT_TINT 0xff2a3442u

typedef enum Screen {
    S_START, S_COVERS, S_LIBRARY, S_SETTINGS, S_ABOUT, S_SCAN, S_SOURCES, S_READING, S_SEARCH, S_REVIEW,
    S_PROGRAMS, S_INSTALL, S_STOP, S_UNINSTALL_LIST, S_UNINSTALL_CONFIRM, S_REMOVING, S_MESSAGE, S_LAUNCH,
    S_UPDATE, S_CHECKING, S_GAME_MENU, S_GAME_READING, S_QUEUE_READ, S_QUEUE, S_BUTTONS, S_KEYS
} Screen;

static const char *const screen_names[] = { "start", "covers", "library", "settings", "about", "scan", "sources",
                                            "reading", "search", "review", "programs", "install", "stop",
                                            "uninstall-list", "uninstall-confirm", "removing", "message", "launch",
                                            "update", "checking", "game-menu", "game-reading", "queue-read", "queue",
                                            "buttons", "keys" };

/* One game of several being installed in a row. */
typedef enum QueueState { Q_WAITING, Q_INSTALLED, Q_FAILED, Q_STOPPED, Q_SKIPPED } QueueState;

typedef struct QueueGame {
    Pc5Source source;
    Pc5Folder folder;
    int exe;
    Pc5CoverSearch search;
    int choice;                    /* the cover's choice, -1 the card */
    Pc5Image cover;
    Pc5Picture thumb;
    uint32_t tint;
    char cover_source[64];
    Pc5Buf original;               /* the downloaded picture */
    QueueState state;
    char id[PC5_ID], error[PC5_ERROR];
} QueueGame;

/* Settings: its rows, on four pages. */
typedef enum SettingRow {
    SR_INSTALL, SR_UNINSTALL, SR_GAMES, SR_OVERLAY, SR_UPDATES, SR_KEEP_LOGS, SR_ABOUT,
    SR_FPS, SR_FRAME_TIME, SR_CPU_USAGE, SR_CPU_TEMPERATURE, SR_SOC_TEMPERATURE, SR_MEMORY, SR_FPS_CORNER,
    SR_VIBRATION, SR_LIGHT, SR_TRIGGERS, SR_TRIGGER_RUMBLE, SR_TRIGGER_RESISTANCE,
    SR_CHECK_ON_OPEN, SR_CHECK_NOW
} SettingRow;

enum { PAGE_ROWS = 7, PAGE_GAMES = 1, PAGE_UPDATES = 2, PAGE_OVERLAY = 3 };
static const char *const page_titles[4] = { "Settings", "In games", "Updates", "Overlay" };

/* A page's rows: the triggers' strengths only while they are used. */
static int page_rows(const Pc5Settings *set, int page, SettingRow rows[PAGE_ROWS])
{
    int n = 0;
    if (page == PAGE_OVERLAY) {
        rows[n++] = SR_FPS;
        rows[n++] = SR_FRAME_TIME;
        rows[n++] = SR_CPU_USAGE;
        rows[n++] = SR_CPU_TEMPERATURE;
        rows[n++] = SR_SOC_TEMPERATURE;
        rows[n++] = SR_MEMORY;
        rows[n++] = SR_FPS_CORNER;
    } else if (page == PAGE_GAMES) {
        rows[n++] = SR_VIBRATION;
        rows[n++] = SR_LIGHT;
        rows[n++] = SR_TRIGGERS;
        if (set->triggers == PC5_TRIGGERS_RUMBLE || set->triggers == PC5_TRIGGERS_BOTH) rows[n++] = SR_TRIGGER_RUMBLE;
        if (set->triggers == PC5_TRIGGERS_RESISTANCE || set->triggers == PC5_TRIGGERS_BOTH)
            rows[n++] = SR_TRIGGER_RESISTANCE;
    } else if (page == PAGE_UPDATES) {
        rows[n++] = SR_CHECK_ON_OPEN;
        rows[n++] = SR_CHECK_NOW;
    } else {
        rows[n++] = SR_INSTALL;
        rows[n++] = SR_UNINSTALL;
        rows[n++] = SR_GAMES;
        rows[n++] = SR_OVERLAY;
        rows[n++] = SR_UPDATES;
        rows[n++] = SR_KEEP_LOGS;
        rows[n++] = SR_ABOUT;
    }
    return n;
}

/* A game's menu: its rows; Button mapping only while it plays the keyboard and mouse. */
typedef enum GameRow { GR_PLAY, GR_CONTROLLER, GR_BUTTONS, GR_FRAMEGEN, GR_MEMORY, GR_PROGRAM, GR_HOME, GR_UNINSTALL } GameRow;
enum { GAME_ROWS = 8 };

/* bits: the game's, 32 or 64 (Memory is a 32-bit game's). */
static int game_rows(const Pc5Controls *controls, int bits, GameRow rows[GAME_ROWS])
{
    int n = 0;
    rows[n++] = GR_PLAY;
    rows[n++] = GR_CONTROLLER;
    if (controls->mode == PC5_PAD_KEYBOARD) rows[n++] = GR_BUTTONS;
    rows[n++] = GR_FRAMEGEN;
    if (bits == 32) rows[n++] = GR_MEMORY;
    rows[n++] = GR_PROGRAM;
    rows[n++] = GR_HOME;
    rows[n++] = GR_UNINSTALL;
    return n;
}

static int game_row_at(const Pc5Controls *controls, int bits, GameRow row)
{
    GameRow rows[GAME_ROWS];
    int n = game_rows(controls, bits, rows);
    for (int i = 0; i < n; i++)
        if (rows[i] == row) return i;
    return 0;
}

/* Button mapping: the sticks (with their four keys while they send keys), the
 * pointer's speed while a stick moves it, the buttons, and the default layout. */
typedef enum PadRowKind { PR_STICK, PR_DIRECTION, PR_SPEED, PR_BUTTON, PR_RESET } PadRowKind;
typedef struct PadRow { PadRowKind kind; int index; } PadRow;
enum { PAD_ROWS = 32, PAD_VISIBLE = 10, PAD_PITCH = 74, KEY_COLS = 8 };

static int pad_rows(const Pc5Controls *controls, PadRow rows[PAD_ROWS])
{
    int n = 0;
    for (int stick = 0; stick < 2; stick++) {
        rows[n++] = (PadRow){ PR_STICK, stick };
        if (controls->stick[stick] == PC5_STICK_KEYS)
            for (int d = 0; d < 4; d++) rows[n++] = (PadRow){ PR_DIRECTION, PC5_STICK_FIRST + 4 * stick + d };
    }
    if (controls->stick[0] == PC5_STICK_POINTER || controls->stick[1] == PC5_STICK_POINTER)
        rows[n++] = (PadRow){ PR_SPEED, 0 };
    for (int button = 0; button < PC5_BUTTONS; button++) rows[n++] = (PadRow){ PR_BUTTON, button };
    rows[n++] = (PadRow){ PR_RESET, 0 };
    return n;
}

static void percent_text(int percent, char *out, size_t size)
{
    if (!percent) pc5_copy(out, size, "Off");
    else snprintf(out, size, "%d%%", percent);
}

typedef struct Cover {
    Pc5Picture picture;
    uint32_t tint;
    int state;           /* 0 not loaded, 1 loaded */
} Cover;

struct Pc5Ui {
    Pc5UiConfig config;
    char root[PC5_PATH], template_prefix[PC5_PATH];
    const char *const *usb_roots;
    Pc5Platform platform;
    Pc5Fonts fonts;
    Screen screen, back_to;
    uint64_t now, entered, last_frame;
    int drawn;                     /* the screen was drawn since it was entered */
    int dirty;

    Pc5Library library;
    Cover covers[PC5_MAX_GAMES];
    int selected;
    float scroll;
    float glow[3], glow_target[3];
    char clock[32];
    uint64_t clock_checked;

    /* held directions: when they repeat next */
    uint32_t repeat_key;
    uint64_t repeat_at, repeat_since;

    int menu;                      /* the row chosen on a list or menu */
    int list_top;
    int button;                    /* the button chosen in a dialog */

    Pc5Source sources[PC5_SOURCES];
    int source_count;
    Pc5Folder folder;
    int exe;
    Pc5CoverSearch search;
    int choice;                    /* -1: the card */
    int choice_loaded;             /* the review cover matches choice */
    Pc5Picture review;
    Pc5Image review_image;
    uint32_t review_tint;
    char review_source[64];
    int fetching;                  /* a new review cover is being fetched */

    Pc5Install install;
    int installing;
    uint64_t speed_at, speed_bytes;
    double speed;
    Pc5Remove remove;
    char removing_title[PC5_NAME];

    /* startup: finishing leftovers, then covers for the games without one */
    int cover_queue[PC5_MAX_GAMES], cover_count, cover_at, cover_searching;

    char message_title[160], message_body[700];
    int message_good;
    char launch_id[PC5_ID];
    char start[PC5_ID];               /* a home screen tile's game, until it is started */
    int removing_tiles;               /* the game being uninstalled had a tile, deleted with it */
    char tile[PC5_HOME_ID];           /* the game menu's game's home screen tile, or "" */
    char select_after[PC5_ID];

    Pc5Settings settings;
    int page;                      /* Settings: 0 its first page, 1 In games, 2 Updates */
    Pc5Controls controls;          /* the controller of the game in launch_id (its menu) */
    int pad_row, pad_top;          /* Button mapping: the row chosen, the first one shown */
    int key_control, key_choice;   /* the key picker: for which control, the key chosen */
    Pc5UpdateMemory update;
    Pc5LogIssue issue;             /* what went wrong last time, told once the library shows */
    int issue_pending;
    Pc5Buf review_original;        /* the review cover's downloaded picture */
    Pc5InstallPhase speed_phase;
    int program_for;               /* S_PROGRAMS for: 0 the review, 1 the game in launch_id */
    Screen uninstall_back;         /* where Cancel in S_UNINSTALL_CONFIRM goes */

    /* several games installed in a row */
    uint8_t ticked[PC5_SOURCES];
    QueueGame *queue;
    int queue_count, queue_at, queue_phase, queue_review, queue_installing, queue_stop_all;

    uint8_t *falloff;              /* the glow's shape, 0-255 per pixel */
    uint32_t *backdrop;            /* the background without the glow */
    uint32_t *composed;            /* with the glow, for composed_glow */
    uint32_t composed_glow;
    int composed_ready;
};

/* The bits of the game whose menu is open (64 when it isn't found). */
static int ui_game_bits(const Pc5Ui *ui)
{
    int index = pc5_library_find(&ui->library, ui->launch_id);
    return index >= 0 && ui->library.games[index].bits == 32 ? 32 : 64;
}

const char *pc5_ui_screen(const Pc5Ui *ui)
{
    return screen_names[ui->screen];
}

static void say(Pc5Ui *ui, const char *format, ...) __attribute__((format(printf, 2, 3)));
static void say(Pc5Ui *ui, const char *format, ...)
{
    char line[600];
    va_list args;
    if (!ui->platform.log) return;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    ui->platform.log(ui->platform.context, line);
}

static void go(Pc5Ui *ui, Screen screen)
{
    ui->screen = screen;
    ui->entered = ui->now;
    ui->drawn = 0;
    ui->dirty = 1;
    say(ui, "PC5 screen=%s", screen_names[screen]);
}

static void message(Pc5Ui *ui, int good, const char *title, const char *body, Screen back_to)
{
    pc5_copy(ui->message_title, sizeof(ui->message_title), title);
    pc5_copy(ui->message_body, sizeof(ui->message_body), body);
    ui->message_good = good;
    ui->back_to = back_to;
    go(ui, S_MESSAGE);
}

/* ---- covers ---------------------------------------------------------------------------- */

static void covers_clear(Pc5Ui *ui)
{
    for (int i = 0; i < PC5_MAX_GAMES; i++) {
        pc5_picture_free(&ui->covers[i].picture);
        ui->covers[i] = (Cover){ 0 };
    }
}

/* A Windows path of a game ("C:\Games\X\x.exe") on the console, found
 * without regard to case. */
static int console_path(const char *root, const char *prefix, const char *windows, char *out, size_t size)
{
    char path[PC5_PATH], part[PC5_NAME * 2];
    const char *at;

    if (!prefix[0] || strlen(windows) < 4 || pc5_lower((unsigned char)windows[0]) != 'c' || windows[1] != ':')
        return -1;
    if (strcmp(prefix, "default") ? pc5_format(path, sizeof(path), "%s/prefixes/%s/drive_c", root, prefix)
                                  : pc5_format(path, sizeof(path), "%s/prefix/drive_c", root))
        return -1;
    at = windows + 3;
    while (*at) {
        const char *slash = strchr(at, '\\');
        size_t length = slash ? (size_t)(slash - at) : strlen(at);
        char next[PC5_PATH];
        DIR *dir;
        struct dirent *entry;
        int found = 0;
        if (!length || length >= sizeof(part)) return -1;
        memcpy(part, at, length);
        part[length] = 0;
        if (!pc5_join(next, sizeof(next), path, part) && pc5_exists(next)) found = 1;
        else if ((dir = opendir(path))) {
            while (!found && (entry = readdir(dir)))
                if (!pc5_casecmp(entry->d_name, part) && !pc5_join(next, sizeof(next), path, entry->d_name)) found = 1;
            closedir(dir);
        }
        if (!found) return -1;
        memcpy(path, next, sizeof(path));
        at += length + (slash != NULL);
    }
    return pc5_format(out, size, "%s", path);
}

static int game_program(Pc5Ui *ui, const Pc5Game *game, char *out, size_t size)
{
    return console_path(ui->root, game->prefix, game->executable, out, size);
}

static int cover_path(Pc5Ui *ui, const Pc5Game *game, char *out, size_t size)
{
    if (!game->prefix[0] || !strcmp(game->prefix, "default")) return -1;
    return pc5_format(out, size, "%s/prefixes/%s/pc5/cover.bin", ui->root, game->prefix);
}

static void card_for(Pc5Ui *ui, const Pc5Game *game, Pc5Image *card, uint32_t *tint)
{
    char program[PC5_PATH];
    Pc5Image icon = { 0 };

    if (!game_program(ui, game, program, sizeof(program))) pc5_pe_icon(program, &icon);
    if (pc5_make_card(&ui->fonts, game->title, icon.px ? &icon : NULL, card, tint)) *card = (Pc5Image){ 0 };
    pc5_image_free(&icon);
}

static void cover_load(Pc5Ui *ui, int index)
{
    Cover *cover = &ui->covers[index];
    const Pc5Game *game = &ui->library.games[index];
    char path[PC5_PATH];
    Pc5Image image = { 0 };

    cover->state = 1;
    cover->tint = C_DEFAULT_TINT;
    if (cover_path(ui, game, path, sizeof(path)) || pc5_cover_load(path, &image, &cover->tint))
        card_for(ui, game, &image, &cover->tint);
    if (image.px) pc5_picture_make(&cover->picture, &image);
    pc5_image_free(&image);
}

/* The covers near the chosen game, two a frame. */
static int covers_near(Pc5Ui *ui)
{
    int loaded = 0;

    for (int d = 0; d <= 6 && loaded < 2; d++)
        for (int side = -1; side <= 1 && loaded < 2; side += 2) {
            int i = ui->selected + d * side;
            if (i < 0 || i >= ui->library.count || ui->covers[i].state) continue;
            cover_load(ui, i);
            loaded++;
            if (!d) break;
        }
    /* Far ones are let go. */
    for (int i = 0; i < ui->library.count; i++)
        if (ui->covers[i].state && abs(i - ui->selected) > 14) {
            pc5_picture_free(&ui->covers[i].picture);
            ui->covers[i].state = 0;
        }
    return loaded;
}

/* ---- the library ------------------------------------------------------------------------ */

static void library_reload(Pc5Ui *ui, const char *select_id)
{
    char keep[PC5_ID] = "";

    if (select_id) pc5_copy(keep, sizeof(keep), select_id);
    else if (ui->selected >= 0 && ui->selected < ui->library.count)
        pc5_copy(keep, sizeof(keep), ui->library.games[ui->selected].id);
    covers_clear(ui);
    pc5_library_load(ui->root, &ui->library);
    ui->selected = 0;
    if (keep[0]) {
        int found = pc5_library_find(&ui->library, keep);
        if (found >= 0) ui->selected = found;
    }
    ui->scroll = (float)ui->selected;
    say(ui, "PC5 library games=%d leftovers=%d", ui->library.count, ui->library.leftovers);
}

static void last_played(Pc5Ui *ui, char *id, size_t size)
{
    char path[PC5_PATH];
    Pc5Buf text = { 0 };

    id[0] = 0;
    if (!pc5_format(path, sizeof(path), "%s/pc5/last.txt", ui->root) && !pc5_read_file(path, &text, 256)) {
        size_t n = 0;
        while (n < text.len && n + 1 < size && text.data[n] > ' ') n++;
        memcpy(id, text.data, n);
        id[n] = 0;
    }
    pc5_buf_free(&text);
}

static void remember_played(Pc5Ui *ui, const char *id)
{
    char path[PC5_PATH];

    if (!pc5_format(path, sizeof(path), "%s/pc5", ui->root) && !pc5_mkdirs(path) &&
        !pc5_format(path, sizeof(path), "%s/pc5/last.txt", ui->root))
        pc5_write_file(path, id, strlen(id));
}

/* ---- creating and the frame clock ---------------------------------------------------------- */

static void make_backdrop(Pc5Ui *ui)
{
    ui->falloff = pc5_alloc((size_t)W * H);
    ui->backdrop = pc5_alloc((size_t)W * H * 4);
    ui->composed = pc5_alloc((size_t)W * H * 4);
    if (!ui->falloff || !ui->backdrop || !ui->composed) return;
    for (int y = 0; y < H; y++) {
        float t = (float)y / (H - 1);
        int r = (int)(13 + (5 - 13) * t), g = (int)(15 + (6 - 15) * t), b = (int)(20 + (9 - 20) * t);
        for (int x = 0; x < W; x++) {
            /* A soft ellipse behind the chosen cover, and a faint floor. */
            float dx = (x - 960.0f) / 1150.0f, dy = (y - 430.0f) / 640.0f, d = dx * dx + dy * dy;
            float glow = d < 1 ? (1 - d) * (1 - d) : 0;
            float floor = y > FLOOR_Y ? expf(-(y - FLOOR_Y) / 90.0f) * 0.25f * (1 - fabsf(x - 960.0f) / 1400.0f) : 0;
            float v = glow * 0.62f + (floor > 0 ? floor : 0);
            ui->falloff[(size_t)y * W + (size_t)x] = (uint8_t)(v > 1 ? 255 : v * 255);
            ui->backdrop[(size_t)y * W + (size_t)x] = pc5_rgba(r, g, b, 255);
        }
    }
}

static void queue_free(Pc5Ui *ui)
{
    for (int i = 0; ui->queue && i < ui->queue_count; i++) {
        pc5_image_free(&ui->queue[i].cover);
        pc5_picture_free(&ui->queue[i].thumb);
        pc5_buf_free(&ui->queue[i].original);
    }
    pc5_free(ui->queue);
    ui->queue = NULL;
    ui->queue_count = ui->queue_at = ui->queue_phase = ui->queue_installing = ui->queue_stop_all = 0;
    ui->queue_review = -1;
}

static void notify(Pc5Ui *ui, const char *text)
{
    if (ui->platform.notify) ui->platform.notify(ui->platform.context, text);
}

static int64_t unix_now(Pc5Ui *ui, int *offset_s)
{
    int offset = 0;
    int64_t now = ui->platform.unix_time ? ui->platform.unix_time(ui->platform.context, &offset) : 0;
    if (offset_s) *offset_s = offset;
    return now;
}

static int image_copy(Pc5Image *to, const Pc5Image *from)
{
    *to = (Pc5Image){ 0 };
    if (!from->px || pc5_image_new(to, from->w, from->h)) return -1;
    memcpy(to->px, from->px, (size_t)from->w * (size_t)from->h * 4);
    return 0;
}

static int buf_copy(Pc5Buf *to, const Pc5Buf *from)
{
    *to = (Pc5Buf){ 0 };
    return from->len ? pc5_buf_put(to, from->data, from->len) : 0;
}

Pc5Ui *pc5_ui_create(const Pc5UiConfig *config, const Pc5Platform *platform)
{
    Pc5Ui *ui = pc5_alloc(sizeof(*ui));

    if (!ui) return NULL;
    ui->config = *config;
    pc5_copy(ui->root, sizeof(ui->root), config->root);
    pc5_copy(ui->template_prefix, sizeof(ui->template_prefix), config->template_prefix);
    if (config->start) pc5_copy(ui->start, sizeof(ui->start), config->start);
    ui->usb_roots = config->usb_roots;
    ui->platform = *platform;
    if (pc5_fonts_load(&ui->fonts, config->fonts)) say(ui, "PC5 fonts missing: %s", config->fonts);
    make_backdrop(ui);
    for (int k = 0; k < 3; k++) ui->glow[k] = ui->glow_target[k] = (float)((C_DEFAULT_TINT >> (16 - 8 * k)) & 255);
    ui->choice = -1;
    ui->queue_review = -1;
    ui->screen = S_START;
    ui->dirty = 1;
    pc5_settings_load(ui->root, &ui->settings);
    pc5_update_load(ui->root, &ui->update);
    return ui;
}

void pc5_ui_destroy(Pc5Ui *ui)
{
    if (!ui) return;
    covers_clear(ui);
    queue_free(ui);
    pc5_picture_free(&ui->review);
    pc5_image_free(&ui->review_image);
    pc5_buf_free(&ui->review_original);
    if (ui->installing) pc5_install_end(&ui->install);
    pc5_remove_end(&ui->remove);
    pc5_fonts_free(&ui->fonts);
    pc5_free(ui->falloff);
    pc5_free(ui->backdrop);
    pc5_free(ui->composed);
    pc5_free(ui);
}

/* ---- input ---------------------------------------------------------------------------------- */

/* Pressed keys, plus held directions repeating: after 380 ms, every 110 ms,
 * every 55 ms after 1.5 s. */
static uint32_t keys(Pc5Ui *ui, const Pc5Input *input)
{
    const uint32_t moves = PC5_KEY_UP | PC5_KEY_DOWN | PC5_KEY_LEFT | PC5_KEY_RIGHT | PC5_KEY_L1 | PC5_KEY_R1;
    uint32_t out = input->pressed;

    if (input->pressed & moves) {
        ui->repeat_key = input->pressed & moves;
        ui->repeat_since = ui->now;
        ui->repeat_at = ui->now + 380;
    } else if (ui->repeat_key && (input->held & ui->repeat_key) == ui->repeat_key) {
        if (ui->now >= ui->repeat_at) {
            out |= ui->repeat_key;
            ui->repeat_at = ui->now + (ui->now - ui->repeat_since > 1500 ? 55 : 110);
        }
    } else {
        ui->repeat_key = 0;
    }
    return out;
}

/* ---- the screens' work ------------------------------------------------------------------------ */

static void start_review_cover(Pc5Ui *ui)
{
    ui->fetching = 1;
    ui->dirty = 1;
}

/* The cover of *choice for a folder, or the card made from its program's
 * icon and its name (choice -1, or when no choice can be had: those that
 * can't are taken out of the search). */
static void make_cover(Pc5Ui *ui, const Pc5Folder *folder, int exe, Pc5CoverSearch *search, int *choice_io,
                       Pc5Image *cover, uint32_t *tint, char *source, size_t source_size, Pc5Buf *original)
{
    int choice = *choice_io;

    *cover = (Pc5Image){ 0 };
    *tint = C_DEFAULT_TINT;
    pc5_buf_free(original);
    while (choice >= 0 && choice < search->count) {
        if (!pc5_cover_fetch(&search->choices[choice], &ui->platform.http, cover, original)) {
            *tint = pc5_image_tint(cover);
            snprintf(source, source_size, "%s:%s", search->choices[choice].source, search->choices[choice].title);
            break;
        }
        say(ui, "PC5 cover fetch failed source=%s url=%s", search->choices[choice].source, search->choices[choice].url);
        memmove(&search->choices[choice], &search->choices[choice + 1],
                sizeof(search->choices[0]) * (size_t)(search->count - choice - 1));
        search->count--;
        if (choice >= search->count) choice = -1;
    }
    if (!cover->px) {
        Pc5Image icon = { 0 };
        char program[PC5_PATH];
        if (exe >= 0 && exe < folder->exe_count && !pc5_join(program, sizeof(program), folder->path, folder->exes[exe].path))
            pc5_pe_icon(program, &icon);
        pc5_make_card(&ui->fonts, folder->name, icon.px ? &icon : NULL, cover, tint);
        pc5_image_free(&icon);
        pc5_copy(source, source_size, "icon");
        pc5_buf_free(original);
        choice = -1;
    }
    *choice_io = choice;
}

/* The review screen's picture from its image. */
static void review_picture(Pc5Ui *ui)
{
    Pc5Image copy;
    pc5_picture_free(&ui->review);
    if (!image_copy(&copy, &ui->review_image)) pc5_picture_make(&ui->review, &copy);
}

/* Fetches the cover of choice (or makes the card) for the review screen. */
static void fetch_review_cover(Pc5Ui *ui)
{
    ui->fetching = 0;
    pc5_image_free(&ui->review_image);
    make_cover(ui, &ui->folder, ui->exe, &ui->search, &ui->choice, &ui->review_image, &ui->review_tint,
               ui->review_source, sizeof(ui->review_source), &ui->review_original);
    review_picture(ui);
    ui->choice_loaded = 1;
    ui->dirty = 1;
}

static void hint_for_folder(const Pc5Folder *folder, int exe, Pc5CoverHint *hint)
{
    const char *path = exe >= 0 && exe < folder->exe_count ? folder->exes[exe].path : "";
    const char *name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;

    memset(hint, 0, sizeof(*hint));
    pc5_copy(hint->folder, sizeof(hint->folder), folder->name);
    if (exe >= 0 && exe < folder->exe_count) pc5_copy(hint->product, sizeof(hint->product), folder->exes[exe].pe.product);
    snprintf(hint->program, sizeof(hint->program), "%.*s", (int)(strlen(name) > 4 ? strlen(name) - 4 : 0), name);
    hint->steam_appid = folder->steam_appid;
    pc5_copy(hint->picture, sizeof(hint->picture), folder->picture);
}

/* The player's own cover for an installed game: a picture in the covers
 * folder named after the game's folder, its name or its id. 0, or -1. */
static int own_cover(Pc5Ui *ui, const Pc5Game *game, char *out, size_t size)
{
    char dir[PC5_PATH], folder[PC5_PATH];
    const char *base = "";

    if (pc5_format(dir, sizeof(dir), "%s/covers", ui->root) || !pc5_is_dir(dir)) return -1;
    if (!pc5_game_folder(ui->root, game->id, folder, sizeof(folder)) && strrchr(folder, '/')) base = strrchr(folder, '/') + 1;
    const char *const names[] = { base, game->title, game->id, NULL };
    return pc5_cover_own(dir, names, out, size);
}

static void hint_for_game(Pc5Ui *ui, const Pc5Game *game, Pc5CoverHint *hint)
{
    char program[PC5_PATH];
    Pc5PeInfo pe;
    const char *name = strrchr(game->executable, '\\') ? strrchr(game->executable, '\\') + 1 : game->executable;

    memset(hint, 0, sizeof(*hint));
    pc5_copy(hint->folder, sizeof(hint->folder), game->title);
    if (!game_program(ui, game, program, sizeof(program)) && !pc5_pe_read(program, &pe))
        pc5_copy(hint->product, sizeof(hint->product), pe.product);
    snprintf(hint->program, sizeof(hint->program), "%.*s", (int)(strlen(name) > 4 ? strlen(name) - 4 : 0), name);
    /* The player's own picture takes the place of anything found online. */
    hint->picture_only = !own_cover(ui, game, hint->picture, sizeof(hint->picture));
}

/* 1 when the player's own picture for game is newer than its cover. */
static int own_cover_new(Pc5Ui *ui, const Pc5Game *game, const char *cover)
{
    char picture[PC5_PATH];
    struct stat own, saved;

    if (own_cover(ui, game, picture, sizeof(picture)) || stat(picture, &own)) return 0;
    return stat(cover, &saved) || own.st_mtime > saved.st_mtime;
}

/* At start: the games that have no cover yet, or one from PC5 1.00 (which
 * could have bands): it is looked for again, kept while there is no answer.
 * And those the player gave a picture of their own since. */
static void queue_missing_covers(Pc5Ui *ui)
{
    ui->cover_count = ui->cover_at = 0;
    for (int i = 0; i < ui->library.count; i++) {
        char path[PC5_PATH];
        if (!cover_path(ui, &ui->library.games[i], path, sizeof(path)) &&
            (!pc5_cover_current(path) || own_cover_new(ui, &ui->library.games[i], path)))
            ui->cover_queue[ui->cover_count++] = i;
    }
}

/* One request towards the next missing cover. 0 when all are done. */
static int missing_cover_step(Pc5Ui *ui)
{
    if (ui->cover_at >= ui->cover_count) return 0;
    const Pc5Game *game = &ui->library.games[ui->cover_queue[ui->cover_at]];
    if (!ui->cover_searching) {
        Pc5CoverHint hint;
        hint_for_game(ui, game, &hint);
        pc5_cover_search_start(&ui->search, &hint);
        ui->cover_searching = 1;
        return 1;
    }
    if (pc5_cover_search_step(&ui->search, &ui->platform.http)) return 1;
    /* Searched: the best picture that can be had, else the card, saved only
     * when a source answered (offline, it is tried again next time). */
    Pc5Image cover = { 0 };
    Pc5Buf original = { 0 };
    uint32_t tint = C_DEFAULT_TINT;
    for (int i = 0; i < ui->search.count && !cover.px; i++)
        if (!pc5_cover_fetch(&ui->search.choices[i], &ui->platform.http, &cover, &original)) tint = pc5_image_tint(&cover);
    if (!cover.px && ui->search.answered) card_for(ui, game, &cover, &tint);
    if (cover.px) {
        char path[PC5_PATH], dir[PC5_PATH];
        if (!cover_path(ui, game, path, sizeof(path)) &&
            !pc5_format(dir, sizeof(dir), "%s/prefixes/%s/pc5", ui->root, game->prefix) && !pc5_mkdirs(dir)) {
            pc5_cover_save(path, &cover, tint);
            if (original.len && !pc5_format(path, sizeof(path), "%s/%s", dir, pc5_picture_name(&original)))
                pc5_write_file(path, original.data, original.len);
        }
    }
    say(ui, "PC5 cover game=%s choices=%d answered=%d saved=%d", game->id, ui->search.count, ui->search.answered,
        cover.px != NULL);
    pc5_image_free(&cover);
    pc5_buf_free(&original);
    ui->cover_searching = 0;
    ui->cover_at++;
    return ui->cover_at < ui->cover_count;
}

static void speed_reset(Pc5Ui *ui)
{
    ui->speed_at = ui->now;
    ui->speed_bytes = ui->install.bytes_done;
    ui->speed = 0;
    ui->speed_phase = ui->install.phase;
}

static void begin_install(Pc5Ui *ui)
{
    Pc5Image cover = ui->review_image;
    int status;

    ui->review_image = (Pc5Image){ 0 };
    status = pc5_install_begin(&ui->install, ui->root, ui->template_prefix, &ui->folder, ui->exe, &cover,
                               ui->review_tint, ui->review_source);
    ui->installing = 1;
    if (status) {
        char title[200];
        snprintf(title, sizeof(title), "%s can't be installed", ui->folder.name);
        say(ui, "PC5 problem install %s: %s", ui->folder.name, ui->install.error);
        message(ui, 0, title, ui->install.error, S_REVIEW);
        pc5_install_end(&ui->install);
        ui->installing = 0;
        /* the review keeps its cover to show */
        if (ui->review.levels) {
            Pc5Image copy;
            if (!pc5_image_new(&copy, ui->review.level[0].w, ui->review.level[0].h)) {
                memcpy(copy.px, ui->review.level[0].px, (size_t)copy.w * (size_t)copy.h * 4);
                ui->review_image = copy;
            }
        }
        return;
    }
    ui->install.original = ui->review_original;
    ui->review_original = (Pc5Buf){ 0 };
    speed_reset(ui);
    say(ui, "PC5 install id=%s from=%s exe=%s", ui->install.id, ui->folder.path, ui->install.info.exe);
    go(ui, S_INSTALL);
}

static void queue_summary(Pc5Ui *ui);

/* The queue's next game that is waiting: started, or skipped with why. */
static void queue_next(Pc5Ui *ui)
{
    for (; ui->queue_at < ui->queue_count; ui->queue_at++) {
        QueueGame *q = &ui->queue[ui->queue_at];
        if (q->state != Q_WAITING) continue;
        if (ui->queue_stop_all) { q->state = Q_STOPPED; continue; }
        if (q->folder.problem[0] || q->exe < 0) {
            q->state = Q_SKIPPED;
            pc5_copy(q->error, sizeof(q->error), q->folder.problem[0] ? q->folder.problem : "It has no program to start.");
            continue;
        }
        Pc5Image cover = q->cover;
        q->cover = (Pc5Image){ 0 };
        ui->folder = q->folder;
        ui->exe = q->exe;
        if (pc5_install_begin(&ui->install, ui->root, ui->template_prefix, &q->folder, q->exe, &cover, q->tint,
                              q->cover_source)) {
            q->state = Q_FAILED;
            pc5_copy(q->error, sizeof(q->error), ui->install.error);
            say(ui, "PC5 problem install %s: %s", q->folder.name, ui->install.error);
            pc5_install_end(&ui->install);
            continue;
        }
        ui->install.original = q->original;
        q->original = (Pc5Buf){ 0 };
        ui->installing = 1;
        speed_reset(ui);
        say(ui, "PC5 install id=%s from=%s exe=%s queue=%d/%d", ui->install.id, q->folder.path, ui->install.info.exe,
            ui->queue_at + 1, ui->queue_count);
        go(ui, S_INSTALL);
        return;
    }
    queue_summary(ui);
}

static void install_progress(Pc5Ui *ui)
{
    Pc5InstallPhase phase = pc5_install_step(&ui->install, 14);

    ui->dirty = 1;
    if (phase != ui->speed_phase) speed_reset(ui);
    else if (ui->now - ui->speed_at >= 1000 && phase <= PC5_INSTALL_VERIFY) {
        double rate = (double)(ui->install.bytes_done - ui->speed_bytes) * 1000.0 / (double)(ui->now - ui->speed_at);
        if (ui->install.bytes_done < ui->speed_bytes) rate = 0;
        ui->speed = ui->speed ? ui->speed * 0.6 + rate * 0.4 : rate;
        ui->speed_at = ui->now;
        ui->speed_bytes = ui->install.bytes_done;
    }
    if (phase != PC5_INSTALL_DONE && phase != PC5_INSTALL_FAILED && phase != PC5_INSTALL_CANCELLED) return;
    if (ui->queue_installing) {
        QueueGame *q = &ui->queue[ui->queue_at];
        q->state = phase == PC5_INSTALL_DONE ? Q_INSTALLED : phase == PC5_INSTALL_CANCELLED ? Q_STOPPED : Q_FAILED;
        pc5_copy(q->id, sizeof(q->id), ui->install.id);
        if (phase == PC5_INSTALL_DONE) say(ui, "PC5 installed id=%s", ui->install.id);
        else if (phase == PC5_INSTALL_FAILED) {
            pc5_copy(q->error, sizeof(q->error), ui->install.error);
            say(ui, "PC5 problem install %s: %s", q->folder.name, ui->install.error);
        } else {
            say(ui, "PC5 install ended cancelled=1 error=%s", ui->install.error);
        }
        pc5_install_end(&ui->install);
        ui->installing = 0;
        ui->queue_at++;
        queue_next(ui);
        return;
    }
    if (phase == PC5_INSTALL_DONE) {
        char title[200], id[PC5_ID];
        pc5_copy(id, sizeof(id), ui->install.id);
        snprintf(title, sizeof(title), "%s is installed", ui->install.info.title);
        say(ui, "PC5 installed id=%s", id);
        pc5_install_end(&ui->install);
        ui->installing = 0;
        library_reload(ui, id);
        message(ui, 1, title, "It's in your library. Press the button to play it from there.", S_LIBRARY);
    } else {
        char title[200], body[600];
        int cancelled = phase == PC5_INSTALL_CANCELLED;
        snprintf(title, sizeof(title), cancelled ? "Install stopped" : "%s wasn't installed", ui->install.info.title);
        if (cancelled) snprintf(body, sizeof(body), "Everything copied so far was deleted. The USB drive wasn't changed.");
        else if (strstr(ui->install.staging, "/.installing-") || !ui->install.staging[0])
            snprintf(body, sizeof(body), "%s Nothing was kept.", ui->install.error);
        else snprintf(body, sizeof(body), "%s PC5 tries again the next time it starts.", ui->install.error);
        if (cancelled) say(ui, "PC5 install ended cancelled=1 error=%s", ui->install.error);
        else say(ui, "PC5 problem install %s: %s", ui->install.info.title, ui->install.error);
        pc5_install_end(&ui->install);
        ui->installing = 0;
        library_reload(ui, NULL);
        message(ui, 0, title, body, S_LIBRARY);
    }
}

/* ---- several games in a row ----------------------------------------------------------------- */

static int ticked_count(const Pc5Ui *ui)
{
    int n = 0;
    for (int i = 0; i < ui->source_count; i++) n += ui->ticked[i] != 0;
    return n;
}

static int queue_begin(Pc5Ui *ui)
{
    int n = ticked_count(ui), k = 0;

    queue_free(ui);
    if (!n || !(ui->queue = pc5_alloc(sizeof(QueueGame) * (size_t)n))) return -1;
    for (int i = 0; i < ui->source_count; i++)
        if (ui->ticked[i]) {
            ui->queue[k].source = ui->sources[i];
            ui->queue[k].choice = -1;
            k++;
        }
    ui->queue_count = n;
    ui->queue_at = 0;
    ui->queue_phase = 0;
    return 0;
}

static void queue_thumb(QueueGame *q)
{
    Pc5Image copy;
    pc5_picture_free(&q->thumb);
    if (!image_copy(&copy, &q->cover)) pc5_picture_make(&q->thumb, &copy);
}

/* One piece of reading the ticked folders: a folder read, one request for
 * its cover, its cover fetched. 1 while there is more. */
static int queue_read_step(Pc5Ui *ui)
{
    QueueGame *q = &ui->queue[ui->queue_at];
    Pc5CoverHint hint;

    switch (ui->queue_phase) {
    case 0:
        if (pc5_folder_inspect(q->source.path, q->source.name, &q->folder) && !q->folder.problem[0])
            snprintf(q->folder.problem, sizeof(q->folder.problem), "PC5 can't read this folder.");
        q->exe = q->folder.exe_count ? 0 : -1;
        say(ui, "PC5 folder=%s files=%u bytes=%llu exes=%d problem=%s queue=%d/%d", q->folder.name,
            (unsigned)q->folder.files, (unsigned long long)q->folder.bytes, q->folder.exe_count, q->folder.problem,
            ui->queue_at + 1, ui->queue_count);
        hint_for_folder(&q->folder, q->exe, &hint);
        pc5_cover_search_start(&q->search, &hint);
        ui->queue_phase = 1;
        return 1;
    case 1:
        if (pc5_cover_search_step(&q->search, &ui->platform.http)) return 1;
        ui->queue_phase = 2;
        return 1;
    default:
        q->choice = q->search.count ? 0 : -1;
        make_cover(ui, &q->folder, q->exe, &q->search, &q->choice, &q->cover, &q->tint, q->cover_source,
                   sizeof(q->cover_source), &q->original);
        queue_thumb(q);
        say(ui, "PC5 cover choices=%d answered=%d queue=%d/%d", q->search.count, q->search.answered, ui->queue_at + 1,
            ui->queue_count);
        ui->queue_phase = 0;
        ui->queue_at++;
        return ui->queue_at < ui->queue_count;
    }
}

/* The review screen for one game of the queue, and back. */
static void queue_review_open(Pc5Ui *ui, int index)
{
    QueueGame *q = &ui->queue[index];
    ui->folder = q->folder;
    ui->exe = q->exe;
    ui->search = q->search;
    ui->choice = q->choice;
    pc5_image_free(&ui->review_image);
    image_copy(&ui->review_image, &q->cover);
    review_picture(ui);
    ui->review_tint = q->tint;
    pc5_copy(ui->review_source, sizeof(ui->review_source), q->cover_source);
    pc5_buf_free(&ui->review_original);
    buf_copy(&ui->review_original, &q->original);
    ui->choice_loaded = 1;
    ui->fetching = 0;
    ui->queue_review = index;
    go(ui, S_REVIEW);
}

static void queue_review_close(Pc5Ui *ui)
{
    QueueGame *q = &ui->queue[ui->queue_review];
    q->exe = ui->exe;
    q->search = ui->search;
    q->choice = ui->choice;
    if (ui->review_image.px) {
        pc5_image_free(&q->cover);
        q->cover = ui->review_image;
        ui->review_image = (Pc5Image){ 0 };
        q->tint = ui->review_tint;
        pc5_copy(q->cover_source, sizeof(q->cover_source), ui->review_source);
        pc5_buf_free(&q->original);
        q->original = ui->review_original;
        ui->review_original = (Pc5Buf){ 0 };
        queue_thumb(q);
    }
    ui->menu = ui->queue_review;
    ui->queue_review = -1;
    go(ui, S_QUEUE);
}

static uint64_t queue_needs(const Pc5Ui *ui)
{
    uint64_t need = 0;
    for (int i = 0; i < ui->queue_count; i++)
        if (!ui->queue[i].folder.problem[0] && ui->queue[i].exe >= 0) need += pc5_install_needs(&ui->queue[i].folder);
    return need;
}

static void queue_summary(Pc5Ui *ui)
{
    char title[160], body[700] = "", first[PC5_ID] = "";
    int installed = 0;
    size_t used = 0;

    for (int i = 0; i < ui->queue_count; i++) {
        const QueueGame *q = &ui->queue[i];
        if (q->state == Q_INSTALLED) {
            if (!installed++) pc5_copy(first, sizeof(first), q->id);
            continue;
        }
        const char *why = q->state == Q_STOPPED ? "stopped, nothing kept." : q->error[0] ? q->error : "not installed.";
        int n = snprintf(body + used, sizeof(body) - used, "%s%s: %s", used ? "  " : "", q->folder.name[0] ? q->folder.name : q->source.name, why);
        if (n > 0) used = used + (size_t)n < sizeof(body) ? used + (size_t)n : sizeof(body) - 1;
    }
    int all = installed == ui->queue_count;
    if (all)
        snprintf(title, sizeof(title), installed == 1 ? "1 game is installed" : "%d games are installed", installed);
    else if (installed)
        snprintf(title, sizeof(title), "%d of %d games are installed", installed, ui->queue_count);
    else
        snprintf(title, sizeof(title), "No game was installed");
    if (!body[0]) snprintf(body, sizeof(body), "They're in your library. The USB drive wasn't changed.");
    say(ui, "PC5 queue done installed=%d of=%d", installed, ui->queue_count);
    queue_free(ui);
    memset(ui->ticked, 0, sizeof(ui->ticked));
    library_reload(ui, first[0] ? first : NULL);
    message(ui, installed > 0 && all, title, body, S_LIBRARY);
}

/* ---- Settings ----------------------------------------------------------------------------- */

static void setting_text(Pc5Ui *ui, SettingRow row, const char **label, char *about, size_t about_size, char *value,
                         size_t value_size)
{
    const Pc5Settings *set = &ui->settings;
    value[0] = 0;
    about[0] = 0;
    switch (row) {
    case SR_INSTALL:
        *label = "Install games from USB";
        pc5_copy(about, about_size, "Copies one game, or several in a row, to the console");
        break;
    case SR_UNINSTALL:
        *label = "Uninstall game";
        pc5_copy(about, about_size, "Deletes a game, with its saves and settings");
        break;
    case SR_GAMES:
        *label = "In games";
        pc5_copy(about, about_size, "Vibration, light bar and triggers");
        pc5_copy(value, value_size, "\xe2\x80\xba");
        break;
    case SR_OVERLAY:
        *label = "Overlay";
        pc5_copy(about, about_size, "FPS, frame time, CPU, temperatures and memory");
        pc5_copy(value, value_size, "\xe2\x80\xba");
        break;
    case SR_UPDATES:
        *label = "Updates";
        pc5_update_text(&ui->update.last, about, about_size);
        pc5_copy(value, value_size, "\xe2\x80\xba");
        break;
    case SR_KEEP_LOGS:
        *label = "Keep all logs";
        pc5_copy(about, about_size, set->keep_logs ? "Every session's log is kept for 7 days"
                                                   : "Only the logs of sessions that went wrong, for 7 days");
        pc5_copy(value, value_size, set->keep_logs ? "On" : "Off");
        break;
    case SR_ABOUT:
        *label = "About PC5";
        pc5_copy(about, about_size, "Version, free space and credits");
        break;
    case SR_FPS:
        *label = "Show FPS";
        pc5_copy(about, about_size, "Frames shown each second");
        pc5_copy(value, value_size, set->fps ? "On" : "Off");
        break;
    case SR_FRAME_TIME:
        *label = "Frame time";
        pc5_copy(about, about_size, "How long a frame takes, on average, in milliseconds");
        pc5_copy(value, value_size, set->frame_time ? "On" : "Off");
        break;
    case SR_CPU_USAGE:
        *label = "CPU usage";
        pc5_copy(about, about_size, "How much of the console's CPU the game uses");
        pc5_copy(value, value_size, set->cpu_usage ? "On" : "Off");
        break;
    case SR_CPU_TEMPERATURE:
        *label = "CPU temperature";
        pc5_copy(about, about_size, "In degrees Celsius");
        pc5_copy(value, value_size, set->cpu_temperature ? "On" : "Off");
        break;
    case SR_SOC_TEMPERATURE:
        *label = "SoC temperature";
        pc5_copy(about, about_size, "The console chip's, in degrees Celsius");
        pc5_copy(value, value_size, set->soc_temperature ? "On" : "Off");
        break;
    case SR_MEMORY:
        *label = "Memory";
        pc5_copy(about, about_size, "What the game uses, of what it has, in GB");
        pc5_copy(value, value_size, set->memory ? "On" : "Off");
        break;
    case SR_FPS_CORNER:
        *label = "Corner";
        pc5_copy(about, about_size, "Where the overlay sits on the screen");
        pc5_copy(value, value_size, pc5_corner_name(set->fps_corner));
        break;
    case SR_VIBRATION:
        *label = "Vibration";
        pc5_copy(about, about_size, set->vibration ? "How strongly the controller rumbles with the game"
                                                   : "The game's rumble on the controller");
        percent_text(set->vibration, value, value_size);
        break;
    case SR_LIGHT:
        *label = "Light bar";
        pc5_copy(about, about_size, set->light_bar == PC5_LIGHT_SCREEN ? "The colour of what is on screen"
                                    : set->light_bar == PC5_LIGHT_GAME ? "The main colour of the game's cover"
                                                                       : "PC5 leaves the console's colour");
        pc5_copy(value, value_size, pc5_light_name(set->light_bar));
        break;
    case SR_TRIGGERS:
        *label = "Adaptive triggers";
        pc5_copy(about, about_size, set->triggers == PC5_TRIGGERS_RUMBLE ? "L2 and R2 vibrate with the game's rumble"
                                    : set->triggers == PC5_TRIGGERS_RESISTANCE ? "L2 and R2 push back as you press them"
                                    : set->triggers == PC5_TRIGGERS_BOTH ? "L2 and R2 push back, and buzz with the rumble"
                                    : "Rumble, resistance or both on L2 and R2");
        pc5_copy(value, value_size, pc5_triggers_name(set->triggers));
        break;
    case SR_TRIGGER_RUMBLE:
        *label = "Trigger rumble";
        pc5_copy(about, about_size, "How strongly L2 and R2 vibrate");
        percent_text(set->trigger_rumble, value, value_size);
        break;
    case SR_TRIGGER_RESISTANCE:
        *label = "Trigger resistance";
        pc5_copy(about, about_size, "How firmly L2 and R2 push back");
        percent_text(set->trigger_resistance, value, value_size);
        break;
    case SR_CHECK_ON_OPEN:
        *label = "Check when PC5 opens";
        pc5_copy(about, about_size, "Asks GitHub for a newer PC5, at most once an hour");
        pc5_copy(value, value_size, set->check_updates ? "On" : "Off");
        break;
    case SR_CHECK_NOW:
        *label = "Check now";
        pc5_update_text(&ui->update.last, about, about_size);
        break;
    }
}

/* A setting with values moves to its next (step 1) or previous (-1) one: 1 when it changed. */
static int setting_change(Pc5Ui *ui, SettingRow row, int step)
{
    Pc5Settings *set = &ui->settings;
    switch (row) {
    case SR_KEEP_LOGS: set->keep_logs = !set->keep_logs; break;
    case SR_FPS: set->fps = !set->fps; break;
    case SR_FRAME_TIME: set->frame_time = !set->frame_time; break;
    case SR_CPU_USAGE: set->cpu_usage = !set->cpu_usage; break;
    case SR_CPU_TEMPERATURE: set->cpu_temperature = !set->cpu_temperature; break;
    case SR_SOC_TEMPERATURE: set->soc_temperature = !set->soc_temperature; break;
    case SR_MEMORY: set->memory = !set->memory; break;
    case SR_FPS_CORNER: set->fps_corner = (Pc5Corner)((set->fps_corner + PC5_CORNERS + step) % PC5_CORNERS); break;
    case SR_VIBRATION: set->vibration = pc5_strength_next(set->vibration, 0, step); break;
    case SR_TRIGGER_RUMBLE: set->trigger_rumble = pc5_strength_next(set->trigger_rumble, 25, step); break;
    case SR_TRIGGER_RESISTANCE: set->trigger_resistance = pc5_strength_next(set->trigger_resistance, 25, step); break;
    case SR_LIGHT: set->light_bar = (Pc5Light)((set->light_bar + PC5_LIGHTS + step) % PC5_LIGHTS); break;
    case SR_TRIGGERS: set->triggers = (Pc5Triggers)((set->triggers + PC5_TRIGGERS_COUNT + step) % PC5_TRIGGERS_COUNT); break;
    case SR_CHECK_ON_OPEN: set->check_updates = !set->check_updates; break;
    default: return 0;
    }
    if (pc5_settings_save(ui->root, set)) say(ui, "PC5 problem settings: can't write them");
    say(ui, "PC5 settings fps=%d frame_time=%d cpu=%d cpu_temp=%d soc_temp=%d memory=%d corner=%d vibration=%d light=%d "
        "triggers=%d trigger_rumble=%d trigger_resistance=%d updates=%d keep_logs=%d", set->fps, set->frame_time,
        set->cpu_usage, set->cpu_temperature, set->soc_temperature, set->memory, (int)set->fps_corner, set->vibration,
        (int)set->light_bar, (int)set->triggers, set->trigger_rumble, set->trigger_resistance, set->check_updates,
        set->keep_logs);
    return 1;
}

/* The game's controller, kept after every change (it reaches the game's profile at its next start). */
static void controls_changed(Pc5Ui *ui)
{
    const Pc5Controls *k = &ui->controls;
    if (pc5_controls_save(ui->root, ui->launch_id, k)) say(ui, "PC5 problem controls %s: can't write them", ui->launch_id);
    say(ui, "PC5 controls id=%s mode=%s left=%s right=%s speed=%d frame_generation=%d memory_32bit=%s", ui->launch_id,
        pc5_pad_mode_name(k->mode), pc5_stick_use_name(k->stick[0]), pc5_stick_use_name(k->stick[1]), k->speed,
        k->frame_generation, k->memory_2gb ? "2gb" : "4gb");
}

/* A Button mapping row moves to its next (step 1) or previous (-1) value: 1 when it changed. */
static int pad_change(Pc5Ui *ui, PadRow row, int step)
{
    Pc5Controls *k = &ui->controls;
    switch (row.kind) {
    case PR_STICK: pc5_controls_next_stick(k, row.index, step); return 1;
    case PR_SPEED: {
        int speed = k->speed + (step < 0 ? -1 : 1);
        if (speed < 1 || speed > PC5_SPEEDS) return 0;
        k->speed = speed;
        return 1;
    }
    case PR_BUTTON:
    case PR_DIRECTION:
        k->key[row.index] = (uint8_t)((k->key[row.index] + pc5_key_count + (step < 0 ? -1 : 1)) % pc5_key_count);
        return 1;
    default: return 0;
    }
}

/* A game's menu, with its controller as PC5 kept it. */
static void game_menu_open(Pc5Ui *ui, const char *id)
{
    pc5_copy(ui->launch_id, sizeof(ui->launch_id), id);
    pc5_controls_load(ui->root, id, &ui->controls);
    if (!ui->config.tiles_root || !pc5_home_find(ui->config.tiles_root, id, ui->tile)) ui->tile[0] = 0;
    ui->menu = 0;
    go(ui, S_GAME_MENU);
}

/* The game's home screen tile: made, or taken away when it has one. */
static void home_toggle(Pc5Ui *ui, const Pc5Game *game)
{
    char error[PC5_ERROR] = "", body[600];

    if (!ui->config.tiles_root || !ui->config.tile_template) {
        message(ui, 0, "Home screen tiles aren't available", "This copy of PC5 has no tile program.", S_GAME_MENU);
        return;
    }
    if (ui->tile[0]) {
        int removed = pc5_home_remove(ui->config.tiles_root, game->id);
        say(ui, "PC5 home tile removed id=%s tile=%s count=%d", game->id, ui->tile, removed);
        if (removed < 0) {
            snprintf(body, sizeof(body), "PC5 can't delete its folder, %s/%s.", ui->config.tiles_root, ui->tile);
            message(ui, 0, "The tile wasn't taken away", body, S_GAME_MENU);
            return;
        }
        ui->tile[0] = 0;
        message(ui, 1, "Taken off the home screen",
                "The tile's folder is deleted. If the tile is still on the home screen, delete it there.", S_GAME_MENU);
        return;
    }
    if (pc5_home_add(ui->config.tiles_root, ui->config.tile_template, ui->root, game->id, game->prefix, game->title,
                     ui->tile, error, sizeof(error))) {
        say(ui, "PC5 problem home tile %s: %s", game->id, error);
        message(ui, 0, "The tile wasn't made", error, S_GAME_MENU);
        return;
    }
    say(ui, "PC5 home tile added id=%s tile=%s", game->id, ui->tile);
    snprintf(body, sizeof(body),
             "Its tile, %s, appears on the home screen once ShadowMountPlus finds it in %s. It starts the game "
             "straight away, and leaving the game goes back to the home screen.", ui->tile, ui->config.tiles_root);
    message(ui, 1, "Added to the home screen", body, S_GAME_MENU);
}

static void settings_open(Pc5Ui *ui, int page, int row)
{
    ui->page = page;
    ui->menu = row;
    go(ui, S_SETTINGS);
}

/* Asks GitHub (blocking, one request) and tells once per release. */
static void update_check_now(Pc5Ui *ui)
{
    Pc5Update update;
    pc5_update_check(&ui->platform.http, PC5_VERSION, &update);
    ui->update.last = update;
    ui->update.checked = unix_now(ui, NULL);
    say(ui, "PC5 update state=%d latest=%s", (int)update.state, update.version);
    if (update.state == PC5_UPDATE_AVAILABLE && strcmp(ui->update.told, update.version)) {
        char text[200];
        snprintf(text, sizeof(text), "PC5 %s is available on GitHub (you have %s).", update.version, PC5_VERSION);
        notify(ui, text);
        pc5_copy(ui->update.told, sizeof(ui->update.told), update.version);
    }
    if (pc5_update_save(ui->root, &ui->update)) say(ui, "PC5 problem update: can't remember the answer");
}

/* What went wrong last time, as a message (once the library shows). */
static void issue_message(Pc5Ui *ui)
{
    char title[200], body[700], file[PC5_PATH];
    const Pc5LogIssue *issue = &ui->issue;
    int index = pc5_library_find(&ui->library, issue->profile);
    const char *name = index >= 0 ? ui->library.games[index].title : "The game";
    const char *slash = strstr(issue->file, "/logs/");

    ui->issue_pending = 0;
    pc5_copy(file, sizeof(file), slash ? slash + 1 : issue->file);
    switch (issue->problem) {
    case PC5_LOG_GAME_CRASHED:
        snprintf(title, sizeof(title), "%s stopped with an error", name);
        snprintf(body, sizeof(body), "Wine reported a crash. Its log is kept as %.120s in %.120s.", file, ui->root);
        break;
    case PC5_LOG_NO_PICTURE:
        snprintf(title, sizeof(title), "%s closed before showing anything", name);
        snprintf(body, sizeof(body), "If PC5 started the wrong program, choose Change program in the game's menu "
                 "(Triangle). Its log is kept as %.120s in %.120s.", file, ui->root);
        break;
    case PC5_LOG_FORCED:
        snprintf(title, sizeof(title), "%s had to be closed", name);
        snprintf(body, sizeof(body), "It didn't close when asked, so PC5 ended it. Its log is kept as %.120s in %.120s.", file,
                 ui->root);
        break;
    case PC5_LOG_CRASHED:
        if (index >= 0) snprintf(title, sizeof(title), "%s ended unexpectedly", name);
        else snprintf(title, sizeof(title), "PC5 ended unexpectedly last time");
        snprintf(body, sizeof(body), "Its log is kept as %.120s in %.120s.", file, ui->root);
        break;
    default:
        return;
    }
    message(ui, 0, title, body, S_LIBRARY);
}

/* From the start screens to the library, telling first what went wrong last time. */
static void to_library(Pc5Ui *ui)
{
    covers_clear(ui);
    go(ui, S_LIBRARY);
    if (ui->issue_pending) issue_message(ui);
}

/* ---- update ------------------------------------------------------------------------------------- */

static int list_move(int *index, int count, uint32_t k)
{
    int before = *index;
    if (count <= 0) { *index = 0; return 0; }
    if (k & PC5_KEY_UP) *index = *index > 0 ? *index - 1 : 0;
    if (k & PC5_KEY_DOWN) *index = *index + 1 < count ? *index + 1 : count - 1;
    return *index != before;
}

static int letter_of(const char *title)
{
    int c = pc5_lower((unsigned char)title[0]);
    return c >= 'a' && c <= 'z' ? c : '#';
}

int pc5_ui_update(Pc5Ui *ui, const Pc5Input *input, uint64_t now_ms)
{
    uint32_t k;
    uint32_t accept = PC5_KEY_ACCEPT, back = PC5_KEY_BACK;
    float dt;

    if (!ui->last_frame) ui->last_frame = now_ms;
    dt = (float)(now_ms - ui->last_frame) / 1000.0f;
    if (dt > 0.1f) dt = 0.1f;
    ui->last_frame = now_ms;
    ui->now = now_ms;
    k = keys(ui, input);

    switch (ui->screen) {
    case S_START:
        if (!ui->drawn) break;
        if (pc5_recover_step(ui->root, &ui->remove, 14)) { ui->dirty = 1; break; }
        pc5_remove_end(&ui->remove);
        {
            char id[PC5_ID];
            int offset = 0;
            int64_t now = unix_now(ui, &offset);
            last_played(ui, id, sizeof(id));
            library_reload(ui, id[0] ? id : NULL);
            /* Last time's logs: kept only when something went wrong, for a week. */
            if (now > 0) {
                int issues = pc5_logs_tidy(ui->root, ui->settings.keep_logs, now, offset, &ui->issue);
                ui->issue_pending = issues > 0 && ui->issue.problem != PC5_LOG_PC5_PROBLEM;
                if (issues) say(ui, "PC5 logs kept=%d newest=%s problem=%d", issues, ui->issue.file, (int)ui->issue.problem);
            }
            /* Started from a home screen tile: its game, now. */
            if (ui->start[0]) {
                int index = pc5_library_find(&ui->library, ui->start);
                say(ui, "PC5 tile game=%s found=%d", ui->start, index >= 0);
                if (index >= 0 && ui->library.games[index].playable) {
                    pc5_copy(ui->launch_id, sizeof(ui->launch_id), ui->start);
                    ui->start[0] = 0;
                    go(ui, S_LAUNCH);
                    break;
                }
                message(ui, 0, "This tile's game can't start",
                        index < 0 ? "It isn't in PC5 any more. Take the tile off the home screen, or install the "
                                    "game again." : ui->library.games[index].problem, S_LIBRARY);
                ui->start[0] = 0;
                break;
            }
            queue_missing_covers(ui);
            if (ui->settings.check_updates && (now <= 0 || now - ui->update.checked >= PC5_UPDATE_EVERY_S ||
                                               now < ui->update.checked))
                go(ui, S_UPDATE);
            else if (ui->cover_count) go(ui, S_COVERS);
            else to_library(ui);
        }
        break;
    case S_UPDATE:
        if (!ui->drawn) break;
        update_check_now(ui);
        if (ui->cover_count) go(ui, S_COVERS);
        else to_library(ui);
        break;
    case S_COVERS:
        if (k & (accept | back)) { to_library(ui); break; }
        if (!ui->drawn) break;
        if (!missing_cover_step(ui)) to_library(ui);
        ui->dirty = 1;
        break;
    case S_LIBRARY: {
        int before = ui->selected, count = ui->library.count;
        if (count) {
            if (k & PC5_KEY_LEFT && ui->selected > 0) ui->selected--;
            if (k & PC5_KEY_RIGHT && ui->selected + 1 < count) ui->selected++;
            if (k & (PC5_KEY_L1 | PC5_KEY_R1)) {
                /* to the first game of the previous or next letter */
                int letter = letter_of(ui->library.games[ui->selected].title), i = ui->selected;
                if (k & PC5_KEY_R1) {
                    while (i + 1 < count && letter_of(ui->library.games[i].title) == letter) i++;
                } else {
                    while (i > 0 && letter_of(ui->library.games[i].title) == letter) i--;
                    letter = letter_of(ui->library.games[i].title);
                    while (i > 0 && letter_of(ui->library.games[i - 1].title) == letter) i--;
                }
                ui->selected = i;
            }
            if (k & accept) {
                const Pc5Game *game = &ui->library.games[ui->selected];
                if (game->playable) {
                    pc5_copy(ui->launch_id, sizeof(ui->launch_id), game->id);
                    go(ui, S_LAUNCH);
                } else {
                    char title[200], body[400];
                    snprintf(title, sizeof(title), "%s can't start", game->title);
                    snprintf(body, sizeof(body), "%s You can uninstall it from its menu (Triangle) and install it again.",
                             game->problem[0] ? game->problem : "Something it needs is missing.");
                    message(ui, 0, title, body, S_LIBRARY);
                }
            } else if (k & PC5_KEY_TRIANGLE) {
                game_menu_open(ui, ui->library.games[ui->selected].id);
            }
        }
        if (k & PC5_KEY_OPTIONS) settings_open(ui, 0, 0);
        if (ui->selected != before) ui->dirty = 1;
        if (covers_near(ui)) ui->dirty = 1;
        break;
    }
    case S_SETTINGS: {
        SettingRow rows[PAGE_ROWS], row;
        int count = page_rows(&ui->settings, ui->page, rows);
        if (ui->menu >= count) ui->menu = count - 1;
        row = rows[ui->menu];
        if (list_move(&ui->menu, count, k)) ui->dirty = 1;
        if (k & (PC5_KEY_LEFT | PC5_KEY_RIGHT)) {
            if (setting_change(ui, row, k & PC5_KEY_LEFT ? -1 : 1)) ui->dirty = 1;
        } else if (k & PC5_KEY_OPTIONS) {
            go(ui, S_LIBRARY);
        } else if (k & back) {
            if (ui->page == PAGE_GAMES) settings_open(ui, 0, 2);
            else if (ui->page == PAGE_OVERLAY) settings_open(ui, 0, 3);
            else if (ui->page == PAGE_UPDATES) settings_open(ui, 0, 4);
            else go(ui, S_LIBRARY);
        } else if (k & accept) {
            switch (row) {
            case SR_INSTALL: memset(ui->ticked, 0, sizeof(ui->ticked)); go(ui, S_SCAN); break;
            case SR_UNINSTALL:
                ui->menu = ui->selected;
                ui->list_top = 0;
                go(ui, S_UNINSTALL_LIST);
                break;
            case SR_GAMES: settings_open(ui, PAGE_GAMES, 0); break;
            case SR_OVERLAY: settings_open(ui, PAGE_OVERLAY, 0); break;
            case SR_UPDATES: settings_open(ui, PAGE_UPDATES, 0); break;
            case SR_ABOUT: go(ui, S_ABOUT); break;
            case SR_CHECK_NOW: go(ui, S_CHECKING); break;
            default: if (setting_change(ui, row, 1)) ui->dirty = 1; break;
            }
        }
        break;
    }
    case S_CHECKING:
        if (!ui->drawn) break;
        update_check_now(ui);
        settings_open(ui, PAGE_UPDATES, 1);
        break;
    case S_ABOUT:
        if (k & (back | accept)) settings_open(ui, 0, 6);
        break;
    case S_SCAN:
        if (!ui->drawn) break;
        ui->source_count = pc5_source_scan(ui->usb_roots, ui->sources, PC5_SOURCES);
        say(ui, "PC5 usb folders=%d", ui->source_count);
        ui->menu = 0;
        ui->list_top = 0;
        memset(ui->ticked, 0, sizeof(ui->ticked));
        go(ui, S_SOURCES);
        break;
    case S_SOURCES:
        if (list_move(&ui->menu, ui->source_count, k)) ui->dirty = 1;
        if (k & back) settings_open(ui, 0, 0);
        else if (k & PC5_KEY_TRIANGLE) go(ui, S_SCAN);
        else if ((k & PC5_KEY_SQUARE) && ui->source_count) {
            ui->ticked[ui->menu] = !ui->ticked[ui->menu];
            ui->dirty = 1;
        } else if ((k & PC5_KEY_OPTIONS) && ticked_count(ui)) {
            if (!queue_begin(ui)) go(ui, S_QUEUE_READ);
            else message(ui, 0, "Out of memory", "PC5 couldn't make room to read the folders.", S_SOURCES);
        } else if ((k & accept) && ui->source_count) {
            go(ui, S_READING);
        }
        break;
    case S_READING:
        if (!ui->drawn) break;
        {
            const Pc5Source *source = &ui->sources[ui->menu];
            Pc5CoverHint hint;
            if (pc5_folder_inspect(source->path, source->name, &ui->folder)) {
                message(ui, 0, "PC5 can't read that folder", "Check that the USB drive is still plugged in.", S_SOURCES);
                break;
            }
            ui->exe = ui->folder.exe_count ? 0 : -1;
            say(ui, "PC5 folder=%s files=%u bytes=%llu exes=%d problem=%s", ui->folder.name, (unsigned)ui->folder.files,
                (unsigned long long)ui->folder.bytes, ui->folder.exe_count, ui->folder.problem);
            hint_for_folder(&ui->folder, ui->exe, &hint);
            pc5_cover_search_start(&ui->search, &hint);
            ui->choice = 0;
            ui->choice_loaded = 0;
            ui->queue_review = -1;
            go(ui, S_SEARCH);
        }
        break;
    case S_SEARCH:
        if (k & back) { go(ui, S_SOURCES); break; }
        if (!ui->drawn) break;
        if (!pc5_cover_search_step(&ui->search, &ui->platform.http)) {
            say(ui, "PC5 cover choices=%d answered=%d", ui->search.count, ui->search.answered);
            ui->choice = ui->search.count ? 0 : -1;
            fetch_review_cover(ui);
            go(ui, S_REVIEW);
        }
        ui->dirty = 1;
        break;
    case S_QUEUE_READ:
        if (k & back) { queue_free(ui); go(ui, S_SOURCES); break; }
        if (!ui->drawn) break;
        if (!queue_read_step(ui)) {
            ui->menu = 0;
            ui->list_top = 0;
            go(ui, S_QUEUE);
        }
        ui->dirty = 1;
        break;
    case S_QUEUE:
        if (list_move(&ui->menu, ui->queue_count, k)) ui->dirty = 1;
        if (k & back) {
            queue_free(ui);
            ui->menu = 0;
            go(ui, S_SOURCES);
        } else if ((k & accept) && ui->queue_count) {
            queue_review_open(ui, ui->menu);
        } else if (k & PC5_KEY_OPTIONS) {
            uint64_t free_bytes = 0, need = queue_needs(ui);
            if (!need) {
                message(ui, 0, "Nothing to install", "None of these folders has a game PC5 can install.", S_QUEUE);
            } else if (!pc5_free_bytes(ui->root, &free_bytes) && free_bytes < need) {
                char needs[32], have[32], body[300];
                pc5_size_text(need, needs, sizeof(needs));
                pc5_size_text(free_bytes, have, sizeof(have));
                snprintf(body, sizeof(body), "Together they need %s and the console has %s free. Go back and untick some.",
                         needs, have);
                message(ui, 0, "Not enough space for all of them", body, S_QUEUE);
            } else {
                ui->queue_at = 0;
                ui->queue_installing = 1;
                ui->queue_stop_all = 0;
                queue_next(ui);
            }
        }
        break;
    case S_REVIEW: {
        int choices = ui->search.count + 1;   /* the card is the last choice */
        if (ui->fetching) {
            if (ui->drawn) fetch_review_cover(ui);
            break;
        }
        if (k & (PC5_KEY_L1 | PC5_KEY_R1)) {
            int at = ui->choice < 0 ? choices - 1 : ui->choice;
            at = (at + (k & PC5_KEY_R1 ? 1 : choices - 1)) % choices;
            ui->choice = at == choices - 1 ? -1 : at;
            start_review_cover(ui);
            ui->drawn = 0;
        } else if ((k & PC5_KEY_SQUARE) && ui->folder.exe_count > 1) {
            ui->menu = ui->exe;
            ui->program_for = 0;
            go(ui, S_PROGRAMS);
        } else if (ui->queue_review >= 0 && (k & (back | accept))) {
            queue_review_close(ui);
        } else if (k & back) {
            ui->menu = 0;
            for (int i = 0; i < ui->source_count; i++)
                if (!strcmp(ui->sources[i].path, ui->folder.path)) ui->menu = i;
            go(ui, S_SOURCES);
        } else if (k & accept) {
            uint64_t free_bytes = 0;
            if (ui->folder.problem[0]) {
                message(ui, 0, "This folder can't be installed", ui->folder.problem, S_REVIEW);
            } else if (!pc5_free_bytes(ui->root, &free_bytes) && free_bytes < pc5_install_needs(&ui->folder)) {
                char need[32], have[32], body[200];
                pc5_size_text(pc5_install_needs(&ui->folder), need, sizeof(need));
                pc5_size_text(free_bytes, have, sizeof(have));
                snprintf(body, sizeof(body), "The game needs %s and the console has %s free.", need, have);
                message(ui, 0, "Not enough space", body, S_REVIEW);
            } else {
                begin_install(ui);
            }
        }
        break;
    }
    case S_PROGRAMS:
        if (list_move(&ui->menu, ui->folder.exe_count, k)) ui->dirty = 1;
        if (k & back) {
            if (ui->program_for) ui->menu = game_row_at(&ui->controls, ui_game_bits(ui), GR_PROGRAM);
            go(ui, ui->program_for ? S_GAME_MENU : S_REVIEW);
        }
        else if (k & accept) {
            if (!ui->program_for) {
                ui->exe = ui->menu;
                go(ui, S_REVIEW);
            } else {
                char error[PC5_ERROR] = "", title[200], body[400];
                int index = pc5_library_find(&ui->library, ui->launch_id);
                const char *name = index >= 0 ? ui->library.games[index].title : "The game";
                if (pc5_game_set_program(ui->root, ui->launch_id, &ui->folder, ui->menu, error, sizeof(error))) {
                    say(ui, "PC5 problem program %s: %s", ui->launch_id, error);
                    snprintf(title, sizeof(title), "%s's program wasn't changed", name);
                    message(ui, 0, title, error, S_LIBRARY);
                } else {
                    say(ui, "PC5 program id=%s exe=%s", ui->launch_id, ui->folder.exes[ui->menu].path);
                    snprintf(title, sizeof(title), "%s starts %s now", name, ui->folder.exes[ui->menu].path);
                    snprintf(body, sizeof(body), "It runs as %d-bit %s.", ui->folder.exes[ui->menu].pe.bits,
                             pc5_api_name(pc5_folder_api(&ui->folder, ui->menu)));
                    library_reload(ui, ui->launch_id);
                    message(ui, 1, title, body, S_LIBRARY);
                }
            }
        }
        break;
    case S_GAME_MENU: {
        int index = pc5_library_find(&ui->library, ui->launch_id);
        GameRow rows[GAME_ROWS];
        int count = game_rows(&ui->controls, ui_game_bits(ui), rows);
        if (index < 0) { go(ui, S_LIBRARY); break; }
        const Pc5Game *game = &ui->library.games[index];
        if (ui->menu >= count) ui->menu = count - 1;
        if (list_move(&ui->menu, count, k)) ui->dirty = 1;
        GameRow row = rows[ui->menu];
        if (k & (back | PC5_KEY_TRIANGLE)) go(ui, S_LIBRARY);
        else if ((k & (PC5_KEY_LEFT | PC5_KEY_RIGHT | accept)) && row == GR_CONTROLLER) {
            int step = (k & PC5_KEY_LEFT) ? -1 : 1;
            ui->controls.mode = (Pc5PadMode)((ui->controls.mode + PC5_PAD_MODES + step) % PC5_PAD_MODES);
            controls_changed(ui);
            ui->dirty = 1;
        } else if ((k & (PC5_KEY_LEFT | PC5_KEY_RIGHT | accept)) && row == GR_FRAMEGEN) {
            ui->controls.frame_generation = !ui->controls.frame_generation;
            controls_changed(ui);
            ui->dirty = 1;
        } else if ((k & (PC5_KEY_LEFT | PC5_KEY_RIGHT | accept)) && row == GR_MEMORY) {
            ui->controls.memory_2gb = !ui->controls.memory_2gb;
            controls_changed(ui);
            ui->dirty = 1;
        } else if ((k & accept) && row == GR_HOME) {
            home_toggle(ui, game);
        } else if (k & accept) {
            if (row == GR_PLAY) {
                if (game->playable) go(ui, S_LAUNCH);
                else message(ui, 0, "This game can't start", game->problem[0] ? game->problem : "Something it needs is missing.",
                             S_GAME_MENU);
            } else if (row == GR_BUTTONS) {
                ui->pad_row = ui->pad_top = 0;
                go(ui, S_BUTTONS);
            } else if (row == GR_PROGRAM) {
                if (!game->pc5) message(ui, 0, "PC5 didn't install this game",
                                        "Its program is set in its prospero-win profile.", S_GAME_MENU);
                else go(ui, S_GAME_READING);
            } else {
                ui->menu = index;
                ui->button = 0;   /* Cancel first */
                ui->uninstall_back = S_GAME_MENU;
                go(ui, S_UNINSTALL_CONFIRM);
            }
        }
        break;
    }
    case S_BUTTONS: {
        PadRow rows[PAD_ROWS];
        int count = pad_rows(&ui->controls, rows), changed = 0;
        if (ui->pad_row >= count) ui->pad_row = count - 1;
        if (list_move(&ui->pad_row, count, k)) ui->dirty = 1;
        PadRow row = rows[ui->pad_row];
        int bindable = row.kind == PR_BUTTON || row.kind == PR_DIRECTION;
        if (k & back) {
            ui->menu = game_row_at(&ui->controls, ui_game_bits(ui), GR_BUTTONS);
            go(ui, S_GAME_MENU);
        } else if (k & (PC5_KEY_LEFT | PC5_KEY_RIGHT)) {
            changed = pad_change(ui, row, (k & PC5_KEY_LEFT) ? -1 : 1);
        } else if ((k & PC5_KEY_SQUARE) && bindable) {
            ui->controls.key[row.index] = (uint8_t)(pc5_key_count - 1);   /* Nothing */
            changed = 1;
        } else if (k & accept) {
            if (bindable) {
                ui->key_control = row.index;
                ui->key_choice = ui->controls.key[row.index] % pc5_key_count;
                go(ui, S_KEYS);
            } else if (row.kind == PR_RESET) {
                Pc5PadMode mode = ui->controls.mode;
                pc5_controls_default(&ui->controls);
                ui->controls.mode = mode;
                changed = 1;
            } else if (row.kind == PR_SPEED) {
                ui->controls.speed = ui->controls.speed % PC5_SPEEDS + 1;
                changed = 1;
            } else {
                changed = pad_change(ui, row, 1);
            }
        }
        if (changed) {
            controls_changed(ui);
            ui->dirty = 1;
        }
        count = pad_rows(&ui->controls, rows);
        if (ui->pad_row >= count) ui->pad_row = count - 1;
        if (ui->pad_row < ui->pad_top) ui->pad_top = ui->pad_row;
        if (ui->pad_row >= ui->pad_top + PAD_VISIBLE) ui->pad_top = ui->pad_row - PAD_VISIBLE + 1;
        if (ui->pad_top > count - PAD_VISIBLE) ui->pad_top = count > PAD_VISIBLE ? count - PAD_VISIBLE : 0;
        break;
    }
    case S_KEYS: {
        int before = ui->key_choice, n = pc5_key_count;
        if ((k & PC5_KEY_LEFT) && ui->key_choice % KEY_COLS) ui->key_choice--;
        if ((k & PC5_KEY_RIGHT) && ui->key_choice % KEY_COLS < KEY_COLS - 1 && ui->key_choice + 1 < n) ui->key_choice++;
        if ((k & PC5_KEY_UP) && ui->key_choice >= KEY_COLS) ui->key_choice -= KEY_COLS;
        if (k & PC5_KEY_DOWN) {
            if (ui->key_choice + KEY_COLS < n) ui->key_choice += KEY_COLS;
            else if (ui->key_choice / KEY_COLS < (n - 1) / KEY_COLS) ui->key_choice = n - 1;
        }
        if (ui->key_choice != before) ui->dirty = 1;
        if (k & back) go(ui, S_BUTTONS);
        else if (k & accept) {
            ui->controls.key[ui->key_control] = (uint8_t)ui->key_choice;
            controls_changed(ui);
            go(ui, S_BUTTONS);
        }
        break;
    }
    case S_GAME_READING:
        if (!ui->drawn) break;
        {
            char folder[PC5_PATH];
            int index = pc5_library_find(&ui->library, ui->launch_id);
            Pc5Info info;
            if (index < 0 || pc5_game_folder(ui->root, ui->launch_id, folder, sizeof(folder)) ||
                pc5_info_read(ui->root, ui->launch_id, &info) ||
                pc5_folder_inspect(folder, info.folder, &ui->folder)) {
                message(ui, 0, "PC5 can't read the game's folder", "Its files may be missing: uninstall it and install it again.",
                        S_GAME_MENU);
                break;
            }
            if (ui->folder.exe_count < 2) {
                message(ui, 0, "This game has one program", "There is no other program in its folder to start.", S_GAME_MENU);
                break;
            }
            ui->menu = 0;
            for (int i = 0; i < ui->folder.exe_count; i++)
                if (!strcmp(ui->folder.exes[i].path, info.exe)) ui->menu = i;
            ui->exe = ui->menu;
            ui->list_top = 0;
            ui->program_for = 1;
            go(ui, S_PROGRAMS);
        }
        break;
    case S_INSTALL:
        if ((k & back) && ui->install.phase < PC5_INSTALL_FINISH) {
            ui->button = 0;
            go(ui, S_STOP);
            break;
        }
        install_progress(ui);
        break;
    case S_STOP:
        if (k & (PC5_KEY_LEFT | PC5_KEY_RIGHT)) { ui->button = !ui->button; ui->dirty = 1; }
        if (k & back) go(ui, S_INSTALL);
        else if ((k & accept) && ui->now - ui->entered > 300) {
            if (ui->queue_installing) {
                /* Stop this game, or Stop all. */
                pc5_install_cancel(&ui->install);
                ui->queue_stop_all = ui->button;
            } else if (ui->button) {
                pc5_install_cancel(&ui->install);
            }
            go(ui, S_INSTALL);
        }
        break;
    case S_UNINSTALL_LIST:
        if (list_move(&ui->menu, ui->library.count, k)) ui->dirty = 1;
        if (k & back) settings_open(ui, 0, 1);
        else if ((k & accept) && ui->library.count) {
            ui->button = 0;   /* Cancel first */
            ui->uninstall_back = S_UNINSTALL_LIST;
            go(ui, S_UNINSTALL_CONFIRM);
        }
        if (covers_near(ui)) ui->dirty = 1;
        break;
    case S_UNINSTALL_CONFIRM:
        if (k & (PC5_KEY_LEFT | PC5_KEY_RIGHT)) { ui->button = !ui->button; ui->dirty = 1; }
        if (k & back) {
            if (ui->uninstall_back == S_GAME_MENU) ui->menu = game_row_at(&ui->controls, ui_game_bits(ui), GR_UNINSTALL);
            go(ui, ui->uninstall_back);
        } else if ((k & accept) && ui->now - ui->entered > 400) {
            if (!ui->button) {
                if (ui->uninstall_back == S_GAME_MENU) ui->menu = game_row_at(&ui->controls, ui_game_bits(ui), GR_UNINSTALL);
                go(ui, ui->uninstall_back);
            } else {
                const Pc5Game *game = &ui->library.games[ui->menu];
                pc5_copy(ui->removing_title, sizeof(ui->removing_title), game->title);
                say(ui, "PC5 uninstall id=%s prefix=%s", game->id, game->prefix);
                /* Its home screen tile would start nothing now. */
                ui->removing_tiles = ui->config.tiles_root ? pc5_home_remove(ui->config.tiles_root, game->id) : 0;
                if (ui->removing_tiles) say(ui, "PC5 home tile removed id=%s count=%d", game->id, ui->removing_tiles);
                pc5_remove_begin(&ui->remove, ui->root, game, &ui->library);
                covers_clear(ui);
                go(ui, S_REMOVING);
            }
        }
        break;
    case S_REMOVING:
        if (!ui->remove.failed && pc5_remove_step(&ui->remove, 14)) { ui->dirty = 1; break; }
        {
            char title[200], body[400];
            int failed = ui->remove.failed;
            snprintf(title, sizeof(title), failed ? "%s wasn't fully uninstalled" : "%s is uninstalled", ui->removing_title);
            if (failed) snprintf(body, sizeof(body), "%s PC5 finishes deleting it the next time it starts.", ui->remove.error);
            else snprintf(body, sizeof(body), "The game, its saves and its settings were deleted.%s",
                          ui->removing_tiles > 0 ? " So was its home screen tile's folder: if the tile is still on "
                                                   "the home screen, delete it there." : "");
            if (failed) say(ui, "PC5 problem uninstall %s: %s", ui->removing_title, ui->remove.error);
            say(ui, "PC5 uninstall done failed=%d removed=%u", failed, (unsigned)ui->remove.removed);
            pc5_remove_end(&ui->remove);
            library_reload(ui, NULL);
            if (ui->selected >= ui->library.count) ui->selected = ui->library.count ? ui->library.count - 1 : 0;
            message(ui, !failed, title, body, S_LIBRARY);
        }
        break;
    case S_MESSAGE:
        if (k & (accept | back)) {
            if (ui->back_to == S_LIBRARY && ui->issue_pending) issue_message(ui);
            else go(ui, ui->back_to);
        }
        break;
    case S_LAUNCH:
        if (!ui->drawn) break;
        {
            int index = pc5_library_find(&ui->library, ui->launch_id);
            char error[PC5_ERROR] = "", title[200];
            if (index >= 0 && ui->library.games[index].pc5 &&
                pc5_profile_refresh(ui->root, ui->launch_id, error, sizeof(error))) {
                snprintf(title, sizeof(title), "%s can't start", ui->library.games[index].title);
                say(ui, "PC5 problem play %s: %s", ui->launch_id, error);
                message(ui, 0, title, error, S_LIBRARY);
                break;
            }
            remember_played(ui, ui->launch_id);
            say(ui, "PC5 play id=%s", ui->launch_id);
            if (ui->platform.play) ui->platform.play(ui->platform.context, ui->launch_id);
            snprintf(title, sizeof(title), "%s didn't start", index >= 0 ? ui->library.games[index].title : "The game");
            say(ui, "PC5 problem play %s: the console refused to start it", ui->launch_id);
            message(ui, 0, title, "The console refused to start it. Try again, or restart PC5.", S_LIBRARY);
        }
        break;
    }

    /* The covers slide; the glow follows the chosen cover's colour. */
    if (ui->screen != S_START && ui->screen != S_COVERS && ui->screen != S_UPDATE && ui->screen != S_LIBRARY &&
        ui->screen != S_UNINSTALL_LIST && covers_near(ui))
        ui->dirty = 1;
    if (ui->library.count) {
        float target = (float)ui->selected, gap = target - ui->scroll;
        if (fabsf(gap) > 0.001f) {
            ui->scroll += gap * (1 - expf(-dt * 13));
            if (fabsf(target - ui->scroll) < 0.002f) ui->scroll = target;
            ui->dirty = 1;
        }
        const Cover *cover = &ui->covers[ui->selected];
        uint32_t tint = cover->state ? cover->tint : C_DEFAULT_TINT;
        for (int c = 0; c < 3; c++) {
            ui->glow_target[c] = (float)((tint >> (16 - 8 * c)) & 255);
            float d = ui->glow_target[c] - ui->glow[c];
            if (fabsf(d) > 0.5f) { ui->glow[c] += d * (1 - expf(-dt * 6)); ui->dirty = 1; }
            else ui->glow[c] = ui->glow_target[c];
        }
    }
    /* The clock, once a second. */
    if (ui->platform.clock && now_ms - ui->clock_checked >= 1000) {
        char clock[32];
        ui->clock_checked = now_ms;
        ui->platform.clock(ui->platform.context, clock, sizeof(clock));
        if (strcmp(clock, ui->clock)) { pc5_copy(ui->clock, sizeof(ui->clock), clock); ui->dirty = 1; }
    }
    /* Spinners turn. */
    if (ui->screen == S_START || ui->screen == S_COVERS || ui->screen == S_SCAN || ui->screen == S_READING ||
        ui->screen == S_SEARCH || ui->screen == S_REMOVING || ui->screen == S_LAUNCH || ui->screen == S_UPDATE ||
        ui->screen == S_CHECKING || ui->screen == S_GAME_READING || ui->screen == S_QUEUE_READ || ui->fetching)
        ui->dirty = 1;
    int dirty = ui->dirty;
    ui->dirty = 0;
    return dirty;
}

/* ---- drawing: pieces ------------------------------------------------------------------------- */

typedef enum Glyph { G_CROSS, G_CIRCLE, G_SQUARE, G_TRIANGLE, G_OPTIONS, G_L1, G_R1, G_L1R1 } Glyph;

static int button_glyph(Pc5Ui *ui, Pc5Canvas *c, Glyph glyph, int x, int y)
{
    const float r = 16, cx = x + r, cy = y - 9;

    if (glyph == G_OPTIONS || glyph == G_L1 || glyph == G_R1) {
        const char *text = glyph == G_OPTIONS ? "OPTIONS" : glyph == G_L1 ? "L1" : "R1";
        int tw = pc5_text_width(&ui->fonts, PC5_FACE_BUTTON, text), w = tw + 24;
        pc5_round_frame(c, x, cy - 15, w, 30, 15, 2, C_ICON);
        pc5_text(c, &ui->fonts, PC5_FACE_BUTTON, x + w / 2, (int)cy + 6, C_ICON, text, 0, 1);
        return w;
    }
    if (glyph == G_L1R1) {
        int w1 = button_glyph(ui, c, G_L1, x, y);
        return w1 + 8 + button_glyph(ui, c, G_R1, x + w1 + 8, y);
    }
    pc5_ring(c, cx, cy, r, 2, C_ICON);
    switch (glyph) {
    case G_CROSS:
        pc5_line(c, cx - 6, cy - 6, cx + 6, cy + 6, 2.6f, C_ICON);
        pc5_line(c, cx - 6, cy + 6, cx + 6, cy - 6, 2.6f, C_ICON);
        break;
    case G_CIRCLE: pc5_ring(c, cx, cy, 7, 2.4f, C_ICON); break;
    case G_SQUARE: pc5_round_frame(c, cx - 6.5f, cy - 6.5f, 13, 13, 1.5f, 2.4f, C_ICON); break;
    case G_TRIANGLE:
        pc5_line(c, cx, cy - 7.5f, cx - 7, cy + 5, 2.4f, C_ICON);
        pc5_line(c, cx - 7, cy + 5, cx + 7, cy + 5, 2.4f, C_ICON);
        pc5_line(c, cx + 7, cy + 5, cx, cy - 7.5f, 2.4f, C_ICON);
        break;
    default: break;
    }
    return (int)(2 * r);
}

typedef struct Hint { Glyph glyph; const char *label; } Hint;

/* Accept and Back follow the console's setting. */
static Glyph accept_glyph(Pc5Ui *ui) { return ui->platform.circle_accepts ? G_CIRCLE : G_CROSS; }
static Glyph back_glyph(Pc5Ui *ui) { return ui->platform.circle_accepts ? G_CROSS : G_CIRCLE; }

static void hints(Pc5Ui *ui, Pc5Canvas *c, int x, int y, const Hint *list, int count)
{
    for (int i = 0; i < count; i++) {
        x += button_glyph(ui, c, list[i].glyph, x, y) + 12;
        x += pc5_text(c, &ui->fonts, PC5_FACE_BODY, x, y, C_DIM, list[i].label, 0, 0) + 40;
    }
}

static void spinner(Pc5Canvas *c, float cx, float cy, float r, uint64_t now, uint32_t color)
{
    float a = (float)(now % 1100) / 1100.0f * 6.2831853f;
    pc5_ring(c, cx, cy, r, 4, 0x30ffffffu);
    pc5_arc(c, cx, cy, r, 4, a, a + 1.9f, color);
}

static void dim(Pc5Canvas *c, int alpha)
{
    pc5_round_rect(c, 0, 0, W, H, 0, (uint32_t)alpha << 24);
}

static void panel(Pc5Canvas *c, float x, float y, float w, float h)
{
    pc5_shadow(c, x, y, w, h, 22, 40, 0x90000000u);
    pc5_round_rect(c, x, y, w, h, 22, C_PANEL);
    pc5_round_frame(c, x, y, w, h, 22, 1.5f, C_EDGE);
}

static void wordmark(Pc5Ui *ui, Pc5Canvas *c, int x, int y)
{
    int w = pc5_text(c, &ui->fonts, PC5_FACE_HEAD, x, y, C_TEXT, "PC", 0, 0);
    pc5_text(c, &ui->fonts, PC5_FACE_HEAD, x + w + 1, y, C_ACCENT, "5", 0, 0);
}

static void progress_bar(Pc5Canvas *c, float x, float y, float w, double fraction, uint32_t color)
{
    if (fraction < 0) fraction = 0;
    if (fraction > 1) fraction = 1;
    pc5_round_rect(c, x, y, w, 12, 6, 0xff262b35u);
    if (fraction > 0) pc5_round_rect(c, x, y, (float)(w * fraction < 12 ? 12 : w * fraction), 12, 6, color);
}

/* ---- drawing: the library ------------------------------------------------------------------- */

/* The background with the glow in its current colour: made again only when
 * the colour moves, copied otherwise. */
static void backdrop(Pc5Ui *ui, Pc5Canvas *c)
{
    uint32_t glow = pc5_rgba((int)ui->glow[0], (int)ui->glow[1], (int)ui->glow[2], 255);

    if (!ui->backdrop || !ui->composed || c->w != W || c->h != H) { pc5_clear(c, 0xff07080au); return; }
    if (glow != ui->composed_glow || !ui->composed_ready) {
        uint32_t add[256];
        for (int k = 0; k < 256; k++) {
            int r = (int)((glow >> 16) & 255) * k / 255, g = (int)((glow >> 8) & 255) * k / 255, b = (int)(glow & 255) * k / 255;
            add[k] = (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
        }
        for (size_t i = 0; i < (size_t)W * H; i++) {
            /* The backdrop is dark (every channel under 32) and the glow at most 255 * 0.62 + 64: no
             * channel carries into the next. */
            ui->composed[i] = ui->backdrop[i] + add[ui->falloff[i]];
        }
        ui->composed_glow = glow;
        ui->composed_ready = 1;
    }
    for (int y = 0; y < H; y++)
        memcpy(c->px + (size_t)y * (size_t)c->stride, ui->composed + (size_t)y * W, (size_t)W * 4);
}

/* Where a cover sits d places from the middle. */
static void slot(float d, float *x, float *scale, float *bright)
{
    static const float xs[] = { 0, 372, 612, 818, 1000, 1165, 1320 }, ss[] = { 1, 0.66f, 0.58f, 0.52f, 0.48f, 0.45f, 0.43f },
                       bs[] = { 1, 0.58f, 0.44f, 0.34f, 0.26f, 0.0f, 0.0f };
    float a = fabsf(d);
    int i = (int)a;
    float t = a - (float)i;
    if (i >= 6) { *x = xs[6]; *scale = ss[6]; *bright = 0; }
    else {
        *x = xs[i] + (xs[i + 1] - xs[i]) * t;
        *scale = ss[i] + (ss[i + 1] - ss[i]) * t;
        *bright = bs[i] + (bs[i + 1] - bs[i]) * t;
    }
    if (d < 0) *x = -*x;
}

static void carousel(Pc5Ui *ui, Pc5Canvas *c)
{
    int order[16], n = 0;
    int first = (int)floorf(ui->scroll) - 6, last = (int)ceilf(ui->scroll) + 6;

    for (int i = first; i <= last; i++)
        if (i >= 0 && i < ui->library.count && n < 16) order[n++] = i;
    /* farthest first */
    for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++)
            if (fabsf(order[b] - ui->scroll) > fabsf(order[a] - ui->scroll)) { int t = order[a]; order[a] = order[b]; order[b] = t; }
    for (int k = 0; k < n; k++) {
        int i = order[k];
        float d = (float)i - ui->scroll, x, scale, bright;
        slot(d, &x, &scale, &bright);
        if (bright <= 0.01f) continue;
        float t = fabsf(d) < 1 ? fabsf(d) : 1;
        float w = COVER_W * scale * (1 - 0.16f * t), h = COVER_H * scale, outer = h * (1 - 0.12f * t);
        float cx = 960 + x, left = cx - w / 2, cy = FLOOR_Y - h / 2;
        float hl = d < 0 ? outer : h, hr = d < 0 ? h : outer;
        const Cover *cover = &ui->covers[i];
        int near = fabsf(d) < 0.5f;
        if (near) {
            uint32_t tint = pc5_rgba((int)ui->glow[0], (int)ui->glow[1], (int)ui->glow[2], 150);
            pc5_shadow(c, left, cy - h / 2, w, h, 14, 70, tint);
        }
        if (cover->state && cover->picture.levels) {
            pc5_draw_cover(c, &cover->picture, left, cy, w, hl, hr, 12 * scale, (int)(bright * 256), 256, 150 * scale);
        } else {
            /* not loaded yet: a plain dark card */
            pc5_round_rect(c, left, cy - h / 2, w, h, 12 * scale, pc5_rgba(28, 32, 40, (int)(255 * bright)));
        }
        if (near) {
            float gap = 7, a = 1 - fabsf(d) * 2;
            pc5_round_frame(c, left - gap, cy - h / 2 - gap, w + 2 * gap, h + 2 * gap, 12 * scale + gap, 2.5f,
                            pc5_rgba(255, 255, 255, (int)(215 * a)));
        }
    }
}

static void library_screen(Pc5Ui *ui, Pc5Canvas *c)
{
    char count[48];

    backdrop(ui, c);
    wordmark(ui, c, MARGIN, 92);
    if (ui->clock[0]) pc5_text(c, &ui->fonts, PC5_FACE_LABEL, W - MARGIN, 88, C_TEXT, ui->clock, 0, 2);
    if (!ui->library.count) {
        pc5_text(c, &ui->fonts, PC5_FACE_TITLE, 960, 470, C_TEXT, "No games yet", 0, 1);
        pc5_text(c, &ui->fonts, PC5_FACE_BODY, 960, 540, C_DIM, "Press OPTIONS and choose Install game from USB.", 0, 1);
        Hint list[] = { { G_OPTIONS, "Settings" } };
        hints(ui, c, MARGIN, 1012, list, 1);
        return;
    }
    carousel(ui, c);
    const Pc5Game *game = &ui->library.games[ui->selected];
    pc5_text(c, &ui->fonts, PC5_FACE_TITLE, 960, 830, C_TEXT, game->title, 1500, 1);
    if (!game->playable) pc5_text(c, &ui->fonts, PC5_FACE_BODY, 960, 884, C_WARN, game->problem, 1500, 1);
    else pc5_text(c, &ui->fonts, PC5_FACE_BODY, 960, 884, C_DIM, game->detail, 1500, 1);
    Hint list[] = { { accept_glyph(ui), "Play" }, { G_TRIANGLE, "Game menu" }, { G_L1R1, "Jump" }, { G_OPTIONS, "Settings" } };
    hints(ui, c, MARGIN, 1012, list, 4);
    snprintf(count, sizeof(count), ui->library.count == 1 ? "%d game" : "%d games", ui->library.count);
    pc5_text(c, &ui->fonts, PC5_FACE_BODY, W - MARGIN, 1012, C_FAINT, count, 0, 2);
}

/* ---- drawing: the other screens ---------------------------------------------------------- */

static void busy_dialog(Pc5Ui *ui, Pc5Canvas *c, const char *title, const char *line, int can_back)
{
    float w = 900, h = 300, x = (W - w) / 2, y = (H - h) / 2;
    dim(c, 150);
    panel(c, x, y, w, h);
    spinner(c, x + 100, y + h / 2, 30, ui->now, C_TEXT);
    pc5_text(c, &ui->fonts, PC5_FACE_HEAD, (int)x + 170, (int)y + 136, C_TEXT, title, (int)w - 220, 0);
    if (line) pc5_text(c, &ui->fonts, PC5_FACE_BODY, (int)x + 170, (int)y + 190, C_DIM, line, (int)w - 220, 0);
    if (can_back) {
        Hint list[] = { { back_glyph(ui), "Back" } };
        hints(ui, c, (int)x + 170, (int)(y + h - 34), list, 1);
    }
}

/* The drawer on the right: Settings and a game's menu. */
static float drawer(Pc5Ui *ui, Pc5Canvas *c, const char *title)
{
    float x = W - 760;
    dim(c, 140);
    pc5_shadow(c, x, -40, 820, H + 80, 0, 50, 0x90000000u);
    pc5_round_rect(c, x, -40, 820, H + 80, 0, C_PANEL);
    pc5_line(c, x, 0, x, H, 1.5f, C_EDGE);
    pc5_text(c, &ui->fonts, PC5_FACE_HEAD, (int)x + 64, 140, C_TEXT, title, 760 - 128, 0);
    return x;
}

static void drawer_row(Pc5Ui *ui, Pc5Canvas *c, float x, float y, float h, int chosen, const char *label,
                       const char *about, const char *value)
{
    int value_w = value && value[0] ? pc5_text_width(&ui->fonts, PC5_FACE_LABEL, value) : 0;
    if (chosen) {
        pc5_round_rect(c, x + 36, y, 760 - 72, h - 12, 16, C_ROW);
        pc5_round_rect(c, x + 36, y + (h - 12) / 2 - 28, 5, 56, 2.5f, C_TEXT);
    }
    pc5_text(c, &ui->fonts, PC5_FACE_LABEL, (int)x + 72, (int)y + 46, C_TEXT, label, 600 - value_w, 0);
    if (about && about[0]) pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)x + 72, (int)y + 82, C_DIM, about, 760 - 144, 0);
    if (value_w) pc5_text(c, &ui->fonts, PC5_FACE_LABEL, (int)x + 760 - 72, (int)y + 46, chosen ? C_TEXT : C_ACCENT, value, 0, 2);
}

static void settings_screen(Pc5Ui *ui, Pc5Canvas *c)
{
    float x = drawer(ui, c, page_titles[ui->page]);
    SettingRow rows[PAGE_ROWS];
    int count = page_rows(&ui->settings, ui->page, rows);
    float pitch = count > 6 ? 106 : 118;   /* seven rows fit above the hints */

    for (int i = 0; i < count; i++) {
        const char *label = "";
        char about[160], value[64];
        setting_text(ui, rows[i], &label, about, sizeof(about), value, sizeof(value));
        drawer_row(ui, c, x, 196 + pitch * i, pitch, i == ui->menu, label, about, value);
    }
    if (ui->page == PAGE_UPDATES && ui->update.last.state == PC5_UPDATE_AVAILABLE)
        pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, (int)x + 72, 196 + (int)pitch * count + 60, 760 - 144, 4, 42, C_DIM,
                      "Get it from github.com/bornaradusin/PC5 (Releases) and copy it to the console the way you "
                      "added PC5. Your games, saves and settings stay.", 0);
    SettingRow row = rows[ui->menu < count ? ui->menu : 0];
    int changes = row == SR_KEEP_LOGS || (row >= SR_FPS && row <= SR_TRIGGER_RESISTANCE) || row == SR_CHECK_ON_OPEN;
    Hint list[] = { { accept_glyph(ui), changes ? "Change" : "Choose" }, { back_glyph(ui), "Back" } };
    hints(ui, c, (int)x + 64, 1012, list, 2);
}

static void game_menu_screen(Pc5Ui *ui, Pc5Canvas *c, int chosen)
{
    int index = pc5_library_find(&ui->library, ui->launch_id);
    if (index < 0) return;
    const Pc5Game *game = &ui->library.games[index];
    float x = drawer(ui, c, game->title);
    char about[200];

    GameRow rows[GAME_ROWS];
    int count = game_rows(&ui->controls, ui_game_bits(ui), rows);

    float pitch = count > 7 ? 92 : count > 6 ? 104 : 118;   /* eight rows fit above the hints */
    pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)x + 64, 182, C_DIM, game->playable ? game->detail : game->problem, 760 - 128, 0);
    for (int i = 0; i < count; i++) {
        float y = 226 + pitch * i;
        switch (rows[i]) {
        case GR_PLAY:
            drawer_row(ui, c, x, y, pitch, chosen == i, "Play", "To leave it, hold the touchpad and OPTIONS for 2 s", "");
            break;
        case GR_CONTROLLER:
            drawer_row(ui, c, x, y, pitch, chosen == i, "Controller",
                       ui->controls.mode == PC5_PAD_DUALSENSE ? "The game sees a DualSense, as one plugged into a PC"
                       : ui->controls.mode == PC5_PAD_KEYBOARD ? "The buttons press keys, the sticks move the mouse"
                                                               : "Games with controller support see an Xbox controller",
                       pc5_pad_mode_name(ui->controls.mode));
            break;
        case GR_BUTTONS:
            drawer_row(ui, c, x, y, pitch, chosen == i, "Button mapping", "The key or mouse button each one sends",
                       "\xe2\x80\xba");
            break;
        case GR_FRAMEGEN:
            drawer_row(ui, c, x, y, pitch, chosen == i, "Frame generation",
                       ui->controls.frame_generation ? "Held at 30 fps and shown at 60"
                                                     : "The game's frames as it makes them",
                       ui->controls.frame_generation ? "30 \xe2\x86\x92 60 fps" : "Off");
            break;
        case GR_MEMORY:
            drawer_row(ui, c, x, y, pitch, chosen == i, "Memory",
                       ui->controls.memory_2gb ? "What Windows gives a 32-bit game that doesn't ask for more"
                                               : "Room for big textures and mods; 2 GB if the game misbehaves",
                       ui->controls.memory_2gb ? "2 GB" : "4 GB");
            break;
        case GR_PROGRAM: {
            const char *exe = strrchr(game->executable, '\\');
            snprintf(about, sizeof(about), "Now: %s", exe ? exe + 1 : game->executable);
            drawer_row(ui, c, x, y, pitch, chosen == i, "Change program", about, "");
            break;
        }
        case GR_HOME:
            if (ui->tile[0]) snprintf(about, sizeof(about), "Its tile, %s, starts the game", ui->tile);
            drawer_row(ui, c, x, y, pitch, chosen == i, "Home screen tile",
                       ui->tile[0] ? about : "A tile on the PS5's home screen that starts it", ui->tile[0] ? "On" : "Off");
            break;
        case GR_UNINSTALL:
            drawer_row(ui, c, x, y, pitch, chosen == i, "Uninstall", "Deletes the game with its saves and settings", "");
            break;
        }
    }
    Hint list[] = { { accept_glyph(ui), chosen < count && (rows[chosen] == GR_CONTROLLER || rows[chosen] == GR_FRAMEGEN ||
                                                           rows[chosen] == GR_HOME)
                                            ? "Change" : "Choose" },
                    { back_glyph(ui), "Back" } };
    hints(ui, c, (int)x + 64, 1012, list, 2);
}

/* Button mapping: what each button and stick does while the game plays the keyboard and mouse. */
static void buttons_screen(Pc5Ui *ui, Pc5Canvas *c)
{
    float x = drawer(ui, c, "Button mapping");
    PadRow rows[PAD_ROWS];
    int count = pad_rows(&ui->controls, rows);
    const Pc5Controls *k = &ui->controls;

    pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)x + 64, 182, C_DIM,
             "Touchpad + OPTIONS for 2 s still leaves the game", 760 - 128, 0);
    for (int i = ui->pad_top; i < count && i < ui->pad_top + PAD_VISIBLE; i++) {
        float y = 214 + PAD_PITCH * (i - ui->pad_top);
        char label[64], value[64];
        PadRow row = rows[i];
        value[0] = 0;
        switch (row.kind) {
        case PR_STICK:
            pc5_copy(label, sizeof(label), row.index ? "Right stick" : "Left stick");
            pc5_copy(value, sizeof(value), pc5_stick_use_name(k->stick[row.index]));
            break;
        case PR_DIRECTION:
            snprintf(label, sizeof(label), "    %s", pc5_control_name(row.index));
            pc5_copy(value, sizeof(value), pc5_key_label(k->key[row.index]));
            break;
        case PR_SPEED:
            pc5_copy(label, sizeof(label), "Pointer speed");
            snprintf(value, sizeof(value), "%d of %d", k->speed, PC5_SPEEDS);
            break;
        case PR_BUTTON:
            pc5_copy(label, sizeof(label), pc5_control_name(row.index));
            pc5_copy(value, sizeof(value), pc5_key_label(k->key[row.index]));
            break;
        case PR_RESET:
            pc5_copy(label, sizeof(label), "Reset to default");
            break;
        }
        drawer_row(ui, c, x, y, PAD_PITCH, i == ui->pad_row, label, NULL, value);
    }
    /* More above or below. */
    if (ui->pad_top > 0) pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)x + 760 - 72, 206, C_FAINT, "\xe2\x80\xa6", 0, 2);
    if (ui->pad_top + PAD_VISIBLE < count)
        pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)x + 760 - 72, 214 + PAD_PITCH * PAD_VISIBLE + 4, C_FAINT,
                 "\xe2\x80\xa6", 0, 2);
    PadRow row = rows[ui->pad_row < count ? ui->pad_row : 0];
    int bindable = row.kind == PR_BUTTON || row.kind == PR_DIRECTION;
    Hint list[] = { { accept_glyph(ui), bindable ? "Choose a key" : row.kind == PR_RESET ? "Reset" : "Change" },
                    { G_SQUARE, "Nothing" }, { back_glyph(ui), "Back" } };
    if (bindable) hints(ui, c, (int)x + 64, 1012, list, 3);
    else {
        list[1] = list[2];
        hints(ui, c, (int)x + 64, 1012, list, 2);
    }
}

/* The key picker: every key, mouse button and Nothing, eight across. */
static void keys_dialog(Pc5Ui *ui, Pc5Canvas *c)
{
    const float cell_w = 176, cell_h = 54, gap = 10;
    int rows = (pc5_key_count + KEY_COLS - 1) / KEY_COLS;
    float w = KEY_COLS * (cell_w + gap) - gap + 120, h = 150 + rows * (cell_h + gap) - gap + 110;
    float x = (W - w) / 2, y = (H - h) / 2;
    char title[96];

    dim(c, 150);
    panel(c, x, y, w, h);
    snprintf(title, sizeof(title), "What %s sends", pc5_control_name(ui->key_control));
    pc5_text(c, &ui->fonts, PC5_FACE_HEAD, (int)x + 60, (int)y + 92, C_TEXT, title, (int)w - 120, 0);
    for (int i = 0; i < pc5_key_count; i++) {
        float cx = x + 60 + (i % KEY_COLS) * (cell_w + gap), cy = y + 140 + (i / KEY_COLS) * (cell_h + gap);
        int chosen = i == ui->key_choice, now = i == ui->controls.key[ui->key_control];
        pc5_round_rect(c, cx, cy, cell_w, cell_h, 12, chosen ? C_TEXT : C_ROW);
        if (now && !chosen) pc5_round_frame(c, cx, cy, cell_w, cell_h, 12, 2.5f, C_ACCENT);
        pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)(cx + cell_w / 2), (int)cy + 36, chosen ? C_PANEL : C_TEXT,
                 pc5_key_label(i), (int)cell_w - 16, 1);
    }
    Hint list[] = { { accept_glyph(ui), "Choose" }, { back_glyph(ui), "Back" } };
    hints(ui, c, (int)x + 60, (int)(y + h - 40), list, 2);
}

static void about_screen(Pc5Ui *ui, Pc5Canvas *c)
{
    float w = 1180, h = 700, x = (W - w) / 2, y = (H - h) / 2;
    char line[200], space[32];
    uint64_t free_bytes = 0;
    int ty = (int)y + 196;
    static const char *const paragraphs[] = {
        ("PC5 runs your PC games with prospero-win: Wine, DXVK for Direct3D 8 to 11, VKD3D-Proton for "
         "Direct3D 12, and Mesa (RADV for Vulkan, Zink for OpenGL)."),
        "Covers come from Steam, GOG and Wikipedia, with no account. No game is part of PC5.",
        "Free software under the GNU LGPL 2.1 or later.",
    };

    dim(c, 150);
    panel(c, x, y, w, h);
    wordmark(ui, c, (int)x + 64, (int)y + 110);
    pc5_text(c, &ui->fonts, PC5_FACE_BODY, (int)x + 160, (int)y + 106, C_DIM,
             "version " PC5_VERSION " \xc2\xb7 " PC5_TITLE_ID, 0, 0);
    for (int i = 0; i < 3; i++)
        ty += 42 * pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, (int)x + 64, ty, (int)w - 128, 3, 42, C_TEXT,
                                 paragraphs[i], 0) + 22;
    snprintf(line, sizeof(line), "Games are kept in %.150s", ui->root);
    pc5_text(c, &ui->fonts, PC5_FACE_BODY, (int)x + 64, ty + 20, C_DIM, line, (int)w - 128, 0);
    if (!pc5_free_bytes(ui->root, &free_bytes)) {
        pc5_size_text(free_bytes, space, sizeof(space));
        snprintf(line, sizeof(line), "%s free on the console", space);
        pc5_text(c, &ui->fonts, PC5_FACE_BODY, (int)x + 64, ty + 68, C_DIM, line, (int)w - 128, 0);
    }
    Hint list[] = { { back_glyph(ui), "Back" } };
    hints(ui, c, (int)x + 64, (int)(y + h - 48), list, 1);
}

/* A list in a large panel: rows of a title and a line under it. */
typedef void (*RowFn)(Pc5Ui *ui, Pc5Canvas *c, int index, float x, float y, float w, int chosen);

static void list_panel(Pc5Ui *ui, Pc5Canvas *c, const char *title, int count, RowFn row, const char *empty_title,
                       const char *empty_body, const Hint *hint_list, int hint_count, float row_h)
{
    float w = 1180, h = 880, x = (W - w) / 2, y = (H - h) / 2;
    int visible = (int)((h - 230) / row_h);

    dim(c, 150);
    panel(c, x, y, w, h);
    pc5_text(c, &ui->fonts, PC5_FACE_HEAD, (int)x + 64, (int)y + 100, C_TEXT, title, (int)w - 128, 0);
    if (!count) {
        pc5_text(c, &ui->fonts, PC5_FACE_LABEL, (int)x + 64, (int)y + 220, C_TEXT, empty_title, (int)w - 128, 0);
        pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, (int)x + 64, (int)y + 272, (int)w - 128, 4, 42, C_DIM, empty_body, 0);
    } else {
        if (ui->menu < ui->list_top) ui->list_top = ui->menu;
        if (ui->menu >= ui->list_top + visible) ui->list_top = ui->menu - visible + 1;
        for (int i = ui->list_top; i < count && i < ui->list_top + visible; i++) {
            float ry = y + 140 + (i - ui->list_top) * row_h;
            if (i == ui->menu) {
                pc5_round_rect(c, x + 32, ry, w - 64, row_h - 8, 14, C_ROW);
                pc5_round_rect(c, x + 32, ry + row_h / 2 - 26, 5, 44, 2.5f, C_TEXT);
            }
            row(ui, c, i, x + 64, ry, w - 128, i == ui->menu);
        }
        if (count > visible) {
            char more[48];
            snprintf(more, sizeof(more), "%d of %d", ui->menu + 1, count);
            pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)(x + w - 64), (int)y + 100, C_FAINT, more, 0, 2);
        }
    }
    hints(ui, c, (int)x + 64, (int)(y + h - 44), hint_list, hint_count);
}

static void tick_box(Pc5Canvas *c, float x, float y, int ticked)
{
    if (ticked) {
        pc5_round_rect(c, x, y, 30, 30, 7, C_ACCENT);
        pc5_line(c, x + 8, y + 15, x + 13, y + 21, 3.2f, 0xff0b0d10u);
        pc5_line(c, x + 13, y + 21, x + 23, y + 9, 3.2f, 0xff0b0d10u);
    } else {
        pc5_round_frame(c, x, y, 30, 30, 7, 2, 0xff4a505cu);
    }
}

static void source_row(Pc5Ui *ui, Pc5Canvas *c, int i, float x, float y, float w, int chosen)
{
    char line[64];
    (void)chosen;
    tick_box(c, x, y + 20, ui->ticked[i]);
    snprintf(line, sizeof(line), "USB drive %d", ui->sources[i].drive + 1);
    pc5_text(c, &ui->fonts, PC5_FACE_LABEL, (int)x + 52, (int)y + 44, C_TEXT, ui->sources[i].name, (int)w - 272, 0);
    pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)(x + w), (int)y + 44, C_DIM, line, 0, 2);
}

static void queue_row(Pc5Ui *ui, Pc5Canvas *c, int i, float x, float y, float w, int chosen)
{
    const QueueGame *q = &ui->queue[i];
    char line[300], size[32];
    (void)chosen;
    if (q->thumb.levels) pc5_draw_cover(c, &q->thumb, x, y + 54, 64, 96, 96, 6, 256, 256, 0);
    else pc5_round_rect(c, x, y + 6, 64, 96, 6, 0xff2a2f38u);
    pc5_text(c, &ui->fonts, PC5_FACE_LABEL, (int)x + 92, (int)y + 44, C_TEXT, q->folder.name, (int)w - 92, 0);
    if (q->folder.problem[0] || q->exe < 0) {
        pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)x + 92, (int)y + 82, C_WARN,
                 q->folder.problem[0] ? q->folder.problem : "No program found: it will be skipped", (int)w - 92, 0);
        return;
    }
    pc5_size_text(q->folder.bytes, size, sizeof(size));
    snprintf(line, sizeof(line), "%s \xc2\xb7 %d-bit \xc2\xb7 %s \xc2\xb7 %s", q->folder.exes[q->exe].path,
             q->folder.exes[q->exe].pe.bits, pc5_api_name(pc5_folder_api(&q->folder, q->exe)), size);
    pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)x + 92, (int)y + 82, C_DIM, line, (int)w - 92, 0);
}

static void program_row(Pc5Ui *ui, Pc5Canvas *c, int i, float x, float y, float w, int chosen)
{
    const Pc5Exe *exe = &ui->folder.exes[i];
    char line[160], size[32];
    (void)chosen;
    pc5_size_text(exe->size, size, sizeof(size));
    snprintf(line, sizeof(line), "%d-bit \xc2\xb7 %s \xc2\xb7 %s%s", exe->pe.bits,
             pc5_api_name(pc5_folder_api(&ui->folder, i)), size, i == ui->exe ? " \xc2\xb7 chosen" : "");
    pc5_text(c, &ui->fonts, PC5_FACE_LABEL, (int)x, (int)y + 40, C_TEXT, exe->path, (int)w, 0);
    pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)x, (int)y + 74, C_DIM, line, (int)w, 0);
}

static void game_row(Pc5Ui *ui, Pc5Canvas *c, int i, float x, float y, float w, int chosen)
{
    const Pc5Game *game = &ui->library.games[i];
    const Cover *cover = &ui->covers[i];
    (void)chosen;
    if (cover->state && cover->picture.levels)
        pc5_draw_cover(c, &cover->picture, x, y + 50, 56, 84, 84, 6, 256, 256, 0);
    else
        pc5_round_rect(c, x, y + 8, 56, 84, 6, 0xff2a2f38u);
    pc5_text(c, &ui->fonts, PC5_FACE_LABEL, (int)x + 84, (int)y + 44, C_TEXT, game->title, (int)w - 84, 0);
    pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)x + 84, (int)y + 78, C_DIM, game->detail, (int)w - 84, 0);
}

static void review_screen(Pc5Ui *ui, Pc5Canvas *c)
{
    float w = 1600, h = 880, x = (W - w) / 2, y = (H - h) / 2;
    float cw = 420, ch = 630, cx = x + 72, cy = y + 90;
    char line[300], size[32], space[32];
    uint64_t free_bytes = 0;
    int row = 0, choices = ui->search.count + 1;
    const Pc5Exe *exe = ui->exe >= 0 ? &ui->folder.exes[ui->exe] : NULL;

    dim(c, 160);
    panel(c, x, y, w, h);
    pc5_shadow(c, cx, cy, cw, ch, 14, 50, pc5_mix(ui->review_tint, 0xff000000u, 60) | 0xa0000000u);
    if (ui->review.levels) pc5_draw_cover(c, &ui->review, cx, cy + ch / 2, cw, ch, ch, 14, 256, 256, 0);
    else pc5_round_rect(c, cx, cy, cw, ch, 14, 0xff1d222bu);
    if (ui->fetching) {
        pc5_round_rect(c, cx, cy, cw, ch, 14, 0xa0000000u);
        spinner(c, cx + cw / 2, cy + ch / 2, 34, ui->now, C_TEXT);
    }
    {
        int at = ui->choice < 0 ? choices : ui->choice + 1;
        const char *from = ui->choice >= 0 && ui->choice < ui->search.count ? ui->search.choices[ui->choice].source : "the game's icon";
        snprintf(line, sizeof(line), "Cover from %s \xc2\xb7 %d of %d", from, at, choices);
        pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)(cx + cw / 2), (int)(cy + ch + 46), C_DIM, line, (int)cw + 40, 1);
    }
    float tx = cx + cw + 72, tw = x + w - 72 - tx;
    pc5_text_wrap(c, &ui->fonts, PC5_FACE_TITLE, (int)tx, (int)y + 150, (int)tw, 2, 64, C_TEXT, ui->folder.name, 0);
    float ry = y + 300;
    const char *labels[4];
    char values[4][300];
    labels[row] = "Program";
    pc5_copy(values[row++], 300, exe ? exe->path : "none found");
    labels[row] = "Runs as";
    if (exe) snprintf(values[row++], 300, "%d-bit \xc2\xb7 %s", exe->pe.bits, pc5_api_name(pc5_folder_api(&ui->folder, ui->exe)));
    else pc5_copy(values[row++], 300, "-");
    pc5_size_text(ui->folder.bytes, size, sizeof(size));
    labels[row] = "Size";
    snprintf(values[row++], 300, ui->folder.files == 1 ? "%s in %u file" : "%s in %u files", size, (unsigned)ui->folder.files);
    labels[row] = "Space";
    if (!pc5_free_bytes(ui->root, &free_bytes)) {
        uint64_t need = pc5_install_needs(&ui->folder);
        pc5_size_text(free_bytes > need ? free_bytes - ui->folder.bytes : 0, space, sizeof(space));
        snprintf(values[row++], 300, free_bytes >= need ? "%s free after installing" : "not enough: %s free", space);
    } else {
        pc5_copy(values[row++], 300, "unknown");
    }
    for (int i = 0; i < row; i++) {
        pc5_text(c, &ui->fonts, PC5_FACE_BODY, (int)tx, (int)(ry + 62 * i), C_FAINT, labels[i], 200, 0);
        pc5_text(c, &ui->fonts, PC5_FACE_LABEL, (int)tx + 190, (int)(ry + 62 * i), C_TEXT, values[i], (int)tw - 190, 0);
    }
    float ny = ry + 62 * row + 40;
    if (ui->folder.problem[0]) {
        pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, (int)tx, (int)ny, (int)tw, 2, 40, C_BAD, ui->folder.problem, 0);
        ny += 90;
    } else if (exe && (exe->pe.imports & PC5_IMPORT_DOTNET)) {
        pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, (int)tx, (int)ny, (int)tw, 2, 40, C_WARN,
                      "This is a .NET program, and PC5 has no .NET yet: it may not start.", 0);
        ny += 90;
    }
    pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, (int)tx, (int)ny, (int)tw, 3, 40, C_DIM,
                  "The game is copied to the console once and checked against the USB drive. Whether a game "
                  "runs is only known by trying it.", 0);
    Hint list[4];
    int n = 0;
    list[n++] = (Hint){ accept_glyph(ui), ui->queue_review >= 0 ? "Done" : "Install" };
    if (choices > 1) list[n++] = (Hint){ G_L1R1, "Other covers" };
    if (ui->folder.exe_count > 1) list[n++] = (Hint){ G_SQUARE, "Change program" };
    if (ui->queue_review < 0) list[n++] = (Hint){ back_glyph(ui), "Back" };
    hints(ui, c, (int)tx, (int)(y + h - 48), list, n);
}

static void install_screen(Pc5Ui *ui, Pc5Canvas *c)
{
    const Pc5Install *in = &ui->install;
    float w = 1180, h = 520, x = (W - w) / 2, y = (H - h) / 2;
    char title[200], line[300], done[32], total[32], speed[32], left[48] = "";
    const char *step;
    int number;

    switch (in->phase) {
    case PC5_INSTALL_PREFIX: step = "Setting up the game's Windows"; number = 1; break;
    case PC5_INSTALL_LIST:
    case PC5_INSTALL_COPY: step = "Copying the game"; number = 2; break;
    case PC5_INSTALL_VERIFY: step = "Checking the copy against the USB drive"; number = 3; break;
    case PC5_INSTALL_FINISH: step = "Finishing"; number = 3; break;
    default: step = in->cancel ? "Stopping: deleting what was copied" : "Deleting what was copied"; number = 0; break;
    }
    dim(c, 150);
    panel(c, x, y, w, h);
    if (ui->queue_installing) {
        snprintf(line, sizeof(line), "Game %d of %d", ui->queue_at + 1, ui->queue_count);
        pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)x + 64, (int)y + 62, C_ACCENT, line, (int)w - 128, 0);
    }
    snprintf(title, sizeof(title), "Installing %s", in->info.title);
    pc5_text(c, &ui->fonts, PC5_FACE_HEAD, (int)x + 64, (int)y + 116, C_TEXT, title, (int)w - 128, 0);
    if (number) snprintf(line, sizeof(line), "Step %d of 3 \xc2\xb7 %s", number, step);
    else pc5_copy(line, sizeof(line), step);
    pc5_text(c, &ui->fonts, PC5_FACE_LABEL, (int)x + 64, (int)y + 186, C_TEXT, line, (int)w - 128, 0);
    double fraction = in->bytes_total ? (double)in->bytes_done / (double)in->bytes_total : 0;
    if (in->phase >= PC5_INSTALL_FINISH) {
        spinner(c, x + w - 100, y + 176, 22, ui->now, C_TEXT);
    } else {
        progress_bar(c, x + 64, y + 236, w - 128, fraction, pc5_mix(C_ACCENT, C_TEXT, 60));
        pc5_size_text(in->bytes_done, done, sizeof(done));
        pc5_size_text(in->bytes_total, total, sizeof(total));
        if (ui->speed > 1024) {
            pc5_size_text((uint64_t)ui->speed, speed, sizeof(speed));
            if (in->bytes_total > in->bytes_done) {
                double seconds = (double)(in->bytes_total - in->bytes_done) / ui->speed;
                if (seconds < 60) snprintf(left, sizeof(left), " \xc2\xb7 under a minute left");
                else snprintf(left, sizeof(left), " \xc2\xb7 about %d min left", (int)(seconds / 60 + 0.5));
            }
            snprintf(line, sizeof(line), "%s of %s \xc2\xb7 %s/s%s", done, total, speed, left);
        } else {
            snprintf(line, sizeof(line), "%s of %s \xc2\xb7 measuring the speed", done, total);
        }
        pc5_text(c, &ui->fonts, PC5_FACE_BODY, (int)x + 64, (int)y + 306, C_DIM, line, (int)w - 128, 0);
        snprintf(line, sizeof(line), "%u of %u files", (unsigned)in->files_done, (unsigned)in->files_total);
        pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)x + 64, (int)y + 350, C_FAINT, line, (int)w - 128, 0);
    }
    if (in->phase < PC5_INSTALL_FINISH) {
        Hint list[] = { { back_glyph(ui), "Stop" } };
        hints(ui, c, (int)x + 64, (int)(y + h - 44), list, 1);
    }
}

static void two_buttons(Pc5Ui *ui, Pc5Canvas *c, float x, float y, const char *one, const char *two, int chosen,
                        int second_is_danger)
{
    const char *labels[2] = { one, two };
    for (int i = 0; i < 2; i++) {
        float bx = x + i * 340, bw = 316, bh = 66;
        if (i == chosen) {
            uint32_t fill = i == 1 && second_is_danger ? C_RED : 0xffeef0f4u;
            pc5_round_rect(c, bx, y, bw, bh, 33, fill);
            pc5_text(c, &ui->fonts, PC5_FACE_LABEL, (int)(bx + bw / 2), (int)y + 43,
                     i == 1 && second_is_danger ? C_TEXT : 0xff111318u, labels[i], (int)bw - 40, 1);
        } else {
            pc5_round_frame(c, bx, y, bw, bh, 33, 2, 0xff3a404cu);
            pc5_text(c, &ui->fonts, PC5_FACE_LABEL, (int)(bx + bw / 2), (int)y + 43, C_DIM, labels[i], (int)bw - 40, 1);
        }
    }
}

static void confirm_dialog(Pc5Ui *ui, Pc5Canvas *c, const char *title, const char *body, const char *one,
                           const char *two, int danger, const Pc5Picture *thumb)
{
    float w = 1100, h = 470, x = (W - w) / 2, y = (H - h) / 2, tx = x + 64;
    dim(c, 170);
    panel(c, x, y, w, h);
    if (thumb && thumb->levels) {
        pc5_draw_cover(c, thumb, x + 64, y + 64 + 120, 160, 240, 240, 10, 256, 256, 0);
        tx = x + 64 + 160 + 56;
    }
    pc5_text_wrap(c, &ui->fonts, PC5_FACE_HEAD, (int)tx, (int)y + 110, (int)(x + w - 64 - tx), 2, 50, C_TEXT, title, 0);
    pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, (int)tx, (int)y + 200, (int)(x + w - 64 - tx), 3, 42, C_DIM, body, 0);
    two_buttons(ui, c, tx, y + h - 120, one, two, ui->button, danger);
}

static void message_dialog(Pc5Ui *ui, Pc5Canvas *c)
{
    float w = 1100, h = strlen(ui->message_body) > 180 ? 520 : 400, x = (W - w) / 2, y = (H - h) / 2;
    dim(c, 160);
    panel(c, x, y, w, h);
    pc5_circle(c, x + 92, y + 98, 22, ui->message_good ? 0xff3fb27fu : C_WARN);
    if (ui->message_good) {
        pc5_line(c, x + 81, y + 99, x + 89, y + 107, 4, 0xff0b0d10u);
        pc5_line(c, x + 89, y + 107, x + 104, y + 89, 4, 0xff0b0d10u);
    } else {
        pc5_line(c, x + 92, y + 86, x + 92, y + 101, 4, 0xff0b0d10u);
        pc5_circle(c, x + 92, y + 110, 2.6f, 0xff0b0d10u);
    }
    pc5_text_wrap(c, &ui->fonts, PC5_FACE_HEAD, (int)x + 140, (int)y + 112, (int)w - 204, 2, 50, C_TEXT, ui->message_title, 0);
    pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, (int)x + 140, (int)y + 190, (int)w - 204, 6, 42, C_DIM, ui->message_body, 0);
    Hint list[] = { { accept_glyph(ui), "OK" } };
    hints(ui, c, (int)x + 140, (int)(y + h - 48), list, 1);
}

static void start_screen(Pc5Ui *ui, Pc5Canvas *c, const char *line, int can_skip)
{
    pc5_clear(c, 0xff07080au);
    int w = pc5_text_width(&ui->fonts, PC5_FACE_TITLE, "PC5");
    int x = 960 - w / 2;
    int pw = pc5_text(c, &ui->fonts, PC5_FACE_TITLE, x, 500, C_TEXT, "PC", 0, 0);
    pc5_text(c, &ui->fonts, PC5_FACE_TITLE, x + pw + 1, 500, C_ACCENT, "5", 0, 0);
    spinner(c, 960, 600, 22, ui->now, C_DIM);
    if (line) pc5_text(c, &ui->fonts, PC5_FACE_BODY, 960, 690, C_DIM, line, 1400, 1);
    if (can_skip) {
        Hint list[] = { { back_glyph(ui), "Skip" } };
        hints(ui, c, MARGIN, 1012, list, 1);
    }
}

void pc5_ui_draw(Pc5Ui *ui, Pc5Canvas *c)
{
    char line[300];

    switch (ui->screen) {
    case S_START:
        start_screen(ui, c, ui->remove.top ? "Finishing an install or uninstall that was cut short" : NULL, 0);
        break;
    case S_COVERS:
        snprintf(line, sizeof(line), "Getting covers \xc2\xb7 %d of %d", ui->cover_at + 1, ui->cover_count);
        start_screen(ui, c, line, 1);
        break;
    case S_UPDATE:
        start_screen(ui, c, "Checking for a newer PC5", 0);
        break;
    default:
        library_screen(ui, c);
        break;
    }
    switch (ui->screen) {
    case S_SETTINGS: settings_screen(ui, c); break;
    case S_CHECKING: settings_screen(ui, c); busy_dialog(ui, c, "Checking for a newer PC5", "Asking GitHub", 0); break;
    case S_GAME_MENU: game_menu_screen(ui, c, ui->menu); break;
    case S_BUTTONS: buttons_screen(ui, c); break;
    case S_KEYS: buttons_screen(ui, c); keys_dialog(ui, c); break;
    case S_GAME_READING: {
        int index = pc5_library_find(&ui->library, ui->launch_id);
        snprintf(line, sizeof(line), "Reading %s", index >= 0 ? ui->library.games[index].title : "the game");
        busy_dialog(ui, c, line, "Finding its programs", 0);
        break;
    }
    case S_QUEUE_READ: {
        const QueueGame *q = ui->queue && ui->queue_at < ui->queue_count ? &ui->queue[ui->queue_at] : NULL;
        snprintf(line, sizeof(line), "Getting ready \xc2\xb7 %d of %d", ui->queue_at + 1 > ui->queue_count ? ui->queue_count : ui->queue_at + 1,
                 ui->queue_count);
        char detail[200];
        snprintf(detail, sizeof(detail), "%s: %s", q ? q->source.name : "",
                 ui->queue_phase == 0 ? "reading its files" : "looking for its cover");
        busy_dialog(ui, c, line, detail, 1);
        break;
    }
    case S_QUEUE: {
        char title[120], size[32];
        uint64_t bytes = 0;
        for (int i = 0; i < ui->queue_count; i++) bytes += ui->queue[i].folder.bytes;
        pc5_size_text(bytes, size, sizeof(size));
        snprintf(title, sizeof(title), "Install %d %s \xc2\xb7 %s", ui->queue_count, ui->queue_count == 1 ? "game" : "games", size);
        Hint list[] = { { G_OPTIONS, "Install all" }, { accept_glyph(ui), "Review" }, { back_glyph(ui), "Back" } };
        list_panel(ui, c, title, ui->queue_count, queue_row, "", "", list, 3, 116);
        break;
    }
    case S_ABOUT: about_screen(ui, c); break;
    case S_SCAN: busy_dialog(ui, c, "Looking at the USB drives", NULL, 0); break;
    case S_SOURCES: {
        int ticked = ticked_count(ui);
        char install[40];
        snprintf(install, sizeof(install), "Install ticked (%d)", ticked);
        Hint list[5];
        int n = 0;
        if (ticked) list[n++] = (Hint){ G_OPTIONS, install };
        list[n++] = (Hint){ accept_glyph(ui), "Install one" };
        list[n++] = (Hint){ G_SQUARE, "Tick" };
        list[n++] = (Hint){ G_TRIANGLE, "Look again" };
        list[n++] = (Hint){ back_glyph(ui), "Back" };
        list_panel(ui, c, "Install games from USB", ui->source_count, source_row, "No game folders found",
                   "Put each game's folder (the one with its .exe in it) at the top of a USB drive formatted as exFAT, "
                   "plug the drive into the PS5, then look again.",
                   list, n, 96);
        break;
    }
    case S_READING:
        snprintf(line, sizeof(line), "Reading %s", ui->sources[ui->menu].name);
        busy_dialog(ui, c, line, "Counting its files and finding its program", 0);
        break;
    case S_SEARCH:
        busy_dialog(ui, c, "Looking for the cover", "On Steam, GOG and Wikipedia", 1);
        break;
    case S_REVIEW: review_screen(ui, c); break;
    case S_PROGRAMS: {
        Hint list[] = { { accept_glyph(ui), "Choose" }, { back_glyph(ui), "Back" } };
        if (ui->program_for) game_menu_screen(ui, c, 1);
        list_panel(ui, c, "Which program is the game?", ui->folder.exe_count, program_row, "", "", list, 2, 104);
        break;
    }
    case S_INSTALL: install_screen(ui, c); break;
    case S_STOP:
        install_screen(ui, c);
        if (ui->queue_installing)
            confirm_dialog(ui, c, "Stop installing?",
                           "What this game copied so far is deleted; games already installed stay. The USB drive isn't "
                           "changed. Press the back button to keep installing.",
                           "Stop this game", "Stop all", 1, NULL);
        else
            confirm_dialog(ui, c, "Stop installing?", "Everything copied so far is deleted. The USB drive isn't changed.",
                           "Keep installing", "Stop", 1, NULL);
        break;
    case S_UNINSTALL_LIST: {
        Hint list[] = { { accept_glyph(ui), "Uninstall" }, { back_glyph(ui), "Back" } };
        list_panel(ui, c, "Uninstall game", ui->library.count, game_row, "There are no games to uninstall", "", list, 2, 112);
        break;
    }
    case S_UNINSTALL_CONFIRM: {
        const Pc5Game *game = &ui->library.games[ui->menu];
        if (ui->uninstall_back == S_GAME_MENU) game_menu_screen(ui, c, game_row_at(&ui->controls, ui_game_bits(ui), GR_UNINSTALL));
        snprintf(line, sizeof(line), "Uninstall %s?", game->title);
        confirm_dialog(ui, c, line,
                       "This deletes the game and all its saves and settings. There is no backup and no undo.",
                       "Cancel", "Uninstall", 1, ui->covers[ui->menu].state ? &ui->covers[ui->menu].picture : NULL);
        break;
    }
    case S_REMOVING:
        snprintf(line, sizeof(line), "Uninstalling %s", ui->removing_title);
        {
            char count[64];
            snprintf(count, sizeof(count), "%u files and folders deleted", (unsigned)ui->remove.removed);
            busy_dialog(ui, c, line, count, 0);
        }
        break;
    case S_MESSAGE: message_dialog(ui, c); break;
    case S_LAUNCH: {
        int index = pc5_library_find(&ui->library, ui->launch_id);
        snprintf(line, sizeof(line), "Starting %s", index >= 0 ? ui->library.games[index].title : "the game");
        busy_dialog(ui, c, line, NULL, 0);
        break;
    }
    default: break;
    }
    ui->drawn = 1;
}

/* ---- cards ------------------------------------------------------------------------------------ */

int pc5_make_card(const Pc5Fonts *fonts, const char *title, const Pc5Image *icon, Pc5Image *card, uint32_t *tint)
{
    const int cw = PC5_COVER_WIDTH, ch = PC5_COVER_HEIGHT;
    Pc5Canvas c;
    uint32_t base;

    if (pc5_image_new(card, cw, ch)) return -1;
    c = (Pc5Canvas){ card->px, cw, ch, cw };
    base = icon ? pc5_image_tint(icon) : C_DEFAULT_TINT;
    if (tint) *tint = base;
    for (int y = 0; y < ch; y++) {
        float t = (float)y / (ch - 1);
        uint32_t color = pc5_mix(pc5_mix(base, 0xffffffffu, 30), 0xff07080au, (int)(70 + t * 170));
        for (int x = 0; x < cw; x++) card->px[(size_t)y * cw + (size_t)x] = color;
    }
    /* A soft light behind the picture. */
    for (int y = 0; y < ch; y++)
        for (int x = 0; x < cw; x++) {
            float dx = (x - cw / 2.0f) / 260.0f, dy = (y - 270.0f) / 260.0f, d = dx * dx + dy * dy;
            if (d < 1) {
                uint32_t *p = &card->px[(size_t)y * cw + (size_t)x];
                *p = pc5_mix(*p, pc5_mix(base, 0xffffffffu, 90), (int)((1 - d) * (1 - d) * 90));
            }
        }
    if (icon && icon->px) {
        float size = 200;
        pc5_shadow(&c, cw / 2.0f - size / 2, 270 - size / 2, size, size, 30, 40, 0x70000000u);
        pc5_draw_image(&c, icon, cw / 2.0f - size / 2, 270 - size / 2, size, size, 256);
    } else {
        /* A screen on a stand: any PC game. */
        pc5_round_frame(&c, cw / 2.0f - 110, 190, 220, 140, 14, 7, 0xd0ffffffu);
        pc5_round_rect(&c, cw / 2.0f - 8, 330, 16, 34, 2, 0xd0ffffffu);
        pc5_round_rect(&c, cw / 2.0f - 60, 360, 120, 10, 5, 0xd0ffffffu);
    }
    if (fonts && fonts->atlas)
        pc5_text_wrap(&c, fonts, PC5_FACE_CARD, 36, 500, cw - 72, 3, 56, C_TEXT, title, 1);
    return 0;
}
