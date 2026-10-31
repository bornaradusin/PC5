/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: the update check (pc5_update.h). */
#include "pc5_update.h"
#include "pc5_i18n.h"
#include "pc5_json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int pc5_version_compare(const char *a, const char *b)
{
    if (*a == 'v' || *a == 'V') a++;
    if (*b == 'v' || *b == 'V') b++;
    for (int part = 0; part < 4; part++) {
        char *end_a, *end_b;
        long x = strtol(a, &end_a, 10), y = strtol(b, &end_b, 10);
        if (end_a == a) x = 0;
        if (end_b == b) y = 0;
        if (x != y) return x < y ? -1 : 1;
        a = *end_a == '.' ? end_a + 1 : end_a;
        b = *end_b == '.' ? end_b + 1 : end_b;
    }
    return 0;
}

void pc5_update_check(const Pc5Http *http, const char *current, Pc5Update *update)
{
    Pc5Buf body = { 0 };
    Pc5Json json = { 0 };
    int status = 0;
    char tag[64];

    memset(update, 0, sizeof(*update));
    update->state = PC5_UPDATE_FAILED;
    if (!http || !http->get || http->get(http->context, PC5_RELEASES_API, &body, 256 * 1024, &status)) goto out;
    if (status == 404) { update->state = PC5_UPDATE_NONE; goto out; }
    if (status != 200 || pc5_buf_terminate(&body) ||
        pc5_json_parse(&json, (const char *)body.data, body.len, 8192) ||
        pc5_json_text(&json, pc5_json_get(&json, 0, "tag_name"), tag, sizeof(tag)) || !tag[0])
        goto out;
    pc5_copy(update->version, sizeof(update->version), tag[0] == 'v' || tag[0] == 'V' ? tag + 1 : tag);
    if (pc5_json_text(&json, pc5_json_get(&json, 0, "name"), update->name, sizeof(update->name)))
        update->name[0] = 0;
    if (pc5_json_text(&json, pc5_json_get(&json, 0, "html_url"), update->url, sizeof(update->url)) ||
        strncmp(update->url, "https://github.com/", 19))
        pc5_copy(update->url, sizeof(update->url), PC5_RELEASES_PAGE);
    update->state = pc5_version_compare(update->version, current) > 0 ? PC5_UPDATE_AVAILABLE : PC5_UPDATE_CURRENT;
    {
        /* The release the update zip holds the changes since, in a line of the notes GitHub
         * doesn't show (from 1.9.8's releases): "<!-- pc5-update-from: 1.9.7 -->". */
        /* On the heap: the check may run on a thread of its own, and once at a time on each. */
        enum { NOTES = 64 * 1024 };
        static const char mark[] = "<!-- pc5-update-from: ";
        char *notes = pc5_alloc(NOTES);
        const char *at;
        if (notes && !pc5_json_text(&json, pc5_json_get(&json, 0, "body"), notes, NOTES) && (at = strstr(notes, mark))) {
            char from[32];
            size_t n = 0;
            at += sizeof(mark) - 1;
            if (*at == 'v' || *at == 'V') at++;
            /* a version: digits and dots, and a test build's suffix ("1.9.8-file-manager-test") */
            while (n + 1 < sizeof(from) && ((at[n] >= '0' && at[n] <= '9') || (at[n] >= 'a' && at[n] <= 'z') || at[n] == '.' ||
                                            at[n] == '-')) {
                from[n] = at[n];
                n++;
            }
            from[n] = 0;
            if (n && !strncmp(at + n, " -->", 4)) pc5_copy(update->delta_from, sizeof(update->delta_from), from);
        }
        pc5_free(notes);
    }
    /* The app's zip and the update zip among the release's files. */
    uint32_t assets = pc5_json_get(&json, 0, "assets");
    for (uint32_t i = 0, asset; assets && (asset = pc5_json_item(&json, assets, i)); i++) {
        char name[64], url[512], digest[80], label[64];
        double size = pc5_json_number(&json, pc5_json_get(&json, asset, "size"), -1);
        char *to_url, *to_sha256;
        uint64_t *to_size;
        if (pc5_json_text(&json, pc5_json_get(&json, asset, "name"), name, sizeof(name))) continue;
        if (!strcmp(name, PC5_TITLE_ID ".zip") && !update->asset_url[0]) {
            to_url = update->asset_url;
            to_size = &update->asset_size;
            to_sha256 = update->asset_sha256;
        } else if (!strcmp(name, PC5_TITLE_ID "-update.zip") && !update->delta_url[0]) {
            to_url = update->delta_url;
            to_size = &update->delta_size;
            to_sha256 = update->delta_sha256;
            /* Its label said which release it holds the changes since on 1.9.7's release ("from
             * 1.9.6"); the notes say it from 1.9.8's on. */
            if (!update->delta_from[0] && !pc5_json_text(&json, pc5_json_get(&json, asset, "label"), label, sizeof(label)) &&
                !strncmp(label, "from ", 5)) {
                const char *from = label + 5;
                if (*from == 'v' || *from == 'V') from++;
                if (*from >= '0' && *from <= '9' && strlen(from) < sizeof(update->delta_from))
                    pc5_copy(update->delta_from, sizeof(update->delta_from), from);
            }
        } else {
            continue;
        }
        /* Only from GitHub itself, and of a size a zip of the app can be. */
        if (pc5_json_text(&json, pc5_json_get(&json, asset, "browser_download_url"), url, sizeof(url)) ||
            strncmp(url, "https://github.com/", 19) || !(size > 0 && size <= 4e9))
            continue;
        pc5_copy(to_url, sizeof(update->asset_url), url);
        *to_size = (uint64_t)size;
        if (!pc5_json_text(&json, pc5_json_get(&json, asset, "digest"), digest, sizeof(digest)) &&
            !strncmp(digest, "sha256:", 7) && strlen(digest + 7) == 64 && strspn(digest + 7, "0123456789abcdef") == 64)
            pc5_copy(to_sha256, sizeof(update->asset_sha256), digest + 7);
    }
out:
    pc5_json_free(&json);
    pc5_buf_free(&body);
}

int pc5_update_small(const Pc5Update *update, const char *current)
{
    return update->state == PC5_UPDATE_AVAILABLE && update->delta_url[0] && update->delta_from[0] &&
           pc5_version_compare(update->delta_from, current) == 0;
}

void pc5_update_text(const Pc5Update *update, char *out, size_t size)
{
    switch (update->state) {
    case PC5_UPDATE_AVAILABLE: pc5_tf(out, size, "PC5 %s is available on GitHub", update->version); break;
    case PC5_UPDATE_CURRENT: pc5_tf(out, size, "You have the newest PC5 (%s)", PC5_VERSION); break;
    case PC5_UPDATE_NONE: pc5_tf(out, size, "No PC5 release is published on GitHub yet"); break;
    case PC5_UPDATE_FAILED: pc5_tf(out, size, "GitHub didn't answer: is the console online?"); break;
    default: pc5_tf(out, size, "Not checked yet"); break;
    }
}

static const char *const state_names[] = { "unknown", "failed", "none", "current", "available" };

void pc5_update_load(const char *root, Pc5UpdateMemory *memory)
{
    char path[PC5_PATH];
    Pc5Buf text = { 0 };

    memset(memory, 0, sizeof(*memory));
    if (pc5_format(path, sizeof(path), "%s/pc5/update.txt", root) || pc5_read_file(path, &text, 4096)) {
        pc5_buf_free(&text);
        return;
    }
    for (char *line = (char *)text.data, *next; line && *line; line = next) {
        char name[16], value[256];
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        if (sscanf(line, " %15[a-z] = %255s", name, value) != 2) continue;
        if (!strcmp(value, "-")) value[0] = 0;
        if (!strcmp(name, "checked")) memory->checked = strtoll(value, NULL, 10);
        else if (!strcmp(name, "told")) pc5_copy(memory->told, sizeof(memory->told), value);
        else if (!strcmp(name, "latest")) pc5_copy(memory->last.version, sizeof(memory->last.version), value);
        else if (!strcmp(name, "url")) pc5_copy(memory->last.url, sizeof(memory->last.url), value);
        else if (!strcmp(name, "state"))
            for (int i = 0; i < 5; i++)
                if (!strcmp(value, state_names[i])) memory->last.state = (Pc5UpdateState)i;
    }
    /* An answer about an older PC5 than this one says nothing now. */
    if (memory->last.state == PC5_UPDATE_AVAILABLE && pc5_version_compare(memory->last.version, PC5_VERSION) <= 0)
        memory->last.state = PC5_UPDATE_CURRENT;
    pc5_buf_free(&text);
}

int pc5_update_save(const char *root, const Pc5UpdateMemory *memory)
{
    char path[PC5_PATH], text[700];
    int length;

    if (pc5_format(path, sizeof(path), "%s/pc5", root) || pc5_mkdirs(path) ||
        pc5_format(path, sizeof(path), "%s/pc5/update.txt", root))
        return -1;
    length = snprintf(text, sizeof(text), "checked = %lld\ntold = %s\nstate = %s\nlatest = %s\nurl = %s\n",
                      (long long)memory->checked, memory->told[0] ? memory->told : "-",
                      state_names[memory->last.state % 5], memory->last.version[0] ? memory->last.version : "-",
                      memory->last.url[0] ? memory->last.url : "-");
    return pc5_write_file(path, text, (size_t)length);
}
