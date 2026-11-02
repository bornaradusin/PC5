#!/usr/bin/env python3
"""Run the real library appearance handler, including legacy phone regressions.

HTTP IO/library listing is isolated; imports are tested by test_enhancements.c
and the real decoder tests. Socket-level checks also run in test_web.py.
"""
from pathlib import Path
import ast, os, subprocess, sys, re
root = Path(__file__).resolve().parents[1]
out = root / '.build/phone-library-check'
out.mkdir(parents=True, exist_ok=True)
(out / 'data').mkdir(exist_ok=True)
tree = ast.parse((root / 'tests/test_phone.py').read_text(encoding='utf-8'))
values = {n.targets[0].id: ast.literal_eval(n.value) for n in tree.body
          if isinstance(n, ast.Assign) and isinstance(n.targets[0], ast.Name) and n.targets[0].id in ('support', 'tests')}
support = values['support']
extra = r'''
static uint64_t upload_advance;
static size_t upload_chunk;
typedef struct { int pc5; char id[PC5_ID], prefix[PC5_ID], title[PC5_NAME]; } Pc5Game;
typedef struct { int count; Pc5Game games[3]; } Pc5Library;
static void *pc5_alloc(size_t n) { return calloc(1,n); }
static void pc5_free(void *p) { free(p); }
static int pc5_library_load(const char *root, Pc5Library *l) {
    (void)root; l->count=3;
    l->games[0].pc5=1;strcpy(l->games[0].id,"alpha");strcpy(l->games[0].prefix,"alpha");strcpy(l->games[0].title,"Game </script> & test");
    strcpy(l->games[1].id,"legacy");strcpy(l->games[1].prefix,"legacy");
    l->games[2].pc5=1;strcpy(l->games[2].id,"default");strcpy(l->games[2].prefix,"default");return 0;
}
static int pc5_library_find(const Pc5Library *l,const char *id) { for(int i=0;i<l->count;i++)if(!strcmp(l->games[i].id,id))return i;return -1; }
static int pc5_buf_printf(Pc5Buf *b,const char *fmt,...) { char s[1024];va_list a;va_start(a,fmt);int n=vsnprintf(s,sizeof(s),fmt,a);va_end(a);assert(n>=0&&(size_t)n<sizeof(s));return pc5_buf_put(b,s,(size_t)n); }
static void respond_buf(Conn *c,Pc5Buf *b) { respond(c,200,"application/json",b->data,b->len);pc5_buf_free(b); }
'''
header = (root / 'title/pc5_web.h').read_text(encoding='utf-8')
api = header[header.index('enum { PC5_PHONE_COVER'):header.index('/* For the tests:')]
impl = (root / 'title/pc5_web_phone.h').read_text(encoding='utf-8')
manager = (root / 'title/pc5_web_library.h').read_text(encoding='utf-8')
tests = values['tests'].replace('int main(int argc,char **argv)', 'static int legacy_main(int argc,char **argv)')
tests = tests.replace('static int read_body(Conn *c,uint64_t *left,void *p,size_t n) {',
    'static int read_body(Conn *c,uint64_t *left,void *p,size_t n) {\n'
    '    clock_ms += upload_advance; if (upload_chunk && n > upload_chunk) n = upload_chunk;')
new_tests = r'''
static int request(const char *url,const char *suffix,const char *method,const char *data,uint64_t length) {
    unsigned char buffer[BUFFER];Conn c={0};char path[PC5_PATH];
    snprintf(path,sizeof(path),"%s%s",strstr(url,"/appearance/")+12,suffix);
    snprintf(c.method,sizeof(c.method),"%s",method);c.length=length;c.buffer=buffer;c.body=(const unsigned char*)data;c.have=data?strlen(data):0;
    appearance_answer(&c,path);assert(!web.transfers);return answer_code;
}
int main(int argc,char **argv) {
    assert(!legacy_main(argc,argv)); char url[160],file[PC5_PATH],id[PC5_ID];int kind=-1;
    assert(!pc5_web_appearance_begin(url,sizeof(url)));
    assert(request(url,"","GET",NULL,0)==200 && strstr(answer_body,"Library appearance"));
    FILE *page=fopen("appearance-preview.html","wb");assert(page);fwrite(answer_body,1,strlen(answer_body),page);fclose(page);
    assert(request(url,"/games","GET",NULL,0)==200);
    assert(strstr(answer_body,"alpha") && !strstr(answer_body,"legacy") && !strstr(answer_body,"default"));
    assert(request(url,"/api/logs","GET",NULL,0)==404);
    assert(request(url,"/upload/../0.png","PUT","x",1)==400);
    assert(request(url,"/upload/alpha/8.png","PUT","x",1)==400);
    assert(request(url,"/upload/alpha/3.mp4","PUT","x",1)==400);
    assert(request(url,"/upload/legacy/0.png","PUT","x",1)==404);
    assert(request(url,"/upload/default/0.png","PUT","x",1)==404);
    appearance_uploading=1;
    assert(request(url,"/upload/alpha/0.png","PUT","picture",7)==409);
    appearance_uploading=0;
    assert(request(url,"/upload/alpha/0.png","PUT","picture",7)==201);
    assert(request(url,"/upload/alpha/3.mp3","PUT","music",5)==409);
    assert(pc5_web_phone_take(file,sizeof(file))); assert(pc5_web_appearance_target(id,sizeof(id),&kind));
    assert(!strcmp(id,"alpha") && kind==0);unlink(file);
    assert(request(url,"/status","GET",NULL,0)==200 && strstr(answer_body,"\"processing\":true"));
    pc5_web_appearance_finish(0);
    assert(request(url,"/status","GET",NULL,0)==200 && strstr(answer_body,"\"result\":-1"));
    assert(request(url,"/upload/alpha/3.mp3","PUT","music",5)==201);
    assert(pc5_web_phone_take(file,sizeof(file)));assert(pc5_web_appearance_target(id,sizeof(id),&kind)&&kind==3);unlink(file);
    pc5_web_appearance_finish(1);
    assert(request(url,"/status","GET",NULL,0)==200 && strstr(answer_body,"\"result\":1"));
    /* Progressing library upload can exceed ten wall-clock minutes, but
     * the one-shot QR retains its original fixed expiry. */
    upload_advance=200001;upload_chunk=1;
    uint64_t expiry=phone.expires;
    assert(request(url,"/upload/alpha/3.mp3","PUT","slow",4)==201);
    assert(clock_ms>expiry && phone.expires>clock_ms);
    upload_advance=0;upload_chunk=0;
    assert(pc5_web_phone_take(file,sizeof(file)));unlink(file);pc5_web_appearance_finish(1);
    char one_shot[160];assert(!pc5_web_phone_begin(3,"Game",one_shot,sizeof(one_shot)));
    expiry=phone.expires;upload_advance=200001;upload_chunk=1;
    /* A game's own link, as the library's: kept alive while bytes still arrive. */
    assert(call(one_shot,"/upload.mp3","PUT","slow",4,0)==201);
    assert(phone.expires>expiry);
    upload_advance=0;upload_chunk=0;
    assert(!pc5_web_appearance_begin(url,sizeof(url)));
    clock_ms+=600001;assert(request(url,"/games","GET",NULL,0)==404);
    pc5_web_phone_end();assert(!phone.manager);
    puts("Library phone: game selection, five media types, fixed target while processing, multiple imports per QR, result polling, route/type restrictions, expiry and legacy mode passed.");
    return 0;
}
'''
unit = out / 'phone_library.c'
unit.write_text(support + extra + api + impl + manager + tests + new_tests, encoding='utf-8')
exe = out / ('phone_library.exe' if os.name == 'nt' else 'phone_library')
if '--msvc' in sys.argv:
    args = ['cl', '/nologo', '/std:c11', '/W4', '/WX', '/D_CRT_SECURE_NO_WARNINGS', str(unit), '/Fe:' + str(exe), '/Fo:' + str(out / 'unit.obj')]
else:
    args = [os.environ.get('CC', 'cc'), '-std=c11', '-D_DEFAULT_SOURCE', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', str(unit), '-o', str(exe)]
subprocess.run(args, check=True)
subprocess.run([str(exe.resolve()), str((out / 'data').resolve())], cwd=out, check=True)
page = (out / 'appearance-preview.html').read_text(encoding='utf-8')
script = re.search(r'<script>(.*?)</script>', page, re.S).group(1)
(out / 'appearance-page.js').write_text(script, encoding='utf-8')
