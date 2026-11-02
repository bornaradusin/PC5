/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "pc5_compat.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

static const char *const statuses[] = {
    "Unknown", "Works perfectly", "Playable with issues", "Fix out, needs testing",
    "Being looked at", "Needs testing", "Copy needs its store's app", "Unresolved: doesn't run"
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
    int count = 0, header = 0;
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
        if (!(count % 32)) {
            Pc5CompatEntry *more = pc5_realloc(rows, (size_t)(count + 32) * sizeof(*rows));
            if (!more) { pc5_free(rows); return -1; }
            rows = more;
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
        pc5_copy(l->error, sizeof(l->error), "Couldn't refresh. Showing the last saved list.");
    } else {
        l->incoming_count = pc5_compat_parse((const char *)b.data, b.len, &l->incoming);
        if (l->incoming_count > 0) {
            l->result = 0;
            if (pc5_write_file(l->cache, b.data, b.len))
                pc5_copy(l->error, sizeof(l->error), "Updated, but the offline copy couldn't be saved.");
        } else pc5_copy(l->error, sizeof(l->error), "The source format changed. Your saved list was kept.");
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
int pc5_compat_poll(Pc5Compat *l)
{
    if (!l->running || !atomic_load(&l->done)) return 0;
    pthread_join(l->thread, NULL); l->running = 0;
    if (!l->result) {
        pc5_free(l->entries); l->entries = l->incoming; l->incoming = NULL;
        l->count = l->incoming_count; l->cached = 0; l->checked = (int64_t)time(NULL);
    }
    return 1;
}
void pc5_compat_end(Pc5Compat *l)
{
    if (l->running) pthread_join(l->thread, NULL);
    pc5_free(l->entries); pc5_free(l->incoming); memset(l, 0, sizeof(*l));
}
