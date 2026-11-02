#!/usr/bin/env python3
"""Linux host: real media/backup and compatibility code with ASan/UBSan."""
from pathlib import Path
import os, shlex, subprocess, tempfile
root = Path(__file__).resolve().parents[1]
out = root / '.build/enhancements-check'
out.mkdir(parents=True, exist_ok=True)
flags = shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','libavformat','libavcodec','libswscale','libavutil'],text=True))
common = [os.environ.get('CC','cc'),'-std=c11','-D_DEFAULT_SOURCE','-DPC5_HEAP_ONLY','-DPC5_VIDEO_FFMPEG',
          '-Wall','-Wextra','-Wpedantic','-Werror','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(root/'title')]
sources = [str(root/'title'/('pc5_'+name+'.c')) for name in ('compat','appearance','look','video','sound','image','stb','base')]
subprocess.run([*common,str(root/'tests/test_enhancements.c'),*sources,*flags,'-lpthread','-lm','-o',str(out/'test_enhancements')],check=True)
subprocess.run([*common,str(root/'tests/test_video_options.c'),*[str(root/'title'/('pc5_'+name+'.c')) for name in ('video','appearance','base')],*flags,'-lpthread','-lm','-o',str(out/'test_video_options')],check=True)
video = out/'colours.mp4'
subprocess.run(['ffmpeg','-nostdin','-hide_banner','-loglevel','error','-y',
    '-f','lavfi','-i','color=c=red:s=320x240:r=30:d=1','-f','lavfi','-i','color=c=blue:s=320x240:r=30:d=1',
    '-f','lavfi','-i','color=c=green:s=320x240:r=30:d=1','-filter_complex','[0:v][1:v][2:v]concat=n=3:v=1:a=0[v]',
    '-map','[v]','-c:v','libx264','-preset','ultrafast','-pix_fmt','yuv420p',str(video)],check=True)
with tempfile.TemporaryDirectory(prefix='data-',dir=out) as folder:
    subprocess.run([str(out/'test_enhancements'),folder,str(root/'tests/fixtures/community-compatibility.md'),str(video)],check=True)
subprocess.run([str(out/'test_video_options'),str(video)],check=True)
