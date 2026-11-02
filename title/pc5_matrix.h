/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: what shows while a game starts, until its first picture: a gaming PC
 * glowing blue (art/loading.jpg, built in) or the player's own picture
 * (pc5_loading_own), and in its bottom right corner "LAUNCHING GAME" and a
 * circle spinning, no game's name. It doesn't say how far the start is: it spins until the game
 * shows. Drawn on the CPU into a 1280x720 B,G,R,X frame (the title shows it
 * scaled to the screen), a step every 33 ms. (Named for the falling code it
 * showed before 2.1.2.) */
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
/* The player's own picture in place of the built-in one: a .jpg or .png of any size, filled
 * into the frame (cut to 16:9 about its middle). 0 when it is used; the built-in one stays
 * otherwise. */
int pc5_matrix_picture(Pc5Matrix *matrix, const char *path);
/* The player's own loading picture for a game: the one chosen in its Appearance
 * (prefixes/<prefix>/pc5/loading.png, pc5_loading_chosen), else one in root/loading/ named as an
 * own cover is (the game's folder name, its name or its id), else loading.jpg/.jpeg/.png there for
 * every game. folder is the game's folder's name (or ""), title its name in PC5, id its id; any may
 * be NULL. 0 with its path in out, or -1 when there is none (PC5's own is shown). */
int pc5_loading_own(const char *root, const char *prefix, const char *folder, const char *title, const char *id, char *out,
                    size_t size);
/* The loading picture chosen for a game in its Appearance: 0 with its path in out when it has one. */
int pc5_loading_chosen(const char *root, const char *prefix, char *out, size_t size);
/* image as a loading picture, filled into the frame (cut to 16:9 about its middle), written to
 * path as a 1280x720 PNG in one go. 0 when written. */
int pc5_loading_save(const Pc5Image *image, const char *path);
/* Draws the next step; the frame stays valid until the next call. */
const uint32_t *pc5_matrix_step(Pc5Matrix *matrix);

#endif
