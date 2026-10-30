/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: the launcher's screens (pc5_ui.h). */
#include "pc5_ui.h"
#include "pc5_backgrounds.h"
#include "pc5_files.h"
#include "pc5_game.h"
#include "pc5_home.h"
#include "pc5_tile.h"
#include "pc5_image.h"
#include "pc5_library.h"
#include "pc5_logs.h"
#include "pc5_qr.h"
#include "pc5_report.h"
#include "pc5_settings.h"
#include "pc5_sound.h"
#include "pc5_source.h"
#include "pc5_update.h"
#include "pc5_upgrade.h"

#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { W = 1920, H = 1080, FLOOR_Y = 750, MARGIN = 72 };

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
    S_UPDATE, S_CHECKING, S_GAME_MENU, S_GAME_READING, S_QUEUE_READ, S_QUEUE, S_BUTTONS, S_KEYS, S_REPORT,
    S_UPGRADE_ASK, S_UPGRADING, S_GAME_UPDATE_ASK, S_SAVES_ASK, S_SAVES_CONFIRM, S_PICTURE_SEARCH, S_PICTURE,
    S_PICTURE_TYPING, S_COMPATIBILITY, S_GRAPHICS_API, S_REPORT_CHECK, S_REPORT_UPDATE, S_FILES, S_FILES_MENU,
    S_FILES_TYPING, S_FILES_CONFLICT, S_FILES_DELETE, S_FILES_WORKING, S_TILE_ART, S_INSTALL_ASK
} Screen;

static const char *const screen_names[] = { "start", "covers", "library", "settings", "about", "scan", "sources",
                                            "reading", "search", "review", "programs", "install", "stop",
                                            "uninstall-list", "uninstall-confirm", "removing", "message", "launch",
                                            "update", "checking", "game-menu", "game-reading", "queue-read", "queue",
                                            "buttons", "keys", "report", "upgrade-ask", "upgrading", "game-update-ask",
                                            "saves-ask", "saves-confirm", "picture-search", "picture", "picture-typing",
                                            "compatibility", "graphics-api", "report-check", "report-update", "files",
                                            "files-menu", "files-typing", "files-conflict", "files-delete",
                                            "files-working", "tile-art", "install-ask" };

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
    QueueState state;
    char id[PC5_ID], error[PC5_ERROR];
} QueueGame;

/* The wiki's game compatibility list, which Game maintenance shows as a QR code. */
#define PC5_COMPATIBILITY "https://github.com/bornaradusin/PC5/wiki/Game-compatibility"

/* Settings: its rows, on six pages. */
typedef enum SettingRow {
    SR_INSTALL, SR_UNINSTALL, SR_COMPATIBILITY, SR_MAINTENANCE, SR_GAMES, SR_OVERLAY, SR_SYSTEM, SR_ABOUT,
    SR_FPS, SR_FRAME_TIME, SR_CPU_USAGE, SR_CPU_TEMPERATURE, SR_SOC_TEMPERATURE, SR_MEMORY, SR_FPS_CORNER,
    SR_OVERLAY_SIZE, SR_VIBRATION, SR_LIGHT, SR_TRIGGERS, SR_TRIGGER_RUMBLE, SR_TRIGGER_RESISTANCE,
    SR_STARTUP_SOUND, SR_STARTUP_VOLUME, SR_KEEP_LOGS, SR_PICTURES, SR_CHECK_ON_OPEN, SR_CHECK_NOW, SR_UPDATE_NOW,
    SR_FILES,
    SR_SOURCE   /* a picture source: SR_SOURCE + its index (pc5_cover.h) */
} SettingRow;

enum { PAGE_ROWS = 8, PAGE_GAMES = 1, PAGE_SYSTEM = 2, PAGE_OVERLAY = 3, PAGE_PICTURES = 4, PAGE_MAINTENANCE = 5,
       SETTING_PAGES = 6 };
static const char *const page_titles[SETTING_PAGES] = { "Settings", "In games", "System", "Overlay", "Picture sources",
                                                        "Game maintenance" };
/* Where each page's row is on the first page (Back goes there); System's rows move with its
 * startup sound (system_row). */
enum { ROW_MAINTENANCE = 0, ROW_GAMES = 1, ROW_OVERLAY = 2, ROW_FILES = 3, ROW_SYSTEM = 4, ROW_ABOUT = 5 };
/* Game maintenance's rows. */
enum { ROW_INSTALL = 0, ROW_UNINSTALL = 1, ROW_COMPATIBILITY = 2 };

/* A page's rows: the triggers' strengths only while they are used, the startup sound's
 * volume only while there is one, Update now only while there is a newer PC5 it can install
 * (offer_update). */
static int page_rows(const Pc5Settings *set, int page, int offer_update, SettingRow rows[PAGE_ROWS])
{
    int n = 0;
    if (page == PAGE_PICTURES) {
        for (int i = 0; i < PC5_PICTURE_SOURCES; i++) rows[n++] = (SettingRow)(SR_SOURCE + i);
    } else if (page == PAGE_OVERLAY) {
        rows[n++] = SR_FPS;
        rows[n++] = SR_FRAME_TIME;
        rows[n++] = SR_CPU_USAGE;
        rows[n++] = SR_CPU_TEMPERATURE;
        rows[n++] = SR_SOC_TEMPERATURE;
        rows[n++] = SR_MEMORY;
        rows[n++] = SR_FPS_CORNER;
        rows[n++] = SR_OVERLAY_SIZE;
    } else if (page == PAGE_GAMES) {
        rows[n++] = SR_VIBRATION;
        rows[n++] = SR_LIGHT;
        rows[n++] = SR_TRIGGERS;
        if (set->triggers == PC5_TRIGGERS_RUMBLE || set->triggers == PC5_TRIGGERS_BOTH) rows[n++] = SR_TRIGGER_RUMBLE;
        if (set->triggers == PC5_TRIGGERS_RESISTANCE || set->triggers == PC5_TRIGGERS_BOTH)
            rows[n++] = SR_TRIGGER_RESISTANCE;
    } else if (page == PAGE_SYSTEM) {
        rows[n++] = SR_STARTUP_SOUND;
        if (strcmp(set->startup_sound, "off")) rows[n++] = SR_STARTUP_VOLUME;
        rows[n++] = SR_KEEP_LOGS;
        rows[n++] = SR_PICTURES;
        rows[n++] = SR_CHECK_ON_OPEN;
        rows[n++] = SR_CHECK_NOW;
        if (offer_update) rows[n++] = SR_UPDATE_NOW;
    } else if (page == PAGE_MAINTENANCE) {
        rows[n++] = SR_INSTALL;
        rows[n++] = SR_UNINSTALL;
        rows[n++] = SR_COMPATIBILITY;
    } else {
        rows[n++] = SR_MAINTENANCE;
        rows[n++] = SR_GAMES;
        rows[n++] = SR_OVERLAY;
        rows[n++] = SR_FILES;
        rows[n++] = SR_SYSTEM;
        rows[n++] = SR_ABOUT;
    }
    return n;
}

/* A game's menu, on four pages: Play, Controller (and Button mapping while it plays the keyboard
 * and mouse) and the three groups on the first; each group's rows on a page of its own. */
typedef enum GameRow {
    GR_PLAY, GR_CONTROLLER, GR_BUTTONS, GR_GRAPHICS, GR_APPEARANCE, GR_MANAGE,
    GR_GRAPHICS_API, GR_FRAMEGEN, GR_RESOLUTION, GR_ASPECT, GR_MEMORY, GR_VIDEO_MEMORY, GR_CPU_SHARING,
    GR_COVER, GR_BACKGROUND, GR_HOME, GR_ICON,
    GR_PROGRAM, GR_FILES, GR_UPDATE, GR_UNINSTALL
} GameRow;
enum { GAME_ROWS = 7, GAME_PAGES = 4, GP_MAIN = 0, GP_GRAPHICS = 1, GP_APPEARANCE = 2, GP_MANAGE = 3 };
static const char *const game_page_titles[GAME_PAGES] = { "", "Graphics", "Appearance", "Manage" };
/* The first page's row that opens each page. */
static const GameRow game_page_rows[GAME_PAGES] = { GR_PLAY, GR_GRAPHICS, GR_APPEARANCE, GR_MANAGE };
/* What a game is, for its menu's rows. */
enum { GAME_32BIT = 1, GAME_VULKAN = 2, GAME_PC5 = 4, GAME_API = 8 };

/* kind: GAME_32BIT for Memory, GAME_VULKAN for Resolution, GAME_PC5 (PC5 installed it) for Game files and Update,
 * GAME_API (it draws with DirectDraw or Direct3D) for Graphics API. */
static int game_rows(const Pc5Controls *controls, int kind, int page, GameRow rows[GAME_ROWS])
{
    int n = 0;
    switch (page) {
    case GP_GRAPHICS:
        if (kind & GAME_API) rows[n++] = GR_GRAPHICS_API;
        rows[n++] = GR_FRAMEGEN;
        if (kind & GAME_VULKAN) rows[n++] = GR_RESOLUTION;
        rows[n++] = GR_ASPECT;
        if (kind & GAME_32BIT) rows[n++] = GR_MEMORY;
        if (kind & GAME_32BIT) rows[n++] = GR_VIDEO_MEMORY;
        rows[n++] = GR_CPU_SHARING;
        break;
    case GP_APPEARANCE:
        rows[n++] = GR_COVER;
        rows[n++] = GR_BACKGROUND;
        rows[n++] = GR_HOME;
        rows[n++] = GR_ICON;
        break;
    case GP_MANAGE:
        rows[n++] = GR_PROGRAM;
        if (kind & GAME_PC5) rows[n++] = GR_FILES;
        if (kind & GAME_PC5) rows[n++] = GR_UPDATE;
        rows[n++] = GR_UNINSTALL;
        break;
    default:
        rows[n++] = GR_PLAY;
        rows[n++] = GR_CONTROLLER;
        if (controls->mode == PC5_PAD_KEYBOARD) rows[n++] = GR_BUTTONS;
        rows[n++] = GR_GRAPHICS;
        rows[n++] = GR_APPEARANCE;
        rows[n++] = GR_MANAGE;
        break;
    }
    return n;
}

/* Where row is: its index, and its page in *page (when not NULL). */
static int game_row_find(const Pc5Controls *controls, int kind, GameRow row, int *page)
{
    GameRow rows[GAME_ROWS];
    for (int p = 0; p < GAME_PAGES; p++) {
        int n = game_rows(controls, kind, p, rows);
        for (int i = 0; i < n; i++)
            if (rows[i] == row) {
                if (page) *page = p;
                return i;
            }
    }
    if (page) *page = GP_MAIN;
    return 0;
}

/* Button mapping: the sticks (with their four keys while they send keys), the
 * pointer's speed while a stick moves it, the buttons, and the default layout. */
typedef enum PadRowKind { PR_STICK, PR_DIRECTION, PR_SPEED, PR_SLOW, PR_SLOW_PERCENT, PR_BUTTON, PR_RESET } PadRowKind;
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
    if (controls->stick[0] == PC5_STICK_POINTER || controls->stick[1] == PC5_STICK_POINTER) {
        rows[n++] = (PadRow){ PR_SPEED, 0 };
        rows[n++] = (PadRow){ PR_SLOW, 0 };
        if (controls->slow_button >= 0) rows[n++] = (PadRow){ PR_SLOW_PERCENT, 0 };
    }
    for (int button = 0; button < PC5_BUTTONS; button++) rows[n++] = (PadRow){ PR_BUTTON, button };
    rows[n++] = (PadRow){ PR_RESET, 0 };
    return n;
}

static void percent_text(int percent, char *out, size_t size)
{
    if (!percent) pc5_copy(out, size, "Off");
    else snprintf(out, size, "%d%%", percent);
}

typedef struct BgJob {
    pthread_t thread;
    int running;                   /* a thread to join */
    atomic_int done;
    char key[PC5_ID + 64];         /* what it makes (Pc5Ui's bg_want then) */
    char path[PC5_PATH];           /* the kept background, or "" to make it from cover */
    Pc5Image cover;                /* a copy of the cover's smallest picture */
    uint32_t *out;                 /* W x H, made there */
    int ok;
} BgJob;

/* The file manager's start: a place to open (Settings › File manager). */
enum { FILES_PLACES = 16 };
typedef struct FilesPlace {
    char name[64], path[PC5_PATH];
    int read_only;
} FilesPlace;

typedef char FilesPath[PC5_PATH];
/* Items chosen: their paths, and the same as a list for pc5_files.h. */
typedef struct FilesSel {
    FilesPath *paths;
    const char **list;
    int count;
} FilesSel;

typedef struct Cover {
    Pc5Picture picture;
    uint32_t tint;
    int state;           /* 0 not loaded, 1 loaded */
} Cover;

struct Pc5Ui {
    Pc5UiConfig config;
    char root[PC5_PATH], template_prefix[PC5_PATH], app[PC5_PATH];
    /* Starting a .NET game the first time: the runtime's copy (pc5_game.h). 0: not looked
     * at yet, 1: copying, 2: nothing (more) to do. */
    Pc5Runtime runtime;
    int runtime_state;
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
    Pc5Upgrade upgrade;            /* PC5 updating itself, on its own thread */
    int upgrade_step;              /* the step speed is measured for */
    Screen upgrade_back;           /* where a stopped update goes back to */
    uint64_t upgrade_drawn;
    Pc5Remove remove;
    char removing_title[PC5_NAME];

    /* startup: finishing leftovers, then covers for the games without one */
    int cover_queue[PC5_MAX_GAMES], cover_count, cover_at, cover_searching;

    char message_title[160], message_body[700];
    int message_good;
    int message_report;               /* the message is about a kept log: Triangle reports it */
    Pc5LogIssue report;               /* Report on GitHub: the log, its game, the form's link as a QR code */
    char report_game[PC5_NAME], report_url[PC5_REPORT_URL];
    Pc5Qr report_qr;
    Pc5Qr coffee_qr;                  /* About PC5: Buy Me a Coffee's page, made once */
    Pc5Qr compatibility_qr;           /* Game maintenance › Game compatibility list: the wiki's page, made once */
    char launch_id[PC5_ID];
    char start[PC5_ID];               /* a home screen tile's game, until it is started */
    int removing_tiles;               /* the game being uninstalled had a tile, deleted with it */
    char tile[PC5_HOME_ID];           /* the game menu's game's home screen tile, or "" */
    char select_after[PC5_ID];

    Pc5Settings settings;
    int page;                      /* Settings: 0 its first page, 1 In games, 2 System, ... (PAGE_*) */
    Pc5Controls controls;          /* the controller of the game in launch_id (its menu) */
    int game_page;                 /* its menu's page: GP_MAIN or a group's */
    int pad_row, pad_top;          /* Button mapping: the row chosen, the first one shown */
    int key_control, key_choice;   /* the key picker: for which control, the key chosen */
    char griddb_key[96];           /* SteamGridDB's key (api_keys.txt), or "": never logged or shown */
    int griddb_refused;            /* SteamGridDB refused that key at its last answer */
    int api_choice;                /* the Graphics API window: the choice picked (an index of pc5_graphics_choices) */
    Pc5UpdateMemory update;
    Pc5LogIssue issue;             /* what went wrong last time, told once the library shows */
    int issue_pending;
    Pc5InstallPhase speed_phase;
    int program_for;               /* S_PROGRAMS for: 0 the review, 1 the game in launch_id */
    Screen uninstall_back;         /* where Cancel in S_UNINSTALL_CONFIRM goes */
    int update_for;                /* S_SCAN, S_SOURCES and S_READING choose a newer copy of the game in launch_id */
    /* PC5's INSTALL folder, in its own folder ("" when PC5's folder isn't found): games put there
     * over FTP are offered when PC5 opens (install_waiting of them), and listed with the USB
     * drives'; one installed from there is moved into the library (pc5_install_take_from). */
    char install_dir[PC5_PATH];
    char pc5_folder[PC5_PATH];     /* PC5's own folder on the console's drives, "" when not found */
    int install_waiting, install_looked, install_scanned;
    int install_only;              /* S_SCAN and S_SOURCES: the INSTALL folder's games only */
    const char *scan_roots[16];
    int removing_kind;             /* S_REMOVING: 0 an uninstall, 1 one that keeps the saves, 2 kept saves deleted */
    char use_saves[PC5_PATH];      /* S_SAVES_ASK: the kept saves the game being installed can have */
    Pc5Saves *saves;               /* Settings › Uninstall game: the kept saves, after the games */
    int saves_count;

    /* several games installed in a row */
    uint8_t ticked[PC5_SOURCES];
    QueueGame *queue;
    int queue_count, queue_at, queue_phase, queue_review, queue_installing, queue_stop_all;

    /* the chosen game's background (pc5_backgrounds.h), faded in over the backdrop */
    Pc5Backgrounds backgrounds;
    int library_loads, backgrounds_for;  /* library loads, and the one the worker last looked at */
    unsigned bg_generation;        /* the worker's count when the background was last looked for */
    char bg_id[PC5_ID];            /* the game whose background was looked for ("": none yet) */
    int bg_selected;
    uint64_t bg_selected_at;       /* when the chosen game changed */
    uint32_t *bg, *bg_old;         /* the background as drawn (W x H), and the one fading out */
    int bg_has, bg_old_has, bg_fading;
    int bg_cover_wait;             /* the chosen game has no background, and its cover isn't loaded yet */
    /* What is wanted behind the library and what is shown ("<id>|file|<time>|<size>", "<id>|cover",
     * "<id>|none" or ""), and the one being made. */
    char bg_want[PC5_ID + 64], bg_key[PC5_ID + 64];
    BgJob bg_job;
    uint64_t bg_fade_at;

    /* A game's cover or background chosen by hand (Appearance): the search, on the heap while
     * the picker is open, and the picture shown, at of count (pick_entry says which it is). */
    int pick_kind;                 /* PICK_COVER or PICK_BACKGROUND */
    Pc5CoverSearch *pick;
    int pick_at, pick_count, pick_now, pick_fetching, pick_step;
    Pc5Image pick_image;           /* as it would be kept: a cover, or a background (960x540) */
    Pc5Picture pick_picture;       /* a cover's, as drawn */
    uint32_t pick_tint;
    char pick_query[PC5_NAME];     /* the name typed to search for, or "": the game's own names */

    /* The file manager (pc5_files.h): a game's own folder, from its menu (files_game), or the
     * console's places, from Settings. */
    int files_game;                /* 1: the folder of the game in launch_id, and nothing above it */
    char files_top[PC5_PATH];      /* the game's folder, or the place opened ("" at the places) */
    char files_path[PC5_PATH];     /* the folder shown, "" for the places */
    FilesPlace files_place[FILES_PLACES];
    int files_place_count;
    Pc5FileList files;
    uint8_t *files_ticked;         /* one per item, 1 ticked */
    int files_at, files_list_top;  /* the row chosen, and the first one shown */
    FilesSel clip;                 /* Copy or Cut: what Paste takes */
    int clip_cut;
    char clip_from[PC5_PATH];      /* the folder it came from */
    Pc5FilesJob files_job;
    int files_busy;                /* files_job is begun */
    int files_changed;             /* something was changed: the library is read again on leaving */
    int files_typing;              /* S_FILES_TYPING: 0 Rename, 1 New folder */
    int files_conflicts;           /* S_FILES_CONFLICT: the names Paste finds there */
    Pc5FilesSize files_size;       /* S_FILES_DELETE: what it deletes */

    uint8_t *falloff;              /* the glow's shape, 0-255 per pixel */
    uint32_t *backdrop;            /* the background without the glow */
    uint32_t *composed;            /* with the glow, for composed_glow */
    uint32_t composed_glow;
    int composed_ready;
};

/* GAME_32BIT, GAME_VULKAN, GAME_PC5 of the game whose menu is open (0 when it isn't found). */
static int ui_game_kind(const Pc5Ui *ui)
{
    int index = pc5_library_find(&ui->library, ui->launch_id);
    if (index < 0) return 0;
    Pc5Graphics choices[PC5_GRAPHICS_COUNT];
    return (ui->library.games[index].bits == 32 ? GAME_32BIT : 0) | (ui->library.games[index].vulkan ? GAME_VULKAN : 0) |
           (ui->library.games[index].pc5 ? GAME_PC5 : 0) |
           (pc5_graphics_choices(ui->library.games[index].api, choices) > 1 ? GAME_API : 0);
}

/* What the game whose menu is open draws with (Pc5Api), PC5_API_GDI when it isn't found. */
static int ui_game_api(const Pc5Ui *ui)
{
    int index = pc5_library_find(&ui->library, ui->launch_id);
    return index < 0 ? PC5_API_GDI : ui->library.games[index].api;
}

/* The game menu on row's page, at row. */
static void game_menu_at(Pc5Ui *ui, GameRow row)
{
    ui->menu = game_row_find(&ui->controls, ui_game_kind(ui), row, &ui->game_page);
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
    if (ui->runtime_state == 1) pc5_runtime_end(&ui->runtime);
    if (screen == S_LAUNCH || ui->runtime_state == 1) ui->runtime_state = 0;
    ui->screen = screen;
    ui->entered = ui->now;
    /* The backgrounds' worker waits while these screens use the network, so they don't wait for it. */
    atomic_store(&ui->backgrounds.pause,
                 screen == S_SCAN || screen == S_SOURCES || screen == S_READING || screen == S_SEARCH ||
                 screen == S_REVIEW || screen == S_PROGRAMS || screen == S_INSTALL || screen == S_STOP ||
                 screen == S_QUEUE_READ || screen == S_QUEUE || screen == S_CHECKING || screen == S_UPGRADE_ASK ||
                 screen == S_UPGRADING || screen == S_GAME_UPDATE_ASK || screen == S_SAVES_ASK || screen == S_REPORT ||
                 screen == S_PICTURE_SEARCH || screen == S_PICTURE || screen == S_PICTURE_TYPING ||
                 screen == S_FILES_WORKING);
    ui->drawn = 0;
    ui->dirty = 1;
    say(ui, "PC5 screen=%s", screen_names[screen]);
}

static void message(Pc5Ui *ui, int good, const char *title, const char *body, Screen back_to)
{
    pc5_copy(ui->message_title, sizeof(ui->message_title), title);
    pc5_copy(ui->message_body, sizeof(ui->message_body), body);
    ui->message_good = good;
    ui->message_report = 0;
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
    /* One a frame while the chosen one is there, so moving along stays smooth. */
    int loaded = 0, most = ui->library.count && ui->covers[ui->selected].state ? 1 : 2;

    for (int d = 0; d <= 6 && loaded < most; d++)
        for (int side = -1; side <= 1 && loaded < most; side += 2) {
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
    ui->library_loads++;
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

/* What updating to the newest PC5 downloads, as the update dialog and Update now say it: only
 * what changed when this PC5 is the release before (and has its list of files), otherwise the
 * whole app ("You have 1.9.2, more than one version behind: PC5 downloads the whole app (388 MB)"). */
static void update_how(const Pc5Ui *ui, char *out, size_t size)
{
    const Pc5Update *u = &ui->update.last;
    char manifest[PC5_PATH], bytes[32];
    int listed = ui->app[0] && !pc5_format(manifest, sizeof(manifest), "%s/pc5/manifest.txt", ui->app) &&
                 pc5_is_file(manifest);

    if (listed && pc5_update_small(u, PC5_VERSION)) {
        pc5_size_text(u->delta_size, bytes, sizeof(bytes));
        snprintf(out, size, "You have %s, the version before: PC5 downloads only what changed (%s)", PC5_VERSION, bytes);
        return;
    }
    pc5_size_text(u->asset_size, bytes, sizeof(bytes));
    if (u->delta_from[0] && pc5_version_compare(PC5_VERSION, u->delta_from) < 0)
        snprintf(out, size, "You have %s, more than one version behind: PC5 downloads the whole app (%s)", PC5_VERSION,
                 bytes);
    else
        snprintf(out, size, "You have %s: PC5 downloads the whole app (%s)", PC5_VERSION, bytes);
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

Pc5Ui *pc5_ui_create(const Pc5UiConfig *config, const Pc5Platform *platform)
{
    Pc5Ui *ui = pc5_alloc(sizeof(*ui));

    if (!ui) return NULL;
    ui->config = *config;
    pc5_copy(ui->root, sizeof(ui->root), config->root);
    pc5_copy(ui->template_prefix, sizeof(ui->template_prefix), config->template_prefix);
    /* The app's folder: the template is its pc5/prefix. */
    pc5_copy(ui->app, sizeof(ui->app), config->template_prefix);
    {
        size_t n = strlen(ui->app);
        if (n > 11 && !strcmp(ui->app + n - 11, "/pc5/prefix")) ui->app[n - 11] = 0;
        else ui->app[0] = 0;
    }
    if (config->start) pc5_copy(ui->start, sizeof(ui->start), config->start);
    ui->usb_roots = config->usb_roots;
    pc5_usb_roots(config->usb_roots);   /* where games played from a USB drive are looked for */
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
    /* The update's thread works in ui: it stops first (or finishes putting files in place). */
    if (pc5_upgrade_busy(&ui->upgrade)) {
        pc5_upgrade_cancel(&ui->upgrade);
        while (pc5_upgrade_busy(&ui->upgrade)) usleep(10000);
    }
    pc5_backgrounds_end(&ui->backgrounds, 1);
    if (ui->bg_job.running) pthread_join(ui->bg_job.thread, NULL);
    pc5_image_free(&ui->bg_job.cover);
    pc5_free(ui->bg_job.out);
    if (ui->runtime_state == 1) pc5_runtime_end(&ui->runtime);
    covers_clear(ui);
    queue_free(ui);
    pc5_picture_free(&ui->review);
    pc5_image_free(&ui->review_image);
    pc5_picture_free(&ui->pick_picture);
    pc5_image_free(&ui->pick_image);
    pc5_free(ui->pick);
    if (ui->installing) pc5_install_end(&ui->install);
    pc5_remove_end(&ui->remove);
    if (ui->files_busy) pc5_files_end(&ui->files_job);
    pc5_files_list_free(&ui->files);
    pc5_free(ui->files_ticked);
    pc5_free(ui->clip.paths);
    pc5_free(ui->clip.list);
    pc5_fonts_free(&ui->fonts);
    pc5_free(ui->saves);
    pc5_free(ui->falloff);
    pc5_free(ui->backdrop);
    pc5_free(ui->composed);
    pc5_free(ui->bg);
    pc5_free(ui->bg_old);
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
/* The program a folder's choice starts, on disk: a batch file's program, or itself. */
static int folder_program(const Pc5Folder *folder, int exe, char *out, size_t size)
{
    Pc5Batch batch;
    const char *path = folder->exes[exe].path;
    if (pc5_is_batch(path) && !pc5_batch_read(folder->path, path, &batch)) path = batch.exe;
    return pc5_join(out, size, folder->path, path);
}

static void make_cover(Pc5Ui *ui, const Pc5Folder *folder, int exe, Pc5CoverSearch *search, int *choice_io,
                       Pc5Image *cover, uint32_t *tint, char *source, size_t source_size)
{
    int choice = *choice_io;

    *cover = (Pc5Image){ 0 };
    *tint = C_DEFAULT_TINT;
    while (choice >= 0 && choice < search->count) {
        if (!pc5_cover_fetch(&search->choices[choice], &ui->platform.http, cover)) {
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
        if (exe >= 0 && exe < folder->exe_count && !folder_program(folder, exe, program, sizeof(program)))
            pc5_pe_icon(program, &icon);
        pc5_make_card(&ui->fonts, folder->name, icon.px ? &icon : NULL, cover, tint);
        pc5_image_free(&icon);
        pc5_copy(source, source_size, "icon");
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
               ui->review_source, sizeof(ui->review_source));
    review_picture(ui);
    ui->choice_loaded = 1;
    ui->dirty = 1;
}

/* The sources to ask, and SteamGridDB's key (pc5_api_key) when there is one. */
static void hint_sources(const Pc5Ui *ui, Pc5CoverHint *hint)
{
    hint->off = ui->settings.pictures_off;
    pc5_copy(hint->steamgriddb_key, sizeof(hint->steamgriddb_key), ui->griddb_key);
}

/* One request of a search (pc5_cover_search_step); at its end, SteamGridDB's answer noted
 * when it refused the key or was asked too often (never the key itself). */
static int search_step(Pc5Ui *ui, Pc5CoverSearch *search)
{
    if (pc5_cover_search_step(search, &ui->platform.http)) return 1;
    int status = search->griddb_status;
    if (status == 401 || status == 403) {
        if (!ui->griddb_refused) say(ui, "PC5 SteamGridDB refused the key in " PC5_API_KEYS " (HTTP %d)", status);
        ui->griddb_refused = 1;
    } else if (status == 200) {
        ui->griddb_refused = 0;
    } else if (status == 429) {
        say(ui, "PC5 SteamGridDB: too many requests for now, skipped");
    }
    return 0;
}

static void hint_for_folder(const Pc5Ui *ui, const Pc5Folder *folder, int exe, Pc5CoverHint *hint)
{
    char program[PC5_PATH] = "";
    if (exe >= 0 && exe < folder->exe_count) folder_program(folder, exe, program, sizeof(program));
    const char *path = program;
    const char *name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;

    memset(hint, 0, sizeof(*hint));
    pc5_copy(hint->folder, sizeof(hint->folder), folder->name);
    if (exe >= 0 && exe < folder->exe_count) pc5_copy(hint->product, sizeof(hint->product), folder->exes[exe].pe.product);
    snprintf(hint->program, sizeof(hint->program), "%.*s", (int)(strlen(name) > 4 ? strlen(name) - 4 : 0), name);
    hint->steam_appid = folder->steam_appid;
    pc5_copy(hint->picture, sizeof(hint->picture), folder->picture);
    hint_sources(ui, hint);
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
    hint_sources(ui, hint);
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
    if (search_step(ui, &ui->search)) return 1;
    /* Searched: the best picture that can be had, else the card, saved only
     * when a source answered (offline, it is tried again next time). */
    Pc5Image cover = { 0 };
    uint32_t tint = C_DEFAULT_TINT;
    for (int i = 0; i < ui->search.count && !cover.px; i++)
        if (!pc5_cover_fetch(&ui->search.choices[i], &ui->platform.http, &cover)) tint = pc5_image_tint(&cover);
    if (!cover.px && ui->search.answered) card_for(ui, game, &cover, &tint);
    if (cover.px) {
        char path[PC5_PATH], dir[PC5_PATH];
        if (!cover_path(ui, game, path, sizeof(path)) &&
            !pc5_format(dir, sizeof(dir), "%s/prefixes/%s/pc5", ui->root, game->prefix) && !pc5_mkdirs(dir)) {
            pc5_cover_save(path, &cover, tint);
        }
    }
    say(ui, "PC5 cover game=%s choices=%d answered=%d saved=%d", game->id, ui->search.count, ui->search.answered,
        cover.px != NULL);
    pc5_image_free(&cover);
    ui->cover_searching = 0;
    ui->cover_at++;
    return ui->cover_at < ui->cover_count;
}

/* ---- the games' backgrounds -------------------------------------------------------------------- */

enum { BG_FADE_MS = 320, BG_SETTLE_MS = 160 };

/* The worker on the games that need a background, once for each load of the library
 * (when it is free: a library loaded again meanwhile is looked at after it). */
static void backgrounds_refresh(Pc5Ui *ui)
{
    if (!ui->config.fetch_backgrounds || ui->backgrounds_for == ui->library_loads || !ui->library.count ||
        atomic_load(&ui->backgrounds.busy))
        return;
    pc5_backgrounds_end(&ui->backgrounds, 0);
    ui->backgrounds_for = ui->library_loads;
    Pc5BackgroundGame *games = pc5_alloc(sizeof(*games) * (size_t)ui->library.count);
    int n = 0;
    if (!games) return;
    for (int i = 0; i < ui->library.count; i++) {
        const Pc5Game *game = &ui->library.games[i];
        Pc5BackgroundGame *b = &games[n];
        if (!game->prefix[0] || !strcmp(game->prefix, "default")) continue;
        memset(b, 0, sizeof(*b));
        pc5_copy(b->id, sizeof(b->id), game->id);
        pc5_copy(b->prefix, sizeof(b->prefix), game->prefix);
        pc5_copy(b->title, sizeof(b->title), game->title);
        if (pc5_game_folder(ui->root, game->id, b->folder, sizeof(b->folder))) b->folder[0] = 0;
        if (!pc5_background_needed(ui->root, b)) continue;
        hint_for_game(ui, game, &b->hint);
        n++;
    }
    if (!n) { pc5_free(games); return; }
    pc5_copy(ui->backgrounds.root, sizeof(ui->backgrounds.root), ui->root);
    ui->backgrounds.http = ui->platform.http;
    ui->backgrounds.log = ui->platform.log;
    ui->backgrounds.log_context = ui->platform.context;
    say(ui, "PC5 backgrounds to look for=%d", n);
    if (pc5_backgrounds_start(&ui->backgrounds, games, n)) say(ui, "PC5 problem backgrounds: no thread");
}

static void hsv_of(uint32_t color, float *h, float *s, float *v);
static uint32_t from_hsv(float h, float s, float v, int alpha);

/* A picture (the kept background, 960x540, or one made from a cover) as drawn behind the
 * library: filling the screen, darker at the top where the clock is and at the sides; below
 * the floor, where the covers stand, its mirror image, stretched and blurred as on a wet
 * floor and darkening towards the hints. */
static int background_prepare(const Pc5Image *small, uint32_t *out)
{
    /* its tables on the heap: it is made on a thread of its own as well */
    int *tables = pc5_alloc(sizeof(int) * (H + 4 * W));
    if (!tables) return -1;
    int *vertical = tables, *horizontal = vertical + H, *x0s = horizontal + W, *x1s = x0s + W, *fxs = x1s + W;
    Pc5Image soft = { 0 };
    for (int y = 0; y < H; y++) {
        float v;
        if (y < FLOOR_Y) v = y < 220 ? 0.42f + 0.36f * (float)y / 220 : 0.78f;
        else {
            float f = (float)(y - FLOOR_Y) / (H - FLOOR_Y);
            v = 0.85f * (1 - (f < 0.5f ? 0.12f + 0.76f * f : 0.50f + 0.80f * (f - 0.5f)));
        }
        vertical[y] = (int)(v * 256);
    }
    /* bilinear: a pixel's centre (x + 0.5) is (x + 0.5) * w / W - 0.5 in the source, in 1/256ths */
    for (int x = 0; x < W; x++) {
        float e = fabsf((float)x - W / 2.0f) / (W / 2.0f);
        horizontal[x] = (int)((1 - 0.25f * e * e) * 256);
    }
    /* The floor's: a quarter of the size, blurred. */
    if (!pc5_image_resize(small, small->w / 4 > 8 ? small->w / 4 : small->w, small->h / 4 > 8 ? small->h / 4 : small->h, &soft))
        pc5_image_blur(&soft, 2);
    for (int y = 0; y < H; y++) {
        int floor_row = y >= FLOOR_Y && soft.px;
        const Pc5Image *from = floor_row ? &soft : small;
        /* above the floor the picture's own row; below it the row as far above the floor, a
         * little under twice as far down */
        float at = floor_row ? FLOOR_Y - (float)(y - FLOOR_Y) / 1.9f : (float)y;
        int sy = (int)((2 * at + 1) * (float)from->h * 128 / H) - 128;
        if (sy < 0) sy = 0;
        int y0 = sy >> 8, fy = sy & 255, y1 = y0 + 1 < from->h ? y0 + 1 : y0;
        if (y0 > from->h - 1) y0 = y1 = from->h - 1;
        const uint32_t *r0 = from->px + (size_t)y0 * (size_t)from->w, *r1 = from->px + (size_t)y1 * (size_t)from->w;
        if (y == 0 || y == FLOOR_Y)
            for (int x = 0; x < W; x++) {
                int sx = (int)((2 * (int64_t)x + 1) * from->w * 128 / W) - 128;
                if (sx < 0) sx = 0;
                x0s[x] = sx >> 8;
                fxs[x] = sx & 255;
                x1s[x] = x0s[x] + 1 < from->w ? x0s[x] + 1 : x0s[x];
            }
        for (int x = 0; x < W; x++) {
            int fx = fxs[x], m = vertical[y] * horizontal[x] >> 8;
            uint32_t a = r0[x0s[x]], b = r0[x1s[x]], c = r1[x0s[x]], d = r1[x1s[x]], px = 0xff000000u;
            for (int k = 0; k < 24; k += 8) {
                int top = (int)((a >> k) & 255) * (256 - fx) + (int)((b >> k) & 255) * fx;
                int bottom = (int)((c >> k) & 255) * (256 - fx) + (int)((d >> k) & 255) * fx;
                int v = (top * (256 - fy) + bottom * fy) >> 16;
                px |= (uint32_t)(v * m >> 8) << k;
            }
            out[(size_t)y * W + (size_t)x] = px;
        }
    }
    /* the floor's edge catches the light */
    for (int x = 0; x < W; x++) {
        float e = fabsf((float)x - W / 2.0f) / (W / 2.0f), a = e < 0.4f ? 0.14f : e < 1 ? 0.14f * (1 - e) / 0.6f : 0;
        uint32_t p = out[(size_t)FLOOR_Y * W + (size_t)x];
        out[(size_t)FLOOR_Y * W + (size_t)x] = pc5_mix(p, 0xffffffffu, (int)(a * 255));
    }
    pc5_image_free(&soft);
    pc5_free(tables);
    return 0;
}

/* A backdrop for a game with no background of its own: its cover, filling the screen,
 * blurred to its colours, fuller and a little darker (then drawn as a background is). */
static int cover_backdrop(const Pc5Image *cover, uint32_t *out)
{
    Pc5Image wide = { 0 }, small = { 0 };
    int status;

    if (!cover->px || pc5_background_compose(cover, &wide) || pc5_image_resize(&wide, 192, 108, &small)) {
        pc5_image_free(&wide);
        pc5_image_free(&small);
        return -1;
    }
    pc5_image_free(&wide);
    pc5_image_blur(&small, 9);
    for (size_t i = 0; i < (size_t)small.w * (size_t)small.h; i++) {
        float h, s, v;
        hsv_of(small.px[i], &h, &s, &v);
        small.px[i] = from_hsv(h, fminf(1, s * 1.5f), v * 0.8f, 255);
    }
    status = background_prepare(&small, out);
    pc5_image_free(&small);
    return status;
}

/* A background made on a thread of its own, so the library keeps moving meanwhile: from the
 * game's kept file, or from (a copy of) its cover. */
static void *bg_job_main(void *argument)
{
    BgJob *job = argument;
    Pc5Image small = { 0 };

    if (job->path[0])
        job->ok = !pc5_cover_load(job->path, &small, NULL) && small.w == PC5_BACKGROUND_WIDTH &&
                  small.h == PC5_BACKGROUND_HEIGHT && !background_prepare(&small, job->out);
    else
        job->ok = !cover_backdrop(&job->cover, job->out);
    pc5_image_free(&small);
    atomic_store(&job->done, 1);
    return NULL;
}

/* What is shown now becomes picture (NULL: PC5's own backdrop), fading in. */
static void bg_show(Pc5Ui *ui, uint32_t *picture, const char *key)
{
    pc5_copy(ui->bg_key, sizeof(ui->bg_key), key);
    if (!picture && !ui->bg_has) return;
    uint32_t *spare = ui->bg_old;
    ui->bg_old = ui->bg;
    ui->bg_old_has = ui->bg_has;
    if (picture) {
        ui->bg = picture;
        ui->bg_has = 1;
        ui->bg_job.out = spare;   /* the next one is made there */
    } else {
        ui->bg = spare;
        ui->bg_has = 0;
    }
    ui->bg_fade_at = ui->now;
    ui->dirty = 1;
}

/* The chosen game's background, looked for once it stays chosen a moment, and again when
 * the worker has saved one or its cover has loaded: its kept file, or a backdrop made from its
 * cover. Either is made on a thread (bg_job_main); a finished one is shown if it is still the
 * one wanted. */
static void background_follow(Pc5Ui *ui)
{
    BgJob *job = &ui->bg_job;
    char path[PC5_PATH];

    if (job->running && atomic_load(&job->done)) {
        pthread_join(job->thread, NULL);
        job->running = 0;
        pc5_image_free(&job->cover);
        if (job->ok && !strcmp(job->key, ui->bg_want)) bg_show(ui, job->out, job->key);
    }
    if (ui->selected != ui->bg_selected) {
        ui->bg_selected = ui->selected;
        ui->bg_selected_at = ui->now;
    }
    unsigned generation = atomic_load(&ui->backgrounds.generation);
    const char *id = ui->library.count ? ui->library.games[ui->selected].id : "";
    const Cover *cover = ui->library.count ? &ui->covers[ui->selected] : NULL;
    int cover_ready = cover && cover->state && cover->picture.levels;
    int same = !strcmp(id, ui->bg_id) && ui->bg_id[0];
    int cover_late = same && ui->bg_cover_wait && cover_ready;   /* its cover loaded after it was chosen */
    if (!(same && generation == ui->bg_generation && !cover_late) &&
        (same || ui->now - ui->bg_selected_at >= BG_SETTLE_MS || !ui->bg_id[0])) {
        struct stat st;
        ui->bg_generation = generation;
        pc5_copy(ui->bg_id, sizeof(ui->bg_id), id[0] ? id : "-");
        ui->bg_cover_wait = 0;
        if (!ui->library.count) {
            ui->bg_want[0] = 0;
        } else if (!pc5_background_path(ui->root, ui->library.games[ui->selected].prefix, path, sizeof(path)) &&
                   !stat(path, &st)) {
            snprintf(ui->bg_want, sizeof(ui->bg_want), "%s|file|%lld|%lld", id, (long long)st.st_mtime, (long long)st.st_size);
        } else if (cover_ready) {
            snprintf(ui->bg_want, sizeof(ui->bg_want), "%s|cover", id);
        } else {
            snprintf(ui->bg_want, sizeof(ui->bg_want), "%s|none", id);
            ui->bg_cover_wait = 1;
        }
    }
    if (!strcmp(ui->bg_want, ui->bg_key) || job->running) return;
    if (!ui->bg_want[0] || pc5_ends_with_ci(ui->bg_want, "|none")) {
        bg_show(ui, NULL, ui->bg_want);
        return;
    }
    /* Made anew: the file, or the cover now chosen. */
    int from_file = !pc5_ends_with_ci(ui->bg_want, "|cover");
    path[0] = 0;
    if ((from_file && pc5_background_path(ui->root, ui->library.games[ui->selected].prefix, path, sizeof(path))) ||
        (!from_file && (!cover_ready || image_copy(&job->cover, &cover->picture.level[cover->picture.levels - 1]))) ||
        (!job->out && !(job->out = pc5_alloc((size_t)W * H * 4)))) {
        pc5_image_free(&job->cover);
        pc5_copy(ui->bg_key, sizeof(ui->bg_key), ui->bg_want);   /* not again for it */
        return;
    }
    pc5_copy(job->key, sizeof(job->key), ui->bg_want);
    pc5_copy(job->path, sizeof(job->path), from_file ? path : "");
    job->ok = 0;
    atomic_store(&job->done, 0);
    if (!pthread_create(&job->thread, NULL, bg_job_main, job)) {
        job->running = 1;
    } else {
        bg_job_main(job);   /* no thread: made here */
        pc5_image_free(&job->cover);
        if (job->ok) bg_show(ui, job->out, job->key);
        else pc5_copy(ui->bg_key, sizeof(ui->bg_key), ui->bg_want);
    }
}

/* ---- a game's cover or background, chosen by hand --------------------------------------------- */

enum { PICK_COVER, PICK_BACKGROUND, PICK_ICON };
static const char *const pick_kinds[] = { "cover", "background", "icon" };

/* What the picture at is: -1 the game's own now, 0 to count - 1 a choice found, count the
 * last: the card made from the game's icon and name, or (a background) none: a backdrop made
 * from its cover. */
static int pick_entry(const Pc5Ui *ui, int at)
{
    return ui->pick_now && at == 0 ? -1 : at - ui->pick_now;
}

static void pick_clear(Pc5Ui *ui)
{
    pc5_image_free(&ui->pick_image);
    pc5_picture_free(&ui->pick_picture);
    ui->pick_tint = C_DEFAULT_TINT;
}

static void picture_close(Pc5Ui *ui)
{
    pick_clear(ui);
    pc5_free(ui->pick);
    ui->pick = NULL;
}

/* Back to the game's menu, on the picker's row. */
static void picture_leave(Pc5Ui *ui)
{
    GameRow row = ui->pick_kind == PICK_COVER ? GR_COVER : ui->pick_kind == PICK_ICON ? GR_ICON : GR_BACKGROUND;
    picture_close(ui);
    game_menu_at(ui, row);
    go(ui, S_GAME_MENU);
}

/* Where the game keeps its picture of the kind. */
static int pick_path(Pc5Ui *ui, const Pc5Game *game, char *out, size_t size)
{
    if (ui->pick_kind == PICK_ICON)
        return game->prefix[0] && strcmp(game->prefix, "default")
                   ? pc5_format(out, size, "%s/prefixes/%s/pc5/icon.bin", ui->root, game->prefix) : -1;
    return ui->pick_kind == PICK_COVER ? cover_path(ui, game, out, size)
                                       : pc5_background_path(ui->root, game->prefix, out, size);
}

/* A search for the game's own names (with the player's own picture among the covers), or for
 * the name typed. */
static void pick_search(Pc5Ui *ui, const Pc5Game *game)
{
    Pc5CoverHint hint;
    /* A key added since PC5 opened counts from this search on. */
    pc5_api_key(ui->root, PC5_STEAMGRIDDB_KEY, ui->griddb_key, sizeof(ui->griddb_key));
    if (ui->pick_query[0]) {
        memset(&hint, 0, sizeof(hint));
        pc5_copy(hint.folder, sizeof(hint.folder), ui->pick_query);
        hint_sources(ui, &hint);
    } else {
        hint_for_game(ui, game, &hint);
        hint.picture_only = 0;
    }
    /* Square pictures for its tile: SteamGridDB's, the player's own cover being a cover. */
    if (ui->pick_kind == PICK_ICON) {
        hint.icons = 1;
        hint.picture[0] = 0;
    }
    pc5_cover_search_start(ui->pick, &hint);
    go(ui, S_PICTURE_SEARCH);
}

/* Appearance › Cover or Background: what the game has, then what is found for it. */
static void picture_open(Pc5Ui *ui, const Pc5Game *game, int kind)
{
    char path[PC5_PATH];
    if (!game->pc5 || !game->prefix[0] || !strcmp(game->prefix, "default")) {
        message(ui, 0, "PC5 didn't install this game",
                kind == PICK_COVER ? "Its cover can only be changed as in Covers on the wiki: a picture of your own."
                : kind == PICK_ICON ? "Only a game PC5 installed has a tile picture."
                                    : "Its background can only be changed as in Covers on the wiki: a picture of your own.",
                S_GAME_MENU);
        return;
    }
    picture_close(ui);
    if (!(ui->pick = pc5_alloc(sizeof(*ui->pick)))) {
        message(ui, 0, "PC5 ran out of memory", "Close PC5 and open it again.", S_GAME_MENU);
        return;
    }
    ui->pick_kind = kind;
    ui->pick_query[0] = 0;
    ui->pick_now = !pick_path(ui, game, path, sizeof(path)) && pc5_is_file(path);
    say(ui, "PC5 picture kind=%s game=%s now=%d", pick_kinds[kind], game->id, ui->pick_now);
    pick_search(ui, game);
}

/* The search is done: a background's choices are those with a background, the first shown is
 * the game's own (or, for a name typed, the first found). */
static void pick_searched(Pc5Ui *ui)
{
    Pc5CoverSearch *pick = ui->pick;
    if (ui->pick_kind == PICK_BACKGROUND || ui->pick_kind == PICK_ICON) {
        int n = 0;
        for (int i = 0; i < pick->count; i++)
            if (ui->pick_kind == PICK_ICON ? pick->choices[i].icon[0] != 0
                                           : pick->choices[i].background[0] || pick->choices[i].steam_appid)
                pick->choices[n++] = pick->choices[i];
        pick->count = n;
    }
    ui->pick_count = ui->pick_now + pick->count + 1;
    ui->pick_at = ui->pick_query[0] && pick->count ? ui->pick_now : 0;
    ui->pick_step = 1;   /* one that can't be had: the next one found, not the one before */
    ui->pick_fetching = 1;
    say(ui, "PC5 picture choices=%d answered=%d query=%s", pick->count, pick->answered, ui->pick_query[0] ? ui->pick_query : "-");
    go(ui, S_PICTURE);
}

/* The picture at pick_at, fetched or made; one that can't be had is taken out of the list. */
/* None, for a background: the backdrop made from the game's cover, at a background's size. */
static void none_preview(Pc5Ui *ui, const Pc5Game *game, Pc5Image *out)
{
    int index = pc5_library_find(&ui->library, game->id);
    uint32_t *screen;

    if (index < 0) return;
    if (!ui->covers[index].state) cover_load(ui, index);
    if (!ui->covers[index].state || !ui->covers[index].picture.levels || !(screen = pc5_alloc((size_t)W * H * 4))) return;
    if (!cover_backdrop(&ui->covers[index].picture.level[ui->covers[index].picture.levels - 1], screen)) {
        Pc5Image full = { .px = screen, .w = W, .h = H };
        pc5_image_resize(&full, PC5_BACKGROUND_WIDTH, PC5_BACKGROUND_HEIGHT, out);
    }
    pc5_free(screen);
}

/* The tile picture made from the game's cover (pc5_home_icon), as its tile has it without one. */
static void icon_from_cover(Pc5Ui *ui, const Pc5Game *game, Pc5Image *out)
{
    char path[PC5_PATH];
    Pc5Image cover = { 0 };
    uint32_t tint = C_DEFAULT_TINT;

    if (cover_path(ui, game, path, sizeof(path)) || pc5_cover_load(path, &cover, &tint)) cover = (Pc5Image){ 0 };
    if (pc5_home_icon(cover.px ? &cover : NULL, tint & 0xffffffu, out)) *out = (Pc5Image){ 0 };
    pc5_image_free(&cover);
}

static void pick_fetch(Pc5Ui *ui, const Pc5Game *game, int step)
{
    char path[PC5_PATH];
    Pc5CoverSearch *pick = ui->pick;

    ui->pick_fetching = 0;
    pick_clear(ui);
    for (;;) {
        int entry = pick_entry(ui, ui->pick_at), failed;
        if (entry < 0) {
            failed = pick_path(ui, game, path, sizeof(path)) ||
                     pc5_cover_load(path, &ui->pick_image, ui->pick_kind == PICK_COVER ? &ui->pick_tint : NULL);
            if (!failed) break;
            ui->pick_now = 0;
        } else if (entry < pick->count) {
            const Pc5CoverChoice *choice = &pick->choices[entry];
            failed = ui->pick_kind == PICK_COVER ? pc5_cover_fetch(choice, &ui->platform.http, &ui->pick_image)
                     : ui->pick_kind == PICK_ICON ? pc5_icon_fetch(choice, &ui->platform.http, &ui->pick_image)
                     : pc5_background_fetch(choice->background, choice->steam_appid, &ui->platform.http, &ui->pick_image);
            if (!failed) {
                if (ui->pick_kind == PICK_COVER) ui->pick_tint = pc5_image_tint(&ui->pick_image);
                break;
            }
            say(ui, "PC5 picture fetch failed source=%s title=%s", choice->source, choice->title);
            memmove(&pick->choices[entry], &pick->choices[entry + 1], sizeof(pick->choices[0]) * (size_t)(pick->count - entry - 1));
            pick->count--;
        } else {
            if (ui->pick_kind == PICK_COVER) card_for(ui, game, &ui->pick_image, &ui->pick_tint);
            else if (ui->pick_kind == PICK_ICON) icon_from_cover(ui, game, &ui->pick_image);
            else none_preview(ui, game, &ui->pick_image);
            break;
        }
        /* Taken out: the list is one shorter, and the next one (going back, the one before) is tried. */
        pick_clear(ui);
        ui->pick_count--;
        if (step < 0) ui->pick_at = ui->pick_at > 0 ? ui->pick_at - 1 : ui->pick_count - 1;
        else if (ui->pick_at >= ui->pick_count) ui->pick_at = 0;
    }
    if (ui->pick_kind == PICK_COVER && ui->pick_image.px) {
        Pc5Image copy;
        if (!image_copy(&copy, &ui->pick_image)) pc5_picture_make(&ui->pick_picture, &copy);
    }
    ui->dirty = 1;
}

/* The game's tile, an app of its own, gets the pictures just changed: its icon0.png and
 * background, in its folder and in the console's copies. A forwarder tile is left as it was. */
/* 1 when the game has a tile of that kind (its folder in dir, its runtime in runtime). */
static int tile_app(Pc5Ui *ui, const Pc5Game *game, char dir[PC5_PATH], char runtime[PC5_PATH])
{
    char tile_game[PC5_ID], error[PC5_ERROR];
    return ui->config.tiles_root && ui->tile[0] && !pc5_join(dir, PC5_PATH, ui->config.tiles_root, ui->tile) &&
           pc5_tile_read_all(dir, tile_game, runtime, PC5_PATH, error, sizeof(error)) == 1 &&
           !strcmp(tile_game, game->id) && runtime[0];
}

static void tile_art_refresh(Pc5Ui *ui, const Pc5Game *game)
{
    char dir[PC5_PATH], runtime[PC5_PATH], error[PC5_ERROR];

    if (!tile_app(ui, game, dir, runtime)) return;
    int status = pc5_tile_art(dir, ui->tile, ui->root, game->prefix, runtime, ui->config.user, error, sizeof(error));
    say(ui, "PC5 tile art tile=%s game=%s status=%d%s%s", ui->tile, game->id, status, status ? " why=" : "",
        status ? error : "");
}

/* The picture shown becomes the game's, in place of the one it had: a cover, a background, or
 * none (a backdrop made from its cover, not looked for again). Only the picture as PC5 keeps
 * it is saved, not the one downloaded. */
static void pick_use(Pc5Ui *ui, const Pc5Game *game)
{
    char path[PC5_PATH], dir[PC5_PATH], note[PC5_PATH];
    int entry = pick_entry(ui, ui->pick_at), failed;

    if (entry < 0) {   /* the one it has */
        picture_leave(ui);
        return;
    }
    failed = pick_path(ui, game, path, sizeof(path)) ||
             pc5_format(dir, sizeof(dir), "%s/prefixes/%s/pc5", ui->root, game->prefix) || pc5_mkdirs(dir);
    if (!failed && ui->pick_kind == PICK_COVER) {
        failed = !ui->pick_image.px || pc5_cover_save(path, &ui->pick_image, ui->pick_tint);
        if (!failed) {
            int index = pc5_library_find(&ui->library, game->id);
            if (index >= 0) {   /* drawn again from its file */
                pc5_picture_free(&ui->covers[index].picture);
                ui->covers[index].state = 0;
            }
            ui->bg_id[0] = 0;   /* a backdrop made from the cover is made again */
            ui->bg_key[0] = 0;
        }
    } else if (!failed && ui->pick_kind == PICK_ICON) {
        /* The last: made from its cover, which the tile is when it has no picture of its own. */
        if (entry >= ui->pick->count) failed = pc5_exists(path) && unlink(path);
        else failed = !ui->pick_image.px || pc5_cover_save(path, &ui->pick_image, 0);
    } else if (!failed) {
        /* The worker, waiting while the picker is open, is stopped first: it doesn't save over
         * this. It looks at the other games again when the library next shows. */
        pc5_backgrounds_end(&ui->backgrounds, 1);
        ui->backgrounds_for = -1;
        if (entry >= ui->pick->count) {
            /* the note first: the background goes only once nothing is looked for again */
            failed = pc5_format(note, sizeof(note), "%s/background-none.txt", dir) || pc5_write_file(note, "none\n", 5);
            if (!failed && pc5_exists(path) && unlink(path)) failed = 1;
        } else {
            failed = !ui->pick_image.px || pc5_cover_save(path, &ui->pick_image, 0);
            if (!failed && !pc5_format(note, sizeof(note), "%s/background-none.txt", dir)) unlink(note);
        }
        if (!pc5_format(note, sizeof(note), "%s/background-tries.txt", dir)) unlink(note);
        ui->bg_id[0] = 0;   /* looked at again: the file changed */
    }
    say(ui, "PC5 picture saved kind=%s game=%s entry=%d of=%d failed=%d", pick_kinds[ui->pick_kind], game->id, entry,
        ui->pick->count, failed);
    if (failed) {
        message(ui, 0, ui->pick_kind == PICK_COVER ? "The cover wasn't changed"
                       : ui->pick_kind == PICK_ICON ? "The tile picture wasn't changed" : "The background wasn't changed",
                entry < ui->pick->count || ui->pick_kind != PICK_COVER || ui->pick_image.px
                    ? "PC5 can't write to the game's folder in /data/prospero-win/prefixes."
                    : "PC5 couldn't make the card from the game's icon and name.",
                S_PICTURE);
        return;
    }
    /* The tile's 4K background takes a few seconds to make: said on a screen of its own. */
    {
        char dir[PC5_PATH], runtime[PC5_PATH];
        int tile = tile_app(ui, game, dir, runtime);
        picture_leave(ui);
        if (tile) go(ui, S_TILE_ART);
    }
}

static void speed_reset(Pc5Ui *ui)
{
    ui->speed_at = ui->now;
    ui->speed_bytes = ui->install.bytes_done;
    ui->speed = 0;
    ui->speed_phase = ui->install.phase;
}

/* 1 when the review's folder is on a USB drive, so it can be played from there. */
static int review_on_usb(const Pc5Ui *ui)
{
    char relative[PC5_PATH];
    return ui->queue_review < 0 && !pc5_usb_relative(ui->folder.path, relative, sizeof(relative));
}

/* The review's game installed: copied to the console, or (in_place) played from its USB drive. */
static void begin_install_as(Pc5Ui *ui, int in_place)
{
    Pc5Image cover = ui->review_image;
    int status;

    ui->review_image = (Pc5Image){ 0 };
    status = (in_place ? pc5_install_begin_usb : pc5_install_begin)(&ui->install, ui->root, ui->template_prefix,
                                                                    &ui->folder, ui->exe, &cover, ui->review_tint,
                                                                    ui->review_source);
    if (!status) say(ui, "PC5 install id=%s from=%s usb=%d", ui->install.id, ui->folder.path, in_place);
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
    if (ui->use_saves[0]) {
        if (pc5_install_use_saves(&ui->install, ui->use_saves))
            say(ui, "PC5 problem saves %s: %s", ui->use_saves, ui->install.error);
        else
            say(ui, "PC5 install saves=%s", ui->use_saves);
        ui->install.error[0] = 0;
        ui->use_saves[0] = 0;
    }
    if (ui->choice >= 0 && ui->choice < ui->search.count) {
        pc5_copy(ui->install.background, sizeof(ui->install.background), ui->search.choices[ui->choice].background);
        ui->install.background_appid = ui->search.choices[ui->choice].steam_appid;
    }
    speed_reset(ui);
    say(ui, "PC5 install id=%s from=%s exe=%s", ui->install.id, ui->folder.path, ui->install.info.exe);
    go(ui, S_INSTALL);
}

static void queue_summary(Pc5Ui *ui);

/* Settings › Uninstall game: the kept saves listed after the games. */
static void saves_load(Pc5Ui *ui)
{
    enum { MAX = 256 };
    if (!ui->saves) ui->saves = pc5_alloc(sizeof(*ui->saves) * MAX);
    ui->saves_count = ui->saves ? pc5_saves_list(ui->root, ui->saves, MAX) : 0;
}

/* The newer copy in ui->folder over the game in launch_id. */
static void begin_update(Pc5Ui *ui)
{
    int index = pc5_library_find(&ui->library, ui->launch_id);
    char title[200];

    snprintf(title, sizeof(title), "%s can't be updated", index >= 0 ? ui->library.games[index].title : "The game");
    if (pc5_update_begin(&ui->install, ui->root, ui->launch_id, &ui->folder)) {
        say(ui, "PC5 problem update %s: %s", ui->launch_id, ui->install.error);
        message(ui, 0, title, ui->install.error, S_SOURCES);
        pc5_install_end(&ui->install);
        return;
    }
    ui->installing = 1;
    speed_reset(ui);
    say(ui, "PC5 update id=%s from=%s exe=%s", ui->launch_id, ui->folder.path, ui->install.info.exe);
    atomic_store(&ui->backgrounds.stop, 1);   /* not to save into the game while it changes */
    go(ui, S_INSTALL);
}

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
        {
            /* Without asking only when one set of kept saves is the game's. */
            char kept[PC5_PATH];
            if (pc5_saves_find(ui->root, q->folder.name, q->folder.exes[q->exe].path, kept, sizeof(kept)) == 1) {
                if (pc5_install_use_saves(&ui->install, kept)) say(ui, "PC5 problem saves %s: %s", kept, ui->install.error);
                else say(ui, "PC5 install saves=%s", kept);
                ui->install.error[0] = 0;
            }
        }
        if (q->choice >= 0 && q->choice < q->search.count) {
            pc5_copy(ui->install.background, sizeof(ui->install.background), q->search.choices[q->choice].background);
            ui->install.background_appid = q->search.choices[q->choice].steam_appid;
        }
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
    if (ui->install.update) {
        /* An update of the game in launch_id. */
        char title[200], body[700], id[PC5_ID];
        int moved = ui->install.moving;
        pc5_copy(id, sizeof(id), ui->install.id);
        if (phase == PC5_INSTALL_DONE && ui->install.skipped) {
            snprintf(title, sizeof(title), "%s is partly updated", ui->install.info.title);
            snprintf(body, sizeof(body), "%u of its files couldn't be put in place. %s Update it again; its saves, settings "
                     "and options are as they were.", (unsigned)ui->install.skipped, ui->install.error);
            say(ui, "PC5 problem update %s: %u files left out: %s", id, (unsigned)ui->install.skipped, ui->install.error);
        } else if (phase == PC5_INSTALL_DONE) {
            snprintf(title, sizeof(title), "%s is updated", ui->install.info.title);
            snprintf(body, sizeof(body), "Its saves, settings and options are as they were.");
            say(ui, "PC5 updated id=%s files=%u", id, (unsigned)ui->install.files_done);
        } else if (phase == PC5_INSTALL_CANCELLED) {
            snprintf(title, sizeof(title), "Update stopped");
            snprintf(body, sizeof(body), "The copy made so far was deleted: the game is as it was. The USB drive wasn't changed.");
            say(ui, "PC5 update ended cancelled=1 id=%s", id);
        } else {
            snprintf(title, sizeof(title), "%s wasn't updated", ui->install.info.title);
            snprintf(body, sizeof(body), moved ? "%s PC5 finishes the update the next time it starts."
                                               : "%s The game is as it was.", ui->install.error);
            say(ui, "PC5 problem update %s: %s", id, ui->install.error);
        }
        int good = phase == PC5_INSTALL_DONE && !ui->install.skipped;
        pc5_install_end(&ui->install);
        ui->installing = 0;
        ui->update_for = 0;
        library_reload(ui, id);
        message(ui, good, title, body, S_LIBRARY);
        return;
    }
    if (phase == PC5_INSTALL_DONE) {
        char title[200], id[PC5_ID];
        int saves = ui->install.kept[0] != 0;
        pc5_copy(id, sizeof(id), ui->install.id);
        snprintf(title, sizeof(title), "%s is installed", ui->install.info.title);
        say(ui, "PC5 installed id=%s saves=%d", id, saves);
        pc5_install_end(&ui->install);
        ui->installing = 0;
        library_reload(ui, id);
        message(ui, 1, title, saves ? "It's in your library, with the saves and settings it had before."
                                    : "It's in your library. Press the button to play it from there.", S_LIBRARY);
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
        hint_for_folder(ui, &q->folder, q->exe, &hint);
        pc5_cover_search_start(&q->search, &hint);
        ui->queue_phase = 1;
        return 1;
    case 1:
        if (search_step(ui, &q->search)) return 1;
        ui->queue_phase = 2;
        return 1;
    default:
        q->choice = q->search.count ? 0 : -1;
        make_cover(ui, &q->folder, q->exe, &q->search, &q->choice, &q->cover, &q->tint, q->cover_source,
                   sizeof(q->cover_source));
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

static int offer_update(const Pc5Ui *ui);

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
        pc5_copy(about, about_size, "Deletes a game, or keeps its saves; deletes kept saves");
        break;
    case SR_COMPATIBILITY:
        *label = "Game compatibility list";
        pc5_copy(about, about_size, "Games players have tried, on your phone with a QR code");
        break;
    case SR_MAINTENANCE:
        *label = "Game maintenance";
        pc5_copy(about, about_size, "Installing, uninstalling, the compatibility list");
        pc5_copy(value, value_size, "\xe2\x80\xba");
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
    case SR_SYSTEM:
        *label = "System";
        if (ui->update.last.state == PC5_UPDATE_AVAILABLE) pc5_update_text(&ui->update.last, about, about_size);
        else pc5_copy(about, about_size, "Startup sound, logs, picture sources and updates");
        pc5_copy(value, value_size, "\xe2\x80\xba");
        break;
    case SR_PICTURES: {
        int on = 0;
        for (int i = 0; i < PC5_PICTURE_SOURCES; i++) on += !(set->pictures_off & (1u << i));
        *label = "Picture sources";
        pc5_copy(about, about_size, "Where covers and backgrounds are looked for");
        snprintf(value, value_size, "%d of %d \xe2\x80\xba", on, PC5_PICTURE_SOURCES);
        break;
    }
    case SR_KEEP_LOGS:
        *label = "Keep all logs";
        pc5_copy(about, about_size, set->keep_logs ? "Every session's log is kept for 7 days"
                                                   : "Sessions that went wrong, and each game's newest, for 7 days");
        pc5_copy(value, value_size, set->keep_logs ? "On" : "Off");
        break;
    case SR_FILES:
        *label = "File manager";
        pc5_copy(about, about_size, "Copy, move, rename and delete files on the console and its drives");
        pc5_copy(value, value_size, "\xe2\x80\xba");
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
    case SR_OVERLAY_SIZE:
        *label = "Size";
        pc5_copy(about, about_size,
                 set->overlay_size == PC5_OVERLAY_EXTRA_LARGE ? "Every line at once, twice the size, by the console"
                 : set->overlay_size == PC5_OVERLAY_LARGE     ? "Twice the size, two lines at a time, taking turns"
                                                              : "Every line at once, in small text");
        pc5_copy(value, value_size, pc5_overlay_size_name(set->overlay_size));
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
        pc5_copy(about, about_size, "Asks GitHub for a newer PC5 each time you open PC5, and offers it");
        pc5_copy(value, value_size, set->check_updates ? "On" : "Off");
        break;
    case SR_CHECK_NOW:
        *label = "Check now";
        if (ui->update.last.state == PC5_UPDATE_AVAILABLE && !offer_update(ui))
            snprintf(about, about_size, "PC5 %s is out: copy it over this one from github.com/bornaradusin/PC5",
                     ui->update.last.version);
        else
            pc5_update_text(&ui->update.last, about, about_size);
        break;
    case SR_STARTUP_SOUND:
        *label = "Startup sound";
        pc5_copy(about, about_size, "On the controller's speaker when PC5 opens");
        pc5_copy(value, value_size, strcmp(set->startup_sound, "off") ? set->startup_sound : "Off");
        break;
    case SR_STARTUP_VOLUME:
        *label = "Startup sound volume";
        pc5_copy(about, about_size, "How loud the startup sound plays");
        percent_text(set->startup_volume, value, value_size);
        break;
    case SR_UPDATE_NOW:
        *label = "Update now";
    {
        char how[200];
        update_how(ui, how, sizeof(how));
        snprintf(about, about_size, "%.160s, installs it and restarts PC5", how);
    }
        break;
    default:
        if (row >= SR_SOURCE && row < SR_SOURCE + PC5_PICTURE_SOURCES) {
            static const char *const abouts[PC5_PICTURE_SOURCES] = {
                "Its store's covers and backgrounds", "Covers and backgrounds its members made, with your key",
                "Its catalogue's covers and backgrounds", "The cover on the game's page, and its Steam background",
                "Steam's pictures for a game Steam's search misses"
            };
            int i = (int)(row - SR_SOURCE), off = (set->pictures_off & (1u << i)) != 0;
            *label = pc5_picture_source_name(i);
            pc5_copy(about, about_size, abouts[i]);
            pc5_copy(value, value_size, off ? "Off" : "On");
            /* SteamGridDB asks for the player's key: where it goes, or that it was refused. */
            if (i == 1 && !ui->griddb_key[0]) {
                pc5_copy(about, about_size, "Needs your key, in /data/prospero-win/" PC5_API_KEYS);
                if (!off) pc5_copy(value, value_size, "No key");
            } else if (i == 1 && ui->griddb_refused) {
                pc5_copy(about, about_size, "It refused the key in /data/prospero-win/" PC5_API_KEYS ": check it there");
            }
        }
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
    case SR_OVERLAY_SIZE:
        set->overlay_size = (Pc5OverlaySize)((set->overlay_size + PC5_OVERLAY_SIZES + step) % PC5_OVERLAY_SIZES);
        break;
    case SR_VIBRATION: set->vibration = pc5_strength_next(set->vibration, 0, step); break;
    case SR_TRIGGER_RUMBLE: set->trigger_rumble = pc5_strength_next(set->trigger_rumble, 25, step); break;
    case SR_TRIGGER_RESISTANCE: set->trigger_resistance = pc5_strength_next(set->trigger_resistance, 25, step); break;
    case SR_LIGHT: set->light_bar = (Pc5Light)((set->light_bar + PC5_LIGHTS + step) % PC5_LIGHTS); break;
    case SR_TRIGGERS: set->triggers = (Pc5Triggers)((set->triggers + PC5_TRIGGERS_COUNT + step) % PC5_TRIGGERS_COUNT); break;
    case SR_CHECK_ON_OPEN: set->check_updates = !set->check_updates; break;
    case SR_STARTUP_SOUND: {
        /* Off, then each sound by name; the one chosen is played. */
        Pc5SoundList list;
        int at = -1, next;
        pc5_sound_list(ui->app, ui->root, &list);
        for (int i = 0; i < list.count; i++)
            if (!pc5_casecmp(list.name[i], set->startup_sound)) at = i;
        next = at + (step < 0 ? -1 : 1);
        if (next < -1) next = list.count - 1;
        if (next >= list.count) next = -1;
        pc5_copy(set->startup_sound, sizeof(set->startup_sound), next < 0 ? "off" : list.name[next]);
        if (next >= 0 && ui->platform.sound)
            ui->platform.sound(ui->platform.context, set->startup_sound, set->startup_volume);
        break;
    }
    case SR_STARTUP_VOLUME:
        /* The sound is played at the new volume. */
        set->startup_volume = pc5_volume_next(set->startup_volume, step);
        if (ui->platform.sound) ui->platform.sound(ui->platform.context, set->startup_sound, set->startup_volume);
        break;
    default:
        if (row < SR_SOURCE || row >= SR_SOURCE + PC5_PICTURE_SOURCES) return 0;
        set->pictures_off ^= 1u << (row - SR_SOURCE);
        break;
    }
    if (pc5_settings_save(ui->root, set)) say(ui, "PC5 problem settings: can't write them");
    say(ui, "PC5 settings fps=%d frame_time=%d cpu=%d cpu_temp=%d soc_temp=%d memory=%d corner=%d size=%s vibration=%d light=%d "
        "triggers=%d trigger_rumble=%d trigger_resistance=%d updates=%d keep_logs=%d startup_sound=%s startup_volume=%d "
        "pictures_off=%#x",
        set->fps, set->frame_time, set->cpu_usage, set->cpu_temperature, set->soc_temperature, set->memory,
        (int)set->fps_corner, pc5_overlay_size_key(set->overlay_size), set->vibration, (int)set->light_bar, (int)set->triggers, set->trigger_rumble,
        set->trigger_resistance, set->check_updates, set->keep_logs, set->startup_sound, set->startup_volume,
        (unsigned)set->pictures_off);
    return 1;
}

/* The game's controller, kept after every change (it reaches the game's profile at its next start). */
static void controls_changed(Pc5Ui *ui)
{
    const Pc5Controls *k = &ui->controls;
    if (pc5_controls_save(ui->root, ui->launch_id, k)) say(ui, "PC5 problem controls %s: can't write them", ui->launch_id);
    say(ui, "PC5 controls id=%s mode=%s left=%s right=%s speed=%d slow=%s/%d frame_generation=%d memory_32bit=%s "
        "video_memory=%s resolution=%s aspect_ratio=%s cpu_sharing=%d graphics_api=%s", ui->launch_id,
        pc5_pad_mode_name(k->mode), pc5_stick_use_name(k->stick[0]), pc5_stick_use_name(k->stick[1]), k->speed,
        k->slow_button >= 0 ? pc5_control_name(k->slow_button) : "none", k->slow_percent, k->frame_generation,
        k->memory_4gb ? "4gb" : "auto", k->video_memory_full ? "full" : "auto", k->resolution == 2 ? "2160p" : k->resolution == 1 ? "1440p" : "1080p",
        k->stretch ? "stretch" : "keep", k->cpu_sharing, pc5_graphics_name(k->graphics, ui_game_api(ui)));
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
    case PR_SLOW: pc5_controls_next_slow(k, step); return 1;
    case PR_SLOW_PERCENT: {
        int percent = k->slow_percent + (step < 0 ? -PC5_SLOW_STEP : PC5_SLOW_STEP);
        if (percent < PC5_SLOW_STEP || percent > PC5_SLOW_MAX) return 0;
        k->slow_percent = percent;
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
    ui->game_page = GP_MAIN;
    go(ui, S_GAME_MENU);
}

/* The game's home screen tile: made, or taken away when it has one. */
static void home_toggle(Pc5Ui *ui, const Pc5Game *game)
{
    char error[PC5_ERROR] = "", body[1600];

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
    /* An app of its own (pc5_tile.h) that runs the game from PC5's installed folder, when
     * PC5 finds that folder; otherwise a forwarder that starts PC5 with the game. */
    {
        Pc5UpgradeConfig find = { ui->platform.http, ui->app, ui->root, ui->config.drives, ui->config.user,
                                  ui->platform.log, ui->platform.context };
        char folder[PC5_PATH] = "", other[PC5_PATH] = "";
        int found = ui->app[0] ? pc5_upgrade_find(&find, folder, sizeof(folder), other, sizeof(other)) : -1;
        if (!found) {
            Pc5TileMake make = { ui->config.tiles_root, folder, ui->root, game->id, game->prefix, game->title,
                                 ui->config.user };
            int made = pc5_tile_make(&make, ui->tile, error, sizeof(error));
            say(ui, "PC5 home tile app id=%s runtime=%s made=%d tile=%s%s%s", game->id, folder, made, ui->tile,
                error[0] ? " why=" : "", error);
            if (made != PC5_TILE_MADE) {
                message(ui, 0, "The tile wasn't made", error, S_GAME_MENU);
                return;
            }
            snprintf(body, sizeof(body),
                     "Its tile, %s, appears on the home screen once you close PC5 and ShadowMountPlus finds it in %s. "
                     "It is the game's own app: it starts the game straight away, and leaving the game goes back to "
                     "the home screen. It runs from PC5's folder, %s: keep PC5 there.", ui->tile,
                     ui->config.tiles_root, folder);
            message(ui, 1, "Added to the home screen", body, S_GAME_MENU);
            return;
        }
        say(ui, "PC5 home tile forwarder id=%s: PC5's folder %s", game->id, found == -2 ? "is there twice" : "not found");
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

static void files_open(Pc5Ui *ui, int game);
static void files_update(Pc5Ui *ui, uint32_t k);

static void settings_open(Pc5Ui *ui, int page, int row)
{
    ui->page = page;
    ui->menu = row;
    go(ui, S_SETTINGS);
}

/* PC5 can update itself here: the screens were given its drives and a way to download. */
static int can_upgrade(const Pc5Ui *ui)
{
    return ui->config.drives && ui->platform.http.download && ui->app[0];
}

/* A newer PC5 to offer: Settings › System's Update now, and the question when PC5 opens. */
static int offer_update(const Pc5Ui *ui)
{
    return can_upgrade(ui) && ui->update.last.state == PC5_UPDATE_AVAILABLE;
}

/* Where that row is on the System page now (0 when it isn't there). */
static int system_row(const Pc5Ui *ui, SettingRow row)
{
    SettingRow rows[PAGE_ROWS];
    int count = page_rows(&ui->settings, PAGE_SYSTEM, offer_update(ui), rows);
    for (int i = 0; i < count; i++)
        if (rows[i] == row) return i;
    return 0;
}

/* Asks GitHub (blocking, one request) and tells with the console's notification once per
 * release, but not when asked to be quiet: PC5 offers to install it at once, its window says
 * so (as each time the player opens PC5), nor ever when quiet_if_offered is 2 (a window says so
 * whatever it offers). 1 when the release has PC5's zip. */
static int update_check_now(Pc5Ui *ui, int quiet_if_offered)
{
    Pc5Update update;
    pc5_update_check(&ui->platform.http, PC5_VERSION, &update);
    ui->update.last = update;
    ui->update.checked = unix_now(ui, NULL);
    say(ui, "PC5 update state=%d latest=%s zip=%d update_zip=%d from=%s small=%d", (int)update.state, update.version,
        update.asset_url[0] != 0, update.delta_url[0] != 0, update.delta_from[0] ? update.delta_from : "-",
        pc5_update_small(&update, PC5_VERSION));
    int offered = update.state == PC5_UPDATE_AVAILABLE && update.asset_url[0] && can_upgrade(ui);
    if ((offered && quiet_if_offered) || (update.state == PC5_UPDATE_AVAILABLE && quiet_if_offered == 2)) {
        pc5_copy(ui->update.told, sizeof(ui->update.told), update.version);
    } else if (update.state == PC5_UPDATE_AVAILABLE && strcmp(ui->update.told, update.version)) {
        char text[200];
        snprintf(text, sizeof(text), "PC5 %s is available on GitHub (you have %s).", update.version, PC5_VERSION);
        notify(ui, text);
        pc5_copy(ui->update.told, sizeof(ui->update.told), update.version);
    }
    if (pc5_update_save(ui->root, &ui->update)) say(ui, "PC5 problem update: can't remember the answer");
    return offered;
}

/* PC5 updates itself: the worker starts, the screen follows it. */
static void upgrade_begin(Pc5Ui *ui, Screen back)
{
    Pc5UpgradeConfig config = { ui->platform.http, ui->app, ui->root, ui->config.drives, ui->config.user,
                                ui->platform.log, ui->platform.context };
    ui->upgrade_back = back;
    ui->upgrade_step = -1;
    ui->speed = 0;
    say(ui, "PC5 update start latest=%s", ui->update.last.version);
    pc5_upgrade_start(&ui->upgrade, &config);
    go(ui, S_UPGRADING);
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
    /* Triangle: Report on GitHub, with this log. */
    ui->report = *issue;
    pc5_copy(ui->report_game, sizeof(ui->report_game), index >= 0 ? name : "");
    ui->message_report = 1;
}

/* Report on GitHub for ui->report: the form's link, as a QR code. */
static void report_open(Pc5Ui *ui)
{
    ui->report_qr.size = 0;
    if (pc5_report_url(&ui->report, ui->report_game, ui->report_url, sizeof(ui->report_url)) ||
        pc5_qr_make(ui->report_url, &ui->report_qr))
        ui->report_url[0] = 0;
    say(ui, "PC5 report log=%s link=%zu qr=%d", pc5_report_file(&ui->report), strlen(ui->report_url), ui->report_qr.size);
    go(ui, S_REPORT);
}

/* A newer PC5 than this one is known to be out. */
static int newer_known(const Pc5Ui *ui)
{
    return ui->update.last.state == PC5_UPDATE_AVAILABLE && pc5_version_compare(ui->update.last.version, PC5_VERSION) > 0;
}

/* Triangle on a kept log's message: a newer PC5 may have fixed it, so when one is out PC5
 * offers it first (S_REPORT_UPDATE). It asks GitHub (S_REPORT_CHECK) unless it did in the
 * last hour; with no answer, the report opens as before. */
static void report_begin(Pc5Ui *ui)
{
    int64_t now = unix_now(ui, NULL);
    int fresh = now > 0 && ui->update.checked && now >= ui->update.checked && now - ui->update.checked < PC5_UPDATE_EVERY_S;
    if (newer_known(ui)) {
        ui->button = 0;
        go(ui, S_REPORT_UPDATE);
    } else if (fresh && ui->update.last.state == PC5_UPDATE_CURRENT) {
        report_open(ui);
    } else {
        go(ui, S_REPORT_CHECK);
    }
}

/* From the start screens to the library, telling first what went wrong last time. */
static void to_library(Pc5Ui *ui)
{
    covers_clear(ui);
    go(ui, S_LIBRARY);
    if (ui->issue_pending) {
        issue_message(ui);
    } else if (ui->install_waiting) {
        /* Games put in PC5's INSTALL folder: asked about once each time PC5 opens. */
        ui->button = 1;   /* Install first */
        go(ui, S_INSTALL_ASK);
    }
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

/* ---- the file manager (pc5_files.h) ---------------------------------------------------------- */

/* Where it may write: Pc5UiConfig's drives (pc5_upgrade_drives when it gives none). */
static const char *const *files_drives(const Pc5Ui *ui)
{
    return ui->config.drives ? ui->config.drives : pc5_upgrade_drives;
}

/* a/b, without a second slash after "/". */
static int files_join(char *out, size_t size, const char *a, const char *b)
{
    size_t n = strlen(a);
    return n && a[n - 1] == '/' ? pc5_format(out, size, "%s%s", a, b) : pc5_join(out, size, a, b);
}

/* PC5 may change things in dir: in the game's folder (Game files), or on one of the drives and
 * not in PC5's own app (File manager); no part of it below there a link, which could lead
 * anywhere. */
static int files_writable(const Pc5Ui *ui, const char *dir)
{
    const char *base = NULL;
    char part[PC5_PATH];
    if (!dir[0]) return 0;
    if (ui->files_game) {
        if (pc5_files_within(ui->files_top, dir)) base = ui->files_top;
    } else {
        if (ui->app[0] && pc5_files_within(ui->app, dir)) return 0;
        /* PC5's own folder, as it is on the drive: only its INSTALL folder can change */
        if (ui->pc5_folder[0] && pc5_files_within(ui->pc5_folder, dir) &&
            !(ui->install_dir[0] && pc5_files_within(ui->install_dir, dir)))
            return 0;
        for (const char *const *d = files_drives(ui); *d && !base; d++)
            if (pc5_files_within(*d, dir)) base = *d;
    }
    if (!base) return 0;
    pc5_copy(part, sizeof(part), dir);
    for (size_t i = strlen(base);; i++) {
        char at = part[i];
        if (at == '/' || !at) {
            struct stat st;
            part[i] = 0;
            if (lstat(part, &st) || S_ISLNK(st.st_mode)) return 0;
            part[i] = at;
        }
        if (!at) break;
    }
    return 1;
}

/* An item that is never deleted, cut or renamed: a drive itself, prospero-win's folder, PC5's
 * app, or a folder holding either. */
static int files_protected(const Pc5Ui *ui, const char *item)
{
    if (pc5_files_within(item, ui->root) || (ui->app[0] && pc5_files_within(item, ui->app))) return 1;
    if ((ui->pc5_folder[0] && pc5_files_within(item, ui->pc5_folder)) ||
        (ui->install_dir[0] && pc5_files_within(item, ui->install_dir)))
        return 1;
    for (const char *const *d = files_drives(ui); *d; d++)
        if (pc5_files_within(item, *d)) return 1;
    return 0;
}

static void sel_free(FilesSel *sel)
{
    pc5_free(sel->paths);
    pc5_free(sel->list);
    memset(sel, 0, sizeof(*sel));
}

static int sel_make(FilesSel *sel, int count)
{
    memset(sel, 0, sizeof(*sel));
    if (count <= 0) return -1;
    sel->paths = pc5_alloc(sizeof(*sel->paths) * (size_t)count);
    sel->list = pc5_alloc(sizeof(*sel->list) * (size_t)count);
    if (!sel->paths || !sel->list) { sel_free(sel); return -1; }
    return 0;
}

static int files_ticks(const Pc5Ui *ui)
{
    int n = 0;
    if (ui->files_ticked)
        for (int i = 0; i < ui->files.count; i++) n += ui->files_ticked[i];
    return n;
}

/* The items an action is for: those ticked, or else the one chosen. */
static int files_targets(const Pc5Ui *ui, FilesSel *sel)
{
    int ticks = files_ticks(ui), count = ticks ? ticks : ui->files.count > 0;
    if (!ui->files_path[0] || sel_make(sel, count)) return -1;
    for (int i = 0; i < ui->files.count; i++) {
        if (ticks ? !ui->files_ticked[i] : i != ui->files_at) continue;
        if (files_join(sel->paths[sel->count], PC5_PATH, ui->files_path, ui->files.items[i].name)) {
            sel_free(sel);
            return -1;
        }
        sel->list[sel->count] = sel->paths[sel->count];
        sel->count++;
    }
    return sel->count ? 0 : (sel_free(sel), -1);
}

static int files_count(const Pc5Ui *ui)
{
    return ui->files_path[0] ? ui->files.count : ui->files_place_count;
}

/* The places to start from: the drives there are, PC5's app and the whole console (those two
 * read-only). */
static void files_places(Pc5Ui *ui)
{
    int n = 0;
    for (const char *const *d = files_drives(ui); *d && n < FILES_PLACES - 3; d++) {
        if (!pc5_is_dir(*d)) continue;
        FilesPlace *place = &ui->files_place[n++];
        const char *slash = strrchr(*d, '/'), *name = slash && slash[1] ? slash + 1 : *d;
        if (!strncmp(name, "usb", 3) && name[3] >= '0' && name[3] <= '9')
            snprintf(place->name, sizeof(place->name), "USB drive %d", atoi(name + 3) + 1);
        else if (!strncmp(name, "ext", 3) && name[3] >= '0' && name[3] <= '9')
            snprintf(place->name, sizeof(place->name), "Extended storage %d", atoi(name + 3) + 1);
        else if (!strcmp(*d, "/data"))
            pc5_copy(place->name, sizeof(place->name), "Console storage");
        else
            pc5_copy(place->name, sizeof(place->name), name);
        pc5_copy(place->path, sizeof(place->path), *d);
        place->read_only = 0;
    }
    if (ui->install_dir[0] && pc5_is_dir(ui->install_dir)) {
        FilesPlace *place = &ui->files_place[n++];
        pc5_copy(place->name, sizeof(place->name), "PC5's INSTALL folder");
        pc5_copy(place->path, sizeof(place->path), ui->install_dir);
        place->read_only = 0;
    }
    if (ui->app[0] && pc5_is_dir(ui->app)) {
        FilesPlace *place = &ui->files_place[n++];
        pc5_copy(place->name, sizeof(place->name), "PC5's app");
        pc5_copy(place->path, sizeof(place->path), ui->app);
        place->read_only = 1;
    }
    FilesPlace *place = &ui->files_place[n++];
    pc5_copy(place->name, sizeof(place->name), "Whole console");
    pc5_copy(place->path, sizeof(place->path), "/");
    place->read_only = 1;
    ui->files_place_count = n;
}

/* Reads the folder shown again, the row on select ("" or NULL: the first). */
static void files_load(Pc5Ui *ui, const char *select)
{
    char keep[PC5_FILES_NAME] = "";
    if (select) pc5_copy(keep, sizeof(keep), select);
    pc5_files_list_free(&ui->files);
    pc5_free(ui->files_ticked);
    ui->files_ticked = NULL;
    ui->files_at = ui->files_list_top = 0;
    if (!ui->files_path[0]) return;
    if (pc5_files_list(ui->files_path, &ui->files))
        say(ui, "PC5 files can't list %s: %s", ui->files_path, strerror(ui->files.error));
    if (ui->files.count) ui->files_ticked = pc5_alloc((size_t)ui->files.count);
    for (int i = 0; keep[0] && i < ui->files.count; i++)
        if (!strcmp(ui->files.items[i].name, keep)) ui->files_at = i;
}

static void files_open(Pc5Ui *ui, int game)
{
    ui->files_game = game;
    ui->files_top[0] = ui->files_path[0] = 0;
    ui->files_changed = 0;
    if (game) {
        if (pc5_game_folder(ui->root, ui->launch_id, ui->files_top, sizeof(ui->files_top)) || !pc5_is_dir(ui->files_top)) {
            int index = pc5_library_find(&ui->library, ui->launch_id);
            ui->files_top[0] = 0;
            if (index >= 0 && ui->library.games[index].usb)   /* played from its USB drive */
                message(ui, 0, "Plug in the game's USB drive", "Its files are on the USB drive it plays from.", S_GAME_MENU);
            else
                message(ui, 0, "PC5 can't find the game's folder", "Its files may be missing: uninstall it and install it again.",
                        S_GAME_MENU);
            return;
        }
        pc5_copy(ui->files_path, sizeof(ui->files_path), ui->files_top);
    } else {
        files_places(ui);
    }
    say(ui, "PC5 files open game=%d path=%s", game, ui->files_path[0] ? ui->files_path : "places");
    files_load(ui, NULL);
    go(ui, S_FILES);
}

static void files_close(Pc5Ui *ui)
{
    pc5_files_list_free(&ui->files);
    pc5_free(ui->files_ticked);
    ui->files_ticked = NULL;
    ui->files_path[0] = 0;
    say(ui, "PC5 files close changed=%d", ui->files_changed);
    if (ui->files_changed) library_reload(ui, NULL);
    if (ui->files_game) {
        game_menu_at(ui, GR_FILES);
        go(ui, S_GAME_MENU);
    } else {
        settings_open(ui, 0, ROW_FILES);
    }
}

/* Back: the folder above, the places from a place's top, out from the game's folder. */
static void files_up(Pc5Ui *ui)
{
    char name[PC5_FILES_NAME];
    if (!ui->files_path[0] || (ui->files_game && !strcmp(ui->files_path, ui->files_top))) {
        files_close(ui);
        return;
    }
    if (!strcmp(ui->files_path, ui->files_top)) {
        int at = 0;
        for (int i = 0; i < ui->files_place_count; i++)
            if (!strcmp(ui->files_place[i].path, ui->files_top)) at = i;
        ui->files_path[0] = ui->files_top[0] = 0;
        files_places(ui);
        files_load(ui, NULL);
        ui->files_at = at < ui->files_place_count ? at : 0;
        ui->dirty = 1;
        return;
    }
    char *slash = strrchr(ui->files_path, '/');
    if (!slash) { files_close(ui); return; }
    pc5_copy(name, sizeof(name), slash + 1);
    if (slash == ui->files_path) slash[1] = 0;
    else *slash = 0;
    files_load(ui, name);
    ui->dirty = 1;
}

static void files_menu_open(Pc5Ui *ui);

/* ✕: a place or a folder opens; a file shows what can be done with it. */
static void files_enter(Pc5Ui *ui)
{
    char next[PC5_PATH];
    if (!ui->files_path[0]) {
        if (ui->files_at >= ui->files_place_count) return;
        pc5_copy(ui->files_top, sizeof(ui->files_top), ui->files_place[ui->files_at].path);
        pc5_copy(ui->files_path, sizeof(ui->files_path), ui->files_top);
        files_load(ui, NULL);
        ui->dirty = 1;
        return;
    }
    if (ui->files_at >= ui->files.count) return;
    const Pc5FileItem *item = &ui->files.items[ui->files_at];
    if (!item->dir) {
        files_menu_open(ui);
        return;
    }
    if (files_join(next, sizeof(next), ui->files_path, item->name)) {
        message(ui, 0, "PC5 can't open this folder", "Its path is too long.", S_FILES);
        return;
    }
    pc5_copy(ui->files_path, sizeof(ui->files_path), next);
    files_load(ui, NULL);
    ui->dirty = 1;
}

/* What △ offers, as the folder and the items chosen allow. */
typedef enum FilesAction { FA_COPY, FA_CUT, FA_PASTE, FA_RENAME, FA_NEW_FOLDER, FA_DELETE, FA_TICK_ALL } FilesAction;
enum { FILES_ACTIONS = 7 };

static int files_actions(const Pc5Ui *ui, FilesAction rows[FILES_ACTIONS])
{
    FilesSel sel;
    int n = 0, targets = 0, locked = 0, write;
    if (!ui->files_path[0]) return 0;
    write = files_writable(ui, ui->files_path);
    if (!files_targets(ui, &sel)) {
        targets = sel.count;
        for (int i = 0; i < sel.count; i++) locked |= files_protected(ui, sel.list[i]);
        sel_free(&sel);
    }
    if (targets) rows[n++] = FA_COPY;
    if (targets && write && !locked) rows[n++] = FA_CUT;
    if (ui->clip.count && write) rows[n++] = FA_PASTE;
    if (targets == 1 && write && !locked) rows[n++] = FA_RENAME;
    if (write) rows[n++] = FA_NEW_FOLDER;
    if (targets && write && !locked) rows[n++] = FA_DELETE;
    if (ui->files.count) rows[n++] = FA_TICK_ALL;
    return n;
}

static void files_menu_open(Pc5Ui *ui)
{
    FilesAction rows[FILES_ACTIONS];
    if (!files_actions(ui, rows)) return;
    ui->menu = 0;
    go(ui, S_FILES_MENU);
}

static const char *const files_op_names[] = { "copy", "move", "delete" };

static void files_start(Pc5Ui *ui, Pc5FilesOp op, const FilesSel *sel, const char *dest, Pc5FilesConflict conflict)
{
    if (ui->files_busy) pc5_files_end(&ui->files_job);
    int rc = pc5_files_begin(&ui->files_job, op, sel->list, sel->count, dest, conflict);
    ui->files_busy = 1;
    say(ui, "PC5 files %s count=%d into=%s conflict=%d items=%u bytes=%llu rc=%d", files_op_names[op], sel->count,
        dest ? dest : "-", (int)conflict, (unsigned)ui->files_job.total_items, (unsigned long long)ui->files_job.total_bytes, rc);
    if (rc) {
        char title[120], body[PC5_ERROR + 40];
        snprintf(title, sizeof(title), op == PC5_FILES_DELETE ? "Nothing was deleted" : op == PC5_FILES_MOVE ? "Nothing was moved"
                                                                                                              : "Nothing was copied");
        pc5_copy(body, sizeof(body), ui->files_job.error);
        say(ui, "PC5 problem files: %s", body);
        pc5_files_end(&ui->files_job);
        ui->files_busy = 0;
        message(ui, 0, title, body, S_FILES);
        return;
    }
    go(ui, S_FILES_WORKING);
}

static void files_paste(Pc5Ui *ui)
{
    ui->files_conflicts = pc5_files_conflicts(ui->clip.list, ui->clip.count, ui->files_path, !ui->clip_cut);
    if (ui->files_conflicts) {
        ui->button = 0;   /* Skip first */
        go(ui, S_FILES_CONFLICT);
        return;
    }
    files_start(ui, ui->clip_cut ? PC5_FILES_MOVE : PC5_FILES_COPY, &ui->clip, ui->files_path, PC5_FILES_REPLACE);
}

/* The work finished: the folder read again, and a word when it didn't all happen. */
static void files_finish(Pc5Ui *ui)
{
    Pc5FilesJob *job = &ui->files_job;
    char title[160], body[PC5_ERROR + 200], keep[PC5_FILES_NAME] = "";
    Pc5FilesState state = job->state;
    Pc5FilesOp op = job->op;
    uint32_t done = job->done_items, skipped = job->skipped;

    if (ui->files_at < ui->files.count) pc5_copy(keep, sizeof(keep), ui->files.items[ui->files_at].name);
    ui->files_changed = 1;
    say(ui, "PC5 files done %s state=%d items=%u/%u bytes=%llu skipped=%u%s%s", files_op_names[op], (int)state,
        (unsigned)done, (unsigned)job->total_items, (unsigned long long)job->done_bytes, (unsigned)skipped,
        job->error[0] ? " error=" : "", job->error);
    pc5_copy(body, sizeof(body), job->error);
    pc5_files_end(job);
    ui->files_busy = 0;
    if (state == PC5_FILES_DONE && op == PC5_FILES_MOVE) {
        sel_free(&ui->clip);   /* they are here now */
        ui->clip_cut = 0;
    }
    files_load(ui, keep);
    if (state == PC5_FILES_DONE) {
        if (!done && skipped) message(ui, 1, "Nothing was pasted", "Those items are here already, and were left as they were.", S_FILES);
        else go(ui, S_FILES);
    } else if (state == PC5_FILES_STOPPED) {
        message(ui, 1, "Stopped",
                op == PC5_FILES_DELETE ? "What was deleted so far is gone; the rest is still here."
                : op == PC5_FILES_MOVE ? "What was moved so far is here. Nothing was deleted from where it came from."
                                       : "What was copied so far is here; the file being copied when it stopped was deleted.",
                S_FILES);
    } else {
        snprintf(title, sizeof(title), op == PC5_FILES_DELETE ? "Not everything was deleted"
                                       : op == PC5_FILES_MOVE ? "Not everything was moved" : "Not everything was copied");
        say(ui, "PC5 problem files: %s", body);
        message(ui, 0, title, body, S_FILES);
    }
}

/* A name typed for Rename or New folder. */
static void files_named(Pc5Ui *ui, const char *name)
{
    char why[200], target[PC5_PATH], from[PC5_PATH], body[PC5_PATH + 80];
    if (pc5_files_name_ok(name, why, sizeof(why))) {
        message(ui, 0, "That name can't be used", why, S_FILES);
        return;
    }
    if (files_join(target, sizeof(target), ui->files_path, name)) {
        message(ui, 0, "That name can't be used", "The path would be too long.", S_FILES);
        return;
    }
    if (ui->files_typing == 0) {
        if (ui->files_at >= ui->files.count) { go(ui, S_FILES); return; }
        const char *old = ui->files.items[ui->files_at].name;
        if (!strcmp(old, name) || files_join(from, sizeof(from), ui->files_path, old)) { go(ui, S_FILES); return; }
        /* Only its case changed: on exFAT the new name is the old one. */
        if (pc5_casecmp(old, name) && pc5_exists(target)) {
            snprintf(body, sizeof(body), "This folder has %.200s already.", name);
            message(ui, 0, "That name is taken", body, S_FILES);
            return;
        }
        if (rename(from, target)) {
            snprintf(body, sizeof(body), "%s.", strerror(errno));
            say(ui, "PC5 problem files rename %s: %s", from, body);
            message(ui, 0, "PC5 can't rename it", body, S_FILES);
            return;
        }
        say(ui, "PC5 files renamed %s to %s", from, name);
    } else {
        if (pc5_exists(target)) {
            snprintf(body, sizeof(body), "This folder has %.200s already.", name);
            message(ui, 0, "That name is taken", body, S_FILES);
            return;
        }
        if (mkdir(target, 0777)) {
            snprintf(body, sizeof(body), "%s.", strerror(errno));
            say(ui, "PC5 problem files new folder %s: %s", target, body);
            message(ui, 0, "PC5 can't make the folder", body, S_FILES);
            return;
        }
        (void)chmod(target, 0777);
        say(ui, "PC5 files new folder %s", target);
    }
    ui->files_changed = 1;
    files_load(ui, name);
    go(ui, S_FILES);
}

static void files_act(Pc5Ui *ui, FilesAction action)
{
    FilesSel sel;
    switch (action) {
    case FA_COPY:
    case FA_CUT:
        if (files_targets(ui, &sel)) { go(ui, S_FILES); break; }
        sel_free(&ui->clip);
        ui->clip = sel;
        ui->clip_cut = action == FA_CUT;
        pc5_copy(ui->clip_from, sizeof(ui->clip_from), ui->files_path);
        if (ui->files_ticked) memset(ui->files_ticked, 0, (size_t)ui->files.count);
        say(ui, "PC5 files %s count=%d from=%s", action == FA_CUT ? "cut" : "copied", ui->clip.count, ui->files_path);
        go(ui, S_FILES);
        break;
    case FA_PASTE:
        files_paste(ui);
        break;
    case FA_RENAME:
    case FA_NEW_FOLDER: {
        int rc = -1;
        ui->files_typing = action == FA_NEW_FOLDER;
        if (ui->platform.keyboard_open)
            rc = ui->platform.keyboard_open(ui->platform.context, action == FA_RENAME ? "Rename" : "New folder",
                                            action == FA_RENAME && ui->files_at < ui->files.count
                                                ? ui->files.items[ui->files_at].name : "New folder");
        say(ui, "PC5 keyboard open rc=%d", rc);
        if (rc >= 0) go(ui, S_FILES_TYPING);
        else message(ui, 0, "The keyboard didn't open", "The console's on-screen keyboard can't be used just now.", S_FILES);
        break;
    }
    case FA_DELETE:
        if (files_targets(ui, &sel)) { go(ui, S_FILES); break; }
        pc5_files_measure(sel.list, sel.count, &ui->files_size, 1500);
        sel_free(&sel);
        ui->button = 0;   /* Cancel first */
        go(ui, S_FILES_DELETE);
        break;
    case FA_TICK_ALL: {
        int all = files_ticks(ui) == ui->files.count;
        if (ui->files_ticked) memset(ui->files_ticked, !all, (size_t)ui->files.count);
        go(ui, S_FILES);
        break;
    }
    }
}

static void files_update(Pc5Ui *ui, uint32_t k)
{
    const uint32_t accept = PC5_KEY_ACCEPT, back = PC5_KEY_BACK;
    switch (ui->screen) {
    case S_FILES: {
        int count = files_count(ui), before = ui->files_at;
        list_move(&ui->files_at, count, k);
        if ((k & PC5_KEY_L1) && count) ui->files_at = ui->files_at > 8 ? ui->files_at - 8 : 0;
        if ((k & PC5_KEY_R1) && count) ui->files_at = ui->files_at + 8 < count ? ui->files_at + 8 : count - 1;
        if (ui->files_at != before) ui->dirty = 1;
        if (k & back) files_up(ui);
        else if ((k & accept) && count) files_enter(ui);
        else if ((k & PC5_KEY_SQUARE) && ui->files_path[0] && ui->files_ticked && ui->files_at < ui->files.count) {
            ui->files_ticked[ui->files_at] = !ui->files_ticked[ui->files_at];
            ui->dirty = 1;
        } else if (k & PC5_KEY_TRIANGLE) {
            files_menu_open(ui);
        }
        break;
    }
    case S_FILES_MENU: {
        FilesAction rows[FILES_ACTIONS];
        int count = files_actions(ui, rows);
        if (!count) { go(ui, S_FILES); break; }
        if (ui->menu >= count) ui->menu = count - 1;
        if (list_move(&ui->menu, count, k)) ui->dirty = 1;
        if (k & (back | PC5_KEY_TRIANGLE)) go(ui, S_FILES);
        else if (k & accept) files_act(ui, rows[ui->menu]);
        break;
    }
    case S_FILES_TYPING: {
        int state = ui->platform.keyboard_state ? ui->platform.keyboard_state(ui->platform.context) : -1;
        if (state == 1) break;   /* still up */
        char typed[PC5_FILES_NAME + 64] = "";
        int rc = ui->platform.keyboard_close ? ui->platform.keyboard_close(ui->platform.context, typed, sizeof(typed)) : -1;
        char *start = typed;
        while (*start == ' ' || *start == '\t') start++;
        for (size_t n = strlen(start); n && (start[n - 1] == ' ' || start[n - 1] == '\t'); n--) start[n - 1] = 0;
        say(ui, "PC5 keyboard closed rc=%d typed=%zu", rc, strlen(start));
        if (rc == 1 && start[0]) files_named(ui, start);
        else go(ui, S_FILES);   /* cancelled: as it was */
        break;
    }
    case S_FILES_CONFLICT:
        /* Skip, Keep both, Replace */
        if ((k & PC5_KEY_LEFT) && ui->button > 0) { ui->button--; ui->dirty = 1; }
        if ((k & PC5_KEY_RIGHT) && ui->button < 2) { ui->button++; ui->dirty = 1; }
        if (k & back) go(ui, S_FILES);
        else if ((k & accept) && ui->now - ui->entered > 300)
            files_start(ui, ui->clip_cut ? PC5_FILES_MOVE : PC5_FILES_COPY, &ui->clip, ui->files_path,
                        ui->button == 0 ? PC5_FILES_SKIP : ui->button == 1 ? PC5_FILES_KEEP_BOTH : PC5_FILES_REPLACE);
        break;
    case S_FILES_DELETE:
        if (k & (PC5_KEY_LEFT | PC5_KEY_RIGHT)) { ui->button = !ui->button; ui->dirty = 1; }
        if (k & back) go(ui, S_FILES);
        else if ((k & accept) && ui->now - ui->entered > 400) {
            FilesSel sel;
            if (!ui->button || files_targets(ui, &sel)) { go(ui, S_FILES); break; }
            files_start(ui, PC5_FILES_DELETE, &sel, NULL, PC5_FILES_REPLACE);
            sel_free(&sel);
        }
        break;
    case S_FILES_WORKING:
        if (!ui->files_busy) { go(ui, S_FILES); break; }
        if ((k & back) && !ui->files_job.stop) {
            say(ui, "PC5 files stop");
            pc5_files_stop(&ui->files_job);
        }
        if (pc5_files_step(&ui->files_job, 14)) { ui->dirty = 1; break; }
        files_finish(ui);
        break;
    default:
        break;
    }
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
        if (ui->config.finished && ui->config.finished[0]) {
            /* An update the console stopped was finished as PC5 opened: the new PC5 is in place, this one restarts into it. */
            pc5_copy(ui->upgrade.version, sizeof(ui->upgrade.version), ui->config.finished);
            ui->config.finished = NULL;
            atomic_store(&ui->upgrade.step, PC5_UPGRADE_DONE);
            ui->upgrade_back = S_LIBRARY;
            say(ui, "PC5 update finished as it opened: %s", ui->upgrade.version);
            go(ui, S_UPGRADING);
            break;
        }
        /* PC5's INSTALL folder: made when it isn't there; its games offered as PC5 opens. */
        if (ui->app[0] && !ui->install_looked) {
            Pc5UpgradeConfig find = { ui->platform.http, ui->app, ui->root, ui->config.drives, ui->config.user,
                                      ui->platform.log, ui->platform.context };
            char folder[PC5_PATH] = "", other[PC5_PATH] = "";
            ui->install_looked = 1;
            if (!pc5_upgrade_find(&find, folder, sizeof(folder), other, sizeof(other)) &&
                !pc5_format(ui->install_dir, sizeof(ui->install_dir), "%s/" PC5_INSTALL_FOLDER, folder)) {
                pc5_copy(ui->pc5_folder, sizeof(ui->pc5_folder), folder);
                if (!pc5_is_dir(ui->install_dir) && pc5_mkdirs(ui->install_dir)) say(ui, "PC5 problem: can't make %s", ui->install_dir);
                pc5_install_take_from(ui->install_dir);
            } else {
                ui->install_dir[0] = 0;
            }
        }
        if (pc5_recover_step(ui->root, &ui->remove, 14)) { ui->dirty = 1; break; }
        pc5_remove_end(&ui->remove);
        /* The API keys, its file made for the player when there is none. */
        pc5_api_key(ui->root, PC5_STEAMGRIDDB_KEY, ui->griddb_key, sizeof(ui->griddb_key));
        /* The INSTALL folder's games, once anything cut short there is put right. */
        if (ui->install_dir[0] && !ui->install_scanned) {
            ui->install_scanned = 1;
            if (ui->config.opened) {
                const char *only[] = { ui->install_dir, NULL };
                Pc5Source *found = pc5_alloc(sizeof(*found) * PC5_SOURCES);
                ui->install_waiting = found ? pc5_source_scan(only, found, PC5_SOURCES) : 0;
                pc5_free(found);
            }
            say(ui, "PC5 install folder %s games=%d", ui->install_dir, ui->install_waiting);
        }
        /* Tiles that run from PC5's folder get this PC5's program, should it have changed
         * (an update) or moved: a tile's program and its runtime must match. */
        if (ui->config.tiles_root && ui->app[0]) {
            Pc5UpgradeConfig find = { ui->platform.http, ui->app, ui->root, ui->config.drives, ui->config.user,
                                      ui->platform.log, ui->platform.context };
            char folder[PC5_PATH] = "", other[PC5_PATH] = "";
            if (!pc5_upgrade_find(&find, folder, sizeof(folder), other, sizeof(other))) {
                int refreshed = pc5_tile_refresh(ui->config.tiles_root, folder, ui->root, ui->config.user,
                                                 ui->platform.log, ui->platform.context);
                if (refreshed) say(ui, "PC5 tiles refreshed=%d runtime=%s", refreshed, folder);
            }
        }
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
            /* Each time the player opens PC5; after a game (the title restarts), at most once an hour. */
            if (ui->settings.check_updates && (ui->config.opened || now <= 0 ||
                                               now - ui->update.checked >= PC5_UPDATE_EVERY_S || now < ui->update.checked))
                go(ui, S_UPDATE);
            else if (ui->cover_count) go(ui, S_COVERS);
            else to_library(ui);
        }
        break;
    case S_UPDATE:
        if (!ui->drawn) break;
        if (update_check_now(ui, 1)) {
            ui->button = 0;
            go(ui, S_UPGRADE_ASK);
        } else if (ui->cover_count) {
            go(ui, S_COVERS);
        } else {
            to_library(ui);
        }
        break;
    case S_UPGRADE_ASK:
        if (k & (PC5_KEY_LEFT | PC5_KEY_RIGHT)) { ui->button = !ui->button; ui->dirty = 1; }
        if ((k & accept) && !ui->button) {
            upgrade_begin(ui, S_LIBRARY);
        } else if (k & (accept | back)) {
            say(ui, "PC5 update later latest=%s", ui->update.last.version);
            if (ui->cover_count) go(ui, S_COVERS);
            else to_library(ui);
        }
        break;
    case S_UPGRADING: {
        int step = atomic_load(&ui->upgrade.step);
        if (step == PC5_UPGRADE_DONE) {
            if (k & (accept | back)) {
                say(ui, "PC5 update restarting PC5");
                if (ui->platform.restart) {
                    ui->platform.restart(ui->platform.context);
                    /* Still here: the console refused to start PC5 again or to close it. */
                    say(ui, "PC5 problem restart: the console refused");
                    message(ui, 0, "PC5 couldn't restart",
                            "The new version is installed. Close PC5 from the PS button and open it again.", S_LIBRARY);
                } else {
                    to_library(ui);
                }
            }
            break;
        }
        if (step == PC5_UPGRADE_FAILED || step == PC5_UPGRADE_CANCELLED) {
            if (ui->upgrade_back == S_SETTINGS) ui->page = PAGE_SYSTEM;
            if (step == PC5_UPGRADE_CANCELLED)
                message(ui, 0, "The update was stopped", "PC5 is as it was, and what was downloaded is deleted.",
                        ui->upgrade_back);
            else
                message(ui, 0, ui->upgrade.partial ? "PC5's update isn't finished" : "PC5 wasn't updated",
                        ui->upgrade.error, ui->upgrade_back);
            break;
        }
        if ((k & back) && step <= PC5_UPGRADE_UNPACKING && !atomic_load(&ui->upgrade.cancel)) {
            say(ui, "PC5 update stop asked");
            pc5_upgrade_cancel(&ui->upgrade);
            ui->dirty = 1;
        }
        /* The speed, as an install's: measured each second, smoothed. */
        uint64_t done = atomic_load(&ui->upgrade.done);
        if (step != ui->upgrade_step || done < ui->speed_bytes) {
            ui->upgrade_step = step;
            ui->speed_at = ui->now;
            ui->speed_bytes = done;
            ui->speed = 0;
        } else if (ui->now - ui->speed_at >= 1000) {
            double rate = (double)(done - ui->speed_bytes) * 1000.0 / (double)(ui->now - ui->speed_at);
            ui->speed = ui->speed ? ui->speed * 0.6 + rate * 0.4 : rate;
            ui->speed_at = ui->now;
            ui->speed_bytes = done;
        }
        if (ui->now - ui->upgrade_drawn >= 200) { ui->upgrade_drawn = ui->now; ui->dirty = 1; }
        break;
    }
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
        int count = page_rows(&ui->settings, ui->page, offer_update(ui), rows);
        if (ui->menu >= count) ui->menu = count - 1;
        row = rows[ui->menu];
        if (list_move(&ui->menu, count, k)) ui->dirty = 1;
        if (k & (PC5_KEY_LEFT | PC5_KEY_RIGHT)) {
            if (setting_change(ui, row, k & PC5_KEY_LEFT ? -1 : 1)) ui->dirty = 1;
        } else if (k & PC5_KEY_OPTIONS) {
            go(ui, S_LIBRARY);
        } else if (k & back) {
            if (ui->page == PAGE_MAINTENANCE) settings_open(ui, 0, ROW_MAINTENANCE);
            else if (ui->page == PAGE_GAMES) settings_open(ui, 0, ROW_GAMES);
            else if (ui->page == PAGE_OVERLAY) settings_open(ui, 0, ROW_OVERLAY);
            else if (ui->page == PAGE_SYSTEM) settings_open(ui, 0, ROW_SYSTEM);
            else if (ui->page == PAGE_PICTURES) settings_open(ui, PAGE_SYSTEM, system_row(ui, SR_PICTURES));
            else go(ui, S_LIBRARY);
        } else if (k & accept) {
            switch (row) {
            case SR_INSTALL: memset(ui->ticked, 0, sizeof(ui->ticked)); ui->update_for = 0; go(ui, S_SCAN); break;
            case SR_UNINSTALL:
                ui->menu = ui->selected;
                ui->list_top = 0;
                saves_load(ui);
                go(ui, S_UNINSTALL_LIST);
                break;
            case SR_MAINTENANCE: settings_open(ui, PAGE_MAINTENANCE, 0); break;
            case SR_COMPATIBILITY:
                if (!ui->compatibility_qr.size) pc5_qr_make(PC5_COMPATIBILITY, &ui->compatibility_qr);
                say(ui, "PC5 compatibility list qr=%d", ui->compatibility_qr.size);
                go(ui, S_COMPATIBILITY);
                break;
            case SR_GAMES: settings_open(ui, PAGE_GAMES, 0); break;
            case SR_OVERLAY: settings_open(ui, PAGE_OVERLAY, 0); break;
            case SR_SYSTEM: settings_open(ui, PAGE_SYSTEM, 0); break;
            case SR_FILES: files_open(ui, 0); break;
            case SR_PICTURES: settings_open(ui, PAGE_PICTURES, 0); break;
            case SR_ABOUT: go(ui, S_ABOUT); break;
            case SR_CHECK_NOW: go(ui, S_CHECKING); break;
            case SR_UPDATE_NOW: upgrade_begin(ui, S_SETTINGS); break;
            default: if (setting_change(ui, row, 1)) ui->dirty = 1; break;
            }
        }
        break;
    }
    case S_CHECKING:
        if (!ui->drawn) break;
        update_check_now(ui, 0);
        /* on Update now when there is one */
        settings_open(ui, PAGE_SYSTEM, system_row(ui, offer_update(ui) ? SR_UPDATE_NOW : SR_CHECK_NOW));
        break;
    case S_ABOUT:
        if (k & (back | accept)) settings_open(ui, 0, ROW_ABOUT);
        break;
    case S_COMPATIBILITY:
        if (k & (back | accept)) settings_open(ui, PAGE_MAINTENANCE, ROW_COMPATIBILITY);
        break;
    case S_SCAN:
        if (!ui->drawn) break;
        {
            /* The USB drives, then PC5's INSTALL folder (a source's drive is its place here). */
            int n = 0;
            for (int i = 0; !ui->install_only && ui->usb_roots && ui->usb_roots[i] && n < 14; i++)
                ui->scan_roots[n++] = ui->usb_roots[i];
            if (ui->install_dir[0] && !ui->update_for) ui->scan_roots[n++] = ui->install_dir;
            ui->scan_roots[n] = NULL;
            ui->source_count = pc5_source_scan(ui->scan_roots, ui->sources, PC5_SOURCES);
        }
        say(ui, "PC5 usb folders=%d", ui->source_count);
        ui->menu = 0;
        ui->list_top = 0;
        memset(ui->ticked, 0, sizeof(ui->ticked));
        go(ui, S_SOURCES);
        break;
    case S_SOURCES:
        if (list_move(&ui->menu, ui->source_count, k)) ui->dirty = 1;
        if (ui->update_for) {
            /* A newer copy of the game in launch_id: one folder, no ticks. */
            if (k & back) {
                ui->update_for = 0;
                game_menu_at(ui, GR_UPDATE);
                go(ui, S_GAME_MENU);
            } else if (k & PC5_KEY_TRIANGLE) {
                go(ui, S_SCAN);
            } else if ((k & accept) && ui->source_count) {
                go(ui, S_READING);
            }
            break;
        }
        if ((k & back) && ui->install_only) { ui->install_only = 0; go(ui, S_LIBRARY); }
        else if (k & back) settings_open(ui, PAGE_MAINTENANCE, ROW_INSTALL);
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
            if (ui->update_for) {
                ui->button = 1;   /* Update */
                go(ui, S_GAME_UPDATE_ASK);
                break;
            }
            hint_for_folder(ui, &ui->folder, ui->exe, &hint);
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
        if (!search_step(ui, &ui->search)) {
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
        } else if ((k & PC5_KEY_TRIANGLE) && review_on_usb(ui)) {
            /* Played from its USB drive: only its Windows on the console, nothing copied. */
            uint64_t free_bytes = 0;
            if (ui->folder.problem[0]) {
                message(ui, 0, "This folder can't be installed", ui->folder.problem, S_REVIEW);
            } else if (!pc5_free_bytes(ui->root, &free_bytes) && free_bytes < pc5_install_needs_usb(&ui->folder)) {
                char need[32], have[32], body[200];
                pc5_size_text(pc5_install_needs_usb(&ui->folder), need, sizeof(need));
                pc5_size_text(free_bytes, have, sizeof(have));
                snprintf(body, sizeof(body), "Its Windows needs %s and the console has %s free.", need, have);
                message(ui, 0, "Not enough space", body, S_REVIEW);
            } else {
                ui->use_saves[0] = 0;
                begin_install_as(ui, 1);
            }
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
            } else if (pc5_saves_find(ui->root, ui->folder.name, ui->exe >= 0 ? ui->folder.exes[ui->exe].path : NULL,
                                      ui->use_saves, sizeof(ui->use_saves)) > 0) {
                ui->button = 1;   /* Put them back */
                go(ui, S_SAVES_ASK);
            } else {
                ui->use_saves[0] = 0;
                begin_install_as(ui, 0);
            }
        }
        break;
    }
    case S_SAVES_ASK:
        if (k & (PC5_KEY_LEFT | PC5_KEY_RIGHT)) { ui->button = !ui->button; ui->dirty = 1; }
        if (k & back) {
            ui->use_saves[0] = 0;
            go(ui, S_REVIEW);
        } else if ((k & accept) && ui->now - ui->entered > 300) {
            if (!ui->button) ui->use_saves[0] = 0;   /* Start fresh: they stay kept */
            begin_install_as(ui, 0);
        }
        break;
    case S_GAME_UPDATE_ASK:
        if (k & (PC5_KEY_LEFT | PC5_KEY_RIGHT)) { ui->button = !ui->button; ui->dirty = 1; }
        if (k & back) {
            go(ui, S_SOURCES);
        } else if ((k & accept) && ui->now - ui->entered > 300) {
            uint64_t free_bytes = 0;
            if (!ui->button) {
                go(ui, S_SOURCES);
            } else if (!pc5_free_bytes(ui->root, &free_bytes) && free_bytes < pc5_update_needs(&ui->folder)) {
                char need[32], have[32], body[200];
                pc5_size_text(pc5_update_needs(&ui->folder), need, sizeof(need));
                pc5_size_text(free_bytes, have, sizeof(have));
                snprintf(body, sizeof(body), "The update needs %s and the console has %s free.", need, have);
                message(ui, 0, "Not enough space", body, S_SOURCES);
            } else {
                begin_update(ui);
            }
        }
        break;
    case S_PROGRAMS:
        if (list_move(&ui->menu, ui->folder.exe_count, k)) ui->dirty = 1;
        if (k & back) {
            if (ui->program_for) game_menu_at(ui, GR_PROGRAM);
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
        int count = game_rows(&ui->controls, ui_game_kind(ui), ui->game_page, rows);
        if (index < 0) { go(ui, S_LIBRARY); break; }
        const Pc5Game *game = &ui->library.games[index];
        if (ui->menu >= count) ui->menu = count - 1;
        if (list_move(&ui->menu, count, k)) ui->dirty = 1;
        GameRow row = rows[ui->menu];
        if (k & PC5_KEY_TRIANGLE) go(ui, S_LIBRARY);
        else if (k & back) {
            /* A group's page back to the first, on the group's row. */
            if (ui->game_page == GP_MAIN) go(ui, S_LIBRARY);
            else {
                game_menu_at(ui, game_page_rows[ui->game_page]);
                ui->dirty = 1;
            }
        } else if ((k & accept) && (row == GR_GRAPHICS || row == GR_APPEARANCE || row == GR_MANAGE)) {
            ui->game_page = row == GR_GRAPHICS ? GP_GRAPHICS : row == GR_APPEARANCE ? GP_APPEARANCE : GP_MANAGE;
            ui->menu = 0;
            ui->dirty = 1;
        } else if ((k & (PC5_KEY_LEFT | PC5_KEY_RIGHT | accept)) && row == GR_CONTROLLER) {
            int step = (k & PC5_KEY_LEFT) ? -1 : 1;
            ui->controls.mode = (Pc5PadMode)((ui->controls.mode + PC5_PAD_MODES + step) % PC5_PAD_MODES);
            controls_changed(ui);
            ui->dirty = 1;
        } else if ((k & accept) && row == GR_GRAPHICS_API) {
            /* A window with each choice and all it does: a row has room for only one. */
            Pc5Graphics choices[PC5_GRAPHICS_COUNT];
            int count = pc5_graphics_choices(ui_game_api(ui), choices);
            ui->api_choice = 0;
            for (int i = 0; i < count; i++)
                if (choices[i] == ui->controls.graphics) ui->api_choice = i;
            go(ui, S_GRAPHICS_API);
        } else if ((k & (PC5_KEY_LEFT | PC5_KEY_RIGHT | accept)) && row == GR_FRAMEGEN) {
            ui->controls.frame_generation = !ui->controls.frame_generation;
            controls_changed(ui);
            ui->dirty = 1;
        } else if ((k & (PC5_KEY_LEFT | PC5_KEY_RIGHT | accept)) && row == GR_RESOLUTION) {
            int step = (k & PC5_KEY_LEFT) ? -1 : 1;
            ui->controls.resolution = (ui->controls.resolution + 3 + step) % 3;
            controls_changed(ui);
            ui->dirty = 1;
        } else if ((k & (PC5_KEY_LEFT | PC5_KEY_RIGHT | accept)) && row == GR_ASPECT) {
            ui->controls.stretch = !ui->controls.stretch;
            controls_changed(ui);
            ui->dirty = 1;
        } else if ((k & (PC5_KEY_LEFT | PC5_KEY_RIGHT | accept)) && row == GR_MEMORY) {
            ui->controls.memory_4gb = !ui->controls.memory_4gb;
            controls_changed(ui);
            ui->dirty = 1;
        } else if ((k & (PC5_KEY_LEFT | PC5_KEY_RIGHT | accept)) && row == GR_VIDEO_MEMORY) {
            ui->controls.video_memory_full = !ui->controls.video_memory_full;
            controls_changed(ui);
            ui->dirty = 1;
        } else if ((k & (PC5_KEY_LEFT | PC5_KEY_RIGHT | accept)) && row == GR_CPU_SHARING) {
            ui->controls.cpu_sharing = !ui->controls.cpu_sharing;
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
            } else if (row == GR_COVER || row == GR_BACKGROUND || row == GR_ICON) {
                picture_open(ui, game, row == GR_COVER ? PICK_COVER : row == GR_ICON ? PICK_ICON : PICK_BACKGROUND);
            } else if (row == GR_PROGRAM) {
                if (!game->pc5) message(ui, 0, "PC5 didn't install this game",
                                        "Its program is set in its prospero-win profile.", S_GAME_MENU);
                else go(ui, S_GAME_READING);
            } else if (row == GR_FILES) {
                files_open(ui, 1);
            } else if (row == GR_UPDATE) {
                ui->update_for = 1;
                go(ui, S_SCAN);
            } else if (row == GR_UNINSTALL) {
                ui->menu = index;
                ui->button = 0;   /* Cancel first */
                ui->uninstall_back = S_GAME_MENU;
                go(ui, S_UNINSTALL_CONFIRM);
            }
        }
        break;
    }
    case S_PICTURE_SEARCH: {
        int index = pc5_library_find(&ui->library, ui->launch_id);
        if (index < 0 || !ui->pick) { picture_close(ui); go(ui, S_LIBRARY); break; }
        if ((k & back) && ui->now - ui->entered > 300) { picture_leave(ui); break; }
        if (!ui->drawn) break;
        if (!search_step(ui, ui->pick)) pick_searched(ui);
        ui->dirty = 1;
        break;
    }
    case S_PICTURE: {
        int index = pc5_library_find(&ui->library, ui->launch_id);
        if (index < 0 || !ui->pick) { picture_close(ui); go(ui, S_LIBRARY); break; }
        const Pc5Game *game = &ui->library.games[index];
        if (ui->pick_fetching) {
            if (ui->drawn) pick_fetch(ui, game, ui->pick_step);
            break;
        }
        if ((k & (PC5_KEY_L1 | PC5_KEY_R1 | PC5_KEY_LEFT | PC5_KEY_RIGHT)) && ui->pick_count > 1) {
            ui->pick_step = (k & (PC5_KEY_R1 | PC5_KEY_RIGHT)) ? 1 : -1;
            ui->pick_at = (ui->pick_at + ui->pick_step + ui->pick_count) % ui->pick_count;
            ui->pick_fetching = 1;
            ui->drawn = 0;
            ui->dirty = 1;
        } else if ((k & PC5_KEY_TRIANGLE) && ui->platform.keyboard_open) {
            int rc = ui->platform.keyboard_open(ui->platform.context,
                                                ui->pick_kind == PICK_COVER ? "Search for covers by name"
                                                : ui->pick_kind == PICK_ICON ? "Search for tile pictures by name"
                                                                             : "Search for backgrounds by name",
                                                ui->pick_query[0] ? ui->pick_query : game->title);
            say(ui, "PC5 keyboard open rc=%d", rc);
            if (rc >= 0) go(ui, S_PICTURE_TYPING);
            else message(ui, 0, "The keyboard didn't open", "The console's on-screen keyboard can't be used just now.", S_PICTURE);
        } else if ((k & back) && ui->now - ui->entered > 300) {
            picture_leave(ui);
        } else if ((k & accept) && ui->now - ui->entered > 300) {
            pick_use(ui, game);
        }
        break;
    }
    case S_PICTURE_TYPING: {
        int state = ui->platform.keyboard_state ? ui->platform.keyboard_state(ui->platform.context) : -1;
        if (state == 1) break;   /* still up */
        char typed[PC5_NAME] = "";
        int rc = ui->platform.keyboard_close ? ui->platform.keyboard_close(ui->platform.context, typed, sizeof(typed)) : -1;
        char *start = typed;
        while (*start == ' ' || *start == '\t') start++;
        for (size_t n = strlen(start); n && (start[n - 1] == ' ' || start[n - 1] == '\t'); n--) start[n - 1] = 0;
        int index = pc5_library_find(&ui->library, ui->launch_id);
        say(ui, "PC5 keyboard closed rc=%d typed=%zu", rc, strlen(start));
        if (index < 0 || !ui->pick) { picture_close(ui); go(ui, S_LIBRARY); break; }
        if (rc == 1 && start[0]) {
            pc5_copy(ui->pick_query, sizeof(ui->pick_query), start);
            pick_search(ui, &ui->library.games[index]);
        } else {
            go(ui, S_PICTURE);   /* cancelled: as it was */
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
            game_menu_at(ui, GR_BUTTONS);
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
            } else if (row.kind == PR_SLOW_PERCENT) {
                ui->controls.slow_percent = ui->controls.slow_percent % PC5_SLOW_MAX + PC5_SLOW_STEP;
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
    case S_GRAPHICS_API: {
        Pc5Graphics choices[PC5_GRAPHICS_COUNT];
        int count = pc5_graphics_choices(ui_game_api(ui), choices);
        if (list_move(&ui->api_choice, count, k)) ui->dirty = 1;
        if (k & back) go(ui, S_GAME_MENU);
        else if ((k & accept) && ui->api_choice < count) {
            ui->controls.graphics = choices[ui->api_choice];
            controls_changed(ui);
            go(ui, S_GAME_MENU);
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
        /* The games, then the saves kept when games were uninstalled. */
        if (list_move(&ui->menu, ui->library.count + ui->saves_count, k)) ui->dirty = 1;
        if (k & back) settings_open(ui, PAGE_MAINTENANCE, ROW_UNINSTALL);
        else if ((k & accept) && ui->menu < ui->library.count) {
            ui->button = 0;   /* Cancel first */
            ui->uninstall_back = S_UNINSTALL_LIST;
            go(ui, S_UNINSTALL_CONFIRM);
        } else if ((k & accept) && ui->menu < ui->library.count + ui->saves_count) {
            ui->button = 0;
            go(ui, S_SAVES_CONFIRM);
        }
        if (covers_near(ui)) ui->dirty = 1;
        break;
    case S_SAVES_CONFIRM:
        if (k & (PC5_KEY_LEFT | PC5_KEY_RIGHT)) { ui->button = !ui->button; ui->dirty = 1; }
        if (k & back) {
            go(ui, S_UNINSTALL_LIST);
        } else if ((k & accept) && ui->now - ui->entered > 400) {
            int at = ui->menu - ui->library.count;
            if (!ui->button || at < 0 || at >= ui->saves_count) {
                go(ui, S_UNINSTALL_LIST);
            } else {
                const Pc5Saves *saves = &ui->saves[at];
                snprintf(ui->removing_title, sizeof(ui->removing_title), "%s", saves->title);
                say(ui, "PC5 saves delete name=%s", saves->name);
                ui->removing_kind = 2;
                ui->removing_tiles = 0;
                pc5_saves_delete_begin(&ui->remove, ui->root, saves->name);
                go(ui, S_REMOVING);
            }
        }
        break;
    case S_UNINSTALL_CONFIRM: {
        /* Cancel, Uninstall; or Cancel, Keep saves, Delete all for a game whose saves can be kept. */
        int keep = pc5_remove_can_keep(&ui->library.games[ui->menu], &ui->library), buttons = keep ? 3 : 2;
        if ((k & PC5_KEY_LEFT) && ui->button > 0) { ui->button--; ui->dirty = 1; }
        if ((k & PC5_KEY_RIGHT) && ui->button < buttons - 1) { ui->button++; ui->dirty = 1; }
        if (k & back) {
            if (ui->uninstall_back == S_GAME_MENU) game_menu_at(ui, GR_UNINSTALL);
            go(ui, ui->uninstall_back);
        } else if ((k & accept) && ui->now - ui->entered > 400) {
            if (!ui->button) {
                if (ui->uninstall_back == S_GAME_MENU) game_menu_at(ui, GR_UNINSTALL);
                go(ui, ui->uninstall_back);
            } else {
                const Pc5Game *game = &ui->library.games[ui->menu];
                pc5_copy(ui->removing_title, sizeof(ui->removing_title), game->title);
                char id[PC5_ID], folder[PC5_PATH];
                ui->removing_kind = keep && ui->button == 1;
                pc5_copy(id, sizeof(id), game->id);
                say(ui, "PC5 uninstall id=%s prefix=%s keep_saves=%d", game->id, game->prefix, ui->removing_kind);
                atomic_store(&ui->backgrounds.stop, 1);   /* not to save into the game as it goes */
                int begun = ui->removing_kind ? pc5_remove_keep_saves(&ui->remove, ui->root, game, &ui->library)
                                              : pc5_remove_begin(&ui->remove, ui->root, game, &ui->library);
                /* It couldn't begin and the game's folder is where it was: nothing changed. */
                if (begun && !pc5_format(folder, sizeof(folder), "%s/prefixes/%s", ui->root, game->prefix) &&
                    game->prefix[0] && pc5_is_dir(folder) && strcmp(game->prefix, "default")) {
                    char title[200], body[PC5_ERROR + 64];
                    snprintf(title, sizeof(title), "%s wasn't uninstalled", game->title);
                    snprintf(body, sizeof(body), "%s Nothing was changed.", ui->remove.error);
                    say(ui, "PC5 problem uninstall %s: %s", game->title, ui->remove.error);
                    pc5_remove_end(&ui->remove);
                    ui->backgrounds_for = -1;   /* the worker, stopped for this, looks again */
                    if (ui->uninstall_back == S_GAME_MENU) game_menu_at(ui, GR_UNINSTALL);
                    message(ui, 0, title, body, ui->uninstall_back);
                    break;
                }
                /* Its home screen tile would start nothing now. */
                ui->removing_tiles = ui->config.tiles_root ? pc5_home_remove(ui->config.tiles_root, id) : 0;
                if (ui->removing_tiles) say(ui, "PC5 home tile removed id=%s count=%d", id, ui->removing_tiles);
                covers_clear(ui);
                go(ui, S_REMOVING);
            }
        }
        break;
    }
    case S_REMOVING:
        if (!ui->remove.failed && pc5_remove_step(&ui->remove, 14)) { ui->dirty = 1; break; }
        {
            char title[200], body[400];
            int failed = ui->remove.failed;
            const char *tile = ui->removing_tiles > 0 ? " Its home screen tile's folder was deleted: if the tile is still on "
                                                        "the home screen, delete it there." : "";
            if (ui->removing_kind == 2) {
                snprintf(title, sizeof(title), failed ? "The saves of %s weren't fully deleted" : "The saves of %s are deleted",
                         ui->removing_title);
                if (failed) snprintf(body, sizeof(body), "%s PC5 finishes deleting them the next time it starts.", ui->remove.error);
                else snprintf(body, sizeof(body), "Its saves and settings, kept when it was uninstalled, are gone.");
            } else {
                snprintf(title, sizeof(title), failed ? "%s wasn't fully uninstalled" : "%s is uninstalled", ui->removing_title);
                if (failed) snprintf(body, sizeof(body), "%s PC5 finishes it the next time it starts.", ui->remove.error);
                else if (ui->removing_kind == 1)
                    snprintf(body, sizeof(body), "Its saves and settings are kept: installing it again puts them back. "
                             "Settings \xe2\x80\xba Game maintenance \xe2\x80\xba Uninstall game deletes them.%s", tile);
                else snprintf(body, sizeof(body), "The game, its saves and its settings were deleted.%s", tile);
            }
            if (failed && ui->removing_kind == 2) pc5_copy(body, sizeof(body), ui->remove.error);
            if (failed) say(ui, "PC5 problem uninstall %s: %s", ui->removing_title, ui->remove.error);
            say(ui, "PC5 uninstall done failed=%d removed=%u", failed, (unsigned)ui->remove.removed);
            pc5_remove_end(&ui->remove);
            library_reload(ui, NULL);
            if (ui->selected >= ui->library.count) ui->selected = ui->library.count ? ui->library.count - 1 : 0;
            message(ui, !failed, title, body, S_LIBRARY);
        }
        break;
    case S_MESSAGE:
        if (ui->message_report && (k & PC5_KEY_TRIANGLE)) {
            report_begin(ui);
            break;
        }
        if (k & (accept | back)) {
            if (ui->back_to == S_LIBRARY && ui->issue_pending) issue_message(ui);
            else go(ui, ui->back_to);
        }
        break;
    case S_REPORT:
        if (k & (accept | back)) go(ui, S_MESSAGE);
        break;
    case S_FILES:
    case S_FILES_MENU:
    case S_FILES_TYPING:
    case S_FILES_CONFLICT:
    case S_FILES_DELETE:
    case S_FILES_WORKING:
        files_update(ui, k);
        break;
    case S_INSTALL_ASK:
        /* Later, Install */
        if (k & (PC5_KEY_LEFT | PC5_KEY_RIGHT)) { ui->button = !ui->button; ui->dirty = 1; }
        if (k & back) { ui->install_waiting = 0; go(ui, S_LIBRARY); }
        else if ((k & accept) && ui->now - ui->entered > 300) {
            say(ui, "PC5 install folder choice=%s games=%d", ui->button ? "install" : "later", ui->install_waiting);
            ui->install_waiting = 0;
            if (ui->button) {
                memset(ui->ticked, 0, sizeof(ui->ticked));
                ui->update_for = 0;
                ui->install_only = 1;
                go(ui, S_SCAN);
            } else {
                go(ui, S_LIBRARY);
            }
        }
        break;
    case S_TILE_ART:
        if (!ui->drawn) break;
        {
            int index = pc5_library_find(&ui->library, ui->launch_id);
            if (index >= 0) tile_art_refresh(ui, &ui->library.games[index]);
        }
        go(ui, S_GAME_MENU);   /* on the row picture_leave chose */
        break;
    case S_REPORT_CHECK:
        if (!ui->drawn) break;
        update_check_now(ui, 2);
        say(ui, "PC5 report update check newer=%d", newer_known(ui));
        if (newer_known(ui)) {
            ui->button = 0;
            go(ui, S_REPORT_UPDATE);
        } else {
            report_open(ui);
        }
        break;
    case S_REPORT_UPDATE:
        /* Update now, and Report anyway. Update now updates PC5 here; where it can't (this release
         * has no zip PC5 can install), it opens Settings › System, whose Check now asks again. */
        if (k & (PC5_KEY_LEFT | PC5_KEY_RIGHT)) { ui->button = !ui->button; ui->dirty = 1; }
        if (k & back) go(ui, S_MESSAGE);
        else if ((k & accept) && ui->now - ui->entered > 300) {
            say(ui, "PC5 report update choice=%s", ui->button ? "report" : offer_update(ui) ? "update" : "system");
            if (ui->button) report_open(ui);
            else if (offer_update(ui)) upgrade_begin(ui, S_MESSAGE);
            else settings_open(ui, PAGE_SYSTEM, system_row(ui, SR_CHECK_NOW));
        }
        break;
    case S_LAUNCH:
        if (!ui->drawn) break;
        {
            int index = pc5_library_find(&ui->library, ui->launch_id);
            char error[PC5_ERROR] = "", title[200];
            /* A .NET game started the first time: Wine Mono's runtime is copied to the
             * console first, a step each frame, under "Starting". */
            if (ui->runtime_state == 0) {
                char program[PC5_PATH];
                ui->runtime_state = 2;
                if (index >= 0 && ui->library.games[index].pc5 && ui->app[0] &&
                    !pc5_game_program(ui->root, ui->launch_id, program, sizeof(program))) {
                    int begun = pc5_runtime_begin(&ui->runtime, ui->root, ui->app, program);
                    if (begun < 0) {
                        snprintf(title, sizeof(title), "%s can't start", ui->library.games[index].title);
                        say(ui, "PC5 problem play %s: %s", ui->launch_id, ui->runtime.error);
                        message(ui, 0, title, ui->runtime.error, S_LIBRARY);
                        break;
                    }
                    if (begun > 0) {
                        ui->runtime_state = 1;
                        say(ui, "PC5 dotnet runtime copy to=%s entries=%u", ui->runtime.to, (unsigned)ui->runtime.total);
                    }
                }
            }
            if (ui->runtime_state == 1) {
                int step = pc5_runtime_step(&ui->runtime, 30);
                ui->dirty = 1;
                if (!step) break;
                if (step < 0) {
                    pc5_copy(error, sizeof(error), ui->runtime.error);
                    snprintf(title, sizeof(title), "%s can't start", index >= 0 ? ui->library.games[index].title : "The game");
                    say(ui, "PC5 problem play %s: %s", ui->launch_id, error);
                    message(ui, 0, title, error, S_LIBRARY);   /* ends the copy; the next start goes on with it */
                    break;
                }
                say(ui, "PC5 dotnet runtime copied entries=%u", (unsigned)ui->runtime.done);
                pc5_runtime_end(&ui->runtime);
                ui->runtime_state = 2;
            }
            if (index >= 0 && ui->library.games[index].pc5 &&
                pc5_profile_refresh(ui->root, ui->launch_id, error, sizeof(error))) {
                snprintf(title, sizeof(title), "%s can't start", ui->library.games[index].title);
                say(ui, "PC5 problem play %s: %s", ui->launch_id, error);
                message(ui, 0, title, error, S_LIBRARY);
                break;
            }
            remember_played(ui, ui->launch_id);
            if (index >= 0) {
                /* What it draws with, as PC5 saw it, and the Graphics API chosen for it. */
                Pc5Controls chosen;
                const Pc5Game *game = &ui->library.games[index];
                pc5_controls_load(ui->root, ui->launch_id, &chosen);
                say(ui, "PC5 play id=%s api=%s graphics=%s bits=%d", ui->launch_id, pc5_api_name(game->api),
                    pc5_graphics_name(chosen.graphics, game->api), game->bits);
            } else {
                say(ui, "PC5 play id=%s", ui->launch_id);
            }
            if (index >= 0 && ui->library.games[index].usb) {
                char where[PC5_PATH];
                if (!pc5_game_folder(ui->root, ui->launch_id, where, sizeof(where))) say(ui, "PC5 play from usb dir=%s", where);
            }
            if (ui->platform.play) ui->platform.play(ui->platform.context, ui->launch_id);
            snprintf(title, sizeof(title), "%s didn't start", index >= 0 ? ui->library.games[index].title : "The game");
            say(ui, "PC5 problem play %s: the console refused to start it", ui->launch_id);
            message(ui, 0, title, "The console refused to start it. Try again, or restart PC5.", S_LIBRARY);
        }
        break;
    }

    /* The chosen game's background, behind the library and what opens over it. */
    if (ui->screen != S_START && ui->screen != S_COVERS && ui->screen != S_UPDATE && ui->screen != S_UPGRADE_ASK) {
        if (ui->screen == S_LIBRARY) backgrounds_refresh(ui);
        background_follow(ui);
    }
    /* The fade, and one frame after it, so its last frame is the whole new picture. */
    if (ui->now - ui->bg_fade_at < BG_FADE_MS) ui->dirty = 1, ui->bg_fading = 1;
    else if (ui->bg_fading) ui->dirty = 1, ui->bg_fading = 0;
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
        ui->screen == S_CHECKING || ui->screen == S_GAME_READING || ui->screen == S_QUEUE_READ || ui->fetching ||
        (ui->screen == S_UPGRADING && atomic_load(&ui->upgrade.step) != PC5_UPGRADE_DONE &&
         atomic_load(&ui->upgrade.step) != PC5_UPGRADE_DOWNLOADING && atomic_load(&ui->upgrade.step) != PC5_UPGRADE_UNPACKING))
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
/* ---- drawing on several threads -------------------------------------------------------------
 * The library's backdrop and covers are most of a frame's work: they are drawn in BANDS parts
 * at once, each on a thread of its own (this one doing the first), each part its own share of
 * the rows (the backdrop) or of the columns (the covers, through the canvas's clip). */
enum { BANDS = 4 };
typedef void (*BandFn)(Pc5Ui *ui, Pc5Canvas *c, int index, void *arg);
typedef struct Band { Pc5Ui *ui; Pc5Canvas canvas; int index; BandFn fn; void *arg; } Band;

static void *band_main(void *p)
{
    Band *band = p;
    band->fn(band->ui, &band->canvas, band->index, band->arg);
    return NULL;
}

/* fn for each band; a band whose thread can't start is drawn here once the others are done. */
static void in_bands(Pc5Ui *ui, Pc5Canvas *c, BandFn fn, void *arg)
{
    Band bands[BANDS];
    pthread_t threads[BANDS];
    int started[BANDS] = { 0 };

    for (int i = 0; i < BANDS; i++) {
        bands[i] = (Band){ ui, *c, i, fn, arg };
        bands[i].canvas.clip_x0 = c->w * i / BANDS;
        bands[i].canvas.clip_x1 = c->w * (i + 1) / BANDS;
    }
    for (int i = 1; i < BANDS; i++) started[i] = !pthread_create(&threads[i], NULL, band_main, &bands[i]);
    band_main(&bands[0]);
    for (int i = 1; i < BANDS; i++) {
        if (started[i]) pthread_join(threads[i], NULL);
        else band_main(&bands[i]);
    }
}

/* What the backdrop's rows are made of this frame. */
typedef struct BackdropPlan {
    const uint32_t *from, *to;   /* the picture fading out and the one fading in (the same when none fades) */
    uint32_t a, b;               /* their shares, 0-256 */
    int compose;                 /* PC5's own backdrop made again in the glow's new colour */
    uint32_t add[256];
} BackdropPlan;

static void backdrop_rows(Pc5Ui *ui, Pc5Canvas *c, int index, void *arg)
{
    const BackdropPlan *k = arg;
    int y0 = H * index / BANDS, y1 = H * (index + 1) / BANDS;

    if (k->compose)
        for (size_t i = (size_t)y0 * W; i < (size_t)y1 * W; i++) {
            /* The backdrop is dark (every channel under 32) and the glow at most 255 * 0.62 + 64: no
             * channel carries into the next. */
            ui->composed[i] = ui->backdrop[i] + k->add[ui->falloff[i]];
        }
    for (int y = y0; y < y1; y++) {
        const uint32_t *p = k->from + (size_t)y * W, *q = k->to + (size_t)y * W;
        uint32_t *o = c->px + (size_t)y * (size_t)c->stride;
        if (p == q) {
            memcpy(o, q, (size_t)W * 4);
            continue;
        }
        for (int x = 0; x < W; x++) {
            uint32_t u = p[x], v = q[x];
            uint32_t rb = (((u & 0xff00ffu) * k->b + (v & 0xff00ffu) * k->a) >> 8) & 0xff00ffu;
            uint32_t g = (((u & 0x00ff00u) * k->b + (v & 0x00ff00u) * k->a) >> 8) & 0x00ff00u;
            o[x] = 0xff000000u | rb | g;
        }
    }
}

/* The background with the glow in its current colour (PC5's own backdrop, made again only
 * when the colour moves and it shows), or the chosen game's background faded in from what was
 * there. */
static void backdrop(Pc5Ui *ui, Pc5Canvas *c)
{
    uint32_t glow = pc5_rgba((int)ui->glow[0], (int)ui->glow[1], (int)ui->glow[2], 255);
    BackdropPlan plan;

    if (!ui->backdrop || !ui->composed || c->w != W || c->h != H) { pc5_clear(c, 0xff07080au); return; }
    uint64_t since = ui->now - ui->bg_fade_at;
    int fading = since < BG_FADE_MS;
    plan.to = ui->bg_has ? ui->bg : ui->composed;
    plan.from = fading && ui->bg_old_has ? ui->bg_old : fading ? ui->composed : plan.to;
    plan.a = fading ? (uint32_t)(since * 256 / BG_FADE_MS) : 256;
    plan.b = 256 - plan.a;
    plan.compose = (plan.to == ui->composed || plan.from == ui->composed) && (glow != ui->composed_glow || !ui->composed_ready);
    if (plan.compose) {
        for (int k = 0; k < 256; k++) {
            int r = (int)((glow >> 16) & 255) * k / 255, g = (int)((glow >> 8) & 255) * k / 255, b = (int)(glow & 255) * k / 255;
            plan.add[k] = (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
        }
    }
    in_bands(ui, c, backdrop_rows, &plan);
    if (plan.compose) {
        ui->composed_glow = glow;
        ui->composed_ready = 1;
    }
}

/* A colour made bright and full: a cover's main colour as the light around it. */
static void hsv_of(uint32_t color, float *h, float *s, float *v)
{
    float r = (float)((color >> 16) & 255) / 255, g = (float)((color >> 8) & 255) / 255, b = (float)(color & 255) / 255;
    float hi = fmaxf(r, fmaxf(g, b)), lo = fminf(r, fminf(g, b)), d = hi - lo;
    *v = hi;
    *s = hi > 0 ? d / hi : 0;
    if (d <= 0) *h = 0;
    else if (hi == r) *h = 60 * fmodf((g - b) / d + 6, 6);
    else if (hi == g) *h = 60 * ((b - r) / d + 2);
    else *h = 60 * ((r - g) / d + 4);
}

static uint32_t from_hsv(float h, float s, float v, int alpha)
{
    h = fmodf(h + 360, 360);
    float c = v * s, x = c * (1 - fabsf(fmodf(h / 60, 2) - 1)), m = v - c, r = 0, g = 0, b = 0;
    if (h < 60) r = c, g = x;
    else if (h < 120) r = x, g = c;
    else if (h < 180) g = c, b = x;
    else if (h < 240) g = x, b = c;
    else if (h < 300) r = x, b = c;
    else r = c, b = x;
    return pc5_rgba((int)((r + m) * 255 + 0.5f), (int)((g + m) * 255 + 0.5f), (int)((b + m) * 255 + 0.5f), alpha);
}

/* The chosen cover's neon: its colour, bright and full (a grey cover's a soft blue), and the
 * same turned round the colour wheel, for the frame's two ends. */
static void neon_of(uint32_t tint, uint32_t *from, uint32_t *to, int alpha)
{
    float h, s, v;
    hsv_of(tint, &h, &s, &v);
    if (s < 0.15f) h = 222, s = 0.35f;
    s = s < 0.55f ? 0.55f + (s - 0.15f) * 0.5f : s;
    if (s > 0.9f) s = 0.9f;
    /* the other end turned towards blue and violet, or, from there, on to magenta */
    float d = fmodf(250 - h + 540, 360) - 180;
    *from = from_hsv(h, s, 1.0f, alpha);
    *to = from_hsv(fabsf(d) < 40 ? h + 60 : h + (d > 0 ? 55 : -55), s, 1.0f, alpha);
}

/* Where a cover stands d places from the middle, on the floor: its left and right edges and
 * their heights (the ones beside the middle turned a little towards it), how bright it is and
 * how much of it the floor shows. */
static void slot(float d, float *left, float *right, float *h_left, float *h_right, float *bright, float *mirror)
{
    static const float ls[] = { 744, 1209, 1452, 1677, 1881, 2090, 2300 }, rs[] = { 1176, 1443, 1671, 1875, 2076, 2280, 2480 },
                       hls[] = { 630, 468, 432, 399, 372, 350, 330 }, hrs[] = { 630, 450, 415, 385, 358, 338, 320 },
                       bs[] = { 1, 0.88f, 0.74f, 0.60f, 0.48f, 0.40f, 0.35f },
                       ms[] = { 0.68f, 0.55f, 0.46f, 0.38f, 0.30f, 0.24f, 0.20f };
    float a = fabsf(d), t;
    int i = (int)a;
    if (i >= 6) { i = 5; t = 1; }
    else t = a - (float)i;
    float l = ls[i] + (ls[i + 1] - ls[i]) * t, r = rs[i] + (rs[i + 1] - rs[i]) * t;
    float hl = hls[i] + (hls[i + 1] - hls[i]) * t, hr = hrs[i] + (hrs[i + 1] - hrs[i]) * t;
    *bright = bs[i] + (bs[i + 1] - bs[i]) * t;
    *mirror = ms[i] + (ms[i + 1] - ms[i]) * t;
    if (d >= 0) { *left = l; *right = r; *h_left = hl; *h_right = hr; }
    else { *left = W - r; *right = W - l; *h_left = hr; *h_right = hl; }
}

static void carousel(Pc5Ui *ui, Pc5Canvas *c)
{
    int order[16], n = 0;
    int first = (int)floorf(ui->scroll) - 6, last = (int)ceilf(ui->scroll) + 6;
    uint32_t tint = pc5_rgba((int)ui->glow[0], (int)ui->glow[1], (int)ui->glow[2], 255), from, to;

    neon_of(tint, &from, &to, 255);
    for (int i = first; i <= last; i++)
        if (i >= 0 && i < ui->library.count && n < 16) order[n++] = i;
    /* farthest first */
    for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++)
            if (fabsf(order[b] - ui->scroll) > fabsf(order[a] - ui->scroll)) { int t = order[a]; order[a] = order[b]; order[b] = t; }
    for (int k = 0; k < n; k++) {
        int i = order[k];
        float d = (float)i - ui->scroll, left, right, hl, hr, bright, mirror;
        slot(d, &left, &right, &hl, &hr, &bright, &mirror);
        if (right <= 0 || left >= W) continue;
        float w = right - left, h = hl > hr ? hl : hr, radius = fabsf(d) < 1 ? 20 - 2 * fabsf(d) : 18;
        const Cover *cover = &ui->covers[i];
        float near = 1 - fabsf(d) * 2;   /* the chosen one's light, gone half way to the next */
        if (near > 0) {
            /* its glow, and its light on the floor */
            pc5_shadow(c, left, FLOOR_Y - h, w, h, radius, 34, (from & 0xffffffu) | (uint32_t)(150 * near) << 24);
            pc5_shadow(c, left, FLOOR_Y - h, w, h, radius, 96, (to & 0xffffffu) | (uint32_t)(70 * near) << 24);
            pc5_shadow(c, left + w * 0.12f, FLOOR_Y - 2, w * 0.76f, 6, 3, 30, (from & 0xffffffu) | (uint32_t)(150 * near) << 24);
        }
        if (cover->state && cover->picture.levels) {
            pc5_draw_card(c, &cover->picture, left, FLOOR_Y, w, hl, hr, radius, (int)(bright * 256), 256, (int)(mirror * 256),
                          0.5f, near > 0 ? 52 : 40);
        } else {
            /* not loaded yet: a plain dark card */
            pc5_round_rect(c, left, FLOOR_Y - h, w, h, radius, pc5_rgba(28, 32, 40, (int)(255 * bright)));
        }
        if (near > 0) {
            pc5_round_frame_graded(c, left - 6, FLOOR_Y - h - 6, w + 12, h + 12, radius + 6, 6,
                                   (from & 0xffffffu) | (uint32_t)(255 * near) << 24, (to & 0xffffffu) | (uint32_t)(255 * near) << 24);
        } else {
            /* the edge turned towards the middle catches the light */
            float x = d < 0 ? right : left, edge = d < 0 ? hr : hl;
            pc5_line(c, x, FLOOR_Y - edge + radius * 0.6f, x, FLOOR_Y - radius * 0.6f, 1.5f, (uint32_t)(64 * bright) << 24 | 0xffffffu);
        }
    }
}

static void carousel_band(Pc5Ui *ui, Pc5Canvas *c, int index, void *arg)
{
    (void)index;
    (void)arg;
    carousel(ui, c);
}

/* text in capitals with letter spacing (ASCII letters only; others as they are), its centre
 * at x; its width. */
static int spaced_caps(Pc5Ui *ui, Pc5Canvas *c, Pc5Face face, int x, int y, uint32_t color, const char *text, int spacing,
                       int draw)
{
    char one[8], caps[256];
    int width = 0, count = 0;
    size_t n = 0;

    for (const char *p = text; *p && n + 1 < sizeof(caps); p++) caps[n++] = (char)(*p >= 'a' && *p <= 'z' ? *p - 32 : *p);
    caps[n] = 0;
    for (size_t at = 0; caps[at];) {
        size_t len = 1;
        while (len < 4 && caps[at + len] && ((unsigned char)caps[at + len] & 0xc0) == 0x80) len++;
        memcpy(one, caps + at, len);
        one[len] = 0;
        width += pc5_text_width(&ui->fonts, face, one) + (count++ ? spacing : 0);
        at += len;
    }
    if (!draw) return width;
    int px = x - width / 2;
    for (size_t at = 0; caps[at];) {
        size_t len = 1;
        while (len < 4 && caps[at + len] && ((unsigned char)caps[at + len] & 0xc0) == 0x80) len++;
        memcpy(one, caps + at, len);
        one[len] = 0;
        px += pc5_text(c, &ui->fonts, face, px, y, color, one, 0, 0) + spacing;
        at += len;
    }
    return width;
}

static void library_screen(Pc5Ui *ui, Pc5Canvas *c)
{
    char count[48];

    backdrop(ui, c);
    /* a hairline across the top */
    for (int k = 0; k < 16; k++) {
        float x0 = W * (float)k / 16, x1 = W * (float)(k + 1) / 16, mid = fabsf((x0 + x1) / 2 - W / 2) / (W / 2);
        int alpha = (int)(34 * (mid < 0.76f ? 1 : (1 - mid) / 0.24f));
        if (alpha > 0) pc5_line(c, x0, 33, x1, 33, 1.2f, (uint32_t)alpha << 24 | 0xffffffu);
    }
    wordmark(ui, c, MARGIN, 92);
    if (ui->clock[0]) pc5_text(c, &ui->fonts, PC5_FACE_LABEL, W - MARGIN, 88, C_TEXT, ui->clock, 0, 2);
    if (!ui->library.count) {
        pc5_text(c, &ui->fonts, PC5_FACE_TITLE, 960, 470, C_TEXT, "No games yet", 0, 1);
        pc5_text(c, &ui->fonts, PC5_FACE_BODY, 960, 540, C_DIM,
                 "Press OPTIONS and choose Game maintenance \xe2\x80\xba Install games from USB.", 0, 1);
        Hint list[] = { { G_OPTIONS, "Settings" } };
        hints(ui, c, MARGIN, 1012, list, 1);
        return;
    }
    in_bands(ui, c, carousel_band, NULL);
    const Pc5Game *game = &ui->library.games[ui->selected];
    /* its name, a shadow behind it, and what it is in a pill */
    pc5_text(c, &ui->fonts, PC5_FACE_TITLE, 960, 834, 0x90000000u, game->title, 1500, 1);
    pc5_text(c, &ui->fonts, PC5_FACE_TITLE, 960, 830, C_TEXT, game->title, 1500, 1);
    if (!game->playable) {
        pc5_text(c, &ui->fonts, PC5_FACE_BODY, 960, 892, C_WARN, game->problem, 1500, 1);
    } else if (game->detail[0]) {
        uint32_t tint = pc5_rgba((int)ui->glow[0], (int)ui->glow[1], (int)ui->glow[2], 255), from, to;
        int tw = spaced_caps(ui, c, PC5_FACE_SMALL, 960, 892, 0, game->detail, 4, 0);
        float pw = (float)tw + 64, px = 960 - pw / 2;
        neon_of(tint, &from, &to, 255);
        pc5_shadow(c, px, 861, pw, 46, 23, 22, (to & 0xffffffu) | 0x50000000u);
        pc5_round_rect(c, px, 861, pw, 46, 23, 0x730a0d1au);
        pc5_round_frame(c, px, 861, pw, 46, 23, 2, pc5_mix(from, 0xffffffffu, 70) & 0xd0ffffffu);
        spaced_caps(ui, c, PC5_FACE_SMALL, 960, 892, 0xf0eef1fcu, game->detail, 4, 1);
    }
    Hint list[] = { { accept_glyph(ui), "Play" }, { G_TRIANGLE, "Game menu" }, { G_L1R1, "Jump" }, { G_OPTIONS, "Settings" } };
    hints(ui, c, MARGIN, 1012, list, 4);
    /* a line from the hints to the count, and the count */
    pc5_line(c, 840, 996, 1545, 996, 1.2f, 0x29ffffffu);
    pc5_line(c, 1545, 996, 1575, 960, 1.2f, 0x29ffffffu);
    pc5_line(c, 1575, 960, W, 960, 1.2f, 0x29ffffffu);
    snprintf(count, sizeof(count), "%d", ui->library.count);
    int words = spaced_caps(ui, c, PC5_FACE_SMALL, 0, 1012, 0, ui->library.count == 1 ? "game" : "games", 5, 0);
    spaced_caps(ui, c, PC5_FACE_SMALL, W - MARGIN - words / 2, 1012, C_DIM, ui->library.count == 1 ? "game" : "games", 5, 1);
    pc5_text(c, &ui->fonts, PC5_FACE_LABEL, W - MARGIN - words - 10, 1012, C_TEXT, count, 0, 2);
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

/* A row of Settings or a game's menu, h high: its label and value, and what it does
 * (about) only on the chosen row, under its label, on two lines at most (the row is ROW_LINE
 * higher for its second: rows_pitch). */
enum { ROW_PITCH = 100, ROW_LINE = 30, ABOUT_WIDTH = 760 - 144 };
static void drawer_row(Pc5Ui *ui, Pc5Canvas *c, float x, float y, float h, int chosen, const char *label,
                       const char *about, const char *value)
{
    int value_w = value && value[0] ? pc5_text_width(&ui->fonts, PC5_FACE_LABEL, value) : 0;
    int two = chosen && about && about[0];
    int lines = two && pc5_text_lines(&ui->fonts, PC5_FACE_SMALL, ABOUT_WIDTH, about) > 1 ? 2 : 1;
    float base = h - (lines - 1) * ROW_LINE - 10, high = h - 10;
    float label_y = two ? 0.42f * base + 8 : 0.5f * base + 12, about_y = 0.82f * base + 4;
    float bar = high - 24 < 56 ? high - 24 : 56;
    if (chosen) {
        pc5_round_rect(c, x + 36, y, 760 - 72, high, 16, C_ROW);
        pc5_round_rect(c, x + 36, y + high / 2 - bar / 2, 5, bar, 2.5f, C_TEXT);
    }
    pc5_text(c, &ui->fonts, PC5_FACE_LABEL, (int)x + 72, (int)(y + label_y), C_TEXT, label, 600 - value_w, 0);
    if (two)
        pc5_text_wrap(c, &ui->fonts, PC5_FACE_SMALL, (int)x + 72, (int)(y + about_y), ABOUT_WIDTH, lines, ROW_LINE, C_DIM,
                      about, 0);
    if (value_w)
        pc5_text(c, &ui->fonts, PC5_FACE_LABEL, (int)x + 760 - 72, (int)(y + label_y), chosen ? C_TEXT : C_ACCENT, value, 0, 2);
}

/* Where a drawer's rows go, from top down to ROWS_BOTTOM, clear of the hints: ROW_PITCH apart,
 * closer when count rows and the chosen one's second line (extra) don't fit; *extra is the
 * room the chosen row takes beyond the pitch. */
enum { ROWS_BOTTOM = 972 };
static float rows_pitch(Pc5Ui *ui, float top, int count, const char *chosen_about, float *extra)
{
    float pitch = ROW_PITCH;
    *extra = chosen_about && chosen_about[0] && pc5_text_lines(&ui->fonts, PC5_FACE_SMALL, ABOUT_WIDTH, chosen_about) > 1
                 ? ROW_LINE : 0;
    if (count > 0 && top + pitch * count + *extra > ROWS_BOTTOM) pitch = (ROWS_BOTTOM - top - *extra) / (float)count;
    return pitch;
}

static void settings_screen(Pc5Ui *ui, Pc5Canvas *c)
{
    float x = drawer(ui, c, page_titles[ui->page]), extra;
    SettingRow rows[PAGE_ROWS];
    int count = page_rows(&ui->settings, ui->page, offer_update(ui), rows);
    const char *label = "";
    char about[200], value[64];

    if (ui->menu < count) setting_text(ui, rows[ui->menu], &label, about, sizeof(about), value, sizeof(value));
    else about[0] = 0;
    float pitch = rows_pitch(ui, 196, count, about, &extra);
    for (int i = 0; i < count; i++) {
        setting_text(ui, rows[i], &label, about, sizeof(about), value, sizeof(value));
        drawer_row(ui, c, x, 196 + pitch * i + (i > ui->menu ? extra : 0), pitch + (i == ui->menu ? extra : 0),
                   i == ui->menu, label, about, value);
    }
    SettingRow row = rows[ui->menu < count ? ui->menu : 0];
    int changes = row == SR_KEEP_LOGS || (row >= SR_FPS && row <= SR_TRIGGER_RESISTANCE) || row == SR_CHECK_ON_OPEN ||
                  row == SR_STARTUP_SOUND || row == SR_STARTUP_VOLUME || row >= SR_SOURCE;
    Hint list[] = { { accept_glyph(ui), changes ? "Change" : "Choose" }, { back_glyph(ui), "Back" } };
    hints(ui, c, (int)x + 64, 1012, list, 2);
}

/* A row of a game's menu: its label, what it does (text, which may be about) and its value. */
static void game_row_text(Pc5Ui *ui, const Pc5Game *game, int kind, GameRow row, char *about, size_t about_size,
                          const char **label, const char **text, const char **value)
{
    static const char *const more = "\xe2\x80\xba";
    *label = *text = *value = "";
    switch (row) {
    case GR_PLAY:
        *label = "Play";
        *text = "To leave it, hold the touchpad and OPTIONS for 2 s";
        *value = "";
        break;
    case GR_CONTROLLER:
        *label = "Controller";
        *text = ui->controls.mode == PC5_PAD_DUALSENSE ? "The game sees a DualSense, as one plugged into a PC" : ui->controls.mode == PC5_PAD_KEYBOARD ? "The buttons press keys, the sticks move the mouse" : "Games with controller support see an Xbox controller";
        *value = pc5_pad_mode_name(ui->controls.mode);
        break;
    case GR_BUTTONS:
        *label = "Button mapping";
        *text = "The key or mouse button each one sends";
        *value = more;
        break;
    case GR_GRAPHICS:
        *label = "Graphics";
        *text = kind & GAME_VULKAN ? (kind & GAME_32BIT ? "Frames, resolution, aspect ratio, memory and CPU" : "Frame generation, resolution, aspect ratio and CPU") : (kind & GAME_32BIT ? "Frame generation, aspect ratio, memory and CPU" : "Frame generation, aspect ratio and CPU");
        *value = more;
        break;
    case GR_APPEARANCE:
        *label = "Appearance";
        *text = "Its cover, its background and a home screen tile";
        *value = more;
        break;
    case GR_MANAGE:
        *label = "Manage";
        *text = kind & GAME_PC5 ? "Its program, updating it and uninstalling it" : "Its program and uninstalling it";
        *value = more;
        break;
    case GR_GRAPHICS_API: {
        int api = ui_game_api(ui);
        const char *about_api =
            ui->controls.graphics == PC5_GRAPHICS_AUTO
                ? (api == PC5_API_D3D12  ? "VKD3D-Proton turns its Direct3D 12 into Vulkan"
                   : api == PC5_API_DDRAW ? "Wine's own Direct3D draws it, with OpenGL"
                                          : "DXVK turns its Direct3D into Vulkan")
            : api == PC5_API_D3D12 ? "Wine's own Direct3D 12; try it if the game won't run"
            : ui->controls.graphics == PC5_GRAPHICS_WINE_VULKAN ? "Wine's own Direct3D, drawing with Vulkan"
                                                                 : "Wine's own Direct3D, with OpenGL; try it if the game won't run";
        *label = "Graphics API";
        *text = about_api;
        *value = pc5_graphics_name(ui->controls.graphics, api);
        break;
    }
    case GR_FRAMEGEN:
        *label = "Frame generation";
        *text = ui->controls.frame_generation ? "Held at 30 fps and shown at 60" : "The game's frames as it makes them";
        *value = ui->controls.frame_generation ? "30 \xe2\x86\x92 60 fps" : "Off";
        break;
    case GR_RESOLUTION:
        *label = "Resolution";
        *text = ui->controls.resolution == 2 ? "Drawn at 4K, as the console shows it; four times the work of 1080p" : ui->controls.resolution == 1 ? "Drawn at 1440p and shown at 4K; sharper than 1080p, slower" : "Drawn at 1080p and shown at 4K by the console";
        *value = ui->controls.resolution == 2 ? "4K" : ui->controls.resolution == 1 ? "1440p" : "1080p";
        break;
    case GR_ASPECT:
        *label = "Aspect ratio";
        *text = ui->controls.stretch ? "Stretched to fill the screen: a 4:3 picture gets wider" : "The picture's own shape, with bars beside a 4:3 one";
        *value = ui->controls.stretch ? "Stretch" : "Keep";
        break;
    case GR_MEMORY:
        *label = "Memory";
        *text = ui->controls.memory_4gb ? "4 GB even for a game not made for it: room for mods; Auto if it misbehaves"
                                        : "As Windows: 4 GB for a game made for it, otherwise 2 GB";
        *value = ui->controls.memory_4gb ? "4 GB" : "Auto";
        break;
    case GR_VIDEO_MEMORY:
        *label = "Video memory";
        *text = ui->controls.video_memory_full ? "All the console's; Auto if the game says there is too little"
                                               : "2 GB at most, a number older games' checks can read";
        *value = ui->controls.video_memory_full ? "Full" : "Auto";
        break;
    case GR_CPU_SHARING:
        *label = "CPU sharing";
        *text = ui->controls.cpu_sharing ? "Its threads take turns on the CPUs, as on a PC" : "Each thread keeps its CPU; On if the game stalls";
        *value = ui->controls.cpu_sharing ? "On" : "Off";
        break;
    case GR_COVER:
        *label = "Cover";
        *text = "Another of those found, or search by another name";
        *value = more;
        break;
    case GR_BACKGROUND:
        *label = "Background";
        *text = "Another of those found, search by another name, or none";
        *value = more;
        break;
    case GR_ICON:
        *label = "Tile picture";
        *text = "Its home screen tile's picture: a square one found online, or one made from its cover";
        *value = more;
        break;
    case GR_HOME:
        if (ui->tile[0]) snprintf(about, about_size, "Its tile, %.40s, starts the game", ui->tile);
        *label = "Home screen tile";
        *text = ui->tile[0] ? about : "A tile on the PS5's home screen that starts it";
        *value = ui->tile[0] ? "On" : "Off";
        break;
    case GR_PROGRAM: {
        const char *exe = strrchr(game->executable, '\\');
        snprintf(about, about_size, "Now: %.150s", exe ? exe + 1 : game->executable);
        *label = "Change program";
        *text = about;
        *value = "";
        break;
    }
    case GR_FILES:
        *label = "Game files";
        *text = "Its folder: copy, move, rename and delete its files";
        *value = more;
        break;
    case GR_UPDATE:
        *label = "Update";
        *text = "A newer copy from USB; its saves and settings stay";
        *value = more;
        break;
    case GR_UNINSTALL:
        *label = "Uninstall";
        *text = "Deletes the game, or keeps its saves for later";
        *value = "";
        break;
    }
}

static void game_menu_screen(Pc5Ui *ui, Pc5Canvas *c, int chosen)
{
    int index = pc5_library_find(&ui->library, ui->launch_id), kind = ui_game_kind(ui), page = ui->game_page;
    if (index < 0) return;
    const Pc5Game *game = &ui->library.games[index];
    float x = drawer(ui, c, page == GP_MAIN ? game->title : game_page_titles[page]), extra;
    char about[200];
    const char *label, *text, *value;

    GameRow rows[GAME_ROWS];
    int count = game_rows(&ui->controls, kind, page, rows);
    pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)x + 64, 182, C_DIM,
             page != GP_MAIN ? game->title : game->playable ? game->detail : game->problem, 760 - 128, 0);
    text = "";
    if (chosen >= 0 && chosen < count) game_row_text(ui, game, kind, rows[chosen], about, sizeof(about), &label, &text, &value);
    float pitch = rows_pitch(ui, 226, count, text, &extra);
    for (int i = 0; i < count; i++) {
        game_row_text(ui, game, kind, rows[i], about, sizeof(about), &label, &text, &value);
        drawer_row(ui, c, x, 226 + pitch * i + (i > chosen ? extra : 0), pitch + (i == chosen ? extra : 0), chosen == i,
                   label, text, value);
    }
    GameRow row = chosen >= 0 && chosen < count ? rows[chosen] : GR_PLAY;
    int changes = row == GR_CONTROLLER || row == GR_FRAMEGEN || row == GR_RESOLUTION || row == GR_ASPECT ||
                  row == GR_MEMORY || row == GR_VIDEO_MEMORY || row == GR_CPU_SHARING || row == GR_HOME;
    Hint list[] = { { accept_glyph(ui), changes ? "Change" : "Choose" }, { back_glyph(ui), "Back" } };
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
        case PR_SLOW:
            pc5_copy(label, sizeof(label), "Slow pointer button");
            pc5_copy(value, sizeof(value), k->slow_button >= 0 ? pc5_control_name(k->slow_button) : "None");
            break;
        case PR_SLOW_PERCENT:
            pc5_copy(label, sizeof(label), "    Slower by");
            snprintf(value, sizeof(value), "%d%%", k->slow_percent);
            break;
        case PR_BUTTON:
            pc5_copy(label, sizeof(label), pc5_control_name(row.index));
            pc5_copy(value, sizeof(value), row.index == k->slow_button ? "Slows the pointer"
                                                                       : pc5_key_label(k->key[row.index]));
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

/* What a Graphics API choice does, in full, for a game whose graphics are api (the Graphics API window). */
static const char *graphics_about(Pc5Graphics graphics, int api)
{
    if (graphics == PC5_GRAPHICS_AUTO)
        return api == PC5_API_D3D12   ? "PC5's choice: VKD3D-Proton turns the game's Direct3D 12 into Vulkan. The fastest, and what most games want."
               : api == PC5_API_DDRAW ? "PC5's choice: Wine's own Direct3D draws the game's DirectDraw with OpenGL."
                                      : "PC5's choice: DXVK turns the game's Direct3D into Vulkan. The fastest, and what most games want.";
    if (api == PC5_API_D3D12)
        return "Wine's own Direct3D 12 (VKD3D). Slower than Auto; try it if the game won't start or draws wrong.";
    if (graphics == PC5_GRAPHICS_WINE_VULKAN)
        return "Wine's own Direct3D, drawing with Vulkan. Try it if the game draws wrong with Auto; often faster than WineD3D.";
    return "Wine's own Direct3D, drawing with OpenGL. The slowest; try it if the game won't start with the others.";
}

/* The Graphics API window: each choice for the game, with all it does; the one in use is marked. */
static void graphics_dialog(Pc5Ui *ui, Pc5Canvas *c)
{
    enum { LINE = 36, TEXT_W = 1100 - 120 - 48 };
    Pc5Graphics choices[PC5_GRAPHICS_COUNT];
    int api = ui_game_api(ui), count = pc5_graphics_choices(api, choices), lines[PC5_GRAPHICS_COUNT];
    float w = 1100, h = 170 + 100, x, y, cy;
    char title[96];

    for (int i = 0; i < count; i++) {
        lines[i] = pc5_text_lines(&ui->fonts, PC5_FACE_SMALL, TEXT_W, graphics_about(choices[i], api));
        h += 74 + lines[i] * LINE + 16;
    }
    x = (W - w) / 2;
    y = (H - h) / 2;
    dim(c, 150);
    panel(c, x, y, w, h);
    pc5_text(c, &ui->fonts, PC5_FACE_HEAD, (int)x + 60, (int)y + 92, C_TEXT, "Graphics API", (int)w - 120, 0);
    snprintf(title, sizeof(title), "The game uses %s", pc5_api_name((Pc5Api)api));
    pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)x + 60, (int)y + 136, C_DIM, title, (int)w - 120, 0);
    cy = y + 170;
    for (int i = 0; i < count; i++) {
        float bh = 74 + lines[i] * LINE;
        int chosen = i == ui->api_choice, now = choices[i] == ui->controls.graphics;
        pc5_round_rect(c, x + 60, cy, w - 120, bh, 16, chosen ? C_TEXT : C_ROW);
        if (now && !chosen) pc5_round_frame(c, x + 60, cy, w - 120, bh, 16, 2.5f, C_ACCENT);
        pc5_text(c, &ui->fonts, PC5_FACE_LABEL, (int)x + 84, (int)cy + 50, chosen ? C_PANEL : C_TEXT,
                 pc5_graphics_name(choices[i], api), TEXT_W - 200, 0);
        if (now)
            pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)(x + w) - 84, (int)cy + 48, chosen ? C_PANEL : C_ACCENT,
                     "In use", 200, 2);
        pc5_text_wrap(c, &ui->fonts, PC5_FACE_SMALL, (int)x + 84, (int)cy + 50 + LINE + 6, TEXT_W, lines[i], LINE,
                      chosen ? C_PANEL : C_DIM, graphics_about(choices[i], api), 0);
        cy += bh + 16;
    }
    Hint list[] = { { accept_glyph(ui), "Choose" }, { back_glyph(ui), "Back" } };
    hints(ui, c, (int)x + 60, (int)(y + h - 40), list, 2);
}

static int qr_draw(Pc5Canvas *c, const Pc5Qr *qr, int x, int y, int box);

#define PC5_COFFEE "https://www.buymeacoffee.com/bornaradusin"

static void about_screen(Pc5Ui *ui, Pc5Canvas *c)
{
    float w = 1500, h = 700, x = (W - w) / 2, y = (H - h) / 2;
    char line[200], space[32];
    uint64_t free_bytes = 0;
    int ty = (int)y + 196;
    static const char *const paragraphs[] = {
        ("PC5 runs your PC games with prospero-win: Wine, DXVK for Direct3D 8 to 11, VKD3D-Proton for "
         "Direct3D 12, and Mesa (RADV for Vulkan, Zink for OpenGL)."),
        "Covers come from Steam, GOG, PCGamingWiki and Wikidata, and SteamGridDB with your key. No game is part of PC5.",
        "Free software under the GNU LGPL 2.1 or later.",
    };

    dim(c, 150);
    panel(c, x, y, w, h);
    wordmark(ui, c, (int)x + 64, (int)y + 110);
    pc5_text(c, &ui->fonts, PC5_FACE_BODY, (int)x + 160, (int)y + 106, C_DIM,
             "version " PC5_VERSION " \xc2\xb7 " PC5_TITLE_ID, 0, 0);
    /* Buy Me a Coffee, on the right: a code for a phone, the address under it. */
    const int box = 300, cx = (int)(x + w) - box - 64, column = cx - 64 - ((int)x + 64);
    if (!ui->coffee_qr.size) pc5_qr_make(PC5_COFFEE, &ui->coffee_qr);
    pc5_text(c, &ui->fonts, PC5_FACE_LABEL, cx + box / 2, (int)y + 96, C_TEXT, "Like PC5?", box, 1);
    qr_draw(c, &ui->coffee_qr, cx, (int)y + 124, box);
    pc5_round_rect(c, (float)cx, y + 444, (float)box, 60, 30, 0xffffdd00u);
    pc5_text(c, &ui->fonts, PC5_FACE_LABEL, cx + box / 2, (int)y + 484, 0xff0d0c22u, "Buy me a coffee", box - 24, 1);
    pc5_text(c, &ui->fonts, PC5_FACE_SMALL, cx + box / 2, (int)y + 548, C_DIM, "buymeacoffee.com/bornaradusin", box + 100, 1);
    for (int i = 0; i < 3; i++)
        ty += 42 * pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, (int)x + 64, ty, column, 3, 42, C_TEXT, paragraphs[i], 0) + 22;
    snprintf(line, sizeof(line), "Games are kept in %.150s", ui->root);
    pc5_text(c, &ui->fonts, PC5_FACE_BODY, (int)x + 64, ty + 20, C_DIM, line, column, 0);
    if (!pc5_free_bytes(ui->root, &free_bytes)) {
        pc5_size_text(free_bytes, space, sizeof(space));
        snprintf(line, sizeof(line), "%s free on the console", space);
        pc5_text(c, &ui->fonts, PC5_FACE_BODY, (int)x + 64, ty + 68, C_DIM, line, column, 0);
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
    if (ui->scan_roots[ui->sources[i].drive] == ui->install_dir) snprintf(line, sizeof(line), "INSTALL folder");
    else snprintf(line, sizeof(line), "USB drive %d", ui->sources[i].drive + 1);
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
    if (i >= ui->library.count) {
        /* Kept saves, after the games (Settings › Uninstall game). */
        const Pc5Saves *saves = &ui->saves[i - ui->library.count];
        char title[PC5_NAME + 16];
        (void)chosen;
        pc5_round_frame(c, x, y + 8, 56, 84, 6, 2, 0xff3a404cu);
        static const char *const months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
        char about[160];
        snprintf(title, sizeof(title), "Saves of %s", saves->title);
        if (saves->kept > 0) {
            /* The day (UTC) from the days since 1970 (Howard Hinnant's civil_from_days; the
             * console's C library has no gmtime_r). */
            int64_t z = saves->kept / 86400 + 719468, era = z / 146097, doe = z - era * 146097;
            int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
            int64_t mp = (5 * doy + 2) / 153, d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
            int64_t y = yoe + era * 400 + (m <= 2);
            snprintf(about, sizeof(about), "Kept %d %s %d; installing it again puts them back", (int)d,
                     months[(m - 1) % 12], (int)y);
        } else
            snprintf(about, sizeof(about), "Kept when it was uninstalled; installing it again puts them back");
        pc5_text(c, &ui->fonts, PC5_FACE_LABEL, (int)x + 84, (int)y + 44, C_TEXT, title, (int)w - 84, 0);
        pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)x + 84, (int)y + 78, C_DIM, about, (int)w - 84, 0);
        return;
    }
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
        if (ui->choice >= 0 && ui->choice < ui->search.count) snprintf(line, sizeof(line), "Cover %d of %d", at, choices);
        else snprintf(line, sizeof(line), "Made from the game's icon \xc2\xb7 %d of %d", at, choices);
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
        pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, (int)tx, (int)ny, (int)tw, 2, 40, C_DIM,
                      "A .NET program: it runs with Wine Mono, which PC5 sets up the first time a .NET game starts.", 0);
        ny += 90;
    }
    pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, (int)tx, (int)ny, (int)tw, 4, 40, C_DIM,
                  review_on_usb(ui)
                      ? "Install copies the game to the console. Play from USB copies nothing: the game plays from "
                        "the drive, which stays plugged in. Whether a game runs is only known by trying it."
                      : "The game is copied to the console once and checked against the USB drive. Whether a game "
                        "runs is only known by trying it.", 0);
    Hint list[5];
    int n = 0;
    list[n++] = (Hint){ accept_glyph(ui), ui->queue_review >= 0 ? "Done" : "Install" };
    if (review_on_usb(ui)) list[n++] = (Hint){ G_TRIANGLE, "Play from USB" };
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
    case PC5_INSTALL_PREFIX: step = in->kept[0] ? "Putting back its saves and settings" : "Setting up the game's Windows";
        number = 1; break;
    case PC5_INSTALL_LIST:
    case PC5_INSTALL_COPY: step = in->update ? "Copying the newer version" : "Copying the game"; number = in->update ? 1 : 2; break;
    case PC5_INSTALL_VERIFY: step = "Checking the copy against the USB drive"; number = in->update ? 2 : 3; break;
    case PC5_INSTALL_FINISH: step = in->update ? "Putting the new files in place" : "Finishing"; number = 3; break;
    default:
        if (in->tidying) { step = "Finishing"; number = 3; break; }
        step = in->cancel ? "Stopping: deleting what was copied" : "Deleting what was copied"; number = 0; break;
    }
    dim(c, 150);
    panel(c, x, y, w, h);
    if (ui->queue_installing) {
        snprintf(line, sizeof(line), "Game %d of %d", ui->queue_at + 1, ui->queue_count);
        pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)x + 64, (int)y + 62, C_ACCENT, line, (int)w - 128, 0);
    }
    snprintf(title, sizeof(title), in->update ? "Updating %s" : "Installing %s", in->info.title);
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

/* A row of buttons, the one at danger (or -1) red when chosen. */
static void button_row(Pc5Ui *ui, Pc5Canvas *c, float x, float y, const char *const *labels, int count, int chosen,
                       int danger)
{
    for (int i = 0; i < count; i++) {
        float bw = count > 2 ? 250 : 316, bx = x + i * (bw + 24), bh = 66;
        if (i == chosen) {
            uint32_t fill = i == danger ? C_RED : 0xffeef0f4u;
            pc5_round_rect(c, bx, y, bw, bh, 33, fill);
            pc5_text(c, &ui->fonts, PC5_FACE_LABEL, (int)(bx + bw / 2), (int)y + 43,
                     i == danger ? C_TEXT : 0xff111318u, labels[i], (int)bw - 40, 1);
        } else {
            pc5_round_frame(c, bx, y, bw, bh, 33, 2, 0xff3a404cu);
            pc5_text(c, &ui->fonts, PC5_FACE_LABEL, (int)(bx + bw / 2), (int)y + 43, C_DIM, labels[i], (int)bw - 40, 1);
        }
    }
}

static void two_buttons(Pc5Ui *ui, Pc5Canvas *c, float x, float y, const char *one, const char *two, int chosen,
                        int second_is_danger)
{
    const char *labels[2] = { one, two };
    button_row(ui, c, x, y, labels, 2, chosen, second_is_danger ? 1 : -1);
}

/* A dialog's height (*h) for its title (2 lines at most) and body (at most max_body lines), width
 * wide, with a row of buttons under them, and where its body starts (*body_y); no lower than
 * least (for a picture beside the text). */
static void dialog_size(Pc5Ui *ui, int width, const char *title, const char *body, int max_body, float least,
                        int *title_lines, int *body_lines, float *body_y, float *h)
{
    *title_lines = pc5_text_lines(&ui->fonts, PC5_FACE_HEAD, width, title);
    *body_lines = body ? pc5_text_lines(&ui->fonts, PC5_FACE_BODY, width, body) : 0;
    if (*title_lines < 1) *title_lines = 1;
    if (*title_lines > 2) *title_lines = 2;
    if (*body_lines > max_body) *body_lines = max_body;
    *body_y = 200 + (*title_lines - 1) * 50;
    *h = *body_y + (*body_lines > 0 ? *body_lines - 1 : 0) * 42 + 66 + 120;
    if (*h < least) *h = least;
}

static void confirm_dialog(Pc5Ui *ui, Pc5Canvas *c, const char *title, const char *body, const char *one,
                           const char *two, int danger, const Pc5Picture *thumb)
{
    float w = 1100, h, x = (W - w) / 2, y, tx = x + 64 + (thumb && thumb->levels ? 160 + 56 : 0), body_y;
    int title_lines, body_lines;
    dialog_size(ui, (int)(x + w - 64 - tx), title, body, 6, thumb && thumb->levels ? 470 : 0, &title_lines, &body_lines,
                &body_y, &h);
    y = (H - h) / 2;
    dim(c, 170);
    panel(c, x, y, w, h);
    if (thumb && thumb->levels) pc5_draw_cover(c, thumb, x + 64, y + 64 + 120, 160, 240, 240, 10, 256, 256, 0);
    pc5_text_wrap(c, &ui->fonts, PC5_FACE_HEAD, (int)tx, (int)y + 110, (int)(x + w - 64 - tx), title_lines, 50, C_TEXT, title, 0);
    pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, (int)tx, (int)(y + body_y), (int)(x + w - 64 - tx), body_lines, 42, C_DIM,
                  body, 0);
    two_buttons(ui, c, tx, y + h - 120, one, two, ui->button, danger);
}

/* The same with three buttons, the third the dangerous one. */
static void confirm3_dialog(Pc5Ui *ui, Pc5Canvas *c, const char *title, const char *body, const char *one,
                            const char *two, const char *three, const Pc5Picture *thumb)
{
    float w = 1100, h, x = (W - w) / 2, y, tx = x + 64 + (thumb && thumb->levels ? 160 + 56 : 0), body_y;
    const char *labels[3] = { one, two, three };
    int title_lines, body_lines;
    dialog_size(ui, (int)(x + w - 64 - tx), title, body, 6, thumb && thumb->levels ? 500 : 0, &title_lines, &body_lines,
                &body_y, &h);
    y = (H - h) / 2;
    dim(c, 170);
    panel(c, x, y, w, h);
    if (thumb && thumb->levels) pc5_draw_cover(c, thumb, x + 64, y + 64 + 120, 160, 240, 240, 10, 256, 256, 0);
    pc5_text_wrap(c, &ui->fonts, PC5_FACE_HEAD, (int)tx, (int)y + 110, (int)(x + w - 64 - tx), title_lines, 50, C_TEXT, title, 0);
    pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, (int)tx, (int)(y + body_y), (int)(x + w - 64 - tx), body_lines, 42, C_DIM,
                  body, 0);
    button_row(ui, c, tx, y + h - 120, labels, 3, ui->button, 2);
}

static void message_dialog(Pc5Ui *ui, Pc5Canvas *c)
{
    float w = 1100, h, x = (W - w) / 2, y, body_y;
    int title_lines, body_lines;
    dialog_size(ui, (int)w - 204, ui->message_title, ui->message_body, 8, 0, &title_lines, &body_lines, &body_y, &h);
    body_y -= 10;   /* the title sits on the icon's line, a little lower */
    h -= 60 + 10;   /* and one line of hints under the text, not a row of buttons */
    if (h < 320) h = 320;
    y = (H - h) / 2;
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
    pc5_text_wrap(c, &ui->fonts, PC5_FACE_HEAD, (int)x + 140, (int)y + 112, (int)w - 204, title_lines, 50, C_TEXT,
                  ui->message_title, 0);
    pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, (int)x + 140, (int)(y + body_y), (int)w - 204, body_lines, 42, C_DIM,
                  ui->message_body, 0);
    Hint list[] = { { accept_glyph(ui), "OK" }, { G_TRIANGLE, "Report on GitHub" } };
    hints(ui, c, (int)x + 140, (int)(y + h - 48), list, ui->message_report ? 2 : 1);
}

/* A rectangle of one color, pixel for pixel (the QR code's modules, which must stay sharp). */
static void fill(Pc5Canvas *c, int x, int y, int w, int h, uint32_t color)
{
    for (int py = y < 0 ? 0 : y; py < y + h && py < c->h; py++)
        for (int px = x < 0 ? 0 : x; px < x + w && px < c->w; px++) c->px[(size_t)py * (size_t)c->stride + (size_t)px] = color;
}

/* A QR code, black on white with its quiet zone, as large as fits a box of that side
 * centred at its place. Returns the side drawn (0: none). */
static int qr_draw(Pc5Canvas *c, const Pc5Qr *qr, int x, int y, int box)
{
    if (!qr->size) return 0;
    int modules = qr->size + 8, scale = box / modules, side = modules * scale;
    if (scale < 1) return 0;
    x += (box - side) / 2;
    y += (box - side) / 2;
    fill(c, x, y, side, side, 0xffffffffu);
    for (int my = 0; my < qr->size; my++)
        for (int mx = 0; mx < qr->size;) {
            int run = 0;
            while (mx + run < qr->size && pc5_qr_dark(qr, mx + run, my)) run++;
            if (run) fill(c, x + (4 + mx) * scale, y + (4 + my) * scale, run * scale, scale, 0xff000000u);
            mx += run ? run : 1;
        }
    return side;
}

/* Report on GitHub: the form's link as a QR code, and what to do with it. */
static void report_screen(Pc5Ui *ui, Pc5Canvas *c)
{
    float w = 1500, h = 700, x = (W - w) / 2, y = (H - h) / 2;
    char line[400], where[PC5_PATH * 2 + 80];
    dim(c, 170);
    panel(c, x, y, w, h);
    pc5_text(c, &ui->fonts, PC5_FACE_HEAD, (int)x + 80, (int)y + 100, C_TEXT, "Report on GitHub", 0, 0);
    int box = 560, qx = (int)(x + w) - box - 70, qy = (int)y + 70;
    qr_draw(c, &ui->report_qr, qx, qy, box);
    int tx = (int)x + 80, tw = qx - tx - 60;
    const char *game = ui->report_game[0] ? ui->report_game : "The game";
    if (ui->report_qr.size)
        snprintf(line, sizeof(line), "1. Scan the code with your phone. GitHub's form for a new issue opens, filled in with %s, "
                 "PC5's version and what PC5 saw.", game);
    else
        snprintf(line, sizeof(line), "1. On your phone or computer, open %s and choose New issue.", "github.com/bornaradusin/PC5/issues");
    int at = (int)y + 190;
    at += 42 * pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, tx, at, tw, 4, 42, C_DIM, line, 0) + 24;
    snprintf(where, sizeof(where), "2. Copy %s off the console, from %s/logs, as you copy files to it.", pc5_report_file(&ui->report),
             ui->root);
    at += 42 * pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, tx, at, tw, 4, 42, C_DIM, where, 0) + 24;
    pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, tx, at, tw, 3, 42, C_DIM,
                  "3. Say what happened, attach the zip, and submit. You need a GitHub account.", 0);
    Hint list[] = { { back_glyph(ui), "Back" } };
    hints(ui, c, tx, (int)(y + h - 48), list, 1);
}

/* Game maintenance › Game compatibility list: the wiki's list as a QR code, and what it holds. */
static void compatibility_screen(Pc5Ui *ui, Pc5Canvas *c)
{
    float w = 1500, h = 700, x = (W - w) / 2, y = (H - h) / 2;
    dim(c, 170);
    panel(c, x, y, w, h);
    pc5_text(c, &ui->fonts, PC5_FACE_HEAD, (int)x + 80, (int)y + 100, C_TEXT, "Game compatibility list", 0, 0);
    int box = 560, qx = (int)(x + w) - box - 70, qy = (int)y + 70;
    qr_draw(c, &ui->compatibility_qr, qx, qy, box);
    int tx = (int)x + 80, tw = qx - tx - 60, at = (int)y + 190;
    at += 42 * pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, tx, at, tw, 4, 42, C_DIM,
                             ui->compatibility_qr.size
                                 ? "Scan the code with your phone: PC5's wiki opens on the list of games players have "
                                   "tried, how each ran and the settings that helped."
                                 : "On your phone or computer, open github.com/bornaradusin/PC5/wiki/Game-compatibility: "
                                   "the games players have tried, how each ran and the settings that helped.", 0) + 24;
    pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, tx, at, tw, 4, 42, C_DIM,
                  "Got a game running? Add a game, at the top of the list's page: the form takes a minute. You need a GitHub "
                  "account.", 0);
    Hint list[] = { { back_glyph(ui), "Back" } };
    hints(ui, c, tx, (int)(y + h - 48), list, 1);
}

/* PC5 updating itself: its steps, the download's and the unpacking's progress, then done. */
static void upgrade_screen(Pc5Ui *ui, Pc5Canvas *c)
{
    const Pc5Upgrade *u = &ui->upgrade;
    int step = atomic_load(&u->step);
    uint64_t done = atomic_load(&u->done), total = atomic_load(&u->total);
    float w = 1180, h = 520, x = (W - w) / 2, y = (H - h) / 2;
    char title[200], line[300], have[32], all[32], speed[32], left[48] = "";
    const char *what;
    int number;

    dim(c, 150);
    panel(c, x, y, w, h);
    if (step == PC5_UPGRADE_DONE) {
        pc5_circle(c, x + 92, y + 108, 22, 0xff3fb27fu);
        pc5_line(c, x + 81, y + 109, x + 89, y + 117, 4, 0xff0b0d10u);
        pc5_line(c, x + 89, y + 117, x + 104, y + 99, 4, 0xff0b0d10u);
        snprintf(title, sizeof(title), "PC5 %s is installed", u->version);
        pc5_text(c, &ui->fonts, PC5_FACE_HEAD, (int)x + 140, (int)y + 122, C_TEXT, title, (int)w - 204, 0);
        pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, (int)x + 140, (int)y + 200, (int)w - 204, 5, 42, C_DIM,
                      "PC5 restarts now with the new version. Your games, saves and settings are as they were.", 0);
        Hint list[] = { { accept_glyph(ui), "Restart PC5" } };
        hints(ui, c, (int)x + 140, (int)(y + h - 48), list, 1);
        return;
    }
    switch (step) {
    case PC5_UPGRADE_DOWNLOADING: what = "Downloading it from GitHub"; number = 1; break;
    case PC5_UPGRADE_UNPACKING: what = "Unpacking it and checking every file"; number = 2; break;
    case PC5_UPGRADE_REPLACING: what = "Putting the new files in place"; number = 3; break;
    case PC5_UPGRADE_TIDYING: what = "Deleting the old files"; number = 3; break;
    default: what = "Asking GitHub and finding PC5's folder"; number = 0; break;
    }
    int stopping = atomic_load(&u->cancel) && step <= PC5_UPGRADE_UNPACKING;
    if (stopping) { what = "Stopping"; number = 0; }
    if (step >= PC5_UPGRADE_DOWNLOADING && u->version[0]) snprintf(title, sizeof(title), "Updating PC5 to %s", u->version);
    else pc5_copy(title, sizeof(title), "Updating PC5");
    pc5_text(c, &ui->fonts, PC5_FACE_HEAD, (int)x + 64, (int)y + 116, C_TEXT, title, (int)w - 128, 0);
    if (number) snprintf(line, sizeof(line), "Step %d of 3 \xc2\xb7 %s", number, what);
    else pc5_copy(line, sizeof(line), what);
    pc5_text(c, &ui->fonts, PC5_FACE_LABEL, (int)x + 64, (int)y + 186, C_TEXT, line, (int)w - 128, 0);
    if (step == PC5_UPGRADE_REPLACING && total) {
        progress_bar(c, x + 64, y + 236, w - 128, (double)done / (double)total, pc5_mix(C_ACCENT, C_TEXT, 60));
        snprintf(line, sizeof(line), "%llu of %llu files", (unsigned long long)done, (unsigned long long)total);
        pc5_text(c, &ui->fonts, PC5_FACE_BODY, (int)x + 64, (int)y + 306, C_DIM, line, (int)w - 128, 0);
    } else if ((step == PC5_UPGRADE_DOWNLOADING || step == PC5_UPGRADE_UNPACKING) && total && !stopping) {
        progress_bar(c, x + 64, y + 236, w - 128, (double)done / (double)total, pc5_mix(C_ACCENT, C_TEXT, 60));
        pc5_size_text(done, have, sizeof(have));
        pc5_size_text(total, all, sizeof(all));
        if (ui->speed > 1024) {
            pc5_size_text((uint64_t)ui->speed, speed, sizeof(speed));
            if (total > done) {
                double seconds = (double)(total - done) / ui->speed;
                if (seconds < 60) snprintf(left, sizeof(left), " \xc2\xb7 under a minute left");
                else snprintf(left, sizeof(left), " \xc2\xb7 about %d min left", (int)(seconds / 60 + 0.5));
            }
            snprintf(line, sizeof(line), "%s of %s \xc2\xb7 %s/s%s", have, all, speed, left);
        } else {
            snprintf(line, sizeof(line), "%s of %s \xc2\xb7 measuring the speed", have, all);
        }
        pc5_text(c, &ui->fonts, PC5_FACE_BODY, (int)x + 64, (int)y + 306, C_DIM, line, (int)w - 128, 0);
    } else {
        spinner(c, x + w - 100, y + 176, 22, ui->now, C_TEXT);
    }
    if (step >= PC5_UPGRADE_DOWNLOADING && u->folder[0]) {
        snprintf(line, sizeof(line), "Into %.280s", u->folder);
        pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)x + 64, (int)y + 350, C_FAINT, line, (int)w - 128, 0);
    }
    if (step <= PC5_UPGRADE_UNPACKING && !stopping) {
        Hint list[] = { { back_glyph(ui), "Stop" } };
        hints(ui, c, (int)x + 64, (int)(y + h - 44), list, 1);
    } else if (step >= PC5_UPGRADE_REPLACING) {
        pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)x + 64, (int)(y + h - 44), C_DIM,
                 "Don't turn the console off now. This takes a moment.", (int)w - 128, 0);
    }
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

/* Appearance › Cover or Background: the picture shown, what it is, and the keys. */
static void picture_screen(Pc5Ui *ui, Pc5Canvas *c)
{
    int index = pc5_library_find(&ui->library, ui->launch_id);
    if (index < 0 || !ui->pick) return;
    const Pc5Game *game = &ui->library.games[index];
    int cover = ui->pick_kind == PICK_COVER, icon = ui->pick_kind == PICK_ICON, entry = pick_entry(ui, ui->pick_at);
    float w = 1600, h = 880, x = (W - w) / 2, y = (H - h) / 2;
    float pw = cover ? 420 : icon ? 540 : 960, ph = cover ? 630 : 540, px = x + 72, py = y + 90;
    char line[300];

    dim(c, 160);
    panel(c, x, y, w, h);
    if (cover) {
        pc5_shadow(c, px, py, pw, ph, 14, 50, pc5_mix(ui->pick_tint, 0xff000000u, 60) | 0xa0000000u);
        if (ui->pick_picture.levels) pc5_draw_cover(c, &ui->pick_picture, px, py + ph / 2, pw, ph, ph, 14, 256, 256, 0);
        else pc5_round_rect(c, px, py, pw, ph, 14, C_ROW);
    } else if (ui->pick_image.px) {
        pc5_draw_image(c, &ui->pick_image, px, py, pw, ph, 255);
    } else {
        /* PC5's own backdrop, when the backdrop made from the cover can't be shown */
        pc5_round_rect(c, px, py, pw, ph, 14, 0xff0d0f14u);
        pc5_round_frame(c, px, py, pw, ph, 14, 1.5f, C_EDGE);
    }
    if (ui->pick_fetching) {
        pc5_round_rect(c, px, py, pw, ph, 14, 0xa0000000u);
        spinner(c, px + pw / 2, py + ph / 2, 34, ui->now, C_TEXT);
    }
    snprintf(line, sizeof(line), "%d of %d", ui->pick_at + 1, ui->pick_count);
    pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)(px + pw / 2), (int)(py + ph + 46), C_DIM, line, (int)pw, 1);

    float tx = px + pw + 64, tw = x + w - 72 - tx, ty = y + 150;
    ty += 64 * (float)pc5_text_wrap(c, &ui->fonts, PC5_FACE_TITLE, (int)tx, (int)ty, (int)tw, 2, 64, C_TEXT, game->title, 0);
    pc5_text(c, &ui->fonts, PC5_FACE_BODY, (int)tx, (int)ty, C_DIM, cover ? "Its cover" : icon ? "Its tile picture" : "Its background", (int)tw, 0);
    ty += 90;
    const char *what = "", *detail = "";
    if (ui->pick_fetching) what = "";
    else if (entry < 0) what = cover ? "The cover it has now" : icon ? "The tile picture it has now" : "The background it has now";
    else if (entry < ui->pick->count) {
        what = cover ? "A cover found online" : icon ? "A square picture found online" : "A background found online";
        detail = ui->pick->choices[entry].title;
    } else what = cover ? "Made from the game's icon and name"
                  : icon ? "Made from its cover" : "None: a backdrop made from its cover";
    ty += 50 * (float)pc5_text_wrap(c, &ui->fonts, PC5_FACE_LABEL, (int)tx, (int)ty, (int)tw, 2, 50, C_TEXT, what, 0);
    if (detail[0]) ty += 42 * (float)pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, (int)tx, (int)ty, (int)tw, 2, 42, C_DIM, detail, 0);
    ty += 40;
    if (!ui->pick->count) {
        char none[200];
        if (ui->pick_query[0])
            snprintf(none, sizeof(none), "Nothing found for \xe2\x80\x9c%s\xe2\x80\x9d.", ui->pick_query);
        else if (icon && !ui->griddb_key[0])
            snprintf(none, sizeof(none), "Square pictures come from SteamGridDB: put your key in api_keys.txt (Covers on the wiki).");
        else
            snprintf(none, sizeof(none), "Nothing found for its name.");
        ty += 40 * (float)pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, (int)tx, (int)ty, (int)tw, 2, 40, C_WARN, none, 0);
    } else if (ui->pick_query[0]) {
        snprintf(line, sizeof(line), "Found for \xe2\x80\x9c%s\xe2\x80\x9d", ui->pick_query);
        ty += 40 * (float)pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, (int)tx, (int)ty, (int)tw, 2, 40, C_FAINT, line, 0);
    }
    Hint list[4];
    int n = 0;
    list[n++] = (Hint){ accept_glyph(ui), "Use this" };
    if (ui->pick_count > 1) list[n++] = (Hint){ G_L1R1, cover ? "Other covers" : icon ? "Other pictures" : "Other backgrounds" };
    if (ui->platform.keyboard_open) list[n++] = (Hint){ G_TRIANGLE, "Search by name" };
    list[n++] = (Hint){ back_glyph(ui), "Back" };
    hints(ui, c, (int)px, (int)(y + h - 48), list, n);
}

/* ---- the file manager's screens ------------------------------------------------------------ */

static void files_row(Pc5Ui *ui, Pc5Canvas *c, int i, float x, float y, float w, int chosen)
{
    char size[32];
    (void)chosen;
    if (!ui->files_path[0]) {
        const FilesPlace *place = &ui->files_place[i];
        char where[PC5_PATH + 32];
        snprintf(where, sizeof(where), "%.900s%s", place->path, place->read_only ? " \xc2\xb7 read-only" : "");
        pc5_text(c, &ui->fonts, PC5_FACE_LABEL, (int)x, (int)y + 40, C_TEXT, place->name, (int)w, 0);
        pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)x, (int)y + 74, C_DIM, where, (int)w, 0);
        return;
    }
    const Pc5FileItem *item = &ui->files.items[i];
    tick_box(c, x, y + 20, ui->files_ticked && ui->files_ticked[i]);
    /* a folder: a folder's shape; a file: a page's */
    if (item->dir) {
        pc5_round_rect(c, x + 52, y + 24, 16, 8, 3, C_DIM);
        pc5_round_rect(c, x + 52, y + 28, 34, 24, 4, C_DIM);
    } else {
        pc5_round_frame(c, x + 57, y + 20, 24, 32, 4, 2, C_DIM);
    }
    pc5_size_text(item->size, size, sizeof(size));
    pc5_text(c, &ui->fonts, PC5_FACE_LABEL, (int)x + 106, (int)y + 46, C_TEXT, item->name, (int)w - 106 - 200, 0);
    pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)(x + w), (int)y + 46, C_DIM, item->dir ? "Folder" : size, 0, 2);
}

/* The folder (or the places), as a list. */
static void files_list_screen(Pc5Ui *ui, Pc5Canvas *c)
{
    char title[PC5_PATH + 200], empty[PC5_ERROR] = "";
    int keep = ui->menu, ticks = files_ticks(ui);
    const char *name = "Game files";
    if (ui->files_game) {
        int index = pc5_library_find(&ui->library, ui->launch_id);
        if (index >= 0) name = ui->library.games[index].title;
    }
    if (!ui->files_path[0]) {
        pc5_copy(title, sizeof(title), "File manager");
    } else if (ui->files_game) {
        size_t n = strlen(ui->files_top);
        if (!strncmp(ui->files_path, ui->files_top, n) && ui->files_path[n] == '/')
            snprintf(title, sizeof(title), "%s \xe2\x80\xba %s", name, ui->files_path + n + 1);
        else
            snprintf(title, sizeof(title), "%s", name);
    } else {
        snprintf(title, sizeof(title), "%s", ui->files_path);
    }
    if (ui->files_path[0] && !files_writable(ui, ui->files_path)) pc5_format(title + strlen(title), sizeof(title) - strlen(title), " \xc2\xb7 read-only");
    if (ticks) pc5_format(title + strlen(title), sizeof(title) - strlen(title), " \xc2\xb7 %d ticked", ticks);
    if (ui->files.error) snprintf(empty, sizeof(empty), "%s.", strerror(ui->files.error));
    else if (files_writable(ui, ui->files_path)) pc5_copy(empty, sizeof(empty), "\xe2\x96\xb3 Paste or New folder puts something in it.");

    int folder = ui->files_path[0] && ui->files_at < ui->files.count && ui->files.items[ui->files_at].dir;
    Hint places[] = { { accept_glyph(ui), "Open" }, { back_glyph(ui), "Back" } };
    /* On a file ✕ shows its actions too, as △ does: said once. */
    Hint list[] = { { accept_glyph(ui), "Open" }, { G_SQUARE, "Tick" }, { G_TRIANGLE, "Actions" }, { back_glyph(ui), "Back" } };
    ui->menu = ui->files_at;
    ui->list_top = ui->files_list_top;
    list_panel(ui, c, title, files_count(ui), files_row, ui->files.error ? "PC5 can't open this folder" : "This folder is empty",
               empty, ui->files_path[0] ? list + !folder : places, ui->files_path[0] ? 4 - !folder : 2, ui->files_path[0] ? 80 : 104);
    ui->files_list_top = ui->list_top;
    ui->menu = keep;
}

static void files_action_text(const Pc5Ui *ui, FilesAction action, const char **label, char *about, size_t size, char *value,
                              size_t value_size)
{
    value[0] = about[0] = 0;
    switch (action) {
    case FA_COPY: *label = "Copy"; pc5_copy(about, size, "Then Paste puts a copy in another folder"); break;
    case FA_CUT: *label = "Cut"; pc5_copy(about, size, "Then Paste moves it to another folder"); break;
    case FA_PASTE: {
        const char *slash = strrchr(ui->clip_from, '/');
        *label = "Paste";
        snprintf(about, size, "%s %d item%s from %.120s", ui->clip_cut ? "Moves" : "Copies", ui->clip.count,
                 ui->clip.count == 1 ? "" : "s", slash && slash[1] ? slash + 1 : ui->clip_from);
        snprintf(value, value_size, "%d", ui->clip.count);
        break;
    }
    case FA_RENAME: *label = "Rename"; pc5_copy(about, size, "Type its new name"); break;
    case FA_NEW_FOLDER: *label = "New folder"; pc5_copy(about, size, "An empty folder in this one"); break;
    case FA_DELETE: *label = "Delete"; pc5_copy(about, size, "Deleted for good: there is no undo"); break;
    case FA_TICK_ALL:
        *label = files_ticks(ui) == ui->files.count ? "Untick all" : "Tick all";
        pc5_copy(about, size, "Every item in this folder");
        break;
    }
}

static void files_menu_screen(Pc5Ui *ui, Pc5Canvas *c)
{
    FilesAction rows[FILES_ACTIONS];
    int count = files_actions(ui, rows), ticks = files_ticks(ui);
    char title[PC5_FILES_NAME + 32], about[200], value[32];
    const char *label = "";
    float extra;
    if (ticks) snprintf(title, sizeof(title), "%d items", ticks);
    else if (ui->files_at < ui->files.count) pc5_copy(title, sizeof(title), ui->files.items[ui->files_at].name);
    else pc5_copy(title, sizeof(title), "This folder");
    float x = drawer(ui, c, title);
    pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)x + 64, 182, C_DIM,
             files_writable(ui, ui->files_path) ? ui->files_path : "Read-only: PC5 changes nothing here", 760 - 128, 0);
    if (ui->menu < count) files_action_text(ui, rows[ui->menu], &label, about, sizeof(about), value, sizeof(value));
    else about[0] = 0;
    float pitch = rows_pitch(ui, 226, count, about, &extra);
    for (int i = 0; i < count; i++) {
        files_action_text(ui, rows[i], &label, about, sizeof(about), value, sizeof(value));
        drawer_row(ui, c, x, 226 + pitch * i + (i > ui->menu ? extra : 0), pitch + (i == ui->menu ? extra : 0), i == ui->menu,
                   label, about, value);
    }
    Hint list[] = { { accept_glyph(ui), "Choose" }, { back_glyph(ui), "Back" } };
    hints(ui, c, (int)x + 64, 1012, list, 2);
}

/* Copying, moving or deleting: how far, and Back to stop. */
static void files_working_screen(Pc5Ui *ui, Pc5Canvas *c)
{
    const Pc5FilesJob *job = &ui->files_job;
    float w = 1100, h = 420, x = (W - w) / 2, y = (H - h) / 2;
    char title[200], line[PC5_FILES_NAME + 64], have[32], all[32];
    int items = job->total_items ? (int)job->total_items : 1;
    dim(c, 150);
    panel(c, x, y, w, h);
    snprintf(title, sizeof(title), "%s", job->stop ? "Stopping"
                                    : job->op == PC5_FILES_DELETE ? "Deleting"
                                    : job->op == PC5_FILES_MOVE ? "Moving" : "Copying");
    pc5_text(c, &ui->fonts, PC5_FACE_HEAD, (int)x + 64, (int)y + 116, C_TEXT, title, (int)w - 128, 0);
    pc5_text(c, &ui->fonts, PC5_FACE_LABEL, (int)x + 64, (int)y + 186, C_TEXT, job->current[0] ? job->current : "Getting ready",
             (int)w - 128, 0);
    double fraction = job->total_bytes ? (double)job->done_bytes / (double)job->total_bytes : (double)job->done_items / items;
    progress_bar(c, x + 64, y + 236, w - 128, fraction, pc5_mix(C_ACCENT, C_TEXT, 60));
    if (job->total_bytes) {
        pc5_size_text(job->done_bytes, have, sizeof(have));
        pc5_size_text(job->total_bytes, all, sizeof(all));
        snprintf(line, sizeof(line), "%s of %s", have, all);
    } else {
        snprintf(line, sizeof(line), "%u of %u", (unsigned)job->done_items, (unsigned)job->total_items);
    }
    pc5_text(c, &ui->fonts, PC5_FACE_BODY, (int)x + 64, (int)y + 306, C_DIM, line, (int)w - 128, 0);
    if (!job->stop) {
        Hint list[] = { { back_glyph(ui), "Stop" } };
        hints(ui, c, (int)x + 64, (int)(y + h - 44), list, 1);
    }
}

static void files_draw(Pc5Ui *ui, Pc5Canvas *c)
{
    char title[PC5_FILES_NAME + 64], body[400], size[32];
    files_list_screen(ui, c);
    switch (ui->screen) {
    case S_FILES_MENU: files_menu_screen(ui, c); break;
    case S_FILES_CONFLICT: {
        const char *one = ui->clip.count ? strrchr(ui->clip.list[0], '/') : NULL;
        if (ui->clip.count == 1) snprintf(title, sizeof(title), "This folder has %.200s already", one ? one + 1 : "it");
        else snprintf(title, sizeof(title), "This folder has %d of the %d items already", ui->files_conflicts, ui->clip.count);
        confirm3_dialog(ui, c, title,
                        "Skip leaves those as they are. Keep both gives the new ones a name like \xe2\x80\x9cname (2)\xe2\x80\x9d. "
                        "Replace overwrites files and puts folders' contents together.",
                        "Skip", "Keep both", "Replace", NULL);
        break;
    }
    case S_FILES_DELETE: {
        int ticks = files_ticks(ui);
        if (ticks > 1) snprintf(title, sizeof(title), "Delete %d items?", ticks);
        else if (ticks == 1 || ui->files_at < ui->files.count) {
            int at = ui->files_at;
            for (int i = 0; ticks && i < ui->files.count; i++)
                if (ui->files_ticked[i]) at = i;
            snprintf(title, sizeof(title), "Delete %.200s?", at < ui->files.count ? ui->files.items[at].name : "it");
        } else pc5_copy(title, sizeof(title), "Delete?");
        pc5_size_text(ui->files_size.bytes, size, sizeof(size));
        snprintf(body, sizeof(body), "%s%u file%s and %u folder%s, %s. They're deleted for good: there is no backup and no undo.",
                 ui->files_size.partial ? "At least " : "", (unsigned)ui->files_size.files, ui->files_size.files == 1 ? "" : "s",
                 (unsigned)ui->files_size.folders, ui->files_size.folders == 1 ? "" : "s", size);
        confirm_dialog(ui, c, title, body, "Cancel", "Delete", 1, NULL);
        break;
    }
    case S_FILES_WORKING: files_working_screen(ui, c); break;
    default: break;
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
    case S_UPGRADE_ASK: {
        char title[120], body[400], how[200];
        update_how(ui, how, sizeof(how));
        snprintf(title, sizeof(title), "PC5 %s is available", ui->update.last.version);
        snprintf(body, sizeof(body), "%s, installs it and restarts. Your games, saves and settings stay. "
                 "Later: Settings \xe2\x80\xba System.", how);
        confirm_dialog(ui, c, title, body, "Update now", "Later", 0, NULL);
        break;
    }
    case S_UPGRADING: upgrade_screen(ui, c); break;
    case S_GAME_MENU: game_menu_screen(ui, c, ui->menu); break;
    case S_BUTTONS: buttons_screen(ui, c); break;
    case S_KEYS: buttons_screen(ui, c); keys_dialog(ui, c); break;
    case S_GRAPHICS_API: game_menu_screen(ui, c, ui->menu); graphics_dialog(ui, c); break;
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
    case S_COMPATIBILITY: compatibility_screen(ui, c); break;
    case S_SCAN: busy_dialog(ui, c, "Looking at the USB drives", NULL, 0); break;
    case S_SOURCES: {
        if (ui->update_for) {
            int index = pc5_library_find(&ui->library, ui->launch_id);
            char title[PC5_NAME + 32];
            Hint list[] = { { accept_glyph(ui), "Choose" }, { G_TRIANGLE, "Look again" }, { back_glyph(ui), "Back" } };
            snprintf(title, sizeof(title), "Update %s from USB", index >= 0 ? ui->library.games[index].title : "the game");
            list_panel(ui, c, title, ui->source_count, source_row, "No game folders found",
                       "Put the newer copy's folder (the one with the game's .exe in it) at the top of a USB drive formatted "
                       "as exFAT, plug the drive into the PS5, then look again.",
                       list, 3, 96);
            break;
        }
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
        list_panel(ui, c, ui->install_only ? "Install games from the INSTALL folder" : "Install games from USB",
                   ui->source_count, source_row, "No game folders found",
                   "Put each game's folder (the one with its .exe in it) at the top of a USB drive formatted as exFAT, "
                   "plug the drive into the PS5, then look again.",
                   list, n, 96);
        break;
    }
    case S_READING:
        snprintf(line, sizeof(line), "Reading %s", ui->sources[ui->menu].name);
        busy_dialog(ui, c, line, "Counting its files and finding its program", 0);
        break;
    case S_GAME_UPDATE_ASK: {
        int index = pc5_library_find(&ui->library, ui->launch_id);
        const char *name = index >= 0 ? ui->library.games[index].title : "the game";
        char title[300], body[500], size[32];
        pc5_size_text(ui->folder.bytes, size, sizeof(size));
        snprintf(title, sizeof(title), "Update %s?", name);
        snprintf(body, sizeof(body), "PC5 copies %s (%s) from the USB drive over the game's folder: the files it has replace "
                 "the game's, the others stay. Its saves, settings and options stay too.", ui->folder.name, size);
        confirm_dialog(ui, c, title, body, "Cancel", "Update", 0,
                       index >= 0 && ui->covers[index].state ? &ui->covers[index].picture : NULL);
        break;
    }
    case S_SAVES_ASK: {
        char title[300];
        snprintf(title, sizeof(title), "Put back the saves of %s?", ui->folder.name);
        review_screen(ui, c);
        confirm_dialog(ui, c, title, "They were kept when it was uninstalled: its saves, settings and options come back "
                       "with it. Start fresh: they stay kept, for later.", "Start fresh", "Put them back", 0, NULL);
        break;
    }
    case S_SEARCH:
        busy_dialog(ui, c, "Looking for the cover", "On Steam, GOG and others", 1);
        break;
    case S_PICTURE_SEARCH: {
        const char *where = "on Steam, GOG and others";
        if (ui->pick_query[0]) snprintf(line, sizeof(line), "\xe2\x80\x9c%s\xe2\x80\x9d, %s", ui->pick_query, where);
        else snprintf(line, sizeof(line), "%c%s", where[0] - 32, where + 1);
        busy_dialog(ui, c, ui->pick_kind == PICK_COVER ? "Looking for covers"
                           : ui->pick_kind == PICK_ICON ? "Looking for tile pictures" : "Looking for backgrounds", line, 1);
        break;
    }
    case S_PICTURE:
    case S_PICTURE_TYPING: picture_screen(ui, c); break;
    case S_REVIEW: review_screen(ui, c); break;
    case S_PROGRAMS: {
        Hint list[] = { { accept_glyph(ui), "Choose" }, { back_glyph(ui), "Back" } };
        if (ui->program_for) game_menu_screen(ui, c, game_row_find(&ui->controls, ui_game_kind(ui), GR_PROGRAM, NULL));
        list_panel(ui, c, "Which program is the game?", ui->folder.exe_count, program_row, "", "", list, 2, 104);
        break;
    }
    case S_INSTALL: install_screen(ui, c); break;
    case S_STOP:
        install_screen(ui, c);
        if (ui->install.update)
            confirm_dialog(ui, c, "Stop updating?", "The copy made so far is deleted and the game stays as it was. The USB "
                           "drive isn't changed.", "Keep updating", "Stop", 1, NULL);
        else if (ui->queue_installing)
            confirm_dialog(ui, c, "Stop installing?",
                           "What this game copied so far is deleted; games already installed stay. The USB drive isn't "
                           "changed. Press the back button to keep installing.",
                           "Stop this game", "Stop all", 1, NULL);
        else
            confirm_dialog(ui, c, "Stop installing?", "Everything copied so far is deleted. The USB drive isn't changed.",
                           "Keep installing", "Stop", 1, NULL);
        break;
    case S_UNINSTALL_LIST: {
        Hint list[] = { { accept_glyph(ui), ui->menu < ui->library.count ? "Uninstall" : "Delete" }, { back_glyph(ui), "Back" } };
        list_panel(ui, c, "Uninstall game", ui->library.count + ui->saves_count, game_row, "There are no games to uninstall",
                   "", list, 2, 112);
        break;
    }
    case S_SAVES_CONFIRM: {
        Hint list[] = { { accept_glyph(ui), "Delete" }, { back_glyph(ui), "Back" } };
        int at = ui->menu - ui->library.count;
        list_panel(ui, c, "Uninstall game", ui->library.count + ui->saves_count, game_row, "", "", list, 2, 112);
        snprintf(line, sizeof(line), "Delete the saves of %s?", at >= 0 && at < ui->saves_count ? ui->saves[at].title : "the game");
        confirm_dialog(ui, c, line, "Its saves and settings, kept when it was uninstalled, are deleted. There is no backup "
                       "and no undo.", "Cancel", "Delete", 1, NULL);
        break;
    }
    case S_UNINSTALL_CONFIRM: {
        const Pc5Game *game = &ui->library.games[ui->menu];
        const Pc5Picture *thumb = ui->covers[ui->menu].state ? &ui->covers[ui->menu].picture : NULL;
        if (ui->uninstall_back == S_GAME_MENU)
            game_menu_screen(ui, c, game_row_find(&ui->controls, ui_game_kind(ui), GR_UNINSTALL, NULL));
        snprintf(line, sizeof(line), "Uninstall %s?", game->title);
        if (pc5_remove_can_keep(game, &ui->library))
            confirm3_dialog(ui, c, line,
                            "Keep saves: the game goes, its saves and settings stay, and installing it again puts them back. "
                            "Delete all: the game, its saves and settings go, with no backup and no undo.",
                            "Cancel", "Keep saves", "Delete all", thumb);
        else
            confirm_dialog(ui, c, line,
                           "This deletes the game and all its saves and settings. There is no backup and no undo.",
                           "Cancel", "Uninstall", 1, thumb);
        break;
    }
    case S_REMOVING:
        snprintf(line, sizeof(line), ui->removing_kind == 2 ? "Deleting the saves of %s" : "Uninstalling %s", ui->removing_title);
        {
            char count[64];
            snprintf(count, sizeof(count), "%u files and folders deleted", (unsigned)ui->remove.removed);
            busy_dialog(ui, c, line, count, 0);
        }
        break;
    case S_MESSAGE: message_dialog(ui, c); break;
    case S_REPORT: library_screen(ui, c); report_screen(ui, c); break;
    case S_FILES:
    case S_FILES_MENU:
    case S_FILES_TYPING:
    case S_FILES_CONFLICT:
    case S_FILES_DELETE:
    case S_FILES_WORKING:
        files_draw(ui, c);
        break;
    case S_INSTALL_ASK: {
        char body[PC5_PATH + 300];
        snprintf(body, sizeof(body),
                 "%d game%s waiting in %.200s. Install %s now? A game installed from there is moved into PC5's "
                 "library, out of the INSTALL folder.", ui->install_waiting, ui->install_waiting == 1 ? " is" : "s are",
                 ui->install_dir, ui->install_waiting == 1 ? "it" : "them");
        confirm_dialog(ui, c, "Games in PC5's INSTALL folder", body, "Later", "Install", 0, NULL);
        break;
    }
    case S_TILE_ART:
        game_menu_screen(ui, c, ui->menu);
        busy_dialog(ui, c, "Updating the home screen tile", "Its picture and background", 0);
        break;
    case S_REPORT_CHECK: message_dialog(ui, c); busy_dialog(ui, c, "Checking for a newer PC5", "A newer one may have fixed it", 0); break;
    case S_REPORT_UPDATE: {
        char title[120], body[500], how[200];
        snprintf(title, sizeof(title), "Update to PC5 %s first?", ui->update.last.version);
        if (offer_update(ui)) update_how(ui, how, sizeof(how));
        else snprintf(how, sizeof(how), "You have %s", PC5_VERSION);
        snprintf(body, sizeof(body), "This problem may already be fixed in it. Update with Update now (PC5's own updater) "
                 "or from ProsperoStore, try the game again, and report it if it still happens. %s.", how);
        message_dialog(ui, c);
        confirm_dialog(ui, c, title, body, "Update now", "Report anyway", 0, NULL);
        break;
    }
    case S_LAUNCH: {
        int index = pc5_library_find(&ui->library, ui->launch_id);
        snprintf(line, sizeof(line), "Starting %s", index >= 0 ? ui->library.games[index].title : "the game");
        if (ui->runtime_state == 1) {
            char count[96];
            snprintf(count, sizeof(count), "Setting up .NET for your games, once: %u of %u", (unsigned)ui->runtime.done,
                     (unsigned)ui->runtime.total);
            busy_dialog(ui, c, line, count, 0);
        } else {
            busy_dialog(ui, c, line, NULL, 0);
        }
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
    c = (Pc5Canvas){ card->px, cw, ch, cw, 0, 0 };
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
