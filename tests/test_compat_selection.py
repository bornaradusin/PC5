#!/usr/bin/env python3
"""Exercise the actual UI refresh commit against reordered/failed tables."""
from pathlib import Path
import os, subprocess, sys
root=Path(__file__).resolve().parents[1]
out=root/'.build/compat-selection-check'
out.mkdir(parents=True,exist_ok=True)
ui=(root/'title/pc5_ui.c').read_text(encoding='utf-8')
start=ui.index('    if (ui->compat_initialized && ui->compatibility.running && atomic_load(&ui->compatibility.done)) {')
end=ui.index('    video_follow(ui);',start)
commit=ui[start:end]
helpers=(root/'title/pc5_ui_enhancements_input.h').read_text(encoding='utf-8')
indices=helpers[helpers.index('static int compatibility_indices('):helpers.index('static void compatibility_input(')]
support=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
enum { PC5_COMPAT_MAX=2048 };
typedef struct { char title[256],report[256];int status; } Pc5CompatEntry;
typedef struct { Pc5CompatEntry *entries;int count,running,result;atomic_int done; } Pc5Compat;
typedef struct {
 Pc5Compat compatibility;int compat_initialized,compat_at,compat_scroll,compat_detail,compat_filter,dirty;
 char compat_query[128];
} Pc5Ui;
static Pc5CompatEntry *incoming;static int incoming_count,fail;
static void pc5_copy(char *out,size_t size,const char *text) { snprintf(out,size,"%s",text); }
static int pc5_compat_matches(const Pc5CompatEntry *e,const char *q,int filter) { return (!filter||e->status==filter)&&(!q[0]||strstr(e->title,q)); }
static int pc5_compat_poll(Pc5Compat *l) {
 assert(l->running&&atomic_load(&l->done));l->running=0;l->result=fail?-1:0;
 if(!fail) {
  /* Reusing old pointers after poll must not accidentally pass this test. */
  memset(l->entries,0,(size_t)l->count*sizeof(*l->entries));
  l->entries=incoming;l->count=incoming_count;
 }
 return 1;
}
'''
tests=r'''
static void commit(Pc5Ui *ui) { COMMIT }
static void load(Pc5CompatEntry *r,const char *name,const char *url,int status) { pc5_copy(r->title,sizeof(r->title),name);pc5_copy(r->report,sizeof(r->report),url);r->status=status; }
static void next(Pc5Ui *ui,Pc5CompatEntry *entries,int count,int failed) { incoming=entries;incoming_count=count;fail=failed;ui->compatibility.running=1;atomic_store(&ui->compatibility.done,1);commit(ui); }
int main(void) {
 Pc5CompatEntry a[3]={0},b[4]={0},c[4]={0},d[4]={0},e[2]={0};
 load(&a[0],"Alpha","a",1);load(&a[1],"Bravo","b",1);load(&a[2],"Charlie","c",1);
 load(&b[0],"Added","x",1);load(&b[1],"Alpha","a",1);load(&b[2],"Bravo","b",1);load(&b[3],"Charlie","c",1);
 Pc5Ui ui={0};ui.compat_initialized=1;ui.compatibility.entries=a;ui.compatibility.count=3;ui.compat_at=1;ui.compat_detail=1;ui.compat_scroll=7;
 next(&ui,b,4,0);assert(ui.compat_at==2&&ui.compat_detail==1&&ui.compat_scroll==7&&ui.dirty);
 ui.dirty=0;next(&ui,NULL,0,1);assert(ui.compat_at==2&&ui.compat_detail==1&&ui.compat_scroll==7&&ui.dirty);
 memcpy(c,b,sizeof(c));pc5_copy(c[2].report,sizeof(c[2].report),"new-report");
 next(&ui,c,4,0);assert(ui.compat_at==2&&ui.compat_detail==1&&ui.compat_scroll==7);
 memcpy(d,c,sizeof(d));d[2].status=2;ui.compat_filter=1;
 next(&ui,d,4,0);assert(ui.compat_at==2&&!ui.compat_detail&&!ui.compat_scroll);
 ui.compat_filter=0;ui.compat_at=2;ui.compat_detail=1;ui.compat_scroll=5;
 load(&e[0],"Added","x",1);load(&e[1],"Alpha","a",1);
 next(&ui,e,2,0);assert(ui.compat_at==1&&!ui.compat_detail&&!ui.compat_scroll);
 puts("Compatibility selection: reordered/failed refresh, newer report, changed filter membership and removed game passed.");return 0;
}
'''.replace('COMMIT',commit)
unit=out/'selection.c';unit.write_text(support+indices+tests,encoding='utf-8')
exe=out/('selection.exe' if os.name=='nt' else 'selection')
if '--msvc' in sys.argv:
 args=['cl','/nologo','/std:c11','/experimental:c11atomics','/W4','/WX','/D_CRT_SECURE_NO_WARNINGS',str(unit),'/Fe:'+str(exe),'/Fo:'+str(out/'selection.obj')]
else:
 args=[os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(unit),'-o',str(exe)]
subprocess.run(args,check=True);subprocess.run([str(exe)],check=True)
