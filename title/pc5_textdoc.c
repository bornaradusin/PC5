/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: a text file a line at a time (pc5_textdoc.h). */
#include "pc5_textdoc.h"
#include "pc5_i18n.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static void why_set(char *why, size_t size, const char *text)
{
    if (why && size) pc5_copy(why, size, text);
}

/* One character of strict UTF-8 at s[*at] (no overlong forms, no surrogates, nothing past
 * U+10FFFF), *at moved past it; -1 when it is malformed. */
static long utf8_next(const uint8_t *s, size_t size, size_t *at)
{
    size_t i = *at;
    unsigned c = s[i];
    long cp;
    int more;
    if (c < 0x80) { *at = i + 1; return (long)c; }
    if (c >= 0xc2 && c <= 0xdf) { cp = c & 0x1f; more = 1; }
    else if (c >= 0xe0 && c <= 0xef) { cp = c & 0x0f; more = 2; }
    else if (c >= 0xf0 && c <= 0xf4) { cp = c & 0x07; more = 3; }
    else return -1;
    if (i + (size_t)more >= size) return -1;
    for (int k = 1; k <= more; k++) {
        unsigned b = s[i + (size_t)k];
        if ((b & 0xc0) != 0x80) return -1;
        cp = (cp << 6) | (long)(b & 0x3f);
    }
    if ((more == 2 && cp < 0x800) || (more == 3 && (cp < 0x10000 || cp > 0x10ffff)) || (cp >= 0xd800 && cp <= 0xdfff))
        return -1;
    *at = i + (size_t)more + 1;
    return cp;
}

static int utf8_valid(const uint8_t *s, size_t size)
{
    for (size_t i = 0; i < size;)
        if (utf8_next(s, size, &i) < 0) return 0;
    return 1;
}

static int put_utf8(Pc5Buf *out, long cp)
{
    uint8_t b[4];
    size_t n;
    if (cp < 0x80) { b[0] = (uint8_t)cp; n = 1; }
    else if (cp < 0x800) { b[0] = (uint8_t)(0xc0 | (cp >> 6)); b[1] = (uint8_t)(0x80 | (cp & 0x3f)); n = 2; }
    else if (cp < 0x10000) {
        b[0] = (uint8_t)(0xe0 | (cp >> 12));
        b[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3f));
        b[2] = (uint8_t)(0x80 | (cp & 0x3f));
        n = 3;
    } else {
        b[0] = (uint8_t)(0xf0 | (cp >> 18));
        b[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3f));
        b[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3f));
        b[3] = (uint8_t)(0x80 | (cp & 0x3f));
        n = 4;
    }
    return pc5_buf_put(out, b, n);
}

/* Little-endian UTF-16 without a mark: an even size, and nearly every second byte 0 in its
 * first 4 KB while the others aren't (ASCII text so written). */
static int looks_utf16le(const uint8_t *data, size_t size)
{
    size_t n = size < 4096 ? size : 4096, zero_high = 0, zero_low = 0;
    if (size < 4 || size % 2) return 0;
    n -= n % 2;
    for (size_t i = 0; i < n; i += 2) {
        zero_low += !data[i];
        zero_high += !data[i + 1];
    }
    return zero_high * 10 >= (n / 2) * 9 && zero_low * 10 <= n / 2;
}

/* The lines' positions again; none: an empty text is no lines rather than one empty line. */
static int reindex(Pc5TextDoc *doc, int none)
{
    int count = doc->text.len || !none ? 1 : 0;
    for (size_t i = 0; i < doc->text.len; i++) count += doc->text.data[i] == '\n';
    if (count > PC5_TEXTDOC_LINES) return -1;
    if (count + 1 > doc->cap) {
        int cap = count + 1024;
        uint32_t *starts = pc5_realloc(doc->starts, sizeof(*starts) * (size_t)cap);
        if (!starts) return -1;
        doc->starts = starts;
        doc->cap = cap;
    }
    doc->count = count;
    if (!count) return 0;
    int n = 0;
    doc->starts[n++] = 0;
    for (size_t i = 0; i < doc->text.len; i++)
        if (doc->text.data[i] == '\n') doc->starts[n++] = (uint32_t)(i + 1);
    return 0;
}

/* text[from, to) becomes add. */
static int splice(Pc5TextDoc *doc, size_t from, size_t to, const char *add, size_t n)
{
    size_t tail = doc->text.len - to;
    if (doc->text.len - (to - from) + n > PC5_TEXTDOC_MAX * 2) return -1;
    if (n > to - from && pc5_buf_reserve(&doc->text, n - (to - from) + 1)) return -1;
    if (!doc->text.data && !n) return 0;
    memmove(doc->text.data + from + n, doc->text.data + to, tail);
    if (n) memcpy(doc->text.data + from, add, n);
    doc->text.len = from + n + tail;
    return 0;
}

/* text without its \r (a line typed or pasted), checked for UTF-8; NULL when it isn't. */
static char *clean(const char *text, size_t size, size_t *out_len)
{
    char *out = pc5_alloc(size + 1);
    size_t n = 0;
    if (!out) return NULL;
    for (size_t i = 0; i < size; i++)
        if (text[i] != '\r') out[n++] = text[i];
    if (!utf8_valid((const uint8_t *)out, n) || memchr(out, 0, n)) {
        pc5_free(out);
        return NULL;
    }
    *out_len = n;
    return out;
}

int pc5_textdoc_parse(Pc5TextDoc *doc, const uint8_t *data, size_t size, char *why, size_t why_size)
{
    Pc5Buf raw = { 0 };
    const uint8_t *at = data;
    size_t left = size;

    memset(doc, 0, sizeof(*doc));
    if (size > PC5_TEXTDOC_MAX) {
        why_set(why, why_size, N_("It's larger than 8 MB: PC5 changes text files up to that size."));
        return -1;
    }
    if (left >= 3 && at[0] == 0xef && at[1] == 0xbb && at[2] == 0xbf) {
        doc->bom = 1;
        at += 3;
        left -= 3;
    } else if (left >= 2 && at[0] == 0xff && at[1] == 0xfe) {
        doc->bom = 1;
        doc->encoding = PC5_TEXT_UTF16LE;
        at += 2;
        left -= 2;
    } else if (left >= 2 && at[0] == 0xfe && at[1] == 0xff) {
        why_set(why, why_size, N_("It's UTF-16 big-endian text, which PC5 doesn't change."));
        return -1;
    } else if (looks_utf16le(at, left)) {
        doc->encoding = PC5_TEXT_UTF16LE;
    }

    if (doc->encoding == PC5_TEXT_UTF16LE) {
        if (left % 2) {
            why_set(why, why_size, N_("It isn't text: PC5 changes only text files (.ini, .cfg, .txt and the like)."));
            return -1;
        }
        for (size_t i = 0; i < left; i += 2) {
            long cp = at[i] | (at[i + 1] << 8);
            if (cp >= 0xd800 && cp <= 0xdbff && i + 3 < left) {
                long low = at[i + 2] | (at[i + 3] << 8);
                if (low >= 0xdc00 && low <= 0xdfff) {
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                    i += 2;
                }
            }
            if (!cp || (cp >= 0xd800 && cp <= 0xdfff)) {
                pc5_buf_free(&raw);
                why_set(why, why_size, N_("It isn't text: PC5 changes only text files (.ini, .cfg, .txt and the like)."));
                return -1;
            }
            if (put_utf8(&raw, cp)) {
                pc5_buf_free(&raw);
                why_set(why, why_size, N_("There isn't memory enough to open it."));
                return -1;
            }
        }
    } else {
        size_t controls = 0;
        for (size_t i = 0; i < left; i++) {
            uint8_t b = at[i];
            if (!b) controls = left;   /* a 0 byte: never text */
            else if (b < 0x20 && b != '\t' && b != '\n' && b != '\r' && b != '\f' && b != '\v' && b != 0x1a && b != 0x1b)
                controls++;
        }
        if (controls * 100 > left) {
            why_set(why, why_size, N_("It isn't text: PC5 changes only text files (.ini, .cfg, .txt and the like)."));
            return -1;
        }
        if (utf8_valid(at, left)) {
            if (pc5_buf_put(&raw, at, left) && left) {
                why_set(why, why_size, N_("There isn't memory enough to open it."));
                return -1;
            }
        } else {
            doc->encoding = PC5_TEXT_LATIN1;
            doc->bom = 0;
            for (size_t i = 0; i < left; i++)
                if (put_utf8(&raw, at[i])) {
                    pc5_buf_free(&raw);
                    why_set(why, why_size, N_("There isn't memory enough to open it."));
                    return -1;
                }
        }
    }

    /* The line ends: \r\n when the first is, every \r before a \n taken out. */
    size_t n = 0;
    uint8_t *t = raw.data;
    const uint8_t *first = raw.len ? memchr(t, '\n', raw.len) : NULL;
    doc->crlf = first && first > t && first[-1] == '\r';
    for (size_t i = 0; i < raw.len; i++) {
        if (t[i] == '\r' && i + 1 < raw.len && t[i + 1] == '\n') continue;
        t[n++] = t[i];
    }
    raw.len = n;
    doc->text = raw;
    doc->final_newline = n && t[n - 1] == '\n';
    if (doc->final_newline) doc->text.len--;
    if (reindex(doc, !doc->final_newline)) {
        pc5_textdoc_free(doc);
        why_set(why, why_size, N_("It has more lines than PC5 can show."));
        return -1;
    }
    return 0;
}

int pc5_textdoc_read(Pc5TextDoc *doc, const char *path, char *why, size_t why_size)
{
    struct stat st;
    Pc5Buf data = { 0 };
    memset(doc, 0, sizeof(*doc));
    if (stat(path, &st) || !S_ISREG(st.st_mode)) {
        why_set(why, why_size, strerror(errno ? errno : EINVAL));
        return -1;
    }
    if ((uint64_t)st.st_size > PC5_TEXTDOC_MAX) {
        why_set(why, why_size, N_("It's larger than 8 MB: PC5 changes text files up to that size."));
        return -1;
    }
    if (pc5_read_file(path, &data, PC5_TEXTDOC_MAX)) {
        why_set(why, why_size, strerror(errno ? errno : EIO));
        pc5_buf_free(&data);
        return -1;
    }
    int rc = pc5_textdoc_parse(doc, data.data, data.len, why, why_size);
    pc5_buf_free(&data);
    return rc;
}

const char *pc5_textdoc_line(const Pc5TextDoc *doc, int i, size_t *len)
{
    if (i < 0 || i >= doc->count) { *len = 0; return ""; }
    size_t from = doc->starts[i], to = i + 1 < doc->count ? doc->starts[i + 1] - 1 : doc->text.len;
    *len = to - from;
    return (const char *)doc->text.data + from;
}

int pc5_textdoc_copy_line(const Pc5TextDoc *doc, int i, char *out, size_t size)
{
    size_t len;
    const char *line = pc5_textdoc_line(doc, i, &len);
    if (!size) return -1;
    size_t n = len < size - 1 ? len : size - 1;
    while (n < len && n && ((unsigned char)line[n] & 0xc0) == 0x80) n--;   /* not inside a character */
    memcpy(out, line, n);
    out[n] = 0;
    return n == len ? 0 : -1;
}

int pc5_textdoc_set(Pc5TextDoc *doc, int i, const char *text)
{
    size_t n, len;
    if (i < 0 || i >= doc->count) return -1;
    char *add = clean(text, strlen(text), &n);
    if (!add) return -1;
    pc5_textdoc_line(doc, i, &len);
    int rc = splice(doc, doc->starts[i], doc->starts[i] + len, add, n) || reindex(doc, 0);
    pc5_free(add);
    if (!rc) doc->changed = 1;
    return rc ? -1 : 0;
}

int pc5_textdoc_insert(Pc5TextDoc *doc, int at, const char *text)
{
    size_t n;
    if (at < 0 || at > doc->count) return -1;
    char *add = clean(text, strlen(text), &n);
    if (!add) return -1;
    char *line = pc5_alloc(n + 2);
    int rc = -1;
    if (line) {
        if (!doc->count) {
            rc = splice(doc, 0, doc->text.len, add, n);
        } else if (at < doc->count) {
            memcpy(line, add, n);
            line[n] = '\n';
            rc = splice(doc, doc->starts[at], doc->starts[at], line, n + 1);
        } else {
            line[0] = '\n';
            memcpy(line + 1, add, n);
            rc = splice(doc, doc->text.len, doc->text.len, line, n + 1);
        }
        rc = rc || reindex(doc, 0);
    }
    pc5_free(line);
    pc5_free(add);
    if (!rc) doc->changed = 1;
    return rc ? -1 : 0;
}

int pc5_textdoc_delete(Pc5TextDoc *doc, int i)
{
    int rc;
    if (i < 0 || i >= doc->count) return -1;
    if (doc->count == 1) {
        doc->text.len = 0;
        rc = reindex(doc, 1);
    } else if (i + 1 < doc->count) {
        rc = splice(doc, doc->starts[i], doc->starts[i + 1], NULL, 0) || reindex(doc, 0);
    } else {
        rc = splice(doc, doc->starts[i] - 1, doc->text.len, NULL, 0) || reindex(doc, 0);
    }
    if (!rc) doc->changed = 1;
    return rc ? -1 : 0;
}

int pc5_textdoc_replace(Pc5TextDoc *doc, const char *text, size_t size)
{
    size_t n;
    char *add = clean(text, size, &n);
    if (!add) return -1;
    int final = n && add[n - 1] == '\n';
    int rc = splice(doc, 0, doc->text.len, add, final ? n - 1 : n) || reindex(doc, !n);
    pc5_free(add);
    if (rc) return -1;
    doc->final_newline = final;
    doc->changed = 1;
    return 0;
}

static int put_unit(Pc5Buf *out, unsigned unit)
{
    uint8_t b[2] = { (uint8_t)(unit & 0xff), (uint8_t)(unit >> 8) };
    return pc5_buf_put(out, b, 2);
}

/* text[0, size) in the doc's encoding onto out. */
static int encode_run(const Pc5TextDoc *doc, Pc5Buf *out, const uint8_t *text, size_t size, char *why, size_t why_size)
{
    if (doc->encoding == PC5_TEXT_UTF8) return size ? pc5_buf_put(out, text, size) : 0;
    for (size_t i = 0; i < size;) {
        long cp = utf8_next(text, size, &i);
        if (cp < 0) return -1;
        if (doc->encoding == PC5_TEXT_LATIN1) {
            if (cp > 0xff) {
                char line[200], ch[8] = "";
                size_t end = i;
                size_t start = end;
                while (start > 0 && (text[start - 1] & 0xc0) == 0x80) start--;
                if (start > 0) start--;
                if (end - start < sizeof(ch)) {
                    memcpy(ch, text + start, end - start);
                    ch[end - start] = 0;
                }
                pc5_tf(line, sizeof(line),
                       "This file is in an older encoding with room for Western letters only, and \xe2\x80\x9c%s\xe2\x80\x9d "
                       "isn't one of them.", ch);
                why_set(why, why_size, line);
                return -2;
            }
            uint8_t b = (uint8_t)cp;
            if (pc5_buf_put(out, &b, 1)) return -1;
        } else if (cp >= 0x10000) {
            cp -= 0x10000;
            if (put_unit(out, 0xd800 + (unsigned)(cp >> 10)) || put_unit(out, 0xdc00 + (unsigned)(cp & 0x3ff))) return -1;
        } else if (put_unit(out, (unsigned)cp)) {
            return -1;
        }
    }
    return 0;
}

int pc5_textdoc_encode(const Pc5TextDoc *doc, Pc5Buf *out, char *why, size_t why_size)
{
    static const uint8_t utf8_bom[] = { 0xef, 0xbb, 0xbf }, utf16_bom[] = { 0xff, 0xfe };
    int rc = 0;
    out->len = 0;
    if (doc->bom)
        rc = doc->encoding == PC5_TEXT_UTF16LE ? pc5_buf_put(out, utf16_bom, 2) : pc5_buf_put(out, utf8_bom, 3);
    for (int i = 0; !rc && i < doc->count; i++) {
        size_t len;
        const char *line = pc5_textdoc_line(doc, i, &len);
        rc = encode_run(doc, out, (const uint8_t *)line, len, why, why_size);
        if (!rc && (i + 1 < doc->count || doc->final_newline))
            rc = encode_run(doc, out, (const uint8_t *)(doc->crlf ? "\r\n" : "\n"), doc->crlf ? 2 : 1, why, why_size);
    }
    if (rc == -1) why_set(why, why_size, N_("There isn't memory enough to save it."));
    if (!rc && !out->data && pc5_buf_reserve(out, 1)) {
        why_set(why, why_size, N_("There isn't memory enough to save it."));
        rc = -1;
    }
    return rc ? -1 : 0;
}

int pc5_textdoc_write(const Pc5TextDoc *doc, const char *path, char *why, size_t why_size)
{
    Pc5Buf out = { 0 };
    if (pc5_textdoc_encode(doc, &out, why, why_size)) {
        pc5_buf_free(&out);
        return -1;
    }
    int rc = pc5_write_file(path, out.data, out.len);
    if (rc) why_set(why, why_size, strerror(errno ? errno : EIO));
    pc5_buf_free(&out);
    return rc;
}

int pc5_textdoc_join(const Pc5TextDoc *doc, Pc5Buf *out)
{
    out->len = 0;
    if ((doc->text.len && pc5_buf_put(out, doc->text.data, doc->text.len)) ||
        (doc->count && doc->final_newline && pc5_buf_put(out, "\n", 1)))
        return -1;
    return pc5_buf_terminate(out);
}

void pc5_textdoc_free(Pc5TextDoc *doc)
{
    pc5_buf_free(&doc->text);
    pc5_free(doc->starts);
    memset(doc, 0, sizeof(*doc));
}
