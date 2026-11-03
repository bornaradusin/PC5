/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5's FTP server's and browser page's platform on the host, for the tests (pc5_ftp_ps5.c is
 * the console's); built with PC5_FTP_SERVE, a server of its own for tests/test_ftp.py:
 * ftp-serve <root> <port>; with PC5_WEB_SERVE, the page's for tests/test_web.py:
 * web-serve <root> <install folder> <port> [user password]. */
#include "../title/pc5_ftp.h"
#include "../title/pc5_web.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int wait_for(int sock, short events, int timeout_ms)
{
    struct pollfd fd = { sock, events, 0 };
    int n = poll(&fd, 1, timeout_ms);
    return n < 0 ? -1 : n == 0 ? -2 : 0;
}

static int host_listen(uint16_t port, int backlog)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0), on = 1;
    struct sockaddr_in at = { 0 };
    if (sock < 0) return -1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    at.sin_family = AF_INET;
    at.sin_port = htons(port);
    at.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(sock, (struct sockaddr *)&at, sizeof(at)) || listen(sock, backlog)) {
        close(sock);
        return -1;
    }
    return sock;
}

static int host_accept(int sock, int timeout_ms)
{
    if (wait_for(sock, POLLIN, timeout_ms)) return -1;
    return accept(sock, NULL, NULL);
}

static int host_address(int sock, uint32_t *ip, uint16_t *port)
{
    struct sockaddr_in at;
    socklen_t length = sizeof(at);
    if (getsockname(sock, (struct sockaddr *)&at, &length)) return -1;
    *ip = at.sin_addr.s_addr;
    *port = ntohs(at.sin_port);
    return 0;
}

static int host_lan_ip(uint32_t *ip)
{
    *ip = htonl(INADDR_LOOPBACK);
    return 0;
}

static int host_send(int sock, const void *data, size_t size, int timeout_ms)
{
    int waited = wait_for(sock, POLLOUT, timeout_ms);
    if (waited) return waited;
    ssize_t n = send(sock, data, size, MSG_NOSIGNAL);
    return n < 0 ? -1 : (int)n;
}

static int host_recv(int sock, void *data, size_t size, int timeout_ms)
{
    int waited = wait_for(sock, POLLIN, timeout_ms);
    if (waited) return waited;
    ssize_t n = recv(sock, data, size, 0);
    return n < 0 ? -1 : (int)n;
}

static void host_close(int sock)
{
    if (sock >= 0) close(sock);
}

static void host_log(const char *line)
{
    if (getenv("PC5_FTP_LOG")) fprintf(stderr, "%s\n", line);
}

static const Pc5FtpOps host_ops = { host_listen, host_accept, host_address, host_lan_ip,
                                    host_send,   host_recv,   host_close,   host_log };

void pc5_ftp_host_init(void);
void pc5_ftp_host_init(void)
{
    pc5_ftp_init(&host_ops);
}

#ifndef PC5_FTP_SERVE
static int host_random(void *data, size_t size)
{
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return -1;
    int failed = fread(data, 1, size, f) != size;
    fclose(f);
    return failed ? -1 : 0;
}
void pc5_web_host_init(void);
void pc5_web_host_init(void)
{
    pc5_web_init(&host_ops);
    pc5_web_random_init(host_random);
}
#endif

#ifdef PC5_FTP_SERVE
int main(int argc, char **argv)
{
    char error[PC5_ERROR];
    if (argc != 3) return 2;
    signal(SIGPIPE, SIG_IGN);
    pc5_ftp_host_init();
    if (pc5_ftp_start(argv[1], (uint16_t)atoi(argv[2]), error, sizeof(error))) {
        fprintf(stderr, "ftp-serve: %s\n", error);
        return 1;
    }
    printf("ready\n");
    fflush(stdout);
    /* Until stdin closes: then stopped, as PC5 stops it. */
    char buffer[16];
    while (fread(buffer, 1, sizeof(buffer), stdin) > 0) {}
    pc5_ftp_stop();
    Pc5FtpStatus status;
    pc5_ftp_status(&status);
    printf("stopped running=%d sessions=%d\n", status.running, status.sessions);
    return 0;
}
#endif

#ifdef PC5_WEB_SERVE
#include "pc5_i18n.h"
int main(int argc, char **argv)
{
    char error[PC5_ERROR];
    if (argc != 4 && argc != 6) return 2;
    signal(SIGPIPE, SIG_IGN);
    const char *language_dir = getenv("PC5_TEST_LANG_DIR");
    if (language_dir) pc5_i18n_use(language_dir, "ar", NULL, NULL);
    pc5_web_host_init();
    if (argc == 6) pc5_web_login(argv[4], argv[5]);
    Pc5WebConfig config = { argv[1], argv[2] };
    if (pc5_web_start(&config, (uint16_t)atoi(argv[3]), error, sizeof(error))) {
        fprintf(stderr, "web-serve: %s\n", error);
        return 1;
    }
    printf("ready\n");
    fflush(stdout);
    /* A line on stdin: the counts; stdin closed: stopped, as PC5 stops it. */
    char line[64];
    while (fgets(line, sizeof(line), stdin)) {
        if (!strncmp(line, "appearance-begin", 16)) {
            char url[160]; int failed = pc5_web_appearance_begin(url, sizeof(url));
            printf("%s\n", failed ? "failed" : url); fflush(stdout); continue;
        }
        if (!strncmp(line, "appearance-finish ", 18)) {
            pc5_web_appearance_finish(atoi(line + 18)); puts("finished"); fflush(stdout); continue;
        }
        if (!strncmp(line, "phone ", 6)) {
            char url[160];
            int failed = pc5_web_phone_begin(atoi(line + 6), "Test game </script>", url, sizeof(url));
            printf("%s\n", failed ? "failed" : url); fflush(stdout); continue;
        }
        if (!strncmp(line, "phone-end", 9)) {
            pc5_web_phone_end(); puts("ended"); fflush(stdout); continue;
        }
        if (!strncmp(line, "phone-take", 10)) {
            char path[PC5_PATH];
            int ready = pc5_web_phone_take(path, sizeof(path));
            printf("%s\n", ready ? path : "none"); fflush(stdout); continue;
        }
        if (!strncmp(line, "phone-only", 10)) {
            pc5_web_phone_only(1); puts("only"); fflush(stdout); continue;
        }
        Pc5WebStatus status;
        pc5_web_status(&status);
        printf("uploads=%u transfers=%d\n", status.uploads, status.transfers);
        fflush(stdout);
    }
    pc5_web_stop();
    Pc5WebStatus status;
    pc5_web_status(&status);
    printf("stopped running=%d\n", status.running);
    return 0;
}
#endif
