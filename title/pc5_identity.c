/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: which game a folder is (pc5_identity.h). */
#include "pc5_identity.h"
#include "pc5_json.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { DEPTH = 3, KIND_NONE = 0, KIND_MANIFEST = 1, KIND_INI = 2, KIND_TXT = 3 };

/* The .ini files Steam stand-ins and cracks carry, with the app id they stand in for. */
static const char *const ini_names[] = {
    "steam_emu.ini", "codex.ini", "steam_api.ini", "steam_api64.ini", "cream_api.ini", "onlinefix.ini",
    "onlinefix64.ini", "coldclientloader.ini", "cpy.ini", "rld.ini", "3dmgame.ini", "ali213.ini", "valve.ini",
    "steamconfig.ini", "ds.ini", "hlm.ini", "tenoke.ini", "empress.ini", NULL,
};

typedef struct Scan {
    uint32_t steam;
    int steam_kind, steam_depth;
    char steam_source[64];
    uint64_t gog;
    int gog_root, gog_depth;     /* the base game's file (gameId == rootGameId) goes first */
    char gog_name[PC5_NAME];
} Scan;

/* An app id Steam stand-ins use when they don't know the game's (Spacewar), and nothing. */
static int real_appid(unsigned long id)
{
    return id && id != 480 && id < 100000000ul;
}

static void steam_found(Scan *s, uint32_t id, int kind, int depth, const char *name)
{
    if (!real_appid(id)) return;
    if (s->steam && (kind < s->steam_kind || (kind == s->steam_kind && depth >= s->steam_depth))) return;
    s->steam = id;
    s->steam_kind = kind;
    s->steam_depth = depth;
    pc5_copy(s->steam_source, sizeof(s->steam_source), name);
}

/* The number after a line's key ("AppId = 123", "RealAppId=123"), the keys compared without case;
 * 0 when the line is another. A fake id (FakeAppId) is never one. */
static uint32_t ini_value(const char *line)
{
    char key[32];
    size_t n = 0;
    while (*line == ' ' || *line == '\t') line++;
    while (*line && *line != '=' && *line != ' ' && *line != '\t' && n + 1 < sizeof(key)) key[n++] = *line++;
    key[n] = 0;
    while (*line == ' ' || *line == '\t') line++;
    if (*line != '=') return 0;
    line++;
    while (*line == ' ' || *line == '\t' || *line == '"') line++;
    if (pc5_casecmp(key, "appid") && pc5_casecmp(key, "app_id") && pc5_casecmp(key, "realappid") &&
        pc5_casecmp(key, "steamappid"))
        return 0;
    if (*line < '0' || *line > '9') return 0;
    unsigned long id = strtoul(line, NULL, 10);
    return real_appid(id) ? (uint32_t)id : 0;
}

static uint32_t ini_appid(const char *path)
{
    Pc5Buf text = { 0 };
    uint32_t found = 0;
    if (pc5_read_file(path, &text, 256 * 1024) || pc5_buf_terminate(&text)) {
        pc5_buf_free(&text);
        return 0;
    }
    for (char *line = (char *)text.data, *next; line && *line && !found; line = next) {
        next = strpbrk(line, "\r\n");
        if (next) *next++ = 0;
        if (*line == ';' || *line == '#') continue;
        found = ini_value(line);
    }
    pc5_buf_free(&text);
    return found;
}

/* Steam's "appid" "123" (an appmanifest_<id>.acf, installscript.vdf), the key without case. */
static uint32_t vdf_appid(const char *path)
{
    Pc5Buf text = { 0 };
    uint32_t found = 0;
    if (pc5_read_file(path, &text, 1024 * 1024) || pc5_buf_terminate(&text)) {
        pc5_buf_free(&text);
        return 0;
    }
    for (const char *p = (const char *)text.data; *p && !found; p++) {
        if (*p != '"' || (p[1] != 'a' && p[1] != 'A')) continue;
        char key[8];
        size_t n = 0;
        const char *q = p + 1;
        while (*q && *q != '"' && n + 1 < sizeof(key)) key[n++] = *q++;
        key[n] = 0;
        if (*q != '"' || pc5_casecmp(key, "appid")) continue;
        q++;
        while (*q == ' ' || *q == '\t') q++;
        if (*q != '"' || q[1] < '0' || q[1] > '9') continue;
        unsigned long id = strtoul(q + 1, NULL, 10);
        if (real_appid(id)) found = (uint32_t)id;
    }
    pc5_buf_free(&text);
    return found;
}

/* goggame-<id>.info: GOG's own note of the product (gameId, rootGameId, name). */
static void gog_info(Scan *s, const char *path, int depth)
{
    Pc5Buf text = { 0 };
    Pc5Json json;
    char game[32] = "", root[32] = "", name[PC5_NAME] = "";
    if (pc5_read_file(path, &text, 256 * 1024) || pc5_json_parse(&json, (const char *)text.data, text.len, 4096)) {
        pc5_buf_free(&text);
        return;
    }
    pc5_json_text(&json, pc5_json_get(&json, 0, "gameId"), game, sizeof(game));
    pc5_json_text(&json, pc5_json_get(&json, 0, "rootGameId"), root, sizeof(root));
    pc5_json_text(&json, pc5_json_get(&json, 0, "name"), name, sizeof(name));
    pc5_json_free(&json);
    pc5_buf_free(&text);
    uint64_t id = strtoull(game, NULL, 10);
    int is_root = !root[0] || !strcmp(root, game);
    if (!id || (s->gog && (s->gog_root > is_root || (s->gog_root == is_root && s->gog_depth <= depth)))) return;
    s->gog = id;
    s->gog_root = is_root;
    s->gog_depth = depth;
    pc5_copy(s->gog_name, sizeof(s->gog_name), name);
}

static int is_ini(const char *name)
{
    for (int i = 0; ini_names[i]; i++)
        if (!pc5_casecmp(name, ini_names[i])) return 1;
    return 0;
}

static void walk(Scan *s, const char *dir, int depth)
{
    DIR *d = opendir(dir);
    struct dirent *entry;
    char path[PC5_PATH];
    if (!d) return;
    while ((entry = readdir(d))) {
        const char *name = entry->d_name;
        struct stat st;
        if (name[0] == '.' && (!name[1] || (name[1] == '.' && !name[2]))) continue;
        if (pc5_join(path, sizeof(path), dir, name) || stat(path, &st)) continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < DEPTH) walk(s, path, depth + 1);
            continue;
        }
        if (!S_ISREG(st.st_mode)) continue;
        if (!pc5_casecmp(name, "steam_appid.txt")) {
            Pc5Buf text = { 0 };
            if (!pc5_read_file(path, &text, 64) && !pc5_buf_terminate(&text)) {
                const char *p = (const char *)text.data;
                while (*p == ' ' || *p == '\t' || (unsigned char)*p == 0xef || (unsigned char)*p == 0xbb ||
                       (unsigned char)*p == 0xbf)
                    p++;
                if (*p >= '0' && *p <= '9') steam_found(s, (uint32_t)strtoul(p, NULL, 10), KIND_TXT, depth, "steam_appid.txt");
            }
            pc5_buf_free(&text);
        } else if (is_ini(name)) {
            steam_found(s, ini_appid(path), KIND_INI, depth, name);
        } else if ((!strncmp(name, "appmanifest_", 12) && pc5_ends_with_ci(name, ".acf")) ||
                   !pc5_casecmp(name, "installscript.vdf")) {
            steam_found(s, vdf_appid(path), KIND_MANIFEST, depth, name);
        } else if (!strncmp(name, "goggame-", 8) && pc5_ends_with_ci(name, ".info")) {
            gog_info(s, path, depth);
        }
    }
    closedir(d);
}

int pc5_identity_scan(const char *game_dir, Pc5Identity *id)
{
    Scan s;
    memset(&s, 0, sizeof(s));
    memset(id, 0, sizeof(*id));
    walk(&s, game_dir, 0);
    id->steam_appid = s.steam;
    id->gog_id = s.gog;
    pc5_copy(id->name, sizeof(id->name), s.gog_name);
    if (s.steam) pc5_copy(id->source, sizeof(id->source), s.steam_source);
    else if (s.gog) pc5_copy(id->source, sizeof(id->source), "goggame.info");
    return s.steam || s.gog ? 0 : -1;
}

static int identity_path(const char *root, const char *prefix, const char *name, char *out, size_t size)
{
    if (!prefix || !prefix[0] || !strcmp(prefix, "default")) return -1;
    return pc5_format(out, size, "%s/prefixes/%s/pc5/%s", root, prefix, name);
}

int pc5_identity_read(const char *root, const char *prefix, Pc5Identity *id)
{
    char path[PC5_PATH];
    Pc5Buf text = { 0 };
    memset(id, 0, sizeof(*id));
    if (identity_path(root, prefix, "identity.txt", path, sizeof(path)) || pc5_read_file(path, &text, 4096) ||
        pc5_buf_terminate(&text)) {
        pc5_buf_free(&text);
        return -1;
    }
    for (char *line = (char *)text.data, *next; line && *line; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        if (!strncmp(line, "steam_appid=", 12)) id->steam_appid = (uint32_t)strtoul(line + 12, NULL, 10);
        else if (!strncmp(line, "gog_id=", 7)) id->gog_id = strtoull(line + 7, NULL, 10);
        else if (!strncmp(line, "name=", 5)) pc5_copy(id->name, sizeof(id->name), line + 5);
        else if (!strncmp(line, "source=", 7)) pc5_copy(id->source, sizeof(id->source), line + 7);
    }
    pc5_buf_free(&text);
    return 0;
}

int pc5_identity_write(const char *root, const char *prefix, const Pc5Identity *id)
{
    char path[PC5_PATH], dir[PC5_PATH], text[PC5_NAME + 200], name[PC5_NAME];
    /* one line each: a name with a line break in it is cut there */
    pc5_copy(name, sizeof(name), id->name);
    name[strcspn(name, "\r\n")] = 0;
    if (identity_path(root, prefix, "identity.txt", path, sizeof(path)) ||
        pc5_format(dir, sizeof(dir), "%s/prefixes/%s/pc5", root, prefix) || pc5_mkdirs(dir))
        return -1;
    int n = snprintf(text, sizeof(text), "steam_appid=%u\ngog_id=%llu\nname=%s\nsource=%s\n", (unsigned)id->steam_appid,
                     (unsigned long long)id->gog_id, name, id->source);
    return n < 0 || (size_t)n >= sizeof(text) ? -1 : pc5_write_file(path, text, (size_t)n);
}

int pc5_identity_set_steam(const char *root, const char *prefix, uint32_t appid)
{
    static const char *const fetched[] = { "details.txt", "logo.png", "background.bin", "background.txt",
                                           "background-none.txt", "background-tries.txt", "cover.bin", NULL };
    char path[PC5_PATH];
    Pc5Identity id;
    if (identity_path(root, prefix, "identity.txt", path, sizeof(path))) return -1;
    if (appid) {
        if (pc5_identity_read(root, prefix, &id)) memset(&id, 0, sizeof(id));
        id.steam_appid = appid;
        pc5_copy(id.source, sizeof(id.source), "you");
        if (pc5_identity_write(root, prefix, &id)) return -1;
    } else if (unlink(path) && pc5_exists(path)) {
        return -1;   /* none: the folder is read again */
    }
    for (int i = 0; fetched[i]; i++)
        if (!identity_path(root, prefix, fetched[i], path, sizeof(path))) unlink(path);
    return 0;
}
