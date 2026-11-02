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
/* The same with fonts the caller keeps (PC5's own, while it hands the game over), shown at once
 * without fading in. NULL when out of memory. */
Pc5Matrix *pc5_matrix_borrow(const Pc5Fonts *fonts);
/* Shown at once, without fading in: PC5 showed it a moment ago, before the game's own process. */
void pc5_matrix_at_once(Pc5Matrix *matrix);
/* Where the start is, for the bar. Phase 0, "LAUNCHING GAME": PC5's part (the game made ready,
 * its Windows starting, its program and DLLs loaded), fill from 0 to 1. Phase 1, "LOADING": the
 * game's own loading once its program runs, fill from how long it took last time, or below 0
 * when that isn't known (a light moving along the bar, which doesn't fill). Without a call the
 * bar fills on time alone, as before 2.2.0. */
void pc5_matrix_progress(Pc5Matrix *matrix, int phase, float fill);
/* Draws the next step; the frame stays valid until the next call. */
const uint32_t *pc5_matrix_step(Pc5Matrix *matrix);

#endif
