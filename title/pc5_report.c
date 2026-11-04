/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: the link for Report on GitHub (pc5_report.h). */
#include "pc5_report.h"

#include <string.h>

const char *pc5_report_file(const Pc5LogIssue *issue)
{
    const char *slash = strrchr(issue->file, '/');
    return slash ? slash + 1 : issue->file;
}

/* Text for a link's query: letters, digits and -._~ as they are, everything else as %XX. */
static int put_encoded(Pc5Buf *out, const char *text)
{
    static const char hex[] = "0123456789ABCDEF";
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        int plain = (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-' ||
                    *p == '.' || *p == '_' || *p == '~';
        char three[3] = { '%', hex[*p >> 4], hex[*p & 15] };
        if (plain ? pc5_buf_put(out, p, 1) : pc5_buf_put(out, three, 3)) return -1;
    }
    return 0;
}

/* The program's file name (Grim Dawn.exe from C:\Games\Grim Dawn\x64\Grim Dawn.exe). */
static const char *program_name(const char *exe)
{
    const char *slash = strrchr(exe, '\\');
    return slash ? slash + 1 : exe;
}

/* One field of the form, filled in: &id=text. */
static int put_field(Pc5Buf *out, const char *id, const char *text)
{
    return pc5_buf_printf(out, "&%s=", id) || put_encoded(out, text);
}

/* The game form's "What goes wrong" choice (.github/ISSUE_TEMPLATE/1-game.yml) for what PC5
 * saw, or NULL when none fits. */
static const char *problem_choice(Pc5LogProblem problem)
{
    switch (problem) {
    case PC5_LOG_NO_PICTURE:
    case PC5_LOG_CLOSED_EARLY: return "Closes before showing anything";
    case PC5_LOG_GAME_CRASHED:
    case PC5_LOG_CRASHED: return "Stops with an error";
    case PC5_LOG_FORCED: return "Freezes, or has to be closed";
    default: return NULL;
    }
}

int pc5_report_url(const Pc5LogIssue *issue, const char *game, char *out, size_t size)
{
    return pc5_report_url_settings(issue, game, NULL, out, size);
}

int pc5_report_url_settings(const Pc5LogIssue *issue, const char *game, const char *settings, char *out, size_t size)
{
    Pc5Buf title = { 0 }, saw = { 0 }, url = { 0 };
    int own = !(game && game[0]) && !strcmp(issue->profile, "launcher");   /* PC5's own session */
    const char *choice = own ? NULL : problem_choice(issue->problem), *file = pc5_report_file(issue);
    const char *name = game && game[0] ? game : own ? "PC5" : issue->profile;
    char detail[161], cause[260];
    int failed;

    pc5_log_cause_text(issue, cause, sizeof(cause));

    /* The crash's own line, shortened so the code stays easy to scan. */
    pc5_copy(detail, sizeof(detail), issue->detail);
    for (char *p = detail; *p; p++)
        if (*p == '`') *p = '\'';
    /* GitHub's issue form (.github/ISSUE_TEMPLATE): a game's, or PC5's own for PC5's session,
     * its fields filled in by their ids. What happened, what goes wrong and the log are the
     * player's to give: the form asks for them. */
    failed = pc5_buf_printf(&title, "%s: %s", name, pc5_log_problem_text(issue->problem)) ||
             pc5_buf_puts(&saw, pc5_log_problem_text(issue->problem)) ||
             (detail[0] && pc5_buf_printf(&saw, "\n`%s`", detail)) ||
             (cause[0] && pc5_buf_printf(&saw, "\nLikely cause: %s", cause)) ||
             (file[0] && pc5_buf_printf(&saw, "\nLog: %s", file)) ||
             (settings && settings[0] && pc5_buf_printf(&saw, "\nSettings: %s", settings)) ||
             pc5_buf_terminate(&title) || pc5_buf_terminate(&saw) ||
             pc5_buf_puts(&url, PC5_ISSUES_NEW) ||
             pc5_buf_puts(&url, own ? "?template=2-pc5.yml" : "?template=1-game.yml") ||
             put_field(&url, "title", (const char *)title.data) ||
             (!own && put_field(&url, "game", name)) ||
             (!own && issue->exe[0] && put_field(&url, "program", program_name(issue->exe))) ||
             put_field(&url, "pc5", PC5_VERSION) ||
             (issue->firmware[0] && put_field(&url, "firmware", issue->firmware)) ||
             (choice && put_field(&url, "problem", choice)) ||
             put_field(&url, "saw", (const char *)saw.data) || pc5_buf_terminate(&url) ||
             url.len >= size;
    if (!failed) memcpy(out, url.data, url.len + 1);
    pc5_buf_free(&title);
    pc5_buf_free(&saw);
    pc5_buf_free(&url);
    return failed ? -1 : 0;
}

int pc5_report_works_url(const char *game, const char *edition, const char *controller, const char *settings, char *out,
                         size_t size)
{
    Pc5Buf url = { 0 };
    const char *name = game && game[0] ? game : "PC5";
    /* The form's fields by their ids; the result is its first choice, as written there. */
    int failed = pc5_buf_puts(&url, PC5_WORKS_NEW) || put_field(&url, "title", name) || put_field(&url, "game", name) ||
                 (edition && edition[0] && put_field(&url, "edition", edition)) ||
                 put_field(&url, "result", "Works perfectly") || put_field(&url, "pc5", PC5_VERSION) ||
                 (controller && controller[0] && put_field(&url, "controller", controller)) ||
                 (settings && settings[0] && put_field(&url, "settings", settings)) || pc5_buf_terminate(&url) ||
                 url.len >= size;
    if (!failed) memcpy(out, url.data, url.len + 1);
    pc5_buf_free(&url);
    return failed ? -1 : 0;
}
