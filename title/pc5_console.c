/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5 on the console (pc5_console.h): prospero-win's presenter, pad and
 * keyboard, the console's clock and settings, its HTTPS, the startup sound on
 * the controller's speaker, and closing PC5 after it updated itself. */
#include "pc5_console.h"
#include "pc5_home.h"
#include "pc5.h"
#include "pc5_game.h"
#include "pc5_net.h"
#include "pc5_settings.h"
#include "pc5_sound.h"
#include "pc5_ui.h"
#include "pc5_upgrade.h"

#include "pw_audio_ps5.h"
#include "pw_diagnostics.h"
#include "pw_hid_ps5.h"
#include "pw_pad_ps5.h"
#include "pw_videoout_ps5.h"
#include "../src/pw_present.h"
#include "../include/prospero_win.h"

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

int sceSystemServiceParamGetInt(int id, int *value);
int sceSystemServiceLoadExec(const char *path, char *const argv[]);

/* The console's notification (libkernel): text in the request's message. */
typedef struct NotifyRequest {
    char reserved[45];
    char message[3075];
} NotifyRequest;
int sceKernelSendNotificationRequest(int device, NotifyRequest *request, size_t size, int blocking);
void pc5_net_ps5_init(const char *app, void (*log)(const char *line));
void pc5_ftp_ps5_init(void (*log)(const char *line));
void pc5_game_ps5_ops(Pc5GameOps *ops, void (*log)(const char *line));

/* sceSystemServiceParamGetInt's ids, as the PS4 and PS5 SDKs number them. */
enum { PARAM_LANG = 1, PARAM_TIME_FORMAT = 3, PARAM_TIME_ZONE = 4, PARAM_SUMMERTIME = 5, PARAM_ENTER_BUTTON = 1000 };

enum { PAD_R3 = 0x4u, PAD_L2 = 0x100u, PAD_R2 = 0x200u, PAD_OPTIONS = 0x8u, PAD_UP = 0x10u, PAD_RIGHT = 0x20u, PAD_DOWN = 0x40u, PAD_LEFT = 0x80u, PAD_L1 = 0x400u,
       PAD_R1 = 0x800u, PAD_TRIANGLE = 0x1000u, PAD_CIRCLE = 0x2000u, PAD_CROSS = 0x4000u, PAD_SQUARE = 0x8000u };

static const Pc5Console *console;
static int time_format = -1, time_zone, summer;

const char *pc5_console_locale(void)
{
    /* SCE_SYSTEM_PARAM_LANG_*: Wine takes the Windows language from LC_ALL. */
    static const char *const locales[] = {
        "ja_JP", "en_US", "fr_FR", "es_ES", "de_DE", "it_IT", "nl_NL", "pt_PT", "ru_RU", "ko_KR",
        "zh_TW", "zh_CN", "fi_FI", "sv_SE", "da_DK", "nb_NO", "pl_PL", "pt_BR", "en_GB", "tr_TR",
        "es_MX", "ar_SA", "fr_CA", "cs_CZ", "hu_HU", "el_GR", "ro_RO", "th_TH", "vi_VN", "id_ID",
    };
    static char locale[24];
    int language = -1;

    if (sceSystemServiceParamGetInt(PARAM_LANG, &language) || language < 0 ||
        language >= (int)(sizeof(locales) / sizeof(locales[0])))
        return NULL;
    snprintf(locale, sizeof(locale), "%s.UTF-8", locales[language]);
    return locale;
}

static void log_line(void *context, const char *line)
{
    (void)context;
    if (console->log) console->log(line);
}

static void plain_log(const char *line)
{
    if (console->log) console->log(line);
}

/* The time as the console shows it: UTC from the clock, the console's time
 * zone and summer time, its 12- or 24-hour setting. */
static void clock_text(void *context, char *out, size_t size)
{
    time_t now = time(NULL);
    (void)context;
    out[0] = 0;
    if (time_format < 0 || now < 1600000000) return;
    long long local = (long long)now + (long long)time_zone * 60 + (summer ? 3600 : 0);
    int minutes = (int)((local / 60) % (24 * 60));
    if (minutes < 0) minutes += 24 * 60;
    int hour = minutes / 60, minute = minutes % 60;
    if (time_format == 0) snprintf(out, size, "%d:%02d %s", hour % 12 ? hour % 12 : 12, minute, hour < 12 ? "AM" : "PM");
    else snprintf(out, size, "%02d:%02d", hour, minute);
}

static void notify_text(void *context, const char *text)
{
    static NotifyRequest request;
    (void)context;
    memset(&request, 0, sizeof(request));
    snprintf(request.message, sizeof(request.message), "%s", text);
    int rc = sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
    char line[160];
    snprintf(line, sizeof(line), "PC5 notification rc=%d", rc);
    if (console->log) console->log(line);
}

/* Seconds since 1970 and the console's offset from UTC (its time zone and summer time). */
static int64_t unix_time(void *context, int *offset_s)
{
    time_t now = time(NULL);
    (void)context;
    if (offset_s) *offset_s = time_format >= 0 ? time_zone * 60 + (summer ? 3600 : 0) : 0;
    return now < 1600000000 ? 0 : (int64_t)now;
}

/* PC5's own crash: a file naming this session, for pc5_logs_tidy when PC5
 * next opens (the kernel ends the process; the log has no end line, as when
 * the PS button closes PC5). Only async-signal-safe calls. */
static char crash_marker[PC5_PATH];

static void crashed(int sig)
{
    int fd = open(crash_marker, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        char text[16] = "signal ";
        text[7] = (char)('0' + (sig / 10) % 10);
        text[8] = (char)('0' + sig % 10);
        text[9] = '\n';
        (void)!write(fd, text, 10);
        close(fd);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

static void watch_for_crashes(const char *root)
{
    char path[PC5_PATH];
    Pc5Buf text = { 0 };
    int next = -1;

    if (!pc5_format(path, sizeof(path), "%s/logs/next.txt", root) && !pc5_read_file(path, &text, 32))
        next = atoi((char *)text.data);
    pc5_buf_free(&text);
    /* This session is the one before next.txt's (prospero-win's eight). */
    if (next < 0 || next > 7 || pc5_format(path, sizeof(path), "%s/pc5", root) || pc5_mkdirs(path) ||
        pc5_format(crash_marker, sizeof(crash_marker), "%s/pc5/crash-%d.txt", root, (next + 7) % 8))
        return;
    unlink(crash_marker);
    signal(SIGSEGV, crashed);
    signal(SIGBUS, crashed);
    signal(SIGILL, crashed);
    signal(SIGFPE, crashed);
    signal(SIGABRT, crashed);
}

static PwVideoOutPs5 video;
static PwPadPs5 pad;
static int video_status = PW_ERR_STATE, pad_status = PW_ERR_STATE;

/* ---- the startup sound -----------------------------------------------------------------------
 * The chosen sound (pc5_sound.h) on the controller's speaker (sceAudioOut's pad speaker port:
 * the user's controller, 16-bit mono at 48 kHz), on a thread of its own, once when PC5 is
 * opened: not when its screens come back after a game (the title restarted, cycle above 0).
 * Settings plays a sound when it is chosen. */
enum { AUDIO_PORT_PADSPK = 4, AUDIO_FORMAT_S16_MONO = 0, AUDIO_GRAIN = 256, AUDIO_VOLUME_0DB = 0x8000,
       AUDIO_VOLUME_FLAG_CH0 = 1 };

static Pc5Sound startup_sound;
static pthread_t sound_thread;
static int sound_started, sound_user = -1;
static atomic_int sound_stop;

static int sound_volume = 100;   /* Settings › System › Startup sound volume, in percent */

/* The port's volume (sceAudioOut's, AUDIO_VOLUME_0DB the most) for that percent, on its square:
 * 50 % is a quarter of the most, 10 % a hundredth, so the lower steps are quiet. PC5 sets only
 * its own port's volume and asks the console's system for none (1.8.2 to 1.8.4 asked it for the
 * Controller Speaker Volume, and closed at start on some consoles, CE-108255-1). */
static int sound_level(int percent)
{
    if (percent < 10) percent = 10;
    if (percent > 100) percent = 100;
    return AUDIO_VOLUME_0DB * percent * percent / 10000;
}

static void *play_sound(void *argument)
{
    PwAudioPs5Ops ops;
    int16_t grain[AUDIO_GRAIN];
    char line[200];
    int init = -1, port = -1, volume = -1, output = 0, level = sound_level(sound_volume);
    size_t played = 0;
    (void)argument;

    if (pw_audio_ps5_platform_ops(&ops) == PW_OK && (init = ops.init()) >= 0) {
        port = ops.open(sound_user, AUDIO_PORT_PADSPK, 0, AUDIO_GRAIN, PC5_SOUND_RATE, AUDIO_FORMAT_S16_MONO);
        if (port >= 0) {
            int32_t volumes[8];
            for (int i = 0; i < 8; i++) volumes[i] = level;
            volume = ops.volume(port, AUDIO_VOLUME_FLAG_CH0, volumes);
        }
    }
    snprintf(line, sizeof(line), "PC5 startup sound user=%d init=%d port=%d volume=%d percent=%d level=%d frames=%zu",
             sound_user, init, port, volume, sound_volume, level, startup_sound.frames);
    plain_log(line);
    if (port >= 0) {
        while (played < startup_sound.frames && !atomic_load(&sound_stop)) {
            size_t n = startup_sound.frames - played < AUDIO_GRAIN ? startup_sound.frames - played : AUDIO_GRAIN;
            memset(grain, 0, sizeof(grain));
            memcpy(grain, startup_sound.samples + played, n * sizeof(int16_t));
            if ((output = ops.output(port, grain)) < 0) break;
            played += n;
        }
        if (output >= 0) (void)ops.output(port, NULL);   /* until what was given is heard */
        (void)ops.close(port);
        snprintf(line, sizeof(line), "PC5 startup sound played=%zu output=%d", played, output);
        plain_log(line);
    }
    pc5_sound_free(&startup_sound);
    return NULL;
}

/* Before the title is replaced or closed, or another sound plays: the sound stops and its
 * port is closed. */
static void sound_end(void)
{
    if (!sound_started) return;
    atomic_store(&sound_stop, 1);
    pthread_join(sound_thread, NULL);
    sound_started = 0;
}

/* The sound of that name (the default one when it is gone), from its start, at that volume. */
static void sound_play(const char *name, int percent)
{
    char path[PC5_PATH], line[PC5_PATH + 80];
    Pc5Buf file = { 0 };

    sound_end();
    if (sound_user < 0 || !console) return;
    if (pc5_sound_path(console->app, console->root, name, path, sizeof(path)) &&
        pc5_sound_path(console->app, console->root, PC5_SOUND_DEFAULT, path, sizeof(path))) {
        snprintf(line, sizeof(line), "PC5 startup sound: no %.64s", name);
        plain_log(line);
        return;
    }
    /* 30 s of the biggest WAV PC5 reads (192 kHz, stereo, 32-bit float) and its header. */
    if (pc5_read_file(path, &file, 48u << 20) || pc5_sound_from_wav(file.data, file.len, &startup_sound)) {
        snprintf(line, sizeof(line), "PC5 startup sound: can't read %s", path);
        plain_log(line);
        pc5_buf_free(&file);
        return;
    }
    pc5_buf_free(&file);
    sound_volume = percent;
    atomic_store(&sound_stop, 0);
    sound_started = !pc5_thread_start(&sound_thread, play_sound, NULL, 0);
    if (!sound_started) pc5_sound_free(&startup_sound);
}

static void sound_start(const Pc5Console *config, int32_t user)
{
    Pc5Settings settings;

    sound_user = user;
    pc5_settings_load(config->root, &settings);
    if (!strcmp(settings.startup_sound, "off") || config->cycle || (config->start && config->start[0])) return;
    sound_play(settings.startup_sound, settings.startup_volume);
}

/* Settings tries a sound. */
static void sound_try(void *context, const char *name, int volume)
{
    (void)context;
    sound_play(name, volume);
}

/* The console's on-screen keyboard over the screens (pc5_game_ps5.c), for a name to search for:
 * its text and title in UTF-16, kept while it is up. */
int pc5_screens_keyboard_open(uint16_t *buffer, uint32_t capacity, const uint16_t *title);
int pc5_screens_keyboard_state(void);
int pc5_screens_keyboard_close(void);
static uint16_t keyboard_text[PC5_NAME], keyboard_title[96];

static int keyboard_open(void *context, const char *title, const char *text)
{
    char line[96];
    (void)context;
    pc5_utf16_from_utf8(keyboard_title, sizeof(keyboard_title) / sizeof(keyboard_title[0]), title);
    pc5_utf16_from_utf8(keyboard_text, sizeof(keyboard_text) / sizeof(keyboard_text[0]), text);
    int rc = pc5_screens_keyboard_open(keyboard_text, sizeof(keyboard_text) / sizeof(keyboard_text[0]), keyboard_title);
    snprintf(line, sizeof(line), "PC5 screens keyboard open rc=%#x", (unsigned)rc);
    plain_log(line);
    return rc < 0 ? rc : 0;
}

static int keyboard_state(void *context)
{
    (void)context;
    return pc5_screens_keyboard_state();
}

static int keyboard_close(void *context, char *out, size_t size)
{
    (void)context;
    int rc = pc5_screens_keyboard_close();
    if (rc == 1) pc5_utf8_from_utf16(out, size, keyboard_text);
    else if (size) out[0] = 0;
    return rc;
}

/* PC5 updated itself: the new PC5 starts in its place, as a game does (the console's
 * LoadExec, the eboot as /app0 or the sandbox shows it), fresh as when it is opened. If
 * the console refuses, PC5 closes as an app does. Never exit(): the console takes that
 * for a crash (CE-108255-1). */
static void restart(void *context)
{
    static char launcher[] = "launcher=1", cycle[] = "cycle=0";
    char *argv[] = { launcher, cycle, NULL };
    char sandbox[PC5_PATH], line[PC5_PATH + 64];
    const char *const eboots[] = { "/app0/eboot.bin", sandbox };
    (void)context;

    sound_end();
    if (pad_status == PW_OK) pw_pad_ps5_close(&pad);
    if (video_status == PW_OK) pw_videoout_ps5_close(&video);
    pad_status = video_status = PW_ERR_STATE;
    snprintf(sandbox, sizeof(sandbox), "%s/eboot.bin", console->app);
    for (size_t i = 0; i < sizeof(eboots) / sizeof(eboots[0]); i++) {
        snprintf(line, sizeof(line), "PC5 restarting after its update eboot=%s", eboots[i]);
        plain_log(line);
        pw_diagnostics_close("pc5-update");
        int rc = sceSystemServiceLoadExec(eboots[i], argv);
        pw_diagnostics_resume();
        snprintf(line, sizeof(line), "PC5 restart refused rc=0x%08x", (unsigned)rc);
        plain_log(line);
    }
    plain_log("PC5 closing after its update");
    pw_diagnostics_close("pc5-update");
    int rc = sceSystemServiceLoadExec("exit", NULL);
    pw_diagnostics_resume();
    if (rc >= 0)
        for (int i = 0; i < 50; i++) usleep(100000);   /* the console ends the process */
    /* Refused, or still here: the screen and the pad back, and the player told (pc5_ui). */
    snprintf(line, sizeof(line), "PC5 close refused rc=0x%08x", (unsigned)rc);
    plain_log(line);
    {
        static const PwPadKeyMap open_map[] = { { PAD_CROSS, 0x20, 0, 0, "cross" } };
        PwPadPs5Ops ops;
        video_status = pw_videoout_ps5_open(&video);
        if (pw_pad_ps5_platform_ops(&ops) == PW_OK) pad_status = pw_pad_ps5_open(&pad, &ops, open_map, 1);
    }
}

/* The game gets the screen and the pad: PC5 lets go of them first, and
 * takes them again only if the game could not be started. */
static void play(void *context, const char *id)
{
    static const PwPadKeyMap open_map[] = { { PAD_CROSS, 0x20, 0, 0, "cross" } };
    PwPadPs5Ops ops;
    (void)context;
    sound_end();
    if (pad_status == PW_OK) pw_pad_ps5_close(&pad);
    if (video_status == PW_OK) pw_videoout_ps5_close(&video);
    pad_status = video_status = PW_ERR_STATE;
    console->play(id);
    video_status = pw_videoout_ps5_open(&video);
    if (pw_pad_ps5_platform_ops(&ops) == PW_OK) pad_status = pw_pad_ps5_open(&pad, &ops, open_map, 1);
}

static uint64_t now_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static uint32_t key_of(uint16_t vk)
{
    switch (vk) {
    case 0x26: return PC5_KEY_UP;
    case 0x28: return PC5_KEY_DOWN;
    case 0x25: return PC5_KEY_LEFT;
    case 0x27: return PC5_KEY_RIGHT;
    case 0x0d: case 0x20: return PC5_KEY_ACCEPT;
    case 0x1b: case 0x08: return PC5_KEY_BACK;
    case 0x70: case 0x5d: case 0x09: return PC5_KEY_OPTIONS;   /* F1, the menu key, Tab */
    case 0x21: return PC5_KEY_L1;                             /* Page Up */
    case 0x22: return PC5_KEY_R1;                             /* Page Down */
    case 0x71: case 0x2e: return PC5_KEY_SQUARE;              /* F2, Delete */
    case 0x72: return PC5_KEY_TRIANGLE;                       /* F3 */
    case 0x24: return PC5_KEY_L2;                             /* Home */
    case 0x23: return PC5_KEY_R2;                             /* End */
    case 0x73: return PC5_KEY_R3;                             /* F4 */
    default: return 0;
    }
}

void pc5_console_run(const Pc5Console *config)
{
    static const PwPadKeyMap open_map[] = { { PAD_CROSS, 0x20, 0, 0, "cross" } };
    static PwHidPs5 hid;
    const char *usb_roots[] = { "/mnt/usb0", "/mnt/usb1", "/mnt/usb2", "/mnt/usb3", "/mnt/usb4", "/mnt/usb5",
                                "/mnt/usb6", "/mnt/usb7", NULL };
    char template_prefix[PC5_PATH], tile_template[PC5_PATH], fonts[PC5_PATH], line[256], finished[32] = "";
    const size_t frame_bytes = (size_t)1920 * 1080 * 4;
    uint32_t *frame;
    PwPadPs5Ops pad_ops;
    int hid_status, enter = 1;
    uint32_t keyboard_held = 0, stick_held = 0, previous = 0;
    uint64_t frames = 0, drawn = 0, draw_ms = 0, last_report;
    Pc5Platform platform;
    Pc5Ui *ui;

    console = config;
    pc5_net_ps5_init(config->app, plain_log);
    pc5_ftp_ps5_init(plain_log);
    snprintf(template_prefix, sizeof(template_prefix), "%s/pc5/prefix", config->app);
    snprintf(tile_template, sizeof(tile_template), "%s/pc5/forwarder", config->app);
    snprintf(fonts, sizeof(fonts), "%s/pc5/fonts.bin", config->app);
    if (sceSystemServiceParamGetInt(PARAM_TIME_FORMAT, &time_format)) time_format = -1;
    if (sceSystemServiceParamGetInt(PARAM_TIME_ZONE, &time_zone) || time_zone < -720 || time_zone > 840) time_format = -1;
    if (sceSystemServiceParamGetInt(PARAM_SUMMERTIME, &summer)) summer = 0;
    if (sceSystemServiceParamGetInt(PARAM_ENTER_BUTTON, &enter)) enter = 1;
    snprintf(line, sizeof(line), "PC5 start version=%s root=%s app=%s cycle=%u time_format=%d zone=%d summer=%d enter=%d locale=%s",
             PC5_VERSION, config->root, config->app, config->cycle, time_format, time_zone, summer, enter,
             pc5_console_locale() ? pc5_console_locale() : "-");
    plain_log(line);
    pc5_system_text(line, sizeof(line));
    plain_log(line);
    {
        /* What an update left, before anything reads the app's files. */
        Pc5UpgradeConfig upgrade = { { NULL, NULL, NULL, NULL }, config->app, config->root, NULL, "/user", log_line, NULL };
        pc5_upgrade_tidy(&upgrade, finished, sizeof(finished));
    }

    frame = mmap(NULL, frame_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    video_status = pw_videoout_ps5_open(&video);
    if (frame == MAP_FAILED || video_status != PW_OK) {
        snprintf(line, sizeof(line), "PC5 no screen video=%s frame=%d", pw_result_name(video_status), frame != MAP_FAILED);
        plain_log(line);
        if (video_status == PW_OK) pw_videoout_ps5_close(&video);
        if (frame != MAP_FAILED) munmap(frame, frame_bytes);
        return;
    }
    pad_status = pw_pad_ps5_platform_ops(&pad_ops);
    if (pad_status == PW_OK) pad_status = pw_pad_ps5_open(&pad, &pad_ops, open_map, 1);
    {
        int32_t user = pad_status == PW_OK ? pad.user_id : -1;
        if (user < 0 && pw_pad_ps5_platform_ops(&pad_ops) == PW_OK) {
            (void)pad_ops.user_initialize(NULL);
            (void)pad_ops.foreground_user(&user);
        }
        hid_status = pw_hid_ps5_open(&hid, pw_hid_ps5_system_ops(plain_log), user);
        sound_start(config, user);
    }
    snprintf(line, sizeof(line), "PC5 devices pad=%s keyboard=%s", pw_result_name(pad_status), pw_result_name(hid_status));
    plain_log(line);
    /* A game may have left the light bar or the triggers set: as the console had them. */
    if (pad_status == PW_OK) {
        Pc5GameOps game_ops;
        Pc5Trigger off = { .mode = PC5_TRIGGER_OFF };
        pc5_game_ps5_ops(&game_ops, plain_log);
        int light = game_ops.light_bar_reset(pad.pad_handle), triggers = game_ops.triggers(pad.pad_handle, &off, &off);
        snprintf(line, sizeof(line), "PC5 controller reset light=%d triggers=%d", light, triggers);
        plain_log(line);
    }

    watch_for_crashes(config->root);
    platform = (Pc5Platform){ NULL, { NULL, pc5_net_get, pc5_net_download, pc5_net_get_authorized }, clock_text, play, log_line, notify_text,
                              unix_time, enter == 0, restart, sound_try, keyboard_open, keyboard_state, keyboard_close };
    {
        Pc5UiConfig ui_config = { config->root, template_prefix, fonts, usb_roots, PC5_HOME_ROOT, tile_template,
                                  config->start, pc5_upgrade_drives, "/user", finished, 1,
                                  !config->cycle && !(config->start && config->start[0]),
                                  !config->cycle && !(config->start && config->start[0]) };
        ui = pc5_ui_create(&ui_config, &platform);
    }
    if (!ui) {
        plain_log("PC5 out of memory");
        sound_end();
        pw_videoout_ps5_close(&video);
        return;
    }
    last_report = now_ms();
    for (uint64_t tick = 1;; tick++) {
        Pc5Input input = { 0, 0 };
        uint32_t held = keyboard_held;
        if (pad_status == PW_OK && pw_pad_ps5_read(&pad) == PW_OK) {
            uint32_t buttons = pad.core.previous_buttons;
            uint32_t accept = enter == 0 ? PAD_CIRCLE : PAD_CROSS, back = enter == 0 ? PAD_CROSS : PAD_CIRCLE;
            if (buttons & PAD_UP) held |= PC5_KEY_UP;
            if (buttons & PAD_DOWN) held |= PC5_KEY_DOWN;
            if (buttons & PAD_LEFT) held |= PC5_KEY_LEFT;
            if (buttons & PAD_RIGHT) held |= PC5_KEY_RIGHT;
            if (buttons & accept) held |= PC5_KEY_ACCEPT;
            if (buttons & back) held |= PC5_KEY_BACK;
            if (buttons & PAD_OPTIONS) held |= PC5_KEY_OPTIONS;
            if (buttons & PAD_L1) held |= PC5_KEY_L1;
            if (buttons & PAD_R1) held |= PC5_KEY_R1;
            if (buttons & PAD_L2) held |= PC5_KEY_L2;
            if (buttons & PAD_R2) held |= PC5_KEY_R2;
            if (buttons & PAD_R3) held |= PC5_KEY_R3;
            if (buttons & PAD_SQUARE) held |= PC5_KEY_SQUARE;
            if (buttons & PAD_TRIANGLE) held |= PC5_KEY_TRIANGLE;
            /* The left stick moves too, past a third of its travel. */
            stick_held = 0;
            if (pad.left_stick.x < 0x40) stick_held |= PC5_KEY_LEFT;
            if (pad.left_stick.x > 0xc0) stick_held |= PC5_KEY_RIGHT;
            if (pad.left_stick.y < 0x40) stick_held |= PC5_KEY_UP;
            if (pad.left_stick.y > 0xc0) stick_held |= PC5_KEY_DOWN;
            held |= stick_held;
        } else {
            held |= stick_held;
        }
        if (hid.ops.load_module) {
            PwHidPs5Poll poll;
            if (hid_status != PW_OK && tick % 300 == 0) hid_status = pw_hid_ps5_retry(&hid);
            pw_hid_ps5_poll(&hid, &poll);
            for (size_t i = 0; i < poll.count; i++)
                if (poll.events[i].kind == PW_HID_EVENT_KEY) {
                    uint32_t key = key_of(poll.events[i].code);
                    if (poll.events[i].down) { keyboard_held |= key; held |= key; input.pressed |= key & ~previous; }
                    else keyboard_held &= ~key;
                }
        }
        input.held = held;
        input.pressed |= held & ~previous;
        previous = held;
        uint64_t now = now_ms();
        if (pc5_ui_update(ui, &input, now)) {
            const PwPresentView view = { (const uint8_t *)frame, 1920, 1080, 1920 * 4, (uint32_t)frame_bytes };
            Pc5Canvas canvas = { frame, 1920, 1080, 1920, 0, 0 };
            uint64_t before = now_ms();
            pc5_ui_draw(ui, &canvas);
            int status = video_status == PW_OK ? pw_videoout_ps5_present(&video, &view) : video_status;
            draw_ms += now_ms() - before;
            drawn++;
            if (status != PW_OK && drawn < 5) {
                snprintf(line, sizeof(line), "PC5 present=%s", pw_result_name(status));
                plain_log(line);
            }
        } else {
            usleep(12000);
        }
        frames++;
        if (now - last_report >= 30000 && drawn) {
            snprintf(line, sizeof(line), "PC5 frames=%llu drawn=%llu draw_ms_avg=%.1f screen=%s",
                     (unsigned long long)frames, (unsigned long long)drawn, (double)draw_ms / (double)drawn,
                     pc5_ui_screen(ui));
            plain_log(line);
            last_report = now;
            frames = drawn = draw_ms = 0;
        }
    }
}
