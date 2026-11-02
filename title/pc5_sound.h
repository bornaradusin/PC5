/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: the startup sounds, played on the controller's speaker (16-bit mono at
 * 48 kHz) when PC5 is opened. The app brings its own (pc5/sounds/<name>.wav);
 * the player can add more as root/sounds/<name>.wav. Settings › In games ›
 * Startup sound chooses one by name, or Off. */
#ifndef PC5_SOUND_H
#define PC5_SOUND_H
#include "pc5.h"
#include <stdio.h>
#include <stdatomic.h>

/* Import MP3 or PCM16 WAV on a worker, resampling to a local 48 kHz stereo
 * WAV. Only a completed, closed file is renamed over destination. */
typedef struct Pc5MusicImport {
    pthread_t thread;
    atomic_int stop, done;
    int running, result;
    char source[PC5_PATH], destination[PC5_PATH];
} Pc5MusicImport;
enum { PC5_MUSIC_IMPORTED = 0, PC5_MUSIC_BAD_FORMAT = -1,
       PC5_MUSIC_IO_ERROR = -2, PC5_MUSIC_TOO_LARGE = -3, PC5_MUSIC_CANCELLED = -4 };
int pc5_music_import_start(Pc5MusicImport *job, const char *source, const char *destination);
void pc5_music_import_end(Pc5MusicImport *job);

/* Library music streams bounded PCM WAV data instead of loading a whole song.
 * 48 kHz, signed 16-bit PCM, mono or stereo. Output is always stereo. */
typedef struct Pc5Music {
    FILE *file;
    long start;
    uint32_t bytes, left;
    int channels;
} Pc5Music;
int pc5_music_open(const char *path, Pc5Music *music);
size_t pc5_music_read(Pc5Music *music, int16_t *stereo, size_t frames);
int pc5_music_start_at(Pc5Music *music, unsigned seconds);
void pc5_music_close(Pc5Music *music);

enum {
    PC5_SOUND_RATE = 48000,
    PC5_SOUND_SECONDS = 30,      /* longer sounds are cut */
    PC5_SOUNDS = 32,             /* names in a list */
    PC5_SOUND_NAME = 64,
};
#define PC5_SOUND_DEFAULT "cheap chinese speaker"

typedef struct Pc5Sound {
    int16_t *samples;        /* mono, at PC5_SOUND_RATE */
    size_t frames;
} Pc5Sound;

/* A WAV file's samples: PCM of 8, 16 or 24 bits or 32-bit float, mono or stereo (mixed
 * to one), at any rate (made 48 kHz). 0, or -1 for anything else. */
int pc5_sound_from_wav(const uint8_t *data, size_t size, Pc5Sound *sound);
void pc5_sound_free(Pc5Sound *sound);

typedef struct Pc5SoundList {
    char name[PC5_SOUNDS][PC5_SOUND_NAME];
    int count;
} Pc5SoundList;

/* The sounds to choose from, by name (the file's, without .wav): the app's in app/pc5/sounds,
 * then the player's in root/sounds; sorted, each name once. */
void pc5_sound_list(const char *app, const char *root, Pc5SoundList *list);
/* A sound's file: the player's of that name, else the app's. 0, or -1 when there is none. */
int pc5_sound_path(const char *app, const char *root, const char *name, char *out, size_t size);

#endif
