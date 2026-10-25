/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: keeping logs only when something went wrong (pc5_logs.h). */
#include "pc5_logs.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* prospero-win's sessions (native/pw_diagnostics.h, PW_DIAGNOSTICS_SESSIONS):
 * session-0.log to session-7.log in turn, next.txt naming the next. A long
 * session also has session-N.start.log (how it began) and
 * session-N.previous.log (the part before the newest). */
enum { SESSIONS = 8, NO_PICTURE_S = 60 };

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

Pc5LogProblem pc5_log_classify(const char *text, size_t length, Pc5LogIssue *issue)
{
    const char *end_of_text = text + length, *line = text;
    double first_t = -1, last_t = -1;
    int ended = 0, launch_failed = 0, unhandled = 0, forced = 0, pc5_problem = 0, picture = 0, asked = 0;
    char crash[200] = "", problem[200] = "";

    memset(issue, 0, sizeof(*issue));
    pc5_copy(issue->profile, sizeof(issue->profile), "launcher");
    while (line < end_of_text) {
        const char *end = memchr(line, '\n', (size_t)(end_of_text - line)), *found;
        if (!end) end = end_of_text;
        if (line == text && (found = after(line, end, " profile="))) {
            size_t n = 0;
            while (found + n < end && found[n] != ' ' && n + 1 < sizeof(issue->profile)) n++;
            memcpy(issue->profile, found, n);
            issue->profile[n] = 0;
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
        if (after(line, end, "PW_WINE64 alive ")) {
            const char *shown = after(line, end, " shown="), *vk = after(line, end, " vk_shown=");
            if ((shown && strtoull(shown, NULL, 10) > 0) || (vk && strtoull(vk, NULL, 10) > 0)) picture = 1;
        }
        line = end + 1;
    }
    if (!strcmp(issue->profile, "launcher")) {
        if (pc5_problem) {
            issue->problem = PC5_LOG_PC5_PROBLEM;
            pc5_copy(issue->detail, sizeof(issue->detail), problem);
        }
    } else if (unhandled) {
        issue->problem = PC5_LOG_GAME_CRASHED;
        pc5_copy(issue->detail, sizeof(issue->detail), crash);
    } else if (asked) {
        /* The player closed it, quickly or not, and a game that ignored Alt+F4 was ended as asked. */
    } else if (forced) {
        issue->problem = PC5_LOG_FORCED;
    } else if (launch_failed) {
        issue->problem = PC5_LOG_NO_PICTURE;
        pc5_copy(issue->detail, sizeof(issue->detail), "Wine couldn't start it.");
    } else if (!picture && first_t >= 0 && last_t - first_t < NO_PICTURE_S) {
        /* A game closed with the PS button soon after starting looks the same when it
         * ended unasked (no end line): both are reported, a quick close rarely. */
        issue->problem = PC5_LOG_NO_PICTURE;
        (void)ended;
    }
    return issue->problem;
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

/* A session's parts in order (how it began, the part before the newest, the
 * newest), as one text. 0, or -1 when its newest part can't be read. */
static int read_session(const char *logs, int session, Pc5Buf *text)
{
    static const char *const parts[] = { "start.", "previous.", "" };
    char path[PC5_PATH];

    for (int i = 0; i < 3; i++) {
        Pc5Buf part = { 0 };
        int failed;
        if (pc5_format(path, sizeof(path), "%s/session-%d.%slog", logs, session, parts[i])) return -1;
        if (i < 2 && !pc5_is_file(path)) continue;
        failed = pc5_read_file(path, &part, 64u << 20) || pc5_buf_put(text, part.data, part.len);
        pc5_buf_free(&part);
        if (failed && i == 2) return -1;
    }
    return 0;
}

/* Moves session-N.log (and its .previous.log and .start.log, the session's
 * earlier parts) to kind-<when>_<profile>.log, -earlier.log and -start.log;
 * the new path in out. */
static int keep(const char *logs, int session, const char *kind, const Pc5LogIssue *issue, int offset_s, int64_t now,
                char *out, size_t size)
{
    char when[32], from[PC5_PATH], to[PC5_PATH];

    if (stamp(issue->time ? issue->time : now, offset_s, when, sizeof(when))) return -1;
    for (int n = 1; n < 100; n++) {
        char suffix[8] = "";
        if (n > 1) snprintf(suffix, sizeof(suffix), "-%d", n);
        if (pc5_format(to, sizeof(to), "%s/%s-%s_%s%s.log", logs, kind, when, issue->profile, suffix)) return -1;
        if (!pc5_exists(to)) break;
    }
    if (pc5_format(from, sizeof(from), "%s/session-%d.log", logs, session) || rename(from, to)) return -1;
    pc5_copy(out, size, to);
    static const char *const parts[][2] = { { "previous.", "-earlier" }, { "start.", "-start" } };
    for (int i = 0; i < 2; i++)
        if (!pc5_format(from, sizeof(from), "%s/session-%d.%slog", logs, session, parts[i][0]) && pc5_exists(from)) {
            char earlier[PC5_PATH];
            size_t n = strlen(to);
            if (n > 4 && !pc5_format(earlier, sizeof(earlier), "%.*s%s.log", (int)(n - 4), to, parts[i][1]))
                rename(from, earlier);
        }
    return 0;
}

int pc5_logs_tidy(const char *root, int keep_all, int64_t now, int offset_s, Pc5LogIssue *newest)
{
    char logs[PC5_PATH], path[PC5_PATH], marker[PC5_PATH];
    int current, issues = 0;
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
        if (issue.problem) {
            if (!keep(logs, session, "issue", &issue, offset_s, now, issue.file, sizeof(issue.file))) {
                issues++;
                if (newest && issue.time >= newest->time) *newest = issue;
            }
        } else if (keep_all) {
            keep(logs, session, "log", &issue, offset_s, now, issue.file, sizeof(issue.file));
        } else {
            unlink(path);
            if (!pc5_format(path, sizeof(path), "%s/session-%d.previous.log", logs, session)) unlink(path);
            if (!pc5_format(path, sizeof(path), "%s/session-%d.start.log", logs, session)) unlink(path);
        }
    }
    /* Anything older than a week, but the session running now. */
    if ((dir = opendir(logs))) {
        char now_name[32] = "", now_earlier[48] = "", now_start[48] = "";
        if (current >= 0) {
            snprintf(now_name, sizeof(now_name), "session-%d.log", current);
            snprintf(now_earlier, sizeof(now_earlier), "session-%d.previous.log", current);
            snprintf(now_start, sizeof(now_start), "session-%d.start.log", current);
        }
        while ((entry = readdir(dir))) {
            struct stat st;
            if (!pc5_ends_with_ci(entry->d_name, ".log") || !strcmp(entry->d_name, now_name) ||
                !strcmp(entry->d_name, now_earlier) || !strcmp(entry->d_name, now_start) ||
                pc5_join(path, sizeof(path), logs, entry->d_name) ||
                stat(path, &st) || !S_ISREG(st.st_mode))
                continue;
            if ((int64_t)st.st_mtime < now - (int64_t)PC5_LOG_DAYS * 86400) unlink(path);
        }
        closedir(dir);
    }
    return issues;
}
