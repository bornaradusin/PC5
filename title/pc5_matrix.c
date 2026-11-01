/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: the loading screen (pc5_matrix.h): its picture, "LAUNCHING GAME" and a bar filling. */
#include "pc5_matrix.h"
#include "pc5_i18n.h"
#include "pc5_image.h"
#include "pc5_loading_art.h"

#include <math.h>
#include <string.h>

/* Where the words and the bar sit, in the 1280x720 frame (under the picture's computer). */
enum { WORDS_Y = 606, BAR_Y = 632, BAR_W = 272, BAR_H = 3, SPACING = 7 };

struct Pc5Matrix {
    Pc5Fonts fonts;
    uint32_t *px;          /* the frame */
    uint32_t *picture;     /* the picture, 1280x720, or NULL: a dark blue fade in its place */
    char words[96];        /* "LAUNCHING GAME", in the player's language */
    uint64_t steps;
};

/* The picture as the frame's size; NULL when it can't be read. */
static uint32_t *load_picture(void)
{
    Pc5Image image, fitted;
    uint32_t *px = NULL;
    if (pc5_image_decode(pc5_loading_jpg, sizeof(pc5_loading_jpg), &image)) return NULL;
    if (image.w == PC5_MATRIX_WIDTH && image.h == PC5_MATRIX_HEIGHT) {
        px = image.px;   /* kept */
        return px;
    }
    if (!pc5_image_resize(&image, PC5_MATRIX_WIDTH, PC5_MATRIX_HEIGHT, &fitted)) px = fitted.px;
    pc5_image_free(&image);
    return px;
}

Pc5Matrix *pc5_matrix_create(Pc5Fonts *fonts, const char *title, uint32_t seed)
{
    Pc5Matrix *m = pc5_alloc(sizeof(*m));
    (void)title;   /* the game's name isn't shown: the picture and the bar are */
    (void)seed;
    if (!m) return NULL;
    m->fonts = *fonts;
    *fonts = (Pc5Fonts){ 0 };
    m->px = pc5_alloc((size_t)PC5_MATRIX_WIDTH * PC5_MATRIX_HEIGHT * 4);
    if (!m->px) {
        pc5_fonts_free(&m->fonts);
        pc5_free(m);
        return NULL;
    }
    m->picture = load_picture();
    /* TRANSLATORS: over the loading screen's bar while a game starts, shown in capitals where the
     * language has them. */
    pc5_upper(T("Launching game"), m->words, sizeof(m->words));
    return m;
}

void pc5_matrix_destroy(Pc5Matrix *m)
{
    if (!m) return;
    pc5_fonts_free(&m->fonts);
    pc5_free(m->picture);
    pc5_free(m->px);
    pc5_free(m);
}

/* The bytes of the UTF-8 character at text. */
static size_t char_len(const char *text)
{
    unsigned char c = (unsigned char)*text;
    return c < 0x80 ? 1 : c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
}

/* text a character at a time with room between them, centred on cx. */
static void spaced(Pc5Canvas *c, const Pc5Fonts *fonts, int cx, int y, uint32_t color, const char *text)
{
    char one[8];
    int width = 0, count = 0;
    for (const char *p = text; *p; p += char_len(p)) {
        size_t n = char_len(p);
        memcpy(one, p, n);
        one[n] = 0;
        width += pc5_text_width(fonts, PC5_FACE_SMALL, one);
        count++;
    }
    width += SPACING * (count > 1 ? count - 1 : 0);
    int x = cx - width / 2;
    for (const char *p = text; *p; p += char_len(p)) {
        size_t n = char_len(p);
        memcpy(one, p, n);
        one[n] = 0;
        x += pc5_text(c, fonts, PC5_FACE_SMALL, x, y, color, one, 0, 0) + SPACING;
    }
}

const uint32_t *pc5_matrix_step(Pc5Matrix *m)
{
    const size_t n = (size_t)PC5_MATRIX_WIDTH * PC5_MATRIX_HEIGHT;
    Pc5Canvas c = { m->px, PC5_MATRIX_WIDTH, PC5_MATRIX_HEIGHT, PC5_MATRIX_WIDTH, 0, 0 };
    const float seconds = (float)(m->steps * PC5_MATRIX_STEP_MS) / 1000.0f;

    if (m->picture) {
        memcpy(m->px, m->picture, n * 4);
    } else {
        for (int y = 0; y < PC5_MATRIX_HEIGHT; y++) {
            uint32_t b = (uint32_t)(10 + 26 * y / PC5_MATRIX_HEIGHT);
            for (int x = 0; x < PC5_MATRIX_WIDTH; x++) m->px[(size_t)y * PC5_MATRIX_WIDTH + x] = 0xff000000u | (b / 3) << 8 | b;
        }
    }
    /* It fades in over the first half second. */
    float shown = seconds < 0.5f ? seconds / 0.5f : 1.0f;
    if (shown < 1.0f) {
        uint32_t k = (uint32_t)(shown * 256.0f);
        for (size_t i = 0; i < n; i++) {
            uint32_t p = m->px[i];
            m->px[i] = 0xff000000u | ((((p >> 16) & 255) * k) >> 8) << 16 | ((((p >> 8) & 255) * k) >> 8) << 8 | (((p & 255) * k) >> 8);
        }
    }
    int alpha = (int)(shown * 255.0f);
    if (m->fonts.atlas) spaced(&c, &m->fonts, PC5_MATRIX_WIDTH / 2, WORDS_Y, pc5_rgba(222, 230, 242, alpha), m->words);

    /* The bar: how long a game takes to start isn't known, so it fills quickly at first and ever
     * more slowly, nearly to the end; the game's first picture takes its place. */
    float fill = 0.94f * (1.0f - expf(-seconds / 7.0f)) + 0.04f;
    float bx = (PC5_MATRIX_WIDTH - BAR_W) / 2.0f, filled = BAR_W * fill;
    pc5_round_rect(&c, bx, BAR_Y, BAR_W, BAR_H, BAR_H / 2.0f, pc5_rgba(60, 72, 92, alpha * 150 / 255));
    pc5_shadow(&c, bx, BAR_Y, filled, BAR_H, BAR_H / 2.0f, 8, pc5_rgba(40, 140, 255, alpha * 70 / 255));
    pc5_round_rect(&c, bx, BAR_Y, filled, BAR_H, BAR_H / 2.0f, pc5_rgba(38, 150, 255, alpha));
    /* a light running along what is filled */
    float sweep = fmodf(seconds / 1.6f, 1.0f) * (filled + 60.0f) - 30.0f;
    if (sweep > 0 && sweep < filled) {
        float w = sweep + 30.0f > filled ? filled - sweep : 30.0f;
        pc5_round_rect(&c, bx + sweep, BAR_Y, w, BAR_H, BAR_H / 2.0f, pc5_rgba(160, 220, 255, alpha * 160 / 255));
    }
    m->steps++;
    return m->px;
}
