/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: its page for a browser on a phone or a PC, while PC5 is open (Settings › Network ›
 * Web page): http://<the console's address>:8125. The page shows PC5's version and free
 * space, the games (each one's newest log to download, its launch.txt and env.txt to edit),
 * sends a game's folder to PC5's INSTALL folder (then PC5 offers to install it, as after
 * FTP) and lists the logs to download. With the username and password set (pc5_web_login,
 * as the FTP server's), the browser asks for them (HTTP Basic); none set: anyone on the
 * network may use it. Plain HTTP/1.1, one request a connection. The sockets come from the
 * platform, the FTP server's (pc5_ftp.h): the console's, or the host's in the tests.
 *
 * The server's threads never touch PC5's screens: what it shows is read from the disk
 * (root's profiles, prefixes and logs), and its counts are read with pc5_web_status. */
#ifndef PC5_WEB_H
#define PC5_WEB_H
#include "pc5_ftp.h"

enum { PC5_WEB_PORT = 8125, PC5_WEB_SESSIONS = 8 };

typedef struct Pc5WebConfig {
    const char *root;          /* prospero-win's (/data/prospero-win): profiles, prefixes, logs */
    const char *install_dir;   /* PC5's INSTALL folder, games are sent there ("" or NULL: none) */
} Pc5WebConfig;

typedef struct Pc5WebStatus {
    int running;
    char address[48];          /* "192.168.1.20:8125", or ":8125" when the console's address isn't known */
    int transfers;             /* uploads and downloads under way */
    unsigned uploads;          /* files sent to the INSTALL folder since it started */
    uint64_t idle_ms;          /* since the last transfer ended (or it started) */
} Pc5WebStatus;

/* Sets the platform up (once); without it, pc5_web_start fails. */
void pc5_web_init(const Pc5FtpOps *ops);
/* Serves the page on port. 0, or -1 with why in error. Already running: restarted. */
int pc5_web_start(const Pc5WebConfig *config, uint16_t port, char *error, size_t size);
/* Stops it, its connections closed (within about a second). */
void pc5_web_stop(void);
void pc5_web_status(Pc5WebStatus *status);
/* The username and password a browser must give ("" or NULL: none asked), from the next request on. */
void pc5_web_login(const char *user, const char *password);

/* What a phone sends: one of a game's media, or (PC5_PHONE_LIBRARY) the library-wide appearance
 * page, which picks the game and the media itself. */
enum { PC5_PHONE_COVER, PC5_PHONE_BACKGROUND, PC5_PHONE_ICON, PC5_PHONE_MUSIC, PC5_PHONE_VIDEO, PC5_PHONE_LIBRARY,
       PC5_PHONE_LOADING };
typedef struct Pc5PhoneStatus {
    int active, busy, ready;
    uint64_t received, total;
} Pc5PhoneStatus;
void pc5_web_random_init(int (*random_bytes)(void *, size_t));
void pc5_web_phone_only(int enabled);
int pc5_web_phone_begin(int kind, const char *game, char *url, size_t size);
void pc5_web_phone_status(Pc5PhoneStatus *status);
/* Claims the received staging file; caller removes it after importing. */
int pc5_web_phone_take(char *path, size_t size);
void pc5_web_phone_end(void);
/* Temporary library-wide appearance capability; no game launch, files or logs. */
int pc5_web_appearance_begin(char *url, size_t size);
int pc5_web_appearance_target(char *id, size_t size, int *kind);
void pc5_web_appearance_finish(int success);

/* For the tests: a path sent to the INSTALL folder ("Neon Drift/data/a.pak"), checked: 0 when
 * every part is a plain name (no "", ".", "..", backslash or control character), -1 otherwise. */
int pc5_web_upload_path_ok(const char *path);

#endif
