/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* The capability pages keep their authorization and upload routes unchanged.
 * Their dictionary is embedded: no unauthenticated /lang.json endpoint. */
#define NW_(text) text
static void phone_respond_page(Conn *c, const char *page, size_t length)
{
    (void)NW_("PC5 phone upload");
    (void)NW_("Cover");
    (void)NW_("Background image");
    (void)NW_("Tile picture");
    (void)NW_("Library music");
    (void)NW_("Background video");
    (void)NW_("Library appearance");
    (void)NW_("Loading screen");
    (void)NW_("MP3 or PCM16 WAV, up to 512 MiB.");
    (void)NW_("H.264 MP4/MOV, up to 1080p, 60 fps and 1 GiB. Audio is ignored. HEVC is not supported.");
    (void)NW_("JPEG or PNG, up to 20 MiB. HEIC is not supported.");
    (void)NW_("Keep PC5's QR screen open. Choose one file. Your current selection stays until validation succeeds.");
    (void)NW_("Send to this game");
    (void)NW_("Cancel");
    (void)NW_("Choose a game and send its pictures, music or silent video. Existing media stays until the new file is validated.");
    (void)NW_("Find a game");
    (void)NW_("Search your library");
    (void)NW_("Game");
    (void)NW_("Appearance");
    (void)NW_("Send to selected game");
    (void)NW_("Cancel upload");
    (void)NW_("Loading your library…");
    (void)NW_("Keep PC5 open. Share its Wi-Fi or hotspot. Link expires after 10 idle minutes.");
    (void)NW_("Uploading…");
    (void)NW_("Received. PC5 is validating/importing. Check the TV for the final result.");
    (void)NW_("Connection failed. Check the network and QR screen.");
    (void)NW_("Cancelled. Your previous selection was kept.");
    (void)NW_("H.264 MP4/MOV up to 1080p / 60 fps, 1 GiB. Audio is ignored.");
    (void)NW_("JPEG or PNG up to 20 MiB. HEIC is not supported.");
    (void)NW_("Ready. Choose a game.");
    (void)NW_("No games PC5 installed.");
    (void)NW_("Link expired. Open a new QR code.");
    (void)NW_("PC5 is validating and saving. Please wait…");
    (void)NW_("Saved to this game. You may send another file.");
    (void)NW_("Import failed or was cancelled. Your previous media was kept. Check the TV.");
    (void)NW_("Connection unavailable. Check the TV and network.");
    (void)NW_("Choose a supported file type.");
    (void)NW_("Upload failed. Check your connection.");
    (void)NW_("Upload cancelled.");
    (void)NW_("PC5 · Library appearance");
    (void)NW_("Invalid game or media type");
    (void)NW_("Invalid game");
    (void)NW_("Unsupported media extension");
    (void)NW_("Expired appearance link. Open a new QR code.");
    (void)NW_("Expired appearance link");
    (void)NW_("Not an appearance action");
    (void)NW_("The selected game is no longer available");
    (void)NW_("An upload is already in progress");
    (void)NW_("PC5 is still validating the previous file");
    (void)NW_("Out of memory");
    (void)NW_("Unsupported extension for this option");
    (void)NW_("Expired link. Open a new QR code on PC5.");
    (void)NW_("Expired link");
    (void)NW_("Choose one file on the upload page");
    (void)NW_("An upload is already in progress or has been received");
    (void)NW_("File is empty or exceeds the upload limit");
    (void)NW_("Path too long");
    Pc5Buf language = { 0 }, body = { 0 };
    const char *script = NULL;
    for (size_t at = 0; at + 8 <= length; ++at)
        if (!memcmp(page + at, "<script>", 8)) { script = page + at; break; }
    if (!script || pc5_i18n_web_json(&language)) { pc5_buf_free(&language); respond_text(c, 500, "Out of memory"); return; }
    size_t before = (size_t)(script - page);
    pc5_buf_put(&body, page, before);
    pc5_buf_puts(&body, "<script>const pc5Language=");
    for (size_t i = 0; i < language.len; ++i) {
        if (language.data[i] == '<') pc5_buf_puts(&body, "\\u003c");
        else pc5_buf_put(&body, language.data + i, 1);
    }
    pc5_buf_puts(&body, ";function t(s){return pc5Language.strings[s]||s}document.documentElement.lang=pc5Language.lang;document.documentElement.dir=pc5Language.lang==='ar'?'rtl':'ltr';const walk=document.createTreeWalker(document.documentElement,NodeFilter.SHOW_TEXT);let node;while(node=walk.nextNode()){if(!['SCRIPT','STYLE'].includes(node.parentElement.tagName)){const key=node.nodeValue.trim();if(pc5Language.strings[key])node.nodeValue=node.nodeValue.replace(key,t(key))}}for(const e of document.querySelectorAll('[placeholder]'))e.placeholder=t(e.placeholder);for(const e of document.querySelectorAll('input[type=search],#game'))e.dir='auto';</script>");
    pc5_buf_put(&body, script, length - before);
    if (body.data) respond(c, 200, "text/html; charset=utf-8", body.data, body.len);
    else respond_text(c, 500, "Out of memory");
    pc5_buf_free(&body); pc5_buf_free(&language);
}
#undef NW_

static void phone_reply(Conn *c, int status, const char *text)
{
    respond_text(c, status, pc5_trc("web", text));
}
