/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "pc5_look.h"
#include "pc5_appearance.h"
#include "pc5_cover.h"
#include "pc5_image.h"
#include "pc5_sound.h"
#include "pc5_video.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Streaming, versioned container: magic, then 7 fixed slots. Each has an
 * unsigned LE64 length, SHA256 and content. No user supplied archive paths. */
static const char *const look_names[] = { "cover.bin", "background.bin", "icon.bin", "music.wav", "video.mp4", "appearance.txt", "background-none.txt" };
static const char *const commit_names[] = { "cover.bin", "background.bin", "icon.bin", "music/track-a.wav", "background-video/clip-a.mp4", "appearance.txt", "background-none.txt", "music.txt", "background-video.txt" };
static uint64_t limit(int i) { return i == 3 ? 2ull * 1024 * 1024 * 1024 : i == 4 ? 1024ull * 1024 * 1024 : i == 5 || i == 6 ? 512 : 40ull * 1024 * 1024; }
static int durable_close(FILE *file)
{
    int failed = fflush(file) || fsync(fileno(file));
    if (fclose(file)) failed = 1;
    return failed ? -1 : 0;
}
static int look_dir(const char *root, const char *prefix, char *dir)
{
    if (!prefix[0] || strchr(prefix, '/') || strchr(prefix, '\\') || !strcmp(prefix, ".") || !strcmp(prefix, "..") || !strcmp(prefix, "default")) return -1;
    return pc5_format(dir, PC5_PATH, "%s/prefixes/%s/pc5", root, prefix);
}
static void look_cleanup(const char *dir)
{
    char p[PC5_PATH];
    for (int i = 0; i < 9; ++i) {
        if (!pc5_format(p, sizeof(p), "%s/.look-new-%d", dir, i)) unlink(p);
        if (!pc5_format(p, sizeof(p), "%s/.look-old-%d", dir, i)) unlink(p);
    }
    if (!pc5_format(p, sizeof(p), "%s/.look-new-4.checked", dir)) unlink(p);
    if (!pc5_format(p, sizeof(p), "%s/.look-new-4.checked.part", dir)) unlink(p);
}
void pc5_look_recover(const char *root, const char *prefix)
{
    char dir[PC5_PATH], marker[PC5_PATH], old[PC5_PATH], dest[PC5_PATH]; Pc5Buf b = { 0 };
    if (look_dir(root, prefix, dir) || pc5_format(marker, sizeof(marker), "%s/.look-pending", dir)) return;
    int valid = !pc5_read_file(marker, &b, 32) && b.len == 9;
    for (size_t i = 0; valid && i < b.len; ++i)
        if (b.data[i] != '0' && b.data[i] != '1') valid = 0;
    if (valid) {
        int failed = 0;
        for (int i = 0; i < 9; ++i) {
            if (pc5_format(old, sizeof(old), "%s/.look-old-%d", dir, i) || pc5_format(dest, sizeof(dest), "%s/%s", dir, commit_names[i])) { failed = 1; continue; }
            if (b.data[i] == '1' && pc5_is_file(old)) {
                if (pc5_exists(dest) && unlink(dest)) failed = 1;
                if (rename(old, dest)) failed = 1;
            } else if (b.data[i] == '0' && pc5_exists(dest) && unlink(dest)) failed = 1;
        }
        if (!failed) unlink(marker);
    }
    pc5_buf_free(&b);
    if (!pc5_exists(marker)) look_cleanup(dir);
    /* If rollback renames failed, retain old files for manual recovery. */
}
static int transfer(Pc5Look *j, FILE *in, FILE *out, uint64_t n, uint8_t digest[32])
{
    Pc5Sha256 sha; pc5_sha256_init(&sha);
    uint8_t *buf = pc5_alloc(65536); if (!buf) return -1;
    while (n && !atomic_load(&j->stop)) {
        size_t part = n < 65536 ? (size_t)n : 65536;
        if (fread(buf, 1, part, in) != part || (out && fwrite(buf, 1, part, out) != part)) { pc5_free(buf); return -1; }
        pc5_sha256_update(&sha, buf, part); n -= part; atomic_fetch_add(&j->bytes, part);
    }
    pc5_free(buf); pc5_sha256_final(&sha, digest); return n ? -1 : 0;
}
static int association(const char *dir, const char *name, const char *folder, const char *a, const char *b, char *path)
{
    char config[PC5_PATH], x[PC5_PATH], y[PC5_PATH]; Pc5Buf data = { 0 }; path[0] = 0;
    if (pc5_format(config, sizeof(config), "%s/%s", dir, name) || pc5_format(x, sizeof(x), "%s/%s/%s", dir, folder, a) ||
        pc5_format(y, sizeof(y), "%s/%s/%s", dir, folder, b)) return -1;
    struct stat st;
    if (stat(config, &st)) return errno == ENOENT ? 0 : -1;
    if (!S_ISREG(st.st_mode)) return -1;
    if (pc5_read_file(config, &data, PC5_PATH - 1)) { pc5_buf_free(&data); return -1; }
    int ok = (data.len == strlen(x) && !memcmp(data.data, x, data.len)) || (data.len == strlen(y) && !memcmp(data.data, y, data.len));
    if (ok) pc5_copy(path, PC5_PATH, (const char *)data.data);
    pc5_buf_free(&data); return ok ? 0 : -1;
}
static int look_export(Pc5Look *j, const char *dir)
{
    char part[PC5_PATH], source[PC5_PATH];
    if (pc5_format(part, sizeof(part), "%s.part", j->archive)) return -1;
    FILE *out = fopen(part, "wb"); if (!out) return -1;
    int failed = fwrite("PC5LOOK1", 1, 8, out) != 8;
    for (int i = 0; !failed && i < 7; ++i) {
        if (i == 3) failed = association(dir, "music.txt", "music", "track-a.wav", "track-b.wav", source);
        else if (i == 4) failed = association(dir, "background-video.txt", "background-video", "clip-a.mp4", "clip-b.mp4", source);
        else failed = pc5_format(source, sizeof(source), "%s/%s", dir, look_names[i]);
        if (failed) break;
        struct stat st;
        int exists = 0;
        if (source[0]) {
            if (!stat(source, &st)) {
                if (!S_ISREG(st.st_mode)) { failed = 1; break; }
                exists = 1;
            } else if (errno != ENOENT) { failed = 1; break; }
        }
        if ((i == 3 || i == 4) && source[0] && !exists) { failed = 1; break; }
        uint64_t size = exists ? (uint64_t)st.st_size : 0;
        if (size > limit(i)) { failed = 1; break; }
        uint8_t head[40] = { 0 }; for (int k = 0; k < 8; ++k) head[k] = (uint8_t)(size >> (k * 8));
        long at = ftell(out); if (at < 0 || fwrite(head, 1, 40, out) != 40) { failed = 1; break; }
        FILE *in = size ? fopen(source, "rb") : NULL;
        if (size && (!in || transfer(j, in, out, size, head + 8))) failed = 1;
        if (in) fclose(in);
        long end = ftell(out);
        if (end < 0 || fseek(out, at, SEEK_SET) || fwrite(head, 1, 40, out) != 40 || fseek(out, end, SEEK_SET)) failed = 1;
    }
    if (durable_close(out)) failed = 1;
    if (atomic_load(&j->stop)) failed = 1;
    if (!failed && rename(part, j->archive)) failed = 1;
    if (failed) unlink(part);
    return failed ? -1 : 0;
}
static int validate_slot(Pc5Look *j, int i, const char *path)
{
    if (i < 3) {
        Pc5Image image = { 0 }; int failed = pc5_cover_load(path, &image, NULL);
        pc5_image_free(&image); return failed;
    }
    if (i == 3) { Pc5Music m; if (pc5_music_open(path, &m)) return -1; pc5_music_close(&m); }
    if (i == 4) {
        /* Decoder validation on its worker before any current association moves. */
        Pc5VideoImport v; char test[PC5_PATH];
        if (pc5_format(test, sizeof(test), "%s.checked", path) || pc5_video_import_start(&v, path, test)) return -1;
        while (!atomic_load(&v.done)) { if (atomic_load(&j->stop)) atomic_store(&v.stop, 1); usleep(10000); }
        pc5_video_import_end(&v); unlink(test); if (v.result) return -1;
    }
    if (i == 5) {
        Pc5Buf b = { 0 }; Pc5Appearance o; int n = 0;
        if (pc5_read_file(path, &b, 512)) { pc5_buf_free(&b); return -1; }
        int ok = sscanf((const char *)b.data, "PC5-APPEARANCE-1\nvolume=%d\nmusic_start=%d\nvideo_start=%d\nvideo_end=%d\nfit=%d\ndarkness=%d\n%n",
            &o.volume, &o.music_start, &o.video_start, &o.video_end, &o.fit, &o.darkness, &n) == 6 && (size_t)n == b.len && pc5_appearance_valid(&o);
        pc5_buf_free(&b); if (!ok) return -1;
    }
    if (i == 6) {
        Pc5Buf b = { 0 }; int bad = pc5_read_file(path, &b, 512) || b.len != 5 || memcmp(b.data, "none\n", 5);
        pc5_buf_free(&b); if (bad) return -1;
    }
    return 0;
}
static int look_restore(Pc5Look *j, const char *dir)
{
    char stage[PC5_PATH], dest[PC5_PATH], old[PC5_PATH], marker[PC5_PATH], sub[PC5_PATH];
    if (pc5_format(marker, sizeof(marker), "%s/.look-pending", dir) || pc5_exists(marker)) return -1;
    FILE *in = fopen(j->archive, "rb"); if (!in) return -1;
    uint8_t magic[8]; int failed = fread(magic, 1, 8, in) != 8 || memcmp(magic, "PC5LOOK1", 8);
    int present[9] = { 0 };
    look_cleanup(dir);
    for (int i = 0; !failed && i < 7; ++i) {
        uint8_t h[40], digest[32] = { 0 }; uint64_t n = 0;
        if (fread(h, 1, 40, in) != 40) { failed = 1; break; }
        for (int k = 0; k < 8; ++k) n |= (uint64_t)h[k] << (k * 8);
        if (n > limit(i) || pc5_format(stage, sizeof(stage), "%s/.look-new-%d", dir, i)) { failed = 1; break; }
        if (!n) continue;
        FILE *out = fopen(stage, "wb"); if (!out) { failed = 1; break; }
        failed = transfer(j, in, out, n, digest);
        if (durable_close(out) || memcmp(digest, h + 8, 32)) failed = 1;
        if (!failed && validate_slot(j, i, stage)) failed = 1;
        present[i] = 1;
    }
    if (fgetc(in) != EOF || ferror(in)) failed = 1;
    fclose(in);
    for (int i = 3; !failed && i <= 4; ++i) {
        if (!present[i]) continue;
        if (pc5_format(dest, sizeof(dest), "%s/%s", dir, commit_names[i]) ||
            pc5_format(stage, sizeof(stage), "%s/.look-new-%d", dir, i + 4)) { failed = 1; break; }
        pc5_copy(sub, sizeof(sub), dest); *strrchr(sub, '/') = 0;
        if (pc5_mkdirs(sub) || pc5_write_file(stage, dest, strlen(dest))) failed = 1;
        present[i + 4] = 1;
    }
    if (atomic_load(&j->stop)) failed = 1;
    if (!failed) {
        uint8_t prior[9];
        for (int i = 0; i < 9; ++i) {
            if (pc5_format(dest, sizeof(dest), "%s/%s", dir, commit_names[i])) { failed = 1; break; }
            prior[i] = pc5_exists(dest) ? '1' : '0';
        }
        if (!failed && (pc5_format(marker, sizeof(marker), "%s/.look-pending", dir) || pc5_write_file(marker, prior, 9))) failed = 1;
        if (!failed) {
            for (int i = 0; i < 9; ++i) {
                if (pc5_format(dest, sizeof(dest), "%s/%s", dir, commit_names[i]) ||
                    pc5_format(stage, sizeof(stage), "%s/.look-new-%d", dir, i) || pc5_format(old, sizeof(old), "%s/.look-old-%d", dir, i) ||
                    (prior[i] == '1' && rename(dest, old)) || (present[i] && rename(stage, dest))) { failed = 1; break; }
            }
            if (failed) pc5_look_recover(j->root, j->prefix);
            else if (unlink(marker)) { failed = 1; pc5_look_recover(j->root, j->prefix); }
        }
    }
    if (!pc5_exists(marker)) look_cleanup(dir);
    return failed ? -1 : 0;
}
static void *look_worker(void *context)
{
    Pc5Look *j = context; char dir[PC5_PATH];
    j->result = look_dir(j->root, j->prefix, dir) ? -1 : j->restore ? look_restore(j, dir) : look_export(j, dir);
    if (j->result) pc5_copy(j->error, sizeof(j->error), "Appearance operation failed. Check the file, source drive and free space. Existing media was kept.");
    atomic_store(&j->done, 1); return NULL;
}
int pc5_look_start(Pc5Look *j, const char *root, const char *prefix, const char *archive, int restore)
{
    memset(j, 0, sizeof(*j)); atomic_init(&j->done, 0); atomic_init(&j->stop, 0); atomic_init(&j->bytes, 0);
    if (strlen(root) >= PC5_PATH || strlen(prefix) >= PC5_ID || strlen(archive) >= PC5_PATH) return -1;
    pc5_copy(j->root, sizeof(j->root), root); pc5_copy(j->prefix, sizeof(j->prefix), prefix); pc5_copy(j->archive, sizeof(j->archive), archive); j->restore = restore;
    char dir[PC5_PATH]; if (look_dir(root, prefix, dir) || pc5_mkdirs(dir)) return -1;
    pc5_look_recover(root, prefix);
    char marker[PC5_PATH];
    if (pc5_format(marker, sizeof(marker), "%s/.look-pending", dir) || pc5_exists(marker)) return -1;
    if (pc5_thread_start(&j->thread, look_worker, j, 0)) return -1;
    j->running = 1; return 0;
}
void pc5_look_end(Pc5Look *j) { if (j->running) pthread_join(j->thread, NULL); j->running = 0; }
