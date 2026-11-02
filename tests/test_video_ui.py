#!/usr/bin/env python3
"""Isolated menu, playback-follow and decoder fallback checks; no FFmpeg required."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / '.build/video-ui-check'
out.mkdir(parents=True, exist_ok=True)
ui = (root / 'title/pc5_ui.c').read_text(encoding='utf-8')
menu = ui[ui.index('typedef enum GameRow'):ui.index('/* Button mapping:')]
header = (root / 'title/pc5_video.h').read_text(encoding='utf-8').replace('#include "pc5.h"', '')
appearance = (root / 'title/pc5_appearance.h').read_text(encoding='utf-8').replace('#include "pc5.h"', '')
appearance_impl = (root / 'title/pc5_appearance.c').read_text(encoding='utf-8')
valid = appearance_impl[appearance_impl.index('int pc5_appearance_valid('):appearance_impl.index('static int options_path(')]
header = appearance + valid + header.replace('#include "pc5_appearance.h"', '')
header = header.replace('pthread_t', 'VideoTestThread')
video = (root / 'title/pc5_video.c').read_text(encoding='utf-8').replace('#include "pc5_video.h"', '')
decoder_next = video[video.index('static int decoder_next('):video.index('static uint64_t video_clock(')]
if os.name == 'nt':
    video = video.replace('#include <unistd.h>', '')
support = r'''
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef uintptr_t VideoTestThread;
enum { PC5_PATH = 1024, PC5_ERROR = 256, PC5_PAD_KEYBOARD = 2 };
typedef struct Pc5Controls { int mode; } Pc5Controls;
#define N_(x) x
static void pc5_free(void *p) { free(p); }
static void pc5_copy(char *out, size_t size, const char *s) { snprintf(out, size, "%s", s); }
#define pthread_join video_test_join
static int video_test_join(VideoTestThread t, void **out) { (void)t; (void)out; return 0; }
'''
main = r'''
int main(void) {
    test_decoder_errno();
    test_video_memory();
    test_video_follow();
    test_media_delete();
    assert(phone_row_kind(GR_COVER) == PC5_PHONE_COVER);
    assert(phone_row_kind(GR_BACKGROUND_IMAGE) == PC5_PHONE_BACKGROUND);
    assert(phone_row_kind(GR_ICON) == PC5_PHONE_ICON);
    assert(phone_row_kind(GR_MUSIC) == PC5_PHONE_MUSIC);
    assert(phone_row_kind(GR_MUSIC_FILE) == PC5_PHONE_MUSIC);
    assert(phone_row_kind(GR_BACKGROUND_VIDEO) == PC5_PHONE_VIDEO);
    assert(phone_row_kind(GR_VIDEO_FILE) == PC5_PHONE_VIDEO);
    assert(phone_row_kind(GR_BACKGROUND) == -1);
    assert(phone_row_kind(GR_MUSIC_CLEAR) == -1 && phone_row_kind(GR_VIDEO_CLEAR) == -1);
    Pc5Controls controls = { 0 };
    GameRow rows[GAME_ROWS];
    /* A game PC5 installed in a folder of its own has music and a video; another, none. */
    assert(game_rows(&controls, 0, GP_APPEARANCE, rows) == 4 && game_rows(&controls, 0, GP_BACKGROUND, rows) == 1);
    int n = game_rows(&controls, GAME_MEDIA, GP_APPEARANCE, rows), background = 0;
    assert(n == 6);
    for (int i = 0; i < n; i++) {
        if (rows[i] == GR_BACKGROUND) background++;
        assert(rows[i] != GR_BACKGROUND_IMAGE && rows[i] != GR_BACKGROUND_VIDEO);
        assert(rows[i] != GR_MUSIC_FILE && rows[i] != GR_VIDEO_FILE);
        assert(rows[i] != GR_MUSIC_CLEAR && rows[i] != GR_VIDEO_CLEAR);
    }
    assert(background == 1 && game_page_rows[GP_BACKGROUND] == GR_BACKGROUND);
    assert(!strcmp(game_page_titles[GP_BACKGROUND], "Background"));
    assert(game_rows(&controls, GAME_MEDIA, GP_BACKGROUND, rows) == 2);
    assert(rows[0] == GR_BACKGROUND_IMAGE && rows[1] == GR_BACKGROUND_VIDEO);
    assert(game_rows(&controls, 0, GP_MUSIC, rows) == 2);
    assert(rows[0] == GR_MUSIC_FILE && rows[1] == GR_MUSIC_CLEAR);
    assert(game_rows(&controls, 0, GP_VIDEO, rows) == 2);
    assert(rows[0] == GR_VIDEO_FILE && rows[1] == GR_VIDEO_CLEAR);
    assert(game_page_rows[GP_MUSIC] == GR_MUSIC && game_page_rows[GP_VIDEO] == GR_BACKGROUND_VIDEO);
    assert(!strcmp(game_page_titles[GP_MUSIC], "Library music"));
    assert(!strcmp(game_page_titles[GP_VIDEO], "Background video"));
    int page;
    assert(game_row_find(&controls, 0, GR_BACKGROUND, &page) == 1 && page == GP_APPEARANCE);
    assert(game_row_find(&controls, 0, GR_BACKGROUND_IMAGE, &page) == 0 && page == GP_BACKGROUND);
    assert(game_row_find(&controls, GAME_MEDIA, GR_MUSIC, &page) == 4 && page == GP_APPEARANCE);
    assert(game_row_find(&controls, GAME_MEDIA, GR_BACKGROUND_VIDEO, &page) == 1 && page == GP_BACKGROUND);
    assert(game_row_find(&controls, GAME_MEDIA, GR_MUSIC_FILE, &page) == 0 && page == GP_MUSIC);
    assert(game_row_find(&controls, GAME_MEDIA, GR_MUSIC_CLEAR, &page) == 1 && page == GP_MUSIC);
    assert(game_row_find(&controls, GAME_MEDIA, GR_VIDEO_FILE, &page) == 0 && page == GP_VIDEO);
    assert(game_row_find(&controls, GAME_MEDIA, GR_VIDEO_CLEAR, &page) == 1 && page == GP_VIDEO);
    Pc5Video player = { 0 };
    assert(pc5_video_start(&player, "/missing.mp4") == -1);
    assert(!pc5_video_poll(&player)); pc5_video_stop(&player);
    assert(!player.front && !player.back && !player.have);
    Pc5VideoImport job;
    assert(pc5_video_import_start(&job, "/source.mp4", "/dest.mp4") == -1);
    pc5_video_import_end(&job);
    puts("video UI checks passed: nested music/video file/delete pages, parent routing, QR kinds and unavailable-decoder fallback");
    return 0;
}
'''
errno_test = r'''
enum { VW = 1920, VH = 1080 };
#define PC5_FFMPEG_EAGAIN 35
#define AVERROR(x) (-(x))
#define AVERROR_EOF (-10000)
#define AVERROR_INVALIDDATA (-10001)
typedef struct FakeFrame { int width, height; } FakeFrame;
typedef struct FakePacket { int size, stream_index; } FakePacket;
typedef struct VideoDecoder {
    atomic_int *stop; void *codec, *format;
    FakeFrame *frame; FakePacket *packet;
    int stream, draining, last_error;
} VideoDecoder;
static int receive_calls, send_calls, injected_error;
static int avcodec_receive_frame(void *codec, FakeFrame *frame) {
    (void)codec; (void)frame;
    if (injected_error) return injected_error;
    return receive_calls++ ? 0 : -35;
}
static int av_read_frame(void *format, FakePacket *packet) {
    (void)format; packet->size = 1; packet->stream_index = 0; return 0;
}
static int avcodec_send_packet(void *codec, FakePacket *packet) { (void)codec; (void)packet; send_calls++; return 0; }
static void av_packet_unref(FakePacket *packet) { (void)packet; }
static int decoder_error(VideoDecoder *d, const char *stage, int code) { (void)stage; d->last_error = code; return -1; }
DECODER_NEXT
static void test_decoder_errno(void) {
    atomic_int stop; atomic_init(&stop, 0);
    FakeFrame frame = { 1920, 1080 }; FakePacket packet = { 0, 0 };
    VideoDecoder d = { 0 }; d.stop = &stop; d.frame = &frame; d.packet = &packet;
    assert(decoder_next(&d) == 1 && send_calls == 1 && receive_calls == 2);
    injected_error = -12345;
    assert(decoder_next(&d) == -1 && d.last_error == -12345);
    injected_error = 0; receive_calls = 0; d.draining = 1;
    assert(decoder_next(&d) == -1 && d.last_error == -35);
    puts("decoder errno checks passed: SDK EAGAIN waits for packets, original errors retained, draining does not spin");
}
'''
unit = out / 'video_ui.c'
memory = (root / 'title/pc5_video_memory.c').read_text(encoding='utf-8').replace('#include "pc5.h"', '')
memory_test = r'''
static int fail_video_alloc;
static void *pc5_alloc(size_t size) { return fail_video_alloc ? NULL : calloc(1, size); }
VIDEO_MEMORY
static void test_video_memory(void) {
    void *p = NULL;
    assert(pc5_video_memalign(&p, 256, 8u << 20) == 0);
    assert(((uintptr_t)p & 255) == 0);
    memset(p, 0x57, 8u << 20);
    unsigned char *q = pc5_video_realloc(p, 16u << 20);
    assert(q && q[0] == 0x57 && q[(8u << 20)-1] == 0x57);
    fail_video_alloc = 1;
    assert(!pc5_video_realloc(q, 32u << 20) && q[0] == 0x57);
    fail_video_alloc = 0;
    pc5_video_free(q);
    p = (void *)(uintptr_t)1;
    assert(pc5_video_memalign(&p, 3, 1024) == EINVAL && p == (void *)(uintptr_t)1);
    assert(!pc5_video_malloc(SIZE_MAX));
    assert(!pc5_video_realloc(NULL, 0));
    pc5_video_free(NULL);
    p = pc5_video_malloc(0); assert(p && !((uintptr_t)p & 63)); pc5_video_free(p);
    puts("video memory checks passed: large aligned buffers, grow, failure preservation, overflow and free");
}
'''.replace('VIDEO_MEMORY', memory)
follow = ui[ui.index('static void video_follow('):ui.index('static void music_follow(')]
follow_test = r'''
enum { S_LIBRARY, S_GAME_MENU, S_OTHER, S_MEDIA_DELETE, TAB_HOME, TAB_LIBRARY, TAB_INSTALL };
typedef struct Pc5Game { char id[8], prefix[8]; } Pc5Game;
typedef struct Pc5Buf { unsigned char *data; size_t len; } Pc5Buf;
typedef struct Pc5Ui {
    int screen, tab, selected, list_count[1], list_at, dirty, button, media_delete_video;
    struct { Pc5Game games[3]; int count; } library;
    char root[16], video_id[16], video_want[PC5_PATH];
    uint64_t now, video_report_at;
    unsigned video_report_shown;
    Pc5Video video;
} Pc5Ui;
static int follow_starts, follow_stops, follow_fail;
static const char *follow_saved[] = { "/one/clip-a.mp4", "/two/clip-b.mp4", NULL };
static int video_paths(Pc5Ui *ui, const Pc5Game *game, char *config, char *dir, char *a, char *b) {
    (void)ui;
    snprintf(config, PC5_PATH, "%s", game->id);
    snprintf(dir, PC5_PATH, "/%s", game->id);
    snprintf(a, PC5_PATH, "/%s/clip-a.mp4", game->id);
    snprintf(b, PC5_PATH, "/%s/clip-b.mp4", game->id);
    return 0;
}
static int pc5_read_file(const char *path, Pc5Buf *out, size_t limit) {
    int n = !strcmp(path, "one") ? 0 : !strcmp(path, "two") ? 1 : 2;
    const char *saved = follow_saved[n];
    if (!saved || strlen(saved) > limit) return -1;
    out->len = strlen(saved); out->data = malloc(out->len);
    assert(out->data); memcpy(out->data, saved, out->len);
    return 0;
}
static void pc5_buf_free(Pc5Buf *buf) { free(buf->data); memset(buf, 0, sizeof(*buf)); }
int pc5_appearance_load(const char *root, const char *prefix, Pc5Appearance *options) {
    assert(!strcmp(root, "/root"));
    memset(options, 0, sizeof(*options));
    options->volume = 30;
    if (!strcmp(prefix, "two")) { options->volume = 70; options->video_start = 3; }
    return 0;
}
static void say(Pc5Ui *ui, const char *format, ...) { (void)ui; (void)format; }
static int follow_start(Pc5Video *video, const char *path, const Pc5Appearance *options) {
    follow_starts++;
    if (follow_fail) return -1;
    pc5_copy(video->path, sizeof(video->path), path);
    video->options = *options;
    atomic_init(&video->done, 0); atomic_init(&video->shown, 0);
    atomic_init(&video->decoded, 0); atomic_init(&video->dropped, 0);
    video->running = 1;
    return 0;
}
static void follow_stop(Pc5Video *video) {
    if (video->running) follow_stops++;
    memset(video, 0, sizeof(*video));
}
static int follow_poll(Pc5Video *video) { (void)video; return 0; }
#define pc5_video_start_options follow_start
#define pc5_video_stop follow_stop
#define pc5_video_poll follow_poll
VIDEO_FOLLOW
#undef pc5_video_start_options
#undef pc5_video_stop
#undef pc5_video_poll
static void test_video_follow(void) {
    Pc5Ui ui = { 0 };
    pc5_copy(ui.root, sizeof(ui.root), "/root");
    ui.screen = S_LIBRARY; ui.tab = TAB_HOME; ui.library.count = 3; ui.list_count[0] = 3;
    const char *ids[] = { "one", "two", "three" };
    for (int i = 0; i < 3; i++) {
        pc5_copy(ui.library.games[i].id, sizeof(ui.library.games[i].id), ids[i]);
        pc5_copy(ui.library.games[i].prefix, sizeof(ui.library.games[i].prefix), ids[i]);
    }
    /* First update at time zero must request decoding, without a timer advance. */
    video_follow(&ui);
    assert(follow_starts == 1 && ui.video.running && !strcmp(ui.video.path, follow_saved[0]));
    assert(ui.video.options.volume == 30 && ui.video.options.video_start == 0);
    video_follow(&ui); assert(follow_starts == 1 && follow_stops == 0);
    ui.selected = 1; video_follow(&ui);
    assert(follow_starts == 2 && follow_stops == 1 && !strcmp(ui.video.path, follow_saved[1]));
    assert(ui.video.options.volume == 70 && ui.video.options.video_start == 3);
    ui.screen = S_GAME_MENU; video_follow(&ui); assert(follow_starts == 2 && ui.video.running);
    ui.screen = S_OTHER; video_follow(&ui); assert(follow_stops == 2 && !ui.video.running);
    ui.screen = S_LIBRARY; ui.selected = 2; video_follow(&ui); assert(follow_starts == 2);
    ui.selected = 0; ui.tab = TAB_INSTALL; video_follow(&ui); assert(follow_starts == 2);
    ui.tab = TAB_LIBRARY; ui.list_count[0] = 0; video_follow(&ui); assert(follow_starts == 2);
    ui.list_count[0] = 3; ui.selected = -1; video_follow(&ui); assert(follow_starts == 2);
    ui.selected = 0; follow_fail = 1; video_follow(&ui);
    assert(follow_starts == 3 && !ui.video.running && !ui.video_want[0]);
    ui.now = 10000; video_follow(&ui); video_follow(&ui); assert(follow_starts == 3);
    puts("video follow checks passed: immediate start/switch, retained settings, screen eligibility and failure fallback");
}
'''.replace('VIDEO_FOLLOW', follow)
enhancement_input = (root / 'title/pc5_ui_enhancements_input.h').read_text(encoding='utf-8')
delete_helpers = enhancement_input[enhancement_input.index('static void media_delete_begin('):enhancement_input.index('static int compatibility_indices(')]
delete_test = r'''
enum { PC5_KEY_LEFT = 1, PC5_KEY_RIGHT = 2, PC5_KEY_TRIANGLE = 4, TEST_BACK = 8, TEST_ACCEPT = 16 };
#define T(x) x
static int delete_music_saves, delete_video_saves, delete_stops, delete_cleanups, delete_fail, delete_errors;
static void go(Pc5Ui *ui, int screen) { ui->screen = screen; }
static int music_save(Pc5Ui *ui, const char *path) {
    (void)ui; assert(!path[0]); delete_music_saves++; return delete_fail;
}
static int video_save(Pc5Ui *ui, const char *path) {
    (void)ui; assert(!path[0]); delete_video_saves++; return delete_fail;
}
static void library_music_stop(Pc5Ui *ui) { (void)ui; delete_stops++; }
static void music_cleanup(Pc5Ui *ui, const char *keep) { (void)ui; assert(!keep); delete_cleanups++; }
static void message(Pc5Ui *ui, int success, const char *title, const char *body, int back) {
    assert(!success && title[0] && body[0] && back == S_GAME_MENU);
    delete_errors++; ui->screen = S_OTHER;
}
DELETE_HELPERS
static void test_media_delete(void) {
    Pc5Ui ui = { 0 };
    ui.button = 1;
    media_delete_begin(&ui, 0);
    assert(ui.screen == S_MEDIA_DELETE && ui.button == 0 && !ui.media_delete_video);
    assert(!delete_music_saves && !delete_video_saves);
    media_delete_input(&ui, TEST_ACCEPT, TEST_BACK, TEST_ACCEPT);
    assert(ui.screen == S_GAME_MENU && !delete_music_saves && !delete_video_saves);

    media_delete_begin(&ui, 1);
    media_delete_input(&ui, PC5_KEY_RIGHT, TEST_BACK, TEST_ACCEPT);
    assert(ui.screen == S_MEDIA_DELETE && ui.button == 1 && !delete_video_saves);
    /* Back and Triangle both take priority over an Accept in the same update. */
    media_delete_input(&ui, TEST_BACK | TEST_ACCEPT, TEST_BACK, TEST_ACCEPT);
    assert(ui.screen == S_GAME_MENU && !delete_video_saves);
    media_delete_begin(&ui, 1);
    media_delete_input(&ui, PC5_KEY_RIGHT, TEST_BACK, TEST_ACCEPT);
    media_delete_input(&ui, PC5_KEY_TRIANGLE | TEST_ACCEPT, TEST_BACK, TEST_ACCEPT);
    assert(ui.screen == S_LIBRARY && !delete_video_saves);
    media_delete_begin(&ui, 0);
    media_delete_input(&ui, PC5_KEY_RIGHT, TEST_BACK, TEST_ACCEPT);
    media_delete_input(&ui, PC5_KEY_LEFT | TEST_ACCEPT, TEST_BACK, TEST_ACCEPT);
    assert(ui.screen == S_GAME_MENU && !delete_music_saves);

    media_delete_begin(&ui, 1);
    media_delete_input(&ui, PC5_KEY_RIGHT, TEST_BACK, TEST_ACCEPT);
    media_delete_input(&ui, TEST_ACCEPT, TEST_BACK, TEST_ACCEPT);
    assert(ui.screen == S_GAME_MENU && delete_video_saves == 1 && !delete_music_saves);
    assert(!delete_stops && !delete_cleanups);
    media_delete_begin(&ui, 0);
    media_delete_input(&ui, PC5_KEY_RIGHT, TEST_BACK, TEST_ACCEPT);
    media_delete_input(&ui, TEST_ACCEPT, TEST_BACK, TEST_ACCEPT);
    assert(ui.screen == S_GAME_MENU && delete_music_saves == 1 && delete_video_saves == 1);
    assert(delete_stops == 1 && delete_cleanups == 1);

    delete_fail = 1;
    media_delete_begin(&ui, 0);
    media_delete_input(&ui, PC5_KEY_RIGHT | TEST_ACCEPT, TEST_BACK, TEST_ACCEPT);
    assert(delete_errors == 1 && delete_music_saves == 2 && ui.screen == S_OTHER);
    assert(delete_stops == 1 && delete_cleanups == 1);
    media_delete_begin(&ui, 1);
    media_delete_input(&ui, PC5_KEY_RIGHT | TEST_ACCEPT, TEST_BACK, TEST_ACCEPT);
    assert(delete_errors == 2 && delete_video_saves == 2 && ui.screen == S_OTHER);
    assert(delete_stops == 1 && delete_cleanups == 1);
    puts("media delete checks passed: default Cancel, Back/Triangle priority, explicit confirmation and save failure preservation");
}
'''.replace('DELETE_HELPERS', delete_helpers)
phone_mapping = ui[ui.index('static int phone_row_kind('):ui.index('static void phone_end(')]
phone_kinds = 'enum { PC5_PHONE_COVER, PC5_PHONE_BACKGROUND, PC5_PHONE_ICON, PC5_PHONE_MUSIC, PC5_PHONE_VIDEO };\n'
unit.write_text(support + header + menu + video + errno_test.replace('DECODER_NEXT', decoder_next) + memory_test + follow_test + delete_test + phone_kinds + phone_mapping + main, encoding='utf-8')
exe = out / ('video_ui.exe' if os.name == 'nt' else 'video_ui')
if '--msvc' in sys.argv:
    args = ['cl', '/nologo', '/std:c11', '/experimental:c11atomics', '/W4', '/WX',
            '/D_CRT_SECURE_NO_WARNINGS', str(unit), '/Fe:' + str(exe), '/Fo:' + str(out / 'video_ui.obj')]
else:
    args = [os.environ.get('CC', 'cc'), '-std=c11', '-D_DEFAULT_SOURCE', '-Wall', '-Wextra', '-Werror', str(unit), '-o', str(exe)]
subprocess.run(args, check=True)
subprocess.run([str(exe)], check=True)
