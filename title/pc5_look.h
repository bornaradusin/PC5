/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef PC5_LOOK_H
#define PC5_LOOK_H
#include "pc5.h"
#include <stdatomic.h>
typedef struct Pc5Look {
    pthread_t thread;
    atomic_int done, stop;
    atomic_uint_fast64_t bytes;
    int running, restore, result;
    char root[PC5_PATH], prefix[PC5_ID], archive[PC5_PATH], error[PC5_ERROR];
} Pc5Look;
int pc5_look_start(Pc5Look *job, const char *root, const char *prefix, const char *archive, int restore);
void pc5_look_end(Pc5Look *job);
/* Undo a restore interrupted before its atomic transaction commit. */
void pc5_look_recover(const char *root, const char *prefix);
#endif
