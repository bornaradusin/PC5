/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Private implementation included by pc5_web.c: shares its socket helpers.
 * Each temporary capability authorizes one file for the UI's fixed game/kind.
 * No request chooses a destination path or changes a game association. */
static struct {
    int (*random)(void *, size_t);
    int only, active, busy, ready, kind, manager, last_result;
    char target[PC5_ID];
    char token[33], game[PC5_NAME], path[PC5_PATH];
    uint64_t expires, received, total;
} phone;

void pc5_web_random_init(int (*random_bytes)(void *, size_t)) { phone.random = random_bytes; }
void pc5_web_phone_only(int enabled)
{
    pthread_mutex_lock(&web.lock); phone.only = enabled; pthread_mutex_unlock(&web.lock);
}
static int phone_only(void)
{
    pthread_mutex_lock(&web.lock); int only = phone.only; pthread_mutex_unlock(&web.lock); return only;
}
void pc5_web_phone_end(void)
{
    pthread_mutex_lock(&web.lock);
    phone.active = 0;
    if (phone.ready == 1) unlink(phone.path);
    phone.ready = 0;
    phone.manager = 0;
    pthread_mutex_unlock(&web.lock);
}
int pc5_web_phone_begin(int kind, const char *game, char *url, size_t size)
{
    uint8_t bytes[16];
    char dir[PC5_PATH];
    if (kind < 0 || kind > PC5_PHONE_VIDEO || !web.running || !phone.random ||
        phone.random(bytes, sizeof(bytes)) || web.address[0] == ':' ||
        pc5_format(dir, sizeof(dir), "%s/.phone", web.root) || pc5_mkdirs(dir)) return -1;
    pc5_web_phone_end();
    pthread_mutex_lock(&web.lock);
    if (phone.busy) { pthread_mutex_unlock(&web.lock); return -1; }
    for (int i = 0; i < 16; ++i) snprintf(phone.token + i * 2, 3, "%02x", bytes[i]);
    phone.kind = kind;
    pc5_copy(phone.game, sizeof(phone.game), game);
    phone.path[0] = 0;
    phone.received = phone.total = 0;
    phone.expires = now_ms() + 10 * 60 * 1000;
    phone.active = 1;
    int failed = pc5_format(url, size, "http://%s/phone/%s", web.address, phone.token);
    if (failed) phone.active = 0;
    pthread_mutex_unlock(&web.lock);
    return failed ? -1 : 0;
}
void pc5_web_phone_status(Pc5PhoneStatus *status)
{
    pthread_mutex_lock(&web.lock);
    status->active = phone.active && now_ms() < phone.expires;
    status->busy = phone.busy;
    status->ready = phone.ready == 1;
    status->received = phone.received;
    status->total = phone.total;
    pthread_mutex_unlock(&web.lock);
}
int pc5_web_phone_take(char *path, size_t size)
{
    pthread_mutex_lock(&web.lock);
    int ready = phone.active && phone.ready == 1;
    if (ready) { pc5_copy(path, size, phone.path); phone.ready = 2; if (!phone.manager) phone.active = 0; }
    pthread_mutex_unlock(&web.lock);
    return ready;
}
static const char *phone_extension(int kind, const char *name)
{
    const char *ext = strrchr(name, '.');
    if (!ext || strchr(name, '/') || strchr(name, '\\')) return NULL;
    if (kind == PC5_PHONE_MUSIC) return !pc5_casecmp(ext, ".mp3") ? ".mp3" : !pc5_casecmp(ext, ".wav") ? ".wav" : NULL;
    if (kind == PC5_PHONE_VIDEO) return !pc5_casecmp(ext, ".mp4") ? ".mp4" : !pc5_casecmp(ext, ".mov") ? ".mov" : NULL;
    return !pc5_casecmp(ext, ".jpg") || !pc5_casecmp(ext, ".jpeg") ? ".jpg" : !pc5_casecmp(ext, ".png") ? ".png" : NULL;
}
static int phone_alive(const char *token)
{
    pthread_mutex_lock(&web.lock);
    int ok = phone.active && now_ms() < phone.expires && !strcmp(token, phone.token);
    pthread_mutex_unlock(&web.lock);
    return ok && !stopping();
}
static void phone_answer(Conn *c, const char *rest)
{
    char token[33], path[PC5_PATH], part[PC5_PATH + 16], game[PC5_NAME];
    if (strlen(rest) < 32 || (rest[32] && rest[32] != '/')) { respond_text(c, 404, "Expired link"); return; }
    memcpy(token, rest, 32); token[32] = 0;
    if (!phone_alive(token)) { respond_text(c, 404, "Expired link. Open a new QR code on PC5."); return; }
    pthread_mutex_lock(&web.lock);
    int kind = phone.kind;
    pc5_copy(game, sizeof(game), phone.game);
    pthread_mutex_unlock(&web.lock);
    if (!rest[32] && !strcmp(c->method, "GET")) {
        static const char *const kinds[] = { "Cover", "Background image", "Tile picture", "Library music", "Background video" };
        Pc5Buf b = { 0 }, title = { 0 };
        /* Names are emitted as JSON and assigned with textContent, never HTML. */
        json_string(&title, game); pc5_buf_terminate(&title);
        pc5_buf_puts(&b, "<!doctype html><html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><title>PC5 phone upload</title><style>body{font:18px system-ui;background:#131b2b;color:#fff;max-width:600px;margin:40px auto;padding:20px}button,input{font:inherit;margin:15px 0;max-width:100%}progress{width:100%}</style><h1>PC5</h1><h2 id=game></h2><p>");
        pc5_buf_puts(&b, kinds[kind]);
        pc5_buf_puts(&b, "</p><p>");
        pc5_buf_puts(&b, kind == PC5_PHONE_MUSIC ? "MP3 or PCM16 WAV, up to 512 MiB." : kind == PC5_PHONE_VIDEO ? "H.264 MP4/MOV, up to 1080p, 60 fps and 1 GiB. Audio is ignored. HEVC is not supported." : "JPEG or PNG, up to 20 MiB. HEIC is not supported.");
        pc5_buf_puts(&b, "</p><p>Keep PC5's QR screen open. Choose one file. Your current selection stays until validation succeeds.</p><input id=file type=file accept='");
        pc5_buf_puts(&b, kind == PC5_PHONE_MUSIC ? ".mp3,.wav" : kind == PC5_PHONE_VIDEO ? ".mp4,.mov" : ".jpg,.jpeg,.png");
        pc5_buf_puts(&b, "'><br><button id=send>Send to this game</button><button id=cancel disabled>Cancel</button><progress id=progress max=100 value=0></progress><p id=status></p><script>document.getElementById('game').textContent=");
        /* Escape '<' in JSON to prevent a crafted game title closing script. */
        for (size_t i = 0; i < title.len; ++i) {
            if (title.data[i] == '<') pc5_buf_puts(&b, "\\u003c");
            else pc5_buf_put(&b, title.data + i, 1);
        }
        pc5_buf_puts(&b, ";let xhr;const f=document.getElementById('file'),s=document.getElementById('status'),b=document.getElementById('send'),c=document.getElementById('cancel');b.onclick=()=>{const v=f.files[0];if(!v)return;s.textContent='Uploading…';b.disabled=true;c.disabled=false;xhr=new XMLHttpRequest();xhr.open('PUT',location.pathname+'/upload'+v.name.slice(v.name.lastIndexOf('.')));xhr.upload.onprogress=e=>{if(e.lengthComputable)document.getElementById('progress').value=e.loaded/e.total*100};const done=t=>{s.textContent=t;b.disabled=false;c.disabled=true};xhr.onload=()=>{done(xhr.status===201?'Received. PC5 is validating/importing. Check the TV for the final result.':xhr.responseText);if(xhr.status===201)b.disabled=true};xhr.onerror=()=>done('Connection failed. Check the network and QR screen.');xhr.onabort=()=>done('Cancelled. Your previous selection was kept.');xhr.send(v)};c.onclick=()=>xhr&&xhr.abort();</script></html>");
        if (b.data) respond(c, 200, "text/html; charset=utf-8", b.data, b.len);
        else respond_text(c, 500, "Out of memory");
        pc5_buf_free(&title); pc5_buf_free(&b);
        return;
    }
    if (strncmp(rest + 32, "/upload.", 8) || strcmp(c->method, "PUT")) { respond_text(c, 405, "Choose one file on the upload page"); return; }
    const char *ext = phone_extension(kind, rest + 33);
    uint64_t max = kind == PC5_PHONE_VIDEO ? 1024ull * 1024 * 1024 : kind == PC5_PHONE_MUSIC ? 512ull * 1024 * 1024 : 20ull * 1024 * 1024;
    if (!ext) { respond_text(c, 400, "Unsupported extension for this option"); return; }
    if (!c->length || c->length == UINT64_MAX || c->length > max) { respond_text(c, 413, "File is empty or exceeds the upload limit"); return; }
    if (pc5_format(path, sizeof(path), "%s/.phone/%s%s", web.root, token, ext) || pc5_format(part, sizeof(part), "%s.part", path)) { respond_text(c, 500, "Path too long"); return; }
    pthread_mutex_lock(&web.lock);
    int allowed = phone.active && !strcmp(token, phone.token) && !phone.busy && !phone.ready;
    if (allowed) { phone.busy = 1; phone.received = 0; phone.total = c->length; }
    pthread_mutex_unlock(&web.lock);
    if (!allowed) { respond_text(c, 409, "An upload is already in progress or has been received"); return; }
    pc5_copy(c->phone_token, sizeof(c->phone_token), token);
    FILE *file = fopen(part, "wb");
    int failed = !file;
    uint64_t left = c->length;
    count(&web.transfers, 1);
    if (!failed && c->expect_continue) send_all(c->sock, "HTTP/1.1 100 Continue\r\n\r\n", 25);
    while (!failed && left && phone_alive(token)) {
        int got = read_body(c, &left, c->buffer, BUFFER);
        if (got <= 0 || fwrite(c->buffer, 1, (size_t)got, file) != (size_t)got) { failed = 1; break; }
        pthread_mutex_lock(&web.lock);
        phone.received = c->length - left;
        /* A library QR expires after inactivity, not during a progressing
         * large upload. Never revive an expired or replaced capability. */
        uint64_t now = now_ms();
        if (phone.manager && phone.active && now < phone.expires && !strcmp(token, phone.token))
            phone.expires = now + 10 * 60 * 1000;
        pthread_mutex_unlock(&web.lock);
    }
    if (file && fclose(file)) failed = 1;
    pthread_mutex_lock(&web.lock);
    int kept = !failed && !left && phone.active && now_ms() < phone.expires && !strcmp(token, phone.token) && !rename(part, path);
    if (kept) { pc5_copy(phone.path, sizeof(phone.path), path); phone.ready = 1; }
    phone.busy = 0;
    pthread_mutex_unlock(&web.lock);
    if (!kept) unlink(part);
    count(&web.transfers, -1);
    respond_text(c, kept ? 201 : 400, kept ? "Received; check PC5 for validation" : "Upload incomplete or cancelled; previous selection kept");
}
