/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef PC5_COMPAT_H
#define PC5_COMPAT_H
#include "pc5_cover.h"
#include <stdatomic.h>
enum { PC5_COMPAT_MAX = 2048, PC5_COMPAT_LIMIT = 4 * 1024 * 1024 };
typedef struct Pc5CompatEntry {
    char title[256], version[96], store[256], controller[128];
    char settings[2048], notes[4096], report[256], author[256];
    int status;
} Pc5CompatEntry;
typedef struct Pc5Compat {
    Pc5CompatEntry *entries, *incoming;
    int count, incoming_count, running, result, cached;
    int64_t checked;
    pthread_t thread;
    atomic_int done;
    Pc5Http http;
    char cache[PC5_PATH], error[PC5_ERROR];
} Pc5Compat;
const char *pc5_compat_status(int status);
int pc5_compat_parse(const char *text, size_t length, Pc5CompatEntry **entries);
int pc5_compat_matches(const Pc5CompatEntry *entry, const char *query, int filter);
void pc5_compat_init(Pc5Compat *list, const char *root, const Pc5Http *http);
int pc5_compat_refresh(Pc5Compat *list);
int pc5_compat_poll(Pc5Compat *list);
/* A refresh under way finished (its connection closed), before a game starts. */
void pc5_compat_wait(Pc5Compat *list);
void pc5_compat_end(Pc5Compat *list);
#endif
