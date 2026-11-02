#!/usr/bin/env python3
"""Actual Home direction handling; held directions do not toggle repeatedly."""
from pathlib import Path
import os,subprocess
r=Path(__file__).resolve().parents[1];out=r/'.build/home-focus-check';out.mkdir(parents=True,exist_ok=True)
ui=(r/'title/pc5_ui.c').read_text(encoding='utf-8')
start=ui.index('static int home_focus_input(');end=ui.index('/* Square:',start)
code=r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
enum { TAB_HOME=0,TAB_LIBRARY=1,PC5_KEY_DOWN=1,PC5_KEY_UP=2,PC5_KEY_R3=4 };
typedef struct { int tab,focus,dirty;struct { int count; } library; } Pc5Ui;
'''
tests=r'''
int main(void) {
 Pc5Ui u={0};u.library.count=1;
 assert(!home_focus_input(&u,PC5_KEY_UP)&&!u.focus);
 assert(home_focus_input(&u,PC5_KEY_DOWN)&&u.focus&&u.dirty);
 assert(!home_focus_input(&u,PC5_KEY_DOWN)&&u.focus);
 assert(!home_focus_input(&u,PC5_KEY_R3)&&u.focus);
 assert(home_focus_input(&u,PC5_KEY_UP)&&!u.focus);
 assert(!home_focus_input(&u,PC5_KEY_UP)&&!u.focus);
 u.tab=TAB_LIBRARY;assert(!home_focus_input(&u,PC5_KEY_DOWN)&&!u.focus);
 u.tab=TAB_HOME;u.library.count=0;assert(!home_focus_input(&u,PC5_KEY_DOWN)&&!u.focus);
 puts("Home focus passed: Down enters, Up leaves, repeats stable, Library/empty Home unaffected.");
}
'''
assert 'T("Focus mode")' not in ui
p=out/'focus.c';p.write_text(code+ui[start:end]+tests,encoding='utf-8');exe=out/('focus.exe' if os.name=='nt' else 'focus')
subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror',str(p),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
