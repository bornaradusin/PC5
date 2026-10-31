/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: its page for a browser (pc5_web.h). A thread takes the connections; each one has a
 * thread of its own, which reads its request, answers it and closes. */
#include "pc5_web.h"

#include "pc5_library.h"
#include "pc5_logs.h"
#include "pc5_controls.h"
#include "pc5_report.h"
#include "pc5_identity.h"
#include "pc5_web_page.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum {
    WAIT_MS = 500,                  /* connections are waited for this long at a time, the stop looked at between */
    REQUEST_MS = 20000,             /* a request's head must come within this */
    BODY_MS = 60000,                /* and its body send something this often; an answer the same */
    HEAD = 16 * 1024,
    BUFFER = 256 * 1024,
    LAUNCH_MAX = 4096,              /* launch.txt: what PC5 reads of it */
    ENV_MAX = 64 * 1024,            /* env.txt: the same */
    LOGS_MAX = 200,
};

typedef struct Conn {
    int sock;
    char head[HEAD + 1];
    size_t have, head_end;          /* bytes read; where the body starts */
    char method[8];
    char path[PC5_PATH];            /* decoded, without its query */
    uint64_t length;                /* Content-Length, or UINT64_MAX when not said */
    int expect_continue;
    char authorization[256];
    uint8_t *buffer;
} Conn;

static struct {
    Pc5FtpOps ops;
    int have_ops;
    pthread_mutex_t lock;
    pthread_t thread;
    int running, stopping, listener;
    char root[PC5_PATH], install[PC5_PATH];
    char address[48];
    char user[PC5_FTP_LOGIN], password[PC5_FTP_LOGIN];
    int connections, transfers;
    unsigned uploads;
    uint64_t last_ms;
} web = { .lock = PTHREAD_MUTEX_INITIALIZER, .listener = -1 };

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
    if (!web.ops.log) return;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    web.ops.log(line);
}

static int stopping(void)
{
    pthread_mutex_lock(&web.lock);
    int stop = web.stopping;
    pthread_mutex_unlock(&web.lock);
    return stop;
}

static void count(int *what, int by)
{
    pthread_mutex_lock(&web.lock);
    *what += by;
    if (what == &web.transfers && by < 0) web.last_ms = now_ms();
    pthread_mutex_unlock(&web.lock);
}

/* ---- paths ------------------------------------------------------------------------------ */

int pc5_web_upload_path_ok(const char *path)
{
    if (!path || !path[0] || path[0] == '/') return -1;
    for (const char *part = path; part;) {
        const char *slash = strchr(part, '/');
        size_t n = slash ? (size_t)(slash - part) : strlen(part);
        if (!n || (n == 1 && part[0] == '.') || (n == 2 && part[0] == '.' && part[1] == '.')) return -1;
        for (size_t i = 0; i < n; i++)
            if ((unsigned char)part[i] < 0x20 || part[i] == 0x7f || part[i] == '\\') return -1;
        part = slash ? slash + 1 : NULL;
    }
    return strlen(path) < PC5_PATH / 2 ? 0 : -1;
}

static int hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* %XX decoded; 0, or -1 when it is malformed or decodes to a 0 byte. */
static int url_decode(const char *in, char *out, size_t size)
{
    size_t n = 0;
    for (; *in; in++) {
        int c = (unsigned char)*in;
        if (c == '%') {
            int hi = hex(in[1]), lo = hi < 0 ? -1 : hex(in[2]);
            if (lo < 0) return -1;
            c = hi * 16 + lo;
            if (!c) return -1;
            in += 2;
        }
        if (n + 1 >= size) return -1;
        out[n++] = (char)c;
    }
    out[n] = 0;
    return 0;
}

/* ---- sending ----------------------------------------------------------------------------- */

static int send_all(int sock, const void *data, size_t size)
{
    const uint8_t *p = data;
    while (size) {
        int sent = web.ops.send(sock, p, size, BODY_MS);
        if (sent <= 0) return -1;
        p += sent;
        size -= (size_t)sent;
    }
    return 0;
}

static const char *reason(int code)
{
    switch (code) {
    case 200: return "OK";
    case 201: return "Created";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 411: return "Length Required";
    case 413: return "Payload Too Large";
    case 500: return "Internal Server Error";
    case 503: return "Service Unavailable";
    default: return "Error";
    }
}

static int send_head(Conn *c, int code, const char *type, uint64_t length, const char *extra)
{
    char head[PC5_PATH + 512];
    int n = snprintf(head, sizeof(head),
                     "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %llu\r\nCache-Control: no-store\r\n"
                     "X-Content-Type-Options: nosniff\r\nConnection: close\r\n%s\r\n",
                     code, reason(code), type, (unsigned long long)length, extra ? extra : "");
    if (n < 0 || (size_t)n >= sizeof(head)) return -1;
    return send_all(c->sock, head, (size_t)n);
}

static int respond(Conn *c, int code, const char *type, const void *body, size_t size)
{
    if (send_head(c, code, type, size, NULL)) return -1;
    return size ? send_all(c->sock, body, size) : 0;
}

static int respond_text(Conn *c, int code, const char *text)
{
    return respond(c, code, "text/plain; charset=utf-8", text, strlen(text));
}

static int respond_buf(Conn *c, Pc5Buf *buf)
{
    int rc = buf->data ? respond(c, 200, "application/json; charset=utf-8", buf->data, buf->len)
                       : respond_text(c, 500, "Out of memory");
    pc5_buf_free(buf);
    return rc;
}

/* A file on disk, sent as a download named name. */
static void send_file(Conn *c, const char *path, const char *name)
{
    struct stat st;
    int fd = open(path, O_RDONLY);
    if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode)) {
        if (fd >= 0) close(fd);
        respond_text(c, 404, "No such file");
        return;
    }
    char extra[PC5_PATH + 64], safe[PC5_PATH];
    size_t n = 0;
    for (const char *p = name; *p && n + 1 < sizeof(safe); p++)
        if (*p != '"' && *p != '\\' && (unsigned char)*p >= 0x20) safe[n++] = *p;
    safe[n] = 0;
    snprintf(extra, sizeof(extra), "Content-Disposition: attachment; filename=\"%s\"\r\n", safe);
    count(&web.transfers, 1);
    int failed = send_head(c, 200, pc5_ends_with_ci(name, ".zip") ? "application/zip" : "application/octet-stream",
                           (uint64_t)st.st_size, extra);
    while (!failed) {
        ssize_t got = read(fd, c->buffer, BUFFER);
        if (got <= 0) break;
        failed = send_all(c->sock, c->buffer, (size_t)got) || stopping();
    }
    close(fd);
    count(&web.transfers, -1);
    say("PC5 web sent %s%s", name, failed ? " cut-short" : "");
}

/* ---- JSON ------------------------------------------------------------------------------- */

static void json_string(Pc5Buf *buf, const char *text)
{
    pc5_buf_puts(buf, "\"");
    for (const unsigned char *p = (const unsigned char *)(text ? text : ""); *p; p++) {
        if (*p == '"' || *p == '\\') pc5_buf_printf(buf, "\\%c", *p);
        else if (*p < 0x20 || *p == 0x7f) pc5_buf_printf(buf, "\\u%04x", *p);
        else pc5_buf_put(buf, p, 1);
    }
    pc5_buf_puts(buf, "\"");
}

/* ---- reading a request -------------------------------------------------------------------- */

/* Reads until the head has ended; 0, or -1 (answered when it could be). */
static int read_head(Conn *c)
{
    uint64_t started = now_ms();
    for (;;) {
        char *end = strstr(c->head, "\r\n\r\n");
        if (end) {
            c->head_end = (size_t)(end + 4 - c->head);
            return 0;
        }
        if (c->have >= HEAD) {
            respond_text(c, 413, "The request's head is too long");
            return -1;
        }
        int got = web.ops.recv(c->sock, c->head + c->have, HEAD - c->have, WAIT_MS);
        if (got == -2) {
            if (stopping() || now_ms() - started > REQUEST_MS) return -1;
            continue;
        }
        if (got <= 0) return -1;
        c->have += (size_t)got;
        c->head[c->have] = 0;
    }
}

static int parse_head(Conn *c)
{
    char target[PC5_PATH], version[16], *line = c->head, *next;
    c->length = UINT64_MAX;
    next = strstr(line, "\r\n");
    *next = 0;
    if (sscanf(line, "%7s %1023s %15s", c->method, target, version) != 3 || strncmp(version, "HTTP/1.", 7) ||
        target[0] != '/')
        return -1;
    char *query = strchr(target, '?');
    if (query) *query = 0;
    if (url_decode(target, c->path, sizeof(c->path))) return -1;
    for (line = next + 2; line < c->head + c->head_end - 2; line = next + 2) {
        next = strstr(line, "\r\n");
        if (!next) break;
        *next = 0;
        char *colon = strchr(line, ':'), *value;
        if (!colon) continue;
        *colon = 0;
        for (value = colon + 1; *value == ' ' || *value == '\t'; value++) {}
        if (!pc5_casecmp(line, "Content-Length")) {
            char *end;
            unsigned long long n = strtoull(value, &end, 10);
            if (end == value || *end) return -1;
            c->length = n;
        } else if (!pc5_casecmp(line, "Expect")) {
            c->expect_continue = !pc5_casecmp(value, "100-continue");
        } else if (!pc5_casecmp(line, "Authorization")) {
            pc5_copy(c->authorization, sizeof(c->authorization), value);
        } else if (!pc5_casecmp(line, "Transfer-Encoding")) {
            if (pc5_casecmp(value, "identity")) return -2;   /* chunked: not taken */
        }
    }
    return 0;
}

/* The body's next bytes: what was read with the head first. Bytes, 0 at its end, -1 on a failure. */
static int read_body(Conn *c, uint64_t *left, void *out, size_t size)
{
    if (!*left) return 0;
    if (size > *left) size = (size_t)*left;
    if (c->head_end < c->have) {
        size_t n = c->have - c->head_end;
        if (n > size) n = size;
        memcpy(out, c->head + c->head_end, n);
        c->head_end += n;
        *left -= n;
        return (int)n;
    }
    /* Waited for a little at a time, so a stop is seen within a wait. */
    for (uint64_t started = now_ms();;) {
        int got = web.ops.recv(c->sock, out, size, WAIT_MS);
        if (stopping()) return -1;
        if (got == -2 && now_ms() - started < BODY_MS) continue;
        if (got <= 0) return -1;
        *left -= (uint64_t)got;
        return got;
    }
}

/* A small body (at most max bytes) in buf, terminated. 0, or -1 (answered). */
static int read_small_body(Conn *c, size_t max, Pc5Buf *buf)
{
    if (c->length == UINT64_MAX) return respond_text(c, 411, "Content-Length is needed"), -1;
    if (c->length > max) return respond_text(c, 413, "Too long"), -1;
    if (c->expect_continue) send_all(c->sock, "HTTP/1.1 100 Continue\r\n\r\n", 25);
    uint64_t left = c->length;
    if (pc5_buf_reserve(buf, (size_t)left + 1)) return respond_text(c, 500, "Out of memory"), -1;
    while (left) {
        int got = read_body(c, &left, buf->data + buf->len, (size_t)left);
        if (got <= 0) return -1;
        buf->len += (size_t)got;
    }
    pc5_buf_terminate(buf);
    return 0;
}

/* ---- logging in ------------------------------------------------------------------------------ */

static int base64_value(int c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static int base64_decode(const char *in, char *out, size_t size)
{
    size_t n = 0;
    unsigned bits = 0;
    int have = 0;
    for (; *in && *in != '='; in++) {
        int v = base64_value((unsigned char)*in);
        if (v < 0) return -1;
        bits = (bits << 6) | (unsigned)v;
        have += 6;
        if (have >= 8) {
            have -= 8;
            if (n + 1 >= size) return -1;
            out[n++] = (char)((bits >> have) & 0xff);
        }
    }
    out[n] = 0;
    return 0;
}

/* 1 when the request may go on: no username or password set, or the right ones given. */
static int logged_in(Conn *c)
{
    char user[PC5_FTP_LOGIN], password[PC5_FTP_LOGIN], given[2 * PC5_FTP_LOGIN + 8];
    pthread_mutex_lock(&web.lock);
    pc5_copy(user, sizeof(user), web.user);
    pc5_copy(password, sizeof(password), web.password);
    pthread_mutex_unlock(&web.lock);
    if (!user[0] && !password[0]) return 1;
    if (strncmp(c->authorization, "Basic ", 6) ||
        base64_decode(c->authorization + 6, given, sizeof(given)))
        return 0;
    char *colon = strchr(given, ':');
    if (!colon) return 0;
    *colon = 0;
    if ((!user[0] || !strcmp(given, user)) && (!password[0] || !strcmp(colon + 1, password))) return 1;
    say("PC5 web login refused user=%s", given);
    usleep(1000000);   /* slows guessing */
    return 0;
}

void pc5_web_login(const char *user, const char *password)
{
    pthread_mutex_lock(&web.lock);
    pc5_copy(web.user, sizeof(web.user), user ? user : "");
    pc5_copy(web.password, sizeof(web.password), password ? password : "");
    pthread_mutex_unlock(&web.lock);
}

/* ---- the answers ------------------------------------------------------------------------------- */

static void api_status(Conn *c)
{
    Pc5Buf buf = { 0 };
    uint64_t free_root = 0, free_install = 0;
    pc5_free_bytes(web.root, &free_root);
    int install = web.install[0] && pc5_is_dir(web.install);
    if (install) pc5_free_bytes(web.install, &free_install);
    pc5_buf_printf(&buf, "{\"version\":");
    json_string(&buf, PC5_VERSION);
    pc5_buf_printf(&buf, ",\"address\":");
    json_string(&buf, web.address);
    pc5_buf_printf(&buf, ",\"free\":%llu,\"install\":%s,\"install_free\":%llu}", (unsigned long long)free_root,
                   install ? "true" : "false", (unsigned long long)free_install);
    respond_buf(c, &buf);
}

/* A game's launch.txt: beside the program PC5 starts (its batch file when it has one). 0, or
 * -1 when it isn't a game PC5 installed. */
static int launch_path(const char *id, char *out, size_t size)
{
    Pc5Info info;
    char top[PC5_PATH], dir[PC5_PATH];
    if (pc5_info_read(web.root, id, &info) || pc5_game_folder(web.root, id, top, sizeof(top))) return -1;
    pc5_copy(dir, sizeof(dir), info.exe);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = 0;
    else dir[0] = 0;
    return pc5_format(out, size, "%s%s%s/launch.txt", top, dir[0] ? "/" : "", dir);
}

static int env_path(const Pc5Game *game, char *out, size_t size)
{
    if (!game->prefix[0]) return -1;
    return pc5_format(out, size, "%s/prefixes/%s/pc5/env.txt", web.root, game->prefix);
}

/* A game's winedebug.txt: what Wine logs for it (WINEDEBUG), as a report sometimes asks. */
static int debug_path(const Pc5Game *game, char *out, size_t size)
{
    if (!game->prefix[0]) return -1;
    return pc5_format(out, size, "%s/prefixes/%s/pc5/winedebug.txt", web.root, game->prefix);
}

/* A game's settings in a line, as Report a problem gives them (pc5_ui.c's report_open). */
static void game_settings(const Pc5Game *g, char *out, size_t size)
{
    Pc5Controls k;
    pc5_controls_load(web.root, g->prefix, &k);
    snprintf(out, size, "%d-bit, draws with %s; Graphics API %s; Controller %s; Frame generation %s; Memory %s; CPU sharing %s%s",
             g->bits, pc5_api_name((Pc5Api)g->api), pc5_graphics_name(k.graphics, g->api), pc5_pad_mode_name(k.mode),
             k.frame_generation ? "on" : "off", k.memory_4gb ? "4 GB" : "Auto", k.cpu_sharing ? "on" : "off",
             k.steam_off ? "; Steam emulator off" : "");
}

/* GitHub's issue form for the game, filled in from its newest kept log (as the console's
 * Report a problem does), into out; "" when it has none. */
static void game_report(const Pc5Game *g, const char *settings, char *out, size_t size)
{
    Pc5LogIssue issue;
    out[0] = 0;
    memset(&issue, 0, sizeof(issue));
    if (pc5_logs_newest(web.root, g->id, issue.file, sizeof(issue.file))) return;
    const char *name = strrchr(issue.file, '/') ? strrchr(issue.file, '/') + 1 : issue.file;
    issue.problem = !strncmp(name, "issue-", 6) ? PC5_LOG_CRASHED : PC5_LOG_FINE;
    pc5_copy(issue.profile, sizeof(issue.profile), g->id);
    pc5_copy(issue.exe, sizeof(issue.exe), g->executable);
    if (pc5_report_url_settings(&issue, g->title, settings, out, size)) out[0] = 0;
}

/* Which game it is (pc5_identity.h): what is kept, else the folder read and what it says kept. */
static void game_identity(const Pc5Game *g, Pc5Identity *id)
{
    char folder[PC5_PATH];
    if (!pc5_identity_read(web.root, g->prefix, id)) return;
    memset(id, 0, sizeof(*id));
    if (!pc5_game_folder(web.root, g->id, folder, sizeof(folder)) && !pc5_identity_scan(folder, id))
        pc5_identity_write(web.root, g->prefix, id);
}

/* A text file's contents into buf as a JSON string ("" when there is none). */
static void json_file(Pc5Buf *buf, const char *path, size_t max)
{
    Pc5Buf text = { 0 };
    if (!path || pc5_read_file(path, &text, max) || pc5_buf_terminate(&text)) json_string(buf, "");
    else json_string(buf, (const char *)text.data);
    pc5_buf_free(&text);
}

static void api_games(Conn *c)
{
    Pc5Library *library = pc5_alloc(sizeof(*library));
    Pc5Buf buf = { 0 };
    char path[PC5_PATH], folder[PC5_PATH];
    if (!library) {
        respond_text(c, 500, "Out of memory");
        return;
    }
    pc5_library_load(web.root, library);
    pc5_buf_puts(&buf, "[");
    for (int i = 0; i < library->count; i++) {
        const Pc5Game *g = &library->games[i];
        const char *name = "";
        if (!pc5_game_folder(web.root, g->id, folder, sizeof(folder))) {
            name = strrchr(folder, '/');
            name = name ? name + 1 : folder;
        }
        pc5_buf_printf(&buf, "%s{\"id\":", i ? "," : "");
        json_string(&buf, g->id);
        pc5_buf_puts(&buf, ",\"title\":");
        json_string(&buf, g->title);
        pc5_buf_puts(&buf, ",\"detail\":");
        json_string(&buf, g->detail);
        pc5_buf_puts(&buf, ",\"folder\":");
        json_string(&buf, name);
        pc5_buf_printf(&buf, ",\"playable\":%s,\"problem\":", g->playable ? "true" : "false");
        json_string(&buf, g->problem);
        pc5_buf_printf(&buf, ",\"log\":%s", pc5_logs_newest(web.root, g->id, path, sizeof(path)) ? "false" : "true");
        int launch = !launch_path(g->id, path, sizeof(path));
        pc5_buf_printf(&buf, ",\"editable\":%s,\"launch\":", launch ? "true" : "false");
        json_file(&buf, launch ? path : NULL, LAUNCH_MAX);
        pc5_buf_puts(&buf, ",\"env\":");
        json_file(&buf, env_path(g, path, sizeof(path)) ? NULL : path, ENV_MAX);
        pc5_buf_puts(&buf, ",\"debug\":");
        json_file(&buf, debug_path(g, path, sizeof(path)) ? NULL : path, ENV_MAX);
        {
            char settings[300], report[PC5_REPORT_URL];
            game_settings(g, settings, sizeof(settings));
            game_report(g, settings, report, sizeof(report));
            pc5_buf_puts(&buf, ",\"settings\":");
            json_string(&buf, settings);
            pc5_buf_puts(&buf, ",\"report\":");
            json_string(&buf, report);
        }
        pc5_buf_printf(&buf, ",\"usb\":%s,\"bits\":%d", g->usb ? "true" : "false", g->bits);
        {
            Pc5Identity identity;
            game_identity(g, &identity);
            pc5_buf_printf(&buf, ",\"steam_appid\":%u,\"gog_id\":%llu,\"identity_source\":", (unsigned)identity.steam_appid,
                           (unsigned long long)identity.gog_id);
            json_string(&buf, identity.source);
            pc5_buf_puts(&buf, ",\"store_name\":");
            json_string(&buf, identity.name);
        }
        pc5_buf_puts(&buf, "}");
    }
    pc5_buf_puts(&buf, "]");
    pc5_free(library);
    respond_buf(c, &buf);
}

/* /api/games/<id>/<what>: log (GET), launch, env or debug (GET, PUT), steam_appid (PUT: the
 * game's Steam app id by hand, digits, or nothing to read the folder again). */
static void api_game(Conn *c, const char *rest)
{
    char id[PC5_ID], path[PC5_PATH];
    const char *slash = strchr(rest, '/');
    if (!slash || (size_t)(slash - rest) >= sizeof(id)) {
        respond_text(c, 404, "No such game");
        return;
    }
    memcpy(id, rest, (size_t)(slash - rest));
    id[slash - rest] = 0;
    const char *what = slash + 1;
    Pc5Library *library = pc5_alloc(sizeof(*library));
    if (!library) {
        respond_text(c, 500, "Out of memory");
        return;
    }
    pc5_library_load(web.root, library);
    int at = pc5_library_find(library, id);
    Pc5Game game;
    if (at >= 0) game = library->games[at];
    pc5_free(library);
    if (at < 0) {
        respond_text(c, 404, "No such game");
        return;
    }
    if (!strcmp(what, "log")) {
        if (strcmp(c->method, "GET")) respond_text(c, 405, "GET only");
        else if (pc5_logs_newest(web.root, id, path, sizeof(path))) respond_text(c, 404, "No log kept for this game");
        else send_file(c, path, strrchr(path, '/') + 1);
        return;
    }
    if (!strcmp(what, "steam_appid")) {
        Pc5Buf body = { 0 };
        if (strcmp(c->method, "PUT")) {
            respond_text(c, 405, "PUT only");
            return;
        }
        if (read_small_body(c, 64, &body)) {
            pc5_buf_free(&body);
            return;
        }
        char digits[24];
        size_t n = 0, bad = 0;
        for (size_t i = 0; i < body.len; i++) {
            char ch = (char)body.data[i];
            if (ch >= '0' && ch <= '9' && n + 1 < sizeof(digits)) digits[n++] = ch;
            else if (ch != ' ' && ch != '\n' && ch != '\r' && ch != '\t') bad = 1;
        }
        digits[n] = 0;
        pc5_buf_free(&body);
        unsigned long appid = n ? strtoul(digits, NULL, 10) : 0;
        if (bad || (n && (!appid || appid > 99999999ul))) {
            respond_text(c, 400, "A Steam app ID is a number (the one in the game's store.steampowered.com/app/ link)");
            return;
        }
        int failed = !game.prefix[0] || pc5_identity_set_steam(web.root, game.prefix, (uint32_t)appid);
        say("PC5 web steam_appid for %s: %lu%s", id, appid, failed ? " failed" : "");
        respond_text(c, failed ? 500 : 200, failed ? "The console couldn't keep it" : "Saved");
        return;
    }
    int launch = !strcmp(what, "launch"), debug = !strcmp(what, "debug");
    if (!launch && !debug && strcmp(what, "env")) {
        respond_text(c, 404, "Not here");
        return;
    }
    if (launch ? launch_path(id, path, sizeof(path))
               : debug ? debug_path(&game, path, sizeof(path)) : env_path(&game, path, sizeof(path))) {
        respond_text(c, 404, "PC5 didn't install this game, so it has no such file");
        return;
    }
    if (!strcmp(c->method, "GET")) {
        Pc5Buf text = { 0 };
        if (pc5_read_file(path, &text, launch ? LAUNCH_MAX : ENV_MAX)) respond_text(c, 200, "");
        else respond(c, 200, "text/plain; charset=utf-8", text.data, text.len);
        pc5_buf_free(&text);
        return;
    }
    if (strcmp(c->method, "PUT")) {
        respond_text(c, 405, "GET or PUT");
        return;
    }
    Pc5Buf body = { 0 };
    if (read_small_body(c, launch ? LAUNCH_MAX : ENV_MAX, &body)) {
        pc5_buf_free(&body);
        return;
    }
    char dir[PC5_PATH];
    pc5_copy(dir, sizeof(dir), path);
    *strrchr(dir, '/') = 0;
    int failed;
    /* Nothing in it: the file goes, as if there never was one. */
    int empty = 1;
    for (size_t i = 0; i < body.len; i++)
        if (body.data[i] != ' ' && body.data[i] != '\n' && body.data[i] != '\r' && body.data[i] != '\t') empty = 0;
    if (empty) failed = unlink(path) && errno != ENOENT;
    else failed = pc5_mkdirs(dir) || pc5_write_file(path, body.data, body.len);
    say("PC5 web %s %s for %s bytes=%zu%s", empty ? "deleted" : "wrote",
        launch ? "launch.txt" : debug ? "winedebug.txt" : "env.txt", id, body.len,
        failed ? " failed" : "");
    pc5_buf_free(&body);
    respond_text(c, failed ? 500 : 200, failed ? "The console couldn't write it" : "Saved");
}

typedef struct LogFile {
    char name[256];
    uint64_t size;
    int64_t time;
} LogFile;

static int newest_first(const void *a, const void *b)
{
    const LogFile *x = a, *y = b;
    if (x->time != y->time) return x->time < y->time ? 1 : -1;
    return strcmp(x->name, y->name);
}

static void api_logs(Conn *c)
{
    char logs[PC5_PATH], path[PC5_PATH];
    LogFile *list = pc5_alloc(sizeof(*list) * LOGS_MAX);
    int n = 0;
    Pc5Buf buf = { 0 };
    DIR *dir;
    struct dirent *entry;
    if (!list) {
        respond_text(c, 500, "Out of memory");
        return;
    }
    if (!pc5_format(logs, sizeof(logs), "%s/logs", web.root) && (dir = opendir(logs))) {
        while (n < LOGS_MAX && (entry = readdir(dir))) {
            struct stat st;
            if (!pc5_ends_with_ci(entry->d_name, ".zip") || strlen(entry->d_name) >= sizeof(list[n].name) ||
                pc5_join(path, sizeof(path), logs, entry->d_name) || stat(path, &st) || !S_ISREG(st.st_mode))
                continue;
            pc5_copy(list[n].name, sizeof(list[n].name), entry->d_name);
            list[n].size = (uint64_t)st.st_size;
            list[n].time = (int64_t)st.st_mtime;
            n++;
        }
        closedir(dir);
    }
    qsort(list, (size_t)n, sizeof(*list), newest_first);
    pc5_buf_puts(&buf, "[");
    for (int i = 0; i < n; i++) {
        pc5_buf_puts(&buf, i ? ",{\"name\":" : "{\"name\":");
        json_string(&buf, list[i].name);
        pc5_buf_printf(&buf, ",\"size\":%llu,\"time\":%lld}", (unsigned long long)list[i].size, (long long)list[i].time);
    }
    pc5_buf_puts(&buf, "]");
    pc5_free(list);
    respond_buf(c, &buf);
}

static void get_log(Conn *c, const char *name)
{
    char path[PC5_PATH];
    if (!name[0] || strchr(name, '/') || !strcmp(name, "..") || !pc5_ends_with_ci(name, ".zip") ||
        pc5_format(path, sizeof(path), "%s/logs/%s", web.root, name)) {
        respond_text(c, 404, "No such log");
        return;
    }
    send_file(c, path, name);
}

/* PUT /upload/<path>: a file of a game's folder into the INSTALL folder, kept as name.pc5part
 * until all of it came. */
static void upload(Conn *c, const char *relative)
{
    char path[PC5_PATH], part[PC5_PATH + 16], dir[PC5_PATH];
    if (strcmp(c->method, "PUT")) {
        respond_text(c, 405, "PUT only");
        return;
    }
    if (!web.install[0] || !pc5_is_dir(web.install)) {
        respond_text(c, 503, "PC5's INSTALL folder isn't there");
        return;
    }
    if (pc5_web_upload_path_ok(relative) || pc5_format(path, sizeof(path), "%s/%s", web.install, relative) ||
        pc5_format(part, sizeof(part), "%s.pc5part", path)) {
        respond_text(c, 400, "Not a path in the INSTALL folder");
        return;
    }
    if (c->length == UINT64_MAX) {
        respond_text(c, 411, "Content-Length is needed");
        return;
    }
    pc5_copy(dir, sizeof(dir), path);
    *strrchr(dir, '/') = 0;
    struct stat st;
    int fd = -1;
    if ((!stat(path, &st) && S_ISDIR(st.st_mode)) || pc5_mkdirs(dir) ||
        (fd = open(part, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
        respond_text(c, 500, "The console couldn't write there");
        return;
    }
    if (c->expect_continue) send_all(c->sock, "HTTP/1.1 100 Continue\r\n\r\n", 25);
    count(&web.transfers, 1);
    uint64_t left = c->length, bytes = 0;
    int failed = 0, write_failed = 0;
    while (left) {
        int got = read_body(c, &left, c->buffer, BUFFER);
        if (got <= 0) {
            failed = 1;
            break;
        }
        for (int done = 0; done < got;) {
            ssize_t put = write(fd, c->buffer + done, (size_t)(got - done));
            if (put <= 0) {
                write_failed = 1;
                break;
            }
            done += (int)put;
        }
        if (write_failed) break;
        bytes += (uint64_t)got;
    }
    if (close(fd)) write_failed = 1;
    int kept = !failed && !write_failed && !rename(part, path);
    if (!kept) unlink(part);
    /* Counted before the answer, so whoever is told it was received sees it counted. */
    pthread_mutex_lock(&web.lock);
    if (kept) web.uploads++;
    pthread_mutex_unlock(&web.lock);
    count(&web.transfers, -1);
    say("PC5 web received %s bytes=%llu%s", relative, (unsigned long long)bytes,
        write_failed ? " write-failed" : failed ? " cut-short" : kept ? "" : " not-kept");
    if (kept) respond_text(c, 201, "Received");
    else if (!failed) respond_text(c, 500, write_failed ? "The console couldn't write it (is it full?)" : "Couldn't keep it");
}

static void answer(Conn *c)
{
    const char *p = c->path;
    if (!strcmp(p, "/") || !strcmp(p, "/index.html")) {
        size_t size = 0;
        for (int i = 0; pc5_web_page[i]; i++) size += strlen(pc5_web_page[i]);
        if (send_head(c, 200, "text/html; charset=utf-8", size, NULL)) return;
        for (int i = 0; pc5_web_page[i]; i++)
            if (send_all(c->sock, pc5_web_page[i], strlen(pc5_web_page[i]))) return;
    } else if (!strcmp(p, "/api/status")) api_status(c);
    else if (!strcmp(p, "/api/games")) api_games(c);
    else if (!strncmp(p, "/api/games/", 11)) api_game(c, p + 11);
    else if (!strcmp(p, "/api/logs")) api_logs(c);
    else if (!strncmp(p, "/logs/", 6)) get_log(c, p + 6);
    else if (!strncmp(p, "/upload/", 8)) upload(c, p + 8);
    else respond_text(c, 404, "Not here");
}

static void *conn_thread(void *arg)
{
    Conn *c = arg;
    if (!read_head(c)) {
        int parsed = parse_head(c);
        if (parsed == -2) respond_text(c, 411, "Send it with Content-Length");
        else if (parsed) respond_text(c, 400, "PC5 didn't understand that");
        else if (!logged_in(c))
            send_head(c, 401, "text/plain; charset=utf-8", 0, "WWW-Authenticate: Basic realm=\"PC5\", charset=\"UTF-8\"\r\n");
        else answer(c);
    }
    web.ops.close(c->sock);
    pc5_free(c->buffer);
    pc5_free(c);
    count(&web.connections, -1);
    return NULL;
}

static void *listen_thread(void *arg)
{
    (void)arg;
    while (!stopping()) {
        int client = web.ops.accept(web.listener, WAIT_MS);
        if (client < 0) continue;
        pthread_mutex_lock(&web.lock);
        int full = web.connections >= PC5_WEB_SESSIONS;
        if (!full) web.connections++;
        pthread_mutex_unlock(&web.lock);
        Conn *c = full ? NULL : pc5_alloc(sizeof(*c));
        if (c) c->buffer = pc5_alloc(BUFFER);
        if (!c || !c->buffer) {
            static const char busy[] = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nRetry-After: 1\r\n"
                                       "Connection: close\r\n\r\n";
            web.ops.send(client, busy, sizeof(busy) - 1, 1000);
            web.ops.close(client);
            if (c) pc5_free(c);
            if (!full) count(&web.connections, -1);
            continue;
        }
        c->sock = client;
        if (pc5_thread_start(NULL, conn_thread, c, 1)) {
            web.ops.close(client);
            pc5_free(c->buffer);
            pc5_free(c);
            count(&web.connections, -1);
        }
    }
    return NULL;
}

/* ---- starting and stopping --------------------------------------------------------------- */

void pc5_web_init(const Pc5FtpOps *ops)
{
    if (!ops) return;
    web.ops = *ops;
    web.have_ops = 1;
}

int pc5_web_start(const Pc5WebConfig *config, uint16_t port, char *error, size_t size)
{
    uint32_t ip = 0;

    pc5_web_stop();
    if (!web.have_ops) return pc5_format(error, size, "There's no network here."), -1;
    if (!config || !config->root || !config->root[0]) return pc5_format(error, size, "PC5 has no folder to show."), -1;
    pc5_copy(web.root, sizeof(web.root), config->root);
    pc5_copy(web.install, sizeof(web.install), config->install_dir ? config->install_dir : "");
    web.listener = web.ops.listen(port, 8);
    if (web.listener < 0) return pc5_format(error, size, "Port %u is taken, or there's no network.", (unsigned)port), -1;
    if (!web.ops.lan_ip || web.ops.lan_ip(&ip) || !ip) snprintf(web.address, sizeof(web.address), ":%u", (unsigned)port);
    else {
        const uint8_t *b = (const uint8_t *)&ip;
        snprintf(web.address, sizeof(web.address), "%u.%u.%u.%u:%u", b[0], b[1], b[2], b[3], (unsigned)port);
    }
    pthread_mutex_lock(&web.lock);
    web.stopping = 0;
    web.uploads = 0;
    web.last_ms = now_ms();
    pthread_mutex_unlock(&web.lock);
    if (pc5_thread_start(&web.thread, listen_thread, NULL, 0)) {
        web.ops.close(web.listener);
        web.listener = -1;
        return pc5_format(error, size, "PC5 can't start it."), -1;
    }
    web.running = 1;
    say("PC5 web on address=%s install=%s", web.address, web.install[0] ? web.install : "-");
    return 0;
}

void pc5_web_stop(void)
{
    if (!web.running) return;
    pthread_mutex_lock(&web.lock);
    web.stopping = 1;
    pthread_mutex_unlock(&web.lock);
    pthread_join(web.thread, NULL);
    web.ops.close(web.listener);
    web.listener = -1;
    for (int i = 0; i < 60; i++) {
        pthread_mutex_lock(&web.lock);
        int left = web.connections;
        pthread_mutex_unlock(&web.lock);
        if (!left) break;
        usleep(50000);
    }
    web.running = 0;
    say("PC5 web off uploads=%u", web.uploads);
}

void pc5_web_status(Pc5WebStatus *status)
{
    memset(status, 0, sizeof(*status));
    pthread_mutex_lock(&web.lock);
    status->running = web.running;
    pc5_copy(status->address, sizeof(status->address), web.address);
    status->transfers = web.transfers;
    status->uploads = web.uploads;
    status->idle_ms = web.transfers ? 0 : now_ms() - web.last_ms;
    pthread_mutex_unlock(&web.lock);
}
