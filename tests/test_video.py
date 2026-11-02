#!/usr/bin/env python3
"""Linux: compile real FFmpeg background player and test synthetic H.264 input."""
from pathlib import Path
import os
import shlex
import subprocess

root = Path(__file__).resolve().parents[1]
out = root / '.build/video-check'
out.mkdir(parents=True, exist_ok=True)
flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'libavformat', 'libavcodec', 'libswscale', 'libavutil'], text=True))
exe = out / 'test_video'
subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-D_DEFAULT_SOURCE', '-DPC5_VIDEO_FFMPEG', '-DPC5_HEAP_ONLY',
                '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-g', '-O1', '-fsanitize=address,undefined',
                '-I' + str(root / 'title'), str(root / 'tests/test_video.c'), str(root / 'title/pc5_video.c'),
                str(root / 'title/pc5_base.c'), str(root / 'title/pc5_appearance.c'), *flags, '-lpthread', '-lm', '-o', str(exe)], check=True)
def generate(path, size, rate):
    subprocess.run(['ffmpeg', '-nostdin', '-hide_banner', '-loglevel', 'error', '-y',
                    '-f', 'lavfi', '-i', f'testsrc2=size={size}:rate={rate}',
                    '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000',
                    '-t', '0.2', '-c:v', 'libx264', '-preset', 'ultrafast', '-pix_fmt', 'yuv420p',
                    '-c:a', 'aac', '-movflags', '+faststart', str(path)], check=True)
source = out / '1080p60-with-audio.mp4'
fast = out / 'unsupported-120fps.mp4'
bad = out / 'damaged.mp4'
generate(source, '1920x1080', 60)
generate(fast, '320x180', 120)
bad.write_bytes(b'not a video')
dest = out / 'local-copy.mp4'
subprocess.run([str(exe), str(source), str(dest), str(fast), str(bad)], check=True)
