/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: what shows while a game starts, until its first picture: a gaming PC
 * glowing blue (art/loading.jpg, built in), "LAUNCHING GAME" under it and a
 * bar filling, no game's name. Drawn on the CPU into a 1280x720 B,G,R,X frame
 * (the title shows it scaled to the screen), a step every 33 ms. (Named for
 * the falling code it showed before 2.1.2.) */
#ifndef PC5_MATRIX_H
#define PC5_MATRIX_H
#include "pc5.h"
#include "pc5_draw.h"

enum { PC5_MATRIX_WIDTH = 1280, PC5_MATRIX_HEIGHT = 720, PC5_MATRIX_STEP_MS = 33 };

typedef struct Pc5Matrix Pc5Matrix;

/* The fonts are taken over (and freed with the matrix); title and seed aren't used
 * any more. NULL when out of memory. */
Pc5Matrix *pc5_matrix_create(Pc5Fonts *fonts, const char *title, uint32_t seed);
void pc5_matrix_destroy(Pc5Matrix *matrix);
/* Draws the next step; the frame stays valid until the next call. */
const uint32_t *pc5_matrix_step(Pc5Matrix *matrix);

#endif
