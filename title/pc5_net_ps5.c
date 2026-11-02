/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5's network on the console (pc5_net.h's platform): TCP through libSceNet
 * with prospero-win's ps5log wrappers (already in the title), names through
 * the system's resolver, random bytes from the kernel, and the console's own
 * root certificates. The C library's getaddrinfo is not used: it lives in a
 * module a native title does not load. */
#include "pc5.h"
#include "pc5_net.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <unistd.h>

int sceNetSend(int, const void *, size_t, int);
int sceNetRecv(int, void *, size_t, int);
int *sceNetErrnoLoc(void);
int sceNetPoolCreate(const char *name, int size, int flags);
int sceNetPoolDestroy(int pool);
int sceNetResolverCreate(const char *name, int pool, int flags);
int sceNetResolverStartNtoa(int resolver, const char *hostname, struct in_addr *address, int timeout_us, int retries,
                            int flags);
int sceNetResolverDestroy(int resolver);
/* native/ps5log/ps5log_ps5_net.c */
int ps5log_ps5_socket(int domain, int type, int protocol);
int ps5log_ps5_connect(int socket_id, const struct sockaddr *address, socklen_t length);
int ps5log_ps5_getsockopt(int socket_id, int level, int option, void *value, socklen_t *length);
int ps5log_ps5_close(int socket_id);
int ps5log_ps5_fcntl(int socket_id, int command, ...);
int ps5log_ps5_poll(struct pollfd *descriptors, unsigned long count, int timeout_milliseconds);

static int resolve(const char *host, uint32_t *address)
{
    struct in_addr found;
    int pool = sceNetPoolCreate("pc5-dns", 16 * 1024, 0), status = -1;

    if (pool < 0) return -1;
    int resolver = sceNetResolverCreate("pc5-dns", pool, 0);
    if (resolver >= 0) {
        if (sceNetResolverStartNtoa(resolver, host, &found, 5000000, 2, 0) >= 0) {
            *address = found.s_addr;
            status = 0;
        }
        sceNetResolverDestroy(resolver);
    }
    sceNetPoolDestroy(pool);
    return status;
}

static int wait_for(int sock, short events, int timeout_ms)
{
    struct pollfd fd = { sock, events, 0 };
    return ps5log_ps5_poll(&fd, 1, timeout_ms) > 0 && (fd.revents & (events | POLLHUP | POLLERR)) ? 0 : -1;
}

static int connect_to(uint32_t address, uint16_t port, int timeout_ms)
{
    struct sockaddr_in to;
    int sock = ps5log_ps5_socket(AF_INET, SOCK_STREAM, 0), error = 0;
    socklen_t length = sizeof(error);

    if (sock < 0) return -1;
    memset(&to, 0, sizeof(to));
    to.sin_len = sizeof(to);
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    to.sin_addr.s_addr = address;
    /* Without blocking, so every wait has its limit. */
    if (ps5log_ps5_fcntl(sock, F_SETFL, O_NONBLOCK) < 0) goto fail;
    if (ps5log_ps5_connect(sock, (const struct sockaddr *)&to, sizeof(to)) < 0) {
        if (errno != EINPROGRESS && errno != EAGAIN) goto fail;
        if (wait_for(sock, POLLOUT, timeout_ms)) goto fail;
        if (ps5log_ps5_getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &length) < 0 || error) goto fail;
    }
    return sock;
fail:
    ps5log_ps5_close(sock);
    return -1;
}

static int net_errno(void)
{
    int *code = sceNetErrnoLoc();
    return code ? *code : 0;
}

static int send_some(int sock, const void *data, size_t size, int timeout_ms)
{
    for (;;) {
        int sent = sceNetSend(sock, data, size, 0);
        if (sent >= 0) return sent;
        if (net_errno() != EAGAIN || wait_for(sock, POLLOUT, timeout_ms)) return -1;
    }
}

static int recv_some(int sock, void *data, size_t size, int timeout_ms)
{
    for (;;) {
        int got = sceNetRecv(sock, data, size, 0);
        if (got >= 0) return got;
        if (net_errno() != EAGAIN || wait_for(sock, POLLIN, timeout_ms)) return -1;
    }
}

static void close_socket(int sock)
{
    if (sock >= 0) ps5log_ps5_close(sock);
}

/* The kernel's generator (kern.arandom), 256 bytes a call; the CPU's RDRAND
 * when that is refused. */
static int random_bytes(void *data, size_t size)
{
    uint8_t *out = data;
    size_t done = 0;

    while (done < size) {
        int mib[2] = { CTL_KERN, KERN_ARND };
        size_t want = size - done > 256 ? 256 : size - done, got = want;
        if (sysctl(mib, 2, out + done, &got, NULL, 0) || !got) break;
        done += got;
    }
    while (done < size) {
        unsigned long long value;
        unsigned char ok = 0;
        for (int tries = 0; tries < 10 && !ok; tries++)
            __asm__ volatile("rdrand %0; setc %1" : "=r"(value), "=qm"(ok));
        if (!ok || value == ~0ull) return -1;
        size_t take = size - done < 8 ? size - done : 8;
        memcpy(out + done, &value, take);
        done += take;
    }
    return 0;
}

void pc5_net_ps5_init(const char *app, void (*log)(const char *line));
void pc5_net_ps5_init(const char *app, void (*log)(const char *line))
{
    extern void pc5_web_random_init(int (*random_bytes)(void *, size_t));
    pc5_web_random_init(random_bytes);
    /* The console's own roots (the title sees the real root with its /data
     * grant), then the ones the app carries: Mozilla's, as Debian ships them. */
    static char bundle[PC5_PATH];
    static const char *roots[] = { "/system/common/cert/CA_LIST.cer", bundle, NULL };
    snprintf(bundle, sizeof(bundle), "%s/pc5/ca-certificates.crt", app);
    const Pc5NetOps ops = { resolve, connect_to, send_some, recv_some, close_socket, random_bytes, roots, log };
    pc5_net_init(&ops);
}
