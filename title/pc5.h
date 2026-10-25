/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: a launcher for PC games on the PS5, on top of prospero-win.
 *
 * This header holds what every part of the title shares: limits, memory,
 * byte buffers, images, files and SHA-256. Everything here is plain C11 and
 * POSIX, so the same code runs in the host tests and on the console. */
#ifndef PC5_H
#define PC5_H
#include <stddef.h>
#include <stdint.h>

#define PC5_VERSION "1.4.0"
#ifndef PC5_TITLE_ID
#define PC5_TITLE_ID "PPSA11018"
#endif

enum {
    PC5_PATH = 1024,     /* a path on the console */
    PC5_NAME = 96,       /* a game's name, as a profile allows it (95 bytes) */
    PC5_ID = 64,         /* a profile id (63 bytes) */
    PC5_MAX_GAMES = 128, /* the library (prospero-win holds as many, patch 0001) */
    PC5_ERROR = 256,
};

/* ---- memory ---------------------------------------------------------------
 * The title's heap is small (prospero-win maps its large buffers itself), so
 * blocks of 64 KiB and more get pages of their own. */
void *pc5_alloc(size_t size);            /* zeroed; NULL when out of memory */
void *pc5_realloc(void *block, size_t size);
void pc5_free(void *block);

/* ---- byte buffers ----------------------------------------------------------- */
typedef struct Pc5Buf {
    uint8_t *data;
    size_t len, cap;
} Pc5Buf;

int pc5_buf_reserve(Pc5Buf *buf, size_t extra);   /* 0, or -1 out of memory */
int pc5_buf_put(Pc5Buf *buf, const void *data, size_t size);
int pc5_buf_puts(Pc5Buf *buf, const char *text);
int pc5_buf_printf(Pc5Buf *buf, const char *format, ...) __attribute__((format(printf, 2, 3)));
/* Ends the text with a 0 byte that len does not count. */
int pc5_buf_terminate(Pc5Buf *buf);
void pc5_buf_free(Pc5Buf *buf);

/* ---- images ------------------------------------------------------------------
 * Pixels are 0xAARRGGBB, which is B, G, R, A in memory: the console's BGRX
 * frame. */
typedef struct Pc5Image {
    int w, h;
    uint32_t *px;
} Pc5Image;

int pc5_image_new(Pc5Image *image, int w, int h);  /* zeroed; 0 or -1 */
void pc5_image_free(Pc5Image *image);

/* ---- files ---------------------------------------------------------------------- */
int pc5_join(char *out, size_t size, const char *a, const char *b);   /* a/b; -1 if too long */
int pc5_is_dir(const char *path);
int pc5_is_file(const char *path);
int pc5_exists(const char *path);                 /* anything, links not followed */
int pc5_mkdirs(const char *path);                 /* mkdir -p */
/* The whole file, up to max bytes (more is an error); text gets a 0 byte. */
int pc5_read_file(const char *path, Pc5Buf *out, size_t max);
/* Written through path.part, flushed and renamed into place. */
int pc5_write_file(const char *path, const void *data, size_t size);
/* Free bytes on the file system that holds path; -1 if unknown. */
int pc5_free_bytes(const char *path, uint64_t *bytes);

/* ---- SHA-256 ---------------------------------------------------------------------- */
typedef struct Pc5Sha256 {
    uint32_t state[8];
    uint64_t bytes;
    uint8_t block[64];
    size_t used;
} Pc5Sha256;

void pc5_sha256_init(Pc5Sha256 *sha);
void pc5_sha256_update(Pc5Sha256 *sha, const void *data, size_t size);
void pc5_sha256_final(Pc5Sha256 *sha, uint8_t digest[32]);

/* ---- text ---------------------------------------------------------------------- */
/* text copied into out, cut to fit (always 0-terminated). */
void pc5_copy(char *out, size_t size, const char *text);
/* snprintf that reports truncation: 0, or -1 when out was too small. */
int pc5_format(char *out, size_t size, const char *format, ...) __attribute__((format(printf, 3, 4)));
/* "23.4 GB", "512 MB", "18 KB". */
void pc5_size_text(uint64_t bytes, char *out, size_t size);
/* ASCII lower case of one byte. */
static inline int pc5_lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
int pc5_casecmp(const char *a, const char *b);
int pc5_ends_with_ci(const char *text, const char *suffix);

#endif
