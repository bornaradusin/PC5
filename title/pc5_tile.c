/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: a home screen tile that is an app of its own (pc5_tile.h). */
#include "pc5_tile.h"
#include "pc5_controls.h"
#include "pc5_msi.h"
#include "pc5_steam.h"
#include "pc5_i18n.h"
#include "pc5_dds.h"
#include "pc5_home.h"
#include "pc5_image.h"
#include "pc5_json.h"
#include "third_party/psfwd/psfwd.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { STAGE_RECOVER, STAGE_FIND, STAGE_RUNTIME, STAGE_PROFILE, STAGE_READY };

/* A profile id as prospero-win takes one: a lower-case letter or digit, then
 * those, '-' and '_'. */
static int valid_id(const char *id)
{
    if (!id[0] || !((id[0] >= 'a' && id[0] <= 'z') || (id[0] >= '0' && id[0] <= '9'))) return 0;
    for (const char *c = id + 1; *c; c++)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-' || *c == '_')) return 0;
    return 1;
}

/* An absolute path with no "." or ".." part. */
static int absolute_path(const char *path)
{
    if (path[0] != '/') return 0;
    for (const char *part = path; part; part = strchr(part + 1, '/')) {
        const char *name = part + 1, *end = strchr(name, '/');
        size_t n = end ? (size_t)(end - name) : strlen(name);
        if ((n == 1 && name[0] == '.') || (n == 2 && name[0] == '.' && name[1] == '.')) return 0;
    }
    return 1;
}

int pc5_tile_read_all(const char *app, char game[PC5_ID], char *runtime, size_t runtime_size, char *error, size_t size)
{
    char path[PC5_PATH], format[32];
    Pc5Buf text = { 0 };
    Pc5Json json = { 0 };
    uint32_t value;
    int status = -1;

    game[0] = 0;
    if (runtime && runtime_size) runtime[0] = 0;
    if (error && size) error[0] = 0;
    if (!app || pc5_format(path, sizeof(path), "%s/" PC5_TILE_FILE, app)) {
        snprintf(error, size, N_("The tile's folder path is too long."));
        return -1;
    }
    if (!pc5_exists(path)) return 0;
    if (pc5_read_file(path, &text, 64 * 1024) || pc5_json_parse(&json, (const char *)text.data, text.len, 256)) {
        snprintf(error, size, N_("The tile's %s can't be read."), PC5_TILE_FILE);
        goto done;
    }
    value = pc5_json_get(&json, 0, "format");
    if (!value || json.tokens[value].type != PC5_JSON_STRING || pc5_json_text(&json, value, format, sizeof(format)) ||
        strcmp(format, PC5_TILE_FORMAT)) {
        snprintf(error, size, N_("The tile was made by a PC5 this one doesn't know: make it again."));
        goto done;
    }
    value = pc5_json_get(&json, 0, "game");
    if (!value || json.tokens[value].type != PC5_JSON_STRING || pc5_json_text(&json, value, game, PC5_ID) ||
        !valid_id(game)) {
        game[0] = 0;
        snprintf(error, size, N_("The tile doesn't say which game it starts: make it again."));
        goto done;
    }
    /* Where its Wine and PC5's files are: PC5's folder. None: they are in the tile. */
    value = pc5_json_get(&json, 0, "runtime");
    if (value && runtime && runtime_size &&
        (json.tokens[value].type != PC5_JSON_STRING || pc5_json_text(&json, value, runtime, runtime_size) ||
         !absolute_path(runtime))) {
        game[0] = runtime[0] = 0;
        snprintf(error, size, N_("The tile doesn't say where PC5 is: make it again."));
        goto done;
    }
    status = 1;
done:
    pc5_json_free(&json);
    pc5_buf_free(&text);
    return status;
}

int pc5_tile_read(const char *app, char game[PC5_ID], char *error, size_t size)
{
    char runtime[PC5_PATH];
    return pc5_tile_read_all(app, game, runtime, sizeof(runtime), error, size);
}

/* ---- making a tile ------------------------------------------------------------------ */

static int tile_id(const char *id)
{
    if (strlen(id) != 9 || memcmp(id, "PPSA", 4)) return 0;
    for (int i = 4; i < 9; i++)
        if (id[i] < '0' || id[i] > '9') return 0;
    return 1;
}

int pc5_tile_lapy_patch(uint8_t *data, size_t size, const char *title_id, char *error, size_t error_size)
{
    static const char old[] = PC5_TITLE_ID;
    size_t at[8], length[8];
    int sites = 0, compares = 0, strings = 0;

    _Static_assert(sizeof(old) == 10, "a title ID is nine characters");
    if (!title_id || !tile_id(title_id)) {
        snprintf(error, error_size, N_("A tile's ID is PPSA and five digits."));
        return -1;
    }
    for (size_t i = 0; i + 10 <= size; i++) {
        /* movabs $"PPSA1101", %reg, then xor $'8', %reg on the ninth character. */
        if (data[i] == 0x48 && data[i + 1] >= 0xb8 && data[i + 1] <= 0xbf && !memcmp(data + i + 2, old, 8)) {
            size_t imm = i + 2, j;
            for (j = imm + 8; j + 3 < size && j < imm + 48; j++)
                if (data[j] == 0x48 && data[j + 1] == 0x83) break;
            if (j + 3 >= size || j >= imm + 48 || (data[j + 2] & 0xf8) != 0xf0 || data[j + 3] != (uint8_t)old[8] ||
                sites + 2 > 8)
                goto unknown;
            at[sites] = imm, length[sites++] = 8;
            at[sites] = j + 3, length[sites++] = 1;
            compares++;
        } else if (!memcmp(data + i, old, 9) && data[i + 9] == 0 && sites < 8) {
            at[sites] = i, length[sites++] = 9;
            strings++;
        }
    }
    if (compares != 2 || strings != 1) goto unknown;
    for (int s = 0; s < sites; s++) memcpy(data + at[s], title_id + (length[s] == 1 ? 8 : 0), length[s]);
    return 0;
unknown:
    snprintf(error, error_size, N_("PC5's lapy.elf isn't the one this PC5 can make tiles from (%d compares, %d names)."),
             compares, strings);
    return -1;
}

/* A string token's text replaced: json's token, its span in text (quotes left). */
typedef struct Span {
    uint32_t start, end;
    const char *with;
    int escape;
} Span;

static int by_start(const void *a, const void *b)
{
    const Span *x = a, *y = b;
    return x->start < y->start ? -1 : x->start > y->start;
}

/* text with each span's text replaced (JSON-escaped when asked). */
static int splice(const char *text, size_t length, Span *spans, int count, Pc5Buf *out)
{
    size_t from = 0;

    qsort(spans, (size_t)count, sizeof(*spans), by_start);
    *out = (Pc5Buf){ 0 };
    for (int s = 0; s < count; s++) {
        if (spans[s].start < from || spans[s].end > length || pc5_buf_put(out, text + from, spans[s].start - from))
            goto fail;
        for (const unsigned char *c = (const unsigned char *)spans[s].with; *c; c++) {
            char escaped[8];
            int n;
            if (spans[s].escape && (*c == '"' || *c == '\\')) n = snprintf(escaped, sizeof(escaped), "\\%c", *c);
            else if (spans[s].escape && *c < 0x20) n = snprintf(escaped, sizeof(escaped), "\\u%04x", *c);
            else escaped[0] = (char)*c, n = 1;
            if (pc5_buf_put(out, escaped, (size_t)n)) goto fail;
        }
        from = spans[s].end;
    }
    if (pc5_buf_put(out, text + from, length - from) || pc5_buf_terminate(out)) goto fail;
    return 0;
fail:
    pc5_buf_free(out);
    return -1;
}

int pc5_tile_param(const char *text, size_t length, const char *title_id, const char *name, Pc5Buf *out)
{
    Pc5Json json = { 0 };
    char content[48];
    Span spans[4];
    uint32_t tokens[4], strings;
    int status = -1;

    *out = (Pc5Buf){ 0 };
    if (!tile_id(title_id) || !name || !name[0] || pc5_json_parse(&json, text, length, 1024)) goto done;
    strings = pc5_json_get(&json, 0, "localizedParameters");
    tokens[0] = pc5_json_get(&json, 0, "titleId");
    tokens[1] = pc5_json_get(&json, 0, "conceptId");
    tokens[2] = pc5_json_get(&json, 0, "contentId");
    tokens[3] = strings ? pc5_json_get(&json, pc5_json_get(&json, strings, "en-US"), "titleName") : 0;
    /* The content ID's label is the tile's own, sixteen characters as the console wants. */
    snprintf(content, sizeof(content), "UP0000-%s_00-PC5TILE000000000", title_id);
    const char *with[4] = { title_id, title_id + 4, content, name };
    for (int i = 0; i < 4; i++) {
        if (!tokens[i] || json.tokens[tokens[i]].type != PC5_JSON_STRING) goto done;
        spans[i] = (Span){ json.tokens[tokens[i]].start, json.tokens[tokens[i]].end, with[i], i == 3 };
    }
    status = splice(text, length, spans, 4, out);
done:
    pc5_json_free(&json);
    return status;
}

/* ---- the tile's folder: what the console starts, PC5's folder for the rest ---------- */

static void delete_tree(const char *path)
{
    Pc5Remove remove = { 0 };
    if (!pc5_exists(path) || pc5_tree_delete_begin(&remove, path)) return;
    while (pc5_remove_step(&remove, 50)) {}
    pc5_remove_end(&remove);
}

/* The same file: the same size and bytes. */
static int same_file(const char *a, const char *b)
{
    Pc5Buf x = { 0 }, y = { 0 };
    int same = !pc5_read_file(a, &x, 64u << 20) && !pc5_read_file(b, &y, 64u << 20) && x.len == y.len &&
               !memcmp(x.data, y.data, x.len);
    pc5_buf_free(&x);
    pc5_buf_free(&y);
    return same;
}

/* The program the console starts, its runtime, and Lapy's helper for the tile's ID, from
 * PC5's folder into dir; then pc5-tile.json naming the game, its prefix and PC5's folder.
 * Everything is read (and the helper patched) before anything is written, and eboot.bin is
 * written last: a tile refreshed only in part keeps its old program, so the next refresh sees
 * it differs and does it all again. */
static int program_files(const char *pc5, const char *dir, const char *title_id, const char *game, const char *prefix,
                         char *error, size_t size)
{
    char from[PC5_PATH], to[PC5_PATH];
    Pc5Buf eboot = { 0 }, libc = { 0 }, lapy = { 0 }, text = { 0 };
    int status = -1, failed;

    /* The tile finds its runtime with this path: as long as Wine's own (tile_runtime in
     * wine64_main.c, 128 bytes) holds it. */
    if (strlen(pc5) >= PC5_TILE_RUNTIME) {
        snprintf(error, size, N_("PC5's folder path is too long for a tile (%d characters at most)."), PC5_TILE_RUNTIME - 1);
        return -1;
    }
    if (pc5_format(from, sizeof(from), "%s/eboot.bin", pc5) || pc5_read_file(from, &eboot, 64u << 20) ||
        pc5_format(from, sizeof(from), "%s/sce_module/libc.prx", pc5) || pc5_read_file(from, &libc, 64u << 20)) {
        snprintf(error, size, N_("PC5 can't read its own program to copy into the tile."));
        goto done;
    }
    /* lapy.elf: a copy, with the tile's ID in place of PC5's. */
    if (pc5_format(from, sizeof(from), "%s/lapy.elf", pc5) || pc5_read_file(from, &lapy, 16u << 20)) {
        snprintf(error, size, N_("PC5's folder has no lapy.elf."));
        goto done;
    }
    if (pc5_tile_lapy_patch(lapy.data, lapy.len, title_id, error, size)) goto done;
    /* PC5's folder as the tile's runtime: a path in JSON, its quotes and backslashes escaped. */
    failed = pc5_buf_printf(&text, "{\n  \"format\": \"" PC5_TILE_FORMAT "\",\n  \"game\": \"%s\",\n  \"prefix\": \"%s\",\n"
                                   "  \"runtime\": \"", game, prefix && valid_id(prefix) ? prefix : game);
    for (const char *c = pc5; !failed && *c; c++)
        failed = *c == '"' || *c == '\\' ? pc5_buf_printf(&text, "\\%c", *c) : pc5_buf_put(&text, c, 1);
    if (!failed) failed = pc5_buf_printf(&text, "\",\n  \"pc5\": \"" PC5_VERSION "\"\n}\n");
    if (failed) {
        snprintf(error, size, N_("PC5 ran out of memory making the tile."));
        goto done;
    }
    if (pc5_format(to, sizeof(to), "%s/sce_module", dir) || pc5_mkdirs(to)) {
        snprintf(error, size, N_("PC5 can't make the tile's folders."));
        goto done;
    }
    if (pc5_format(to, sizeof(to), "%s/sce_module/libc.prx", dir) || pc5_write_file(to, libc.data, libc.len) ||
        chmod(to, 0755)) {
        snprintf(error, size, N_("PC5 can't copy its sce_module/libc.prx into the tile."));
        goto done;
    }
    if (pc5_format(to, sizeof(to), "%s/lapy.elf", dir) || pc5_write_file(to, lapy.data, lapy.len) || chmod(to, 0755)) {
        snprintf(error, size, N_("PC5 can't write the tile's lapy.elf."));
        goto done;
    }
    if (pc5_format(to, sizeof(to), "%s/" PC5_TILE_FILE, dir) || pc5_write_file(to, text.data, text.len)) {
        snprintf(error, size, N_("PC5 can't write the tile's %s."), PC5_TILE_FILE);
        goto done;
    }
    if (pc5_format(to, sizeof(to), "%s/eboot.bin", dir) || pc5_write_file(to, eboot.data, eboot.len) || chmod(to, 0755)) {
        snprintf(error, size, N_("PC5 can't copy its eboot.bin into the tile."));
        goto done;
    }
    status = 0;
done:
    pc5_buf_free(&eboot);
    pc5_buf_free(&libc);
    pc5_buf_free(&lapy);
    pc5_buf_free(&text);
    return status;
}

/* The game's prefix as the tile names it (pc5-tile.json's "prefix"), or "" when it doesn't
 * (a tile made before it did: its game's id is its prefix then, as PC5 makes them). */
static void tile_prefix(const char *folder, char *out, size_t size)
{
    char path[PC5_PATH];
    Pc5Buf text = { 0 };
    Pc5Json json = { 0 };
    uint32_t value;
    out[0] = 0;
    if (pc5_format(path, sizeof(path), "%s/" PC5_TILE_FILE, folder) || pc5_read_file(path, &text, 64 * 1024) ||
        pc5_json_parse(&json, (const char *)text.data, text.len, 256)) {
        pc5_buf_free(&text);
        return;
    }
    value = pc5_json_get(&json, 0, "prefix");
    if (!value || json.tokens[value].type != PC5_JSON_STRING || pc5_json_text(&json, value, out, size) || !valid_id(out))
        out[0] = 0;
    pc5_json_free(&json);
    pc5_buf_free(&text);
}

/* ---- the tile's art --------------------------------------------------------------- */

enum { PIC0_WIDTH = 1920, PIC0_HEIGHT = 1080, DDS_WIDTH = 3840, DDS_HEIGHT = 2160 };

/* The tile's art, by name in sce_sys, and what each is made of. */
typedef struct Art {
    Pc5Buf icon0, pic0, dds;   /* icon0.png; pic0.png; pic0.dds and pic1.dds, the same picture */
} Art;

static void art_free(Art *art)
{
    pc5_buf_free(&art->icon0);
    pc5_buf_free(&art->pic0);
    pc5_buf_free(&art->dds);
}

/* The game's background (background.bin, 960x540) as the home screen's: a 1920x1080 PNG and a
 * 4K BC7 DDS (pc5_dds.h). -1 when the game has none (or out of memory). */
static int background_art(const char *library_root, const char *prefix, Art *art)
{
    char path[PC5_PATH];
    Pc5Image background = { 0 }, sized = { 0 };
    int status = -1;

    if (pc5_format(path, sizeof(path), "%s/prefixes/%s/pc5/background.bin", library_root, prefix) ||
        pc5_cover_load(path, &background, NULL))
        return -1;
    if (pc5_image_resize(&background, PIC0_WIDTH, PIC0_HEIGHT, &sized) || pc5_image_png(&sized, &art->pic0)) goto done;
    pc5_image_free(&sized);
    if (pc5_image_resize(&background, DDS_WIDTH, DDS_HEIGHT, &sized) ||
        !(art->dds.data = pc5_alloc(pc5_dds_size(DDS_WIDTH, DDS_HEIGHT))) ||
        pc5_dds_bc7(sized.px, sized.w, sized.h, sized.w, art->dds.data))
        goto done;
    art->dds.len = art->dds.cap = pc5_dds_size(DDS_WIDTH, DDS_HEIGHT);
    status = 0;
done:
    if (status) {
        pc5_buf_free(&art->pic0);
        pc5_buf_free(&art->dds);
    }
    pc5_image_free(&sized);
    pc5_image_free(&background);
    return status;
}

/* art's files into folder (a tile's sce_sys, or the console's copy of it). */
static int art_write(const char *folder, const Art *art)
{
    char path[PC5_PATH];
    int failed = 0;
    if (art->icon0.len)
        failed |= pc5_format(path, sizeof(path), "%s/icon0.png", folder) || pc5_write_file(path, art->icon0.data, art->icon0.len);
    if (art->pic0.len)
        failed |= pc5_format(path, sizeof(path), "%s/pic0.png", folder) || pc5_write_file(path, art->pic0.data, art->pic0.len);
    if (art->dds.len)
        failed |= pc5_format(path, sizeof(path), "%s/pic0.dds", folder) || pc5_write_file(path, art->dds.data, art->dds.len) ||
                  pc5_format(path, sizeof(path), "%s/pic1.dds", folder) || pc5_write_file(path, art->dds.data, art->dds.len);
    return failed ? -1 : 0;
}

int pc5_tile_art(const char *tile, const char *title_id, const char *library_root, const char *prefix, const char *pc5,
                 const char *user, char *error, size_t size)
{
    static const char *const pc5_art[] = { "pic0.png", "pic0.dds", "pic1.dds" };
    char path[PC5_PATH], copy[PC5_PATH];
    Art art;
    int status = -1;

    memset(&art, 0, sizeof(art));
    if (pc5_format(path, sizeof(path), "%s/sce_sys", tile) || pc5_mkdirs(path)) {
        snprintf(error, size, N_("PC5 can't make the tile's sce_sys."));
        return -1;
    }
    if (pc5_home_picture(library_root, prefix, &art.icon0)) {
        snprintf(error, size, N_("PC5 can't make the tile's picture."));
        goto done;
    }
    /* The game's background behind its tile; PC5's own when it has none. */
    if (background_art(library_root, prefix, &art)) {
        for (size_t i = 0; pc5 && i < sizeof(pc5_art) / sizeof(pc5_art[0]); i++) {
            Pc5Buf data = { 0 };
            if (!pc5_format(copy, sizeof(copy), "%s/sce_sys/%s", pc5, pc5_art[i]) && !pc5_read_file(copy, &data, 32u << 20) &&
                !pc5_format(copy, sizeof(copy), "%s/sce_sys/%s", tile, pc5_art[i]))
                (void)pc5_write_file(copy, data.data, data.len);
            pc5_buf_free(&data);
        }
    }
    if (art_write(path, &art)) {
        snprintf(error, size, N_("PC5 can't write the tile's pictures."));
        goto done;
    }
    /* The console's copies of a registered tile's sce_sys, which ShadowMountPlus made once when it
     * first registered the tile: brought up to date as PC5's updater does its own. Best effort. */
    if (user && title_id) {
        if (!pc5_format(copy, sizeof(copy), "%s/appmeta/%s", user, title_id) && pc5_is_dir(copy)) (void)art_write(copy, &art);
        if (!pc5_format(copy, sizeof(copy), "%s/app/%s/sce_sys", user, title_id) && pc5_is_dir(copy)) (void)art_write(copy, &art);
        if (!pc5_format(copy, sizeof(copy), "%s/app/%s/icon0.png", user, title_id) && pc5_is_file(copy))
            (void)pc5_write_file(copy, art.icon0.data, art.icon0.len);
    }
    status = 0;
done:
    art_free(&art);
    return status;
}

/* The tile's sce_sys: PC5's param.json as the tile's, and its art. */
static int tile_sce_sys(const Pc5TileMake *make, const char *dir, const char *title_id, char *error, size_t size)
{
    char from[PC5_PATH], to[PC5_PATH];
    Pc5Buf data = { 0 }, text = { 0 };
    int status = -1;

    if (pc5_format(to, sizeof(to), "%s/sce_sys", dir) || pc5_mkdirs(to)) {
        snprintf(error, size, N_("PC5 can't make the tile's sce_sys."));
        return -1;
    }
    if (pc5_format(from, sizeof(from), "%s/sce_sys/param.json", make->pc5) || pc5_read_file(from, &data, 256 * 1024) ||
        pc5_tile_param((const char *)data.data, data.len, title_id, make->name && make->name[0] ? make->name : make->id,
                       &text) ||
        pc5_format(to, sizeof(to), "%s/sce_sys/param.json", dir) || pc5_write_file(to, text.data, text.len)) {
        snprintf(error, size, N_("PC5 can't write the tile's param.json from its own."));
        goto done;
    }
    if (pc5_tile_art(dir, title_id, make->library_root, make->prefix ? make->prefix : make->id, make->pc5, make->user,
                     error, size))
        goto done;
    status = 0;
done:
    pc5_buf_free(&data);
    pc5_buf_free(&text);
    return status;
}

/* A title ID for a new tile, in psfwd's range (PPSA99200 to PPSA99899, not a known app's), that
 * nothing under tiles has, nor the console's registrations (user/app, user/appmeta: an app
 * registered from another folder or drive keeps its ID there). 1 with it in out, or 0. */
static int new_tile_id(const char *tiles, const char *user, char out[16])
{
    char first[16];
    int start;
    if (!psfwd_new_title_id(tiles, first)) return 0;
    start = atoi(first + 4);
    /* From psfwd's pick on, through its range (it picks the same one again within a second). */
    for (int step = 0; step < 700; step++) {
        char path[PC5_PATH];
        int number = 99200 + (start - 99200 + step) % 700, taken = 0;
        snprintf(out, 16, "PPSA%05d", number);
        /* psfwd's IDs of known apps in its range, which it never picks */
        if (!strcmp(out, "PPSA99203") || !strcmp(out, "PPSA99764")) continue;
        if (!pc5_format(path, sizeof(path), "%s/%s", tiles, out)) taken |= pc5_exists(path);
        if (!pc5_format(path, sizeof(path), "%s/.staging-%s", tiles, out)) taken |= pc5_exists(path);
        if (!pc5_format(path, sizeof(path), "%s/.old-%s", tiles, out)) taken |= pc5_exists(path);
        if (user && user[0]) {
            if (!pc5_format(path, sizeof(path), "%s/app/%s", user, out)) taken |= pc5_exists(path);
            if (!pc5_format(path, sizeof(path), "%s/appmeta/%s", user, out)) taken |= pc5_exists(path);
        }
        if (!taken) return 1;
    }
    out[0] = 0;
    return 0;
}

int pc5_tile_make(const Pc5TileMake *make, char out[16], char *error, size_t size)
{
    char stage[PC5_PATH], final[PC5_PATH], old[PC5_PATH];
    Pc5TileMake tile = *make;
    int again;

    out[0] = 0;
    error[0] = 0;
    if (!make->tiles || !make->pc5 || !make->library_root || !make->id || !valid_id(make->id) ||
        !absolute_path(make->pc5)) {
        snprintf(error, size, N_("PC5 doesn't know where to make the tile."));
        return PC5_TILE_FAILED;
    }
    again = pc5_home_find(make->tiles, make->id, out);
    if (!again && (pc5_mkdirs(make->tiles) || !new_tile_id(make->tiles, make->user, out))) {
        snprintf(error, size, N_("PC5 can't find a free title ID for the tile in %s."), make->tiles);
        out[0] = 0;
        return PC5_TILE_FAILED;
    }
    /* Only the game's own tile, made again in place, has its art updated in the console's
     * registrations: a new ID's are someone else's, should the console have one. */
    if (!again) tile.user = NULL;
    if (pc5_format(stage, sizeof(stage), "%s/.staging-%s", make->tiles, out) ||
        pc5_format(old, sizeof(old), "%s/.old-%s", make->tiles, out) ||
        pc5_format(final, sizeof(final), "%s/%s", make->tiles, out)) {
        snprintf(error, size, N_("The tiles' folder path is too long."));
        out[0] = 0;
        return PC5_TILE_FAILED;
    }
    delete_tree(stage);
    delete_tree(old);
    if (pc5_mkdirs(stage) || program_files(make->pc5, stage, out, make->id, make->prefix, error, size) ||
        tile_sce_sys(&tile, stage, out, error, size)) {
        if (!error[0]) snprintf(error, size, N_("PC5 can't make %s: %s."), stage, strerror(errno));
        delete_tree(stage);
        out[0] = 0;
        return PC5_TILE_FAILED;
    }
    /* The old tile goes only once the new one is in its place: moved aside first, and put
     * back should the new one not go in. */
    if (pc5_exists(final) && rename(final, old)) {
        snprintf(error, size, N_("PC5 can't replace the tile: %s."), strerror(errno));
        delete_tree(stage);
        out[0] = 0;
        return PC5_TILE_FAILED;
    }
    if (rename(stage, final)) {
        snprintf(error, size, N_("PC5 can't put the tile in place: %s."), strerror(errno));
        delete_tree(stage);
        if (pc5_exists(old)) (void)rename(old, final);
        out[0] = 0;
        return PC5_TILE_FAILED;
    }
    delete_tree(old);
    return PC5_TILE_MADE;
}

/* 1 when one of the game's pictures (in its prefix) is newer than the tile's art (its icon0.png). */
static int art_stale(const char *folder, const char *library_root, const char *game)
{
    static const char *const pictures[] = { "cover.bin", "background.bin", "icon.bin", "background-none.txt" };
    char path[PC5_PATH];
    struct stat art, picture;

    if (pc5_format(path, sizeof(path), "%s/sce_sys/icon0.png", folder) || stat(path, &art)) return 1;
    for (size_t i = 0; i < sizeof(pictures) / sizeof(pictures[0]); i++)
        if (!pc5_format(path, sizeof(path), "%s/prefixes/%s/pc5/%s", library_root, game, pictures[i]) &&
            !stat(path, &picture) && picture.st_mtime > art.st_mtime)
            return 1;
    return 0;
}

int pc5_tile_refresh(const char *tiles, const char *pc5, const char *library_root, const char *user,
                     void (*log)(void *context, const char *line), void *context)
{
    char line[PC5_PATH + 200];
    DIR *dir;
    struct dirent *entry;
    int refreshed = 0;

    if (!tiles || !pc5 || !absolute_path(pc5) || !(dir = opendir(tiles))) return 0;
    while ((entry = readdir(dir))) {
        char folder[PC5_PATH], game[PC5_ID], prefix[PC5_ID], runtime[PC5_PATH], error[PC5_ERROR], ours[PC5_PATH],
             theirs[PC5_PATH];
        /* A tile made or replaced when PC5 stopped part way: the new one, or the old one put
         * aside, left beside the tile. */
        if ((!strncmp(entry->d_name, ".staging-", 9) && tile_id(entry->d_name + 9)) ||
            (!strncmp(entry->d_name, ".old-", 5) && tile_id(entry->d_name + 5))) {
            if (!pc5_join(folder, sizeof(folder), tiles, entry->d_name)) delete_tree(folder);
            continue;
        }
        if (!tile_id(entry->d_name) || pc5_join(folder, sizeof(folder), tiles, entry->d_name) ||
            pc5_tile_read_all(folder, game, runtime, sizeof(runtime), error, sizeof(error)) != 1 || !runtime[0])
            continue;
        /* A tile another PC5 made runs from that PC5's folder while it is there (a second PC5,
         * a test build): it stays that one's. It comes to this PC5 once that folder is gone. */
        if (strcmp(runtime, pc5) && pc5_format(theirs, sizeof(theirs), "%s/eboot.bin", runtime) == 0 &&
            pc5_is_file(theirs) && pc5_format(theirs, sizeof(theirs), "%s/sce_sys/param.json", runtime) == 0 &&
            pc5_is_file(theirs))
            continue;
        tile_prefix(folder, prefix, sizeof(prefix));
        if (!prefix[0]) pc5_copy(prefix, sizeof(prefix), game);
        /* This PC5's program, with this folder as its runtime. */
        int program = strcmp(runtime, pc5) || pc5_format(ours, sizeof(ours), "%s/eboot.bin", pc5) ||
                      pc5_format(theirs, sizeof(theirs), "%s/eboot.bin", folder) || !same_file(ours, theirs);
        /* The game's pictures, changed since (a background found while the library showed). */
        int art = library_root && art_stale(folder, library_root, prefix);
        if (!program && !art) continue;
        int status = program ? program_files(pc5, folder, entry->d_name, game, prefix, error, sizeof(error)) : 0;
        if (!status && art)
            status = pc5_tile_art(folder, entry->d_name, library_root, prefix, pc5, user, error, sizeof(error));
        snprintf(line, sizeof(line), "PC5 tile refreshed %s game=%s runtime=%s program=%d art=%d%s%s", entry->d_name, game,
                 pc5, program, art, status ? " failed: " : "", status ? error : "");
        if (log) log(context, line);
        if (!status) refreshed++;
    }
    closedir(dir);
    return refreshed;
}

/* ---- readying a tile's game ------------------------------------------------------- */

void pc5_tile_prepare_begin(Pc5TilePrep *prep, const char *root, const char *app, const char *id)
{
    memset(prep, 0, sizeof(*prep));
    pc5_copy(prep->root, sizeof(prep->root), root ? root : "");
    pc5_copy(prep->app, sizeof(prep->app), app ? app : "");
    pc5_copy(prep->id, sizeof(prep->id), id ? id : "");
    pc5_copy(prep->title, sizeof(prep->title), prep->id);
    prep->stage = STAGE_RECOVER;
}

static int fail(Pc5TilePrep *prep, const char *text)
{
    pc5_copy(prep->error, sizeof(prep->error), text);
    return -1;
}

/* The game found, and whether it can start; what it needs before it does. */
static int find(Pc5TilePrep *prep)
{
    Pc5Library *library = pc5_alloc(sizeof(*library));
    char program[PC5_PATH];
    int index, begun;

    if (!library) return fail(prep, N_("PC5 ran out of memory."));
    pc5_library_load(prep->root, library);
    index = pc5_library_find(library, prep->id);
    if (index < 0) {
        pc5_free(library);
        return fail(prep, N_("It isn't in PC5 any more. Take the tile off the home screen, or install the game again."));
    }
    pc5_copy(prep->title, sizeof(prep->title), library->games[index].title);
    prep->found = 1;
    prep->pc5 = library->games[index].pc5;
    if (!library->games[index].playable) {
        fail(prep, library->games[index].problem);
        pc5_free(library);
        return -1;
    }
    pc5_free(library);
    /* A .NET game started the first time: Wine Mono's runtime is copied to the console first. */
    prep->stage = STAGE_PROFILE;
    if (prep->pc5 && prep->app[0] && !pc5_game_program(prep->root, prep->id, program, sizeof(program))) {
        begun = pc5_runtime_begin(&prep->runtime, prep->root, prep->app, program);
        if (begun < 0) return fail(prep, prep->runtime.error);
        if (begun > 0) {
            prep->copying = 1;
            prep->stage = STAGE_RUNTIME;
        }
    }
    return 0;
}

int pc5_tile_prepare_step(Pc5TilePrep *prep, int budget_ms)
{
    uint64_t until = pc5_now_ms() + (uint64_t)(budget_ms > 0 ? budget_ms : 1);
    int step;

    if (prep->error[0]) return -1;
    while (prep->stage != STAGE_READY) {
        switch (prep->stage) {
        case STAGE_RECOVER:
            /* What a stopped install, update or uninstall left, as PC5 does when it opens. */
            if (pc5_recover_step(prep->root, &prep->remove, budget_ms)) return 0;
            pc5_remove_end(&prep->remove);
            prep->stage = STAGE_FIND;
            break;
        case STAGE_FIND:
            if (find(prep)) return -1;
            break;
        case STAGE_RUNTIME:
            step = pc5_runtime_step(&prep->runtime, budget_ms);
            if (step < 0) return fail(prep, prep->runtime.error);
            if (!step) return 0;
            pc5_runtime_end(&prep->runtime);
            prep->copying = 0;
            prep->stage = STAGE_PROFILE;
            break;
        case STAGE_PROFILE: {
            char error[PC5_ERROR] = "", path[PC5_PATH], game_dir[PC5_PATH], program[PC5_PATH];
            /* What PC5 does before a game starts from its library, from a tile too: PhysX
             * registered, a program in Steam's DRM refused, and the Steam emulator put in place
             * or taken away as the game's menu says (a tile start skipped it before 2.3.5, and a
             * game that needs Steam closed at once). */
            if (prep->pc5 && !pc5_game_folder(prep->root, prep->id, game_dir, sizeof(game_dir)) &&
                !pc5_game_program(prep->root, prep->id, program, sizeof(program))) {
                Pc5Controls chosen;
                char why[PC5_ERROR];
                pc5_physx_register(prep->root, prep->id, NULL, NULL);
                if (pc5_steam_stub(program)) {
                    pc5_steam_check_keep(prep->root, prep->id, game_dir, program, 1, 1, NULL, 0);
                    return fail(prep, N_("Its program is wrapped in Steam's DRM (SteamStub), which starts only with Steam "
                                         "running, and Steam doesn't run on the console. A copy of the game without it can "
                                         "start."));
                }
                pc5_controls_load(prep->root, prep->id, &chosen);
                if (prep->app[0])
                    pc5_steam_prepare(prep->root, prep->id, game_dir, program, prep->app, !chosen.steam_off, why, sizeof(why));
                pc5_steam_check_keep(prep->root, prep->id, game_dir, program, !chosen.steam_off, 0, NULL, 0);
            }
            /* Its profile from PC5's notes and its settings, as every start of a PC5 game. */
            if (prep->pc5 && pc5_profile_refresh(prep->root, prep->id, error, sizeof(error))) return fail(prep, error);
            /* The game PC5's library shows first next time. */
            if (!pc5_format(path, sizeof(path), "%s/pc5", prep->root) && !pc5_mkdirs(path) &&
                !pc5_format(path, sizeof(path), "%s/pc5/last.txt", prep->root))
                pc5_write_file(path, prep->id, strlen(prep->id));
            prep->stage = STAGE_READY;
            break;
        }
        default:
            return fail(prep, N_("PC5 lost its place readying the game."));
        }
        if (prep->stage != STAGE_READY && pc5_now_ms() >= until) return 0;
    }
    return 1;
}

void pc5_tile_prepare_end(Pc5TilePrep *prep)
{
    pc5_remove_end(&prep->remove);
    pc5_runtime_end(&prep->runtime);
}
