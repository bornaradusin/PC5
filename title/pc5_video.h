/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef PC5_VIDEO_H
#define PC5_VIDEO_H
#include "pc5.h"
#include "pc5_appearance.h"
#include <stdatomic.h>

/* Silent experimental H.264 background player. Buffers belong to this object;
 * poll/draw/stop run on the UI thread, decoding runs on its worker. */
typedef struct Pc5Video {
    pthread_t thread;
    atomic_int stop, pending, done;
    atomic_uint decoded, dropped, shown;
    int running, have, failed;
    uint32_t *front, *back;
    char path[PC5_PATH];
    char error[PC5_ERROR];
    Pc5Appearance options;
} Pc5Video;
int pc5_video_start(Pc5Video *video, const char *path);
int pc5_video_start_options(Pc5Video *video, const char *path, const Pc5Appearance *options);
int pc5_video_poll(Pc5Video *video);
void pc5_video_stop(Pc5Video *video);

typedef struct Pc5VideoImport {
    pthread_t thread;
    atomic_int stop, done;
    int running, result;
    char source[PC5_PATH], destination[PC5_PATH];
    char error[PC5_ERROR];
} Pc5VideoImport;
enum { PC5_VIDEO_OK = 0, PC5_VIDEO_FORMAT = -1, PC5_VIDEO_IO = -2, PC5_VIDEO_CANCELLED = -3 };
/* Copies to an inactive slot only after validating a decoded frame. Supports
 * H.264 video up to 1920x1080 and 60 fps; audio tracks are never decoded. */
int pc5_video_import_start(Pc5VideoImport *job, const char *source, const char *destination);
void pc5_video_import_end(Pc5VideoImport *job);
#endif
