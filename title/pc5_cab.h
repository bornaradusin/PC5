/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: a Microsoft cabinet (.cab) read and its files handed out one after another, as a
 * Windows Installer package carries them (pc5_msi.h). Folders stored, MSZIP (deflate) or LZX,
 * as Microsoft's "Cabinet File Format" and "LZX DELTA Compression" documents describe them;
 * Quantum, and a cabinet that goes on in another one, are refused. Nothing is run. */
#ifndef PC5_CAB_H
#define PC5_CAB_H
#include "pc5.h"

/* The cabinet: size bytes, read with read (0, or -1). */
typedef struct Pc5CabIn {
    void *context;
    uint64_t size;
    int (*read)(void *context, uint64_t offset, void *out, size_t size);
} Pc5CabIn;

/* Where its files go. begin: a file starts (its name in the cabinet, its size): 1 to take its
 * bytes (write, then end), 0 to pass it by, -1 to stop. write: the next bytes of it, 0 or -1.
 * end: all of it given, 0 or -1. */
typedef struct Pc5CabOut {
    void *context;
    int (*begin)(void *context, const char *name, uint32_t size);
    int (*write)(void *context, const void *data, size_t size);
    int (*end)(void *context);
} Pc5CabOut;

/* Every file of the cabinet, in the order its bytes come (one whose bytes another file has too
 * after the others of its folder): how many were taken, or -1 with why in error. */
int pc5_cab_extract(const Pc5CabIn *in, const Pc5CabOut *out, char *error, size_t size);

/* For the tests: raw deflate (in, size) after dictionary (the bytes before it, may be NULL)
 * into out: the bytes made, or -1. */
long pc5_inflate(const uint8_t *in, size_t size, const uint8_t *dictionary, size_t dictionary_size, uint8_t *out,
                 size_t out_size);

#endif
