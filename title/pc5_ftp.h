/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: an FTP server for PC5's INSTALL folder, so a game can be sent from a PC (FileZilla,
 * Windows Explorer) and installed as PC5 offers. Plain FTP, passive mode only (PASV, EPSV),
 * with the username and password set (pc5_ftp_login; none set: any); a client sees only the
 * INSTALL folder, as "/", and nothing outside it. It runs while PC5 is open (Settings ›
 * Network › FTP server). The
 * sockets come from the platform: the console's (pc5_ftp_ps5.c) or the host's in the tests. */
#ifndef PC5_FTP_H
#define PC5_FTP_H
#include "pc5.h"

#include <sys/stat.h>
#include <time.h>

enum { PC5_FTP_PORT = 2125, PC5_FTP_SESSIONS = 12, PC5_FTP_LOGIN = 64 };

typedef struct Pc5FtpOps {
    /* A TCP socket listening on every address at port (0: any free one), or -1. */
    int (*listen)(uint16_t port, int backlog);
    /* A connection to it, or -1 when none came within timeout_ms (or on an error). */
    int (*accept)(int socket, int timeout_ms);
    /* The socket's own address: IPv4 in network order, the port in host order. 0 or -1. */
    int (*address)(int socket, uint32_t *ip, uint16_t *port);
    /* The console's address on its network (network order), for showing; 0 or -1. */
    int (*lan_ip)(uint32_t *ip);
    /* Bytes sent or received (0: closed), -1 on an error, -2 when nothing came within timeout_ms. */
    int (*send)(int socket, const void *data, size_t size, int timeout_ms);
    int (*recv)(int socket, void *data, size_t size, int timeout_ms);
    void (*close)(int socket);
    void (*log)(const char *line);
} Pc5FtpOps;

typedef struct Pc5FtpStatus {
    int running;
    char address[48];          /* "192.168.1.20:2125", or ":2125" when the console's address isn't known */
    int sessions;              /* clients connected */
    int transfers;             /* uploads and downloads under way */
    unsigned uploads;          /* files uploaded since it started */
    uint64_t idle_ms;          /* since the last transfer ended (or it started) */
} Pc5FtpStatus;

/* Sets the platform up (once); without it, pc5_ftp_start fails. */
void pc5_ftp_init(const Pc5FtpOps *ops);
/* Serves root (a folder) on port. 0, or -1 with why in error. Already running: restarted. */
int pc5_ftp_start(const char *root, uint16_t port, char *error, size_t size);
/* Stops it, its clients disconnected (within about a second). */
void pc5_ftp_stop(void);
void pc5_ftp_status(Pc5FtpStatus *status);
/* The username and password a client must give ("" or NULL: any), from the next login on. */
void pc5_ftp_login(const char *user, const char *password);

/* For the tests: a client's path (absolute, or relative to cwd, "/" the root) as the server
 * sees it, ".." never above the root: 0, or -1 when it is too long. */
int pc5_ftp_path(const char *cwd, const char *arg, char *out, size_t size);
/* For the tests: one LIST line for name with st, its date as seen at now. */
void pc5_ftp_list_line(const char *name, const struct stat *st, time_t now, char *out, size_t size);

#endif
