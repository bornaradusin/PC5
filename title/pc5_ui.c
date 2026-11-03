/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: the launcher's screens (pc5_ui.h). */
#include "pc5_ui.h"
#include "pc5_backgrounds.h"
#include "pc5_identity.h"
#include "pc5_ftp.h"
#include "pc5_web.h"
#include "pc5_files.h"
#include "pc5_game.h"
#include "pc5_home.h"
#include "pc5_i18n.h"
#include "pc5_tile.h"
#include "pc5_image.h"
#include "pc5_library.h"
#include "pc5_logs.h"
#include "pc5_msi.h"
#include "pc5_qr.h"
#include "pc5_report.h"
#include "pc5_settings.h"
#include "pc5_sound.h"
#include "pc5_video.h"
#include "pc5_compat.h"
#include "pc5_appearance.h"
#include "pc5_look.h"
#include "pc5_steam.h"
#include "pc5_textdoc.h"
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
/* Home's covers along the bottom, and the Games tab's grid. */
enum { COVER_W = 200, COVER_H = 300, COVER_GAP = 18, ROW_TOP = 612, CHOSEN_W = 222, CHOSEN_H = 333, GRID_COLUMNS = 7 };


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

/* The library's tabs (Settings and Install open their pages), and the Library tab's lists. */
enum { TAB_HOME, TAB_LIBRARY, TAB_COMPATIBILITY, TAB_INSTALL, TAB_SETTINGS, TABS };
static const char *const tab_names[TABS] = { N_("Home"), N_("Library"), N_("Compatibility"), N_("Install"), N_("Settings") };
/* The Library tab's filters, its pills (L2 and R2 move between them). */
enum { LIST_ALL, LIST_RECENT, LIST_FAVOURITES, LIST_USB, LIST_ATTENTION, LISTS };
static const char *const list_names[LISTS] = { N_("All"), N_("Recently played"), N_("Favourites"), N_("USB drive"), N_("Needs attention") };

/* The USB drives and extended storage drives at most (Pc5UiConfig.usb_roots). */
#define PC5_DRIVES 12

typedef enum Screen {
    S_START, S_COVERS, S_LIBRARY, S_SETTINGS, S_ABOUT, S_SCAN, S_SOURCES, S_READING, S_SEARCH, S_REVIEW,
    S_PROGRAMS, S_INSTALL, S_STOP, S_UNINSTALL_LIST, S_UNINSTALL_CONFIRM, S_REMOVING, S_MESSAGE, S_LAUNCH,
    S_UPDATE, S_CHECKING, S_GAME_MENU, S_GAME_READING, S_QUEUE_READ, S_QUEUE, S_BUTTONS, S_KEYS, S_REPORT,
    S_UPGRADE_ASK, S_UPGRADING, S_GAME_UPDATE_ASK, S_SAVES_ASK, S_SAVES_CONFIRM, S_PICTURE_SEARCH, S_PICTURE,
    S_PICTURE_TYPING, S_COMPATIBILITY, S_WEB_QR, S_GRAPHICS_API, S_REPORT_CHECK, S_REPORT_UPDATE, S_FILES, S_FILES_MENU,
    S_FILES_TYPING, S_FILES_CONFLICT, S_FILES_DELETE, S_FILES_WORKING, S_TILE_ART, S_INSTALL_ASK, S_SETTINGS_TYPING, S_INSTALL_WHERE,
    S_PLAY_WHICH, S_FILES_EDIT, S_FILES_EDIT_MENU, S_FILES_EDIT_TYPING, S_FILES_EDIT_LEAVE,
    S_MUSIC_IMPORT, S_VIDEO_IMPORT, S_PHONE_QR, S_COMPAT_SEARCH, S_COMPAT_REPORT, S_MEDIA_OPTIONS, S_LOOK_CONFIRM, S_LOOK_WORK,
    S_MEDIA_DELETE, S_COMPAT_SHARE, S_SETTING_ABOUT
} Screen;

static const char *const screen_names[] = { "start", "covers", "library", "settings", "about", "scan", "sources",
                                            "reading", "search", "review", "programs", "install", "stop",
                                            "uninstall-list", "uninstall-confirm", "removing", "message", "launch",
                                            "update", "checking", "game-menu", "game-reading", "queue-read", "queue",
                                            "buttons", "keys", "report", "upgrade-ask", "upgrading", "game-update-ask",
                                            "saves-ask", "saves-confirm", "picture-search", "picture", "picture-typing",
                                            "compatibility", "web-qr", "graphics-api", "report-check", "report-update", "files",
                                            "files-menu", "files-typing", "files-conflict", "files-delete",
                                            "files-working", "tile-art", "install-ask", "settings-typing", "install-where",
                                            "play-which", "files-edit", "files-edit-menu", "files-edit-typing",
                                            "files-edit-leave", "music-import", "video-import", "phone-qr", "compat-search", "compat-report", "media-options", "look-confirm", "look-work",
                                            "media-delete", "compat-share", "setting-about" };

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
#define PC5_COMPATIBILITY_NEW "https://github.com/bornaradusin/PC5/discussions/new?category=game-compatibility"

/* Settings: its rows, on seven pages. */
typedef enum SettingRow {
    SR_INSTALL, SR_UNINSTALL, SR_COMPATIBILITY, SR_MAINTENANCE, SR_GAMES, SR_OVERLAY, SR_SYSTEM, SR_ABOUT,
    SR_FPS, SR_FRAME_TIME, SR_CPU_USAGE, SR_GPU_USAGE, SR_CPU_TEMPERATURE, SR_SOC_TEMPERATURE, SR_MEMORY, SR_FPS_CORNER,
    SR_OVERLAY_SIZE, SR_VIBRATION, SR_LIGHT, SR_TRIGGERS, SR_TRIGGER_RUMBLE, SR_TRIGGER_RESISTANCE,
    SR_STARTUP_SOUND, SR_STARTUP_VOLUME, SR_KEEP_LOGS, SR_PICTURES, SR_CHECK_ON_OPEN, SR_CHECK_NOW, SR_UPDATE_NOW,
    SR_FILES, SR_FTP, SR_WEB, SR_PHONE_LIBRARY, SR_NETWORK, SR_REMOTE_USER, SR_REMOTE_PASSWORD, SR_LANGUAGE, SR_KEYBOARD,
    SR_HIGH_REFRESH,
    SR_SOURCE   /* a picture source: SR_SOURCE + its index (pc5_cover.h) */
} SettingRow;

enum { PAGE_ROWS = 9, PAGE_GAMES = 1, PAGE_SYSTEM = 2, PAGE_OVERLAY = 3, PAGE_PICTURES = 4, PAGE_MAINTENANCE = 5,
       PAGE_NETWORK = 6, SETTING_PAGES = 7 };
static const char *const page_titles[SETTING_PAGES] = { N_("Settings"), N_("In games"), N_("System"), N_("Overlay"), N_("Picture sources"),
                                                        N_("Game maintenance"), N_("Network") };
/* Where each page's row is on the first page (Back goes there); System's rows move with its
 * startup sound (system_row). */
enum { ROW_MAINTENANCE = 0, ROW_GAMES = 1, ROW_OVERLAY = 2, ROW_FILES = 3, ROW_SYSTEM = 4, ROW_NETWORK = 5, ROW_ABOUT = 6 };
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
        rows[n++] = SR_GPU_USAGE;
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
        rows[n++] = SR_HIGH_REFRESH;
    } else if (page == PAGE_SYSTEM) {
        rows[n++] = SR_STARTUP_SOUND;
        if (strcmp(set->startup_sound, "off")) rows[n++] = SR_STARTUP_VOLUME;
        rows[n++] = SR_KEEP_LOGS;
        rows[n++] = SR_PICTURES;
        rows[n++] = SR_CHECK_ON_OPEN;
        rows[n++] = SR_CHECK_NOW;
        if (offer_update) rows[n++] = SR_UPDATE_NOW;
        rows[n++] = SR_KEYBOARD;
        rows[n++] = SR_LANGUAGE;
    } else if (page == PAGE_MAINTENANCE) {
        rows[n++] = SR_INSTALL;
        rows[n++] = SR_UNINSTALL;
    } else if (page == PAGE_NETWORK) {
        rows[n++] = SR_FTP;
        rows[n++] = SR_WEB;
        rows[n++] = SR_REMOTE_USER;
        rows[n++] = SR_REMOTE_PASSWORD;
        rows[n++] = SR_PHONE_LIBRARY;
    } else {
        rows[n++] = SR_MAINTENANCE;
        rows[n++] = SR_GAMES;
        rows[n++] = SR_OVERLAY;
        rows[n++] = SR_FILES;
        rows[n++] = SR_SYSTEM;
        rows[n++] = SR_NETWORK;
        rows[n++] = SR_ABOUT;
    }
    return n;
}

/* The game's groups and media submenus each have their own page. */
typedef enum GameRow {
    GR_PLAY, GR_CONTROLLER, GR_BUTTONS, GR_POINTER, GR_GRAPHICS, GR_SOUND, GR_APPEARANCE, GR_MANAGE,
    GR_GRAPHICS_API, GR_FRAMEGEN, GR_RESOLUTION, GR_ASPECT, GR_MEMORY, GR_VIDEO_MEMORY, GR_CPU_SHARING,
    GR_COVER, GR_BACKGROUND, GR_HOME, GR_ICON, GR_MUSIC, GR_MUSIC_CLEAR,
    GR_BACKGROUND_IMAGE, GR_BACKGROUND_VIDEO, GR_VIDEO_CLEAR, GR_MEDIA_OPTIONS, GR_MUSIC_FILE, GR_VIDEO_FILE,
    GR_MEDIA, GR_LOADING, GR_LOADING_FILE, GR_LOADING_BACKGROUND, GR_LOADING_CLEAR,
    GR_PROGRAM, GR_STEAM, GR_FILES, GR_UPDATE, GR_REPORT, GR_UNINSTALL
} GameRow;
enum { GAME_ROWS = 8, GAME_PAGES = 9, GP_MAIN = 0, GP_GRAPHICS = 1, GP_APPEARANCE = 2, GP_MANAGE = 3, GP_BACKGROUND = 4,
       GP_MUSIC = 5, GP_VIDEO = 6, GP_MEDIA = 7, GP_LOADING = 8 };
static const char *const game_page_titles[GAME_PAGES] = { "", N_("Graphics"), N_("Appearance"), N_("Manage"), N_("Background"),
                                                        N_("Library music"), N_("Background video"),
                                                        N_("Media settings and backup"), N_("Loading screen") };
/* The parent row that opens each page; Back returns to that row. */
static int setting_about(Pc5Ui *ui, GameRow row, const char *lines[6]);
static const GameRow game_page_rows[GAME_PAGES] = { GR_PLAY, GR_GRAPHICS, GR_APPEARANCE, GR_MANAGE, GR_BACKGROUND,
                                                  GR_MUSIC, GR_BACKGROUND_VIDEO, GR_MEDIA, GR_LOADING };
/* What a game is, for its menu's rows. */
enum { GAME_32BIT = 1, GAME_VULKAN = 2, GAME_PC5 = 4, GAME_API = 8, GAME_STEAM = 16, GAME_DXGI = 32, GAME_MEDIA = 64 };

/* kind: GAME_32BIT for Memory, GAME_VULKAN for Resolution, GAME_PC5 (PC5 installed it) for Game files and Update,
 * GAME_API (it draws with DirectDraw or Direct3D) for Graphics API, GAME_STEAM (it has Steam's library) for
 * Steam emulator. */
static int game_rows(const Pc5Controls *controls, int kind, int page, GameRow rows[GAME_ROWS])
{
    int n = 0;
    switch (page) {
    case GP_BACKGROUND:
        rows[n++] = GR_BACKGROUND_IMAGE;
        if (kind & GAME_MEDIA) rows[n++] = GR_BACKGROUND_VIDEO;
        break;
    case GP_MUSIC:
        rows[n++] = GR_MUSIC_FILE;
        rows[n++] = GR_MUSIC_CLEAR;
        break;
    case GP_VIDEO:
        rows[n++] = GR_VIDEO_FILE;
        rows[n++] = GR_VIDEO_CLEAR;
        break;
    case GP_GRAPHICS:
        if (kind & GAME_API) rows[n++] = GR_GRAPHICS_API;
        rows[n++] = GR_FRAMEGEN;
        if (kind & GAME_VULKAN) rows[n++] = GR_RESOLUTION;
        rows[n++] = GR_ASPECT;
        if (kind & GAME_32BIT) rows[n++] = GR_MEMORY;
        if (kind & (GAME_32BIT | GAME_DXGI)) rows[n++] = GR_VIDEO_MEMORY;
        rows[n++] = GR_CPU_SHARING;
        break;
    case GP_APPEARANCE:
        rows[n++] = GR_MEDIA;
        /* Music, a video and their settings are kept in the game's own folder: only a game
         * PC5 installed in a prefix of its own has one (others may share the default one). */
        if (kind & GAME_MEDIA) rows[n++] = GR_MUSIC;
        rows[n++] = GR_HOME;
        rows[n++] = GR_ICON;
        break;
    case GP_MEDIA:
        rows[n++] = GR_COVER;
        rows[n++] = GR_BACKGROUND;
        /* The loading picture is kept in the game's own folder too. */
        if (kind & GAME_MEDIA) rows[n++] = GR_LOADING;
        if (kind & GAME_MEDIA) rows[n++] = GR_MEDIA_OPTIONS;
        break;
    case GP_LOADING:
        rows[n++] = GR_LOADING_FILE;
        rows[n++] = GR_LOADING_BACKGROUND;
        rows[n++] = GR_LOADING_CLEAR;
        break;
    case GP_MANAGE:
        rows[n++] = GR_PROGRAM;
        if (kind & GAME_STEAM) rows[n++] = GR_STEAM;
        if (kind & GAME_PC5) rows[n++] = GR_FILES;
        if (kind & GAME_PC5) rows[n++] = GR_UPDATE;
        rows[n++] = GR_REPORT;
        rows[n++] = GR_UNINSTALL;
        break;
    default:
        rows[n++] = GR_PLAY;
        rows[n++] = GR_CONTROLLER;
        if (controls->mode == PC5_PAD_KEYBOARD) rows[n++] = GR_BUTTONS;
        rows[n++] = GR_POINTER;
        rows[n++] = GR_GRAPHICS;
        rows[n++] = GR_SOUND;
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
typedef enum PadRowKind { PR_STICK, PR_DIRECTION, PR_SPEED, PR_SLOW, PR_SLOW_PERCENT, PR_BUTTON, PR_TOUCH_MODE, PR_RESET,
                          PR_SAVE_MAPPING, PR_USE_MAPPING } PadRowKind;
typedef struct PadRow { PadRowKind kind; int index; } PadRow;
enum { PAD_ROWS = 40, PAD_VISIBLE = 10, PAD_PITCH = 74, KEY_COLS = 8 };

static int pad_rows(const Pc5Controls *controls, PadRow rows[PAD_ROWS], int has_mapping)
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
    for (int button = 0; button < PC5_BUTTONS; button++)
        if (button != PC5_TOUCHPAD) rows[n++] = (PadRow){ PR_BUTTON, button };
    /* The touchpad: what it is, then its click (one button, or a mouse's), or its halves and swipes;
     * and a two-finger click, PC5's on-screen keyboard unless it is a key. */
    rows[n++] = (PadRow){ PR_TOUCH_MODE, 0 };
    if (controls->touch_mode == 1)
        for (int t = 0; t < PC5_TOUCH_ZONES; t++) rows[n++] = (PadRow){ PR_BUTTON, PC5_TOUCH_FIRST + t };
    else
        rows[n++] = (PadRow){ PR_BUTTON, PC5_TOUCHPAD };
    rows[n++] = (PadRow){ PR_BUTTON, PC5_TWO_FINGER };
    rows[n++] = (PadRow){ PR_RESET, 0 };
    /* The player's own mapping, kept for every game (pc5/mapping.txt): saved from this one, and
     * put on this one when there is one. */
    rows[n++] = (PadRow){ PR_SAVE_MAPPING, 0 };
    if (has_mapping) rows[n++] = (PadRow){ PR_USE_MAPPING, 0 };
    return n;
}

static void percent_text(int percent, char *out, size_t size)
{
    if (!percent) pc5_copy(out, size, T("Off"));
    /* TRANSLATORS: a percentage (Vibration 50%); put a space before % where your language does. */
    else pc5_tf(out, size, "%d%%", percent);
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
    char name[128], path[PC5_PATH];
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

/* The update check as PC5 opens, on a thread of its own so the library shows at once: its
 * answer is taken on the screens' thread (update_check_follow), and a newer PC5 is offered
 * once the library is showing. */
typedef struct UpdateJob {
    pthread_t thread;
    int running, offer;
    atomic_int done;
    Pc5Http http;
    Pc5Update result;
} UpdateJob;

struct Pc5Ui {
    Pc5UiConfig config;
    char root[PC5_PATH], template_prefix[PC5_PATH], app[PC5_PATH];
    /* Starting a .NET game the first time: the runtime's copy (pc5_game.h). 0: not looked
     * at yet, 1: copying, 2: nothing (more) to do. */
    Pc5Runtime runtime;
    Pc5Matrix *launch_matrix;     /* the loading picture while PC5 hands a game over (S_LAUNCH) */
    int launch_ready;             /* the game made ready: the loading picture fades in, then it starts */
    int loading_saved;            /* the last loading_import saved the picture */
    uint64_t launch_ready_at;
    int runtime_state;
    /* Starting a game: its redistributables the first time (pc5_msi.h). 0: not looked at yet,
     * 1: installing them (the busy line names them), 2: nothing (more) to do. */
    int redist_state;
    char redist_name[PC5_NAME];
    int steam_dlls;                /* the game whose menu is open: its steam_api DLLs (pc5_steam.h) */
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

    char message_title[320], message_body[1400];
    int message_good;
    int message_report;               /* the message is about a kept log: Triangle reports it */
    Pc5LogIssue report;               /* Report on GitHub: the log, its game, the form's link as a QR code */
    char report_game[PC5_NAME], report_url[PC5_REPORT_URL];
    Pc5Qr report_qr;
    Pc5Qr coffee_qr;                  /* About PC5: Buy Me a Coffee's page, made once */
    Pc5Compat compatibility;
    int compat_initialized, compat_at, compat_filter, compat_detail, compat_scroll;
    char compat_query[128];
    uint64_t compat_refresh_at;
    Pc5Appearance appearance;
    int media_row, files_look, media_delete_video;
    Pc5Look look;
    char look_source[PC5_PATH];
    Pc5Qr compatibility_qr;           /* Game maintenance › Game compatibility list: the wiki's page, made once */
    Pc5Qr web_qr;                     /* Network › Web page (Triangle): its address, made as it is shown */
    char web_url[160];
    int phone_kind, phone_temporary, phone_library, phone_manager_pending, phone_import_ok;
    char phone_source[PC5_PATH];
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
    UpdateJob update_job;
    Pc5LogIssue issue;             /* what went wrong last time, told once the library shows */
    int issue_pending;
    Pc5InstallPhase speed_phase;
    int program_for;               /* S_PROGRAMS for: 0 the review, 1 the game in launch_id */
    /* The game's programs offered at Play (pc5_programs_load), for S_PROGRAMS' marks, and
     * S_PLAY_WHICH's choices: the game's program first, then those; back to play_back. */
    Pc5Offered offered[PC5_OFFERED];
    int offered_count;
    Pc5Offered play[PC5_OFFERED + 1];
    int play_count;
    int play_back;
    Screen uninstall_back;         /* where Cancel in S_UNINSTALL_CONFIRM goes */
    int update_for;                /* S_SCAN, S_SOURCES and S_READING choose a newer copy of the game in launch_id */
    /* PC5's INSTALL folder, in its own folder ("" when PC5's folder isn't found): games put there
     * over FTP are offered when PC5 opens (install_waiting of them), and listed with the USB
     * drives'; one installed from there is moved into the library (pc5_install_take_from). */
    char install_dir[PC5_PATH];
    /* The USB drives' PC5/INSTALL folders (pc5_usb_install_dir), "" where there is no drive;
     * how many drives were there when last looked (usb_seen, -1 never), and when (usb_looked). */
    char usb_install[PC5_DRIVES][PC5_PATH];
    int usb_seen;
    uint64_t usb_looked;
    /* S_INSTALL_WHERE: for the review's game (0) or the queue (1); the USB drive offered
     * (where_usb, a USB root) and the one chosen (install_usb, "" for the console). Left and
     * right go through the console and every drive a game can be installed on (where_drive:
     * indexes in usb_roots, the one offered first, where_count of them). */
    int where_for;
    char where_usb[PC5_PATH], install_usb[PC5_PATH];
    int where_drive[PC5_DRIVES], where_count;
    char pc5_folder[PC5_PATH];     /* PC5's own folder on the console's drives, "" when not found */
    int install_waiting, install_looked, install_scanned;
    /* The INSTALL folder's FTP server: why it didn't start, and the uploads already asked about. */
    char ftp_error[PC5_ERROR];
    int typing_row;                /* S_SETTINGS_TYPING: the row (SR_REMOTE_USER, SR_REMOTE_PASSWORD) */
    unsigned ftp_uploads_seen;
    char web_error[PC5_ERROR];
    unsigned web_uploads_seen;
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

    /* The library's tabs along the top (TAB_*), focus (Down enters, Up leaves), and
     * what the top of Home shows for the chosen game: its details (genres, a line, its logo),
     * its settings, loaded once it stays chosen a moment. */
    int tab, focus;
    int hero_for;                  /* the game they are for (-1: none yet) */
    unsigned hero_generation;      /* the worker's count when they were read */
    Pc5Details hero;
    Pc5Image hero_logo;
    Pc5Controls hero_controls;
    /* The Library tab: its lists (LISTS_*) of library indexes, the list and place chosen. */
    int lists[LISTS][PC5_MAX_GAMES], list_count[LISTS], list_at, list_item[LISTS];
    uint8_t favourite[PC5_MAX_GAMES];   /* prefixes/<prefix>/pc5/favourite.txt is there; read with the library */
    /* PC5's pictures (the app's pc5/ui/: logo.png, controller.png, hero.png, cover.png), each
     * left empty when there is none: drawn instead. */
    Pc5Image art_logo, art_controller, art_hero, art_cover, art_boot, art_sweep;
    Pc5Image art_icons[12];        /* ICON_*: the buttons and marks (pc5/ui/btn-*.png, icon-*.png) */
    int hero_backdrop;             /* the backdrop is PC5's own picture (art_hero), not the glow */
    uint64_t boot_at;              /* the first frame's time, for the logo as PC5 opens (config.boot) */

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
    int files_music;               /* read-only file selection for library music */
    int files_loading;             /* read-only file selection for a game's loading picture */
    char music_id[PC5_ID], music_want[PC5_PATH], music_playing[PC5_PATH];
    uint64_t music_since;
    Pc5MusicImport music_import;
    Pc5Video video;
    Pc5VideoImport video_import;
    int files_video;
    char video_id[PC5_ID], video_want[PC5_PATH];
    uint64_t video_report_at;
    unsigned video_report_shown;
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
    /* A text file opened from the file manager, changed a line at a time (pc5_textdoc.h). */
    Pc5TextDoc edit;
    char edit_path[PC5_PATH];
    int edit_read_only;            /* in a read-only place: shown, never changed */
    int edit_at, edit_top;         /* the line chosen, and the first one shown */
    int edit_typing;               /* S_FILES_EDIT_TYPING: 0 the line chosen, 1 a new one below it, 2 above it */

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
           (ui->library.games[index].pc5 && ui->library.games[index].prefix[0] &&
                    strcmp(ui->library.games[index].prefix, "default") ? GAME_MEDIA : 0) |
           (pc5_graphics_choices(ui->library.games[index].api, choices) > 1 ? GAME_API : 0) |
           (ui->steam_dlls > 0 ? GAME_STEAM : 0) |
           (ui->library.games[index].bits != 32 && ui->library.games[index].api >= PC5_API_D3D10 &&
                    ui->library.games[index].api <= PC5_API_D3D12 ? GAME_DXGI : 0);
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

/* Start and stop must use the same provider: an extended-only host need not
 * supply the legacy callback, and may use a different player when both exist. */
static void library_music_stop(Pc5Ui *ui)
{
    if (ui->platform.music_options) ui->platform.music_options(ui->platform.context, "", 0, 0);
    else if (ui->platform.music) ui->platform.music(ui->platform.context, "");
    ui->music_playing[0] = 0;
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
    if (screen != S_LAUNCH && ui->launch_matrix) {
        pc5_matrix_destroy(ui->launch_matrix);
        ui->launch_matrix = NULL;
    }
    if (ui->runtime_state == 1) pc5_runtime_end(&ui->runtime);
    if (screen == S_LAUNCH || ui->runtime_state == 1) ui->runtime_state = 0;
    if (screen == S_LAUNCH) ui->redist_state = 0, ui->launch_ready = 0;
    ui->screen = screen;
    ui->entered = ui->now;
    /* The backgrounds' worker waits while these screens use the network, so they don't wait for it. */
    atomic_store(&ui->backgrounds.pause,
                 screen == S_SCAN || screen == S_SOURCES || screen == S_READING || screen == S_SEARCH ||
                 screen == S_REVIEW || screen == S_PROGRAMS || screen == S_INSTALL || screen == S_STOP ||
                 screen == S_QUEUE_READ || screen == S_QUEUE || screen == S_CHECKING || screen == S_UPGRADE_ASK ||
                 screen == S_UPGRADING || screen == S_GAME_UPDATE_ASK || screen == S_SAVES_ASK || screen == S_REPORT ||
                 screen == S_PICTURE_SEARCH || screen == S_PICTURE || screen == S_PICTURE_TYPING ||
                 screen == S_FILES_WORKING || screen == S_MUSIC_IMPORT || screen == S_VIDEO_IMPORT);
    ui->drawn = 0;
    ui->dirty = 1;
    say(ui, "PC5 screen=%s", screen_names[screen]);
}

/* title and body in the player's language: T() or pc5_tf's, or English from elsewhere (an
 * install's error), which TX translates here. */
static void message(Pc5Ui *ui, int good, const char *title, const char *body, Screen back_to)
{
    pc5_copy(ui->message_title, sizeof(ui->message_title), TX(title));
    pc5_copy(ui->message_body, sizeof(ui->message_body), TX(body));
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

/* Where Home's row of covers is scrolled to: the chosen game first, except towards the end, where
 * it stops with the last three in view (all of them, with fewer) and the choice moves along them. */
static float row_scroll(const Pc5Ui *ui)
{
    int last_start = ui->library.count > 3 ? ui->library.count - 3 : 0;
    return (float)(ui->selected < last_start ? ui->selected : last_start);
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
    for (int i = 0; i < ui->library.count; ++i)
        if (ui->library.games[i].pc5) pc5_look_recover(ui->root, ui->library.games[i].prefix);
    ui->library_loads++;
    ui->selected = 0;
    if (keep[0]) {
        int found = pc5_library_find(&ui->library, keep);
        if (found >= 0) ui->selected = found;
    }
    ui->scroll = row_scroll(ui);
    for (int i = 0; i < ui->library.count; i++) {
        char path[PC5_PATH];
        ui->favourite[i] = ui->library.games[i].prefix[0] &&
                           !pc5_format(path, sizeof(path), "%s/prefixes/%s/pc5/favourite.txt", ui->root,
                                       ui->library.games[i].prefix) && pc5_is_file(path);
    }
    ui->hero_for = -1;
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

static int64_t unix_now(Pc5Ui *ui, int *offset_s);

static void remember_played(Pc5Ui *ui, const char *id)
{
    char path[PC5_PATH];
    int index = pc5_library_find(&ui->library, id);

    /* when, for the Library tab's Recently played */
    if (index >= 0 && ui->library.games[index].prefix[0] &&
        !pc5_format(path, sizeof(path), "%s/prefixes/%s/pc5/played.txt", ui->root, ui->library.games[index].prefix)) {
        char when[32];
        int n = snprintf(when, sizeof(when), "%lld\n", (long long)unix_now(ui, NULL));
        pc5_write_file(path, when, (size_t)n);
    }

    if (!pc5_format(path, sizeof(path), "%s/pc5", ui->root) && !pc5_mkdirs(path) &&
        !pc5_format(path, sizeof(path), "%s/pc5/last.txt", ui->root))
        pc5_write_file(path, id, strlen(id));
}

/* ---- creating and the frame clock ---------------------------------------------------------- */

/* PC5's own pictures, beside its fonts (the app's pc5/ui/), each left empty when it isn't there. */
enum { ICON_CROSS, ICON_CIRCLE, ICON_TRIANGLE, ICON_SQUARE, ICON_L1, ICON_R1, ICON_R3, ICON_OPTIONS, ICON_STAR, ICON_USB,
       ICON_WARNING, ICON_GEAR, ICONS };

static void load_art(Pc5Ui *ui, const char *fonts)
{
    static const char *const names[] = { "logo.png", "controller.png", "hero.jpg", "cover.jpg", "boot.jpg", "sweep.png",
                                         "btn-cross.png", "btn-circle.png", "btn-triangle.png", "btn-square.png",
                                         "btn-l1.png", "btn-r1.png", "btn-r3.png", "btn-options.png", "icon-star.png",
                                         "icon-usb.png", "icon-warning.png", "icon-gear.png" };
    Pc5Image *images[6 + ICONS] = { &ui->art_logo, &ui->art_controller, &ui->art_hero, &ui->art_cover, &ui->art_boot, &ui->art_sweep };
    for (int i = 0; i < ICONS; i++) images[6 + i] = &ui->art_icons[i];
    char dir[PC5_PATH], path[PC5_PATH];
    pc5_copy(dir, sizeof(dir), fonts ? fonts : "");
    char *slash = strrchr(dir, '/');
    if (!slash) return;
    *slash = 0;
    for (int i = 0; i < 6 + ICONS; i++) {
        Pc5Buf bytes = { 0 };
        if (!pc5_format(path, sizeof(path), "%s/ui/%s", dir, names[i]) && !pc5_read_file(path, &bytes, 16u << 20) &&
            pc5_image_decode(bytes.data, bytes.len, images[i]))
            *images[i] = (Pc5Image){ 0 };
        pc5_buf_free(&bytes);
    }
}

static int background_prepare(const Pc5Image *small, uint32_t *out);

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
    /* PC5's own picture behind a game without a background, shaded as one is. */
    if (ui->art_hero.px && !background_prepare(&ui->art_hero, ui->composed)) {
        ui->hero_backdrop = 1;
        ui->composed_ready = 1;
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
        pc5_tf(out, size, "You have %s, the version before: PC5 downloads only what changed (%s)", PC5_VERSION, bytes);
        return;
    }
    pc5_size_text(u->asset_size, bytes, sizeof(bytes));
    if (u->delta_from[0] && pc5_version_compare(PC5_VERSION, u->delta_from) < 0)
        pc5_tf(out, size, "You have %s, more than one version behind: PC5 downloads the whole app (%s)", PC5_VERSION,
                 bytes);
    else
        pc5_tf(out, size, "You have %s: PC5 downloads the whole app (%s)", PC5_VERSION, bytes);
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

static void language_apply(Pc5Ui *ui);

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
    load_art(ui, config->fonts);
    ui->hero_for = -1;
    make_backdrop(ui);
    for (int k = 0; k < 3; k++) ui->glow[k] = ui->glow_target[k] = (float)((C_DEFAULT_TINT >> (16 - 8 * k)) & 255);
    ui->choice = -1;
    ui->queue_review = -1;
    ui->screen = S_START;
    ui->dirty = 1;
    pc5_settings_load(ui->root, &ui->settings);
    language_apply(ui);
    pc5_update_load(ui->root, &ui->update);
    return ui;
}

void pc5_ui_destroy(Pc5Ui *ui)
{
    if (!ui) return;
    pc5_matrix_destroy(ui->launch_matrix);
    ui->launch_matrix = NULL;
    library_music_stop(ui);
    atomic_store(&ui->look.stop, 1); pc5_look_end(&ui->look);
    if (ui->compat_initialized) pc5_compat_end(&ui->compatibility);
    pc5_music_import_end(&ui->music_import);
    pc5_video_import_end(&ui->video_import);
    if (ui->phone_source[0]) unlink(ui->phone_source);
    pc5_video_stop(&ui->video);
    pc5_ftp_stop();
    pc5_web_stop();
    /* The update's thread works in ui: it stops first (or finishes putting files in place). */
    if (pc5_upgrade_busy(&ui->upgrade)) {
        pc5_upgrade_cancel(&ui->upgrade);
        while (pc5_upgrade_busy(&ui->upgrade)) usleep(10000);
    }
    pc5_backgrounds_end(&ui->backgrounds, 1);
    if (ui->bg_job.running) pthread_join(ui->bg_job.thread, NULL);
    if (ui->update_job.running) pthread_join(ui->update_job.thread, NULL);
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
    pc5_textdoc_free(&ui->edit);
    pc5_free(ui->clip.paths);
    pc5_free(ui->clip.list);
    pc5_fonts_free(&ui->fonts);
    /* Back to English, the languages read freed: the web page's server, which reads them, is stopped. */
    pc5_i18n_end();
    pc5_free(ui->saves);
    pc5_free(ui->falloff);
    pc5_free(ui->backdrop);
    pc5_free(ui->composed);
    pc5_free(ui->bg);
    pc5_free(ui->bg_old);
    pc5_image_free(&ui->art_logo);
    pc5_image_free(&ui->art_controller);
    pc5_image_free(&ui->art_hero);
    pc5_image_free(&ui->art_cover);
    pc5_image_free(&ui->art_boot);
    pc5_image_free(&ui->art_sweep);
    for (int i = 0; i < ICONS; i++) pc5_image_free(&ui->art_icons[i]);
    pc5_image_free(&ui->hero_logo);
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
    pc5_copy(hint->name, sizeof(hint->name), folder->store_name);
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
    /* Which game it is (pc5_identity.h): kept, or read from its folder once and kept. */
    {
        Pc5Identity id;
        char dir[PC5_PATH];
        if (pc5_identity_read(ui->root, game->prefix, &id)) {
            memset(&id, 0, sizeof(id));
            if (!pc5_game_folder(ui->root, game->id, dir, sizeof(dir)) && !pc5_identity_scan(dir, &id))
                pc5_identity_write(ui->root, game->prefix, &id);
        }
        hint->steam_appid = id.steam_appid;
        pc5_copy(hint->name, sizeof(hint->name), id.name);
    }
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
/* Play: PC5's screen fades to black for this long before the game is handed over. */
enum { LAUNCH_FADE_MS = 400 };

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
    ui->backgrounds.details = 1;
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
    /* Home's look: the picture full, darker under the top bar, on the left where the game's
     * name and buttons are, and at the bottom where the covers are. */
    for (int y = 0; y < H; y++) {
        float v = y < 170 ? 0.62f + 0.38f * (float)y / 170 : 1;
        if (y > 470) {
            float f = (float)(y - 470) / (H - 470);
            v *= 1 - 0.86f * f * (2 - f);
        }
        vertical[y] = (int)(v * 256);
    }
    for (int x = 0; x < W; x++) {
        float f = x < 1250 ? (float)x / 1250 : 1;
        horizontal[x] = (int)((0.22f + 0.78f * f * f * (3 - 2 * f)) * 256);
    }
    for (int x = 0; x < W; x++) {
        int sx = (int)((2 * (int64_t)x + 1) * small->w * 128 / W) - 128;
        if (sx < 0) sx = 0;
        x0s[x] = sx >> 8;
        fxs[x] = sx & 255;
        x1s[x] = x0s[x] + 1 < small->w ? x0s[x] + 1 : x0s[x];
    }
    for (int y = 0; y < H; y++) {
        int sy = (int)((2 * (int64_t)y + 1) * small->h * 128 / H) - 128;
        if (sy < 0) sy = 0;
        int y0 = sy >> 8, fy = sy & 255, y1 = y0 + 1 < small->h ? y0 + 1 : y0;
        if (y0 > small->h - 1) y0 = y1 = small->h - 1;
        const uint32_t *r0 = small->px + (size_t)y0 * (size_t)small->w, *r1 = small->px + (size_t)y1 * (size_t)small->w;
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
        } else if (cover_ready && !ui->hero_backdrop) {
            snprintf(ui->bg_want, sizeof(ui->bg_want), "%s|cover", id);
        } else {
            /* PC5's own picture stands in for a game without a background, when it has one. */
            snprintf(ui->bg_want, sizeof(ui->bg_want), "%s|none", id);
            ui->bg_cover_wait = !ui->hero_backdrop;
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
    if (!pc5_thread_start(&job->thread, bg_job_main, job, 0)) {
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
    GameRow row = ui->pick_kind == PICK_COVER ? GR_COVER : ui->pick_kind == PICK_ICON ? GR_ICON
                  : ui_game_kind(ui) & GAME_MEDIA ? GR_BACKGROUND_IMAGE : GR_BACKGROUND;
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
        message(ui, 0, T("PC5 didn't install this game"),
                kind == PICK_COVER ? T("Its cover can only be changed as in Covers on the wiki: a picture of your own.")
                : kind == PICK_ICON ? T("Only a game PC5 installed has a tile picture.")
                                    : T("Its background can only be changed as in Covers on the wiki: a picture of your own."),
                S_GAME_MENU);
        return;
    }
    picture_close(ui);
    if (!(ui->pick = pc5_alloc(sizeof(*ui->pick)))) {
        message(ui, 0, T("PC5 ran out of memory"), T("Close PC5 and open it again."), S_GAME_MENU);
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
        message(ui, 0, ui->pick_kind == PICK_COVER ? T("The cover wasn't changed")
                       : ui->pick_kind == PICK_ICON ? T("The tile picture wasn't changed") : T("The background wasn't changed"),
                entry < ui->pick->count || ui->pick_kind != PICK_COVER || ui->pick_image.px
                    ? T("PC5 can't write to the game's folder in /data/prospero-win/prefixes.")
                    : T("PC5 couldn't make the card from the game's icon and name."),
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

/* The USB drive a game could be installed on: the one it is on, or else the one with the most
 * room, when it can be written to. 1 with its root in out, 0 for none. */
static int usb_target(const Pc5Ui *ui, const char *source, char *out, size_t size)
{
    uint64_t best = 0, room;
    out[0] = 0;
    if (!ui->usb_roots) return 0;
    if (source && !pc5_usb_install_root(source, out, size) && !access(out, W_OK)) return 1;
    out[0] = 0;
    for (int i = 0; ui->usb_roots[i]; i++)
        if (pc5_is_dir(ui->usb_roots[i]) && !access(ui->usb_roots[i], W_OK) && !pc5_free_bytes(ui->usb_roots[i], &room) &&
            room > best) {
            best = room;
            pc5_copy(out, size, ui->usb_roots[i]);
        }
    return out[0] != 0;
}

/* Every drive a game could be installed on, for S_INSTALL_WHERE's left and right: the one
 * usb_target offered (ui->where_usb) first, then the others in usb_roots' order. */
static void where_drives(Pc5Ui *ui)
{
    uint64_t room;
    ui->where_count = 0;
    if (!ui->usb_roots) return;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; ui->usb_roots[i] && ui->where_count < PC5_DRIVES; i++) {
            int offered = !strcmp(ui->usb_roots[i], ui->where_usb);
            if (offered != !pass) continue;
            if (pc5_is_dir(ui->usb_roots[i]) && !access(ui->usb_roots[i], W_OK) && !pc5_free_bytes(ui->usb_roots[i], &room))
                ui->where_drive[ui->where_count++] = i;
        }
}

/* S_INSTALL_WHERE: one step left (-1) or right (1) through the console and the drives. */
static void where_step(Pc5Ui *ui, int step)
{
    int at = 0, places = ui->where_count + 1;
    if (ui->button)
        for (int i = 0; i < ui->where_count; i++)
            if (!strcmp(ui->usb_roots[ui->where_drive[i]], ui->where_usb)) at = i + 1;
    if (places < 2) return;
    at = (at + step + places) % places;
    ui->button = at > 0;
    if (at) pc5_copy(ui->where_usb, sizeof(ui->where_usb), ui->usb_roots[ui->where_drive[at - 1]]);
}

/* A drive's name as PC5 shows it: USB drive 1, Extended storage 1, the console's storage. */
static void drive_name(const char *root, char *out, size_t size)
{
    const char *slash = strrchr(root, '/'), *name = slash && slash[1] ? slash + 1 : root;
    if (!strncmp(name, "usb", 3) && name[3] >= '0' && name[3] <= '9')
        pc5_tf(out, size, "USB drive %d", atoi(name + 3) + 1);
    else if (!strncmp(name, "ext", 3) && name[3] >= '0' && name[3] <= '9')
        pc5_tf(out, size, "Extended storage %d", atoi(name + 3) + 1);
    else if (!strcmp(root, "/data"))
        pc5_copy(out, size, T("Console storage"));
    else
        pc5_copy(out, size, name);
}

/* The review's game installed: copied to the console, or to the USB drive chosen (install_usb). */
static void begin_install_as(Pc5Ui *ui)
{
    Pc5Image cover = ui->review_image;
    int status;

    ui->review_image = (Pc5Image){ 0 };
    status = pc5_install_begin_on_usb(&ui->install, ui->root, ui->template_prefix, &ui->folder, ui->exe, &cover,
                                      ui->review_tint, ui->review_source, ui->install_usb);
    if (!status) say(ui, "PC5 install id=%s from=%s to=%s", ui->install.id, ui->folder.path, ui->install_usb[0] ? ui->install_usb : "console");
    ui->installing = 1;
    if (status) {
        char title[200];
        pc5_tf(title, sizeof(title), "%s can't be installed", ui->folder.name);
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

    pc5_tf(title, sizeof(title), "%s can't be updated", index >= 0 ? ui->library.games[index].title : T("The game"));
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

/* The USB drives' PC5/INSTALL folders, made on each drive plugged in that can be written to;
 * how many drives there are. */
static int usb_install_refresh(Pc5Ui *ui)
{
    int drives = 0;
    for (int i = 0; i < PC5_DRIVES; i++) {
        char drop[PC5_PATH];
        ui->usb_install[i][0] = 0;
        if (!ui->usb_roots || pc5_usb_install_dir(i, drop, sizeof(drop))) continue;
        const char *root = ui->usb_roots[i];
        if (!root) break;
        if (!pc5_is_dir(root)) continue;
        drives++;
        if (!pc5_is_dir(drop) && (access(root, W_OK) || pc5_mkdirs(drop))) continue;
        pc5_copy(ui->usb_install[i], sizeof(ui->usb_install[i]), drop);
    }
    ui->usb_seen = drives;
    return drives;
}

/* PC5's INSTALL folders, its own and the USB drives', into roots (NULL-ended, room for PC5_DRIVES + 2). */
static int install_folders(const Pc5Ui *ui, const char **roots)
{
    int n = 0;
    if (ui->install_dir[0]) roots[n++] = ui->install_dir;
    for (int i = 0; i < PC5_DRIVES; i++)
        if (ui->usb_install[i][0]) roots[n++] = ui->usb_install[i];
    roots[n] = NULL;
    return n;
}

/* The games waiting in the INSTALL folders. */
static int install_waiting_count(const Pc5Ui *ui)
{
    const char *only[PC5_DRIVES + 2];
    if (!install_folders(ui, only)) return 0;
    Pc5Source *found = pc5_alloc(sizeof(*found) * PC5_SOURCES);
    int games = found ? pc5_source_scan(only, found, PC5_SOURCES) : 0;
    pc5_free(found);
    return games;
}

/* The review's game, once where it goes is known (install_usb): the room it needs there
 * checked, its kept saves offered, then installed. */
static void review_install(Pc5Ui *ui)
{
    uint64_t free_bytes = 0, needs = ui->install_usb[0] ? pc5_install_needs_usb(&ui->folder) : pc5_install_needs(&ui->folder);
    if (!pc5_free_bytes(ui->root, &free_bytes) && free_bytes < needs) {
        char need[32], have[32], body[400];
        pc5_size_text(needs, need, sizeof(need));
        pc5_size_text(free_bytes, have, sizeof(have));
        pc5_tf(body, sizeof(body), ui->install_usb[0] ? "Its Windows needs %s and the console has %s free."
                                                        : "The game needs %s and the console has %s free.", need, have);
        message(ui, 0, T("Not enough space"), body, S_REVIEW);
    } else if (pc5_saves_find(ui->root, ui->folder.name, ui->exe >= 0 ? ui->folder.exes[ui->exe].path : NULL,
                              ui->use_saves, sizeof(ui->use_saves)) > 0) {
        ui->button = 1;   /* Put them back */
        go(ui, S_SAVES_ASK);
    } else {
        ui->use_saves[0] = 0;
        begin_install_as(ui);
    }
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
            pc5_copy(q->error, sizeof(q->error), q->folder.problem[0] ? q->folder.problem : T("It has no program to start."));
            continue;
        }
        Pc5Image cover = q->cover;
        q->cover = (Pc5Image){ 0 };
        ui->folder = q->folder;
        ui->exe = q->exe;
        if (pc5_install_begin_on_usb(&ui->install, ui->root, ui->template_prefix, &q->folder, q->exe, &cover, q->tint,
                                     q->cover_source, ui->install_usb)) {
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
        char title[200], body[1400], id[PC5_ID];
        int moved = ui->install.moving;
        pc5_copy(id, sizeof(id), ui->install.id);
        if (phase == PC5_INSTALL_DONE && ui->install.skipped) {
            pc5_tf(title, sizeof(title), "%s is partly updated", ui->install.info.title);
            pc5_tf(body, sizeof(body), "%u of its files couldn't be put in place. %s Update it again; its saves, settings "
                     "and options are as they were.", (unsigned)ui->install.skipped, TX(ui->install.error));
            say(ui, "PC5 problem update %s: %u files left out: %s", id, (unsigned)ui->install.skipped, ui->install.error);
        } else if (phase == PC5_INSTALL_DONE) {
            pc5_tf(title, sizeof(title), "%s is updated", ui->install.info.title);
            pc5_tf(body, sizeof(body), "Its saves, settings and options are as they were.");
            say(ui, "PC5 updated id=%s files=%u", id, (unsigned)ui->install.files_done);
        } else if (phase == PC5_INSTALL_CANCELLED) {
            pc5_tf(title, sizeof(title), "Update stopped");
            pc5_tf(body, sizeof(body), "The copy made so far was deleted: the game is as it was. The USB drive wasn't changed.");
            say(ui, "PC5 update ended cancelled=1 id=%s", id);
        } else {
            pc5_tf(title, sizeof(title), "%s wasn't updated", ui->install.info.title);
            pc5_tf(body, sizeof(body), moved ? "%s PC5 finishes the update the next time it starts."
                                               : "%s The game is as it was.", TX(ui->install.error));
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
        pc5_tf(title, sizeof(title), "%s is installed", ui->install.info.title);
        say(ui, "PC5 installed id=%s saves=%d", id, saves);
        pc5_install_end(&ui->install);
        ui->installing = 0;
        library_reload(ui, id);
        message(ui, 1, title, saves ? T("It's in your library, with the saves and settings it had before.")
                                    : T("It's in your library. Press the button to play it from there."), S_LIBRARY);
    } else {
        char title[200], body[1400];
        int cancelled = phase == PC5_INSTALL_CANCELLED;
        pc5_tf(title, sizeof(title), cancelled ? "Install stopped" : "%s wasn't installed", ui->install.info.title);
        if (cancelled) pc5_tf(body, sizeof(body), "Everything copied so far was deleted. The USB drive wasn't changed.");
        else if (strstr(ui->install.staging, "/.installing-") || !ui->install.staging[0])
            pc5_tf(body, sizeof(body), "%s Nothing was kept.", TX(ui->install.error));
        else pc5_tf(body, sizeof(body), "%s PC5 tries again the next time it starts.", TX(ui->install.error));
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
            pc5_copy(q->folder.problem, sizeof(q->folder.problem), N_("PC5 can't read this folder."));
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
    char title[160], body[1400] = "", first[PC5_ID] = "";
    int installed = 0;
    size_t used = 0;

    for (int i = 0; i < ui->queue_count; i++) {
        const QueueGame *q = &ui->queue[i];
        if (q->state == Q_INSTALLED) {
            if (!installed++) pc5_copy(first, sizeof(first), q->id);
            continue;
        }
        const char *why = q->state == Q_STOPPED ? T("stopped, nothing kept.") : q->error[0] ? TX(q->error) : T("not installed.");
        int n = snprintf(body + used, sizeof(body) - used, "%s%s: %s", used ? "  " : "", q->folder.name[0] ? q->folder.name : q->source.name, why);
        if (n > 0) used = used + (size_t)n < sizeof(body) ? used + (size_t)n : sizeof(body) - 1;
    }
    int all = installed == ui->queue_count;
    if (all)
        pc5_tf(title, sizeof(title), installed == 1 ? "1 game is installed" : "%d games are installed", installed);
    else if (installed)
        pc5_tf(title, sizeof(title), "%d of %d games are installed", installed, ui->queue_count);
    else
        pc5_tf(title, sizeof(title), "No game was installed");
    if (!body[0]) pc5_tf(body, sizeof(body), "They're in your library. The USB drive wasn't changed.");
    say(ui, "PC5 queue done installed=%d of=%d", installed, ui->queue_count);
    queue_free(ui);
    memset(ui->ticked, 0, sizeof(ui->ticked));
    library_reload(ui, first[0] ? first : NULL);
    message(ui, installed > 0 && all, title, body, S_LIBRARY);
}

/* ---- Settings ----------------------------------------------------------------------------- */

static void language_log(void *context, const char *line)
{
    say(context, "%s", line);
}

/* PC5's language as its setting says, from now on (pc5_i18n.h): the app's pc5/lang/<code>.po,
 * and for Chinese, Japanese and Korean their letters, pc5/fonts-<code>.bin, beside fonts.bin. */
static void language_apply(Pc5Ui *ui)
{
    char dir[PC5_PATH], path[PC5_PATH];
    const char *code = pc5_language_choose(ui->settings.language, ui->config.language);
    const Pc5Language *language = pc5_language(pc5_language_find(code));
    char *slash;

    pc5_copy(dir, sizeof(dir), ui->config.fonts ? ui->config.fonts : "");
    if ((slash = strrchr(dir, '/'))) *slash = 0;
    else pc5_copy(dir, sizeof(dir), ".");
    int strings = pc5_format(path, sizeof(path), "%s/lang", dir) ? -1 : pc5_i18n_use(path, code, language_log, ui);
    int font = 0;
    if (!language->cjk) pc5_fonts_extra(&ui->fonts, NULL);
    else if (pc5_format(path, sizeof(path), "%s/fonts-%s.bin", dir, code) || (font = pc5_fonts_extra(&ui->fonts, path)))
        say(ui, "PC5 problem language %s: no %s, its letters show as ?", code, path);
    say(ui, "PC5 language %s setting=%s console=%s strings=%d extra_font=%s", code,
        ui->settings.language[0] ? ui->settings.language : "system", ui->config.language ? ui->config.language : "-",
        strings, !language->cjk ? "none" : font ? "missing" : "loaded");
    ui->dirty = 1;
}

static int offer_update(const Pc5Ui *ui);

static void setting_text(Pc5Ui *ui, SettingRow row, const char **label, char *about, size_t about_size, char *value,
                         size_t value_size)
{
    const Pc5Settings *set = &ui->settings;
    value[0] = 0;
    about[0] = 0;
    switch (row) {
    case SR_INSTALL:
        *label = T("Install games from USB");
        pc5_copy(about, about_size, T("Copies one game, or several in a row, to the console"));
        break;
    case SR_UNINSTALL:
        *label = T("Uninstall game");
        pc5_copy(about, about_size, T("Deletes a game (its saves can be kept), or saves kept from before"));
        break;
    case SR_PHONE_LIBRARY:
        *label = T("Library appearance from phone");
        pc5_copy(about, about_size, T("One QR code to choose any installed game and send its pictures, music or video"));
        pc5_copy(value, value_size, T("Open"));
        break;
    case SR_COMPATIBILITY:
        *label = T("Game compatibility list");
        pc5_copy(about, about_size, T("Browse community reports, settings and saved offline results"));
        break;
    case SR_MAINTENANCE:
        *label = T("Game maintenance");
        pc5_copy(about, about_size, T("Installing, uninstalling and the compatibility list"));
        pc5_copy(value, value_size, "\xe2\x80\xba");
        break;
    case SR_GAMES:
        *label = T("In games");
        pc5_copy(about, about_size, T("Vibration, light bar, triggers and 120 Hz output"));
        pc5_copy(value, value_size, "\xe2\x80\xba");
        break;
    case SR_OVERLAY:
        *label = T("Overlay");
        pc5_copy(about, about_size, T("FPS, frame time, CPU and GPU use, temperatures and memory"));
        pc5_copy(value, value_size, "\xe2\x80\xba");
        break;
    case SR_SYSTEM:
        *label = T("System");
        if (ui->update.last.state == PC5_UPDATE_AVAILABLE) pc5_update_text(&ui->update.last, about, about_size);
        else pc5_copy(about, about_size, T("Startup sound, logs, picture sources, updates and language"));
        pc5_copy(value, value_size, "\xe2\x80\xba");
        break;
    case SR_PICTURES: {
        int on = 0;
        for (int i = 0; i < PC5_PICTURE_SOURCES; i++) on += !(set->pictures_off & (1u << i));
        *label = T("Picture sources");
        pc5_copy(about, about_size, T("Where covers and backgrounds are looked for"));
        pc5_tf(value, value_size, "%d of %d \xe2\x80\xba", on, PC5_PICTURE_SOURCES);
        break;
    }
    case SR_HIGH_REFRESH:
        *label = T("120 Hz output");
        pc5_copy(about, about_size, set->high_refresh ? T("Up to 120 fps on a TV that takes it; turn on the PS5's 120 Hz output and turn VRR off")
                                                      : T("Off: games show up to 60 fps"));
        pc5_copy(value, value_size, set->high_refresh ? T("On") : T("Off"));
        break;
    case SR_KEYBOARD:
        *label = T("Keyboard layout");
        pc5_copy(about, about_size, set->keyboard_us ? T("A USB keyboard types as a US one in games")
                                                     : T("A USB keyboard types as your language's in games"));
        pc5_copy(value, value_size, set->keyboard_us ? T("US") : T("Auto"));
        break;
    case SR_KEEP_LOGS:
        *label = T("Keep all logs");
        pc5_copy(about, about_size, set->keep_logs ? T("Every session's log is kept for 7 days")
                                                   : T("Problem sessions and each game's newest; turn on when testing a game"));
        pc5_copy(value, value_size, set->keep_logs ? T("On") : T("Off"));
        break;
    case SR_FTP: {
        Pc5FtpStatus ftp;
        pc5_ftp_status(&ftp);
        *label = T("FTP server");
        if (ftp.running)
            pc5_tf(about, about_size, "ftp://%s from a PC (FileZilla): the INSTALL folder, while PC5 is open",
                     ftp.address);
        else if (set->ftp_server && ui->ftp_error[0])
            pc5_tf(about, about_size, "Not started: %.150s", TX(ui->ftp_error));
        else
            pc5_copy(about, about_size, T("Send games from a PC to PC5's INSTALL folder, then install them here"));
        pc5_copy(value, value_size, set->ftp_server ? T("On") : T("Off"));
        break;
    }
    case SR_WEB: {
        Pc5WebStatus web;
        pc5_web_status(&web);
        *label = T("Web page");
        if (web.running)
            pc5_tf(about, about_size, "http://%s in a browser on a phone or a PC; Triangle: a QR code for it", web.address);
        else if (set->web_page && ui->web_error[0])
            pc5_tf(about, about_size, "Not started: %.150s", TX(ui->web_error));
        else
            pc5_copy(about, about_size, T("PC5 from a browser: send games, launch options, logs"));
        pc5_copy(value, value_size, set->web_page ? T("On") : T("Off"));
        break;
    }
    case SR_NETWORK:
        *label = T("Network");
        pc5_copy(about, about_size, T("From a PC or a phone: the FTP server, the web page, a username and password"));
        pc5_copy(value, value_size, "\xe2\x80\xba");
        break;
    case SR_REMOTE_USER:
        *label = T("Username");
        pc5_copy(about, about_size, set->remote_user[0] ? T("The username the FTP server and the web page ask for")
                                                        : T("None set: any username will do"));
        pc5_copy(value, value_size, set->remote_user[0] ? set->remote_user : T("Any"));
        break;
    case SR_REMOTE_PASSWORD:
        *label = T("Password");
        pc5_copy(about, about_size, set->remote_password[0] ? T("The password the FTP server and the web page ask for")
                                                            : T("Optional: none set, no password is asked for"));
        pc5_copy(value, value_size, set->remote_password[0] ? "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2" : T("None"));
        break;
    case SR_FILES:
        *label = T("File manager");
        pc5_copy(about, about_size, T("Copy, move, rename and delete files on the console and its drives"));
        pc5_copy(value, value_size, "\xe2\x80\xba");
        break;
    case SR_ABOUT:
        *label = T("About PC5");
        pc5_copy(about, about_size, T("Version, free space and credits"));
        break;
    case SR_FPS:
        *label = T("Show FPS");
        pc5_copy(about, about_size, T("Frames shown each second"));
        pc5_copy(value, value_size, set->fps ? T("On") : T("Off"));
        break;
    case SR_FRAME_TIME:
        *label = T("Frame time");
        pc5_copy(about, about_size, T("How long a frame takes, on average, in milliseconds"));
        pc5_copy(value, value_size, set->frame_time ? T("On") : T("Off"));
        break;
    case SR_CPU_USAGE:
        *label = T("CPU usage");
        pc5_copy(about, about_size, T("How busy the console's CPU is, with everything it runs"));
        pc5_copy(value, value_size, set->cpu_usage ? T("On") : T("Off"));
        break;
    case SR_GPU_USAGE:
        *label = T("GPU usage");
        pc5_copy(about, about_size, T("How busy the console's GPU is"));
        pc5_copy(value, value_size, set->gpu_usage ? T("On") : T("Off"));
        break;
    case SR_CPU_TEMPERATURE:
        *label = T("CPU temperature");
        pc5_copy(about, about_size, T("In degrees Celsius"));
        pc5_copy(value, value_size, set->cpu_temperature ? T("On") : T("Off"));
        break;
    case SR_SOC_TEMPERATURE:
        *label = T("Chip temperature");
        pc5_copy(about, about_size, T("The console chip's, in degrees Celsius"));
        pc5_copy(value, value_size, set->soc_temperature ? T("On") : T("Off"));
        break;
    case SR_MEMORY:
        *label = T("Memory");
        pc5_copy(about, about_size, T("What the game uses, of what it has, in GB"));
        pc5_copy(value, value_size, set->memory ? T("On") : T("Off"));
        break;
    case SR_FPS_CORNER:
        *label = T("Corner");
        pc5_copy(about, about_size, T("Where the overlay sits on the screen"));
        pc5_copy(value, value_size, T(pc5_corner_name(set->fps_corner)));
        break;
    case SR_OVERLAY_SIZE:
        *label = T("Size");
        pc5_copy(about, about_size,
                 set->overlay_size == PC5_OVERLAY_EXTRA_LARGE ? T("Every line at once, twice the size, by the console")
                 : set->overlay_size == PC5_OVERLAY_LARGE     ? T("Twice the size, two lines at a time, taking turns")
                                                              : T("Every line at once, in small text"));
        pc5_copy(value, value_size, T(pc5_overlay_size_name(set->overlay_size)));
        break;
    case SR_VIBRATION:
        *label = T("Vibration");
        pc5_copy(about, about_size, set->vibration ? T("How strongly the controller rumbles with the game")
                                                   : T("The game's rumble on the controller"));
        percent_text(set->vibration, value, value_size);
        break;
    case SR_LIGHT:
        *label = T("Light bar");
        pc5_copy(about, about_size, set->light_bar == PC5_LIGHT_SCREEN ? T("The colour of what is on screen")
                                    : set->light_bar == PC5_LIGHT_GAME ? T("The main colour of the game's cover")
                                                                       : T("PC5 leaves the console's colour"));
        pc5_copy(value, value_size, T(pc5_light_name(set->light_bar)));
        break;
    case SR_TRIGGERS:
        *label = T("Adaptive triggers");
        pc5_copy(about, about_size, set->triggers == PC5_TRIGGERS_RUMBLE ? T("L2 and R2 vibrate with the game's rumble")
                                    : set->triggers == PC5_TRIGGERS_RESISTANCE ? T("L2 and R2 push back as you press them")
                                    : set->triggers == PC5_TRIGGERS_BOTH ? T("L2 and R2 push back, and buzz with the rumble")
                                    : T("Rumble, resistance or both on L2 and R2"));
        pc5_copy(value, value_size, T(pc5_triggers_name(set->triggers)));
        break;
    case SR_TRIGGER_RUMBLE:
        *label = T("Trigger rumble");
        pc5_copy(about, about_size, T("How strongly L2 and R2 vibrate"));
        percent_text(set->trigger_rumble, value, value_size);
        break;
    case SR_TRIGGER_RESISTANCE:
        *label = T("Trigger resistance");
        pc5_copy(about, about_size, T("How firmly L2 and R2 push back"));
        percent_text(set->trigger_resistance, value, value_size);
        break;
    case SR_CHECK_ON_OPEN:
        *label = T("Check when PC5 opens");
        pc5_copy(about, about_size, T("Asks GitHub for a newer PC5 each time you open PC5, and offers it"));
        pc5_copy(value, value_size, set->check_updates ? T("On") : T("Off"));
        break;
    case SR_CHECK_NOW:
        *label = T("Check now");
        if (ui->update.last.state == PC5_UPDATE_AVAILABLE && !offer_update(ui))
            pc5_tf(about, about_size, "PC5 %s is out: copy it over this one from github.com/bornaradusin/PC5",
                     ui->update.last.version);
        else
            pc5_update_text(&ui->update.last, about, about_size);
        break;
    case SR_STARTUP_SOUND:
        *label = T("Startup sound");
        pc5_copy(about, about_size, T("On the controller's speaker when PC5 opens"));
        pc5_copy(value, value_size, strcmp(set->startup_sound, "off") ? set->startup_sound : T("Off"));
        break;
    case SR_STARTUP_VOLUME:
        *label = T("Startup sound volume");
        pc5_copy(about, about_size, T("How loud the startup sound plays"));
        percent_text(set->startup_volume, value, value_size);
        break;
    case SR_LANGUAGE: {
        /* The setting's language, or the console's (System default), each in its own name. */
        const Pc5Language *in = pc5_language(pc5_language_find(pc5_language_choose(set->language, ui->config.language)));
        *label = T("Language");
        if (set->language[0]) {
            pc5_copy(about, about_size, T("PC5's screens and web page; System default follows the console"));
            pc5_copy(value, value_size, in->name);
        } else {
            pc5_tf(about, about_size, "As the console: %s", in->name);
            /* TRANSLATORS: the Language setting's value when PC5 follows the console's language. */
            pc5_copy(value, value_size, T("System default"));
        }
        break;
    }
    case SR_UPDATE_NOW:
        *label = T("Update now");
    {
        char how[200];
        update_how(ui, how, sizeof(how));
        pc5_tf(about, about_size, "%.160s, installs it and restarts PC5", how);
    }
        break;
    default:
        if (row >= SR_SOURCE && row < SR_SOURCE + PC5_PICTURE_SOURCES) {
            static const char *const abouts[PC5_PICTURE_SOURCES] = {
                N_("Its store's covers and backgrounds"), N_("Covers and backgrounds its members made, with your key"),
                N_("Its catalogue's covers and backgrounds"), N_("The cover on the game's page, and its Steam background"),
                N_("Steam's pictures for a game Steam's search misses")
            };
            int i = (int)(row - SR_SOURCE), off = (set->pictures_off & (1u << i)) != 0;
            *label = pc5_picture_source_name(i);
            pc5_copy(about, about_size, T(abouts[i]));
            pc5_copy(value, value_size, off ? T("Off") : T("On"));
            /* SteamGridDB asks for the player's key: where it goes, or that it was refused. */
            if (i == 1 && !ui->griddb_key[0]) {
                pc5_tf(about, about_size, "Needs your key, in %s", "/data/prospero-win/" PC5_API_KEYS);
                if (!off) pc5_copy(value, value_size, T("No key"));
            } else if (i == 1 && ui->griddb_refused) {
                pc5_tf(about, about_size, "It refused the key in %s: check it there", "/data/prospero-win/" PC5_API_KEYS);
            }
        }
        break;
    }
}

/* The INSTALL folder's FTP server, as its setting says: started (on PC5's INSTALL folder) or stopped. */
static void ftp_apply(Pc5Ui *ui)
{
    ui->ftp_error[0] = 0;
    if (!ui->settings.ftp_server || !ui->install_dir[0]) {
        pc5_ftp_stop();
        if (ui->settings.ftp_server) pc5_copy(ui->ftp_error, sizeof(ui->ftp_error), N_("PC5's INSTALL folder isn't there."));
        return;
    }
    Pc5FtpStatus status;
    pc5_ftp_login(ui->settings.remote_user, ui->settings.remote_password);
    pc5_ftp_status(&status);
    if (status.running) return;
    if (pc5_ftp_start(ui->install_dir, PC5_FTP_PORT, ui->ftp_error, sizeof(ui->ftp_error)))
        say(ui, "PC5 problem ftp: %s", ui->ftp_error);
    ui->ftp_uploads_seen = 0;
}

/* PC5's page for a browser, as its setting says: started (with PC5's folders) or stopped. */
static void web_apply(Pc5Ui *ui)
{
    pc5_web_phone_only(0);
    ui->web_error[0] = 0;
    if (!ui->settings.web_page) {
        pc5_web_stop();
        return;
    }
    Pc5WebStatus status;
    pc5_web_login(ui->settings.remote_user, ui->settings.remote_password);
    pc5_web_status(&status);
    if (status.running) return;
    const Pc5WebConfig config = { ui->root, ui->install_dir };
    if (pc5_web_start(&config, PC5_WEB_PORT, ui->web_error, sizeof(ui->web_error)))
        say(ui, "PC5 problem web page: %s", ui->web_error);
    ui->web_uploads_seen = 0;
}

/* Games sent over FTP or from the web page: once nothing has moved for a few seconds after an
 * upload, the INSTALL folder looked at again, and its games offered as when PC5 opens. */
static int usb_install_refresh(Pc5Ui *ui);
static int install_waiting_count(const Pc5Ui *ui);

/* A USB drive plugged in while the library shows: its PC5/INSTALL folder made, and the games
 * waiting there (and in PC5's own) offered, as when PC5 opens. Looked at every few seconds. */
static void usb_plugged_check(Pc5Ui *ui)
{
    int drives = 0;
    if (!ui->usb_roots || ui->now - ui->usb_looked < 3000) return;
    ui->usb_looked = ui->now;
    for (int i = 0; ui->usb_roots[i] && i < PC5_DRIVES; i++) drives += pc5_is_dir(ui->usb_roots[i]);
    if (drives == ui->usb_seen) return;
    int more = drives > ui->usb_seen;
    usb_install_refresh(ui);
    if (!more) return;
    int games = install_waiting_count(ui);
    say(ui, "PC5 usb drive plugged in drives=%d install folder games=%d", drives, games);
    if (games) {
        ui->install_waiting = games;
        ui->button = 1;   /* Install first */
        go(ui, S_INSTALL_ASK);
    }
}

static void uploads_check(Pc5Ui *ui)
{
    Pc5FtpStatus ftp;
    Pc5WebStatus web;
    pc5_ftp_status(&ftp);
    pc5_web_status(&web);
    int sent = (ftp.running && ftp.uploads != ui->ftp_uploads_seen) || (web.running && web.uploads != ui->web_uploads_seen);
    if (!sent || (ftp.running && (ftp.transfers || ftp.idle_ms < 3000)) ||
        (web.running && (web.transfers || web.idle_ms < 3000)))
        return;
    if (ftp.running) ui->ftp_uploads_seen = ftp.uploads;
    if (web.running) ui->web_uploads_seen = web.uploads;
    const char *only[] = { ui->install_dir, NULL };
    Pc5Source *found = pc5_alloc(sizeof(*found) * PC5_SOURCES);
    int games = found ? pc5_source_scan(only, found, PC5_SOURCES) : 0;
    pc5_free(found);
    say(ui, "PC5 uploads ftp=%u web=%u install folder games=%d", ftp.uploads, web.uploads, games);
    if (games) {
        ui->install_waiting = games;
        ui->button = 1;   /* Install first */
        go(ui, S_INSTALL_ASK);
    }
}

/* A setting with values moves to its next (step 1) or previous (-1) one: 1 when it changed. */
static int setting_change(Pc5Ui *ui, SettingRow row, int step)
{
    Pc5Settings *set = &ui->settings;
    switch (row) {
    case SR_KEEP_LOGS: set->keep_logs = !set->keep_logs; break;
    case SR_KEYBOARD: set->keyboard_us = !set->keyboard_us; break;
    case SR_HIGH_REFRESH: set->high_refresh = !set->high_refresh; break;
    case SR_FTP:
        set->ftp_server = !set->ftp_server;
        ftp_apply(ui);
        break;
    case SR_WEB:
        set->web_page = !set->web_page;
        web_apply(ui);
        break;
    case SR_FPS: set->fps = !set->fps; break;
    case SR_FRAME_TIME: set->frame_time = !set->frame_time; break;
    case SR_CPU_USAGE: set->cpu_usage = !set->cpu_usage; break;
    case SR_GPU_USAGE: set->gpu_usage = !set->gpu_usage; break;
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
    case SR_LANGUAGE: {
        /* System default (""), then each language, round. */
        int count = pc5_language_count() + 1, at = set->language[0] ? pc5_language_find(set->language) + 1 : 0;
        at = ((at + (step < 0 ? -1 : 1)) % count + count) % count;
        pc5_copy(set->language, sizeof(set->language), at ? pc5_language(at - 1)->code : "");
        language_apply(ui);
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
    say(ui, "PC5 settings fps=%d frame_time=%d cpu=%d gpu=%d cpu_temp=%d soc_temp=%d memory=%d corner=%d size=%s vibration=%d light=%d "
        "triggers=%d trigger_rumble=%d trigger_resistance=%d updates=%d keep_logs=%d startup_sound=%s startup_volume=%d "
        "pictures_off=%#x language=%s",
        set->fps, set->frame_time, set->cpu_usage, set->gpu_usage, set->cpu_temperature, set->soc_temperature, set->memory,
        (int)set->fps_corner, pc5_overlay_size_key(set->overlay_size), set->vibration, (int)set->light_bar, (int)set->triggers, set->trigger_rumble,
        set->trigger_resistance, set->check_updates, set->keep_logs, set->startup_sound, set->startup_volume,
        (unsigned)set->pictures_off, set->language[0] ? set->language : "system");
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
        k->memory_4gb ? "4gb" : "auto", pc5_video_memory_key(k), k->resolution == 2 ? "2160p" : k->resolution == 1 ? "1440p" : "1080p",
        k->stretch ? "stretch" : "keep", k->cpu_sharing, pc5_graphics_tech_name(k->graphics, ui_game_api(ui)));
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
    case PR_TOUCH_MODE: k->touch_mode = (k->touch_mode + 3 + (step < 0 ? -1 : 1)) % 3; return 1;
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

/* A redistributable's line in the log (pc5_redist_install). */
static void redist_log(void *context, const char *line)
{
    say(context, "%s", line);
}

/* A game's menu, with its controller as PC5 kept it. */
static void game_menu_open(Pc5Ui *ui, const char *id)
{
    pc5_copy(ui->launch_id, sizeof(ui->launch_id), id);
    pc5_controls_load(ui->root, id, &ui->controls);
    if (!ui->config.tiles_root || !pc5_home_find(ui->config.tiles_root, id, ui->tile)) ui->tile[0] = 0;
    /* Steam's library in its folder: its Steam emulator row (looked for once, pc5_steam.h). */
    ui->steam_dlls = 0;
    int index = pc5_library_find(&ui->library, id);
    char folder[PC5_PATH], program[PC5_PATH];
    if (index >= 0 && ui->library.games[index].pc5 && !pc5_game_folder(ui->root, id, folder, sizeof(folder))) {
        char (*found)[PC5_PATH] = pc5_alloc(sizeof(*found) * PC5_STEAM_DLLS);
        if (found)
            ui->steam_dlls = pc5_steam_find(ui->root, id, folder,
                                            pc5_game_program(ui->root, id, program, sizeof(program)) ? NULL : program,
                                            found, PC5_STEAM_DLLS);
        pc5_free(found);
    }
    ui->menu = 0;
    ui->game_page = GP_MAIN;
    go(ui, S_GAME_MENU);
}

/* The game's home screen tile: made, or taken away when it has one. */
static void home_toggle(Pc5Ui *ui, const Pc5Game *game)
{
    char error[PC5_ERROR] = "", body[1600];

    if (!ui->config.tiles_root || !ui->config.tile_template) {
        message(ui, 0, T("Home screen tiles aren't available"), T("This copy of PC5 has no tile program."), S_GAME_MENU);
        return;
    }
    if (ui->tile[0]) {
        int removed = pc5_home_remove(ui->config.tiles_root, game->id);
        say(ui, "PC5 home tile removed id=%s tile=%s count=%d", game->id, ui->tile, removed);
        if (removed < 0) {
            pc5_tf(body, sizeof(body), "PC5 can't delete its folder, %s/%s.", ui->config.tiles_root, ui->tile);
            message(ui, 0, T("The tile wasn't taken away"), body, S_GAME_MENU);
            return;
        }
        ui->tile[0] = 0;
        message(ui, 1, T("Taken off the home screen"),
                T("The tile's folder is deleted. If the tile is still on the home screen, delete it there."), S_GAME_MENU);
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
                message(ui, 0, T("The tile wasn't made"), error, S_GAME_MENU);
                return;
            }
            pc5_tf(body, sizeof(body),
                     "Its tile, %s, appears on the home screen once you close PC5 and ShadowMountPlus finds it in %s. "
                     "It is the game's own app: it starts the game straight away, and leaving the game goes back to "
                     "the home screen. It runs from PC5's folder, %s: keep PC5 there.", ui->tile,
                     ui->config.tiles_root, folder);
            message(ui, 1, T("Added to the home screen"), body, S_GAME_MENU);
            return;
        }
        say(ui, "PC5 home tile forwarder id=%s: PC5's folder %s", game->id, found == -2 ? "is there twice" : "not found");
    }
    if (pc5_home_add(ui->config.tiles_root, ui->config.tile_template, ui->root, game->id, game->prefix, game->title,
                     ui->tile, error, sizeof(error))) {
        say(ui, "PC5 problem home tile %s: %s", game->id, error);
        message(ui, 0, T("The tile wasn't made"), error, S_GAME_MENU);
        return;
    }
    say(ui, "PC5 home tile added id=%s tile=%s", game->id, ui->tile);
    pc5_tf(body, sizeof(body),
             "Its tile, %s, appears on the home screen once ShadowMountPlus finds it in %s. It starts the game "
             "straight away, and leaving the game goes back to the home screen.", ui->tile, ui->config.tiles_root);
    message(ui, 1, T("Added to the home screen"), body, S_GAME_MENU);
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
static int update_answer(Pc5Ui *ui, const Pc5Update *answer, int quiet_if_offered);

static int update_check_now(Pc5Ui *ui, int quiet_if_offered)
{
    Pc5Update update;
    pc5_update_check(&ui->platform.http, PC5_VERSION, &update);
    return update_answer(ui, &update, quiet_if_offered);
}

static void *update_job_main(void *arg)
{
    UpdateJob *job = arg;
    pc5_update_check(&job->http, PC5_VERSION, &job->result);
    atomic_store(&job->done, 1);
    return NULL;
}

/* The check as PC5 opens, started on its thread: 0, or -1 when there is no thread (the
 * caller asks on the screens' thread instead). */
static int update_check_start(Pc5Ui *ui)
{
    if (ui->update_job.running) return 0;
    ui->update_job.http = ui->platform.http;
    atomic_store(&ui->update_job.done, 0);
    if (pc5_thread_start(&ui->update_job.thread, update_job_main, &ui->update_job, 0)) return -1;
    ui->update_job.running = 1;
    say(ui, "PC5 update checking in the background");
    return 0;
}

/* Each frame: the check's answer once it is in, and its offer once the library has shown
 * (not over a menu or a game being started). */
static void update_check_follow(Pc5Ui *ui)
{
    if (ui->update_job.running && atomic_load(&ui->update_job.done)) {
        pthread_join(ui->update_job.thread, NULL);
        ui->update_job.running = 0;
        ui->update_job.offer = update_answer(ui, &ui->update_job.result, 1);
    }
    if (ui->update_job.offer && ui->screen == S_LIBRARY && ui->drawn) {
        ui->update_job.offer = 0;
        ui->button = 0;
        go(ui, S_UPGRADE_ASK);
    }
}

/* What the check found, remembered and told (as update_check_now says). */
static int update_answer(Pc5Ui *ui, const Pc5Update *answer, int quiet_if_offered)
{
    Pc5Update update = *answer;
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
        pc5_tf(text, sizeof(text), "PC5 %s is available on GitHub (you have %s).", update.version, PC5_VERSION);
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
    const char *name = index >= 0 ? ui->library.games[index].title : T("The game");
    const char *slash = strstr(issue->file, "/logs/");

    ui->issue_pending = 0;
    pc5_copy(file, sizeof(file), slash ? slash + 1 : issue->file);
    switch (issue->problem) {
    case PC5_LOG_GAME_CRASHED:
        pc5_tf(title, sizeof(title), "%s stopped with an error", name);
        pc5_tf(body, sizeof(body), "Wine reported a crash. Its log is kept as %.120s in %.120s.", file, ui->root);
        break;
    case PC5_LOG_NO_PICTURE:
        pc5_tf(title, sizeof(title), "%s closed before showing anything", name);
        pc5_tf(body, sizeof(body), "If PC5 started the wrong program, choose Change program in the game's menu "
                 "(Triangle). Its log is kept as %.120s in %.120s.", file, ui->root);
        break;
    case PC5_LOG_FORCED:
        pc5_tf(title, sizeof(title), "%s had to be closed", name);
        pc5_tf(body, sizeof(body), "It didn't close when asked, so PC5 ended it. Its log is kept as %.120s in %.120s.", file,
                 ui->root);
        break;
    case PC5_LOG_CRASHED:
        if (index >= 0) pc5_tf(title, sizeof(title), "%s ended unexpectedly", name);
        else pc5_tf(title, sizeof(title), "PC5 ended unexpectedly last time");
        pc5_tf(body, sizeof(body), "Its log is kept as %.120s in %.120s.", file, ui->root);
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

/* Manage › Report a problem: the game's newest kept log, as the message after a crash offers
 * it (Triangle: Report on GitHub), back to the game's menu. What PC5 saw is known when it is
 * the log the library last found a problem in; otherwise only that it went wrong (issue-...)
 * or not (log-...). */
static void report_game_log(Pc5Ui *ui, const Pc5Game *game)
{
    char path[PC5_PATH], title[200], body[700];
    Pc5LogIssue issue;

    if (pc5_logs_newest(ui->root, game->id, path, sizeof(path))) {
        pc5_tf(title, sizeof(title), "No log of %s yet", game->title);
        message(ui, 0, title, T("Play it once until the problem happens, then come back here: its log goes with the report."),
                S_GAME_MENU);
        return;
    }
    const char *name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
    if (!strcmp(ui->issue.profile, game->id) && !strcmp(ui->issue.file, path)) {
        issue = ui->issue;
    } else {
        memset(&issue, 0, sizeof(issue));
        issue.problem = !strncmp(name, "issue-", 6) ? PC5_LOG_CRASHED : PC5_LOG_FINE;
        pc5_copy(issue.profile, sizeof(issue.profile), game->id);
        pc5_copy(issue.file, sizeof(issue.file), path);
        pc5_copy(issue.exe, sizeof(issue.exe), game->executable);
    }
    pc5_tf(title, sizeof(title), "Report a problem with %s", game->title);
    pc5_tf(body, sizeof(body), "Its newest log, %.120s, is in %.120s/logs. Press Triangle for a QR code that opens "
             "GitHub's form with the game, PC5 and the game's settings filled in; attach that zip there.", name, ui->root);
    message(ui, 1, title, body, S_GAME_MENU);
    ui->report = issue;
    pc5_copy(ui->report_game, sizeof(ui->report_game), game->title);
    ui->message_report = 1;
}

/* Report on GitHub for ui->report: the form's link, as a QR code. */
static void report_open(Pc5Ui *ui)
{
    char settings[300] = "";
    int index = pc5_library_find(&ui->library, ui->report.profile);

    /* The game's own settings go with it, so a report says them without its log. */
    if (index >= 0) {
        const Pc5Game *game = &ui->library.games[index];
        Pc5Controls k;
        pc5_controls_load(ui->root, game->prefix, &k);
        snprintf(settings, sizeof(settings),
                 "%d-bit, draws with %s; Graphics API %s; Controller %s; Frame generation %s; Memory %s; CPU sharing %s%s%s",
                 game->bits, pc5_api_name((Pc5Api)game->api), pc5_graphics_tech_name(k.graphics, game->api),
                 pc5_pad_mode_name(k.mode), k.frame_generation == 2 ? "40 -> 120 (MihawkGen)" : k.frame_generation ? "on" : "off", k.memory_4gb ? "4 GB" : "Auto",
                 k.cpu_sharing ? "on" : "off", k.steam_off ? "; Steam emulator off" : "",
                 k.surround == 2 ? "; Sound 7.1" : k.surround == 1 ? "; Sound 5.1" : "");
    }
    ui->report_qr.size = 0;
    if (pc5_report_url_settings(&ui->report, ui->report_game, settings, ui->report_url, sizeof(ui->report_url)) ||
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

/* The tab to the left (-1) or right (1): Install and Settings open their pages over Home,
 * the others show in the library. */
static void settings_open(Pc5Ui *ui, int page, int row);
static void lists_make(Pc5Ui *ui);
static void hero_refresh(Pc5Ui *ui);

static int home_focus_input(Pc5Ui *ui, uint32_t k)
{
    if (ui->tab != TAB_HOME || !ui->library.count) return 0;
    if (((k & PC5_KEY_DOWN) && !ui->focus) || ((k & PC5_KEY_UP) && ui->focus)) {
        ui->focus = (k & PC5_KEY_DOWN) != 0;
        ui->dirty = 1;
        return 1;
    }
    return 0;
}

/* Square: a game in or out of the Library tab's Favourites (pc5/favourite.txt). */
static void favourite_toggle(Pc5Ui *ui, int index)
{
    char path[PC5_PATH];
    const Pc5Game *game = &ui->library.games[index];
    if (!game->prefix[0] || pc5_format(path, sizeof(path), "%s/prefixes/%s/pc5/favourite.txt", ui->root, game->prefix)) return;
    if (ui->favourite[index]) unlink(path);
    else pc5_write_file(path, "favourite\n", 10);
    ui->favourite[index] = (uint8_t)pc5_is_file(path);
    say(ui, "PC5 favourite %s=%d", game->id, ui->favourite[index]);
    lists_make(ui);
    ui->dirty = 1;
}

static void compatibility_open(Pc5Ui *ui)
{
    ui->tab = TAB_COMPATIBILITY;
    if (!ui->compat_initialized) {
        pc5_compat_init(&ui->compatibility, ui->root, &ui->platform.http);
        ui->compat_initialized = 1;
    }
    pc5_compat_refresh(&ui->compatibility);
    ui->compat_refresh_at = ui->now;
    ui->compat_detail = ui->compat_scroll = 0;
    if (!ui->compatibility_qr.size) pc5_qr_make(PC5_COMPATIBILITY, &ui->compatibility_qr);
    go(ui, S_COMPATIBILITY);
}

static void tab_move(Pc5Ui *ui, int by)
{
    int shown = ui->screen == S_SETTINGS ? (ui->tab == TAB_INSTALL ? TAB_INSTALL : TAB_SETTINGS) : ui->tab;
    int next = (shown + by + TABS) % TABS;
    ui->tab = next;
    ui->focus = 0;
    ui->dirty = 1;
    if (next == TAB_COMPATIBILITY) compatibility_open(ui);
    else if (next == TAB_INSTALL) settings_open(ui, PAGE_MAINTENANCE, 0);
    else if (next == TAB_SETTINGS) settings_open(ui, 0, 0);
    else {
        if (next == TAB_LIBRARY) lists_make(ui);
        if (ui->screen != S_LIBRARY) go(ui, S_LIBRARY);
    }
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
        const char *internal = !strncmp(ui->root, "/data/", 6) ? "/data" : ui->root;
        if (ui->files_look == 2 && pc5_files_within(internal, dir)) base = internal;
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
    if (ui->files_look) {
        FilesPlace *place = &ui->files_place[n++];
        pc5_copy(place->name, sizeof(place->name), T("Internal storage"));
        pc5_copy(place->path, sizeof(place->path), !strncmp(ui->root, "/data/", 6) ? "/data" : ui->root);
        place->read_only = ui->files_look != 2;
    }
    for (const char *const *d = files_drives(ui); *d && n < FILES_PLACES - 3; d++) {
        if (!pc5_is_dir(*d)) continue;
        FilesPlace *place = &ui->files_place[n++];
        drive_name(*d, place->name, sizeof(place->name));
        pc5_copy(place->path, sizeof(place->path), *d);
        place->read_only = 0;
    }
    if (ui->install_dir[0] && pc5_is_dir(ui->install_dir)) {
        FilesPlace *place = &ui->files_place[n++];
        pc5_copy(place->name, sizeof(place->name), T("PC5's INSTALL folder"));
        pc5_copy(place->path, sizeof(place->path), ui->install_dir);
        place->read_only = 0;
    }
    if (ui->app[0] && pc5_is_dir(ui->app)) {
        FilesPlace *place = &ui->files_place[n++];
        pc5_copy(place->name, sizeof(place->name), T("PC5's app"));
        pc5_copy(place->path, sizeof(place->path), ui->app);
        place->read_only = 1;
    }
    FilesPlace *place = &ui->files_place[n++];
    pc5_copy(place->name, sizeof(place->name), T("Whole console"));
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

static int music_config_path(Pc5Ui *ui, const Pc5Game *game, char *out, size_t size)
{
    return pc5_format(out, size, "%s/prefixes/%s/pc5/music.txt", ui->root, game->prefix);
}

/* Only the association changes here. New files are prepared by the import
 * worker before this atomic commit; legacy external paths remain readable. */
static int music_save(Pc5Ui *ui, const char *path)
{
    int index = pc5_library_find(&ui->library, ui->launch_id);
    char dest[PC5_PATH], temp[PC5_PATH], dir[PC5_PATH];
    if (index < 0 || music_config_path(ui, &ui->library.games[index], dest, sizeof(dest))) return -1;
    if (!path[0]) {
        if (unlink(dest) && errno != ENOENT) return -1;
    } else {
        if (path[0] != '/' || strchr(path, '\n') || strchr(path, '\r') ||
            pc5_format(temp, sizeof(temp), "%s.tmp", dest)) return -1;
        pc5_copy(dir, sizeof(dir), dest);
        char *slash = strrchr(dir, '/');
        if (!slash) return -1;
        *slash = 0;
        if (pc5_mkdirs(dir) || pc5_write_file(temp, path, strlen(path)) || rename(temp, dest)) {
            unlink(temp);
            return -1;
        }
    }
    ui->music_id[0] = 0;   /* read the changed selection again */
    return 0;
}

static int music_slots(Pc5Ui *ui, char *a, char *b, char *dir)
{
    int index = pc5_library_find(&ui->library, ui->launch_id);
    if (index < 0 || pc5_format(dir, PC5_PATH, "%s/prefixes/%s/pc5/music", ui->root,
                                ui->library.games[index].prefix) ||
        pc5_format(a, PC5_PATH, "%s/track-a.wav", dir) || pc5_format(b, PC5_PATH, "%s/track-b.wav", dir)) return -1;
    return 0;
}

/* Clean only the two application-owned slots, never a legacy external file. */
static void music_cleanup(Pc5Ui *ui, const char *keep)
{
    char a[PC5_PATH], b[PC5_PATH], dir[PC5_PATH];
    if (music_slots(ui, a, b, dir)) return;
    if (!keep || strcmp(keep, a)) unlink(a);
    if (!keep || strcmp(keep, b)) unlink(b);
}

static void music_import_begin(Pc5Ui *ui, const char *source)
{
    char a[PC5_PATH], b[PC5_PATH], dir[PC5_PATH], config[PC5_PATH];
    int index = pc5_library_find(&ui->library, ui->launch_id);
    Pc5Buf previous = { 0 };
    if (index < 0 || music_slots(ui, a, b, dir) || pc5_mkdirs(dir) ||
        music_config_path(ui, &ui->library.games[index], config, sizeof(config))) {
        message(ui, 0, T("Music wasn't saved"), T("PC5 couldn't create the game's music folder. Check free space."), S_FILES);
        return;
    }
    (void)pc5_read_file(config, &previous, PC5_PATH - 1);
    int use_b = previous.len == strlen(a) && !memcmp(previous.data, a, previous.len);
    pc5_buf_free(&previous);
    if (pc5_music_import_start(&ui->music_import, source, use_b ? b : a)) {
        message(ui, 0, T("Music wasn't saved"), T("PC5 couldn't start importing the music. Try again."), S_FILES);
        return;
    }
    go(ui, S_MUSIC_IMPORT);
}

static int video_paths(Pc5Ui *ui, const Pc5Game *game, char *config, char *dir, char *a, char *b)
{
    return pc5_format(config, PC5_PATH, "%s/prefixes/%s/pc5/background-video.txt", ui->root, game->prefix) ||
           pc5_format(dir, PC5_PATH, "%s/prefixes/%s/pc5/background-video", ui->root, game->prefix) ||
           pc5_format(a, PC5_PATH, "%s/clip-a.mp4", dir) || pc5_format(b, PC5_PATH, "%s/clip-b.mp4", dir);
}

static int video_save(Pc5Ui *ui, const char *path)
{
    int index = pc5_library_find(&ui->library, ui->launch_id);
    char config[PC5_PATH], dir[PC5_PATH], a[PC5_PATH], b[PC5_PATH], temp[PC5_PATH];
    if (index < 0 || video_paths(ui, &ui->library.games[index], config, dir, a, b)) return -1;
    if (!path[0]) {
        if (unlink(config) && errno != ENOENT) return -1;
    } else {
        if ((strcmp(path, a) && strcmp(path, b)) || pc5_format(temp, sizeof(temp), "%s.tmp", config)) return -1;
        if (pc5_write_file(temp, path, strlen(path)) || rename(temp, config)) { unlink(temp); return -1; }
    }
    pc5_video_stop(&ui->video);
    ui->video_id[0] = 0;
    if (strcmp(path, a)) unlink(a);
    if (strcmp(path, b)) unlink(b);
    return 0;
}

static void video_import_begin(Pc5Ui *ui, const char *source)
{
    int index = pc5_library_find(&ui->library, ui->launch_id);
    char config[PC5_PATH], dir[PC5_PATH], a[PC5_PATH], b[PC5_PATH];
    Pc5Buf previous = { 0 };
    if (index < 0 || video_paths(ui, &ui->library.games[index], config, dir, a, b) || pc5_mkdirs(dir)) {
        message(ui, 0, T("Video wasn't saved"), T("PC5 couldn't create the game's video folder. Check free space."), S_FILES);
        return;
    }
    (void)pc5_read_file(config, &previous, PC5_PATH - 1);
    int use_b = previous.len == strlen(a) && !memcmp(previous.data, a, previous.len);
    pc5_buf_free(&previous);
    if (pc5_video_import_start(&ui->video_import, source, use_b ? b : a)) {
        message(ui, 0, T("Video wasn't saved"), T("This build couldn't start the video importer."), S_FILES);
        return;
    }
    go(ui, S_VIDEO_IMPORT);
}

static void video_follow(Pc5Ui *ui)
{
    int eligible = (ui->screen == S_LIBRARY || ui->screen == S_GAME_MENU) && ui->tab != TAB_INSTALL &&
                   ui->selected >= 0 && ui->selected < ui->library.count;
    if (ui->screen == S_LIBRARY && ui->tab == TAB_LIBRARY && !ui->list_count[ui->list_at]) eligible = 0;
    const Pc5Game *game = eligible ? &ui->library.games[ui->selected] : NULL;
    const char *id = game ? game->id : "";
    if (strcmp(ui->video_id, id) || (!id[0] && ui->video_want[0])) {
        pc5_video_stop(&ui->video);
        pc5_copy(ui->video_id, sizeof(ui->video_id), id);
        ui->video_want[0] = 0;
        if (game) {
            char config[PC5_PATH], dir[PC5_PATH], a[PC5_PATH], b[PC5_PATH];
            Pc5Buf saved = { 0 };
            if (!video_paths(ui, game, config, dir, a, b) && !pc5_read_file(config, &saved, PC5_PATH - 1) &&
                saved.len && !memchr(saved.data, 0, saved.len)) {
                if ((saved.len == strlen(a) && !memcmp(saved.data, a, saved.len)) ||
                    (saved.len == strlen(b) && !memcmp(saved.data, b, saved.len))) {
                    memcpy(ui->video_want, saved.data, saved.len);
                    ui->video_want[saved.len] = 0;
                }
            }
            pc5_buf_free(&saved);
        }
        ui->dirty = 1;
    }
    /* Open on the first eligible update; the worker supplies the first frame
     * as soon as it can, without an intentional cover-selection delay. */
    if (ui->video_want[0] && !ui->video.running) {
        ui->video_report_at = ui->now;
        ui->video_report_shown = 0;
        Pc5Appearance options;
        pc5_appearance_load(ui->root, game->prefix, &options);
        if (pc5_video_start_options(&ui->video, ui->video_want, &options)) {
            say(ui, "PC5 background video: couldn't start; keeping image");
            ui->video_want[0] = 0;
        }
    }
    if (pc5_video_poll(&ui->video)) ui->dirty = 1;
    if (ui->video.running && ui->now - ui->video_report_at >= 5000) {
        unsigned shown = atomic_load(&ui->video.shown);
        unsigned tenths = (unsigned)((uint64_t)(shown - ui->video_report_shown) * 10000 / (ui->now - ui->video_report_at));
        say(ui, "PC5 background video fps=%u.%u decoded=%u dropped=%u", tenths / 10, tenths % 10,
            atomic_load(&ui->video.decoded), atomic_load(&ui->video.dropped));
        ui->video_report_at = ui->now;
        ui->video_report_shown = shown;
    }
    if (ui->video.running && atomic_load(&ui->video.done)) {
        if (ui->video.failed) say(ui, "PC5 background video: %s; keeping image", ui->video.error);
        pc5_video_stop(&ui->video);
        ui->video_want[0] = 0;
        ui->dirty = 1;
    }
}

static void music_follow(Pc5Ui *ui)
{
    if (!ui->platform.music && !ui->platform.music_options) return;
    int eligible = (ui->screen == S_LIBRARY || ui->screen == S_GAME_MENU) &&
                   ui->tab != TAB_INSTALL && ui->selected >= 0 && ui->selected < ui->library.count;
    if (ui->screen == S_LIBRARY && ui->tab == TAB_LIBRARY && !ui->list_count[ui->list_at]) eligible = 0;
    const Pc5Game *game = eligible ? &ui->library.games[ui->selected] : NULL;
    const char *id = game ? game->id : "";
    if (strcmp(ui->music_id, id) || (!id[0] && ui->music_want[0])) {
        if (ui->music_playing[0]) library_music_stop(ui);
        ui->music_playing[0] = ui->music_want[0] = 0;
        pc5_copy(ui->music_id, sizeof(ui->music_id), id);
        ui->music_since = ui->now;
        char path[PC5_PATH];
        Pc5Buf file = { 0 };
        if (game && !music_config_path(ui, game, path, sizeof(path)) &&
            !pc5_read_file(path, &file, PC5_PATH - 1) && file.len && file.data[0] == '/' &&
            !memchr(file.data, 0, file.len) && !memchr(file.data, '\n', file.len) && !memchr(file.data, '\r', file.len)) {
            memcpy(ui->music_want, file.data, file.len);
            ui->music_want[file.len] = 0;
        }
        pc5_buf_free(&file);
    }
    if (ui->music_want[0] && !ui->music_playing[0] && ui->now - ui->music_since >= 350) {
        Pc5Appearance options;
        pc5_appearance_load(ui->root, game->prefix, &options);
        if (ui->platform.music_options) ui->platform.music_options(ui->platform.context, ui->music_want, options.volume, options.music_start);
        else ui->platform.music(ui->platform.context, ui->music_want);
        pc5_copy(ui->music_playing, sizeof(ui->music_playing), ui->music_want);
    }
}

static void files_open(Pc5Ui *ui, int game)
{
    ui->files_music = 0;
    ui->files_video = 0;
    ui->files_loading = 0;
    ui->files_look = 0;
    ui->files_game = game;
    ui->files_top[0] = ui->files_path[0] = 0;
    ui->files_changed = 0;
    if (game) {
        if (pc5_game_folder(ui->root, ui->launch_id, ui->files_top, sizeof(ui->files_top)) || !pc5_is_dir(ui->files_top)) {
            int index = pc5_library_find(&ui->library, ui->launch_id);
            ui->files_top[0] = 0;
            if (index >= 0 && ui->library.games[index].usb)   /* played from its USB drive */
                message(ui, 0, T("Plug in the game's USB drive"), T("Its files are on the USB drive it plays from."), S_GAME_MENU);
            else
                message(ui, 0, T("PC5 can't find the game's folder"), T("Its files may be missing: uninstall it and install it again."),
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

#include "pc5_ui_enhancements_input.h"

/* The game the menu is on, for its loading picture: its folder of its own (prefixes/<prefix>/pc5). */
static int loading_path(Pc5Ui *ui, const Pc5Game *game, char *path, size_t size)
{
    char dir[PC5_PATH];
    if (!game || !game->prefix[0] || !strcmp(game->prefix, "default") ||
        pc5_format(dir, sizeof(dir), "%s/prefixes/%s/pc5", ui->root, game->prefix) || pc5_mkdirs(dir) ||
        pc5_format(path, size, "%s/loading.png", dir))
        return -1;
    return 0;
}

static void files_close(Pc5Ui *ui);

/* A picture file (a JPEG or PNG on a USB drive, the console or from the phone) as the loading
 * picture of the game PC5 is on (ui->launch_id); then back to the loading screen's rows. */
static void loading_import(Pc5Ui *ui, const char *source, Screen back)
{
    int index = pc5_library_find(&ui->library, ui->launch_id);
    const Pc5Game *game = index >= 0 ? &ui->library.games[index] : NULL;
    char path[PC5_PATH];
    Pc5Buf data = { 0 };
    Pc5Image image = { 0 };
    int failed = !game || loading_path(ui, game, path, sizeof(path)) || pc5_read_file(source, &data, 20u << 20) ||
                 pc5_image_decode(data.data, data.len, &image) || pc5_loading_save(&image, path);

    pc5_buf_free(&data);
    pc5_image_free(&image);
    ui->loading_saved = !failed;
    say(ui, "PC5 loading picture %s from %s: %s", game ? game->id : "-", source, failed ? "not saved" : "saved");
    if (failed) {
        message(ui, 0, T("Picture wasn't saved"), T("Choose a readable JPEG or PNG image, at most 20 MB. Your previous picture was kept."),
                back);
        return;
    }
    if (ui->screen == S_FILES) files_close(ui);
    game_menu_at(ui, GR_LOADING);
    message(ui, 1, T("Loading screen saved"), T("It shows the next time the game starts."), S_GAME_MENU);
}

/* The game's background picture as its loading picture. */
static void loading_from_background(Pc5Ui *ui, const Pc5Game *game)
{
    char from[PC5_PATH], path[PC5_PATH];
    Pc5Image image = { 0 };
    int failed = pc5_format(from, sizeof(from), "%s/prefixes/%s/pc5/background.bin", ui->root, game->prefix) ||
                 loading_path(ui, game, path, sizeof(path)) || pc5_cover_load(from, &image, NULL) ||
                 pc5_loading_save(&image, path);

    pc5_image_free(&image);
    say(ui, "PC5 loading picture %s from its background: %s", game->id, failed ? "not saved" : "saved");
    game_menu_at(ui, GR_LOADING);
    if (failed) message(ui, 0, T("Picture wasn't saved"), T("It has no background picture yet: choose one in Background first."), S_GAME_MENU);
    else message(ui, 1, T("Loading screen saved"), T("Its background shows the next time the game starts."), S_GAME_MENU);
}

/* Back to PC5's own loading picture for the game. */
static void loading_clear(Pc5Ui *ui, const Pc5Game *game)
{
    char path[PC5_PATH];
    if (!pc5_loading_chosen(ui->root, game->prefix, path, sizeof(path))) unlink(path);
    say(ui, "PC5 loading picture %s: PC5's", game->id);
    game_menu_at(ui, GR_LOADING);
    message(ui, 1, T("Loading screen"), T("It shows PC5's own loading screen again."), S_GAME_MENU);
}

static int phone_row_kind(GameRow row)
{
    return row == GR_COVER ? PC5_PHONE_COVER : row == GR_BACKGROUND_IMAGE ? PC5_PHONE_BACKGROUND :
           row == GR_ICON ? PC5_PHONE_ICON : (row == GR_MUSIC || row == GR_MUSIC_FILE) ? PC5_PHONE_MUSIC :
           (row == GR_LOADING || row == GR_LOADING_FILE) ? PC5_PHONE_LOADING :
           (row == GR_BACKGROUND_VIDEO || row == GR_VIDEO_FILE) ? PC5_PHONE_VIDEO : -1;
}

static void phone_end(Pc5Ui *ui)
{
    pc5_web_phone_end();
    ui->phone_library = ui->phone_manager_pending = 0;
    if (ui->phone_temporary) { pc5_web_stop(); pc5_web_phone_only(0); ui->phone_temporary = 0; }
}

static void phone_source_remove(Pc5Ui *ui)
{
    if (ui->phone_source[0]) unlink(ui->phone_source);
    ui->phone_source[0] = 0;
}

static void phone_open(Pc5Ui *ui, const Pc5Game *game, int kind)
{
    Pc5WebStatus status;
    Screen return_to = kind == PC5_PHONE_LIBRARY ? S_SETTINGS : S_GAME_MENU;
    if (kind == PC5_PHONE_LIBRARY) settings_open(ui, PAGE_NETWORK, 4);
    if (!game->pc5 || !game->prefix[0] || !strcmp(game->prefix, "default")) {
        message(ui, 0, T("PC5 didn't install this game"), T("Phone uploads need a game with its own PC5 folder."), return_to);
        return;
    }
    pc5_web_status(&status);
    ui->phone_temporary = !status.running;
    if (ui->phone_temporary) {
        const Pc5WebConfig config = { ui->root, "" };
        pc5_web_phone_only(1);
        if (pc5_web_start(&config, PC5_WEB_PORT, ui->web_error, sizeof(ui->web_error))) {
            phone_end(ui);
            message(ui, 0, T("Phone connection unavailable"), TX(ui->web_error), return_to);
            return;
        }
    }
    phone_source_remove(ui);
    ui->phone_kind = kind;
    ui->phone_library = kind == PC5_PHONE_LIBRARY;
    if ((kind == PC5_PHONE_LIBRARY ? pc5_web_appearance_begin(ui->web_url, sizeof(ui->web_url)) : pc5_web_phone_begin(kind, game->title, ui->web_url, sizeof(ui->web_url))) || pc5_qr_make(ui->web_url, &ui->web_qr)) {
        phone_end(ui);
        message(ui, 0, T("Phone connection unavailable"), T("Check the network, then try again. The phone and PS5 must share a network or phone hotspot."), return_to);
        return;
    }
    go(ui, S_PHONE_QR);
}

static void phone_picture_use(Pc5Ui *ui, const Pc5Game *game)
{
    Pc5Buf data = { 0 };
    Pc5Image original = { 0 }, composed = { 0 };
    int kind = ui->phone_kind;
    int failed = pc5_read_file(ui->phone_source, &data, 20u << 20) || pc5_image_decode(data.data, data.len, &original);
    if (!failed) failed = kind == PC5_PHONE_COVER ? pc5_cover_compose(&original, &composed) :
                          kind == PC5_PHONE_ICON ? pc5_icon_compose(&original, &composed) :
                          pc5_background_compose(&original, &composed);
    pc5_buf_free(&data); pc5_image_free(&original);
    phone_source_remove(ui);
    if (!failed) {
        picture_close(ui);
        ui->pick = pc5_alloc(sizeof(*ui->pick));
        if (!ui->pick) failed = 1;
    }
    if (failed) {
        pc5_image_free(&composed);
        message(ui, 0, T("Picture wasn't saved"), T("Choose a readable JPEG or PNG image, at most 20 MB. Your previous picture was kept."), S_GAME_MENU);
        return;
    }
    ui->pick->count = 1;
    ui->pick_kind = kind == PC5_PHONE_COVER ? PICK_COVER : kind == PC5_PHONE_ICON ? PICK_ICON : PICK_BACKGROUND;
    ui->pick_at = ui->pick_now = 0;
    ui->pick_count = 1;
    ui->pick_image = composed;
    ui->pick_tint = pc5_image_tint(&composed);
    pick_use(ui, game);
}

static void phone_update(Pc5Ui *ui, uint32_t k, uint32_t back)
{
    Pc5PhoneStatus status;
    pc5_web_phone_status(&status);
    ui->dirty = 1;
    if ((k & back) || !status.active) {
        int library = ui->phone_library;
        phone_end(ui);
        if (library) settings_open(ui, PAGE_NETWORK, 4);
        else go(ui, S_GAME_MENU);
        return;
    }
    if (!pc5_web_phone_take(ui->phone_source, sizeof(ui->phone_source))) return;
    if (ui->phone_library) {
        if (!pc5_web_appearance_target(ui->launch_id, sizeof(ui->launch_id), &ui->phone_kind)) {
            phone_source_remove(ui); pc5_web_appearance_finish(0); return;
        }
        ui->phone_manager_pending = 1; ui->phone_import_ok = 0;
        if (!ui->config.tiles_root || !pc5_home_find(ui->config.tiles_root, ui->launch_id, ui->tile)) ui->tile[0] = 0;
    } else phone_end(ui);
    int index = pc5_library_find(&ui->library, ui->launch_id);
    if (index < 0) { phone_source_remove(ui); go(ui, S_LIBRARY); return; }
    if (ui->phone_kind == PC5_PHONE_MUSIC || ui->phone_kind == PC5_PHONE_VIDEO) {
        /* Reuse the existing importer and its commit/cancellation/error UI. */
        files_open(ui, 0);
        ui->files_music = ui->phone_kind == PC5_PHONE_MUSIC;
        ui->files_video = ui->phone_kind == PC5_PHONE_VIDEO;
        if (ui->files_music) music_import_begin(ui, ui->phone_source);
        else video_import_begin(ui, ui->phone_source);
        if (ui->screen != S_MUSIC_IMPORT && ui->screen != S_VIDEO_IMPORT) phone_source_remove(ui);
    } else if (ui->phone_kind == PC5_PHONE_LOADING) {
        loading_import(ui, ui->phone_source, S_GAME_MENU);
        phone_source_remove(ui);
        ui->phone_import_ok = ui->loading_saved;
    } else {
        phone_picture_use(ui, &ui->library.games[index]);
        ui->phone_import_ok = ui->screen != S_MESSAGE;
    }
}

static void files_close(Pc5Ui *ui)
{
    pc5_files_list_free(&ui->files);
    pc5_free(ui->files_ticked);
    ui->files_ticked = NULL;
    ui->files_path[0] = 0;
    say(ui, "PC5 files close changed=%d", ui->files_changed);
    if (ui->files_changed) library_reload(ui, NULL);
    if (ui->files_look) {
        ui->files_look = 0; go(ui, S_MEDIA_OPTIONS);
    } else if (ui->files_video) {
        ui->files_video = 0;
        game_menu_at(ui, GR_VIDEO_FILE);
        go(ui, S_GAME_MENU);
    } else if (ui->files_music) {
        ui->files_music = 0;
        game_menu_at(ui, GR_MUSIC_FILE);
        go(ui, S_GAME_MENU);
    } else if (ui->files_loading) {
        ui->files_loading = 0;
        game_menu_at(ui, GR_LOADING_FILE);
        go(ui, S_GAME_MENU);
    } else if (ui->files_game) {
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
static int files_edit_open(Pc5Ui *ui, int quiet);

/* ✕: a place or a folder opens; a text file opens to be changed (Edit); another file shows what
 * can be done with it. */
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
        if (ui->files_video) {
            if (files_join(next, sizeof(next), ui->files_path, item->name)) return;
            video_import_begin(ui, next);
            return;
        }
        if (ui->files_look == 2) return; /* This picker chooses folders, never files. */
        if (ui->files_look == 1) {
            if (files_join(next, sizeof(next), ui->files_path, item->name) || !pc5_ends_with_ci(item->name, ".pc5look")) {
                message(ui, 0, T("Choose an appearance backup"), T("Choose a .pc5look backup made by PC5."), S_FILES); return;
            }
            pc5_copy(ui->look_source, sizeof(ui->look_source), next);
            ui->button = 0; go(ui, S_LOOK_CONFIRM); return;
        }
        if (ui->files_loading) {
            if (files_join(next, sizeof(next), ui->files_path, item->name) ||
                (!pc5_ends_with_ci(item->name, ".jpg") && !pc5_ends_with_ci(item->name, ".jpeg") &&
                 !pc5_ends_with_ci(item->name, ".png"))) {
                message(ui, 0, T("Choose a picture"), T("Choose a JPEG or PNG picture."), S_FILES);
                return;
            }
            loading_import(ui, next, S_FILES);
            return;
        }
        if (ui->files_music) {
            if (files_join(next, sizeof(next), ui->files_path, item->name) ||
                (!pc5_ends_with_ci(item->name, ".mp3") && !pc5_ends_with_ci(item->name, ".wav"))) {
                message(ui, 0, T("Choose a music file"), T("Choose an MP3 or a 16-bit PCM WAV file."), S_FILES);
                return;
            }
            music_import_begin(ui, next);
            return;
        }
        if (files_edit_open(ui, 1)) files_menu_open(ui);   /* not text: what can be done with it */
        return;
    }
    if (files_join(next, sizeof(next), ui->files_path, item->name)) {
        message(ui, 0, T("PC5 can't open this folder"), T("Its path is too long."), S_FILES);
        return;
    }
    pc5_copy(ui->files_path, sizeof(ui->files_path), next);
    files_load(ui, NULL);
    ui->dirty = 1;
}

/* What △ offers, as the folder and the items chosen allow. */
typedef enum FilesAction { FA_COPY, FA_CUT, FA_PASTE, FA_RENAME, FA_NEW_FOLDER, FA_DELETE, FA_TICK_ALL, FA_EDIT } FilesAction;
enum { FILES_ACTIONS = 8 };

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
    /* one file, not ticked: Edit (View where nothing changes), as ✕ opens a text file */
    if (!files_ticks(ui) && ui->files_at < ui->files.count && !ui->files.items[ui->files_at].dir &&
        ui->files.items[ui->files_at].size <= PC5_TEXTDOC_MAX)
        rows[n++] = FA_EDIT;
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
        pc5_tf(title, sizeof(title), op == PC5_FILES_DELETE ? "Nothing was deleted" : op == PC5_FILES_MOVE ? "Nothing was moved"
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
        if (!done && skipped) message(ui, 1, T("Nothing was pasted"), T("Those items are here already, and were left as they were."), S_FILES);
        else go(ui, S_FILES);
    } else if (state == PC5_FILES_STOPPED) {
        message(ui, 1, T("Stopped"),
                op == PC5_FILES_DELETE ? T("What was deleted so far is gone; the rest is still here.")
                : op == PC5_FILES_MOVE ? T("What was moved so far is here. Nothing was deleted from where it came from.")
                                       : T("What was copied so far is here; the file being copied when it stopped was deleted."),
                S_FILES);
    } else {
        pc5_tf(title, sizeof(title), op == PC5_FILES_DELETE ? "Not everything was deleted"
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
        message(ui, 0, T("That name can't be used"), why, S_FILES);
        return;
    }
    if (files_join(target, sizeof(target), ui->files_path, name)) {
        message(ui, 0, T("That name can't be used"), T("The path would be too long."), S_FILES);
        return;
    }
    if (ui->files_typing == 0) {
        if (ui->files_at >= ui->files.count) { go(ui, S_FILES); return; }
        const char *old = ui->files.items[ui->files_at].name;
        if (!strcmp(old, name) || files_join(from, sizeof(from), ui->files_path, old)) { go(ui, S_FILES); return; }
        /* Only its case changed: on exFAT the new name is the old one. */
        if (pc5_casecmp(old, name) && pc5_exists(target)) {
            pc5_tf(body, sizeof(body), "This folder has %.200s already.", name);
            message(ui, 0, T("That name is taken"), body, S_FILES);
            return;
        }
        if (rename(from, target)) {
            pc5_tf(body, sizeof(body), "%s.", strerror(errno));
            say(ui, "PC5 problem files rename %s: %s", from, body);
            message(ui, 0, T("PC5 can't rename it"), body, S_FILES);
            return;
        }
        say(ui, "PC5 files renamed %s to %s", from, name);
    } else {
        if (pc5_exists(target)) {
            pc5_tf(body, sizeof(body), "This folder has %.200s already.", name);
            message(ui, 0, T("That name is taken"), body, S_FILES);
            return;
        }
        if (mkdir(target, 0777)) {
            pc5_tf(body, sizeof(body), "%s.", strerror(errno));
            say(ui, "PC5 problem files new folder %s: %s", target, body);
            message(ui, 0, T("PC5 can't make the folder"), body, S_FILES);
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
            rc = ui->platform.keyboard_open(ui->platform.context, action == FA_RENAME ? T("Rename") : T("New folder"),
                                            action == FA_RENAME && ui->files_at < ui->files.count
                                                ? ui->files.items[ui->files_at].name : T("New folder"));
        say(ui, "PC5 keyboard open rc=%d", rc);
        if (rc >= 0) go(ui, S_FILES_TYPING);
        else message(ui, 0, T("The keyboard didn't open"), T("The console's on-screen keyboard can't be used just now."), S_FILES);
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
    case FA_EDIT:
        files_edit_open(ui, 0);   /* not text: it says why */
        break;
    }
}

/* ---- Edit: a text file a line at a time --------------------------------------------------- */

/* A line longer than this isn't changed with the console's keyboard (which holds 1024
 * characters): the web page or FTP changes it. */
enum { EDIT_LINE = 1000 };

/* The file chosen, read to be changed (or only shown, in a read-only place). quiet: one that
 * isn't text says nothing (✕ shows its actions instead). 0, or -1. */
static int files_edit_open(Pc5Ui *ui, int quiet)
{
    char path[PC5_PATH], why[PC5_ERROR];
    if (ui->files_at >= ui->files.count) return -1;
    const Pc5FileItem *item = &ui->files.items[ui->files_at];
    if (item->dir || files_join(path, sizeof(path), ui->files_path, item->name)) return -1;
    if (quiet && item->size > PC5_TEXTDOC_MAX) return -1;
    pc5_textdoc_free(&ui->edit);
    if (pc5_textdoc_read(&ui->edit, path, why, sizeof(why))) {
        say(ui, "PC5 files can't edit %s: %s", path, why);
        if (!quiet) message(ui, 0, T("PC5 can't open it as text"), why, S_FILES);
        return -1;
    }
    pc5_copy(ui->edit_path, sizeof(ui->edit_path), path);
    ui->edit_read_only = !files_writable(ui, ui->files_path);
    ui->edit_at = ui->edit_top = 0;
    say(ui, "PC5 files edit %s lines=%d encoding=%d bom=%d crlf=%d read_only=%d", path, ui->edit.count,
        (int)ui->edit.encoding, ui->edit.bom, ui->edit.crlf, ui->edit_read_only);
    go(ui, S_FILES_EDIT);
    return 0;
}

static void files_edit_close(Pc5Ui *ui)
{
    const char *slash = strrchr(ui->edit_path, '/');
    char name[PC5_FILES_NAME];
    pc5_copy(name, sizeof(name), slash ? slash + 1 : ui->edit_path);
    say(ui, "PC5 files edit closed %s changed=%d", ui->edit_path, ui->edit.changed);
    pc5_textdoc_free(&ui->edit);
    ui->edit_path[0] = 0;
    files_load(ui, name);   /* its size as it is now */
    go(ui, S_FILES);
}

/* Written back as it was found (encoding, line ends). 0, or -1 having said why. */
static int files_edit_save(Pc5Ui *ui)
{
    char why[PC5_ERROR];
    if (pc5_textdoc_write(&ui->edit, ui->edit_path, why, sizeof(why))) {
        say(ui, "PC5 problem files edit save %s: %s", ui->edit_path, why);
        message(ui, 0, T("PC5 couldn't save it"), why, S_FILES_EDIT);
        return -1;
    }
    say(ui, "PC5 files edit saved %s lines=%d", ui->edit_path, ui->edit.count);
    ui->edit.changed = 0;
    ui->files_changed = 1;
    return 0;
}

/* The keyboard for the line chosen (typing 0), or for a new line below (1) or above it (2). */
static void files_edit_type(Pc5Ui *ui, int typing)
{
    char start[EDIT_LINE + 8] = "", title[64];
    size_t len = 0;
    if (typing == 0) {
        pc5_textdoc_line(&ui->edit, ui->edit_at, &len);
        if (len > EDIT_LINE) {
            message(ui, 0, T("This line is too long to change here"),
                    T("The console's keyboard holds about 1000 characters. Change it from PC5's web page, or a computer over FTP."),
                    S_FILES_EDIT);
            return;
        }
        pc5_textdoc_copy_line(&ui->edit, ui->edit_at, start, sizeof(start));
        pc5_tf(title, sizeof(title), "Line %d", ui->edit_at + 1);
    } else {
        pc5_copy(title, sizeof(title), T("New line"));
    }
    ui->edit_typing = typing;
    int rc = ui->platform.keyboard_open ? ui->platform.keyboard_open(ui->platform.context, title, start) : -1;
    say(ui, "PC5 keyboard open rc=%d", rc);
    if (rc >= 0) go(ui, S_FILES_EDIT_TYPING);
    else message(ui, 0, T("The keyboard didn't open"), T("The console's on-screen keyboard can't be used just now."), S_FILES_EDIT);
}

/* What △ offers on a line. */
typedef enum EditAction { EA_ADD_BELOW, EA_ADD_ABOVE, EA_DELETE, EA_SAVE } EditAction;
enum { EDIT_ACTIONS = 4 };

static int files_edit_actions(const Pc5Ui *ui, EditAction rows[EDIT_ACTIONS])
{
    int n = 0;
    if (ui->edit_read_only) return 0;
    rows[n++] = EA_ADD_BELOW;
    if (ui->edit.count) rows[n++] = EA_ADD_ABOVE;
    if (ui->edit.count) rows[n++] = EA_DELETE;
    if (ui->edit.changed) rows[n++] = EA_SAVE;
    return n;
}

static void files_edit_act(Pc5Ui *ui, EditAction action)
{
    switch (action) {
    case EA_ADD_BELOW: files_edit_type(ui, 1); break;
    case EA_ADD_ABOVE: files_edit_type(ui, 2); break;
    case EA_DELETE:
        if (pc5_textdoc_delete(&ui->edit, ui->edit_at)) say(ui, "PC5 problem files edit delete line %d", ui->edit_at + 1);
        if (ui->edit_at >= ui->edit.count) ui->edit_at = ui->edit.count ? ui->edit.count - 1 : 0;
        go(ui, S_FILES_EDIT);
        break;
    case EA_SAVE:
        if (!files_edit_save(ui)) go(ui, S_FILES_EDIT);
        break;
    }
}

static void files_edit_update(Pc5Ui *ui, uint32_t k)
{
    const uint32_t accept = PC5_KEY_ACCEPT, back = PC5_KEY_BACK;
    switch (ui->screen) {
    case S_FILES_EDIT: {
        int count = ui->edit.count, before = ui->edit_at;
        list_move(&ui->edit_at, count, k);
        if ((k & PC5_KEY_L1) && count) ui->edit_at = ui->edit_at > 10 ? ui->edit_at - 10 : 0;
        if ((k & PC5_KEY_R1) && count) ui->edit_at = ui->edit_at + 10 < count ? ui->edit_at + 10 : count - 1;
        if (ui->edit_at != before) ui->dirty = 1;
        if (k & back) {
            if (ui->edit.changed) {
                ui->button = 0;   /* Keep changing first */
                go(ui, S_FILES_EDIT_LEAVE);
            } else {
                files_edit_close(ui);
            }
        } else if ((k & accept) && !ui->edit_read_only) {
            files_edit_type(ui, count ? 0 : 1);   /* an empty file: its first line */
        } else if ((k & PC5_KEY_TRIANGLE) && !ui->edit_read_only) {
            ui->menu = 0;
            go(ui, S_FILES_EDIT_MENU);
        }
        break;
    }
    case S_FILES_EDIT_MENU: {
        EditAction rows[EDIT_ACTIONS];
        int count = files_edit_actions(ui, rows);
        if (!count) { go(ui, S_FILES_EDIT); break; }
        if (ui->menu >= count) ui->menu = count - 1;
        if (list_move(&ui->menu, count, k)) ui->dirty = 1;
        if (k & (back | PC5_KEY_TRIANGLE)) go(ui, S_FILES_EDIT);
        else if (k & accept) files_edit_act(ui, rows[ui->menu]);
        break;
    }
    case S_FILES_EDIT_TYPING: {
        int state = ui->platform.keyboard_state ? ui->platform.keyboard_state(ui->platform.context) : -1;
        if (state == 1) break;   /* still up */
        char typed[4 * 1024 + 64] = "";
        int rc = ui->platform.keyboard_close ? ui->platform.keyboard_close(ui->platform.context, typed, sizeof(typed)) : -1, failed = 0;
        say(ui, "PC5 keyboard closed rc=%d typed=%zu", rc, strlen(typed));
        if (rc == 1) {
            if (ui->edit_typing == 0) {
                failed = pc5_textdoc_set(&ui->edit, ui->edit_at, typed);
            } else {
                int at = !ui->edit.count ? 0 : ui->edit_typing == 1 ? ui->edit_at + 1 : ui->edit_at;
                failed = pc5_textdoc_insert(&ui->edit, at, typed);
                if (!failed) ui->edit_at = at;
            }
            if (failed) say(ui, "PC5 problem files edit: the line typed wasn't taken");
        }
        go(ui, S_FILES_EDIT);   /* cancelled: as it was */
        break;
    }
    case S_FILES_EDIT_LEAVE:
        /* Cancel (back to the file), Save, Don't save */
        if ((k & PC5_KEY_LEFT) && ui->button > 0) { ui->button--; ui->dirty = 1; }
        if ((k & PC5_KEY_RIGHT) && ui->button < 2) { ui->button++; ui->dirty = 1; }
        if (k & back) go(ui, S_FILES_EDIT);
        else if ((k & accept) && ui->now - ui->entered > 300) {
            if (ui->button == 0) go(ui, S_FILES_EDIT);
            else if (ui->button == 1) { if (!files_edit_save(ui)) files_edit_close(ui); }
            else files_edit_close(ui);
        }
        break;
    default:
        break;
    }
}

static void look_export_here(Pc5Ui *ui)
{
    char path[PC5_PATH], part[PC5_PATH];
    if (!files_writable(ui, ui->files_path)) {
        message(ui, 0, T("Choose a writable folder"), T("Open a folder on internal storage or a USB drive, then choose Save here."), S_FILES);
        return;
    }
    int found = 0;
    for (int suffix = 0; suffix < 1000; ++suffix) {
        if (pc5_format(path, sizeof(path), "%s/%s-%llu-%d.pc5look", ui->files_path, ui->launch_id,
                       (unsigned long long)ui->now, suffix) || pc5_format(part, sizeof(part), "%s.part", path)) break;
        if (!pc5_exists(path) && !pc5_exists(part)) { found = 1; break; }
    }
    if (!found) {
        message(ui, 0, T("Backup wasn't saved"), T("Choose another folder. The backup name is unavailable or its path is too long."), S_FILES);
        return;
    }
    files_close(ui);
    look_begin(ui, path, 0);
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
        else if (ui->files_look == 2 && (k & PC5_KEY_OPTIONS)) look_export_here(ui);
        else if ((k & accept) && count) files_enter(ui);
        else if (!ui->files_look && !ui->files_music && !ui->files_video && (k & PC5_KEY_SQUARE) && ui->files_path[0] && ui->files_ticked && ui->files_at < ui->files.count) {
            ui->files_ticked[ui->files_at] = !ui->files_ticked[ui->files_at];
            ui->dirty = 1;
        } else if (!ui->files_look && !ui->files_music && !ui->files_video && (k & PC5_KEY_TRIANGLE)) {
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

/* Plays the game in launch_id: its program, or with other programs offered at Play
 * (an expansion with a program of its own), a choice of them first. back: where Back goes. */
static void play(Pc5Ui *ui, Screen back)
{
    Pc5Info info;
    Pc5Offered offered[PC5_OFFERED];
    int count = pc5_programs_load(ui->root, ui->launch_id, offered, PC5_OFFERED);

    ui->play_count = 0;
    if (count && !pc5_info_read(ui->root, ui->launch_id, &info)) {
        char folder[PC5_PATH], file[PC5_PATH];
        Pc5Offered *first = &ui->play[ui->play_count++];
        pc5_copy(first->exe, sizeof(first->exe), info.exe);
        first->bits = info.bits;
        first->api = info.api;
        for (int i = 0; i < count; i++) {
            if (!strcmp(offered[i].exe, info.exe)) continue;
            /* one whose file is gone (an update took it away) isn't offered */
            if (!pc5_game_folder(ui->root, ui->launch_id, folder, sizeof(folder)) &&
                !pc5_join(file, sizeof(file), folder, offered[i].exe) && !pc5_is_file(file))
                continue;
            ui->play[ui->play_count++] = offered[i];
        }
    }
    if (ui->play_count < 2) {
        go(ui, S_LAUNCH);
        return;
    }
    ui->play_back = (int)back;
    ui->menu = 0;
    ui->list_top = 0;
    go(ui, S_PLAY_WHICH);
}

/* The game made ready and the loading picture faded in: the console is asked to start it. Back
 * here only when it refused. */
static void launch_hand_over(Pc5Ui *ui)
{
    int index = pc5_library_find(&ui->library, ui->launch_id);
    char title[200];

    ui->launch_ready = 0;
    pc5_video_stop(&ui->video);
    library_music_stop(ui);
    if (ui->compat_initialized) pc5_compat_wait(&ui->compatibility);
    if (ui->platform.play) ui->platform.play(ui->platform.context, ui->launch_id);
    pc5_tf(title, sizeof(title), "%s didn't start", index >= 0 ? ui->library.games[index].title : T("The game"));
    say(ui, "PC5 problem play %s: the console refused to start it", ui->launch_id);
    message(ui, 0, title, T("The console refused to start it. Try again, or restart PC5."), S_LIBRARY);
}


/* How long PC5's logo shows as it opens, and how long the screen under it takes to fade in. */
enum { BOOT_SHOW_MS = 1700, BOOT_FADE_MS = 500 };

int pc5_ui_update(Pc5Ui *ui, const Pc5Input *input, uint64_t now_ms)
{
    int from_phone;          /* an import of a file a phone sent */
    Screen back_to;          /* where its message goes back to */
    uint32_t k;
    uint32_t accept = PC5_KEY_ACCEPT, back = PC5_KEY_BACK;
    float dt;

    if (!ui->last_frame) ui->last_frame = now_ms;
    dt = (float)(now_ms - ui->last_frame) / 1000.0f;
    if (dt > 0.1f) dt = 0.1f;
    ui->last_frame = now_ms;
    ui->now = now_ms;
    k = keys(ui, input);
    /* While the logo shows as PC5 opens, the buttons do nothing: a ✕ pressed then would start
     * the game under it before the library is even seen. */
    if (ui->config.boot && !ui->boot_at) ui->boot_at = now_ms ? now_ms : 1;
    if (ui->config.boot && now_ms - ui->boot_at < BOOT_SHOW_MS + BOOT_FADE_MS) k = 0;

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
                if (ui->settings.ftp_server) ftp_apply(ui);
            } else {
                ui->install_dir[0] = 0;
            }
            if (ui->settings.web_page) web_apply(ui);
        }
        if (pc5_recover_step(ui->root, &ui->remove, 14)) { ui->dirty = 1; break; }
        pc5_remove_end(&ui->remove);
        /* The API keys, its file made for the player when there is none. */
        pc5_api_key(ui->root, PC5_STEAMGRIDDB_KEY, ui->griddb_key, sizeof(ui->griddb_key));
        /* The INSTALL folder's games, once anything cut short there is put right. */
        if (!ui->install_scanned) {
            ui->install_scanned = 1;
            usb_install_refresh(ui);
            ui->usb_looked = ui->now;
            if (ui->config.opened) ui->install_waiting = install_waiting_count(ui);
            say(ui, "PC5 install folders %s usb_drives=%d games=%d", ui->install_dir[0] ? ui->install_dir : "-", ui->usb_seen,
                ui->install_waiting);
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
                message(ui, 0, T("This tile's game can't start"),
                        index < 0 ? T("It isn't in PC5 any more. Take the tile off the home screen, or install the "
                                    "game again.") : TX(ui->library.games[index].problem), S_LIBRARY);
                ui->start[0] = 0;
                break;
            }
            queue_missing_covers(ui);
            /* Each time the player opens PC5; after a game (the title restarts), at most once an hour. */
            if (ui->settings.check_updates && (ui->config.opened || now <= 0 ||
                                               now - ui->update.checked >= PC5_UPDATE_EVERY_S || now < ui->update.checked) &&
                update_check_start(ui))
                go(ui, S_UPDATE);   /* no thread: asked here, behind its screen */
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
            if (ui->cover_at < ui->cover_count) go(ui, S_COVERS);
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
                    message(ui, 0, T("PC5 couldn't restart"),
                            T("The new version is installed. Close PC5 from the PS button and open it again."), S_LIBRARY);
                } else {
                    to_library(ui);
                }
            }
            break;
        }
        if (step == PC5_UPGRADE_FAILED || step == PC5_UPGRADE_CANCELLED) {
            if (ui->upgrade_back == S_SETTINGS) ui->page = PAGE_SYSTEM;
            if (step == PC5_UPGRADE_CANCELLED)
                message(ui, 0, T("The update was stopped"), T("PC5 is as it was, and what was downloaded is deleted."),
                        ui->upgrade_back);
            else
                message(ui, 0, ui->upgrade.partial ? T("PC5's update isn't finished") : T("PC5 wasn't updated"),
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
        uploads_check(ui);
        usb_plugged_check(ui);
        /* the tabs: Install and Settings open their pages */
        if (k & (PC5_KEY_L1 | PC5_KEY_R1)) {
            tab_move(ui, k & PC5_KEY_R1 ? 1 : -1);
            break;
        }
        if ((k & PC5_KEY_SQUARE) && count && ui->tab <= TAB_LIBRARY && !ui->focus) {
            favourite_toggle(ui, ui->selected);
            break;
        }
        if (home_focus_input(ui, k)) break;
        /* Library: a grid of the filter's games; L2 and R2 change the filter */
        if (count && ui->tab == TAB_LIBRARY) {
            int l = ui->list_at, n = ui->list_count[l], *at = &ui->list_item[l];
            if (k & (PC5_KEY_L2 | PC5_KEY_R2)) {
                ui->list_at = (l + (k & PC5_KEY_R2 ? 1 : LISTS - 1)) % LISTS;
                l = ui->list_at;
                n = ui->list_count[l];
                at = &ui->list_item[l];
            }
            if ((k & PC5_KEY_LEFT) && *at > 0) (*at)--;
            if ((k & PC5_KEY_RIGHT) && *at + 1 < n) (*at)++;
            if ((k & PC5_KEY_UP) && *at >= GRID_COLUMNS) *at -= GRID_COLUMNS;
            if ((k & PC5_KEY_DOWN) && *at + GRID_COLUMNS < n) *at += GRID_COLUMNS;
            else if ((k & PC5_KEY_DOWN) && n && *at / GRID_COLUMNS < (n - 1) / GRID_COLUMNS) *at = n - 1;
            if (n) ui->selected = ui->lists[l][*at];
            k &= ~(uint32_t)(PC5_KEY_LEFT | PC5_KEY_RIGHT | PC5_KEY_L2 | PC5_KEY_R2);
            ui->dirty = 1;
            if (!n && (k & (accept | PC5_KEY_TRIANGLE))) break;
        }
        if (count) {
            if (k & PC5_KEY_LEFT && ui->selected > 0) ui->selected--;
            if (k & PC5_KEY_RIGHT && ui->selected + 1 < count) ui->selected++;
            if (k & (PC5_KEY_L2 | PC5_KEY_R2)) {
                /* to the first game of the previous or next letter */
                int letter = letter_of(ui->library.games[ui->selected].title), i = ui->selected;
                if (k & PC5_KEY_R2) {
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
                    play(ui, S_LIBRARY);
                } else {
                    char title[200], body[400];
                    pc5_tf(title, sizeof(title), "%s can't start", game->title);
                    pc5_tf(body, sizeof(body), "%s You can uninstall it from its menu (Triangle) and install it again.",
                             game->problem[0] ? TX(game->problem) : T("Something it needs is missing."));
                    message(ui, 0, title, body, S_LIBRARY);
                }
            } else if (k & PC5_KEY_TRIANGLE) {
                game_menu_open(ui, ui->library.games[ui->selected].id);
            }
        }
        if (k & PC5_KEY_OPTIONS) {
            ui->tab = TAB_SETTINGS;
            settings_open(ui, 0, 0);
        }
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
        if (k & (PC5_KEY_L1 | PC5_KEY_R1)) {
            tab_move(ui, k & PC5_KEY_R1 ? 1 : -1);
        } else if (k & (PC5_KEY_LEFT | PC5_KEY_RIGHT)) {
            if (setting_change(ui, row, k & PC5_KEY_LEFT ? -1 : 1)) ui->dirty = 1;
        } else if (k & PC5_KEY_OPTIONS) {
            ui->tab = TAB_HOME;
            go(ui, S_LIBRARY);
        } else if ((k & PC5_KEY_TRIANGLE) && row == SR_WEB) {
            /* the page's address as a QR code, for a phone */
            Pc5WebStatus web;
            pc5_web_status(&web);
            if (web.running) {
                snprintf(ui->web_url, sizeof(ui->web_url), "http://%s", web.address);
                if (pc5_qr_make(ui->web_url, &ui->web_qr)) ui->web_qr.size = 0;
                say(ui, "PC5 web page qr=%d", ui->web_qr.size);
                go(ui, S_WEB_QR);
            }
        } else if (k & back) {
            if (ui->page == PAGE_MAINTENANCE && ui->tab == TAB_INSTALL) { ui->tab = TAB_HOME; go(ui, S_LIBRARY); }
            else if (ui->page == PAGE_MAINTENANCE) settings_open(ui, 0, ROW_MAINTENANCE);
            else if (ui->page == PAGE_NETWORK) settings_open(ui, 0, ROW_NETWORK);
            else if (ui->page == PAGE_GAMES) settings_open(ui, 0, ROW_GAMES);
            else if (ui->page == PAGE_OVERLAY) settings_open(ui, 0, ROW_OVERLAY);
            else if (ui->page == PAGE_SYSTEM) settings_open(ui, 0, ROW_SYSTEM);
            else if (ui->page == PAGE_PICTURES) settings_open(ui, PAGE_SYSTEM, system_row(ui, SR_PICTURES));
            else { ui->tab = TAB_HOME; go(ui, S_LIBRARY); }
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
            case SR_NETWORK: settings_open(ui, PAGE_NETWORK, 0); break;
            case SR_REMOTE_USER:
            case SR_REMOTE_PASSWORD: {
                int rc = -1, user = row == SR_REMOTE_USER;
                if (ui->platform.keyboard_open)
                    rc = ui->platform.keyboard_open(ui->platform.context, user ? T("Username (empty: any)") : T("Password (empty: none)"),
                                                    user ? ui->settings.remote_user : ui->settings.remote_password);
                say(ui, "PC5 keyboard open rc=%d", rc);
                ui->typing_row = row;
                if (rc >= 0) go(ui, S_SETTINGS_TYPING);
                else message(ui, 0, T("The keyboard didn't open"), T("The console's on-screen keyboard can't be used just now."), S_SETTINGS);
                break;
            }
            case SR_COMPATIBILITY:
                compatibility_open(ui);
                break;
            case SR_PHONE_LIBRARY: {
                const Pc5Game library = { .title = "Library appearance", .prefix = "library", .pc5 = 1 };
                phone_open(ui, &library, PC5_PHONE_LIBRARY); break;
            }
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
    case S_SETTINGS_TYPING: {
        int state = ui->platform.keyboard_state ? ui->platform.keyboard_state(ui->platform.context) : -1;
        if (state == 1) break;   /* still up */
        char typed[256] = "";
        int rc = ui->platform.keyboard_close ? ui->platform.keyboard_close(ui->platform.context, typed, sizeof(typed)) : -1;
        say(ui, "PC5 keyboard closed rc=%d", rc);
        if (rc == 1) {
            int user = ui->typing_row == SR_REMOTE_USER;
            char *start = typed, *to = user ? ui->settings.remote_user : ui->settings.remote_password;
            size_t size = user ? sizeof(ui->settings.remote_user) : sizeof(ui->settings.remote_password);
            /* One line; a username without the spaces around it. */
            start[strcspn(start, "\r\n")] = 0;
            if (user) {
                while (*start == ' ' || *start == '\t') start++;
                for (size_t n = strlen(start); n && (start[n - 1] == ' ' || start[n - 1] == '\t'); n--) start[n - 1] = 0;
            }
            pc5_copy(to, size, start);
            pc5_ftp_login(ui->settings.remote_user, ui->settings.remote_password);
            pc5_web_login(ui->settings.remote_user, ui->settings.remote_password);
            if (pc5_settings_save(ui->root, &ui->settings)) say(ui, "PC5 problem settings: can't write them");
            say(ui, "PC5 settings remote user=%s password=%s", ui->settings.remote_user[0] ? "set" : "any",
                ui->settings.remote_password[0] ? "set" : "none");
        }
        go(ui, S_SETTINGS);
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
        if (k & (PC5_KEY_L1 | PC5_KEY_R1))
            tab_move(ui, k & PC5_KEY_R1 ? 1 : -1);
        else compatibility_input(ui, k, back, accept);
        break;
    case S_COMPAT_SEARCH: {
        int state = ui->platform.keyboard_state ? ui->platform.keyboard_state(ui->platform.context) : -1;
        if (state == 1) break;
        char text[128] = "";
        int rc = ui->platform.keyboard_close ? ui->platform.keyboard_close(ui->platform.context, text, sizeof(text)) : -1;
        if (rc > 0) { pc5_copy(ui->compat_query, sizeof(ui->compat_query), text); ui->compat_at = 0; }
        go(ui, S_COMPATIBILITY); break;
    }
    case S_COMPAT_REPORT:
    case S_COMPAT_SHARE:
        if (k & (back | accept)) go(ui, S_COMPATIBILITY);
        break;
    case S_MEDIA_OPTIONS:
        media_options_input(ui, k, back, accept);
        break;
    case S_MEDIA_DELETE:
        media_delete_input(ui, k, back, accept);
        break;
    case S_LOOK_CONFIRM:
        if (k & back) go(ui, S_FILES);
        else {
            if (k & PC5_KEY_LEFT) ui->button = 0;
            if (k & PC5_KEY_RIGHT) ui->button = 1;
            if (k & accept) {
                if (ui->button) look_begin(ui, ui->look_source, 1);
                else go(ui, S_FILES);
            }
            if (k) ui->dirty = 1;
        }
        break;
    case S_LOOK_WORK:
        if (k & back) atomic_store(&ui->look.stop, 1);
        if (atomic_load(&ui->look.done)) {
            pc5_look_end(&ui->look);
            if (!ui->look.result) {
                library_reload(ui, ui->launch_id);
                ui->music_id[0] = ui->video_id[0] = ui->bg_id[0] = ui->bg_key[0] = 0;
                pc5_appearance_load(ui->root, ui->look.prefix, &ui->appearance);
                int game = pc5_library_find(&ui->library, ui->launch_id);
                if (ui->look.restore && game >= 0) tile_art_refresh(ui, &ui->library.games[game]);
            }
            message(ui, !ui->look.result, ui->look.result ? T("Appearance wasn't changed") : T("Appearance backup complete"),
                ui->look.result ? ui->look.error : ui->look.restore ? T("The saved appearance was restored. Game files and saves were kept.") : ui->look.archive, S_MEDIA_OPTIONS);
        }
        break;
    case S_WEB_QR: {
        SettingRow rows[PAGE_ROWS];
        int count = page_rows(&ui->settings, PAGE_NETWORK, 0, rows), at = 0;
        for (int i = 0; i < count; i++)
            if (rows[i] == SR_WEB) at = i;
        if (k & (back | accept | PC5_KEY_TRIANGLE)) settings_open(ui, PAGE_NETWORK, at);
        break;
    }
    case S_SCAN:
        if (!ui->drawn) break;
        {
            /* The USB drives, then PC5's INSTALL folder (a source's drive is its place here). */
            int n = 0;
            /* the USB drives' tops; extended storage only through its PC5/INSTALL folder, below:
             * the console's own folders are at its top */
            for (int i = 0; !ui->install_only && ui->usb_roots && ui->usb_roots[i] && n < 8; i++)
                if (strncmp(ui->usb_roots[i], "/mnt/ext", 8)) ui->scan_roots[n++] = ui->usb_roots[i];
            if (!ui->update_for) {
                const char *folders[PC5_DRIVES + 2];
                usb_install_refresh(ui);
                for (int i = 0, count = install_folders(ui, folders); i < count && n < 15; i++) ui->scan_roots[n++] = folders[i];
            }
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
            if (usb_target(ui, NULL, ui->where_usb, sizeof(ui->where_usb))) {
                ui->where_for = 1;
                ui->button = 0;   /* the console first */
                where_drives(ui);
                go(ui, S_INSTALL_WHERE);
            } else {
                ui->install_usb[0] = 0;
                if (!queue_begin(ui)) go(ui, S_QUEUE_READ);
                else message(ui, 0, T("Out of memory"), T("PC5 couldn't make room to read the folders."), S_SOURCES);
            }
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
                message(ui, 0, T("PC5 can't read that folder"), T("Check that the USB drive is still plugged in."), S_SOURCES);
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
                message(ui, 0, T("Nothing to install"), T("None of these folders has a game PC5 can install."), S_QUEUE);
            } else if (!pc5_free_bytes(ui->root, &free_bytes) && free_bytes < need) {
                char needs[32], have[32], body[300];
                pc5_size_text(need, needs, sizeof(needs));
                pc5_size_text(free_bytes, have, sizeof(have));
                pc5_tf(body, sizeof(body), "Together they need %s and the console has %s free. Go back and untick some.",
                         needs, have);
                message(ui, 0, T("Not enough space for all of them"), body, S_QUEUE);
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
            if (ui->folder.problem[0]) {
                message(ui, 0, T("This folder can't be installed"), ui->folder.problem, S_REVIEW);
            } else if (usb_target(ui, ui->folder.path, ui->where_usb, sizeof(ui->where_usb))) {
                /* A USB drive is plugged in: the console or that drive (left and right: another). */
                ui->where_for = 0;
                ui->button = 0;   /* the console first */
                where_drives(ui);
                go(ui, S_INSTALL_WHERE);
            } else {
                ui->install_usb[0] = 0;
                review_install(ui);
            }
        }
        break;
    }
    case S_INSTALL_WHERE:
        if (k & (PC5_KEY_LEFT | PC5_KEY_RIGHT)) {
            if (ui->where_count > 1) where_step(ui, (k & PC5_KEY_RIGHT) ? 1 : -1);
            else ui->button = !ui->button;
            ui->dirty = 1;
        }
        if (k & back) {
            go(ui, ui->where_for ? S_SOURCES : S_REVIEW);
        } else if ((k & accept) && ui->now - ui->entered > 300) {
            pc5_copy(ui->install_usb, sizeof(ui->install_usb), ui->button ? ui->where_usb : "");
            say(ui, "PC5 install where=%s", ui->install_usb[0] ? ui->install_usb : "console");
            if (ui->where_for) {
                if (!queue_begin(ui)) go(ui, S_QUEUE_READ);
                else message(ui, 0, T("Out of memory"), T("PC5 couldn't make room to read the folders."), S_SOURCES);
            } else {
                review_install(ui);
            }
        }
        break;
    case S_SAVES_ASK:
        if (k & (PC5_KEY_LEFT | PC5_KEY_RIGHT)) { ui->button = !ui->button; ui->dirty = 1; }
        if (k & back) {
            ui->use_saves[0] = 0;
            go(ui, S_REVIEW);
        } else if ((k & accept) && ui->now - ui->entered > 300) {
            if (!ui->button) ui->use_saves[0] = 0;   /* Start fresh: they stay kept */
            begin_install_as(ui);
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
                pc5_tf(body, sizeof(body), "The update needs %s and the console has %s free.", need, have);
                message(ui, 0, T("Not enough space"), body, S_SOURCES);
            } else {
                begin_update(ui);
            }
        }
        break;
    case S_PROGRAMS:
        if (list_move(&ui->menu, ui->folder.exe_count, k)) ui->dirty = 1;
        if ((k & PC5_KEY_SQUARE) && ui->program_for && ui->menu >= 0 && ui->menu < ui->folder.exe_count) {
            /* Square: offered at Play too, or not any more */
            const Pc5Exe *exe = &ui->folder.exes[ui->menu];
            Pc5Offered program = { "", exe->pe.bits, pc5_folder_api(&ui->folder, ui->menu) };
            pc5_copy(program.exe, sizeof(program.exe), exe->path);
            int now = pc5_programs_toggle(ui->root, ui->launch_id, &program);
            say(ui, "PC5 program offered id=%s exe=%s now=%d", ui->launch_id, exe->path, now);
            ui->offered_count = pc5_programs_load(ui->root, ui->launch_id, ui->offered, PC5_OFFERED);
            ui->dirty = 1;
        }
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
                const char *name = index >= 0 ? ui->library.games[index].title : T("The game");
                if (pc5_game_set_program(ui->root, ui->launch_id, &ui->folder, ui->menu, error, sizeof(error))) {
                    say(ui, "PC5 problem program %s: %s", ui->launch_id, error);
                    pc5_tf(title, sizeof(title), "%s's program wasn't changed", name);
                    message(ui, 0, title, error, S_LIBRARY);
                } else {
                    say(ui, "PC5 program id=%s exe=%s", ui->launch_id, ui->folder.exes[ui->menu].path);
                    pc5_tf(title, sizeof(title), "%s starts %s now", name, ui->folder.exes[ui->menu].path);
                    pc5_tf(body, sizeof(body), "It runs as %d-bit %s.", ui->folder.exes[ui->menu].pe.bits,
                             pc5_api_name(pc5_folder_api(&ui->folder, ui->menu)));
                    library_reload(ui, ui->launch_id);
                    message(ui, 1, title, body, S_LIBRARY);
                }
            }
        }
        break;
    case S_PLAY_WHICH:
        if (list_move(&ui->menu, ui->play_count, k)) ui->dirty = 1;
        if (k & back) {
            go(ui, (Screen)ui->play_back);
        } else if ((k & accept) && ui->menu >= 0 && ui->menu < ui->play_count) {
            const Pc5Offered *chosen = &ui->play[ui->menu];
            char error[PC5_ERROR] = "";
            /* the game's program becomes the one chosen: its tile and the next Play start it too */
            if (ui->menu && pc5_game_use_program(ui->root, ui->launch_id, chosen->exe, chosen->bits, chosen->api, error,
                                                 sizeof(error))) {
                say(ui, "PC5 problem program %s: %s", ui->launch_id, error);
                message(ui, 0, T("This game can't start"), TX(error), (Screen)ui->play_back);
                break;
            }
            if (ui->menu) {
                say(ui, "PC5 program id=%s exe=%s (chosen at Play)", ui->launch_id, chosen->exe);
                library_reload(ui, ui->launch_id);
            }
            go(ui, S_LAUNCH);
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
        const char *about_lines[6];
        if ((k & PC5_KEY_SQUARE) && phone_row_kind(row) >= 0) phone_open(ui, game, phone_row_kind(row));
        else if ((k & PC5_KEY_SQUARE) && setting_about(ui, row, about_lines) > 0) go(ui, S_SETTING_ABOUT);
        else if (k & PC5_KEY_TRIANGLE) go(ui, S_LIBRARY);
        else if (k & back) {
            /* Return to this page's parent, focused on the row that opened it. */
            if (ui->game_page == GP_MAIN) go(ui, S_LIBRARY);
            else {
                game_menu_at(ui, game_page_rows[ui->game_page]);
                ui->dirty = 1;
            }
        } else if ((k & accept) && row == GR_BACKGROUND && !(ui_game_kind(ui) & GAME_MEDIA)) {
            picture_open(ui, game, PICK_BACKGROUND);   /* no video for it: the picture, as before */
        } else if ((k & accept) && (row == GR_GRAPHICS || row == GR_APPEARANCE || row == GR_MANAGE || row == GR_BACKGROUND ||
                                  row == GR_MUSIC || row == GR_BACKGROUND_VIDEO || row == GR_MEDIA || row == GR_LOADING)) {
            ui->game_page = row == GR_GRAPHICS ? GP_GRAPHICS : row == GR_APPEARANCE ? GP_APPEARANCE :
                            row == GR_BACKGROUND ? GP_BACKGROUND : row == GR_MUSIC ? GP_MUSIC :
                            row == GR_BACKGROUND_VIDEO ? GP_VIDEO : row == GR_MEDIA ? GP_MEDIA :
                            row == GR_LOADING ? GP_LOADING : GP_MANAGE;
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
            /* Off, 60 -> 120, 40 -> 120 with 120 Hz output; Off, 30 -> 60 without (40 -> 120 shows as that) */
            int step = (k & PC5_KEY_LEFT) ? -1 : 1, count = ui->settings.high_refresh ? 3 : 2;
            int now = ui->controls.frame_generation >= count ? count - 1 : ui->controls.frame_generation;
            ui->controls.frame_generation = (now + count + step) % count;
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
            /* From least to Maximum (pc5_video_memory_sizes): → and ✕ go on, ← back, round */
            const int bits32 = (ui_game_kind(ui) & GAME_32BIT) != 0;
            const int *sizes;
            int count = pc5_video_memory_sizes(bits32, &sizes);
            int level = pc5_video_memory_level(&ui->controls, bits32);
            pc5_video_memory_set(&ui->controls, bits32, (level + ((k & PC5_KEY_LEFT) ? count - 1 : 1)) % count);
            controls_changed(ui);
            ui->dirty = 1;
        } else if ((k & (PC5_KEY_LEFT | PC5_KEY_RIGHT | accept)) && row == GR_POINTER) {
            ui->controls.pointer_hide = !ui->controls.pointer_hide;
            controls_changed(ui);
            ui->dirty = 1;
        } else if ((k & (PC5_KEY_LEFT | PC5_KEY_RIGHT | accept)) && row == GR_SOUND) {
            /* Stereo, 5.1, 7.1: → and ✕ go on, ← back */
            ui->controls.surround = (ui->controls.surround + ((k & PC5_KEY_LEFT) ? 2 : 1)) % 3;
            controls_changed(ui);
            ui->dirty = 1;
        } else if ((k & (PC5_KEY_LEFT | PC5_KEY_RIGHT | accept)) && row == GR_CPU_SHARING) {
            ui->controls.cpu_sharing = !ui->controls.cpu_sharing;
            controls_changed(ui);
            ui->dirty = 1;
        } else if ((k & (PC5_KEY_LEFT | PC5_KEY_RIGHT | accept)) && row == GR_STEAM) {
            ui->controls.steam_off = !ui->controls.steam_off;
            controls_changed(ui);
            ui->dirty = 1;
        } else if ((k & accept) && row == GR_HOME) {
            home_toggle(ui, game);
        } else if (k & accept) {
            if (row == GR_PLAY) {
                if (game->playable) play(ui, S_GAME_MENU);
                else message(ui, 0, T("This game can't start"), game->problem[0] ? TX(game->problem) : T("Something it needs is missing."),
                             S_GAME_MENU);
            } else if (row == GR_BUTTONS) {
                ui->pad_row = ui->pad_top = 0;
                go(ui, S_BUTTONS);
            } else if (row == GR_COVER || row == GR_BACKGROUND_IMAGE || row == GR_ICON) {
                picture_open(ui, game, row == GR_COVER ? PICK_COVER : row == GR_ICON ? PICK_ICON : PICK_BACKGROUND);
            } else if (row == GR_VIDEO_FILE) {
                files_open(ui, 0);
                ui->files_video = 1;
            } else if (row == GR_VIDEO_CLEAR) {
                media_delete_begin(ui, 1);
            } else if (row == GR_MUSIC_FILE) {
                files_open(ui, 0);
                ui->files_music = 1;
            } else if (row == GR_LOADING_FILE) {
                files_open(ui, 0);
                ui->files_loading = 1;
            } else if (row == GR_LOADING_BACKGROUND) {
                loading_from_background(ui, game);
            } else if (row == GR_LOADING_CLEAR) {
                loading_clear(ui, game);
            } else if (row == GR_MEDIA_OPTIONS) {
                if (!game->pc5 || !game->prefix[0] || !strcmp(game->prefix, "default")) {
                    message(ui, 0, T("PC5 didn't install this game"), T("Media settings need a game with its own PC5 folder."), S_GAME_MENU);
                    break;
                }
                pc5_appearance_load(ui->root, game->prefix, &ui->appearance);
                ui->media_row = 0;
                go(ui, S_MEDIA_OPTIONS);
            } else if (row == GR_MUSIC_CLEAR) {
                media_delete_begin(ui, 0);
            } else if (row == GR_PROGRAM) {
                if (!game->pc5) message(ui, 0, T("PC5 didn't install this game"),
                                        T("PC5 didn't install it, so its program can't be changed here."), S_GAME_MENU);
                else go(ui, S_GAME_READING);
            } else if (row == GR_FILES) {
                files_open(ui, 1);
            } else if (row == GR_UPDATE) {
                ui->update_for = 1;
                go(ui, S_SCAN);
            } else if (row == GR_REPORT) {
                report_game_log(ui, &ui->library.games[ui->selected]);
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
                                                ui->pick_kind == PICK_COVER ? T("Search for covers by name")
                                                : ui->pick_kind == PICK_ICON ? T("Search for tile pictures by name")
                                                                             : T("Search for backgrounds by name"),
                                                ui->pick_query[0] ? ui->pick_query : game->title);
            say(ui, "PC5 keyboard open rc=%d", rc);
            if (rc >= 0) go(ui, S_PICTURE_TYPING);
            else message(ui, 0, T("The keyboard didn't open"), T("The console's on-screen keyboard can't be used just now."), S_PICTURE);
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
        int count = pad_rows(&ui->controls, rows, pc5_controls_mapping_exists(ui->root)), changed = 0;
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
            } else if (row.kind == PR_SAVE_MAPPING) {
                if (pc5_controls_mapping_save(ui->root, &ui->controls))
                    message(ui, 0, T("Your mapping wasn't saved"), T("PC5 couldn't write it to the console's storage."),
                            S_BUTTONS);
                else
                    message(ui, 0, T("Saved as your mapping"),
                            T("Games set up from now on start with it, and Use my mapping puts it on any game."), S_BUTTONS);
            } else if (row.kind == PR_USE_MAPPING) {
                changed = !pc5_controls_mapping_load(ui->root, &ui->controls);
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
        count = pad_rows(&ui->controls, rows, pc5_controls_mapping_exists(ui->root));
        /* The rows above can come and go (a stick's keys, the pointer's rows): the cursor stays
         * on the row it was on (Reset, Save or Use my mapping). */
        if (row.kind >= PR_RESET)
            for (int i = 0; i < count; i++)
                if (rows[i].kind == row.kind) ui->pad_row = i;
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
    case S_SETTING_ABOUT:
        if (k & (back | accept | PC5_KEY_SQUARE)) go(ui, S_GAME_MENU);
        break;
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
                message(ui, 0, T("PC5 can't read the game's folder"), T("Its files may be missing: uninstall it and install it again."),
                        S_GAME_MENU);
                break;
            }
            if (ui->folder.exe_count < 2) {
                message(ui, 0, T("This game has one program"), T("There is no other program in its folder to start."), S_GAME_MENU);
                break;
            }
            ui->menu = 0;
            for (int i = 0; i < ui->folder.exe_count; i++)
                if (!strcmp(ui->folder.exes[i].path, info.exe)) ui->menu = i;
            ui->exe = ui->menu;
            ui->list_top = 0;
            ui->program_for = 1;
            ui->offered_count = pc5_programs_load(ui->root, ui->launch_id, ui->offered, PC5_OFFERED);
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
                    pc5_tf(title, sizeof(title), "%s wasn't uninstalled", game->title);
                    pc5_tf(body, sizeof(body), "%s Nothing was changed.", TX(ui->remove.error));
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
            /* TRANSLATORS: added after the sentence before it, hence the space at its start. */
            const char *tile = ui->removing_tiles > 0 ? T(" Its home screen tile's folder was deleted: if the tile is still on "
                                                        "the home screen, delete it there.") : "";
            if (ui->removing_kind == 2) {
                pc5_tf(title, sizeof(title), failed ? "The saves of %s weren't fully deleted" : "The saves of %s are deleted",
                         ui->removing_title);
                if (failed) pc5_tf(body, sizeof(body), "%s PC5 finishes deleting them the next time it starts.", TX(ui->remove.error));
                else pc5_tf(body, sizeof(body), "Its saves and settings, kept when it was uninstalled, are gone.");
            } else {
                pc5_tf(title, sizeof(title), failed ? "%s wasn't fully uninstalled" : "%s is uninstalled", ui->removing_title);
                if (failed) pc5_tf(body, sizeof(body), "%s PC5 finishes it the next time it starts.", TX(ui->remove.error));
                else if (ui->removing_kind == 1)
                    pc5_tf(body, sizeof(body), "Its saves and settings are kept: installing it again puts them back. "
                             "Settings \xe2\x80\xba Game maintenance \xe2\x80\xba Uninstall game deletes them.%s", tile);
                else pc5_tf(body, sizeof(body), "The game, its saves and its settings were deleted.%s", tile);
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
    case S_MUSIC_IMPORT:
        if (k & back) atomic_store(&ui->music_import.stop, 1);
        if (!atomic_load(&ui->music_import.done)) break;
        pc5_music_import_end(&ui->music_import);
        /* A file a phone sent: back to the game's menu, not to a file list it never opened. */
        from_phone = ui->phone_source[0] != 0;
        phone_source_remove(ui);
        if (atomic_load(&ui->music_import.stop) && ui->music_import.result == PC5_MUSIC_IMPORTED) {
            unlink(ui->music_import.destination);
            ui->music_import.result = PC5_MUSIC_CANCELLED;
        }
        if (from_phone && ui->music_import.result != PC5_MUSIC_IMPORTED) files_close(ui);
        back_to = from_phone ? S_GAME_MENU : S_FILES;
        if (ui->music_import.result == PC5_MUSIC_CANCELLED) {
            go(ui, back_to);
        } else if (ui->music_import.result != PC5_MUSIC_IMPORTED) {
            const char *why = ui->music_import.result == PC5_MUSIC_BAD_FORMAT
                ? T("Choose a readable MP3 or 16-bit PCM WAV file. Protected or damaged files may not work.")
                : ui->music_import.result == PC5_MUSIC_TOO_LARGE
                ? T("The prepared music exceeds the 2 GB limit. Choose a shorter track.")
                : T("Check free space and keep the source drive connected until importing finishes. Your previous music was kept.");
            message(ui, 0, T("Music wasn't saved"), why, back_to);
        } else if (music_save(ui, ui->music_import.destination)) {
            unlink(ui->music_import.destination);
            if (from_phone) files_close(ui);
            message(ui, 0, T("Music wasn't saved"), T("PC5 couldn't save the music selection. Try again."), back_to);
        } else {
            ui->phone_import_ok = 1;
            music_cleanup(ui, ui->music_import.destination);
            files_close(ui);
        }
        break;
    case S_VIDEO_IMPORT:
        if (k & back) atomic_store(&ui->video_import.stop, 1);
        if (!atomic_load(&ui->video_import.done)) break;
        pc5_video_import_end(&ui->video_import);
        from_phone = ui->phone_source[0] != 0;
        phone_source_remove(ui);
        if (from_phone && (ui->video_import.result != PC5_VIDEO_OK || atomic_load(&ui->video_import.stop))) files_close(ui);
        back_to = from_phone ? S_GAME_MENU : S_FILES;
        if (atomic_load(&ui->video_import.stop) || ui->video_import.result == PC5_VIDEO_CANCELLED) {
            unlink(ui->video_import.destination);
            go(ui, back_to);
        } else if (ui->video_import.result != PC5_VIDEO_OK) {
            const char *why = ui->video_import.result == PC5_VIDEO_FORMAT
                ? T("Choose an H.264 video up to 1920x1080, 60 fps and 1 GB. MP4 is recommended. Audio is ignored.")
                : T("Check free space and keep the source drive connected until importing finishes. Your previous video was kept.");
            say(ui, "PC5 background video import failed: %s", ui->video_import.error);
            message(ui, 0, T("Video wasn't saved"), why, back_to);
        } else if (video_save(ui, ui->video_import.destination)) {
            unlink(ui->video_import.destination);
            if (from_phone) files_close(ui);
            message(ui, 0, T("Video wasn't saved"), T("PC5 couldn't save the video selection. Try again."), back_to);
        } else { ui->phone_import_ok = 1; files_close(ui); }
        break;
    case S_PHONE_QR:
        phone_update(ui, k, back);
        break;
    case S_FILES:
    case S_FILES_MENU:
    case S_FILES_TYPING:
    case S_FILES_CONFLICT:
    case S_FILES_DELETE:
    case S_FILES_WORKING:
        files_update(ui, k);
        break;
    case S_FILES_EDIT:
    case S_FILES_EDIT_MENU:
    case S_FILES_EDIT_TYPING:
    case S_FILES_EDIT_LEAVE:
        files_edit_update(ui, k);
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
        if (ui->launch_ready) {
            /* Made ready: PC5's screen fades to black (LAUNCH_FADE_MS), then the game is handed
             * over. */
            if (ui->now - ui->launch_ready_at < LAUNCH_FADE_MS) {
                ui->dirty = 1;
                break;
            }
            launch_hand_over(ui);
            break;
        }
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
                        pc5_tf(title, sizeof(title), "%s can't start", ui->library.games[index].title);
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
                    pc5_tf(title, sizeof(title), "%s can't start", index >= 0 ? ui->library.games[index].title : T("The game"));
                    say(ui, "PC5 problem play %s: %s", ui->launch_id, error);
                    message(ui, 0, title, error, S_LIBRARY);   /* ends the copy; the next start goes on with it */
                    break;
                }
                say(ui, "PC5 dotnet runtime copied entries=%u", (unsigned)ui->runtime.done);
                pc5_runtime_end(&ui->runtime);
                ui->runtime_state = 2;
            }
            /* The game's redistributables the first time it starts (PhysX): their busy line is
             * drawn, then they are installed. */
            char game_dir[PC5_PATH], program[PC5_PATH];
            int own = index >= 0 && ui->library.games[index].pc5 &&
                      !pc5_game_folder(ui->root, ui->launch_id, game_dir, sizeof(game_dir));
            if (ui->redist_state == 0) {
                ui->redist_state = 2;
                if (own && pc5_redist_pending(ui->root, ui->launch_id, game_dir, ui->redist_name, sizeof(ui->redist_name))) {
                    ui->redist_state = 1;
                    ui->drawn = 0;
                    ui->dirty = 1;
                    break;
                }
            }
            if (ui->redist_state == 1) {
                int installed = pc5_redist_install(ui->root, ui->launch_id, game_dir, redist_log, ui);
                say(ui, "PC5 redist %s installed=%d", ui->launch_id, installed);
                ui->redist_state = 2;
            }
            if (own) pc5_physx_register(ui->root, ui->launch_id, redist_log, ui);
            /* Steam: a program in Steam's DRM wrapper can't start without it; Steam's library
             * gets the emulator in its place, or back, as the game's menu says. */
            if (own && !pc5_game_program(ui->root, ui->launch_id, program, sizeof(program))) {
                /* VKD3D-Proton keeps a Direct3D 12 game's compiled pipelines in vkd3d-proton.cache in
                 * its working folder (the program's), written to vkd3d-proton.cache.write while it
                 * runs and merged at the next start: what the last run left, for the log. */
                {
                    char dir[PC5_PATH], cache[PC5_PATH], pending[PC5_PATH];
                    struct stat st;
                    long long kept = -1, written = -1;
                    char *slash;
                    pc5_copy(dir, sizeof(dir), program);
                    if ((slash = strrchr(dir, '/'))) *slash = 0;
                    if (!pc5_format(cache, sizeof(cache), "%s/vkd3d-proton.cache", dir) && !stat(cache, &st))
                        kept = (long long)st.st_size;
                    if (!pc5_format(pending, sizeof(pending), "%s/vkd3d-proton.cache.write", dir) && !stat(pending, &st))
                        written = (long long)st.st_size;
                    if (kept >= 0 || written >= 0)
                        say(ui, "PC5 vkd3d cache in=%s cache=%lld write=%lld", dir, kept, written);
                    else
                        say(ui, "PC5 vkd3d cache in=%s none", dir);
                }
                if (pc5_steam_stub(program)) {
                    pc5_tf(title, sizeof(title), "%s needs Steam", ui->library.games[index].title);
                    say(ui, "PC5 problem play %s: SteamStub (.bind) in %s", ui->launch_id, program);
                    message(ui, 0, title,
                            T("Its program is wrapped in Steam's DRM (SteamStub), which starts only with Steam running, "
                            "and Steam doesn't run on the console. A copy of the game without it can start."),
                            S_LIBRARY);
                    break;
                }
                Pc5Controls chosen;
                char why[PC5_ERROR];
                pc5_controls_load(ui->root, ui->launch_id, &chosen);
                int changed = pc5_steam_prepare(ui->root, ui->launch_id, game_dir, program, ui->app, !chosen.steam_off, why,
                                                sizeof(why));
                if (changed < 0) say(ui, "PC5 problem steam %s: %s", ui->launch_id, why);
                else if (changed) say(ui, "PC5 steam emulator %s changed=%d", chosen.steam_off ? "off" : "on", changed);
            }
            if (index >= 0 && ui->library.games[index].pc5 &&
                pc5_profile_refresh(ui->root, ui->launch_id, error, sizeof(error))) {
                pc5_tf(title, sizeof(title), "%s can't start", ui->library.games[index].title);
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
                    pc5_graphics_tech_name(chosen.graphics, game->api), game->bits);
            } else {
                say(ui, "PC5 play id=%s", ui->launch_id);
            }
            if (index >= 0 && ui->library.games[index].usb) {
                char where[PC5_PATH];
                if (!pc5_game_folder(ui->root, ui->launch_id, where, sizeof(where))) say(ui, "PC5 play from usb dir=%s", where);
            }
            ui->launch_ready = 1;   /* the fade, then launch_hand_over */
            ui->launch_ready_at = ui->now;
            ui->dirty = 1;
        }
        break;
    }

    if (ui->phone_library && ui->phone_manager_pending && ui->screen != S_PHONE_QR &&
        ui->screen != S_MUSIC_IMPORT && ui->screen != S_VIDEO_IMPORT && ui->screen != S_TILE_ART) {
        pc5_web_appearance_finish(ui->phone_import_ok);
        ui->phone_manager_pending = 0;
        if (ui->screen == S_MESSAGE) ui->back_to = S_PHONE_QR;
        else go(ui, S_PHONE_QR);
    }
    if (ui->compat_initialized && ui->compatibility.running && atomic_load(&ui->compatibility.done)) {
        /* Poll replaces/frees the old table. Copy the selected identity first,
         * so a background refresh cannot switch the report being read. */
        int indices[PC5_COMPAT_MAX], count = compatibility_indices(ui, indices);
        int selected = ui->compat_at >= 0 && ui->compat_at < count;
        char title[256] = "", report[256] = "";
        if (selected) {
            const Pc5CompatEntry *entry = &ui->compatibility.entries[indices[ui->compat_at]];
            pc5_copy(title, sizeof(title), entry->title);
            pc5_copy(report, sizeof(report), entry->report);
        }
        if (pc5_compat_poll(&ui->compatibility)) {
            if (!ui->compatibility.result) {
                count = compatibility_indices(ui, indices);
                int same = -1, by_title = -1;
                for (int i = 0; selected && i < count; ++i) {
                    const Pc5CompatEntry *entry = &ui->compatibility.entries[indices[i]];
                    if (strcmp(entry->title, title)) continue;
                    if (by_title < 0) by_title = i;
                    if (!strcmp(entry->report, report)) { same = i; break; }
                }
                if (same < 0) same = by_title; /* a newer report for the same game */
                if (same >= 0) ui->compat_at = same;
                else {
                    if (ui->compat_at < 0) ui->compat_at = 0;
                    if (ui->compat_at >= count) ui->compat_at = count ? count - 1 : 0;
                    ui->compat_detail = ui->compat_scroll = 0;
                }
            }
            ui->dirty = 1;
        }
    }
    video_follow(ui);
    music_follow(ui);
    update_check_follow(ui);
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
    if (ui->library.count) hero_refresh(ui);
    /* the Library tab's covers, wherever they are in the library: two a frame */
    if (ui->screen == S_LIBRARY && ui->tab == TAB_LIBRARY) {
        int loaded = 0;
        int l = ui->list_at, from = (ui->list_item[l] / GRID_COLUMNS - 1) * GRID_COLUMNS;
        if (from < 0) from = 0;
        for (int n = from; n < ui->list_count[l] && n < from + 3 * GRID_COLUMNS && loaded < 2; n++) {
                int i = ui->lists[l][n];
                if (!ui->covers[i].state) { cover_load(ui, i); loaded++; ui->dirty = 1; }
            }
    }
    if (ui->library.count) {
        float target = row_scroll(ui), gap = target - ui->scroll;
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
    if (ui->screen == S_LOOK_WORK || ui->screen == S_VIDEO_IMPORT || ui->screen == S_MUSIC_IMPORT || ui->screen == S_START || ui->screen == S_COVERS || ui->screen == S_SCAN || ui->screen == S_READING ||
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

typedef enum Glyph { G_CROSS, G_CIRCLE, G_SQUARE, G_TRIANGLE, G_OPTIONS, G_L1, G_R1, G_L1R1, G_R3, G_L2R2, G_L2, G_R2,
                     G_OPTIONS_PAD, G_UP_PAD } Glyph;

static const Pc5Image *glyph_icon(Pc5Ui *ui, Glyph glyph)
{
    int i = glyph == G_CROSS ? ICON_CROSS : glyph == G_CIRCLE ? ICON_CIRCLE : glyph == G_TRIANGLE ? ICON_TRIANGLE :
            glyph == G_SQUARE ? ICON_SQUARE : glyph == G_L1 ? ICON_L1 : glyph == G_R1 ? ICON_R1 :
            glyph == G_R3 ? ICON_R3 : -1;   /* OPTIONS: its picture's letters are too small at this size */
    return i >= 0 && ui->art_icons[i].px ? &ui->art_icons[i] : NULL;
}

static int button_glyph(Pc5Ui *ui, Pc5Canvas *c, Glyph glyph, int x, int y)
{
    const float r = 16, cx = x + r, cy = y - 9;
    const Pc5Image *icon = glyph_icon(ui, glyph);

    /* PC5's own picture of the button, at its height (32, or 30 for the long ones) */
    if (icon) {
        float h = icon->h, w = icon->w;
        pc5_draw_image(c, icon, x, cy - h / 2, w, h, 256);
        return (int)w;
    }

    if (glyph == G_OPTIONS || glyph == G_L1 || glyph == G_R1 || glyph == G_R3 || glyph == G_L2R2 || glyph == G_L2 || glyph == G_R2) {
        const char *text = glyph == G_OPTIONS ? "OPTIONS" : glyph == G_L1 ? "L1" : glyph == G_R1 ? "R1" : glyph == G_R3 ? "R3" : glyph == G_L2 ? "L2" : glyph == G_R2 ? "R2" : "L2 R2";
        int tw = pc5_text_width(&ui->fonts, PC5_FACE_BUTTON, text), w = tw + 24;
        /* white with dark letters, as PC5's pictures of the buttons */
        pc5_round_rect(c, x, cy - 15, w, 30, 15, 0xf2ffffffu);
        pc5_text(c, &ui->fonts, PC5_FACE_BUTTON, x + w / 2, (int)cy + 6, 0xff15171cu, text, 0, 1);
        return w;
    }
    if (glyph == G_L1R1) {
        int w1 = button_glyph(ui, c, G_L1, x, y);
        return w1 + 8 + button_glyph(ui, c, G_R1, x + w1 + 8, y);
    }
    /* a white button with its symbol dark on it */
    const uint32_t ink = 0xff15171cu;
    pc5_circle(c, cx, cy, r, 0xf2ffffffu);
    switch (glyph) {
    case G_UP_PAD:
        pc5_line(c, cx, cy + 7, cx, cy - 7, 2, ink);
        pc5_line(c, cx - 6, cy - 1, cx, cy - 7, 2, ink);
        pc5_line(c, cx, cy - 7, cx + 6, cy - 1, 2, ink);
        break;
    case G_OPTIONS_PAD:
        for (int line = -1; line <= 1; ++line)
            pc5_line(c, cx - 6, cy + line * 5, cx + 6, cy + line * 5, 2, ink);
        break;
    case G_CROSS:
        pc5_line(c, cx - 6, cy - 6, cx + 6, cy + 6, 2.6f, ink);
        pc5_line(c, cx - 6, cy + 6, cx + 6, cy - 6, 2.6f, ink);
        break;
    case G_CIRCLE: pc5_ring(c, cx, cy, 7, 2.4f, ink); break;
    case G_SQUARE: pc5_round_frame(c, cx - 6.5f, cy - 6.5f, 13, 13, 1.5f, 2.4f, ink); break;
    case G_TRIANGLE:
        pc5_line(c, cx, cy - 7.5f, cx - 7, cy + 5, 2.4f, ink);
        pc5_line(c, cx - 7, cy + 5, cx + 7, cy + 5, 2.4f, ink);
        pc5_line(c, cx + 7, cy + 5, cx, cy - 7.5f, 2.4f, ink);
        break;
    default: break;
    }
    return (int)(2 * r);
}

typedef struct Hint { Glyph glyph; const char *label; } Hint;

/* How wide button_glyph draws a glyph. */
static int glyph_width(Pc5Ui *ui, Glyph glyph)
{
    const Pc5Image *icon = glyph_icon(ui, glyph);
    if (icon) return icon->w;
    if (glyph == G_L1R1) return glyph_width(ui, G_L1) + 8 + glyph_width(ui, G_R1);
    if (glyph == G_OPTIONS || glyph == G_L1 || glyph == G_R1 || glyph == G_R3 || glyph == G_L2R2 || glyph == G_L2 || glyph == G_R2) {
        const char *text = glyph == G_OPTIONS ? "OPTIONS" : glyph == G_L1 ? "L1" : glyph == G_R1 ? "R1" : glyph == G_R3 ? "R3" : glyph == G_L2 ? "L2" : glyph == G_R2 ? "R2" : "L2 R2";
        return pc5_text_width(&ui->fonts, PC5_FACE_BUTTON, text) + 24;
    }
    return 32;
}

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
    pc5_shadow(c, x, y, w, h, 28, 44, 0x90000000u);
    pc5_shadow(c, x, y, w, h, 28, 18, 0x264d7cffu);
    pc5_round_rect(c, x, y, w, h, 28, 0xf2121620u);
    pc5_round_frame(c, x, y, w, h, 28, 1.5f, 0x47ffffffu);
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
    for (int i = 1; i < BANDS; i++) started[i] = !pc5_thread_start(&threads[i], band_main, &bands[i], 0);
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
    if (ui->video.have && ui->video.front) {
        plan.to = plan.from = ui->video.front;
        plan.a = 256; plan.b = 0;
    }
    plan.compose = !ui->hero_backdrop && (plan.to == ui->composed || plan.from == ui->composed) &&
                   (glow != ui->composed_glow || !ui->composed_ready);
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

/* text in capitals with letter spacing (ASCII letters only; others as they are), its centre
 * at x; its width. */
static int spaced_caps(Pc5Ui *ui, Pc5Canvas *c, Pc5Face face, int x, int y, uint32_t color, const char *text, int spacing,
                       int draw)
{
    char one[8], caps[256];
    int width = 0, count = 0;

    pc5_upper(text, caps, sizeof(caps));
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

/* ---- drawing: Home, Games and Library ----------------------------------------------------- */

#define C_BLUE 0xff4d7cffu
#define C_GLASS 0xf7121620u

/* text in capitals with letter spacing, from x (left), on one line; its width */
static int caps_left(Pc5Ui *ui, Pc5Canvas *c, Pc5Face face, int x, int y, uint32_t color, const char *text, int spacing)
{
    int width = spaced_caps(ui, c, face, 0, y, 0, text, spacing, 0);
    spaced_caps(ui, c, face, x + width / 2, y, color, text, spacing, 1);
    return width;
}

/* A pill with text, from x; its width. filled: white with dark text (a button), else glass. */
static int pill(Pc5Ui *ui, Pc5Canvas *c, int x, int y, int h, const char *text, int filled)
{
    int tw = pc5_text_width(&ui->fonts, PC5_FACE_SMALL, text), w = tw + 44;
    if (filled < 0) {
        /* a chip: a rounded box, as Home's settings show */
        pc5_round_rect(c, x, y, w, h, 12, 0x73080a10u);
        pc5_round_frame(c, x, y, w, h, 12, 1.5f, 0x4dffffffu);
        pc5_text(c, &ui->fonts, PC5_FACE_SMALL, x + 22, y + h / 2 + 9, 0xf2ffffffu, text, 0, 0);
        return w;
    }
    if (filled) {
        pc5_round_rect(c, x, y, w, h, h / 2.0f, 0xf2ffffffu);
        pc5_text(c, &ui->fonts, PC5_FACE_SMALL, x + 22, y + h / 2 + 9, 0xff111318u, text, 0, 0);
    } else {
        pc5_round_rect(c, x, y, w, h, h / 2.0f, 0x66080a10u);
        pc5_round_frame(c, x, y, w, h, h / 2.0f, 1.5f, 0x59ffffffu);
        pc5_text(c, &ui->fonts, PC5_FACE_SMALL, x + 22, y + h / 2 + 9, 0xf2ffffffu, text, 0, 0);
    }
    return w;
}

/* The tab shown as chosen: Settings or Install while their pages are open. */
static int tab_shown(const Pc5Ui *ui)
{
    if (ui->screen == S_SETTINGS || ui->screen == S_SETTINGS_TYPING || ui->screen == S_WEB_QR)
        return ui->tab == TAB_INSTALL ? TAB_INSTALL : TAB_SETTINGS;
    return ui->tab;
}

/* The top: PC5's logo, the tabs, the controller and the clock. */
static void top_bar(Pc5Ui *ui, Pc5Canvas *c)
{
    int chosen = tab_shown(ui), x;
    if (ui->art_logo.px) {
        float h = 44, w = (float)ui->art_logo.w * h / (float)ui->art_logo.h;
        pc5_draw_image(c, &ui->art_logo, MARGIN - 6, 49, w, h, 256);
        x = MARGIN - 6 + (int)w + 30;
    } else {
        int w = pc5_text(c, &ui->fonts, PC5_FACE_HEAD, MARGIN, 86, C_TEXT, "PC", 0, 0);
        x = MARGIN + w + 1 + pc5_text(c, &ui->fonts, PC5_FACE_HEAD, MARGIN + w + 1, 86, C_ACCENT, "5", 0, 0) + 34;
    }
    pc5_line(c, x, 46, x, 96, 1.5f, 0x55ffffffu);
    x += 36;
    for (int i = 0; i < TABS; i++) {
        const char *name = T(tab_names[i]);
        int tw = pc5_text_width(&ui->fonts, PC5_FACE_LABEL, name), w = tw + 70;
        if (i == chosen) {
            pc5_shadow(c, x, 42, w, 58, 29, 18, 0x704d7cffu);
            pc5_round_rect(c, x, 42, w, 58, 29, 0x592a3f8cu);
            pc5_round_frame(c, x, 42, w, 58, 29, 2.5f, 0xff8fb0ffu);
        }
        pc5_text(c, &ui->fonts, PC5_FACE_LABEL, x + 35, 81, i == chosen ? C_TEXT : 0xe6e3e7efu, name, 0, 0);
        x += w + 8;
    }
    int cw = ui->clock[0] ? pc5_text_width(&ui->fonts, PC5_FACE_LABEL, ui->clock) : 0;
    if (ui->clock[0]) pc5_text(c, &ui->fonts, PC5_FACE_LABEL, W - MARGIN, 82, C_TEXT, ui->clock, 0, 2);
    if (ui->art_controller.px) {
        float h = 36, w = (float)ui->art_controller.w * h / (float)ui->art_controller.h;
        pc5_draw_image(c, &ui->art_controller, W - MARGIN - cw - 28 - w, 53, w, h, 256);
    }
}

/* Hints from the right edge leftwards (so they end at W - MARGIN). */
static void hints_right(Pc5Ui *ui, Pc5Canvas *c, int y, const Hint *list, int count)
{
    int width = 0;
    for (int i = 0; i < count; i++)
        width += glyph_width(ui, list[i].glyph) + 12 + pc5_text_width(&ui->fonts, PC5_FACE_BODY, list[i].label) +
                 (i + 1 < count ? 40 : 0);
    hints(ui, c, W - MARGIN - width, y, list, count);
}

/* A cover at x, y (its top), w x h; chosen: the blue glow around it. */
static void cover_at(Pc5Ui *ui, Pc5Canvas *c, int index, float x, float y, float w, float h, int chosen)
{
    const Cover *cover = &ui->covers[index];
    if (chosen) {
        pc5_shadow(c, x - 6, y - 6, w + 12, h + 12, 20, 34, 0xb04d7cffu);
    }
    if (cover->state && cover->picture.levels) {
        pc5_draw_card(c, &cover->picture, x, y + h, w, h, h, 14, chosen ? 256 : 214, 256, chosen ? 70 : 34, 0.2f,
                      chosen ? 48 : 30);
    } else {
        /* none yet: PC5's own cover with the game's name on it */
        if (ui->art_cover.px) pc5_draw_image(c, &ui->art_cover, x, y, w, h, chosen ? 256 : 220);
        else pc5_round_rect(c, x, y, w, h, 14, 0xff1c2028u);
        const char *name = ui->library.games[index].title;
        int lines = pc5_text_lines(&ui->fonts, PC5_FACE_LABEL, (int)w - 28, name) > 1 ? 2 : 1;
        pc5_text_wrap(c, &ui->fonts, PC5_FACE_LABEL, (int)x + 14, (int)(y + h - 34 - (lines - 1) * 36), (int)w - 28,
                      2, 36, C_TEXT, name, 1);
    }
    if (chosen) pc5_round_frame(c, x - 6, y - 6, w + 12, h + 12, 20, 4, 0xff7da2ffu);
    else pc5_round_frame(c, x, y, w, h, 14, 1, 0x26ffffffu);
    /* a favourite's mark, in its top right corner */
    if (ui->favourite[index]) {
        const Pc5Image *star = &ui->art_icons[ICON_STAR];
        if (star->px) {
            pc5_draw_image(c, star, x + w - 10 - star->w, y + 10, star->w, star->h, 256);
        } else {
            float cx = x + w - 22, cy = y + 22;
            pc5_circle(c, cx, cy, 13, 0xe64d7cffu);
            pc5_ring(c, cx, cy, 13, 1.5f, 0xffffffffu);
            pc5_circle(c, cx, cy, 4.5f, 0xffffffffu);
        }
    }
}

/* The chosen game's details and settings for the top of Home, read once it stays chosen a
 * moment, and again when the worker has saved its details. */
static void hero_refresh(Pc5Ui *ui)
{
    unsigned generation = atomic_load(&ui->backgrounds.generation);
    if (!ui->library.count || (ui->hero_for == ui->selected && ui->hero_generation == generation)) return;
    if (ui->hero_for != ui->selected && ui->now - ui->bg_selected_at < BG_SETTLE_MS) return;
    const Pc5Game *game = &ui->library.games[ui->selected];
    char path[PC5_PATH];
    ui->hero_for = ui->selected;
    ui->hero_generation = generation;
    pc5_image_free(&ui->hero_logo);
    pc5_details_read(ui->root, game->prefix, &ui->hero);
    if (ui->hero.has_logo && !pc5_details_logo_path(ui->root, game->prefix, path, sizeof(path))) {
        Pc5Buf bytes = { 0 };
        if (pc5_read_file(path, &bytes, 8u << 20) || pc5_image_decode(bytes.data, bytes.len, &ui->hero_logo))
            ui->hero_logo = (Pc5Image){ 0 };
        pc5_buf_free(&bytes);
    }
    pc5_controls_load(ui->root, game->prefix, &ui->hero_controls);
    ui->dirty = 1;
}

/* Home's top: what the game is, its name (its logo when it has one), a line about it, Play
 * and its menu, and its settings in chips. */
static void hero(Pc5Ui *ui, Pc5Canvas *c)
{
    const Pc5Game *game = &ui->library.games[ui->selected];
    int ready = ui->hero_for == ui->selected;
    const Pc5Controls *k = &ui->hero_controls;
    char tags[200];

    /* what it is: its genres, or what PC5 knows */
    pc5_copy(tags, sizeof(tags), ready && ui->hero.genres[0] ? ui->hero.genres : TX(game->detail));
    caps_left(ui, c, PC5_FACE_SMALL, MARGIN, 196, 0xd9e6e9f0u, tags, 4);
    /* its name: the logo, fitted into 640 x 150, or the name in large letters */
    int below = 340;
    if (ready && ui->hero_logo.px) {
        float bw = 640, bh = 150, w = bw, h = (float)ui->hero_logo.h * bw / (float)ui->hero_logo.w;
        if (h > bh) { h = bh; w = (float)ui->hero_logo.w * bh / (float)ui->hero_logo.h; }
        pc5_draw_image(c, &ui->hero_logo, MARGIN, 220 + (bh - h), w, h, 256);
        below = 220 + (int)bh + 44;
    } else {
        int lines = pc5_text_lines(&ui->fonts, PC5_FACE_HERO, 1000, game->title) > 1 ? 2 : 1;
        Pc5Face face = lines > 1 || pc5_text_width(&ui->fonts, PC5_FACE_HERO, game->title) > 1000 ? PC5_FACE_TITLE : PC5_FACE_HERO;
        if (face == PC5_FACE_TITLE) {
            pc5_text_wrap(c, &ui->fonts, face, MARGIN + 2, 286, 1000, 2, 62, 0x90000000u, game->title, 0);
            pc5_text_wrap(c, &ui->fonts, face, MARGIN, 284, 1000, 2, 62, C_TEXT, game->title, 0);
        } else {
            pc5_text(c, &ui->fonts, face, MARGIN + 3, 323, 0x90000000u, game->title, 1000, 0);
            pc5_text(c, &ui->fonts, face, MARGIN, 320, C_TEXT, game->title, 1000, 0);
        }
        below = 384;
    }
    /* a line about it, or why it can't start */
    if (!game->playable)
        pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, MARGIN, below, 860, 2, 38, C_WARN, TX(game->problem), 0);
    else if (ready && ui->hero.description[0])
        pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, MARGIN, below, 860, 2, 38, 0xf0eef1f6u, ui->hero.description, 0);
    /* Play, and the game's menu */
    int by = 436;
    /* as wide as its words need in a longer language, 300 at least */
    float bw = 100 + pc5_text_width(&ui->fonts, PC5_FACE_LABEL, T("Play Game")) + 44;
    if (bw < 300) bw = 300;
    pc5_shadow(c, MARGIN, by, bw, 72, 36, 20, 0x40000000u);
    pc5_round_rect(c, MARGIN, by, bw, 72, 36, game->playable ? 0xf5ffffffu : 0x99ffffffu);
    float tx = MARGIN + 62, ty = by + 36;
    for (int row = -12; row <= 12; row++) {
        float half = 12 - fabsf((float)row);
        pc5_line(c, tx - 10, ty + row, tx - 10 + 20 * (1 - fabsf((float)row) / 12), ty + row, 1.2f, 0xff111318u);
        (void)half;
    }
    pc5_text(c, &ui->fonts, PC5_FACE_LABEL, MARGIN + 100, by + 46, 0xff111318u, T("Play Game"), 0, 0);
    float mx = MARGIN + bw + 30 + 36;
    pc5_round_rect(c, mx - 36, by, 72, 72, 36, 0x66080a10u);
    pc5_ring(c, mx, by + 36, 35, 2, 0x99ffffffu);
    for (int d = -1; d <= 1; d++) pc5_circle(c, mx + d * 13, by + 36, 4, C_TEXT);
    /* its settings, in chips */
    char text[200];
    int x = MARGIN, y = 530;
    const char *api = pc5_api_name((Pc5Api)game->api);
    x += pill(ui, c, x, y, 42, api, -1) + 12;
    /* TRANSLATORS: a chip under the game's name: 64-bit or 32-bit. Keep it short. */
    pc5_tf(text, sizeof(text), "%d-bit", game->bits ? game->bits : 64);
    x += pill(ui, c, x, y, 42, text, -1) + 12;
    if (ready) {
        if (game->vulkan) x += pill(ui, c, x, y, 42, k->resolution == 2 ? "2160p" : k->resolution == 1 ? "1440p" : "1080p", -1) + 12;
        if (k->frame_generation) x += pill(ui, c, x, y, 42, T("Frame gen"), -1) + 12;
        pc5_tf(text, sizeof(text), "Controller: %s", T(pc5_pad_mode_name(k->mode)));
        x += pill(ui, c, x, y, 42, text, -1) + 12;
    }
    if (game->usb) pill(ui, c, x, y, 42, T("USB drive"), -1);
}

/* The covers along the bottom of Home, the chosen one first (towards the end, the last three stay
 * in view and the chosen one moves along them), and dots for where it is. */
static void cover_row(Pc5Ui *ui, Pc5Canvas *c)
{
    float step = COVER_W + COVER_GAP, extra = CHOSEN_W - COVER_W;
    int first = (int)floorf(ui->scroll) - 1, last = (int)ceilf(ui->scroll) + 10;
    for (int i = first; i <= last; i++) {
        if (i < 0 || i >= ui->library.count || i == ui->selected) continue;
        float d = (float)i - ui->scroll, x = MARGIN + d * step + (i > ui->selected ? extra : 0);
        if (x > W || x + COVER_W < 0) continue;
        cover_at(ui, c, i, x, ROW_TOP, COVER_W, COVER_H, 0);
    }
    float sx = MARGIN + ((float)ui->selected - ui->scroll) * step, sy = ROW_TOP + COVER_H - CHOSEN_H;
    /* its light on the floor under it: a soft glow */
    pc5_shadow(c, sx + 30, ROW_TOP + COVER_H + 24, CHOSEN_W - 60, 2, 1, 46, 0x384d7cffu);
    cover_at(ui, c, ui->selected, sx, sy, CHOSEN_W, CHOSEN_H, 1);
    /* where it is: a dot a page of eight */
    int pages = (ui->library.count + 7) / 8, page = ui->selected / 8;
    if (pages > 1 && pages <= 24) {
        float dw = 34, gap = 14, total = pages * dw + (pages - 1) * gap, x = (W - total) / 2;
        for (int p = 0; p < pages; p++)
            pc5_round_rect(c, x + p * (dw + gap), 958, dw, 4, 2, p == page ? 0xffffffffu : 0x4dffffffu);
    }
}

/* The Library tab's lists, made again when it opens. */
static int64_t played_at(Pc5Ui *ui, const Pc5Game *game)
{
    char path[PC5_PATH];
    Pc5Buf text = { 0 };
    int64_t at = 0;
    if (!pc5_format(path, sizeof(path), "%s/prefixes/%s/pc5/played.txt", ui->root, game->prefix) &&
        !pc5_read_file(path, &text, 64) && !pc5_buf_terminate(&text))
        at = strtoll((const char *)text.data, NULL, 10);
    pc5_buf_free(&text);
    return at;
}

static void lists_make(Pc5Ui *ui)
{
    int64_t when[PC5_MAX_GAMES];
    memset(ui->list_count, 0, sizeof(ui->list_count));
    for (int i = 0; i < ui->library.count; i++) {
        const Pc5Game *game = &ui->library.games[i];
        when[i] = played_at(ui, game);
        if (when[i] > 0) ui->lists[LIST_RECENT][ui->list_count[LIST_RECENT]++] = i;
        if (ui->favourite[i]) ui->lists[LIST_FAVOURITES][ui->list_count[LIST_FAVOURITES]++] = i;
        if (game->usb) ui->lists[LIST_USB][ui->list_count[LIST_USB]++] = i;
        if (!game->playable) ui->lists[LIST_ATTENTION][ui->list_count[LIST_ATTENTION]++] = i;
        ui->lists[LIST_ALL][ui->list_count[LIST_ALL]++] = i;
    }
    /* recently played: newest first */
    for (int a = 0; a < ui->list_count[LIST_RECENT]; a++)
        for (int b = a + 1; b < ui->list_count[LIST_RECENT]; b++)
            if (when[ui->lists[LIST_RECENT][b]] > when[ui->lists[LIST_RECENT][a]]) {
                int t = ui->lists[LIST_RECENT][a];
                ui->lists[LIST_RECENT][a] = ui->lists[LIST_RECENT][b];
                ui->lists[LIST_RECENT][b] = t;
            }
    for (int l = 0; l < LISTS; l++)
        if (ui->list_item[l] >= ui->list_count[l]) ui->list_item[l] = ui->list_count[l] ? ui->list_count[l] - 1 : 0;
    if (ui->list_at < 0 || ui->list_at >= LISTS) ui->list_at = LIST_ALL;
}

/* Library: the filters as pills, then the filter's games in a grid of covers with their
 * names, two rows and a bit showing. */
static void library_grid(Pc5Ui *ui, Pc5Canvas *c)
{
    const float cw = 208, ch = 312, gap_x = (W - 2 * MARGIN - GRID_COLUMNS * 208) / (GRID_COLUMNS - 1.0f), pitch = 384;
    int l = ui->list_at, n = ui->list_count[l], at = ui->list_item[l], x = MARGIN;
    for (int f = 0; f < LISTS; f++) {
        char text[160];
        snprintf(text, sizeof(text), "%s  %d", T(list_names[f]), ui->list_count[f]);
        if (f == l) {
            int w = pc5_text_width(&ui->fonts, PC5_FACE_SMALL, text) + 44;
            pc5_shadow(c, x, 136, w, 44, 22, 14, 0x704d7cffu);
            pill(ui, c, x, 136, 44, text, 1);
            x += w + 12;
        } else {
            x += pill(ui, c, x, 136, 44, text, 0) + 12;
        }
    }
    if (!n) {
        static const char *const empty[LISTS] = { N_("No games yet."), N_("Nothing played yet: games show here once you play them."),
                                                  N_("No favourites yet: press Square on a game to add it."),
                                                  N_("No game is played from a USB drive."), N_("Every game can start.") };
        pc5_text(c, &ui->fonts, PC5_FACE_BODY, 960, 520, C_DIM, T(empty[l]), 0, 1);
        return;
    }
    int row = at / GRID_COLUMNS, top = row > 0 ? row - 1 : 0;
    for (int r = top; r < top + 3; r++)
        for (int col = 0; col < GRID_COLUMNS; col++) {
            int k = r * GRID_COLUMNS + col;
            if (k >= n) break;
            float gx = MARGIN + col * (cw + gap_x), gy = 214 + (r - top) * pitch;
            if (gy > H - 120) continue;
            int i = ui->lists[l][k];
            cover_at(ui, c, i, gx, gy, cw, ch, k == at);
            pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)(gx + cw / 2), (int)(gy + ch + 36), k == at ? C_TEXT : C_DIM,
                     ui->library.games[i].title, (int)cw + 20, 1);
        }
}

static void library_screen(Pc5Ui *ui, Pc5Canvas *c)
{
    backdrop(ui, c);
    if (ui->focus && ui->library.count && ui->tab == TAB_HOME) {
        pc5_text(c, &ui->fonts, PC5_FACE_TITLE, MARGIN + 2, 1014, 0x90000000u, ui->library.games[ui->selected].title, 1100, 0);
        pc5_text(c, &ui->fonts, PC5_FACE_TITLE, MARGIN, 1012, C_TEXT, ui->library.games[ui->selected].title, 1100, 0);
        Hint list[] = { { G_UP_PAD, T("Back") }, { accept_glyph(ui), T("Play") } };
        hints_right(ui, c, 1012, list, 2);
        return;
    }
    top_bar(ui, c);
    if (!ui->library.count) {
        /* on a glass panel, so it reads over PC5's picture */
        pc5_shadow(c, 360, 380, 1200, 270, 28, 30, 0x80000000u);
        pc5_round_rect(c, 360, 380, 1200, 270, 28, 0xcc0b0f1au);
        pc5_round_frame(c, 360, 380, 1200, 270, 28, 1.5f, 0x40ffffffu);
        pc5_text(c, &ui->fonts, PC5_FACE_TITLE, 960, 470, C_TEXT, T("No games yet"), 0, 1);
        pc5_text(c, &ui->fonts, PC5_FACE_BODY, 960, 540, 0xf0e3e7efu,
                 T("Put a game's folder in PC5/INSTALL on a USB drive, then open Install \xe2\x80\xba Install games from USB."),
                 0, 1);
        pc5_text(c, &ui->fonts, PC5_FACE_BODY, 960, 590, 0xf0e3e7efu,
                 T("Or send it from a phone or PC: Settings \xe2\x80\xba Network \xe2\x80\xba Web page or FTP server."), 0, 1);
        Hint list[] = { { G_L1R1, T("Tabs") }, { G_OPTIONS, T("Settings") } };
        hints_right(ui, c, 1012, list, 2);
        return;
    }
    if (ui->tab == TAB_LIBRARY) {
        dim(c, 160);
        top_bar(ui, c);
        library_grid(ui, c);
        Hint list[] = { { G_L2R2, T("Filter") }, { accept_glyph(ui), T("Play") }, { G_TRIANGLE, T("Game options") }, { G_SQUARE, T("Favourite") } };
        hints_right(ui, c, 1012, list, 4);
        return;
    }
    hero(ui, c);
    cover_row(ui, c);
    Hint list[] = { { accept_glyph(ui), T("Play") }, { G_TRIANGLE, T("Game options") }, { G_SQUARE, T("Favourite") }, { G_OPTIONS, T("Settings") } };
    hints_right(ui, c, 1012, list, 4);
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
        Hint list[] = { { back_glyph(ui), T("Back") } };
        hints(ui, c, (int)x + 170, (int)(y + h - 34), list, 1);
    }
}

/* The drawer on the right: Settings and a game's menu. */
static float drawer(Pc5Ui *ui, Pc5Canvas *c, const char *title)
{
    float x = W - 760;
    /* the page under the top bar, the tabs still showing */
    pc5_round_rect(c, 0, 0, W, H, 0, 0x8c000000u);
    top_bar(ui, c);
    pc5_shadow(c, x - 24, 112, 760 + 48, H - 112 - 24, 28, 40, 0x90000000u);
    pc5_round_rect(c, x - 24, 112, 760 + 48, H - 112 - 24, 28, C_GLASS);
    pc5_round_frame(c, x - 24, 112, 760 + 48, H - 112 - 24, 28, 1.5f, 0x40ffffffu);
    pc5_text(c, &ui->fonts, PC5_FACE_HEAD, (int)x + 64, 172, C_TEXT, title, 760 - 128, 0);
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
    float x = drawer(ui, c, T(page_titles[ui->page])), extra;
    SettingRow rows[PAGE_ROWS];
    int count = page_rows(&ui->settings, ui->page, offer_update(ui), rows);
    const char *label = "";
    char about[200], value[64];

    if (ui->menu < count) setting_text(ui, rows[ui->menu], &label, about, sizeof(about), value, sizeof(value));
    else about[0] = 0;
    float pitch = rows_pitch(ui, 212, count, about, &extra);
    for (int i = 0; i < count; i++) {
        setting_text(ui, rows[i], &label, about, sizeof(about), value, sizeof(value));
        drawer_row(ui, c, x, 212 + pitch * i + (i > ui->menu ? extra : 0), pitch + (i == ui->menu ? extra : 0),
                   i == ui->menu, label, about, value);
    }
    SettingRow row = rows[ui->menu < count ? ui->menu : 0];
    int changes = row == SR_KEEP_LOGS || row == SR_KEYBOARD || row == SR_HIGH_REFRESH || row == SR_FTP || row == SR_WEB || (row >= SR_FPS && row <= SR_TRIGGER_RESISTANCE) || row == SR_CHECK_ON_OPEN ||
                  row == SR_STARTUP_SOUND || row == SR_STARTUP_VOLUME || row >= SR_SOURCE;
    Hint list[] = { { accept_glyph(ui), changes ? T("Change") : T("Choose") }, { back_glyph(ui), T("Back") }, { G_TRIANGLE, T("QR code") } };
    Pc5WebStatus web = { 0 };
    if (row == SR_WEB) pc5_web_status(&web);
    hints(ui, c, (int)x + 64, 1012, list, row == SR_WEB && web.running ? 3 : 2);
}

/* A row of a game's menu: its label, what it does (text, which may be about) and its value. */
static void game_row_text(Pc5Ui *ui, const Pc5Game *game, int kind, GameRow row, char *about, size_t about_size,
                          const char **label, const char **text, const char **value)
{
    static const char *const more = "\xe2\x80\xba";
    *label = *text = *value = "";
    switch (row) {
    case GR_PLAY:
        *label = T("Play");
        *text = T("To leave it, hold the touchpad and OPTIONS for 2 s");
        *value = "";
        break;
    case GR_CONTROLLER:
        *label = T("Controller");
        *text = ui->controls.mode == PC5_PAD_DUALSENSE ? T("The game sees a DualSense, as one plugged into a PC") : ui->controls.mode == PC5_PAD_WHEEL ? T("The game sees a Logitech G29 steering wheel") : ui->controls.mode == PC5_PAD_KEYBOARD ? T("The buttons press keys, the sticks move the mouse") : T("Games with controller support see an Xbox controller");
        *value = T(pc5_pad_mode_name(ui->controls.mode));
        break;
    case GR_BUTTONS:
        *label = T("Button mapping");
        *text = T("The key or mouse button each one sends");
        *value = more;
        break;
    case GR_POINTER:
        *label = T("Mouse pointer");
        *text = ui->controls.pointer_hide ? T("Hides when not moved or clicked for 5 s") : T("Stays as the game shows it");
        *value = ui->controls.pointer_hide ? T("Auto-hide") : T("Normal");
        break;
    case GR_GRAPHICS:
        *label = T("Graphics");
        *text = kind & GAME_VULKAN ? (kind & GAME_32BIT ? T("Frames, resolution, aspect ratio, memory and CPU") : T("Frame generation, resolution, aspect ratio and CPU")) : (kind & GAME_32BIT ? T("Frame generation, aspect ratio, memory and CPU") : T("Frame generation, aspect ratio and CPU"));
        *value = more;
        break;
    case GR_APPEARANCE:
        *label = T("Appearance");
        *text = T("Its cover, its background and a home screen tile");
        *value = more;
        break;
    case GR_MANAGE:
        *label = T("Manage");
        *text = kind & GAME_PC5 ? T("Its program, updating it and uninstalling it") : T("Its program and uninstalling it");
        *value = more;
        break;
    case GR_GRAPHICS_API: {
        int api = ui_game_api(ui);
        const char *about_api =
            ui->controls.graphics == PC5_GRAPHICS_AUTO
                ? (api == PC5_API_DDRAW ? T("The usual way for older games like this; change it only if the picture is wrong")
                                        : T("Best for most games; change it only if the picture is wrong or it won't start"))
            : api == PC5_API_D3D12 ? T("A slower way to draw it; set it back to Auto if it doesn't help")
            : ui->controls.graphics == PC5_GRAPHICS_WINE_2D ? T("Drawn in 2D only; set it back to Auto if it doesn't help")
            : ui->controls.graphics == PC5_GRAPHICS_WINE_VULKAN ? T("Another way to draw it; set it back to Auto if it doesn't help")
                                                                 : T("The slowest way, but it starts the most games; Auto if it doesn't help");
        *label = T("Graphics mode");
        *text = about_api;
        *value = T(pc5_graphics_name(ui->controls.graphics, api));
        break;
    }
    case GR_FRAMEGEN:
        *label = T("Frame generation");
        /* 40 -> 120 (MihawkGen) only with 120 Hz output: at 60 Hz it would be 30 -> 60 as well,
         * so a game set to it shows and starts with 30 -> 60 until 120 Hz output is on again. */
        if (ui->controls.frame_generation == 2 && ui->settings.high_refresh) {
            *text = T("Held at 40 fps, two frames made between each two: for a game that can't hold 60");
            *value = T("40 \xe2\x86\x92 120 fps");
        } else if (ui->controls.frame_generation) {
            *text = ui->settings.high_refresh ? T("Held at 60 fps, a frame made between each two: smoother on a 120 Hz TV")
                                              : T("Held at 30 fps and shown at 60: smoother where it runs below 60");
            *value = ui->settings.high_refresh ? T("60 \xe2\x86\x92 120 fps") : T("30 \xe2\x86\x92 60 fps");
        } else {
            *text = ui->settings.high_refresh ? T("Off. Left and right: 60 \xe2\x86\x92 120 or 40 \xe2\x86\x92 120 fps")
                                              : T("Off. Left and right: 30 \xe2\x86\x92 60 fps (with 120 Hz output on, 60 \xe2\x86\x92 120 or 40 \xe2\x86\x92 120)");
            *value = T("Off");
        }
        break;
    case GR_RESOLUTION:
        *label = T("Resolution");
        *text = ui->controls.resolution == 2 ? T("Drawn at 4K, as the console shows it; four times the work of 1080p") : ui->controls.resolution == 1 ? T("Drawn at 1440p and shown at 4K; sharper than 1080p, slower") : T("Drawn at 1080p and shown at 4K by the console");
        *value = ui->controls.resolution == 2 ? T("4K") : ui->controls.resolution == 1 ? "1440p" : "1080p";
        break;
    case GR_ASPECT:
        *label = T("Aspect ratio");
        *text = ui->controls.stretch ? T("Stretched to fill the screen: a 4:3 picture gets wider") : T("The picture's own shape, with bars beside a 4:3 one");
        *value = ui->controls.stretch ? T("Stretch") : T("Keep");
        break;
    case GR_MEMORY:
        *label = T("Game memory");
        *text = ui->controls.memory_4gb ? T("4 GB even for a game not made for it: room for mods; Auto if it misbehaves")
                                        : T("2 GB, or 4 GB for a game made for it, as on Windows");
        *value = ui->controls.memory_4gb ? T("4 GB") : T("Auto");
        break;
    case GR_VIDEO_MEMORY: {
        const int bits32 = (kind & GAME_32BIT) != 0;
        const int *sizes;
        int level = pc5_video_memory_level(&ui->controls, bits32);
        (void)pc5_video_memory_sizes(bits32, &sizes);
        *label = T("Graphics memory");
        if (sizes[level] < 0)
            *text = bits32 ? T("All the console's; Auto if the game says there is too little")
                           : T("All 8 GB: the game may take memory the rest of it needs");
        else if (!sizes[level])
            *text = bits32 ? T("2 GB at most, so older games don't get confused")
                           : T("4 GB, so the game leaves room for the rest: the console's memory is shared");
        else if (sizes[level] < (bits32 ? 2048 : 4096))
            *text = T("Less for textures, more for the game: for one that runs out of memory");
        else
            *text = bits32 ? T("More for a game that says there is too little; Auto if it misbehaves")
                           : T("More for the game's textures, less for the rest; Auto if it runs out of memory");
        if (sizes[level] < 0) *value = T("Maximum");
        else if (!sizes[level]) {
            pc5_tf(about, about_size, "%s (%s)", T("Auto"), bits32 ? T("2 GB") : T("4 GB"));
            *value = about;
        } else
            *value = sizes[level] == 512 ? T("512 MB") : sizes[level] == 1024 ? T("1 GB") : sizes[level] == 2048 ? T("2 GB")
                   : sizes[level] == 3072 ? T("3 GB") : sizes[level] == 5120 ? T("5 GB") : T("6 GB");
        break;
    }
    case GR_SOUND:
        *label = T("Sound output");
        *text = ui->controls.surround ? T("Each sound on its own speaker of a surround system; Stereo if it is silent")
                                      : T("Two speakers, for a TV or headphones; 5.1 or 7.1 for a surround system");
        *value = ui->controls.surround == 2 ? "7.1" : ui->controls.surround == 1 ? "5.1" : T("Stereo");
        break;
    case GR_CPU_SHARING:
        *label = T("Stutter fix");
        *text = ui->controls.cpu_sharing ? T("The game's work takes turns, as on a PC: steadier for some games") : T("Fastest for most games; turn on if it freezes or stutters");
        *value = ui->controls.cpu_sharing ? T("On") : T("Off");
        break;
    case GR_COVER:
        *label = T("Cover");
        *text = T("Another of those found, or search by another name");
        *value = more;
        break;
    case GR_BACKGROUND:
        *label = T("Background");
        *text = kind & GAME_MEDIA ? T("Choose a background image or a silent video")
                                  : T("Another of those found, search by another name, or none");
        *value = more;
        break;
    case GR_BACKGROUND_IMAGE:
        *label = T("Background image");
        *text = T("Choose a still image; it also appears when video playback is unavailable");
        *value = more;
        break;
    case GR_BACKGROUND_VIDEO:
        *label = T("Background video");
        *text = T("Choose or remove this game's silent background video");
        *value = more;
        break;
    case GR_VIDEO_FILE:
        *label = T("Choose video file");
        *text = T("Import H.264 video to console storage; experimental 1080p/60, without audio");
        *value = more;
        break;
    case GR_VIDEO_CLEAR:
        *label = T("Remove background video");
        *text = T("Delete the imported video and return to the background image; keep the source file");
        *value = more;
        break;
    case GR_MUSIC:
        *label = T("Library music");
        *text = T("Choose or remove the music that plays on this game's cover");
        *value = more;
        break;
    case GR_MUSIC_FILE:
        *label = T("Choose music file");
        *text = T("Import MP3 or WAV to console storage: plays on this cover, even without USB");
        *value = more;
        break;
    case GR_MEDIA:
        *label = T("Media settings and backup");
        *text = T("Its cover, background and loading screen, and its music and video settings");
        *value = more;
        break;
    case GR_MEDIA_OPTIONS:
        *label = T("Music, video and backup");
        *text = T("Music volume and start, video loop, framing, darkness and appearance backup");
        *value = more;
        break;
    case GR_LOADING: {
        char chosen[PC5_PATH];
        *label = T("Loading screen");
        *text = T("The picture shown while it starts: PC5's own, or one of yours");
        *value = !pc5_loading_chosen(ui->root, game->prefix, chosen, sizeof(chosen)) ? T("Own") : T("PC5's");
        break;
    }
    case GR_LOADING_FILE:
        *label = T("Choose image file");
        *text = T("A JPEG or PNG from a USB drive or the console; Square: from your phone");
        *value = more;
        break;
    case GR_LOADING_BACKGROUND:
        *label = T("Use its background");
        *text = T("Its background picture, as its loading screen");
        *value = "";
        break;
    case GR_LOADING_CLEAR:
        *label = T("Use PC5's");
        *text = T("Back to PC5's own loading screen for it");
        *value = "";
        break;
    case GR_MUSIC_CLEAR:
        *label = T("Remove library music");
        *text = T("Delete this game's imported music; the original source file is kept");
        *value = "";
        break;
    case GR_ICON:
        *label = T("Tile picture");
        *text = T("Its home screen tile's picture: a square one found online, or one made from its cover");
        *value = more;
        break;
    case GR_HOME:
        if (ui->tile[0]) pc5_tf(about, about_size, "Its tile, %.40s, starts the game", ui->tile);
        *label = T("Home screen tile");
        *text = ui->tile[0] ? about : T("A tile on the PS5's home screen that starts it");
        *value = ui->tile[0] ? T("On") : T("Off");
        break;
    case GR_PROGRAM: {
        const char *exe = strrchr(game->executable, '\\');
        pc5_tf(about, about_size, "Now: %.150s", exe ? exe + 1 : game->executable);
        *label = T("Change program");
        *text = about;
        *value = "";
        break;
    }
    case GR_STEAM:
        *label = T("Steam emulator");
        *text = ui->controls.steam_off ? T("Steam's own library: the game won't find Steam on the console")
                                       : T("Stands in for Steam, which the game asks for; Off if it misbehaves");
        *value = ui->controls.steam_off ? T("Off") : T("Auto");
        break;
    case GR_FILES:
        *label = T("Game files");
        *text = T("Its folder: copy, move, rename and delete its files");
        *value = more;
        break;
    case GR_UPDATE:
        *label = T("Update");
        *text = T("A newer copy from USB; its saves and settings stay");
        *value = more;
        break;
    case GR_REPORT:
        *label = T("Report a problem");
        *text = T("A QR code for GitHub's form, filled in from the game's newest log");
        *value = more;
        break;
    case GR_UNINSTALL:
        *label = T("Uninstall");
        *text = T("Deletes the game, or keeps its saves for later");
        *value = "";
        break;
    }
}

/* What a game setting does, in plain words, for its About window (□ on the row): a line on what
 * it is, then one for each choice. 0 for a row without one. */
static int setting_about(Pc5Ui *ui, GameRow row, const char *lines[6])
{
    int n = 0, api = ui_game_api(ui), bits32 = ui_game_kind(ui) & GAME_32BIT;
    switch (row) {
    case GR_CONTROLLER:
        lines[n++] = T("What the game sees when you play with the controller.");
        lines[n++] = T("Xbox controller: what most PC games expect. Try this first.");
        lines[n++] = T("DualSense: a PS5 controller plugged into a PC, with PlayStation buttons in games that support it.");
        lines[n++] = T("Keyboard & mouse: for games with no controller support. The buttons press keys and the sticks move the mouse; Button mapping changes which key each one sends.");
        break;
    case GR_BUTTONS:
        lines[n++] = T("What each button, stick and the touchpad sends to the game with Keyboard & mouse.");
        lines[n++] = T("Touchpad: One button (a click sends one key), Halves and swipes (a key for each half's click and each quick swipe up, down, left or right), or Mouse (a finger moves the pointer, like a laptop's).");
        lines[n++] = T("Two-finger click: PC5's on-screen keyboard, or a key of its own.");
        lines[n++] = T("Save as my mapping keeps your layout for every game set up after it.");
        break;
    case GR_POINTER:
        lines[n++] = T("Whether the mouse pointer shows over the game.");
        lines[n++] = T("Normal: the game decides, as on a PC.");
        lines[n++] = T("Auto-hide: hidden after 5 seconds without moving or clicking, for a game that leaves it on the screen.");
        break;
    case GR_GRAPHICS_API:
        lines[n++] = T("How the game's pictures are made on the console. Auto is right for almost every game; the others are for a game that won't start or shows a wrong picture.");
        if (api == PC5_API_DDRAW) {
            lines[n++] = T("Alternative: another way to draw the game. Try it if the picture is wrong with Auto.");
            lines[n++] = T("2D only: for a 2D game that flickers or shows only part of its screen.");
        } else if (api == PC5_API_D3D12) {
            lines[n++] = T("Alternative: slower than Auto. Try it if the game won't start or its picture is wrong.");
        } else {
            lines[n++] = T("Alternative: another way to draw the game. Try it if the picture is wrong with Auto. Some mods need it.");
            lines[n++] = T("Compatibility (slow): the slowest, but it starts the most games.");
        }
        lines[n++] = T("Choose the row to see each one with what it does.");
        break;
    case GR_FRAMEGEN:
        lines[n++] = T("Makes extra frames between the game's own, so movement looks smoother. It can add a little delay to the controls.");
        lines[n++] = T("30 \xe2\x86\x92 60 fps: the game is held at 30 fps and shown at 60.");
        lines[n++] = T("60 \xe2\x86\x92 120 fps: held at 60 and shown at 120, on a 120 Hz TV with 120 Hz output on in Settings.");
        lines[n++] = T("40 \xe2\x86\x92 120 fps: held at 40, two frames made between each two, for a game that can't hold 60.");
        break;
    case GR_RESOLUTION:
        lines[n++] = T("The size the game is drawn at. The console always shows 4K, so a smaller picture is scaled up.");
        lines[n++] = T("1080p: the fastest, and right for most games.");
        lines[n++] = T("1440p: sharper, but slower.");
        lines[n++] = T("4K: the sharpest, four times the work of 1080p: only for light games.");
        break;
    case GR_ASPECT:
        lines[n++] = T("The shape of the picture on the TV.");
        lines[n++] = T("Keep: the picture's own shape. An older 4:3 game gets black bars at the sides.");
        lines[n++] = T("Stretch: fills the whole screen, so a 4:3 picture gets wider.");
        break;
    case GR_MEMORY:
        lines[n++] = T("How much memory an older 32-bit game may use.");
        lines[n++] = T("Auto: as on Windows, 4 GB for a game made for it, otherwise 2 GB.");
        lines[n++] = T("4 GB: for a game that runs out of memory, often with big mods. Set it back to Auto if the game misbehaves.");
        break;
    case GR_VIDEO_MEMORY:
        lines[n++] = T("How much graphics memory the game is told the console has. The PS5's memory is shared by the game and its graphics, so more for graphics leaves less for the game.");
        if (bits32) {
            lines[n++] = T("512 MB or 1 GB: less for textures, more for the game, for one that runs out of memory.");
            lines[n++] = T("Auto: 2 GB at most, so older games don't get confused by a large number.");
            lines[n++] = T("3 GB or Maximum: for a game that says there is too little. Set it back to Auto if it misbehaves.");
        } else {
            lines[n++] = T("2 GB or 3 GB: less for textures, more for the game, for one that runs out of memory.");
            lines[n++] = T("Auto: 4 GB, room for both.");
            lines[n++] = T("5 GB or 6 GB: more for textures. Set it back to Auto if the game runs out of memory.");
            lines[n++] = T("Maximum: all of it, only for a game that won't start because it sees too little.");
        }
        break;
    case GR_SOUND:
        lines[n++] = T("How many speakers the game is told the console has.");
        lines[n++] = T("Stereo: two, right for a TV or headphones.");
        lines[n++] = T("5.1 or 7.1: the game plays each sound on its own speaker of a surround system, and the PS5 sends it on as it does its own games' sound.");
        lines[n++] = T("If the game is silent or plays only from the front, set it back to Stereo.");
        break;
    case GR_CPU_SHARING:
        lines[n++] = T("Changes how the game's work is shared out on the console's processor.");
        lines[n++] = T("Off: the fastest for most games.");
        lines[n++] = T("On: the game's work takes turns, as on a PC. Try it if the game freezes, stutters or hangs while loading; it can be a little slower.");
        break;
    case GR_STEAM:
        lines[n++] = T("Many PC games need Steam running to start. The console has no Steam, so PC5 stands in for it.");
        lines[n++] = T("Auto: the game starts as if Steam were running, offline.");
        lines[n++] = T("Off: the game's own Steam files are used. It won't find Steam and may not start; only for a game that misbehaves with Auto.");
        break;
    default:
        break;
    }
    return n;
}

/* The About window over the game menu: what the chosen setting does and what each choice means. */
static void setting_about_dialog(Pc5Ui *ui, Pc5Canvas *c, GameRow row, const char *title)
{
    enum { LINE = 36, TEXT_W = 1100 - 120 };
    const char *lines[6];
    int count = setting_about(ui, row, lines), wraps[6];
    float w = 1100, h = 130 + 110, x, y, cy;
    for (int i = 0; i < count; i++) {
        wraps[i] = pc5_text_lines(&ui->fonts, PC5_FACE_SMALL, TEXT_W, lines[i]);
        h += wraps[i] * LINE + 18;
    }
    x = (W - w) / 2;
    y = (H - h) / 2;
    dim(c, 150);
    panel(c, x, y, w, h);
    pc5_text(c, &ui->fonts, PC5_FACE_HEAD, (int)x + 60, (int)y + 92, C_TEXT, title, (int)w - 120, 0);
    cy = y + 130;
    for (int i = 0; i < count; i++) {
        pc5_text_wrap(c, &ui->fonts, PC5_FACE_SMALL, (int)x + 60, (int)cy + LINE, TEXT_W, wraps[i], LINE,
                      i ? C_DIM : C_TEXT, lines[i], 0);
        cy += wraps[i] * LINE + 18;
    }
    Hint list[] = { { back_glyph(ui), T("Back") } };
    hints(ui, c, (int)x + 60, (int)(y + h - 40), list, 1);
}

static void game_menu_screen(Pc5Ui *ui, Pc5Canvas *c, int chosen)
{
    int index = pc5_library_find(&ui->library, ui->launch_id), kind = ui_game_kind(ui), page = ui->game_page;
    if (index < 0) return;
    const Pc5Game *game = &ui->library.games[index];
    float x = drawer(ui, c, page == GP_MAIN ? game->title : T(game_page_titles[page])), extra;
    char about[200];
    const char *label, *text, *value;

    GameRow rows[GAME_ROWS];
    int count = game_rows(&ui->controls, kind, page, rows);
    pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)x + 64, 210, C_DIM,
             page != GP_MAIN ? game->title : game->playable ? TX(game->detail) : TX(game->problem), 760 - 128, 0);
    text = "";
    if (chosen >= 0 && chosen < count) game_row_text(ui, game, kind, rows[chosen], about, sizeof(about), &label, &text, &value);
    float pitch = rows_pitch(ui, 240, count, text, &extra);
    for (int i = 0; i < count; i++) {
        game_row_text(ui, game, kind, rows[i], about, sizeof(about), &label, &text, &value);
        drawer_row(ui, c, x, 240 + pitch * i + (i > chosen ? extra : 0), pitch + (i == chosen ? extra : 0), chosen == i,
                   label, text, value);
    }
    GameRow row = chosen >= 0 && chosen < count ? rows[chosen] : GR_PLAY;
    int changes = row == GR_CONTROLLER || row == GR_FRAMEGEN || row == GR_RESOLUTION || row == GR_ASPECT ||
                  row == GR_MEMORY || row == GR_VIDEO_MEMORY || row == GR_CPU_SHARING || row == GR_HOME || row == GR_STEAM ||
                  row == GR_SOUND;
    const char *about_lines[6];
    int has_about = setting_about(ui, row, about_lines) > 0;
    Hint list[] = { { accept_glyph(ui), changes ? T("Change") : T("Choose") }, { back_glyph(ui), T("Back") },
                    { G_SQUARE, has_about ? T("About") : T("Receive from phone") } };
    hints(ui, c, (int)x + 64, 1012, list, phone_row_kind(row) >= 0 || has_about ? 3 : 2);
}

/* The About window of the game menu's chosen row, over the menu. */
static void setting_about_screen(Pc5Ui *ui, Pc5Canvas *c)
{
    int index = pc5_library_find(&ui->library, ui->launch_id), kind = ui_game_kind(ui);
    GameRow rows[GAME_ROWS];
    int count = game_rows(&ui->controls, kind, ui->game_page, rows);
    char about[200];
    const char *label, *text, *value;
    if (index < 0 || ui->menu < 0 || ui->menu >= count) return;
    game_row_text(ui, &ui->library.games[index], kind, rows[ui->menu], about, sizeof(about), &label, &text, &value);
    setting_about_dialog(ui, c, rows[ui->menu], label);
}

/* Button mapping: what each button and stick does while the game plays the keyboard and mouse. */
static void buttons_screen(Pc5Ui *ui, Pc5Canvas *c)
{
    float x = drawer(ui, c, T("Button mapping"));
    PadRow rows[PAD_ROWS];
    int count = pad_rows(&ui->controls, rows, pc5_controls_mapping_exists(ui->root));
    const Pc5Controls *k = &ui->controls;

    /* What the touchpad's rows do, over them while one is chosen; else how to leave the game. */
    {
        PadRow chosen = ui->pad_row < count ? rows[ui->pad_row] : rows[0];
        const char *about = T("Touchpad + OPTIONS for 2 s still leaves the game");
        if (chosen.kind == PR_BUTTON && chosen.index == PC5_TWO_FINGER)
            about = T("Two fingers: PC5's on-screen keyboard, or a key of its own");
        else if (chosen.kind == PR_TOUCH_MODE || (chosen.kind == PR_BUTTON && (chosen.index == PC5_TOUCHPAD ||
                                                                               chosen.index >= PC5_TOUCH_FIRST)))
            about = k->touch_mode == 1   ? T("A key for each half's click and each quick swipe")
                    : k->touch_mode == 2 ? T("A finger moves the pointer; a click sends its key")
                                         : T("The whole touchpad is one button");
        pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)x + 64, 210, C_DIM, about, 760 - 128, 0);
    }
    for (int i = ui->pad_top; i < count && i < ui->pad_top + PAD_VISIBLE; i++) {
        float y = 214 + PAD_PITCH * (i - ui->pad_top);
        char label[64], value[64];
        PadRow row = rows[i];
        value[0] = 0;
        switch (row.kind) {
        case PR_STICK:
            pc5_copy(label, sizeof(label), row.index ? T("Right stick") : T("Left stick"));
            pc5_copy(value, sizeof(value), T(pc5_stick_use_name(k->stick[row.index])));
            break;
        case PR_DIRECTION:
            snprintf(label, sizeof(label), "    %s", T(pc5_control_name(row.index)));
            pc5_copy(value, sizeof(value), pc5_key_text(k->key[row.index]));
            break;
        case PR_SPEED:
            pc5_copy(label, sizeof(label), T("Pointer speed"));
            pc5_tf(value, sizeof(value), "%d of %d", k->speed, PC5_SPEEDS);
            break;
        case PR_SLOW:
            pc5_copy(label, sizeof(label), T("Slow pointer button"));
            pc5_copy(value, sizeof(value), k->slow_button >= 0 ? T(pc5_control_name(k->slow_button)) : T("None"));
            break;
        case PR_SLOW_PERCENT:
            pc5_copy(label, sizeof(label), T("    Slower by"));
            pc5_tf(value, sizeof(value), "%d%%", k->slow_percent);
            break;
        case PR_BUTTON:
            /* The touchpad's rows sit under Touchpad, indented. */
            if (row.index == PC5_TOUCHPAD) snprintf(label, sizeof(label), "    %s", T("Click"));
            else if (row.index >= PC5_TOUCH_FIRST) snprintf(label, sizeof(label), "    %s", T(pc5_control_name(row.index)));
            else pc5_copy(label, sizeof(label), T(pc5_control_name(row.index)));
            pc5_copy(value, sizeof(value), row.index == k->slow_button ? T("Slows the pointer")
                                           : row.index == PC5_TWO_FINGER && !strcmp(pc5_keys[k->key[row.index] % pc5_key_count].name, "none")
                                               ? T("On-screen keyboard")
                                               : pc5_key_text(k->key[row.index]));
            break;
        case PR_TOUCH_MODE:
            pc5_copy(label, sizeof(label), T("Touchpad"));
            pc5_copy(value, sizeof(value), T(pc5_touch_mode_name(k->touch_mode)));
            break;
        case PR_RESET:
            pc5_copy(label, sizeof(label), T("Reset to default"));
            break;
        case PR_SAVE_MAPPING:
            pc5_copy(label, sizeof(label), T("Save as my mapping"));
            break;
        case PR_USE_MAPPING:
            pc5_copy(label, sizeof(label), T("Use my mapping"));
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
    Hint list[] = { { accept_glyph(ui), bindable ? T("Choose a key") : row.kind == PR_RESET ? T("Reset")
                                         : row.kind == PR_SAVE_MAPPING ? T("Save") : row.kind == PR_USE_MAPPING ? T("Use")
                                         : T("Change") },
                    { G_SQUARE, T("Nothing") }, { back_glyph(ui), T("Back") } };
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
    pc5_tf(title, sizeof(title), "What %s sends", T(pc5_control_name(ui->key_control)));
    pc5_text(c, &ui->fonts, PC5_FACE_HEAD, (int)x + 60, (int)y + 92, C_TEXT, title, (int)w - 120, 0);
    for (int i = 0; i < pc5_key_count; i++) {
        float cx = x + 60 + (i % KEY_COLS) * (cell_w + gap), cy = y + 140 + (i / KEY_COLS) * (cell_h + gap);
        int chosen = i == ui->key_choice, now = i == ui->controls.key[ui->key_control];
        pc5_round_rect(c, cx, cy, cell_w, cell_h, 12, chosen ? C_TEXT : C_ROW);
        if (now && !chosen) pc5_round_frame(c, cx, cy, cell_w, cell_h, 12, 2.5f, C_ACCENT);
        pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)(cx + cell_w / 2), (int)cy + 36, chosen ? C_PANEL : C_TEXT,
                 pc5_key_text(i), (int)cell_w - 16, 1);
    }
    Hint list[] = { { accept_glyph(ui), T("Choose") }, { back_glyph(ui), T("Back") } };
    hints(ui, c, (int)x + 60, (int)(y + h - 40), list, 2);
}

/* What a Graphics API choice does, in full, for a game whose graphics are api (the Graphics API window). */
static const char *graphics_about(Pc5Graphics graphics, int api)
{
    if (graphics == PC5_GRAPHICS_AUTO)
        return api == PC5_API_DDRAW ? T("The usual way for older games like this one, with their 2D and 3D.")
                                    : T("The fastest, and what most games want. Leave it here unless the game won't start or its picture is wrong.");
    if (api == PC5_API_D3D12)
        return T("Another way to draw the game, slower than Auto. Try it if the game won't start or its picture is wrong.");
    if (graphics == PC5_GRAPHICS_WINE_2D)
        return T("Draws everything in 2D, the way ordinary Windows programs are drawn. For a 2D game that flickers or shows only part of its screen; a game with 3D scenes won't start with it.");
    if (graphics == PC5_GRAPHICS_WINE_VULKAN)
        return T("Another way to draw the game. Try it if the picture is wrong with Auto; faster than Compatibility. Some mods need it.");
    return T("The slowest, but it starts the most games. Try it if the game won't start with the others.");
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
    pc5_text(c, &ui->fonts, PC5_FACE_HEAD, (int)x + 60, (int)y + 92, C_TEXT, T("Graphics mode"), (int)w - 120, 0);
    pc5_tf(title, sizeof(title), "The game uses %s", pc5_api_name((Pc5Api)api));
    pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)x + 60, (int)y + 136, C_DIM, title, (int)w - 120, 0);
    cy = y + 170;
    for (int i = 0; i < count; i++) {
        float bh = 74 + lines[i] * LINE;
        int chosen = i == ui->api_choice, now = choices[i] == ui->controls.graphics;
        pc5_round_rect(c, x + 60, cy, w - 120, bh, 16, chosen ? C_TEXT : C_ROW);
        if (now && !chosen) pc5_round_frame(c, x + 60, cy, w - 120, bh, 16, 2.5f, C_ACCENT);
        pc5_text(c, &ui->fonts, PC5_FACE_LABEL, (int)x + 84, (int)cy + 50, chosen ? C_PANEL : C_TEXT,
                 T(pc5_graphics_name(choices[i], api)), TEXT_W - 200, 0);
        if (now)
            pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)(x + w) - 84, (int)cy + 48, chosen ? C_PANEL : C_ACCENT,
                     T("In use"), 200, 2);
        pc5_text_wrap(c, &ui->fonts, PC5_FACE_SMALL, (int)x + 84, (int)cy + 50 + LINE + 6, TEXT_W, lines[i], LINE,
                      chosen ? C_PANEL : C_DIM, graphics_about(choices[i], api), 0);
        cy += bh + 16;
    }
    Hint list[] = { { accept_glyph(ui), T("Choose") }, { back_glyph(ui), T("Back") } };
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
        N_("PC5 runs your PC games with prospero-win: Wine, DXVK for Direct3D 8 to 11, VKD3D-Proton for "
         "Direct3D 12, and Mesa (RADV for Vulkan, Zink for OpenGL)."),
        N_("Covers come from Steam, GOG, PCGamingWiki and Wikidata, and SteamGridDB with your key. No game is part of PC5."),
        N_("Free software under the GNU LGPL 2.1 or later."),
    };

    dim(c, 150);
    panel(c, x, y, w, h);
    wordmark(ui, c, (int)x + 64, (int)y + 110);
    {
        char version[96];
        /* TRANSLATORS: About PC5: the version (2.0.7 · PPSA11018). */
        pc5_tf(version, sizeof(version), "version %s", PC5_VERSION " \xc2\xb7 " PC5_TITLE_ID);
        pc5_text(c, &ui->fonts, PC5_FACE_BODY, (int)x + 160, (int)y + 106, C_DIM, version, 0, 0);
    }
    /* Buy Me a Coffee, on the right: a code for a phone, the address under it. */
    const int box = 300, cx = (int)(x + w) - box - 64, column = cx - 64 - ((int)x + 64);
    if (!ui->coffee_qr.size) pc5_qr_make(PC5_COFFEE, &ui->coffee_qr);
    pc5_text(c, &ui->fonts, PC5_FACE_LABEL, cx + box / 2, (int)y + 96, C_TEXT, T("Like PC5?"), box, 1);
    qr_draw(c, &ui->coffee_qr, cx, (int)y + 124, box);
    pc5_round_rect(c, (float)cx, y + 444, (float)box, 60, 30, 0xffffdd00u);
    pc5_text(c, &ui->fonts, PC5_FACE_LABEL, cx + box / 2, (int)y + 484, 0xff0d0c22u, T("Buy me a coffee"), box - 24, 1);
    pc5_text(c, &ui->fonts, PC5_FACE_SMALL, cx + box / 2, (int)y + 548, C_DIM, "buymeacoffee.com/bornaradusin", box + 100, 1);
    for (int i = 0; i < 3; i++)
        ty += 42 * pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, (int)x + 64, ty, column, 3, 42, C_TEXT, T(paragraphs[i]), 0) + 22;
    pc5_tf(line, sizeof(line), "Games are kept in %.150s", ui->root);
    pc5_text(c, &ui->fonts, PC5_FACE_BODY, (int)x + 64, ty + 20, C_DIM, line, column, 0);
    if (!pc5_free_bytes(ui->root, &free_bytes)) {
        pc5_size_text(free_bytes, space, sizeof(space));
        pc5_tf(line, sizeof(line), "%s free on the console", space);
        pc5_text(c, &ui->fonts, PC5_FACE_BODY, (int)x + 64, ty + 68, C_DIM, line, column, 0);
    }
    Hint list[] = { { back_glyph(ui), T("Back") } };
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
            pc5_tf(more, sizeof(more), "%d of %d", ui->menu + 1, count);
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
    char line[160];
    (void)chosen;
    tick_box(c, x, y + 20, ui->ticked[i]);
    const char *from = ui->scan_roots[ui->sources[i].drive];
    char drive_root[PC5_PATH];
    if (from == ui->install_dir) pc5_tf(line, sizeof(line), "INSTALL folder");
    else if (strstr(from, "/" PC5_USB_HOME "/" PC5_INSTALL_FOLDER) && !pc5_usb_install_root(from, drive_root, sizeof(drive_root)))
        pc5_tf(line, sizeof(line), "INSTALL folder, %.20s", strrchr(drive_root, '/') + 1);
    else pc5_tf(line, sizeof(line), "USB drive %d", ui->sources[i].drive + 1);
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
                 q->folder.problem[0] ? TX(q->folder.problem) : T("No program found: it will be skipped"), (int)w - 92, 0);
        return;
    }
    pc5_size_text(q->folder.bytes, size, sizeof(size));
    pc5_tf(line, sizeof(line), "%s \xc2\xb7 %d-bit \xc2\xb7 %s \xc2\xb7 %s", q->folder.exes[q->exe].path,
             q->folder.exes[q->exe].pe.bits, pc5_api_name(pc5_folder_api(&q->folder, q->exe)), size);
    pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)x + 92, (int)y + 82, C_DIM, line, (int)w - 92, 0);
}

static void program_row(Pc5Ui *ui, Pc5Canvas *c, int i, float x, float y, float w, int chosen)
{
    const Pc5Exe *exe = &ui->folder.exes[i];
    char line[160], size[32];
    (void)chosen;
    pc5_size_text(exe->size, size, sizeof(size));
    int offered = 0;
    for (int k = 0; ui->program_for && k < ui->offered_count; k++) offered |= !strcmp(ui->offered[k].exe, exe->path);
    pc5_tf(line, sizeof(line), "%d-bit \xc2\xb7 %s \xc2\xb7 %s%s", exe->pe.bits,
             pc5_api_name(pc5_folder_api(&ui->folder, i)), size, i == ui->exe ? T(" \xc2\xb7 chosen") : "");
    if (offered) strncat(line, T(" \xc2\xb7 offered at Play"), sizeof(line) - strlen(line) - 1);
    pc5_text(c, &ui->fonts, PC5_FACE_LABEL, (int)x, (int)y + 40, C_TEXT, exe->path, (int)w, 0);
    pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)x, (int)y + 74, C_DIM, line, (int)w, 0);
}

/* One of S_PLAY_WHICH's programs: its name, then its file, bits and graphics. */
static void play_row(Pc5Ui *ui, Pc5Canvas *c, int i, float x, float y, float w, int chosen)
{
    const Pc5Offered *program = &ui->play[i];
    const char *file = strrchr(program->exe, '/');
    char name[PC5_PATH], line[PC5_PATH + 80];
    (void)chosen;
    pc5_copy(name, sizeof(name), file ? file + 1 : program->exe);
    char *dot = strrchr(name, '.');
    if (dot && dot != name) *dot = 0;
    char kind[96];
    pc5_tf(kind, sizeof(kind), "%d-bit \xc2\xb7 %s", program->bits, pc5_api_name(program->api));
    snprintf(line, sizeof(line), "%s \xc2\xb7 %s%s", program->exe, kind, i == 0 ? T(" \xc2\xb7 chosen") : "");
    pc5_text(c, &ui->fonts, PC5_FACE_LABEL, (int)x, (int)y + 40, C_TEXT, name, (int)w, 0);
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
        /* TRANSLATORS: a month, short, in the day a game's saves were kept (Kept 3 Oct 2026). */
        static const char *const months[] = { N_("Jan"), N_("Feb"), N_("Mar"), N_("Apr"), N_("May"), N_("Jun"),
                                               N_("Jul"), N_("Aug"), N_("Sep"), N_("Oct"), N_("Nov"), N_("Dec") };
        char about[320];
        pc5_tf(title, sizeof(title), "Saves of %s", saves->title);
        if (saves->kept > 0) {
            /* The day (UTC) from the days since 1970 (Howard Hinnant's civil_from_days; the
             * console's C library has no gmtime_r). */
            int64_t z = saves->kept / 86400 + 719468, era = z / 146097, doe = z - era * 146097;
            int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
            int64_t mp = (5 * doy + 2) / 153, d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
            int64_t y = yoe + era * 400 + (m <= 2);
            /* TRANSLATORS: the day, the month (Jan to Dec, translated on their own) and the year: put them in your order with %1$d, %2$s, %3$d. */
            pc5_tf(about, sizeof(about), "Kept %d %s %d; installing it again puts them back", (int)d,
                     T(months[(m - 1) % 12]), (int)y);
        } else
            pc5_tf(about, sizeof(about), "Kept when it was uninstalled; installing it again puts them back");
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
        if (ui->choice >= 0 && ui->choice < ui->search.count) pc5_tf(line, sizeof(line), "Cover %d of %d", at, choices);
        else pc5_tf(line, sizeof(line), "Made from the game's icon \xc2\xb7 %d of %d", at, choices);
        pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)(cx + cw / 2), (int)(cy + ch + 46), C_DIM, line, (int)cw + 40, 1);
    }
    float tx = cx + cw + 72, tw = x + w - 72 - tx;
    pc5_text_wrap(c, &ui->fonts, PC5_FACE_TITLE, (int)tx, (int)y + 150, (int)tw, 2, 64, C_TEXT, ui->folder.name, 0);
    float ry = y + 300;
    const char *labels[4];
    char values[4][300];
    labels[row] = T("Program");
    pc5_copy(values[row++], 300, exe ? exe->path : T("none found"));
    labels[row] = T("Runs as");
    if (exe) pc5_tf(values[row++], 300, "%d-bit \xc2\xb7 %s", exe->pe.bits, pc5_api_name(pc5_folder_api(&ui->folder, ui->exe)));
    else pc5_copy(values[row++], 300, "-");
    pc5_size_text(ui->folder.bytes, size, sizeof(size));
    labels[row] = T("Size");
    pc5_tf(values[row++], 300, ui->folder.files == 1 ? "%s in %u file" : "%s in %u files", size, (unsigned)ui->folder.files);
    labels[row] = T("Space");
    if (!pc5_free_bytes(ui->root, &free_bytes)) {
        uint64_t need = pc5_install_needs(&ui->folder);
        pc5_size_text(free_bytes > need ? free_bytes - ui->folder.bytes : 0, space, sizeof(space));
        pc5_tf(values[row++], 300, free_bytes >= need ? "%s free after installing" : "not enough: %s free", space);
    } else {
        pc5_copy(values[row++], 300, T("unknown"));
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
                      T("A .NET program: PC5 sets up what it needs the first time a .NET game starts."), 0);
        ny += 90;
    }
    pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, (int)tx, (int)ny, (int)tw, 4, 40, C_DIM,
                  T("The game is copied once and checked, to the console or, with a USB drive plugged in, to its PC5 "
                  "folder. Whether a game runs is only known by trying it."), 0);
    Hint list[5];
    int n = 0;
    list[n++] = (Hint){ accept_glyph(ui), ui->queue_review >= 0 ? T("Done") : T("Install") };
    if (choices > 1) list[n++] = (Hint){ G_L1R1, T("Other covers") };
    if (ui->folder.exe_count > 1) list[n++] = (Hint){ G_SQUARE, T("Change program") };
    if (ui->queue_review < 0) list[n++] = (Hint){ back_glyph(ui), T("Back") };
    hints(ui, c, (int)tx, (int)(y + h - 48), list, n);
}

static void sources_screen(Pc5Ui *ui, Pc5Canvas *c)
{
    if (ui->update_for) {
        int index = pc5_library_find(&ui->library, ui->launch_id);
        char title[PC5_NAME + 32];
        Hint list[] = { { accept_glyph(ui), T("Choose") }, { G_TRIANGLE, T("Look again") }, { back_glyph(ui), T("Back") } };
        pc5_tf(title, sizeof(title), "Update %s from USB", index >= 0 ? ui->library.games[index].title : T("the game"));
        list_panel(ui, c, title, ui->source_count, source_row, T("No game folders found"),
                   T("Put the newer copy's folder (the one with the game's .exe in it) at the top of a USB drive formatted "
                   "as exFAT, plug the drive into the PS5, then look again."),
                   list, 3, 96);
        return;
    }
    int ticked = ticked_count(ui);
    char install[40];
    pc5_tf(install, sizeof(install), "Install ticked (%d)", ticked);
    Hint list[5];
    int n = 0;
    if (ticked) list[n++] = (Hint){ G_OPTIONS, install };
    list[n++] = (Hint){ accept_glyph(ui), T("Install one") };
    list[n++] = (Hint){ G_SQUARE, T("Tick") };
    list[n++] = (Hint){ G_TRIANGLE, T("Look again") };
    list[n++] = (Hint){ back_glyph(ui), T("Back") };
    list_panel(ui, c, ui->install_only ? T("Install games from the INSTALL folder") : T("Install games from USB"),
               ui->source_count, source_row, T("No game folders found"),
               T("Put each game's folder (the one with its .exe in it) at the top of a USB drive formatted as exFAT, "
               "plug the drive into the PS5, then look again."),
               list, n, 96);
}

static void install_screen(Pc5Ui *ui, Pc5Canvas *c)
{
    const Pc5Install *in = &ui->install;
    float w = 1180, h = 520, x = (W - w) / 2, y = (H - h) / 2;
    char title[200], line[300], done[32], total[32], speed[32], left[48] = "";
    const char *step;
    int number;

    switch (in->phase) {
    case PC5_INSTALL_PREFIX: step = in->kept[0] ? T("Putting back its saves and settings") : T("Setting up the game's Windows");
        number = 1; break;
    case PC5_INSTALL_LIST:
    case PC5_INSTALL_COPY: step = in->update ? T("Copying the newer version") : T("Copying the game"); number = in->update ? 1 : 2; break;
    case PC5_INSTALL_VERIFY: step = T("Checking the copy against the USB drive"); number = in->update ? 2 : 3; break;
    case PC5_INSTALL_FINISH: step = in->update ? T("Putting the new files in place") : T("Finishing"); number = 3; break;
    default:
        if (in->tidying) { step = T("Finishing"); number = 3; break; }
        step = in->cancel ? T("Stopping: deleting what was copied") : T("Deleting what was copied"); number = 0; break;
    }
    dim(c, 150);
    panel(c, x, y, w, h);
    if (ui->queue_installing) {
        pc5_tf(line, sizeof(line), "Game %d of %d", ui->queue_at + 1, ui->queue_count);
        pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)x + 64, (int)y + 62, C_ACCENT, line, (int)w - 128, 0);
    }
    pc5_tf(title, sizeof(title), in->update ? "Updating %s" : "Installing %s", in->info.title);
    pc5_text(c, &ui->fonts, PC5_FACE_HEAD, (int)x + 64, (int)y + 116, C_TEXT, title, (int)w - 128, 0);
    if (number) pc5_tf(line, sizeof(line), "Step %d of 3 \xc2\xb7 %s", number, step);
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
                if (seconds < 60) pc5_tf(left, sizeof(left), " \xc2\xb7 under a minute left");
                else pc5_tf(left, sizeof(left), " \xc2\xb7 about %d min left", (int)(seconds / 60 + 0.5));
            }
            pc5_tf(line, sizeof(line), "%s of %s \xc2\xb7 %s/s%s", done, total, speed, left);
        } else {
            pc5_tf(line, sizeof(line), "%s of %s \xc2\xb7 measuring the speed", done, total);
        }
        pc5_text(c, &ui->fonts, PC5_FACE_BODY, (int)x + 64, (int)y + 306, C_DIM, line, (int)w - 128, 0);
        pc5_tf(line, sizeof(line), "%u of %u files", (unsigned)in->files_done, (unsigned)in->files_total);
        pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)x + 64, (int)y + 350, C_FAINT, line, (int)w - 128, 0);
    }
    if (in->phase < PC5_INSTALL_FINISH) {
        Hint list[] = { { back_glyph(ui), T("Stop") } };
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
    Hint list[] = { { accept_glyph(ui), T("OK") }, { G_TRIANGLE, T("Report on GitHub") } };
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
    char line[800], where[PC5_PATH * 2 + 80];
    dim(c, 170);
    panel(c, x, y, w, h);
    pc5_text(c, &ui->fonts, PC5_FACE_HEAD, (int)x + 80, (int)y + 100, C_TEXT, T("Report on GitHub"), 0, 0);
    int box = 560, qx = (int)(x + w) - box - 70, qy = (int)y + 70;
    qr_draw(c, &ui->report_qr, qx, qy, box);
    int tx = (int)x + 80, tw = qx - tx - 60;
    const char *game = ui->report_game[0] ? ui->report_game : T("The game");
    if (ui->report_qr.size)
        pc5_tf(line, sizeof(line), "1. Scan the code with your phone. GitHub's form for a new issue opens, filled in with %s, "
                 "PC5's version and what PC5 saw.", game);
    else
        pc5_tf(line, sizeof(line), "1. On your phone or computer, open %s and choose New issue.", "github.com/bornaradusin/PC5/issues");
    int at = (int)y + 190;
    at += 42 * pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, tx, at, tw, 4, 42, C_DIM, line, 0) + 24;
    pc5_tf(where, sizeof(where), "2. Copy %s off the console, from %s/logs, as you copy files to it.", pc5_report_file(&ui->report),
             ui->root);
    at += 42 * pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, tx, at, tw, 4, 42, C_DIM, where, 0) + 24;
    pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, tx, at, tw, 3, 42, C_DIM,
                  T("3. Say what happened, attach the zip, and submit. You need a GitHub account."), 0);
    Hint list[] = { { back_glyph(ui), T("Back") } };
    hints(ui, c, tx, (int)(y + h - 48), list, 1);
}

/* Game maintenance › Game compatibility list: the wiki's list as a QR code, and what it holds. */
/* Network › Web page, Triangle: the page's address as a QR code for a phone. */
static void web_qr_screen(Pc5Ui *ui, Pc5Canvas *c)
{
    float w = 1500, h = 700, x = (W - w) / 2, y = (H - h) / 2;
    char line[200];
    dim(c, 170);
    panel(c, x, y, w, h);
    pc5_text(c, &ui->fonts, PC5_FACE_HEAD, (int)x + 80, (int)y + 100, C_TEXT, T("PC5's web page"), 0, 0);
    int box = 560, qx = (int)(x + w) - box - 70, qy = (int)y + 70;
    qr_draw(c, &ui->web_qr, qx, qy, box);
    int tx = (int)x + 80, tw = qx - tx - 60, at = (int)y + 190;
    pc5_tf(line, sizeof(line), "Scan the code with your phone, or open %s in a browser on a PC on the same network.", ui->web_url);
    at += 42 * pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, tx, at, tw, 4, 42, C_DIM, line, 0) + 24;
    pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, tx, at, tw, 6, 42, C_DIM,
                  T("From there: send a game's folder to install, report a problem with a game (its log and settings go "
                  "with it), edit launch options and download logs. It works while PC5 is open."), 0);
    Hint list[] = { { accept_glyph(ui), T("Done") } };
    hints(ui, c, (int)x + 80, (int)(y + h - 60), list, 1);
}

static void phone_screen(Pc5Ui *ui, Pc5Canvas *c)
{
    float w = 1600, h = 800, x = (W - w) / 2, y = (H - h) / 2;
    Pc5PhoneStatus status;
    char line[256];
    static const char *const kinds[] = { N_("Cover"), N_("Background image"), N_("Tile picture"), N_("Library music"), N_("Background video"),
                                         N_("Library appearance from phone"), N_("Loading screen") };
    pc5_web_phone_status(&status);
    dim(c, 170); panel(c, x, y, w, h);
    int tx = (int)x + 60, tw = 760;
    pc5_text(c, &ui->fonts, PC5_FACE_HEAD, tx, (int)y + 100, C_TEXT, T("Receive from phone"), tw, 0);
    int index = pc5_library_find(&ui->library, ui->launch_id);
    pc5_text(c, &ui->fonts, PC5_FACE_BODY, tx, (int)y + 155, C_TEXT, index >= 0 ? ui->library.games[index].title : "", tw, 0);
    pc5_text(c, &ui->fonts, PC5_FACE_LABEL, tx, (int)y + 210, C_ACCENT, ui->phone_library ? T("Library appearance from phone") : T(kinds[ui->phone_kind]), tw, 0);
    pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, tx, (int)y + 270, tw, 4, 40, C_DIM,
                  T("Connect your phone and PS5 to the same network or phone hotspot. Scan this code and choose one file. Keep this screen open."), 0);
    pc5_text_wrap(c, &ui->fonts, PC5_FACE_SMALL, tx, (int)y + 455, tw, 3, 32, C_DIM, ui->web_url, 0);
    pc5_text_wrap(c, &ui->fonts, PC5_FACE_SMALL, tx, (int)y + 555, tw, 3, 32, C_DIM,
                  T("This link expires after 10 minutes or when you leave. Your previous selection stays until import succeeds."), 0);
    if (status.total) pc5_tf(line, sizeof(line), "Receiving: %u%%", (unsigned)(status.received * 100 / status.total));
    else pc5_copy(line, sizeof(line), T("Waiting for a file"));
    pc5_text(c, &ui->fonts, PC5_FACE_BODY, tx, (int)y + 685, C_TEXT, line, tw, 0);
    qr_draw(c, &ui->web_qr, (int)(x + w) - 650, (int)y + 90, 580);
    Hint list[] = { { back_glyph(ui), T("Cancel") } };
    hints(ui, c, tx, (int)(y + h - 40), list, 1);
}

#include "pc5_ui_enhancements_draw.h"

/* PC5 updating itself: its steps, the download's and the unpacking's progress, then done. */
static void upgrade_screen(Pc5Ui *ui, Pc5Canvas *c)
{
    const Pc5Upgrade *u = &ui->upgrade;
    int step = atomic_load(&u->step);
    uint64_t done = atomic_load(&u->done), total = atomic_load(&u->total);
    float w = 1180, h = 520, x = (W - w) / 2, y = (H - h) / 2;
    char title[200], line[600], have[32], all[32], speed[32], left[48] = "";
    const char *what;
    int number;

    dim(c, 150);
    panel(c, x, y, w, h);
    if (step == PC5_UPGRADE_DONE) {
        pc5_circle(c, x + 92, y + 108, 22, 0xff3fb27fu);
        pc5_line(c, x + 81, y + 109, x + 89, y + 117, 4, 0xff0b0d10u);
        pc5_line(c, x + 89, y + 117, x + 104, y + 99, 4, 0xff0b0d10u);
        pc5_tf(title, sizeof(title), "PC5 %s is installed", u->version);
        pc5_text(c, &ui->fonts, PC5_FACE_HEAD, (int)x + 140, (int)y + 122, C_TEXT, title, (int)w - 204, 0);
        pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, (int)x + 140, (int)y + 200, (int)w - 204, 5, 42, C_DIM,
                      T("PC5 restarts now with the new version. Your games, saves and settings are as they were."), 0);
        Hint list[] = { { accept_glyph(ui), T("Restart PC5") } };
        hints(ui, c, (int)x + 140, (int)(y + h - 48), list, 1);
        return;
    }
    switch (step) {
    case PC5_UPGRADE_DOWNLOADING: what = T("Downloading it from GitHub"); number = 1; break;
    case PC5_UPGRADE_UNPACKING: what = T("Unpacking it and checking every file"); number = 2; break;
    case PC5_UPGRADE_REPLACING: what = T("Putting the new files in place"); number = 3; break;
    case PC5_UPGRADE_TIDYING: what = T("Deleting the old files"); number = 3; break;
    default: what = T("Asking GitHub and finding PC5's folder"); number = 0; break;
    }
    int stopping = atomic_load(&u->cancel) && step <= PC5_UPGRADE_UNPACKING;
    if (stopping) { what = T("Stopping"); number = 0; }
    if (step >= PC5_UPGRADE_DOWNLOADING && u->version[0]) pc5_tf(title, sizeof(title), "Updating PC5 to %s", u->version);
    else pc5_copy(title, sizeof(title), T("Updating PC5"));
    pc5_text(c, &ui->fonts, PC5_FACE_HEAD, (int)x + 64, (int)y + 116, C_TEXT, title, (int)w - 128, 0);
    if (number) pc5_tf(line, sizeof(line), "Step %d of 3 \xc2\xb7 %s", number, what);
    else pc5_copy(line, sizeof(line), what);
    pc5_text(c, &ui->fonts, PC5_FACE_LABEL, (int)x + 64, (int)y + 186, C_TEXT, line, (int)w - 128, 0);
    if (step == PC5_UPGRADE_REPLACING && total) {
        progress_bar(c, x + 64, y + 236, w - 128, (double)done / (double)total, pc5_mix(C_ACCENT, C_TEXT, 60));
        pc5_tf(line, sizeof(line), "%llu of %llu files", (unsigned long long)done, (unsigned long long)total);
        pc5_text(c, &ui->fonts, PC5_FACE_BODY, (int)x + 64, (int)y + 306, C_DIM, line, (int)w - 128, 0);
    } else if ((step == PC5_UPGRADE_DOWNLOADING || step == PC5_UPGRADE_UNPACKING) && total && !stopping) {
        progress_bar(c, x + 64, y + 236, w - 128, (double)done / (double)total, pc5_mix(C_ACCENT, C_TEXT, 60));
        pc5_size_text(done, have, sizeof(have));
        pc5_size_text(total, all, sizeof(all));
        if (ui->speed > 1024) {
            pc5_size_text((uint64_t)ui->speed, speed, sizeof(speed));
            if (total > done) {
                double seconds = (double)(total - done) / ui->speed;
                if (seconds < 60) pc5_tf(left, sizeof(left), " \xc2\xb7 under a minute left");
                else pc5_tf(left, sizeof(left), " \xc2\xb7 about %d min left", (int)(seconds / 60 + 0.5));
            }
            pc5_tf(line, sizeof(line), "%s of %s \xc2\xb7 %s/s%s", have, all, speed, left);
        } else {
            pc5_tf(line, sizeof(line), "%s of %s \xc2\xb7 measuring the speed", have, all);
        }
        pc5_text(c, &ui->fonts, PC5_FACE_BODY, (int)x + 64, (int)y + 306, C_DIM, line, (int)w - 128, 0);
    } else {
        spinner(c, x + w - 100, y + 176, 22, ui->now, C_TEXT);
    }
    if (step >= PC5_UPGRADE_DOWNLOADING && u->folder[0]) {
        pc5_tf(line, sizeof(line), "Into %.280s", u->folder);
        pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)x + 64, (int)y + 350, C_FAINT, line, (int)w - 128, 0);
    }
    if (step <= PC5_UPGRADE_UNPACKING && !stopping) {
        Hint list[] = { { back_glyph(ui), T("Stop") } };
        hints(ui, c, (int)x + 64, (int)(y + h - 44), list, 1);
    } else if (step >= PC5_UPGRADE_REPLACING) {
        pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)x + 64, (int)(y + h - 44), C_DIM,
                 T("Don't turn the console off now. This takes a moment."), (int)w - 128, 0);
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
        Hint list[] = { { back_glyph(ui), T("Skip") } };
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
    char line[600];

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
    /* TRANSLATORS: which picture is shown: 3 of 12. */
    pc5_tf(line, sizeof(line), "%d of %d", ui->pick_at + 1, ui->pick_count);
    pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)(px + pw / 2), (int)(py + ph + 46), C_DIM, line, (int)pw, 1);

    float tx = px + pw + 64, tw = x + w - 72 - tx, ty = y + 150;
    ty += 64 * (float)pc5_text_wrap(c, &ui->fonts, PC5_FACE_TITLE, (int)tx, (int)ty, (int)tw, 2, 64, C_TEXT, game->title, 0);
    pc5_text(c, &ui->fonts, PC5_FACE_BODY, (int)tx, (int)ty, C_DIM, cover ? T("Its cover") : icon ? T("Its tile picture") : T("Its background"), (int)tw, 0);
    ty += 90;
    const char *what = "", *detail = "";
    if (ui->pick_fetching) what = "";
    else if (entry < 0) what = cover ? T("The cover it has now") : icon ? T("The tile picture it has now") : T("The background it has now");
    else if (entry < ui->pick->count) {
        what = cover ? T("A cover found online") : icon ? T("A square picture found online") : T("A background found online");
        detail = ui->pick->choices[entry].title;
    } else what = cover ? T("Made from the game's icon and name")
                  : icon ? T("Made from its cover") : T("None: a backdrop made from its cover");
    ty += 50 * (float)pc5_text_wrap(c, &ui->fonts, PC5_FACE_LABEL, (int)tx, (int)ty, (int)tw, 2, 50, C_TEXT, what, 0);
    if (detail[0]) ty += 42 * (float)pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, (int)tx, (int)ty, (int)tw, 2, 42, C_DIM, detail, 0);
    ty += 40;
    if (!ui->pick->count) {
        char none[400];
        if (ui->pick_query[0])
            pc5_tf(none, sizeof(none), "Nothing found for \xe2\x80\x9c%s\xe2\x80\x9d.", ui->pick_query);
        else if (icon && !ui->griddb_key[0])
            pc5_tf(none, sizeof(none), "Square pictures come from SteamGridDB: put your key in api_keys.txt (Covers on the wiki).");
        else
            pc5_tf(none, sizeof(none), "Nothing found for its name.");
        ty += 40 * (float)pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, (int)tx, (int)ty, (int)tw, 2, 40, C_WARN, none, 0);
    } else if (ui->pick_query[0]) {
        pc5_tf(line, sizeof(line), "Found for \xe2\x80\x9c%s\xe2\x80\x9d", ui->pick_query);
        ty += 40 * (float)pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, (int)tx, (int)ty, (int)tw, 2, 40, C_FAINT, line, 0);
    }
    Hint list[4];
    int n = 0;
    list[n++] = (Hint){ accept_glyph(ui), T("Use this") };
    if (ui->pick_count > 1) list[n++] = (Hint){ G_L1R1, cover ? T("Other covers") : icon ? T("Other pictures") : T("Other backgrounds") };
    if (ui->platform.keyboard_open) list[n++] = (Hint){ G_TRIANGLE, T("Search by name") };
    list[n++] = (Hint){ back_glyph(ui), T("Back") };
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
        snprintf(where, sizeof(where), "%.900s%s", place->path, place->read_only ? T(" \xc2\xb7 read-only") : "");
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
    pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)(x + w), (int)y + 46, C_DIM, item->dir ? T("Folder") : size, 0, 2);
}

/* The folder (or the places), as a list. */
static void files_list_screen(Pc5Ui *ui, Pc5Canvas *c)
{
    char title[PC5_PATH + 200], empty[PC5_ERROR] = "";
    int keep = ui->menu, ticks = files_ticks(ui);
    const char *name = T("Game files");
    if (ui->files_game) {
        int index = pc5_library_find(&ui->library, ui->launch_id);
        if (index >= 0) name = ui->library.games[index].title;
    }
    if (!ui->files_path[0]) {
        pc5_copy(title, sizeof(title), T("File manager"));
    } else if (ui->files_game) {
        size_t n = strlen(ui->files_top);
        if (!strncmp(ui->files_path, ui->files_top, n) && ui->files_path[n] == '/')
            snprintf(title, sizeof(title), "%s \xe2\x80\xba %s", name, ui->files_path + n + 1);
        else
            snprintf(title, sizeof(title), "%s", name);
    } else {
        snprintf(title, sizeof(title), "%s", ui->files_path);
    }
    if (ui->files_path[0] && !files_writable(ui, ui->files_path)) pc5_format(title + strlen(title), sizeof(title) - strlen(title), "%s", T(" \xc2\xb7 read-only"));
    if (ticks) pc5_tf(title + strlen(title), sizeof(title) - strlen(title), " \xc2\xb7 %d ticked", ticks);
    if (ui->files.error) snprintf(empty, sizeof(empty), "%s.", strerror(ui->files.error));
    else if (files_writable(ui, ui->files_path)) pc5_copy(empty, sizeof(empty), T("\xe2\x96\xb3 Paste or New folder puts something in it."));

    int folder = ui->files_path[0] && ui->files_at < ui->files.count && ui->files.items[ui->files_at].dir;
    Hint places[] = { { accept_glyph(ui), T("Open") }, { back_glyph(ui), T("Back") } };
    /* On a file ✕ shows its actions too, as △ does: said once. */
    Hint list[] = { { accept_glyph(ui), T("Open") }, { G_SQUARE, T("Tick") }, { G_TRIANGLE, T("Actions") }, { back_glyph(ui), T("Back") } };
    Hint music[] = { { accept_glyph(ui), T("Choose") }, { back_glyph(ui), T("Back") } };
    if (ui->files_look) {
        if (ui->files_look == 2) {
            if (!ui->files_path[0]) pc5_copy(title, sizeof(title), T("Choose backup destination"));
            pc5_copy(empty, sizeof(empty), T("Open a folder, then press OPTIONS to save the backup here."));
        } else pc5_copy(title, sizeof(title), T("Choose an appearance backup"));
    } else if (ui->files_music) {
        pc5_copy(title, sizeof(title), T("Choose a music file"));
        pc5_copy(empty, sizeof(empty), T("Choose an MP3 or a 16-bit PCM WAV file."));
    }
    if (ui->files_video) {
        pc5_copy(title, sizeof(title), T("Choose a background video"));
        pc5_copy(empty, sizeof(empty), T("H.264 up to 1080p/60 and 1 GB; MP4 recommended. Playback is silent."));
    }
    ui->menu = ui->files_at;
    ui->list_top = ui->files_list_top;
    Hint destination[] = { { accept_glyph(ui), T("Open folder") }, { G_OPTIONS, T("Save here") }, { back_glyph(ui), T("Back") } };
    list_panel(ui, c, title, files_count(ui), files_row, ui->files.error ? T("PC5 can't open this folder") : T("This folder is empty"),
               empty, ui->files_look == 2 ? destination : (ui->files_look || ui->files_music || ui->files_video) ? music : ui->files_path[0] ? list + !folder : places,
               ui->files_look == 2 ? 3 : (ui->files_look || ui->files_music || ui->files_video) ? 2 : ui->files_path[0] ? 4 - !folder : 2, ui->files_path[0] ? 80 : 104);
    ui->files_list_top = ui->list_top;
    ui->menu = keep;
}

static void files_action_text(const Pc5Ui *ui, FilesAction action, const char **label, char *about, size_t size, char *value,
                              size_t value_size)
{
    value[0] = about[0] = 0;
    switch (action) {
    case FA_COPY: *label = T("Copy"); pc5_copy(about, size, T("Then Paste puts a copy in another folder")); break;
    case FA_CUT: *label = T("Cut"); pc5_copy(about, size, T("Then Paste moves it to another folder")); break;
    case FA_PASTE: {
        const char *slash = strrchr(ui->clip_from, '/');
        *label = T("Paste");
        pc5_tf(about, size, ui->clip_cut ? (ui->clip.count == 1 ? "Moves %d item from %.120s" : "Moves %d items from %.120s")
                                         : (ui->clip.count == 1 ? "Copies %d item from %.120s" : "Copies %d items from %.120s"),
               ui->clip.count, slash && slash[1] ? slash + 1 : ui->clip_from);
        snprintf(value, value_size, "%d", ui->clip.count);
        break;
    }
    case FA_RENAME: *label = T("Rename"); pc5_copy(about, size, T("Type its new name")); break;
    case FA_NEW_FOLDER: *label = T("New folder"); pc5_copy(about, size, T("An empty folder in this one")); break;
    case FA_DELETE: *label = T("Delete"); pc5_copy(about, size, T("Deleted for good: there is no undo")); break;
    case FA_TICK_ALL:
        *label = files_ticks(ui) == ui->files.count ? T("Untick all") : T("Tick all");
        pc5_copy(about, size, T("Every item in this folder"));
        break;
    case FA_EDIT:
        if (files_writable(ui, ui->files_path)) {
            *label = T("Edit");
            pc5_copy(about, size, T("A text file (.ini, .cfg, .txt and the like), changed a line at a time"));
        } else {
            *label = T("View");
            pc5_copy(about, size, T("A text file, shown: nothing here can be changed"));
        }
        break;
    }
}

static void files_menu_screen(Pc5Ui *ui, Pc5Canvas *c)
{
    FilesAction rows[FILES_ACTIONS];
    int count = files_actions(ui, rows), ticks = files_ticks(ui);
    char title[PC5_FILES_NAME + 64], about[400], value[32];
    const char *label = "";
    float extra;
    if (ticks) pc5_tf(title, sizeof(title), "%d items", ticks);
    else if (ui->files_at < ui->files.count) pc5_copy(title, sizeof(title), ui->files.items[ui->files_at].name);
    else pc5_copy(title, sizeof(title), T("This folder"));
    float x = drawer(ui, c, title);
    /* where, in small letters under the drawer's title */
    pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)x + 64, 210, C_DIM,
             files_writable(ui, ui->files_path) ? ui->files_path : T("Read-only: PC5 changes nothing here"), 760 - 128, 0);
    if (ui->menu < count) files_action_text(ui, rows[ui->menu], &label, about, sizeof(about), value, sizeof(value));
    else about[0] = 0;
    float pitch = rows_pitch(ui, 240, count, about, &extra);
    for (int i = 0; i < count; i++) {
        files_action_text(ui, rows[i], &label, about, sizeof(about), value, sizeof(value));
        drawer_row(ui, c, x, 240 + pitch * i + (i > ui->menu ? extra : 0), pitch + (i == ui->menu ? extra : 0), i == ui->menu,
                   label, about, value);
    }
    Hint list[] = { { accept_glyph(ui), T("Choose") }, { back_glyph(ui), T("Back") } };
    hints(ui, c, (int)x + 64, 1012, list, 2);
}

/* A line of the file being changed: its number, then as much of it as fits. */
static void edit_row(Pc5Ui *ui, Pc5Canvas *c, int i, float x, float y, float w, int chosen)
{
    char number[16], text[600];
    size_t len, n = 0;
    const char *line = pc5_textdoc_line(&ui->edit, i, &len);
    (void)chosen;
    for (size_t at = 0; at < len && n + 1 < sizeof(text); at++) {
        unsigned char b = (unsigned char)line[at];
        if (b >= 0x80 && n + 4 >= sizeof(text)) break;   /* not half a character */
        text[n++] = b < 0x20 ? ' ' : (char)b;            /* a tab as a space */
    }
    while (n && n < len && ((unsigned char)text[n] & 0xc0) == 0x80) n--;
    text[n] = 0;
    snprintf(number, sizeof(number), "%d", i + 1);
    pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)x + 72, (int)y + 34, C_FAINT, number, 0, 2);
    pc5_text(c, &ui->fonts, PC5_FACE_BODY, (int)x + 96, (int)y + 34, C_TEXT, text, (int)w - 96, 0);
}

static void files_edit_screen(Pc5Ui *ui, Pc5Canvas *c)
{
    char title[PC5_FILES_NAME + 120];
    const char *slash = strrchr(ui->edit_path, '/');
    int keep = ui->menu;
    pc5_copy(title, sizeof(title) - 40, slash ? slash + 1 : ui->edit_path);
    if (ui->edit_read_only) pc5_format(title + strlen(title), sizeof(title) - strlen(title), "%s", T(" \xc2\xb7 read-only"));
    else if (ui->edit.changed) pc5_format(title + strlen(title), sizeof(title) - strlen(title), "%s", T(" \xc2\xb7 not saved"));
    Hint shown[] = { { G_L1R1, T("Page") }, { back_glyph(ui), T("Back") } };
    Hint list[] = { { accept_glyph(ui), ui->edit.count ? T("Change") : T("Add a line") }, { G_TRIANGLE, T("Actions") },
                    { G_L1R1, T("Page") }, { back_glyph(ui), T("Back") } };
    ui->menu = ui->edit_at;
    ui->list_top = ui->edit_top;
    list_panel(ui, c, title, ui->edit.count, edit_row, T("This file is empty"),
               ui->edit_read_only ? T("There's nothing in it.") : T("\xe2\x9c\x95 puts a first line in it."),
               ui->edit_read_only ? shown : list, ui->edit_read_only ? 2 : 4, 56);
    ui->edit_top = ui->list_top;
    ui->menu = keep;
}

static void edit_action_text(const Pc5Ui *ui, EditAction action, const char **label, const char **about)
{
    switch (action) {
    case EA_ADD_BELOW: *label = T("Add a line below"); *about = T("Typed with the keyboard, under the line chosen"); break;
    case EA_ADD_ABOVE: *label = T("Add a line above"); *about = T("Typed with the keyboard, over the line chosen"); break;
    case EA_DELETE: *label = T("Delete line"); *about = T("The line chosen goes; nothing is saved until Save"); break;
    case EA_SAVE:
        *label = T("Save");
        *about = ui->edit.encoding == PC5_TEXT_UTF16LE ? T("Written back as it was: UTF-16, with its line ends")
                 : ui->edit.encoding == PC5_TEXT_LATIN1 ? T("Written back as it was: one byte a letter, with its line ends")
                                                         : T("Written back as it was: UTF-8, with its line ends");
        break;
    }
}

static void files_edit_menu_screen(Pc5Ui *ui, Pc5Canvas *c)
{
    EditAction rows[EDIT_ACTIONS];
    int count = files_edit_actions(ui, rows);
    char title[64];
    const char *label = "", *about = "";
    float extra;
    if (ui->edit.count) pc5_tf(title, sizeof(title), "Line %d", ui->edit_at + 1);
    else pc5_copy(title, sizeof(title), T("This file"));
    float x = drawer(ui, c, title);
    pc5_text(c, &ui->fonts, PC5_FACE_SMALL, (int)x + 64, 210, C_DIM, ui->edit_path, 760 - 128, 0);
    if (ui->menu < count) edit_action_text(ui, rows[ui->menu], &label, &about);
    float pitch = rows_pitch(ui, 240, count, about, &extra);
    for (int i = 0; i < count; i++) {
        edit_action_text(ui, rows[i], &label, &about);
        drawer_row(ui, c, x, 240 + pitch * i + (i > ui->menu ? extra : 0), pitch + (i == ui->menu ? extra : 0), i == ui->menu,
                   label, about, "");
    }
    Hint list[] = { { accept_glyph(ui), T("Choose") }, { back_glyph(ui), T("Back") } };
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
    snprintf(title, sizeof(title), "%s", job->stop ? T("Stopping")
                                    : job->op == PC5_FILES_DELETE ? T("Deleting")
                                    : job->op == PC5_FILES_MOVE ? T("Moving") : T("Copying"));
    pc5_text(c, &ui->fonts, PC5_FACE_HEAD, (int)x + 64, (int)y + 116, C_TEXT, title, (int)w - 128, 0);
    pc5_text(c, &ui->fonts, PC5_FACE_LABEL, (int)x + 64, (int)y + 186, C_TEXT, job->current[0] ? job->current : T("Getting ready"),
             (int)w - 128, 0);
    double fraction = job->total_bytes ? (double)job->done_bytes / (double)job->total_bytes : (double)job->done_items / items;
    progress_bar(c, x + 64, y + 236, w - 128, fraction, pc5_mix(C_ACCENT, C_TEXT, 60));
    if (job->total_bytes) {
        pc5_size_text(job->done_bytes, have, sizeof(have));
        pc5_size_text(job->total_bytes, all, sizeof(all));
        pc5_tf(line, sizeof(line), "%s of %s", have, all);
    } else {
        pc5_tf(line, sizeof(line), "%u of %u", (unsigned)job->done_items, (unsigned)job->total_items);
    }
    pc5_text(c, &ui->fonts, PC5_FACE_BODY, (int)x + 64, (int)y + 306, C_DIM, line, (int)w - 128, 0);
    if (!job->stop) {
        Hint list[] = { { back_glyph(ui), T("Stop") } };
        hints(ui, c, (int)x + 64, (int)(y + h - 44), list, 1);
    }
}

static void files_draw(Pc5Ui *ui, Pc5Canvas *c)
{
    char title[PC5_FILES_NAME + 128], body[800], size[32], files[64], folders[64];
    if (ui->screen == S_FILES_EDIT || ui->screen == S_FILES_EDIT_MENU || ui->screen == S_FILES_EDIT_TYPING ||
        ui->screen == S_FILES_EDIT_LEAVE) {
        const char *slash = strrchr(ui->edit_path, '/');
        files_edit_screen(ui, c);
        if (ui->screen == S_FILES_EDIT_MENU) files_edit_menu_screen(ui, c);
        if (ui->screen == S_FILES_EDIT_LEAVE) {
            pc5_tf(title, sizeof(title), "Save the changes to %.200s?", slash ? slash + 1 : ui->edit_path);
            confirm3_dialog(ui, c, title, T("Don't save leaves the file as it was before."), T("Cancel"), T("Save"),
                            T("Don't save"), NULL);
        }
        return;
    }
    files_list_screen(ui, c);
    switch (ui->screen) {
    case S_FILES_MENU: files_menu_screen(ui, c); break;
    case S_FILES_CONFLICT: {
        const char *one = ui->clip.count ? strrchr(ui->clip.list[0], '/') : NULL;
        if (ui->clip.count == 1) pc5_tf(title, sizeof(title), "This folder has %.200s already", one ? one + 1 : T("it"));
        else pc5_tf(title, sizeof(title), "This folder has %d of the %d items already", ui->files_conflicts, ui->clip.count);
        confirm3_dialog(ui, c, title,
                        T("Skip leaves those as they are. Keep both gives the new ones a name like \xe2\x80\x9cname (2)\xe2\x80\x9d. "
                          "Replace overwrites files and puts folders' contents together."),
                        T("Skip"), T("Keep both"), T("Replace"), NULL);
        break;
    }
    case S_FILES_DELETE: {
        int ticks = files_ticks(ui);
        if (ticks > 1) pc5_tf(title, sizeof(title), "Delete %d items?", ticks);
        else if (ticks == 1 || ui->files_at < ui->files.count) {
            int at = ui->files_at;
            for (int i = 0; ticks && i < ui->files.count; i++)
                if (ui->files_ticked[i]) at = i;
            pc5_tf(title, sizeof(title), "Delete %.200s?", at < ui->files.count ? ui->files.items[at].name : T("it"));
        } else pc5_copy(title, sizeof(title), T("Delete?"));
        pc5_size_text(ui->files_size.bytes, size, sizeof(size));
        pc5_tf(files, sizeof(files), ui->files_size.files == 1 ? "%u file" : "%u files", (unsigned)ui->files_size.files);
        pc5_tf(folders, sizeof(folders), ui->files_size.folders == 1 ? "%u folder" : "%u folders",
               (unsigned)ui->files_size.folders);
        /* TRANSLATORS: the first %s is a count of files ("3 files"), the second of folders, the third a size ("2.1 GB"). */
        pc5_tf(body, sizeof(body),
               ui->files_size.partial ? "At least %s and %s, %s. They're deleted for good: there is no backup and no undo."
                                      : "%s and %s, %s. They're deleted for good: there is no backup and no undo.",
               files, folders, size);
        confirm_dialog(ui, c, title, body, T("Cancel"), T("Delete"), 1, NULL);
        break;
    }
    case S_FILES_WORKING: files_working_screen(ui, c); break;
    default: break;
    }
}

/* As PC5 opens: its logo comes up out of the dark with a light passing over it and a line of
 * light under it, then the screen under it fades in (BOOT_SHOW_MS, above). */
static void boot_overlay(Pc5Ui *ui, Pc5Canvas *c)
{
    if (!ui->config.boot) return;
    if (!ui->boot_at) ui->boot_at = ui->now ? ui->now : 1;
    uint64_t t = ui->now - ui->boot_at;
    if (t >= BOOT_SHOW_MS + BOOT_FADE_MS) return;
    int cover = t < BOOT_SHOW_MS ? 256 : (int)(256 * (BOOT_SHOW_MS + BOOT_FADE_MS - t) / BOOT_FADE_MS);
    pc5_round_rect(c, 0, 0, W, H, 0, (uint32_t)(cover > 255 ? 255 : cover) << 24 | 0x05060au);
    if (ui->art_boot.px) {
        /* PC5's own boot picture (its logo in it): faded in while it draws back a little, and
         * a light passing over the logo */
        float in = t < 900 ? (float)t / 900 : 1, z = 1.05f - 0.05f * in * (2 - in);
        pc5_draw_image(c, &ui->art_boot, (W - W * z) / 2, (H - H * z) / 2, W * z, H * z,
                       cover * (t < 600 ? (int)t : 600) / 600);
        if (t > 450 && t < 1450 && ui->art_sweep.px) {
            float p = (float)(t - 450) / 1000, sx = 560 + 800 * p;
            pc5_draw_image(c, &ui->art_sweep, sx - 260, 510 - 130, 520, 260, (int)(cover * sinf(3.14159f * p)));
        }
        ui->dirty = 1;
        return;
    }
    float in = t < 700 ? (float)t / 700 : 1, scale = 0.94f + 0.06f * in * (2 - in);
    int alpha = (int)(256 * in) * cover / 256;
    float lh = 120 * scale, lw = ui->art_logo.px ? (float)ui->art_logo.w * lh / (float)ui->art_logo.h : 0, x = (W - lw) / 2, y = 470 - lh / 2;
    /* the glow behind it, breathing once */
    float glow = in * (0.6f + 0.4f * sinf((float)t / 600.0f));
    pc5_shadow(c, x + lw * 0.1f, y + lh * 0.2f, lw * 0.8f, lh * 0.6f, 40, 120, (uint32_t)(glow * 110 * cover / 256) << 24 | 0x4d7cffu);
    if (ui->art_logo.px) pc5_draw_image(c, &ui->art_logo, x, y, lw, lh, alpha);
    else {
        int w = pc5_text_width(&ui->fonts, PC5_FACE_HERO, "PC5");
        pc5_text(c, &ui->fonts, PC5_FACE_HERO, 960 - w / 2, 505, (uint32_t)(alpha > 255 ? 255 : alpha) << 24 | 0xffffffu, "PC5", 0, 0);
    }
    /* a light passing over it */
    if (t > 350 && t < 1350 && lw > 0) {
        float p = (float)(t - 350) / 1000, sx = x - 80 + (lw + 160) * p;
        if (ui->art_sweep.px) pc5_draw_image(c, &ui->art_sweep, sx - lh, y - lh / 2, 2 * lh, 2 * lh, cover);
        else pc5_shadow(c, sx - 10, y, 20, lh, 10, 40, (uint32_t)(90 * cover / 256) << 24 | 0xffffffu);
    }
    /* the line of light under it, opening from the middle */
    if (t > 250) {
        float p = t > 1150 ? 1 : (float)(t - 250) / 900, half = 330 * p * (2 - p);
        pc5_shadow(c, 960 - half, y + lh + 34, 2 * half, 3, 1.5f, 16, (uint32_t)(150 * cover / 256) << 24 | 0x4d7cffu);
        pc5_round_rect(c, 960 - half, y + lh + 34, 2 * half, 3, 1.5f, (uint32_t)(230 * cover / 256) << 24 | 0xcfe0ffu);
    }
    ui->dirty = 1;
}

void pc5_ui_draw(Pc5Ui *ui, Pc5Canvas *c)
{
    char line[300];

    switch (ui->screen) {
    case S_START:
        start_screen(ui, c, ui->remove.top ? T("Finishing an install or uninstall that was cut short") : NULL, 0);
        break;
    case S_COVERS:
        pc5_tf(line, sizeof(line), "Getting covers \xc2\xb7 %d of %d", ui->cover_at + 1, ui->cover_count);
        start_screen(ui, c, line, 1);
        break;
    case S_UPDATE:
        start_screen(ui, c, T("Checking for a newer PC5"), 0);
        break;
    case S_COMPATIBILITY:
    case S_COMPAT_SEARCH:
    case S_COMPAT_REPORT:
    case S_COMPAT_SHARE:
        backdrop(ui, c);
        top_bar(ui, c);
        break;
    default:
        library_screen(ui, c);
        break;
    }
    switch (ui->screen) {
    case S_SETTINGS:
    case S_SETTINGS_TYPING: settings_screen(ui, c); break;
    case S_CHECKING: settings_screen(ui, c); busy_dialog(ui, c, T("Checking for a newer PC5"), T("Asking GitHub"), 0); break;
    case S_UPGRADE_ASK: {
        char title[120], body[400], how[200];
        update_how(ui, how, sizeof(how));
        pc5_tf(title, sizeof(title), "PC5 %s is available", ui->update.last.version);
        pc5_tf(body, sizeof(body), "%s, installs it and restarts. Your games, saves and settings stay. "
                 "Later: Settings \xe2\x80\xba System.", how);
        confirm_dialog(ui, c, title, body, T("Update now"), T("Later"), 0, NULL);
        break;
    }
    case S_UPGRADING: upgrade_screen(ui, c); break;
    case S_GAME_MENU: game_menu_screen(ui, c, ui->menu); break;
    case S_BUTTONS: buttons_screen(ui, c); break;
    case S_KEYS: buttons_screen(ui, c); keys_dialog(ui, c); break;
    case S_GRAPHICS_API: game_menu_screen(ui, c, ui->menu); graphics_dialog(ui, c); break;
    case S_SETTING_ABOUT: game_menu_screen(ui, c, ui->menu); setting_about_screen(ui, c); break;
    case S_GAME_READING: {
        int index = pc5_library_find(&ui->library, ui->launch_id);
        pc5_tf(line, sizeof(line), "Reading %s", index >= 0 ? ui->library.games[index].title : T("the game"));
        busy_dialog(ui, c, line, T("Finding its programs"), 0);
        break;
    }
    case S_QUEUE_READ: {
        const QueueGame *q = ui->queue && ui->queue_at < ui->queue_count ? &ui->queue[ui->queue_at] : NULL;
        pc5_tf(line, sizeof(line), "Getting ready \xc2\xb7 %d of %d", ui->queue_at + 1 > ui->queue_count ? ui->queue_count : ui->queue_at + 1,
                 ui->queue_count);
        char detail[200];
        snprintf(detail, sizeof(detail), "%s: %s", q ? q->source.name : "",
                 ui->queue_phase == 0 ? T("reading its files") : T("looking for its cover"));
        busy_dialog(ui, c, line, detail, 1);
        break;
    }
    case S_QUEUE: {
        char title[120], size[32];
        uint64_t bytes = 0;
        for (int i = 0; i < ui->queue_count; i++) bytes += ui->queue[i].folder.bytes;
        pc5_size_text(bytes, size, sizeof(size));
        pc5_tf(title, sizeof(title), ui->queue_count == 1 ? "Install %d game \xc2\xb7 %s" : "Install %d games \xc2\xb7 %s",
               ui->queue_count, size);
        Hint list[] = { { G_OPTIONS, T("Install all") }, { accept_glyph(ui), T("Review") }, { back_glyph(ui), T("Back") } };
        list_panel(ui, c, title, ui->queue_count, queue_row, "", "", list, 3, 116);
        break;
    }
    case S_ABOUT: about_screen(ui, c); break;
    case S_COMPATIBILITY: compatibility_screen(ui, c); break;
    case S_COMPAT_REPORT: compatibility_report_screen(ui, c); break;
    case S_COMPAT_SHARE: compatibility_report_screen(ui, c); break;
    case S_COMPAT_SEARCH: compatibility_screen(ui, c); break;
    case S_MEDIA_OPTIONS: media_options_screen(ui, c); break;
    case S_MEDIA_DELETE:
        game_menu_screen(ui, c, ui->menu);
        confirm_dialog(ui, c, ui->media_delete_video ? T("Remove background video?") : T("Remove library music?"),
                       ui->media_delete_video ? T("Delete this game's imported video and return to the background image? The original source file is kept.")
                                              : T("Delete this game's imported music? The original source file is kept."),
                       T("Cancel"), T("Delete"), 1, NULL);
        break;
    case S_LOOK_CONFIRM:
        confirm_dialog(ui, c, T("Restore this appearance?"), T("This replaces this game's pictures, music, video and media settings. Game files and saves are kept."), T("Cancel"), T("Restore"), 1, NULL); break;
    case S_LOOK_WORK:
        busy_dialog(ui, c, ui->look.restore ? T("Restoring appearance") : T("Exporting appearance"), T("Keep the drive connected. Back cancels."), 1); break;
    case S_WEB_QR: web_qr_screen(ui, c); break;
    case S_PHONE_QR: phone_screen(ui, c); break;
    case S_SCAN: busy_dialog(ui, c, T("Looking at the USB drives"), NULL, 0); break;
    case S_SOURCES: sources_screen(ui, c); break;
    case S_READING:
        pc5_tf(line, sizeof(line), "Reading %s", ui->sources[ui->menu].name);
        busy_dialog(ui, c, line, T("Counting its files and finding its program"), 0);
        break;
    case S_GAME_UPDATE_ASK: {
        int index = pc5_library_find(&ui->library, ui->launch_id);
        const char *name = index >= 0 ? ui->library.games[index].title : T("the game");
        char title[300], body[500], size[32];
        pc5_size_text(ui->folder.bytes, size, sizeof(size));
        pc5_tf(title, sizeof(title), "Update %s?", name);
        pc5_tf(body, sizeof(body), "PC5 copies %s (%s) from the USB drive over the game's folder: the files it has replace "
                 "the game's, the others stay. Its saves, settings and options stay too.", ui->folder.name, size);
        confirm_dialog(ui, c, title, body, T("Cancel"), T("Update"), 0,
                       index >= 0 && ui->covers[index].state ? &ui->covers[index].picture : NULL);
        break;
    }
    case S_INSTALL_WHERE: {
        char title[300], body[600], console_free[32] = "?", usb_free[32] = "?";
        uint64_t room = 0;
        const char *drive = strrchr(ui->where_usb, '/');
        if (!pc5_free_bytes(ui->root, &room)) pc5_size_text(room, console_free, sizeof(console_free));
        if (!pc5_free_bytes(ui->where_usb, &room)) pc5_size_text(room, usb_free, sizeof(usb_free));
        if (ui->where_for) pc5_tf(title, sizeof(title), "Where should the %d games go?", ticked_count(ui));
        else pc5_tf(title, sizeof(title), "Where should %s go?", ui->folder.name);
        /* extended storage (/mnt/ext0, the M.2 drive) is named as such */
        int extended = !strncmp(ui->where_usb, "/mnt/ext", 8);
        if (extended)
            pc5_tf(body, sizeof(body),
                     "The console has %s free. Extended storage (%.40s) has %s free: a game there plays from its PC5 "
                     "folder. Its saves stay on the console either way.",
                     console_free, drive ? drive + 1 : ui->where_usb, usb_free);
        else
            pc5_tf(body, sizeof(body),
                     "The console has %s free. The USB drive (%.40s) has %s free: a game there plays from its PC5 folder, so "
                     "keep the drive plugged into a fast port. Its saves stay on the console either way.",
                     console_free, drive ? drive + 1 : ui->where_usb, usb_free);
        /* With more than one drive, the second button names the drive left and right go through. */
        char drive_label[64];
        if (ui->where_count > 1) {
            char more[200];
            size_t n = strlen(body);
            drive_name(ui->where_usb, drive_label, sizeof(drive_label));
            pc5_tf(more, sizeof(more), "Left and right choose among the %d drives a game can go on.", ui->where_count);
            snprintf(body + n, sizeof(body) - n, " %s", more);
        } else pc5_copy(drive_label, sizeof(drive_label), extended ? T("Extended storage") : T("USB drive"));
        if (ui->where_for) sources_screen(ui, c);
        else review_screen(ui, c);
        confirm_dialog(ui, c, title, body, T("Console"), drive_label, 0, NULL);
        break;
    }
    case S_SAVES_ASK: {
        char title[300];
        pc5_tf(title, sizeof(title), "Put back the saves of %s?", ui->folder.name);
        review_screen(ui, c);
        confirm_dialog(ui, c, title, T("They were kept when it was uninstalled: its saves, settings and options come back "
                       "with it. Start fresh: they stay kept, for later."), T("Start fresh"), T("Put them back"), 0, NULL);
        break;
    }
    case S_SEARCH:
        busy_dialog(ui, c, T("Looking for the cover"), T("On Steam, GOG and others"), 1);
        break;
    case S_PICTURE_SEARCH: {
        if (ui->pick_query[0])
            /* TRANSLATORS: the name typed, in quotation marks (use your language's), then where it is looked for. */
            pc5_tf(line, sizeof(line), "\xe2\x80\x9c%s\xe2\x80\x9d, %s", ui->pick_query, T("on Steam, GOG and others"));
        else pc5_copy(line, sizeof(line), T("On Steam, GOG and others"));
        busy_dialog(ui, c, ui->pick_kind == PICK_COVER ? T("Looking for covers")
                           : ui->pick_kind == PICK_ICON ? T("Looking for tile pictures") : T("Looking for backgrounds"), line, 1);
        break;
    }
    case S_PICTURE:
    case S_PICTURE_TYPING: picture_screen(ui, c); break;
    case S_REVIEW: review_screen(ui, c); break;
    case S_PROGRAMS: {
        Hint list[] = { { accept_glyph(ui), T("Choose") }, { G_SQUARE, T("Offer at Play") }, { back_glyph(ui), T("Back") } };
        if (ui->program_for) {
            game_menu_screen(ui, c, game_row_find(&ui->controls, ui_game_kind(ui), GR_PROGRAM, NULL));
            list_panel(ui, c, T("Which program is the game?"), ui->folder.exe_count, program_row, "", "", list, 3, 104);
        } else {
            list[1] = list[2];
            list_panel(ui, c, T("Which program is the game?"), ui->folder.exe_count, program_row, "", "", list, 2, 104);
        }
        break;
    }
    case S_PLAY_WHICH: {
        Hint list[] = { { accept_glyph(ui), T("Play") }, { back_glyph(ui), T("Back") } };
        if ((Screen)ui->play_back == S_GAME_MENU)
            game_menu_screen(ui, c, game_row_find(&ui->controls, ui_game_kind(ui), GR_PLAY, NULL));
        else
            library_screen(ui, c);
        list_panel(ui, c, T("Which program?"), ui->play_count, play_row, "", "", list, 2, 104);
        break;
    }
    case S_INSTALL: install_screen(ui, c); break;
    case S_STOP:
        install_screen(ui, c);
        if (ui->install.update)
            confirm_dialog(ui, c, T("Stop updating?"), T("The copy made so far is deleted and the game stays as it was. The USB "
                           "drive isn't changed."), T("Keep updating"), T("Stop"), 1, NULL);
        else if (ui->queue_installing)
            confirm_dialog(ui, c, T("Stop installing?"),
                           T("What this game copied so far is deleted; games already installed stay. The USB drive isn't "
                           "changed. Press the back button to keep installing."),
                           T("Stop this game"), T("Stop all"), 1, NULL);
        else
            confirm_dialog(ui, c, T("Stop installing?"), T("Everything copied so far is deleted. The USB drive isn't changed."),
                           T("Keep installing"), T("Stop"), 1, NULL);
        break;
    case S_UNINSTALL_LIST: {
        Hint list[] = { { accept_glyph(ui), ui->menu < ui->library.count ? T("Uninstall") : T("Delete") }, { back_glyph(ui), T("Back") } };
        list_panel(ui, c, T("Uninstall game"), ui->library.count + ui->saves_count, game_row, T("There are no games to uninstall"),
                   "", list, 2, 112);
        break;
    }
    case S_SAVES_CONFIRM: {
        Hint list[] = { { accept_glyph(ui), T("Delete") }, { back_glyph(ui), T("Back") } };
        int at = ui->menu - ui->library.count;
        list_panel(ui, c, T("Uninstall game"), ui->library.count + ui->saves_count, game_row, "", "", list, 2, 112);
        pc5_tf(line, sizeof(line), "Delete the saves of %s?", at >= 0 && at < ui->saves_count ? ui->saves[at].title : T("the game"));
        confirm_dialog(ui, c, line, T("Its saves and settings, kept when it was uninstalled, are deleted. There is no backup "
                       "and no undo."), T("Cancel"), T("Delete"), 1, NULL);
        break;
    }
    case S_UNINSTALL_CONFIRM: {
        const Pc5Game *game = &ui->library.games[ui->menu];
        const Pc5Picture *thumb = ui->covers[ui->menu].state ? &ui->covers[ui->menu].picture : NULL;
        if (ui->uninstall_back == S_GAME_MENU)
            game_menu_screen(ui, c, game_row_find(&ui->controls, ui_game_kind(ui), GR_UNINSTALL, NULL));
        pc5_tf(line, sizeof(line), "Uninstall %s?", game->title);
        if (pc5_remove_can_keep(game, &ui->library))
            confirm3_dialog(ui, c, line,
                            T("Keep saves: the game goes, its saves and settings stay, and installing it again puts them back. "
                            "Delete all: the game, its saves and settings go, with no backup and no undo."),
                            T("Cancel"), T("Keep saves"), T("Delete all"), thumb);
        else
            confirm_dialog(ui, c, line,
                           T("This deletes the game and all its saves and settings. There is no backup and no undo."),
                           T("Cancel"), T("Uninstall"), 1, thumb);
        break;
    }
    case S_REMOVING:
        pc5_tf(line, sizeof(line), ui->removing_kind == 2 ? "Deleting the saves of %s" : "Uninstalling %s", ui->removing_title);
        {
            char count[64];
            pc5_tf(count, sizeof(count), "%u files and folders deleted", (unsigned)ui->remove.removed);
            busy_dialog(ui, c, line, count, 0);
        }
        break;
    case S_MESSAGE: message_dialog(ui, c); break;
    case S_MUSIC_IMPORT:
        busy_dialog(ui, c, T("Importing library music"),
                    T("Preparing a copy on the console. Keep the source drive connected until this finishes."), 1);
        break;
    case S_VIDEO_IMPORT:
        busy_dialog(ui, c, T("Importing background video"),
                    T("Copying video to the console. Keep the source drive connected until this finishes."), 1);
        break;
    case S_REPORT: library_screen(ui, c); report_screen(ui, c); break;
    case S_FILES:
    case S_FILES_MENU:
    case S_FILES_TYPING:
    case S_FILES_CONFLICT:
    case S_FILES_DELETE:
    case S_FILES_WORKING:
    case S_FILES_EDIT:
    case S_FILES_EDIT_MENU:
    case S_FILES_EDIT_TYPING:
    case S_FILES_EDIT_LEAVE:
        files_draw(ui, c);
        break;
    case S_INSTALL_ASK: {
        char body[PC5_PATH + 300];
        int one = ui->install_waiting == 1;
        pc5_tf(body, sizeof(body),
               ui->usb_seen ? (one ? "%d game is waiting in PC5's INSTALL folders (its own, and PC5/INSTALL on the USB drive). "
                                     "Install it now? A game installed from there is moved into PC5's library, out of the "
                                     "INSTALL folder."
                                   : "%d games are waiting in PC5's INSTALL folders (its own, and PC5/INSTALL on the USB drive). "
                                     "Install them now? A game installed from there is moved into PC5's library, out of the "
                                     "INSTALL folder.")
                            : (one ? "%d game is waiting in PC5's INSTALL folder. Install it now? A game installed from there "
                                     "is moved into PC5's library, out of the INSTALL folder."
                                   : "%d games are waiting in PC5's INSTALL folder. Install them now? A game installed from "
                                     "there is moved into PC5's library, out of the INSTALL folder."),
               ui->install_waiting);
        confirm_dialog(ui, c, T("Games in PC5's INSTALL folder"), body, T("Later"), T("Install"), 0, NULL);
        break;
    }
    case S_TILE_ART:
        game_menu_screen(ui, c, ui->menu);
        busy_dialog(ui, c, T("Updating the home screen tile"), T("Its picture and background"), 0);
        break;
    case S_REPORT_CHECK: message_dialog(ui, c); busy_dialog(ui, c, T("Checking for a newer PC5"), T("A newer one may have fixed it"), 0); break;
    case S_REPORT_UPDATE: {
        char title[120], body[500], how[200];
        pc5_tf(title, sizeof(title), "Update to PC5 %s first?", ui->update.last.version);
        if (offer_update(ui)) update_how(ui, how, sizeof(how));
        else pc5_tf(how, sizeof(how), "You have %s", PC5_VERSION);
        pc5_tf(body, sizeof(body), "This may already be fixed in it. Update, try the game again, and report it if it "
                 "still happens. %s.", how);
        message_dialog(ui, c);
        confirm_dialog(ui, c, title, body, T("Update now"), T("Report anyway"), 0, NULL);
        break;
    }
    case S_LAUNCH: {
        int index = pc5_library_find(&ui->library, ui->launch_id);
        pc5_tf(line, sizeof(line), "Starting %s", index >= 0 ? ui->library.games[index].title : T("the game"));
        if (ui->redist_state == 1) {
            char count[PC5_NAME + 64];
            pc5_tf(count, sizeof(count), "Installing %.80s for it, once", ui->redist_name);
            busy_dialog(ui, c, line, count, 0);
        } else if (ui->runtime_state == 1) {
            char count[96];
            pc5_tf(count, sizeof(count), "Setting up .NET for your games, once: %u of %u", (unsigned)ui->runtime.done,
                     (unsigned)ui->runtime.total);
            busy_dialog(ui, c, line, count, 0);
        } else {
            /* Made ready: PC5's screen fades to black, and the game's process shows the loading
             * picture from black as soon as it can draw (prospero-win 0117): one change of
             * picture, where PC5's own loading picture went black between them. */
            uint64_t since = ui->launch_ready ? ui->now - ui->launch_ready_at : 0;
            int alpha = !ui->launch_ready ? 0 : since >= LAUNCH_FADE_MS ? 255 : (int)(since * 255 / LAUNCH_FADE_MS);
            if (alpha) dim(c, alpha);
        }
        break;
    }
    default: break;
    }
    boot_overlay(ui, c);
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
