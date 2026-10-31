/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: its screens and its web page in the player's language. Each language is a gettext
 * file in the app, pc5/lang/<code>.po (lang/ in the repo, made from lang/pc5.pot by
 * tools/update-po.py), which players can edit with Poedit or any text editor. The English
 * text is the key: pc5_tr("Settings") gives the language's msgstr, or the English itself
 * when there is none (an empty msgstr, a fuzzy one, or no file).
 *
 * Marking text for the template (tools/update-po.py finds these):
 *   T("...")                 text shown now
 *   N_("...")                text kept in a table, shown later through pc5_tr()
 *   pc5_tf(out, size, "...", ...)   a printf format, translated, then filled in
 * A comment starting with TRANSLATORS: just before the text goes into the template for the
 * translators (what it is, how much room there is).
 *
 * A translated format may put its values in another order with POSIX positions ("%2$s ...
 * %1$d"); pc5_tf reorders them itself, so it needs nothing from the C library. A
 * translation whose conversions aren't the English's (another type, one missing, an extra
 * one, %n) is left out as the file is read, and logged once. Not for logs, file names,
 * settings keys or anything written to a file: those stay English. */
#ifndef PC5_I18N_H
#define PC5_I18N_H
#include "pc5.h"

#include <stdarg.h>

#define T(text) pc5_tr(text)
#define N_(text) text

/* The text in the language chosen, or english itself. Fast: called many times a frame. */
const char *pc5_tr(const char *english);
/* The same with a msgctxt ("web" for the web page's text). */
const char *pc5_trc(const char *context, const char *english);
/* snprintf with a translated format (POSIX positions allowed in the translation). The
 * length the whole text needs, as snprintf gives it, or -1. */
int pc5_tf(char *out, size_t size, const char *format, ...) __attribute__((format(printf, 3, 4)));
int pc5_vtf(char *out, size_t size, const char *format, va_list args) __attribute__((format(printf, 3, 0)));

/* Text made elsewhere in English (an install's error, a folder's problem: what the logs keep
 * as it is), in the language chosen: its translation, or, for text made from a format marked
 * N_() ("%s needs %s free"), that format's translation with the same values. Exact text
 * first; a format matches only when its words are all there, the one with the most words
 * winning. text itself when nothing matches (or English is in use), else out. */
const char *pc5_tr_text(const char *text, char *out, size_t size);
/* The same into one of eight buffers of its own, taken in turn (the screens' thread only):
 * good until eight more calls. */
const char *pc5_trx(const char *text);
#define TX(text) pc5_trx(text)

/* text in capitals (UTF-8): Latin with its accents, Greek (without its accents, as Greek
 * capitals are written), Cyrillic and Vietnamese; Turkish's dotted İ while Turkish is in use.
 * Cut to fit out (always 0-terminated). */
void pc5_upper(const char *text, char *out, size_t size);

/* ---- the languages ------------------------------------------------------------------- */
typedef struct Pc5Language {
    const char *code;       /* "de", "pt-BR", "zh-Hans": the .po's name */
    const char *name;       /* in its own language: "Deutsch" */
    const char *fallback;   /* a language whose text is used where this one has none, or NULL */
    int cjk;                /* drawn with pc5/fonts-<code>.bin beside the main fonts */
} Pc5Language;

/* The languages PC5 offers (Settings › System › Language), English first. */
int pc5_language_count(void);
const Pc5Language *pc5_language(int index);
/* The index of a code (case doesn't matter), or -1. */
int pc5_language_find(const char *code);
/* The console's language (sceSystemServiceParamGetInt's SCE_SYSTEM_SERVICE_PARAM_ID_LANG, as
 * the PS4 and PS5 SDKs number them) as one of PC5's codes: "en" for English (US and UK) and for
 * the languages PC5 has no file for yet (Arabic, Thai), NULL for a number it doesn't know. */
const char *pc5_language_of_console(int ps5_language);
/* The code the setting means: setting ("" or "system": the console's, else its code) and the
 * console's code (NULL: English). Always one of pc5_language's codes. */
const char *pc5_language_choose(const char *setting, const char *console);

/* ---- a language's text ------------------------------------------------------------------ */
typedef struct Pc5Catalog Pc5Catalog;
typedef void (*Pc5I18nLog)(void *context, const char *line);

/* A .po file's text as a catalog (the translated entries; the header, fuzzy, plural and
 * obsolete ones, and those whose format doesn't match, are left out: each rejected one
 * logged). NULL when out of memory. rejected (may be NULL): how many were left out for
 * their format. */
Pc5Catalog *pc5_catalog_parse(const char *text, size_t length, Pc5I18nLog log, void *context, int *rejected);
/* The msgstr of (context, english), or NULL. context NULL or "": none. */
const char *pc5_catalog_get(const Pc5Catalog *catalog, const char *context, const char *english);
int pc5_catalog_size(const Pc5Catalog *catalog);
void pc5_catalog_free(Pc5Catalog *catalog);
/* 1 when translated has the conversions english has (positions allowed), 0 when not. */
int pc5_format_matches(const char *english, const char *translated);

/* Uses the language code from dir (<dir>/<code>.po, and its fallback's), from now on: "en",
 * NULL or a code PC5 hasn't: English. A language read once is kept until pc5_i18n_end (so
 * text another thread took from it stays good). The strings translated, or -1 when its file
 * couldn't be read (English then). */
int pc5_i18n_use(const char *dir, const char *code, Pc5I18nLog log, void *context);
/* The language in use ("en" for English). */
const char *pc5_i18n_code(void);
/* Back to English, every language read freed (as PC5 ends; tests). */
void pc5_i18n_end(void);
/* The web page's text in the language in use, as JSON for the page's /lang.json:
 * {"lang":"de","strings":{"English":"Deutsch",...}} (the entries with msgctxt "web"). */
int pc5_i18n_web_json(Pc5Buf *out);

#endif
