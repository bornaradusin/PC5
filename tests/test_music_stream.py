#!/usr/bin/env python3
"""Compile and exercise the actual WAV streaming functions without the PS5 SDK.

Run with Python 3 and cc on PATH, or in a Visual Studio developer prompt with
--msvc. Only the stream's source is isolated; it is not reimplemented here.
"""
import os
from pathlib import Path
import re
import struct
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / '.build' / 'music-stream-check'
out.mkdir(parents=True, exist_ok=True)
source = (root / 'title/pc5_sound.c').read_text(encoding='utf-8')
header = (root / 'title/pc5_sound.h').read_text(encoding='utf-8')
types = re.search(r'typedef struct Pc5Music \{.*?\} Pc5Music;', header, re.S).group()
functions = source[source.index('static uint32_t le32'):source.index('/* One sample of a frame')]
ui_source = (root / 'title/pc5_ui.c').read_text(encoding='utf-8')
follow = ui_source[ui_source.index('static void music_follow('):ui_source.index('static void files_open(Pc5Ui *ui, int game)\n{')]
stop = re.search(r'static void library_music_stop\(Pc5Ui \*ui\)\n\{.*?\n\}', ui_source, re.S).group()
follow = stop + '\n' + follow

def wav(channels, samples, rate=48000, bits=16):
    data = struct.pack('<' + 'h' * len(samples), *samples)
    fmt = struct.pack('<HHIIHH', 1, channels, rate, rate * channels * 2, channels * 2, bits)
    body = b'WAVEfmt ' + struct.pack('<I', len(fmt)) + fmt + b'data' + struct.pack('<I', len(data)) + data
    return b'RIFF' + struct.pack('<I', len(body)) + body

fixtures = {
    'offset.wav': wav(1, [111] * 48000 + [222] * 48000),
    'mono.wav': wav(1, [1234, -2345]),
    'stereo.wav': wav(2, [100, -200, 300, -400]),
    'wrong-rate.wav': wav(1, [1, 2], rate=44100),
    'wrong-depth.wav': wav(1, [1, 2], bits=24),
    'empty.wav': wav(1, []),
    'truncated.wav': wav(1, [1, 2])[:-1],
    'huge.wav': b'RIFF\xff\xff\xff\xffWAVEdata\xff\xff\xff\xff',
    'not-wave.wav': b'not a wav file',
    'unaligned.wav': wav(2, [1, 2, 3]),
}
# An odd unknown chunk with padding, and data before fmt, are legal WAVs.
original = fixtures['mono.wav']
body = b'WAVEJUNK\x01\x00\x00\x00x\x00' + original[36:] + original[12:36]
fixtures['chunks.wav'] = b'RIFF' + struct.pack('<I', len(body)) + body
for name, data in fixtures.items():
    (out / name).write_bytes(data)

harness = r'''
enum { S_LIBRARY, S_GAME_MENU, S_LAUNCH, S_SETTINGS, TAB_LIBRARY = 1, TAB_INSTALL = 2 };
typedef struct Pc5Game { char id[64], prefix[64]; } Pc5Game;
typedef struct Pc5Buf { uint8_t *data; size_t len; } Pc5Buf;
typedef struct Pc5Ui {
    struct { void *context; void (*music)(void *, const char *); void (*music_options)(void *, const char *, int, int); } platform;
    int screen, tab, selected;
    int list_count[1], list_at;
    struct { int count; Pc5Game games[2]; } library;
    char music_id[64], music_want[1024], music_playing[1024];
    uint64_t music_since, now;
    char root[1024];
} Pc5Ui;
enum { PC5_PATH = 1024 };
static const char *configured = "/usb0/song.wav";
static int plays, stops;
static void record(void *context, const char *path) {
    (void)context;
    if (path[0]) plays++; else stops++;
}
static void record_options(void *context, const char *path, int volume, int seconds) {
    if (path[0]) assert(volume == 30 && seconds == 0);
    else assert(volume == 0 && seconds == 0);
    record(context, path);
}
static void forbidden_legacy(void *context, const char *path) {
    (void)context; (void)path; assert(!"The extended provider must own both start and stop");
}
static void pc5_copy(char *out, size_t size, const char *text) { snprintf(out, size, "%s", text); }
static int music_config_path(Pc5Ui *ui, const Pc5Game *game, char *out, size_t size) {
    (void)ui; (void)game; pc5_copy(out, size, "config"); return 0;
}
static int pc5_read_file(const char *path, Pc5Buf *file, size_t limit) {
    (void)path;
    if (!configured || strlen(configured) > limit) return -1;
    file->data = (uint8_t *)configured; file->len = strlen(configured); return 0;
}
static void pc5_buf_free(Pc5Buf *file) { file->data = NULL; file->len = 0; }
typedef struct { int volume, music_start; } Pc5Appearance;
static int pc5_appearance_load(const char *root, const char *prefix, Pc5Appearance *o) {
    (void)root; (void)prefix; o->volume = 30; o->music_start = 0; return 0;
}
FOLLOW_SOURCE
static void test_follow(void) {
    Pc5Ui ui = { 0 };
    ui.platform.music = record;
    ui.library.count = 2;
    pc5_copy(ui.library.games[0].id, 64, "a");
    pc5_copy(ui.library.games[1].id, 64, "b");
    ui.now = 100; music_follow(&ui); assert(plays == 0);
    ui.now = 449; music_follow(&ui); assert(plays == 0);
    ui.now = 450; music_follow(&ui); assert(plays == 1 && stops == 0);
    ui.now = 900; music_follow(&ui); assert(plays == 1);
    ui.selected = 1; music_follow(&ui); assert(stops == 1);
    ui.selected = 0; ui.now = 1000; music_follow(&ui);
    ui.now = 1349; music_follow(&ui); assert(plays == 1);
    ui.now = 1350; music_follow(&ui); assert(plays == 2);
    ui.screen = S_GAME_MENU; music_follow(&ui); assert(stops == 1);
    ui.screen = S_LAUNCH; music_follow(&ui); assert(stops == 2 && !ui.music_want[0]);
    ui.screen = S_LIBRARY; ui.now = 2000; music_follow(&ui);
    ui.now = 2350; music_follow(&ui); assert(plays == 3);
    configured = NULL; ui.selected = 1; music_follow(&ui); assert(stops == 3);
    ui.now = 3000; music_follow(&ui); assert(plays == 3);
    configured = "/usb0/replacement.wav"; ui.music_id[0] = 0; music_follow(&ui);
    ui.now = 3350; music_follow(&ui); assert(plays == 4);
    ui.library.count = 0; music_follow(&ui); assert(stops == 4);
    ui.library.count = 2; ui.now = 4000; music_follow(&ui);
    ui.now = 4350; music_follow(&ui); assert(plays == 5);
    ui.tab = TAB_LIBRARY; music_follow(&ui); assert(stops == 5);
    puts("music selection: debounce, rapid browsing, menu, launch, return, no track and empty library passed");
}
static void test_options_only(void) {
    Pc5Ui ui = { 0 };
    configured = "/usb0/options.wav"; plays = stops = 0;
    ui.platform.music_options = record_options;
    ui.library.count = 2;
    pc5_copy(ui.library.games[0].id, 64, "a");
    pc5_copy(ui.library.games[1].id, 64, "b");
    ui.now = 100; music_follow(&ui);
    ui.now = 450; music_follow(&ui); assert(plays == 1 && !stops);
    ui.screen = S_GAME_MENU; music_follow(&ui); assert(plays == 1 && !stops);
    ui.selected = 1; music_follow(&ui); assert(stops == 1);
    ui.now = 800; music_follow(&ui); assert(plays == 2);
    ui.screen = S_LAUNCH; music_follow(&ui); assert(stops == 2 && !ui.music_playing[0]);
    ui.screen = S_LIBRARY; ui.now = 900; music_follow(&ui);
    ui.now = 1250; music_follow(&ui); assert(plays == 3);
    ui.screen = S_SETTINGS; music_follow(&ui); assert(stops == 3);
    ui.screen = S_LIBRARY; ui.now = 1300; music_follow(&ui);
    ui.now = 1650; music_follow(&ui); assert(plays == 4);
    /* The shared helper used by destroy, remove, backup and final launch. */
    library_music_stop(&ui); assert(stops == 4 && !ui.music_playing[0]);
    ui.platform.music = forbidden_legacy;
    ui.music_id[0] = 0; ui.music_want[0] = 0;
    ui.now = 1700; music_follow(&ui);
    ui.now = 2050; music_follow(&ui); assert(plays == 5);
    ui.screen = S_LAUNCH; music_follow(&ui); assert(stops == 5);
    ui.platform.music = NULL; ui.platform.music_options = NULL;
    library_music_stop(&ui);
    puts("music providers: options-only start, menu, selection, launch/settings stop and shared stop helper passed");
}
static void open_file(const char *dir, const char *name, Pc5Music *music) {
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    assert(pc5_music_open(path, music) == 0);
}
int main(int argc, char **argv) {
    Pc5Music music;
    int16_t samples[20];
    assert(argc == 2);
    open_file(argv[1], "mono.wav", &music);
    assert(pc5_music_read(&music, samples, 5) == 5);
    for (int i = 0; i < 5; i++) {
        assert(samples[i*2] == (i % 2 ? -2345 : 1234));
        assert(samples[i*2+1] == samples[i*2]);
    }
    pc5_music_close(&music);
    pc5_music_close(&music);
    assert(pc5_music_read(&music, samples, 1) == 0);
    open_file(argv[1], "stereo.wav", &music);
    assert(pc5_music_read(&music, samples, 3) == 3);
    assert(samples[0] == 100 && samples[1] == -200);
    assert(samples[2] == 300 && samples[3] == -400);
    assert(samples[4] == 100 && samples[5] == -200);
    pc5_music_close(&music);
    open_file(argv[1], "offset.wav", &music);
    assert(pc5_music_start_at(&music, 10) == -1);
    assert(pc5_music_read(&music, samples, 1) == 1 && samples[0] == 111);
    assert(!pc5_music_start_at(&music, 1));
    assert(pc5_music_read(&music, samples, 1) == 1 && samples[0] == 222);
    music.left = 0;
    assert(pc5_music_read(&music, samples, 1) == 1 && samples[0] == 222);
    pc5_music_close(&music);
    open_file(argv[1], "chunks.wav", &music);
    assert(pc5_music_read(&music, samples, 1) == 1 && samples[0] == 1234);
    pc5_music_close(&music);
    const char *bad[] = { "wrong-rate.wav", "wrong-depth.wav", "empty.wav", "truncated.wav",
                          "huge.wav", "not-wave.wav", "unaligned.wav", "missing.wav" };
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) {
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", argv[1], bad[i]);
        assert(pc5_music_open(path, &music) == -1 && music.file == NULL);
        pc5_music_close(&music);
    }
    puts("music stream: mono, stereo, looping, chunks and 8 invalid inputs passed");
    test_follow();
    test_options_only();
    return 0;
}
'''
unit = out / 'stream_check.c'
unit.write_text('#include <stdint.h>\n#include <stdio.h>\n#include <string.h>\n#include <limits.h>\n#include <assert.h>\n'
                + types + '\n' + functions + harness.replace('FOLLOW_SOURCE', follow), encoding='utf-8')
exe = out / ('stream_check.exe' if os.name == 'nt' else 'stream_check')
if '--msvc' in sys.argv:
    command = ['cl', '/nologo', '/std:c11', '/W4', '/WX', '/D_CRT_SECURE_NO_WARNINGS',
               str(unit), '/Fe:' + str(exe), '/Fo:' + str(out / 'stream_check.obj')]
else:
    command = [os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
               '-fsanitize=address,undefined', str(unit), '-o', str(exe)]
subprocess.run(command, check=True)
subprocess.run([str(exe), str(out)], check=True)
