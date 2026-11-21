/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: a cabinet's files (pc5_cab.h). The cabinet's layout is Microsoft's "Cabinet File
 * Format"; MSZIP is deflate (RFC 1951) in blocks that each start with "CK" and see the 32 KiB
 * before them; LZX is as Microsoft's "LZX DELTA Compression and Decompression" describes it for
 * cabinets (a folder's blocks are one stream, its output 32 KiB frames). Every offset and size
 * read from the cabinet is checked before it is used. */
#include "pc5_cab.h"

#include <stdlib.h>
#include <string.h>

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* ---- Huffman codes ------------------------------------------------------------------------
 * Canonical codes from their lengths, decoded a bit at a time (code lengths up to 16). */
enum { HUFF_BITS = 16 };
typedef struct Huff {
    uint16_t count[HUFF_BITS + 1];
    uint16_t *symbol;
} Huff;

/* 0 for a complete code, 1 for an incomplete one (or none), -1 for an impossible one. */
static int huff_build(Huff *h, const uint8_t *length, int n)
{
    uint16_t offs[HUFF_BITS + 1];
    int left = 1;

    memset(h->count, 0, sizeof(h->count));
    for (int i = 0; i < n; i++) {
        if (length[i] > HUFF_BITS) return -1;
        h->count[length[i]]++;
    }
    if (h->count[0] == n) return 1;
    for (int len = 1; len <= HUFF_BITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return -1;
    }
    offs[1] = 0;
    for (int len = 1; len < HUFF_BITS; len++) offs[len + 1] = (uint16_t)(offs[len] + h->count[len]);
    for (int i = 0; i < n; i++)
        if (length[i]) h->symbol[offs[length[i]]++] = (uint16_t)i;
    return left > 0;
}

/* ---- deflate ------------------------------------------------------------------------------ */
typedef struct Inflate {
    const uint8_t *in;
    size_t size, pos;
    uint32_t bitbuf;
    int bitcnt, failed;
    uint8_t *out;          /* the dictionary, then what is made */
    size_t at, out_size;
} Inflate;

static int in_bits(Inflate *s, int need)
{
    uint32_t val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->pos >= s->size) {
            s->failed = 1;
            return 0;
        }
        val |= (uint32_t)s->in[s->pos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1u << need) - 1));
}

static int in_decode(Inflate *s, const Huff *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; len++) {
        code |= in_bits(s, 1);
        if (s->failed) return -1;
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static const uint16_t len_base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                       35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t len_extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t dist_base[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const uint8_t dist_extra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static int in_codes(Inflate *s, const Huff *lens, const Huff *dists)
{
    for (;;) {
        int sym = in_decode(s, lens);
        if (sym < 0) return -1;
        if (sym < 256) {
            if (s->at >= s->out_size) return -1;
            s->out[s->at++] = (uint8_t)sym;
        } else if (sym == 256) {
            return 0;
        } else {
            sym -= 257;
            if (sym >= 29) return -1;
            size_t len = len_base[sym] + (size_t)in_bits(s, len_extra[sym]);
            int d = in_decode(s, dists);
            if (d < 0 || d >= 30) return -1;
            size_t dist = dist_base[d] + (size_t)in_bits(s, dist_extra[d]);
            if (s->failed || dist > s->at || len > s->out_size - s->at) return -1;
            for (; len; len--, s->at++) s->out[s->at] = s->out[s->at - dist];
        }
    }
}

static int in_fixed(Inflate *s)
{
    static uint16_t lsym[288], dsym[30];
    uint8_t lengths[288];
    Huff lens = { { 0 }, lsym }, dists = { { 0 }, dsym };
    for (int i = 0; i < 144; i++) lengths[i] = 8;
    for (int i = 144; i < 256; i++) lengths[i] = 9;
    for (int i = 256; i < 280; i++) lengths[i] = 7;
    for (int i = 280; i < 288; i++) lengths[i] = 8;
    huff_build(&lens, lengths, 288);
    for (int i = 0; i < 30; i++) lengths[i] = 5;
    huff_build(&dists, lengths, 30);
    return in_codes(s, &lens, &dists);
}

static int in_dynamic(Inflate *s)
{
    static const uint8_t order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    uint16_t lsym[288], dsym[30];
    uint8_t lengths[288 + 30];
    Huff lens = { { 0 }, lsym }, dists = { { 0 }, dsym };
    int nlen = in_bits(s, 5) + 257, ndist = in_bits(s, 5) + 1, ncode = in_bits(s, 4) + 4, index;

    if (s->failed || nlen > 286 || ndist > 30) return -1;
    memset(lengths, 0, sizeof(lengths));
    for (index = 0; index < ncode; index++) lengths[order[index]] = (uint8_t)in_bits(s, 3);
    if (huff_build(&lens, lengths, 19) != 0) return -1;
    for (index = 0; index < nlen + ndist;) {
        int sym = in_decode(s, &lens), len = 0, repeat;
        if (sym < 0) return -1;
        if (sym < 16) {
            lengths[index++] = (uint8_t)sym;
            continue;
        }
        if (sym == 16) {
            if (!index) return -1;
            len = lengths[index - 1];
            repeat = 3 + in_bits(s, 2);
        } else if (sym == 17) {
            repeat = 3 + in_bits(s, 3);
        } else {
            repeat = 11 + in_bits(s, 7);
        }
        if (s->failed || index + repeat > nlen + ndist) return -1;
        while (repeat--) lengths[index++] = (uint8_t)len;
    }
    if (!lengths[256]) return -1;
    int err = huff_build(&lens, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lens.count[0] != 1)) return -1;
    err = huff_build(&dists, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - dists.count[0] != 1)) return -1;
    return in_codes(s, &lens, &dists);
}

/* Raw deflate into s->out from s->at on: 0, or -1. */
static int inflate_run(Inflate *s)
{
    int last;
    do {
        last = in_bits(s, 1);
        int type = in_bits(s, 2);
        if (s->failed) return -1;
        if (type == 0) {
            s->bitbuf = 0;
            s->bitcnt = 0;
            if (s->pos + 4 > s->size) return -1;
            unsigned len = u16(s->in + s->pos), nlen = u16(s->in + s->pos + 2);
            s->pos += 4;
            if ((len ^ 0xffff) != nlen || len > s->size - s->pos || len > s->out_size - s->at) return -1;
            memcpy(s->out + s->at, s->in + s->pos, len);
            s->pos += len;
            s->at += len;
        } else if (type == 1) {
            if (in_fixed(s)) return -1;
        } else if (type == 2) {
            if (in_dynamic(s)) return -1;
        } else {
            return -1;
        }
    } while (!last);
    return 0;
}

long pc5_inflate(const uint8_t *in, size_t size, const uint8_t *dictionary, size_t dictionary_size, uint8_t *out,
                 size_t out_size)
{
    uint8_t *work = pc5_alloc(dictionary_size + out_size + 1);
    Inflate s = { in, size, 0, 0, 0, 0, work, dictionary_size, dictionary_size + out_size };
    long made = -1;
    if (!work) return -1;
    if (dictionary_size) memcpy(work, dictionary, dictionary_size);
    if (!inflate_run(&s)) {
        made = (long)(s.at - dictionary_size);
        memcpy(out, work + dictionary_size, (size_t)made);
    }
    pc5_free(work);
    return made;
}

/* ---- LZX ---------------------------------------------------------------------------------- */
enum { LZX_FRAME = 32768, LZX_MAIN_MAX = 256 + 50 * 8, LZX_LENGTHS = 249, LZX_VERBATIM = 1, LZX_ALIGNED = 2,
       LZX_UNCOMPRESSED = 3 };

typedef struct Lzx {
    const uint8_t *in;
    size_t size, pos;
    uint32_t bitbuf;
    int bitcnt, past_end;
    uint8_t *window;
    uint32_t wsize, wpos, fpos;
    uint32_t r0, r1, r2;
    int slots, header_read, intel_started;
    int32_t intel_filesize;
    uint32_t intel_curpos, frames;
    int block_type;
    uint32_t block_length, block_remaining;
    uint8_t main_len[LZX_MAIN_MAX], length_len[LZX_LENGTHS], aligned_len[8];
    uint16_t main_sym[LZX_MAIN_MAX], length_sym[LZX_LENGTHS], aligned_sym[8];
    Huff main, length, aligned;
    uint32_t extra[51], base[51];
} Lzx;

/* A bit, the stream read as 16-bit little-endian words, each from its top bit down; past
 * the end, zeros (the last block may read a little ahead of its data). */
static int lz_bit(Lzx *z)
{
    if (!z->bitcnt) {
        if (z->pos + 2 <= z->size) {
            z->bitbuf = u16(z->in + z->pos);
        } else {
            z->bitbuf = 0;
            z->past_end++;
        }
        z->pos += 2;
        z->bitcnt = 16;
    }
    z->bitcnt--;
    return (int)(z->bitbuf >> z->bitcnt) & 1;
}

static uint32_t lz_bits(Lzx *z, int n)
{
    uint32_t v = 0;
    while (n--) v = v << 1 | (uint32_t)lz_bit(z);
    return v;
}

static int lz_decode(Lzx *z, const Huff *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= HUFF_BITS; len++) {
        code |= lz_bit(z);
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

/* A tree's lengths first..last, each the change from the last block's, through a pretree. */
static int lz_lengths(Lzx *z, uint8_t *lens, int first, int last)
{
    uint8_t pre_len[20];
    uint16_t pre_sym[20];
    Huff pre = { { 0 }, pre_sym };

    for (int i = 0; i < 20; i++) pre_len[i] = (uint8_t)lz_bits(z, 4);
    if (huff_build(&pre, pre_len, 20) < 0) return -1;
    for (int x = first; x < last;) {
        int sym = lz_decode(z, &pre), run, value;
        if (sym < 0) return -1;
        if (sym == 17 || sym == 18) {
            run = sym == 17 ? (int)lz_bits(z, 4) + 4 : (int)lz_bits(z, 5) + 20;
            if (x + run > last) return -1;
            while (run--) lens[x++] = 0;
        } else if (sym == 19) {
            run = (int)lz_bits(z, 1) + 4;
            sym = lz_decode(z, &pre);
            if (sym < 0 || sym > 16 || x + run > last) return -1;
            value = lens[x] - sym;
            if (value < 0) value += 17;
            while (run--) lens[x++] = (uint8_t)value;
        } else {
            value = lens[x] - sym;
            if (value < 0) value += 17;
            lens[x++] = (uint8_t)value;
        }
    }
    return 0;
}

static int lzx_init(Lzx *z, int window_bits, const uint8_t *in, size_t size)
{
    static const int slots[22] = { [15] = 30, [16] = 32, [17] = 34, [18] = 36, [19] = 38, [20] = 42, [21] = 50 };
    if (window_bits < 15 || window_bits > 21) return -1;
    memset(z, 0, sizeof(*z));
    z->wsize = 1u << window_bits;
    if (!(z->window = pc5_alloc(z->wsize))) return -1;
    z->in = in;
    z->size = size;
    z->slots = slots[window_bits];
    z->r0 = z->r1 = z->r2 = 1;
    z->main.symbol = z->main_sym;
    z->length.symbol = z->length_sym;
    z->aligned.symbol = z->aligned_sym;
    for (int i = 0, j = 0; i < 51; i += 2) {
        z->extra[i] = (uint32_t)j;
        if (i + 1 < 51) z->extra[i + 1] = (uint32_t)j;
        if (i != 0 && j < 17) j++;
    }
    for (int i = 0, j = 0; i < 51; i++) {
        z->base[i] = (uint32_t)j;
        j += 1 << z->extra[i];
    }
    return 0;
}

/* Up to want bytes of the current block into the window (a match may run past them): the
 * bytes made, or -1. */
static int64_t lzx_run(Lzx *z, uint32_t want)
{
    uint32_t made = 0;
    if (z->block_type == LZX_UNCOMPRESSED) {
        if (z->pos > z->size || want > z->size - z->pos) return -1;
        memcpy(z->window + z->wpos, z->in + z->pos, want);
        z->pos += want;
        z->wpos += want;
        return want;
    }
    while (made < want) {
        int sym = lz_decode(z, &z->main);
        if (sym < 0) return -1;
        if (sym < 256) {
            z->window[z->wpos++] = (uint8_t)sym;
            made++;
            continue;
        }
        sym -= 256;
        uint32_t len = (uint32_t)sym & 7, slot = (uint32_t)sym >> 3, off;
        if (len == 7) {
            int footer = lz_decode(z, &z->length);
            if (footer < 0) return -1;
            len += (uint32_t)footer;
        }
        len += 2;
        if (slot > 2) {
            uint32_t extra = z->extra[slot];
            off = z->base[slot] - 2;
            if (z->block_type == LZX_VERBATIM) {
                off += lz_bits(z, (int)extra);
            } else if (extra > 3) {
                off += lz_bits(z, (int)extra - 3) << 3;
                int aligned = lz_decode(z, &z->aligned);
                if (aligned < 0) return -1;
                off += (uint32_t)aligned;
            } else if (extra == 3) {
                int aligned = lz_decode(z, &z->aligned);
                if (aligned < 0) return -1;
                off += (uint32_t)aligned;
            } else if (extra > 0) {
                off += lz_bits(z, (int)extra);
            } else {
                off = 1;
            }
            z->r2 = z->r1;
            z->r1 = z->r0;
            z->r0 = off;
        } else if (slot == 0) {
            off = z->r0;
        } else if (slot == 1) {
            off = z->r1;
            z->r1 = z->r0;
            z->r0 = off;
        } else {
            off = z->r2;
            z->r2 = z->r0;
            z->r0 = off;
        }
        if (!off || off > z->wsize || len > z->wsize - z->wpos) return -1;
        uint32_t from = z->wpos >= off ? z->wpos - off : z->wpos + z->wsize - off;
        for (uint32_t i = 0; i < len; i++) {
            z->window[z->wpos++] = z->window[from++];
            if (from == z->wsize) from = 0;
        }
        made += len;
    }
    return made;
}

static int lzx_block_header(Lzx *z)
{
    if (z->block_type == LZX_UNCOMPRESSED && (z->block_length & 1)) z->pos++;   /* its padding byte */
    z->block_type = (int)lz_bits(z, 3);
    uint32_t high = lz_bits(z, 16), low = lz_bits(z, 8);
    z->block_length = z->block_remaining = high << 8 | low;
    switch (z->block_type) {
    case LZX_ALIGNED:
        for (int i = 0; i < 8; i++) z->aligned_len[i] = (uint8_t)lz_bits(z, 3);
        if (huff_build(&z->aligned, z->aligned_len, 8) < 0) return -1;
        /* the rest as a verbatim block's */
        __attribute__((fallthrough));
    case LZX_VERBATIM:
        if (lz_lengths(z, z->main_len, 0, 256) || lz_lengths(z, z->main_len, 256, 256 + z->slots * 8) ||
            huff_build(&z->main, z->main_len, 256 + z->slots * 8) < 0)
            return -1;
        if (z->main_len[0xe8]) z->intel_started = 1;
        if (lz_lengths(z, z->length_len, 0, LZX_LENGTHS) || huff_build(&z->length, z->length_len, LZX_LENGTHS) < 0)
            return -1;
        return 0;
    case LZX_UNCOMPRESSED:
        z->intel_started = 1;
        /* 1 to 16 bits to the next 16-bit boundary, then R0, R1 and R2. */
        if (!z->bitcnt) z->pos += 2;
        z->bitcnt = 0;
        if (z->pos > z->size || z->size - z->pos < 12) return -1;
        z->r0 = u32(z->in + z->pos);
        z->r1 = u32(z->in + z->pos + 4);
        z->r2 = u32(z->in + z->pos + 8);
        z->pos += 12;
        return 0;
    default:
        return -1;
    }
}

/* The next frame (frame_size bytes, 32 KiB but for the last) into out. 0, or -1. */
static int lzx_frame(Lzx *z, uint32_t frame_size, uint8_t *out)
{
    if (!frame_size || frame_size > LZX_FRAME || frame_size > z->wsize - z->fpos) return -1;
    if (!z->header_read) {
        if (lz_bit(z)) {
            uint32_t high = lz_bits(z, 16), low = lz_bits(z, 16);
            z->intel_filesize = (int32_t)(high << 16 | low);
        }
        z->header_read = 1;
    }
    uint32_t end = z->fpos + frame_size;
    while (z->wpos < end) {
        if (!z->block_remaining && lzx_block_header(z)) return -1;
        uint32_t want = end - z->wpos;
        if (want > z->block_remaining) want = z->block_remaining;
        int64_t made = lzx_run(z, want);
        if (made < 0 || (uint64_t)made > z->block_remaining) return -1;
        z->block_remaining -= (uint32_t)made;
    }
    if (z->past_end > 8) return -1;
    z->bitcnt = 0;   /* the next frame starts on a 16-bit boundary */
    memcpy(out, z->window + z->fpos, frame_size);
    /* Intel's E8 calls back from absolute to relative addresses. */
    if (z->intel_started && z->intel_filesize && z->frames < 32768 && frame_size > 10) {
        int32_t curpos = (int32_t)z->intel_curpos;
        for (uint32_t i = 0; i < frame_size - 10; i++, curpos++) {
            if (out[i] != 0xe8) continue;
            int32_t abs_off = (int32_t)u32(out + i + 1);
            if (abs_off >= -curpos && abs_off < z->intel_filesize) {
                int32_t rel = abs_off >= 0 ? abs_off - curpos : abs_off + z->intel_filesize;
                out[i + 1] = (uint8_t)rel;
                out[i + 2] = (uint8_t)(rel >> 8);
                out[i + 3] = (uint8_t)(rel >> 16);
                out[i + 4] = (uint8_t)(rel >> 24);
            }
            i += 4;
            curpos += 4;
        }
    }
    if (z->intel_filesize) z->intel_curpos += frame_size;
    z->frames++;
    z->fpos += frame_size;
    if (z->fpos == z->wsize) z->fpos = 0;
    if (z->wpos == z->wsize) z->wpos = 0;
    return 0;
}

/* ---- the cabinet ---------------------------------------------------------------------- */
typedef struct CabFile {
    uint32_t size, offset;
    uint16_t folder;
    uint8_t handed;   /* its folder's bytes have been read for it */
    char name[256];
} CabFile;

typedef struct CabFolder {
    uint32_t data_at;
    uint16_t blocks, type;
} CabFolder;

/* What the folder's bytes are handed to: the folder's files in order of where they start. */
typedef struct Dispatch {
    const Pc5CabOut *out;
    CabFile **files;
    int count, at, started, taking, taken;
    uint64_t pos, left;
} Dispatch;

static int dispatch(Dispatch *d, const uint8_t *data, size_t n)
{
    for (;;) {
        while (!d->started && d->at < d->count && d->files[d->at]->offset <= d->pos) {
            int take = d->out->begin(d->out->context, d->files[d->at]->name, d->files[d->at]->size);
            if (take < 0) return -1;
            d->taking = take > 0;
            d->started = 1;
            d->left = d->files[d->at]->size;
            if (!d->left) {
                if (d->taking && d->out->end(d->out->context)) return -1;
                d->taken += d->taking;
                d->at++;
                d->started = 0;
            }
        }
        if (!n) return 0;
        if (!d->started) {   /* bytes no file holds */
            uint64_t skip = d->at < d->count ? d->files[d->at]->offset - d->pos : n;
            if (skip > n) skip = n;
            data += skip;
            n -= (size_t)skip;
            d->pos += skip;
            continue;
        }
        size_t k = n < d->left ? n : (size_t)d->left;
        if (d->taking && d->out->write(d->out->context, data, k)) return -1;
        data += k;
        n -= k;
        d->pos += k;
        d->left -= k;
        if (!d->left) {
            if (d->taking && d->out->end(d->out->context)) return -1;
            d->taken += d->taking;
            d->at++;
            d->started = 0;
        }
    }
}

static int fail(char *error, size_t size, const char *why)
{
    pc5_copy(error, size, why);
    return -1;
}

static int cmp_offset(const void *a, const void *b)
{
    const CabFile *x = *(CabFile *const *)a, *y = *(CabFile *const *)b;
    return x->offset < y->offset ? -1 : x->offset > y->offset;
}

/* One folder's bytes, block by block, to its files. */
static int cab_folder(const Pc5CabIn *in, const CabFolder *folder, uint8_t data_reserve, Dispatch *d, char *error,
                      size_t size)
{
    int kind = folder->type & 0xf, status = -1;
    uint64_t at = folder->data_at;
    uint8_t *packed = NULL, *work = NULL, *frame = NULL, *all = NULL;
    uint32_t *frame_sizes = NULL;
    Lzx *lzx = NULL;
    size_t all_size = 0;

    if (kind == 2) return fail(error, size, "a Quantum cabinet, which PC5 can't read");
    if (kind > 3) return fail(error, size, "a cabinet compressed in a way PC5 doesn't know");
    packed = pc5_alloc(65536 + 8);
    work = pc5_alloc(2 * 32768 + 8);   /* MSZIP: the 32 KiB before, then the block */
    frame = pc5_alloc(LZX_FRAME);
    if (!packed || !work || !frame) goto out_of_memory;
    if (kind == 3) {
        /* LZX: the folder's blocks are one stream; each block's output is one frame of it. */
        if (!(frame_sizes = pc5_alloc(sizeof(*frame_sizes) * (folder->blocks + 1u)))) goto out_of_memory;
        uint64_t scan = at, total = 0;
        for (int b = 0; b < folder->blocks; b++) {
            uint8_t head[8];
            if (in->read(in->context, scan, head, 8)) goto damaged;
            total += u16(head + 4);
            scan += 8u + data_reserve + u16(head + 4);
        }
        if (total > (512u << 20) || !(all = pc5_alloc((size_t)total + 16))) goto out_of_memory;
        for (int b = 0; b < folder->blocks; b++) {
            uint8_t head[8];
            if (in->read(in->context, at, head, 8)) goto damaged;
            uint16_t bytes = u16(head + 4);
            frame_sizes[b] = u16(head + 6);
            if (in->read(in->context, at + 8 + data_reserve, all + all_size, bytes)) goto damaged;
            all_size += bytes;
            at += 8u + data_reserve + bytes;
        }
        if (!(lzx = pc5_alloc(sizeof(*lzx))) || lzx_init(lzx, (folder->type >> 8) & 0x1f, all, all_size)) {
            if (lzx && !lzx->window) goto out_of_memory;
            pc5_copy(error, size, "an LZX cabinet with a window PC5 doesn't know");
            goto done;
        }
        for (int b = 0; b < folder->blocks && d->at < d->count; b++) {
            if (lzx_frame(lzx, frame_sizes[b], frame)) goto damaged;
            if (dispatch(d, frame, frame_sizes[b])) goto stopped;
        }
        status = 0;
        goto done;
    }
    size_t history = 0;
    /* Once its files have all been handed out, the rest of the folder isn't read. */
    for (int b = 0; b < folder->blocks && d->at < d->count; b++) {
        uint8_t head[8];
        if (in->read(in->context, at, head, 8)) goto damaged;
        uint16_t bytes = u16(head + 4), unpacked = u16(head + 6);
        if (in->read(in->context, at + 8 + data_reserve, packed, bytes)) goto damaged;
        at += 8u + data_reserve + bytes;
        if (kind == 0) {
            if (bytes != unpacked) goto damaged;
            if (dispatch(d, packed, bytes)) goto stopped;
            continue;
        }
        /* MSZIP: "CK", then deflate that may look back into the block before. */
        if (bytes < 2 || packed[0] != 'C' || packed[1] != 'K' || unpacked > 32768) goto damaged;
        Inflate s = { packed + 2, bytes - 2u, 0, 0, 0, 0, work, history, history + unpacked };
        if (inflate_run(&s) || s.at != history + unpacked) goto damaged;
        if (dispatch(d, work + history, unpacked)) goto stopped;
        size_t kept = s.at < 32768 ? s.at : 32768;
        memmove(work, work + s.at - kept, kept);
        history = kept;
    }
    status = 0;
    goto done;
out_of_memory:
    pc5_copy(error, size, "not enough memory to read the cabinet");
    goto done;
damaged:
    pc5_copy(error, size, "the cabinet is damaged");
    goto done;
stopped:
    pc5_copy(error, size, "a file couldn't be written");
done:
    if (lzx) pc5_free(lzx->window);
    pc5_free(lzx);
    pc5_free(all);
    pc5_free(frame_sizes);
    pc5_free(frame);
    pc5_free(work);
    pc5_free(packed);
    return status;
}

int pc5_cab_extract(const Pc5CabIn *in, const Pc5CabOut *out, char *error, size_t size)
{
    uint8_t head[36], extra[4];
    uint16_t header_reserve = 0;
    uint8_t folder_reserve = 0, data_reserve = 0;
    CabFolder *folders = NULL;
    CabFile *files = NULL;
    CabFile **order = NULL;
    int taken = 0, status = -1;

    if (in->size < 36 || in->read(in->context, 0, head, 36) || memcmp(head, "MSCF", 4))
        return fail(error, size, "not a cabinet");
    uint32_t files_at = u32(head + 16);
    uint16_t nfolders = u16(head + 26), nfiles = u16(head + 28), flags = u16(head + 30);
    uint64_t at = 36;
    if (flags & 3) return fail(error, size, "a cabinet that goes on in another one");
    if (flags & 4) {
        if (in->read(in->context, at, extra, 4)) return fail(error, size, "the cabinet is damaged");
        header_reserve = u16(extra);
        folder_reserve = extra[2];
        data_reserve = extra[3];
        at += 4u + header_reserve;
    }
    folders = pc5_alloc(sizeof(*folders) * (nfolders + 1u));
    files = pc5_alloc(sizeof(*files) * (nfiles + 1u));
    order = pc5_alloc(sizeof(*order) * (nfiles + 1u));
    if (!folders || !files || !order) {
        pc5_copy(error, size, "not enough memory to read the cabinet");
        goto done;
    }
    for (int f = 0; f < nfolders; f++) {
        uint8_t entry[8];
        if (in->read(in->context, at, entry, 8)) goto damaged;
        folders[f] = (CabFolder){ u32(entry), u16(entry + 4), u16(entry + 6) };
        at += 8u + folder_reserve;
    }
    at = files_at;
    for (int i = 0; i < nfiles; i++) {
        uint8_t entry[16 + 256];
        size_t got = 0;
        if (in->read(in->context, at, entry, 16)) goto damaged;
        files[i].size = u32(entry);
        files[i].offset = u32(entry + 4);
        files[i].folder = u16(entry + 8);
        files[i].handed = 0;
        /* The name: up to 255 bytes and its 0. */
        uint64_t left = in->size > at + 16 ? in->size - at - 16 : 0;
        size_t want = left < 256 ? (size_t)left : 256;
        if (!want || in->read(in->context, at + 16, entry + 16, want)) goto damaged;
        while (got < want && entry[16 + got]) got++;
        if (got == want) goto damaged;
        memcpy(files[i].name, entry + 16, got + 1);
        if (files[i].folder >= nfolders) {
            pc5_copy(error, size, "a cabinet with files in another cabinet");
            goto done;
        }
        at += 16 + got + 1;
    }
    /* A folder's bytes are read once for the files that follow one another in it. Two files
     * may also be the same bytes, which a cabinet stores once (NVIDIA's PhysX installer lists
     * two copies of one PhysXCore.dll at the same place): one that starts inside a file
     * already handed out in that reading is left for another reading of the folder, which
     * goes as far as its files need. Each reading hands out at least one file. */
    for (int f = 0; f < nfolders; f++) {
        for (;;) {
            Dispatch d = { out, order, 0, 0, 0, 0, 0, 0, 0 };
            int listed = 0;
            for (int i = 0; i < nfiles; i++)
                if (files[i].folder == f && !files[i].handed) order[listed++] = &files[i];
            if (!listed) break;
            qsort(order, (size_t)listed, sizeof(*order), cmp_offset);
            uint64_t end = 0;
            for (int i = 0; i < listed; i++) {
                if (d.count && order[i]->offset < end) continue;   /* its bytes come again in the next reading */
                order[i]->handed = 1;
                if ((uint64_t)order[i]->offset + order[i]->size > end) end = (uint64_t)order[i]->offset + order[i]->size;
                order[d.count++] = order[i];
            }
            if (cab_folder(in, &folders[f], data_reserve, &d, error, size)) goto done;
            if (d.at < d.count) {
                pc5_copy(error, size, "the cabinet ends before its files do");
                goto done;
            }
            taken += d.taken;
        }
    }
    status = taken;
    goto done;
damaged:
    pc5_copy(error, size, "the cabinet is damaged");
done:
    pc5_free(order);
    pc5_free(files);
    pc5_free(folders);
    return status;
}
