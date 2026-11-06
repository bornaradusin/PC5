/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: its text in the player's language (pc5_i18n.h). */
#include "pc5_i18n.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the languages ------------------------------------------------------------------- */

/* English first. Arabic uses the native bidi/shaping renderer and Noto Naskh. */
static const Pc5Language languages[] = {
    { "en", "English", NULL, 0 },
    { "ar", "\xd8\xa7\xd9\x84\xd8\xb9\xd8\xb1\xd8\xa8\xd9\x8a\xd8\xa9", NULL, 0 },
    { "id", "Bahasa Indonesia", NULL, 0 },
    { "cs", "\xc4\x8c" "e\xc5\xa1tina", NULL, 0 },
    { "da", "Dansk", NULL, 0 },
    { "de", "Deutsch", NULL, 0 },
    { "es", "Espa\xc3\xb1ol", NULL, 0 },
    { "es-419", "Espa\xc3\xb1ol (Latinoam\xc3\xa9rica)", "es", 0 },
    { "fr", "Fran\xc3\xa7" "ais", NULL, 0 },
    { "fr-CA", "Fran\xc3\xa7" "ais (Canada)", "fr", 0 },
    { "it", "Italiano", NULL, 0 },
    { "hu", "Magyar", NULL, 0 },
    { "nl", "Nederlands", NULL, 0 },
    { "no", "Norsk", NULL, 0 },
    { "pl", "Polski", NULL, 0 },
    { "pt-BR", "Portugu\xc3\xaas (Brasil)", "pt-PT", 0 },
    { "pt-PT", "Portugu\xc3\xaas (Portugal)", "pt-BR", 0 },
    { "ro", "Rom\xc3\xa2n\xc4\x83", NULL, 0 },
    { "fi", "Suomi", NULL, 0 },
    { "sv", "Svenska", NULL, 0 },
    { "vi", "Ti\xe1\xba\xbfng Vi\xe1\xbb\x87t", NULL, 0 },
    { "tr", "T\xc3\xbcrk\xc3\xa7" "e", NULL, 0 },
    { "el", "\xce\x95\xce\xbb\xce\xbb\xce\xb7\xce\xbd\xce\xb9\xce\xba\xce\xac", NULL, 0 },
    { "ru", "\xd0\xa0\xd1\x83\xd1\x81\xd1\x81\xd0\xba\xd0\xb8\xd0\xb9", NULL, 0 },
    { "uk", "\xd0\xa3\xd0\xba\xd1\x80\xd0\xb0\xd1\x97\xd0\xbd\xd1\x81\xd1\x8c\xd0\xba\xd0\xb0", NULL, 0 },
    { "ja", "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e", NULL, 1 },
    { "ko", "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4", NULL, 1 },
    { "zh-Hans", "\xe7\xae\x80\xe4\xbd\x93\xe4\xb8\xad\xe6\x96\x87", "zh-Hant", 1 },
    { "zh-Hant", "\xe7\xb9\x81\xe9\xab\x94\xe4\xb8\xad\xe6\x96\x87", "zh-Hans", 1 },
};
enum { LANGUAGES = (int)(sizeof(languages) / sizeof(languages[0])) };

int pc5_language_count(void) { return LANGUAGES; }

const Pc5Language *pc5_language(int index)
{
    return index >= 0 && index < LANGUAGES ? &languages[index] : &languages[0];
}

int pc5_language_find(const char *code)
{
    for (int i = 0; code && i < LANGUAGES; i++)
        if (!pc5_casecmp(code, languages[i].code)) return i;
    return -1;
}

const char *pc5_language_locale(const char *code)
{
    /* Each language's Windows locale (LC_ALL), as the console's own are (pc5_console_locale). */
    static const struct { const char *code, *locale; } locales[] = {
        { "en", "en_US.UTF-8" }, { "ar", "ar_SA.UTF-8" }, { "id", "id_ID.UTF-8" }, { "cs", "cs_CZ.UTF-8" },
        { "da", "da_DK.UTF-8" }, { "de", "de_DE.UTF-8" }, { "es", "es_ES.UTF-8" }, { "es-419", "es_MX.UTF-8" },
        { "fr", "fr_FR.UTF-8" }, { "fr-CA", "fr_CA.UTF-8" }, { "it", "it_IT.UTF-8" }, { "hu", "hu_HU.UTF-8" },
        { "nl", "nl_NL.UTF-8" }, { "no", "nb_NO.UTF-8" }, { "pl", "pl_PL.UTF-8" }, { "pt-BR", "pt_BR.UTF-8" },
        { "pt-PT", "pt_PT.UTF-8" }, { "ro", "ro_RO.UTF-8" }, { "fi", "fi_FI.UTF-8" }, { "sv", "sv_SE.UTF-8" },
        { "vi", "vi_VN.UTF-8" }, { "tr", "tr_TR.UTF-8" }, { "el", "el_GR.UTF-8" }, { "ru", "ru_RU.UTF-8" },
        { "uk", "uk_UA.UTF-8" }, { "ja", "ja_JP.UTF-8" }, { "ko", "ko_KR.UTF-8" }, { "zh-Hans", "zh_CN.UTF-8" },
        { "zh-Hant", "zh_TW.UTF-8" },
    };
    for (size_t i = 0; code && i < sizeof(locales) / sizeof(locales[0]); i++)
        if (!pc5_casecmp(code, locales[i].code)) return locales[i].locale;
    return NULL;
}

const char *pc5_language_of_console(int ps5_language)
{
    /* SCE_SYSTEM_PARAM_LANG_*: 0 Japanese, 1 English (US), ... 18 English (UK), 20 Spanish
     * (Latin America), 21 Arabic, 22 French (Canada), 27 Thai, 30 Ukrainian. */
    static const char *const codes[] = {
        "ja", "en", "fr", "es", "de", "it", "nl", "pt-PT", "ru", "ko",
        "zh-Hant", "zh-Hans", "fi", "sv", "da", "no", "pl", "pt-BR", "en", "tr",
        "es-419", "ar", "fr-CA", "cs", "hu", "el", "ro", "en", "vi", "id",
        "uk",
    };
    if (ps5_language < 0 || ps5_language >= (int)(sizeof(codes) / sizeof(codes[0]))) return NULL;
    return codes[ps5_language];
}

const char *pc5_keyboard_layout(const char *setting, int ps5_language)
{
    /* SCE_SYSTEM_PARAM_LANG_*, as pc5_language_of_console: each one's keyboard. */
    static const char *const console_layouts[] = {
        "us", "us", "fr", "es", "de", "it", "us", "pt", "ru", "us",
        "us", "us", "fi", "se", "dk", "no", "pl", "br", "gb", "tr",
        "latam", "us", "ca", "cz", "hu", "gr", "ro", "us", "us", "us",
        "ua",
    };
    static const struct { const char *code, *layout; } layouts[] = {
        { "fr", "fr" }, { "fr-CA", "ca" }, { "es", "es" }, { "es-419", "latam" }, { "de", "de" }, { "it", "it" },
        { "pt-PT", "pt" }, { "pt-BR", "br" }, { "ru", "ru" }, { "uk", "ua" }, { "el", "gr" }, { "da", "dk" },
        { "no", "no" }, { "sv", "se" }, { "fi", "fi" }, { "pl", "pl" }, { "cs", "cz" }, { "hu", "hu" },
        { "ro", "ro" }, { "tr", "tr" },
    };
    int count = (int)(sizeof(console_layouts) / sizeof(console_layouts[0]));
    const char *console = ps5_language >= 0 && ps5_language < count ? console_layouts[ps5_language] : "us";
    const char *console_code = pc5_language_of_console(ps5_language);
    if (!setting || !setting[0] || !pc5_casecmp(setting, "system")) return console;
    /* PC5 in another language than the console: that language's keyboard, unless the console's
     * is the same language (English (UK) with English, French (Canada) with French). */
    if (console_code && (!pc5_casecmp(console_code, setting) || (strlen(setting) == 2 && !strncmp(console_code, setting, 2))))
        return console;
    for (size_t i = 0; i < sizeof(layouts) / sizeof(layouts[0]); i++)
        if (!pc5_casecmp(setting, layouts[i].code)) return layouts[i].layout;
    return "us";
}

const char *pc5_language_choose(const char *setting, const char *console)
{
    int at = setting && setting[0] && pc5_casecmp(setting, "system") ? pc5_language_find(setting) : -1;
    if (at < 0) at = pc5_language_find(console);
    return languages[at < 0 ? 0 : at].code;
}

/* ---- formats ------------------------------------------------------------------------- */

/* A conversion's kind: what va_arg takes for it. */
enum { K_INT, K_LONG, K_LLONG, K_SIZE, K_INTMAX, K_PTRDIFF, K_DOUBLE, K_LDOUBLE, K_STRING, K_POINTER, K_BAD };
enum { MAX_ARGS = 16 };

typedef struct Conv {
    const char *start;      /* the '%' */
    const char *end;        /* after its letter */
    int position;           /* 1.., or 0 when it has none */
    int kind;
    int stars;              /* '*' widths or precisions (they take ints first) */
} Conv;

/* The conversion at *p (just after a '%'), its end in conv->end. 0, or -1 when it isn't one
 * PC5 takes. "%%" is kind -1. */
static int conv_read(const char *percent, Conv *conv)
{
    const char *p = percent + 1;
    int length = 0;   /* 0 none, 1 hh/h, 2 l, 3 ll, 4 z, 5 j, 6 t, 7 L */

    memset(conv, 0, sizeof(*conv));
    conv->start = percent;
    if (*p == '%') {
        conv->end = p + 1;
        conv->kind = -1;
        return 0;
    }
    /* A position: digits and '$'. */
    {
        const char *q = p;
        int n = 0;
        while (*q >= '0' && *q <= '9' && n < 1000) n = n * 10 + (*q++ - '0');
        if (*q == '$' && q > p) {
            if (n < 1 || n > MAX_ARGS) return -1;
            conv->position = n;
            p = q + 1;
        }
    }
    while (*p && strchr("-+ #0'", *p)) p++;
    if (*p == '*') { conv->stars++; p++; }
    else while (*p >= '0' && *p <= '9') p++;
    if (*p == '.') {
        p++;
        if (*p == '*') { conv->stars++; p++; }
        else while (*p >= '0' && *p <= '9') p++;
    }
    if (*p == 'h') { length = 1; p++; if (*p == 'h') p++; }
    else if (*p == 'l') { length = 2; p++; if (*p == 'l') { length = 3; p++; } }
    else if (*p == 'z') { length = 4; p++; }
    else if (*p == 'j') { length = 5; p++; }
    else if (*p == 't') { length = 6; p++; }
    else if (*p == 'L') { length = 7; p++; }
    if (!*p) return -1;
    switch (*p) {
    case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': case 'c':
        conv->kind = length == 0 || length == 1 ? K_INT : length == 2 ? K_LONG : length == 3 ? K_LLONG :
                     length == 4 ? K_SIZE : length == 5 ? K_INTMAX : length == 6 ? K_PTRDIFF : K_BAD;
        break;
    case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A':
        conv->kind = length == 7 ? K_LDOUBLE : length == 0 || length == 2 ? K_DOUBLE : K_BAD;
        break;
    case 's': conv->kind = length == 0 ? K_STRING : K_BAD; break;
    case 'p': conv->kind = length == 0 ? K_POINTER : K_BAD; break;
    default: conv->kind = K_BAD; break;   /* %n and anything else */
    }
    conv->end = p + 1;
    return conv->kind == K_BAD ? -1 : 0;
}

/* The conversions of a format (no "%%"), at most max. Their count, or -1 when one isn't
 * taken, there are too many, or positions are mixed with none. */
static int convs_of(const char *format, Conv *out, int max)
{
    int n = 0, positioned = -1;
    for (const char *p = format; *p; p++) {
        Conv conv;
        if (*p != '%') continue;
        if (conv_read(p, &conv)) return -1;
        p = conv.end - 1;
        if (conv.kind < 0) continue;
        if (n == max) return -1;
        if (positioned < 0) positioned = conv.position > 0;
        else if (positioned != (conv.position > 0)) return -1;
        /* A position with a '*' would need the star's own position: not taken. */
        if (conv.position && conv.stars) return -1;
        out[n++] = conv;
    }
    return n;
}

int pc5_format_matches(const char *english, const char *translated)
{
    Conv want[MAX_ARGS], got[MAX_ARGS];
    int wanted = convs_of(english, want, MAX_ARGS), have = convs_of(translated, got, MAX_ARGS);
    int used[MAX_ARGS] = { 0 };

    if (wanted < 0 || have < 0) return 0;
    if (!have || !got[0].position) {
        /* In order: the same kinds, the same '*'s. */
        if (have != wanted) return 0;
        for (int i = 0; i < have; i++)
            if (got[i].kind != want[i].kind || got[i].stars != want[i].stars) return 0;
        return 1;
    }
    /* With positions: each one the English's of that number, and every one used. */
    for (int i = 0; i < wanted; i++)
        if (want[i].stars) return 0;
    for (int i = 0; i < have; i++) {
        int at = got[i].position - 1;
        if (at >= wanted || got[i].kind != want[at].kind) return 0;
        used[at] = 1;
    }
    for (int i = 0; i < wanted; i++)
        if (!used[i]) return 0;
    return 1;
}

/* ---- catalogs ------------------------------------------------------------------------ */

typedef struct Entry {
    const char *key;     /* "context\004english" or "english" */
    const char *text;
    uint32_t hash;
} Entry;

struct Pc5Catalog {
    Entry *slots;
    uint32_t mask;       /* slots - 1, a power of two less one */
    int count;
    char *arena;         /* the keys and texts */
    size_t arena_len, arena_cap;
};

static uint32_t hash_of(const char *context, const char *text)
{
    uint32_t h = 2166136261u;
    if (context && context[0]) {
        for (const unsigned char *p = (const unsigned char *)context; *p; p++) h = (h ^ *p) * 16777619u;
        h = (h ^ 4u) * 16777619u;
    }
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) h = (h ^ *p) * 16777619u;
    return h;
}

static int key_is(const char *key, const char *context, const char *text)
{
    if (context && context[0]) {
        size_t n = strlen(context);
        if (strncmp(key, context, n) || key[n] != 4) return 0;
        key += n + 1;
    } else if (strchr(key, 4)) {
        return 0;
    }
    return !strcmp(key, text);
}

const char *pc5_catalog_get(const Pc5Catalog *catalog, const char *context, const char *english)
{
    if (!catalog || !catalog->count || !english) return NULL;
    uint32_t h = hash_of(context, english);
    for (uint32_t at = h & catalog->mask;; at = (at + 1) & catalog->mask) {
        const Entry *e = &catalog->slots[at];
        if (!e->key) return NULL;
        if (e->hash == h && key_is(e->key, context, english)) return e->text;
    }
}

int pc5_catalog_size(const Pc5Catalog *catalog) { return catalog ? catalog->count : 0; }

void pc5_catalog_free(Pc5Catalog *catalog)
{
    if (!catalog) return;
    pc5_free(catalog->slots);
    pc5_free(catalog->arena);
    pc5_free(catalog);
}


static int arena_put(Pc5Catalog *catalog, const char *data, size_t n)
{
    if (catalog->arena_len + n + 1 > catalog->arena_cap) {
        size_t cap = catalog->arena_cap ? catalog->arena_cap * 2 : 4096;
        while (cap < catalog->arena_len + n + 1) cap *= 2;
        char *arena = pc5_realloc(catalog->arena, cap);
        if (!arena) return -1;
        catalog->arena = arena;
        catalog->arena_cap = cap;
    }
    memcpy(catalog->arena + catalog->arena_len, data, n);
    catalog->arena_len += n;
    catalog->arena[catalog->arena_len] = 0;
    return 0;
}

/* The quoted string at *at ("..." with C's escapes) added to the arena's end. 0, -1 when it
 * isn't one, -2 when out of memory. */
static int string_add(Pc5Catalog *catalog, const char **at, const char *end)
{
    const char *p = *at;
    if (p >= end || *p != '"') return -1;
    for (p++; p < end && *p != '"';) {
        char c = *p++;
        if (c == '\\') {
            if (p >= end) return -1;
            char e = *p++;
            switch (e) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case 'a': c = '\a'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'v': c = '\v'; break;
            case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
                int v = e - '0';
                for (int k = 0; k < 2 && p < end && *p >= '0' && *p <= '7'; k++) v = v * 8 + (*p++ - '0');
                c = (char)v;
                break;
            }
            default: c = e; break;   /* \" \\ \' \? as they are */
            }
            if (!c) return -1;
        }
        if (arena_put(catalog, &c, 1)) return -2;
    }
    if (p >= end) return -1;
    for (p++; p < end && (*p == ' ' || *p == '\t' || *p == '\r'); p++) {
    }
    if (p < end) return -1;   /* something after the string */
    *at = p;
    return 0;
}

/* The arena's offsets of an entry's parts as it is read (-1: not there). */
typedef struct Reading {
    long context, id, plural, text;
    long *into;           /* the part a line of only a string continues */
    int fuzzy, obsolete, line;
} Reading;

typedef struct Pending {
    long key, text;
} Pending;

typedef struct Parse {
    Pc5Catalog *catalog;
    Pending *pending;
    size_t count, cap;
    Pc5I18nLog log;
    void *context;
    int rejected, failed;
} Parse;

static void parse_say(Parse *parse, int line, const char *what, const char *english)
{
    char text[400];
    if (!parse->log) return;
    snprintf(text, sizeof(text), "PC5 language line %d: %s%s%.200s%s", line, what, english ? " \"" : "",
             english ? english : "", english ? "\"" : "");
    parse->log(parse->context, text);
}

/* The entry read, kept when it has a translation that can be used. */
static void entry_end(Parse *parse, Reading *r)
{
    Pc5Catalog *catalog = parse->catalog;
    if (r->id >= 0 && !parse->failed) {
        const char *id = catalog->arena + r->id, *msgstr = r->text >= 0 ? catalog->arena + r->text : "";
        if (r->fuzzy || r->obsolete || r->plural >= 0 || !id[0] || !msgstr[0]) {
            /* the header, untranslated, fuzzy, plural or obsolete: English */
        } else if (strchr(id, '%') && !pc5_format_matches(id, msgstr)) {
            /* Only text with a conversion is ever a format: the rest is shown as it is. */
            parse->rejected++;
            parse_say(parse, r->line, "left out, its % conversions aren't the English's:", id);
        } else {
            long key = r->id;
            if (r->context >= 0 && catalog->arena[r->context]) {
                /* "context\004english", as gettext keys it */
                size_t cn = strlen(catalog->arena + r->context), in = strlen(id);
                char *joined = pc5_alloc(cn + in + 2);
                if (!joined) goto out_of_memory;
                memcpy(joined, catalog->arena + r->context, cn);
                joined[cn] = 4;
                memcpy(joined + cn + 1, id, in);
                if (arena_put(catalog, "", 1)) { pc5_free(joined); goto out_of_memory; }
                key = (long)catalog->arena_len;
                int failed = arena_put(catalog, joined, cn + 1 + in);
                pc5_free(joined);
                if (failed) goto out_of_memory;
            }
            if (parse->count == parse->cap) {
                size_t cap = parse->cap ? parse->cap * 2 : 256;
                Pending *more = pc5_realloc(parse->pending, cap * sizeof(*more));
                if (!more) goto out_of_memory;
                parse->pending = more;
                parse->cap = cap;
            }
            parse->pending[parse->count++] = (Pending){ key, r->text };
        }
    }
    *r = (Reading){ -1, -1, -1, -1, NULL, 0, 0, 0 };
    return;
out_of_memory:
    parse->failed = 1;
}

static int catalog_add(Pc5Catalog *catalog, const char *key, const char *text)
{
    if ((uint32_t)(catalog->count + 1) * 2 > catalog->mask + 1) {
        uint32_t size = (catalog->mask + 1) * 2;
        Entry *slots = pc5_alloc(sizeof(Entry) * size);
        if (!slots) return -1;
        for (uint32_t i = 0; i <= catalog->mask; i++) {
            Entry *e = &catalog->slots[i];
            if (!e->key) continue;
            uint32_t at = e->hash & (size - 1);
            while (slots[at].key) at = (at + 1) & (size - 1);
            slots[at] = *e;
        }
        pc5_free(catalog->slots);
        catalog->slots = slots;
        catalog->mask = size - 1;
    }
    const char *sep = strchr(key, 4);
    uint32_t h;
    if (sep) {
        char context[64];
        size_t n = (size_t)(sep - key) < sizeof(context) - 1 ? (size_t)(sep - key) : sizeof(context) - 1;
        memcpy(context, key, n);
        context[n] = 0;
        h = hash_of(context, sep + 1);
    } else {
        h = hash_of(NULL, key);
    }
    uint32_t at = h & catalog->mask;
    for (; catalog->slots[at].key; at = (at + 1) & catalog->mask)
        if (catalog->slots[at].hash == h && !strcmp(catalog->slots[at].key, key)) {
            catalog->slots[at].text = text;   /* a duplicate: the last one counts */
            return 0;
        }
    catalog->slots[at] = (Entry){ key, text, h };
    catalog->count++;
    return 0;
}

Pc5Catalog *pc5_catalog_parse(const char *text, size_t length, Pc5I18nLog log, void *context, int *rejected)
{
    Parse parse = { pc5_alloc(sizeof(Pc5Catalog)), NULL, 0, 0, log, context, 0, 0 };
    Pc5Catalog *catalog = parse.catalog;
    Reading r = { -1, -1, -1, -1, NULL, 0, 0, 0 };
    const char *p = text, *end = text + length;
    int line = 0;

    if (rejected) *rejected = 0;
    if (!catalog) return NULL;
    catalog->slots = pc5_alloc(sizeof(Entry) * 64);
    catalog->mask = 63;
    /* Offset 0 is an empty text, so no part is ever at 0 by mistake. */
    if (!catalog->slots || arena_put(catalog, "", 1)) parse.failed = 1;
    /* A UTF-8 byte order mark, as some editors write. */
    if (length >= 3 && !memcmp(p, "\xef\xbb\xbf", 3)) p += 3;

    while (p < end && !parse.failed) {
        const char *eol = memchr(p, '\n', (size_t)(end - p)), *stop = eol ? eol : end, *q = p;
        line++;
        p = eol ? eol + 1 : end;
        while (q < stop && (*q == ' ' || *q == '\t' || *q == '\r')) q++;
        if (q >= stop) {                     /* a blank line ends an entry */
            entry_end(&parse, &r);
            continue;
        }
        if (*q == '#') {
            /* Comments come before their entry: one after a msgstr starts the next. */
            if (r.id >= 0) entry_end(&parse, &r);
            if (stop - q >= 2 && q[1] == ',') {
                for (const char *f = q + 2; f + 5 <= stop; f++)
                    if (!strncmp(f, "fuzzy", 5)) r.fuzzy = 1;
            }
            if (stop - q >= 2 && q[1] == '~') r.obsolete = 1;
            r.into = NULL;
            continue;
        }
        long *slot = NULL;
        int keyword = 1;
        if (*q == '"') {
            keyword = 0;
            slot = r.into;
            if (!slot) {
                parse_say(&parse, line, "a string that continues nothing", NULL);
                continue;
            }
        } else if (!strncmp(q, "msgctxt", 7) && (q[7] == ' ' || q[7] == '\t')) {
            if (r.id >= 0) entry_end(&parse, &r);
            slot = &r.context;
            q += 7;
        } else if (!strncmp(q, "msgid_plural", 12) && (q[12] == ' ' || q[12] == '\t')) {
            slot = &r.plural;
            q += 12;
        } else if (!strncmp(q, "msgid", 5) && (q[5] == ' ' || q[5] == '\t')) {
            if (r.id >= 0) entry_end(&parse, &r);
            slot = &r.id;
            q += 5;
        } else if (!strncmp(q, "msgstr[", 7)) {
            int form = 0;
            for (q += 7; q < stop && *q >= '0' && *q <= '9'; q++) form = form * 10 + (*q - '0');
            if (q < stop && *q == ']') q++;
            /* Plural entries are English (PC5 has none): their forms are read and not kept. */
            slot = form ? NULL : &r.text;
            if (!slot) { r.into = NULL; continue; }
        } else if (!strncmp(q, "msgstr", 6) && (q[6] == ' ' || q[6] == '\t')) {
            slot = &r.text;
            q += 6;
        } else {
            parse_say(&parse, line, "not understood", NULL);
            r.into = NULL;
            continue;
        }
        while (q < stop && (*q == ' ' || *q == '\t')) q++;
        if (keyword) {
            /* A part's text starts after a 0, at the arena's end, and its continuations follow it:
             * a part is always the last thing added while it is read. */
            if (arena_put(catalog, "", 1)) { parse.failed = 1; break; }
            *slot = (long)catalog->arena_len;
            r.into = slot;
            if (!r.line) r.line = line;
        }
        size_t before = catalog->arena_len;
        int got = string_add(catalog, &q, stop);
        if (got == -2) { parse.failed = 1; break; }
        if (got) {
            catalog->arena_len = before;
            catalog->arena[before] = 0;
            parse_say(&parse, line, "not a quoted string", NULL);
        }
    }
    entry_end(&parse, &r);
    for (size_t i = 0; i < parse.count && !parse.failed; i++)
        parse.failed = catalog_add(catalog, catalog->arena + parse.pending[i].key, catalog->arena + parse.pending[i].text) != 0;
    pc5_free(parse.pending);
    if (parse.failed) {
        pc5_catalog_free(catalog);
        return NULL;
    }
    if (rejected) *rejected = parse.rejected;
    return catalog;
}

/* ---- the language in use --------------------------------------------------------------- */

typedef struct Loaded {
    char code[16];
    Pc5Catalog *own, *fallback;
    int strings;
} Loaded;

static Loaded loaded[LANGUAGES];
static int loaded_count;
static Loaded *_Atomic active;

static const char *lookup(const Loaded *in, const char *context, const char *english)
{
    const char *text = pc5_catalog_get(in->own, context, english);
    if (!text) text = pc5_catalog_get(in->fallback, context, english);
    return text ? text : english;
}

const char *pc5_tr(const char *english)
{
    Loaded *in = atomic_load_explicit(&active, memory_order_acquire);
    return in && english && english[0] ? lookup(in, NULL, english) : english;
}

const char *pc5_trc(const char *context, const char *english)
{
    Loaded *in = atomic_load_explicit(&active, memory_order_acquire);
    return in && english && english[0] ? lookup(in, context, english) : english;
}

static Pc5Catalog *catalog_read(const char *dir, const char *code, Pc5I18nLog log, void *context, int *failed)
{
    char path[PC5_PATH], line[PC5_PATH + 80];
    Pc5Buf text = { 0 };
    int rejected = 0;
    Pc5Catalog *catalog = NULL;

    if (pc5_format(path, sizeof(path), "%s/%s.po", dir, code) || pc5_read_file(path, &text, 8u * 1024 * 1024)) {
        pc5_buf_free(&text);
        if (failed) *failed = 1;
        snprintf(line, sizeof(line), "PC5 language %s: no %s", code, path);
        if (log) log(context, line);
        return NULL;
    }
    catalog = pc5_catalog_parse((const char *)text.data, text.len, log, context, &rejected);
    pc5_buf_free(&text);
    snprintf(line, sizeof(line), "PC5 language %s: %d translated, %d left out for their format%s", code,
             pc5_catalog_size(catalog), rejected, catalog ? "" : " (out of memory)");
    if (log) log(context, line);
    return catalog;
}

int pc5_i18n_use(const char *dir, const char *code, Pc5I18nLog log, void *context)
{
    int at = pc5_language_find(code), failed = 0;
    if (at <= 0) {   /* English, or a code PC5 hasn't */
        atomic_store_explicit(&active, NULL, memory_order_release);
        return at == 0 || !code ? 0 : -1;
    }
    const Pc5Language *language = &languages[at];
    for (int i = 0; i < loaded_count; i++)
        if (!strcmp(loaded[i].code, language->code)) {
            atomic_store_explicit(&active, &loaded[i], memory_order_release);
            return loaded[i].strings;
        }
    if (loaded_count == LANGUAGES) return -1;
    Loaded *in = &loaded[loaded_count];
    memset(in, 0, sizeof(*in));
    pc5_copy(in->code, sizeof(in->code), language->code);
    in->own = catalog_read(dir, language->code, log, context, &failed);
    if (language->fallback) in->fallback = catalog_read(dir, language->fallback, log, context, NULL);
    in->strings = pc5_catalog_size(in->own);
    loaded_count++;
    atomic_store_explicit(&active, in, memory_order_release);
    return failed ? -1 : in->strings;
}

const char *pc5_i18n_code(void)
{
    Loaded *in = atomic_load_explicit(&active, memory_order_acquire);
    return in ? in->code : "en";
}

void pc5_i18n_end(void)
{
    atomic_store_explicit(&active, NULL, memory_order_release);
    for (int i = 0; i < loaded_count; i++) {
        pc5_catalog_free(loaded[i].own);
        pc5_catalog_free(loaded[i].fallback);
    }
    memset(loaded, 0, sizeof(loaded));
    loaded_count = 0;
}

/* ---- formatting ------------------------------------------------------------------------ */

typedef struct Arg {
    int kind;
    union {
        int i;
        long l;
        long long ll;
        size_t z;
        intmax_t j;
        ptrdiff_t t;
        double d;
        long double ld;
        const char *s;
        void *p;
    } v;
} Arg;

/* One conversion of the translation, without its position, with its argument. */
static int put_one(char *out, size_t size, const Conv *conv, const Arg *arg)
{
    char spec[48];
    size_t n = 0;
    const char *p = conv->start + 1;
    spec[n++] = '%';
    while (*p >= '0' && *p <= '9') p++;   /* the position, then its '$' */
    p++;
    for (; p < conv->end && n < sizeof(spec) - 1; p++) spec[n++] = *p;
    spec[n] = 0;
    switch (arg->kind) {
    case K_INT: return snprintf(out, size, spec, arg->v.i);
    case K_LONG: return snprintf(out, size, spec, arg->v.l);
    case K_LLONG: return snprintf(out, size, spec, arg->v.ll);
    case K_SIZE: return snprintf(out, size, spec, arg->v.z);
    case K_INTMAX: return snprintf(out, size, spec, arg->v.j);
    case K_PTRDIFF: return snprintf(out, size, spec, arg->v.t);
    case K_DOUBLE: return snprintf(out, size, spec, arg->v.d);
    case K_LDOUBLE: return snprintf(out, size, spec, arg->v.ld);
    case K_STRING: return snprintf(out, size, spec, arg->v.s ? arg->v.s : "(null)");
    case K_POINTER: return snprintf(out, size, spec, arg->v.p);
    default: return -1;
    }
}

int pc5_vtf(char *out, size_t size, const char *format, va_list args)
{
    const char *text = pc5_tr(format);
    Conv want[MAX_ARGS];
    Arg values[MAX_ARGS];
    int wanted;
    size_t length = 0;

    /* English with no conversion: the translation as it is, whatever '%' it has. */
    if (text != format && !strchr(format, '%')) return snprintf(out, size, "%s", text);
    /* In order (or English): the C library's own. */
    if (text == format || !strchr(text, '$')) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
        return vsnprintf(out, size, text, args);
#pragma GCC diagnostic pop
    }
    {
        Conv first[MAX_ARGS];
        int have = convs_of(text, first, MAX_ARGS);
        if (have <= 0 || !first[0].position) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
            return vsnprintf(out, size, text, args);
#pragma GCC diagnostic pop
        }
    }
    wanted = convs_of(format, want, MAX_ARGS);
    if (wanted < 0) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
        return vsnprintf(out, size, format, args);
#pragma GCC diagnostic pop
    }
    for (int i = 0; i < wanted; i++) {
        values[i].kind = want[i].kind;
        switch (want[i].kind) {
        case K_INT: values[i].v.i = va_arg(args, int); break;
        case K_LONG: values[i].v.l = va_arg(args, long); break;
        case K_LLONG: values[i].v.ll = va_arg(args, long long); break;
        case K_SIZE: values[i].v.z = va_arg(args, size_t); break;
        case K_INTMAX: values[i].v.j = va_arg(args, intmax_t); break;
        case K_PTRDIFF: values[i].v.t = va_arg(args, ptrdiff_t); break;
        case K_DOUBLE: values[i].v.d = va_arg(args, double); break;
        case K_LDOUBLE: values[i].v.ld = va_arg(args, long double); break;
        case K_STRING: values[i].v.s = va_arg(args, const char *); break;
        default: values[i].v.p = va_arg(args, void *); break;
        }
    }
    if (size) out[0] = 0;
    for (const char *p = text; *p;) {
        if (*p != '%') {
            if (length + 1 < size) out[length] = *p, out[length + 1] = 0;
            length++;
            p++;
            continue;
        }
        Conv conv;
        if (conv_read(p, &conv)) break;   /* never: the text was checked as it was read */
        int got;
        if (conv.kind < 0) {
            if (length + 1 < size) out[length] = '%', out[length + 1] = 0;
            got = 1;
        } else {
            got = put_one(length < size ? out + length : NULL, length < size ? size - length : 0, &conv,
                          &values[conv.position - 1]);
            if (got < 0) return -1;
        }
        length += (size_t)got;
        p = conv.end;
    }
    if (size && length >= size) out[size - 1] = 0;
    return (int)length;
}

int pc5_tf(char *out, size_t size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int length = pc5_vtf(out, size, format, args);
    va_end(args);
    return length;
}

/* ---- text made from formats ---------------------------------------------------------- */

enum { MAX_MATCH_STEPS = 20000 };

typedef struct Match {
    const char *start[MAX_ARGS], *end[MAX_ARGS];
    int steps;
} Match;

/* Whether text (to its end) is what format makes, each conversion's text into m (by its
 * number in the format). Numbers take digits and signs, strings anything (the least first). */
static int match_at(const char *f, const char *t, int index, Match *m)
{
    if (++m->steps > MAX_MATCH_STEPS) return 0;
    while (*f && *f != '%') {
        if (*f != *t) return 0;
        f++;
        t++;
    }
    if (!*f) return !*t;
    Conv conv;
    if (conv_read(f, &conv)) return 0;
    if (conv.kind < 0) return *t == '%' && match_at(conv.end, t + 1, index, m);
    if (index >= MAX_ARGS) return 0;
    if (conv.kind == K_STRING) {
        for (const char *e = t;; e++) {
            m->start[index] = t;
            m->end[index] = e;
            if (match_at(conv.end, e, index + 1, m)) return 1;
            if (!*e || m->steps > MAX_MATCH_STEPS) return 0;
        }
    }
    /* A number: digits, a sign, a point or a comma, hex digits for %x (at least one). */
    const char *e = t;
    char letter = conv.end[-1];
    while (*e && ((*e >= '0' && *e <= '9') || *e == '-' || *e == '+' || *e == '.' ||
                  ((letter == 'x' || letter == 'X' || letter == 'p') && strchr("abcdefABCDEFx", *e))))
        e++;
    for (; e > t; e--) {
        m->start[index] = t;
        m->end[index] = e;
        if (match_at(conv.end, e, index + 1, m)) return 1;
    }
    return 0;
}

/* The words of a format (what isn't a conversion). */
static int words_of(const char *format)
{
    int n = 0;
    for (const char *p = format; *p;) {
        Conv conv;
        if (*p == '%' && !conv_read(p, &conv)) { p = conv.end; continue; }
        n += *p != ' ';
        p++;
    }
    return n;
}

static void put(char *out, size_t size, size_t *length, const char *from, size_t n)
{
    for (size_t i = 0; i < n; i++, (*length)++)
        if (*length + 1 < size) out[*length] = from[i];
    if (size) out[*length + 1 < size ? *length : size - 1] = 0;
}

/* The best format of a catalog that text is made from: its English, or NULL. */
static const Entry *best_format(const Pc5Catalog *catalog, const char *text, Match *best, int *best_words)
{
    const Entry *found = NULL;
    if (!catalog) return NULL;
    for (uint32_t i = 0; i <= catalog->mask; i++) {
        const Entry *e = &catalog->slots[i];
        if (!e->key || strchr(e->key, 4) || !strchr(e->key, '%')) continue;
        int words = words_of(e->key);
        /* Enough words that it can't be a mistake ("%s: %s" isn't a sentence). */
        if (words < 6 || words <= *best_words) continue;
        Match m;
        memset(&m, 0, sizeof(m));
        if (match_at(e->key, text, 0, &m)) {
            *best = m;
            *best_words = words;
            found = e;
        }
    }
    return found;
}

const char *pc5_tr_text(const char *text, char *out, size_t size)
{
    Loaded *in = atomic_load_explicit(&active, memory_order_acquire);
    Match m;
    int words = 0;
    size_t length = 0;
    const Entry *e;

    if (!in || !text || !text[0] || !size) return text;
    {
        const char *exact = pc5_catalog_get(in->own, NULL, text);
        if (!exact) exact = pc5_catalog_get(in->fallback, NULL, text);
        if (exact) return exact;
    }
    e = best_format(in->own, text, &m, &words);
    {
        const Entry *other = best_format(in->fallback, text, &m, &words);
        if (other) e = other;
    }
    if (!e) return text;
    out[0] = 0;
    int next = 0;
    for (const char *p = e->text; *p;) {
        Conv conv;
        if (*p != '%' || conv_read(p, &conv)) {
            put(out, size, &length, p, 1);
            p++;
            continue;
        }
        if (conv.kind < 0) {
            put(out, size, &length, "%", 1);
        } else {
            int at = conv.position ? conv.position - 1 : next++;
            if (at >= 0 && at < MAX_ARGS && m.start[at]) {
                /* A value that is itself a text PC5 has (a reason in a sentence): translated too. */
                char piece[PC5_ERROR];
                size_t n = (size_t)(m.end[at] - m.start[at]);
                if (n < sizeof(piece)) {
                    memcpy(piece, m.start[at], n);
                    piece[n] = 0;
                    const char *own = pc5_catalog_get(in->own, NULL, piece);
                    if (!own) own = pc5_catalog_get(in->fallback, NULL, piece);
                    if (own) put(out, size, &length, own, strlen(own));
                    else put(out, size, &length, piece, n);
                } else {
                    put(out, size, &length, m.start[at], n);
                }
            }
        }
        p = conv.end;
    }
    return out;
}

const char *pc5_trx(const char *text)
{
    static char ring[8][1024];
    static int turn;
    char *out = ring[turn];
    turn = (turn + 1) % 8;
    return pc5_tr_text(text, out, sizeof(ring[0]));
}

/* ---- capitals ------------------------------------------------------------------------------ */

static uint32_t upper_of(uint32_t c, int turkish)
{
    if (c >= 'a' && c <= 'z') return turkish && c == 'i' ? 0x130 : c - 32;
    if (c < 0x80) return c;
    if ((c >= 0xe0 && c <= 0xfe && c != 0xf7)) return c - 32;
    if (c == 0xff) return 0x178;
    if (c >= 0x100 && c <= 0x17f) {
        if (c == 0x131) return 'I';
        if (c == 0x138 || c == 0x149 || c == 0x17f) return c;
        if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17e)) return c & 1 ? c : c - 1;
        return c & 1 ? c - 1 : c;
    }
    if (c == 0x219 || c == 0x21b) return c - 1;   /* ș ț */
    /* Greek: capitals without their accents. */
    switch (c) {
    case 0x3ac: return 0x391;
    case 0x3ad: return 0x395;
    case 0x3ae: return 0x397;
    case 0x3af: return 0x399;
    case 0x3cc: return 0x39f;
    case 0x3cd: return 0x3a5;
    case 0x3ce: return 0x3a9;
    case 0x390: return 0x3aa;
    case 0x3b0: return 0x3ab;
    case 0x3c2: return 0x3a3;
    case 0x386: return 0x391;
    case 0x388: return 0x395;
    case 0x389: return 0x397;
    case 0x38a: return 0x399;
    case 0x38c: return 0x39f;
    case 0x38e: return 0x3a5;
    case 0x38f: return 0x3a9;
    default: break;
    }
    if (c >= 0x3b1 && c <= 0x3cb) return c - 32;
    if (c >= 0x430 && c <= 0x44f) return c - 32;
    if (c >= 0x450 && c <= 0x45f) return c - 80;
    if (c >= 0x460 && c <= 0x4ff && c != 0x482 && !(c >= 0x483 && c <= 0x489)) {
        if (c >= 0x4c1 && c <= 0x4ce) return c & 1 ? c : c - 1;
        if (c == 0x4cf) return 0x4c0;
        return c & 1 ? c - 1 : c;
    }
    if (c >= 0x1e00 && c <= 0x1eff && !(c >= 0x1e96 && c <= 0x1e9f)) return c & 1 ? c - 1 : c;
    return c;
}

void pc5_upper(const char *text, char *out, size_t size)
{
    const unsigned char *p = (const unsigned char *)text;
    int turkish = !strcmp(pc5_i18n_code(), "tr");
    size_t n = 0;

    if (!size) return;
    while (*p) {
        uint32_t c = *p;
        int extra = c < 0x80 ? 0 : c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
        if (extra) c &= extra == 1 ? 0x1f : extra == 2 ? 0x0f : 0x07;
        p++;
        for (int k = 0; k < extra && (*p & 0xc0) == 0x80; k++) c = c << 6 | (*p++ & 0x3f);
        c = upper_of(c, turkish);
        char bytes[4];
        int length = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
        if (length == 1) bytes[0] = (char)c;
        else if (length == 2) bytes[0] = (char)(0xc0 | c >> 6), bytes[1] = (char)(0x80 | (c & 0x3f));
        else if (length == 3)
            bytes[0] = (char)(0xe0 | c >> 12), bytes[1] = (char)(0x80 | ((c >> 6) & 0x3f)), bytes[2] = (char)(0x80 | (c & 0x3f));
        else
            bytes[0] = (char)(0xf0 | c >> 18), bytes[1] = (char)(0x80 | ((c >> 12) & 0x3f)),
            bytes[2] = (char)(0x80 | ((c >> 6) & 0x3f)), bytes[3] = (char)(0x80 | (c & 0x3f));
        if (n + (size_t)length + 1 > size) break;
        memcpy(out + n, bytes, (size_t)length);
        n += (size_t)length;
    }
    out[n] = 0;
}

/* ---- the web page's text ----------------------------------------------------------------- */

static int json_string(Pc5Buf *out, const char *text)
{
    if (pc5_buf_puts(out, "\"")) return -1;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        int failed;
        if (*p == '"' || *p == '\\') failed = pc5_buf_printf(out, "\\%c", *p);
        else if (*p == '\n') failed = pc5_buf_puts(out, "\\n");
        else if (*p < 0x20 || *p == 0x7f) failed = pc5_buf_printf(out, "\\u%04x", *p);
        else if (*p == '<') failed = pc5_buf_puts(out, "\\u003c");   /* never "</script>" */
        else failed = pc5_buf_put(out, p, 1);
        if (failed) return -1;
    }
    return pc5_buf_puts(out, "\"");
}

static int web_entries(Pc5Buf *out, const Pc5Catalog *catalog, const Pc5Catalog *over, int *first)
{
    if (!catalog) return 0;
    for (uint32_t i = 0; i <= catalog->mask; i++) {
        const Entry *e = &catalog->slots[i];
        if (!e->key || strncmp(e->key, "web\004", 4)) continue;
        /* The language's own over its fallback's. */
        if (over && pc5_catalog_get(over, "web", e->key + 4)) continue;
        if ((!*first && pc5_buf_puts(out, ",")) || json_string(out, e->key + 4) || pc5_buf_puts(out, ":") ||
            json_string(out, e->text))
            return -1;
        *first = 0;
    }
    return 0;
}

int pc5_i18n_web_json(Pc5Buf *out)
{
    Loaded *in = atomic_load_explicit(&active, memory_order_acquire);
    int first = 1;
    if (pc5_buf_puts(out, "{\"lang\":") || json_string(out, in ? in->code : "en") || pc5_buf_puts(out, ",\"strings\":{"))
        return -1;
    if (in && (web_entries(out, in->own, NULL, &first) || web_entries(out, in->fallback, in->own, &first))) return -1;
    return pc5_buf_puts(out, "}}");
}
