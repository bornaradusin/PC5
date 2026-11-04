/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: programs a game starts, each in a process of its own (pc5_children.h). */
#include "pc5_children.h"
#include "pc5_child_wire.h"
#include "ps5log/ps5log_ps5_net.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

extern int sceNetRecv(int socket, void *data, size_t length, int flags);
extern int *sceNetErrnoLoc(void);

enum {
    ELFLDR_PORT = 9021,
    MAX_LOADER = 4 * 1024 * 1024,
    MAX_RUNNING = 8,
    MAX_STARTS = 64,                 /* in one run of the title */
    CONNECT_MS = 20000,              /* elfldr starting the loader, and it connecting back */
    REPLY_MS = 10000,
    READY_S = 60,                    /* Wine loaded */
    HIDDEN_S = 20 * 60,              /* a hidden program can't be answered: one that waits for a click ends */
    STOP_MS = 2000,
};

typedef struct Child {
    int used, ended, ready;
    int threads;         /* its output and control threads still running */
    pid_t pid;
    int stream;          /* elfldr's connection: the loader's output */
    int control;
    time_t started;
    char name[64];
    pthread_mutex_t send_lock;
} Child;

static Pc5ChildrenConfig config;
static uint8_t *loader;
static size_t loader_size;
static int ready;
static unsigned starts;
static Child children[MAX_RUNNING];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;     /* children[] */
static pthread_mutex_t spawn_lock = PTHREAD_MUTEX_INITIALIZER;   /* one start at a time */

static void say(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *format, ...)
{
    char line[600];
    va_list args;
    if (!config.log) return;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    config.log(line);
}

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

/* The program's name, from Wine's argv: its last path part. */
static void program_name(const char *const *argv, char *out, size_t size)
{
    const char *path = argv[1] ? argv[1] : argv[0], *p;
    for (p = path; *path; path++)
        if (*path == '\\' || *path == '/') p = path + 1;
    snprintf(out, size, "%s", *p ? p : "program");
}

int pc5_children_init(const Pc5ChildrenConfig *c)
{
    struct stat st;
    int fd;
    size_t got = 0;

    if (ready) return 0;
    if (!c || !c->loader || !c->app0 || !c->ntdll || !c->run_dir) return -1;
    config = *c;
    if ((mkdir(c->run_dir, 0777) && errno != EEXIST) || stat(c->run_dir, &st) || !S_ISDIR(st.st_mode)) {
        say("PC5 programs: no folder %s (%s): a game can't start other programs", c->run_dir, strerror(errno));
        return -1;
    }
    if ((fd = open(c->loader, O_RDONLY)) < 0 || fstat(fd, &st) || st.st_size <= 0 || st.st_size > MAX_LOADER ||
        !(loader = malloc((size_t)st.st_size))) {
        say("PC5 programs: no loader %s (%s): a game can't start other programs", c->loader, strerror(errno));
        if (fd >= 0) close(fd);
        return -1;
    }
    while (got < (size_t)st.st_size) {
        ssize_t n = read(fd, loader + got, (size_t)st.st_size - got);
        if (n <= 0) break;
        got += (size_t)n;
    }
    close(fd);
    if (got != (size_t)st.st_size || memcmp(loader, "\177ELF", 4)) {
        say("PC5 programs: %s isn't a whole ELF file", c->loader);
        free(loader);
        loader = NULL;
        return -1;
    }
    loader_size = got;
    for (unsigned i = 0; i < MAX_RUNNING; i++) pthread_mutex_init(&children[i].send_lock, NULL);
    ready = 1;
    say("PC5 programs: ready, loader %zu bytes, ntdll %s", loader_size, c->ntdll);
    return 0;
}

/* ---- sockets ---- */

static int net_result(int result)
{
    if (result < 0) {
        int *e = sceNetErrnoLoc();
        errno = e ? *e : EIO;
        return -1;
    }
    return result;
}

static int net_send_all(int socket, const void *data, size_t size)
{
    const uint8_t *bytes = data;
    while (size) {
        long n = ps5log_ps5_send(socket, bytes, size, 0);
        if (n <= 0) { if (!n) errno = EPIPE; return -1; }
        bytes += n;
        size -= (size_t)n;
    }
    return 0;
}

/* Waits for fd to be readable: 1, 0 on timeout, or -1. */
static int wait_readable(int fd, int ms)
{
    struct pollfd p = { fd, POLLIN, 0 };
    int n;
    do n = poll(&p, 1, ms); while (n < 0 && errno == EINTR);
    return n;
}

static int read_all(int fd, void *data, size_t size, int ms)
{
    uint8_t *bytes = data;
    uint64_t until = now_ms() + (uint64_t)ms;
    while (size) {
        uint64_t now = now_ms();
        int w = now >= until ? 0 : wait_readable(fd, (int)(until - now));
        if (w <= 0) { if (!w) errno = ETIMEDOUT; return -1; }
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

/* A frame and its payload, with up to two descriptors as rights. */
static int send_frame(int fd, const Pc5ChildFrame *frame, const void *payload, const int *fds, int nfds)
{
    struct iovec iov = { (void *)frame, sizeof(*frame) };
    union { struct cmsghdr header; char space[CMSG_SPACE(2 * sizeof(int))]; } control;
    struct msghdr message;
    ssize_t n;

    memset(&message, 0, sizeof(message));
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    if (nfds > 0) {
        memset(&control, 0, sizeof(control));
        message.msg_control = control.space;
        message.msg_controllen = CMSG_SPACE((size_t)nfds * sizeof(int));
        struct cmsghdr *c = CMSG_FIRSTHDR(&message);
        c->cmsg_level = SOL_SOCKET;
        c->cmsg_type = SCM_RIGHTS;
        c->cmsg_len = CMSG_LEN((size_t)nfds * sizeof(int));
        memcpy(CMSG_DATA(c), fds, (size_t)nfds * sizeof(int));
    }
    do n = sendmsg(fd, &message, 0); while (n < 0 && errno == EINTR);
    if (n < 0) return -1;
    if ((size_t)n < sizeof(*frame) && write_all(fd, (const uint8_t *)frame + n, sizeof(*frame) - (size_t)n)) return -1;
    return frame->size ? write_all(fd, payload, frame->size) : 0;
}

/* A frame (and the one descriptor that may come with it, else -1); its payload in a buffer
 * the caller frees. */
static int receive_frame(int fd, Pc5ChildFrame *frame, uint8_t **payload, int *right, int ms)
{
    struct iovec iov = { frame, sizeof(*frame) };
    union { struct cmsghdr header; char space[CMSG_SPACE(2 * sizeof(int))]; } control;
    struct msghdr message;
    ssize_t n;

    *right = -1;
    if (payload) *payload = NULL;
    if (ms >= 0) {
        int w = wait_readable(fd, ms);
        if (w <= 0) { if (!w) errno = ETIMEDOUT; return -1; }
    }
    memset(&message, 0, sizeof(message));
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    message.msg_control = control.space;
    message.msg_controllen = sizeof(control.space);
    do n = recvmsg(fd, &message, 0); while (n < 0 && errno == EINTR);
    if (n <= 0) { if (!n) errno = ECONNRESET; return -1; }
    for (struct cmsghdr *c = CMSG_FIRSTHDR(&message); c; c = CMSG_NXTHDR(&message, c)) {
        if (c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS) continue;
        int count = (int)((c->cmsg_len - CMSG_LEN(0)) / sizeof(int)), fds[2];
        if (count > 2) count = 2;
        memcpy(fds, CMSG_DATA(c), (size_t)count * sizeof(int));
        for (int i = 0; i < count; i++) {
            if (*right < 0) *right = fds[i];
            else close(fds[i]);
        }
    }
    if ((size_t)n < sizeof(*frame) && read_all(fd, (uint8_t *)frame + n, sizeof(*frame) - (size_t)n, REPLY_MS))
        goto fail;
    if (!pc5_child_frame_valid(frame)) { errno = EPROTO; goto fail; }
    if (frame->size) {
        uint8_t *data = malloc(frame->size);
        if (!data || read_all(fd, data, frame->size, REPLY_MS) || !payload) { free(data); if (!payload) errno = EPROTO; goto fail; }
        *payload = data;
    }
    return 0;
fail:
    if (*right >= 0) { close(*right); *right = -1; }
    return -1;
}

/* ---- the started programs ---- */

static void ended(Child *child, const char *how)
{
    pthread_mutex_lock(&lock);
    if (child->used && !child->ended) {
        child->ended = 1;
        say("PC5 programs: %s (pid %d) %s after %lds", child->name, (int)child->pid, how,
            (long)(time(NULL) - child->started));
    }
    pthread_mutex_unlock(&lock);
}

/* The loader's output, a line at a time, into the title's log; its end is the program's. */
static void *read_output(void *arg)
{
    Child *child = arg;
    char line[512];
    size_t used = 0;
    for (;;) {
        char buffer[1024];
        int n = net_result(sceNetRecv(child->stream, buffer, sizeof(buffer), 0));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        for (int i = 0; i < n; i++) {
            if (buffer[i] == '\n' || used + 1 == sizeof(line)) {
                line[used] = 0;
                if (used) say("PC5 program %d: %s", (int)child->pid, line);
                used = 0;
                if (buffer[i] == '\n') continue;
            }
            if (buffer[i] != '\r') line[used++] = buffer[i];
        }
    }
    if (used) { line[used] = 0; say("PC5 program %d: %s", (int)child->pid, line); }
    ended(child, "ended");
    pthread_mutex_lock(&lock);
    child->threads--;
    pthread_mutex_unlock(&lock);
    return NULL;
}

/* The loader's frames: Wine loaded, a program it starts in turn, a step that failed. */
static void *read_control(void *arg)
{
    Child *child = arg;
    for (;;) {
        Pc5ChildFrame frame, reply;
        uint8_t *payload;
        int right;
        if (receive_frame(child->control, &frame, &payload, &right, -1)) break;
        if (frame.kind == PC5_CHILD_READY) {
            child->ready = 1;
            say("PC5 programs: %s (pid %d) runs", child->name, (int)child->pid);
        } else if (frame.kind == PC5_CHILD_FAILED) {
            say("PC5 programs: %s (pid %d) failed at %s, status %lld", child->name, (int)child->pid,
                pc5_child_step_name(frame.value), (long long)frame.extra);
        } else if (frame.kind == PC5_CHILD_SPAWN) {
            const char *argv[PC5_CHILD_MAX_ARGS + 1], *envp[PC5_CHILD_MAX_ENV + 1];
            int pid = -1, error = EPROTO;
            if (right >= 0 && payload &&
                !pc5_child_args_decode(payload, frame.size, argv, PC5_CHILD_MAX_ARGS, envp, PC5_CHILD_MAX_ENV)) {
                pid = pc5_children_spawn(argv, envp, right);
                error = pid < 0 ? errno : 0;
            }
            pc5_child_frame(&reply, PC5_CHILD_SPAWNED, error, pid, 0);
            pthread_mutex_lock(&child->send_lock);
            (void)send_frame(child->control, &reply, NULL, NULL, 0);
            pthread_mutex_unlock(&child->send_lock);
        }
        free(payload);
        if (right >= 0) close(right);
    }
    pthread_mutex_lock(&lock);
    child->threads--;
    pthread_mutex_unlock(&lock);
    return NULL;
}

/* Every second: a program whose Wine didn't load, or a hidden one that hasn't ended. */
static void *supervise(void *arg)
{
    (void)arg;
    for (;;) {
        sleep(1);
        time_t now = time(NULL);
        pthread_mutex_lock(&lock);
        for (unsigned i = 0; i < MAX_RUNNING; i++) {
            Child *c = &children[i];
            const char *why = NULL;
            if (!c->used || c->ended) continue;
            if (!c->ready && now - c->started > READY_S) why = "Wine didn't load";
            else if (now - c->started > HIDDEN_S) why = "still running hidden";
            if (!why) continue;
            say("PC5 programs: %s (pid %d) ends: %s", c->name, (int)c->pid, why);
            Pc5ChildFrame stop;
            pc5_child_frame(&stop, PC5_CHILD_STOP, 0, 0, 0);
            pthread_mutex_lock(&c->send_lock);
            (void)send_frame(c->control, &stop, NULL, NULL, 0);
            pthread_mutex_unlock(&c->send_lock);
            (void)kill(c->pid, SIGKILL);
            c->started = now + 3600;   /* once */
        }
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

/* A free slot (the ended programs' given back): it, or NULL. */
static Child *take_slot(void)
{
    static int supervised;
    Child *slot = NULL;
    pthread_mutex_lock(&lock);
    for (unsigned i = 0; i < MAX_RUNNING && !slot; i++) {
        Child *c = &children[i];
        if (c->used && c->ended && !c->threads) {
            /* its threads are done with it: output ended, and the control socket with it */
            if (c->stream >= 0) ps5log_ps5_close(c->stream);
            if (c->control >= 0) close(c->control);
            c->used = 0;
        }
        if (!c->used) slot = c;
    }
    if (slot) {
        slot->used = 1;
        slot->ended = slot->ready = slot->threads = 0;
        slot->pid = 0;
        slot->stream = slot->control = -1;
    }
    if (!supervised) {
        pthread_t thread;
        supervised = !pthread_create(&thread, NULL, supervise, NULL);
        if (supervised) pthread_detach(thread);
    }
    pthread_mutex_unlock(&lock);
    return slot;
}

static void make_token(uint8_t token[PC5_CHILD_TOKEN])
{
    int fd = open("/dev/urandom", O_RDONLY);
    ssize_t got = fd >= 0 ? read(fd, token, PC5_CHILD_TOKEN) : -1;
    if (fd >= 0) close(fd);
    if (got != PC5_CHILD_TOKEN) {
        uint64_t x = now_ms() ^ ((uint64_t)getpid() << 32) ^ ((uint64_t)starts * 0x9e3779b97f4a7c15u);
        for (unsigned i = 0; i < PC5_CHILD_TOKEN; i++) {
            x ^= x << 13; x ^= x >> 7; x ^= x << 17;
            token[i] = (uint8_t)x;
        }
    }
}

static int connect_elfldr(void)
{
    struct { uint8_t length, family; uint16_t port; uint32_t address; uint16_t vport; uint8_t zero[6]; } address;
    uint32_t timeout = CONNECT_MS * 1000u;
    int s = ps5log_ps5_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0) return -1;
    (void)ps5log_ps5_setsockopt(s, 0xffff, 0x1105, &timeout, sizeof(timeout));   /* send timeout */
    memset(&address, 0, sizeof(address));
    address.length = sizeof(address);
    address.family = AF_INET;
    address.port = (uint16_t)((ELFLDR_PORT << 8) | (ELFLDR_PORT >> 8));
    address.address = UINT32_C(0x0100007f);
    if (ps5log_ps5_connect(s, (const struct sockaddr *)&address, sizeof(address)) < 0) {
        int e = errno;
        ps5log_ps5_close(s);
        errno = e;
        return -1;
    }
    return s;
}

int pc5_children_spawn(const char *const *argv, const char *const *envp, int server_fd)
{
    static uint8_t boot[PC5_CHILD_MAX_BOOT + sizeof(Pc5ChildBootHeader)];
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)], name[64];
    uint8_t token[PC5_CHILD_TOKEN];
    struct sockaddr_un address;
    Pc5ChildFrame frame;
    uint8_t *payload = NULL;
    int listener = -1, link = -1, stream = -1, pair[2] = { -1, -1 }, right = -1, error = 0;
    size_t boot_size;
    Child *child = NULL;
    const char *step = "";

    if (!ready) { errno = ENOSYS; return -1; }
    if (!argv || !argv[0] || server_fd < 0) { errno = EINVAL; return -1; }
    program_name(argv, name, sizeof(name));
    pthread_mutex_lock(&spawn_lock);
    if (starts >= MAX_STARTS) { error = EAGAIN; step = "too many starts in this run"; goto done; }
    if (!(child = take_slot())) { error = EAGAIN; step = "8 programs already running"; goto done; }
    starts++;
    make_token(token);
    snprintf(path, sizeof(path), "%s/c%d-%u.sock", config.run_dir, (int)getpid(), starts);
    boot_size = pc5_child_boot_encode(boot, sizeof(boot), token, 0, path, config.app0, config.ntdll, argv, envp);
    if (!boot_size) { error = E2BIG; step = "its start doesn't fit"; goto done; }

    /* The socket the loader connects back to, first: elfldr may start it at once. */
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, path, strlen(path) + 1);
    (void)unlink(path);
    if ((listener = socket(AF_UNIX, SOCK_STREAM, 0)) < 0 ||
        bind(listener, (struct sockaddr *)&address, sizeof(address)) || listen(listener, 1)) {
        error = errno; step = "no hand-over socket"; goto done;
    }
    (void)chmod(path, 0777);
    if ((stream = connect_elfldr()) < 0) { error = errno; step = "no elfldr on port 9021"; goto done; }
    if (net_send_all(stream, loader, loader_size) || net_send_all(stream, boot, boot_size)) {
        error = errno; step = "elfldr didn't take the loader"; goto done;
    }
    {
        int w = wait_readable(listener, CONNECT_MS);
        if (w <= 0) { error = w ? errno : ETIMEDOUT; step = "the loader didn't connect back"; goto done; }
    }
    if ((link = accept(listener, NULL, NULL)) < 0) { error = errno; step = "accept"; goto done; }
    errno = 0;
    if (receive_frame(link, &frame, &payload, &right, REPLY_MS) || frame.kind != PC5_CHILD_HELLO ||
        frame.size != PC5_CHILD_TOKEN || memcmp(payload, token, PC5_CHILD_TOKEN) || frame.value <= 1) {
        error = errno ? errno : EPROTO; step = "the loader's hello"; goto done;
    }
    child->pid = (pid_t)frame.value;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair)) { error = errno; step = "socketpair"; goto done; }
    {
        int fds[2] = { server_fd, pair[1] };
        pc5_child_frame(&frame, PC5_CHILD_HANDOVER, 0, 0, 0);
        if (send_frame(link, &frame, NULL, fds, 2)) { error = errno; step = "the hand-over"; goto done; }
    }
    free(payload);
    payload = NULL;
    /* Only once the loader holds them are the title's copies closed (pc5_child_wire.h). */
    errno = 0;
    if (receive_frame(link, &frame, &payload, &right, REPLY_MS) || frame.kind != PC5_CHILD_HELD ||
        frame.value != child->pid) {
        error = errno ? errno : EPROTO; step = "the loader didn't take the hand-over"; goto done;
    }
    close(pair[1]);
    pair[1] = -1;

    pthread_mutex_lock(&lock);
    child->stream = stream;
    child->control = pair[0];
    child->started = time(NULL);
    snprintf(child->name, sizeof(child->name), "%s", name);
    pthread_mutex_unlock(&lock);
    stream = pair[0] = -1;
    {
        pthread_t a, b;
        pthread_mutex_lock(&lock);
        child->threads = 2;
        pthread_mutex_unlock(&lock);
        if (pthread_create(&a, NULL, read_output, child) == 0) pthread_detach(a);
        else ended(child, "lost (no thread for its output)"), child->threads--;
        if (pthread_create(&b, NULL, read_control, child) == 0) pthread_detach(b);
        else child->threads--;
    }
    say("PC5 programs: %s starts as pid %d (%u running)", name, (int)child->pid, pc5_children_running());

done:
    free(payload);
    if (right >= 0) close(right);
    if (link >= 0) close(link);
    if (listener >= 0) { close(listener); (void)unlink(path); }
    if (pair[0] >= 0) close(pair[0]);
    if (pair[1] >= 0) close(pair[1]);
    if (stream >= 0) ps5log_ps5_close(stream);
    if (error) {
        if (child) {
            pthread_mutex_lock(&lock);
            if (child->pid > 1) (void)kill(child->pid, SIGKILL);
            child->used = 0;
            pthread_mutex_unlock(&lock);
        }
        say("PC5 programs: %s didn't start: %s (%s)", name, step, strerror(error));
    }
    pid_t pid = error ? -1 : child->pid;
    pthread_mutex_unlock(&spawn_lock);
    if (error) errno = error;
    return pid;
}

int pc5_children_signal(pid_t pid, long tid, int signal)
{
    Child *child = NULL;
    Pc5ChildFrame frame;
    int status = -1;

    pthread_mutex_lock(&lock);
    for (unsigned i = 0; i < MAX_RUNNING && !child; i++)
        if (children[i].used && !children[i].ended && children[i].pid == pid && children[i].control >= 0)
            child = &children[i];
    pthread_mutex_unlock(&lock);
    if (!child) { errno = ESRCH; return -1; }
    pc5_child_frame(&frame, PC5_CHILD_SIGNAL, signal, tid, 0);
    pthread_mutex_lock(&child->send_lock);
    status = send_frame(child->control, &frame, NULL, NULL, 0);
    pthread_mutex_unlock(&child->send_lock);
    if (status) errno = ESRCH;
    return status;
}

unsigned pc5_children_running(void)
{
    unsigned n = 0;
    pthread_mutex_lock(&lock);
    for (unsigned i = 0; i < MAX_RUNNING; i++) n += children[i].used && !children[i].ended && children[i].pid > 1;
    pthread_mutex_unlock(&lock);
    return n;
}

void pc5_children_stop(const char *reason)
{
    Pc5ChildFrame frame;
    unsigned running;
    uint64_t until;

    if (!ready || !(running = pc5_children_running())) return;
    say("PC5 programs: %u still running end (%s)", running, reason ? reason : "-");
    pc5_child_frame(&frame, PC5_CHILD_STOP, 0, 0, 0);
    pthread_mutex_lock(&lock);
    for (unsigned i = 0; i < MAX_RUNNING; i++)
        if (children[i].used && !children[i].ended && children[i].control >= 0) {
            pthread_mutex_lock(&children[i].send_lock);
            (void)send_frame(children[i].control, &frame, NULL, NULL, 0);
            pthread_mutex_unlock(&children[i].send_lock);
        }
    pthread_mutex_unlock(&lock);
    until = now_ms() + STOP_MS;
    while (pc5_children_running() && now_ms() < until) usleep(50000);
    pthread_mutex_lock(&lock);
    for (unsigned i = 0; i < MAX_RUNNING; i++)
        if (children[i].used && !children[i].ended && children[i].pid > 1) {
            say("PC5 programs: %s (pid %d) killed", children[i].name, (int)children[i].pid);
            (void)kill(children[i].pid, SIGKILL);
        }
    pthread_mutex_unlock(&lock);
    until = now_ms() + 1000;
    while (pc5_children_running() && now_ms() < until) usleep(50000);
}
