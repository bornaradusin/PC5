/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5's loader for a program a game starts (pc5_children.h, pc5_child_wire.h): elfldr runs it
 * as a process of its own, with the title's connection as its standard input and output. It
 * reads the bootstrap, takes the hand-over, loads Wine's ntdll.prx from the title's app0 and
 * runs the program there, connected to the title's Wine server, which knows it as the process
 * the game started. Its main thread then answers the title: a signal for one of its threads,
 * a start of yet another program, the end. When the title's end of the control socket goes,
 * so does this process.
 *
 * Built with the payload SDK as an ELF of its own (tools/build.sh), not into the title. */
#include "../pc5_child_wire.h"
#include "pw_wine_start.h"
#include "prospero_win.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int32_t sceKernelLoadStartModule(const char *path, size_t argc, const void *argv, uint32_t flags,
                                 const void *option, int *result);
int sceKernelGetModuleInfo(int32_t handle, void *info);

enum { WINE_STACK = 16u << 20, MAX_THREADS = 1024 };

static int control = -1;
static pthread_mutex_t send_lock = PTHREAD_MUTEX_INITIALIZER;

/* ---- output: a line each, to the title's log ---- */

static void say(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *format, ...)
{
    char line[512];
    va_list args;
    int n = snprintf(line, sizeof(line), "pc5child: ");
    va_start(args, format);
    n += vsnprintf(line + n, sizeof(line) - (size_t)n - 1, format, args);
    va_end(args);
    if (n > (int)sizeof(line) - 2) n = (int)sizeof(line) - 2;
    line[n++] = '\n';
    (void)!write(1, line, (size_t)n);
}

static void wine_output(const char *text, size_t size)
{
    while (size) {
        ssize_t n = write(1, text, size);
        if (n <= 0) return;
        text += n;
        size -= (size_t)n;
    }
}

/* ---- the control socket ---- */

static int read_all(int fd, void *data, size_t size)
{
    uint8_t *bytes = data;
    while (size) {
        ssize_t n = read(fd, bytes, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { if (!n) errno = ECONNRESET; return -1; }
        bytes += n;
        size -= (size_t)n;
    }
    return 0;
}

static int write_all(int fd, const void *data, size_t size)
{
    const uint8_t *bytes = data;
    while (size) {
        ssize_t n = write(fd, bytes, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        bytes += n;
        size -= (size_t)n;
    }
    return 0;
}

static int send_frame(int fd, const Pc5ChildFrame *frame, const void *payload, int right)
{
    struct iovec iov = { (void *)frame, sizeof(*frame) };
    union { struct cmsghdr header; char space[CMSG_SPACE(sizeof(int))]; } rights;
    struct msghdr message;
    ssize_t n;

    memset(&message, 0, sizeof(message));
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    if (right >= 0) {
        memset(&rights, 0, sizeof(rights));
        message.msg_control = rights.space;
        message.msg_controllen = sizeof(rights.space);
        struct cmsghdr *c = CMSG_FIRSTHDR(&message);
        c->cmsg_level = SOL_SOCKET;
        c->cmsg_type = SCM_RIGHTS;
        c->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(c), &right, sizeof(int));
    }
    do n = sendmsg(fd, &message, 0); while (n < 0 && errno == EINTR);
    if (n < 0) return -1;
    if ((size_t)n < sizeof(*frame) && write_all(fd, (const uint8_t *)frame + n, sizeof(*frame) - (size_t)n)) return -1;
    return frame->size ? write_all(fd, payload, frame->size) : 0;
}

/* A frame, and up to two rights that came with it (-1 for the others). */
static int receive_frame(int fd, Pc5ChildFrame *frame, int rights[2])
{
    struct iovec iov = { frame, sizeof(*frame) };
    union { struct cmsghdr header; char space[CMSG_SPACE(2 * sizeof(int))]; } control_data;
    struct msghdr message;
    ssize_t n;

    rights[0] = rights[1] = -1;
    memset(&message, 0, sizeof(message));
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    message.msg_control = control_data.space;
    message.msg_controllen = sizeof(control_data.space);
    do n = recvmsg(fd, &message, 0); while (n < 0 && errno == EINTR);
    if (n <= 0) { if (!n) errno = ECONNRESET; return -1; }
    for (struct cmsghdr *c = CMSG_FIRSTHDR(&message); c; c = CMSG_NXTHDR(&message, c)) {
        if (c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS) continue;
        int count = (int)((c->cmsg_len - CMSG_LEN(0)) / sizeof(int)), fds[2];
        if (count > 2) count = 2;
        memcpy(fds, CMSG_DATA(c), (size_t)count * sizeof(int));
        for (int i = 0; i < count; i++) {
            if (rights[0] < 0) rights[0] = fds[i];
            else if (rights[1] < 0) rights[1] = fds[i];
            else close(fds[i]);
        }
    }
    if ((size_t)n < sizeof(*frame) && read_all(fd, (uint8_t *)frame + n, sizeof(*frame) - (size_t)n)) return -1;
    if (!pc5_child_frame_valid(frame)) { errno = EPROTO; return -1; }
    /* No frame to this loader carries a payload. */
    if (frame->size) { errno = EPROTO; return -1; }
    return 0;
}

static void failed(int step, long long status)
{
    Pc5ChildFrame frame;
    say("failed at %s, status %lld", pc5_child_step_name(step), status);
    if (control >= 0) {
        pc5_child_frame(&frame, PC5_CHILD_FAILED, step, status, 0);
        pthread_mutex_lock(&send_lock);
        (void)send_frame(control, &frame, NULL, -1);
        pthread_mutex_unlock(&send_lock);
    }
    _exit(2);
}

/* ---- Wine's threads, by kernel id: the title's server signals them through here ---- */

static struct { long tid; pthread_t thread; } threads[MAX_THREADS];
static pthread_mutex_t threads_lock = PTHREAD_MUTEX_INITIALIZER;

static int thread_register(long tid, pthread_t thread)
{
    int status = -1;
    pthread_mutex_lock(&threads_lock);
    for (unsigned i = 0; i < MAX_THREADS; i++)
        if (!threads[i].tid || threads[i].tid == tid) {
            threads[i].tid = tid;
            threads[i].thread = thread;
            status = 0;
            break;
        }
    pthread_mutex_unlock(&threads_lock);
    return status;
}

static void thread_unregister(long tid)
{
    pthread_mutex_lock(&threads_lock);
    for (unsigned i = 0; i < MAX_THREADS; i++)
        if (threads[i].tid == tid) threads[i].tid = 0;
    pthread_mutex_unlock(&threads_lock);
}

static void thread_signal(long tid, int signal)
{
    int status = ESRCH;
    pthread_mutex_lock(&threads_lock);
    for (unsigned i = 0; i < MAX_THREADS; i++)
        if (threads[i].tid == tid) { status = pthread_kill(threads[i].thread, signal); break; }
    pthread_mutex_unlock(&threads_lock);
    if (status) say("signal %d for thread %ld: %s", signal, tid, strerror(status));
}

/* ---- a program this one starts: the title starts it ---- */

static pthread_mutex_t spawn_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t spawn_answered = PTHREAD_COND_INITIALIZER;
static int spawn_waiting, spawn_done, spawn_error;
static long long spawn_pid;

static int spawn(const char *const *argv, const char *const *envp, int server_fd)
{
    static uint8_t payload[PC5_CHILD_MAX_FRAME];
    Pc5ChildFrame frame;
    size_t size;
    int error;

    if (!(size = pc5_child_args_encode(payload, sizeof(payload), argv, envp))) { errno = E2BIG; return -1; }
    pthread_mutex_lock(&spawn_lock);
    while (spawn_waiting) pthread_cond_wait(&spawn_answered, &spawn_lock);   /* one at a time */
    spawn_waiting = 1;
    spawn_done = 0;
    pc5_child_frame(&frame, PC5_CHILD_SPAWN, 0, 0, (uint32_t)size);
    pthread_mutex_lock(&send_lock);
    error = send_frame(control, &frame, payload, server_fd) ? errno : 0;
    pthread_mutex_unlock(&send_lock);
    /* The descriptor stays open until the title answers: it answers once the new process
     * holds it (pc5_child_wire.h). */
    while (!error && !spawn_done) pthread_cond_wait(&spawn_answered, &spawn_lock);
    if (!error) error = spawn_error;
    long long pid = spawn_pid;
    spawn_waiting = 0;
    pthread_cond_broadcast(&spawn_answered);
    pthread_mutex_unlock(&spawn_lock);
    if (error) { errno = error; return -1; }
    return (int)pid;
}

/* ---- the start ---- */

static int set_env(const char *name, const char *value) { return setenv(name, value, 1); }

struct wine_thread { void (*entry)(int, char **); int argc; char **argv; };

static void *run_wine(void *opaque)
{
    struct wine_thread *w = opaque;
    w->entry(w->argc, w->argv);   /* doesn't return: the program's end is exit() */
    return NULL;
}

static int no_thread(void (*entry)(void *), void *arg, size_t stack) { (void)entry; (void)arg; (void)stack; return -1; }

int main(void)
{
    static Pc5ChildBoot boot;
    Pc5ChildBootHeader header;
    Pc5ChildFrame frame;
    struct sockaddr_un address;
    char *payload, ntdll_dir[512], libc[600];
    int link, rights[2], server_fd, null;
    uint32_t size;

    signal(SIGPIPE, SIG_IGN);
    if (read_all(0, &header, sizeof(header)) || !(size = pc5_child_boot_payload_size(&header)) ||
        !(payload = malloc(size)) || read_all(0, payload, size) || pc5_child_boot_decode(&header, payload, size, &boot))
        failed(PC5_CHILD_STEP_BOOT, errno);
    /* Standard input is the title's connection: nothing more is read from it. */
    if ((null = open("/dev/null", O_RDONLY)) >= 0) { dup2(null, 0); if (null) close(null); }
    say("pid %d starts %s", (int)getpid(), boot.argv[1] ? boot.argv[1] : boot.argv[0]);

    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    if (strlen(boot.socket_path) >= sizeof(address.sun_path)) failed(PC5_CHILD_STEP_CONNECT, ENAMETOOLONG);
    memcpy(address.sun_path, boot.socket_path, strlen(boot.socket_path) + 1);
    if ((link = socket(AF_UNIX, SOCK_STREAM, 0)) < 0 || connect(link, (struct sockaddr *)&address, sizeof(address)))
        failed(PC5_CHILD_STEP_CONNECT, errno);
    pc5_child_frame(&frame, PC5_CHILD_HELLO, (int32_t)getpid(), 0, PC5_CHILD_TOKEN);
    if (send_frame(link, &frame, boot.token, -1) || receive_frame(link, &frame, rights) ||
        frame.kind != PC5_CHILD_HANDOVER || rights[0] < 0 || rights[1] < 0)
        failed(PC5_CHILD_STEP_HANDOVER, errno);
    server_fd = rights[0];
    control = rights[1];
    pc5_child_frame(&frame, PC5_CHILD_HELD, (int32_t)getpid(), 0, 0);
    if (send_frame(link, &frame, NULL, -1)) failed(PC5_CHILD_STEP_HANDOVER, errno);
    close(link);

    /* The program's environment is the title's, as Wine gave it, and the server's socket. */
    for (uint32_t i = 0; i < boot.envc; i++) {
        char *pair = strdup(boot.envp[i]), *eq = pair ? strchr(pair, '=') : NULL;
        if (!eq || eq == pair) { free(pair); continue; }
        *eq = 0;
        (void)setenv(pair, eq + 1, 1);
        free(pair);
    }
    {
        char fd_text[16];
        snprintf(fd_text, sizeof(fd_text), "%d", server_fd);
        setenv("WINESERVERSOCKET", fd_text, 1);
    }

    /* Wine's ntdll.prx, as the title loads it; the title's own libc module first when the
     * console wants it loaded for ntdll's sake. */
    static PwWineStart start;
    static const PwWineStartOps ops = { sceKernelLoadStartModule, sceKernelGetModuleInfo, set_env, no_thread };
    PwWineStartConfig config;
    memset(&config, 0, sizeof(config));
    snprintf(ntdll_dir, sizeof(ntdll_dir), "%s", boot.ntdll);
    if (strrchr(ntdll_dir, '/')) *strrchr(ntdll_dir, '/') = 0;
    config.ntdll_path = boot.ntdll;
    config.ntdll_dir = ntdll_dir;
    config.prefix = getenv("WINEPREFIX");
    int status = pw_wine_start_load(&start, &config, &ops);
    if (status != PW_OK && start.stage == PW_WINE_START_LOAD) {
        int result = 0;
        snprintf(libc, sizeof(libc), "%s/sce_module/libc.prx", boot.app0);
        int32_t module = sceKernelLoadStartModule(libc, 0, NULL, 0, NULL, &result);
        say("ntdll.prx didn't load (0x%x): %s loaded as 0x%x", (unsigned)start.module, libc, (unsigned)module);
        status = pw_wine_start_load(&start, &config, &ops);
    }
    say("ntdll load status=%d stage=%d module=0x%x", status, start.stage, (unsigned)start.module);
    if (status != PW_OK) failed(PC5_CHILD_STEP_NTDLL, start.module < 0 ? start.module : status);

    {
        void (*set_sink)(void (*)(const char *, size_t)) =
            (void (*)(void (*)(const char *, size_t)))(uintptr_t)pw_prx_lookup(start.descriptor, "__wine_ps5_set_output_sink");
        void (*set_registry)(int (*)(long, pthread_t), void (*)(long)) =
            (void (*)(int (*)(long, pthread_t), void (*)(long)))(uintptr_t)pw_prx_lookup(start.descriptor,
                                                                                           "__wine_ps5_set_thread_registry");
        void (*set_spawn)(int (*)(const char *const *, const char *const *, int)) =
            (void (*)(int (*)(const char *const *, const char *const *, int)))(uintptr_t)pw_prx_lookup(start.descriptor,
                                                                                                       "__wine_ps5_set_spawn");
        if (set_sink) set_sink(wine_output);
        if (set_registry) set_registry(thread_register, thread_unregister);
        if (set_spawn) set_spawn(spawn);
        say("hooks output=%d threads=%d spawn=%d", set_sink != NULL, set_registry != NULL, set_spawn != NULL);
        if (!set_registry) failed(PC5_CHILD_STEP_NTDLL, -1);   /* an ntdll from before: no signals */
    }
    if (!config.prefix || (status = pw_wine_start_environment(&start, &config, &ops)) != PW_OK)
        failed(PC5_CHILD_STEP_WINE, status);

    {
        static struct wine_thread w;
        pthread_attr_t attr;
        pthread_t thread;
        w.entry = start.wine_main;
        w.argc = (int)boot.argc;
        w.argv = (char **)boot.argv;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, WINE_STACK);
        status = pthread_create(&thread, &attr, run_wine, &w);
        pthread_attr_destroy(&attr);
        if (status) failed(PC5_CHILD_STEP_WINE, status);
    }
    pc5_child_frame(&frame, PC5_CHILD_READY, 0, 0, 0);
    pthread_mutex_lock(&send_lock);
    (void)send_frame(control, &frame, NULL, -1);
    pthread_mutex_unlock(&send_lock);

    /* The title's frames, until it ends. */
    for (;;) {
        if (receive_frame(control, &frame, rights)) {
            say("the title is gone (%s): the program ends", strerror(errno));
            _exit(1);
        }
        for (int i = 0; i < 2; i++) if (rights[i] >= 0) close(rights[i]);
        switch (frame.kind) {
        case PC5_CHILD_SIGNAL: thread_signal((long)frame.extra, frame.value); break;
        case PC5_CHILD_STOP: say("the title ends the program"); _exit(1);
        case PC5_CHILD_SPAWNED:
            pthread_mutex_lock(&spawn_lock);
            spawn_error = frame.value;
            spawn_pid = frame.extra;
            spawn_done = 1;
            pthread_cond_broadcast(&spawn_answered);
            pthread_mutex_unlock(&spawn_lock);
            break;
        default: break;
        }
    }
}
