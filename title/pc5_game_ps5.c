/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5's game ops on the console (pc5_game.h): libScePad's light bar and
 * adaptive triggers, VideoOut's second hardware cursor for the overlay over
 * the frames the title shows itself, the overlay's numbers (the process's
 * CPU time, libkernel's temperatures and memory), the on-screen
 * keyboard (libSceImeDialog), and the console's firmware for the log. */
#include "pc5_game.h"
#include "pc5_console.h"
#include "pc5_logs.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/user.h>
#include <unistd.h>
#include <x86intrin.h>

typedef struct LightBar { uint8_t r, g, b, reserved; } LightBar;
int scePadSetLightBar(int32_t handle, const LightBar *light);
int scePadResetLightBar(int32_t handle);

/* The adaptive triggers (libScePad's ScePadTriggerEffectParam): which
 * triggers, then a command for L2 and one for R2, each a mode and its
 * numbers. Feedback: where it starts (0-9) and how firm (1-8); vibration:
 * where it starts, how strong (1-8) and how fast (Hz); a game's own effect
 * (DualSense mode) comes with its mode, 1 to 6, and its numbers. */
enum { TRIGGER_L2 = 0x01, TRIGGER_R2 = 0x02, MODE_OFF = 0, MODE_FEEDBACK = 1, MODE_VIBRATION = 3,
       MODE_MULTIPLE_VIBRATION = 6 };
typedef struct TriggerCommand { uint32_t mode; uint8_t padding[4]; uint8_t data[48]; } TriggerCommand;
typedef struct TriggerParam { uint8_t mask; uint8_t padding[7]; TriggerCommand command[2]; } TriggerParam;
_Static_assert(sizeof(TriggerParam) == 120, "ScePadTriggerEffectParam is 120 bytes");
int scePadSetTriggerEffect(int32_t handle, const TriggerParam *param);

int sceVideoOutCursorEnable(int32_t handle, int32_t index, const void *address);
int sceVideoOutCursorDisable(int32_t handle, int32_t index);
int sceVideoOutCursorSetImageAddress(int32_t handle, int32_t index, const void *address);
int sceVideoOutCursorSetPosition(int32_t handle, int32_t index, uint32_t x, uint32_t y);
int sceVideoOutCursorSet2xMagnify(int32_t handle, int32_t index, uint32_t enable);
size_t sceKernelGetDirectMemorySize(void);
int sceKernelAllocateDirectMemory(int64_t start, int64_t end, size_t bytes, size_t alignment, int type, int64_t *offset);
int sceKernelMapDirectMemory(void **address, size_t bytes, int protection, int flags, int64_t offset, size_t alignment);
int sceKernelGetCpuTemperature(int *celsius);
int sceKernelGetSocSensorTemperature(int sensor, int *celsius);
int sceKernelConfiguredFlexibleMemorySize(size_t *bytes);
int sceKernelAvailableFlexibleMemorySize(size_t *bytes);
/* What sceKernelDirectMemoryQuery tells of an allocation (SCE_KERNEL_DMQ_FIND_NEXT: the one at or after offset). */
typedef struct DirectMemoryQueryInfo { int64_t start, end; int32_t type; } DirectMemoryQueryInfo;
int sceKernelDirectMemoryQuery(int64_t offset, int flags, DirectMemoryQueryInfo *info, size_t size);

static int light_bar(int handle, uint32_t rgb)
{
    const LightBar light = { (uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb, 0 };
    return scePadSetLightBar(handle, &light);
}

static int light_bar_reset(int handle)
{
    return scePadResetLightBar(handle);
}

static void command(TriggerCommand *out, const Pc5Trigger *trigger)
{
    memset(out, 0, sizeof(*out));
    switch (trigger->mode) {
    case PC5_TRIGGER_FEEDBACK:
        out->mode = MODE_FEEDBACK;
        out->data[0] = trigger->position;
        out->data[1] = trigger->strength;
        break;
    case PC5_TRIGGER_VIBRATION:
        out->mode = MODE_VIBRATION;
        out->data[0] = trigger->position;
        out->data[1] = trigger->amplitude;
        out->data[2] = trigger->frequency;
        break;
    case PC5_TRIGGER_GAME:
        /* the game's effect, already in libScePad's terms */
        out->mode = trigger->effect <= MODE_MULTIPLE_VIBRATION ? trigger->effect : MODE_OFF;
        if (out->mode != MODE_OFF) memcpy(out->data, trigger->data, sizeof(trigger->data));
        break;
    default:
        out->mode = MODE_OFF;
        break;
    }
}

static int triggers(int handle, const Pc5Trigger *l2, const Pc5Trigger *r2)
{
    TriggerParam param;
    memset(&param, 0, sizeof(param));
    param.mask = TRIGGER_L2 | TRIGGER_R2;
    command(&param.command[0], l2);
    command(&param.command[1], r2);
    return scePadSetTriggerEffect(handle, &param);
}

/* Two 64x64 images in display-readable memory, written in turn. */
static uint32_t *cursor_images;
static int cursor_failed, cursor_enabled, cursor_current;

static int cursor(int videoout, const uint32_t *image, uint32_t x, uint32_t y)
{
    enum { PIXELS = 64 * 64, MEMORY = 64 << 10 };
    if (!image) {
        if (cursor_enabled) sceVideoOutCursorDisable(videoout, 1);
        cursor_enabled = 0;
        return 0;
    }
    if (!cursor_images) {
        int64_t offset;
        void *address = NULL;
        if (cursor_failed) return -1;
        if (sceKernelAllocateDirectMemory(0, (int64_t)sceKernelGetDirectMemorySize(), MEMORY, MEMORY, 3, &offset) < 0 ||
            sceKernelMapDirectMemory(&address, MEMORY, 0x33, 0, offset, MEMORY) < 0) {
            cursor_failed = 1;
            return -1;
        }
        cursor_images = address;
    }
    uint32_t *next = cursor_images + (cursor_enabled ? cursor_current ^ 1 : cursor_current) * PIXELS;
    memcpy(next, image, PIXELS * 4);
    for (size_t at = 0; at < PIXELS * 4; at += 64) _mm_clflush((const uint8_t *)next + at);
    _mm_mfence();
    int rc = cursor_enabled ? sceVideoOutCursorSetImageAddress(videoout, 1, next) : sceVideoOutCursorEnable(videoout, 1, next);
    if (rc) return rc;
    cursor_current = (int)((next - cursor_images) / PIXELS);
    cursor_enabled = 1;
    return sceVideoOutCursorSetPosition(videoout, 1, x, y);
}

static int cursor_magnify(int videoout, int on)
{
    return sceVideoOutCursorSet2xMagnify(videoout, 1, on ? 1u : 0u);
}

static uint64_t cpu_time_us(void)
{
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage)) return 0;
    return (uint64_t)(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1000000u +
           (uint64_t)(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec);
}

static unsigned cpu_count(void)
{
    long online = sysconf(_SC_NPROCESSORS_ONLN);
    return online > 0 ? (unsigned)online : 1u;
}

/* The console's own count of each CPU's time (FreeBSD's kern.cp_times: user, nice,
 * system, interrupt and idle ticks a CPU, as longs), of everything it runs and not
 * the game alone: busy is all but idle. 0 when the console doesn't give it. */
static unsigned cpu_ticks(uint64_t *busy, uint64_t *total, unsigned most)
{
    enum { STATES = 5, IDLE = 4, MOST = 64 };
    long times[MOST * STATES];
    size_t length = sizeof(times);

    if (sysctlbyname("kern.cp_times", times, &length, NULL, 0) || length < sizeof(long) * STATES) return 0;
    unsigned count = (unsigned)(length / (sizeof(long) * STATES));
    if (count > most) count = most;
    for (unsigned i = 0; i < count; i++) {
        uint64_t all = 0;
        for (unsigned state = 0; state < STATES; state++) all += (uint64_t)times[i * STATES + state];
        total[i] = all;
        busy[i] = all - (uint64_t)times[i * STATES + IDLE];
    }
    return count;
}

static void (*ps5_log)(const char *line);

/* This process's threads from the kernel (kern.proc.pid.<pid>, a kinfo_proc a thread):
 * each one's CPU time, name and last CPU. The entries are taken at the size the console
 * gives (the first field), as its kinfo_proc may be longer than this SDK's; what it
 * answered is logged once. 0 when it says no. */
static unsigned thread_times(Pc5ThreadTime *out, unsigned most)
{
    static uint8_t buffer[PC5_THREADS_MOST * 2048];
    static int noted;
    const size_t need_tid = offsetof(struct kinfo_proc, ki_tid) + sizeof(lwpid_t),
                 need_name = offsetof(struct kinfo_proc, ki_tdname) + sizeof(((struct kinfo_proc *)0)->ki_tdname),
                 need_cpu = offsetof(struct kinfo_proc, ki_lastcpu) + sizeof(int),
                 need = need_tid > need_name ? (need_tid > need_cpu ? need_tid : need_cpu) : (need_name > need_cpu ? need_name : need_cpu);
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID | KERN_PROC_INC_THREAD, (int)getpid() };
    size_t length = sizeof(buffer);
    int rc = sysctl(mib, 4, buffer, &length, NULL, 0), error = rc ? errno : 0, size = 0;
    char line[200];

    if (length >= sizeof(int)) memcpy(&size, buffer, sizeof(int));
    /* more threads than room: the first ones, as many as fit (ENOMEM) */
    if ((rc && error != ENOMEM) || size < (int)need || (size_t)size > length) {
        if (!noted && ps5_log) {
            snprintf(line, sizeof(line), "PC5 threads unknown: rc=%d errno=%d length=%zu size=%d (this SDK's %zu)", rc,
                     error, length, size, sizeof(struct kinfo_proc));
            ps5_log(line);
        }
        noted = 1;
        return 0;
    }
    unsigned count = (unsigned)(length / (size_t)size);
    if (count > most) count = most;
    for (unsigned i = 0; i < count; i++) {
        const uint8_t *k = buffer + (size_t)i * (size_t)size;
        int32_t tid;
        uint64_t runtime;
        memcpy(&tid, k + offsetof(struct kinfo_proc, ki_tid), sizeof(tid));
        memcpy(&out[i].cpu, k + offsetof(struct kinfo_proc, ki_lastcpu), sizeof(int));
        memcpy(&runtime, k + offsetof(struct kinfo_proc, ki_runtime), sizeof(runtime));
        out[i].tid = tid;
        out[i].runtime_us = runtime;
        snprintf(out[i].name, sizeof(out[i].name), "%.*s", (int)sizeof(((struct kinfo_proc *)0)->ki_tdname),
                 (const char *)(k + offsetof(struct kinfo_proc, ki_tdname)));
    }
    if (!noted && ps5_log) {
        snprintf(line, sizeof(line), "PC5 threads: %u, %d bytes each (this SDK's %zu); first tid=%d time=%lluus name=%s",
                 count, size, sizeof(struct kinfo_proc), out[0].tid, (unsigned long long)out[0].runtime_us, out[0].name);
        ps5_log(line);
    }
    noted = 1;
    return count;
}

static int temperature(int sensor, int *celsius)
{
    return sensor ? sceKernelGetSocSensorTemperature(0, celsius) : sceKernelGetCpuTemperature(celsius);
}

/* The memory the game's process has of what the console gives it: the
 * direct memory allocated (Wine's, the GPU's, the display's, a walk over
 * the allocations) and the flexible memory in use. */
static int memory(uint64_t *used, uint64_t *total)
{
    enum { FIND_NEXT = 1, MOST = 65536 };
    uint64_t direct = sceKernelGetDirectMemorySize(), allocated = 0;
    size_t configured = 0, available = 0;
    int64_t at = 0;
    int rc = 0, found = 0;

    while ((uint64_t)at < direct && found < MOST) {
        DirectMemoryQueryInfo info = { 0 };
        if ((rc = sceKernelDirectMemoryQuery(at, FIND_NEXT, &info, sizeof(info)))) break;
        if (info.end <= info.start || info.end <= at || (uint64_t)info.end > direct) { rc = -1; break; }
        allocated += (uint64_t)(info.end - (info.start > at ? info.start : at));
        at = info.end;
        found++;
    }
    if (!found) return rc ? rc : -1;   /* a running game has allocations: the query doesn't work */
    if (sceKernelConfiguredFlexibleMemorySize(&configured) || sceKernelAvailableFlexibleMemorySize(&available))
        configured = available = 0;
    *used = allocated + (configured > available ? configured - available : 0);
    *total = direct + configured;
    return 0;
}

/* ---- the on-screen keyboard -------------------------------------------------------------- */

/* libSceImeDialog's parameters and result (SceImeDialogParam, SceImeDialogResult): the
 * user, the keyboard's kind, languages, Enter's label, the input method, a filter, the
 * options, the longest text and its buffer (UTF-16), where the panel goes on a 1920x1080
 * screen and from which of its edges, a placeholder and a title (UTF-16). */
typedef struct ImeDialogParam {
    int32_t user_id, type;
    uint64_t languages;
    int32_t enter_label, input_method;
    void *filter;
    uint32_t option, max_text_length;
    uint16_t *text;
    float x, y;
    int32_t horizontal, vertical;
    const uint16_t *placeholder, *title;
    int8_t reserved[16];
} ImeDialogParam;
_Static_assert(sizeof(ImeDialogParam) == 96, "SceImeDialogParam is 96 bytes");
typedef struct ImeDialogResult { int32_t end_status; int8_t reserved[12]; } ImeDialogResult;
enum { IME_DIALOG_MODULE = 0x0096, IME_STATUS_RUNNING = 1, IME_STATUS_FINISHED = 2, IME_END_OK = 0,
       IME_CENTER = 1, IME_BOTTOM = 2, COMMON_DIALOG_ALREADY = (int)0x80B80002 };

int sceSysmoduleLoadModule(uint16_t id);
int sceSysmoduleGetModuleHandleInternal(uint32_t id, int32_t *handle);
int sceKernelDlsym(int32_t handle, const char *symbol, void **address);
int sceUserServiceGetInitialUser(int32_t *user);

/* Looked up once, the first time: a fake-signed title may not load a system library by
 * path, so the system loads it as its module, and its functions are looked up in it. The
 * common dialog, which the keyboard needs set up first, is one of the libraries it uses. */
static struct {
    int looked, status;
    int32_t handle;
    int (*init)(const ImeDialogParam *, const void *);
    int (*get_status)(void);
    int (*get_result)(ImeDialogResult *);
    int (*abort)(void);
    int (*term)(void);
    int (*common_init)(void);
} ime;
static void *ime_symbol(const char *name)
{
    void *address = NULL;
    return sceKernelDlsym(ime.handle, name, &address) == 0 ? address : NULL;
}

static int ime_find(void)
{
    if (ime.looked) return ime.init && ime.get_status && ime.get_result && ime.term;
    ime.looked = 1;
    ime.handle = -1;
    ime.status = sceSysmoduleLoadModule(IME_DIALOG_MODULE);
    if (sceSysmoduleGetModuleHandleInternal(IME_DIALOG_MODULE, &ime.handle) || ime.handle < 0)
        (void)sceSysmoduleGetModuleHandleInternal(0x80000000u | IME_DIALOG_MODULE, &ime.handle);
    if (ime.handle >= 0) {
        ime.init = (int (*)(const ImeDialogParam *, const void *))(uintptr_t)ime_symbol("sceImeDialogInit");
        ime.get_status = (int (*)(void))(uintptr_t)ime_symbol("sceImeDialogGetStatus");
        ime.get_result = (int (*)(ImeDialogResult *))(uintptr_t)ime_symbol("sceImeDialogGetResult");
        ime.abort = (int (*)(void))(uintptr_t)ime_symbol("sceImeDialogAbort");
        ime.term = (int (*)(void))(uintptr_t)ime_symbol("sceImeDialogTerm");
        ime.common_init = (int (*)(void))(uintptr_t)ime_symbol("sceCommonDialogInitialize");
    }
    if (ps5_log) {
        char line[160];
        snprintf(line, sizeof(line), "PC5 keyboard module status=%#x handle=%d init=%d status=%d result=%d term=%d common=%d",
                 (unsigned)ime.status, (int)ime.handle, ime.init != NULL, ime.get_status != NULL,
                 ime.get_result != NULL, ime.term != NULL, ime.common_init != NULL);
        ps5_log(line);
    }
    return ime.init && ime.get_status && ime.get_result && ime.term;
}

static int keyboard_open_titled(uint16_t *buffer, uint32_t capacity, const uint16_t *title)
{
    if (!ime_find()) return ime.status < 0 ? ime.status : -1;
    if (ime.common_init) {
        int rc = ime.common_init();
        if (rc && rc != COMMON_DIALOG_ALREADY && ps5_log) {
            char line[64];
            snprintf(line, sizeof(line), "PC5 keyboard common dialog rc=%#x", (unsigned)rc);
            ps5_log(line);
        }
    }
    ImeDialogParam param = { 0 };
    param.user_id = -1;
    (void)sceUserServiceGetInitialUser(&param.user_id);
    param.max_text_length = capacity - 1;
    param.text = buffer;
    param.x = 960.0f;   /* at the bottom, in the middle */
    param.y = 1040.0f;
    param.horizontal = IME_CENTER;
    param.vertical = IME_BOTTOM;
    param.title = title;
    return ime.init(&param, NULL);
}

static int keyboard_open(uint16_t *buffer, uint32_t capacity)
{
    static const uint16_t title[] = u"Type into the game";
    return keyboard_open_titled(buffer, capacity, title);
}

static int keyboard_state(void)
{
    int status = ime.get_status ? ime.get_status() : -1;
    return status == IME_STATUS_RUNNING ? 1 : status == IME_STATUS_FINISHED ? 2 : status < 0 ? status : 0;
}

/* Its result, once finished; one still open is ended first (its end comes through
 * the status, a second at most). */
static int keyboard_close(void)
{
    ImeDialogResult result = { 0 };
    int status = ime.get_status ? ime.get_status() : -1, rc;

    if (status == IME_STATUS_RUNNING && ime.abort) {
        struct timespec tick = { 0, 16000000 };
        (void)ime.abort();
        for (int i = 0; i < 60 && (status = ime.get_status()) == IME_STATUS_RUNNING; i++) nanosleep(&tick, NULL);
    }
    rc = status == IME_STATUS_FINISHED ? ime.get_result(&result) : -1;
    if (ime.term) (void)ime.term();
    return rc < 0 ? rc : result.end_status == IME_END_OK ? 1 : 0;
}

/* libkernel's SceKernelSwVersion: its size first, then the version as text and as a number
 * (0x12400009: 12.40). Room to spare, should the console's be longer. */
typedef struct SwVersion {
    uint64_t size;
    char text[0x1c];
    uint32_t version;
} SwVersion;
int sceKernelGetSystemSwVersion(SwVersion *version);

/* The firmware, from the kernel's version text (kern.version names the release it was built
 * from): sceKernelGetSystemSwVersion answers what a payload that changes the version makes it
 * say (13.59 on a 13.60 console). Its answer is kept beside it, and is the firmware when the
 * kernel's text names none. */
void pc5_system_text(char *out, size_t size)
{
    union { SwVersion sw; uint8_t room[256]; } answer;
    char firmware[16] = "unknown", reported[16] = "", space[32] = "unknown", kernel[1024];
    int mib[2] = { CTL_KERN, KERN_VERSION };
    size_t length = sizeof(kernel) - 1;
    uint64_t free_bytes;

    memset(&answer, 0, sizeof(answer));
    answer.sw.size = sizeof(answer.sw);
    if (!sceKernelGetSystemSwVersion(&answer.sw) && answer.sw.version >> 24)
        snprintf(reported, sizeof(reported), "%x.%02x", (unsigned)(answer.sw.version >> 24),
                 (unsigned)((answer.sw.version >> 16) & 0xff));
    memset(kernel, 0, sizeof(kernel));
    if (sysctl(mib, 2, kernel, &length, NULL, 0) || pc5_firmware_from_kernel(kernel, firmware, sizeof(firmware)))
        pc5_copy(firmware, sizeof(firmware), reported[0] ? reported : "unknown");
    if (!pc5_free_bytes("/data", &free_bytes))
        snprintf(space, sizeof(space), "%.1fGB", (double)free_bytes / 1e9);
    snprintf(out, size, "PC5 system firmware=%s firmware_version=0x%08x%s%s data_free=%s version=%s", firmware,
             (unsigned)answer.sw.version, reported[0] && strcmp(reported, firmware) ? " reported=" : "",
             reported[0] && strcmp(reported, firmware) ? reported : "", space, PC5_VERSION);
}

/* The same keyboard over PC5's own screens (pc5_console.c), with a title of their own. */
int pc5_screens_keyboard_open(uint16_t *buffer, uint32_t capacity, const uint16_t *title);
int pc5_screens_keyboard_state(void);
int pc5_screens_keyboard_close(void);
int pc5_screens_keyboard_open(uint16_t *buffer, uint32_t capacity, const uint16_t *title)
{
    return keyboard_open_titled(buffer, capacity, title);
}
int pc5_screens_keyboard_state(void) { return keyboard_state(); }
int pc5_screens_keyboard_close(void) { return keyboard_close(); }

void pc5_game_ps5_ops(Pc5GameOps *ops, void (*log)(const char *line));
void pc5_game_ps5_ops(Pc5GameOps *ops, void (*log)(const char *line))
{
    char system[160];
    ps5_log = log;
    pc5_system_text(system, sizeof(system));
    if (log) log(system);
    *ops = (Pc5GameOps){ .light_bar = light_bar, .light_bar_reset = light_bar_reset, .triggers = triggers,
                         .cursor = cursor, .cursor_magnify = cursor_magnify, .log = log, .cpu_time_us = cpu_time_us, .cpu_count = cpu_count, .cpu_ticks = cpu_ticks, .thread_times = thread_times,
                         .temperature = temperature, .memory = memory, .keyboard_open = keyboard_open,
                         .keyboard_state = keyboard_state, .keyboard_close = keyboard_close };
}
