/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "pc5_compat.h"
#include "pc5_i18n.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *const statuses[] = {
    N_("Unknown"), N_("Works perfectly"), N_("Playable with issues"), N_("Fix out, needs testing"),
    N_("Being looked at"), N_("Needs testing"), N_("Copy needs its store's app"), N_("Unresolved: doesn't run")
};
const char *pc5_compat_status(int status) { return statuses[status >= 0 && status < 8 ? status : 0]; }

/* Markdown is data, never executed or followed. Keep link labels, strip markup,
 * fold whitespace and reject controls. UTF-8 bytes are otherwise preserved. */
static void plain(char *out, size_t cap, const char *s, size_t length)
{
    size_t n = 0;
    for (size_t i = 0; i < length && n + 1 < cap; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\\' && i + 1 < length) c = (unsigned char)s[++i];
        else if (c == '*' || c == '`' || c == '[' || c == ']') {
            if (c == ']' && i + 1 < length && s[i + 1] == '(') {
                i += 2; while (i < length && s[i] != ')') ++i;
            }
            continue;
        } else if (c == '<') { while (i < length && s[i] != '>') ++i; c = ' '; }
        if (c < 33 || c == 127) c = ' ';
        if (c == ' ' && (!n || out[n - 1] == ' ')) continue;
        out[n++] = (char)c;
    }
    while (n && out[n - 1] == ' ') --n;
    /* Never leave a truncated multibyte code point at the end. */
    if (n && n + 1 == cap) {
        size_t at = n - 1;
        while (at && ((unsigned char)out[at] & 0xc0) == 0x80) --at;
        if ((unsigned char)out[at] >= 0xc0) n = at;
    }
    out[n] = 0;
}
static int source_status(const char *s)
{
    for (int i = 1; i < 8; ++i) if (strstr(s, statuses[i])) return i;
    return 0;
}
static void report_url(char *out, size_t cap, const char *s, size_t len)
{
    static const char base[] = "https://github.com/bornaradusin/PC5/";
    for (size_t i = 0; i + sizeof(base) < len; ++i) {
        if (memcmp(s + i, base, sizeof(base) - 1)) continue;
        size_t start = i; i += sizeof(base) - 1;
        if (i + 12 < len && !memcmp(s + i, "discussions/", 12)) i += 12;
        else if (i + 7 < len && !memcmp(s + i, "issues/", 7)) i += 7;
        else continue;
        size_t first = i;
        while (i < len && s[i] >= '0' && s[i] <= '9') ++i;
        if (i == first || (i < len && s[i] != ')' && s[i] != ' ')) continue;
        if (i - start < cap) { memcpy(out, s + start, i - start); out[i - start] = 0; }
        return;
    }
}
int pc5_compat_parse(const char *text, size_t length, Pc5CompatEntry **entries)
{
    static const char *const columns[8] = {
        "Game", "How it runs", "PC5", "Store and version", "Controller",
        "Settings that helped", "Notes", "Report or issue"
    };
    *entries = NULL;
    if (!text || !length || length > PC5_COMPAT_LIMIT || memchr(text, 0, length)) return -1;
    Pc5CompatEntry *rows = NULL;
    int count = 0, capacity = 0, header = 0;
    for (size_t start = 0; start < length;) {
        size_t end = start; while (end < length && text[end] != '\n') ++end;
        const char *line = text + start; size_t len = end - start;
        start = end + 1;
        if (!len || line[0] != '|') continue;
        const char *cells[8]; size_t sizes[8], at = 1; int n = 0;
        while (at < len && n < 8) {
            size_t from = at;
            for (; at < len; ++at) {
                if (line[at] == '\\' && at + 1 < len) { ++at; continue; }
                if (line[at] == '|') break;
            }
            if (at == len) break;
            cells[n] = line + from; sizes[n++] = at++ - from;
        }
        if (n != 8) { if (header) { pc5_free(rows); return -1; } continue; }
        char title[256]; plain(title, sizeof(title), cells[0], sizes[0]);
        if (!header && strcmp(title, columns[0])) continue;
        /* The eighth closing delimiter must be the end of this row. Silently
         * ignoring a new column could map FPS, notes or reports to the wrong
         * fields and replace a valid offline cache with misleading results. */
        for (; at < len; ++at) {
            if (line[at] != ' ' && line[at] != '\t' && line[at] != '\r') {
                pc5_free(rows); return -1;
            }
        }
        if (!header) {
            for (int i = 0; i < 8; ++i) {
                char name[128]; plain(name, sizeof(name), cells[i], sizes[i]);
                if (strcmp(name, columns[i])) { pc5_free(rows); return -1; }
            }
            header = 1; continue;
        }
        if (!header || !title[0] || title[0] == '-') continue;
        if (count == PC5_COMPAT_MAX) { pc5_free(rows); return -1; }
        if (count == capacity) {
            /* Doubling: a long list isn't copied again for every 32 games. */
            int grown = capacity ? capacity * 2 : 32;
            if (grown > PC5_COMPAT_MAX) grown = PC5_COMPAT_MAX;
            Pc5CompatEntry *more = pc5_realloc(rows, (size_t)grown * sizeof(*rows));
            if (!more) { pc5_free(rows); return -1; }
            rows = more;
            capacity = grown;
        }
        Pc5CompatEntry *r = &rows[count++]; memset(r, 0, sizeof(*r));
        pc5_copy(r->title, sizeof(r->title), title);
        char state[256]; plain(state, sizeof(state), cells[1], sizes[1]); r->status = source_status(state);
        plain(r->version, sizeof(r->version), cells[2], sizes[2]);
        plain(r->store, sizeof(r->store), cells[3], sizes[3]);
        plain(r->controller, sizeof(r->controller), cells[4], sizes[4]);
        plain(r->settings, sizeof(r->settings), cells[5], sizes[5]);
        plain(r->notes, sizeof(r->notes), cells[6], sizes[6]);
        plain(r->author, sizeof(r->author), cells[7], sizes[7]);
        report_url(r->report, sizeof(r->report), cells[7], sizes[7]);
    }
    if (!count) { pc5_free(rows); return -1; }
    *entries = rows; return count;
}
int pc5_compat_matches(const Pc5CompatEntry *r, const char *q, int filter)
{
    if (filter && r->status != filter) return 0;
    for (size_t i = 0; r->title[i]; ++i) {
        size_t j = 0;
        while (q[j] && r->title[i + j] && pc5_lower((unsigned char)q[j]) == pc5_lower((unsigned char)r->title[i + j])) ++j;
        if (!q[j]) return 1;
    }
    return !q[0];
}
void pc5_compat_key(const char *name, char *out, size_t size)
{
    static const char *const numerals[][2] = { { "ii", "2" }, { "iii", "3" }, { "iv", "4" }, { "v", "5" },
                                               { "vi", "6" }, { "vii", "7" }, { "viii", "8" }, { "ix", "9" } };
    char text[512], words[64][64];
    size_t n = 0, count = 0, used = 0;

    if (!size) return;
    out[0] = 0;
    /* Lower case, "&" as " and ", apostrophes (' and U+2019) gone. */
    for (const unsigned char *p = (const unsigned char *)name; *p && n + 6 < sizeof(text); p++) {
        if (*p == '\'') continue;
        if (p[0] == 0xe2 && p[1] == 0x80 && p[2] == 0x99) { p += 2; continue; }
        if (*p == '&') { memcpy(text + n, " and ", 5); n += 5; continue; }
        text[n++] = (char)pc5_lower(*p);
    }
    text[n] = 0;
    /* A version after the name: spaces, an optional "v", digits with at least one point, maybe a
     * letter, then only spaces. */
    {
        size_t end = n;
        while (end && text[end - 1] == ' ') end--;
        size_t at = end;
        if (at && text[at - 1] >= 'a' && text[at - 1] <= 'z' && at >= 2 && text[at - 2] >= '0' && text[at - 2] <= '9') at--;
        size_t digits_end = at;
        int points = 0, ok = 1;
        while (at && ((text[at - 1] >= '0' && text[at - 1] <= '9') || text[at - 1] == '.')) {
            if (text[at - 1] == '.') points++;
            at--;
        }
        /* digits, points between them: no point first or last, none doubled */
        if (at == digits_end || !points || text[at] == '.' || text[digits_end - 1] == '.') ok = 0;
        for (size_t i = at; ok && i + 1 < digits_end; i++)
            if (text[i] == '.' && text[i + 1] == '.') ok = 0;
        if (ok && at && text[at - 1] == 'v') at--;
        if (ok && at && text[at - 1] == ' ') {
            while (at && text[at - 1] == ' ') at--;
            text[at] = 0;
            n = at;
        }
    }
    /* Words: letters, digits and any byte of a character past ASCII. */
    for (size_t i = 0; i <= n && count < 64; i++) {
        unsigned char c = i < n ? (unsigned char)text[i] : 0;
        int letter = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c >= 0x80;
        if (letter) {
            if (used + 1 < sizeof(words[0])) words[count][used++] = (char)c;
        } else if (used) {
            words[count++][used] = 0;
            used = 0;
        }
    }
    size_t first = count > 1 && !strcmp(words[0], "the") ? 1 : 0, at = 0;
    for (size_t w = first; w < count; w++) {
        const char *word = words[w];
        for (size_t k = 0; k < sizeof(numerals) / sizeof(numerals[0]); k++)
            if (!strcmp(word, numerals[k][0])) word = numerals[k][1];
        size_t len = strlen(word);
        if (at + len + 1 > size) break;
        memcpy(out + at, word, len);
        at += len;
    }
    out[at] = 0;
    if (!at) {   /* nothing but punctuation: the name in lower case */
        for (; name[at] && at + 1 < size; at++) out[at] = (char)pc5_lower((unsigned char)name[at]);
        out[at] = 0;
    }
}

int pc5_compat_with_settings(const Pc5CompatEntry *entries, int count, const char *title)
{
    char want[512], key[512];
    pc5_compat_key(title, want, sizeof(want));
    if (!want[0]) return -1;
    for (int i = 0; i < count; i++) {
        const Pc5CompatEntry *e = &entries[i];
        if ((e->status != 1 && e->status != 2) || (!strstr(e->settings, "Graphics mode ") && !strstr(e->settings, "Graphics API ")))
            continue;
        pc5_compat_key(e->title, key, sizeof(key));
        if (!strcmp(key, want)) return i;
    }
    return -1;
}

void pc5_compat_init(Pc5Compat *l, const char *root, const Pc5Http *http)
{
    memset(l, 0, sizeof(*l)); l->http = *http;
    pc5_format(l->cache, sizeof(l->cache), "%s/compatibility.md", root);
    Pc5Buf b = { 0 };
    if (!pc5_read_file(l->cache, &b, PC5_COMPAT_LIMIT)) {
        int n = pc5_compat_parse((const char *)b.data, b.len, &l->entries);
        if (n > 0) {
            struct stat st;
            l->count = n; l->cached = 1;
            if (!stat(l->cache, &st)) l->checked = (int64_t)st.st_mtime;
        }
    }
    pc5_buf_free(&b);
}
static void *compat_worker(void *context)
{
    Pc5Compat *l = context; Pc5Buf b = { 0 }; int status = 0;
    l->result = -1;
    if (!l->http.get || l->http.get(l->http.context,
        "https://raw.githubusercontent.com/wiki/bornaradusin/PC5/Game-compatibility.md",
        &b, PC5_COMPAT_LIMIT, &status) || status != 200) {
        pc5_copy(l->error, sizeof(l->error), N_("Couldn't refresh. Showing the last saved list."));
    } else {
        l->incoming_count = pc5_compat_parse((const char *)b.data, b.len, &l->incoming);
        if (l->incoming_count > 0) {
            /* The offline copy is written only when the list changed, not every minute the
             * tab is open: the console's storage isn't worn by the same file again. */
            Pc5Buf saved = { 0 };
            int same = !pc5_read_file(l->cache, &saved, PC5_COMPAT_LIMIT) && saved.len == b.len &&
                       !memcmp(saved.data, b.data, b.len);
            pc5_buf_free(&saved);
            l->result = 0;
            if (!same && pc5_write_file(l->cache, b.data, b.len))
                pc5_copy(l->error, sizeof(l->error), N_("Updated, but the offline copy couldn't be saved."));
        } else pc5_copy(l->error, sizeof(l->error), N_("The source format changed. Your saved list was kept."));
    }
    pc5_buf_free(&b); atomic_store(&l->done, 1); return NULL;
}
int pc5_compat_refresh(Pc5Compat *l)
{
    if (l->running) return 0;
    l->error[0] = 0; atomic_init(&l->done, 0);
    if (pc5_thread_start(&l->thread, compat_worker, l, 0)) return -1;
    l->running = 1; return 0;
}
/* The refresh's thread joined, and its list used when it got one. */
static void compat_finish(Pc5Compat *l)
{
    pthread_join(l->thread, NULL); l->running = 0;
    if (!l->result) {
        pc5_free(l->entries); l->entries = l->incoming; l->incoming = NULL;
        l->count = l->incoming_count; l->cached = 0; l->checked = (int64_t)time(NULL);
    }
}
int pc5_compat_poll(Pc5Compat *l)
{
    if (!l->running || !atomic_load(&l->done)) return 0;
    compat_finish(l);
    return 1;
}
void pc5_compat_wait(Pc5Compat *l)
{
    /* Its connection closed; the list itself is taken by the next poll, as usual. */
    while (l->running && !atomic_load(&l->done)) usleep(10000);
}
void pc5_compat_end(Pc5Compat *l)
{
    if (l->running) pthread_join(l->thread, NULL);
    pc5_free(l->entries); pc5_free(l->incoming); memset(l, 0, sizeof(*l));
}
