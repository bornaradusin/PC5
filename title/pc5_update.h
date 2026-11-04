/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: is there a newer PC5? GitHub's public API answers with the newest
 * release of the repository (no account, no key), with the app's zip
 * (PPSA11018.zip) that PC5 can install itself (pc5_upgrade.h), and from 1.9.0 the
 * update zip beside it (PPSA11018-update.zip): only the files that differ from the
 * release before, and the new release's list of its files. */
#ifndef PC5_UPDATE_H
#define PC5_UPDATE_H
#include "pc5.h"
#include "pc5_cover.h"

#define PC5_RELEASES_API "https://api.github.com/repos/bornaradusin/PC5/releases/latest"
#define PC5_RELEASES_PAGE "https://github.com/bornaradusin/PC5/releases"

typedef enum Pc5UpdateState {
    PC5_UPDATE_UNKNOWN,       /* not asked yet */
    PC5_UPDATE_FAILED,        /* no answer (offline?) */
    PC5_UPDATE_NONE,          /* no release published yet */
    PC5_UPDATE_CURRENT,       /* this is the newest */
    PC5_UPDATE_AVAILABLE,     /* a newer one is out */
} Pc5UpdateState;

typedef struct Pc5Update {
    Pc5UpdateState state;
    char version[32];         /* the newest release's, without a leading v */
    char name[96];            /* its title */
    char url[256];            /* its page */
    /* The app's zip in the release (PC5_TITLE_ID ".zip"): where it is, how big, its
     * SHA-256 as GitHub gives it (lower-case hex, "" when it gives none). url "" when
     * the release has none. */
    char asset_url[512];
    uint64_t asset_size;
    char asset_sha256[65];
    /* The update zip (PC5_TITLE_ID "-update.zip"), the same way; delta_url "" when the
     * release has none. PC5 installs from it when the console has the rest of the release. */
    char delta_url[512];
    uint64_t delta_size;
    char delta_sha256[65];
    /* The release the update zip holds the changes since: from the release's notes, a line
     * GitHub doesn't show ("<!-- pc5-update-from: 1.9.7 -->", from 1.9.8's releases), or the
     * update zip's label ("from 1.9.6", 1.9.7's release); "" when neither says. */
    char delta_from[32];
} Pc5Update;

/* <0, 0, >0 as version a is older, the same, newer than b ("1.2.0", "v1.10"). */
int pc5_version_compare(const char *a, const char *b);
/* Whether pc5_update_check offers test versions too (Settings' test_versions): GitHub's newest
 * release or pre-release then, of its 10 newest, by version. Off: only its latest release. */
void pc5_update_track(int test);
#define PC5_RELEASES_LIST_API "https://api.github.com/repos/bornaradusin/PC5/releases?per_page=10"
/* Asks GitHub; current is this PC5's version. Fills update in any case. */
void pc5_update_check(const Pc5Http *http, const char *current, Pc5Update *update);
/* What to show in Settings: "PC5 1.1.0 is available", "You have the newest PC5". */
void pc5_update_text(const Pc5Update *update, char *out, size_t size);
/* 1 when the release's update zip is for current: it holds the changes since that very
 * version (one release behind). Otherwise (more than one release behind, or a release
 * whose label doesn't say) PC5 downloads the whole app. */
int pc5_update_small(const Pc5Update *update, const char *current);

/* What PC5 remembers between starts (root/pc5/update.txt): when it last
 * asked (each time the player opens PC5, otherwise at most once an hour, as
 * GitHub allows 60 questions an hour: the title restarts after every game),
 * the answer, and the version it told about with the console's notification. */
typedef struct Pc5UpdateMemory {
    int64_t checked;          /* seconds since 1970, 0 never */
    char told[32];
    Pc5Update last;
} Pc5UpdateMemory;

enum { PC5_UPDATE_EVERY_S = 3600 };

void pc5_update_load(const char *root, Pc5UpdateMemory *memory);
int pc5_update_save(const char *root, const Pc5UpdateMemory *memory);

#endif
