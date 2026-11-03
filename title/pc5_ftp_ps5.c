/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5's FTP server's and browser page's platform on the console (pc5_ftp.h, pc5_web.h): TCP
 * through libSceNet, as pc5_net_ps5.c connects, with prospero-win's ps5log wrappers for waiting. */
#include "pc5_ftp.h"
#include "pc5_web.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>

int sceNetSocket(const char *name, int domain, int type, int protocol);
int sceNetBind(int socket, const struct sockaddr *address, socklen_t length);
int sceNetListen(int socket, int backlog);
int sceNetAccept(int socket, struct sockaddr *address, socklen_t *length);
int sceNetConnect(int socket, const struct sockaddr *address, socklen_t length);
int sceNetGetsockname(int socket, struct sockaddr *address, socklen_t *length);
int sceNetSetsockopt(int socket, int level, int option, const void *value, socklen_t length);
int sceNetSend(int, const void *, size_t, int);
int sceNetRecv(int, void *, size_t, int);
int *sceNetErrnoLoc(void);
/* native/ps5log/ps5log_ps5_net.c */
int ps5log_ps5_close(int socket_id);
int ps5log_ps5_fcntl(int socket_id, int command, ...);
int ps5log_ps5_poll(struct pollfd *descriptors, unsigned long count, int timeout_milliseconds);

static int net_errno(void)
{
    int *code = sceNetErrnoLoc();
    return code ? *code : 0;
}

/* 0 when ready, -2 when the time ran out, -1 on an error. */
static int wait_for(int sock, short events, int timeout_ms)
{
    struct pollfd fd = { sock, events, 0 };
    int n = ps5log_ps5_poll(&fd, 1, timeout_ms);
    if (n < 0) return -1;
    if (n == 0) return -2;
    return (fd.revents & (events | POLLHUP | POLLERR)) ? 0 : -2;
}

static int ps5_listen(uint16_t port, int backlog)
{
    struct sockaddr_in at;
    int sock = sceNetSocket("pc5-ftp", AF_INET, SOCK_STREAM, 0), on = 1;

    if (sock < 0) return -1;
    sceNetSetsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    /* Large windows, which the connections it takes get too, so a game sent from a PC isn't
     * held back by the console's small default ones: 4 MB, or when the console refuses that,
     * half as much each time, down to 256 KB (the default kept when it takes none). */
    for (int size = 4 << 20; size >= 256 << 10; size /= 2)
        if (sceNetSetsockopt(sock, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)) >= 0) break;
    for (int size = 4 << 20; size >= 256 << 10; size /= 2)
        if (sceNetSetsockopt(sock, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)) >= 0) break;
    memset(&at, 0, sizeof(at));
    at.sin_len = sizeof(at);
    at.sin_family = AF_INET;
    at.sin_port = htons(port);
    at.sin_addr.s_addr = htonl(INADDR_ANY);
    if (sceNetBind(sock, (const struct sockaddr *)&at, sizeof(at)) < 0 || sceNetListen(sock, backlog) < 0 ||
        ps5log_ps5_fcntl(sock, F_SETFL, O_NONBLOCK) < 0) {
        ps5log_ps5_close(sock);
        return -1;
    }
    return sock;
}

static int ps5_accept(int sock, int timeout_ms)
{
    if (wait_for(sock, POLLIN, timeout_ms)) return -1;
    int client = sceNetAccept(sock, NULL, NULL);
    if (client < 0) return -1;
    /* Without blocking, so every wait has its limit. */
    if (ps5log_ps5_fcntl(client, F_SETFL, O_NONBLOCK) < 0) {
        ps5log_ps5_close(client);
        return -1;
    }
    return client;
}

static int ps5_address(int sock, uint32_t *ip, uint16_t *port)
{
    struct sockaddr_in at;
    socklen_t length = sizeof(at);
    if (sceNetGetsockname(sock, (struct sockaddr *)&at, &length) < 0) return -1;
    *ip = at.sin_addr.s_addr;
    *port = ntohs(at.sin_port);
    return 0;
}

/* The address the console reaches its network from: a UDP socket "connected" towards the
 * internet (nothing is sent), and its own address read back. */
static int ps5_lan_ip(uint32_t *ip)
{
    struct sockaddr_in to;
    uint16_t port;
    int sock = sceNetSocket("pc5-ftp-ip", AF_INET, SOCK_DGRAM, 0), status = -1;

    if (sock < 0) return -1;
    memset(&to, 0, sizeof(to));
    to.sin_len = sizeof(to);
    to.sin_family = AF_INET;
    to.sin_port = htons(53);
    to.sin_addr.s_addr = htonl(0x08080808);
    if (sceNetConnect(sock, (const struct sockaddr *)&to, sizeof(to)) >= 0 && !ps5_address(sock, ip, &port) && *ip)
        status = 0;
    ps5log_ps5_close(sock);
    return status;
}

static int ps5_send(int sock, const void *data, size_t size, int timeout_ms)
{
    for (;;) {
        int sent = sceNetSend(sock, data, size, 0);
        if (sent >= 0) return sent;
        if (net_errno() != EAGAIN) return -1;
        int waited = wait_for(sock, POLLOUT, timeout_ms);
        if (waited) return waited;
    }
}

static int ps5_recv(int sock, void *data, size_t size, int timeout_ms)
{
    for (;;) {
        int got = sceNetRecv(sock, data, size, 0);
        if (got >= 0) return got;
        if (net_errno() != EAGAIN) return -1;
        int waited = wait_for(sock, POLLIN, timeout_ms);
        if (waited) return waited;
    }
}

static void ps5_close(int sock)
{
    if (sock >= 0) ps5log_ps5_close(sock);
}

void pc5_ftp_ps5_init(void (*log)(const char *line));
void pc5_ftp_ps5_init(void (*log)(const char *line))
{
    const Pc5FtpOps ops = { ps5_listen, ps5_accept, ps5_address, ps5_lan_ip, ps5_send, ps5_recv, ps5_close, log };
    pc5_ftp_init(&ops);
    pc5_web_init(&ops);
}
