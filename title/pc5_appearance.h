/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef PC5_APPEARANCE_H
#define PC5_APPEARANCE_H
#include "pc5.h"
typedef struct Pc5Appearance {
    int volume, music_start, video_start, video_end, fit, darkness;
} Pc5Appearance;
int pc5_appearance_load(const char *root, const char *prefix, Pc5Appearance *options);
int pc5_appearance_save(const char *root, const char *prefix, const Pc5Appearance *options);
int pc5_appearance_valid(const Pc5Appearance *options);
#endif
