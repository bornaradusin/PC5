/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: the loading screen (pc5_matrix.h): its picture, "LAUNCHING GAME" and a circle spinning. */
#include "pc5_matrix.h"
#include "pc5_cover.h"
#include "pc5_i18n.h"
#include "pc5_image.h"
#include "pc5_loading_art.h"

#include <math.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

/* Where the circle and the words sit, in the 1280x720 frame: the bottom right corner, the words
 * just left of the circle, as games show their own loading. */
enum { SPIN_X = 1280 - 52, SPIN_Y = 720 - 48, SPIN_R = 11, WORDS_RIGHT = SPIN_X - SPIN_R - 18, WORDS_Y = SPIN_Y + 9,
       SPACING = 7 };

struct Pc5Matrix {
    Pc5Fonts own;          /* the fonts it was given, freed with it */
    const Pc5Fonts *fonts; /* &own, or the caller's */
    uint32_t *px;          /* the frame */
    uint32_t *picture;     /* the picture, 1280x720, or NULL: a dark blue fade in its place */
    char words[96];        /* "LAUNCHING GAME", in the player's language */
    uint64_t steps;
    int no_fade;           /* shown at once: PC5 showed it before */
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

static Pc5Matrix *matrix_new(void)
{
    Pc5Matrix *m = pc5_alloc(sizeof(*m));
    if (!m) return NULL;
    memset(m, 0, sizeof(*m));
    m->px = pc5_alloc((size_t)PC5_MATRIX_WIDTH * PC5_MATRIX_HEIGHT * 4);
    if (!m->px) {
        pc5_free(m);
        return NULL;
    }
    m->fonts = &m->own;
    m->picture = load_picture();
    /* TRANSLATORS: over the loading screen's spinning circle while a game starts, shown in capitals
     * where the language has them. */
    pc5_upper(T("Launching game"), m->words, sizeof(m->words));
    return m;
}

Pc5Matrix *pc5_matrix_create(Pc5Fonts *fonts, const char *title, uint32_t seed)
{
    Pc5Matrix *m = matrix_new();
    (void)title;   /* the game's name isn't shown: the picture and the bar are */
    (void)seed;
    if (!m) return NULL;   /* the fonts stay the caller's */
    m->own = *fonts;
    *fonts = (Pc5Fonts){ 0 };
    return m;
}

Pc5Matrix *pc5_matrix_borrow(const Pc5Fonts *fonts)
{
    Pc5Matrix *m = matrix_new();
    if (!m) return NULL;
    m->fonts = fonts;
    m->no_fade = 1;
    return m;
}

void pc5_matrix_at_once(Pc5Matrix *m)
{
    if (m) m->no_fade = 1;
}

/* image filled into the frame: cut to 16:9 about its middle, then to 1280x720, opaque. */
static int fill_frame(const Pc5Image *image, Pc5Image *fitted)
{
    Pc5Image cut = { 0 };
    int w = image->w, h = image->h, status = -1;

    if (w <= 0 || h <= 0 || !image->px) return -1;
    if ((int64_t)w * PC5_MATRIX_HEIGHT > (int64_t)h * PC5_MATRIX_WIDTH) w = (int)((int64_t)h * PC5_MATRIX_WIDTH / PC5_MATRIX_HEIGHT);
    else h = (int)((int64_t)w * PC5_MATRIX_HEIGHT / PC5_MATRIX_WIDTH);
    if (w <= 0 || h <= 0 || !(cut.px = pc5_alloc((size_t)w * h * 4))) return -1;
    int x0 = (image->w - w) / 2, y0 = (image->h - h) / 2;
    cut.w = w;
    cut.h = h;
    for (int y = 0; y < h; y++) memcpy(cut.px + (size_t)y * w, image->px + (size_t)(y0 + y) * image->w + x0, (size_t)w * 4);
    if (!pc5_image_resize(&cut, PC5_MATRIX_WIDTH, PC5_MATRIX_HEIGHT, fitted)) {
        for (size_t i = 0; i < (size_t)PC5_MATRIX_WIDTH * PC5_MATRIX_HEIGHT; i++) fitted->px[i] |= 0xff000000u;
        status = 0;
    }
    pc5_image_free(&cut);
    return status;
}

int pc5_matrix_picture(Pc5Matrix *m, const char *path)
{
    Pc5Buf bytes = { 0 };
    Pc5Image image = { 0 }, fitted = { 0 };
    int status = -1;

    if (!m || !path || pc5_read_file(path, &bytes, 64u << 20)) return -1;
    if (!pc5_image_decode(bytes.data, bytes.len, &image) && !fill_frame(&image, &fitted)) {
        pc5_free(m->picture);
        m->picture = fitted.px;   /* kept */
        status = 0;
    }
    pc5_image_free(&image);
    pc5_buf_free(&bytes);
    return status;
}

int pc5_loading_save(const Pc5Image *image, const char *path)
{
    Pc5Image fitted = { 0 };
    Pc5Buf png = { 0 };
    char part[PC5_PATH];
    int status = -1;

    if (!image || !path || pc5_format(part, sizeof(part), "%s.part", path) || fill_frame(image, &fitted)) return -1;
    if (!pc5_image_png(&fitted, &png) && !pc5_write_file(part, png.data, png.len) && !rename(part, path)) status = 0;
    else unlink(part);
    pc5_buf_free(&png);
    pc5_image_free(&fitted);
    return status;
}

int pc5_loading_chosen(const char *root, const char *prefix, char *out, size_t size)
{
    if (!root || !prefix || !prefix[0] || !strcmp(prefix, "default") ||
        pc5_format(out, size, "%s/prefixes/%s/pc5/loading.png", root, prefix))
        return -1;
    return pc5_is_file(out) ? 0 : -1;
}

int pc5_loading_own(const char *root, const char *prefix, const char *folder, const char *title, const char *id, char *out,
                    size_t size)
{
    char dir[PC5_PATH];
    if (!pc5_loading_chosen(root, prefix, out, size)) return 0;   /* chosen in its Appearance */
    const char *const names[] = { folder ? folder : "", title ? title : "", id ? id : "", NULL };
    const char *const every[] = { "loading", NULL };

    if (!root || pc5_format(dir, sizeof(dir), "%s/loading", root) || !pc5_is_dir(dir)) return -1;
    if (!pc5_cover_own(dir, names, out, size)) return 0;
    return pc5_cover_own(dir, every, out, size);
}

void pc5_matrix_destroy(Pc5Matrix *m)
{
    if (!m) return;
    pc5_fonts_free(&m->own);
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

/* text a character at a time with room between them, ending at right. */
static void spaced(Pc5Canvas *c, const Pc5Fonts *fonts, int right, int y, uint32_t color, const char *text)
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
    int x = right - width;
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
    /* It fades in over the first half second, unless PC5 showed it before. */
    float shown = m->no_fade || seconds >= 0.5f ? 1.0f : seconds / 0.5f;
    if (shown < 1.0f) {
        uint32_t k = (uint32_t)(shown * 256.0f);
        for (size_t i = 0; i < n; i++) {
            uint32_t p = m->px[i];
            m->px[i] = 0xff000000u | ((((p >> 16) & 255) * k) >> 8) << 16 | ((((p >> 8) & 255) * k) >> 8) << 8 | (((p & 255) * k) >> 8);
        }
    }
    /* The bottom right corner a little darker, so the words and the circle read on any picture
     * (a player's own bright one too): towards the corner, up to about half. */
    for (int y = PC5_MATRIX_HEIGHT - 150; y < PC5_MATRIX_HEIGHT; y++)
        for (int x = PC5_MATRIX_WIDTH - 620; x < PC5_MATRIX_WIDTH; x++) {
            float fy = (float)(y - (PC5_MATRIX_HEIGHT - 150)) / 150.0f, fx = (float)(x - (PC5_MATRIX_WIDTH - 620)) / 620.0f;
            float d = fy * fx;
            uint32_t k = 256 - (uint32_t)(140.0f * (d < 1.0f ? d : 1.0f)), p = m->px[(size_t)y * PC5_MATRIX_WIDTH + x];
            m->px[(size_t)y * PC5_MATRIX_WIDTH + x] = 0xff000000u | ((((p >> 16) & 255) * k) >> 8) << 16 |
                                                      ((((p >> 8) & 255) * k) >> 8) << 8 | (((p & 255) * k) >> 8);
        }
    int alpha = (int)(shown * 255.0f);
    if (m->fonts->atlas)
        spaced(&c, m->fonts, WORDS_RIGHT, WORDS_Y, pc5_rgba(222, 230, 242, alpha), m->words);

    /* A circle spinning: a faint ring, and a quarter of it going round once a second. */
    const float pi = 3.14159265f, turn = fmodf(seconds, 1.0f) * 2.0f * pi;
    pc5_ring(&c, SPIN_X, SPIN_Y, SPIN_R, 3.0f, pc5_rgba(60, 72, 92, alpha * 150 / 255));
    pc5_arc(&c, SPIN_X, SPIN_Y, SPIN_R, 3.0f, turn, turn + pi / 2.0f, pc5_rgba(38, 150, 255, alpha));
    m->steps++;
    return m->px;
}
