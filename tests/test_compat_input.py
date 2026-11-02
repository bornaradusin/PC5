#!/usr/bin/env python3
"""Actual compatibility input: keyboard success/failure, details and clearing."""
from pathlib import Path
import os, subprocess
root = Path(__file__).resolve().parents[1]
out = root / '.build/compat-input-check'
out.mkdir(parents=True, exist_ok=True)
source = (root / 'title/pc5_ui_enhancements_input.h').read_text(encoding='utf-8')
function = source[source.index('static void compatibility_input('):source.index('static void look_begin(')]
support = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define T(x) (x)
#define PC5_COMPATIBILITY "wiki"
#define PC5_COMPATIBILITY_NEW "new-report"
enum { PC5_COMPAT_MAX=4, PC5_KEY_L2=1, PC5_KEY_R2=2, PC5_KEY_UP=4, PC5_KEY_DOWN=8,
 PC5_KEY_SQUARE=16, PC5_KEY_TRIANGLE=32, ACCEPT=64, BACK=128, PC5_KEY_OPTIONS=256, PC5_KEY_LEFT=512, PC5_KEY_RIGHT=1024,
 S_LIBRARY=1, S_COMPAT_SEARCH, S_COMPATIBILITY, S_COMPAT_REPORT, S_COMPAT_SHARE, TAB_LIBRARY=1 };
typedef struct { char report[32]; } Entry;
typedef struct Pc5Ui {
 struct { int count; Entry entries[4]; } compatibility;
 struct { int (*keyboard_open)(void *,const char *,const char *);void *context; } platform;
 uint64_t now,compat_refresh_at;
 int compat_detail,compat_scroll,compat_at,compat_filter,dirty,tab,screen,compatibility_qr;
 char compat_query[128];
} Pc5Ui;
static int opened,rc,errors,refreshed;
static int keyboard(void *p,const char *title,const char *query) {
 (void)p;assert(!strcmp(title,"Search games"));assert(!strcmp(query,"Prince"));opened++;return rc;
}
static void go(Pc5Ui *u,int screen) { u->screen=screen; }
static void pc5_compat_refresh(void *p) { (void)p;refreshed++; }
static int compatibility_indices(Pc5Ui *u,int *indices) { indices[0]=0;return u->compatibility.count; }
static void list_move(int *at,int count,uint32_t k) { (void)at;(void)count;(void)k; }
static int pc5_qr_make(const char *url,int *q) { assert(!strcmp(url,"new-report")||!strcmp(url,"wiki"));*q=1;return 0; }
static void say(Pc5Ui *u,const char *fmt,int result) { (void)u;(void)fmt;assert(result==rc||result==-1); }
static void message(Pc5Ui *u,int ok,const char *title,const char *body,int back) {
 (void)u;assert(!ok&&title[0]&&body[0]&&back==S_COMPATIBILITY);errors++;
}
'''
tests = r'''
int main(void) {
 Pc5Ui u={0};u.platform.keyboard_open=keyboard;u.compatibility.count=1;
 strcpy(u.compat_query,"Prince");u.compat_detail=1;u.compat_scroll=9;
 compatibility_input(&u,PC5_KEY_L2,BACK,ACCEPT);
 assert(opened==1&&u.screen==S_COMPAT_SEARCH&&!u.compat_detail&&!u.compat_scroll);
 rc=-7;compatibility_input(&u,PC5_KEY_L2,BACK,ACCEPT);assert(opened==2&&errors==1);
 u.platform.keyboard_open=0;compatibility_input(&u,PC5_KEY_L2,BACK,ACCEPT);assert(errors==2);
 u.compat_filter=3;u.compat_detail=1;u.compat_scroll=8;u.compat_at=2;
 compatibility_input(&u,PC5_KEY_R2,BACK,ACCEPT);
 assert(!u.compat_query[0]&&!u.compat_at&&!u.compat_detail&&!u.compat_scroll&&u.compat_filter==3&&u.dirty);
 compatibility_input(&u,PC5_KEY_R2,BACK,ACCEPT);assert(!u.compat_query[0]);
 u.screen=S_COMPATIBILITY;compatibility_input(&u,BACK,BACK,ACCEPT);assert(u.screen==S_COMPATIBILITY);
 compatibility_input(&u,PC5_KEY_OPTIONS,BACK,ACCEPT);assert(u.screen==S_COMPAT_SHARE&&u.compatibility_qr==1);
 compatibility_input(&u,PC5_KEY_TRIANGLE,BACK,ACCEPT);assert(refreshed==1);
 u.compatibility.count=19;u.compat_at=0;
 compatibility_input(&u,PC5_KEY_RIGHT,BACK,ACCEPT);assert(u.compat_at==8);
 compatibility_input(&u,PC5_KEY_RIGHT,BACK,ACCEPT);assert(u.compat_at==16);
 compatibility_input(&u,PC5_KEY_RIGHT,BACK,ACCEPT);assert(u.compat_at==18);
 compatibility_input(&u,PC5_KEY_RIGHT,BACK,ACCEPT);assert(u.compat_at==18);
 compatibility_input(&u,PC5_KEY_LEFT,BACK,ACCEPT);assert(u.compat_at==10);
 compatibility_input(&u,PC5_KEY_LEFT,BACK,ACCEPT);assert(u.compat_at==2);
 compatibility_input(&u,PC5_KEY_LEFT,BACK,ACCEPT);assert(u.compat_at==0);
 u.compat_detail=1;u.compat_at=7;compatibility_input(&u,PC5_KEY_RIGHT,BACK,ACCEPT);assert(u.compat_at==7);
 u.compat_detail=0;u.compatibility.count=0;u.compat_at=0;compatibility_input(&u,PC5_KEY_RIGHT|PC5_KEY_LEFT,BACK,ACCEPT);assert(!u.compat_at);
 puts("Compatibility input passed: search from details, keyboard error, clear preserves filter, Back and refresh.");
}
'''
unit = out / 'input.c'
unit.write_text(support + function + tests, encoding='utf-8')
exe = out / ('input.exe' if os.name == 'nt' else 'input')
subprocess.run([os.environ.get('CC','cc'), '-std=c11', '-Wall', '-Wextra', '-Werror', str(unit), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)
