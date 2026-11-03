/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef PC5_BIDI_H
#define PC5_BIDI_H
#include <stddef.h>
enum { PC5_BIDI_BYTES = 8192 };
/* Logical UTF-8 to a visual, joined Arabic line. -1: no conversion needed or
 * invalid/oversized input. Source is never modified; output is always bounded.
 * Drawing and measurement use the same conversion, after line breaking. */
int pc5_bidi_visual(const char *text, size_t bytes, char *out, size_t capacity, int *rtl);
#endif
