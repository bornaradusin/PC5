/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: memory, buffers, images, files, SHA-256 and text (pc5.h). */
#include "pc5.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __PROSPERO__
#include <sys/mount.h>
#else
#include <sys/statvfs.h>
#endif

/* ---- memory ------------------------------------------------------------------ */

enum { PC5_MAPPED_FROM = 64 * 1024, PC5_HEADER = 16 };

typedef struct Pc5Block {
    size_t size;      /* what was asked for */
    size_t mapped;    /* bytes mapped, 0 for the heap */
} Pc5Block;

int pc5_thread_start(pthread_t *thread, void *(*main)(void *), void *arg, int detached)
{
    pthread_attr_t attributes;
    pthread_t own;
    int rc = pthread_attr_init(&attributes);
    if (rc) return rc;
    pthread_attr_setstacksize(&attributes, PC5_THREAD_STACK);
    if (detached) pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    rc = pthread_create(thread ? thread : &own, &attributes, main, arg);
    pthread_attr_destroy(&attributes);
    return rc;
}

void *pc5_alloc(size_t size)
{
    Pc5Block *block;

    if (size > SIZE_MAX / 2) return NULL;
#ifndef PC5_HEAP_ONLY   /* the host tests keep everything on the heap, where the sanitizers see it */
    if (size + PC5_HEADER >= PC5_MAPPED_FROM) {
        size_t page = 16384, length = (size + PC5_HEADER + page - 1) / page * page;
        void *pages = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (pages == MAP_FAILED) return NULL;
        block = pages;
        block->mapped = length;
    } else
#endif
    {
        block = calloc(1, size + PC5_HEADER);
        if (!block) return NULL;
        block->mapped = 0;
    }
    block->size = size;
    return (uint8_t *)block + PC5_HEADER;
}

void pc5_free(void *pointer)
{
    Pc5Block *block;

    if (!pointer) return;
    block = (Pc5Block *)((uint8_t *)pointer - PC5_HEADER);
    if (block->mapped) munmap(block, block->mapped);
    else free(block);
}

void *pc5_realloc(void *pointer, size_t size)
{
    Pc5Block *block;
    void *grown;

    if (!pointer) return pc5_alloc(size);
    block = (Pc5Block *)((uint8_t *)pointer - PC5_HEADER);
    if (block->mapped && size + PC5_HEADER <= block->mapped) {
        block->size = size;
        return pointer;
    }
    if (!(grown = pc5_alloc(size))) return NULL;
    memcpy(grown, pointer, block->size < size ? block->size : size);
    pc5_free(pointer);
    return grown;
}

/* ---- buffers ------------------------------------------------------------------- */

int pc5_buf_reserve(Pc5Buf *buf, size_t extra)
{
    size_t wanted, cap;
    uint8_t *data;

    if (extra > SIZE_MAX / 4 - buf->len) return -1;
    wanted = buf->len + extra + 1;   /* room for a closing 0 byte */
    if (wanted <= buf->cap) return 0;
    cap = buf->cap ? buf->cap : 256;
    while (cap < wanted) cap *= 2;
    if (!(data = pc5_realloc(buf->data, cap))) return -1;
    buf->data = data;
    buf->cap = cap;
    return 0;
}

int pc5_buf_put(Pc5Buf *buf, const void *data, size_t size)
{
    if (pc5_buf_reserve(buf, size)) return -1;
    if (size) memcpy(buf->data + buf->len, data, size);
    buf->len += size;
    buf->data[buf->len] = 0;
    return 0;
}

int pc5_buf_puts(Pc5Buf *buf, const char *text)
{
    return pc5_buf_put(buf, text, strlen(text));
}

int pc5_buf_printf(Pc5Buf *buf, const char *format, ...)
{
    va_list args;
    int length;

    va_start(args, format);
    length = vsnprintf(NULL, 0, format, args);
    va_end(args);
    if (length < 0 || pc5_buf_reserve(buf, (size_t)length)) return -1;
    va_start(args, format);
    vsnprintf((char *)buf->data + buf->len, (size_t)length + 1, format, args);
    va_end(args);
    buf->len += (size_t)length;
    return 0;
}

int pc5_buf_terminate(Pc5Buf *buf)
{
    if (pc5_buf_reserve(buf, 0)) return -1;
    buf->data[buf->len] = 0;
    return 0;
}

void pc5_buf_free(Pc5Buf *buf)
{
    pc5_free(buf->data);
    *buf = (Pc5Buf){ 0 };
}

/* ---- images --------------------------------------------------------------------- */

int pc5_image_new(Pc5Image *image, int w, int h)
{
    *image = (Pc5Image){ 0 };
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384) return -1;
    if (!(image->px = pc5_alloc((size_t)w * (size_t)h * 4u))) return -1;
    image->w = w;
    image->h = h;
    return 0;
}

void pc5_image_free(Pc5Image *image)
{
    pc5_free(image->px);
    *image = (Pc5Image){ 0 };
}

/* ---- files ---------------------------------------------------------------------- */

int pc5_join(char *out, size_t size, const char *a, const char *b)
{
    int length = snprintf(out, size, "%s/%s", a, b);
    return length < 0 || (size_t)length >= size ? -1 : 0;
}

int pc5_is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int pc5_is_file(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int pc5_exists(const char *path)
{
    struct stat st;
    return lstat(path, &st) == 0;
}

static int make_dir(const char *path)
{
    return mkdir(path, 0777) == 0 || (errno == EEXIST && pc5_is_dir(path)) ? 0 : -1;
}

int pc5_mkdirs(const char *path)
{
    char folder[PC5_PATH];

    if (pc5_format(folder, sizeof(folder), "%s", path)) return -1;
    for (char *slash = strchr(folder + 1, '/'); slash; slash = strchr(slash + 1, '/')) {
        *slash = 0;
        if (make_dir(folder)) return -1;
        *slash = '/';
    }
    return make_dir(folder);
}

int pc5_read_file(const char *path, Pc5Buf *out, size_t max)
{
    struct stat st;
    int fd = open(path, O_RDONLY);
    ssize_t got;

    out->len = 0;
    if (fd < 0) return -1;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || (uint64_t)st.st_size > max ||
        pc5_buf_reserve(out, (size_t)st.st_size)) {
        close(fd);
        return -1;
    }
    while ((got = read(fd, out->data + out->len, out->cap - 1 - out->len)) > 0) {
        out->len += (size_t)got;
        if (out->len + 1 == out->cap) {
            if (out->len >= max || pc5_buf_reserve(out, 4096)) { got = -1; break; }
        }
    }
    close(fd);
    if (got < 0 || out->len > max) return -1;
    out->data[out->len] = 0;
    return 0;
}

int pc5_write_file(const char *path, const void *data, size_t size)
{
    char part[PC5_PATH + 8];
    const uint8_t *at = data;
    int fd, ok = 1;

    if (pc5_format(part, sizeof(part), "%s.part", path)) return -1;
    if ((fd = open(part, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0) return -1;
    for (size_t done = 0; ok && done < size;) {
        ssize_t put = write(fd, at + done, size - done);
        if (put <= 0) ok = 0;
        else done += (size_t)put;
    }
    if (ok && fsync(fd)) ok = 0;
    if (close(fd)) ok = 0;
    if (!ok || rename(part, path)) {
        int saved = errno;
        unlink(part);
        errno = saved;
        return -1;
    }
    return 0;
}

#ifdef __PROSPERO__
/* A title can't call statvfs: the console's libc answers it with statfs, which
 * only libkernel_sys exports, and a title's imports from libkernel_sys stay
 * empty, so the call jumps to nothing (prospero-win's docs/WINE_PS5_BUILD.md,
 * "Imports a title does not get"). libkernel itself exports the system call as
 * _fstatfs. Its answer goes into a buffer with room to spare, in case the
 * kernel's statfs is larger than the SDK's; the fields read come first in
 * every layout. */
int _fstatfs(int fd, struct statfs *buf);

int pc5_free_bytes(const char *path, uint64_t *bytes)
{
    union { struct statfs st; uint64_t room[512]; } answer;
    int fd = open(path, O_RDONLY | O_DIRECTORY);

    if (fd < 0) return -1;
    memset(&answer, 0, sizeof(answer));
    int status = _fstatfs(fd, &answer.st);
    close(fd);
    if (status || !answer.st.f_bsize || answer.st.f_bavail < 0) return -1;
    *bytes = (uint64_t)answer.st.f_bavail * answer.st.f_bsize;
    return 0;
}
#else
int pc5_free_bytes(const char *path, uint64_t *bytes)
{
    struct statvfs st;

    if (statvfs(path, &st)) return -1;
    *bytes = (uint64_t)st.f_bavail * (uint64_t)(st.f_frsize ? st.f_frsize : st.f_bsize);
    return 0;
}
#endif

/* ---- SHA-256 (FIPS 180-4) ------------------------------------------------------------ */

static const uint32_t sha_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static void sha_block(Pc5Sha256 *sha, const uint8_t *p)
{
    uint32_t w[64], a, b, c, d, e, f, g, h;

    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = sha->state[0]; b = sha->state[1]; c = sha->state[2]; d = sha->state[3];
    e = sha->state[4]; f = sha->state[5]; g = sha->state[6]; h = sha->state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + sha_k[i] + w[i];
        uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    sha->state[0] += a; sha->state[1] += b; sha->state[2] += c; sha->state[3] += d;
    sha->state[4] += e; sha->state[5] += f; sha->state[6] += g; sha->state[7] += h;
}

void pc5_sha256_init(Pc5Sha256 *sha)
{
    static const uint32_t start[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    memcpy(sha->state, start, sizeof(start));
    sha->bytes = 0;
    sha->used = 0;
}

void pc5_sha256_update(Pc5Sha256 *sha, const void *data, size_t size)
{
    const uint8_t *p = data;

    sha->bytes += size;
    if (sha->used) {
        size_t take = 64 - sha->used < size ? 64 - sha->used : size;
        memcpy(sha->block + sha->used, p, take);
        sha->used += take;
        p += take;
        size -= take;
        if (sha->used < 64) return;
        sha_block(sha, sha->block);
        sha->used = 0;
    }
    for (; size >= 64; p += 64, size -= 64) sha_block(sha, p);
    memcpy(sha->block, p, size);
    sha->used = size;
}

void pc5_sha256_final(Pc5Sha256 *sha, uint8_t digest[32])
{
    uint64_t bits = sha->bytes * 8;
    uint8_t one = 0x80, zero = 0, length[8];

    pc5_sha256_update(sha, &one, 1);
    while (sha->used != 56) pc5_sha256_update(sha, &zero, 1);
    for (int i = 0; i < 8; i++) length[i] = (uint8_t)(bits >> (56 - 8 * i));
    pc5_sha256_update(sha, length, 8);
    for (int i = 0; i < 8; i++) {
        digest[4 * i] = (uint8_t)(sha->state[i] >> 24);
        digest[4 * i + 1] = (uint8_t)(sha->state[i] >> 16);
        digest[4 * i + 2] = (uint8_t)(sha->state[i] >> 8);
        digest[4 * i + 3] = (uint8_t)sha->state[i];
    }
}

/* ---- text ----------------------------------------------------------------------- */

void pc5_copy(char *out, size_t size, const char *text)
{
    size_t n = 0;

    if (!size) return;
    while (text && text[n] && n + 1 < size) n++;
    /* Cut short: not in the middle of a character (translated text is often longer). */
    if (text && text[n])
        while (n > 0 && ((unsigned char)text[n] & 0xc0) == 0x80) n--;
    memmove(out, text ? text : "", n);
    out[n] = 0;
}

int pc5_format(char *out, size_t size, const char *format, ...)
{
    va_list args;
    int length;

    va_start(args, format);
    length = vsnprintf(out, size, format, args);
    va_end(args);
    return length < 0 || (size_t)length >= size ? -1 : 0;
}

void pc5_size_text(uint64_t bytes, char *out, size_t size)
{
    static const char *const units[] = { "KB", "MB", "GB", "TB" };
    double value = (double)bytes / 1024.0;
    int unit = 0;

    while (value >= 1000.0 && unit < 3) {
        value /= 1024.0;
        unit++;
    }
    if (value < 10.0 && unit >= 2) snprintf(out, size, "%.1f %s", value, units[unit]);
    else if (value < 100.0 && unit >= 2) snprintf(out, size, "%.1f %s", value, units[unit]);
    else snprintf(out, size, "%.0f %s", value < 1.0 ? 1.0 : value, units[unit]);
}

int pc5_casecmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        int x = pc5_lower((unsigned char)*a), y = pc5_lower((unsigned char)*b);
        if (x != y || !x) return x - y;
    }
}

int pc5_ends_with_ci(const char *text, const char *suffix)
{
    size_t a = strlen(text), b = strlen(suffix);
    return a >= b && !pc5_casecmp(text + a - b, suffix);
}

size_t pc5_utf16_from_utf8(uint16_t *out, size_t size, const char *text)
{
    const unsigned char *at = (const unsigned char *)text;
    size_t n = 0;

    if (!size) return 0;
    while (*at && n + 1 < size) {
        uint32_t cp = *at, need = cp < 0x80 ? 0 : (cp & 0xe0) == 0xc0 ? 1 : (cp & 0xf0) == 0xe0 ? 2 : (cp & 0xf8) == 0xf0 ? 3 : 4;
        size_t used = 1;
        if (need == 4) cp = 0xfffd;
        else if (need) {
            cp &= 0x3fu >> need;
            for (uint32_t i = 0; i < need; i++, used++) {
                if ((at[used] & 0xc0) != 0x80) { cp = 0xfffd; break; }
                cp = cp << 6 | (at[used] & 0x3fu);
            }
            /* overlong forms, surrogates and what is past Unicode are not characters */
            if (cp != 0xfffd && ((need == 1 && cp < 0x80) || (need == 2 && cp < 0x800) || (need == 3 && cp < 0x10000) ||
                                 (cp >= 0xd800 && cp < 0xe000) || cp > 0x10ffff))
                cp = 0xfffd;
        }
        at += used;
        if (cp >= 0x10000) {
            if (n + 2 >= size) break;
            out[n++] = (uint16_t)(0xd800 + ((cp - 0x10000) >> 10));
            out[n++] = (uint16_t)(0xdc00 + ((cp - 0x10000) & 0x3ff));
        } else {
            out[n++] = (uint16_t)cp;
        }
    }
    out[n] = 0;
    return n;
}

void pc5_utf8_from_utf16(char *out, size_t size, const uint16_t *text)
{
    size_t n = 0;

    if (!size) return;
    for (size_t i = 0; text[i]; i++) {
        uint32_t cp = text[i];
        if (cp >= 0xd800 && cp < 0xdc00 && text[i + 1] >= 0xdc00 && text[i + 1] < 0xe000)
            cp = 0x10000 + ((cp - 0xd800) << 10) + (text[++i] - 0xdc00u);
        else if (cp >= 0xd800 && cp < 0xe000)
            cp = 0xfffd;
        size_t length = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
        if (n + length >= size) break;
        if (length == 1) out[n++] = (char)cp;
        else {
            out[n++] = (char)(length == 2 ? 0xc0 | cp >> 6 : length == 3 ? 0xe0 | cp >> 12 : 0xf0 | cp >> 18);
            for (size_t k = length - 1; k > 0; k--) out[n++] = (char)(0x80 | ((cp >> (6 * (k - 1))) & 0x3f));
        }
    }
    out[n] = 0;
}
