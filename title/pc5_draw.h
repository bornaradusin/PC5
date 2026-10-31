/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: drawing into the 1920x1080 frame on the CPU: shapes with soft edges,
 * pictures (covers in perspective, with their reflections) and text from the
 * font atlas tools/make-font.py makes. */
#ifndef PC5_DRAW_H
#define PC5_DRAW_H
#include "pc5.h"

typedef struct Pc5Canvas {
    uint32_t *px;      /* 0xAARRGGBB, the frame's BGRX */
    int w, h, stride;  /* stride in pixels */
    /* Shapes and pictures are drawn only into columns clip_x0 to clip_x1 - 1 (so threads can
     * draw beside each other); clip_x1 0: every column. Text isn't clipped. */
    int clip_x0, clip_x1;
} Pc5Canvas;

/* ---- text ------------------------------------------------------------------------ */
typedef enum Pc5Face { PC5_FACE_SMALL, PC5_FACE_BODY, PC5_FACE_LABEL, PC5_FACE_HEAD, PC5_FACE_TITLE,
                       PC5_FACE_CARD, PC5_FACE_BUTTON, PC5_FACE_HERO, PC5_FACES } Pc5Face;

typedef struct Pc5Glyph {
    uint32_t codepoint;
    uint16_t x, y, w, h;
    int16_t left, top;
    uint16_t advance;    /* 64ths of a pixel */
} Pc5Glyph;

typedef struct Pc5Fonts {
    int ascent[PC5_FACES], descent[PC5_FACES], line[PC5_FACES];
    const Pc5Glyph *glyphs[PC5_FACES];
    uint32_t count[PC5_FACES];
    const uint8_t *atlas;
    int atlas_w, atlas_h;
    Pc5Buf file;
    /* A language's own letters (Chinese, Japanese, Korean: pc5/fonts-<code>.bin, made as
     * fonts.bin is), looked in when these fonts haven't a letter; NULL: none. */
    struct Pc5Fonts *extra;
} Pc5Fonts;

int pc5_fonts_load(Pc5Fonts *fonts, const char *path);
/* The extra letters from path (replacing those there were), or none (path NULL). 0, or -1
 * when the file couldn't be read (none then). */
int pc5_fonts_extra(Pc5Fonts *fonts, const char *path);
void pc5_fonts_free(Pc5Fonts *fonts);
/* Width of text in pixels. */
int pc5_text_width(const Pc5Fonts *fonts, Pc5Face face, const char *text);
/* How many times pc5_text has cut a text short (with "…"): the tests check menus have none. */
extern int pc5_text_cuts;
/* Draws text with its baseline at y, cut with "…" to max_width (0: no
 * limit). align: 0 left of x, 1 centred on x, 2 right of x. Returns the
 * width drawn. */
int pc5_text(Pc5Canvas *c, const Pc5Fonts *fonts, Pc5Face face, int x, int y, uint32_t color, const char *text,
             int max_width, int align);
/* Text wraps at spaces, and between the characters of Chinese and Japanese (not before the
 * punctuation that closes, nor after what opens). */
/* Text wrapped into lines of max_width, at most max_lines (the last cut with
 * "…"), each centred (align 1) or left (0). Returns the lines drawn. */
int pc5_text_wrap(Pc5Canvas *c, const Pc5Fonts *fonts, Pc5Face face, int x, int y, int max_width, int max_lines,
                  int line_height, uint32_t color, const char *text, int align);
/* How many lines pc5_text_wrap needs for all of text at max_width (0 for none). */
int pc5_text_lines(const Pc5Fonts *fonts, Pc5Face face, int max_width, const char *text);

/* ---- shapes --------------------------------------------------------------------- */
uint32_t pc5_rgba(int r, int g, int b, int a);
uint32_t pc5_mix(uint32_t a, uint32_t b, int t255);   /* a towards b */
void pc5_clear(Pc5Canvas *c, uint32_t color);
/* A rectangle with rounded corners (radius 0: square), soft-edged. */
void pc5_round_rect(Pc5Canvas *c, float x, float y, float w, float h, float radius, uint32_t color);
void pc5_round_frame(Pc5Canvas *c, float x, float y, float w, float h, float radius, float width, uint32_t color);
/* The same frame, its colour going from from (top left) to to (bottom right). */
void pc5_round_frame_graded(Pc5Canvas *c, float x, float y, float w, float h, float radius, float width, uint32_t from,
                            uint32_t to);
void pc5_circle(Pc5Canvas *c, float cx, float cy, float r, uint32_t color);
void pc5_ring(Pc5Canvas *c, float cx, float cy, float r, float width, uint32_t color);
void pc5_line(Pc5Canvas *c, float x0, float y0, float x1, float y1, float width, uint32_t color);
/* An arc from angle a0 to a1 (radians, clockwise from the top). */
void pc5_arc(Pc5Canvas *c, float cx, float cy, float r, float width, float a0, float a1, uint32_t color);
/* A soft shadow (or glow) around a rounded rectangle. */
void pc5_shadow(Pc5Canvas *c, float x, float y, float w, float h, float radius, float spread, uint32_t color);

/* ---- pictures ------------------------------------------------------------------------ */
/* A picture, in up to three sizes (full, half, quarter) so shrinking stays
 * smooth. */
typedef struct Pc5Picture {
    Pc5Image level[3];
    int levels;
} Pc5Picture;

int pc5_picture_make(Pc5Picture *picture, Pc5Image *full);   /* takes full over */
void pc5_picture_free(Pc5Picture *picture);

/* A picture drawn into a quad standing up: x..x+w across, its left edge
 * h_left tall and its right edge h_right tall, both centred on cy (a cover
 * turned a little about its upright axis). Corners rounded by radius,
 * colours multiplied by bright (0-256), the whole by alpha (0-256).
 * reflect: also its mirror image under it, fading out over reflect pixels. */
void pc5_draw_cover(Pc5Canvas *c, const Pc5Picture *picture, float x, float cy, float w, float h_left, float h_right,
                    float radius, int bright, int alpha, float reflect);
/* A cover standing on the floor (its bottom at y = ground): x..x+w across, its left edge
 * h_left tall and its right edge h_right tall (turned a little about its upright axis).
 * Corners rounded by radius, colours by bright (0-256), the whole by alpha (0-256); a sheen
 * from its top left corner, gloss (0-256) at most; and its mirror image on the floor under
 * it, reflect (0-256) at most, fading out over fade of its height. */
void pc5_draw_card(Pc5Canvas *c, const Pc5Picture *picture, float x, float ground, float w, float h_left, float h_right,
                   float radius, int bright, int alpha, int reflect, float fade, int gloss);
/* A picture scaled into a rectangle (alpha 0-256). */
void pc5_draw_image(Pc5Canvas *c, const Pc5Image *image, float x, float y, float w, float h, int alpha);

#endif
