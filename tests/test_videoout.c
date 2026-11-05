/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Which window's picture has the console's display (patches/radv 0003, 0005, 0007, 0010 and 0012,
 * wsi_common_videoout_takeover.h), with the swapchains the games' own logs showed. A
 * game fixed by these rules stays here, so a change to them that would break it
 * again fails this test before it reaches a release. */
#include "wsi_common_videoout_takeover.h"

#include <stdio.h>

static int failures;

#define CHECK(cond, ...)                                                                    \
    do {                                                                                    \
        if (!(cond)) {                                                                      \
            fprintf(stderr, "test_videoout:%d: ", __LINE__);                                \
            fprintf(stderr, __VA_ARGS__);                                                   \
            fputc('\n', stderr);                                                            \
            failures++;                                                                     \
        }                                                                                   \
    } while (0)

#define MS UINT64_C(1000000)

/* A window of that age showing a picture of that size, which last presented at present_ms. */
static struct wsi_videoout_window window(uint64_t age, uint32_t width, uint32_t height, int presented, uint64_t present_ms)
{
    return (struct wsi_videoout_window){ age, width, height, presented != 0, present_ms * MS, false };
}

/* The same, a newer, smaller window's that took the display from a game standing still (a movie's). */
static struct wsi_videoout_window movie(uint64_t age, uint32_t width, uint32_t height, uint64_t present_ms)
{
    return (struct wsi_videoout_window){ age, width, height, true, present_ms * MS, true };
}

static enum wsi_videoout_takeover take(struct wsi_videoout_window owner, struct wsi_videoout_window chain, uint64_t now_ms)
{
    return wsi_videoout_takeover(&owner, false, &chain, now_ms * MS);
}

int main(void)
{
    struct wsi_videoout_window none = window(9, 640, 480, 0, 0);

    /* Nothing has the display, the new swapchain replaces the owner, or the owner never
     * presented (wined3d's hidden 10x10 window probing OpenGL). */
    CHECK(wsi_videoout_takeover(NULL, false, &none, 0) == WSI_VIDEOOUT_FREE, "nothing has it");
    struct wsi_videoout_window owner = window(1, 640, 480, 1, 1000);
    CHECK(wsi_videoout_takeover(&owner, true, &none, 1000 * MS) == WSI_VIDEOOUT_FREE, "its own oldSwapchain");
    CHECK(take(window(1, 10, 10, 0, 0), window(2, 640, 480, 0, 0), 10) == WSI_VIDEOOUT_FREE, "an owner that never presented");

    /* Chronicles of the Wolf, Aggelos 2 (#227, #218): a Clickteam Fusion game's menu opens as a
     * window of its own, as large as the game's, while the game's keeps presenting. */
    CHECK(take(window(1, 3840, 2160, 1, 1000), window(2, 3840, 2160, 0, 0), 1000) == WSI_VIDEOOUT_NEWER,
          "Aggelos 2: the menu's window takes the display");
    CHECK(take(window(1, 1920, 1080, 1, 1000), window(2, 1920, 1080, 0, 0), 1000) == WSI_VIDEOOUT_NEWER,
          "Chronicles of the Wolf: the menu's window takes the display");
    /* and back: the game's window, older, doesn't take it from the menu while it presents */
    CHECK(take(window(2, 1920, 1080, 1, 1000), window(1, 1920, 1080, 0, 0), 1100) == WSI_VIDEOOUT_REFUSED,
          "the game's window waits while the menu presents");
    /* a window hidden but not destroyed: half a second without presenting */
    CHECK(take(window(2, 1920, 1080, 1, 1000), window(1, 1920, 1080, 0, 0), 1500) == WSI_VIDEOOUT_STALE,
          "an owner that stopped presenting");
    CHECK(take(window(2, 1920, 1080, 1, 1000), window(1, 1920, 1080, 0, 0), 1499) == WSI_VIDEOOUT_REFUSED,
          "not before half a second");

    /* Blade & Sword (#290): its movie plays through DirectShow in a framed window, a 634x448
     * picture inside the fullscreen game's 640x480. Before 2.0.9 it was refused and the game
     * played on; taking the display stopped the game with no picture. Ys: The Oath in Felghana
     * and Ys I draw their movies through DirectDraw in such a window, and Romance of the Three
     * Kingdoms XI through OpenGL in a 1914x1048 one beside its 1680x1050: refused, they crashed.
     * Such a window gets a swapchain that is never shown. */
    CHECK(take(window(1, 640, 480, 1, 1000), window(2, 634, 448, 0, 0), 1000) == WSI_VIDEOOUT_HIDDEN,
          "Blade & Sword, Ys: the movie's window leaves the display to the game's while it draws");
    CHECK(take(window(1, 640, 480, 1, 1000), window(2, 634, 448, 0, 0), 1499) == WSI_VIDEOOUT_HIDDEN,
          "Blade & Sword: for half a second after the game's last picture");
    CHECK(take(window(1, 1680, 1050, 1, 1000), window(2, 1914, 1048, 0, 0), 1000) == WSI_VIDEOOUT_HIDDEN,
          "Romance of the Three Kingdoms XI: a movie's window shorter than the game's");

    /* Romance of the Three Kingdoms XI (#365), 2.3.2: the game's 1920x1080 picture stood still
     * while its movie played in a 1914x1048 window of its own, never shown: nothing was seen and
     * the game waited for good. A game that stopped presenting for half a second lets its movie's
     * picture take the display ... */
    CHECK(take(window(1, 1920, 1080, 1, 1000), window(2, 1914, 1048, 0, 0), 1500) == WSI_VIDEOOUT_STALE,
          "Romance of the Three Kingdoms XI: the movie, once the game stood still");
    CHECK(take(window(1, 640, 480, 1, 1000), window(2, 634, 448, 0, 0), 5000) == WSI_VIDEOOUT_STALE,
          "Ys: a DirectDraw movie, once the game waits for it");
    /* ... and the game's own window, older and larger, takes it back as soon as it draws again,
     * movie playing or not: Blade & Sword stopped with no picture when its movie kept it. */
    CHECK(take(movie(2, 1914, 1048, 1000), window(1, 1920, 1080, 0, 0), 1000) == WSI_VIDEOOUT_BACK,
          "Romance of the Three Kingdoms XI: the game's window takes the display back from its movie");
    CHECK(take(movie(2, 634, 448, 1000), window(1, 640, 480, 0, 0), 1000) == WSI_VIDEOOUT_BACK,
          "Blade & Sword: the game drawing over its movie");
    CHECK(take(movie(2, 634, 448, 1000), window(1, 640, 480, 0, 0), 1600) == WSI_VIDEOOUT_STALE,
          "once the movie has stopped too");
    /* not from a smaller window that got the display otherwise (the game's own, after a launcher's
     * larger one that never presented) */
    CHECK(take(window(2, 1280, 720, 1, 1000), window(1, 1920, 1080, 0, 0), 1000) == WSI_VIDEOOUT_REFUSED,
          "an older, larger window doesn't take it from a smaller game's that presents");
    /* nor a movie's window from the game's (older, smaller) */
    CHECK(take(movie(2, 1914, 1048, 1000), window(1, 640, 480, 0, 0), 1000) == WSI_VIDEOOUT_REFUSED,
          "an older, smaller window doesn't take it from a movie that presents");
    /* nor one smaller in one direction only */
    CHECK(take(window(1, 640, 480, 1, 1000), window(2, 640, 460, 0, 0), 1000) == WSI_VIDEOOUT_HIDDEN,
          "a window shorter than the game's");
    /* a newer window larger than the owner's (a launcher's, then the game's) takes it */
    CHECK(take(window(1, 640, 480, 1, 1000), window(2, 1920, 1080, 0, 0), 1000) == WSI_VIDEOOUT_NEWER,
          "a larger newer window");

    /* Ruined King (#288), changing its resolution: a 3840x2160 picture once on a window of its
     * own, then left; the game's own window, older, goes on at 2560x1080 and was refused for
     * being smaller, and the game stayed on its last picture. */
    CHECK(take(window(2, 3840, 2160, 1, 1000), window(1, 2560, 1080, 0, 0), 1040) == WSI_VIDEOOUT_REFUSED,
          "Ruined King: not while the 4K picture presents");
    CHECK(take(window(2, 3840, 2160, 1, 1000), window(1, 2560, 1080, 0, 0), 1500) == WSI_VIDEOOUT_STALE,
          "Ruined King: the game's own window, once the 4K one stopped");
    CHECK(take(window(2, 3840, 2160, 1, 1000), window(1, 1920, 1080, 0, 0), 3000) == WSI_VIDEOOUT_STALE,
          "Ruined King: at 1080p too");
    /* a helper's never, from an owner that stopped or not */
    CHECK(take(window(2, 3840, 2160, 1, 1000), window(1, 113, 2, 0, 0), 9000) == WSI_VIDEOOUT_REFUSED,
          "an older helper's strip");

    /* Helpers' pictures never take it from an owner that presents: wined3d's 10x10 probe,
     * Warcraft III's and Blade & Sword's 111x1, Max Payne 3's 113x2 (#262). */
    CHECK(take(window(1, 800, 600, 1, 1000), window(2, 111, 1, 0, 0), 9000) == WSI_VIDEOOUT_REFUSED, "a 111x1 strip");
    CHECK(take(window(1, 1920, 1058, 1, 1000), window(2, 113, 2, 0, 0), 9000) == WSI_VIDEOOUT_REFUSED, "a 113x2 strip");
    CHECK(take(window(1, 320, 200, 1, 1000), window(2, 10, 10, 0, 0), 9000) == WSI_VIDEOOUT_REFUSED, "a 10x10 probe");
    CHECK(wsi_videoout_window_sized(320, 200) && !wsi_videoout_window_sized(319, 200) && !wsi_videoout_window_sized(320, 199),
          "a window's picture is 320x200 or more");

    if (failures) return 1;
    puts("test_videoout: ok");
    return 0;
}
