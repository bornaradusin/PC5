/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "pc5_video.h"
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
static void check(const char *path, int fit, int darkness, int start, int end, int blue)
{
    Pc5Video video = { 0 };
    Pc5Appearance o = { .volume = 30, .fit = fit, .darkness = darkness, .video_start = start, .video_end = end };
    assert(!pc5_video_start_options(&video, path, &o));
    int frames = 0;
    for (int i = 0; i < 350; ++i) {
        if (pc5_video_poll(&video)) {
            ++frames;
            uint32_t px = video.front[300 * 1920 + 960];
            if (blue) assert((px & 255) > ((px >> 16) & 255) + 10);
            else if (frames == 1) assert(((px >> 16) & 255) > (px & 255) + 50);
            if (fit == 1) assert((video.front[300 * 1920 + 10] & 0xffffff) == 0);
            if (fit == 2) assert((video.front[300 * 1920 + 10] & 0xffffff) != 0);
            if (darkness == 80) assert((px & 255) < 60);
        }
        if (atomic_load(&video.done)) break;
        usleep(5000);
    }
    if (video.failed) fprintf(stderr, "%s\n", video.error);
    assert(frames > 15 && !video.failed && !atomic_load(&video.done));
    pc5_video_stop(&video);
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    check(argv[1], 0, 0, 1, 2, 1);
    check(argv[1], 1, 0, 1, 2, 1);
    check(argv[1], 2, 80, 1, 2, 1);
    check(argv[1], 0, 0, 999, 0, 0);
    puts("Video options: trimmed loop excludes adjacent colours, fit bars, centered fill, dimming and out-of-range fallback passed.");
    return 0;
}
