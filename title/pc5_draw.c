/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: drawing (pc5_draw.h). Shapes are drawn from their distance to each
 * pixel, which gives soft edges without any oversampling. */
#include "pc5_draw.h"
#include "pc5_image.h"

#include <math.h>
#include <string.h>

/* ---- colours ------------------------------------------------------------------------- */

uint32_t pc5_rgba(int r, int g, int b, int a)
{
    return (uint32_t)(a & 255) << 24 | (uint32_t)(r & 255) << 16 | (uint32_t)(g & 255) << 8 | (uint32_t)(b & 255);
}

uint32_t pc5_mix(uint32_t a, uint32_t b, int t)
{
    uint32_t out = 0;
    if (t < 0) t = 0;
    if (t > 255) t = 255;
    for (int k = 0; k < 32; k += 8) {
        int x = (int)((a >> k) & 255), y = (int)((b >> k) & 255);
        out |= (uint32_t)(x + (y - x) * t / 255) << k;
    }
    return out;
}

/* dst blended towards color by color's alpha times cover (0-256). */
static inline void blend(uint32_t *dst, uint32_t color, int cover)
{
    int a = (int)(color >> 24) * cover >> 8;
    if (a <= 0) return;
    if (a >= 255) { *dst = color | 0xff000000u; return; }
    uint32_t d = *dst;
    uint32_t rb = ((color & 0xff00ffu) * (uint32_t)a + (d & 0xff00ffu) * (uint32_t)(255 - a)) >> 8 & 0xff00ffu;
    uint32_t g = ((color & 0x00ff00u) * (uint32_t)a + (d & 0x00ff00u) * (uint32_t)(255 - a)) >> 8 & 0x00ff00u;
    *dst = 0xff000000u | rb | g;
}

void pc5_clear(Pc5Canvas *c, uint32_t color)
{
    for (int y = 0; y < c->h; y++) {
        uint32_t *row = c->px + (size_t)y * (size_t)c->stride;
        for (int x = 0; x < c->w; x++) row[x] = color | 0xff000000u;
    }
}

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* The columns drawn into: the canvas's clip (clip_x0 to clip_x1 - 1), or all of them. */
static inline int first_column(const Pc5Canvas *c) { return c->clip_x1 > 0 && c->clip_x0 > 0 ? c->clip_x0 : 0; }
static inline int end_column(const Pc5Canvas *c) { return c->clip_x1 > 0 && c->clip_x1 < c->w ? c->clip_x1 : c->w; }

/* ---- shapes --------------------------------------------------------------------------- */

/* Signed distance from (px, py) to a rounded rectangle given by its centre,
 * half sizes and radius: negative inside. */
static inline float rect_distance(float px, float py, float cx, float cy, float hw, float hh, float r)
{
    float qx = fabsf(px - cx) - (hw - r), qy = fabsf(py - cy) - (hh - r);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    float inside = qx > qy ? qx : qy;
    return sqrtf(ox * ox + oy * oy) + (inside < 0 ? inside : 0) - r;
}

/* A rounded rectangle, filled or (width > 0) its frame, in color; with color2 not color, the
 * colour goes from color at its top left corner to color2 at its bottom right. */
static void rect_shape(Pc5Canvas *c, float x, float y, float w, float h, float radius, float width, uint32_t color,
                       uint32_t color2)
{
    float hw = w / 2, hh = h / 2, cx = x + hw, cy = y + hh;
    int x0, x1, y0, y1, graded = color2 != color;

    if (w <= 0 || h <= 0 || (!(color >> 24) && !(color2 >> 24))) return;
    if (radius > hw) radius = hw;
    if (radius > hh) radius = hh;
    x0 = (int)floorf(x - 1); x1 = (int)ceilf(x + w + 1);
    y0 = (int)floorf(y - 1); y1 = (int)ceilf(y + h + 1);
    if (x0 < first_column(c)) x0 = first_column(c);
    if (y0 < 0) y0 = 0;
    if (x1 > end_column(c)) x1 = end_column(c);
    if (y1 > c->h) y1 = c->h;
    for (int py = y0; py < y1; py++) {
        uint32_t *row = c->px + (size_t)py * (size_t)c->stride;
        float fy = py + 0.5f;
        /* The span sure to be inside (filled) or, for a frame, inside the hole. */
        float dy = fabsf(fy - cy) - (hh - radius);
        float half = dy <= 0 ? hw : (dy < radius ? (hw - radius) + sqrtf(radius * radius - dy * dy) : 0);
        int in0 = (int)ceilf(cx - half + 1.5f), in1 = (int)floorf(cx + half - 1.5f);
        if (width > 0) {
            float hole = half - width;
            in0 = (int)ceilf(cx - hole + 1.5f);
            in1 = (int)floorf(cx + hole - 1.5f);
            if (fabsf(fy - cy) > hh - width - 1.5f) in0 = 1, in1 = 0;
        }
        for (int px = x0; px < x1; px++) {
            uint32_t at = color;
            if (graded) at = pc5_mix(color, color2, (int)(clampf(((px + 0.5f - x) / w * 0.45f + (fy - y) / h * 0.55f), 0, 1) * 255));
            if (px >= in0 && px <= in1) {
                if (width > 0) { px = in1; continue; }
                blend(&row[px], at, 256);
                continue;
            }
            float d = rect_distance(px + 0.5f, fy, cx, cy, hw, hh, radius);
            if (width > 0) d = fabsf(d + width / 2) - width / 2;
            float cover = clampf(0.5f - d, 0, 1);
            if (cover > 0) blend(&row[px], at, (int)(cover * 256));
        }
    }
}

void pc5_round_rect(Pc5Canvas *c, float x, float y, float w, float h, float radius, uint32_t color)
{
    rect_shape(c, x, y, w, h, radius, 0, color, color);
}

void pc5_round_frame(Pc5Canvas *c, float x, float y, float w, float h, float radius, float width, uint32_t color)
{
    rect_shape(c, x, y, w, h, radius, width, color, color);
}

void pc5_round_frame_graded(Pc5Canvas *c, float x, float y, float w, float h, float radius, float width, uint32_t from,
                            uint32_t to)
{
    rect_shape(c, x, y, w, h, radius, width, from, to);
}

/* Any shape given by a distance function over a box. */
typedef float (*DistanceFn)(float px, float py, const float *p);

static void shape(Pc5Canvas *c, float bx0, float by0, float bx1, float by1, DistanceFn fn, const float *p, uint32_t color)
{
    int x0 = (int)floorf(bx0), y0 = (int)floorf(by0), x1 = (int)ceilf(bx1), y1 = (int)ceilf(by1);
    if (x0 < first_column(c)) x0 = first_column(c);
    if (y0 < 0) y0 = 0;
    if (x1 > end_column(c)) x1 = end_column(c);
    if (y1 > c->h) y1 = c->h;
    for (int py = y0; py < y1; py++) {
        uint32_t *row = c->px + (size_t)py * (size_t)c->stride;
        for (int px = x0; px < x1; px++) {
            float cover = clampf(0.5f - fn(px + 0.5f, py + 0.5f, p), 0, 1);
            if (cover > 0) blend(&row[px], color, (int)(cover * 256));
        }
    }
}

static float circle_fn(float px, float py, const float *p)
{
    return sqrtf((px - p[0]) * (px - p[0]) + (py - p[1]) * (py - p[1])) - p[2];
}

static float ring_fn(float px, float py, const float *p)
{
    return fabsf(circle_fn(px, py, p)) - p[3] / 2;
}

static float line_fn(float px, float py, const float *p)
{
    float ax = px - p[0], ay = py - p[1], bx = p[2] - p[0], by = p[3] - p[1];
    float t = clampf((ax * bx + ay * by) / (bx * bx + by * by + 1e-6f), 0, 1);
    float dx = ax - bx * t, dy = ay - by * t;
    return sqrtf(dx * dx + dy * dy) - p[4] / 2;
}

static float arc_fn(float px, float py, const float *p)
{
    float angle = atan2f(px - p[0], -(py - p[1]));   /* clockwise from the top */
    if (angle < 0) angle += 6.2831853f;
    float a0 = p[4], a1 = p[5];
    int inside = a1 - a0 >= 6.283f || (angle >= a0 && angle <= a1) || (angle + 6.2831853f >= a0 && angle + 6.2831853f <= a1);
    if (inside) return fabsf(circle_fn(px, py, p)) - p[3] / 2;
    /* the round ends */
    float d = 1e9f;
    for (int k = 0; k < 2; k++) {
        float a = k ? a1 : a0, ex = p[0] + sinf(a) * p[2], ey = p[1] - cosf(a) * p[2];
        float e = sqrtf((px - ex) * (px - ex) + (py - ey) * (py - ey)) - p[3] / 2;
        if (e < d) d = e;
    }
    return d;
}

void pc5_circle(Pc5Canvas *c, float cx, float cy, float r, uint32_t color)
{
    float p[3] = { cx, cy, r };
    shape(c, cx - r - 1, cy - r - 1, cx + r + 1, cy + r + 1, circle_fn, p, color);
}

void pc5_ring(Pc5Canvas *c, float cx, float cy, float r, float width, uint32_t color)
{
    float p[4] = { cx, cy, r, width }, e = r + width + 1;
    shape(c, cx - e, cy - e, cx + e, cy + e, ring_fn, p, color);
}

void pc5_line(Pc5Canvas *c, float x0, float y0, float x1, float y1, float width, uint32_t color)
{
    float p[5] = { x0, y0, x1, y1, width }, e = width + 1;
    shape(c, fminf(x0, x1) - e, fminf(y0, y1) - e, fmaxf(x0, x1) + e, fmaxf(y0, y1) + e, line_fn, p, color);
}

void pc5_arc(Pc5Canvas *c, float cx, float cy, float r, float width, float a0, float a1, uint32_t color)
{
    while (a0 < 0) { a0 += 6.2831853f; a1 += 6.2831853f; }
    while (a0 >= 6.2831853f) { a0 -= 6.2831853f; a1 -= 6.2831853f; }
    float p[6] = { cx, cy, r, width, a0, a1 }, e = r + width + 1;
    shape(c, cx - e, cy - e, cx + e, cy + e, arc_fn, p, color);
}

void pc5_shadow(Pc5Canvas *c, float x, float y, float w, float h, float radius, float spread, uint32_t color)
{
    float hw = w / 2, hh = h / 2, cx = x + hw, cy = y + hh;
    int x0 = (int)floorf(x - spread), y0 = (int)floorf(y - spread), x1 = (int)ceilf(x + w + spread),
        y1 = (int)ceilf(y + h + spread);
    if (x0 < first_column(c)) x0 = first_column(c);
    if (y0 < 0) y0 = 0;
    if (x1 > end_column(c)) x1 = end_column(c);
    if (y1 > c->h) y1 = c->h;
    for (int py = y0; py < y1; py++) {
        uint32_t *row = c->px + (size_t)py * (size_t)c->stride;
        /* Under the shape itself nothing shows: those columns are skipped. */
        int skip0 = 1, skip1 = 0;
        if (fabsf(py + 0.5f - cy) < hh - radius - 2) {
            skip0 = (int)ceilf(cx - hw + 2);
            skip1 = (int)floorf(cx + hw - 2);
        }
        for (int px = x0; px < x1; px++) {
            if (px >= skip0 && px < skip1) { px = skip1 - 1; continue; }
            float d = rect_distance(px + 0.5f, py + 0.5f, cx, cy, hw, hh, radius);
            if (d < -2) continue;
            float t = clampf(1 - d / spread, 0, 1);
            if (t > 0) blend(&row[px], color, (int)(t * t * 256));
        }
    }
}

/* ---- pictures --------------------------------------------------------------------------- */

int pc5_picture_make(Pc5Picture *picture, Pc5Image *full)
{
    memset(picture, 0, sizeof(*picture));
    picture->level[0] = *full;
    *full = (Pc5Image){ 0 };
    picture->levels = 1;
    for (int k = 1; k < 3; k++) {
        const Pc5Image *above = &picture->level[k - 1];
        if (above->w < 32 || pc5_image_resize(above, above->w / 2, above->h / 2, &picture->level[k])) break;
        picture->levels++;
    }
    return 0;
}

void pc5_picture_free(Pc5Picture *picture)
{
    for (int k = 0; k < 3; k++) pc5_image_free(&picture->level[k]);
    picture->levels = 0;
}

/* Bilinear sample at (u, v) in 16.16 fixed point, edges clamped. */
static inline uint32_t sample(const Pc5Image *im, int32_t u, int32_t v)
{
    int x = u >> 16, y = v >> 16;
    uint32_t fx = (uint32_t)(u >> 8) & 255, fy = (uint32_t)(v >> 8) & 255;
    if (x < 0) { x = 0; fx = 0; }
    if (y < 0) { y = 0; fy = 0; }
    if (x > im->w - 1) x = im->w - 1;
    if (y > im->h - 1) y = im->h - 1;
    int x1 = x + 1 < im->w ? x + 1 : x, y1 = y + 1 < im->h ? y + 1 : y;
    const uint32_t *r0 = im->px + (size_t)y * (size_t)im->w, *r1 = im->px + (size_t)y1 * (size_t)im->w;
    uint32_t a = r0[x], b = r0[x1], c = r1[x], d = r1[x1];
    uint32_t top_rb = ((a & 0xff00ffu) * (256 - fx) + (b & 0xff00ffu) * fx) >> 8 & 0xff00ffu;
    uint32_t top_ag = (((a >> 8) & 0xff00ffu) * (256 - fx) + ((b >> 8) & 0xff00ffu) * fx) >> 8 & 0xff00ffu;
    uint32_t bot_rb = ((c & 0xff00ffu) * (256 - fx) + (d & 0xff00ffu) * fx) >> 8 & 0xff00ffu;
    uint32_t bot_ag = (((c >> 8) & 0xff00ffu) * (256 - fx) + ((d >> 8) & 0xff00ffu) * fx) >> 8 & 0xff00ffu;
    uint32_t rb = (top_rb * (256 - fy) + bot_rb * fy) >> 8 & 0xff00ffu;
    uint32_t ag = (top_ag * (256 - fy) + bot_ag * fy) >> 8 & 0xff00ffu;
    return rb | ag << 8;
}

static const Pc5Image *pick_level(const Pc5Picture *picture, float height)
{
    int k = 0;
    while (k + 1 < picture->levels && height < picture->level[k + 1].h * 1.2f) k++;
    return &picture->level[k];
}

/* How much of a corner pixel is inside: distance to the corner's circle. */
static inline float corner_cover(float lx, float ly, float w, float h, float r)
{
    float dx = lx < r ? r - lx : lx > w - r ? lx - (w - r) : 0;
    float dy = ly < r ? r - ly : ly > h - r ? ly - (h - r) : 0;
    if (dx <= 0 || dy <= 0) return 1;
    return clampf(r - sqrtf(dx * dx + dy * dy) + 0.5f, 0, 1);
}

static inline uint32_t darken(uint32_t pixel, int bright)
{
    if (bright >= 256) return pixel;
    return (pixel & 0xff000000u) | ((((pixel >> 16) & 255) * (uint32_t)bright >> 8) << 16) |
           ((((pixel >> 8) & 255) * (uint32_t)bright >> 8) << 8) | ((pixel & 255) * (uint32_t)bright >> 8);
}

void pc5_draw_cover(Pc5Canvas *c, const Pc5Picture *picture, float x, float cy, float w, float h_left, float h_right,
                    float radius, int bright, int alpha, float reflect)
{
    const Pc5Image *im;
    int x0, x1;

    if (!picture->levels || w < 1 || alpha <= 0) return;
    im = pick_level(picture, h_left > h_right ? h_left : h_right);
    x0 = (int)floorf(x);
    x1 = (int)ceilf(x + w);
    if (x0 < first_column(c)) x0 = first_column(c);
    if (x1 > end_column(c)) x1 = end_column(c);
    for (int px = x0; px < x1; px++) {
        float t = (px + 0.5f - x) / w;
        if (t < 0 || t > 1) continue;
        float edge = clampf(fminf(px + 1 - x, x + w - px), 0, 1);   /* the outer columns, partly covered */
        float h = h_left + (h_right - h_left) * t, top = cy - h / 2, bottom = cy + h / 2;
        int32_t u = (int32_t)(t * im->w * 65536.0f) - 32768;
        float v_step = im->h / h;
        int y0 = (int)floorf(top), y1 = (int)ceilf(bottom + (reflect > 0 ? reflect : 0));
        float lx = t * w;
        /* Rows wholly inside, away from the rounded corners: copied straight. */
        int corner = radius > 0 && (lx < radius || lx > w - radius);
        float inner_top = top + (corner ? radius : 0), inner_bottom = bottom - (corner ? radius : 0);
        int fast0 = (int)ceilf(inner_top), fast1 = (int)floorf(inner_bottom);
        int fast = edge >= 1 && alpha >= 256 && fast1 > fast0;
        if (y0 < 0) y0 = 0;
        if (y1 > c->h) y1 = c->h;
        if (fast) {
            int a = fast0 < 0 ? 0 : fast0, b = fast1 > c->h ? c->h : fast1;
            int32_t v = (int32_t)(((float)a + 0.5f - top) * v_step * 65536.0f) - 32768, dv = (int32_t)(v_step * 65536.0f);
            uint32_t *dst = c->px + (size_t)a * (size_t)c->stride + (size_t)px;
            for (int py = a; py < b; py++, v += dv, dst += c->stride) {
                uint32_t pixel = darken(sample(im, u, v), bright);
                if ((pixel >> 24) == 255) *dst = pixel;
                else blend(dst, pixel, 256);
            }
        }
        for (int py = y0; py < y1; py++) {
            float fy = py + 0.5f;
            uint32_t pixel;
            float cover;
            if (fast && py >= fast0 && py < fast1) { py = fast1 - 1; continue; }
            if (fy < bottom + 0.5f) {
                float ly = fy - top;
                if (ly < 0) ly = 0;
                cover = clampf(fminf(fy + 0.5f - top, bottom - (fy - 0.5f)), 0, 1) * edge;
                if (radius > 0) cover *= corner_cover(lx, ly, w, h, radius);
                if (cover <= 0) continue;
                pixel = darken(sample(im, u, (int32_t)(ly * v_step * 65536.0f) - 32768), bright);
                blend(&c->px[(size_t)py * (size_t)c->stride + (size_t)px], pixel, (int)(cover * (float)alpha));
            } else {
                /* The reflection: mirrored, fading out. */
                float below = fy - bottom, fade = 1 - below / reflect;
                if (fade <= 0) break;
                float ly = h - below;
                if (ly < 0) break;
                cover = 0.24f * fade * fade * edge;
                if (radius > 0) cover *= corner_cover(lx, ly, w, h, radius);
                pixel = darken(sample(im, u, (int32_t)(ly * v_step * 65536.0f) - 32768), bright);
                blend(&c->px[(size_t)py * (size_t)c->stride + (size_t)px], pixel, (int)(cover * (float)alpha));
            }
        }
    }
}

/* pixel lightened towards white by amount (0-256). */
static inline uint32_t lighten(uint32_t pixel, int amount)
{
    if (amount <= 0) return pixel;
    uint32_t rb = pixel & 0xff00ffu, g = pixel & 0x00ff00u;
    rb += ((0xff00ffu - rb) * (uint32_t)amount >> 8) & 0xff00ffu;
    g += ((0x00ff00u - g) * (uint32_t)amount >> 8) & 0x00ff00u;
    return (pixel & 0xff000000u) | rb | g;
}

/* One pixel of a card where it is only partly covered: its top and bottom rows, and its
 * rounded corners. */
static inline void card_edge_pixel(Pc5Canvas *c, const Pc5Image *im, int px, int py, int32_t u, float top, float ground,
                                   float h, float w, float lx, float v_step, float edge, float radius, int bright,
                                   int alpha, float sheen_rows, int gloss)
{
    float fy = py + 0.5f, ly = fy - top;
    if (ly < 0) ly = 0;
    float cover = clampf(fminf(fy + 0.5f - top, ground - (fy - 0.5f)), 0, 1) * edge;
    if (radius > 0) cover *= corner_cover(lx, ly, w, h, radius);
    if (cover <= 0) return;
    uint32_t pixel = darken(sample(im, u, (int32_t)(ly * v_step * 65536.0f) - 32768), bright);
    if (ly < sheen_rows) {
        float s = 1 - ly / sheen_rows;
        pixel = lighten(pixel, (int)((float)gloss * s * s));
    }
    blend(&c->px[(size_t)py * (size_t)c->stride + (size_t)px], pixel, (int)(cover * (float)alpha));
}

/* Bilinear sample down a column, its colour times bright/256 (0-256): x, x1 and fx are the
 * column's, worked out once for it. */
static inline uint32_t sample_down(const Pc5Image *im, int x, int x1, uint32_t fx, int32_t v, uint32_t bright)
{
    int y = v >> 16;
    uint32_t fy = (uint32_t)(v >> 8) & 255;
    if (y < 0) { y = 0; fy = 0; }
    if (y > im->h - 1) y = im->h - 1;
    int y1 = y + 1 < im->h ? y + 1 : y;
    const uint32_t *r0 = im->px + (size_t)y * (size_t)im->w, *r1 = im->px + (size_t)y1 * (size_t)im->w;
    uint32_t a = r0[x], b = r0[x1], c = r1[x], d = r1[x1];
    uint32_t top_rb = ((a & 0xff00ffu) * (256 - fx) + (b & 0xff00ffu) * fx) >> 8 & 0xff00ffu;
    uint32_t top_ag = (((a >> 8) & 0xff00ffu) * (256 - fx) + ((b >> 8) & 0xff00ffu) * fx) >> 8 & 0xff00ffu;
    uint32_t bot_rb = ((c & 0xff00ffu) * (256 - fx) + (d & 0xff00ffu) * fx) >> 8 & 0xff00ffu;
    uint32_t bot_ag = (((c >> 8) & 0xff00ffu) * (256 - fx) + ((d >> 8) & 0xff00ffu) * fx) >> 8 & 0xff00ffu;
    uint32_t w0 = (256 - fy) * bright >> 8, w1 = fy * bright >> 8;
    uint32_t rb = (top_rb * w0 + bot_rb * w1) >> 8 & 0xff00ffu;
    uint32_t ag = (top_ag * w0 + bot_ag * w1) >> 8 & 0xff00ffu;
    return rb | ag << 8;
}

/* dst towards src: src times w_src and dst times w_dst (each 0-256), added. */
static inline void blend_weighted(uint32_t *dst, uint32_t src, uint32_t w_src, uint32_t w_dst)
{
    uint32_t d = *dst;
    uint32_t rb = ((src & 0xff00ffu) * w_src + (d & 0xff00ffu) * w_dst) >> 8 & 0xff00ffu;
    uint32_t g = ((src & 0x00ff00u) * w_src + (d & 0x00ff00u) * w_dst) >> 8 & 0x00ff00u;
    *dst = 0xff000000u | rb | g;
}

/* Linear sample down a column of the picture's nearest column (for the soft reflection). */
static inline uint32_t sample_line(const Pc5Image *im, int x, int32_t v)
{
    int y = v >> 16;
    uint32_t fy = (uint32_t)(v >> 8) & 255;
    if (y < 0) { y = 0; fy = 0; }
    if (y > im->h - 1) y = im->h - 1;
    int y1 = y + 1 < im->h ? y + 1 : y;
    uint32_t a = im->px[(size_t)y * (size_t)im->w + (size_t)x], c = im->px[(size_t)y1 * (size_t)im->w + (size_t)x];
    uint32_t rb = ((a & 0xff00ffu) * (256 - fy) + (c & 0xff00ffu) * fy) >> 8 & 0xff00ffu;
    uint32_t ag = (((a >> 8) & 0xff00ffu) * (256 - fy) + ((c >> 8) & 0xff00ffu) * fy) >> 8 & 0xff00ffu;
    return rb | ag << 8;
}

void pc5_draw_card(Pc5Canvas *c, const Pc5Picture *picture, float x, float ground, float w, float h_left, float h_right,
                   float radius, int bright, int alpha, int reflect, float fade, int gloss)
{
    const Pc5Image *im, *soft;
    int x0, x1, pic_end = (int)ceilf(ground);

    if (!picture->levels || w < 1 || alpha <= 0) return;
    im = pick_level(picture, h_left > h_right ? h_left : h_right);
    /* The reflection a level smaller: softer, as on a wet floor. */
    soft = pick_level(picture, (h_left > h_right ? h_left : h_right) * 0.5f);
    x0 = (int)floorf(x);
    x1 = (int)ceilf(x + w);
    if (x0 < first_column(c)) x0 = first_column(c);
    if (x1 > end_column(c)) x1 = end_column(c);
    for (int px = x0; px < x1; px++) {
        float t = (px + 0.5f - x) / w;
        if (t < 0 || t > 1) continue;
        float edge = clampf(fminf(px + 1 - x, x + w - px), 0, 1);   /* the outer columns, partly covered */
        float h = h_left + (h_right - h_left) * t, top = ground - h;
        int32_t u = (int32_t)(t * im->w * 65536.0f) - 32768;
        int ux = u >> 16, ux1;
        uint32_t ufx = (uint32_t)(u >> 8) & 255;
        if (ux < 0) { ux = 0; ufx = 0; }
        if (ux > im->w - 1) ux = im->w - 1;
        ux1 = ux + 1 < im->w ? ux + 1 : ux;
        int sx = (int)(t * soft->w);
        if (sx > soft->w - 1) sx = soft->w - 1;
        float v_step = im->h / h, sv_step = soft->h / h, lx = t * w;
        /* The sheen: lightest at the top left corner, gone by the middle. */
        float sheen_rows = gloss > 0 ? h * (0.5f - 0.6f * t) / 0.4f : 0;
        int corner = radius > 0 && (lx < radius || lx > w - radius);
        float r = corner ? radius : 0;
        int body0 = (int)ceilf(top + r), body1 = (int)floorf(ground - r), cover_all = (int)(edge * (float)alpha);
        if (body1 < body0) body1 = body0;
        /* the top rows and corners, a pixel at a time */
        for (int py = (int)floorf(top) < 0 ? 0 : (int)floorf(top); py < body0 && py < c->h; py++)
            card_edge_pixel(c, im, px, py, u, top, ground, h, w, lx, v_step, edge, radius, bright, alpha, sheen_rows, gloss);
        /* the body: rows wholly inside; the sheen first, then the rest straight */
        {
            int a = body0 < 0 ? 0 : body0, b = body1 > c->h ? c->h : body1;
            if (a < b) {
                float ly = (float)a + 0.5f - top;
                int32_t v = (int32_t)(ly * v_step * 65536.0f) - 32768, dv = (int32_t)(v_step * 65536.0f);
                uint32_t *dst = c->px + (size_t)a * (size_t)c->stride + (size_t)px;
                int py = a, sheen_end = sheen_rows > 0 ? (int)ceilf(top + sheen_rows) : a;
                if (sheen_end > b) sheen_end = b;
                uint32_t lit = bright >= 256 ? 256 : (uint32_t)bright;
                for (; py < sheen_end; py++, v += dv, ly += 1, dst += c->stride) {
                    float s = 1 - ly / sheen_rows;
                    uint32_t pixel = lighten(sample_down(im, ux, ux1, ufx, v, lit), (int)((float)gloss * s * s));
                    if (cover_all >= 256) *dst = pixel | 0xff000000u;
                    else blend(dst, pixel | 0xff000000u, cover_all);
                }
                if (cover_all >= 256)
                    for (; py < b; py++, v += dv, dst += c->stride) *dst = sample_down(im, ux, ux1, ufx, v, lit) | 0xff000000u;
                else
                    for (; py < b; py++, v += dv, dst += c->stride)
                        blend_weighted(dst, sample_down(im, ux, ux1, ufx, v, lit), (uint32_t)cover_all, 256 - (uint32_t)cover_all);
            }
        }
        /* the bottom rows and corners */
        for (int py = body1 < 0 ? 0 : body1; py < pic_end && py < c->h; py++)
            card_edge_pixel(c, im, px, py, u, top, ground, h, w, lx, v_step, edge, radius, bright, alpha, sheen_rows, gloss);
        /* The reflection: mirrored on the floor, fading out over fade of its height, until it can't be seen. */
        if (reflect > 0 && fade > 0) {
            float span = h * fade, strength = (float)reflect / 256 * edge * (float)alpha;
            int py = pic_end < 0 ? 0 : pic_end;
            float below = (float)py + 0.5f - ground;
            int32_t sv = (int32_t)((h - below) * sv_step * 65536.0f) - 32768, dsv = (int32_t)(sv_step * 65536.0f);
            /* the strength falls by the same step each row: in 1/256ths, fixed point */
            int32_t cover = (int32_t)(strength * (1 - below / span) * 256), dcover = (int32_t)(strength / span * 256);
            uint32_t *dst = c->px + (size_t)py * (size_t)c->stride + (size_t)px;
            int corner_rows = corner ? (int)ceilf(radius) : 0;
            uint32_t lit = bright >= 256 ? 256 : (uint32_t)bright;
            for (int n = 0; py < c->h && cover >= 8 * 256 && below < h; py++, n++, below += 1, sv -= dsv, cover -= dcover,
                     dst += c->stride) {
                uint32_t amount = (uint32_t)(cover >> 8);
                if (n < corner_rows) amount = (uint32_t)((float)amount * corner_cover(lx, h - below, w, h, radius));
                if (amount > 256) amount = 256;
                blend_weighted(dst, sample_line(soft, sx, sv), amount * lit >> 8, 256 - amount);
            }
        }
    }
}

void pc5_draw_image(Pc5Canvas *c, const Pc5Image *image, float x, float y, float w, float h, int alpha)
{
    int x0 = (int)floorf(x), y0 = (int)floorf(y), x1 = (int)ceilf(x + w), y1 = (int)ceilf(y + h);
    float us = image->w / w, vs = image->h / h;

    if (!image->px || w < 1 || h < 1) return;
    if (x0 < first_column(c)) x0 = first_column(c);
    if (y0 < 0) y0 = 0;
    if (x1 > end_column(c)) x1 = end_column(c);
    if (y1 > c->h) y1 = c->h;
    for (int py = y0; py < y1; py++) {
        int32_t v = (int32_t)((py + 0.5f - y) * vs * 65536.0f) - 32768;
        for (int px = x0; px < x1; px++) {
            int32_t u = (int32_t)((px + 0.5f - x) * us * 65536.0f) - 32768;
            blend(&c->px[(size_t)py * (size_t)c->stride + (size_t)px], sample(image, u, v), alpha);
        }
    }
}

/* ---- text ------------------------------------------------------------------------------- */

static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* A font file's faces: an extra one (a language's letters) may have faces without glyphs. */
static int fonts_read(Pc5Fonts *fonts, const char *path)
{
    const uint8_t *d;
    uint32_t faces, glyph_total = 0;
    size_t at;

    memset(fonts, 0, sizeof(*fonts));
    if (pc5_read_file(path, &fonts->file, 32u * 1024 * 1024) || fonts->file.len < 20) goto bad;
    d = fonts->file.data;
    if (memcmp(d, "PC5F", 4) || get32(d + 4) != 1) goto bad;
    faces = get32(d + 8);
    fonts->atlas_w = (int)get32(d + 12);
    fonts->atlas_h = (int)get32(d + 16);
    if (faces != PC5_FACES) goto bad;
    at = 20;
    for (uint32_t f = 0; f < faces; f++, at += 24) {
        if (at + 24 > fonts->file.len) goto bad;
        fonts->ascent[f] = (int)get32(d + at + 4);
        fonts->descent[f] = (int)get32(d + at + 8);
        fonts->line[f] = (int)get32(d + at + 12);
        fonts->count[f] = get32(d + at + 16);
        glyph_total += fonts->count[f];
    }
    /* Glyphs are 20 bytes each, laid out as Pc5Glyph. */
    if (sizeof(Pc5Glyph) != 20 || at + (size_t)glyph_total * 20 + (size_t)fonts->atlas_w * (size_t)fonts->atlas_h > fonts->file.len)
        goto bad;
    {
        size_t face_at = 20, first;
        for (uint32_t f = 0; f < faces; f++, face_at += 24) {
            first = get32(d + face_at + 20);
            if (first + fonts->count[f] > glyph_total) goto bad;
            fonts->glyphs[f] = fonts->count[f] ? (const Pc5Glyph *)(d + at + first * 20) : NULL;
        }
    }
    fonts->atlas = d + at + (size_t)glyph_total * 20;
    return 0;
bad:
    pc5_fonts_free(fonts);
    return -1;
}

int pc5_fonts_load(Pc5Fonts *fonts, const char *path)
{
    return fonts_read(fonts, path);
}

int pc5_fonts_extra(Pc5Fonts *fonts, const char *path)
{
    if (fonts->extra) {
        pc5_fonts_free(fonts->extra);
        pc5_free(fonts->extra);
        fonts->extra = NULL;
    }
    if (!path) return 0;
    Pc5Fonts *extra = pc5_alloc(sizeof(*extra));
    if (!extra) return -1;
    if (fonts_read(extra, path)) {
        pc5_free(extra);
        return -1;
    }
    fonts->extra = extra;
    return 0;
}

void pc5_fonts_free(Pc5Fonts *fonts)
{
    if (fonts->extra) {
        pc5_fonts_free(fonts->extra);
        pc5_free(fonts->extra);
    }
    pc5_buf_free(&fonts->file);
    memset(fonts, 0, sizeof(*fonts));
}

static uint32_t next_codepoint(const char **text)
{
    const unsigned char *p = (const unsigned char *)*text;
    uint32_t c = *p;
    int extra = c < 0x80 ? 0 : c < 0xe0 ? 1 : c < 0xf0 ? 2 : 3;

    if (c >= 0x80) c &= extra == 1 ? 0x1f : extra == 2 ? 0x0f : 0x07;
    p++;
    for (int k = 0; k < extra && (*p & 0xc0) == 0x80; k++) c = c << 6 | (*p++ & 0x3f);
    *text = (const char *)p;
    return c;
}

static const Pc5Glyph *find_glyph(const Pc5Fonts *fonts, Pc5Face face, uint32_t codepoint)
{
    const Pc5Glyph *g = fonts->glyphs[face];
    int lo = 0, hi = (int)fonts->count[face] - 1;

    if (!g) return NULL;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (g[mid].codepoint == codepoint) return &g[mid];
        if (g[mid].codepoint < codepoint) lo = mid + 1;
        else hi = mid - 1;
    }
    return NULL;
}

/* A letter, from the fonts or their extra letters (*from: the fonts it is in). The game-title
 * faces (hero, card) have only fonts.bin's letters: others come from the title face. */
static const Pc5Glyph *glyph(const Pc5Fonts *fonts, Pc5Face face, uint32_t codepoint, const Pc5Fonts **from)
{
    Pc5Face near = face == PC5_FACE_HERO || face == PC5_FACE_CARD ? PC5_FACE_TITLE : face;
    const Pc5Glyph *g = find_glyph(fonts, face, codepoint);

    *from = fonts;
    if (g) return g;
    if (near != face && (g = find_glyph(fonts, near, codepoint))) return g;
    if (fonts->extra) {
        *from = fonts->extra;
        if ((g = find_glyph(fonts->extra, face, codepoint)) || (g = find_glyph(fonts->extra, near, codepoint))) return g;
        *from = fonts;
    }
    return codepoint != '?' ? find_glyph(fonts, face, '?') : NULL;
}

#include "pc5_bidi.h"

/* Width in 64ths of a pixel of an already visual run. */
static int raw_width_64(const Pc5Fonts *fonts, Pc5Face face, const char *text, size_t bytes)
{
    const char *end = text + bytes;
    int width = 0;

    while (text < end && *text) {
        const Pc5Fonts *from;
        const Pc5Glyph *g = glyph(fonts, face, next_codepoint(&text), &from);
        if (g) width += g->advance;
    }
    return width;
}

static int width_64(const Pc5Fonts *fonts, Pc5Face face, const char *text, size_t bytes)
{
    char visual[PC5_BIDI_BYTES];
    int length = pc5_bidi_visual(text, bytes, visual, sizeof(visual), NULL);
    return length >= 0 ? raw_width_64(fonts, face, visual, (size_t)length) : raw_width_64(fonts, face, text, bytes);
}

int pc5_text_width(const Pc5Fonts *fonts, Pc5Face face, const char *text)
{
    return (width_64(fonts, face, text, strlen(text)) + 63) / 64;
}

static void raw_draw_run(Pc5Canvas *c, const Pc5Fonts *fonts, Pc5Face face, int x, int y, uint32_t color, const char *text,
                     size_t bytes)
{
    const char *end = text + bytes;
    int pen = x * 64;

    while (text < end && *text) {
        const Pc5Fonts *from;
        const Pc5Glyph *g = glyph(fonts, face, next_codepoint(&text), &from);
        if (!g) continue;
        int gx = (pen + 32) / 64 + g->left, gy = y + g->top;
        for (int row = 0; row < g->h; row++) {
            int py = gy + row;
            if (py < 0 || py >= c->h) continue;
            const uint8_t *src = from->atlas + (size_t)(g->y + row) * (size_t)from->atlas_w + g->x;
            uint32_t *dst = c->px + (size_t)py * (size_t)c->stride;
            for (int col = 0; col < g->w; col++) {
                int px = gx + col;
                if (px < 0 || px >= c->w || !src[col]) continue;
                blend(&dst[px], color, src[col] + (src[col] >> 7));
            }
        }
        pen += g->advance;
    }
}

static void draw_run(Pc5Canvas *c, const Pc5Fonts *fonts, Pc5Face face, int x, int y, uint32_t color, const char *text, size_t bytes)
{
    char visual[PC5_BIDI_BYTES];
    int length = pc5_bidi_visual(text, bytes, visual, sizeof(visual), NULL);
    if (length >= 0) raw_draw_run(c, fonts, face, x, y, color, visual, (size_t)length);
    else raw_draw_run(c, fonts, face, x, y, color, text, bytes);
}

int pc5_text_cuts;

int pc5_text(Pc5Canvas *c, const Pc5Fonts *fonts, Pc5Face face, int x, int y, uint32_t color, const char *text,
             int max_width, int align)
{
    static const char ellipsis[] = "\xe2\x80\xa6";
    size_t bytes = strlen(text);
    int width = (width_64(fonts, face, text, bytes) + 63) / 64, cut = 0;
    char visual[PC5_BIDI_BYTES];
    int rtl = 0;
    (void)pc5_bidi_visual(text, bytes, visual, sizeof(visual), &rtl);

    if (!fonts->atlas) return 0;
    if (max_width > 0 && width > max_width) {
        int room = max_width * 64 - width_64(fonts, face, ellipsis, 3);
        while (bytes > 0) {
            do bytes--; while (bytes > 0 && ((unsigned char)text[bytes] & 0xc0) == 0x80);
            if (width_64(fonts, face, text, bytes) <= room) break;
        }
        while (bytes > 0 && text[bytes - 1] == ' ') bytes--;
        width = (width_64(fonts, face, text, bytes) + width_64(fonts, face, ellipsis, 3) + 63) / 64;
        cut = 1;
        pc5_text_cuts++;
    }
    if (align == 1) x -= width / 2;
    else if (align == 2) x -= width;
    else if (align == 0 && rtl && max_width > 0) x += max_width - width;
    int prefix = cut && rtl ? (width_64(fonts, face, ellipsis, 3) + 63) / 64 : 0;
    draw_run(c, fonts, face, x + prefix, y, color, text, bytes);
    if (cut) draw_run(c, fonts, face, rtl ? x : x + (width_64(fonts, face, text, bytes) + 32) / 64, y, color, ellipsis, 3);
    return width;
}

/* Where a line may end in Chinese and Japanese, which have no spaces: between any two of
 * their characters, but not before the punctuation that closes (nor small kana and the
 * long-sound mark), nor after what opens. Korean has spaces. */
static int cjk(uint32_t c)
{
    return (c >= 0x2e80 && c <= 0x9fff) || (c >= 0xf900 && c <= 0xfaff) || (c >= 0xff00 && c <= 0xffef);
}

static int no_start(uint32_t c)
{
    static const uint32_t marks[] = { 0x3001, 0x3002, 0xff0c, 0xff0e, 0xff01, 0xff1f, 0xff1a, 0xff1b, 0xff09, 0x300d,
                                      0x300f, 0x3011, 0x3015, 0x3009, 0x300b, 0x30fc, 0x3041, 0x3043, 0x3045, 0x3047,
                                      0x3049, 0x3063, 0x3083, 0x3085, 0x3087, 0x30a1, 0x30a3, 0x30a5, 0x30a7, 0x30a9,
                                      0x30c3, 0x30e3, 0x30e5, 0x30e7, 0x30fb, 0x2026, ')', ']', '.', ',', '!', '?', ':', ';' };
    for (size_t i = 0; i < sizeof(marks) / sizeof(marks[0]); i++)
        if (c == marks[i]) return 1;
    return 0;
}

static int no_end(uint32_t c)
{
    return c == 0x300c || c == 0x300e || c == 0x3010 || c == 0x3014 || c == 0x3008 || c == 0x300a || c == 0xff08 ||
           c == '(' || c == '[';
}

/* The end of the word at text + at (not a space): the next space, or a place between two
 * characters where a line may end. At least one character. */
static size_t word_end(const char *text, size_t at, size_t length)
{
    const char *p = text + at, *end = text + length;
    uint32_t c = next_codepoint(&p);

    while (p < end && *p != ' ') {
        const char *q = p;
        uint32_t d = next_codepoint(&q);
        if ((cjk(c) || cjk(d)) && !no_start(d) && !no_end(c)) break;
        c = d;
        p = q;
    }
    return (size_t)(p > end ? length : (size_t)(p - text));
}

/* How much of text (its first length bytes) goes on a line of max_width: whole words while
 * they fit, at least one. */
static size_t line_fit(const Pc5Fonts *fonts, Pc5Face face, const char *text, size_t length, int max_width)
{
    size_t take = 0, fit = 0;
    while (take < length) {
        size_t next = take;
        while (next < length && text[next] == ' ') next++;
        if (next < length) next = word_end(text, next, length);
        if (width_64(fonts, face, text, next) > max_width * 64 && fit) break;
        fit = next;
        take = next;
        if (width_64(fonts, face, text, next) > max_width * 64) break;
    }
    return fit ? fit : length;
}

int pc5_text_wrap(Pc5Canvas *c, const Pc5Fonts *fonts, Pc5Face face, int x, int y, int max_width, int max_lines,
                  int line_height, uint32_t color, const char *text, int align)
{
    int lines = 0;

    while (*text && lines < max_lines) {
        size_t length = strcspn(text, "\n");
        while (*text == ' ') { text++; length--; }
        size_t fit = line_fit(fonts, face, text, length, max_width);
        char line[512];
        size_t n = fit < sizeof(line) - 1 ? fit : sizeof(line) - 1;
        if (lines + 1 == max_lines && fit < length) n = length < sizeof(line) - 1 ? length : sizeof(line) - 1;
        while (n && ((unsigned char)text[n] & 0xc0) == 0x80) --n;
        memcpy(line, text, n);
        line[n] = 0;
        int lx = align == 1 ? x + max_width / 2 : x;
        pc5_text(c, fonts, face, lx, y + lines * line_height, color, line, max_width, align);
        text += fit;
        if (*text == '\n') ++text;
        lines++;
    }
    return lines;
}

int pc5_text_lines(const Pc5Fonts *fonts, Pc5Face face, int max_width, const char *text)
{
    int lines = 0;

    if (!fonts->atlas) return *text ? 1 : 0;
    while (*text) {
        while (*text == ' ') text++;
        if (!*text) break;
        text += line_fit(fonts, face, text, strcspn(text, "\n"), max_width);
        if (*text == '\n') ++text;
        lines++;
    }
    return lines;
}
