/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "pc5.h"
#include "pc5_bidi.h"
#include <fribidi.h>
#include <stdatomic.h>
#include <string.h>

enum { BIDI_CHARS = 2048, CACHE_ENTRIES = 64, CACHE_INPUT = 1024, CACHE_OUTPUT = 4096 };
typedef struct BidiCache {
    size_t bytes, length;
    int rtl;
    char input[CACHE_INPUT], visual[CACHE_OUTPUT];
} BidiCache;
static BidiCache cache[CACHE_ENTRIES];
static unsigned cache_next;
static atomic_flag cache_lock = ATOMIC_FLAG_INIT;
static void lock_cache(void) { while (atomic_flag_test_and_set_explicit(&cache_lock, memory_order_acquire)) {} }
static void unlock_cache(void) { atomic_flag_clear_explicit(&cache_lock, memory_order_release); }

static int decode(const unsigned char **at, const unsigned char *end, FriBidiChar *cp)
{
    unsigned char first = *(*at)++;
    unsigned more = first < 0x80 ? 0 : first >= 0xc2 && first <= 0xdf ? 1 :
                    first >= 0xe0 && first <= 0xef ? 2 : first >= 0xf0 && first <= 0xf4 ? 3 : 9;
    if (more == 9 || (size_t)(end - *at) < more) return -1;
    uint32_t value = more ? first & ((1u << (6 - more)) - 1) : first;
    for (unsigned i = 0; i < more; ++i) {
        unsigned char next = *(*at)++;
        if ((next & 0xc0) != 0x80) return -1;
        value = value << 6 | (next & 63);
    }
    if ((more == 1 && value < 0x80) || (more == 2 && value < 0x800) ||
        (more == 3 && value < 0x10000) || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return -1;
    *cp = value;
    return 0;
}
static size_t encode(FriBidiChar cp, char bytes[4])
{
    if (cp < 0x80) { bytes[0] = (char)cp; return 1; }
    if (cp < 0x800) { bytes[0] = (char)(0xc0 | cp >> 6); bytes[1] = (char)(0x80 | (cp & 63)); return 2; }
    if (cp < 0x10000) {
        bytes[0] = (char)(0xe0 | cp >> 12); bytes[1] = (char)(0x80 | ((cp >> 6) & 63)); bytes[2] = (char)(0x80 | (cp & 63)); return 3;
    }
    bytes[0] = (char)(0xf0 | cp >> 18); bytes[1] = (char)(0x80 | ((cp >> 12) & 63));
    bytes[2] = (char)(0x80 | ((cp >> 6) & 63)); bytes[3] = (char)(0x80 | (cp & 63)); return 4;
}
static int ascii_word(FriBidiChar cp)
{
    return (cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z');
}
static int ascii_span(FriBidiChar cp)
{
    /* Frame-generation rates are a single LTR expression, e.g. 30 → 60. */
    if (cp == 0x2190 || cp == 0x2192) return 1;
    return cp >= 0x20 && cp < 0x7f && cp != '(' && cp != ')' && cp != '[' && cp != ']' &&
           cp != '{' && cp != '}' && cp != ',' && cp != ';' && cp != '!' && cp != '?' && cp != '"';
}
int pc5_bidi_visual(const char *text, size_t bytes, char *out, size_t capacity, int *rtl)
{
    if (rtl) *rtl = 0;
    if (!text || !out || !capacity || !bytes || bytes >= PC5_BIDI_BYTES) return -1;
    out[0] = 0;
    int needed = 0, arabic = 0;
    for (size_t i = 0; i < bytes; ++i) {
        unsigned char c = (unsigned char)text[i];
        int rtl_letter = (c >= 0xd6 && c <= 0xdf) ||
            (c == 0xe0 && i + 1 < bytes && ((unsigned char)text[i + 1] == 0xa2 || (unsigned char)text[i + 1] == 0xa3));
        if (rtl_letter) arabic = 1;
        if (rtl_letter ||
            (c == 0xe2 && i + 2 < bytes && (unsigned char)text[i + 1] == 0x81 &&
             (unsigned char)text[i + 2] >= 0xa6 && (unsigned char)text[i + 2] <= 0xa9)) needed = 1;
    }
    if (!needed) return -1;
    if (bytes < CACHE_INPUT) {
        lock_cache();
        for (int i = 0; i < CACHE_ENTRIES; ++i) {
            if (cache[i].bytes != bytes || memcmp(cache[i].input, text, bytes)) continue;
            if (cache[i].length >= capacity) { unlock_cache(); return -1; }
            memcpy(out, cache[i].visual, cache[i].length + 1);
            if (rtl) *rtl = cache[i].rtl;
            int length = (int)cache[i].length;
            unlock_cache(); return length;
        }
        unlock_cache();
    }
    typedef struct Work { FriBidiChar raw[BIDI_CHARS], logical[BIDI_CHARS * 3], visual[BIDI_CHARS * 3]; } Work;
    Work *work = pc5_alloc(sizeof(*work));
    if (!work) return -1;
    const unsigned char *at = (const unsigned char *)text, *end = at + bytes;
    int count = 0, failed = 0;
    while (at < end && count < BIDI_CHARS) {
        if (decode(&at, end, &work->raw[count])) { failed = 1; break; }
        ++count;
    }
    if (at != end || !count || failed) { pc5_free(work); return -1; }
    int path = work->raw[0] == '/' ||
        (count > 2 && ((work->raw[0] >= 'A' && work->raw[0] <= 'Z') || (work->raw[0] >= 'a' && work->raw[0] <= 'z')) &&
         work->raw[1] == ':' && (work->raw[2] == '/' || work->raw[2] == '\\'));
    int protected_count = 0, isolated_path = 0, isolate_depth = 0;
    for (int i = 0; i < count;) {
        /* Translated %s values use FSI/PDI. Paths inside those values retain
         * their component order, while Arabic component names stay readable. */
        if (work->raw[i] == 0x2068 && i + 1 < count && work->raw[i + 1] == '/') {
            work->logical[protected_count++] = 0x2066;
            isolated_path = 1; ++isolate_depth; ++i; continue;
        }
        if (isolated_path && work->raw[i] == 0x2069) isolated_path = 0;
        if (work->raw[i] >= 0x2066 && work->raw[i] <= 0x2068) ++isolate_depth;
        else if (work->raw[i] == 0x2069 && isolate_depth) --isolate_depth;
        /* A whole path keeps its component order, including Arabic file names.
         * Each component chooses its own internal direction. */
        if ((path || isolated_path) && work->raw[i] != '/' && work->raw[i] != '\\' && work->raw[i] != 0x2069) {
            work->logical[protected_count++] = 0x2068;
            while (i < count && work->raw[i] != '/' && work->raw[i] != '\\' && work->raw[i] != 0x2069)
                work->logical[protected_count++] = work->raw[i++];
            work->logical[protected_count++] = 0x2069;
        } else if (!path && work->raw[i] == '/' && (!i || work->raw[i-1] == ' ' || work->raw[i-1] == ':')) {
            /* A literal ASCII path embedded in an Arabic sentence. Its leading
             * slash is neutral; isolate the token instead of letting it move. */
            int to = i;
            while (to < count && work->raw[to] >= 0x20 && work->raw[to] < 0x7f) ++to;
            while (to > i && work->raw[to - 1] == ' ') --to;
            work->logical[protected_count++] = 0x2066;
            while (i < to) work->logical[protected_count++] = work->raw[i++];
            work->logical[protected_count++] = 0x2069;
        } else if (!path && !isolated_path && !isolate_depth && ascii_word(work->raw[i])) {
            /* Keep units, versions and multiword Latin names together: 120 Hz
             * must not become Hz 120 in an Arabic paragraph. Brackets stay
             * outside the isolate so UBA can pair/mirror them with the sentence. */
            int to = i;
            while (to < count && ascii_span(work->raw[to])) ++to;
            while (to > i && (work->raw[to - 1] == ' ' || work->raw[to - 1] == ':')) --to;
            work->logical[protected_count++] = 0x2066;
            while (i < to) work->logical[protected_count++] = work->raw[i++];
            work->logical[protected_count++] = 0x2069;
        } else work->logical[protected_count++] = work->raw[i++];
    }
    FriBidiParType direction = path ? FRIBIDI_PAR_LTR : arabic ? FRIBIDI_PAR_RTL : FRIBIDI_PAR_ON;
    if (!fribidi_log2vis(work->logical, protected_count, &direction, work->visual, NULL, NULL, NULL)) { pc5_free(work); return -1; }
    size_t used = 0;
    for (int i = 0; i < protected_count; ++i) {
        FriBidiChar cp = work->visual[i];
        /* Joining and direction controls take part above but have no picture. */
        if (cp == 0xfeff || (cp >= 0x200b && cp <= 0x200f) || (cp >= 0x202a && cp <= 0x202e) ||
            (cp >= 0x2066 && cp <= 0x2069)) continue;
        char encoded[4]; size_t size = encode(cp, encoded);
        if (used + size >= capacity) { out[0] = 0; pc5_free(work); return -1; }
        memcpy(out + used, encoded, size); used += size;
    }
    out[used] = 0; pc5_free(work);
    int right = FRIBIDI_IS_RTL(direction) != 0;
    if (rtl) *rtl = right;
    if (bytes < CACHE_INPUT && used < CACHE_OUTPUT) {
        lock_cache();
        BidiCache *entry = &cache[cache_next++ % CACHE_ENTRIES];
        entry->bytes = bytes; entry->length = used; entry->rtl = right;
        memcpy(entry->input, text, bytes); entry->input[bytes] = 0;
        memcpy(entry->visual, out, used + 1);
        unlock_cache();
    }
    return (int)used;
}
