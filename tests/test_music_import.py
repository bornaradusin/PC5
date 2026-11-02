#!/usr/bin/env python3
"""Compile actual importer; test generated MP3 rate/channel/CBR/VBR matrix.

Needs Python 3, FFmpeg with libmp3lame, and cc, or --msvc in a VS developer
prompt. FFMPEG can point to an executable. Generates only synthetic tones.
"""
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import wave

root = Path(__file__).resolve().parents[1]
out = root / '.build/music-import-check'
out.mkdir(parents=True, exist_ok=True)
source = (root / 'title/pc5_sound.c').read_text(encoding='utf-8')
header = (root / 'title/pc5_sound.h').read_text(encoding='utf-8')
types = re.search(r'typedef struct Pc5Music \{.*?\} Pc5Music;', header, re.S).group()
job = re.search(r'typedef struct Pc5MusicImport \{.*?PC5_MUSIC_CANCELLED = -4 \};', header, re.S).group()
job = job.replace('pthread_t', 'MusicTestThread')
stream = source[source.index('static uint32_t le32'):source.index('/* One sample of a frame')]
importer = source[source.index('/* ---- importing library music'):]
ui_source = (root / 'title/pc5_ui.c').read_text(encoding='utf-8')
association = ui_source[ui_source.index('static int music_config_path('):ui_source.index('static void music_import_begin(')]
if os.name == 'nt':
    importer = importer.replace('#include <unistd.h>', '#include <io.h>\n#define unlink _unlink')
    importer = importer.replace('#include "third_party/dr_mp3.h"',
        '#pragma warning(push, 0)\n#include "third_party/dr_mp3.h"\n#pragma warning(pop)')
support = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <limits.h>
#include <assert.h>
#include <sys/stat.h>
#ifndef S_ISREG
#define S_ISREG(mode) (((mode) & S_IFMT) == S_IFREG)
#endif
typedef uintptr_t MusicTestThread;
enum { PC5_PATH = 1024 };
static void *pc5_alloc(size_t size) { return calloc(1, size); }
static void pc5_free(void *p) { free(p); }
static void pc5_copy(char *out, size_t size, const char *text) { snprintf(out, size, "%s", text); }
static int pc5_format(char *out, size_t size, const char *fmt, ...) {
    va_list args; va_start(args, fmt); int n = vsnprintf(out, size, fmt, args); va_end(args);
    return n < 0 || (size_t)n >= size ? -1 : 0;
}
static int pc5_ends_with_ci(const char *text, const char *suffix) {
    size_t n = strlen(text), m = strlen(suffix); if (n < m) return 0;
    for (size_t i = 0; i < m; i++) {
        char a = text[n-m+i], b = suffix[i];
        if (a >= 'A' && a <= 'Z') a += 'a'-'A';
        if (b >= 'A' && b <= 'Z') b += 'a'-'A';
        if (a != b) return 0;
    }
    return 1;
}
/* Synchronous worker adapter isolates conversion/commit, not thread timing. */
static int pc5_thread_start(MusicTestThread *thread, void *(*fn)(void *), void *arg, int detached) {
    (void)thread; (void)detached; fn(arg); return 0;
}
#define pthread_join music_test_join
static int music_test_join(MusicTestThread thread, void **result) { (void)thread; (void)result; return 0; }
'''
ownership = r'''
enum { PC5_ID = 64 };
typedef struct Pc5Game { char id[64], prefix[64]; } Pc5Game;
typedef struct Pc5Ui {
    char root[1024], launch_id[64], music_id[64];
    struct { int count; Pc5Game games[1]; } library;
} Pc5Ui;
static int pc5_library_find(const void *library, const char *id) { (void)library; return !strcmp(id, "game") ? 0 : -1; }
static int pc5_mkdirs(const char *path) { struct stat st; return !stat(path, &st) && (st.st_mode & S_IFDIR) ? 0 : -1; }
static int pc5_write_file(const char *path, const void *data, size_t size) {
    FILE *f = fopen(path, "wb"); if (!f) return -1;
    int ok = fwrite(data, 1, size, f) == size; if (fclose(f)) ok = 0; return ok ? 0 : -1;
}
ASSOCIATION_SOURCE
static void test_association(const char *root) {
    Pc5Ui ui = { 0 };
    pc5_copy(ui.root, sizeof(ui.root), root);
    pc5_copy(ui.launch_id, sizeof(ui.launch_id), "game");
    pc5_copy(ui.library.games[0].prefix, 64, "game");
    char a[1024], b[1024], dir[1024], external[1024], config[1024];
    assert(!music_slots(&ui, a, b, dir));
    assert(!pc5_format(external, sizeof(external), "%s/source.mp3", root));
    assert(!pc5_write_file(external, "source", 6));
    assert(!pc5_write_file(a, "local a", 7) && !pc5_write_file(b, "local b", 7));
    assert(!music_save(&ui, a));
    music_cleanup(&ui, a);
    struct stat st;
    assert(!stat(a, &st) && stat(b, &st) && !stat(external, &st));
    assert(!music_config_path(&ui, &ui.library.games[0], config, sizeof(config)));
    assert(!stat(config, &st));
    assert(!music_save(&ui, "")); music_cleanup(&ui, NULL);
    assert(stat(config, &st) && stat(a, &st) && !stat(external, &st));
    puts("music association: game-local slots, inactive-slot cleanup, remove and original preservation passed");
}
'''
main = r'''
int main(int argc, char **argv) {
    Pc5MusicImport job;
    if (argc == 2) { test_association(argv[1]); return 0; }
    if (argc < 3) return 2;
    if (argc == 4) {
        memset(&job, 0, sizeof(job));
        atomic_init(&job.stop, 1); atomic_init(&job.done, 0);
        pc5_copy(job.source, sizeof(job.source), argv[1]);
        pc5_copy(job.destination, sizeof(job.destination), argv[2]);
        music_import_worker(&job);
    } else {
        if (pc5_music_import_start(&job, argv[1], argv[2])) return 3;
        pc5_music_import_end(&job);
    }
    printf("result=%d\n", job.result);
    return job.result == 0 ? 0 : 10;
}
'''
unit = out / 'import_check.c'
unit.write_text(support + types + '\n' + job + '\n' + stream + importer
                + ownership.replace('ASSOCIATION_SOURCE', association) + main, encoding='utf-8')
exe = out / ('import_check.exe' if os.name == 'nt' else 'import_check')
if '--msvc' in sys.argv:
    compile_args = ['cl', '/nologo', '/std:c11', '/experimental:c11atomics', '/W4', '/WX', '/D_CRT_SECURE_NO_WARNINGS',
                    '/D_CRT_NONSTDC_NO_DEPRECATE', '/I' + str(root / 'title'), str(unit),
                    '/Fe:' + str(exe), '/Fo:' + str(out / 'import_check.obj')]
else:
    compile_args = [os.environ.get('CC', 'cc'), '-std=c11', '-D_DEFAULT_SOURCE', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                    '-fsanitize=address,undefined', '-I' + str(root / 'title'), str(unit), '-lm', '-o', str(exe)]
subprocess.run(compile_args, check=True)
association_root = out / 'association'
(association_root / 'prefixes/game/pc5/music').mkdir(parents=True, exist_ok=True)
association_path = association_root.as_posix()
if os.name == 'nt':
    association_path = association_path[2:]  # current-drive absolute path, begins with '/'
subprocess.run([str(exe), association_path], check=True)

def run_import(src, dest, *, good=True, cancel=False):
    # The Windows CRT does not replace an existing target with rename(). The
    # application always imports to the other, inactive slot; remove fixtures
    # only for successful tests, preserve a sentinel for failure tests.
    if good and dest.exists():
        dest.unlink()
    result = subprocess.run([str(exe), str(src), str(dest)] + (['cancel'] if cancel else []), capture_output=True, text=True)
    assert (result.returncode == 0) == good, (src.name, result.stdout, result.stderr)
    assert not Path(str(dest) + '.part').exists(), 'Partial file left after import'
    return result.stdout

ffmpeg = os.environ.get('FFMPEG', 'ffmpeg')
matrix = 0
for rate in (8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000):
    for channels in (1, 2):
        for mode in ('cbr', 'vbr'):
            src = out / f'tone-{rate}-{channels}-{mode}.mp3'
            args = ['-b:a', '128k' if rate >= 32000 else '32k'] if mode == 'cbr' else ['-q:a', '4']
            subprocess.run([ffmpeg, '-nostdin', '-hide_banner', '-loglevel', 'error', '-y', '-f', 'lavfi',
                            '-i', 'sine=frequency=440:duration=0.25', '-ar', str(rate), '-ac', str(channels),
                            '-c:a', 'libmp3lame', *args, '-id3v2_version', '3', '-metadata', 'title=PC5 synthetic test',
                            str(src)], check=True)
            dest = out / 'imported.wav'
            run_import(src, dest)
            with wave.open(str(dest), 'rb') as audio:
                assert (audio.getnchannels(), audio.getsampwidth(), audio.getframerate()) == (2, 2, 48000)
                assert 0.15 <= audio.getnframes() / 48000 <= 0.6, (src.name, audio.getnframes())
                values = struct.unpack('<' + 'h' * audio.getnframes() * 2, audio.readframes(audio.getnframes()))
                assert max(abs(value) for value in values) > 100
            src.unlink()  # Imported output stays playable without the source.
            with wave.open(str(dest), 'rb') as audio:
                assert audio.readframes(1)
            matrix += 1

for rate, bitrate, tag in ((8000, '8k', '3'), (48000, '320k', '4')):
    src = out / f'bitrate-{bitrate}.MP3'
    subprocess.run([ffmpeg, '-nostdin', '-hide_banner', '-loglevel', 'error', '-y', '-f', 'lavfi',
                    '-i', 'sine=frequency=660:duration=0.25', '-ar', str(rate), '-ac', '2',
                    '-c:a', 'libmp3lame', '-b:a', bitrate, '-id3v2_version', tag,
                    '-metadata', 'title=PC5 ID3 metadata test', str(src)], check=True)
    run_import(src, out / 'imported.wav')

for rate in (8000, 44100, 48000, 96000, 192000):
    src = out / f'pcm-{rate}.wav'
    with wave.open(str(src), 'wb') as audio:
        audio.setparams((1, 2, rate, 0, 'NONE', 'not compressed'))
        audio.writeframes(b''.join(struct.pack('<h', int(12000 * math.sin(i * 2 * math.pi * 440 / rate))) for i in range(rate // 4)))
    run_import(src, out / 'imported.wav')
    with wave.open(str(out / 'imported.wav'), 'rb') as audio:
        assert abs(audio.getnframes() - 12000) <= 1

sentinel = out / 'previous.wav'
sentinel.write_bytes(b'previous music remains unchanged')
bad = out / 'broken.mp3'
bad.write_bytes(b'ID3' + b'\0' * 100)
for src, cancel in ((bad, False), (out / 'missing.mp3', False), (out / 'pcm-48000.wav', True)):
    run_import(src, sentinel, good=False, cancel=cancel)
    assert sentinel.read_bytes() == b'previous music remains unchanged'
print(f'music import: {matrix} MP3 combinations, 8/320 kbps, ID3v2.3/v2.4, uppercase extension, 5 WAV rates, source removal, invalid/missing input and cancellation passed')
