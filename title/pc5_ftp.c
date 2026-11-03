/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: the INSTALL folder's FTP server (pc5_ftp.h). A thread takes the connections; each
 * client has a thread of its own, which reads its commands and does its transfers. */
#include "pc5_ftp.h"
#include "pc5_i18n.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
    CONTROL_WAIT_MS = 500,          /* a client's commands are read this long at a time, the stop looked at between */
    IDLE_MS = 600000,               /* a client silent this long is let go */
    DATA_ACCEPT_MS = 20000,         /* a passive connection must come within this */
    DATA_WAIT_MS = 60000,           /* and send or take something this often */
    BUFFER = 512 * 1024,
    GATHER_ROOM = 64 * 1024,        /* an upload's data is gathered until less than this is left of the buffer */
    LINE = 1024,
};

typedef struct Session {
    int control, passive;           /* the passive listener, -1 until PASV or EPSV */
    char cwd[PC5_PATH];             /* as the client sees it: "/", "/Neon Drift" */
    char rename_from[PC5_PATH];     /* RNFR's, until RNTO */
    uint64_t rest;                  /* REST's offset for the next RETR or STOR */
    char user[PC5_FTP_LOGIN];       /* USER's, until PASS */
    int logged_in, failures;
    uint8_t *buffer;
} Session;

static struct {
    Pc5FtpOps ops;
    int have_ops;
    pthread_mutex_t lock;
    pthread_t thread;
    int running, stopping, listener;
    char root[PC5_PATH];
    char address[48];
    char user[PC5_FTP_LOGIN], password[PC5_FTP_LOGIN];   /* "": any */
    int sessions, transfers;
    unsigned uploads;
    uint64_t last_ms;
} ftp = { .lock = PTHREAD_MUTEX_INITIALIZER, .listener = -1 };

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static void say(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *format, ...)
{
    char line[PC5_PATH + 200];
    va_list args;
    if (!ftp.ops.log) return;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    ftp.ops.log(line);
}

static int stopping(void)
{
    pthread_mutex_lock(&ftp.lock);
    int stop = ftp.stopping;
    pthread_mutex_unlock(&ftp.lock);
    return stop;
}

static void count(int *what, int by)
{
    pthread_mutex_lock(&ftp.lock);
    *what += by;
    if (what == &ftp.transfers && by < 0) ftp.last_ms = now_ms();
    pthread_mutex_unlock(&ftp.lock);
}

/* UTC calendar time (Howard Hinnant's civil_from_days): the console's C library has no gmtime_r. */
static void utc(time_t t, struct tm *tm)
{
    int64_t secs = (int64_t)t, days = secs / 86400, rem = secs % 86400;
    if (rem < 0) {
        rem += 86400;
        days--;
    }
    memset(tm, 0, sizeof(*tm));
    tm->tm_hour = (int)(rem / 3600);
    tm->tm_min = (int)(rem % 3600 / 60);
    tm->tm_sec = (int)(rem % 60);
    days += 719468;
    int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    int64_t doe = days - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    int64_t d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
    tm->tm_year = (int)(yoe + era * 400 + (m <= 2) - 1900);
    tm->tm_mon = (int)m - 1;
    tm->tm_mday = (int)d;
}

/* ---- paths ---------------------------------------------------------------------------- */

int pc5_ftp_path(const char *cwd, const char *arg, char *out, size_t size)
{
    char joined[PC5_PATH * 2];
    size_t n = 0;

    if (!arg) arg = "";
    if (arg[0] == '/') snprintf(joined, sizeof(joined), "%s", arg);
    else snprintf(joined, sizeof(joined), "%s/%s", cwd && cwd[0] ? cwd : "/", arg);
    if (size < 2) return -1;
    out[0] = 0;
    for (char *part = joined, *next; part; part = next) {
        next = strchr(part, '/');
        if (next) *next++ = 0;
        if (!part[0] || !strcmp(part, ".")) continue;
        if (!strcmp(part, "..")) {
            while (n > 0 && out[n - 1] != '/') n--;
            if (n > 0) n--;           /* the slash before it */
            out[n] = 0;
            continue;
        }
        size_t len = strlen(part);
        if (n + 1 + len + 1 > size) return -1;
        out[n++] = '/';
        memcpy(out + n, part, len);
        n += len;
        out[n] = 0;
    }
    if (!n) snprintf(out, size, "/");
    return 0;
}

/* The client's path arg as a path on disk (real) and as the client sees it (seen). */
static int resolve(const Session *s, const char *arg, char *seen, char *real)
{
    if (pc5_ftp_path(s->cwd, arg, seen, PC5_PATH)) return -1;
    return pc5_format(real, PC5_PATH, "%s%s", ftp.root, strcmp(seen, "/") ? seen : "");
}

/* ---- replies --------------------------------------------------------------------------- */

static int send_all(int sock, const void *data, size_t size)
{
    const uint8_t *p = data;
    while (size) {
        int sent = ftp.ops.send(sock, p, size, DATA_WAIT_MS);
        if (sent <= 0) return -1;
        p += sent;
        size -= (size_t)sent;
    }
    return 0;
}

static int reply(Session *s, const char *format, ...) __attribute__((format(printf, 2, 3)));
static int reply(Session *s, const char *format, ...)
{
    char line[PC5_PATH + 128];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(line, sizeof(line) - 2, format, args);
    va_end(args);
    if (n < 0) return -1;
    if ((size_t)n > sizeof(line) - 3) n = (int)sizeof(line) - 3;
    line[n++] = '\r';
    line[n++] = '\n';
    return send_all(s->control, line, (size_t)n);
}

/* A path in a reply: its quotes doubled (RFC 959's 257). */
static void quoted(const char *path, char *out, size_t size)
{
    size_t n = 0;
    for (; *path && n + 3 < size; path++) {
        if (*path == '"') out[n++] = '"';
        out[n++] = *path;
    }
    out[n] = 0;
}

void pc5_ftp_list_line(const char *name, const struct stat *st, time_t now, char *out, size_t size)
{
    static const char *const months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    struct tm tm;
    char when[32];
    time_t t = st->st_mtime;
    utc(t, &tm);
    /* As ls does: the time for the last six months, else the year. */
    if (t <= now && now - t < 182 * 24 * 3600)
        snprintf(when, sizeof(when), "%s %2d %02d:%02d", months[tm.tm_mon % 12], tm.tm_mday, tm.tm_hour, tm.tm_min);
    else
        snprintf(when, sizeof(when), "%s %2d  %d", months[tm.tm_mon % 12], tm.tm_mday, tm.tm_year + 1900);
    int dir = S_ISDIR(st->st_mode);
    snprintf(out, size, "%s 1 pc5 pc5 %llu %s %s\r\n", dir ? "drwxr-xr-x" : "-rw-r--r--",
             (unsigned long long)(dir ? 4096 : st->st_size), when, name);
}

/* ---- the data connection --------------------------------------------------------------- */

static void passive_close(Session *s)
{
    if (s->passive >= 0) ftp.ops.close(s->passive);
    s->passive = -1;
}

/* PASV (extended 0) or EPSV (1): a listener for the next transfer, its address replied. */
static void passive(Session *s, int extended)
{
    uint32_t ip = 0, unused;
    uint16_t port = 0, control_port;

    passive_close(s);
    s->passive = ftp.ops.listen(0, 1);
    if (s->passive < 0 || ftp.ops.address(s->passive, &unused, &port) ||
        ftp.ops.address(s->control, &ip, &control_port)) {
        passive_close(s);
        reply(s, "425 Can't open a data connection");
        return;
    }
    if (extended) {
        reply(s, "229 Entering Extended Passive Mode (|||%u|)", (unsigned)port);
    } else {
        const uint8_t *b = (const uint8_t *)&ip;
        reply(s, "227 Entering Passive Mode (%u,%u,%u,%u,%u,%u)", b[0], b[1], b[2], b[3], port >> 8, port & 0xff);
    }
}

/* The client's data connection, once it has come; -1 (replied) when it doesn't. */
static int data_open(Session *s)
{
    if (s->passive < 0) {
        reply(s, "425 Use PASV or EPSV first");
        return -1;
    }
    int data = ftp.ops.accept(s->passive, DATA_ACCEPT_MS);
    passive_close(s);
    if (data < 0) reply(s, "425 The data connection didn't come");
    return data;
}

/* ---- the commands ---------------------------------------------------------------------- */

static void list(Session *s, const char *arg, int names_only)
{
    char seen[PC5_PATH], real[PC5_PATH], path[PC5_PATH], line[PC5_PATH + 96];
    struct stat st;

    /* ls's options (-a, -la) are left out: everything is listed. */
    while (arg && *arg == '-') {
        while (*arg && *arg != ' ') arg++;
        while (*arg == ' ') arg++;
    }
    if (resolve(s, arg, seen, real) || stat(real, &st)) {
        passive_close(s);
        reply(s, "550 No such file or folder");
        return;
    }
    int data = data_open(s);
    if (data < 0) return;
    reply(s, "150 Here it comes");
    time_t now = time(NULL);
    int failed = 0;
    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(real);
        struct dirent *entry;
        while (dir && !failed && (entry = readdir(dir))) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") ||
                pc5_join(path, sizeof(path), real, entry->d_name) || stat(path, &st))
                continue;
            if (names_only) snprintf(line, sizeof(line), "%s\r\n", entry->d_name);
            else pc5_ftp_list_line(entry->d_name, &st, now, line, sizeof(line));
            failed = send_all(data, line, strlen(line));
        }
        if (dir) closedir(dir);
    } else {
        const char *name = strrchr(seen, '/');
        if (names_only) snprintf(line, sizeof(line), "%s\r\n", name + 1);
        else pc5_ftp_list_line(name + 1, &st, now, line, sizeof(line));
        failed = send_all(data, line, strlen(line));
    }
    ftp.ops.close(data);
    reply(s, failed ? "426 The listing was cut short" : "226 Done");
}

static void retrieve(Session *s, const char *arg)
{
    char seen[PC5_PATH], real[PC5_PATH];
    struct stat st;
    int fd = -1;
    uint64_t rest = s->rest;

    s->rest = 0;
    if (resolve(s, arg, seen, real) || stat(real, &st) || !S_ISREG(st.st_mode) || (fd = open(real, O_RDONLY)) < 0 ||
        (rest && lseek(fd, (off_t)rest, SEEK_SET) < 0)) {
        if (fd >= 0) close(fd);
        passive_close(s);
        reply(s, "550 Can't read that file");
        return;
    }
    int data = data_open(s);
    if (data < 0) {
        close(fd);
        return;
    }
    count(&ftp.transfers, 1);
    reply(s, "150 Sending %s", seen);
    int failed = 0;
    for (;;) {
        ssize_t got = read(fd, s->buffer, BUFFER);
        if (got <= 0) {
            failed = got < 0;
            break;
        }
        if (send_all(data, s->buffer, (size_t)got) || stopping()) {
            failed = 1;
            break;
        }
    }
    close(fd);
    ftp.ops.close(data);
    count(&ftp.transfers, -1);
    reply(s, failed ? "426 The transfer was cut short" : "226 Sent");
}

static void store(Session *s, const char *arg, int append)
{
    char seen[PC5_PATH], real[PC5_PATH];
    uint64_t rest = s->rest, bytes = 0;
    struct stat st;
    int fd;

    s->rest = 0;
    if (resolve(s, arg, seen, real) || !strcmp(seen, "/") || (!stat(real, &st) && S_ISDIR(st.st_mode))) {
        passive_close(s);
        reply(s, "553 Can't write there");
        return;
    }
    fd = open(real, O_WRONLY | O_CREAT | (append ? O_APPEND : rest ? 0 : O_TRUNC), 0644);
    if (fd < 0 || (rest && !append && lseek(fd, (off_t)rest, SEEK_SET) < 0)) {
        if (fd >= 0) close(fd);
        passive_close(s);
        reply(s, "553 Can't write there: %s", strerror(errno));
        return;
    }
    int data = data_open(s);
    if (data < 0) {
        close(fd);
        return;
    }
    count(&ftp.transfers, 1);
    reply(s, "150 Receiving %s", seen);
    int failed = 0, write_failed = 0, ended = 0;
    while (!ended && !failed) {
        int got = ftp.ops.recv(data, s->buffer, BUFFER, DATA_WAIT_MS);
        if (got == 0) break;
        if (got < 0 || stopping()) {
            failed = 1;
            break;
        }
        /* What has come already is gathered (nothing waited for) until the buffer is nearly
         * full, so the disk gets a few large writes instead of one for each piece the network
         * hands over. What was gathered is written even when the connection then fails or
         * ends, as it would have been a piece at a time. */
        size_t have = (size_t)got;
        while (BUFFER - have >= GATHER_ROOM) {
            int more = ftp.ops.recv(data, s->buffer + have, BUFFER - have, 0);
            if (more == -2) break;
            if (more == 0) ended = 1;
            if (more < 0) failed = 1;
            if (more <= 0) break;
            have += (size_t)more;
        }
        for (size_t done = 0; done < have;) {
            ssize_t put = write(fd, s->buffer + done, have - done);
            if (put <= 0) {
                write_failed = 1;
                break;
            }
            done += (size_t)put;
        }
        if (write_failed) break;
        bytes += (uint64_t)have;
    }
    if (close(fd)) write_failed = 1;
    ftp.ops.close(data);
    if (write_failed) {
        reply(s, "452 The console couldn't write it: %s", strerror(errno ? errno : ENOSPC));
    } else if (failed) {
        reply(s, "426 The transfer was cut short");
    } else {
        pthread_mutex_lock(&ftp.lock);
        ftp.uploads++;
        pthread_mutex_unlock(&ftp.lock);
        reply(s, "226 Received");
    }
    say("PC5 ftp %s %s bytes=%llu%s", append ? "appended" : "received", seen, (unsigned long long)bytes,
        write_failed ? " write-failed" : failed ? " cut-short" : "");
    count(&ftp.transfers, -1);
}

/* 1 when user and password are the ones set (pc5_ftp_login); "" set: any is right. */
static int login_right(const char *user, const char *password)
{
    pthread_mutex_lock(&ftp.lock);
    int right = (!ftp.user[0] || !strcmp(user, ftp.user)) && (!ftp.password[0] || !strcmp(password, ftp.password));
    pthread_mutex_unlock(&ftp.lock);
    return right;
}

static int login_needed(void)
{
    pthread_mutex_lock(&ftp.lock);
    int needed = ftp.user[0] || ftp.password[0];
    pthread_mutex_unlock(&ftp.lock);
    return needed;
}

void pc5_ftp_login(const char *user, const char *password)
{
    pthread_mutex_lock(&ftp.lock);
    pc5_copy(ftp.user, sizeof(ftp.user), user ? user : "");
    pc5_copy(ftp.password, sizeof(ftp.password), password ? password : "");
    pthread_mutex_unlock(&ftp.lock);
}

static void session_command(Session *s, char *command, char *arg, int *quit)
{
    char seen[PC5_PATH], real[PC5_PATH], text[PC5_PATH * 2];
    struct stat st;

    for (char *c = command; *c; c++)
        if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
#define IS(name) (!strcmp(command, name))
    if (IS("USER")) {
        pc5_copy(s->user, sizeof(s->user), arg);
        s->logged_in = 0;
        reply(s, "331 And the password");
    } else if (IS("PASS")) {
        if (login_right(s->user, arg)) {
            s->logged_in = 1;
            s->failures = 0;
            reply(s, "230 Welcome: this is PC5's INSTALL folder");
        } else {
            s->logged_in = 0;
            say("PC5 ftp login refused user=%s", s->user);
            usleep(1000000);   /* slows guessing */
            reply(s, "530 Wrong username or password (they are in PC5: Settings, Network)");
            if (++s->failures >= 3) *quit = 1;
        }
    } else if (!s->logged_in && !IS("QUIT") && !IS("FEAT") && !IS("SYST") && !IS("OPTS") && !IS("NOOP") &&
               !IS("AUTH")) {
        reply(s, "530 Log in first");
    } else if (IS("SYST")) reply(s, "215 UNIX Type: L8");
    else if (IS("FEAT")) {
        static const char features[] = "211-Features:\r\n UTF8\r\n EPSV\r\n PASV\r\n SIZE\r\n MDTM\r\n REST STREAM\r\n"
                                       "211 End\r\n";
        send_all(s->control, features, sizeof(features) - 1);
    } else if (IS("OPTS")) reply(s, "200 OK");
    else if (IS("NOOP") || IS("MODE") || IS("STRU") || IS("TYPE")) reply(s, "200 OK");
    else if (IS("ALLO")) reply(s, "202 No need");
    else if (IS("PWD") || IS("XPWD")) {
        quoted(s->cwd, text, sizeof(text));
        reply(s, "257 \"%s\"", text);
    } else if (IS("CWD") || IS("XCWD") || IS("CDUP") || IS("XCUP")) {
        if (resolve(s, IS("CDUP") || IS("XCUP") ? ".." : arg, seen, real) || stat(real, &st) || !S_ISDIR(st.st_mode)) {
            reply(s, "550 No such folder");
        } else {
            pc5_copy(s->cwd, sizeof(s->cwd), seen);
            reply(s, "250 OK");
        }
    } else if (IS("PASV")) passive(s, 0);
    else if (IS("EPSV")) {
        if (arg[0] && !strcmp(arg, "ALL")) reply(s, "200 OK");
        else passive(s, 1);
    } else if (IS("PORT") || IS("EPRT")) reply(s, "502 Use passive mode");
    else if (IS("LIST")) list(s, arg, 0);
    else if (IS("NLST")) list(s, arg, 1);
    else if (IS("RETR")) retrieve(s, arg);
    else if (IS("STOR")) store(s, arg, 0);
    else if (IS("APPE")) store(s, arg, 1);
    else if (IS("REST")) {
        char *end;
        unsigned long long at = strtoull(arg, &end, 10);
        if (!arg[0] || *end) reply(s, "501 A number");
        else {
            s->rest = at;
            reply(s, "350 From %llu", at);
        }
    } else if (IS("SIZE") || IS("MDTM")) {
        if (resolve(s, arg, seen, real) || stat(real, &st) || !S_ISREG(st.st_mode)) {
            reply(s, "550 No such file");
        } else if (IS("SIZE")) {
            reply(s, "213 %llu", (unsigned long long)st.st_size);
        } else {
            struct tm tm;
            time_t t = st.st_mtime;
            utc(t, &tm);
            reply(s, "213 %04d%02d%02d%02d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                  tm.tm_min, tm.tm_sec);
        }
    } else if (IS("MKD") || IS("XMKD")) {
        errno = EINVAL;
        if (resolve(s, arg, seen, real) || mkdir(real, 0755)) {
            reply(s, "550 Can't make it: %s", strerror(errno));
        } else {
            quoted(seen, text, sizeof(text));
            reply(s, "257 \"%s\" made", text);
        }
    } else if (IS("RMD") || IS("XRMD") || IS("DELE")) {
        errno = 0;
        int failed = resolve(s, arg, seen, real) || !strcmp(seen, "/") || (IS("DELE") ? unlink(real) : rmdir(real));
        if (failed) reply(s, "550 Can't delete it%s%s", errno ? ": " : "", errno ? strerror(errno) : "");
        else reply(s, "250 Deleted");
    } else if (IS("RNFR")) {
        if (resolve(s, arg, seen, real) || !strcmp(seen, "/") || stat(real, &st)) {
            reply(s, "550 No such file or folder");
        } else {
            pc5_copy(s->rename_from, sizeof(s->rename_from), real);
            reply(s, "350 And its new name?");
        }
    } else if (IS("RNTO")) {
        errno = 0;
        if (!s->rename_from[0]) reply(s, "503 RNFR first");
        else if (resolve(s, arg, seen, real) || !strcmp(seen, "/") || rename(s->rename_from, real))
            reply(s, "550 Can't rename it%s%s", errno ? ": " : "", errno ? strerror(errno) : "");
        else reply(s, "250 Renamed");
        s->rename_from[0] = 0;
    } else if (IS("ABOR")) reply(s, "226 Nothing to stop");
    else if (IS("QUIT")) {
        reply(s, "221 Bye");
        *quit = 1;
    } else reply(s, "502 PC5 doesn't do %s", command);
#undef IS
    /* RNTO must follow RNFR at once. */
    if (strcmp(command, "RNFR") && strcmp(command, "RNTO")) s->rename_from[0] = 0;
}

static void *session_thread(void *arg)
{
    Session *s = arg;
    char line[LINE];
    size_t have = 0;
    uint64_t heard = now_ms();
    int quit = 0;

    reply(s, "220 PC5: its INSTALL folder. Put a game's folder here, then install it in PC5.");
    while (!quit && !stopping()) {
        int got = ftp.ops.recv(s->control, line + have, sizeof(line) - 1 - have, CONTROL_WAIT_MS);
        if (got == -2) {
            if (now_ms() - heard > IDLE_MS) break;
            continue;
        }
        if (got <= 0) break;
        heard = now_ms();
        have += (size_t)got;
        line[have] = 0;
        char *end;
        while (!quit && (end = strstr(line, "\n"))) {
            *end = 0;
            if (end > line && end[-1] == '\r') end[-1] = 0;
            char *command = line, *space = strchr(line, ' ');
            if (space) *space = 0;
            session_command(s, command, space ? space + 1 : command + strlen(command), &quit);
            size_t used = (size_t)(end + 1 - line);
            memmove(line, end + 1, have - used + 1);
            have -= used;
        }
        if (have >= sizeof(line) - 1) {
            reply(s, "500 Too long");
            break;
        }
    }
    passive_close(s);
    ftp.ops.close(s->control);
    pc5_free(s->buffer);
    pc5_free(s);
    count(&ftp.sessions, -1);
    return NULL;
}

static void *listen_thread(void *arg)
{
    (void)arg;
    while (!stopping()) {
        int client = ftp.ops.accept(ftp.listener, CONTROL_WAIT_MS);
        if (client < 0) continue;
        pthread_mutex_lock(&ftp.lock);
        int full = ftp.sessions >= PC5_FTP_SESSIONS;
        if (!full) ftp.sessions++;
        pthread_mutex_unlock(&ftp.lock);
        Session *s = full ? NULL : pc5_alloc(sizeof(*s));
        if (s) s->buffer = pc5_alloc(BUFFER);
        if (!s || !s->buffer) {
            static const char busy[] = "421 Too many connections, try again in a moment\r\n";
            ftp.ops.send(client, busy, sizeof(busy) - 1, 1000);
            ftp.ops.close(client);
            if (s) pc5_free(s);
            if (!full) count(&ftp.sessions, -1);
            continue;
        }
        s->control = client;
        s->passive = -1;
        s->logged_in = !login_needed();
        pc5_copy(s->cwd, sizeof(s->cwd), "/");
        if (pc5_thread_start(NULL, session_thread, s, 1)) {
            ftp.ops.close(client);
            pc5_free(s->buffer);
            pc5_free(s);
            count(&ftp.sessions, -1);
        }
    }
    return NULL;
}

/* ---- starting and stopping --------------------------------------------------------------- */

void pc5_ftp_init(const Pc5FtpOps *ops)
{
    if (!ops) return;
    ftp.ops = *ops;
    ftp.have_ops = 1;
}

int pc5_ftp_start(const char *root, uint16_t port, char *error, size_t size)
{
    struct stat st;
    uint32_t ip = 0;

    pc5_ftp_stop();
    if (!ftp.have_ops) return pc5_format(error, size, N_("There's no network here.")), -1;
    if (!root || stat(root, &st) || !S_ISDIR(st.st_mode))
        return pc5_format(error, size, N_("PC5's INSTALL folder isn't there.")), -1;
    pc5_copy(ftp.root, sizeof(ftp.root), root);
    size_t n = strlen(ftp.root);
    while (n > 1 && ftp.root[n - 1] == '/') ftp.root[--n] = 0;
    ftp.listener = ftp.ops.listen(port, 4);
    if (ftp.listener < 0) return pc5_format(error, size, N_("Port %u is taken, or there's no network."), (unsigned)port), -1;
    if (!ftp.ops.lan_ip || ftp.ops.lan_ip(&ip) || !ip) snprintf(ftp.address, sizeof(ftp.address), ":%u", (unsigned)port);
    else {
        const uint8_t *b = (const uint8_t *)&ip;
        snprintf(ftp.address, sizeof(ftp.address), "%u.%u.%u.%u:%u", b[0], b[1], b[2], b[3], (unsigned)port);
    }
    pthread_mutex_lock(&ftp.lock);
    ftp.stopping = 0;
    ftp.uploads = 0;
    ftp.last_ms = now_ms();
    pthread_mutex_unlock(&ftp.lock);
    if (pc5_thread_start(&ftp.thread, listen_thread, NULL, 0)) {
        ftp.ops.close(ftp.listener);
        ftp.listener = -1;
        return pc5_format(error, size, N_("PC5 can't start it.")), -1;
    }
    ftp.running = 1;
    say("PC5 ftp on address=%s root=%s", ftp.address, ftp.root);
    return 0;
}

void pc5_ftp_stop(void)
{
    if (!ftp.running) return;
    pthread_mutex_lock(&ftp.lock);
    ftp.stopping = 1;
    pthread_mutex_unlock(&ftp.lock);
    pthread_join(ftp.thread, NULL);
    ftp.ops.close(ftp.listener);
    ftp.listener = -1;
    /* The clients see the stop within a read's wait. */
    for (int i = 0; i < 60; i++) {
        pthread_mutex_lock(&ftp.lock);
        int left = ftp.sessions;
        pthread_mutex_unlock(&ftp.lock);
        if (!left) break;
        usleep(50000);
    }
    ftp.running = 0;
    say("PC5 ftp off uploads=%u", ftp.uploads);
}

void pc5_ftp_status(Pc5FtpStatus *status)
{
    memset(status, 0, sizeof(*status));
    pthread_mutex_lock(&ftp.lock);
    status->running = ftp.running;
    pc5_copy(status->address, sizeof(status->address), ftp.address);
    status->sessions = ftp.sessions;
    status->transfers = ftp.transfers;
    status->uploads = ftp.uploads;
    status->idle_ms = ftp.transfers ? 0 : now_ms() - ftp.last_ms;
    pthread_mutex_unlock(&ftp.lock);
}
