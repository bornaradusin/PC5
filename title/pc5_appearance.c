/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "pc5_appearance.h"
#include <stdio.h>
#include <string.h>
int pc5_appearance_valid(const Pc5Appearance *o)
{
    return o->volume >= 0 && o->volume <= 100 && o->music_start >= 0 && o->music_start <= 86400 &&
        o->video_start >= 0 && o->video_start <= 86400 && o->video_end >= 0 && o->video_end <= 86400 &&
        (!o->video_end || o->video_end > o->video_start) && o->fit >= 0 && o->fit <= 2 &&
        o->darkness >= 0 && o->darkness <= 80;
}
static int options_path(const char *root, const char *prefix, char *out)
{
    if (!prefix[0] || strchr(prefix, '/') || strchr(prefix, '\\') || !strcmp(prefix, ".") || !strcmp(prefix, "..") || !strcmp(prefix, "default")) return -1;
    return pc5_format(out, PC5_PATH, "%s/prefixes/%s/pc5/appearance.txt", root, prefix);
}
int pc5_appearance_load(const char *root, const char *prefix, Pc5Appearance *o)
{
    *o = (Pc5Appearance){ .volume = 30 };
    char path[PC5_PATH]; Pc5Buf b = { 0 };
    if (options_path(root, prefix, path)) return -1;
    if (pc5_read_file(path, &b, 512)) { pc5_buf_free(&b); return 0; }
    Pc5Appearance read = { 0 }; int used = 0;
    int ok = sscanf((const char *)b.data, "PC5-APPEARANCE-1\nvolume=%d\nmusic_start=%d\nvideo_start=%d\nvideo_end=%d\nfit=%d\ndarkness=%d\n%n",
        &read.volume, &read.music_start, &read.video_start, &read.video_end, &read.fit, &read.darkness, &used) == 6 &&
        (size_t)used == b.len && pc5_appearance_valid(&read);
    if (ok) *o = read;
    pc5_buf_free(&b); return ok ? 0 : -1;
}
int pc5_appearance_save(const char *root, const char *prefix, const Pc5Appearance *o)
{
    char path[PC5_PATH], dir[PC5_PATH], text[512];
    if (!pc5_appearance_valid(o) || options_path(root, prefix, path)) return -1;
    pc5_copy(dir, sizeof(dir), path); *strrchr(dir, '/') = 0;
    int n = snprintf(text, sizeof(text), "PC5-APPEARANCE-1\nvolume=%d\nmusic_start=%d\nvideo_start=%d\nvideo_end=%d\nfit=%d\ndarkness=%d\n",
        o->volume, o->music_start, o->video_start, o->video_end, o->fit, o->darkness);
    return pc5_mkdirs(dir) || pc5_write_file(path, text, (size_t)n) ? -1 : 0;
}
