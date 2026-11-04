/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: keeping prospero-win's logs (<root>/logs) for a report. Each session
 * (PC5's, or a game's) writes session-N.log while it runs, since a crash
 * can't write its log afterwards. When PC5 opens it reads the sessions that
 * have ended. One with a problem is kept as a zip named for when and what
 * (issue-2026-10-22_18-47_grand-theft-auto-iv.zip), with the files the game
 * itself wrote while it ran (its own logs, crash reports and dumps) and a
 * page saying what PC5 saw. A game's newest session is kept the same way
 * (log-...zip) even when nothing went wrong, so a problem without a crash
 * still has its log; older ones of that game, and PC5's own sessions that
 * went well, are deleted. Logs older than seven days go, whatever they hold. */
#ifndef PC5_LOGS_H
#define PC5_LOGS_H
#include "pc5.h"

enum { PC5_LOG_DAYS = 7 };

typedef enum Pc5LogProblem {
    PC5_LOG_FINE,
    PC5_LOG_CRASHED,          /* the process ended without closing its log */
    PC5_LOG_GAME_CRASHED,     /* Wine reported an unhandled exception */
    PC5_LOG_NO_PICTURE,       /* the game closed within a minute, having shown nothing */
    PC5_LOG_FORCED,           /* the game didn't close when asked and was ended */
    PC5_LOG_PC5_PROBLEM,      /* PC5 logged a problem (an install or uninstall that failed) */
    PC5_LOG_CLOSED_EARLY,     /* the game closed by itself within a minute, for a reason PC5 found (cause) */
} Pc5LogProblem;

/* What likely went wrong, when the log shows it: what a player can act on. */
typedef enum Pc5LogCause {
    PC5_CAUSE_NONE,
    PC5_CAUSE_INSTALLER,      /* it tried to start an installer (vc_redist.x64.exe), which can't run on the console */
    PC5_CAUSE_COPY_FILES,     /* its copy loaded files that aren't the game's as its store ships it (OnlineFix64.dll) */
    PC5_CAUSE_NO_OPENGL,      /* its OpenGL never started: it drew with no rendering context */
    PC5_CAUSE_CRASH_REPORT,   /* the game's own crash report (Unreal's CrashContext) says what it was */
} Pc5LogCause;

typedef struct Pc5LogIssue {
    Pc5LogProblem problem;
    char profile[PC5_ID];     /* the game's id, or "launcher" for PC5 */
    char detail[200];         /* Wine's line about the crash, or PC5's problem */
    char file[PC5_PATH];      /* where the log is kept */
    int64_t time;             /* when the session began (seconds since 1970) */
    int64_t ended;            /* about when its last line was written */
    int closed;               /* it has its end line (it didn't stop half-way) */
    char prefix[PC5_PATH];    /* the game's Wine prefix, as its log names it, or "" */
    char exe[PC5_PATH];       /* the program Wine ran (C:\Games\...), or "" */
    char firmware[32];        /* the console's, as the log names it, or "" */
    int files;                /* the game's own files kept with the log */
    Pc5LogCause cause;
    char cause_what[160];     /* the installer's or file's name, or the crash report's message */
} Pc5LogIssue;

/* What a session's log says: the problem, if any. */
Pc5LogProblem pc5_log_classify(const char *text, size_t length, Pc5LogIssue *issue);

/* What PC5 saw, in a few words ("stopped with an error"), for a report. */
const char *pc5_log_problem_text(Pc5LogProblem problem);
/* The likely cause in a sentence, for a report and about.txt ("the game tried to start
 * vc_redist.x64.exe, an installer, which can't run on the console"); "" for none. */
void pc5_log_cause_text(const Pc5LogIssue *issue, char *out, size_t size);

/* Tidies root/logs as above, leaving the session now running alone. now is
 * the time in seconds since 1970 and offset_s the console's offset from UTC,
 * for the kept logs' names. Returns how many sessions had a problem; the
 * newest of them is in *newest (when not NULL). */
int pc5_logs_tidy(const char *root, int keep_all, int64_t now, int offset_s, Pc5LogIssue *newest);

/* The newest kept log of a game (issue-... or log-...), its path in out. 0, or -1 when there is none. */
int pc5_logs_newest(const char *root, const char *profile, char *out, size_t size);

/* The console's firmware ("13.60") from the kernel's version text (kern.version, which
 * names the release branch it was built from: "... r229358/releases/13.60: ..."), its major
 * number without a leading zero, as libkernel's number is written. 0, or -1 when the text
 * names none. */
int pc5_firmware_from_kernel(const char *version, char *out, size_t size);

#endif
