/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: the startup sounds (pc5_sound.h). */
#include "pc5_sound.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

void pc5_music_close(Pc5Music *music)
{
    if (music->file) fclose(music->file);
    memset(music, 0, sizeof(*music));
}

static int music_open_rate(const char *path, Pc5Music *music, uint32_t *rate)
{
    uint8_t head[16];
    int format = 0;
    long size, end;
    memset(music, 0, sizeof(*music));
    music->file = fopen(path, "rb");
    if (!music->file) return -1;
    if (fseek(music->file, 0, SEEK_END) || (size = ftell(music->file)) < 12 ||
        fseek(music->file, 0, SEEK_SET) || fread(head, 1, 12, music->file) != 12 ||
        memcmp(head, "RIFF", 4) || memcmp(head + 8, "WAVE", 4)) goto bad;
    uint32_t riff = le32(head + 4);
    if (riff < 4 || (uint64_t)riff + 8 > (uint64_t)size || (uint64_t)riff + 8 > LONG_MAX) goto bad;
    end = (long)((uint64_t)riff + 8);
    for (long at = 12; at <= end - 8;) {
        if (fseek(music->file, at, SEEK_SET) || fread(head, 1, 8, music->file) != 8) goto bad;
        uint32_t bytes = le32(head + 4);
        long body = at + 8;
        if ((uint64_t)bytes + (bytes & 1u) > (uint64_t)(end - body)) goto bad;
        if (!memcmp(head, "fmt ", 4)) {
            if (format || bytes < 16 || fread(head, 1, 16, music->file) != 16) goto bad;
            music->channels = le16(head + 2);
            if (le16(head) != 1 || (music->channels != 1 && music->channels != 2) ||
                le32(head + 4) < 8000 || le32(head + 4) > 192000 || le16(head + 14) != 16 ||
                le16(head + 12) != music->channels * 2 ||
                le32(head + 8) != le32(head + 4) * (uint32_t)music->channels * 2) goto bad;
            *rate = le32(head + 4);
            format = 1;
        } else if (!memcmp(head, "data", 4) && !music->start) {
            music->start = body;
            music->bytes = bytes;
        }
        at = body + (long)bytes + (bytes & 1u);
    }
    if (!format || !music->start || !music->bytes || music->bytes % (music->channels * 2) ||
        fseek(music->file, music->start, SEEK_SET)) goto bad;
    music->left = music->bytes;
    return 0;
bad:
    pc5_music_close(music);
    return -1;
}

int pc5_music_open(const char *path, Pc5Music *music)
{
    uint32_t rate = 0;
    if (music_open_rate(path, music, &rate)) return -1;
    if (rate == 48000) return 0;
    pc5_music_close(music);
    return -1;
}

int pc5_music_start_at(Pc5Music *music, unsigned seconds)
{
    uint64_t offset = (uint64_t)seconds * 48000 * (unsigned)music->channels * 2;
    if (!music->file || offset >= music->bytes) return -1;
    if (fseek(music->file, music->start + (long)offset, SEEK_SET)) return -1;
    music->start += (long)offset;
    music->bytes -= (uint32_t)offset;
    music->left = music->bytes;
    return 0;
}

size_t pc5_music_read(Pc5Music *music, int16_t *stereo, size_t frames)
{
    if (!music->file) return 0;
    for (size_t i = 0; i < frames; i++) {
        uint8_t raw[4];
        size_t width = (size_t)music->channels * 2;
        if (!music->left) {
            if (fseek(music->file, music->start, SEEK_SET)) return i;
            music->left = music->bytes;
        }
        if (fread(raw, 1, width, music->file) != width) return i;
        music->left -= (uint32_t)width;
        stereo[i * 2] = (int16_t)le16(raw);
        stereo[i * 2 + 1] = (int16_t)le16(raw + (music->channels == 2 ? 2 : 0));
    }
    return frames;
}

/* One sample of a frame's channel, as 16 bits. */
static int sample(const uint8_t *p, int bits, int is_float)
{
    if (is_float) {
        uint32_t u = le32(p);
        float f;
        memcpy(&f, &u, sizeof(f));
        if (!(f == f)) return 0;   /* NaN */
        f = f > 1 ? 1 : f < -1 ? -1 : f;
        return (int)(f * 32767);
    }
    if (bits == 8) return ((int)p[0] - 128) * 256;
    if (bits == 16) return (int16_t)le16(p);
    return (int)((int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) >> 16);   /* 24 */
}

int pc5_sound_from_wav(const uint8_t *data, size_t size, Pc5Sound *sound)
{
    const uint8_t *samples = NULL;
    size_t bytes = 0;
    int channels = 0, bits = 0, is_float = 0, have_format = 0;
    uint32_t rate = 0;

    memset(sound, 0, sizeof(*sound));
    if (size < 12 || memcmp(data, "RIFF", 4) || memcmp(data + 8, "WAVE", 4)) return -1;
    /* Chunks: "fmt " says what the samples are, "data" holds them. */
    for (size_t at = 12; at + 8 <= size;) {
        uint32_t length = le32(data + at + 4);
        const uint8_t *body = data + at + 8;
        if (length > size - at - 8) {
            if (memcmp(data + at, "data", 4)) return -1;
            length = (uint32_t)(size - at - 8);   /* a file cut short: what is there */
        }
        if (!memcmp(data + at, "fmt ", 4)) {
            if (length < 16) return -1;
            uint16_t format = le16(body);
            /* WAVE_FORMAT_EXTENSIBLE carries the format's tag in its sub-format. */
            if (format == 0xfffe && length >= 26) format = le16(body + 24);
            channels = le16(body + 2);
            rate = le32(body + 4);
            bits = le16(body + 14);
            is_float = format == 3;
            if ((format != 1 && format != 3) || (is_float && bits != 32) ||
                (!is_float && bits != 8 && bits != 16 && bits != 24) || (channels != 1 && channels != 2) ||
                rate < 8000 || rate > 192000)
                return -1;
            have_format = 1;
        } else if (!memcmp(data + at, "data", 4) && !samples) {
            samples = body;
            bytes = length;
        }
        at += 8 + (size_t)length + (length & 1);
    }
    if (!have_format || !samples) return -1;
    size_t width = (size_t)(bits / 8), frame_bytes = width * (size_t)channels, in = bytes / frame_bytes;
    if (in > (size_t)rate * PC5_SOUND_SECONDS) in = (size_t)rate * PC5_SOUND_SECONDS;
    sound->frames = (size_t)((uint64_t)in * PC5_SOUND_RATE / rate);
    if (!in || !sound->frames || !(sound->samples = pc5_alloc(sound->frames * sizeof(int16_t)))) {
        sound->frames = 0;
        return -1;
    }
    /* Mixed to one channel, then to 48 kHz: each new sample between the two old ones around it. */
    for (size_t i = 0; i < sound->frames; i++) {
        uint64_t at = (uint64_t)i * rate * 256 / PC5_SOUND_RATE;   /* in 1/256ths of an old frame */
        size_t a = (size_t)(at >> 8), b = a + 1 < in ? a + 1 : a;
        int t = (int)(at & 255), va = 0, vb = 0;
        for (int c = 0; c < channels; c++) {
            va += sample(samples + a * frame_bytes + (size_t)c * width, bits, is_float);
            vb += sample(samples + b * frame_bytes + (size_t)c * width, bits, is_float);
        }
        int value = (va * (256 - t) + vb * t) / (256 * channels);
        sound->samples[i] = (int16_t)(value > 32767 ? 32767 : value < -32768 ? -32768 : value);
    }
    return 0;
}

void pc5_sound_free(Pc5Sound *sound)
{
    pc5_free(sound->samples);
    sound->samples = NULL;
    sound->frames = 0;
}

static int name_order(const void *a, const void *b)
{
    return pc5_casecmp((const char *)a, (const char *)b);
}

/* The .wav files in dir, by name, added to list when not there yet. */
static void list_in(const char *dir, Pc5SoundList *list)
{
    DIR *folder = opendir(dir);
    struct dirent *entry;

    if (!folder) return;
    while ((entry = readdir(folder)) && list->count < PC5_SOUNDS) {
        size_t n = strlen(entry->d_name);
        if (entry->d_name[0] == '.' || n <= 4 || n - 4 >= PC5_SOUND_NAME || !pc5_ends_with_ci(entry->d_name, ".wav") ||
            entry->d_name[0] == ' ' || entry->d_name[0] == '\t' || entry->d_name[n - 5] == ' ' || entry->d_name[n - 5] == '\t')
            continue;   /* a name the settings file couldn't keep as it is */
        char name[PC5_SOUND_NAME];
        memcpy(name, entry->d_name, n - 4);
        name[n - 4] = 0;
        int seen = 0;
        for (int i = 0; i < list->count && !seen; i++) seen = !pc5_casecmp(list->name[i], name);
        if (!seen) pc5_copy(list->name[list->count++], PC5_SOUND_NAME, name);
    }
    closedir(folder);
}

void pc5_sound_list(const char *app, const char *root, Pc5SoundList *list)
{
    char dir[PC5_PATH];

    list->count = 0;
    if (app && !pc5_format(dir, sizeof(dir), "%s/pc5/sounds", app)) list_in(dir, list);
    if (root && !pc5_format(dir, sizeof(dir), "%s/sounds", root)) list_in(dir, list);
    qsort(list->name, (size_t)list->count, sizeof(list->name[0]), name_order);
}

int pc5_sound_path(const char *app, const char *root, const char *name, char *out, size_t size)
{
    char dir[PC5_PATH];
    const char *const names[] = { name, NULL };
    static const char *const wav[] = { ".wav", NULL };
    DIR *folder;
    struct dirent *entry;

    if (!name || !name[0] || strchr(name, '/')) return -1;
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 0 ? !root || pc5_format(dir, sizeof(dir), "%s/sounds", root)
                      : !app || pc5_format(dir, sizeof(dir), "%s/pc5/sounds", app))
            continue;
        if (!(folder = opendir(dir))) continue;
        /* the file's name in any case, as the list shows it */
        while ((entry = readdir(folder))) {
            size_t n = strlen(entry->d_name);
            if (n <= 4 || !pc5_ends_with_ci(entry->d_name, wav[0]) || n - 4 != strlen(names[0]) ||
                pc5_casecmp(entry->d_name + n - 4, ".wav"))
                continue;
            char base[PC5_SOUND_NAME + 1];
            if (n - 4 >= sizeof(base)) continue;
            memcpy(base, entry->d_name, n - 4);
            base[n - 4] = 0;
            if (!pc5_casecmp(base, name) && !pc5_join(out, size, dir, entry->d_name) && pc5_is_file(out)) {
                closedir(folder);
                return 0;
            }
        }
        closedir(folder);
    }
    return -1;
}

/* ---- importing library music ---------------------------------------------------------- */
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_SIMD
/* The PS5 title's libc does not export __assert. Keep expressions type-checked
 * without the decoder's debug-only assertion calls, as in a release build. */
#define DRMP3_ASSERT(expression) ((void)sizeof(expression))
#include "third_party/dr_mp3.h"
#include <sys/stat.h>
#include <unistd.h>

typedef struct MusicInput {
    drmp3 *mp3;
    Pc5Music wav;
    uint32_t rate;
    unsigned channels;
    int16_t pcm[4096 * 2];
    size_t at, count;
    int failed;
} MusicInput;

static void music_input_close(MusicInput *input)
{
    if (input->mp3) {
        drmp3_uninit(input->mp3);
        pc5_free(input->mp3);
    }
    pc5_music_close(&input->wav);
}

static int music_input_open(MusicInput *input, const char *path)
{
    memset(input, 0, sizeof(*input));
    if (pc5_ends_with_ci(path, ".mp3")) {
        input->mp3 = pc5_alloc(sizeof(*input->mp3));
        if (!input->mp3) return -1;
        if (!drmp3_init_file(input->mp3, path, NULL)) {
            pc5_free(input->mp3);
            input->mp3 = NULL;
            return -1;
        }
        input->rate = input->mp3->sampleRate;
        input->channels = input->mp3->channels;
        if (input->rate < 8000 || input->rate > 48000 || (input->channels != 1 && input->channels != 2)) {
            music_input_close(input);
            return -1;
        }
    } else {
        if (!pc5_ends_with_ci(path, ".wav") || music_open_rate(path, &input->wav, &input->rate)) return -1;
        input->channels = (unsigned)input->wav.channels;
    }
    return 0;
}

/* A single stereo frame, buffered from the decoder/file, without looping. */
static int music_input_frame(MusicInput *input, int16_t frame[2])
{
    if (input->at == input->count) {
        input->at = 0;
        if (input->mp3) {
            input->count = (size_t)drmp3_read_pcm_frames_s16(input->mp3, 4096, input->pcm);
        } else {
            uint8_t bytes[4096 * 4];
            size_t width = (size_t)input->channels * 2;
            size_t n = input->wav.left / width;
            if (n > 4096) n = 4096;
            size_t got = fread(bytes, width, n, input->wav.file);
            if (got != n) input->failed = 1;
            input->count = got;
            input->wav.left -= (uint32_t)(got * width);
            for (size_t i = 0; i < got * input->channels; i++) input->pcm[i] = (int16_t)le16(bytes + i * 2);
        }
        if (!input->count) return 0;
    }
    frame[0] = input->pcm[input->at * input->channels];
    frame[1] = input->pcm[input->at * input->channels + (input->channels == 2 ? 1 : 0)];
    input->at++;
    return 1;
}

static void music_put32(uint8_t *out, uint32_t n)
{
    for (int i = 0; i < 4; i++) out[i] = (uint8_t)(n >> (i * 8));
}

static int music_wav_header(FILE *file, uint32_t bytes)
{
    uint8_t head[44] = { 0 };
    memcpy(head, "RIFF", 4); music_put32(head + 4, bytes + 36);
    memcpy(head + 8, "WAVEfmt ", 8); music_put32(head + 16, 16);
    head[20] = 1; head[22] = 2;
    music_put32(head + 24, 48000); music_put32(head + 28, 192000);
    head[32] = 4; head[34] = 16;
    memcpy(head + 36, "data", 4); music_put32(head + 40, bytes);
    return fwrite(head, 1, sizeof(head), file) == sizeof(head) ? 0 : -1;
}

static int music_convert(Pc5MusicImport *job, MusicInput *input, FILE *output)
{
    uint8_t block[4096 * 4];
    size_t used = 0;
    uint32_t phase = 0, bytes = 0;
    int16_t a[2], b[2];
    if (music_wav_header(output, 0)) return PC5_MUSIC_IO_ERROR;
    if (!music_input_frame(input, a)) return PC5_MUSIC_BAD_FORMAT;
    int next = music_input_frame(input, b);
    if (!next) memcpy(b, a, sizeof(b));
    for (;;) {
        if (atomic_load(&job->stop)) return PC5_MUSIC_CANCELLED;
        /* Keep WAVs below 2 GiB, also readable by hosts with 32-bit long. */
        if (bytes > 0x7ffff000u - 4) return PC5_MUSIC_TOO_LARGE;
        for (int c = 0; c < 2; c++) {
            int32_t value = ((int32_t)a[c] * (int32_t)(48000 - phase) + (int32_t)b[c] * (int32_t)phase) / 48000;
            uint16_t sample = (uint16_t)(int16_t)value;
            block[used++] = (uint8_t)sample;
            block[used++] = (uint8_t)(sample >> 8);
        }
        bytes += 4;
        if (used == sizeof(block)) {
            if (fwrite(block, 1, used, output) != used) return PC5_MUSIC_IO_ERROR;
            used = 0;
        }
        phase += input->rate;
        while (phase >= 48000) {
            phase -= 48000;
            if (!next) goto finished;
            memcpy(a, b, sizeof(a));
            next = music_input_frame(input, b);
            if (!next) memcpy(b, a, sizeof(b));
        }
    }
finished:
    if (input->failed) return PC5_MUSIC_IO_ERROR;
    if (used && fwrite(block, 1, used, output) != used) return PC5_MUSIC_IO_ERROR;
    if (fseek(output, 0, SEEK_SET) || music_wav_header(output, bytes) || fflush(output)) return PC5_MUSIC_IO_ERROR;
    return PC5_MUSIC_IMPORTED;
}

static void *music_import_worker(void *argument)
{
    Pc5MusicImport *job = argument;
    char part[PC5_PATH];
    struct stat before, after;
    MusicInput *input = pc5_alloc(sizeof(*input));
    FILE *output = NULL;
    int opened = 0;
    job->result = PC5_MUSIC_IO_ERROR;
    part[0] = 0;
    if (!input || pc5_format(part, sizeof(part), "%s.part", job->destination) ||
        stat(job->source, &before) || !S_ISREG(before.st_mode)) goto done;
    if (atomic_load(&job->stop)) { job->result = PC5_MUSIC_CANCELLED; goto done; }
    if (music_input_open(input, job->source)) { job->result = PC5_MUSIC_BAD_FORMAT; goto done; }
    opened = 1;
    output = fopen(part, "wb");
    if (!output) goto done;
    job->result = music_convert(job, input, output);
    if (fclose(output) && job->result == PC5_MUSIC_IMPORTED) job->result = PC5_MUSIC_IO_ERROR;
    output = NULL;
    if (job->result == PC5_MUSIC_IMPORTED &&
        (stat(job->source, &after) || before.st_size != after.st_size || before.st_mtime != after.st_mtime))
        job->result = PC5_MUSIC_IO_ERROR;
    if (atomic_load(&job->stop)) job->result = PC5_MUSIC_CANCELLED;
    if (job->result == PC5_MUSIC_IMPORTED && rename(part, job->destination)) job->result = PC5_MUSIC_IO_ERROR;
done:
    if (output) fclose(output);
    if (opened) music_input_close(input);
    pc5_free(input);
    if (part[0]) unlink(part);
    atomic_store(&job->done, 1);
    return NULL;
}

int pc5_music_import_start(Pc5MusicImport *job, const char *source, const char *destination)
{
    memset(job, 0, sizeof(*job));
    atomic_init(&job->stop, 0);
    atomic_init(&job->done, 0);
    job->result = PC5_MUSIC_IO_ERROR;
    if (strlen(source) >= sizeof(job->source) || strlen(destination) + 5 >= sizeof(job->destination)) return -1;
    pc5_copy(job->source, sizeof(job->source), source);
    pc5_copy(job->destination, sizeof(job->destination), destination);
    if (pc5_thread_start(&job->thread, music_import_worker, job, 0)) return -1;
    job->running = 1;
    return 0;
}

void pc5_music_import_end(Pc5MusicImport *job)
{
    if (!job->running) return;
    if (!atomic_load(&job->done)) atomic_store(&job->stop, 1);
    pthread_join(job->thread, NULL);
    job->running = 0;
}
