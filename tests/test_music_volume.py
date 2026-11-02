#!/usr/bin/env python3
"""The console's actual music mixer loop, with synthetic audio-port operations."""
from pathlib import Path
import os, subprocess, sys
root = Path(__file__).resolve().parents[1]
out = root / '.build/music-volume-check'
out.mkdir(parents=True, exist_ok=True)
source = (root / 'title/pc5_console.c').read_text(encoding='utf-8')
source = source[source.index('/* A track playing, and the one before it'):source.index("/* The port's volume")]
support = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdatomic.h>
typedef uintptr_t pthread_t;
enum { PC5_PATH=1024, PW_OK=0, AUDIO_GRAIN=256, PC5_SOUND_RATE=48000, AUDIO_VOLUME_0DB=32768 };
typedef struct { int used; } Pc5Music;
typedef struct { int (*init)(void); int (*open)(int,int,int,int,int,int); int (*volume)(int,int,int32_t*); int (*output)(int,const void*); int (*close)(int); } PwAudioPs5Ops;
static int calls, expected;
static int init(void) { return 0; }
static int open_port(int a,int b,int c,int d,int e,int f) { (void)a;(void)b;(void)c;assert(d==256&&e==48000&&f==1);return 1; }
static int volume_port(int a,int b,int32_t *v) { (void)a;(void)b;(void)v;return 0; }
static int output(int port,const void *data) {
    (void)port;if(!data)return 0;const int16_t *s=data;
    if(calls++>20) { assert(s[0]==expected && s[1]==-expected-((expected==32767)?1:0)); return -1; }
    return 0;
}
static int close_port(int p) { (void)p;return 0; }
static int pw_audio_ps5_platform_ops(PwAudioPs5Ops *o) { *o=(PwAudioPs5Ops){init,open_port,volume_port,output,close_port};return 0; }
static int pc5_music_open(const char *p,Pc5Music *m) { (void)p; m->used=1;return 0; }
static int pc5_music_start_at(Pc5Music *m,unsigned s) { (void)m;return s>10?-1:0; }
static size_t pc5_music_read(Pc5Music *m,int16_t *p,size_t n) { (void)m;for(size_t i=0;i<n;i++){p[2*i]=32767;p[2*i+1]=-32768;}return n; }
static void pc5_music_close(Pc5Music *m) { m->used=0; }
static void plain_log(const char *s) { (void)s; }
static void pc5_copy(char *p,size_t n,const char *s) { snprintf(p,n,"%s",s); }
static int pc5_thread_start(pthread_t *t,void *(*worker)(void*),void *arg,int detach) { (void)detach;*t=1;worker(arg);return 0; }
static int pthread_join(pthread_t t,void **out) { (void)t;(void)out;return 0; }
static void music_end(void);
'''
main = r'''
int main(void) {
    const int volumes[]={0,30,65,100};
    for(int i=0;i<4;i++) {
        calls=0;expected=32767*volumes[i]/100;
        music_play_options(NULL,"synthetic.wav",volumes[i],1);
        music_end();assert(calls>20);
    }
    music_play(NULL,"");
    puts("Music mixer: actual fade loop at 0/30/65/100 percent, full-scale positive/negative samples and thread stop passed.");
    return 0;
}
'''
# Negative integer division truncates toward zero: allow its one-count asymmetry.
support = support.replace('assert(s[0]==expected && s[1]==-expected-((expected==32767)?1:0));', 'assert(s[0]==expected && s[1]>=-expected-1 && s[1]<=-expected);')
unit = out / 'volume.c'; unit.write_text(support + source + main, encoding='utf-8')
exe = out / ('volume.exe' if os.name == 'nt' else 'volume')
if '--msvc' in sys.argv:
    args = ['cl','/nologo','/std:c11','/experimental:c11atomics','/W4','/WX','/D_CRT_SECURE_NO_WARNINGS',str(unit),'/Fe:'+str(exe),'/Fo:'+str(out/'unit.obj')]
else:
    args = [os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(unit),'-o',str(exe)]
subprocess.run(args,check=True);subprocess.run([str(exe)],check=True)
