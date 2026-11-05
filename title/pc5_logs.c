/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: keeping logs for a report (pc5_logs.h). */
#include "pc5_logs.h"
#include "pc5_zip.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* prospero-win's sessions (native/pw_diagnostics.h, PW_DIAGNOSTICS_SESSIONS):
 * session-0.log to session-7.log in turn, next.txt naming the next. A long
 * session also has session-N.start.log (how it began),
 * session-N.crash.log (the part with its first crash, when later parts would
 * have pushed it out) and session-N.previous.log (the part before the newest). */
enum { SESSIONS = 8, NO_PICTURE_S = 60 };

/* A session's parts in the order they were written, the newest last, and the end of the
 * name each is kept by. */
static const char *const PARTS[][2] = {
    { "start.", "-start" }, { "crash.", "-crash" }, { "previous.", "-earlier" }, { "", "" },
};
enum { PART_COUNT = 4, NEWEST = PART_COUNT - 1 };

/* The game's own files kept with its log: a log's last 2 MiB, a dump of up
 * to 16 MiB, 32 files and 40 MiB in all, found among 20000 entries at most,
 * written from just before the session began to a minute after it ended. */
enum {
    GAME_TEXT_MAX = 2 << 20, GAME_DUMP_MAX = 16 << 20, GAME_FILES_MAX = 32, GAME_DEPTH_MAX = 8,
    GAME_ENTRIES_MAX = 20000, GAME_BEFORE_S = 2, GAME_AFTER_S = 60,
};
#define GAME_BYTES_MAX (40ull << 20)

/* The rest of the line after needle, if the line holds it. */
static const char *after(const char *line, const char *end, const char *needle)
{
    size_t n = strlen(needle);
    for (const char *p = line; p + n <= end; p++)
        if (!memcmp(p, needle, n)) return p + n;
    return NULL;
}

static void line_text(const char *from, const char *end, char *out, size_t size)
{
    size_t n = (size_t)(end - from);
    while (n && (from[n - 1] == '\r' || from[n - 1] == ' ')) n--;
    if (n >= size) n = size - 1;
    memcpy(out, from, n);
    out[n] = 0;
}

/* A word of the line (up to the next space). */
static void word(const char *from, const char *end, char *out, size_t size)
{
    size_t n = 0;
    while (from + n < end && from[n] != ' ' && from[n] != '\r' && n + 1 < size) n++;
    memcpy(out, from, n);
    out[n] = 0;
}

/* 1 when the line holds needle, its case aside. */
static int contains_ci_range(const char *line, const char *end, const char *needle)
{
    size_t n = strlen(needle);
    for (const char *p = line; p + n <= end; p++) {
        size_t i = 0;
        while (i < n && pc5_lower((unsigned char)p[i]) == pc5_lower((unsigned char)needle[i])) i++;
        if (i == n) return 1;
    }
    return 0;
}

Pc5LogProblem pc5_log_classify(const char *text, size_t length, Pc5LogIssue *issue)
{
    const char *end_of_text = text + length, *line = text;
    double first_t = -1, last_t = -1;
    int ended = 0, launch_failed = 0, unhandled = 0, forced = 0, pc5_problem = 0, picture = 0, asked = 0, handed_off = 0;
    char crash[200] = "", problem[200] = "", installer[160] = "", copy_file[160] = "";
    int no_opengl = 0;

    memset(issue, 0, sizeof(*issue));
    pc5_copy(issue->profile, sizeof(issue->profile), "launcher");
    while (line < end_of_text) {
        const char *end = memchr(line, '\n', (size_t)(end_of_text - line)), *found;
        if (!end) end = end_of_text;
        if (line == text && (found = after(line, end, " profile="))) {
            word(found, end, issue->profile, sizeof(issue->profile));
            if ((found = after(line, end, " time="))) issue->time = strtoll(found, NULL, 10);
        }
        if ((found = after(line, end, " t="))) {
            double t = strtod(found, NULL);
            if (first_t < 0) first_t = t;
            last_t = t;
        }
        if ((found = after(line, end, "session_end reason="))) {
            ended = 1;
            launch_failed |= !strncmp(found, "wine64-launch-failed", 20);
        }
        if (!unhandled && ((found = after(line, end, "Unhandled exception")) ||
                           (found = after(line, end, "Unhandled page fault")) ||
                           (found = after(line, end, "unhandled exception")))) {
            const char *from = after(line, end, "WINE ");
            unhandled = 1;
            line_text(from ? from : line, end, crash, sizeof(crash));
        }
        if (after(line, end, "close timeout")) forced = 1;
        if (after(line, end, "close requested by=combo")) asked = 1;   /* the player held touchpad + Options */
        if (!pc5_problem && (found = after(line, end, "PC5 problem "))) {
            pc5_problem = 1;
            line_text(found, end, problem, sizeof(problem));
        }
        if (after(line, end, "display released to vulkan")) picture = 1;
        /* Causes a player can act on. A program the game tried to start that PC5 can't hand over to:
         * "wine-ps5: C:\\...\\vc_redist.x64.exe is a helper: not handed off". */
        if (!installer[0] && (found = after(line, end, " is a helper: not handed off"))) {
            const char *name_end = found - strlen(" is a helper: not handed off"), *name = name_end;
            while (name > line && name[-1] != '\\' && name[-1] != '/' && name[-1] != ' ') name--;
            if (name_end > name) line_text(name, name_end, installer, sizeof(installer));
            /* Only an installer: a crash reporter a game starts beside it (Unity's
             * UnityCrashHandler64.exe) is handed over to nothing either, and stops nothing. */
            static const char *const setups[] = { "redist", "setup", "install", "unins", "dxweb", "dotnet", "prereq",
                                                  "physx", "oalinst" };
            int is_setup = 0;
            for (size_t i = 0; i < sizeof(setups) / sizeof(setups[0]) && installer[0] && !is_setup; i++)
                is_setup = contains_ci_range(installer, installer + strlen(installer), setups[i]);
            if (!is_setup) installer[0] = 0;
        }
        if (!copy_file[0] && after(line, end, "trace:loaddll:")) {
            static const char *const others[] = { "OnlineFix64.dll", "OnlineFix.dll", "steam_api64_o.dll", "steam_api_o.dll" };
            for (size_t i = 0; i < sizeof(others) / sizeof(others[0]) && !copy_file[0]; i++)
                if (contains_ci_range(line, end, others[i])) pc5_copy(copy_file, sizeof(copy_file), others[i]);
        }
        if (after(line, end, "called without a rendering context")) no_opengl = 1;
        /* A game's own launcher that started the game itself (Warcraft III's Frozen Throne.exe
         * starting war3.exe): PC5 starts that program in its place, so no picture is expected. */
        if (after(line, end, "PW_WINE64 restart ") && after(line, end, " reason=handoff")) handed_off = 1;
        if (after(line, end, "PW_WINE64 alive ")) {
            const char *shown = after(line, end, " shown="), *vk = after(line, end, " vk_shown=");
            if ((shown && strtoull(shown, NULL, 10) > 0) || (vk && strtoull(vk, NULL, 10) > 0)) picture = 1;
        }
        /* Where the game is: its prefix, its program, and the console it ran on. */
        if (!issue->prefix[0] && after(line, end, "PW_WINE64 profile id=") && (found = after(line, end, " prefix=")))
            word(found, end, issue->prefix, sizeof(issue->prefix));
        if (!issue->exe[0] && after(line, end, "PW_WINE64 ntdll=") && (found = after(line, end, " exe=")))
            line_text(found, end, issue->exe, sizeof(issue->exe));
        if (!issue->firmware[0] && after(line, end, "PC5 system ") && (found = after(line, end, " firmware=")))
            word(found, end, issue->firmware, sizeof(issue->firmware));
        line = end + 1;
    }
    if (installer[0]) {
        issue->cause = PC5_CAUSE_INSTALLER;
        pc5_copy(issue->cause_what, sizeof(issue->cause_what), installer);
    } else if (copy_file[0]) {
        issue->cause = PC5_CAUSE_COPY_FILES;
        pc5_copy(issue->cause_what, sizeof(issue->cause_what), copy_file);
    } else if (no_opengl) {
        issue->cause = PC5_CAUSE_NO_OPENGL;
    }
    issue->closed = ended;
    issue->ended = issue->time;
    if (first_t >= 0 && last_t > first_t) issue->ended += (int64_t)(last_t - first_t);
    if (!strcmp(issue->profile, "launcher")) {
        if (pc5_problem) {
            issue->problem = PC5_LOG_PC5_PROBLEM;
            pc5_copy(issue->detail, sizeof(issue->detail), problem);
        }
    } else if (unhandled) {
        issue->problem = PC5_LOG_GAME_CRASHED;
        pc5_copy(issue->detail, sizeof(issue->detail), crash);
    } else if (asked || handed_off) {
        /* The player closed it, quickly or not, and a game that ignored Alt+F4 was ended as asked;
         * or the program handed off to the game it started. */
    } else if (forced) {
        issue->problem = PC5_LOG_FORCED;
    } else if (launch_failed) {
        issue->problem = PC5_LOG_NO_PICTURE;
        pc5_copy(issue->detail, sizeof(issue->detail), "Wine couldn't start it.");
    } else if (!picture && first_t >= 0 && last_t - first_t < NO_PICTURE_S) {
        /* A game closed with the PS button soon after starting looks the same when it
         * ended unasked (no end line): both are reported, a quick close rarely. */
        issue->problem = PC5_LOG_NO_PICTURE;
    } else if (issue->cause && first_t >= 0 && last_t - first_t < NO_PICTURE_S) {
        /* It showed something (an installer's question) and closed by itself soon after, for a
         * reason the log shows (RV There Yet's starter, closed when vc_redist couldn't start). */
        issue->problem = PC5_LOG_CLOSED_EARLY;
    }
    if (!strcmp(issue->profile, "launcher")) issue->cause = PC5_CAUSE_NONE;
    return issue->problem;
}

void pc5_log_cause_text(const Pc5LogIssue *issue, char *out, size_t size)
{
    switch (issue->cause) {
    case PC5_CAUSE_INSTALLER:
        snprintf(out, size, "the game tried to start %s, an installer, which can't run on the console", issue->cause_what);
        break;
    case PC5_CAUSE_COPY_FILES:
        snprintf(out, size, "its copy loaded %s, which isn't one of the game's files as its store ships it", issue->cause_what);
        break;
    case PC5_CAUSE_NO_OPENGL:
        snprintf(out, size, "its OpenGL never started: it drew with no rendering context");
        break;
    case PC5_CAUSE_CRASH_REPORT:
        snprintf(out, size, "the game's own crash report says: %s", issue->cause_what);
        break;
    default:
        if (size) out[0] = 0;
        break;
    }
}

const char *pc5_log_problem_text(Pc5LogProblem problem)
{
    switch (problem) {
    case PC5_LOG_GAME_CRASHED: return "stopped with an error";
    case PC5_LOG_NO_PICTURE: return "closed before showing anything";
    case PC5_LOG_FORCED: return "had to be closed";
    case PC5_LOG_CRASHED: return "ended unexpectedly";
    case PC5_LOG_PC5_PROBLEM: return "PC5 logged a problem";
    case PC5_LOG_CLOSED_EARLY: return "closed soon after starting";
    case PC5_LOG_FINE: break;
    }
    return "nothing went wrong that PC5 could see";
}

/* ---- tidying ------------------------------------------------------------------------ */

/* Days since 1970-01-01 to a date (Howard Hinnant's civil_from_days). */
static void civil(int64_t days, int *year, int *month, int *day)
{
    days += 719468;
    int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    unsigned doe = (unsigned)(days - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    *day = (int)(doy - (153 * mp + 2) / 5 + 1);
    *month = (int)(mp < 10 ? mp + 3 : mp - 9);
    *year = (int)(yoe + era * 400 + (*month <= 2));
}

static int stamp(int64_t time, int offset_s, char *out, size_t size)
{
    int64_t local = time + offset_s, days = local >= 0 ? local / 86400 : (local - 86399) / 86400;
    int64_t seconds = local - days * 86400;
    int year, month, day;
    civil(days, &year, &month, &day);
    return pc5_format(out, size, "%04d-%02d-%02d_%02d-%02d", year, month, day, (int)(seconds / 3600),
                      (int)(seconds / 60 % 60));
}

/* The session running now: the one before next.txt's. -1 when unknown. */
static int current_session(const char *logs)
{
    char path[PC5_PATH];
    Pc5Buf text = { 0 };
    int current = -1;

    if (!pc5_join(path, sizeof(path), logs, "next.txt") && !pc5_read_file(path, &text, 32)) {
        int next = atoi((char *)text.data);
        if (next >= 0 && next < SESSIONS) current = (next + SESSIONS - 1) % SESSIONS;
    }
    pc5_buf_free(&text);
    return current;
}

/* A session's parts in order (PARTS), as one text. 0, or -1 when its newest part can't be
 * read. */
static int read_session(const char *logs, int session, Pc5Buf *text)
{
    char path[PC5_PATH];

    for (int i = 0; i < PART_COUNT; i++) {
        Pc5Buf part = { 0 };
        int failed;
        if (pc5_format(path, sizeof(path), "%s/session-%d.%slog", logs, session, PARTS[i][0])) return -1;
        if (i < NEWEST && !pc5_is_file(path)) continue;
        failed = pc5_read_file(path, &part, 64u << 20) || pc5_buf_put(text, part.data, part.len);
        pc5_buf_free(&part);
        if (failed && i == NEWEST) return -1;
    }
    return 0;
}

/* ---- the game's own files ------------------------------------------------------------- */

static int contains_ci(const char *text, const char *needle)
{
    size_t n = strlen(needle);
    for (; *text; text++) {
        size_t i = 0;
        while (i < n && text[i] && pc5_lower((unsigned char)text[i]) == needle[i]) i++;
        if (i == n) return 1;
    }
    return 0;
}

/* What a file of this name is to a report: 1 a log (its end is enough), 2 a
 * crash dump (whole or not at all), 0 nothing. */
static int game_file_kind(const char *name)
{
    if (pc5_ends_with_ci(name, ".dmp") || pc5_ends_with_ci(name, ".mdmp")) return 2;
    if (pc5_ends_with_ci(name, ".log")) return 1;
    if (pc5_ends_with_ci(name, ".txt"))
        return contains_ci(name, "log") || contains_ci(name, "crash") || contains_ci(name, "error");
    if (pc5_ends_with_ci(name, ".html") || pc5_ends_with_ci(name, ".htm"))
        return contains_ci(name, "log") || contains_ci(name, "crash");
    if (pc5_ends_with_ci(name, ".xml") || pc5_ends_with_ci(name, ".json") || pc5_ends_with_ci(name, ".runtime-xml"))
        return contains_ci(name, "crash");
    return 0;
}

typedef struct Collect {
    Pc5Zip *zip;
    Pc5Buf *about;
    Pc5LogIssue *issue;
    int64_t from, to;
    int offset_s;
    int files, entries;
    uint64_t bytes;
} Collect;

/* The last max bytes of a file (all of it when it is smaller); *cut when the start was left out. */
static int read_end(const char *path, uint64_t size, size_t max, Pc5Buf *out, int *cut)
{
    int fd = open(path, O_RDONLY);
    size_t want = size > max ? max : (size_t)size;
    *cut = size > max;
    if (fd < 0) return -1;
    if ((*cut && lseek(fd, (off_t)(size - want), SEEK_SET) < 0) || pc5_buf_reserve(out, want)) {
        close(fd);
        return -1;
    }
    while (out->len < want) {
        ssize_t n = read(fd, out->data + out->len, want - out->len);
        if (n <= 0) break;
        out->len += (size_t)n;
    }
    close(fd);
    return 0;
}

static void windows_path(const char *rel, char *out, size_t size)
{
    size_t n = 0;
    if (size < 4) return;
    out[n++] = 'C'; out[n++] = ':'; out[n++] = '\\';
    for (; *rel && n + 1 < size; rel++) out[n++] = *rel == '/' ? '\\' : *rel;
    out[n] = 0;
}

/* <ErrorMessage> of an Unreal crash report in data, as the issue's cause. */
static void crash_message(const Pc5Buf *data, Pc5LogIssue *issue)
{
    const char *text = (const char *)data->data, *end = text + data->len;
    const char *open = text ? after(text, end, "<ErrorMessage>") : NULL, *close = NULL;
    if (!open) return;
    for (const char *p = open; p + 15 <= end && !close; p++)
        if (!memcmp(p, "</ErrorMessage>", 15)) close = p;
    if (!close || close == open) return;
    size_t n = 0;
    for (const char *p = open; p < close && n + 1 < sizeof(issue->cause_what); p++) {
        char ch = *p == '\r' || *p == '\n' || *p == '\t' ? ' ' : *p;
        if (ch == ' ' && (!n || issue->cause_what[n - 1] == ' ')) continue;
        issue->cause_what[n++] = ch;
    }
    while (n && issue->cause_what[n - 1] == ' ') n--;
    issue->cause_what[n] = 0;
    if (n) issue->cause = PC5_CAUSE_CRASH_REPORT;
}

static void walk(Collect *c, const char *dir, const char *rel, int depth)
{
    DIR *d = opendir(dir);
    struct dirent *entry;

    if (!d) return;
    while ((entry = readdir(d)) && c->entries < GAME_ENTRIES_MAX && c->files < GAME_FILES_MAX) {
        char full[PC5_PATH], inner[PC5_PATH], shown[PC5_PATH];
        struct stat st;
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        c->entries++;
        if (pc5_join(full, sizeof(full), dir, entry->d_name) ||
            pc5_format(inner, sizeof(inner), "%s%s%s", rel, rel[0] ? "/" : "", entry->d_name) || stat(full, &st))
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < GAME_DEPTH_MAX) walk(c, full, inner, depth + 1);
            continue;
        }
        int kind = S_ISREG(st.st_mode) ? game_file_kind(entry->d_name) : 0;
        if (!kind || (int64_t)st.st_mtime < c->from || (int64_t)st.st_mtime > c->to) continue;
        windows_path(inner, shown, sizeof(shown));
        uint64_t size = (uint64_t)st.st_size;
        char amount[32];
        pc5_size_text(size, amount, sizeof(amount));
        if (kind == 2 && size > GAME_DUMP_MAX) {
            pc5_buf_printf(c->about, "  left out, too large to attach: %s (%s)\n", shown, amount);
            continue;
        }
        uint64_t taken = kind == 1 && size > GAME_TEXT_MAX ? GAME_TEXT_MAX : size;
        if (c->bytes + taken > GAME_BYTES_MAX) {
            pc5_buf_printf(c->about, "  left out, no room: %s (%s)\n", shown, amount);
            continue;
        }
        Pc5Buf data = { 0 };
        char name[PC5_PATH + 8];
        int cut = 0;
        if (!read_end(full, size, kind == 1 ? GAME_TEXT_MAX : GAME_DUMP_MAX, &data, &cut) &&
            !pc5_format(name, sizeof(name), "game/%s", inner) &&
            !pc5_zip_add(c->zip, name, data.data, data.len, (int64_t)st.st_mtime + c->offset_s)) {
            c->files++;
            c->bytes += data.len;
            /* Unreal's crash report says what it was ("Unhandled Exception: EXCEPTION_ACCESS_VIOLATION
             * writing address 0x0..."): the likely cause when the log showed none. */
            if (!c->issue->cause && pc5_ends_with_ci(entry->d_name, "CrashContext.runtime-xml"))
                crash_message(&data, c->issue);
            pc5_buf_printf(c->about, "  %s (%s%s)\n", shown, amount, cut ? ", its last 2 MB" : "");
        }
        pc5_buf_free(&data);
    }
    closedir(d);
}

/* Where the game's files are: its prefix's drive_c, under root/prefixes (the
 * name the log gives, so the same log read anywhere finds them). */
static int game_drive(const char *root, const Pc5LogIssue *issue, char *out, size_t size)
{
    const char *name = strrchr(issue->prefix, '/');
    name = name ? name + 1 : issue->prefix;
    if (!name[0]) name = issue->profile;
    if (!name[0] || !strcmp(name, "default") || strstr(name, "..")) return -1;
    return pc5_format(out, size, "%s/prefixes/%s/drive_c", root, name);
}

/* The folder the game's program is in, under drive_c: C:\Games\Name\x64\game.exe gives
 * Games/Name (its top two folders, where a game writes beside itself). */
static int game_folder(const char *exe, char *out, size_t size)
{
    const char *p = exe;
    size_t n = 0;
    int parts = 0;
    if (pc5_lower((unsigned char)p[0]) < 'a' || pc5_lower((unsigned char)p[0]) > 'z' || p[1] != ':' || p[2] != '\\')
        return -1;
    for (p += 3; *p && n + 1 < size; p++) {
        if (*p == '\\') {
            if (++parts == 2) break;
            out[n++] = '/';
        } else {
            out[n++] = *p;
        }
    }
    out[n] = 0;
    return parts >= 1 && n && !strstr(out, "..") ? 0 : -1;
}

/* What PC5 saw of Steam when the game started (pc5_steam_check_keep: the game's
 * pc5/steam-check.txt), for about.txt. */
static void steam_check(const char *root, const Pc5LogIssue *issue, Pc5Buf *about)
{
    char drive[PC5_PATH], path[PC5_PATH];
    Pc5Buf text = { 0 };
    if (game_drive(root, issue, drive, sizeof(drive)) || strlen(drive) < 9) return;
    drive[strlen(drive) - 8] = 0;   /* "/drive_c" off: the game's prefix */
    if (pc5_format(path, sizeof(path), "%s/pc5/steam-check.txt", drive) || !pc5_is_file(path) ||
        pc5_read_file(path, &text, 16 * 1024) || !text.len) {
        pc5_buf_free(&text);
        return;
    }
    pc5_buf_puts(about, "\nSteam, as PC5 saw it when the game started:\n");
    pc5_buf_put(about, text.data, text.len);
    pc5_buf_free(&text);
}

static void collect_game_files(const char *root, Pc5LogIssue *issue, Pc5Zip *zip, Pc5Buf *about, int offset_s)
{
    char drive[PC5_PATH], dir[PC5_PATH], folder[PC5_PATH];
    Collect c = { zip, about, issue, issue->time - GAME_BEFORE_S, issue->ended + GAME_AFTER_S, offset_s, 0, 0, 0 };

    if (game_drive(root, issue, drive, sizeof(drive)) || !pc5_is_dir(drive)) return;
    pc5_buf_puts(about, "\nThe game's own files, written while it ran:\n");
    size_t before = about->len;
    if (!pc5_join(dir, sizeof(dir), drive, "users")) walk(&c, dir, "users", 1);
    if (!game_folder(issue->exe, folder, sizeof(folder)) && strncmp(folder, "users/", 6) &&
        !pc5_join(dir, sizeof(dir), drive, folder))
        walk(&c, dir, folder, 2);
    if (about->len == before) pc5_buf_puts(about, "  none\n");
    issue->files = c.files;
}

/* ---- keeping a session ---------------------------------------------------------------- */

static void about_page(Pc5Buf *about, const Pc5LogIssue *issue, const char *when, const char *base)
{
    pc5_buf_printf(about, "Kept by PC5 %s for a report: https://github.com/bornaradusin/PC5/issues\n\n", PC5_VERSION);
    if (!strcmp(issue->profile, "launcher")) pc5_buf_printf(about, "PC5's own session, begun %s.\n", when);
    else pc5_buf_printf(about, "The game %s, begun %s (the console's clock).\n", issue->profile, when);
    if (issue->exe[0]) pc5_buf_printf(about, "Program: %s\n", issue->exe);
    if (issue->firmware[0]) pc5_buf_printf(about, "Console firmware: %s\n", issue->firmware);
    pc5_buf_printf(about, "What PC5 saw: %s%s%s\n", pc5_log_problem_text(issue->problem), issue->detail[0] ? ": " : "",
                   issue->detail);
    char cause[260];
    pc5_log_cause_text(issue, cause, sizeof(cause));
    if (cause[0]) pc5_buf_printf(about, "Likely cause: %s.\n", cause);
    if (!issue->closed)
        pc5_buf_puts(about, "Its log has no end line: it was closed with the PS button, or the console stopped while it ran.\n");
    pc5_buf_printf(about, "\nPC5's log: %s.log, with %s-earlier.log and %s-start.log when the session was long, and "
                          "%s-crash.log, the part with its crash, when a long session went on after it.\n",
                   base, base, base, base);
}

static int part_path(const char *logs, int session, const char *part, char *out, size_t size)
{
    return pc5_format(out, size, "%s/session-%d.%slog", logs, session, part);
}

/* The session's parts as plain files, kind-<when>_<profile>.log, -earlier.log, -crash.log
 * and -start.log: when a zip can't be written. The newest first: without it, nothing. */
static int keep_plain(const char *logs, int session, const char *base, char *out, size_t size)
{
    char from[PC5_PATH], to[PC5_PATH];
    for (int i = NEWEST; i >= 0; i--) {
        if (part_path(logs, session, PARTS[i][0], from, sizeof(from)) ||
            pc5_format(to, sizeof(to), "%s/%s%s.log", logs, base, PARTS[i][1]))
            return -1;
        if (!pc5_exists(from)) continue;
        if (rename(from, to)) return i < NEWEST ? 0 : -1;
        if (i == NEWEST) pc5_copy(out, size, to);
    }
    return 0;
}

/* Keeps session N as kind-<when>_<profile>.zip: its log's parts, the files the game wrote
 * while it ran, and a page saying what PC5 saw. The path in issue->file. */
static int keep(const char *root, const char *logs, int session, const char *kind, Pc5LogIssue *issue, int offset_s,
                int64_t now)
{
    char when[32], base[PC5_PATH], zip_path[PC5_PATH], log_path[PC5_PATH], when_text[48];
    Pc5Buf about = { 0 };
    Pc5Zip zip;
    int64_t at = issue->time ? issue->time : now;

    if (stamp(at, offset_s, when, sizeof(when))) return -1;
    for (int n = 1; n < 100; n++) {
        char suffix[8] = "";
        if (n > 1) snprintf(suffix, sizeof(suffix), "-%d", n);
        if (pc5_format(base, sizeof(base), "%s-%s_%s%s", kind, when, issue->profile, suffix) ||
            pc5_format(zip_path, sizeof(zip_path), "%s/%s.zip", logs, base) ||
            pc5_format(log_path, sizeof(log_path), "%s/%s.log", logs, base))
            return -1;
        if (!pc5_exists(zip_path) && !pc5_exists(log_path)) break;
    }
    pc5_copy(when_text, sizeof(when_text), when);
    if (strlen(when_text) == 16) { when_text[10] = ' '; when_text[13] = ':'; }

    if (!pc5_zip_open(&zip, zip_path)) {
        int failed = 0;
        for (int i = 0; i < PART_COUNT && !failed; i++) {
            char from[PC5_PATH], name[PC5_PATH + 16];
            Pc5Buf data = { 0 };
            struct stat st;
            if (part_path(logs, session, PARTS[i][0], from, sizeof(from)) || stat(from, &st)) continue;
            failed = pc5_read_file(from, &data, 64u << 20) || pc5_format(name, sizeof(name), "%s%s.log", base, PARTS[i][1]) ||
                     pc5_zip_add(&zip, name, data.data, data.len, (int64_t)st.st_mtime + offset_s);
            pc5_buf_free(&data);
        }
        if (!failed) {
            /* The game's files first: its crash report may say the likely cause about.txt gives. */
            Pc5Buf files = { 0 };
            if (strcmp(issue->profile, "launcher")) collect_game_files(root, issue, &zip, &files, offset_s);
            about_page(&about, issue, when_text, base);
            if (strcmp(issue->profile, "launcher")) steam_check(root, issue, &about);
            if (files.len) pc5_buf_put(&about, files.data, files.len);
            pc5_buf_free(&files);
            failed = pc5_zip_add(&zip, "about.txt", about.data, about.len, now + offset_s);
        }
        pc5_buf_free(&about);
        if (!failed && !pc5_zip_close(&zip)) {
            for (int i = 0; i < PART_COUNT; i++) {
                char from[PC5_PATH];
                if (!part_path(logs, session, PARTS[i][0], from, sizeof(from))) unlink(from);
            }
            pc5_copy(issue->file, sizeof(issue->file), zip_path);
            return 0;
        }
        if (failed) pc5_zip_abandon(&zip);
    }
    issue->files = 0;
    return keep_plain(logs, session, base, issue->file, sizeof(issue->file));
}

/* A kept log's name: kind-YYYY-MM-DD_HH-MM_<profile>[-N] then .zip, .log, -earlier.log,
 * -crash.log or -start.log. Its time in when (16 characters) and 1 when it is profile's. */
static int kept_for(const char *name, const char *kind, const char *profile, char when[17])
{
    size_t k = strlen(kind), p = strlen(profile);
    static const char *const ends[] = { ".zip", ".log", "-earlier.log", "-crash.log", "-start.log" };
    enum { ENDS = sizeof(ends) / sizeof(ends[0]) };
    if (strncmp(name, kind, k) || name[k] != '-' || strlen(name) < k + 18 || name[k + 17] != '_') return 0;
    memcpy(when, name + k + 1, 16);
    when[16] = 0;
    const char *rest = name + k + 18;
    if (strncmp(rest, profile, p)) return 0;
    rest += p;
    if (rest[0] == '-' && rest[1] >= '0' && rest[1] <= '9') {
        const char *digits = rest + 1;
        while (*digits >= '0' && *digits <= '9') digits++;
        for (int i = 0; i < ENDS; i++)
            if (!strcmp(digits, ends[i])) return 1;
    }
    for (int i = 0; i < ENDS; i++)
        if (!strcmp(rest, ends[i])) return 1;
    return 0;
}

/* Of a game's logs kept as log-... (it went well), only the newest stays. */
static void keep_newest(const char *logs, const char *profile)
{
    char newest[17] = "", when[17], path[PC5_PATH];
    DIR *dir;
    struct dirent *entry;

    for (int pass = 0; pass < 2; pass++) {
        if (!(dir = opendir(logs))) return;
        while ((entry = readdir(dir))) {
            if (!kept_for(entry->d_name, "log", profile, when)) continue;
            if (!pass) {
                if (strcmp(when, newest) > 0) memcpy(newest, when, sizeof(newest));
            } else if (strcmp(when, newest) < 0 && !pc5_join(path, sizeof(path), logs, entry->d_name)) {
                unlink(path);
            }
        }
        closedir(dir);
    }
}

int pc5_logs_newest(const char *root, const char *profile, char *out, size_t size)
{
    char logs[PC5_PATH], when[17], best[17] = "", name[PC5_PATH] = "";
    DIR *dir;
    struct dirent *entry;

    if (pc5_format(logs, sizeof(logs), "%s/logs", root) || !(dir = opendir(logs))) return -1;
    while ((entry = readdir(dir))) {
        const char *n = entry->d_name;
        if (pc5_ends_with_ci(n, "-earlier.log") || pc5_ends_with_ci(n, "-crash.log") || pc5_ends_with_ci(n, "-start.log"))
            continue;
        if ((kept_for(n, "issue", profile, when) || kept_for(n, "log", profile, when)) &&
            (strcmp(when, best) > 0 || (!strcmp(when, best) && pc5_ends_with_ci(n, ".zip")))) {
            memcpy(best, when, sizeof(best));
            pc5_copy(name, sizeof(name), n);
        }
    }
    closedir(dir);
    return name[0] ? pc5_join(out, size, logs, name) : -1;
}

int pc5_logs_tidy(const char *root, int keep_all, int64_t now, int offset_s, Pc5LogIssue *newest)
{
    char logs[PC5_PATH], path[PC5_PATH], marker[PC5_PATH];
    char played[SESSIONS][PC5_ID];
    int current, issues = 0, games = 0;
    DIR *dir;
    struct dirent *entry;

    if (newest) memset(newest, 0, sizeof(*newest));
    if (pc5_format(logs, sizeof(logs), "%s/logs", root) || !pc5_is_dir(logs)) return 0;
    current = current_session(logs);
    /* The sessions that have ended. */
    for (int session = 0; current >= 0 && session < SESSIONS; session++) {
        Pc5Buf text = { 0 };
        Pc5LogIssue issue;
        if (session == current || pc5_format(path, sizeof(path), "%s/session-%d.log", logs, session) ||
            !pc5_is_file(path) || read_session(logs, session, &text)) {
            pc5_buf_free(&text);
            continue;
        }
        pc5_log_classify((const char *)text.data, text.len, &issue);
        pc5_buf_free(&text);
        /* PC5's own crash (pc5_console.c leaves crash-<session>.txt behind). */
        if (!pc5_format(marker, sizeof(marker), "%s/pc5/crash-%d.txt", root, session) && pc5_exists(marker)) {
            if (!issue.problem) issue.problem = PC5_LOG_CRASHED;
            unlink(marker);
        }
        int game = strcmp(issue.profile, "launcher") != 0;
        if (issue.problem) {
            if (!keep(root, logs, session, "issue", &issue, offset_s, now)) {
                issues++;
                if (newest && issue.time >= newest->time) *newest = issue;
            }
        } else if (keep_all || game) {
            /* A game's newest session stays even when it went well: a problem without a
             * crash (no sound, a wrong picture) still has its log. */
            if (!keep(root, logs, session, "log", &issue, offset_s, now) && game && !keep_all && games < SESSIONS)
                pc5_copy(played[games++], PC5_ID, issue.profile);
        } else {
            for (int i = 0; i < PART_COUNT; i++)
                if (!part_path(logs, session, PARTS[i][0], path, sizeof(path))) unlink(path);
        }
    }
    for (int i = 0; i < games; i++) keep_newest(logs, played[i]);
    /* Anything older than a week, but the session running now. */
    if ((dir = opendir(logs))) {
        char now_parts[PART_COUNT][48] = { "" };
        for (int i = 0; current >= 0 && i < PART_COUNT; i++)
            snprintf(now_parts[i], sizeof(now_parts[i]), "session-%d.%slog", current, PARTS[i][0]);
        while ((entry = readdir(dir))) {
            struct stat st;
            int running = 0;
            for (int i = 0; i < PART_COUNT; i++) running |= now_parts[i][0] && !strcmp(entry->d_name, now_parts[i]);
            if ((!pc5_ends_with_ci(entry->d_name, ".log") && !pc5_ends_with_ci(entry->d_name, ".zip")) || running ||
                pc5_join(path, sizeof(path), logs, entry->d_name) || stat(path, &st) || !S_ISREG(st.st_mode))
                continue;
            if ((int64_t)st.st_mtime < now - (int64_t)PC5_LOG_DAYS * 86400) unlink(path);
        }
        closedir(dir);
    }
    return issues;
}

int pc5_firmware_from_kernel(const char *version, char *out, size_t size)
{
    const char *at = version;

    while (at && (at = strstr(at, "releases/"))) {
        const char *p = at += 9;
        int digits = 0, minor = 0, major = 0;
        while (*p >= '0' && *p <= '9' && digits < 3) major = major * 10 + *p++ - '0', digits++;
        if (digits && *p == '.') {
            const char *dot = p++;
            while (*p >= '0' && *p <= '9' && minor < 3) p++, minor++;
            /* "09.60" as libkernel's number reads, 9.60 */
            if (minor && !(*p >= '0' && *p <= '9') && *p != '.')
                return pc5_format(out, size, "%d.%.*s", major, minor, dot + 1);
        }
    }
    return -1;
}
