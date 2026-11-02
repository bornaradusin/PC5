#!/usr/bin/env python3
"""Exercise the actual phone session/HTTP handler with bounded in-memory IO.
Real socket concurrency/regression coverage is in test_web.py on Linux.
"""
from pathlib import Path
import os, subprocess, sys
root = Path(__file__).resolve().parents[1]
out = root / '.build/phone-check'
out.mkdir(parents=True, exist_ok=True)
data = out / 'data'
data.mkdir(exist_ok=True)
header = (root/'title/pc5_web.h').read_text(encoding='utf-8')
api = header[header.index('enum { PC5_PHONE_COVER'):header.index('/* For the tests:')]
implementation = (root/'title/pc5_web_phone.h').read_text(encoding='utf-8')
support = r'''
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define unlink _unlink
#define strcasecmp _stricmp
#else
#include <unistd.h>
#include <strings.h>
#include <sys/stat.h>
#endif
enum { PC5_PATH=1024, PC5_NAME=96, PC5_ID=64, BUFFER=256*1024 };
typedef struct { unsigned char *data; size_t len, cap; } Pc5Buf;
typedef struct { char method[8], phone_token[33]; uint64_t length; int sock, expect_continue;
unsigned char *buffer; const unsigned char *body; size_t have; int cancel; } Conn;
static struct { int lock, running, transfers; char root[PC5_PATH], address[48]; } web;
static uint64_t clock_ms=1;
static int answer_code, stop_flag;
static char answer_body[65536];
static void pthread_mutex_lock(int *l) { (void)l; }
static void pthread_mutex_unlock(int *l) { (void)l; }
static uint64_t now_ms(void) { return clock_ms; }
static int stopping(void) { return stop_flag; }
static int pc5_casecmp(const char *a,const char *b) { return strcasecmp(a,b); }
static void pc5_copy(char *p,size_t n,const char *s) { snprintf(p,n,"%s",s); }
static int pc5_format(char *p,size_t n,const char *fmt,...) { va_list a;va_start(a,fmt);int k=vsnprintf(p,n,fmt,a);va_end(a);return k<0||(size_t)k>=n?-1:0; }
static int pc5_mkdirs(const char *p) {
#ifdef _WIN32
    int rc=_mkdir(p);
#else
    int rc=mkdir(p,0700);
#endif
    (void)rc; return 0;
}
static int pc5_buf_put(Pc5Buf *b,const void *p,size_t n) { if(b->len+n+1>b->cap) { b->cap=b->len+n+1024;b->data=realloc(b->data,b->cap);assert(b->data); }memcpy(b->data+b->len,p,n);b->len+=n;b->data[b->len]=0;return 0; }
static int pc5_buf_puts(Pc5Buf *b,const char *p) { return pc5_buf_put(b,p,strlen(p)); }
static void pc5_buf_terminate(Pc5Buf *b) { (void)pc5_buf_puts(b,""); }
static void pc5_buf_free(Pc5Buf *b) { free(b->data);memset(b,0,sizeof(*b)); }
static void json_string(Pc5Buf *b,const char *s) { pc5_buf_puts(b,"\"");for(;*s;s++) { if(*s=='"'||*s=='\\')pc5_buf_puts(b,"\\");pc5_buf_put(b,s,1); }pc5_buf_puts(b,"\""); }
static int respond(Conn *c,int code,const char *type,const void *body,size_t n) { (void)c;(void)type;answer_code=code;assert(n<sizeof(answer_body));memcpy(answer_body,body,n);answer_body[n]=0;return 0; }
static int respond_text(Conn *c,int code,const char *s) { return respond(c,code,"text",s,strlen(s)); }
static int send_all(int sock,const void *p,size_t n) { (void)sock;(void)p;(void)n;return 0; }
static void count(int *p,int n) { *p+=n; }
static int read_body(Conn *c,uint64_t *left,void *p,size_t n);
'''
tests = r'''
static int read_body(Conn *c,uint64_t *left,void *p,size_t n) {
    if(c->cancel) { pc5_web_phone_end(); return -1; }
    if(!c->have)return -1;
    if(n>c->have) { n=c->have; }
    if(n>*left) { n=(size_t)*left; }
    memcpy(p,c->body,n);c->body+=n;c->have-=n;*left-=n;return (int)n;
}
static int random_bytes(void *p,size_t n) { static unsigned char k=1;memset(p,k++,n);return 0; }
static int call(const char *url,const char *suffix,const char *method,const char *bytes,uint64_t length,int cancel) {
    unsigned char buffer[BUFFER];Conn c={0};char path[PC5_PATH];
    snprintf(path,sizeof(path),"%s%s",strstr(url,"/phone/")+7,suffix);
    snprintf(c.method,sizeof(c.method),"%s",method);c.length=length;c.buffer=buffer;
    c.body=(const unsigned char*)bytes;c.have=bytes?strlen(bytes):0;c.cancel=cancel;
    phone_answer(&c,path);assert(web.transfers==0);return answer_code;
}
int main(int argc,char **argv) {
    assert(argc==2);pc5_copy(web.root,sizeof(web.root),argv[1]);
    strcpy(web.address,"127.0.0.1:8125");web.running=1;
    char url[160],old[160],file[PC5_PATH];
    assert(pc5_web_phone_begin(0,"Game",url,sizeof(url))==-1);
    pc5_web_random_init(random_bytes);
    assert(pc5_web_phone_begin(-1,"Game",url,sizeof(url))==-1);
    const char *ext[]={".png",".jpg",".jpeg",".MP3",".mp4"};
    for(int k=0;k<5;k++) {
        assert(!pc5_web_phone_begin(k,"Game </script><script>alert(1)</script>",url,sizeof(url)));
        assert(call(url,"","GET",NULL,0,0)==200);
        assert(strstr(answer_body,"\\u003c/script>") && !strstr(answer_body,"Game </script>"));
        char suffix[64];snprintf(suffix,sizeof(suffix),"/upload%s",ext[k]);
        assert(call(url,"/upload.exe","PUT","x",1,0)==400);
        assert(call(url,suffix,"PUT","",0,0)==413);
        assert(call(url,suffix,"PUT","fixture",7,0)==201);
        assert(call(url,suffix,"PUT","fixture",7,0)==409);
        Pc5PhoneStatus status;pc5_web_phone_status(&status);
        assert(status.ready && status.received==7 && status.total==7 && !status.busy);
        assert(pc5_web_phone_take(file,sizeof(file))==1);
        assert(call(url,"","GET",NULL,0,0)==404);
        FILE *f=fopen(file,"rb");assert(f);char b[8]={0};assert(fread(b,1,7,f)==7);fclose(f);assert(!strcmp(b,"fixture"));unlink(file);
    }
    assert(!pc5_web_phone_begin(3,"Game",url,sizeof(url)));
    assert(call(url,"/upload.mp4","PUT","x",1,0)==400);
    assert(call(url,"/upload.mp3","PUT","x",513ull<<20,0)==413);
    assert(call(url,"/upload.mp3","PUT","short",100,0)==400);
    assert(!phone.busy && !phone.ready);
    assert(call(url,"/upload.mp3","PUT","cancel",100,1)==400);
    strcpy(old,url);assert(!pc5_web_phone_begin(4,"Game",url,sizeof(url)));
    assert(call(old,"","GET",NULL,0,0)==404);
    assert(call(url,"/upload.mp4","PUT","x",(1ull<<30)+1,0)==413);
    clock_ms+=600001;assert(call(url,"","GET",NULL,0,0)==404);
    assert(!pc5_web_phone_begin(0,"Game",url,sizeof(url)));
    assert(call(url,"/upload.png","PUT","pending",7,0)==201);
    strcpy(file,phone.path);pc5_web_phone_end();assert(!fopen(file,"rb"));
    pc5_web_phone_only(1);assert(phone_only());pc5_web_phone_only(0);assert(!phone_only());
    puts("phone checks passed: five media kinds, capability invalidation, one-shot uploads, size/type limits, partial/cancel cleanup, expiry, HTML safety");
    return 0;
}
'''
unit=out/'phone.c'
unit.write_text(support+api+implementation+tests,encoding='utf-8')
exe=out/('phone.exe' if os.name=='nt' else 'phone')
if '--msvc' in sys.argv:
    args=['cl','/nologo','/std:c11','/W4','/WX','/D_CRT_SECURE_NO_WARNINGS',str(unit),'/Fe:'+str(exe),'/Fo:'+str(out/'phone.obj')]
else:
    args=[os.environ.get('CC','cc'),'-std=c11','-D_DEFAULT_SOURCE','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(unit),'-o',str(exe)]
subprocess.run(args,check=True)
subprocess.run([str(exe),str(data)],check=True)
