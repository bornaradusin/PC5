/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Included after pc5_web_phone.h. Reuses one bounded upload and the existing
 * import workers. Game and media kind are locked before its upload starts. */
static int appearance_uploading;
static int appearance_claim(void)
{
    pthread_mutex_lock(&web.lock);
    int busy = appearance_uploading;
    if (!busy) appearance_uploading = 1;
    pthread_mutex_unlock(&web.lock); return busy;
}
static void appearance_release(void)
{
    pthread_mutex_lock(&web.lock); appearance_uploading = 0; pthread_mutex_unlock(&web.lock);
}
int pc5_web_appearance_begin(char *url, size_t size)
{
    if (pc5_web_phone_begin(PC5_PHONE_COVER, "Library appearance", url, size)) return -1;
    pthread_mutex_lock(&web.lock);
    phone.manager = 1; phone.target[0] = 0; phone.last_result = 0;
    int bad = pc5_format(url, size, "http://%s/appearance/%s", web.address, phone.token);
    pthread_mutex_unlock(&web.lock);
    if (bad) pc5_web_phone_end();
    return bad;
}
int pc5_web_appearance_target(char *id, size_t size, int *kind)
{
    pthread_mutex_lock(&web.lock);
    int ok = phone.manager && phone.ready == 2;
    if (ok) { pc5_copy(id, size, phone.target); *kind = phone.kind; }
    pthread_mutex_unlock(&web.lock); return ok;
}
void pc5_web_appearance_finish(int success)
{
    pthread_mutex_lock(&web.lock);
    if (phone.manager && phone.ready == 2) {
        phone.ready = 0; phone.last_result = success ? 1 : -1; phone.path[0] = 0;
        phone.expires = now_ms() + 10 * 60 * 1000;
    }
    pthread_mutex_unlock(&web.lock);
}
static const char appearance_page[] =
    "<!doctype html><html lang=en><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>PC5 · Library appearance</title><style>:root{color-scheme:dark}body{font:17px system-ui;background:#101b30;color:#edf4ff;max-width:760px;margin:30px auto;padding:20px}h1{letter-spacing:3px}section{padding:20px;background:#192941;border:1px solid #3b5575;border-radius:18px}input,select,button{font:inherit;padding:12px;border:1px solid #48698c;border-radius:10px;box-sizing:border-box;width:100%;margin:8px 0;background:#102139;color:inherit}button{background:#185f91}progress{width:100%}small{color:#b8c9de}</style>"
    "<h1>PC5</h1><h2>Library appearance</h2><p>Choose a game and send its pictures, music or silent video. Existing media stays until the new file is validated.</p>"
    "<section><label>Find a game<input id=search type=search placeholder='Search your library'></label><label>Game<select id=game></select></label>"
    "<label>Appearance<select id=kind><option value=0>Cover</option><option value=1>Background image</option><option value=4>Background video</option><option value=6>Loading screen</option><option value=3>Library music</option><option value=2>Tile picture</option></select></label>"
    "<small id=help></small><input id=file type=file><button id=send>Send to selected game</button><button id=cancel disabled>Cancel upload</button><progress id=progress value=0 max=100></progress><p id=status>Loading your library…</p></section>"
    "<p><small>Keep PC5 open. Share its Wi-Fi or hotspot. Link expires after 10 idle minutes.</small></p>"
    "<script>const $=id=>document.getElementById(id),base=(location.pathname.endsWith('/')?location.pathname.slice(0,-1):location.pathname),g=$('game'),k=$('kind'),f=$('file'),s=$('status'),b=$('send'),c=$('cancel');let games=[],xhr,busy=false;"
    "const fill=()=>{const old=g.value;g.replaceChildren();for(const x of games.filter(x=>x.title.toLocaleLowerCase().includes($('search').value.toLocaleLowerCase()))){const o=document.createElement('option');o.value=x.id;o.textContent=x.title;g.append(o)}if([...g.options].some(o=>o.value===old))g.value=old};"
    "const help=()=>{f.accept=k.value==='3'?'.mp3,.wav':k.value==='4'?'.mp4,.mov':'.jpg,.jpeg,.png';$('help').textContent=k.value==='3'?'MP3 or PCM16 WAV, up to 512 MiB.':k.value==='4'?'H.264 MP4/MOV up to 1080p / 60 fps, 1 GiB. Audio is ignored.':'JPEG or PNG up to 20 MiB. HEIC is not supported.'};help();k.onchange=help;$('search').oninput=fill;"
    "fetch(base+'/games',{cache:'no-store'}).then(r=>{if(!r.ok)throw Error();return r.json()}).then(x=>{games=x;fill();s.textContent=x.length?'Ready. Choose a game.':'No games PC5 installed.'}).catch(()=>{s.textContent='Link expired. Open a new QR code.';b.disabled=true});"
    "const lock=v=>{busy=v;b.disabled=v;c.disabled=!v;g.disabled=k.disabled=f.disabled=$('search').disabled=v};"
    "const poll=async()=>{try{const r=await fetch(base+'/status',{cache:'no-store'});if(!r.ok)throw Error();const x=await r.json();if(x.processing||x.ready){s.textContent='PC5 is validating and saving. Please wait…';setTimeout(poll,1000)}else{lock(false);s.textContent=x.result===1?'Saved to this game. You may send another file.':'Import failed or was cancelled. Your previous media was kept. Check the TV.'}}catch{lock(false);s.textContent='Connection unavailable. Check the TV and network.'}};"
    "b.onclick=()=>{const v=f.files[0];if(busy||!v||!g.value)return;const ext=v.name.slice(v.name.lastIndexOf('.')).toLowerCase();if(!f.accept.split(',').includes(ext)){s.textContent='Choose a supported file type.';return}lock(true);s.textContent='Uploading…';xhr=new XMLHttpRequest();xhr.open('PUT',base+'/upload/'+encodeURIComponent(g.value)+'/'+k.value+ext);xhr.upload.onprogress=e=>{if(e.lengthComputable)$('progress').value=e.loaded/e.total*100};xhr.onload=()=>{c.disabled=true;if(xhr.status===201)poll();else{lock(false);s.textContent=xhr.responseText}};xhr.onerror=()=>{lock(false);s.textContent='Upload failed. Check your connection.'};xhr.onabort=()=>{lock(false);s.textContent='Upload cancelled.'};xhr.send(v)};c.onclick=()=>xhr&&xhr.abort();</script></html>";
static void appearance_answer(Conn *c, const char *rest)
{
    char token[33]; size_t length = strlen(rest);
    if (length < 32 || (rest[32] && rest[32] != '/')) { respond_text(c, 404, "Expired appearance link"); return; }
    memcpy(token, rest, 32); token[32] = 0;
    pthread_mutex_lock(&web.lock); int manager = phone.manager; pthread_mutex_unlock(&web.lock);
    if (!manager || !phone_alive(token)) { respond_text(c, 404, "Expired appearance link. Open a new QR code."); return; }
    const char *route = rest + 32;
    if (!*route && !strcmp(c->method, "GET")) { respond(c, 200, "text/html; charset=utf-8", appearance_page, sizeof(appearance_page) - 1); return; }
    if (!strcmp(route, "/status") && !strcmp(c->method, "GET")) {
        Pc5Buf b = { 0 }; pthread_mutex_lock(&web.lock);
        pc5_buf_printf(&b, "{\"processing\":%s,\"ready\":%s,\"result\":%d}", phone.ready == 2 ? "true" : "false", phone.ready == 1 ? "true" : "false", phone.last_result);
        pthread_mutex_unlock(&web.lock); respond_buf(c, &b); return;
    }
    if (!strcmp(route, "/games") && !strcmp(c->method, "GET")) {
        Pc5Library *l = pc5_alloc(sizeof(*l)); Pc5Buf b = { 0 };
        if (!l) { respond_text(c, 500, "Out of memory"); return; }
        pc5_library_load(web.root, l); pc5_buf_puts(&b, "["); int n = 0;
        for (int i = 0; i < l->count; ++i) {
            const Pc5Game *g = &l->games[i];
            if (!g->pc5 || !g->prefix[0] || !strcmp(g->prefix, "default")) continue;
            pc5_buf_puts(&b, n++ ? ",{\"id\":" : "{\"id\":"); json_string(&b, g->id);
            pc5_buf_puts(&b, ",\"title\":"); json_string(&b, g->title); pc5_buf_puts(&b, "}");
        }
        pc5_buf_puts(&b, "]"); pc5_free(l); respond_buf(c, &b); return;
    }
    if (!strncmp(route, "/upload/", 8) && !strcmp(c->method, "PUT")) {
        const char *id = route + 8, *slash = strchr(id, '/');
        if (!slash || slash - id < 1 || slash - id >= PC5_ID || slash[1] < '0' || slash[1] > '6' || slash[1] == '5' || slash[2] != '.') { respond_text(c, 400, "Invalid game or media type"); return; }
        char selected[PC5_ID]; memcpy(selected, id, (size_t)(slash - id)); selected[slash - id] = 0;
        for (const char *p = selected; *p; ++p) if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == '_')) { respond_text(c, 400, "Invalid game"); return; }
        int kind = slash[1] - '0';
        if (!phone_extension(kind, slash + 1)) { respond_text(c, 400, "Unsupported media extension"); return; }
        if (appearance_claim()) { respond_text(c, 409, "An upload is already in progress"); return; }
        Pc5Library *l = pc5_alloc(sizeof(*l));
        int found = -1;
        if (l) { pc5_library_load(web.root, l); found = pc5_library_find(l, selected); }
        if (found < 0 || !l->games[found].pc5 || !l->games[found].prefix[0] || !strcmp(l->games[found].prefix, "default")) {
            pc5_free(l); appearance_release(); respond_text(c, 404, "The selected game is no longer available"); return;
        }
        pthread_mutex_lock(&web.lock);
        int ready = !phone.busy && !phone.ready && phone.active && phone.manager && !strcmp(token, phone.token);
        if (ready) { phone.kind = kind; phone.last_result = 0; pc5_copy(phone.target, sizeof(phone.target), selected); pc5_copy(phone.game, sizeof(phone.game), l->games[found].title); }
        pthread_mutex_unlock(&web.lock); pc5_free(l);
        if (!ready) { appearance_release(); respond_text(c, 409, "PC5 is still validating the previous file"); return; }
        char upload[64]; pc5_format(upload, sizeof(upload), "%s/upload%s", token, slash + 2);
        phone_answer(c, upload); appearance_release(); return;
    }
    respond_text(c, 404, "Not an appearance action");
}
