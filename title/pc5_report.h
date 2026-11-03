/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: Report on GitHub. A kept log (pc5_logs.h) becomes the link to
 * GitHub's issue form for a game that doesn't run (PC5's own for PC5's
 * session), its title and fields filled in: the game, its program, PC5's
 * version, the console's firmware, what goes wrong (the form's choice that fits
 * what PC5 saw) and what PC5 saw: the crash's line, the zip to attach and the
 * game's settings. The form asks the player what happened and for the zip,
 * which the console names beside the code. The
 * console shows it as a QR code for a phone; nothing leaves the console unless
 * the player sends the form and attaches the log. */
#ifndef PC5_REPORT_H
#define PC5_REPORT_H
#include "pc5.h"
#include "pc5_logs.h"

#define PC5_ISSUES_NEW "https://github.com/bornaradusin/PC5/issues/new"
/* The Discussions' form for a game that runs (.github/DISCUSSION_TEMPLATE/game-compatibility.yml). */
#define PC5_WORKS_NEW "https://github.com/bornaradusin/PC5/discussions/new?category=game-compatibility"

enum { PC5_REPORT_URL = 2048 };

/* The link for this issue; game is its name in the library (or NULL). 0, or -1 when it doesn't fit. */
int pc5_report_url(const Pc5LogIssue *issue, const char *game, char *out, size_t size);
/* The same with the game's settings (a line such as "Graphics API: Auto, Controller: Xbox"),
 * NULL or "" for none, added to what PC5 saw. */
int pc5_report_url_settings(const Pc5LogIssue *issue, const char *game, const char *settings, char *out, size_t size);

/* The link for a game that runs (Report it works): the compatibility form, filled in by its
 * field ids. game: its name (the discussion's title too); edition: where it is from (the
 * form's Store and version), controller: the form's Controller mode choice and settings: the
 * game's settings in a line, each NULL or "" for none. How it runs is the form's Works
 * perfectly, which the player changes when it isn't so; the notes are the player's. 0, or -1
 * when it doesn't fit. */
int pc5_report_works_url(const char *game, const char *edition, const char *controller, const char *settings, char *out,
                         size_t size);

/* The kept log's file name alone (issue-...zip). */
const char *pc5_report_file(const Pc5LogIssue *issue);

#endif
