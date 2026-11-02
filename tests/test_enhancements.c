/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "pc5_compat.h"
#include "pc5_appearance.h"
#include "pc5_look.h"
#include "pc5_sound.h"
#include "pc5_image.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifdef _WIN32
#include <fcntl.h>
#include <stdlib.h>
#endif
static void wait_look(Pc5Look *j)
{
    int n = 0; while (!atomic_load(&j->done) && n++ < 2000) usleep(10000);
    assert(atomic_load(&j->done)); pc5_look_end(j);
}
static void same(const char *path, const char *want)
{
    Pc5Buf b = { 0 }; assert(!pc5_read_file(path, &b, 4096));
    assert(b.len == strlen(want) && !memcmp(b.data, want, b.len)); pc5_buf_free(&b);
}
static int fake_status = 200;
static const char *wiki;
static int fake_get(void *context, const char *url, Pc5Buf *out, size_t max, int *status)
{
    (void)context; assert(strstr(url, "raw.githubusercontent.com/wiki/bornaradusin/PC5/"));
    *status = fake_status; return strlen(wiki) > max ? -1 : pc5_buf_puts(out, wiki);
}
int main(int argc, char **argv)
{
#ifdef _WIN32
    _set_fmode(_O_BINARY);
#endif
    assert(argc == 3 || argc == 4); const char *root = argv[1];
    assert(!pc5_mkdirs(root));
    Pc5Buf fixture = { 0 }; Pc5CompatEntry *rows = NULL;
    assert(!pc5_read_file(argv[2], &fixture, PC5_COMPAT_LIMIT));
    int n = pc5_compat_parse((const char *)fixture.data, fixture.len, &rows);
    assert(n > 100); int states[8] = { 0 };
    for (int i = 0; i < n; ++i) { assert(rows[i].title[0]); ++states[rows[i].status]; }
    for (int i = 1; i < 8; ++i) assert(states[i]);
    assert(pc5_compat_matches(&rows[0], "", 0));
    assert(!pc5_compat_matches(&rows[0], "unlikely-game-name-xyz", 0));
    pc5_free(rows); rows = NULL;
    const char *sample = "| Game | How it runs | PC5 | Store and version | Controller | Settings that helped | Notes | Report or issue |\n"
        "| --- | --- | --- | --- | --- | --- | --- | --- |\n"
        "| Test \\| Game | Works perfectly | 2.1.1 | GOG | DualSense | **Default** | [Read](https://evil.example) <script>x</script> | [#123](https://github.com/bornaradusin/PC5/discussions/123) by Tester |\n";
    n = pc5_compat_parse(sample, strlen(sample), &rows); assert(n == 1);
    assert(!strcmp(rows[0].title, "Test | Game") && !strcmp(rows[0].settings, "Default"));
    assert(!strcmp(rows[0].report, "https://github.com/bornaradusin/PC5/discussions/123"));
    assert(pc5_compat_matches(&rows[0], "TEST", 1) && !pc5_compat_matches(&rows[0], "TEST", 2));
    pc5_free(rows);
    const char *body = strchr(sample, '\n');
    assert(body);
    Pc5Buf reordered = { 0 }, extra_header = { 0 }, extra_row = { 0 }, renamed = { 0 }, crlf = { 0 };
    assert(!pc5_buf_puts(&reordered, "| Game | PC5 | How it runs | Store and version | Controller | Settings that helped | Notes | Report or issue |"));
    assert(!pc5_buf_puts(&reordered, body));
    assert(pc5_compat_parse((const char *)reordered.data, reordered.len, &rows) < 0 && !rows);
    assert(!pc5_buf_put(&extra_header, sample, (size_t)(body - sample)));
    assert(!pc5_buf_puts(&extra_header, " FPS |"));
    assert(!pc5_buf_puts(&extra_header, body));
    assert(pc5_compat_parse((const char *)extra_header.data, extra_header.len, &rows) < 0 && !rows);
    assert(!pc5_buf_put(&extra_row, sample, strlen(sample) - 1));
    assert(!pc5_buf_puts(&extra_row, " Extra report field |\n"));
    assert(pc5_compat_parse((const char *)extra_row.data, extra_row.len, &rows) < 0 && !rows);
    assert(!pc5_buf_puts(&renamed, "| Game | How it runs | PC5 | Store and version | Controller | Settings that helped | Notes | Report |"));
    assert(!pc5_buf_puts(&renamed, body));
    assert(pc5_compat_parse((const char *)renamed.data, renamed.len, &rows) < 0 && !rows);
    for (const char *at = sample; *at; ++at) {
        if (*at == '\n') assert(!pc5_buf_puts(&crlf, " \t\r\n"));
        else assert(!pc5_buf_put(&crlf, at, 1));
    }
    assert(pc5_compat_parse((const char *)crlf.data, crlf.len, &rows) == 1);
    pc5_free(rows); pc5_buf_free(&crlf);
    assert(pc5_compat_parse("<html>Error</html>", 18, &rows) < 0);
    assert(pc5_compat_parse("| Game |\n| broken |", 19, &rows) < 0);
    Pc5Http http = { .get = fake_get }; Pc5Compat list;
    wiki = sample; pc5_compat_init(&list, root, &http); assert(!pc5_compat_refresh(&list));
    while (!pc5_compat_poll(&list)) usleep(1000);
    assert(list.count == 1 && !list.result && list.checked > 0);
    const Pc5Buf *changed_schemas[] = { &reordered, &extra_header, &extra_row, &renamed };
    for (size_t i = 0; i < sizeof(changed_schemas) / sizeof(changed_schemas[0]); ++i) {
        wiki = (const char *)changed_schemas[i]->data;
        assert(!pc5_compat_refresh(&list)); while (!pc5_compat_poll(&list)) usleep(1000);
        assert(list.result && list.count == 1 && !strcmp(list.entries[0].title, "Test | Game"));
        assert(!strcmp(list.entries[0].report, "https://github.com/bornaradusin/PC5/discussions/123"));
    }
    fake_status = 503; assert(!pc5_compat_refresh(&list)); while (!pc5_compat_poll(&list)) usleep(1000);
    assert(list.count == 1 && list.result && list.error[0]);
    fake_status = 200; wiki = "bad"; assert(!pc5_compat_refresh(&list)); while (!pc5_compat_poll(&list)) usleep(1000);
    assert(list.count == 1 && list.result); pc5_compat_end(&list);
    pc5_compat_init(&list, root, &http); assert(list.count == 1 && list.cached); pc5_compat_end(&list);
    pc5_buf_free(&reordered); pc5_buf_free(&extra_header); pc5_buf_free(&extra_row); pc5_buf_free(&renamed);
    pc5_buf_free(&fixture);
    puts("Compatibility: live table, all seven statuses, escaped cells, exact eight-column schema, source URL restriction, search, failed/schema-changed refresh and offline cache passed.");

    char dir[PC5_PATH], cover[PC5_PATH], options[PC5_PATH], archive[PC5_PATH], save[PC5_PATH];
    assert(!pc5_format(dir, sizeof(dir), "%s/prefixes/test/pc5", root) && !pc5_mkdirs(dir));
    assert(!pc5_format(cover, sizeof(cover), "%s/cover.bin", dir));
    assert(!pc5_format(options, sizeof(options), "%s/appearance.txt", dir));
    assert(!pc5_format(archive, sizeof(archive), "%s/test.pc5look", root));
    assert(!pc5_format(save, sizeof(save), "%s/untouched-save.dat", root));
    assert(!pc5_write_file(save, "KEEP", 4));
    char media[PC5_PATH], config[PC5_PATH], music_path[PC5_PATH], video_path[PC5_PATH] = "";
    assert(!pc5_format(media, sizeof(media), "%s/music", dir) && !pc5_mkdirs(media));
    assert(!pc5_format(music_path, sizeof(music_path), "%s/track-b.wav", media));
    const uint8_t wave[48] = { 'R','I','F','F',40,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,
        0x80,0xbb,0,0,0,0x77,1,0,2,0,16,0,'d','a','t','a',4,0,0,0,123,0,200,0 };
    assert(!pc5_write_file(music_path, wave, sizeof(wave)));
    assert(!pc5_format(config, sizeof(config), "%s/music.txt", dir));
    assert(!pc5_write_file(config, music_path, strlen(music_path)));
    if (argc == 4) {
        Pc5Buf v = { 0 }; assert(!pc5_read_file(argv[3], &v, 2u << 20));
        assert(!pc5_format(media, sizeof(media), "%s/background-video", dir) && !pc5_mkdirs(media));
        assert(!pc5_format(video_path, sizeof(video_path), "%s/clip-b.mp4", media));
        assert(!pc5_write_file(video_path, v.data, v.len)); pc5_buf_free(&v);
        assert(!pc5_format(config, sizeof(config), "%s/background-video.txt", dir));
        assert(!pc5_write_file(config, video_path, strlen(video_path)));
    }
    Pc5Appearance a; assert(!pc5_appearance_load(root, "test", &a) && a.volume == 30);
    a.volume = 65; a.music_start = 1; a.video_start = 2; a.video_end = 4; a.fit = 2; a.darkness = 20;
    assert(!pc5_appearance_save(root, "test", &a));
    Pc5Appearance read; assert(!pc5_appearance_load(root, "test", &read) && !memcmp(&a, &read, sizeof(a)));
    read.video_end = 1; assert(pc5_appearance_save(root, "test", &read) < 0);
    assert(pc5_appearance_save(root, "../escape", &a) < 0);
    Pc5Image image = { 0 }; assert(!pc5_image_new(&image, 4, 4));
    image.px[0] = 0xffabcdef; assert(!pc5_cover_save(cover, &image, 123)); pc5_image_free(&image);
    Pc5Look job; assert(!pc5_look_start(&job, root, "test", archive, 0)); wait_look(&job); assert(!job.result);
    a.volume = 10; assert(!pc5_appearance_save(root, "test", &a)); assert(!pc5_write_file(cover, "old", 3));
    assert(!unlink(music_path)); if (video_path[0]) assert(!unlink(video_path));
    assert(!pc5_look_start(&job, root, "test", archive, 1)); wait_look(&job); assert(!job.result);
    assert(!pc5_appearance_load(root, "test", &read) && read.volume == 65);
    assert(!pc5_cover_load(cover, &image, NULL) && image.px[0] == 0xffabcdef); pc5_image_free(&image); same(save, "KEEP");
    assert(!pc5_format(music_path, sizeof(music_path), "%s/music/track-a.wav", dir));
    Pc5Music music; assert(!pc5_music_open(music_path, &music)); int16_t samples[4];
    assert(pc5_music_read(&music, samples, 2) == 2 && samples[0] == 123 && samples[2] == 200); pc5_music_close(&music);
    assert(!pc5_format(config, sizeof(config), "%s/music.txt", dir)); same(config, music_path);
    if (video_path[0]) {
        assert(!pc5_format(video_path, sizeof(video_path), "%s/background-video/clip-a.mp4", dir));
        assert(pc5_is_file(video_path));
        assert(!pc5_format(config, sizeof(config), "%s/background-video.txt", dir)); same(config, video_path);
    }
    /* Corrupt a payload, not just its magic. No files may change on failure. */
    FILE *f = fopen(archive, "r+b"); assert(f && !fseek(f, 60, SEEK_SET)); int byte = fgetc(f);
    assert(byte != EOF && !fseek(f, 60, SEEK_SET)); fputc(byte ^ 1, f); fclose(f);
    assert(!pc5_look_start(&job, root, "test", archive, 1)); wait_look(&job); assert(job.result);
    assert(!pc5_appearance_load(root, "test", &read) && read.volume == 65); same(save, "KEEP");
    char marker[PC5_PATH], old[PC5_PATH];
    assert(!pc5_format(marker, sizeof(marker), "%s/.look-pending", dir));
    assert(!pc5_format(old, sizeof(old), "%s/.look-old-0", dir));
    assert(!pc5_write_file(old, "ORIGINAL", 8)); assert(!pc5_write_file(cover, "INCOMPLETE", 10));
    /* Validate the entire journal before doing even its first rename. A valid
     * first flag followed by an invalid one must retain every recovery file. */
    assert(!pc5_write_file(marker, "1XXXXXXX0", 9));
    pc5_look_recover(root, "test"); same(old, "ORIGINAL"); same(cover, "INCOMPLETE");
    same(marker, "1XXXXXXX0"); same(save, "KEEP");
    assert(pc5_look_start(&job, root, "test", archive, 1) < 0);
    same(old, "ORIGINAL"); same(cover, "INCOMPLETE"); same(marker, "1XXXXXXX0");
    assert(!pc5_write_file(marker, "111111111", 9));
    pc5_look_recover(root, "test"); same(cover, "ORIGINAL"); assert(!pc5_exists(marker)); same(save, "KEEP");
    assert(!unlink(music_path));
    char missing[PC5_PATH]; assert(!pc5_format(missing, sizeof(missing), "%s/missing.pc5look", root));
    assert(!pc5_look_start(&job, root, "test", missing, 0)); wait_look(&job);
    assert(job.result && !pc5_exists(missing)); same(cover, "ORIGINAL");
    assert(pc5_look_start(&job, root, "default", missing, 1) < 0);
    /* An existing non-file slot must never silently become an empty backup. */
    assert(!pc5_write_file(music_path, wave, sizeof(wave)));
    char invalid_slot[PC5_PATH];
    assert(!pc5_format(invalid_slot, sizeof(invalid_slot), "%s/icon.bin", dir));
    assert(!pc5_mkdirs(invalid_slot));
    assert(!pc5_look_start(&job, root, "test", missing, 0)); wait_look(&job);
    assert(job.result && !pc5_exists(missing));
    assert(!rmdir(invalid_slot)); same(cover, "ORIGINAL");
    assert(!pc5_format(config, sizeof(config), "%s/music.txt", dir));
    assert(!unlink(config) && !pc5_mkdirs(config));
    assert(!pc5_look_start(&job, root, "test", missing, 0)); wait_look(&job);
    assert(job.result && !pc5_exists(missing)); assert(!rmdir(config));
    puts("Appearance: validated per-game settings, streamed backup/restore, content checksums, unchanged saves and interrupted-restore recovery passed.");
    return 0;
}
