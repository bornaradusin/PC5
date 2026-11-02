#!/usr/bin/env python3
"""Real export selection helper: writable folders, collision and cancellation state."""
from pathlib import Path
import os,subprocess
r=Path(__file__).resolve().parents[1]; out=r/'.build/export-picker-check';out.mkdir(parents=True,exist_ok=True)
ui=(r/'title/pc5_ui.c').read_text(encoding='utf-8')
start=ui.index('static void look_export_here(')
helper=ui[start:ui.index('static void files_update(',start)]
code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#define T(x) (x)
enum { PC5_PATH=1024,S_FILES=1 };
typedef struct { char files_path[1024],launch_id[64];uint64_t now;int files_look; } Pc5Ui;
static int writable=1,closed,started,errors,collide;
static char saved[1024];
static int pc5_format(char *out,size_t n,const char *fmt,...) { va_list a;va_start(a,fmt);int c=vsnprintf(out,n,fmt,a);va_end(a);return c<0||(size_t)c>=n; }
static int files_writable(Pc5Ui *u,const char *dir) { (void)u;return writable&&dir[0]; }
static int pc5_exists(const char *path) { return collide&&strstr(path,"-0.pc5look")!=NULL; }
static void message(Pc5Ui *u,int ok,const char *title,const char *body,int screen) { (void)u;assert(!ok&&title[0]&&body[0]&&screen==S_FILES);errors++; }
static void files_close(Pc5Ui *u) { closed++;u->files_look=0;u->files_path[0]=0; }
static void look_begin(Pc5Ui *u,const char *path,int restore) { assert(!restore&&!u->files_look);started++;snprintf(saved,sizeof(saved),"%s",path); }
'''
tests=r'''
int main(void) {
 Pc5Ui u={0};strcpy(u.launch_id,"game");u.now=12;u.files_look=2;
 look_export_here(&u);assert(errors==1&&!closed&&!started);
 strcpy(u.files_path,"/data/backups");writable=0;look_export_here(&u);assert(errors==2&&!closed&&!started&&u.files_look==2);
 writable=1;look_export_here(&u);assert(started==1&&closed==1&&!strcmp(saved,"/data/backups/game-12-0.pc5look"));
 strcpy(u.files_path,"/mnt/usb0/My backups");u.files_look=2;collide=1;look_export_here(&u);
 assert(started==2&&closed==2&&!strcmp(saved,"/mnt/usb0/My backups/game-12-1.pc5look"));
 puts("Export picker passed: empty/read-only rejected, internal/USB paths, collision suffix, picker cleanup.");
}
'''
p=out/'picker.c';p.write_text(code+helper+tests,encoding='utf-8');exe=out/('picker.exe' if os.name=='nt' else 'picker')
subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror',str(p),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
