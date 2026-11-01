/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: a text file to change a line at a time (the file manager's Edit) or as a whole (the web
 * page's). It is read into UTF-8 lines and written back as it was found: the same encoding
 * (UTF-8 with or without its mark, UTF-16 little-endian, or one byte a character), the same
 * line ends (\r\n or \n), and a line end after the last line where it had one. */
#ifndef PC5_TEXTDOC_H
#define PC5_TEXTDOC_H

#include "pc5.h"

enum {
    PC5_TEXTDOC_MAX = 8 * 1024 * 1024,   /* a file larger than this isn't opened */
    PC5_TEXTDOC_LINES = 400000,          /* nor one of more lines */
};

typedef enum Pc5TextEncoding {
    PC5_TEXT_UTF8,
    PC5_TEXT_UTF16LE,
    PC5_TEXT_LATIN1,   /* not UTF-8: a byte a character, as Windows' Western code page mostly is */
} Pc5TextEncoding;

typedef struct Pc5TextDoc {
    Pc5Buf text;              /* UTF-8, the lines joined by \n, without one after the last */
    uint32_t *starts;         /* where each line starts in text */
    int count, cap;           /* lines; an empty file has none */
    Pc5TextEncoding encoding;
    int bom;                  /* it started with the encoding's mark */
    int crlf;                 /* its lines end in \r\n */
    int final_newline;        /* the last line ends in one */
    int changed;
} Pc5TextDoc;

/* A file's bytes as lines. 0, or -1 with why (in English, for N_()) when it isn't text PC5 can
 * change: a 0 byte where text has none, UTF-16 big-endian, too large or too many lines. */
int pc5_textdoc_parse(Pc5TextDoc *doc, const uint8_t *data, size_t size, char *why, size_t why_size);
int pc5_textdoc_read(Pc5TextDoc *doc, const char *path, char *why, size_t why_size);
/* Line i (0 ..count-1), its length in *len; not 0-terminated. */
const char *pc5_textdoc_line(const Pc5TextDoc *doc, int i, size_t *len);
/* Line i as a C string into out, cut to fit; -1 when it didn't fit. */
int pc5_textdoc_copy_line(const Pc5TextDoc *doc, int i, char *out, size_t size);
/* Line i becomes text (which may hold \n: then it is several lines). 0, or -1. */
int pc5_textdoc_set(Pc5TextDoc *doc, int i, const char *text);
/* text as a new line before line at (count: after the last). */
int pc5_textdoc_insert(Pc5TextDoc *doc, int at, const char *text);
int pc5_textdoc_delete(Pc5TextDoc *doc, int i);
/* All of it becomes text, UTF-8 with \n or \r\n line ends (a browser's text box): the file's
 * encoding and line ends stay. A last line end in text says whether the file ends in one. */
int pc5_textdoc_replace(Pc5TextDoc *doc, const char *text, size_t size);
/* As the file has it: its encoding, mark and line ends. 0, or -1 with why when a character
 * can't be written in its encoding (one byte a character holds only U+0000-U+00FF). */
int pc5_textdoc_encode(const Pc5TextDoc *doc, Pc5Buf *out, char *why, size_t why_size);
/* Encoded and written in place of path (through path.part, renamed over it). */
int pc5_textdoc_write(const Pc5TextDoc *doc, const char *path, char *why, size_t why_size);
/* The whole text as UTF-8 with \n line ends and a last one where the file has it (for a
 * browser); a 0 byte follows. */
int pc5_textdoc_join(const Pc5TextDoc *doc, Pc5Buf *out);
void pc5_textdoc_free(Pc5TextDoc *doc);

#endif
