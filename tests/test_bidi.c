/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "pc5_bidi.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *worker(void *unused)
{
    (void)unused;
    for (int i = 0; i < 300; ++i) {
        char a[PC5_BIDI_BYTES], b[PC5_BIDI_BYTES]; int rtl = 0;
        const char *text = i % 2 ? "العربية PC5 2.2.2" : "موسيقى Prince of Persia";
        int length = pc5_bidi_visual(text, strlen(text), a, sizeof(a), &rtl);
        assert(length > 0 && rtl);
        assert(pc5_bidi_visual(text, strlen(text), b, sizeof(b), NULL) == length && !strcmp(a, b));
    }
    return NULL;
}
int main(void)
{
    char out[PC5_BIDI_BYTES]; int rtl;
    const char *arabic = "العربية";
    /* The exact seven joined presentation forms, in visual glyph order. */
    const char expected[] = "\xef\xba\x94\xef\xbb\xb4\xef\xba\x91\xef\xba\xae\xef\xbb\x8c\xef\xbb\x9f\xef\xba\x8d";
    assert(pc5_bidi_visual(arabic, strlen(arabic), out, sizeof(out), &rtl) == 21);
    assert(rtl && !strcmp(out, expected));
    assert(pc5_bidi_visual("لا", strlen("لا"), out, sizeof(out), &rtl) == 3);
    assert(!strcmp(out, "\xef\xbb\xbb"));
    const char *mixed = "PC5 2.2.2 — موسيقى Prince of Persia 1080p / 60 fps";
    assert(pc5_bidi_visual(mixed, strlen(mixed), out, sizeof(out), &rtl) > 0 && rtl);
    assert(strstr(out, "PC5 2.2.2") && strstr(out, "Prince of Persia 1080p / 60 fps"));
    const char *units = "إخراج 120 Hz وفيديو 1080p / 60 fps";
    assert(pc5_bidi_visual(units, strlen(units), out, sizeof(out), &rtl) > 0 && rtl);
    assert(strstr(out, "120 Hz") && strstr(out, "1080p / 60 fps"));
    const char *rates = "توليد الإطارات: 30 → 60 إطارا/ث";
    assert(pc5_bidi_visual(rates, strlen(rates), out, sizeof(out), &rtl) > 0 && rtl);
    assert(strstr(out, "30 → 60"));
    const char *brackets = "الإصدار (PC5 2.2.2)";
    assert(pc5_bidi_visual(brackets, strlen(brackets), out, sizeof(out), &rtl) > 0 && rtl);
    assert(strstr(out, "(PC5 2.2.2)"));
    const char *value = "المتجر والإصدار: \xe2\x81\xa8Sample report (rendering fixture)\xe2\x81\xa9";
    assert(pc5_bidi_visual(value, strlen(value), out, sizeof(out), &rtl) > 0 && rtl);
    assert(strstr(out, "Sample report (rendering fixture)"));
    const char *path = "/data/صور/game.mp4";
    assert(pc5_bidi_visual(path, strlen(path), out, sizeof(out), &rtl) > 0 && !rtl);
    assert(!strncmp(out, "/data/", 6) && strstr(out, "/game.mp4"));
    const char *sentence = "المسار: /data/pc5/game.mp4";
    assert(pc5_bidi_visual(sentence, strlen(sentence), out, sizeof(out), &rtl) > 0 && rtl);
    assert(strstr(out, "/data/pc5/game.mp4"));
    const char *spaces = "المسار: /data/Prince of Persia/game.mp4";
    assert(pc5_bidi_visual(spaces, strlen(spaces), out, sizeof(out), &rtl) > 0 && rtl);
    assert(strstr(out, "/data/Prince of Persia/game.mp4"));
    const char *isolated = "المسار: \xe2\x81\xa8/data/صور/game.mp4\xe2\x81\xa9";
    assert(pc5_bidi_visual(isolated, strlen(isolated), out, sizeof(out), &rtl) > 0 && rtl);
    assert(strstr(out, "/data/") && strstr(out, "/game.mp4") && !strstr(out, "\xe2\x81"));
    /* Isolation can also be needed with no Arabic characters in a value. */
    const char *controls = "\xe2\x81\xa8/data/game.mp4\xe2\x81\xa9";
    assert(pc5_bidi_visual(controls, strlen(controls), out, sizeof(out), &rtl) > 0);
    assert(!strcmp(out, "/data/game.mp4"));
    struct { char text[4]; unsigned guard; } small = { { 0 }, 0xa5a5a5a5 };
    assert(pc5_bidi_visual(arabic, strlen(arabic), small.text, sizeof(small.text), NULL) == -1);
    assert(!small.text[0] && small.guard == 0xa5a5a5a5);
    const char bad[] = "ع\xc0\xaf";
    assert(pc5_bidi_visual(bad, sizeof(bad) - 1, out, sizeof(out), NULL) == -1);
    assert(pc5_bidi_visual(arabic, strlen(arabic) - 1, out, sizeof(out), NULL) == -1);
    assert(pc5_bidi_visual("English stays unchanged", 23, out, sizeof(out), &rtl) == -1 && !rtl);
    assert(pc5_bidi_visual(NULL, 1, out, sizeof(out), NULL) == -1);
    char large[PC5_BIDI_BYTES];
    for (int i = 0; i < 2049; ++i) { large[i * 2] = (char)0xd8; large[i * 2 + 1] = (char)0xb9; }
    assert(pc5_bidi_visual(large, 4098, out, sizeof(out), NULL) == -1);
    assert(pc5_bidi_visual(large, sizeof(large), out, sizeof(out), NULL) == -1);
    pthread_t threads[8];
    for (int i = 0; i < 8; ++i) assert(!pthread_create(&threads[i], NULL, worker, NULL));
    for (int i = 0; i < 8; ++i) assert(!pthread_join(threads[i], NULL));
    puts("Arabic joining, mixed text, paths, UTF-8 bounds and concurrent cache: passed");
    return 0;
}
