/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5's host tests: the title's code on real files, against prospero-win's own
 * profile parser and library loader.
 *
 *   test_pc5 WORK_DIR FIXTURES PROGRAMS COVERS
 *
 * PROGRAMS holds tests/make-test-programs.py's Windows programs, COVERS
 * tests/make-sample-covers.py's pictures. */
#include "pc5.h"
#include "pc5_backgrounds.h"
#include "pc5_cover.h"
#include "pc5_files.h"
#include "pc5_game.h"
#include "pc5_home.h"
#include "pc5_image.h"
#include "pc5_json.h"
#include "pc5_library.h"
#include "pc5_logs.h"
#include "pc5_qr.h"
#include "pc5_report.h"
#include "pc5_pe.h"
#include "pc5_settings.h"
#include "pc5_source.h"
#include "pc5_ui.h"
#include "pc5_sound.h"
#include "pc5_tile.h"
#include "pc5_dds.h"
#include "pc5_update.h"
#include "pc5_upgrade.h"
#include "pc5_zip.h"

#include "pw_game_profile.h"
#include "pw_wine_library.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

static int failures, checks;
static const char *work, *fixtures, *programs, *covers;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static void run(const char *command)
{
    if (system(command) != 0) { fprintf(stderr, "setup failed: %s\n", command); exit(2); }
}

static void path(char *out, size_t size, const char *format, ...) __attribute__((format(printf, 3, 4)));
static void path(char *out, size_t size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(out, size, format, args);
    va_end(args);
}

static void write_text(const char *file, const char *text)
{
    char dir[PC5_PATH];
    pc5_copy(dir, sizeof(dir), file);
    *strrchr(dir, '/') = 0;
    pc5_mkdirs(dir);
    if (pc5_write_file(file, text, strlen(text))) { fprintf(stderr, "can't write %s\n", file); exit(2); }
}

static void copy_file(const char *from, const char *to)
{
    char command[3 * PC5_PATH];
    char dir[PC5_PATH];
    pc5_copy(dir, sizeof(dir), to);
    *strrchr(dir, '/') = 0;
    pc5_mkdirs(dir);
    snprintf(command, sizeof(command), "cp '%s' '%s'", from, to);
    run(command);
}

static int file_is(const char *file, const char *text);
static int file_holds_text(const char *file, const char *text);
static void write_png(const char *file, int w, int h, uint32_t color);
static void save_shot(const Pc5Canvas *c, const char *name);

static int same_files(const char *a, const char *b)
{
    Pc5Buf x = { 0 }, y = { 0 };
    int same = !pc5_read_file(a, &x, 1u << 30) && !pc5_read_file(b, &y, 1u << 30) && x.len == y.len &&
               !memcmp(x.data, y.data, x.len);
    pc5_buf_free(&x);
    pc5_buf_free(&y);
    return same;
}

/* ---- SHA-256 ----------------------------------------------------------------------------- */

static void hex(const uint8_t *digest, char *out)
{
    for (int i = 0; i < 32; i++) sprintf(out + 2 * i, "%02x", digest[i]);
}

static void test_sha256(void)
{
    Pc5Sha256 sha;
    uint8_t digest[32];
    char text[65];
    static char million[1000000];

    pc5_sha256_init(&sha);
    pc5_sha256_final(&sha, digest);
    hex(digest, text);
    CHECK(!strcmp(text, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"), "empty: %s", text);
    pc5_sha256_init(&sha);
    pc5_sha256_update(&sha, "abc", 3);
    pc5_sha256_final(&sha, digest);
    hex(digest, text);
    CHECK(!strcmp(text, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "abc: %s", text);
    memset(million, 'a', sizeof(million));
    pc5_sha256_init(&sha);
    for (int i = 0; i < 1000000; i += 777) pc5_sha256_update(&sha, million + i, 1000000 - i < 777 ? 1000000 - i : 777);
    pc5_sha256_final(&sha, digest);
    hex(digest, text);
    CHECK(!strcmp(text, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"), "million a: %s", text);
}

/* ---- JSON ------------------------------------------------------------------------------------- */

static void test_json(void)
{
    char file[PC5_PATH], text[600];
    Pc5Buf data = { 0 };
    Pc5Json json;
    const char *tricky = "{\"a\":[1,2,{\"b\":\"x\\u00e9\\\"y\\/z\"}],\"c\":true,\"d\":null,\"e\":-1.5e2,\"f\":\"\\ud83c\\udfae\"}";

    CHECK(!pc5_json_parse(&json, tricky, strlen(tricky), 100), "tricky parses");
    uint32_t b = pc5_json_get(&json, pc5_json_item(&json, pc5_json_get(&json, 0, "a"), 2), "b");
    pc5_json_text(&json, b, text, sizeof(text));
    CHECK(!strcmp(text, "x\xc3\xa9\"y/z"), "escapes: %s", text);
    CHECK(pc5_json_number(&json, pc5_json_get(&json, 0, "e"), 0) == -150.0, "number");
    pc5_json_text(&json, pc5_json_get(&json, 0, "f"), text, sizeof(text));
    CHECK(!strcmp(text, "\xf0\x9f\x8e\xae"), "surrogate pair");
    CHECK(!pc5_json_get(&json, 0, "missing") && !pc5_json_item(&json, pc5_json_get(&json, 0, "a"), 3), "absent");
    pc5_json_free(&json);
    CHECK(pc5_json_parse(&json, "{\"a\":1,}", 8, 10), "trailing comma refused");
    CHECK(pc5_json_parse(&json, "[1 2]", 5, 10), "missing comma refused");
    CHECK(pc5_json_parse(&json, "{\"a\":\"b", 7, 10), "open string refused");
    CHECK(pc5_json_parse(&json, "[[[[[[1]]]]]]", 13, 3), "token limit");

    path(file, sizeof(file), "%s/gog-catalog.json", fixtures);
    CHECK(!pc5_read_file(file, &data, 1 << 20), "gog fixture");
    CHECK(!pc5_json_parse(&json, (char *)data.data, data.len, 10000), "gog parses");
    uint32_t product = pc5_json_item(&json, pc5_json_get(&json, 0, "products"), 0);
    pc5_json_text(&json, pc5_json_get(&json, product, "coverVertical"), text, sizeof(text));
    CHECK(!strcmp(text, "https://images.gog-statics.com/e19cac8a80476f260cc9477d54230e406107f2f61a983239af4a79f3aa84a5cc.jpg"),
          "gog cover: %s", text);
    pc5_json_free(&json);
    pc5_buf_free(&data);
}

/* ---- names and the cover search --------------------------------------------------------------- */

static void test_names(void)
{
    /* Text for and from the console's keyboard (UTF-16), every kind of character there and back. */
    {
        uint16_t wide[16];
        char back[64];
        const char *text = "A\xc3\xa9\xe2\x82\xac\xf0\x9f\x8e\xae";   /* A, e acute, the euro, a game controller */
        CHECK(pc5_utf16_from_utf8(wide, 16, text) == 5 && wide[0] == 'A' && wide[1] == 0xe9 && wide[2] == 0x20ac &&
                  wide[3] == 0xd83c && wide[4] == 0xdfae && !wide[5],
              "UTF-16: %04x %04x %04x %04x %04x", wide[0], wide[1], wide[2], wide[3], wide[4]);
        pc5_utf8_from_utf16(back, sizeof(back), wide);
        CHECK(!strcmp(back, text), "and back");
        CHECK(pc5_utf16_from_utf8(wide, 4, text) == 3 && wide[2] == 0x20ac && !wide[3], "cut to fit, not in a pair");
        CHECK(pc5_utf16_from_utf8(wide, 16, "\xff\xc3x") == 3 && wide[0] == 0xfffd && wide[1] == 0xfffd && wide[2] == 'x',
              "a broken byte is U+FFFD");
        CHECK(pc5_utf16_from_utf8(wide, 16, "\xc0\xaf") == 1 && wide[0] == 0xfffd, "an overlong form too");
        uint16_t lone[] = { 'a', 0xd800, 'b', 0 };
        pc5_utf8_from_utf16(back, sizeof(back), lone);
        CHECK(!strcmp(back, "a\xef\xbf\xbd" "b"), "a lone surrogate is U+FFFD");
        uint16_t euro[] = { 'x', 0x20ac, 0 };
        pc5_utf8_from_utf16(back, 4, euro);
        CHECK(!strcmp(back, "x"), "cut before a character that doesn't fit: %s", back);
    }
    char q[3][PC5_NAME];
    Pc5CoverHint hint = { .folder = "Neon.Drift.Complete.Edition.v1.2.0-GOG", .product = "Neon Drift: Complete Edition",
                          .program = "NeonDrift" };
    int n = pc5_cover_queries(&hint, q);

    CHECK(n == 3, "three queries, got %d", n);
    CHECK(n > 0 && !strcmp(q[0], "Neon Drift: Complete Edition"), "product first: %s", q[0]);
    CHECK(n > 1 && !strcmp(q[1], "Neon Drift Complete Edition"), "folder cleaned: %s", n > 1 ? q[1] : "");
    CHECK(n > 2 && !strcmp(q[2], "Neon Drift"), "program split: %s", n > 2 ? q[2] : "");
    Pc5CoverHint unity = { .folder = "HalfLife2", .product = "Unity", .program = "hl2" };
    n = pc5_cover_queries(&unity, q);
    CHECK(n >= 1 && !strcmp(q[0], "Half Life 2"), "an engine's name is skipped: %s", q[0]);
    CHECK(pc5_cover_similarity("Grand Theft Auto IV", "Grand Theft Auto IV: The Complete Edition") >= 95, "editions ignored");
    CHECK(pc5_cover_similarity("GTA IV", "Grand Theft Auto IV") >= 90, "initials: %d", pc5_cover_similarity("GTA IV", "Grand Theft Auto IV"));
    CHECK(pc5_cover_similarity("Neon Drift", "The Witcher 2: Assassins of Kings") < 20, "unrelated");
    CHECK(pc5_cover_similarity("Neon Drift", "Neon Drift 2") >= 60 &&
          pc5_cover_similarity("Neon Drift", "Neon Drift 2") < pc5_cover_similarity("Neon Drift", "Neon Drift"),
          "a sequel scores lower");
    char encoded[64];
    pc5_url_encode("Dust & Dawn: 2", encoded, sizeof(encoded));
    CHECK(!strcmp(encoded, "Dust%20%26%20Dawn%3A%202"), "url encode: %s", encoded);
}

typedef struct FakeNet {
    int offline;
    int requests;
    char last[1024];
    const char *release;     /* GitHub's answer about the newest PC5, or NULL: none published */
    /* the release's zip, for downloads */
    char zip[PC5_PATH];
    int download_status;     /* 0: 200 */
    int corrupt;             /* one byte of the download changed */
    uint64_t gate_bytes;     /* the download waits there while gate is set */
    volatile int gate, at_gate;
    int downloads;
    /* the release's update zip, and how often each zip was downloaded */
    char update_zip[PC5_PATH];
    int zip_downloads, update_downloads;
    /* SteamGridDB: the Authorization asked with last, how many requests it had, and a status
     * it answers everything with (0: as asked) */
    char authorization[128];
    int griddb_requests, griddb_status;
    int no_steam_heroes;     /* Steam has no library hero for any game */
} FakeNet;

static int fake_get(void *context, const char *url, Pc5Buf *out, size_t max, int *status)
{
    FakeNet *net = context;
    char file[PC5_PATH] = "";

    net->requests++;
    pc5_copy(net->last, sizeof(net->last), url);
    out->len = 0;
    if (net->offline) return -1;
    *status = 200;
    if (net->no_steam_heroes && (strstr(url, "library_hero") || strstr(url, "IStoreBrowseService"))) {
        *status = 404;
        return 0;
    }
    if (!strcmp(url, PC5_RELEASES_API)) {
        if (!net->release) { *status = 404; return pc5_buf_puts(out, "{\"message\":\"Not Found\"}"); }
        return pc5_buf_puts(out, net->release);
    }
    if (!strncmp(url, "https://store.steampowered.com/api/storesearch/?term=", 53)) {
        if (strstr(url, "Neon")) path(file, sizeof(file), "%s/steam-search.json", fixtures);
        else return pc5_buf_puts(out, "{\"total\":0,\"items\":[]}");
    } else if (!strncmp(url, "https://catalog.gog.com/v1/catalog?query=like:", 46)) {
        path(file, sizeof(file), "%s/gog-catalog.json", fixtures);
    } else if (!strncmp(url, "https://www.steamgriddb.com/", 27)) {
        *status = 401;   /* the API without its key */
        return pc5_buf_puts(out, "{\"success\":false,\"errors\":[\"Authentication required\"]}");
    } else if (!strncmp(url, "https://cdn2.steamgriddb.com/grid/", 34)) {
        path(file, sizeof(file), "%s/cover-04.png", covers);
    } else if (!strncmp(url, "https://cdn2.steamgriddb.com/hero/", 34)) {
        path(file, sizeof(file), "%s/wide.png", covers);
    } else if (!strncmp(url, "https://www.pcgamingwiki.com/w/api.php?", 39)) {
        if (!strstr(url, "generator=search") || !strstr(url, "rvslots=main") || !strstr(url, "formatversion=2")) {
            *status = 400;
            return 0;
        }
        path(file, sizeof(file), "%s/pcgamingwiki.json", fixtures);
    } else if (!strcmp(url, "https://www.pcgamingwiki.com/wiki/Special:FilePath/Neon%20Drift%20cover.jpg")) {
        path(file, sizeof(file), "%s/cover-02.png", covers);
    } else if (!strncmp(url, "https://www.wikidata.org/w/api.php?action=wbsearchentities&", 59)) {
        path(file, sizeof(file), "%s/wikidata-search.json", fixtures);
    } else if (!strncmp(url, "https://www.wikidata.org/w/api.php?action=wbgetentities&", 56)) {
        if (!strstr(url, "ids=Q900111%7CQ900222%7CQ900333") || !strstr(url, "props=claims%7Clabels")) { *status = 400; return 0; }
        path(file, sizeof(file), "%s/wikidata-entities.json", fixtures);
    } else if (!strcmp(url, "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/901003/library_600x900_2x.jpg")) {
        path(file, sizeof(file), "%s/cover-03.png", covers);
    } else if (!strncmp(url, "https://api.steampowered.com/IStoreBrowseService/GetItems/v1/?input_json=", 73)) {
        path(file, sizeof(file), "%s/steam-items.json", fixtures);
    } else if (!strcmp(url, "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/901001/0a1b2c3d/library_600x900_2x.jpg?t=1700000000")) {
        path(file, sizeof(file), "%s/cover-00.png", covers);
    } else if (!strcmp(url, "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/901002/library_600x900_2x.jpg")) {
        path(file, sizeof(file), "%s/cover-01.png", covers);
    } else if (!strcmp(url, "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/901001/library_hero.jpg?t=1700000000")) {
        path(file, sizeof(file), "%s/wide.png", covers);   /* the hero, by the name the asset list gives */
    } else if (strstr(url, "images.gog-statics.com")) {
        path(file, sizeof(file), "%s/box.jpg", covers);
    } else {
        *status = 404;   /* e.g. the plain library picture of a newer app */
        return 0;
    }
    if (pc5_read_file(file, out, max)) { *status = 500; return 0; }
    return 0;
}

#define TEST_GRIDDB_KEY "0123456789abcdef0123456789abcdef"

/* SteamGridDB's API, which wants "Bearer <key>": its search, and the grids and heroes of the
 * made-up game 5001 (Steam app 901001). */
static int fake_get_authorized(void *context, const char *url, const char *authorization, Pc5Buf *out, size_t max,
                               int *status)
{
    FakeNet *net = context;
    char file[PC5_PATH] = "";
    const char *api = "https://www.steamgriddb.com/api/v2/", *plain =
        "&types=static&mimes=image%2Fpng%2Cimage%2Fjpeg&nsfw=false&humor=false&epilepsy=false";

    if (strncmp(url, api, strlen(api))) return fake_get(context, url, out, max, status);   /* the key goes nowhere else */
    net->requests++;
    net->griddb_requests++;
    pc5_copy(net->last, sizeof(net->last), url);
    pc5_copy(net->authorization, sizeof(net->authorization), authorization);
    out->len = 0;
    if (net->offline) return -1;
    url += strlen(api);
    if (strcmp(authorization, "Bearer " TEST_GRIDDB_KEY)) {
        *status = 401;
        return pc5_buf_puts(out, "{\"success\":false,\"errors\":[\"Invalid API key\"]}");
    }
    if (net->griddb_status) {
        *status = net->griddb_status;
        return pc5_buf_puts(out, "not json");
    }
    *status = 200;
    if (!strncmp(url, "search/autocomplete/", 20)) {
        if (!strncmp(url + 20, "Neon", 4)) path(file, sizeof(file), "%s/griddb-search.json", fixtures);
        else return pc5_buf_puts(out, "{\"success\":true,\"data\":[]}");
    } else if ((!strncmp(url, "grids/steam/901001?", 19) || !strncmp(url, "grids/game/5001?", 16)) &&
               strstr(url, "?dimensions=600x900%2C660x930&") && strstr(url, plain)) {
        path(file, sizeof(file), "%s/griddb-grids.json", fixtures);
    } else if ((!strncmp(url, "grids/steam/901001?", 19) || !strncmp(url, "grids/game/5001?", 16)) &&
               strstr(url, "?dimensions=512x512%2C1024x1024&") && strstr(url, plain)) {
        path(file, sizeof(file), "%s/griddb-squares.json", fixtures);
    } else if ((!strncmp(url, "heroes/steam/901001?", 20) || !strncmp(url, "heroes/game/5001?", 17)) &&
               strstr(url, "?dimensions=1920x620%2C3840x1240&") && strstr(url, plain)) {
        path(file, sizeof(file), "%s/griddb-heroes.json", fixtures);
    } else {
        *status = 404;
        return pc5_buf_puts(out, "{\"success\":false,\"errors\":[\"Game not found\"]}");
    }
    if (pc5_read_file(file, out, max)) { *status = 500; return 0; }
    return 0;
}

#define TEST_ZIP_URL "https://github.com/bornaradusin/PC5/releases/download/v99.0.0/PPSA11018.zip"
#define TEST_UPDATE_URL "https://github.com/bornaradusin/PC5/releases/download/v99.0.0/PPSA11018-update.zip"

/* GitHub's download of the release's zip or update zip: a piece at a time, as pc5_net_download gives it. */
static int fake_download(void *context, const char *url, const Pc5Sink *sink, int *status)
{
    FakeNet *net = context;
    Pc5Buf zip = { 0 };
    int result = 0;

    net->downloads++;
    *status = 0;
    if (net->offline) return -1;
    const char *file = !strcmp(url, TEST_ZIP_URL) ? net->zip : !strcmp(url, TEST_UPDATE_URL) ? net->update_zip : NULL;
    if (file == net->zip) net->zip_downloads++;
    if (file && file == net->update_zip) net->update_downloads++;
    if (!file || !file[0] || pc5_read_file(file, &zip, 64u << 20)) { *status = 404; return 0; }
    *status = net->download_status ? net->download_status : 200;
    if (*status != 200) goto out;
    if (net->corrupt && zip.len > 400) zip.data[zip.len / 3] ^= 0x5a;
    if (sink->begin && sink->begin(sink->context, zip.len)) { result = -1; goto out; }
    for (size_t at = 0; at < zip.len;) {
        if (net->gate && net->gate_bytes && at >= net->gate_bytes) {
            net->at_gate = 1;
            while (net->gate) usleep(1000);
        }
        if (sink->stop && sink->stop(sink->context)) { result = -1; goto out; }
        size_t n = zip.len - at < 7000 ? zip.len - at : 7000;
        if (net->gate && at < net->gate_bytes && n > net->gate_bytes - at) n = (size_t)(net->gate_bytes - at);   /* up to the gate */
        if (sink->write(sink->context, zip.data + at, n)) { result = -1; goto out; }
        at += n;
    }
out:
    pc5_buf_free(&zip);
    return result;
}

/* Square pictures for a game's tile: Steam to know the game, then SteamGridDB's square grids
 * alone, each a choice with its icon; nothing without a key. */
static void test_icon_search(void)
{
    FakeNet net = { 0 };
    Pc5Http http = { &net, fake_get, NULL, fake_get_authorized };
    Pc5CoverSearch *search = pc5_alloc(sizeof(*search));
    Pc5CoverHint hint = { .folder = "NeonDrift", .product = "Neon Drift: Complete Edition", .program = "NeonDrift",
                          .icons = 1 };
    Pc5Image icon = { 0 };
    int icons = 0, first = -1;

    pc5_cover_search_start(search, &hint);
    while (pc5_cover_search_step(search, &http)) {}
    for (int i = 0; i < search->count; i++) icons += search->choices[i].icon[0] != 0;
    CHECK(!icons && net.griddb_requests == 0, "no key: no square pictures, SteamGridDB not asked");

    pc5_copy(hint.steamgriddb_key, sizeof(hint.steamgriddb_key), TEST_GRIDDB_KEY);
    net.requests = net.griddb_requests = 0;
    pc5_cover_search_start(search, &hint);
    while (pc5_cover_search_step(search, &http)) {}
    icons = 0;
    for (int i = 0; i < search->count; i++) {
        const Pc5CoverChoice *choice = &search->choices[i];
        CHECK(strcmp(choice->source, "GOG") && strcmp(choice->source, "PCGamingWiki") && strcmp(choice->source, "Wikidata"),
              "only Steam and SteamGridDB asked: %s", choice->source);
        if (!choice->icon[0]) continue;
        icons++;
        if (first < 0) first = i;
        CHECK(!strcmp(choice->source, "SteamGridDB") && !strcmp(choice->icon, choice->url) &&
              !strstr(choice->icon, "notsquare"), "a square one: %s", choice->icon);
    }
    CHECK(icons == 2 && net.griddb_requests == 1 && strstr(net.last, "dimensions=512x512%2C1024x1024"),
          "two square pictures in one request: %d %d %s", icons, net.griddb_requests, net.last);
    CHECK(first == 0 && strstr(search->choices[0].icon, "91sq1024"), "SteamGridDB's best first, ahead of Steam's covers");
    CHECK(!pc5_icon_fetch(&search->choices[first], &http, &icon) && icon.w == PC5_ICON_SIZE && icon.h == PC5_ICON_SIZE,
          "made a tile picture: %dx%d", icon.w, icon.h);
    pc5_image_free(&icon);
    {
        Pc5CoverChoice none = { .source = "Steam", .url = "https://example.invalid/cover.jpg" };
        CHECK(pc5_icon_fetch(&none, &http, &icon) == -1 && !icon.px, "a choice without a square picture: none");
    }
    pc5_free(search);
}

static void test_cover_search(void)
{
    FakeNet net = { 0 };
    Pc5Http http = { &net, fake_get, NULL, fake_get_authorized };
    Pc5CoverSearch search;
    Pc5CoverHint hint = { .folder = "NeonDrift", .product = "Neon Drift: Complete Edition", .program = "NeonDrift" };
    Pc5Image cover;

    pc5_cover_search_start(&search, &hint);
    int steps = 0;
    while (pc5_cover_search_step(&search, &http) && steps < 20) steps++;
    CHECK(search.answered >= 3, "sources answered: %d", search.answered);
    CHECK(search.count >= 3, "choices: %d", search.count);
    CHECK(search.count && !strcmp(search.choices[0].source, "Steam") && search.choices[0].steam_appid == 901001,
          "Steam's exact game first: %s %u", search.choices[0].source, search.choices[0].steam_appid);
    int has_witcher = 0, has_soundtrack = 0;
    for (int i = 0; i < search.count; i++) {
        has_witcher |= strstr(search.choices[i].title, "Witcher") != NULL;
        has_soundtrack |= strstr(search.choices[i].title, "Soundtrack") != NULL;
        CHECK(strcmp(search.choices[i].source, "SteamGridDB"), "SteamGridDB without a key");
        CHECK(!strstr(search.choices[i].url, "wikipedia") && !strstr(search.choices[i].url, "wikimedia"),
              "no picture from Wikipedia: %s", search.choices[i].url);
    }
    CHECK(net.griddb_requests == 0 && search.griddb_status == 0, "SteamGridDB not asked without a key");
    CHECK(!has_witcher, "an unrelated GOG game was kept");
    CHECK(!has_soundtrack, "an unrelated Steam app was kept");
    /* The plain library picture is missing (newer app): the asset list gives the real one. */
    CHECK(!pc5_cover_fetch(&search.choices[0], &http, &cover), "Steam cover through GetItems");
    CHECK(cover.w == PC5_COVER_WIDTH && cover.h == PC5_COVER_HEIGHT, "cover size %dx%d", cover.w, cover.h);
    CHECK(strstr(net.last, "0a1b2c3d/library_600x900_2x.jpg"), "fetched the hashed name: %s", net.last);
    pc5_image_free(&cover);

    /* A wide picture sits whole on a blurred copy of itself. */
    {
        Pc5CoverChoice wide = { .source = "SteamGridDB", .url = "https://cdn2.steamgriddb.com/hero/82small.jpg" };
        CHECK(!pc5_cover_fetch(&wide, &http, &cover), "a wide picture");
        /* top rows are the darkened blur, the middle the picture */
        uint32_t top = cover.px ? cover.px[5 * cover.w + cover.w / 2] : 0xffffffffu;
        CHECK(((top >> 16) & 255) < 200 && ((top >> 8) & 255) < 200, "blurred and darkened above");
        pc5_image_free(&cover);
    }

    /* SteamGridDB, with a key: by the Steam app Steam's search found (no search of its own),
     * its covers after Steam's and before GOG's, each with a background, 1920x620 first. */
    {
        Pc5CoverHint keyed = hint;
        int first = -1, gog = -1, steam = -1, griddb = 0;
        pc5_copy(keyed.steamgriddb_key, sizeof(keyed.steamgriddb_key), TEST_GRIDDB_KEY);
        net.griddb_requests = 0;
        pc5_cover_search_start(&search, &keyed);
        while (pc5_cover_search_step(&search, &http)) {}
        for (int i = 0; i < search.count; i++) {
            const Pc5CoverChoice *choice = &search.choices[i];
            if (!strcmp(choice->source, "SteamGridDB")) {
                griddb++;
                if (first < 0) first = i;
                CHECK(!strcmp(choice->title, search.choices[0].title) && choice->steam_appid == 901001 &&
                      !strncmp(choice->url, "https://cdn2.steamgriddb.com/grid/", 34), "SteamGridDB's cover: %s %s",
                      choice->title, choice->url);
                CHECK(!strncmp(choice->background, "https://cdn2.steamgriddb.com/hero/", 34), "with a background: %s",
                      choice->background);
            }
            if (!strcmp(choice->source, "GOG") && gog < 0) gog = i;
            if (!strcmp(choice->source, "Steam") && steam < 0) steam = i;
        }
        CHECK(griddb == 3 && net.griddb_requests == 2 && search.griddb_status == 200 &&
              !strcmp(net.authorization, "Bearer " TEST_GRIDDB_KEY), "3 covers in 2 requests: %d, %d, %s", griddb,
              net.griddb_requests, net.authorization);
        CHECK(steam == 0 && first > steam && (gog < 0 || first < gog), "Steam, SteamGridDB, GOG: %d %d %d", steam, first, gog);
        CHECK(first >= 0 && strstr(search.choices[first].url, "/71a2b3.png") &&
              strstr(search.choices[first].background, "/82small.jpg"), "the best first, its 1920x620 background");
        if (first >= 0) {
            CHECK(!pc5_cover_fetch(&search.choices[first], &http, &cover), "SteamGridDB's picture");
            CHECK(cover.w == PC5_COVER_WIDTH && cover.h == PC5_COVER_HEIGHT, "made a cover: %dx%d", cover.w, cover.h);
            pc5_image_free(&cover);
            Pc5Image background = { 0 };
            CHECK(!pc5_background_fetch(search.choices[first].background, 0, &http, &background) &&
                  background.w == 960 && background.h == 540, "and its background: %dx%d", background.w, background.h);
            pc5_image_free(&background);
        }

        /* Steam turned off: SteamGridDB's own search, its best match (not the sequel). */
        keyed.off = PC5_PICTURES_STEAM;
        net.griddb_requests = 0;
        pc5_cover_search_start(&search, &keyed);
        while (pc5_cover_search_step(&search, &http)) {}
        griddb = 0;
        for (int i = 0; i < search.count; i++) griddb += !strcmp(search.choices[i].source, "SteamGridDB");
        CHECK(griddb == 3 && net.griddb_requests == 3 && search.griddb_game == 5001 && !search.griddb_app &&
              !strcmp(search.griddb_title, "Neon Drift"), "by name: %d covers, %d requests, game %u", griddb,
              net.griddb_requests, (unsigned)search.griddb_game);

        /* A key refused, too many requests, an answer that isn't JSON, no network: the other
         * sources' covers all the same. */
        static const int answers[] = { 401, 429, 200, 500 };
        for (int a = 0; a < 4; a++) {
            Pc5CoverHint other = hint;
            pc5_copy(other.steamgriddb_key, sizeof(other.steamgriddb_key), a ? TEST_GRIDDB_KEY : "not-the-key");
            net.griddb_status = a ? answers[a] : 0;
            pc5_cover_search_start(&search, &other);
            while (pc5_cover_search_step(&search, &http)) {}
            griddb = 0;
            for (int i = 0; i < search.count; i++) griddb += !strcmp(search.choices[i].source, "SteamGridDB");
            CHECK(!griddb && search.count >= 3 && !strcmp(search.choices[0].source, "Steam") &&
                  search.griddb_status == answers[a], "SteamGridDB answering %d: %d choices, status %d", answers[a],
                  search.count, search.griddb_status);
            net.griddb_status = 0;
        }
        /* Off in the settings: not asked, key or not. */
        keyed.off = PC5_PICTURES_STEAMGRIDDB;
        net.griddb_requests = 0;
        pc5_cover_search_start(&search, &keyed);
        while (pc5_cover_search_step(&search, &http)) {}
        CHECK(!net.griddb_requests, "SteamGridDB off: not asked");
        /* No way to send a key (a platform without it): left out. */
        Pc5Http plain = { &net, fake_get, NULL, NULL };
        keyed.off = 0;
        pc5_cover_search_start(&search, &keyed);
        while (pc5_cover_search_step(&search, &plain)) {}
        griddb = 0;
        for (int i = 0; i < search.count; i++) griddb += !strcmp(search.choices[i].source, "SteamGridDB");
        CHECK(!griddb && search.count >= 3, "no authorized GET: SteamGridDB left out, the others there");
    }

    /* PCGamingWiki: the page's infobox cover, its Steam app's hero the background; Wikidata:
     * its items' Steam apps, an app Steam's search found already once. */
    {
        int pcgw = -1, wikidata = -1, steam = 0;
        for (int i = 0; i < search.count; i++) {
            if (!strcmp(search.choices[i].source, "PCGamingWiki")) pcgw = i;
            if (!strcmp(search.choices[i].source, "Wikidata")) wikidata = i;
            steam += strstr(search.choices[i].url, "/901001/library_600x900_2x.jpg") != NULL;
            CHECK(!strstr(search.choices[i].title, "Road Kings"), "an unrelated PCGamingWiki page kept");
        }
        CHECK(pcgw >= 0 && !strcmp(search.choices[pcgw].title, "Neon Drift") &&
              !strcmp(search.choices[pcgw].url, "https://www.pcgamingwiki.com/wiki/Special:FilePath/Neon%20Drift%20cover.jpg") &&
              search.choices[pcgw].steam_appid == 901001 && strstr(search.choices[pcgw].background, "/901001/library_hero.jpg"),
              "PCGamingWiki's cover: %s", pcgw >= 0 ? search.choices[pcgw].url : "none");
        CHECK(wikidata >= 0 && search.choices[wikidata].steam_appid == 901003 &&
              !strcmp(search.choices[wikidata].title, "Neon Drift: Overdrive"), "Wikidata's other app");
        CHECK(steam == 1, "one choice for one Steam picture: %d", steam);
        if (pcgw >= 0) {
            CHECK(!pc5_cover_fetch(&search.choices[pcgw], &http, &cover), "PCGamingWiki's picture");
            pc5_image_free(&cover);
        }
        if (wikidata >= 0) {
            CHECK(!pc5_cover_fetch(&search.choices[wikidata], &http, &cover), "Wikidata's app's picture");
            pc5_image_free(&cover);
        }
    }

    /* Sources turned off aren't asked. */
    {
        Pc5CoverHint some = hint;
        some.off = PC5_PICTURES_STEAM | PC5_PICTURES_PCGAMINGWIKI | PC5_PICTURES_WIKIDATA;   /* SteamGridDB: no key */
        net.requests = 0;
        pc5_cover_search_start(&search, &some);
        while (pc5_cover_search_step(&search, &http)) {}
        int others = 0;
        for (int i = 0; i < search.count; i++) others += strcmp(search.choices[i].source, "GOG") != 0;
        CHECK(net.requests == 1 && strstr(net.last, "https://catalog.gog.com/") && !others,
              "only GOG: %d requests, %d others, %s", net.requests, others, net.last);
        some.off = (1u << PC5_PICTURE_SOURCES) - 1;
        some.steam_appid = 901002;
        net.requests = 0;
        pc5_cover_search_start(&search, &some);
        while (pc5_cover_search_step(&search, &http)) {}
        CHECK(net.requests == 0 && search.count == 0 && search.answered, "all off: nothing asked, nothing found, done");
        CHECK(!strcmp(pc5_picture_source_name(1), "SteamGridDB") && !strcmp(pc5_picture_source_key(1), "steamgriddb") &&
              !strcmp(pc5_picture_source_name(3), "PCGamingWiki") && !strcmp(pc5_picture_source_key(4), "wikidata") &&
              !pc5_picture_source_name(5)[0], "the sources' names");
    }

    /* A steam_appid.txt is taken at its word. */
    Pc5CoverHint known = { .folder = "Whatever", .steam_appid = 901002 };
    pc5_cover_search_start(&search, &known);
    while (pc5_cover_search_step(&search, &http)) {}
    CHECK(search.count && search.choices[0].steam_appid == 901002 && search.choices[0].score == 1000, "appid first");

    /* Offline: nothing answers, nothing is chosen. */
    net.offline = 1;
    pc5_cover_search_start(&search, &hint);
    while (pc5_cover_search_step(&search, &http)) {}
    CHECK(search.count == 0 && search.answered == 0, "offline: %d choices %d answered", search.count, search.answered);
}

/* ---- PE files --------------------------------------------------------------------------------- */

static void test_pe(void)
{
    char file[PC5_PATH];
    Pc5PeInfo pe;
    Pc5Image icon;

    {
        /* An Unreal Engine 5 program that loads Direct3D 12 itself. */
        char ue[PC5_PATH], other[PC5_PATH];
        path(ue, sizeof(ue), "%s/ue5/Game/Binaries/Win64/Game-Win64-Shipping.exe", work);
        write_text(ue, "MZ ... D3D12RHI d3d12.dll ...");
        path(other, sizeof(other), "%s/ue5/Game-Win64-Shipping.exe", work);
        write_text(other, "MZ ... D3D12RHI d3d12.dll ...");
        CHECK(pc5_pe_unreal_d3d12(ue, PC5_IMPORT_D3D11) && pc5_pe_unreal_d3d12(ue, PC5_IMPORT_DXGI),
              "an Unreal program naming d3d12.dll draws with Direct3D 12");
        CHECK(!pc5_pe_unreal_d3d12(ue, PC5_IMPORT_D3D12 | PC5_IMPORT_D3D11) && !pc5_pe_unreal_d3d12(ue, PC5_IMPORT_D3D9) &&
              !pc5_pe_unreal_d3d12(other, PC5_IMPORT_D3D11), "not one importing it, a Direct3D 9 one, or one elsewhere");
        CHECK(pc5_pe_unreal_d3d12("Game\\Binaries\\Win64\\missing.exe", PC5_IMPORT_D3D11) == 0, "nor a missing one");
        write_text(ue, "MZ ... d3d11.dll only ...");
        CHECK(!pc5_pe_unreal_d3d12(ue, PC5_IMPORT_D3D11), "nor one that doesn't name it");
        /* One that doesn't name it where PC5 looks (Gears of War: E-Day's) but ships the
         * Direct3D 12 Agility SDK: its D3D12 folder beside it, any case. */
        char sdk[PC5_PATH];
        path(sdk, sizeof(sdk), "%s/ue5/Game/Binaries/Win64/d3d12/D3D12Core.DLL", work);
        write_text(sdk, "MZ");
        CHECK(pc5_pe_unreal_d3d12(ue, PC5_IMPORT_DXGI), "one with the Agility SDK's D3D12 folder");
        CHECK(!pc5_pe_unreal_d3d12(other, PC5_IMPORT_DXGI), "not outside Binaries\\Win64");
        unlink(sdk);
        /* or its export in the program (a real PE file, made by MinGW) */
        char agility[PC5_PATH];
        path(file, sizeof(file), "%s/agility.exe", programs);
        path(agility, sizeof(agility), "%s/ue5/Game/Binaries/Win64/Agility-Win64-Shipping.exe", work);
        copy_file(file, agility);
        CHECK(!pc5_pe_read(agility, &pe) && pe.imports == PC5_IMPORT_DXGI && pc5_pe_unreal_d3d12(agility, pe.imports),
              "one exporting D3D12SDKVersion: %#x", (unsigned)pe.imports);
        path(file, sizeof(file), "%s/game64.exe", programs);
        path(agility, sizeof(agility), "%s/ue5/Game/Binaries/Win64/Plain-Win64-Shipping.exe", work);
        copy_file(file, agility);
        CHECK(!pc5_pe_unreal_d3d12(agility, PC5_IMPORT_D3D11), "and not a Direct3D 11 program without it");
        /* A Godot game: the engine's name in its program, or its data beside it. */
        char godot[PC5_PATH], pck[PC5_PATH];
        path(godot, sizeof(godot), "%s/godot/Mega Game 1.0.exe", work);
        write_text(godot, "MZ ... Godot Engine v4.7.stable ...");
        CHECK(pc5_pe_godot(godot), "a Godot game's program");
        write_text(godot, "MZ ... nothing of the engine ...");
        CHECK(!pc5_pe_godot(godot), "not another program");
        path(pck, sizeof(pck), "%s/godot/Mega Game 1.0.pck", work);
        write_text(pck, "GDPC");
        CHECK(pc5_pe_godot(godot), "a program with its .pck beside it");
        CHECK(!pc5_pe_godot(ue), "nor an Unreal one");
    }
    path(file, sizeof(file), "%s/game64.exe", programs);
    CHECK(!pc5_pe_read(file, &pe), "game64 reads");
    CHECK(pe.bits == 64 && pe.gui && !pe.dll, "game64: bits %d gui %d", pe.bits, pe.gui);
    CHECK(pe.imports & PC5_IMPORT_D3D11, "game64 imports d3d11");
    CHECK(!strcmp(pe.product, "Neon Drift: Complete Edition"), "product: '%s'", pe.product);
    CHECK(!strcmp(pe.description, "Neon Drift game client"), "description: '%s'", pe.description);
    CHECK(!pc5_pe_icon(file, &icon) && icon.w == 256 && icon.h == 256, "icon %dx%d", icon.w, icon.h);
    if (icon.px) {
        uint32_t middle = icon.px[128 * 256 + 128], corner = icon.px[0];
        CHECK((middle >> 24) == 255 && ((middle >> 16) & 255) > 200, "icon middle %08x", middle);
        CHECK((corner >> 24) == 0, "icon corner see-through %08x", corner);
    }
    pc5_image_free(&icon);
    path(file, sizeof(file), "%s/game32.exe", programs);
    CHECK(!pc5_pe_read(file, &pe) && pe.bits == 32 && (pe.imports & PC5_IMPORT_D3D9), "game32 d3d9");
    CHECK(pc5_pe_icon(file, &icon), "game32 has no icon");
    path(file, sizeof(file), "%s/engine.dll", programs);
    CHECK(!pc5_pe_read(file, &pe) && pe.dll && (pe.imports & PC5_IMPORT_D3D12) && (pe.imports & PC5_IMPORT_DXGI), "engine d3d12");
    path(file, sizeof(file), "%s/gl32.exe", programs);
    CHECK(!pc5_pe_read(file, &pe) && pc5_api_from_imports(pe.imports) == PC5_API_OPENGL, "gl32 opengl");
    path(file, sizeof(file), "%s/tool.exe", programs);
    CHECK(!pc5_pe_read(file, &pe) && !pe.gui, "tool is a console program");
    path(file, sizeof(file), "%s/steam-search.json", fixtures);
    CHECK(pc5_pe_read(file, &pe), "a text file is no program");
    CHECK(pc5_api_from_imports(PC5_IMPORT_D3D11 | PC5_IMPORT_D3D12 | PC5_IMPORT_DXGI) == PC5_API_D3D12, "12 over 11");
}

/* ---- a USB drive ------------------------------------------------------------------------------- */

static char usb[PC5_PATH];

/* The drive: two games at the top, one in Games/, a folder with no program and the drive's own folders. */
static void make_usb(void)
{
    char file[PC5_PATH], from[PC5_PATH];

    path(usb, sizeof(usb), "%s/usb0", work);
    path(file, sizeof(file), "%s/NeonDrift/NeonDrift.exe", usb);
    path(from, sizeof(from), "%s/game64.exe", programs);
    copy_file(from, file);
    path(file, sizeof(file), "%s/NeonDrift/unins000.exe", usb);
    copy_file(from, file);
    path(file, sizeof(file), "%s/NeonDrift/_CommonRedist/vcredist/vc_redist.x64.exe", usb);
    copy_file(from, file);
    path(file, sizeof(file), "%s/NeonDrift/data/level1.pak", usb);
    write_text(file, "level one data\n");
    path(file, sizeof(file), "%s/NeonDrift/data/sub/deep/textures.pak", usb);
    {
        static char big[3 * 1024 * 1024 + 123];
        for (size_t i = 0; i < sizeof(big); i++) big[i] = (char)(i * 7 + (i >> 9));
        char dir[PC5_PATH];
        pc5_copy(dir, sizeof(dir), file);
        *strrchr(dir, '/') = 0;
        pc5_mkdirs(dir);
        pc5_write_file(file, big, sizeof(big));
    }
    path(file, sizeof(file), "%s/NeonDrift/launch.txt", usb);
    write_text(file, "# options\n  -windowed -lang en  \n");
    path(file, sizeof(file), "%s/NeonDrift/steam_appid.txt", usb);
    write_text(file, "901001\n");
    /* An engine game: a small starter and the real program beside an engine library. */
    path(file, sizeof(file), "%s/Games/Orbital Nine/Orbital Nine.exe", usb);
    path(from, sizeof(from), "%s/launcher.exe", programs);
    copy_file(from, file);
    path(file, sizeof(file), "%s/Games/Orbital Nine/Binaries/Win64/OrbitalNine-Win64-Shipping.exe", usb);
    copy_file(from, file);
    path(file, sizeof(file), "%s/Games/Orbital Nine/Binaries/Win64/engine.dll", usb);
    path(from, sizeof(from), "%s/engine.dll", programs);
    copy_file(from, file);
    /* An old OpenGL game. */
    path(file, sizeof(file), "%s/Starfall/bin/gl32.exe", usb);
    path(from, sizeof(from), "%s/gl32.exe", programs);
    copy_file(from, file);
    path(file, sizeof(file), "%s/Music/song.mp3", usb);
    write_text(file, "not a game");
    path(file, sizeof(file), "%s/System Volume Information/IndexerVolumeGuid", usb);
    write_text(file, "x");
}

static void test_sources(void)
{
    const char *roots[] = { usb, "/nonexistent/usb1", NULL };
    Pc5Source sources[16];
    Pc5Folder folder;
    char dir[PC5_PATH];
    int n = pc5_source_scan(roots, sources, 16);

    CHECK(n == 3, "three game folders, got %d", n);
    if (n == 3) {
        CHECK(!strcmp(sources[0].name, "NeonDrift") && !strcmp(sources[1].name, "Orbital Nine") &&
              !strcmp(sources[2].name, "Starfall"), "sorted: %s %s %s", sources[0].name, sources[1].name, sources[2].name);
    }
    path(dir, sizeof(dir), "%s/NeonDrift", usb);
    CHECK(!pc5_folder_inspect(dir, "NeonDrift", &folder), "inspect");
    CHECK(!folder.problem[0], "no problem: %s", folder.problem);
    CHECK(folder.exe_count >= 1 && !strcmp(folder.exes[0].path, "NeonDrift.exe"), "the game first: %s",
          folder.exe_count ? folder.exes[0].path : "");
    for (int i = 0; i < folder.exe_count; i++)
        CHECK(!strstr(folder.exes[i].path, "vc_redist") && (i > 0 || !strstr(folder.exes[i].path, "unins")),
              "helpers not offered first: %s", folder.exes[i].path);
    CHECK(folder.files == 7, "files: %u", (unsigned)folder.files);
    CHECK(folder.steam_appid == 901001, "appid %u", (unsigned)folder.steam_appid);
    CHECK(pc5_folder_api(&folder, 0) == PC5_API_D3D11, "d3d11");
    path(dir, sizeof(dir), "%s/Games/Orbital Nine", usb);
    CHECK(!pc5_folder_inspect(dir, "Orbital Nine", &folder), "inspect engine game");
    CHECK(folder.exe_count == 2 && strstr(folder.exes[0].path, "-Shipping"), "the shipping program first: %s",
          folder.exe_count ? folder.exes[0].path : "");
    CHECK(pc5_folder_api(&folder, 0) == PC5_API_D3D12, "the engine library's d3d12");
    path(dir, sizeof(dir), "%s/Music", usb);
    CHECK(!pc5_folder_inspect(dir, "Music", &folder) && folder.problem[0], "no program: %s", folder.problem);
    /* A 64-bit starter beside the 32-bit game it starts (as PlayGTAIV.exe beside GTAIV.exe): the game. */
    {
        char file[PC5_PATH], from[PC5_PATH];
        path(file, sizeof(file), "%s/stubs/Liberty Town/PlayLibertyTown.exe", work);
        path(from, sizeof(from), "%s/game64.exe", programs);
        copy_file(from, file);
        path(file, sizeof(file), "%s/stubs/Liberty Town/LibertyTown.exe", work);
        path(from, sizeof(from), "%s/game32.exe", programs);
        copy_file(from, file);
        path(dir, sizeof(dir), "%s/stubs/Liberty Town", work);
        CHECK(!pc5_folder_inspect(dir, "Liberty Town", &folder) && folder.exe_count == 2 &&
              !strcmp(folder.exes[0].path, "LibertyTown.exe"), "the game, not its starter: %s",
              folder.exe_count ? folder.exes[0].path : "");
    }
    /* A game whose name holds a helper's word (Crashlands), beside a real crash reporter. */
    {
        char file[PC5_PATH], from[PC5_PATH];
        path(from, sizeof(from), "%s/game64.exe", programs);
        path(file, sizeof(file), "%s/stubs/Crashlands 2/Crashlands2.exe", work);
        copy_file(from, file);
        path(dir, sizeof(dir), "%s/stubs/Crashlands 2", work);
        CHECK(!pc5_folder_inspect(dir, "Crashlands 2", &folder) && !folder.problem[0] && folder.exe_count == 1 &&
              !strcmp(folder.exes[0].path, "Crashlands2.exe"), "a game named Crash…: %s %s", folder.problem,
              folder.exe_count ? folder.exes[0].path : "");
        path(file, sizeof(file), "%s/stubs/Crashlands 2/UnityCrashHandler64.exe", work);
        copy_file(from, file);
        CHECK(!pc5_folder_inspect(dir, "Crashlands 2", &folder) && !folder.problem[0] &&
              !strcmp(folder.exes[0].path, "Crashlands2.exe"), "the game before its crash handler: %s",
              folder.exe_count ? folder.exes[0].path : "");
        /* A folder's only program is the game, a helper's word in its name or not; an installer isn't. */
        path(file, sizeof(file), "%s/stubs/Wreck/ServerQuest.exe", work);
        copy_file(from, file);
        path(dir, sizeof(dir), "%s/stubs/Wreck", work);
        CHECK(!pc5_folder_inspect(dir, "Wreck", &folder) && !folder.problem[0] && folder.exe_count == 1,
              "the only program: %s", folder.problem);
        path(file, sizeof(file), "%s/stubs/Installer only/setup.exe", work);
        copy_file(from, file);
        path(dir, sizeof(dir), "%s/stubs/Installer only", work);
        CHECK(!pc5_folder_inspect(dir, "Installer only", &folder) && strstr(folder.problem, "only has installers"),
              "an installer alone isn't a game: %s", folder.problem);
        /* A game's settings tool named like an installer (Brian Lara International Cricket 2007's
         * HardwareSetup.exe): Change program offers it, after the game; the redistributables'
         * installers it doesn't. */
        path(file, sizeof(file), "%s/stubs/Cricket/ICC2007.exe", work);
        copy_file(from, file);
        path(file, sizeof(file), "%s/stubs/Cricket/HardwareSetup.exe", work);
        copy_file(from, file);
        path(file, sizeof(file), "%s/stubs/Cricket/_CommonRedist/DirectX/DXSETUP.exe", work);
        copy_file(from, file);
        path(dir, sizeof(dir), "%s/stubs/Cricket", work);
        CHECK(!pc5_folder_inspect(dir, "Cricket", &folder) && !folder.problem[0] && folder.exe_count == 2 &&
              !strcmp(folder.exes[0].path, "ICC2007.exe") && !strcmp(folder.exes[1].path, "HardwareSetup.exe"),
              "the game, then its settings tool: %d %s %s", folder.exe_count, folder.exe_count ? folder.exes[0].path : "",
              folder.exe_count > 1 ? folder.exes[1].path : "");
        /* A settings tool named like the game and bigger than its small program (Far Cry's
         * Bin32\FarCryConfigurator.exe beside Bin32\FarCry.exe): the game first. */
        path(file, sizeof(file), "%s/stubs/Far Cry/Bin32/FarCry.exe", work);
        copy_file(from, file);
        path(file, sizeof(file), "%s/stubs/Far Cry/Bin32/FarCryConfigurator.exe", work);
        copy_file(from, file);
        {
            FILE *grow = fopen(file, "ab");
            for (int i = 0; grow && i < 3 * 1024; i++) {
                static const char kilobyte[1024];
                fwrite(kilobyte, 1, sizeof(kilobyte), grow);
            }
            if (grow) fclose(grow);
        }
        path(dir, sizeof(dir), "%s/stubs/Far Cry", work);
        CHECK(!pc5_folder_inspect(dir, "Far Cry", &folder) && folder.exe_count == 2 &&
              !strcmp(folder.exes[0].path, "Bin32/FarCry.exe") && !strcmp(folder.exes[1].path, "Bin32/FarCryConfigurator.exe"),
              "the game before its settings tool: %s", folder.exe_count ? folder.exes[0].path : "");
    }
    /* A game that starts from a batch file: read as cmd.exe would, its program found whatever
     * the case of its name, offered first when it is the folder's one way to start. */
    {
        char file[PC5_PATH], from[PC5_PATH];
        Pc5Batch batch;
        int game = -1, windowed = -1, setup = -1, readme = -1;
        path(from, sizeof(from), "%s/game64.exe", programs);
        path(file, sizeof(file), "%s/stubs/Witch Tale/Binaries/Win64/WitchTale.exe", work);
        copy_file(from, file);
        path(file, sizeof(file), "%s/stubs/Witch Tale/Launch.bat", work);
        write_text(file, "\xef\xbb\xbf@echo off\r\n:: starts the game\r\ncd /d \"%~dp0\"\r\nset GAME=Binaries\\Win64\r\n"
                         "if exist \"%GAME%\\WitchTale.exe\" (\r\n"
                         "  start \"\" /d \"%GAME%\" \"%GAME%\\witchtale.exe\" -dx12 -log=\"%LOCALAPPDATA%\\WitchTale\\log.txt\" %* >nul 2>&1\r\n"
                         ")\r\nexit\r\n");
        path(file, sizeof(file), "%s/stubs/Witch Tale/Windowed.bat", work);
        write_text(file, "@cd /d \"%~dp0Binaries\\Win64\" && WitchTale -windowed ^& pause\n");
        path(file, sizeof(file), "%s/stubs/Witch Tale/Safe Mode.bat", work);
        write_text(file, "start /min \"\" \"Binaries\\Win64\\WitchTale.exe\" %~1 %x:~0,3% -a ^\r\n-b\t\"x\ty\"\r\n\x1a"
                         "start other.exe\r\n");
        path(file, sizeof(file), "%s/stubs/Witch Tale/Setup.bat", work);
        write_text(file, "Binaries\\Win64\\WitchTale.exe -install\n");
        path(file, sizeof(file), "%s/stubs/Witch Tale/Readme.bat", work);
        write_text(file, "notepad readme.txt\nstart C:\\windows\\system32\\cmd.exe\nstart ..\\Other\\x.exe\n");
        path(dir, sizeof(dir), "%s/stubs/Witch Tale", work);
        CHECK(!pc5_folder_inspect(dir, "Witch Tale", &folder) && !folder.problem[0], "inspect a batch game: %s", folder.problem);
        for (int i = 0; i < folder.exe_count; i++) {
            if (!strcmp(folder.exes[i].path, "Binaries/Win64/WitchTale.exe")) game = i;
            if (!strcmp(folder.exes[i].path, "Windowed.bat")) windowed = i;
            if (!strcmp(folder.exes[i].path, "Setup.bat")) setup = i;
            if (!strcmp(folder.exes[i].path, "Readme.bat")) readme = i;
        }
        CHECK(folder.exe_count && !strcmp(folder.exes[0].path, "Launch.bat"), "its batch file first: %s",
              folder.exe_count ? folder.exes[0].path : "");
        CHECK(folder.exe_count && folder.exes[0].pe.bits == 64 && pc5_folder_api(&folder, 0) == PC5_API_D3D11,
              "with its program's bits and drawing");
        /* Setup.bat, named as an installer is, last (Change program offers it); Readme.bat starts
         * none of the game's programs. */
        CHECK(game > 0 && windowed > game && setup > windowed && setup == folder.exe_count - 1 && readme < 0,
              "the others after it: %d %d %d %d", game, windowed, setup, readme);
        CHECK(!pc5_batch_read(dir, "Launch.bat", &batch) && !strcmp(batch.exe, "Binaries/Win64/WitchTale.exe") &&
              !strcmp(batch.cwd, "Binaries/Win64") &&
              !strcmp(batch.arguments, "-dx12 -log=\"C:\\users\\prospero\\AppData\\Local\\WitchTale\\log.txt\""),
              "Launch.bat: %s | %s | %s", batch.exe, batch.cwd, batch.arguments);
        CHECK(!pc5_batch_read(dir, "Windowed.bat", &batch) && !strcmp(batch.exe, "Binaries/Win64/WitchTale.exe") &&
              !strcmp(batch.cwd, "Binaries/Win64") && !strcmp(batch.arguments, "-windowed & pause"),
              "Windowed.bat: %s | %s | %s", batch.exe, batch.cwd, batch.arguments);
        CHECK(pc5_batch_read(dir, "Readme.bat", &batch) == -1, "nothing of the game's started: nothing");
        CHECK(!pc5_batch_read(dir, "Safe Mode.bat", &batch) && !strcmp(batch.exe, "Binaries/Win64/WitchTale.exe") &&
              !strcmp(batch.arguments, "%x:~0,3% -a -b \"x y\""),
              "start's title after its switches, ^ carrying a line on, no tabs, Ctrl-Z the end: %s | %s", batch.exe,
              batch.arguments);
    }
}

/* ---- the library: installing and uninstalling -------------------------------------------------- */

static char root[PC5_PATH], template_prefix[PC5_PATH];

static void make_template(void)
{
    char file[PC5_PATH];
    static const char *const files[] = { "drive_c/windows/system32/kernel32.dll", "drive_c/windows/syswow64/d3d9.dll",
                                         "drive_c/users/prospero/Documents/.keep", "dosdevices/.pw-symlinks",
                                         "user.reg", "userdef.reg", "system.reg" };
    Pc5Buf list = { 0 };

    path(template_prefix, sizeof(template_prefix), "%s/app0/pc5/prefix", work);
    pc5_buf_puts(&list, "drive_c/\ndrive_c/Games/\ndrive_c/users/prospero/Saved Games/\n");
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        path(file, sizeof(file), "%s/%s", template_prefix, files[i]);
        write_text(file, files[i]);
        pc5_buf_printf(&list, "%s\n", files[i]);
    }
    path(file, sizeof(file), "%s.list", template_prefix);
    pc5_write_file(file, list.data, list.len);
    pc5_buf_free(&list);
}

/* Kept saves the next install_folder makes the game's Windows from (pc5_install_use_saves), or NULL. */
static const char *install_saves;

static Pc5InstallPhase install_folder(const char *name, int cancel_after_copy_steps, int corrupt, char *id, size_t id_size)
{
    Pc5Folder folder;
    Pc5Install install;
    Pc5Image cover;
    char dir[PC5_PATH], file[PC5_PATH];
    Pc5InstallPhase phase;
    int steps = 0;

    path(dir, sizeof(dir), "%s/%s", usb, name);
    if (pc5_folder_inspect(dir, name, &folder)) { path(dir, sizeof(dir), "%s/Games/%s", usb, name); pc5_folder_inspect(dir, name, &folder); }
    path(file, sizeof(file), "%s/cover-03.png", covers);
    {
        Pc5Buf png = { 0 };
        Pc5Image picture;
        pc5_read_file(file, &png, 1 << 24);
        pc5_image_decode(png.data, png.len, &picture);
        pc5_cover_compose(&picture, &cover);
        pc5_image_free(&picture);
        pc5_buf_free(&png);
    }
    if (pc5_install_begin(&install, root, template_prefix, &folder, 0, &cover, 0xff336699u, "Steam:Test")) {
        fprintf(stderr, "install_begin: %s\n", install.error);
        pc5_install_end(&install);
        return PC5_INSTALL_FAILED;
    }
    /* Step 1 counts the template's files and bytes; steps 2 and 3 the game's. */
    if (install_saves) CHECK(!pc5_install_use_saves(&install, install_saves), "saves used: %s", install.error);
    else CHECK(install.files_total == 7 && install.bytes_total > 0, "step 1's progress: %u files", (unsigned)install.files_total);
    pc5_copy(install.background, sizeof(install.background), "https://images.gog-statics.com/background.jpg");
    install.background_appid = 901001;
    if (id) pc5_copy(id, id_size, install.id);
    do {
        phase = pc5_install_step(&install, 1);
        steps++;
        /* Copying and checking, a step after another: on a fast machine all of it may take only a step or two. */
        if (cancel_after_copy_steps && (phase == PC5_INSTALL_COPY || phase == PC5_INSTALL_VERIFY) && --cancel_after_copy_steps == 0)
            pc5_install_cancel(&install);
        /* The copy of level1.pak spoilt once it is whole and before it is read back. A step is
         * a millisecond's work and the files come in the order the drive lists them, so the
         * first step of checking may already have read it back: it is spoilt while the files
         * are still copied (the other files, the 3 MB one among them, take more than a step). */
        if (corrupt && (phase == PC5_INSTALL_COPY || phase == PC5_INSTALL_VERIFY)) {
            char copy[PC5_PATH];
            path(copy, sizeof(copy), "%s/drive_c/Games/%s/data/level1.pak", install.staging, name);
            if (file_is(copy, "level one data\n")) {
                write_text(copy, "LEVEL ONE DATA\n");
                corrupt = 0;
            }
        }
    } while (phase < PC5_INSTALL_DONE && steps < 100000);
    if (phase != PC5_INSTALL_DONE && install.error[0]) fprintf(stderr, "(install ended: %s)\n", install.error);
    pc5_install_end(&install);
    return phase;
}

static int count_entries(const char *dir, const char *prefix)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    int n = 0;
    if (!d) return -1;
    while ((e = readdir(d))) n += !strncmp(e->d_name, prefix, strlen(prefix));
    closedir(d);
    return n;
}

/* A DXVK game's profile sets Wine's own Direct3D (its DirectDraw, a hardware check) on Vulkan, as
 * there is no OpenGL for it: [debug] env WINE_D3D_CONFIG=renderer=vulkan, and nothing else. */
static int wine_d3d_vulkan(const PwGameProfile *profile)
{
    return profile->debug_env_count == 1 && !strcmp(profile->debug_env[0].name, "WINE_D3D_CONFIG") &&
           !strcmp(profile->debug_env[0].value, "renderer=vulkan");
}

/* PC5's INSTALL folder: a game put there is moved into the library, not copied; one whose
 * install is cut short after the move goes back there. */
static void install_from(const char *dir, const char *root2, Pc5Install *install, int stop_when_taken, int copy)
{
    Pc5Folder folder;
    Pc5Image cover = { 0 };
    CHECK(!pc5_folder_inspect(dir, "NeonDrift", &folder), "the INSTALL folder's game read");
    CHECK(!pc5_install_begin(install, root2, template_prefix, &folder, 0, &cover, 0xff336699u, "icon"), "begun: %s",
          install->error);
    if (copy) install->take = 0;   /* as on another drive */
    for (int i = 0; i < 100000; i++) {
        Pc5InstallPhase phase = pc5_install_step(install, 5);
        if (phase == PC5_INSTALL_DONE || phase == PC5_INSTALL_FAILED || phase == PC5_INSTALL_CANCELLED) break;
        if (stop_when_taken && install->taken) break;
    }
}

static void test_install_folder(void)
{
    char take[PC5_PATH], root2[PC5_PATH], game[PC5_PATH], file[PC5_PATH], from[PC5_PATH];
    Pc5Install install;

    path(take, sizeof(take), "%s/take-app/INSTALL", work);
    path(root2, sizeof(root2), "%s/take-data/prospero-win", work);
    path(file, sizeof(file), "rm -rf '%s/take-app' '%s/take-data' && mkdir -p '%s' '%s/profiles' && cp -r '%s/NeonDrift' '%s/'",
         work, work, take, root2, usb, take);
    run(file);
    path(game, sizeof(game), "%s/NeonDrift", take);
    pc5_install_take_from(take);
    CHECK(pc5_install_take_dir() && !strcmp(pc5_install_take_dir(), take), "the INSTALL folder named");

    install_from(game, root2, &install, 0, 0);
    CHECK(install.phase == PC5_INSTALL_DONE && install.take, "installed by moving it: %s", install.error);
    path(file, sizeof(file), "%s/prefixes/neondrift/drive_c/Games/NeonDrift/data/level1.pak", root2);
    path(from, sizeof(from), "%s/NeonDrift/data/level1.pak", usb);
    CHECK(same_files(file, from), "its files in the library");
    CHECK(!pc5_exists(game), "and no longer in the INSTALL folder");
    path(file, sizeof(file), "%s/prefixes/neondrift/pc5-taken.txt", root2);
    CHECK(!pc5_exists(file), "no note left");
    path(file, sizeof(file), "%s/prefixes/neondrift/pc5/files.txt", root2);
    CHECK(pc5_exists(file), "its files listed, as a copied game's are");
    pc5_install_end(&install);

    /* Cut short after the move (the console turned off): put back at the next start. */
    path(file, sizeof(file), "cp -r '%s/NeonDrift' '%s/'", usb, take);
    run(file);
    install_from(game, root2, &install, 1, 0);
    CHECK(install.taken && !pc5_exists(game), "moved in, then cut short");
    {
        Pc5Remove remove = { 0 };
        int steps = 0;
        while (pc5_recover_step(root2, &remove, 50) && steps++ < 1000) {}
        pc5_remove_end(&remove);
    }
    path(file, sizeof(file), "%s/data/level1.pak", game);
    CHECK(same_files(file, from), "back in the INSTALL folder, whole");
    {
        char prefixes[PC5_PATH];
        path(prefixes, sizeof(prefixes), "%s/prefixes", root2);
        CHECK(count_entries(prefixes, ".installing-") == 0 && count_entries(prefixes, ".taken-") == 0, "nothing left");
    }
    pc5_install_end(&install);

    /* Stopped once moved: the move is the whole copy, so it finishes. */
    install_from(game, root2, &install, 1, 0);
    CHECK(install.taken, "moved in");
    pc5_install_cancel(&install);
    for (int i = 0; i < 10000; i++) {
        Pc5InstallPhase phase = pc5_install_step(&install, 5);
        if (phase == PC5_INSTALL_DONE || phase == PC5_INSTALL_FAILED || phase == PC5_INSTALL_CANCELLED) break;
    }
    CHECK(install.phase == PC5_INSTALL_DONE && !pc5_exists(game), "stopped once moved: installed all the same");
    pc5_install_end(&install);

    /* On another drive: copied, then its copy in the INSTALL folder deleted. */
    path(file, sizeof(file), "cp -r '%s/NeonDrift' '%s/' && rm -rf '%s/prefixes/neondrift' '%s/profiles/neondrift.profile'",
         usb, take, root2, root2);
    run(file);
    install_from(game, root2, &install, 0, 1);
    path(file, sizeof(file), "%s/prefixes/%s/drive_c/Games/NeonDrift/data/level1.pak", root2, install.id);
    path(from, sizeof(from), "%s/NeonDrift/data/level1.pak", usb);
    CHECK(install.phase == PC5_INSTALL_DONE && !install.take && same_files(file, from), "copied from the INSTALL folder: %s",
          install.error);
    CHECK(!pc5_exists(game) && count_entries(take, ".installed-") == 0, "and gone from the INSTALL folder");
    pc5_install_end(&install);
    /* Its copy set aside but not yet deleted (PC5 closed): deleted as PC5 opens. */
    path(file, sizeof(file), "cp -r '%s/NeonDrift' '%s/.installed-NeonDrift'", usb, take);
    run(file);
    {
        Pc5Remove remove = { 0 };
        int steps = 0;
        while (pc5_recover_step(root2, &remove, 50) && steps++ < 1000) {}
        pc5_remove_end(&remove);
    }
    CHECK(count_entries(take, ".installed-") == 0, "a copy set aside, deleted at the next start");

    /* A game elsewhere (a USB drive) is copied, as before. */
    path(from, sizeof(from), "%s/NeonDrift", usb);
    install_from(from, root2, &install, 0, 0);
    CHECK(install.phase == PC5_INSTALL_DONE && !install.take && pc5_is_dir(from), "from a USB drive: copied");
    pc5_install_end(&install);
    pc5_install_take_from(NULL);
    CHECK(!pc5_install_take_dir(), "and none named after");
}

static void test_install(void)
{
    char id[PC5_ID], file[PC5_PATH], other[PC5_PATH], prefixes[PC5_PATH];
    Pc5Library library;
    Pc5Info info;
    PwWineLibrary *pw = pc5_alloc(sizeof(*pw));

    path(root, sizeof(root), "%s/data/prospero-win", work);
    path(prefixes, sizeof(prefixes), "%s/prefixes", root);
    make_template();
    /* An index, as prospero-win's docs describe: PC5 keeps it up to date. */
    path(file, sizeof(file), "%s/profiles/profiles.lst", root);
    write_text(file, "");

    CHECK(install_folder("NeonDrift", 0, 0, id, sizeof(id)) == PC5_INSTALL_DONE, "install");
    CHECK(!strcmp(id, "neondrift"), "id: %s", id);
    /* Every file, byte for byte; the source untouched. */
    static const char *const game_files[] = { "NeonDrift.exe", "unins000.exe", "data/level1.pak",
                                              "data/sub/deep/textures.pak", "_CommonRedist/vcredist/vc_redist.x64.exe",
                                              "launch.txt", "steam_appid.txt" };
    for (size_t i = 0; i < sizeof(game_files) / sizeof(game_files[0]); i++) {
        path(file, sizeof(file), "%s/neondrift/drive_c/Games/NeonDrift/%s", prefixes, game_files[i]);
        path(other, sizeof(other), "%s/NeonDrift/%s", usb, game_files[i]);
        CHECK(same_files(file, other), "copied: %s", game_files[i]);
    }
    path(file, sizeof(file), "%s/neondrift/system.reg", prefixes);
    CHECK(pc5_is_file(file), "its Windows");
    path(file, sizeof(file), "%s/neondrift/drive_c/users/prospero/Saved Games", prefixes);
    CHECK(pc5_is_dir(file), "the template's folders");
    CHECK(count_entries(prefixes, ".installing-") == 0, "no staging left");
    CHECK(!pc5_info_read(root, "neondrift", &info) && info.bits == 64 && info.api == PC5_API_D3D11 &&
          !strcmp(info.exe, "NeonDrift.exe") && info.files == 7, "notes");
    {
        Pc5Image cover;
        uint32_t tint = 0;
        path(file, sizeof(file), "%s/neondrift/pc5/cover.bin", prefixes);
        CHECK(!pc5_cover_load(file, &cover, &tint) && cover.w == PC5_COVER_WIDTH && tint == 0xff336699u, "cover saved");
        CHECK(pc5_cover_current(file), "a cover that fills its frame");
        pc5_image_free(&cover);
        path(file, sizeof(file), "%s/neondrift/pc5/background.txt", prefixes);
        CHECK(file_is(file, "https://images.gog-statics.com/background.jpg\n901001\n"), "the cover's background kept");
        path(file, sizeof(file), "%s/neondrift/pc5/cover-original.jpg", prefixes);
        CHECK(!pc5_exists(file), "only the cover kept, not the picture downloaded");
    }
    /* prospero-win reads the profile PC5 wrote, with the options from launch.txt. */
    pw_wine_library_load(pw, root);
    const PwGameProfile *profile = pw_wine_library_find(pw, "neondrift");
    CHECK(profile != NULL, "prospero-win finds the game (count %u, first status %d)", pw->count, pw->count ? pw->entries[0].status : 0);
    if (profile) {
        CHECK(!strcmp(profile->app.executable, "C:\\Games\\NeonDrift\\NeonDrift.exe"), "exe: %s", profile->app.executable);
        CHECK(!strcmp(profile->app.working_directory, "C:\\Games\\NeonDrift"), "cwd: %s", profile->app.working_directory);
        CHECK(!strcmp(profile->app.arguments, "-windowed -lang en"), "arguments: '%s'", profile->app.arguments);
        CHECK(profile->app.architecture == PW_APP_ARCH_PE64 && profile->app.graphics == PW_APP_GRAPHICS_DXVK, "pe64 dxvk");
        CHECK(!strcmp(profile->app.prefix, "neondrift"), "own prefix");
        /* A Direct3D 11 game: DXVK as prospero-win's profiles have it, Wine's own d3d12. */
        CHECK(!strcmp(profile->app.dll_overrides, "d3d8,d3d9,d3d10core,d3d11,dxgi=n;d3d12,d3d12core=b"), "overrides: %s",
              profile->app.dll_overrides);
        CHECK(!profile->display.show_fps, "no fps counter");
    }
    path(file, sizeof(file), "%s/profiles/profiles.lst", root);
    {
        Pc5Buf text = { 0 };
        pc5_read_file(file, &text, 4096);
        CHECK(text.len && strstr((char *)text.data, "neondrift.profile\n"), "index has the game");
        pc5_buf_free(&text);
    }
    /* A Direct3D 12 game gets VKD3D-Proton. */
    {
        Pc5Info twelve = info;
        static char text[4096];
        twelve.api = PC5_API_D3D12;
        CHECK(!pc5_profile_text("twelve", &twelve, "", NULL, NULL, text, sizeof(text)) &&
              strstr(text, "dll_overrides = d3d8,d3d9,d3d10core,d3d11,d3d12,d3d12core,dxgi=n\n"), "d3d12 overrides");
    }
    /* Changed launch options take effect at the next start. */
    path(file, sizeof(file), "%s/neondrift/drive_c/Games/NeonDrift/launch.txt", prefixes);
    write_text(file, "-fullscreen\n");
    {
        char error[PC5_ERROR];
        CHECK(!pc5_profile_refresh(root, "neondrift", error, sizeof(error)), "refresh");
        pw_wine_library_load(pw, root);
        profile = pw_wine_library_find(pw, "neondrift");
        CHECK(profile && !strcmp(profile->app.arguments, "-fullscreen"), "new options");
    }
    /* The game's own Microsoft DLLs go first (native, then builtin), in name order, the
     * others Wine's; a Resolution of 4K or 1440p is the game's desktop. */
    path(file, sizeof(file), "%s/neondrift/drive_c/Games/NeonDrift/MSVCP140.dll", prefixes);
    write_text(file, "MZ");
    path(file, sizeof(file), "%s/neondrift/drive_c/Games/NeonDrift/d3dcompiler_47.dll", prefixes);
    write_text(file, "MZ");
    path(file, sizeof(file), "%s/neondrift/drive_c/Games/NeonDrift/xinput1_3.dll", prefixes);
    write_text(file, "MZ");
    path(file, sizeof(file), "%s/neondrift/drive_c/Games/NeonDrift/d3dx9.dll", prefixes);
    write_text(file, "MZ");
    {
        char error[PC5_ERROR], own[256];
        Pc5Controls controls;
        path(file, sizeof(file), "%s/neondrift/drive_c/Games/NeonDrift", prefixes);
        CHECK(pc5_own_dlls(file, own, sizeof(own)) == 2 && !strcmp(own, "d3dcompiler_47,msvcp140"), "own DLLs: %s", own);
        pc5_controls_load(root, "neondrift", &controls);
        controls.resolution = 2;
        pc5_controls_save(root, "neondrift", &controls);
        CHECK(!pc5_profile_refresh(root, "neondrift", error, sizeof(error)), "refresh again");
        pw_wine_library_load(pw, root);
        profile = pw_wine_library_find(pw, "neondrift");
        CHECK(profile && !strcmp(profile->app.dll_overrides,
                                 "d3d8,d3d9,d3d10core,d3d11,dxgi=n;d3d12,d3d12core=b;d3dcompiler_47,msvcp140=n,b"),
              "overrides with its own DLLs: %s", profile ? profile->app.dll_overrides : "");
        CHECK(profile && profile->display.width == 3840 && profile->display.height == 2160, "4K desktop");
        controls.resolution = 1;
        pc5_controls_save(root, "neondrift", &controls);
        pc5_profile_refresh(root, "neondrift", error, sizeof(error));
        pw_wine_library_load(pw, root);
        profile = pw_wine_library_find(pw, "neondrift");
        CHECK(profile && profile->display.width == 2560 && profile->display.height == 1440, "1440p desktop");
        controls.resolution = 0;
        pc5_controls_save(root, "neondrift", &controls);
        pc5_profile_refresh(root, "neondrift", error, sizeof(error));
        pw_wine_library_load(pw, root);
        profile = pw_wine_library_find(pw, "neondrift");
        CHECK(profile && profile->display.width == 1920 && profile->display.scaling == PW_GAME_SCALING_FIT,
              "and 1080p again, its own shape");
        controls.stretch = 1;
        pc5_controls_save(root, "neondrift", &controls);
        pc5_profile_refresh(root, "neondrift", error, sizeof(error));
        pw_wine_library_load(pw, root);
        profile = pw_wine_library_find(pw, "neondrift");
        CHECK(profile && profile->display.scaling == PW_GAME_SCALING_STRETCH, "Aspect ratio Stretch: stretched");
        controls.stretch = 0;
        CHECK(profile && !profile->runtime.thread_scheduling, "CPU sharing off: the console's own scheduling");
        controls.cpu_sharing = 1;
        pc5_controls_save(root, "neondrift", &controls);
        pc5_profile_refresh(root, "neondrift", error, sizeof(error));
        pw_wine_library_load(pw, root);
        profile = pw_wine_library_find(pw, "neondrift");
        CHECK(profile && profile->runtime.thread_scheduling, "CPU sharing on: prospero-win's thread scheduling");
        controls.mode = PC5_PAD_KEYBOARD;   /* after the keyboard's [input] lines too */
        pc5_controls_save(root, "neondrift", &controls);
        pc5_profile_refresh(root, "neondrift", error, sizeof(error));
        pw_wine_library_load(pw, root);
        profile = pw_wine_library_find(pw, "neondrift");
        CHECK(profile && profile->runtime.thread_scheduling && profile->input.mode == PW_GAME_INPUT_KEYBOARD,
              "CPU sharing with the keyboard and mouse");
        controls.mode = PC5_PAD_XBOX;
        controls.cpu_sharing = 0;
        pc5_controls_save(root, "neondrift", &controls);
        pc5_profile_refresh(root, "neondrift", error, sizeof(error));
    }
    /* A game drawn without Vulkan stays at 1080p, whatever its Resolution says. */
    {
        Pc5Info flat = info;
        Pc5Controls controls;
        static char text[4096];
        pc5_controls_default(&controls);
        controls.resolution = 2;
        flat.api = PC5_API_DDRAW;
        CHECK(!pc5_profile_text("flat", &flat, "", &controls, NULL, text, sizeof(text)) && strstr(text, "desktop = 1920x1080\n"),
              "DirectDraw at 1080p");
        CHECK(strstr(text, "graphics = opengl\n") != NULL, "DirectDraw through Wine's Direct3D on Zink");
        flat.api = PC5_API_GDI;
        CHECK(!pc5_profile_text("flat", &flat, "", &controls, NULL, text, sizeof(text)) && strstr(text, "graphics = gdi\n"),
              "plain Windows drawing stays with Wine");
    }
    /* Overrides stay within prospero-win's 256 characters: whole names only. */
    {
        static char text[4096], many[400];
        snprintf(many, sizeof(many), "%s", "api-ms-win-crt-conio-l1-1-0,api-ms-win-crt-heap-l1-1-0,api-ms-win-crt-locale-l1-1-0,"
                 "api-ms-win-crt-math-l1-1-0,api-ms-win-crt-runtime-l1-1-0,api-ms-win-crt-stdio-l1-1-0,"
                 "api-ms-win-crt-time-l1-1-0,msvcp140,vcruntime140");
        CHECK(!pc5_profile_text("many", &info, "", NULL, many, text, sizeof(text)), "long list");
        char *line = strstr(text, "dll_overrides = "), *end = line ? strchr(line, '\n') : NULL;
        CHECK(line && end && end - line - 16 <= 256 && !memcmp(end - 4, "=n,b", 4) && *(end - 5) != ',', "cut at a name: %.*s",
              line && end ? (int)(end - line) : 0, line ? line : "");
    }

    /* A launcher's hand-off: Wine's file names the program it started; PC5 writes the
     * profile for it, its arguments and working folder, once, and an ordinary start writes
     * the game's own program again. */
    {
        char error[PC5_ERROR], handoff[PC5_PATH], dir[PC5_PATH];
        CHECK(!pc5_handoff_env(root, "neondrift", handoff, sizeof(handoff), dir, sizeof(dir)) &&
              strstr(handoff, "/prefixes/neondrift/pc5/handoff.txt") && !strcmp(dir, "C:\\Games\\NeonDrift\\"),
              "the hand-off's file and folder: %s %s", handoff, dir);
        CHECK(pc5_handoff_take(root, "neondrift", error, sizeof(error)) == 0, "no hand-off: nothing done");
        path(file, sizeof(file), "%s/neondrift/drive_c/Games/NeonDrift/bin/Real Game.exe", prefixes);
        write_text(file, "MZ");
        write_text(handoff, "C:\\Games\\NeonDrift\\bin\\Real Game.exe\n"
                            "\"C:\\Games\\NeonDrift\\bin\\Real Game.exe\" -token \"a b\" -fast  \nC:\\Games\\NeonDrift\n");
        CHECK(pc5_handoff_take(root, "neondrift", error, sizeof(error)) == 1, "a hand-off taken: %s", error);
        CHECK(!pc5_exists(handoff), "its file taken");
        pw_wine_library_load(pw, root);
        profile = pw_wine_library_find(pw, "neondrift");
        CHECK(profile && !strcmp(profile->app.executable, "C:\\Games\\NeonDrift\\bin\\Real Game.exe") &&
              !strcmp(profile->app.arguments, "-token \"a b\" -fast") &&
              !strcmp(profile->app.working_directory, "C:\\Games\\NeonDrift"),
              "the profile starts it: %s | %s | %s", profile ? profile->app.executable : "",
              profile ? profile->app.arguments : "", profile ? profile->app.working_directory : "");
        /* Outside the game's folder, or not there: refused, the profile as it was. */
        write_text(handoff, "C:\\windows\\system32\\notepad.exe\nnotepad.exe\nC:\\windows\n");
        CHECK(pc5_handoff_take(root, "neondrift", error, sizeof(error)) == -1 && strstr(error, "isn't one of the game's"),
              "outside the folder: %s", error);
        write_text(handoff, "C:\\Games\\NeonDrift\\..\\Other\\x.exe\nx.exe\nC:\\\n");
        CHECK(pc5_handoff_take(root, "neondrift", error, sizeof(error)) == -1, "no climbing out");
        write_text(handoff, "C:\\Games\\NeonDrift\\Missing.exe\nMissing.exe\nC:\\Games\\NeonDrift\n");
        CHECK(pc5_handoff_take(root, "neondrift", error, sizeof(error)) == -1, "a program that isn't there");
        /* A working folder outside the game's: the program's own. A bare program name: no arguments. */
        write_text(handoff, "C:\\Games\\NeonDrift\\bin\\Real Game.exe\nRealGame\nC:\\users\n");
        CHECK(pc5_handoff_take(root, "neondrift", error, sizeof(error)) == 1, "second in a row");
        pw_wine_library_load(pw, root);
        profile = pw_wine_library_find(pw, "neondrift");
        CHECK(profile && !strcmp(profile->app.working_directory, "C:\\Games\\NeonDrift\\bin") && !profile->app.arguments[0],
              "its own folder: %s", profile ? profile->app.working_directory : "");
        /* Three in a row are the most: a launcher and a game that keep starting each other stop. */
        write_text(handoff, "C:\\Games\\NeonDrift\\bin\\Real Game.exe\nx\nC:\\Games\\NeonDrift\n");
        CHECK(pc5_handoff_take(root, "neondrift", error, sizeof(error)) == 1, "third in a row");
        write_text(handoff, "C:\\Games\\NeonDrift\\bin\\Real Game.exe\nx\nC:\\Games\\NeonDrift\n");
        CHECK(pc5_handoff_take(root, "neondrift", error, sizeof(error)) == -1 && strstr(error, "kept starting"),
              "a fourth refused: %s", error);
        /* An ordinary start: the game's own program, and the count starts again. */
        CHECK(!pc5_profile_refresh(root, "neondrift", error, sizeof(error)), "ordinary start");
        pw_wine_library_load(pw, root);
        profile = pw_wine_library_find(pw, "neondrift");
        CHECK(profile && !strstr(profile->app.executable, "Real Game"), "its own program again: %s",
              profile ? profile->app.executable : "");
        write_text(handoff, "C:\\Games\\NeonDrift\\bin\\Real Game.exe\nx\nC:\\Games\\NeonDrift\n");
        CHECK(pc5_handoff_take(root, "neondrift", error, sizeof(error)) == 1, "a new chain");
        CHECK(!pc5_profile_refresh(root, "neondrift", error, sizeof(error)), "and back");
        path(file, sizeof(file), "%s/neondrift/drive_c/Games/NeonDrift/bin/Real Game.exe", prefixes);
        unlink(file);
        /* The program handed to starts with its own bits and drawing, not the launcher's (this
         * game's is 64-bit Direct3D 11): a 32-bit OpenGL one, then a 32-bit Direct3D 9 one. */
        path(file, sizeof(file), "%s/neondrift/drive_c/Games/NeonDrift/bin/Old Game.exe", prefixes);
        path(other, sizeof(other), "%s/gl32.exe", programs);
        copy_file(other, file);
        write_text(handoff, "C:\\Games\\NeonDrift\\bin\\Old Game.exe\nOld Game.exe\nC:\\Games\\NeonDrift\\bin\n");
        CHECK(pc5_handoff_take(root, "neondrift", error, sizeof(error)) == 1, "to a 32-bit OpenGL program: %s", error);
        pw_wine_library_load(pw, root);
        profile = pw_wine_library_find(pw, "neondrift");
        CHECK(profile && profile->app.architecture == PW_APP_ARCH_PE32 && profile->app.graphics == PW_APP_GRAPHICS_ZINK,
              "it starts 32-bit with Zink: arch=%d graphics=%d", profile ? (int)profile->app.architecture : -1,
              profile ? (int)profile->app.graphics : -1);
        path(other, sizeof(other), "%s/game32.exe", programs);
        copy_file(other, file);
        write_text(handoff, "C:\\Games\\NeonDrift\\bin\\Old Game.exe\nOld Game.exe\nC:\\Games\\NeonDrift\\bin\n");
        CHECK(pc5_handoff_take(root, "neondrift", error, sizeof(error)) == 1, "to a 32-bit Direct3D 9 program: %s", error);
        pw_wine_library_load(pw, root);
        profile = pw_wine_library_find(pw, "neondrift");
        CHECK(profile && profile->app.architecture == PW_APP_ARCH_PE32 && profile->app.graphics == PW_APP_GRAPHICS_DXVK,
              "it starts 32-bit with DXVK: arch=%d graphics=%d", profile ? (int)profile->app.architecture : -1,
              profile ? (int)profile->app.graphics : -1);
        CHECK(!pc5_info_read(root, "neondrift", &info) && info.bits == 64 && info.api == PC5_API_D3D11,
              "the game's notes unchanged: %d %d", info.bits, (int)info.api);
        CHECK(!pc5_profile_refresh(root, "neondrift", error, sizeof(error)), "an ordinary start");
        pw_wine_library_load(pw, root);
        profile = pw_wine_library_find(pw, "neondrift");
        CHECK(profile && profile->app.architecture == PW_APP_ARCH_PE64 && profile->app.graphics == PW_APP_GRAPHICS_DXVK,
              "the game's own 64-bit program again");
        unlink(file);
    }
    /* A batch file chosen as the game's program: the profile starts the program it starts,
     * with its options, then launch.txt's, in its folder. */
    {
        char error[PC5_ERROR], game_dir[PC5_PATH], program[PC5_PATH];
        static Pc5Folder copy;
        int batch = -1, own = -1;
        path(file, sizeof(file), "%s/neondrift/drive_c/Games/NeonDrift/Start Game.cmd", prefixes);
        write_text(file, "@echo off\nrem from a repack\npushd \"%~dp0data\"\nset OPTS=-windowed -lang en\n"
                         "\"%~dp0NeonDrift\" %OPTS%\npopd\n");
        path(game_dir, sizeof(game_dir), "%s/neondrift/drive_c/Games/NeonDrift", prefixes);
        CHECK(!pc5_folder_inspect(game_dir, "NeonDrift", &copy), "inspect the copy");
        for (int i = 0; i < copy.exe_count; i++) {
            if (!strcmp(copy.exes[i].path, "Start Game.cmd")) batch = i;
            if (!strcmp(copy.exes[i].path, "NeonDrift.exe")) own = i;
        }
        CHECK(batch == 0 && own > 0, "the batch file first: %d %d", batch, own);
        CHECK(batch >= 0 && !pc5_game_set_program(root, "neondrift", &copy, batch, error, sizeof(error)), "batch: %s", error);
        pw_wine_library_load(pw, root);
        profile = pw_wine_library_find(pw, "neondrift");
        CHECK(profile && !strcmp(profile->app.executable, "C:\\Games\\NeonDrift\\NeonDrift.exe") &&
              !strcmp(profile->app.arguments, "-windowed -lang en -fullscreen") &&
              !strcmp(profile->app.working_directory, "C:\\Games\\NeonDrift\\data"),
              "its program, options and folder: %s | %s | %s", profile ? profile->app.executable : "",
              profile ? profile->app.arguments : "", profile ? profile->app.working_directory : "");
        CHECK(!pc5_game_program(root, "neondrift", program, sizeof(program)) && strstr(program, "/NeonDrift/NeonDrift.exe"),
              "the program it starts: %s", program);
        /* A batch file that no longer starts one of the game's programs: refused, said why. */
        write_text(file, "echo nothing\n");
        CHECK(pc5_profile_refresh(root, "neondrift", error, sizeof(error)) == -1 &&
              strstr(error, "doesn't start any of the game's programs"), "said why: %s", error);
        CHECK(own >= 0 && !pc5_game_set_program(root, "neondrift", &copy, own, error, sizeof(error)), "back: %s", error);
        unlink(file);
    }

    /* More games: an engine game (a second id from the same name isn't needed), an OpenGL one. */
    CHECK(install_folder("Orbital Nine", 0, 0, id, sizeof(id)) == PC5_INSTALL_DONE && !strcmp(id, "orbital-nine"), "engine game %s", id);
    CHECK(install_folder("Starfall", 0, 0, id, sizeof(id)) == PC5_INSTALL_DONE, "opengl game");
    pw_wine_library_load(pw, root);
    profile = pw_wine_library_find(pw, "starfall");
    CHECK(profile && profile->app.graphics == PW_APP_GRAPHICS_ZINK && profile->app.architecture == PW_APP_ARCH_PE32,
          "starfall: opengl pe32");
    profile = pw_wine_library_find(pw, "orbital-nine");
    CHECK(profile && strstr(profile->app.executable, "OrbitalNine-Win64-Shipping.exe"), "orbital: %s",
          profile ? profile->app.executable : "-");
    CHECK(profile && !profile->winedebug[0], "the title's Wine log channels");
    /* The player's channels for one game (winedebug.txt), for a report: in its profile while the file is there. */
    {
        char why[PC5_ERROR], debug[PC5_PATH];
        path(debug, sizeof(debug), "%s/prefixes/orbital-nine/pc5/winedebug.txt", root);
        write_text(debug, "# for issue 84\n\n  err+all,+loaddll,+process,+seh,+mmdevapi  \r\nwarn+all\n");
        CHECK(!pc5_profile_refresh(root, "orbital-nine", why, sizeof(why)), "refreshed: %s", why);
        pw_wine_library_load(pw, root);
        profile = pw_wine_library_find(pw, "orbital-nine");
        CHECK(profile && !strcmp(profile->winedebug, "err+all,+loaddll,+process,+seh,+mmdevapi"), "its channels: %s",
              profile ? profile->winedebug : "-");
        write_text(debug, "+seh; rm -rf /\n");
        CHECK(!pc5_profile_refresh(root, "orbital-nine", why, sizeof(why)), "refreshed: %s", why);
        pw_wine_library_load(pw, root);
        profile = pw_wine_library_find(pw, "orbital-nine");
        CHECK(profile && !profile->winedebug[0], "anything but channels is left out");
        unlink(debug);
        CHECK(!pc5_profile_refresh(root, "orbital-nine", why, sizeof(why)), "refreshed: %s", why);
        pw_wine_library_load(pw, root);
        profile = pw_wine_library_find(pw, "orbital-nine");
        CHECK(profile && !profile->winedebug[0], "gone with the file");
        /* Graphics API, a Direct3D 12 game: Wine's VKD3D (all of Direct3D Wine's own, drawn with Vulkan);
         * WineD3D on Vulkan isn't one of its choices, so it starts as Auto. */
        Pc5Controls k;
        pc5_controls_load(root, "orbital-nine", &k);
        k.graphics = PC5_GRAPHICS_WINE;
        pc5_controls_save(root, "orbital-nine", &k);
        CHECK(!pc5_profile_refresh(root, "orbital-nine", why, sizeof(why)), "refreshed: %s", why);
        pw_wine_library_load(pw, root);
        profile = pw_wine_library_find(pw, "orbital-nine");
        CHECK(profile && profile->app.graphics == PW_APP_GRAPHICS_DXVK &&
              strstr(profile->app.dll_overrides, "d3d8,d3d9,d3d10core,d3d11,dxgi,d3d12,d3d12core=b") &&
              wine_d3d_vulkan(profile), "Wine's VKD3D: Wine's DLLs: %s", profile ? profile->app.dll_overrides : "-");
        k.graphics = PC5_GRAPHICS_WINE_VULKAN;
        pc5_controls_save(root, "orbital-nine", &k);
        CHECK(!pc5_profile_refresh(root, "orbital-nine", why, sizeof(why)), "refreshed: %s", why);
        pw_wine_library_load(pw, root);
        profile = pw_wine_library_find(pw, "orbital-nine");
        CHECK(profile && strstr(profile->app.dll_overrides, "d3d12,d3d12core,dxgi=n") && wine_d3d_vulkan(profile),
              "not a Direct3D 12 choice: Auto (VKD3D-Proton)");
        k.graphics = PC5_GRAPHICS_AUTO;
        pc5_controls_save(root, "orbital-nine", &k);
        CHECK(!pc5_profile_refresh(root, "orbital-nine", why, sizeof(why)), "refreshed: %s", why);
        /* A Direct3D 11 game: WineD3D on OpenGL (Zink), then on Vulkan, its winedebug.txt in the same [debug]. */
        char neon_debug[PC5_PATH];
        path(neon_debug, sizeof(neon_debug), "%s/prefixes/neondrift/pc5/winedebug.txt", root);
        pc5_controls_load(root, "neondrift", &k);
        k.graphics = PC5_GRAPHICS_WINE;
        pc5_controls_save(root, "neondrift", &k);
        CHECK(!pc5_profile_refresh(root, "neondrift", why, sizeof(why)), "refreshed: %s", why);
        pw_wine_library_load(pw, root);
        profile = pw_wine_library_find(pw, "neondrift");
        CHECK(profile && profile->app.graphics == PW_APP_GRAPHICS_ZINK &&
              strstr(profile->app.dll_overrides, "d3d8,d3d9,d3d10core,d3d11,dxgi,d3d12,d3d12core=b") &&
              !profile->debug_env_count, "WineD3D: Wine's DLLs, on OpenGL: %s", profile ? profile->app.dll_overrides : "-");
        k.graphics = PC5_GRAPHICS_WINE_VULKAN;
        pc5_controls_save(root, "neondrift", &k);
        write_text(neon_debug, "+d3d\n");
        CHECK(!pc5_profile_refresh(root, "neondrift", why, sizeof(why)), "refreshed: %s", why);
        pw_wine_library_load(pw, root);
        profile = pw_wine_library_find(pw, "neondrift");
        CHECK(profile && profile->app.graphics == PW_APP_GRAPHICS_DXVK &&
              strstr(profile->app.dll_overrides, "dxgi,d3d12,d3d12core=b") && profile->debug_env_count == 1 &&
              !strcmp(profile->debug_env[0].name, "WINE_D3D_CONFIG") &&
              !strcmp(profile->debug_env[0].value, "renderer=vulkan") && !strcmp(profile->winedebug, "+d3d"),
              "WineD3D on Vulkan, with the game's channels: %s %s", profile ? profile->app.dll_overrides : "-",
              profile ? profile->winedebug : "-");
        unlink(neon_debug);
        k.graphics = PC5_GRAPHICS_AUTO;
        pc5_controls_save(root, "neondrift", &k);
        CHECK(!pc5_profile_refresh(root, "neondrift", why, sizeof(why)), "refreshed: %s", why);
        pw_wine_library_load(pw, root);
        profile = pw_wine_library_find(pw, "neondrift");
        CHECK(profile && profile->app.graphics == PW_APP_GRAPHICS_DXVK &&
              strstr(profile->app.dll_overrides, "dxgi=n") && wine_d3d_vulkan(profile),
              "Auto: DXVK again, Wine's own Direct3D (DirectDraw, a hardware check) on Vulkan");
        /* An OpenGL game has no choice: what is saved for it is left out. */
        pc5_controls_load(root, "starfall", &k);
        k.graphics = PC5_GRAPHICS_WINE_VULKAN;
        pc5_controls_save(root, "starfall", &k);
        CHECK(!pc5_profile_refresh(root, "starfall", why, sizeof(why)), "refreshed: %s", why);
        pw_wine_library_load(pw, root);
        profile = pw_wine_library_find(pw, "starfall");
        /* (a 32-bit game: told at most 2 GB of video memory, Video memory's Auto) */
        CHECK(profile && profile->app.graphics == PW_APP_GRAPHICS_ZINK && profile->debug_env_count == 1 &&
              !strcmp(profile->debug_env[0].name, "WINE_D3D_CONFIG") &&
              !strcmp(profile->debug_env[0].value, "VideoMemorySize=2048"), "OpenGL: as it was, 2 GB of video memory told: %s",
              profile && profile->debug_env_count ? profile->debug_env[0].value : "-");
        /* Video memory: Full: told all of it. */
        k.graphics = PC5_GRAPHICS_AUTO;
        k.video_memory_full = 1;
        pc5_controls_save(root, "starfall", &k);
        CHECK(!pc5_profile_refresh(root, "starfall", why, sizeof(why)), "refreshed: %s", why);
        pw_wine_library_load(pw, root);
        profile = pw_wine_library_find(pw, "starfall");
        CHECK(profile && !profile->debug_env_count, "Video memory: Full: nothing told");
        {
            Pc5Controls back;
            pc5_controls_load(root, "starfall", &back);
            CHECK(back.video_memory_full == 1, "kept in its controls");
        }
        k.video_memory_full = 0;
        pc5_controls_save(root, "starfall", &k);
        CHECK(!pc5_profile_refresh(root, "starfall", why, sizeof(why)), "refreshed: %s", why);
    }
    /* The choices each kind of game has, and their names. */
    {
        Pc5Graphics choices[PC5_GRAPHICS_COUNT];
        CHECK(pc5_graphics_choices(PC5_API_D3D11, choices) == 3 && choices[1] == PC5_GRAPHICS_WINE &&
              choices[2] == PC5_GRAPHICS_WINE_VULKAN, "Direct3D 11: Auto, WineD3D, WineD3D on Vulkan");
        CHECK(pc5_graphics_choices(PC5_API_D3D12, choices) == 2 && choices[1] == PC5_GRAPHICS_WINE &&
              !strcmp(pc5_graphics_name(PC5_GRAPHICS_WINE, PC5_API_D3D12), "Wine's VKD3D"), "Direct3D 12: Auto, Wine's VKD3D");
        CHECK(pc5_graphics_choices(PC5_API_DDRAW, choices) == 2 && choices[1] == PC5_GRAPHICS_WINE_VULKAN,
              "DirectDraw: Auto (WineD3D already), WineD3D on Vulkan");
        CHECK(!pc5_graphics_choices(PC5_API_OPENGL, choices) && !pc5_graphics_choices(PC5_API_GDI, choices) &&
              !pc5_graphics_choices(PC5_API_VULKAN, choices), "OpenGL, Vulkan, plain drawing: none");
        CHECK(!strcmp(pc5_graphics_name(PC5_GRAPHICS_WINE_VULKAN, PC5_API_D3D9), "WineD3D on Vulkan") &&
              !strcmp(pc5_graphics_name(PC5_GRAPHICS_AUTO, PC5_API_D3D9), "Auto"), "the names");
    }
    /* Installing the same folder again makes a second game, never a second copy over the first. */
    CHECK(install_folder("NeonDrift", 0, 0, id, sizeof(id)) == PC5_INSTALL_DONE && !strcmp(id, "neondrift-2"), "second: %s", id);

    /* Cancelled: nothing stays. */
    CHECK(install_folder("NeonDrift", 1, 0, id, sizeof(id)) == PC5_INSTALL_CANCELLED, "cancel");
    path(file, sizeof(file), "%s/profiles/%s.profile", root, id);
    CHECK(!pc5_exists(file), "no profile after cancel");
    CHECK(count_entries(prefixes, ".installing-") == 0, "no staging after cancel");
    /* A copy that doesn't match: refused, nothing stays. */
    CHECK(install_folder("NeonDrift", 0, 1, id, sizeof(id)) == PC5_INSTALL_FAILED, "verify catches a bad copy");
    path(file, sizeof(file), "%s/%s", prefixes, id);
    CHECK(!pc5_exists(file) && count_entries(prefixes, ".installing-") == 0, "nothing kept after a bad copy");

    /* The library as PC5 shows it. */
    CHECK(!pc5_library_load(root, &library), "library");
    CHECK(library.count == 4, "four games: %d", library.count);
    int neon = pc5_library_find(&library, "neondrift");
    CHECK(neon >= 0 && library.games[neon].pc5 && library.games[neon].playable, "neondrift playable");
    CHECK(neon >= 0 && strstr(library.games[neon].detail, "64-bit \xc2\xb7 DirectX 11 \xc2\xb7 "), "detail: %s",
          neon >= 0 ? library.games[neon].detail : "");
    pc5_free(pw);
}

static void test_uninstall(void)
{
    char file[PC5_PATH], prefixes[PC5_PATH], usb_file[PC5_PATH];
    Pc5Library library;
    Pc5Remove remove;

    path(prefixes, sizeof(prefixes), "%s/prefixes", root);
    /* A save, made after installing; logs of this game and another. */
    path(file, sizeof(file), "%s/neondrift/drive_c/users/prospero/Saved Games/NeonDrift/slot1.sav", prefixes);
    write_text(file, "save");
    path(file, sizeof(file), "%s/logs/session-1.log", root);
    write_text(file, "PW_REPORT/1 build=x profile=neondrift cycle=1 pid=1 time=1\nline\n");
    path(file, sizeof(file), "%s/logs/session-2.log", root);
    write_text(file, "PW_REPORT/1 build=x profile=starfall cycle=1 pid=1 time=1\nline\n");
    /* A game PC5 didn't install, sharing nothing. */
    path(file, sizeof(file), "%s/profiles/handmade.profile", root);
    write_text(file, "[application]\nid = handmade\nname = Hand Made\nexecutable = C:\\hm.exe\nworking_directory = C:\\\n"
                     "prefix = default\nruntime = wine-wow64\narchitecture = pe32\ngraphics = gdi\n");
    path(file, sizeof(file), "%s/prefix/system.reg", root);
    write_text(file, "shared");

    pc5_library_load(root, &library);
    int i = pc5_library_find(&library, "neondrift");
    CHECK(i >= 0, "found");
    CHECK(!pc5_remove_begin(&remove, root, &library.games[i], &library), "remove begins");
    /* At once: out of the library, its folder moved aside. */
    path(file, sizeof(file), "%s/profiles/neondrift.profile", root);
    CHECK(!pc5_exists(file), "profile gone at once");
    path(file, sizeof(file), "%s/neondrift", prefixes);
    CHECK(!pc5_exists(file) && count_entries(prefixes, ".removing-neondrift") == 1, "moved aside");
    while (pc5_remove_step(&remove, 1)) {}
    CHECK(remove.done && !remove.failed, "removed (%s)", remove.error);
    pc5_remove_end(&remove);
    CHECK(count_entries(prefixes, ".removing-") == 0, "nothing left");
    path(file, sizeof(file), "%s/logs/session-1.log", root);
    CHECK(!pc5_exists(file), "its log deleted");
    path(file, sizeof(file), "%s/logs/session-2.log", root);
    CHECK(pc5_exists(file), "another game's log kept");
    path(file, sizeof(file), "%s/neondrift-2/system.reg", prefixes);
    CHECK(pc5_exists(file), "the second copy is another game and stays");
    path(usb_file, sizeof(usb_file), "%s/NeonDrift/NeonDrift.exe", usb);
    CHECK(pc5_is_file(usb_file), "USB untouched");
    path(file, sizeof(file), "%s/profiles/profiles.lst", root);
    {
        Pc5Buf text = { 0 };
        pc5_read_file(file, &text, 4096);
        CHECK(!strstr((char *)text.data, "neondrift.profile\n") && strstr((char *)text.data, "neondrift-2.profile\n"),
              "index updated: %s", (char *)text.data);
        pc5_buf_free(&text);
        /* The hand-made game isn't in the index; put it there so prospero-win offers it. */
        write_text(file, "neondrift-2.profile\norbital-nine.profile\nstarfall.profile\nhandmade.profile\n");
    }

    /* The default prefix is shared: uninstalling that game keeps it. */
    pc5_library_load(root, &library);
    i = pc5_library_find(&library, "handmade");
    CHECK(i >= 0 && !library.games[i].pc5, "hand-made game listed");
    if (i >= 0) {
        CHECK(!pc5_remove_begin(&remove, root, &library.games[i], &library) && remove.done, "profile only");
        pc5_remove_end(&remove);
    }
    path(file, sizeof(file), "%s/prefix/system.reg", root);
    CHECK(pc5_exists(file), "shared prefix kept");

    /* A PC5 game whose profile can't be read: uninstalling it deletes its folder too. */
    {
        Pc5Info info = { "Broken Game", "Broken", "Broken.exe", 64, PC5_API_D3D11, 5, 1, "", "" };
        char text[1024];
        path(file, sizeof(file), "%s/broken/system.reg", prefixes);
        write_text(file, "registry");
        path(file, sizeof(file), "%s/broken/drive_c/Games/Broken/data.pak", prefixes);
        write_text(file, "a big file");
        CHECK(!pc5_info_text(&info, text, sizeof(text)), "its info");
        path(file, sizeof(file), "%s/broken/pc5/info.txt", prefixes);
        write_text(file, text);
        path(file, sizeof(file), "%s/profiles/broken.profile", root);
        write_text(file, "[application]\nid = broken\nname = Broken Game\nlaunch_options = -x\n");
        path(file, sizeof(file), "%s/profiles/profiles.lst", root);
        write_text(file, "neondrift-2.profile\norbital-nine.profile\nstarfall.profile\nbroken.profile\n");
        pc5_library_load(root, &library);
        i = pc5_library_find(&library, "broken");
        CHECK(i >= 0 && library.games[i].problem[0] && !library.games[i].playable &&
              !strcmp(library.games[i].prefix, "broken") && !strcmp(library.games[i].title, "Broken Game"),
              "listed with its problem and its folder: %s", i >= 0 ? library.games[i].prefix : "-");
        if (i >= 0) {
            CHECK(!pc5_remove_begin(&remove, root, &library.games[i], &library), "broken one begins");
            while (pc5_remove_step(&remove, 1)) {}
            CHECK(remove.done && !remove.failed, "broken one removed (%s)", remove.error);
            pc5_remove_end(&remove);
        }
        path(file, sizeof(file), "%s/broken", prefixes);
        CHECK(!pc5_exists(file) && count_entries(prefixes, ".removing-") == 0, "its folder deleted");
        path(file, sizeof(file), "%s/profiles/broken.profile", root);
        CHECK(!pc5_exists(file), "its profile deleted");
    }

    /* Cut short: the folder moved aside but not deleted; and an install's staging. */
    pc5_library_load(root, &library);
    i = pc5_library_find(&library, "neondrift-2");
    CHECK(i >= 0 && !pc5_remove_begin(&remove, root, &library.games[i], &library), "second begins");
    pc5_remove_end(&remove);
    path(file, sizeof(file), "%s/.installing-ghost/drive_c/x.dat", prefixes);
    write_text(file, "half");
    /* and a PC5 game whose profile went missing */
    path(file, sizeof(file), "%s/profiles/starfall.profile", root);
    unlink(file);
    pc5_library_load(root, &library);
    CHECK(library.leftovers == 2, "two leftovers: %d", library.leftovers);
    memset(&remove, 0, sizeof(remove));
    int rounds = 0;
    while (pc5_recover_step(root, &remove, 1) && rounds < 100000) rounds++;
    pc5_remove_end(&remove);
    CHECK(count_entries(prefixes, ".removing-") == 0 && count_entries(prefixes, ".installing-") == 0, "leftovers finished");
    CHECK(pc5_exists(file), "lost profile written again");
    pc5_library_load(root, &library);
    CHECK(library.count == 2 && pc5_library_find(&library, "starfall") >= 0 && pc5_library_find(&library, "orbital-nine") >= 0,
          "two games left: %d", library.count);
}

/* A newer copy of a game over its folder: its saves and settings stay. */
static void test_game_update(void)
{
    char kept_root[PC5_PATH], id[PC5_ID], game[PC5_PATH], newer[PC5_PATH], file[PC5_PATH], other[PC5_PATH];
    char prefixes[PC5_PATH];
    Pc5Folder folder;
    Pc5Install install;
    Pc5InstallPhase phase;
    Pc5Info info;
    Pc5Remove remove;
    int steps;

    pc5_copy(kept_root, sizeof(kept_root), root);
    path(root, sizeof(root), "%s/data-update/prospero-win", work);
    path(prefixes, sizeof(prefixes), "%s/prefixes", root);
    CHECK(install_folder("NeonDrift", 0, 0, id, sizeof(id)) == PC5_INSTALL_DONE, "update: installed");
    path(game, sizeof(game), "%s/%s/drive_c/Games/NeonDrift", prefixes, id);
    /* What the game made itself in its folder, and the player's settings there. */
    path(file, sizeof(file), "%s/save/slot1.sav", game);
    write_text(file, "my save");
    path(file, sizeof(file), "%s/settings.ini", game);
    write_text(file, "fov=90");
    /* The newer copy: a level changed in a folder named in another case, one added, a new
     * folder; the uninstaller and the redistributables gone. */
    path(newer, sizeof(newer), "%s/NeonDrift v2", usb);
    path(file, sizeof(file), "%s/NeonDrift/NeonDrift.exe", usb);
    path(other, sizeof(other), "%s/NeonDrift.exe", newer);
    copy_file(file, other);
    path(file, sizeof(file), "%s/DATA/LEVEL1.pak", newer);
    write_text(file, "LEVEL ONE v2\n");
    path(file, sizeof(file), "%s/DATA/level2.pak", newer);
    write_text(file, "LEVEL TWO\n");
    path(file, sizeof(file), "%s/bonus/extra.txt", newer);
    write_text(file, "new");
    CHECK(!pc5_folder_inspect(newer, "NeonDrift v2", &folder), "newer copy read");
    CHECK(!pc5_update_begin(&install, root, id, &folder), "update begins: %s", install.error);
    steps = 0;
    do phase = pc5_install_step(&install, 1);
    while (phase < PC5_INSTALL_DONE && ++steps < 100000);
    CHECK(phase == PC5_INSTALL_DONE, "updated: %d %s", (int)phase, install.error);
    pc5_install_end(&install);
    path(file, sizeof(file), "%s/data/level1.pak", game);
    CHECK(file_is(file, "LEVEL ONE v2\n"), "a file replaced, in the case the game had");
    path(file, sizeof(file), "%s/DATA", game);
    CHECK(!pc5_exists(file), "no second folder in another case");
    path(file, sizeof(file), "%s/data/level2.pak", game);
    CHECK(file_is(file, "LEVEL TWO\n"), "a file added to the game's folder");
    path(file, sizeof(file), "%s/bonus/extra.txt", game);
    CHECK(file_is(file, "new"), "a new folder");
    path(file, sizeof(file), "%s/save/slot1.sav", game);
    CHECK(file_is(file, "my save"), "the game's own save stays");
    path(file, sizeof(file), "%s/settings.ini", game);
    CHECK(file_is(file, "fov=90"), "its settings stay");
    path(file, sizeof(file), "%s/unins000.exe", game);
    CHECK(pc5_is_file(file), "a file the copy doesn't have stays");
    CHECK(count_entries(prefixes, ".updating-") == 0, "no copy left");
    CHECK(!pc5_info_read(root, id, &info) && info.files == folder.files && info.bytes == folder.bytes &&
          !strcmp(info.folder, "NeonDrift") && !strcmp(info.exe, "NeonDrift.exe"), "notes updated: %u files", (unsigned)info.files);
    path(file, sizeof(file), "%s/profiles/%s.profile", root, id);
    CHECK(pc5_is_file(file), "its profile");
    {
        Pc5Buf text = { 0 };
        path(file, sizeof(file), "%s/%s/pc5/files.txt", prefixes, id);
        pc5_read_file(file, &text, 1 << 20);
        pc5_buf_terminate(&text);
        CHECK(text.data && strstr((char *)text.data, "bonus/extra.txt\n") && strstr((char *)text.data, "unins000.exe\n") &&
              strstr((char *)text.data, "data/level1.pak\n") && !strstr((char *)text.data, "DATA/LEVEL1.pak") &&
              !strstr((char *)text.data, "save/slot1.sav"), "the game's files: the old and the copy's, once each");
        pc5_buf_free(&text);
    }
    /* An unfinished update goes with the game; its id isn't given out while it is there. */
    {
        char ghost[PC5_ID];
        path(file, sizeof(file), "%s/.updating-ghost/files/x.dat", prefixes);
        write_text(file, "x");
        CHECK(!pc5_new_id(root, "Ghost", ghost, sizeof(ghost)) && !strcmp(ghost, "ghost-2"), "id: %s", ghost);
        path(file, sizeof(file), "%s/.updating-ghost", prefixes);
        char command[2 * PC5_PATH];
        snprintf(command, sizeof(command), "rm -rf '%s'", file);
        run(command);
    }

    /* A folder without the game's program: refused, nothing made. */
    {
        Pc5Folder wrong;
        path(file, sizeof(file), "%s/NeonDrift/NeonDrift.exe", usb);
        path(other, sizeof(other), "%s/Not It/Other.exe", usb);
        copy_file(file, other);
        path(other, sizeof(other), "%s/Not It", usb);
        CHECK(!pc5_folder_inspect(other, "Not It", &wrong), "other folder read");
        CHECK(pc5_update_begin(&install, root, id, &wrong) && strstr(install.error, "NeonDrift.exe"),
              "refused: %s", install.error);
        pc5_install_end(&install);
        CHECK(count_entries(prefixes, ".updating-") == 0, "nothing made");
        char command[PC5_PATH + 32];
        snprintf(command, sizeof(command), "rm -rf '%s/Not It'", usb);
        run(command);
    }

    /* Stopped while copying: the game as it was. */
    path(file, sizeof(file), "%s/DATA/LEVEL1.pak", newer);
    write_text(file, "LEVEL ONE v3\n");
    CHECK(!pc5_update_begin(&install, root, id, &folder), "second update begins");
    steps = 0;
    do {
        phase = pc5_install_step(&install, 1);
        if (phase == PC5_INSTALL_COPY) pc5_install_cancel(&install);
    } while (phase < PC5_INSTALL_DONE && ++steps < 100000);
    CHECK(phase == PC5_INSTALL_CANCELLED, "stopped: %d", (int)phase);
    pc5_install_end(&install);
    path(file, sizeof(file), "%s/data/level1.pak", game);
    CHECK(file_is(file, "LEVEL ONE v2\n") && count_entries(prefixes, ".updating-") == 0, "unchanged, nothing left");

    /* Cut short: a copy that wasn't whole is deleted at the next start; a whole one (its notes
     * written) is put in place then. */
    path(file, sizeof(file), "%s/.updating-%s/files/data/level1.pak", prefixes, id);
    write_text(file, "HALF");
    memset(&remove, 0, sizeof(remove));
    steps = 0;
    while (pc5_recover_step(root, &remove, 1) && ++steps < 100000) {}
    pc5_remove_end(&remove);
    path(file, sizeof(file), "%s/data/level1.pak", game);
    CHECK(file_is(file, "LEVEL ONE v2\n") && count_entries(prefixes, ".updating-") == 0, "a half copy deleted");
    {
        char text[2048];
        Pc5Info whole = info;
        whole.files = 99;
        path(file, sizeof(file), "%s/.updating-%s/files/Data/Level1.pak", prefixes, id);
        write_text(file, "LEVEL ONE v3\n");
        path(file, sizeof(file), "%s/.updating-%s/files/data/sub", prefixes, id);
        pc5_mkdirs(file);
        pc5_info_text(&whole, text, sizeof(text));
        path(file, sizeof(file), "%s/.updating-%s/info.txt", prefixes, id);
        write_text(file, text);
    }
    Pc5Library library;
    pc5_library_load(root, &library);
    CHECK(library.leftovers == 1, "a leftover: %d", library.leftovers);
    memset(&remove, 0, sizeof(remove));
    steps = 0;
    while (pc5_recover_step(root, &remove, 1) && ++steps < 100000) {}
    pc5_remove_end(&remove);
    path(file, sizeof(file), "%s/data/level1.pak", game);
    CHECK(file_is(file, "LEVEL ONE v3\n") && count_entries(prefixes, ".updating-") == 0, "a whole copy put in place");
    CHECK(!pc5_info_read(root, id, &info) && info.files == 99, "its notes too: %u", (unsigned)info.files);
    pc5_copy(root, sizeof(root), kept_root);
}

/* A game played from its USB drive: only its Windows on the console, its folder a link to the
 * drive's, found on whichever drive it is in; the drive never written to by PC5. */
static void test_usb_play(void)
{
    char kept_root[PC5_PATH], prefixes[PC5_PATH], file[PC5_PATH], dir[PC5_PATH], other[PC5_PATH], error[PC5_ERROR];
    char game_usb[PC5_PATH], second[PC5_PATH];
    Pc5Folder folder;
    Pc5Install install;
    Pc5Image cover = { 0 };
    Pc5Info info;
    Pc5Library library;
    Pc5Remove remove;
    int steps = 0;

    pc5_copy(kept_root, sizeof(kept_root), root);
    path(root, sizeof(root), "%s/data-usb/prospero-win", work);
    path(prefixes, sizeof(prefixes), "%s/prefixes", root);
    path(second, sizeof(second), "%s/usb-second", work);
    pc5_mkdirs(second);
    const char *roots[] = { usb, second, NULL };
    pc5_usb_roots(roots);

    path(dir, sizeof(dir), "%s/NeonDrift", usb);
    CHECK(!pc5_folder_inspect(dir, "NeonDrift", &folder), "the game's folder on the drive");
    CHECK(!pc5_usb_relative(dir, file, sizeof(file)) && !strcmp(file, "NeonDrift"), "its place on the drive: %s", file);
    CHECK(pc5_usb_relative(work, file, sizeof(file)), "a folder elsewhere isn't on a drive");
    CHECK(pc5_install_needs_usb(&folder) < pc5_install_needs(&folder) - folder.bytes + 1, "it needs room for its Windows only");
    CHECK(!pc5_install_begin_usb(&install, root, template_prefix, &folder, 0, &cover, 0xff336699u, "icon"),
          "playing from the drive begins: %s", install.error);
    CHECK(pc5_install_use_saves(&install, dir) && strstr(install.error, "USB"), "no kept saves for it: %s", install.error);
    install.error[0] = 0;
    Pc5InstallPhase phase;
    int copied = 0;
    do {
        phase = pc5_install_step(&install, 1);
        copied |= phase == PC5_INSTALL_COPY || phase == PC5_INSTALL_VERIFY;
    } while (phase < PC5_INSTALL_DONE && ++steps < 100000);
    CHECK(phase == PC5_INSTALL_DONE && !copied, "done, nothing copied: %d %s", (int)phase, install.error);
    char id[PC5_ID];
    pc5_copy(id, sizeof(id), install.id);
    pc5_install_end(&install);

    CHECK(!pc5_info_read(root, id, &info) && !strcmp(info.usb, "NeonDrift"), "its notes say where: %s", info.usb);
    path(file, sizeof(file), "%s/%s/drive_c/Games/NeonDrift", prefixes, id);
    CHECK(!pc5_exists(file), "no copy of the game on the console");
    path(file, sizeof(file), "%s/%s/drive_c/Games/.pw-symlinks", prefixes, id);
    path(game_usb, sizeof(game_usb), "%s/NeonDrift", usb);
    {
        char line[PC5_PATH + 32];
        snprintf(line, sizeof(line), "NeonDrift\t%s\n", game_usb);
        CHECK(file_is(file, line), "its folder linked to the drive's");
    }
    CHECK(!pc5_game_folder(root, id, dir, sizeof(dir)) && !strcmp(dir, game_usb), "PC5 reads it on the drive: %s", dir);
    path(file, sizeof(file), "%s/profiles/%s.profile", root, id);
    CHECK(file_holds_text(file, "executable = C:\\Games\\NeonDrift\\"), "its profile starts it in C:\\Games");
    pc5_library_load(root, &library);
    int i = pc5_library_find(&library, id);
    CHECK(i >= 0 && library.games[i].usb && !strncmp(library.games[i].detail, "USB \xc2\xb7 ", 5),
          "the library says USB: %s", i >= 0 ? library.games[i].detail : "-");

    /* The drive in another port: found there, the link follows; another link of Wine's kept. */
    path(file, sizeof(file), "%s/%s/drive_c/Games/.pw-symlinks", prefixes, id);
    {
        Pc5Buf old = { 0 };
        pc5_read_file(file, &old, 4096);
        pc5_buf_puts(&old, "Saved\t/dev/null\n");
        pc5_write_file(file, old.data, old.len);
        pc5_buf_free(&old);
    }
    path(other, sizeof(other), "%s/NeonDrift", second);
    CHECK(!rename(game_usb, other), "the drive moved");
    CHECK(!pc5_profile_refresh(root, id, error, sizeof(error)), "starts from the other port: %s", error);
    {
        char line[PC5_PATH + 64];
        snprintf(line, sizeof(line), "Saved\t/dev/null\nNeonDrift\t%s\n", other);
        CHECK(file_is(file, line), "linked to it there, Wine's own link kept");
    }
    /* Unplugged: a sentence to plug it in, not a start. */
    path(dir, sizeof(dir), "%s/unplugged", work);
    CHECK(!rename(other, dir), "unplugged");
    CHECK(pc5_profile_refresh(root, id, error, sizeof(error)) && strstr(error, "Plug in the USB drive with NeonDrift"),
          "plug it in: %s", error);
    CHECK(!rename(dir, game_usb), "plugged back");
    CHECK(!pc5_profile_refresh(root, id, error, sizeof(error)), "starts again: %s", error);

    /* Updating it is copying over its folder on the drive. */
    path(dir, sizeof(dir), "%s/NeonDrift", usb);
    pc5_folder_inspect(dir, "NeonDrift", &folder);
    CHECK(pc5_update_begin(&install, root, id, &folder) && strstr(install.error, "plays from its USB drive"),
          "no update from PC5: %s", install.error);
    pc5_install_end(&install);

    /* Uninstalled with its saves kept, then for good: the drive's folder as it was. */
    path(file, sizeof(file), "%s/%s/drive_c/users/prospero/Saved Games/NeonDrift/slot1.sav", prefixes, id);
    write_text(file, "my save");
    pc5_library_load(root, &library);
    i = pc5_library_find(&library, id);
    CHECK(i >= 0 && !pc5_remove_keep_saves(&remove, root, &library.games[i], &library), "kept: %s", remove.error);
    while (pc5_remove_step(&remove, 1)) {}
    pc5_remove_end(&remove);
    path(file, sizeof(file), "%s/saves/%s/drive_c/users/prospero/Saved Games/NeonDrift/slot1.sav", root, id);
    CHECK(file_is(file, "my save"), "its save kept");
    path(file, sizeof(file), "%s/NeonDrift/NeonDrift.exe", usb);
    CHECK(pc5_is_file(file), "its program still on the drive");
    path(file, sizeof(file), "%s/NeonDrift/data/level1.pak", usb);
    CHECK(file_is(file, "level one data\n"), "its data still on the drive, unchanged");

    pc5_usb_roots(NULL);
    pc5_copy(root, sizeof(root), kept_root);
}

/* Uninstalling with the saves kept, and installing the game again with them. */
static void test_keep_saves(void)
{
    char kept_root[PC5_PATH], id[PC5_ID], again[PC5_ID], prefixes[PC5_PATH], file[PC5_PATH], kept[PC5_PATH];
    Pc5Library library;
    Pc5Remove remove;
    Pc5Saves saves[4];
    int steps, i;

    pc5_copy(kept_root, sizeof(kept_root), root);
    path(root, sizeof(root), "%s/data-saves/prospero-win", work);
    path(prefixes, sizeof(prefixes), "%s/prefixes", root);
    CHECK(install_folder("NeonDrift", 0, 0, id, sizeof(id)) == PC5_INSTALL_DONE, "saves: installed");
    path(file, sizeof(file), "%s/%s/drive_c/users/prospero/Saved Games/NeonDrift/slot1.sav", prefixes, id);
    write_text(file, "my save");
    path(file, sizeof(file), "%s/%s/user.reg", prefixes, id);
    write_text(file, "the game's settings");
    path(file, sizeof(file), "%s/%s/pc5/controls.txt", prefixes, id);
    write_text(file, "mode = keyboard\n");
    /* Its pictures (the install saved its cover and background.txt), and the player's own cover elsewhere. */
    path(file, sizeof(file), "%s/%s/pc5/background.bin", prefixes, id);
    write_text(file, "background");
    path(file, sizeof(file), "%s/%s/pc5/background-tries.txt", prefixes, id);
    write_text(file, "1\n");
    path(file, sizeof(file), "%s/covers/NeonDrift.png", root);
    write_text(file, "the player's picture");
    path(file, sizeof(file), "%s/%s/pc5/cover.bin", prefixes, id);
    CHECK(pc5_is_file(file), "a cover to delete");
    path(file, sizeof(file), "%s/%s/pc5/background.txt", prefixes, id);
    CHECK(pc5_is_file(file), "and where its background is");
    /* A save the game keeps beside its program, and an unfinished update of it. */
    path(file, sizeof(file), "%s/%s/drive_c/Games/NeonDrift/save/slot2.sav", prefixes, id);
    write_text(file, "in its folder");
    path(file, sizeof(file), "%s/.updating-%s/files/x.dat", prefixes, id);
    write_text(file, "x");
    pc5_library_load(root, &library);
    i = pc5_library_find(&library, id);
    CHECK(i >= 0 && pc5_remove_can_keep(&library.games[i], &library), "its saves can be kept");
    CHECK(i >= 0 && !pc5_remove_keep_saves(&remove, root, &library.games[i], &library), "keeping begins: %s", remove.error);
    path(file, sizeof(file), "%s/profiles/%s.profile", root, id);
    CHECK(!pc5_exists(file), "out of the library at once");
    path(file, sizeof(file), "%s/%s", prefixes, id);
    CHECK(!pc5_exists(file), "its folder moved");
    path(file, sizeof(file), "%s/saves/%s/drive_c/users/prospero/Saved Games/NeonDrift/slot1.sav", root, id);
    CHECK(file_is(file, "my save"), "the save kept");
    path(file, sizeof(file), "%s/saves/%s/drive_c/Games", root, id);
    CHECK(!pc5_exists(file), "without the game");
    path(file, sizeof(file), "%s/saves/%s/pc5-game/save/slot2.sav", root, id);
    CHECK(file_is(file, "in its folder"), "the save in the game's folder kept");
    path(file, sizeof(file), "%s/saves/%s/pc5-game/NeonDrift.exe", root, id);
    CHECK(!pc5_exists(file), "the game's own files not");
    {
        static const char *const pictures[] = { "cover.bin", "background.bin", "background.txt", "background-tries.txt" };
        int left = 0;
        for (int p = 0; p < 4; p++) {
            path(file, sizeof(file), "%s/saves/%s/pc5/%s", root, id, pictures[p]);
            left += pc5_exists(file);
        }
        path(file, sizeof(file), "%s/saves/%s/pc5/controls.txt", root, id);
        CHECK(!left && pc5_is_file(file), "its pictures deleted with the game, its options kept: %d left", left);
        path(file, sizeof(file), "%s/covers/NeonDrift.png", root);
        CHECK(file_is(file, "the player's picture"), "the player's own picture stays");
    }
    CHECK(count_entries(prefixes, ".updating-") == 0 && count_entries(prefixes, ".removing-update-") == 1,
          "the unfinished update goes with the game");
    while (pc5_remove_step(&remove, 1)) {}
    CHECK(remove.done && !remove.failed, "the game deleted: %s", remove.error);
    pc5_remove_end(&remove);
    memset(&remove, 0, sizeof(remove));
    steps = 0;
    while (pc5_recover_step(root, &remove, 1) && ++steps < 100000) {}
    pc5_remove_end(&remove);
    CHECK(count_entries(prefixes, ".removing-") == 0, "nothing left");
    CHECK(pc5_saves_list(root, saves, 4) == 1 && !strcmp(saves[0].title, "NeonDrift") && !strcmp(saves[0].name, id),
          "listed: %s", saves[0].title);
    pc5_library_load(root, &library);
    CHECK(pc5_library_find(&library, id) < 0, "not a game any more");

    /* Installed again: found by the folder's name in any case, and the Windows made from them. */
    CHECK(pc5_saves_find(root, "neondrift", "neondrift.EXE", kept, sizeof(kept)) == 1 && strstr(kept, "/saves/"),
          "found: %s", kept);
    CHECK(pc5_saves_find(root, "Starfall", NULL, file, sizeof(file)) == 0, "not another game's");
    CHECK(pc5_saves_find(root, "NeonDrift", "Other.exe", file, sizeof(file)) == 0, "not another program's");
    install_saves = kept;
    CHECK(install_folder("NeonDrift", 0, 0, again, sizeof(again)) == PC5_INSTALL_DONE, "installed with the saves");
    install_saves = NULL;
    path(file, sizeof(file), "%s/%s/drive_c/users/prospero/Saved Games/NeonDrift/slot1.sav", prefixes, again);
    CHECK(file_is(file, "my save"), "the save is back");
    path(file, sizeof(file), "%s/%s/user.reg", prefixes, again);
    CHECK(file_is(file, "the game's settings"), "the registry is back");
    path(file, sizeof(file), "%s/%s/pc5/controls.txt", prefixes, again);
    CHECK(file_is(file, "mode = keyboard\n"), "the options are back");
    path(file, sizeof(file), "%s/%s/drive_c/Games/NeonDrift/NeonDrift.exe", prefixes, again);
    CHECK(pc5_is_file(file), "with the game");
    path(file, sizeof(file), "%s/%s/drive_c/Games/NeonDrift/save/slot2.sav", prefixes, again);
    CHECK(file_is(file, "in its folder"), "the save in its folder is back");
    path(file, sizeof(file), "%s/%s/pc5-game", prefixes, again);
    CHECK(!pc5_exists(file), "not left in its Windows");
    CHECK(pc5_saves_list(root, saves, 4) == 0 && count_entries(prefixes, ".removing-") == 0, "the kept saves are used up");

    /* Keeping cut short after the move: finished at the next start. */
    path(file, sizeof(file), "%s/%s", prefixes, again);
    path(kept, sizeof(kept), "%s/saves/.keeping-%s", root, again);
    CHECK(!rename(file, kept), "moved as the uninstall does");
    pc5_library_load(root, &library);
    CHECK(library.leftovers == 1, "a leftover: %d", library.leftovers);
    memset(&remove, 0, sizeof(remove));
    steps = 0;
    while (pc5_recover_step(root, &remove, 1) && ++steps < 100000) {}
    pc5_remove_end(&remove);
    path(file, sizeof(file), "%s/profiles/%s.profile", root, again);
    CHECK(!pc5_exists(file) && count_entries(prefixes, ".removing-") == 0, "finished");
    path(file, sizeof(file), "%s/saves", root);
    CHECK(count_entries(file, ".keeping-") == 0 && pc5_saves_list(root, saves, 4) == 1, "kept");
    path(file, sizeof(file), "%s/saves/%s/drive_c/Games", root, saves[0].name);
    CHECK(!pc5_exists(file), "without the game");

    /* Two sets of the same game's saves: the newest is the one put back, and listed first. */
    {
        char command[3 * PC5_PATH], newest[PC5_NAME];
        pc5_copy(newest, sizeof(newest), saves[0].name);
        snprintf(command, sizeof(command), "cp -a '%s/saves/%s' '%s/saves/older'", root, newest, root);
        run(command);
        path(file, sizeof(file), "%s/saves/older/pc5/kept.txt", root);
        write_text(file, "1000\n");
        CHECK(pc5_saves_find(root, "NeonDrift", "NeonDrift.exe", kept, sizeof(kept)) == 2 &&
              !strcmp(strrchr(kept, '/') + 1, newest), "the newest of two: %s", kept);
        CHECK(pc5_saves_list(root, saves, 4) == 2 && !strcmp(saves[0].name, newest) && !strcmp(saves[1].name, "older") &&
              saves[1].kept == 1000, "listed newest first");
    }

    /* Kept saves deleted. */
    for (int k = 0; k < 2; k++) {
        CHECK(!pc5_saves_delete_begin(&remove, root, saves[k].name), "deleting begins: %s", remove.error);
        while (pc5_remove_step(&remove, 1)) {}
        CHECK(remove.done && !remove.failed, "deleted");
        pc5_remove_end(&remove);
    }
    CHECK(pc5_saves_list(root, saves, 4) == 0 && count_entries(prefixes, ".removing-") == 0, "none left");
    pc5_copy(root, sizeof(root), kept_root);
}

/* ---- the screens, driven like a pad ------------------------------------------------------------ */

typedef struct FakeConsole {
    char played[PC5_ID];
    int plays;
    char told[256];
    int notifications;
    int restarts;
    char sound[64];
    int sounds, sound_volume;
    /* the on-screen keyboard: opened how often, its title and starting text, and what is typed
     * (cancelled when typed is NULL) */
    int keyboards;
    char keyboard_title[96], keyboard_start[PC5_NAME];
    const char *typed;
} FakeConsole;

static int fake_keyboard_open_text(void *context, const char *title, const char *text)
{
    FakeConsole *console = context;
    console->keyboards++;
    pc5_copy(console->keyboard_title, sizeof(console->keyboard_title), title);
    pc5_copy(console->keyboard_start, sizeof(console->keyboard_start), text);
    return 0;
}

static int fake_keyboard_state_text(void *context) { (void)context; return 2; }

/* The screens' log: how often it named SteamGridDB's key as refused, and how often it showed
 * the key itself (never). */
#define TEST_WRONG_KEY "not-the-key-but-secret"
static int key_refused_lines, key_shown_lines;
static int files_cut_lines;

static void fake_log(void *context, const char *line)
{
    (void)context;
    files_cut_lines += !strncmp(line, "PC5 files cut ", 14);
    key_refused_lines += strstr(line, "SteamGridDB refused the key") != NULL;
    key_shown_lines += strstr(line, TEST_WRONG_KEY) != NULL || strstr(line, TEST_GRIDDB_KEY) != NULL;
}

static int fake_keyboard_close_text(void *context, char *out, size_t size)
{
    FakeConsole *console = context;
    pc5_copy(out, size, console->typed ? console->typed : "");
    return console->typed != NULL;
}

static void fake_sound(void *context, const char *name, int volume)
{
    FakeConsole *console = context;
    pc5_copy(console->sound, sizeof(console->sound), name);
    console->sound_volume = volume;
    console->sounds++;
}

static void fake_restart(void *context)
{
    FakeConsole *console = context;
    console->restarts++;
}

static void fake_notify(void *context, const char *text)
{
    FakeConsole *console = context;
    pc5_copy(console->told, sizeof(console->told), text);
    console->notifications++;
}

static int64_t fake_time(void *context, int *offset_s)
{
    (void)context;
    if (offset_s) *offset_s = 3600;
    return 1792700000;
}

static void fake_clock(void *context, char *out, size_t size) { (void)context; pc5_copy(out, size, "14:32"); }
static void fake_play(void *context, const char *id)
{
    FakeConsole *console = context;
    pc5_copy(console->played, sizeof(console->played), id);
    console->plays++;
}

static uint64_t clock_ms = 1000;

static void frames(Pc5Ui *ui, uint32_t held, uint32_t pressed, int count, Pc5Canvas *canvas)
{
    for (int i = 0; i < count; i++) {
        Pc5Input input = { held | pressed, i == 0 ? pressed : 0 };
        clock_ms += 16;
        if (pc5_ui_update(ui, &input, clock_ms)) pc5_ui_draw(ui, canvas);
    }
}

static void press(Pc5Ui *ui, uint32_t key, Pc5Canvas *canvas)
{
    frames(ui, 0, key, 1, canvas);
    frames(ui, 0, 0, 3, canvas);
}

static void until_screen(Pc5Ui *ui, const char *name, Pc5Canvas *canvas)
{
    for (int i = 0; i < 20000 && strcmp(pc5_ui_screen(ui), name); i++) frames(ui, 0, 0, 1, canvas);
}

/* How many texts were cut short (with "…") pressing key (0: none) and drawing again. */
static int cuts_after(Pc5Ui *ui, uint32_t key, Pc5Canvas *canvas)
{
    pc5_text_cuts = 0;
    if (key) press(ui, key, canvas);
    pc5_ui_draw(ui, canvas);
    return pc5_text_cuts;
}

/* Every row of a drawer page, from the top, chosen in turn (each with its description) and,
 * sideways, with its value changed both ways: no text cut short. It ends on the last row. */
static void rows_uncut(Pc5Ui *ui, Pc5Canvas *canvas, const char *where, int sideways)
{
    for (int i = 0; i < 12; i++) press(ui, PC5_KEY_UP, canvas);
    for (int i = 0; i < 12; i++) {
        int cut = cuts_after(ui, 0, canvas);
        if (sideways) cut += cuts_after(ui, PC5_KEY_RIGHT, canvas) + cuts_after(ui, PC5_KEY_LEFT, canvas);
        CHECK(!cut, "%s, row %d: no text cut short (%d)", where, i, cut);
        press(ui, PC5_KEY_DOWN, canvas);
    }
}

/* ---- a game's controller ----------------------------------------------------------------------------- */

static void test_controls(void)
{
    char dir[PC5_PATH], file[PC5_PATH], text[2048];
    Pc5Controls k, back;
    PwGameInput input;

    path(dir, sizeof(dir), "%s/controls-root", work);
    CHECK(pc5_controls_load(dir, "game", &k) == -1 && k.mode == PC5_PAD_XBOX && k.stick[0] == PC5_STICK_KEYS &&
              k.stick[1] == PC5_STICK_POINTER && k.speed == 4 && pc5_controls_pixels(&k) == 1200,
          "the default: the Xbox controller, W A S D and the pointer for keyboard and mouse");
    CHECK(!strcmp(pc5_keys[k.key[0]].name, "space") && !strcmp(pc5_keys[k.key[7]].name, "mouse_left") &&
              !strcmp(pc5_keys[k.key[17]].name, "w") && !strcmp(pc5_keys[k.key[20]].name, "d") &&
              !strcmp(pc5_keys[pc5_key_count - 1].name, "none"),
          "default keys");
    CHECK(!pc5_controls_input(&k, text, sizeof(text)) && !strcmp(text, "[input]\nmode = xinput\n"), "xbox: %s", text);
    k.mode = PC5_PAD_DUALSENSE;
    CHECK(!pc5_controls_input(&k, text, sizeof(text)) && !strcmp(text, "[input]\nmode = dualsense\n"), "dualsense: %s", text);
    k.mode = PC5_PAD_KEYBOARD;
    CHECK(!pc5_controls_input(&k, text, sizeof(text)) && strstr(text, "mode = keyboard\nmouse = right_stick\nmouse_speed = 1200\n") &&
              strstr(text, "cross = space\n") && strstr(text, "left_stick_up = w\n") && !strstr(text, "right_stick_up"),
          "keyboard: %s", text);
    {
        char small[40];
        CHECK(pc5_controls_input(&k, small, sizeof(small)) == -1, "too small");
    }
    /* What prospero-win makes of it. */
    pw_game_input_init(&input);
    CHECK(pw_game_input_parse((const uint8_t *)text, strlen(text), &input) == 0 && input.mode == PW_GAME_INPUT_KEYBOARD &&
              input.mouse == PW_GAME_STICK_RIGHT && input.bindings[17].code == 'W' && input.bindings[6].kind == PW_GAME_BIND_MOUSE,
          "prospero-win reads the keyboard layout");
    /* Every key's name is one prospero-win takes. */
    for (int i = 0; i < pc5_key_count; i++) {
        char line[64];
        PwGameInput one;
        pw_game_input_init(&one);
        snprintf(line, sizeof(line), "[input]\ncross = %s\n", pc5_keys[i].name);
        CHECK(pw_game_input_parse((const uint8_t *)line, strlen(line), &one) == 0, "key %s", pc5_keys[i].name);
    }
    /* Kept and read back; one pointer at a time. */
    k.stick[0] = PC5_STICK_NOTHING;
    k.speed = 9;
    k.key[3] = 5;
    CHECK(!pc5_controls_save(dir, "game", &k) && !pc5_controls_load(dir, "game", &back) && !memcmp(&k, &back, sizeof(k)),
          "saved and read back");
    pc5_controls_next_stick(&back, 0, 1);           /* nothing -> keys */
    CHECK(back.stick[0] == PC5_STICK_KEYS, "nothing, then keys");
    pc5_controls_next_stick(&back, 0, 1);           /* keys -> pointer: the right stick sends keys */
    CHECK(back.stick[0] == PC5_STICK_POINTER && back.stick[1] == PC5_STICK_KEYS, "one pointer");
    CHECK(!pc5_controls_input(&back, text, sizeof(text)) && strstr(text, "mouse = left_stick\n") &&
              strstr(text, "right_stick_left = left\n") && !strstr(text, "left_stick_up"),
          "the left stick the pointer: %s", text);
    pc5_controls_next_stick(&back, 1, -1);          /* keys -> nothing */
    CHECK(back.stick[1] == PC5_STICK_NOTHING, "back to nothing");
    /* A file with two pointers, a bad speed and keys PC5 doesn't know keeps the defaults for them. */
    path(file, sizeof(file), "%s/prefixes/odd/pc5/controls.txt", dir);
    write_text(file, "mode = keyboard\nleft_stick = pointer\nright_stick = pointer\npointer_speed = 11\n"
                     "cross = warp\ncircle = f13\nsquare = m\nwhat = a\n");
    CHECK(!pc5_controls_load(dir, "odd", &back) && back.mode == PC5_PAD_KEYBOARD && back.stick[0] == PC5_STICK_KEYS &&
              back.stick[1] == PC5_STICK_POINTER && back.speed == 4 && !strcmp(pc5_keys[back.key[0]].name, "space") &&
              !strcmp(pc5_keys[back.key[1]].name, "ctrl") && !strcmp(pc5_keys[back.key[2]].name, "m"),
          "an odd file");
    /* The slow pointer button: none by default; one chosen reaches prospero-win, which slows the pointer
     * while it is held and sends nothing else for it. The wheel as two keys. */
    pc5_controls_default(&k);
    CHECK(k.slow_button == -1 && k.slow_percent == 50, "no slow button by default");
    k.mode = PC5_PAD_KEYBOARD;
    CHECK(!pc5_controls_input(&k, text, sizeof(text)) && !strstr(text, "mouse_slow"), "none: not written");
    pc5_controls_next_slow(&k, 1);
    CHECK(k.slow_button == 0, "then Cross");
    pc5_controls_next_slow(&k, -1);
    pc5_controls_next_slow(&k, -1);
    CHECK(k.slow_button == PC5_BUTTONS - 1, "back round to the touchpad");
    k.slow_button = 6;   /* L2 */
    k.slow_percent = 70;
    k.key[4] = (uint8_t)3;   /* L1: Wheel up */
    CHECK(!strcmp(pc5_keys[3].name, "mouse_wheel_up") && !strcmp(pc5_keys[4].name, "mouse_wheel_down"), "the wheel's keys");
    CHECK(!pc5_controls_input(&k, text, sizeof(text)) && strstr(text, "mouse_slow = l2\nmouse_slow_percent = 70\n") &&
              strstr(text, "l1 = mouse_wheel_up\n"), "written: %s", text);
    pw_game_input_init(&input);
    CHECK(pw_game_input_parse((const uint8_t *)text, strlen(text), &input) == 0 && input.slow_mask == 0x100u &&
              input.slow_percent == 70 && input.bindings[4].kind == PW_GAME_BIND_WHEEL && input.bindings[4].code == 0,
          "prospero-win reads them");
    CHECK(!pc5_controls_save(dir, "slow", &k) && !pc5_controls_load(dir, "slow", &back) && back.slow_button == 6 &&
              back.slow_percent == 70 && back.key[4] == 3, "kept and read back");
    k.stick[1] = PC5_STICK_KEYS;   /* no pointer: no slow button */
    CHECK(!pc5_controls_input(&k, text, sizeof(text)) && !strstr(text, "mouse_slow"), "no pointer: not written");
    CHECK(!strcmp(pc5_control_name(0), "Cross") && !strcmp(pc5_control_name(24), "Right stick right") &&
              !strcmp(pc5_control_name(25), "") && !strcmp(pc5_pad_mode_name(PC5_PAD_DUALSENSE), "DualSense"),
          "names");
}

/* ---- settings, updates, logs, covers ---------------------------------------------------------- */

/* api_keys.txt: made when there is none, read as NAME=value lines, never a key PC5 can't send. */
static void test_api_keys(void)
{
    char dir[PC5_PATH], file[PC5_PATH], key[96];
    struct stat st;

    path(dir, sizeof(dir), "%s/api-keys", work);
    pc5_mkdirs(dir);
    path(file, sizeof(file), "%s/api_keys.txt", dir);
    unlink(file);
    CHECK(pc5_api_key(dir, PC5_STEAMGRIDDB_KEY, key, sizeof(key)) && !key[0], "no file: no key");
    Pc5Buf text = { 0 };
    CHECK(!pc5_read_file(file, &text, 4096) && !pc5_buf_terminate(&text) &&
          strstr((char *)text.data, "\nSTEAMGRIDDB_API_KEY=\n") && strstr((char *)text.data, "# "), "made, explained, empty");
    pc5_buf_free(&text);
    CHECK(!stat(file, &st) && (st.st_mode & 077) == 0, "readable by its owner only: %o", (unsigned)(st.st_mode & 0777));
    CHECK(pc5_api_key(dir, PC5_STEAMGRIDDB_KEY, key, sizeof(key)) && !key[0], "made empty: still no key");
    write_text(file, "# STEAMGRIDDB_API_KEY=commented\n\n  OTHER_API_KEY = other\r\n"
                     "  STEAMGRIDDB_API_KEY =   abc123def  \r\nSTEAMGRIDDB_API_KEY_OLD=old\n");
    CHECK(!pc5_api_key(dir, PC5_STEAMGRIDDB_KEY, key, sizeof(key)) && !strcmp(key, "abc123def"), "spaces trimmed: '%s'", key);
    CHECK(!pc5_api_key(dir, "OTHER_API_KEY", key, sizeof(key)) && !strcmp(key, "other"), "another key: '%s'", key);
    write_text(file, "STEAMGRIDDB_API_KEY=two words\nSTEAMGRIDDB_API_KEY\n");
    CHECK(pc5_api_key(dir, PC5_STEAMGRIDDB_KEY, key, sizeof(key)) && !key[0], "a key with a space is no key");
    write_text(file, "STEAMGRIDDB_API_KEY=\nSTEAMGRIDDB_API_KEY=later\n");
    CHECK(!pc5_api_key(dir, PC5_STEAMGRIDDB_KEY, key, sizeof(key)) && !strcmp(key, "later"), "the first that is one: %s", key);
    char small[4];
    write_text(file, "STEAMGRIDDB_API_KEY=toolong\n");
    CHECK(pc5_api_key(dir, PC5_STEAMGRIDDB_KEY, small, sizeof(small)) && !small[0], "too long for out: none");
}

static void test_settings(void)
{
    char dir[PC5_PATH], file[PC5_PATH];
    Pc5Settings set, back;

    path(dir, sizeof(dir), "%s/settings-root", work);
    pc5_mkdirs(dir);
    CHECK(pc5_settings_load(dir, &set) == -1 && !set.fps && !set.frame_time && !set.cpu_usage && !set.cpu_temperature &&
          !set.soc_temperature && !set.memory && !pc5_game_overlay_lines(&set) && set.vibration == 100 &&
          set.check_updates && !set.keep_logs && !strcmp(set.startup_sound, "cheap chinese speaker") &&
          set.startup_volume == 100 &&
          set.fps_corner == PC5_CORNER_TOP_RIGHT && set.light_bar == PC5_LIGHT_GAME && set.triggers == PC5_TRIGGERS_OFF &&
          set.trigger_rumble == 100 && set.trigger_resistance == 50,
          "defaults without a file");
    set.fps = 1;
    set.cpu_usage = 1;
    set.soc_temperature = 1;
    set.fps_corner = PC5_CORNER_BOTTOM_LEFT;
    set.vibration = 0;
    set.light_bar = PC5_LIGHT_SCREEN;
    set.triggers = PC5_TRIGGERS_BOTH;
    set.trigger_rumble = 25;
    set.trigger_resistance = 75;
    set.check_updates = 0;
    set.keep_logs = 1;
    pc5_copy(set.startup_sound, sizeof(set.startup_sound), "Chime Two");
    set.startup_volume = 40;
    CHECK(!set.pictures_off, "every picture source on by default");
    set.pictures_off = PC5_PICTURES_GOG | PC5_PICTURES_WIKIDATA;
    CHECK(!pc5_settings_save(dir, &set), "saved");
    CHECK(!pc5_settings_load(dir, &back) && !memcmp(&set, &back, sizeof(set)), "read back the same");
    path(file, sizeof(file), "%s/pc5/settings.txt", dir);
    write_text(file, "fps = maybe\nfps_corner = middle\nnonsense line\nvibration = off\nlight_bar = console\n"
                     "trigger_rumble = 30\ntrigger_resistance = 0\ntriggers = both\n");
    CHECK(!pc5_settings_load(dir, &back) && !back.fps && back.fps_corner == PC5_CORNER_TOP_RIGHT && !back.vibration &&
          back.light_bar == PC5_LIGHT_CONSOLE && back.trigger_rumble == 100 && back.trigger_resistance == 50 &&
          back.triggers == PC5_TRIGGERS_BOTH, "what it doesn't know gives the default");
    /* Wikipedia, a source until 1.9.2, is a line PC5 doesn't know now. */
    write_text(file, "pictures_steam = maybe\npictures_steamgriddb = off\npictures_wikipedia = off\npictures_bing = off\n");
    CHECK(!pc5_settings_load(dir, &back) && back.pictures_off == PC5_PICTURES_STEAMGRIDDB, "picture sources: %#x",
          (unsigned)back.pictures_off);
    write_text(file, "startup_volume = 70\n");
    CHECK(!pc5_settings_load(dir, &back) && back.startup_volume == 70, "startup volume read: %d", back.startup_volume);
    write_text(file, "startup_volume = 0\nstartup_volume = 105\nstartup_volume = 55\nstartup_volume = 7x\n");
    CHECK(!pc5_settings_load(dir, &back) && back.startup_volume == 100, "a startup volume off its steps is the default: %d",
          back.startup_volume);
    CHECK(pc5_volume_next(100, 1) == 10 && pc5_volume_next(10, -1) == 100 && pc5_volume_next(50, -1) == 40 &&
          pc5_volume_next(90, 1) == 100, "the startup volume's steps go round");
    write_text(file, "vibration = on\n");
    CHECK(!pc5_settings_load(dir, &back) && back.vibration == 100, "an earlier PC5's vibration = on is 100 %%");
    write_text(file, "startup_sound = PC5\n");
    CHECK(!pc5_settings_load(dir, &back) && !strcmp(back.startup_sound, "cheap chinese speaker"),
          "1.8.0's own sound, gone, is the default: %s", back.startup_sound);
    write_text(file, "startup_sound = on\n");
    CHECK(!pc5_settings_load(dir, &back) && !strcmp(back.startup_sound, "cheap chinese speaker"), "1.7.0's on too");
    write_text(file, "fps = on\noverlay_size = large\n");
    CHECK(!pc5_settings_load(dir, &back) && back.overlay_size == PC5_OVERLAY_LARGE && !pc5_settings_save(dir, &back) &&
          !pc5_settings_load(dir, &back) && back.overlay_size == PC5_OVERLAY_LARGE &&
          pc5_game_overlay_lines(&back) == (PW_PC5_HUD_FPS | PW_PC5_HUD_LARGE), "Size: Large, read and saved");
    write_text(file, "fps = on\noverlay_size = extra-large\n");
    CHECK(!pc5_settings_load(dir, &back) && back.overlay_size == PC5_OVERLAY_EXTRA_LARGE && !pc5_settings_save(dir, &back) &&
          file_holds_text(file, "overlay_size = extra-large\n") &&
          pc5_game_overlay_lines(&back) == (PW_PC5_HUD_FPS | PW_PC5_HUD_DOUBLE) &&
          !strcmp(pc5_overlay_size_name(back.overlay_size), "Extra large"), "Size: Extra large, read and saved");
    write_text(file, "fps = on\noverlay_size = giant\n");
    CHECK(!pc5_settings_load(dir, &back) && back.overlay_size == PC5_OVERLAY_NORMAL, "a size PC5 doesn't know: Normal");
    back.fps = 0;
    CHECK(!pc5_game_overlay_lines(&back), "no line: no overlay, large or not");
    write_text(file, "vibration = 75\ntrigger_rumble = 50\n");
    CHECK(!pc5_settings_load(dir, &back) && back.vibration == 75 && back.trigger_rumble == 50, "strengths read");
    write_text(file, "frame_time = on\ncpu_usage = on\ncpu_temperature = on\nsoc_temperature = yes\nmemory = on\n");
    CHECK(!pc5_settings_load(dir, &back) && !back.fps && back.frame_time && back.cpu_usage && back.cpu_temperature &&
          !back.soc_temperature && back.memory &&
          pc5_game_overlay_lines(&back) == (PW_PC5_HUD_FRAME_TIME | PW_PC5_HUD_CPU | PW_PC5_HUD_CPU_TEMP | PW_PC5_HUD_MEMORY),
          "the overlay's lines read, each on its own: %#x", pc5_game_overlay_lines(&back));
    CHECK(pc5_strength_next(100, 0, 1) == 0 && pc5_strength_next(0, 0, -1) == 100 && pc5_strength_next(25, 25, -1) == 100 &&
          pc5_strength_next(100, 25, 1) == 25 && pc5_strength_next(50, 25, 1) == 75 && pc5_strength_next(50, 0, -1) == 25,
          "strength steps");
    CHECK(!strcmp(pc5_corner_name(PC5_CORNER_BOTTOM_RIGHT), "Bottom right"), "corner name");
    CHECK(!strcmp(pc5_triggers_name(PC5_TRIGGERS_BOTH), "Rumble + resistance"), "triggers name");
}

static void test_update(void)
{
    FakeNet net = { 0 };
    Pc5Http http = { &net, fake_get, NULL, fake_get_authorized };
    Pc5Update update;
    Pc5UpdateMemory memory, back;
    char dir[PC5_PATH];

    CHECK(pc5_version_compare("1.0.0", "1.0.0") == 0 && pc5_version_compare("v1.0.1", "1.0.0") > 0 &&
          pc5_version_compare("1.10", "1.9.4") > 0 && pc5_version_compare("1.0", "1.0.0") == 0 &&
          pc5_version_compare("0.9.9", "v1.0.0") < 0, "versions");
    pc5_update_check(&http, "1.0.0", &update);
    CHECK(update.state == PC5_UPDATE_NONE && !strcmp(net.last, PC5_RELEASES_API), "no release yet: %d", (int)update.state);
    net.release = "{\"tag_name\":\"v1.1.0\",\"name\":\"PC5 1.1.0\",\"draft\":false,"
                  "\"html_url\":\"https://github.com/bornaradusin/PC5/releases/tag/v1.1.0\",\"body\":\"New: \\\"x\\\"\"}";
    pc5_update_check(&http, "1.0.0", &update);
    CHECK(update.state == PC5_UPDATE_AVAILABLE && !strcmp(update.version, "1.1.0") && !strcmp(update.name, "PC5 1.1.0") &&
          strstr(update.url, "/releases/tag/v1.1.0"), "a newer one: %d %s", (int)update.state, update.version);
    net.release = "{\"tag_name\":\"v1.0.0\",\"html_url\":\"https://example.com/evil\"}";
    pc5_update_check(&http, "1.0.0", &update);
    CHECK(update.state == PC5_UPDATE_CURRENT && !strcmp(update.url, PC5_RELEASES_PAGE), "the newest, and only GitHub links");
    net.release = "not json";
    pc5_update_check(&http, "1.0.0", &update);
    CHECK(update.state == PC5_UPDATE_FAILED, "a broken answer");
    net.offline = 1;
    pc5_update_check(&http, "1.0.0", &update);
    CHECK(update.state == PC5_UPDATE_FAILED, "offline");
    path(dir, sizeof(dir), "%s/update-root", work);
    pc5_mkdirs(dir);
    pc5_update_load(dir, &memory);
    CHECK(!memory.checked && !memory.told[0] && memory.last.state == PC5_UPDATE_UNKNOWN, "nothing remembered yet");
    memory.checked = 1792691250;
    pc5_copy(memory.told, sizeof(memory.told), "9.0.0");
    memory.last.state = PC5_UPDATE_AVAILABLE;
    pc5_copy(memory.last.version, sizeof(memory.last.version), "9.0.0");
    CHECK(!pc5_update_save(dir, &memory), "remembered");
    pc5_update_load(dir, &back);
    CHECK(back.checked == memory.checked && !strcmp(back.told, "9.0.0") && back.last.state == PC5_UPDATE_AVAILABLE &&
          !strcmp(back.last.version, "9.0.0"), "read back");
}


/* ---- PC5 updating itself ------------------------------------------------------------------- */

/* A release's JSON with its zip and, when update_size isn't 0, its update zip at update_url:
 * a digest "" leaves it out. */
/* The release the update zip holds the changes since, as the notes say it (from 1.9.8's
 * releases), and as its label said it (1.9.7's release); NULL: neither says. */
static const char *update_from = PC5_VERSION;
static const char *update_label = NULL;

static void release_json_with(char *out, size_t size, const char *tag, uint64_t zip_size, const char *digest,
                              uint64_t update_size, const char *update_digest, const char *update_url)
{
    char extra[120] = "", update[1024] = "", update_extra[200] = "", body[200] = "";
    if (digest && digest[0]) snprintf(extra, sizeof(extra), ",\"digest\":\"sha256:%s\"", digest);
    if (update_digest && update_digest[0])
        snprintf(update_extra, sizeof(update_extra), ",\"digest\":\"sha256:%s\"", update_digest);
    if (update_label) {
        size_t n = strlen(update_extra);
        snprintf(update_extra + n, sizeof(update_extra) - n, ",\"label\":\"%s\"", update_label);
    }
    if (update_size && update_from)
        snprintf(body, sizeof(body), ",\"body\":\"Notes.\\n\\n<!-- pc5-update-from: %s -->\\n\"", update_from);
    if (update_size)
        snprintf(update, sizeof(update), ",{\"name\":\"PPSA11018-update.zip\",\"size\":%llu%s,\"browser_download_url\":\"%s\"}",
                 (unsigned long long)update_size, update_extra, update_url);
    snprintf(out, size,
             "{\"tag_name\":\"%s\",\"name\":\"PC5\",\"html_url\":\"https://github.com/bornaradusin/PC5/releases/tag/%s\"%s,"
             "\"assets\":[{\"name\":\"SHA256SUMS\",\"size\":10,\"browser_download_url\":\"https://github.com/x/y\"},"
             "{\"name\":\"PPSA11018.zip\",\"size\":%llu,\"uploader\":{\"login\":\"x\"}%s,"
             "\"browser_download_url\":\"" TEST_ZIP_URL "\"}%s]}",
             tag, tag, body, (unsigned long long)zip_size, extra, update);
}

/* A release's JSON with its zip: digest "" leaves it out. */
static void release_json(char *out, size_t size, const char *tag, uint64_t zip_size, const char *digest)
{
    release_json_with(out, size, tag, zip_size, digest, 0, NULL, NULL);
}

static void file_sha256(const char *file, char *out)
{
    Pc5Buf data = { 0 };
    Pc5Sha256 sha;
    uint8_t digest[32];
    pc5_read_file(file, &data, 64u << 20);
    pc5_sha256_init(&sha);
    pc5_sha256_update(&sha, data.data, data.len);
    pc5_sha256_final(&sha, digest);
    hex(digest, out);
    pc5_buf_free(&data);
}

/* 1 when file has text somewhere in it. */
static int file_holds_text(const char *file, const char *text)
{
    Pc5Buf data = { 0 };
    int holds = !pc5_read_file(file, &data, 1u << 20) && !pc5_buf_terminate(&data) && strstr((char *)data.data, text);
    pc5_buf_free(&data);
    return holds;
}

static int file_is(const char *file, const char *text)
{
    Pc5Buf data = { 0 };
    int same = !pc5_read_file(file, &data, 1u << 20) && data.len == strlen(text) && !memcmp(data.data, text, data.len);
    pc5_buf_free(&data);
    return same;
}

static const char *const new_files[][2] = {
    { "eboot.bin", "PC5 NEW EBOOT\n" },
    { "sce_sys/param.json", "{\"titleId\":\"PPSA11018\",\"contentVersion\":\"99.000.000\"}" },
    { "sce_sys/icon0.png", "new icon" },
    { "pc5/fonts.bin", "new fonts" },
    { "LICENSE", "new licence" },
    { "win/wine/lib/empty.txt", "" },
    /* a folder, and one whose name goes on past it with a byte above 0x7f (sorted after '/') */
    { "pc5/data/a.bin", "a" },
    { "pc5/data\xc3\xa9/b.bin", "b" },
};

/* A file's size and CRC-32 as the update's journal gives them. */
static void mark_of(const char *text, char *out, size_t size)
{
    snprintf(out, size, "%zu %08x", strlen(text), (unsigned)pc5_crc32(text, strlen(text)));
}

/* The large file's bytes: half that deflates well, half that hardly does. */
static uint8_t big_byte(size_t i)
{
    if (i < 150000) return (uint8_t)((i * 7 + (i >> 11)) % 61);
    uint32_t x = (uint32_t)i * 0x9e3779b1u;
    x ^= x >> 15;
    x *= 0x85ebca77u;
    x ^= x >> 13;
    return (uint8_t)(x >> 24);
}

/* The release's zip as pc5_zip writes it, with a large file that deflates. */
static void make_release_zip(const char *file, const char *extra_name)
{
    Pc5Zip zip;
    char name[PC5_PATH];
    uint8_t *big = pc5_alloc(300000);
    for (size_t i = 0; i < 300000; i++) big[i] = big_byte(i);
    CHECK(!pc5_zip_open(&zip, file), "a zip to update with");
    for (size_t i = 0; i < sizeof(new_files) / sizeof(new_files[0]); i++) {
        path(name, sizeof(name), "PPSA11018/%s", new_files[i][0]);
        pc5_zip_add(&zip, name, new_files[i][1], strlen(new_files[i][1]), 1792700000);
    }
    pc5_zip_add(&zip, "PPSA11018/win/wine/lib/big.bin", big, 300000, 1792700000);
    if (extra_name) pc5_zip_add(&zip, extra_name, "x", 1, 1792700000);
    CHECK(!pc5_zip_close(&zip), "the zip written");
    pc5_free(big);
}

/* An installed PC5 at folder, the same as the running app at app. */
static void make_installed(const char *app, const char *folder)
{
    char command[3 * PC5_PATH];
    snprintf(command, sizeof(command), "rm -rf '%s' && mkdir -p '%s' && cp -r '%s/.' '%s/'", folder, folder, app, folder);
    run(command);
    char file[PC5_PATH];
    path(file, sizeof(file), "%s/notes.txt", folder);
    write_text(file, "mine");
}

static void wait_upgrade(Pc5Upgrade *upgrade)
{
    for (int i = 0; i < 20000 && pc5_upgrade_busy(upgrade); i++) usleep(1000);
}

static void test_upgrade(void)
{
    char base[PC5_PATH], app[PC5_PATH], data[PC5_PATH], usb0[PC5_PATH], folder[PC5_PATH], user[PC5_PATH],
        zip[PC5_PATH], file[PC5_PATH], found[PC5_PATH], digest[65], json[2048], unpacked[PC5_PATH], error[PC5_ERROR];
    FakeNet net = { 0 };
    static Pc5Upgrade upgrade;
    Pc5Update update;

    path(base, sizeof(base), "%s/upgrade", work);
    path(app, sizeof(app), "%s/app0", base);
    path(data, sizeof(data), "%s/data", base);
    path(usb0, sizeof(usb0), "%s/usb0", base);
    path(folder, sizeof(folder), "%s/homebrew/PPSA11018", usb0);
    path(user, sizeof(user), "%s/user", base);
    path(zip, sizeof(zip), "%s/release/PPSA11018.zip", base);
    const char *drives[] = { data, usb0, NULL };
    Pc5UpgradeConfig config = { { &net, fake_get, fake_download, fake_get_authorized }, app, NULL, drives, user, NULL, NULL };

    /* The running PC5, its installed folder, and two that aren't it. */
    path(file, sizeof(file), "%s/eboot.bin", app);
    write_text(file, "PC5 OLD EBOOT\n");
    path(file, sizeof(file), "%s/sce_sys/param.json", app);
    write_text(file, "{\"titleId\":\"PPSA11018\",\"contentVersion\":\"01.007.000\"}");
    path(file, sizeof(file), "%s/pc5/fonts.bin", app);
    write_text(file, "old fonts");
    path(file, sizeof(file), "%s/LICENSE", app);
    write_text(file, "old licence");
    path(file, sizeof(file), "%s/old-only.txt", app);
    write_text(file, "only in the old version");
    path(file, sizeof(file), "%s/homebrew/Other/eboot.bin", usb0);
    write_text(file, "PC5 OLD EBOOT\n");
    path(file, sizeof(file), "%s/homebrew/Other/sce_sys/param.json", usb0);
    write_text(file, "{\"titleId\":\"PPSA99999\"}");
    path(file, sizeof(file), "%s/PC5-copy/eboot.bin", usb0);
    write_text(file, "PC5 SOME OTHER EBOOT\n");
    path(file, sizeof(file), "%s/PC5-copy/sce_sys/param.json", usb0);
    write_text(file, "{\"titleId\":\"PPSA11018\"}");
    path(file, sizeof(file), "%s/appmeta/PPSA11018/icon0.png", user);
    write_text(file, "old icon");
    path(file, sizeof(file), "%s/app/PPSA11018/sce_sys/param.json", user);
    write_text(file, "old param");
    path(file, sizeof(file), "%s/app/PPSA11018/icon0.png", user);
    write_text(file, "old icon");
    pc5_mkdirs(data);
    make_installed(app, folder);
    path(file, sizeof(file), "%s/release", base);
    pc5_mkdirs(file);
    make_release_zip(zip, NULL);
    pc5_copy(net.zip, sizeof(net.zip), zip);
    file_sha256(zip, digest);
    struct stat st;
    stat(zip, &st);

    /* The release's zip, as the update check reads it. */
    release_json(json, sizeof(json), "v99.0.0", (uint64_t)st.st_size, digest);
    net.release = json;
    pc5_update_check(&config.http, PC5_VERSION, &update);
    CHECK(update.state == PC5_UPDATE_AVAILABLE && !strcmp(update.asset_url, TEST_ZIP_URL) &&
          update.asset_size == (uint64_t)st.st_size && !strcmp(update.asset_sha256, digest),
          "the release's zip: %s %llu %s", update.asset_url, (unsigned long long)update.asset_size, update.asset_sha256);
    {
        char other[2048];
        snprintf(other, sizeof(other), "{\"tag_name\":\"v99.0.0\",\"assets\":[{\"name\":\"PPSA11018.zip\",\"size\":5,"
                 "\"digest\":\"md5:abc\",\"browser_download_url\":\"https://example.com/PPSA11018.zip\"}]}");
        net.release = other;
        pc5_update_check(&config.http, PC5_VERSION, &update);
        CHECK(update.state == PC5_UPDATE_AVAILABLE && !update.asset_url[0], "a zip somewhere other than GitHub is ignored");
        /* The update zip beside it, read the same way. */
        release_json_with(other, sizeof(other), "v99.0.0", (uint64_t)st.st_size, digest, 1234, digest, TEST_UPDATE_URL);
        pc5_update_check(&config.http, PC5_VERSION, &update);
        CHECK(!strcmp(update.asset_url, TEST_ZIP_URL) && !strcmp(update.delta_url, TEST_UPDATE_URL) &&
              update.delta_size == 1234 && !strcmp(update.delta_sha256, digest), "both zips: %s %llu %s", update.delta_url,
              (unsigned long long)update.delta_size, update.delta_sha256);
        release_json_with(other, sizeof(other), "v99.0.0", (uint64_t)st.st_size, digest, 1234, "",
                          "https://example.com/PPSA11018-update.zip");
        pc5_update_check(&config.http, PC5_VERSION, &update);
        CHECK(!strcmp(update.asset_url, TEST_ZIP_URL) && !update.delta_url[0] && !update.delta_size,
              "an update zip somewhere other than GitHub is ignored: %s", update.delta_url);
        release_json_with(other, sizeof(other), "v99.0.0", (uint64_t)st.st_size, digest, 1234, "", TEST_UPDATE_URL);
        pc5_update_check(&config.http, PC5_VERSION, &update);
        CHECK(!strcmp(update.delta_url, TEST_UPDATE_URL) && !update.delta_sha256[0], "one without a digest has none");
        /* The notes name the release it holds the changes since: only for that one. */
        CHECK(!strcmp(update.delta_from, PC5_VERSION) && pc5_update_small(&update, PC5_VERSION) &&
              !pc5_update_small(&update, "1.0.0"), "made from this PC5: %s", update.delta_from);
        update_from = "v1.0.0";
        release_json_with(other, sizeof(other), "v99.0.0", (uint64_t)st.st_size, digest, 1234, "", TEST_UPDATE_URL);
        pc5_update_check(&config.http, PC5_VERSION, &update);
        CHECK(!strcmp(update.delta_from, "1.0.0") && !pc5_update_small(&update, PC5_VERSION) &&
              pc5_update_small(&update, "1.0.0"), "made from another: %s", update.delta_from);
        /* 1.9.7's release said it with the update zip's label instead. */
        update_from = NULL;
        update_label = "from 1.0.0";
        release_json_with(other, sizeof(other), "v99.0.0", (uint64_t)st.st_size, digest, 1234, "", TEST_UPDATE_URL);
        pc5_update_check(&config.http, PC5_VERSION, &update);
        CHECK(!strcmp(update.delta_from, "1.0.0"), "a label as 1.9.7's release has it: %s", update.delta_from);
        update_label = "PPSA11018-update.zip";
        release_json_with(other, sizeof(other), "v99.0.0", (uint64_t)st.st_size, digest, 1234, "", TEST_UPDATE_URL);
        pc5_update_check(&config.http, PC5_VERSION, &update);
        CHECK(!update.delta_from[0] && !pc5_update_small(&update, PC5_VERSION), "neither says: %s", update.delta_from);
        update_label = NULL;
        update_from = PC5_VERSION;
        net.release = json;
    }

    /* PC5's folder: the one whose eboot is the running one's, and a PC5 title. */
    char other[PC5_PATH];
    CHECK(!pc5_upgrade_find(&config, found, sizeof(found), other, sizeof(other)) && !strcmp(found, folder),
          "PC5's folder: %s", found);
    /* A copy of PC5 named like the work folder, at the top of a drive: not PC5's folder, and
     * not PC5's work folder either (no marker), so nothing deletes it. */
    {
        char copy[PC5_PATH], command[4 * PC5_PATH];
        path(copy, sizeof(copy), "%s/pc5-update", usb0);
        snprintf(command, sizeof(command), "cp -r '%s' '%s'", folder, copy);
        run(command);
        CHECK(!pc5_upgrade_find(&config, found, sizeof(found), other, sizeof(other)) && !strcmp(found, folder),
              "a pc5-update folder isn't PC5's: %s", found);
        CHECK(!pc5_upgrade_tidy(&config, NULL, 0) && pc5_is_dir(copy), "and nothing deletes it");
        upgrade.config = config;
        pc5_upgrade_run(&upgrade);
        CHECK(atomic_load(&upgrade.step) == PC5_UPGRADE_FAILED && strstr(upgrade.error, "work folder") && pc5_is_dir(copy),
              "nor the update: %s", upgrade.error);
        snprintf(command, sizeof(command), "rm -rf '%s'", copy);
        run(command);
        /* Two of them: PC5 doesn't guess. */
        path(copy, sizeof(copy), "%s/homebrew/PC5-backup", data);
        snprintf(command, sizeof(command), "mkdir -p '%s/homebrew' && cp -r '%s' '%s'", data, folder, copy);
        run(command);
        CHECK(pc5_upgrade_find(&config, found, sizeof(found), other, sizeof(other)) == -2 && !strcmp(found, copy) &&
              !strcmp(other, folder), "PC5 twice: %s %s", found, other);
        pc5_upgrade_run(&upgrade);
        CHECK(atomic_load(&upgrade.step) == PC5_UPGRADE_FAILED && strstr(upgrade.error, "more than once"),
              "nothing updated: %s", upgrade.error);
        snprintf(command, sizeof(command), "rm -rf '%s'", copy);
        run(command);
    }

    /* What the zip holds, and only that. */
    path(unpacked, sizeof(unpacked), "%s/unpacked", base);
    {
        atomic_uint_fast64_t done = 0, total = 0;
        uint64_t expected = 300000;
        for (size_t i = 0; i < sizeof(new_files) / sizeof(new_files[0]); i++) expected += strlen(new_files[i][1]);
        Pc5Buf names = { 0 };
        CHECK(!pc5_unzip(zip, "PPSA11018/", unpacked, &done, &total, NULL, UINT64_MAX, &names, error, sizeof(error)) &&
              atomic_load(&done) == atomic_load(&total) && atomic_load(&total) == expected,
              "unzipped: %s %llu", error, (unsigned long long)atomic_load(&total));
        CHECK(pc5_buf_terminate(&names) == 0 && strstr((char *)names.data, "eboot.bin\t14\n") &&
              strstr((char *)names.data, "win/wine/lib/big.bin\t300000\n") && !strstr((char *)names.data, "PPSA11018"),
              "its names: %.80s", names.data ? (char *)names.data : "");
        pc5_buf_free(&names);
        CHECK(pc5_unzip(zip, "PPSA11018", unpacked, NULL, NULL, NULL, UINT64_MAX, NULL, error, sizeof(error)),
              "a prefix that isn't a folder");
        path(file, sizeof(file), "%s/eboot.bin", unpacked);
        CHECK(file_is(file, "PC5 NEW EBOOT\n"), "a file unzipped");
        path(file, sizeof(file), "%s/win/wine/lib/empty.txt", unpacked);
        CHECK(pc5_is_file(file) && file_is(file, ""), "an empty file");
        Pc5Buf big = { 0 };
        path(file, sizeof(file), "%s/win/wine/lib/big.bin", unpacked);
        int same = !pc5_read_file(file, &big, 1u << 20) && big.len == 300000;
        for (size_t i = 0; same && i < big.len; i++) same = big.data[i] == big_byte(i);
        CHECK(same, "a deflated file intact");
        pc5_buf_free(&big);
        struct stat mode;
        CHECK(!stat(file, &mode) && (mode.st_mode & 0777) == 0777, "made 0777: %o", (unsigned)(mode.st_mode & 0777));
        CHECK(pc5_unzip(zip, "PPSA11018/", unpacked, NULL, NULL, NULL, 1000, NULL, error, sizeof(error)) && strstr(error, "room"),
              "not without room: %s", error);
    }
    {
        char bad[PC5_PATH], escaped[PC5_PATH];
        path(bad, sizeof(bad), "%s/release/bad.zip", base);
        path(escaped, sizeof(escaped), "%s/evil.txt", base);
        make_release_zip(bad, "PPSA11018/../../evil.txt");
        CHECK(pc5_unzip(bad, "PPSA11018/", unpacked, NULL, NULL, NULL, UINT64_MAX, NULL, error, sizeof(error)) &&
              strstr(error, "won't unpack") && !pc5_exists(escaped), "a name leaving the folder: %s", error);
        make_release_zip(bad, "Other/x.txt");
        CHECK(pc5_unzip(bad, "PPSA11018/", unpacked, NULL, NULL, NULL, UINT64_MAX, NULL, error, sizeof(error)), "outside PPSA11018/");
        /* As Python's zipfile writes one (tools/make-app.py): folders listed, files stored and deflated. */
        char command[4 * PC5_PATH];
        snprintf(command, sizeof(command), "python3 -c \"import zipfile, sys\n"
                 "z = zipfile.ZipFile(sys.argv[1], 'w', zipfile.ZIP_DEFLATED, compresslevel=6)\n"
                 "z.writestr('PPSA11018/', '')\nz.writestr('PPSA11018/pc5/', '')\n"
                 "z.writestr(zipfile.ZipInfo('PPSA11018/stored.txt'), 'stored ' * 50)\n"
                 "z.writestr('PPSA11018/pc5/deflated.txt', 'deflated ' * 5000)\nz.close()\" '%s'", bad);
        run(command);
        char python[PC5_PATH];
        path(python, sizeof(python), "%s/python", base);
        CHECK(!pc5_unzip(bad, "PPSA11018/", python, NULL, NULL, NULL, UINT64_MAX, NULL, error, sizeof(error)), "Python's zip: %s", error);
        path(file, sizeof(file), "%s/pc5/deflated.txt", python);
        Pc5Buf text = { 0 };
        CHECK(!pc5_read_file(file, &text, 1u << 20) && text.len == 45000 && !memcmp(text.data, "deflated deflated", 17),
              "its deflated file");
        pc5_buf_free(&text);
        path(file, sizeof(file), "%s/stored.txt", python);
        CHECK(pc5_is_file(file), "its stored file");
        /* A damaged byte: the CRC-32 (or the deflate data) says so. */
        Pc5Buf copy = { 0 };
        pc5_read_file(zip, &copy, 64u << 20);
        copy.data[copy.len / 3] ^= 0x5a;
        pc5_write_file(bad, copy.data, copy.len);
        pc5_buf_free(&copy);
        CHECK(pc5_unzip(bad, "PPSA11018/", python, NULL, NULL, NULL, UINT64_MAX, NULL, error, sizeof(error)) &&
              (strstr(error, "damaged") || strstr(error, "can read")), "damaged: %s", error);
    }

    /* The update: downloaded, checked, unpacked, in place; the player's files at the top stay,
     * what the release's folders no longer have goes. */
    path(file, sizeof(file), "%s/pc5/stale.txt", folder);
    write_text(file, "from an older PC5");
    path(file, sizeof(file), "%s/win/wine/lib/stale-folder/x.dll", folder);
    write_text(file, "from an older PC5");
    path(file, sizeof(file), "%s/mystuff/keep.txt", folder);
    write_text(file, "mine too");
    path(file, sizeof(file), "%s/pc5/data", folder);
    write_text(file, "a file where the release has a folder");
    /* A folder at the top where the release has a file: the player's place, so PC5 stops
     * before anything moves. */
    path(file, sizeof(file), "%s/LICENSE", folder);
    unlink(file);
    path(file, sizeof(file), "%s/LICENSE/mine.txt", folder);
    write_text(file, "a player's file");
    upgrade.config = config;
    pc5_upgrade_run(&upgrade);
    path(found, sizeof(found), "%s/eboot.bin", folder);
    CHECK(atomic_load(&upgrade.step) == PC5_UPGRADE_FAILED && strstr(upgrade.error, "Move it") && !upgrade.partial &&
          file_is(found, "PC5 OLD EBOOT\n") && file_is(file, "a player's file"), "a folder in the way at the top: %s",
          upgrade.error);
    path(found, sizeof(found), "%s/pc5-update", usb0);
    CHECK(!pc5_exists(found), "and nothing kept");
    {
        char command[3 * PC5_PATH];
        snprintf(command, sizeof(command), "rm -rf '%s/LICENSE'", folder);
        run(command);
    }
    pc5_upgrade_run(&upgrade);
    CHECK(atomic_load(&upgrade.step) == PC5_UPGRADE_DONE && !strcmp(upgrade.version, "99.0.0") &&
          !strcmp(upgrade.folder, folder), "updated: %d %s", atomic_load(&upgrade.step), upgrade.error);
    path(file, sizeof(file), "%s/eboot.bin", folder);
    CHECK(file_is(file, "PC5 NEW EBOOT\n"), "the new eboot in PC5's folder");
    path(file, sizeof(file), "%s/pc5/fonts.bin", folder);
    CHECK(file_is(file, "new fonts"), "the new pc5 folder");
    path(file, sizeof(file), "%s/notes.txt", folder);
    CHECK(file_is(file, "mine"), "a file of the player's stays");
    path(file, sizeof(file), "%s/old-only.txt", folder);
    CHECK(pc5_is_file(file), "a file the new version doesn't have stays");
    path(file, sizeof(file), "%s/win/wine/lib/big.bin", folder);
    CHECK(pc5_is_file(file), "folders in place");
    path(file, sizeof(file), "%s/pc5/stale.txt", folder);
    CHECK(!pc5_exists(file), "a file the release's pc5 folder no longer has is gone");
    path(file, sizeof(file), "%s/win/wine/lib/stale-folder", folder);
    CHECK(!pc5_exists(file), "and a folder deep in it");
    path(file, sizeof(file), "%s/mystuff/keep.txt", folder);
    CHECK(file_is(file, "mine too"), "a folder of the player's at the top stays");
    path(file, sizeof(file), "%s/sce_module", folder);
    CHECK(!pc5_exists(file), "nothing made that the release doesn't have");
    path(file, sizeof(file), "%s/pc5/data/a.bin", folder);
    CHECK(file_is(file, "a"), "a file in a folder's place moved away, and the folder's file kept");
    path(file, sizeof(file), "%s/pc5-update", usb0);
    CHECK(!pc5_exists(file), "the work folder deleted");
    path(file, sizeof(file), "%s/appmeta/PPSA11018/icon0.png", user);
    CHECK(file_is(file, "new icon"), "the console's copy of the icon");
    path(file, sizeof(file), "%s/appmeta/PPSA11018/param.json", user);
    CHECK(file_is(file, new_files[1][1]), "and of param.json");
    path(file, sizeof(file), "%s/app/PPSA11018/sce_sys/param.json", user);
    CHECK(file_is(file, new_files[1][1]), "the registered sce_sys");
    path(file, sizeof(file), "%s/app/PPSA11018/icon0.png", user);
    CHECK(file_is(file, "new icon"), "the registered icon");
    path(file, sizeof(file), "%s/homebrew/Other/eboot.bin", usb0);
    CHECK(file_is(file, "PC5 OLD EBOOT\n"), "another title's folder untouched");

    /* Its eboot isn't the running one's any more: no folder to update. */
    pc5_upgrade_run(&upgrade);
    CHECK(atomic_load(&upgrade.step) == PC5_UPGRADE_FAILED && strstr(upgrade.error, "folder wasn't found"),
          "no folder: %s", upgrade.error);

    /* Downloads that aren't right change nothing. */
    make_installed(app, folder);
    path(file, sizeof(file), "%s/eboot.bin", folder);
    release_json(json, sizeof(json), "v99.0.0", (uint64_t)st.st_size,
                 "0000000000000000000000000000000000000000000000000000000000000000");
    pc5_upgrade_run(&upgrade);
    CHECK(atomic_load(&upgrade.step) == PC5_UPGRADE_FAILED && strstr(upgrade.error, "checksum") &&
          file_is(file, "PC5 OLD EBOOT\n"), "a wrong checksum: %s", upgrade.error);
    release_json(json, sizeof(json), "v99.0.0", (uint64_t)st.st_size, "");
    net.corrupt = 1;
    pc5_upgrade_run(&upgrade);
    CHECK(atomic_load(&upgrade.step) == PC5_UPGRADE_FAILED && (strstr(upgrade.error, "damaged") || strstr(upgrade.error, "can read")) &&
          file_is(file, "PC5 OLD EBOOT\n"), "damaged on the way, no checksum listed: %s", upgrade.error);
    net.corrupt = 0;
    release_json(json, sizeof(json), "v99.0.0", (uint64_t)st.st_size + 1, digest);
    pc5_upgrade_run(&upgrade);
    CHECK(atomic_load(&upgrade.step) == PC5_UPGRADE_FAILED && file_is(file, "PC5 OLD EBOOT\n"), "the wrong size: %s",
          upgrade.error);
    release_json(json, sizeof(json), "v99.0.0", (uint64_t)st.st_size, digest);
    net.download_status = 404;
    pc5_upgrade_run(&upgrade);
    CHECK(atomic_load(&upgrade.step) == PC5_UPGRADE_FAILED && strstr(upgrade.error, "HTTP 404"), "not found: %s", upgrade.error);
    net.download_status = 0;
    release_json(json, sizeof(json), "v1.0.0", (uint64_t)st.st_size, digest);
    pc5_upgrade_run(&upgrade);
    CHECK(atomic_load(&upgrade.step) == PC5_UPGRADE_FAILED && strstr(upgrade.error, "newest"), "nothing newer: %s",
          upgrade.error);
    release_json(json, sizeof(json), "v99.0.0", (uint64_t)st.st_size, digest);
    path(found, sizeof(found), "%s/pc5-update", usb0);
    CHECK(!pc5_exists(found), "nothing left behind");

    /* Stopped while downloading, on the worker thread. */
    net.gate = 1;
    net.gate_bytes = 14000;
    net.at_gate = 0;
    CHECK(!pc5_upgrade_start(&upgrade, &config), "the worker started");
    for (int i = 0; i < 20000 && !net.at_gate; i++) usleep(1000);
    CHECK(net.at_gate && atomic_load(&upgrade.step) == PC5_UPGRADE_DOWNLOADING && atomic_load(&upgrade.done) == 14000 &&
          pc5_upgrade_busy(&upgrade), "downloading: %d %llu", atomic_load(&upgrade.step),
          (unsigned long long)atomic_load(&upgrade.done));
    pc5_upgrade_cancel(&upgrade);
    net.gate = 0;
    wait_upgrade(&upgrade);
    CHECK(atomic_load(&upgrade.step) == PC5_UPGRADE_CANCELLED && file_is(file, "PC5 OLD EBOOT\n") && !pc5_exists(found),
          "stopped: %d %s", atomic_load(&upgrade.step), upgrade.error);

    /* The console stopped part way through putting files in place: the next start finishes it,
     * even when the drive comes back with another number (usb0 then, usb1 now). */
    {
        char work[PC5_PATH], usb1[PC5_PATH], moved[PC5_PATH], finished[32], command[4 * PC5_PATH], old_mark[48],
            new_mark[48], text[1024];
        path(work, sizeof(work), "%s/pc5-update", usb0);
        path(file, sizeof(file), "%s/pc5-update.txt", work);
        write_text(file, "PC5's update work folder.\n");
        path(file, sizeof(file), "%s/files.txt", work);
        snprintf(text, sizeof(text), "eboot.bin\t16\npc5/fonts.bin\t11\nLICENSE\t11\nsce_sys/param.json\t%zu\n"
                 "sce_sys/icon0.png\t10\n", strlen(new_files[1][1]));
        write_text(file, text);
        mark_of("PC5 OLD EBOOT\n", old_mark, sizeof(old_mark));
        mark_of("PC5 NEWER EBOOT\n", new_mark, sizeof(new_mark));
        snprintf(text, sizeof(text), "homebrew/PPSA11018\n98.0.0\n%s\n%s\n", old_mark, new_mark);
        path(file, sizeof(file), "%s/replacing.txt", work);
        write_text(file, text);
        path(file, sizeof(file), "%s/new/PPSA11018/eboot.bin", work);
        write_text(file, "PC5 NEWER EBOOT\n");
        path(file, sizeof(file), "%s/new/PPSA11018/sce_sys/icon0.png", work);
        write_text(file, "newer icon");
        /* fonts.bin, LICENSE and param.json were in place already; the eboot not yet. */
        path(file, sizeof(file), "%s/pc5/fonts.bin", folder);
        write_text(file, "newer fonts");
        /* A new update waits for it. */
        upgrade.config = config;
        atomic_store(&upgrade.cancel, 0);
        pc5_upgrade_run(&upgrade);
        path(file, sizeof(file), "%s/replacing.txt", work);
        CHECK(atomic_load(&upgrade.step) == PC5_UPGRADE_FAILED && strstr(upgrade.error, "isn't finished") &&
              pc5_is_file(file), "an unfinished update isn't thrown away: %s", upgrade.error);
        path(usb1, sizeof(usb1), "%s/usb1", base);
        snprintf(command, sizeof(command), "mv '%s' '%s'", usb0, usb1);
        run(command);
        const char *later[] = { data, usb1, NULL };
        Pc5UpgradeConfig next = config;
        next.drives = later;
        CHECK(pc5_upgrade_tidy(&next, finished, sizeof(finished)) == 1 && !strcmp(finished, "98.0.0"),
              "finished as PC5 opens, the eboot last: %s", finished);
        path(moved, sizeof(moved), "%s/homebrew/PPSA11018/eboot.bin", usb1);
        CHECK(file_is(moved, "PC5 NEWER EBOOT\n"), "the new eboot put in place");
        path(moved, sizeof(moved), "%s/homebrew/PPSA11018/pc5/fonts.bin", usb1);
        CHECK(file_is(moved, "newer fonts"), "what was in place stays");
        path(moved, sizeof(moved), "%s/pc5-update", usb1);
        CHECK(!pc5_exists(moved), "then the work folder deleted");
        path(moved, sizeof(moved), "%s/appmeta/PPSA11018/icon0.png", user);
        CHECK(file_is(moved, "newer icon"), "and the console's copies brought up to date");
        /* A journal it can't finish: everything kept for the next try. */
        path(work, sizeof(work), "%s/pc5-update", usb1);
        path(file, sizeof(file), "%s/pc5-update.txt", work);
        write_text(file, "PC5's update work folder.\n");
        path(file, sizeof(file), "%s/files.txt", work);
        write_text(file, "eboot.bin\t21\nmissing.txt\t3\n");
        mark_of("PC5 NEWER EBOOT\n", old_mark, sizeof(old_mark));
        mark_of("PC5 EVEN NEWER EBOOT\n", new_mark, sizeof(new_mark));
        snprintf(text, sizeof(text), "homebrew/PPSA11018\n98.0.1\n%s\n%s\n", old_mark, new_mark);
        path(file, sizeof(file), "%s/replacing.txt", work);
        write_text(file, text);
        path(file, sizeof(file), "%s/new/PPSA11018/eboot.bin", work);
        write_text(file, "PC5 EVEN NEWER EBOOT\n");
        CHECK(!pc5_upgrade_tidy(&next, finished, sizeof(finished)) && pc5_is_file(file), "kept when it can't finish");
        path(moved, sizeof(moved), "%s/homebrew/PPSA11018/eboot.bin", usb1);
        CHECK(file_is(moved, "PC5 NEWER EBOOT\n"), "and the eboot not replaced before the rest");
        /* PC5 installed again meanwhile (its eboot neither the old nor the new): the update is dropped. */
        write_text(moved, "PC5 INSTALLED BY HAND\n");
        CHECK(!pc5_upgrade_tidy(&next, finished, sizeof(finished)) && !pc5_exists(work) &&
              file_is(moved, "PC5 INSTALLED BY HAND\n"), "a PC5 installed again isn't touched");
        snprintf(command, sizeof(command), "mv '%s' '%s'", usb1, usb0);
        run(command);
    }
}

/* ---- updating with the update zip ------------------------------------------------------------- */

/* A release's files, and the list of them (pc5/manifest.txt) as tools/make-app.py writes it. */
typedef struct DeltaFile {
    const char *name, *text;
} DeltaFile;

static const DeltaFile old_release[] = {
    { "LICENSE", "licence v1" },
    { "eboot.bin", "PC5 OLD EBOOT\n" },
    { "pc5/fonts.bin", "the fonts" },
    { "pc5/stale.txt", "only in the old release" },
    { "sce_sys/param.json", "{\"titleId\":\"PPSA11018\",\"contentVersion\":\"01.009.000\"}" },
    { "win/wine/changed.dll", "old dll" },
    { "win/wine/same.dll", "the same dll" },
};
static const DeltaFile new_release[] = {
    { "LICENSE", "licence v2" },                       /* changed, the same size */
    { "eboot.bin", "PC5 DELTA EBOOT\n" },
    { "pc5/added.txt", "a new file" },
    { "pc5/fonts.bin", "the fonts" },
    { "sce_sys/param.json", "{\"titleId\":\"PPSA11018\",\"contentVersion\":\"01.009.000\"}" },
    { "win/wine/changed.dll", "a new dll" },
    { "win/wine/same.dll", "the same dll" },
};
/* What the update zip holds besides the list: what differs from the old release. */
static const char *const delta_names[] = { "LICENSE", "eboot.bin", "pc5/added.txt", "win/wine/changed.dll" };

static void text_sha256(const char *text, char *out)
{
    Pc5Sha256 sha;
    uint8_t digest[32];
    pc5_sha256_init(&sha);
    pc5_sha256_update(&sha, text, strlen(text));
    pc5_sha256_final(&sha, digest);
    hex(digest, out);
}

static void manifest_text(const DeltaFile *files, size_t count, char *out, size_t size)
{
    size_t at = (size_t)snprintf(out, size, "# PC5's files: name, size and SHA-256 of each.\n");
    for (size_t i = 0; i < count && at < size; i++) {
        char sha[65];
        text_sha256(files[i].text, sha);
        at += (size_t)snprintf(out + at, size - at, "%s\t%zu\t%s\n", files[i].name, strlen(files[i].text), sha);
    }
}

/* The new release's zip: every file (only: the update zip's, NULL for all), the list, and extra. */
static void make_delta_zip(const char *file, const char *const *only, size_t only_count, const char *changed_name,
                           const char *changed_text, const char *extra_name)
{
    Pc5Zip zip;
    char name[PC5_PATH], manifest[4096];
    CHECK(!pc5_zip_open(&zip, file), "a zip to update with");
    for (size_t i = 0; i < sizeof(new_release) / sizeof(new_release[0]); i++) {
        int take = !only;
        for (size_t j = 0; only && j < only_count; j++) take |= !strcmp(only[j], new_release[i].name);
        if (!take) continue;
        const char *text = changed_name && !strcmp(changed_name, new_release[i].name) ? changed_text : new_release[i].text;
        path(name, sizeof(name), "PPSA11018/%s", new_release[i].name);
        pc5_zip_add(&zip, name, text, strlen(text), 1792700000);
    }
    manifest_text(new_release, sizeof(new_release) / sizeof(new_release[0]), manifest, sizeof(manifest));
    pc5_zip_add(&zip, "PPSA11018/pc5/manifest.txt", manifest, strlen(manifest), 1792700000);
    if (extra_name) pc5_zip_add(&zip, extra_name, "extra", 5, 1792700000);
    CHECK(!pc5_zip_close(&zip), "the zip written");
}

/* Lines the updater logged, for what it says it did. */
static char delta_log[16384];
static char delta_trap[PC5_PATH];   /* set: the trash's place taken once the update zip is checked */
static void delta_note(void *context, const char *line)
{
    (void)context;
    size_t at = strlen(delta_log);
    snprintf(delta_log + at, sizeof(delta_log) - at, "%s\n", line);
    if (delta_trap[0] && strstr(line, "PC5 update the update zip holds")) write_text(delta_trap, "in the trash's way");
}

static ino_t inode_of(const char *file)
{
    struct stat st;
    return stat(file, &st) ? 0 : st.st_ino;
}

static void test_upgrade_delta(void)
{
    char base[PC5_PATH], app[PC5_PATH], data[PC5_PATH], usb0[PC5_PATH], folder[PC5_PATH], file[PC5_PATH],
        full[PC5_PATH], small[PC5_PATH], bad[PC5_PATH], full_digest[65], small_digest[65], json[4096],
        manifest[4096], command[3 * PC5_PATH];
    FakeNet net = { 0 };
    static Pc5Upgrade upgrade;
    struct stat full_st, small_st;
    const size_t delta_count = sizeof(delta_names) / sizeof(delta_names[0]);

    path(base, sizeof(base), "%s/delta", work);
    path(app, sizeof(app), "%s/app0", base);
    path(data, sizeof(data), "%s/data", base);
    path(usb0, sizeof(usb0), "%s/usb0", base);
    path(folder, sizeof(folder), "%s/homebrew/PPSA11018", usb0);
    path(full, sizeof(full), "%s/release/PPSA11018.zip", base);
    path(small, sizeof(small), "%s/release/PPSA11018-update.zip", base);
    path(bad, sizeof(bad), "%s/release/bad-update.zip", base);
    const char *drives[] = { data, usb0, NULL };
    Pc5UpgradeConfig config = { { &net, fake_get, fake_download, fake_get_authorized }, app, NULL, drives, NULL, delta_note, NULL };

    /* The running PC5 is the old release, with its list. */
    for (size_t i = 0; i < sizeof(old_release) / sizeof(old_release[0]); i++) {
        path(file, sizeof(file), "%s/%s", app, old_release[i].name);
        write_text(file, old_release[i].text);
    }
    manifest_text(old_release, sizeof(old_release) / sizeof(old_release[0]), manifest, sizeof(manifest));
    path(file, sizeof(file), "%s/pc5/manifest.txt", app);
    write_text(file, manifest);
    pc5_mkdirs(data);
    path(file, sizeof(file), "%s/release", base);
    pc5_mkdirs(file);
    make_delta_zip(full, NULL, 0, NULL, NULL, NULL);
    make_delta_zip(small, delta_names, delta_count, NULL, NULL, NULL);
    file_sha256(full, full_digest);
    file_sha256(small, small_digest);
    stat(full, &full_st);
    stat(small, &small_st);
    release_json_with(json, sizeof(json), "v99.0.0", (uint64_t)full_st.st_size, full_digest, (uint64_t)small_st.st_size,
                      small_digest, TEST_UPDATE_URL);
    net.release = json;
    pc5_copy(net.zip, sizeof(net.zip), full);
    pc5_copy(net.update_zip, sizeof(net.update_zip), small);
    upgrade.config = config;

    /* The update zip and the files on the console make the new release: only it is downloaded. */
    make_installed(app, folder);
    char same[PC5_PATH], fonts[PC5_PATH];
    path(same, sizeof(same), "%s/win/wine/same.dll", folder);
    path(fonts, sizeof(fonts), "%s/pc5/fonts.bin", folder);
    ino_t same_inode = inode_of(same), fonts_inode = inode_of(fonts);
    path(file, sizeof(file), "%s/mystuff/keep.txt", folder);
    write_text(file, "mine too");
    delta_log[0] = 0;
    pc5_upgrade_run(&upgrade);
    CHECK(atomic_load(&upgrade.step) == PC5_UPGRADE_DONE && net.update_downloads == 1 && net.zip_downloads == 0,
          "updated from the update zip alone: %d %s (%d %d)", atomic_load(&upgrade.step), upgrade.error,
          net.update_downloads, net.zip_downloads);
    CHECK(strstr(delta_log, "PC5 update using PPSA11018-update.zip size=") && !strstr(delta_log, "using PPSA11018.zip"),
          "the log names the zip used: %s", delta_log);
    {
        /* Only the folders it changed are flushed, not every one of the release's. */
        const char *at = strstr(delta_log, "flushed=");
        unsigned flushed = 0, of = 0;
        CHECK(at && sscanf(at, "flushed=%u of %u", &flushed, &of) == 2 && flushed < of,
              "only the folders it changed flushed: %s", at ? at : delta_log);
    }
    for (size_t i = 0; i < sizeof(new_release) / sizeof(new_release[0]); i++) {
        path(file, sizeof(file), "%s/%s", folder, new_release[i].name);
        CHECK(file_is(file, new_release[i].text), "the new release's %s in place", new_release[i].name);
    }
    CHECK(inode_of(same) == same_inode && inode_of(fonts) == fonts_inode, "the unchanged files untouched");
    manifest_text(new_release, sizeof(new_release) / sizeof(new_release[0]), manifest, sizeof(manifest));
    path(file, sizeof(file), "%s/pc5/manifest.txt", folder);
    CHECK(file_is(file, manifest), "the new list in place");
    path(file, sizeof(file), "%s/pc5/stale.txt", folder);
    CHECK(!pc5_exists(file), "a file the new release doesn't have is gone");
    path(file, sizeof(file), "%s/notes.txt", folder);
    CHECK(file_is(file, "mine"), "the player's file at the top stays");
    path(file, sizeof(file), "%s/mystuff/keep.txt", folder);
    CHECK(file_is(file, "mine too"), "and the player's folder");
    path(file, sizeof(file), "%s/pc5-update", usb0);
    CHECK(!pc5_exists(file), "the work folder deleted");

    /* Each way the update zip can't be used: the whole app instead, which updates as before. */
    for (int kind = 0; kind < 5; kind++) {
        static const char *const what[] = { "no installed list", "an installed file of another size",
                                            "the lists differ for a file the update zip hasn't",
                                            "a file in the update zip that isn't the list's",
                                            "a file in the update zip the list doesn't have" };
        make_installed(app, folder);
        pc5_copy(net.update_zip, sizeof(net.update_zip), small);
        if (kind == 0) {
            path(file, sizeof(file), "%s/pc5/manifest.txt", folder);
            unlink(file);
        } else if (kind == 1) {
            write_text(same, "the same dll, longer");
        } else if (kind == 2) {
            /* the console's list says same.dll is other bytes than the new release's */
            Pc5Buf text = { 0 };
            char sha[65], *at;
            path(file, sizeof(file), "%s/pc5/manifest.txt", folder);
            pc5_read_file(file, &text, 1u << 20);
            text_sha256("the same dll", sha);
            if ((at = strstr((char *)text.data, sha))) memset(at, '0', 64);
            pc5_write_file(file, text.data, text.len);
            pc5_buf_free(&text);
        } else {
            if (kind == 3) make_delta_zip(bad, delta_names, delta_count, "LICENSE", "licence v3", NULL);
            else make_delta_zip(bad, delta_names, delta_count, NULL, NULL, "PPSA11018/pc5/extra.txt");
            struct stat bad_st;
            char bad_digest[65];
            stat(bad, &bad_st);
            file_sha256(bad, bad_digest);
            release_json_with(json, sizeof(json), "v99.0.0", (uint64_t)full_st.st_size, full_digest,
                              (uint64_t)bad_st.st_size, bad_digest, TEST_UPDATE_URL);
            pc5_copy(net.update_zip, sizeof(net.update_zip), bad);
        }
        net.zip_downloads = net.update_downloads = 0;
        delta_log[0] = 0;
        pc5_upgrade_run(&upgrade);
        path(file, sizeof(file), "%s/eboot.bin", folder);
        CHECK(atomic_load(&upgrade.step) == PC5_UPGRADE_DONE && file_is(file, "PC5 DELTA EBOOT\n") &&
              net.zip_downloads == 1 && net.update_downloads == (kind == 0 ? 0 : 1),
              "%s: the whole app: %d %s (%d %d)", what[kind], atomic_load(&upgrade.step), upgrade.error,
              net.update_downloads, net.zip_downloads);
        CHECK(file_is(same, "the same dll") && file_is(fonts, "the fonts"), "%s: every file the new one's", what[kind]);
        path(file, sizeof(file), "%s/pc5/manifest.txt", folder);
        CHECK(file_is(file, manifest), "%s: the new list", what[kind]);
        CHECK(strstr(delta_log, "PC5 update using PPSA11018.zip size=") &&
              (kind == 0 || strstr(delta_log, "can't use the update zip")), "%s: said why: %s", what[kind], delta_log);
        release_json_with(json, sizeof(json), "v99.0.0", (uint64_t)full_st.st_size, full_digest,
                          (uint64_t)small_st.st_size, small_digest, TEST_UPDATE_URL);
    }
    pc5_copy(net.update_zip, sizeof(net.update_zip), small);

    /* More than one release behind (the update zip holds the changes since another): the
     * whole app at once, the update zip not downloaded. */
    make_installed(app, folder);
    update_from = "1.0.0";
    release_json_with(json, sizeof(json), "v99.0.0", (uint64_t)full_st.st_size, full_digest,
                      (uint64_t)small_st.st_size, small_digest, TEST_UPDATE_URL);
    net.zip_downloads = net.update_downloads = 0;
    delta_log[0] = 0;
    pc5_upgrade_run(&upgrade);
    path(file, sizeof(file), "%s/eboot.bin", folder);
    CHECK(atomic_load(&upgrade.step) == PC5_UPGRADE_DONE && file_is(file, "PC5 DELTA EBOOT\n") &&
              net.zip_downloads == 1 && net.update_downloads == 0 && strstr(delta_log, "the changes since 1.0.0"),
          "more than one release behind: the whole app only: %d %s (%d %d) %s", atomic_load(&upgrade.step), upgrade.error,
          net.update_downloads, net.zip_downloads, delta_log);
    update_from = PC5_VERSION;
    release_json_with(json, sizeof(json), "v99.0.0", (uint64_t)full_st.st_size, full_digest,
                      (uint64_t)small_st.st_size, small_digest, TEST_UPDATE_URL);

    /* A download of the update zip that fails only falls back. */
    make_installed(app, folder);
    net.zip_downloads = net.update_downloads = 0;
    net.update_zip[0] = 0;   /* GitHub answers 404 */
    pc5_upgrade_run(&upgrade);
    CHECK(atomic_load(&upgrade.step) == PC5_UPGRADE_DONE && net.zip_downloads == 1 && net.update_downloads == 1,
          "the update zip not found: the whole app: %s", upgrade.error);
    pc5_copy(net.update_zip, sizeof(net.update_zip), small);

    /* Stopped while the update zip downloads: stopped, not the whole app. */
    make_installed(app, folder);
    net.zip_downloads = net.update_downloads = 0;
    net.gate = 1;
    net.gate_bytes = 100;
    net.at_gate = 0;
    CHECK(!pc5_upgrade_start(&upgrade, &config), "the worker started");
    for (int i = 0; i < 20000 && !net.at_gate; i++) usleep(1000);
    pc5_upgrade_cancel(&upgrade);
    net.gate = 0;
    wait_upgrade(&upgrade);
    path(file, sizeof(file), "%s/eboot.bin", folder);
    CHECK(atomic_load(&upgrade.step) == PC5_UPGRADE_CANCELLED && net.zip_downloads == 0 && file_is(file, "PC5 OLD EBOOT\n"),
          "stopped while the update zip downloads: %d %s", atomic_load(&upgrade.step), upgrade.error);
    net.gate_bytes = 0;

    /* The console stopped while files from the update zip were being put in place (here: the
     * trash's place taken once the zip is checked, so a file in a new file's way can't go):
     * the next start finishes it, the unchanged files counted as in place. */
    make_installed(app, folder);
    upgrade.config = config;
    atomic_store(&upgrade.cancel, 0);
    snprintf(command, sizeof(command), "rm -f '%s/win/wine/changed.dll' && mkdir -p '%s/win/wine/changed.dll/sub'", folder,
             folder);
    run(command);
    path(delta_trap, sizeof(delta_trap), "%s/pc5-update/old", usb0);
    pc5_upgrade_run(&upgrade);
    delta_trap[0] = 0;
    path(file, sizeof(file), "%s/eboot.bin", folder);
    char journal[PC5_PATH], licence[PC5_PATH], finished[32];
    path(journal, sizeof(journal), "%s/pc5-update/replacing.txt", usb0);
    path(licence, sizeof(licence), "%s/LICENSE", folder);
    CHECK(atomic_load(&upgrade.step) == PC5_UPGRADE_FAILED && upgrade.partial && pc5_is_file(journal) &&
          file_is(file, "PC5 OLD EBOOT\n") && file_is(licence, "licence v2"),
          "stopped part way: %d %s", atomic_load(&upgrade.step), upgrade.error);
    path(file, sizeof(file), "%s/pc5-update/old", usb0);
    unlink(file);
    CHECK(pc5_upgrade_tidy(&config, finished, sizeof(finished)) == 1 && !strcmp(finished, "99.0.0"),
          "finished as PC5 opens: %s", finished);
    for (size_t i = 0; i < sizeof(new_release) / sizeof(new_release[0]); i++) {
        path(file, sizeof(file), "%s/%s", folder, new_release[i].name);
        CHECK(file_is(file, new_release[i].text), "then the new release's %s in place", new_release[i].name);
    }
    path(file, sizeof(file), "%s/pc5/manifest.txt", folder);
    CHECK(file_is(file, manifest), "and its list");
    path(file, sizeof(file), "%s/pc5/stale.txt", folder);
    CHECK(!pc5_exists(file), "and the stale file gone");
    path(file, sizeof(file), "%s/pc5-update", usb0);
    CHECK(!pc5_exists(file), "and the work folder");
}

static void test_sound(void)
{
    uint8_t wav[44 + 8];
    Pc5Sound sound;
    const int16_t samples[4] = { 1000, -1000, 32767, -32768 };
    memcpy(wav, "RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x01\0\x80\xbb\0\0\0\x77\x01\0\x02\0\x10\0data\x08\0\0\0", 44);
    memcpy(wav + 44, samples, 8);
    CHECK(!pc5_sound_from_wav(wav, sizeof(wav), &sound) && sound.frames == 4 && sound.samples[2] == 32767 &&
          sound.samples[3] == -32768, "a mono WAV: %zu", sound.frames);
    pc5_sound_free(&sound);
    wav[22] = 2;   /* stereo: two frames, mixed */
    CHECK(!pc5_sound_from_wav(wav, sizeof(wav), &sound) && sound.frames == 2 && sound.samples[0] == 0 &&
          sound.samples[1] == 0, "a stereo WAV, mixed to one: %d %d", sound.frames ? sound.samples[0] : -1,
          sound.frames > 1 ? sound.samples[1] : -1);
    pc5_sound_free(&sound);
    wav[22] = 1;
    wav[24] = 0xc0;   /* 24000 Hz: each sample twice, in between halfway */
    wav[25] = 0x5d;
    CHECK(!pc5_sound_from_wav(wav, sizeof(wav), &sound) && sound.frames == 8 && sound.samples[0] == 1000 &&
          sound.samples[1] == 0 && sound.samples[2] == -1000, "made 48 kHz: %zu %d %d %d", sound.frames,
          sound.frames ? sound.samples[0] : 0, sound.frames > 1 ? sound.samples[1] : 0, sound.frames > 2 ? sound.samples[2] : 0);
    pc5_sound_free(&sound);
    wav[24] = 0x80;   /* 48000 again */
    wav[25] = 0xbb;
    wav[34] = 24;     /* 24-bit: the first frame's bytes e8 03 18, its top 16 bits 0x1803 */
    CHECK(!pc5_sound_from_wav(wav, sizeof(wav), &sound) && sound.frames == 2 && sound.samples[0] == 0x1803,
          "24-bit: %d", sound.frames ? sound.samples[0] : 0);
    pc5_sound_free(&sound);
    {
        /* 32-bit float, one frame of 0.5 */
        uint8_t f[44 + 4];
        float half = 0.5f;
        memcpy(f, wav, 44);
        f[20] = 3;
        f[34] = 32;
        f[40] = 4;
        memcpy(f + 44, &half, 4);
        CHECK(!pc5_sound_from_wav(f, sizeof(f), &sound) && sound.frames == 1 && sound.samples[0] == 16383, "float: %d",
              sound.frames ? sound.samples[0] : 0);
        pc5_sound_free(&sound);
    }
    wav[34] = 12;
    CHECK(pc5_sound_from_wav(wav, sizeof(wav), &sound) && !sound.samples, "12-bit isn't read");
    CHECK(pc5_sound_from_wav((const uint8_t *)"RIFF", 4, &sound), "not a WAV");
    /* The sounds to choose from, and their files: the player's of a name first. */
    {
        char app[PC5_PATH], own[PC5_PATH], file[PC5_PATH];
        Pc5SoundList list;
        path(app, sizeof(app), "%s/sounds-app", work);
        path(own, sizeof(own), "%s/sounds-root", work);
        path(file, sizeof(file), "%s/pc5/sounds/PC5.wav", app);
        write_text(file, "app");
        path(file, sizeof(file), "%s/pc5/sounds/Retro Beep.WAV", app);
        write_text(file, "app");
        path(file, sizeof(file), "%s/sounds/pc5.wav", own);
        write_text(file, "own");
        path(file, sizeof(file), "%s/sounds/Mine.wav", own);
        write_text(file, "own");
        path(file, sizeof(file), "%s/sounds/notes.txt", own);
        write_text(file, "not a sound");
        pc5_sound_list(app, own, &list);
        CHECK(list.count == 3 && !strcmp(list.name[0], "Mine") && !strcmp(list.name[1], "PC5") &&
              !strcmp(list.name[2], "Retro Beep"), "the list: %d %s", list.count, list.count ? list.name[0] : "");
        CHECK(!pc5_sound_path(app, own, "PC5", file, sizeof(file)) && file_is(file, "own"), "the player's of a name first");
        CHECK(!pc5_sound_path(app, own, "retro beep", file, sizeof(file)) && file_is(file, "app"), "any case");
        CHECK(pc5_sound_path(app, own, "Gone", file, sizeof(file)) && pc5_sound_path(app, own, "../x", file, sizeof(file)),
              "none of that name");
    }
    /* The app's own, each read, the default among them. */
    static const char *const own[] = { "Beeper - Push", "Cha-Ching", "System Activated", "cheap chinese speaker", "fahh",
                                       "funny message tone", "rizz" };
    for (size_t i = 0; i < sizeof(own) / sizeof(own[0]); i++) {
        char file[PC5_PATH];
        Pc5Buf data = { 0 };
        path(file, sizeof(file), "%s/../../art/sounds/%s.wav", fixtures, own[i]);
        CHECK(!pc5_read_file(file, &data, 64u << 20) && !pc5_sound_from_wav(data.data, data.len, &sound) &&
              sound.frames > 4800 && sound.frames <= 48000 * PC5_SOUND_SECONDS, "art/sounds/%s.wav: %zu frames", own[i],
              sound.frames);
        pc5_sound_free(&sound);
        pc5_buf_free(&data);
    }
    CHECK(!strcmp(PC5_SOUND_DEFAULT, "cheap chinese speaker"), "the default is one of them");
}


/* ---- the games' backgrounds --------------------------------------------------------------------- */

static void test_backgrounds(void)
{
    char base[PC5_PATH], file[PC5_PATH], own[PC5_PATH];
    FakeNet net = { 0 };
    Pc5Http http = { &net, fake_get, NULL, fake_get_authorized };
    Pc5Image picture = { 0 }, background = { 0 };
    static Pc5Backgrounds b;
    uint32_t tint;

    /* A wide picture made 16:9, filling it. */
    CHECK(!pc5_image_new(&picture, 3840, 1240), "a hero picture");
    for (int i = 0; i < 3840 * 1240; i++) picture.px[i] = 0xff204060u;
    CHECK(!pc5_background_compose(&picture, &background) && background.w == PC5_BACKGROUND_WIDTH &&
          background.h == PC5_BACKGROUND_HEIGHT && background.px[0] == 0xff204060u, "16:9 at 960x540");
    pc5_image_free(&picture);
    pc5_image_free(&background);
    /* Steam's hero, by its plain name or (here) the asset list's; GOG's background. */
    CHECK(!pc5_background_fetch(NULL, 901001, &http, &background) && background.w == 960, "Steam's hero from the asset list");
    pc5_image_free(&background);
    CHECK(!pc5_background_fetch("https://images.gog-statics.com/x.jpg", 0, &http, &background), "GOG's background");
    pc5_image_free(&background);
    CHECK(pc5_background_fetch("https://example.com/none.jpg", 0, &http, &background), "none there");
    {
        Pc5CoverSearch search;
        Pc5CoverHint hint = { 0 };
        pc5_copy(hint.folder, sizeof(hint.folder), "Neon Drift");
        pc5_cover_search_start(&search, &hint);
        while (pc5_cover_search_step(&search, &http)) {
        }
        int steam = 0;
        for (int i = 0; i < search.count; i++)
            if (!strcmp(search.choices[i].source, "Steam") && strstr(search.choices[i].background, "/library_hero.jpg")) steam = 1;
        CHECK(steam, "a Steam choice comes with its hero");
    }

    /* The worker: three games, one with the player's own picture, one found on Steam, one nowhere. */
    path(base, sizeof(base), "%s/backgrounds-root", work);
    Pc5BackgroundGame *games = pc5_alloc(sizeof(*games) * 3);
    const char *const titles[3] = { "Own Picture", "Neon Drift", "Zzzq Nothing" };
    for (int i = 0; i < 3; i++) {
        snprintf(games[i].id, sizeof(games[i].id), "game%d", i);
        snprintf(games[i].prefix, sizeof(games[i].prefix), "game%d", i);
        pc5_copy(games[i].title, sizeof(games[i].title), titles[i]);
        pc5_copy(games[i].hint.folder, sizeof(games[i].hint.folder), titles[i]);
        path(file, sizeof(file), "%s/prefixes/game%d/system.reg", base, i);
        write_text(file, "registry");
    }
    path(own, sizeof(own), "%s/backgrounds/own picture.png", base);
    {
        char dir[PC5_PATH];
        path(dir, sizeof(dir), "%s/backgrounds", base);
        pc5_mkdirs(dir);
        write_png(own, 64, 36, 0xff11aa33u);
    }
    CHECK(pc5_background_needed(base, &games[0]) && pc5_background_needed(base, &games[1]), "all need one");
    memset(&b, 0, sizeof(b));
    pc5_copy(b.root, sizeof(b.root), base);
    b.http = http;
    b.games = games;
    b.count = 3;
    pc5_backgrounds_run(&b);
    path(file, sizeof(file), "%s/prefixes/game0/pc5/background.bin", base);
    CHECK(!pc5_cover_load(file, &background, &tint) && background.w == 960 && background.px[0] == 0xff11aa33u,
          "the player's own picture");
    pc5_image_free(&background);
    path(file, sizeof(file), "%s/prefixes/game1/pc5/background.bin", base);
    CHECK(!pc5_cover_load(file, &background, &tint) && background.w == 960, "found on Steam");
    pc5_image_free(&background);
    path(file, sizeof(file), "%s/prefixes/game2/pc5/background-none.txt", base);
    CHECK(pc5_is_file(file) && !pc5_background_needed(base, &games[2]), "none anywhere: not asked again");
    CHECK(atomic_load(&b.generation) == 2, "two saved: %u", atomic_load(&b.generation));
    CHECK(!pc5_background_needed(base, &games[0]) && !pc5_background_needed(base, &games[1]), "done");
    /* A picture of the player's for the game with none: used at the next look. */
    path(own, sizeof(own), "%s/backgrounds/game2.jpg", base);
    write_png(own, 32, 18, 0xff3344ffu);   /* a PNG named .jpg: read for what it is */
    CHECK(pc5_background_needed(base, &games[2]), "the player's picture is looked at");
    /* Every source turned off: nothing asked and no note (looked for again once one is on). */
    {
        Pc5Backgrounds quiet;
        Pc5BackgroundGame *off = pc5_alloc(sizeof(*off));
        pc5_copy(off->id, sizeof(off->id), "game9");
        pc5_copy(off->prefix, sizeof(off->prefix), "game9");
        pc5_copy(off->title, sizeof(off->title), "Neon Drift");
        pc5_copy(off->hint.folder, sizeof(off->hint.folder), "Neon Drift");
        off->hint.off = (1u << PC5_PICTURE_SOURCES) - 1;
        path(file, sizeof(file), "%s/prefixes/game9/system.reg", base);
        write_text(file, "registry");
        memset(&quiet, 0, sizeof(quiet));
        pc5_copy(quiet.root, sizeof(quiet.root), base);
        quiet.http = http;
        quiet.games = off;
        quiet.count = 1;
        int asked = net.requests;
        pc5_backgrounds_run(&quiet);
        path(file, sizeof(file), "%s/prefixes/game9/pc5/background-none.txt", base);
        CHECK(net.requests == asked && !pc5_exists(file) && pc5_background_needed(base, off), "all off: nothing asked, no note");
        pc5_free(off);
    }
    /* On a thread, as the screens start it. */
    CHECK(!pc5_backgrounds_start(&b, games, 3), "the worker started");
    pc5_backgrounds_end(&b, 0);
    path(file, sizeof(file), "%s/prefixes/game2/pc5/background.bin", base);
    CHECK(!pc5_cover_load(file, &background, &tint) && background.px[0] == 0xff3344ffu, "and used");
    pc5_image_free(&background);
    path(file, sizeof(file), "%s/prefixes/game2/pc5/background-none.txt", base);
    CHECK(!pc5_exists(file), "its none note gone");
    /* None chosen in Appearance after the player's picture: it stays none. (The games went with
     * the worker: game2 again.) */
    {
        char saved[PC5_PATH];
        struct timespec old[2] = { { 1700000000, 0 }, { 1700000000, 0 } };
        Pc5BackgroundGame again = { 0 };
        pc5_copy(again.id, sizeof(again.id), "game2");
        pc5_copy(again.prefix, sizeof(again.prefix), "game2");
        path(saved, sizeof(saved), "%s/prefixes/game2/pc5/background.bin", base);
        unlink(saved);
        write_text(file, "none\n");
        CHECK(!utimensat(AT_FDCWD, own, old, 0) && !pc5_background_needed(base, &again), "None chosen: kept");
        write_png(own, 32, 18, 0xff3344ffu);   /* a newer picture of the player's: used again */
        CHECK(pc5_background_needed(base, &again), "a newer picture: looked at");
        unlink(own);
    }
    /* A game whose Windows went (uninstalled while the worker looked): nothing made for it. */
    {
        Pc5BackgroundGame *gone = pc5_alloc(sizeof(*gone));   /* games went with the worker */
        pc5_copy(gone->title, sizeof(gone->title), "Neon Drift");
        pc5_copy(gone->hint.folder, sizeof(gone->hint.folder), "Neon Drift");
        snprintf(gone->prefix, sizeof(gone->prefix), "gone");
        snprintf(gone->id, sizeof(gone->id), "gone");
        memset(&b, 0, sizeof(b));
        pc5_copy(b.root, sizeof(b.root), base);
        b.http = http;
        b.games = gone;
        b.count = 1;
        pc5_backgrounds_run(&b);
        pc5_free(gone);
        path(file, sizeof(file), "%s/prefixes/gone", base);
        CHECK(!pc5_exists(file) && atomic_load(&b.generation) == 0, "no folder made for a game that is gone");
    }
    /* Steam has the game's cover but no background: SteamGridDB's, with a key; without one, none
     * yet (looked for again, as Steam's could be fetched another time). */
    for (int keyed = 1; keyed >= 0; keyed--) {
        Pc5BackgroundGame *one = pc5_alloc(sizeof(*one));
        memset(one, 0, sizeof(*one));
        pc5_copy(one->title, sizeof(one->title), "Neon Drift");
        pc5_copy(one->hint.folder, sizeof(one->hint.folder), "Neon Drift");
        if (keyed) pc5_copy(one->hint.steamgriddb_key, sizeof(one->hint.steamgriddb_key), TEST_GRIDDB_KEY);
        snprintf(one->prefix, sizeof(one->prefix), "griddb%d", keyed);
        snprintf(one->id, sizeof(one->id), "griddb%d", keyed);
        path(file, sizeof(file), "%s/prefixes/griddb%d/system.reg", base, keyed);
        write_text(file, "registry");
        memset(&b, 0, sizeof(b));
        pc5_copy(b.root, sizeof(b.root), base);
        net.no_steam_heroes = 1;
        b.http = http;
        b.games = one;
        b.count = 1;
        pc5_backgrounds_run(&b);
        net.no_steam_heroes = 0;
        pc5_free(one);
        path(file, sizeof(file), "%s/prefixes/griddb%d/pc5/background.bin", base, keyed);
        if (keyed) {
            CHECK(!pc5_cover_load(file, &background, &tint) && background.w == 960 && strstr(net.last, "cdn2.steamgriddb.com/hero/"),
                  "SteamGridDB's background for a game Steam has none for: %s", net.last);
            pc5_image_free(&background);
        } else {
            CHECK(!pc5_exists(file), "none without a key");
        }
    }
}

/* The library with the chosen game's background behind it. */
static void test_background_screen(const char *fonts)
{
    FakeConsole console = { 0 };
    FakeNet net = { 0 };
    Pc5Platform platform = { &console, { &net, fake_get, NULL, fake_get_authorized }, fake_clock, fake_play, NULL, fake_notify, fake_time, 0,
                             NULL, NULL, NULL, NULL, NULL };
    const char *roots[] = { usb, NULL };
    Pc5Canvas canvas = { pc5_alloc(1920u * 1080u * 4u), 1920, 1080, 1920, 0, 0 };
    Pc5Library library;
    Pc5Image red = { 0 };
    char file[PC5_PATH];

    pc5_library_load(root, &library);
    CHECK(library.count > 0, "a game for a background");
    if (!library.count || !canvas.px) { pc5_free(canvas.px); return; }
    pc5_image_new(&red, PC5_BACKGROUND_WIDTH, PC5_BACKGROUND_HEIGHT);
    for (int i = 0; i < red.w * red.h; i++) red.px[i] = 0xffcc2222u;
    pc5_background_path(root, library.games[0].prefix, file, sizeof(file));
    pc5_cover_save(file, &red, 0);
    pc5_image_free(&red);
    {
        Pc5UpdateMemory memory;
        memset(&memory, 0, sizeof(memory));
        memory.checked = 1792700000;   /* no update check now */
        pc5_update_save(root, &memory);
    }
    Pc5UiConfig config = { root, template_prefix, fonts, roots, NULL, NULL, NULL, NULL, NULL, NULL, 0, 0 };
    Pc5Ui *ui = pc5_ui_create(&config, &platform);
    until_screen(ui, "library", &canvas);
    for (int i = 0; i < 12; i++) press(ui, PC5_KEY_LEFT, &canvas);   /* the first game */
    frames(ui, 0, 0, 60, &canvas);
    pc5_ui_draw(ui, &canvas);
    save_shot(&canvas, "library-background");
    uint32_t p = canvas.px[500 * 1920 + 100];
    int r = (int)(p >> 16 & 255), g = (int)(p >> 8 & 255);
    CHECK(r > 50 && r > g + 30, "the background behind the library: %08x", p);
    pc5_ui_destroy(ui);
    unlink(file);
    pc5_free(canvas.px);
}

/* The names in a zip's directory, one per line (its central directory, read as unzip does). */
static int zip_names(const char *file, char *out, size_t size)
{
    Pc5Buf zip = { 0 };
    size_t n = 0;
    out[0] = 0;
    if (pc5_read_file(file, &zip, 64u << 20) || zip.len < 22) { pc5_buf_free(&zip); return -1; }
    const uint8_t *z = zip.data, *end = z + zip.len - 22;
    while (end > z && !(end[0] == 0x50 && end[1] == 0x4b && end[2] == 5 && end[3] == 6)) end--;
    uint32_t count = end[10] | end[11] << 8, at = end[16] | end[17] << 8 | end[18] << 16 | (uint32_t)end[19] << 24;
    for (uint32_t i = 0; i < count && at + 46 <= zip.len; i++) {
        const uint8_t *c = z + at;
        uint32_t name = c[28] | c[29] << 8, extra = c[30] | c[31] << 8, comment = c[32] | c[33] << 8;
        if (c[0] != 0x50 || c[1] != 0x4b || c[2] != 1 || c[3] != 2 || at + 46 + name > zip.len || n + name + 2 > size) break;
        memcpy(out + n, c + 46, name);
        n += name;
        out[n++] = '\n';
        out[n] = 0;
        at += 46 + name + extra + comment;
    }
    pc5_buf_free(&zip);
    return 0;
}

static void test_zip(void)
{
    char file[PC5_PATH], names[512];
    Pc5Zip zip;
    static uint8_t noise[3000], text[40000];
    for (size_t i = 0; i < sizeof(noise); i++) noise[i] = (uint8_t)((i * 2654435761u) >> 13);
    for (size_t i = 0; i < sizeof(text); i++) text[i] = (uint8_t)"PC5 log line, repeated.\n"[i % 24];
    CHECK(pc5_crc32("123456789", 9) == 0xcbf43926u, "CRC-32 of the check string");
    path(file, sizeof(file), "%s/zip-test.zip", work);
    CHECK(!pc5_zip_open(&zip, file), "zip opened");
    CHECK(!pc5_zip_add(&zip, "text.log", text, sizeof(text), 1792700000) &&
          !pc5_zip_add(&zip, "game/users/prospero/noise.dmp", noise, sizeof(noise), 1792700000) &&
          !pc5_zip_add(&zip, "empty.txt", "", 0, 0) && !pc5_zip_add(&zip, "k\xc3\xa4ytt\xc3\xa4j\xc3\xa4.txt", "x", 1, 1792700000),
          "four files added");
    CHECK(!pc5_zip_close(&zip) && pc5_is_file(file), "zip written (tests/test.sh opens it with Python)");
    path(names, sizeof(names), "%s/zip-test.zip.part", work);
    CHECK(!pc5_exists(names), "no part left behind");
    CHECK(!zip_names(file, names, sizeof(names)) && strstr(names, "text.log\n") && strstr(names, "noise.dmp\n"),
          "its directory: %s", names);
    struct stat st;
    CHECK(!stat(file, &st) && st.st_size < 4000 + 2000, "the log line deflated: %lld bytes", (long long)st.st_size);
    path(file, sizeof(file), "%s/no-such-dir/x.zip", work);
    CHECK(pc5_zip_open(&zip, file) == -1, "a zip that can't be written");
}

static void log_file(const char *dir, const char *name, const char *profile, long long time, const char *body)
{
    char file[PC5_PATH], text[4096];
    snprintf(text, sizeof(text), "PW_REPORT/1 build=abc profile=%s cycle=2 pid=276 time=%lld\n%s", profile, time, body);
    path(file, sizeof(file), "%s/%s", dir, name);
    write_text(file, text);
}

static void file_at(const char *file, const char *text, long long time)
{
    char dir[PC5_PATH];
    pc5_copy(dir, sizeof(dir), file);
    *strrchr(dir, '/') = 0;
    pc5_mkdirs(dir);
    write_text(file, text);
    struct timeval at[2] = { { (time_t)time, 0 }, { (time_t)time, 0 } };
    utimes(file, at);
}

static void test_logs(void)
{
    char root_dir[PC5_PATH], logs[PC5_PATH], file[PC5_PATH], names[4096], drive[PC5_PATH];
    const long long now = 1792700000;
    Pc5LogIssue issue;
    const char *crash =
        "REC seq=1 t=100.000 PW_WINE64 args argc=3 mode=game profile=gtaiv\n"
        "REC seq=2 t=100.100 PW_WINE64 profile id=gtaiv prefix=/data/prospero-win/prefixes/gtaiv desktop=1920x1080\n"
        "REC seq=3 t=100.200 PW_WINE64 ntdll=/app0/ntdll.prx prefix=/data/prospero-win/prefixes/gtaiv exe=C:\\Games\\GTA IV\\GTAIV.exe\n"
        "REC seq=4 t=100.300 PC5 system firmware=12.40 data_free=200GB\n"
        "REC seq=5 t=100.400 WINE wine: Unhandled page fault on read access to 0000002C at address 7985D308 (thread 0024), starting debugger...\n"
        "REC seq=6 t=130.500 PW_WINE64 session_end reason=wine-exit\n";
    const char *played =
        "REC seq=1 t=100.000 PW_WINE64 args argc=3 mode=game profile=blur\n"
        "REC seq=2 t=130.000 PW_WINE64 display released to vulkan close=ok shown=0\n"
        "REC seq=3 t=900.000 PW_WINE64 alive tick=46800 sink_calls=1 frames_put=2 shown=0 rejected=0 vk_shown=40000\n"
        "REC seq=4 t=901.000 PW_WINE64 session_end reason=wine-exit\n";
    const char *quick =
        "REC seq=1 t=100.000 PW_WINE64 args argc=3 mode=game profile=quick\n"
        "REC seq=2 t=104.000 PW_WINE64 alive tick=60 sink_calls=1 frames_put=0 shown=0 rejected=0 vk_shown=0\n"
        "REC seq=3 t=105.000 PW_WINE64 session_end reason=wine-exit\n";
    const char *pc5_fine =
        "REC seq=1 t=10.000 PC5 start version=1.0.0\nREC seq=2 t=50.000 PC5 play id=blur\n"
        "REC seq=3 t=50.100 PW_WINE64 session_end reason=pc5\n";
    const char *pc5_problem =
        "REC seq=1 t=10.000 PC5 start version=1.0.0\n"
        "REC seq=2 t=20.000 PC5 problem install Blur: Not enough space\n";
    const char *handed =
        "REC seq=1 t=10.000 WINE wine: Unhandled page fault on read access to 0000000000000000 at address 000000014001D11A "
        "(thread 0024), handed to the program's own crash handler\n"
        "REC seq=2 t=12.000 PW_WINE64 alive tick=60 sink_calls=1 frames_put=2 shown=1\n";

    char text[4096];
#define CLASSIFY(profile, body) (snprintf(text, sizeof(text), "PW_REPORT/1 build=abc profile=%s cycle=2 pid=1 time=5\n%s", \
                                          profile, body), pc5_log_classify(text, strlen(text), &issue))
    CHECK(CLASSIFY("gtaiv", crash) == PC5_LOG_GAME_CRASHED, "a crash");
    CHECK(!strcmp(issue.profile, "gtaiv") && issue.time == 5 && strstr(issue.detail, "Unhandled page fault"),
          "its line: %s", issue.detail);
    CHECK(!strcmp(issue.prefix, "/data/prospero-win/prefixes/gtaiv") && !strcmp(issue.exe, "C:\\Games\\GTA IV\\GTAIV.exe") &&
          !strcmp(issue.firmware, "12.40") && issue.closed && issue.ended == 35,
          "where it ran: %s %s %s %d %lld", issue.prefix, issue.exe, issue.firmware, issue.closed, (long long)issue.ended);
    CHECK(CLASSIFY("grim", handed) == PC5_LOG_GAME_CRASHED && !issue.closed, "a crash the game's own handler took");
    CHECK(CLASSIFY("launcher", crash) == PC5_LOG_FINE, "PC5's own session isn't a game's");
    CHECK(CLASSIFY("blur", played) == PC5_LOG_FINE, "a game played");
    CHECK(CLASSIFY("quick", quick) == PC5_LOG_NO_PICTURE, "closed before any picture");
    CHECK(CLASSIFY("launcher", pc5_fine) == PC5_LOG_FINE, "PC5 starting a game");
    CHECK(CLASSIFY("quick", "REC seq=1 t=1.0 PW_WINE64 close requested by=combo\nREC seq=2 t=7.0 PW_WINE64 close timeout: "
                            "leaving the game\n") == PC5_LOG_FINE, "closed by the player, ended when it didn't close");
    CHECK(CLASSIFY("launcher", pc5_problem) == PC5_LOG_PC5_PROBLEM && !strcmp(issue.detail, "install Blur: Not enough space"),
          "PC5's problem: %s", issue.detail);
    CHECK(!strcmp(pc5_log_problem_text(PC5_LOG_GAME_CRASHED), "stopped with an error"), "a problem in words");
    /* The firmware a report names: the kernel's own, with the number libkernel answered beside it. */
    CHECK(CLASSIFY("gtaiv", "REC seq=1 t=1.0 PC5 system firmware=13.60 firmware_version=0x13590001 reported=13.59 "
                            "data_free=1.0GB version=1.8.3\n") != PC5_LOG_PC5_PROBLEM && !strcmp(issue.firmware, "13.60"),
          "the kernel's firmware: %s", issue.firmware);
#undef CLASSIFY
    {
        char firmware[16];
        CHECK(!pc5_firmware_from_kernel("FreeBSD 11.0-RELEASE #1 r229358/releases/13.60: Thu Sep 10 2026\n"
                                        "    builder@ps5:/build\n", firmware, sizeof(firmware)) &&
              !strcmp(firmware, "13.60"), "kern.version's release: %s", firmware);
        CHECK(!pc5_firmware_from_kernel("r1/releases/9.00", firmware, sizeof(firmware)) && !strcmp(firmware, "9.00"),
              "at the end of the text, one digit before the point: %s", firmware);
        CHECK(!pc5_firmware_from_kernel("r1/releases/09.60:", firmware, sizeof(firmware)) && !strcmp(firmware, "9.60"),
              "a leading zero, as libkernel's number reads: %s", firmware);
        CHECK(!pc5_firmware_from_kernel("releases/ releases/x releases/12.4000 releases/12.40", firmware, sizeof(firmware)) &&
              !strcmp(firmware, "12.40"), "the first one that is a version: %s", firmware);
        CHECK(pc5_firmware_from_kernel("FreeBSD 11.0 r1/head", firmware, sizeof(firmware)) &&
              pc5_firmware_from_kernel("releases/13", firmware, sizeof(firmware)) &&
              pc5_firmware_from_kernel("releases/13.60.1", firmware, sizeof(firmware)) &&
              pc5_firmware_from_kernel("", firmware, sizeof(firmware)), "no release named");
    }

    path(root_dir, sizeof(root_dir), "%s/logs-root", work);
    path(logs, sizeof(logs), "%s/logs", root_dir);
    pc5_mkdirs(logs);
    path(file, sizeof(file), "%s/next.txt", logs);
    write_text(file, "2\n");                                     /* session 1 is the one running */
    /* A long session whose crash came early: the crash in its own part, the game's crash
     * handler logging on after it. Every part repeats the session's first line, the time it
     * began among it. */
    log_file(logs, "session-0.crash.log", "gtaiv", now - 600, crash);
    log_file(logs, "session-0.log", "gtaiv", now - 600, "REC seq=7 t=130.600 WINE its crash handler waits\n");
    log_file(logs, "session-0.previous.log", "gtaiv", now - 600, "REC seq=1 t=1.0 earlier\n");
    log_file(logs, "session-0.start.log", "gtaiv", now - 600, "REC seq=1 t=1.0 how it began\n");
    /* What GTA IV wrote while it ran (its session: now - 600 for 30 seconds), and what it didn't. */
    path(drive, sizeof(drive), "%s/prefixes/gtaiv/drive_c", root_dir);
    path(file, sizeof(file), "%s/users/prospero/Documents/Rockstar Games/GTA IV/log.txt", drive);
    file_at(file, "GTA IV's own log\n", now - 590);
    path(file, sizeof(file), "%s/users/prospero/AppData/Local/Temp/1234/crash.xml", drive);
    file_at(file, "<crashreport/>\n", now - 575);
    path(file, sizeof(file), "%s/users/prospero/Documents/Rockstar Games/GTA IV/old.log", drive);
    file_at(file, "from yesterday\n", now - 86400);
    path(file, sizeof(file), "%s/users/prospero/Documents/Rockstar Games/GTA IV/save.dat", drive);
    file_at(file, "a save\n", now - 590);
    path(file, sizeof(file), "%s/Games/GTA IV/crash.dmp", drive);
    file_at(file, "MDMP", now - 580);
    path(file, sizeof(file), "%s/Games/Other/other.log", drive);
    file_at(file, "another game's\n", now - 590);
    /* A long session: its newest part alone looks like a game that closed within a
     * minute without a picture; how it began says otherwise. */
    log_file(logs, "session-2.start.log", "longplay", now - 4000, "REC seq=1 t=1.0 display released to vulkan\n");
    log_file(logs, "session-2.log", "longplay", now - 4000, "REC seq=900 t=5000.0 PW_WINE64 alive\n"
             "REC seq=901 t=5010.0 PW_WINE64 session_end reason=wine-exit\n");
    log_file(logs, "session-7.start.log", "blur", now - 3050, "REC seq=1 t=1.0 began\n");
    log_file(logs, "session-1.log", "launcher", now - 10, pc5_fine);
    log_file(logs, "session-7.log", "blur", now - 3000, played);
    log_file(logs, "session-6.log", "launcher", now - 3100, pc5_fine);
    log_file(logs, "session-5.log", "quick", now - 2000, quick);
    log_file(logs, "session-4.log", "launcher", now - 1000, pc5_problem);
    log_file(logs, "session-3.log", "launcher", now - 500, "REC seq=1 t=1.0 PC5 start version=1.0.0\n");
    path(file, sizeof(file), "%s/pc5/crash-3.txt", root_dir);
    write_text(file, "signal 11\n");
    log_file(logs, "issue-2026-10-01_10-00_old.log", "old", now - 20 * 86400, crash);
    log_file(logs, "log-2026-10-20_10-00_blur.zip", "blur", now - 5 * 86400, "an older one of Blur's that went well\n");
    {
        struct timeval old[2] = { { (time_t)(now - 10 * 86400), 0 }, { (time_t)(now - 10 * 86400), 0 } };
        path(file, sizeof(file), "%s/issue-2026-10-01_10-00_old.log", logs);
        utimes(file, old);
    }
    int issues = pc5_logs_tidy(root_dir, 0, now, 3600, &issue);
    CHECK(issues == 4, "four sessions with a problem: %d", issues);
    path(file, sizeof(file), "%s/session-1.log", logs);
    CHECK(pc5_is_file(file), "the running session stays");
    path(file, sizeof(file), "%s/session-7.log", logs);
    CHECK(!pc5_exists(file), "a game that went well: its session filed");
    path(file, sizeof(file), "%s/session-7.start.log", logs);
    CHECK(!pc5_exists(file), "with how it began");
    path(file, sizeof(file), "%s/log-2026-10-22_20-22_blur.zip", logs);
    CHECK(pc5_is_file(file), "kept as the game's newest log, though it went well");
    CHECK(!zip_names(file, names, sizeof(names)) && strstr(names, "log-2026-10-22_20-22_blur.log\n") &&
          strstr(names, "log-2026-10-22_20-22_blur-start.log\n") && strstr(names, "about.txt\n"), "its parts: %s", names);
    path(file, sizeof(file), "%s/log-2026-10-20_10-00_blur.zip", logs);
    CHECK(!pc5_exists(file), "Blur's older one that went well: deleted");
    path(file, sizeof(file), "%s/log-2026-10-22_20-06_longplay.zip", logs);
    CHECK(pc5_is_file(file), "a long session read from how it began: went well, kept as the newest");
    path(file, sizeof(file), "%s/session-6.log", logs);
    CHECK(!pc5_exists(file), "PC5 that went well: deleted");
    CHECK(count_entries(logs, "log-") == 2, "only games' logs that went well: %d", count_entries(logs, "log-"));
    path(file, sizeof(file), "%s/issue-2026-10-01_10-00_old.log", logs);
    CHECK(!pc5_exists(file), "older than a week: deleted");
    path(file, sizeof(file), "%s/pc5/crash-3.txt", root_dir);
    CHECK(!pc5_exists(file), "the crash marker is used once");
    CHECK(count_entries(logs, "issue-") == 4 && count_entries(logs, "session-") == 1, "kept as one zip each: %d",
          count_entries(logs, "issue-"));
    CHECK(issue.problem == PC5_LOG_CRASHED && strstr(issue.file, "/logs/issue-") && strstr(issue.file, "_launcher.zip"),
          "the newest: %d %s", (int)issue.problem, issue.file);
    path(file, sizeof(file), "%s/issue-2026-10-22_21-03_gtaiv.zip", logs);   /* local time: UTC+1 */
    CHECK(pc5_is_file(file), "the crash kept with the game's name and when it began");
    CHECK(!zip_names(file, names, sizeof(names)) && strstr(names, "issue-2026-10-22_21-03_gtaiv.log\n") &&
          strstr(names, "issue-2026-10-22_21-03_gtaiv-earlier.log\n") && strstr(names, "issue-2026-10-22_21-03_gtaiv-start.log\n") &&
          strstr(names, "issue-2026-10-22_21-03_gtaiv-crash.log\n") && strstr(names, "about.txt\n"),
          "with its parts, the one with the crash among them: %s", names);
    CHECK(strstr(names, "game/users/prospero/Documents/Rockstar Games/GTA IV/log.txt\n") &&
          strstr(names, "game/users/prospero/AppData/Local/Temp/1234/crash.xml\n") && strstr(names, "game/Games/GTA IV/crash.dmp\n"),
          "and the game's own log, crash report and dump: %s", names);
    CHECK(!strstr(names, "old.log") && !strstr(names, "save.dat") && !strstr(names, "other.log"),
          "not what it wrote another time, a save, or another game's");
    path(file, sizeof(file), "%s/session-0.start.log", logs);
    CHECK(!pc5_exists(file), "its session's parts are in the zip, not beside it");
    path(file, sizeof(file), "%s/session-0.crash.log", logs);
    CHECK(!pc5_exists(file), "the crash's part too");
    CHECK(!pc5_logs_newest(root_dir, "gtaiv", file, sizeof(file)) && strstr(file, "issue-2026-10-22_21-03_gtaiv.zip"),
          "GTA IV's newest log: %s", file);
    CHECK(!pc5_logs_newest(root_dir, "blur", file, sizeof(file)) && strstr(file, "log-2026-10-22_20-22_blur.zip"),
          "Blur's: %s", file);
    CHECK(pc5_logs_newest(root_dir, "nothing", file, sizeof(file)) == -1, "none for a game never played");
    /* Keep all: one that went well is kept too, as log-..., PC5's own among them. */
    path(file, sizeof(file), "%s/next.txt", logs);
    write_text(file, "3\n");
    log_file(logs, "session-0.log", "blur", now - 100, played);
    CHECK(pc5_logs_tidy(root_dir, 1, now, 0, NULL) == 0 && count_entries(logs, "log-") == 4, "keep all: %d",
          count_entries(logs, "log-"));   /* both of Blur's, longplay's and PC5's session before it */
}

static void test_cover_fill(void)
{
    Pc5Image wide, cover;
    CHECK(!pc5_image_new(&wide, 900, 400), "picture");
    for (size_t i = 0; i < 900u * 400u; i++) wide.px[i] = 0xff3366ccu;
    CHECK(!pc5_cover_compose(&wide, &cover) && cover.w == PC5_COVER_WIDTH && cover.h == PC5_COVER_HEIGHT, "composed");
    int bands = 0;
    for (int y = 0; y < cover.h; y += 7)
        for (int x = 0; x < cover.w; x += 7) {
            uint32_t p = cover.px[(size_t)y * (size_t)cover.w + (size_t)x];
            int dr = (int)((p >> 16) & 255) - 0x33, dg = (int)((p >> 8) & 255) - 0x66, db = (int)(p & 255) - 0xcc;
            bands += abs(dr) > 2 || abs(dg) > 2 || abs(db) > 2;
        }
    CHECK(!bands, "a wide picture fills the cover: %d pixels differ", bands);
    pc5_image_free(&wide);
    pc5_image_free(&cover);
}

/* ---- in a game: the controller, the loading screen, the counter -------------------------------- */

static struct {
    uint32_t light;
    int lights, resets, cursors;
    Pc5Trigger l2, r2;
    int triggers;
    uint32_t cursor_image[64 * 64];
    uint32_t cursor_x, cursor_y;
    int magnify_calls, magnified, magnify_rc;
} pad_seen;

static int fake_light(int handle, uint32_t rgb) { (void)handle; pad_seen.light = rgb; pad_seen.lights++; return 0; }
static int fake_light_reset(int handle) { (void)handle; pad_seen.resets++; return 0; }
static int fake_triggers(int handle, const Pc5Trigger *l2, const Pc5Trigger *r2)
{
    (void)handle;
    pad_seen.l2 = *l2;
    pad_seen.r2 = *r2;
    pad_seen.triggers++;
    return 0;
}
static int fake_cursor(int videoout, const uint32_t *image, uint32_t x, uint32_t y)
{
    (void)videoout;
    if (image) memcpy(pad_seen.cursor_image, image, sizeof(pad_seen.cursor_image));
    pad_seen.cursor_x = x;
    pad_seen.cursor_y = y;
    pad_seen.cursors++;
    return 0;
}
static int fake_cursor_magnify(int videoout, int on)
{
    (void)videoout;
    pad_seen.magnify_calls++;
    pad_seen.magnified = on;
    return pad_seen.magnify_rc;
}

/* The overlay's numbers: a process that has used 4 of 16 CPUs since, a
 * CPU at 61 degrees, a chip whose sensor can't be read, 5.1 of 12.5 GiB. */
static struct { uint64_t cpu_us; int soc_rc; int memory_rc; } measured;
static uint64_t fake_cpu_time_us(void) { return measured.cpu_us; }
static unsigned fake_cpu_count(void) { return 16; }
static int fake_temperature(int sensor, int *celsius)
{
    if (sensor) return measured.soc_rc;
    *celsius = 61;
    return 0;
}
static int fake_memory(uint64_t *used, uint64_t *total)
{
    *used = ((uint64_t)51 << 30) / 10;
    *total = ((uint64_t)125 << 30) / 10;
    return measured.memory_rc;
}

/* The on-screen keyboard: what the console's keyboard is doing, and what PC5 asked of it. */
static struct {
    int opens, closes, open_rc, state, accept;
    uint32_t capacity;
    uint16_t *buffer;
    char log[512];
} ime_seen;
static int fake_keyboard_open(uint16_t *buffer, uint32_t capacity)
{
    ime_seen.opens++;
    ime_seen.buffer = buffer;
    ime_seen.capacity = capacity;
    if (!ime_seen.open_rc) ime_seen.state = 1;
    return ime_seen.open_rc;
}
static int fake_keyboard_state(void) { return ime_seen.state; }
static int fake_keyboard_close(void)
{
    ime_seen.closes++;
    ime_seen.state = 0;
    return ime_seen.accept;
}
static void fake_keyboard_log(const char *line)
{
    size_t used = strlen(ime_seen.log);
    snprintf(ime_seen.log + used, sizeof(ime_seen.log) - used, "%s\n", line);
}

/* The touchpad clicked with two fingers opens the keyboard, the click stays the
 * keyboard's until released, and accepted text is typed once, in parts. */
static void test_keyboard(void)
{
    enum { CROSS = 0x4000u, TOUCHPAD = 0x100000u };
    const Pc5GameOps ops = { .log = fake_keyboard_log, .keyboard_open = fake_keyboard_open,
                             .keyboard_state = fake_keyboard_state, .keyboard_close = fake_keyboard_close };
    Pc5GameSession game;
    size_t count = 0;
    const uint16_t *text;

    memset(&game, 0, sizeof(game));
    game.ops = ops;
    memset(&ime_seen, 0, sizeof(ime_seen));
    CHECK(pc5_game_keyboard(&game, TOUCHPAD, TOUCHPAD, 0, 1) == 0 && !ime_seen.opens, "a one-finger click is the game's");
    pc5_game_keyboard(&game, 0, 0, TOUCHPAD, 0);
    CHECK(pc5_game_keyboard(&game, TOUCHPAD | CROSS, CROSS, 0, 2) == 0 && !ime_seen.opens, "and so is Cross with two fingers down");
    pc5_game_keyboard(&game, 0, 0, TOUCHPAD | CROSS, 0);
    CHECK(pc5_game_keyboard(&game, TOUCHPAD, TOUCHPAD, 0, 2) == TOUCHPAD && ime_seen.opens == 1 &&
          ime_seen.capacity == PC5_KEYBOARD_TEXT && game.keyboard_open, "a two-finger click: the keyboard opens");
    CHECK(pc5_game_keyboard(&game, TOUCHPAD, 0, 0, 2) == TOUCHPAD, "the click is hidden while held");
    CHECK(pc5_game_keyboard(&game, 0, 0, TOUCHPAD, 0) == TOUCHPAD, "and its release too");
    CHECK(pc5_game_keyboard(&game, 0, 0, 0, 0) == 0 && ime_seen.closes == 0, "then nothing is hidden, the keyboard still open");
    text = pc5_game_keyboard_text(&game, &count);
    CHECK(count == 0, "no text while it is open");
    /* Accepted: "Né😀" (a surrogate pair at the end) */
    static const uint16_t typed[] = { 'N', 0x00e9, 0xd83d, 0xde00, 0 };
    memcpy(ime_seen.buffer, typed, sizeof(typed));
    ime_seen.state = 2;
    ime_seen.accept = 1;
    CHECK(pc5_game_keyboard(&game, 0, 0, 0, 0) == 0 && ime_seen.closes == 1 && !game.keyboard_open, "done: closed once");
    text = pc5_game_keyboard_text(&game, &count);
    CHECK(count == 4 && text[0] == 'N' && text[1] == 0x00e9 && text[3] == 0xde00, "its text, unit by unit: %zu", count);
    pc5_game_keyboard_typed(&game, 3);
    text = pc5_game_keyboard_text(&game, &count);
    CHECK(count == 1 && text[0] == 0xde00, "typed in parts: %zu left", count);
    pc5_game_keyboard_typed(&game, 5);
    text = pc5_game_keyboard_text(&game, &count);
    CHECK(count == 0, "then none");
    pc5_game_keyboard(&game, 0, 0, 0, 0);
    CHECK(ime_seen.closes == 1, "a closed keyboard isn't closed again");
    /* Cancelled: nothing to type. */
    pc5_game_keyboard(&game, TOUCHPAD, TOUCHPAD, 0, 2);
    ime_seen.buffer[0] = 'x';
    ime_seen.state = 2;
    ime_seen.accept = 0;
    pc5_game_keyboard(&game, 0, 0, TOUCHPAD, 0);
    text = pc5_game_keyboard_text(&game, &count);
    CHECK(ime_seen.opens == 2 && ime_seen.closes == 2 && count == 0, "cancelled: nothing typed");
    /* Refused: the click is still the keyboard's, nothing stays open. */
    ime_seen.open_rc = -5;
    CHECK(pc5_game_keyboard(&game, TOUCHPAD, TOUCHPAD, 0, 2) == TOUCHPAD && !game.keyboard_open &&
          ime_seen.opens == 3, "refused: not open");
    CHECK(strstr(ime_seen.log, "PC5 keyboard open rc=0") && strstr(ime_seen.log, "4 characters to type") &&
          strstr(ime_seen.log, "typed 4 characters") && strstr(ime_seen.log, "cancelled") &&
          strstr(ime_seen.log, "refused rc=0xfffffffb"), "the log says each: %s", ime_seen.log);
    /* A game that ends with the keyboard open closes it. */
    ime_seen.open_rc = 0;
    pc5_game_keyboard(&game, 0, 0, TOUCHPAD, 0);
    pc5_game_keyboard(&game, TOUCHPAD, TOUCHPAD, 0, 2);
    pc5_game_end(&game, -1);
    CHECK(ime_seen.closes == 3 && !game.keyboard_open, "the game's end closes it");
    /* No keyboard on this console: the click stays the game's. */
    pc5_game_keyboard(&game, 0, 0, TOUCHPAD, 0);
    game.ops.keyboard_open = NULL;
    CHECK(pc5_game_keyboard(&game, TOUCHPAD, TOUCHPAD, 0, 2) == 0, "without a keyboard, nothing is taken");
}

/* A program that loads Direct3D or Vulkan itself names it in its data: found in ASCII or
 * UTF-16, any case; an installed game noted as drawing with Windows gets its API on its
 * next start. */
static void test_named_libraries(void)
{
    char root[PC5_PATH], from[PC5_PATH], exe[PC5_PATH], file[PC5_PATH], error[PC5_ERROR];
    static char text[4096];
    static const uint8_t wide[] = { 'V', 0, 'u', 0, 'L', 0, 'k', 0, 'A', 0, 'n', 0, '-', 0, '1', 0, '.', 0, 'D', 0, 'L', 0, 'L', 0 };
    Pc5Info info;
    FILE *out;

    path(from, sizeof(from), "%s/launcher.exe", programs);
    CHECK(!(pc5_pe_named_libraries(from) & PC5_IMPORT_DRAWING), "a plain program names none");
    path(root, sizeof(root), "%s/named-root", work);
    path(exe, sizeof(exe), "%s/prefixes/flat/drive_c/Games/Flat/Flat.exe", root);
    copy_file(from, exe);
    if ((out = fopen(exe, "ab"))) {
        fwrite(wide, 1, sizeof(wide), out);
        fclose(out);
    }
    CHECK(pc5_pe_named_libraries(exe) == PC5_IMPORT_VULKAN, "Vulkan in UTF-16: %#x", pc5_pe_named_libraries(exe));
    if ((out = fopen(exe, "ab"))) {
        fwrite("xx\0D3D11.dll", 1, 12, out);
        fclose(out);
    }
    CHECK(pc5_pe_named_libraries(exe) == (PC5_IMPORT_VULKAN | PC5_IMPORT_D3D11), "and D3D11 in ASCII");
    memset(&info, 0, sizeof(info));
    pc5_copy(info.title, sizeof(info.title), "Flat");
    pc5_copy(info.folder, sizeof(info.folder), "Flat");
    pc5_copy(info.exe, sizeof(info.exe), "Flat.exe");
    info.bits = 64;
    info.api = PC5_API_GDI;
    pc5_info_text(&info, text, sizeof(text));
    path(file, sizeof(file), "%s/prefixes/flat/pc5/info.txt", root);
    write_text(file, text);
    path(file, sizeof(file), "%s/profiles/index.txt", root);
    write_text(file, "");
    CHECK(!pc5_profile_refresh(root, "flat", error, sizeof(error)), "refreshed: %s", error);
    CHECK(!pc5_info_read(root, "flat", &info) && info.api == PC5_API_D3D11, "its API kept in its notes: %d", (int)info.api);
    path(file, sizeof(file), "%s/profiles/flat.profile", root);
    {
        Pc5Buf profile = { 0 };
        CHECK(!pc5_read_file(file, &profile, 16384) && !pc5_buf_terminate(&profile) &&
              strstr((char *)profile.data, "graphics = dxvk\n"), "drawn through DXVK now");
        pc5_buf_free(&profile);
    }
}

/* A Java game: a launcher that imports nothing it draws with, a Java runtime beside it. It
 * draws with OpenGL, at install and for one installed before; one made with jpackage gets
 * its app folder in PATH (WINEPATH) so it doesn't start itself again. */
static void test_java(void)
{
    char root[PC5_PATH], from[PC5_PATH], file[PC5_PATH], dir[PC5_PATH], error[PC5_ERROR], winepath[PC5_PATH];
    static char text[4096];
    Pc5Folder *folder = pc5_alloc(sizeof(*folder));
    Pc5Info info;

    if (!folder) return;
    path(from, sizeof(from), "%s/launcher.exe", programs);
    path(dir, sizeof(dir), "%s/stubs/Pixel Dungeon", work);
    path(file, sizeof(file), "%s/Pixel Dungeon.exe", dir);
    copy_file(from, file);
    CHECK(!pc5_folder_inspect(dir, "Pixel Dungeon", folder) && folder->exe_count && !folder->exes[0].java &&
          pc5_folder_api(folder, 0) == PC5_API_GDI, "without a runtime: Windows' own drawing");
    CHECK(!pc5_java_beside(dir), "no runtime beside it");
    path(file, sizeof(file), "%s/Runtime/BIN/Server/JVM.dll", dir);
    copy_file(from, file);
    CHECK(pc5_java_beside(dir), "a runtime beside it, names in any case");
    CHECK(!pc5_folder_inspect(dir, "Pixel Dungeon", folder) && folder->exe_count && folder->exes[0].java &&
          !strcmp(folder->exes[0].path, "Pixel Dungeon.exe") && pc5_folder_api(folder, 0) == PC5_API_OPENGL,
          "with one: OpenGL (%s, %d)", folder->exe_count ? folder->exes[0].path : "", (int)pc5_folder_api(folder, 0));
    /* A runtime elsewhere in the folder (a tool's) isn't the game's: not beside its program. */
    {
        char other[PC5_PATH];
        path(other, sizeof(other), "%s/stubs/Tooled", work);
        path(file, sizeof(file), "%s/Tooled.exe", other);
        copy_file(from, file);
        path(file, sizeof(file), "%s/tools/editor/jre64/bin/server/jvm.dll", other);
        copy_file(from, file);
        CHECK(!pc5_folder_inspect(other, "Tooled", folder) && folder->exe_count && !folder->exes[0].java &&
              pc5_folder_api(folder, 0) == PC5_API_GDI, "a tool's runtime: not a Java game");
    }
    /* A library beside the program that draws comes first, at install and at start. */
    {
        char beside[PC5_PATH], engine[PC5_PATH];
        path(beside, sizeof(beside), "%s/stubs/Engined", work);
        path(file, sizeof(file), "%s/Engined.exe", beside);
        copy_file(from, file);
        path(engine, sizeof(engine), "%s/engine.dll", programs);
        path(file, sizeof(file), "%s/engine.dll", beside);
        copy_file(engine, file);
        path(file, sizeof(file), "%s/jre/bin/server/jvm.dll", beside);
        copy_file(from, file);
        CHECK(!pc5_folder_inspect(beside, "Engined", folder) && folder->exe_count && folder->exes[0].java &&
              pc5_folder_api(folder, 0) == PC5_API_D3D12, "its library's Direct3D 12 first: %d", (int)pc5_folder_api(folder, 0));
        CHECK(pc5_dll_imports_in(beside) & PC5_IMPORT_D3D12, "the library's imports");
    }
    pc5_free(folder);

    path(root, sizeof(root), "%s/java-root", work);
    path(file, sizeof(file), "%s/prefixes/pixel/drive_c/Games/Pixel Dungeon/Pixel Dungeon.exe", root);
    copy_file(from, file);
    path(file, sizeof(file), "%s/prefixes/pixel/drive_c/Games/Pixel Dungeon/jre/bin/client/jvm.dll", root);
    copy_file(from, file);
    memset(&info, 0, sizeof(info));
    pc5_copy(info.title, sizeof(info.title), "Pixel Dungeon");
    pc5_copy(info.folder, sizeof(info.folder), "Pixel Dungeon");
    pc5_copy(info.exe, sizeof(info.exe), "Pixel Dungeon.exe");
    info.bits = 64;
    info.api = PC5_API_D3D9;   /* what its runtime's libraries import */
    pc5_info_text(&info, text, sizeof(text));
    path(file, sizeof(file), "%s/prefixes/pixel/pc5/info.txt", root);
    write_text(file, text);
    path(file, sizeof(file), "%s/profiles/index.txt", root);
    write_text(file, "");
    CHECK(pc5_game_winepath(root, "pixel", winepath, sizeof(winepath)) == -1 && !winepath[0], "not made with jpackage");
    CHECK(!pc5_profile_refresh(root, "pixel", error, sizeof(error)), "refreshed: %s", error);
    CHECK(!pc5_info_read(root, "pixel", &info) && info.api == PC5_API_OPENGL, "OpenGL kept in its notes: %d", (int)info.api);
    path(file, sizeof(file), "%s/profiles/pixel.profile", root);
    {
        Pc5Buf profile = { 0 };
        CHECK(!pc5_read_file(file, &profile, 16384) && !pc5_buf_terminate(&profile) &&
              strstr((char *)profile.data, "graphics = opengl\n"), "drawn through OpenGL now");
        pc5_buf_free(&profile);
    }
    /* With a library beside it that draws, it isn't switched. */
    {
        char engine[PC5_PATH];
        Pc5Info drawn;
        path(file, sizeof(file), "%s/prefixes/engined/drive_c/Games/Engined/Engined.exe", root);
        copy_file(from, file);
        path(engine, sizeof(engine), "%s/engine.dll", programs);
        path(file, sizeof(file), "%s/prefixes/engined/drive_c/Games/Engined/engine.dll", root);
        copy_file(engine, file);
        path(file, sizeof(file), "%s/prefixes/engined/drive_c/Games/Engined/jre/bin/server/jvm.dll", root);
        copy_file(from, file);
        memset(&drawn, 0, sizeof(drawn));
        pc5_copy(drawn.title, sizeof(drawn.title), "Engined");
        pc5_copy(drawn.folder, sizeof(drawn.folder), "Engined");
        pc5_copy(drawn.exe, sizeof(drawn.exe), "Engined.exe");
        drawn.bits = 64;
        drawn.api = PC5_API_D3D12;
        pc5_info_text(&drawn, text, sizeof(text));
        path(file, sizeof(file), "%s/prefixes/engined/pc5/info.txt", root);
        write_text(file, text);
        CHECK(!pc5_profile_refresh(root, "engined", error, sizeof(error)) && !pc5_info_read(root, "engined", &drawn) &&
              drawn.api == PC5_API_D3D12, "its library's Direct3D 12 kept: %d", (int)drawn.api);
    }
    path(file, sizeof(file), "%s/prefixes/pixel/drive_c/Games/Pixel Dungeon/App/pixel dungeon.CFG", root);
    write_text(file, "[Application]\napp.mainclass=com.example.Main\n");
    CHECK(!pc5_game_winepath(root, "pixel", winepath, sizeof(winepath)) &&
          !strcmp(winepath, "C:\\Games\\Pixel Dungeon\\app"), "jpackage's app folder: %s", winepath);

    /* env.txt: the game's own settings in its environment, never what Wine or PC5 set. */
    {
        char names[256];
        unsetenv("MEMORIES_NO_MONITOR");
        unsetenv("GAME_LOG");
        CHECK(pc5_game_env(root, "pixel", names, sizeof(names)) == 0 && !names[0], "no env.txt: nothing");
        path(file, sizeof(file), "%s/prefixes/pixel/pc5/env.txt", root);
        write_text(file, "# the game's own\nMEMORIES_NO_MONITOR=1\n  GAME_LOG = C:\\logs\\a b.txt  \r\n\n"
                         "WINEDEBUG=+all\nPW_LARGE_ADDRESS_AWARE=1\nLD_PRELOAD=x\nPath=C:\\\n9LIVES=1\nBAD-NAME=1\nno equals\n");
        CHECK(pc5_game_env(root, "pixel", names, sizeof(names)) == 2 && !strcmp(names, "MEMORIES_NO_MONITOR,GAME_LOG"),
              "two set: %s", names);
        CHECK(getenv("MEMORIES_NO_MONITOR") && !strcmp(getenv("MEMORIES_NO_MONITOR"), "1") && getenv("GAME_LOG") &&
              !strcmp(getenv("GAME_LOG"), "C:\\logs\\a b.txt"), "with their values, spaces around them left out");
        CHECK(!getenv("PW_LARGE_ADDRESS_AWARE") || strcmp(getenv("PW_LARGE_ADDRESS_AWARE"), "1"), "never what PC5 sets");
        CHECK(!pc5_game_env_name_ok("WineDllOverrides") && !pc5_game_env_name_ok("pw_x") && !pc5_game_env_name_ok("PATH") &&
              pc5_game_env_name_ok("SteamAppId") && pc5_game_env_name_ok("_X1"), "the names it takes");
        unsetenv("MEMORIES_NO_MONITOR");
        unsetenv("GAME_LOG");
        unlink(file);
    }
}

/* A game started from a batch file runs with the bits of the program the batch file starts,
 * whatever its notes say. */
static void test_batch_bits(void)
{
    char root[PC5_PATH], from[PC5_PATH], file[PC5_PATH], error[PC5_ERROR];
    static char text[4096];
    Pc5Info info;
    Pc5Buf profile = { 0 };

    path(root, sizeof(root), "%s/batch-root", work);
    path(from, sizeof(from), "%s/game64.exe", programs);
    path(file, sizeof(file), "%s/prefixes/hunt/drive_c/Games/Hunt/System/Win64_AMD64/Engine.exe", root);
    copy_file(from, file);
    path(from, sizeof(from), "%s/game32.exe", programs);
    path(file, sizeof(file), "%s/prefixes/hunt/drive_c/Games/Hunt/System/Engine.exe", root);
    copy_file(from, file);
    path(file, sizeof(file), "%s/prefixes/hunt/drive_c/Games/Hunt/Hunt.bat", root);
    write_text(file, "@echo off\r\nstart \"\" \"System\\Win64_AMD64\\Engine.exe\" -game\r\n");
    memset(&info, 0, sizeof(info));
    pc5_copy(info.title, sizeof(info.title), "Hunt");
    pc5_copy(info.folder, sizeof(info.folder), "Hunt");
    pc5_copy(info.exe, sizeof(info.exe), "Hunt.bat");
    info.bits = 32;   /* the 32-bit program's, chosen before */
    info.api = PC5_API_D3D9;
    pc5_info_text(&info, text, sizeof(text));
    path(file, sizeof(file), "%s/prefixes/hunt/pc5/info.txt", root);
    write_text(file, text);
    path(file, sizeof(file), "%s/profiles/index.txt", root);
    write_text(file, "");
    CHECK(!pc5_profile_refresh(root, "hunt", error, sizeof(error)), "refreshed: %s", error);
    path(file, sizeof(file), "%s/profiles/hunt.profile", root);
    CHECK(!pc5_read_file(file, &profile, 16384) && !pc5_buf_terminate(&profile) &&
          strstr((char *)profile.data, "executable = C:\\Games\\Hunt\\System\\Win64_AMD64\\Engine.exe\n") &&
          strstr((char *)profile.data, "architecture = pe64\n"), "the 64-bit program it starts, as 64-bit:\n%s",
          profile.data ? (char *)profile.data : "");
    pc5_buf_free(&profile);
}

/* A game's prefix from before winegstreamer gets its registrations once. */
/* An installed game's DXVK follows the app's: replaced once when the app names another. */
/* .NET: what an older game's prefix lacks, the runtime's path, and the runtime's one copy. */
static void test_dotnet(void)
{
    char root[PC5_PATH], app[PC5_PATH], file[PC5_PATH], program[PC5_PATH];
    Pc5Buf text = { 0 };
    Pc5Runtime runtime;

    path(root, sizeof(root), "%s/dotnet-root", work);
    path(app, sizeof(app), "%s/dotnet-app", work);
    path(file, sizeof(file), "%s/prefixes/game/system.reg", root);
    write_text(file, "WINE REGISTRY Version 2\n");
    path(file, sizeof(file), "%s/prefixes/game/user.reg", root);
    write_text(file, "WINE REGISTRY Version 2\n");
    CHECK(pc5_prefix_dotnet(root, "game", app) == 0, "an app without Wine Mono: nothing done");
    path(file, sizeof(file), "%s/pc5/mono-version.txt", app);
    write_text(file, "11.3.0\n");
    path(file, sizeof(file), "%s/pc5/prefix.list", app);
    write_text(file, "drive_c/windows/\ndrive_c/windows/Microsoft.NET/\ndrive_c/windows/Microsoft.NET/Framework/\n"
                     "drive_c/windows/Microsoft.NET/Framework/fusion.dll\ndrive_c/windows/system32/kernel32.dll\n");
    path(file, sizeof(file), "%s/pc5/prefix/drive_c/windows/Microsoft.NET/Framework/fusion.dll", app);
    write_text(file, "fusion");
    path(file, sizeof(file), "%s/pc5/prefix-dotnet.reg", app);
    write_text(file, "\n[Software\\\\Microsoft\\\\NET Framework Setup\\\\NDP\\\\v4\\\\Full] 1792921398\n\"Install\"=dword:00000001\n");
    CHECK(pc5_prefix_dotnet(root, "game", app) == 1, "an older game's prefix gets .NET");
    path(file, sizeof(file), "%s/prefixes/game/system.reg", root);
    CHECK(!pc5_read_file(file, &text, 4096) && !pc5_buf_terminate(&text) &&
          strstr((char *)text.data, "NDP\\\\v4\\\\Full] 1792921398\n\"Install\""), "its registry");
    pc5_buf_free(&text);
    path(file, sizeof(file), "%s/prefixes/game/drive_c/windows/Microsoft.NET/Framework/fusion.dll", root);
    CHECK(pc5_is_file(file), "its files");
    path(file, sizeof(file), "%s/prefixes/game/drive_c/windows/system32/kernel32.dll", root);
    CHECK(!pc5_exists(file), "and nothing else of the template");
    path(file, sizeof(file), "%s/prefixes/game/user.reg", root);
    CHECK(!pc5_read_file(file, &text, 4096) && !pc5_buf_terminate(&text) &&
          strstr((char *)text.data, "[Software\\\\Wine\\\\Mono]\n\"RuntimePath\"=\"Z:\\\\") &&
          strstr((char *)text.data, "dotnet-root\\\\pc5\\\\mono-11.3.0\\\\mono-2.0\"\n"), "the runtime's path: %s",
          text.len ? (char *)text.data : "");
    pc5_buf_free(&text);
    CHECK(pc5_prefix_dotnet(root, "game", app) == 0, "only once");
    path(file, sizeof(file), "%s/pc5/mono-version.txt", app);
    write_text(file, "11.4.0\n");
    CHECK(pc5_prefix_dotnet(root, "game", app) == 1, "a new version: the new path");
    path(file, sizeof(file), "%s/prefixes/game/user.reg", root);
    CHECK(!pc5_read_file(file, &text, 4096) && !pc5_buf_terminate(&text) && strstr((char *)text.data, "mono-11.4.0"),
          "the new path");
    pc5_buf_free(&text);
    path(file, sizeof(file), "%s/prefixes/game/system.reg", root);
    {
        int times = 0;
        if (!pc5_read_file(file, &text, 4096) && !pc5_buf_terminate(&text))
            for (const char *at = (char *)text.data; (at = strstr(at, "NDP")); at++) times++;
        CHECK(times == 1, "the registry once: %d", times);
        pc5_buf_free(&text);
    }
    CHECK(pc5_prefix_dotnet(root, "default", app) == 0, "the default prefix is left alone");

    /* Fonts: the names Windows' fonts have, added once to an older prefix's user.reg. */
    CHECK(pc5_prefix_fonts(root, "game", app) == 0, "an app without the fonts: nothing done");
    path(file, sizeof(file), "%s/pc5/prefix-fonts.reg", app);
    write_text(file, "\n[Software\\\\Wine\\\\Fonts\\\\Replacements] 1792921398\n#time=1dc4\n"
                     "\"Arial\"=\"Liberation Sans\"\n\"MS Gothic\"=\"Droid Sans Fallback\"\n");
    CHECK(pc5_prefix_fonts(root, "game", app) == 1, "an older game's prefix gets the font names");
    CHECK(pc5_prefix_fonts(root, "game", app) == 0, "only once");
    path(file, sizeof(file), "%s/prefixes/game/user.reg", root);
    {
        int times = 0;
        if (!pc5_read_file(file, &text, 8192) && !pc5_buf_terminate(&text))
            for (const char *at = (char *)text.data; (at = strstr(at, "Fonts\\\\Replacements]")); at++) times++;
        CHECK(times == 1 && strstr((char *)text.data, "\"MS Gothic\"=\"Droid Sans Fallback\"\n") &&
              strstr((char *)text.data, "mono-11.4.0"), "the names once, after what it had: %d", times);
        pc5_buf_free(&text);
    }
    CHECK(pc5_prefix_fonts(root, "missing", app) == -1, "no prefix: nothing done");

    /* Windows versions: Splinter Cell: Conviction's program told it runs on Windows 7, once. */
    CHECK(pc5_prefix_versions(root, "game") == 1, "an older game's prefix gets the Windows versions");
    CHECK(pc5_prefix_versions(root, "game") == 0, "only once");
    path(file, sizeof(file), "%s/prefixes/game/user.reg", root);
    {
        int times = 0;
        if (!pc5_read_file(file, &text, 8192) && !pc5_buf_terminate(&text))
            for (const char *at = (char *)text.data; (at = strstr(at, "AppDefaults\\\\Conviction_game.exe]")); at++) times++;
        CHECK(times == 1 && strstr((char *)text.data,
                                   "\n[Software\\\\Wine\\\\AppDefaults\\\\Conviction_game.exe]\n\"Version\"=\"win7\"\n") &&
              strstr((char *)text.data, "\"MS Gothic\"=\"Droid Sans Fallback\"\n"), "Windows 7 for it, once, after what it had: %d",
              times);
        pc5_buf_free(&text);
    }
    CHECK(pc5_prefix_versions(root, "missing") == 0, "no Windows yet: nothing done, done at its next start");

    /* The runtime: copied once, for a .NET program only. */
    path(file, sizeof(file), "%s/pc5/mono.list", app);
    write_text(file, "mono-2.0/\nmono-2.0/bin/\nmono-2.0/bin/libmono-2.0-x86_64.dll\nmono-2.0/lib/mono/4.5/mscorlib.dll\n");
    path(file, sizeof(file), "%s/pc5/mono/mono-2.0/bin/libmono-2.0-x86_64.dll", app);
    write_text(file, "libmono");
    path(file, sizeof(file), "%s/pc5/mono/mono-2.0/lib/mono/4.5/mscorlib.dll", app);
    write_text(file, "mscorlib");
    path(program, sizeof(program), "%s/game64.exe", programs);
    CHECK(pc5_runtime_begin(&runtime, root, app, program) == 0, "not a .NET program: nothing to copy");
    path(program, sizeof(program), "%s/dotnet.exe", programs);
    CHECK(pc5_runtime_begin(&runtime, root, app, program) == 1 && runtime.total == 4, "a .NET program: %u entries",
          (unsigned)runtime.total);
    int step = 0, steps = 0;
    while (!(step = pc5_runtime_step(&runtime, 1)) && steps < 100) steps++;
    pc5_runtime_end(&runtime);
    path(file, sizeof(file), "%s/pc5/mono-11.4.0/mono-2.0/lib/mono/4.5/mscorlib.dll", root);
    CHECK(step == 1 && runtime.done == 4 && !pc5_read_file(file, &text, 4096) && text.len == 8, "copied: %d %u", step,
          (unsigned)runtime.done);
    pc5_buf_free(&text);
    path(file, sizeof(file), "%s/pc5/mono-11.4.0/.complete", root);
    CHECK(pc5_is_file(file), "and marked whole");
    CHECK(pc5_runtime_begin(&runtime, root, app, program) == 0, "once");
    path(file, sizeof(file), "%s/pc5/mono-11.4.0/.complete", root);
    unlink(file);
    path(file, sizeof(file), "%s/pc5/mono/mono-2.0/bin/libmono-2.0-x86_64.dll", app);
    unlink(file);
    CHECK(pc5_runtime_begin(&runtime, root, app, program) == 1, "a copy cut short is made again");
    while (!(step = pc5_runtime_step(&runtime, 1)) && steps < 200) steps++;
    pc5_runtime_end(&runtime);
    CHECK(step == -1 && strstr(runtime.error, "has no libmono-2.0-x86_64.dll") && strstr(runtime.error, "PPSA11018"),
          "a file missing from the app is named, not taken for a full console: %s", runtime.error);
}

/* A game's Windows from an older PC5 gets the system DLLs the template has since gained. */
static void test_prefix_system(void)
{
    char root_dir[PC5_PATH], app[PC5_PATH], file[PC5_PATH];
    Pc5Buf text = { 0 };
    static const char *const listed[] = { "drive_c/windows/system32/kept.dll", "drive_c/windows/syswow64/winegstreamer.dll",
                                          "drive_c/windows/system32/winegstreamer.dll", "drive_c/windows/system32/sub/deep.dll",
                                          "drive_c/windows/system32/notes.txt", "drive_c/users/prospero/x.dll" };

    path(root_dir, sizeof(root_dir), "%s/system-root", work);
    path(app, sizeof(app), "%s/system-app", work);
    for (size_t i = 0; i < sizeof(listed) / sizeof(listed[0]); i++) {
        path(file, sizeof(file), "%s/pc5/prefix/%s", app, listed[i]);
        write_text(file, "from the template");
        pc5_buf_printf(&text, "%s\n", listed[i]);
    }
    path(file, sizeof(file), "%s/pc5/prefix.list", app);
    pc5_write_file(file, text.data, text.len);
    pc5_buf_free(&text);
    path(file, sizeof(file), "%s/prefixes/old/drive_c/windows/system32/kept.dll", root_dir);
    write_text(file, "the game's own");
    CHECK(pc5_prefix_system(root_dir, "old", app) == 2, "the two decoders added");
    path(file, sizeof(file), "%s/prefixes/old/drive_c/windows/syswow64/winegstreamer.dll", root_dir);
    CHECK(!pc5_read_file(file, &text, 4096) && text.len == 17, "the 32-bit one");
    pc5_buf_free(&text);
    path(file, sizeof(file), "%s/prefixes/old/drive_c/windows/system32/kept.dll", root_dir);
    CHECK(!pc5_read_file(file, &text, 4096) && text.len == 14, "what it had stays as it was");
    pc5_buf_free(&text);
    path(file, sizeof(file), "%s/prefixes/old/drive_c/windows/system32/sub/deep.dll", root_dir);
    CHECK(!pc5_exists(file), "only the folders' own DLLs");
    path(file, sizeof(file), "%s/prefixes/old/drive_c/users/prospero/x.dll", root_dir);
    CHECK(!pc5_exists(file), "nothing outside system32 and syswow64");
    path(file, sizeof(file), "%s/prefixes/old/pc5/template.txt", root_dir);
    CHECK(!pc5_read_file(file, &text, 4096) && !strncmp((char *)text.data, PC5_VERSION, strlen(PC5_VERSION)), "noted");
    pc5_buf_free(&text);
    path(file, sizeof(file), "%s/prefixes/old/drive_c/windows/syswow64/winegstreamer.dll", root_dir);
    unlink(file);
    CHECK(pc5_prefix_system(root_dir, "old", app) == 0 && !pc5_exists(file), "once for each PC5 version");
    CHECK(pc5_prefix_system(root_dir, "default", app) == 0, "not the default prefix");
}

static void test_prefix_dxvk(void)
{
    char root[PC5_PATH], app[PC5_PATH], file[PC5_PATH];
    static const char *const dlls[] = { "d3d8", "d3d9", "d3d10core", "d3d11", "dxgi", "d3d12", "d3d12core" };
    Pc5Buf text = { 0 };

    path(root, sizeof(root), "%s/dxvk-root", work);
    path(app, sizeof(app), "%s/dxvk-app", work);
    for (int f = 0; f < 2; f++)
        for (int d = 0; d < 7; d++) {
            char data[64];
            snprintf(data, sizeof(data), "old %s %d", dlls[d], f);
            path(file, sizeof(file), "%s/prefixes/game/drive_c/windows/%s/%s.dll", root, f ? "syswow64" : "system32", dlls[d]);
            if (!(f == 1 && d == 0)) write_text(file, data);   /* a Windows without the 32-bit d3d8.dll */
            snprintf(data, sizeof(data), "new %s %d", dlls[d], f);
            path(file, sizeof(file), "%s/pc5/prefix/drive_c/windows/%s/%s.dll", app, f ? "syswow64" : "system32", dlls[d]);
            write_text(file, data);
        }
    CHECK(pc5_prefix_dxvk(root, "game", app) == 0, "an app that doesn't name its DXVK: nothing done");
    path(file, sizeof(file), "%s/pc5/dxvk.txt", app);
    write_text(file, "2.6.2-prospero2-pc5\n");
    CHECK(pc5_prefix_dxvk(root, "game", app) == 1, "replaced");
    path(file, sizeof(file), "%s/prefixes/game/drive_c/windows/syswow64/d3d11.dll", root);
    CHECK(!pc5_read_file(file, &text, 4096) && text.len == 11 && !memcmp(text.data, "new d3d11 1", 11), "the app's DLL");
    pc5_buf_free(&text);
    path(file, sizeof(file), "%s/prefixes/game/drive_c/windows/system32/d3d12core.dll", root);
    CHECK(!pc5_read_file(file, &text, 4096) && text.len == 15 && !memcmp(text.data, "new d3d12core 0", 15),
          "and VKD3D-Proton's");
    pc5_buf_free(&text);
    path(file, sizeof(file), "%s/prefixes/game/drive_c/windows/syswow64/d3d8.dll", root);
    CHECK(!pc5_exists(file), "none added where the game's Windows had none");
    path(file, sizeof(file), "%s/prefixes/game/drive_c/windows/system32/d3d11.dll.pc5-part", root);
    CHECK(!pc5_exists(file), "nothing half-written left");
    path(file, sizeof(file), "%s/prefixes/game/pc5/dxvk.txt", root);
    CHECK(!pc5_read_file(file, &text, 4096) && text.len == 20 && !memcmp(text.data, "2.6.2-prospero2-pc5\n", 20), "noted");
    pc5_buf_free(&text);
    /* once: a DLL changed by hand afterwards stays */
    path(file, sizeof(file), "%s/prefixes/game/drive_c/windows/system32/d3d9.dll", root);
    write_text(file, "the player's");
    CHECK(pc5_prefix_dxvk(root, "game", app) == 0 && pc5_read_file(file, &text, 4096) == 0 && text.len == 12, "only once");
    pc5_buf_free(&text);
    CHECK(pc5_prefix_dxvk(root, "default", app) == 0, "the default prefix is left alone");
    /* an app missing one of its DLLs: not done, and tried again */
    path(file, sizeof(file), "%s/pc5/dxvk.txt", app);
    write_text(file, "next\n");
    path(file, sizeof(file), "%s/pc5/prefix/drive_c/windows/system32/dxgi.dll", app);
    unlink(file);
    CHECK(pc5_prefix_dxvk(root, "game", app) == -1, "a missing DLL fails");
    path(file, sizeof(file), "%s/prefixes/game/pc5/dxvk.txt", root);
    CHECK(!pc5_read_file(file, &text, 4096) && !memcmp(text.data, "2.6.2", 5), "and isn't noted");
    pc5_buf_free(&text);
}

static void test_prefix_media(void)
{
    char root[PC5_PATH], app[PC5_PATH], file[PC5_PATH], big[200000];
    static const char fragment[] = "\n[Software\\\\Classes\\\\CLSID\\\\{1F1E273D-12C0-4B3A-8E9B-1933C2498AEA}"
                                   "\\\\InprocServer32] 1792812437\n@=\"C:\\\\windows\\\\system32\\\\winegstreamer.dll\"\n";
    Pc5Buf text = { 0 };

    path(root, sizeof(root), "%s/media-root", work);
    path(app, sizeof(app), "%s/media-app", work);
    path(file, sizeof(file), "%s/prefixes/old/system.reg", root);
    write_text(file, "WINE REGISTRY Version 2\n\n[Software\\\\Wine] 1\n");
    CHECK(pc5_prefix_media(root, "old", app) == -1, "no fragment in the app: nothing done");
    path(file, sizeof(file), "%s/pc5/prefix-media.reg", app);
    write_text(file, fragment);
    CHECK(pc5_prefix_media(root, "old", app) == 1, "an old prefix gets the registrations");
    path(file, sizeof(file), "%s/prefixes/old/system.reg", root);
    CHECK(!pc5_read_file(file, &text, 1u << 20) && text.len == strlen("WINE REGISTRY Version 2\n\n[Software\\\\Wine] 1\n") + strlen(fragment)
          && !memcmp(text.data + text.len - strlen(fragment), fragment, strlen(fragment)), "appended at the end");
    pc5_buf_free(&text);
    CHECK(pc5_prefix_media(root, "old", app) == 0, "and only once");
    /* The default prefix, and a name that spans two reads of a large file. */
    memset(big, 'x', sizeof(big));
    memcpy(big + 65536 - 5, "winegstreamer.dll", 17);
    big[sizeof(big) - 1] = 0;
    path(file, sizeof(file), "%s/prefix/system.reg", root);
    write_text(file, big);
    CHECK(pc5_prefix_media(root, "default", app) == 0, "found across two reads");
    CHECK(pc5_prefix_media(root, "missing", app) == -1, "no prefix: nothing done");
}

/* A picture of one colour, as a PNG file. */
static void write_png(const char *file, int w, int h, uint32_t color)
{
    Pc5Image image = { 0 };
    Pc5Buf png = { 0 };
    char dir[PC5_PATH];

    pc5_copy(dir, sizeof(dir), file);
    *strrchr(dir, '/') = 0;
    pc5_mkdirs(dir);
    if (!pc5_image_new(&image, w, h)) {
        for (int i = 0; i < w * h; i++) image.px[i] = color;
        if (!pc5_image_png(&image, &png)) pc5_write_file(file, png.data, png.len);
    }
    pc5_image_free(&image);
    pc5_buf_free(&png);
}

/* The forwarder program a tile is made from, as the app carries it. */
static void make_tile_template(char *dir, size_t size)
{
    char file[PC5_PATH];
    path(dir, size, "%s/app/pc5/forwarder", work);
    path(file, sizeof(file), "%s/eboot.bin", dir);
    write_text(file, "forwarder program");
    path(file, sizeof(file), "%s/sce_module/libc.prx", dir);
    write_text(file, "its libc");
}

static int has_text(const char *file, const char *text)
{
    Pc5Buf buf = { 0 };
    int found = !pc5_read_file(file, &buf, 1u << 20) && !pc5_buf_terminate(&buf) && strstr((char *)buf.data, text);
    pc5_buf_free(&buf);
    return found;
}

/* A game's home screen tile: made in the PS5 Forwarder Format, found again,
 * kept when made twice, taken away; another app's tile left alone. */
static void test_home(void)
{
    char root[PC5_PATH], tiles[PC5_PATH], template_dir[PC5_PATH], file[PC5_PATH], id[PC5_HOME_ID], again[PC5_HOME_ID];
    char error[PC5_ERROR] = "", missing[PC5_PATH];
    Pc5Image cover = { 0 }, icon = { 0 };
    Pc5Buf png = { 0 };

    path(root, sizeof(root), "%s/home-root", work);
    path(tiles, sizeof(tiles), "%s/homebrew", work);
    make_tile_template(template_dir, sizeof(template_dir));
    /* The game's cover: blue at the top, orange below. */
    if (!pc5_image_new(&cover, PC5_COVER_WIDTH, PC5_COVER_HEIGHT)) {
        for (int y = 0; y < PC5_COVER_HEIGHT; y++)
            for (int x = 0; x < PC5_COVER_WIDTH; x++)
                cover.px[y * PC5_COVER_WIDTH + x] = y < PC5_COVER_HEIGHT / 2 ? 0xff2050e0u : 0xffe08020u;
        path(file, sizeof(file), "%s/prefixes/neon/pc5/cover.bin", root);
        path(missing, sizeof(missing), "%s/prefixes/neon/pc5", root);
        pc5_mkdirs(missing);
        pc5_cover_save(file, &cover, 0xff2050e0u);
    }
    pc5_image_free(&cover);

    CHECK(!pc5_home_find(tiles, "neon", id) && !id[0], "no tile yet");
    CHECK(!pc5_home_add(tiles, template_dir, root, "neon", "neon", "Neon Drift", id, error, sizeof(error)),
          "tile made: %s", error);
    CHECK(strlen(id) == 9 && !strncmp(id, "PPSA99", 6) && atoi(id + 4) >= 99200 && atoi(id + 4) <= 99899,
          "in the forwarders' range: %s", id);
    path(file, sizeof(file), "%s/%s/eboot.bin", tiles, id);
    CHECK(has_text(file, "forwarder program"), "the forwarder program, byte for byte");
    path(file, sizeof(file), "%s/%s/sce_module/libc.prx", tiles, id);
    CHECK(has_text(file, "its libc"), "and its libc");
    path(file, sizeof(file), "%s/%s/forwarder.json", tiles, id);
    CHECK(has_text(file, "\"target\": \"" PC5_TITLE_ID "\"") && has_text(file, "\"--rom\"") &&
          has_text(file, "\"neon\"") && has_text(file, "\"--exit-after-game\"") &&
          has_text(file, "\"format\": \"ps5-forwarder/1\"") && has_text(file, "PC5 " PC5_VERSION),
          "it starts PC5 with the game, closing after it");
    path(file, sizeof(file), "%s/%s/sce_sys/param.json", tiles, id);
    CHECK(has_text(file, "\"titleName\": \"Neon Drift\"") && has_text(file, id) &&
          has_text(file, "PS5FORWARDER0000"), "named after the game");
    path(file, sizeof(file), "%s/%s/sce_sys/icon0.png", tiles, id);
    CHECK(!pc5_read_file(file, &png, 4u << 20) && !pc5_image_decode(png.data, png.len, &icon) && icon.w == 512 &&
          icon.h == 512, "a 512 x 512 PNG picture: %dx%d", icon.w, icon.h);
    if (icon.px) {
        uint32_t top = icon.px[100 * 512 + 256], bottom = icon.px[400 * 512 + 256], edge = icon.px[256 * 512 + 4];
        CHECK((top & 0xff) > 0xc0 && ((top >> 16) & 0xff) < 0x40, "the cover in front, its blue top: %08x", top);
        CHECK(((bottom >> 16) & 0xff) > 0xc0, "its orange bottom: %08x", bottom);
        CHECK(((edge >> 16) & 0xff) < 0xa0 && (edge & 0xff) < 0xa0, "a dimmed copy behind: %08x", edge);
    }
    pc5_image_free(&icon);
    pc5_buf_free(&png);
    CHECK(pc5_home_find(tiles, "neon", again) && !strcmp(again, id), "found again: %s", again);
    CHECK(!pc5_home_find(tiles, "neo", again), "only that game's");
    CHECK(!pc5_home_add(tiles, template_dir, root, "neon", "neon", "Neon Drift", again, error, sizeof(error)) &&
          !strcmp(again, id), "made twice: the same tile");
    /* Another app's tile for a game of the same name is left alone. */
    path(file, sizeof(file), "%s/PPSA99300/forwarder.json", tiles);
    write_text(file, "{\"format\": \"ps5-forwarder/1\", \"target\": \"PPSA99008\", \"args\": [\"--rom\", \"neon\"]}");
    path(file, sizeof(file), "%s/PPSA99300/eboot.bin", tiles);
    write_text(file, "x");
    /* A game without a cover gets a plain picture. */
    CHECK(!pc5_home_add(tiles, template_dir, root, "plain", "plain", "Plain", again, error, sizeof(error)) &&
          strcmp(again, id), "a game without a cover: its own tile %s", again);
    CHECK(pc5_home_remove(tiles, "neon") == 1, "taken away");
    CHECK(!pc5_home_find(tiles, "neon", again), "gone");
    path(file, sizeof(file), "%s/%s", tiles, id);
    CHECK(!pc5_exists(file), "its folder deleted");
    path(file, sizeof(file), "%s/PPSA99300/forwarder.json", tiles);
    CHECK(pc5_exists(file), "another app's tile stays");
    CHECK(pc5_home_remove(tiles, "neon") == 0 && pc5_home_remove(tiles, "plain") == 1, "each game's own");
    path(missing, sizeof(missing), "%s/nowhere", work);
    CHECK(pc5_home_add(tiles, missing, root, "neon", "neon", "Neon Drift", id, error, sizeof(error)) == -1 &&
          strstr(error, "pc5/forwarder") && !id[0], "no tile program: %s", error);
}

/* A PC5 game in root as an install leaves it, without its profile when profile is 0. */
static void make_tile_game(const char *root, const char *id, const char *title, const char *program, int profile)
{
    Pc5Info info = { "", "", "", 64, PC5_API_D3D11, 5, 1, "", "" };
    char file[PC5_PATH], from[PC5_PATH], text[2048], error[PC5_ERROR];

    pc5_copy(info.title, sizeof(info.title), title);
    pc5_copy(info.folder, sizeof(info.folder), title);
    snprintf(info.exe, sizeof(info.exe), "%s.exe", id);
    path(file, sizeof(file), "%s/profiles", root);
    pc5_mkdirs(file);
    path(file, sizeof(file), "%s/prefixes/%s/system.reg", root, id);
    write_text(file, "WINE REGISTRY Version 2\n");
    path(file, sizeof(file), "%s/prefixes/%s/user.reg", root, id);
    write_text(file, "WINE REGISTRY Version 2\n");
    pc5_info_text(&info, text, sizeof(text));
    path(file, sizeof(file), "%s/prefixes/%s/pc5/info.txt", root, id);
    write_text(file, text);
    path(from, sizeof(from), "%s/%s", programs, program);
    path(file, sizeof(file), "%s/prefixes/%s/drive_c/Games/%s/%s.exe", root, id, title, id);
    copy_file(from, file);
    if (profile) CHECK(!pc5_profile_refresh(root, id, error, sizeof(error)), "%s's profile: %s", id, error);
}

/* A tile's game readied without PC5's screens: its tile file read, what a stopped
 * install left finished, the game found, .NET copied, its profile written again. */
static void test_tile(void)
{
    char app[PC5_PATH], root[PC5_PATH], file[PC5_PATH], game[PC5_ID], error[PC5_ERROR];
    Pc5TilePrep *prep = pc5_alloc(sizeof(*prep));
    Pc5Buf text = { 0 };
    int step, steps;

    path(app, sizeof(app), "%s/tile-app", work);
    path(root, sizeof(root), "%s/tile-root", work);
    pc5_mkdirs(app);

    /* The tile file. */
    CHECK(pc5_tile_read(app, game, error, sizeof(error)) == 0 && !game[0], "PC5 itself: not a tile");
    path(file, sizeof(file), "%s/pc5-tile.json", app);
    write_text(file, "{\n  \"format\": \"pc5-tile/1\",\n  \"game\": \"tilegame\",\n  \"pc5\": \"1.9.6\"\n}\n");
    CHECK(pc5_tile_read(app, game, error, sizeof(error)) == 1 && !strcmp(game, "tilegame"), "a tile: %s", game);
    write_text(file, "{\"format\": \"pc5-tile/2\", \"game\": \"tilegame\"}");
    CHECK(pc5_tile_read(app, game, error, sizeof(error)) == -1 && !game[0] && strstr(error, "doesn't know"),
          "another format: %s", error);
    write_text(file, "{\"format\": \"pc5-tile/1\", \"game\": \"../tilegame\"}");
    CHECK(pc5_tile_read(app, game, error, sizeof(error)) == -1 && !game[0], "not a profile id: %s", error);
    write_text(file, "{\"format\": \"pc5-tile/1\", \"game\": \"TileGame\"}");
    CHECK(pc5_tile_read(app, game, error, sizeof(error)) == -1, "upper case isn't an id");
    write_text(file, "{\"format\": \"pc5-tile/1\", \"game\": 7}");
    CHECK(pc5_tile_read(app, game, error, sizeof(error)) == -1, "a number isn't an id");
    write_text(file, "{\"format\": \"pc5-tile/1\"}");
    CHECK(pc5_tile_read(app, game, error, sizeof(error)) == -1 && strstr(error, "which game"), "no game: %s", error);
    write_text(file, "not json");
    CHECK(pc5_tile_read(app, game, error, sizeof(error)) == -1 && strstr(error, "can't be read"), "not JSON: %s", error);

    /* A game whose install stopped before its profile was written, and a stopped install's
     * staging folder: both finished before the game starts. */
    make_tile_game(root, "tilegame", "Tile Game", "game64.exe", 0);
    path(file, sizeof(file), "%s/prefixes/.installing-other/drive_c/big.pak", root);
    write_text(file, "half a game");
    pc5_tile_prepare_begin(prep, root, app, "tilegame");
    for (steps = 0; !(step = pc5_tile_prepare_step(prep, 5)) && steps < 1000; steps++) {}
    CHECK(step == 1, "readied: %d %s", step, prep->error);
    CHECK(!strcmp(prep->title, "Tile Game") && prep->found && prep->pc5 && !prep->copying, "its name: %s", prep->title);
    CHECK(pc5_tile_prepare_step(prep, 5) == 1, "ready stays ready");
    pc5_tile_prepare_end(prep);
    path(file, sizeof(file), "%s/profiles/tilegame.profile", root);
    CHECK(!pc5_read_file(file, &text, 64 * 1024) && !pc5_buf_terminate(&text) &&
          strstr((char *)text.data, "id = tilegame") && strstr((char *)text.data, "TileGame.exe") == NULL &&
          strstr((char *)text.data, "tilegame.exe"), "its profile written: %s", text.len ? (char *)text.data : "");
    pc5_buf_free(&text);
    path(file, sizeof(file), "%s/prefixes/.installing-other", root);
    CHECK(!pc5_exists(file), "the stopped install cleared");
    path(file, sizeof(file), "%s/pc5/last.txt", root);
    CHECK(!pc5_read_file(file, &text, 256) && text.len == 8 && !memcmp(text.data, "tilegame", 8), "the last game played");
    pc5_buf_free(&text);

    /* Its controls changed in PC5 since: the profile follows at the tile's next start. */
    {
        Pc5Controls controls;
        pc5_controls_load(root, "tilegame", &controls);
        controls.mode = PC5_PAD_KEYBOARD;
        CHECK(!pc5_controls_save(root, "tilegame", &controls), "controls saved");
        pc5_tile_prepare_begin(prep, root, app, "tilegame");
        while (!(step = pc5_tile_prepare_step(prep, 5))) {}
        pc5_tile_prepare_end(prep);
        path(file, sizeof(file), "%s/profiles/tilegame.profile", root);
        CHECK(step == 1 && !pc5_read_file(file, &text, 64 * 1024) && !pc5_buf_terminate(&text) &&
              strstr((char *)text.data, "mode = keyboard"), "the new controller mode: %s",
              text.len ? (char *)text.data : "");
        pc5_buf_free(&text);
    }

    /* Its game uninstalled: the reason PC5's screens give. */
    pc5_tile_prepare_begin(prep, root, app, "gone");
    while (!(step = pc5_tile_prepare_step(prep, 5))) {}
    CHECK(step == -1 && strstr(prep->error, "isn't in PC5 any more") && !strcmp(prep->title, "gone") && !prep->found,
          "a game no longer there: %s", prep->error);
    CHECK(pc5_tile_prepare_step(prep, 5) == -1, "and stays so");
    pc5_tile_prepare_end(prep);
    /* Its Windows folder gone. */
    make_tile_game(root, "broken", "Broken", "game64.exe", 1);
    path(file, sizeof(file), "%s/prefixes/broken/system.reg", root);
    unlink(file);
    pc5_tile_prepare_begin(prep, root, app, "broken");
    while (!(step = pc5_tile_prepare_step(prep, 5))) {}
    CHECK(step == -1 && strstr(prep->error, "Windows folder is missing") && !strcmp(prep->title, "Broken"),
          "a game that can't start: %s", prep->error);
    pc5_tile_prepare_end(prep);

    /* A .NET game started the first time: Wine Mono's runtime copied from the tile's app. */
    make_tile_game(root, "dotgame", "Dot Game", "dotnet.exe", 1);
    path(file, sizeof(file), "%s/pc5/mono-version.txt", app);
    write_text(file, "11.3.0\n");
    path(file, sizeof(file), "%s/pc5/mono.list", app);
    write_text(file, "mono-2.0/\nmono-2.0/bin/\nmono-2.0/bin/libmono-2.0-x86_64.dll\n");
    path(file, sizeof(file), "%s/pc5/mono/mono-2.0/bin/libmono-2.0-x86_64.dll", app);
    write_text(file, "libmono");
    pc5_tile_prepare_begin(prep, root, app, "dotgame");
    for (steps = 0; !(step = pc5_tile_prepare_step(prep, 1)) && steps < 1000; steps++) {}
    pc5_tile_prepare_end(prep);
    path(file, sizeof(file), "%s/pc5/mono-11.3.0/.complete", root);
    CHECK(step == 1 && pc5_is_file(file), "its runtime copied: %d %s", step, prep->error);
    /* The app's runtime incomplete: why, before Wine is started. */
    path(file, sizeof(file), "%s/pc5/mono-11.3.0", root);
    {
        Pc5Remove remove = { 0 };
        pc5_tree_delete_begin(&remove, file);
        while (pc5_remove_step(&remove, 5)) {}
        pc5_remove_end(&remove);
    }
    path(file, sizeof(file), "%s/pc5/mono/mono-2.0/bin/libmono-2.0-x86_64.dll", app);
    unlink(file);
    pc5_tile_prepare_begin(prep, root, app, "dotgame");
    while (!(step = pc5_tile_prepare_step(prep, 5))) {}
    CHECK(step == -1 && strstr(prep->error, ".NET runtime"), "a runtime file missing: %s", prep->error);
    pc5_tile_prepare_end(prep);
    pc5_free(prep);
}

/* Where needle is in data, or NULL. */
static const uint8_t *find_bytes(const void *data, size_t size, const void *needle, size_t n)
{
    const uint8_t *d = data;
    for (size_t i = 0; n && i + n <= size; i++)
        if (!memcmp(d + i, needle, n)) return d + i;
    return NULL;
}

/* A made-up lapy.elf with PC5's ID where the real one has it: two inlined compares and
 * its log line's name. */
static size_t fake_lapy(uint8_t *out, size_t size)
{
    static const uint8_t compare_a[] = { 0x48, 0xb8, 'P', 'P', 'S', 'A', '1', '1', '0', '1', 0x0f, 0xb7, 0x8d, 0x18,
                                         0xff, 0xff, 0xff, 0x48, 0x33, 0x85, 0x10, 0xff, 0xff, 0xff, 0xbb, 0x04, 0, 0,
                                         0, 0x48, 0x83, 0xf1, '8', 0x48, 0x09, 0xc1 };
    static const uint8_t compare_b[] = { 0x48, 0xb9, 'P', 'P', 'S', 'A', '1', '1', '0', '1', 0x0f, 0xb7, 0x55, 0xb8,
                                         0x48, 0x33, 0x4d, 0xb0, 0x31, 0xc0, 0x48, 0x83, 0xf2, '8', 0x48, 0x09, 0xca };
    static const char name[] = "LAPYOWN\0PPSA11018\0Unknown kernel";
    size_t n = 0;
    memset(out, 0xcc, size);
    memcpy(out, "\x7f" "ELF", 4);
    n = 64;
    memcpy(out + n, compare_a, sizeof(compare_a)), n += sizeof(compare_a) + 40;
    memcpy(out + n, compare_b, sizeof(compare_b)), n += sizeof(compare_b) + 40;
    memcpy(out + n, name, sizeof(name)), n += sizeof(name) + 16;
    return n;
}

/* The home screen's background as the shell reads it: a DDS of BC7 mode 6 blocks. */
static void test_dds(void)
{
    uint32_t pixels[8 * 4];
    uint8_t out[148 + 2 * 16];

    for (int i = 0; i < 8 * 4; i++) pixels[i] = i < 4 * 4 ? 0xff102030u : 0xffc08040u;   /* two blocks... */
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 8; x++) pixels[y * 8 + x] = x < 4 ? 0xff102030u : 0xffc08040u;
    CHECK(pc5_dds_size(8, 4) == sizeof(out), "size: %zu", pc5_dds_size(8, 4));
    CHECK(!pc5_dds_bc7(pixels, 8, 4, 8, out) && !memcmp(out, "DDS ", 4) && out[4] == 124 && out[12] == 4 && out[16] == 8 &&
          !memcmp(out + 84, "DX10", 4) && out[128] == 98 && out[132] == 3 && out[140] == 1 && out[144] == 1,
          "a DX10 header, BC7_UNORM, one 2D surface, alpha straight");
    CHECK((out[148] & 0x7f) == 0x40 && (out[164] & 0x7f) == 0x40, "mode 6 blocks: %02x %02x", out[148], out[164]);
    CHECK(memcmp(out + 148, out + 164, 16), "each block its own colour");
    CHECK(pc5_dds_bc7(pixels, 6, 4, 8, out) == -1 && pc5_dds_bc7(pixels, 8, 3, 8, out) == -1 &&
          pc5_dds_bc7(pixels, 8, 4, 4, out) == -1, "sides of 4s only, a whole row");
}

/* A log line kept, for the checks. */
static void test_log_line(void *context, const char *line)
{
    snprintf((char *)context, PC5_PATH + 200, "%s", line);
}

/* Making a game's tile as an app of its own that runs from PC5's folder: only what the
 * console starts in it, PC5's folder named as its runtime; made again in place; refreshed
 * when PC5's program changes; taken away leaving PC5 whole. */
static void test_tile_make(void)
{
    char pc5[PC5_PATH], tiles[PC5_PATH], root[PC5_PATH], file[PC5_PATH], other[PC5_PATH], id[16], again[16],
         game[PC5_ID], runtime[PC5_PATH], error[PC5_ERROR];
    uint8_t *lapy = pc5_alloc(4096), *copy = pc5_alloc(4096);
    size_t lapy_size = fake_lapy(lapy, 4096);
    Pc5Buf text = { 0 }, param = { 0 };
    struct stat a, b;

    /* The helper's ID changed where the console's helper checks it. */
    memcpy(copy, lapy, lapy_size);
    CHECK(!pc5_tile_lapy_patch(copy, lapy_size, "PPSA99437", error, sizeof(error)), "patched: %s", error);
    CHECK(!find_bytes(copy, lapy_size, "PPSA1101", 8) && find_bytes(copy, lapy_size, "PPSA9943", 8) &&
          find_bytes(copy, lapy_size, "PPSA99437\0", 10), "every PPSA11018 now PPSA99437");
    {
        const uint8_t *x = find_bytes(copy, lapy_size, "\x48\x83\xf1", 3), *y = find_bytes(copy, lapy_size, "\x48\x83\xf2", 3);
        CHECK(x && y && x[3] == '7' && y[3] == '7', "and the ninth character's compares");
        size_t changed = 0;
        for (size_t i = 0; i < lapy_size; i++) changed += copy[i] != lapy[i];
        CHECK(changed == 15, "15 bytes changed: %zu", changed);
    }
    memcpy(copy, lapy, lapy_size);
    CHECK(pc5_tile_lapy_patch(copy, lapy_size, "CUSA99437", error, sizeof(error)) == -1, "a tile's ID is PPSA");
    CHECK(pc5_tile_lapy_patch(copy, lapy_size, "PPSA9943", error, sizeof(error)) == -1, "and nine characters");
    copy[lapy_size - 40] = 'X';
    memcpy(copy + 64 + 9, "9", 1);
    CHECK(pc5_tile_lapy_patch(copy, lapy_size, "PPSA99437", error, sizeof(error)) == -1 && strstr(error, "isn't the one"),
          "another helper: %s", error);

    /* PC5's own param.json, the tile's. */
    path(file, sizeof(file), "%s/../../sce_sys/param.json", fixtures);
    CHECK(!pc5_read_file(file, &text, 64 * 1024), "PC5's param.json");
    CHECK(!pc5_tile_param((const char *)text.data, text.len, "PPSA99437", "Neon \"Drift\" \\ II", &param) &&
          strstr((char *)param.data, "\"titleId\": \"PPSA99437\"") && strstr((char *)param.data, "\"conceptId\": \"99437\"") &&
          strstr((char *)param.data, "\"contentId\": \"UP0000-PPSA99437_00-PC5TILE000000000\"") &&
          strstr((char *)param.data, "\"titleName\": \"Neon \\\"Drift\\\" \\\\ II\"") &&
          !strstr((char *)param.data, "11018") && strstr((char *)param.data, "\"attribute3\": 64"),
          "the tile's: %s", param.len ? (char *)param.data : "");
    pc5_buf_free(&param);
    CHECK(pc5_tile_param("{\"titleId\": \"PPSA11018\"}", 24, "PPSA99437", "x", &param) == -1, "not a whole param.json");

    /* PC5's folder as installed. */
    path(tiles, sizeof(tiles), "%s/tile-drive/homebrew", work);
    path(pc5, sizeof(pc5), "%s/PPSA11018", tiles);
    path(root, sizeof(root), "%s/tile-drive/prospero-win", work);
    path(file, sizeof(file), "%s/eboot.bin", pc5);
    write_text(file, "PC5's program");
    path(file, sizeof(file), "%s/sce_module/libc.prx", pc5);
    write_text(file, "its libc");
    path(file, sizeof(file), "%s/win/wine/lib/wine/x86_64-unix/ntdll.prx", pc5);
    write_text(file, "Wine");
    path(file, sizeof(file), "%s/pc5/fonts.bin", pc5);
    write_text(file, "fonts");
    path(file, sizeof(file), "%s/sce_sys", pc5);
    pc5_mkdirs(file);
    path(file, sizeof(file), "%s/sce_sys/param.json", pc5);
    CHECK(!pc5_write_file(file, text.data, text.len), "PC5's param.json in its folder");
    path(file, sizeof(file), "%s/sce_sys/pic0.dds", pc5);
    write_text(file, "PC5's background");
    path(file, sizeof(file), "%s/lapy.elf", pc5);
    pc5_write_file(file, lapy, lapy_size);
    path(file, sizeof(file), "%s/profiles", root);
    pc5_mkdirs(file);
    pc5_buf_free(&text);

    Pc5TileMake make = { tiles, pc5, root, "neon", "neon", "Neon Drift", NULL };
    CHECK(pc5_tile_make(&make, id, error, sizeof(error)) == PC5_TILE_MADE && strlen(id) == 9 && !strncmp(id, "PPSA99", 6),
          "made: %s %s", id, error);
    path(file, sizeof(file), "%s/%s/eboot.bin", tiles, id);
    CHECK(!stat(file, &a) && (a.st_mode & 0111) && !pc5_read_file(file, &text, 64) && text.len == 13 &&
          !memcmp(text.data, "PC5's program", 13), "PC5's program in it, runnable");
    pc5_buf_free(&text);
    path(file, sizeof(file), "%s/%s/sce_module/libc.prx", tiles, id);
    CHECK(!stat(file, &a) && (a.st_mode & 0111), "and its libc");
    path(file, sizeof(file), "%s/%s/win", tiles, id);
    path(other, sizeof(other), "%s/%s/pc5", tiles, id);
    CHECK(!pc5_exists(file) && !pc5_exists(other), "no Wine and no PC5 files in it: they are PC5's folder's");
    /* No background of its own: PC5's behind its tile. */
    path(file, sizeof(file), "%s/%s/sce_sys/pic0.dds", tiles, id);
    CHECK(!pc5_read_file(file, &text, 64) && text.len == 16 && !memcmp(text.data, "PC5's background", 16),
          "PC5's background behind it");
    pc5_buf_free(&text);
    path(file, sizeof(file), "%s/%s/lapy.elf", tiles, id);
    CHECK(!stat(file, &a) && (a.st_mode & 0111) && !pc5_read_file(file, &text, 8192) && find_bytes(text.data, text.len, id, 9) &&
          !find_bytes(text.data, text.len, "PPSA1101", 8), "its own lapy.elf, for its ID");
    pc5_buf_free(&text);
    path(file, sizeof(file), "%s/lapy.elf", pc5);
    CHECK(!pc5_read_file(file, &text, 8192) && text.len == lapy_size && !memcmp(text.data, lapy, lapy_size),
          "PC5's lapy.elf untouched");
    pc5_buf_free(&text);
    path(file, sizeof(file), "%s/%s/sce_sys/param.json", tiles, id);
    CHECK(!pc5_read_file(file, &text, 64 * 1024) && !pc5_buf_terminate(&text) && strstr((char *)text.data, id) &&
          strstr((char *)text.data, "\"titleName\": \"Neon Drift\""), "its param.json, the game's name");
    pc5_buf_free(&text);
    path(file, sizeof(file), "%s/%s/sce_sys/icon0.png", tiles, id);
    {
        Pc5Image icon = { 0 };
        CHECK(!pc5_read_file(file, &text, 4u << 20) && !pc5_image_decode(text.data, text.len, &icon) && icon.w == 512,
              "its own picture");
        pc5_image_free(&icon);
        pc5_buf_free(&text);
    }
    path(file, sizeof(file), "%s/%s", tiles, id);
    CHECK(pc5_tile_read_all(file, game, runtime, sizeof(runtime), error, sizeof(error)) == 1 && !strcmp(game, "neon") &&
          !strcmp(runtime, pc5), "its game and its runtime, PC5's folder: %s %s", game, runtime);
    CHECK(pc5_home_find(tiles, "neon", again) && !strcmp(again, id), "found as the game's tile");
    CHECK(count_entries(tiles, ".staging-") == 0, "nothing staged left");

    /* A tile's file naming its runtime: an absolute path, or none at all (a hand-made copy). */
    path(other, sizeof(other), "%s/tile-files", work);
    path(file, sizeof(file), "%s/pc5-tile.json", other);
    write_text(file, "{\"format\": \"pc5-tile/1\", \"game\": \"neon\"}");
    CHECK(pc5_tile_read_all(other, game, runtime, sizeof(runtime), error, sizeof(error)) == 1 && !runtime[0],
          "no runtime: in the tile");
    write_text(file, "{\"format\": \"pc5-tile/1\", \"game\": \"neon\", \"runtime\": \"data/homebrew/PPSA11018\"}");
    CHECK(pc5_tile_read_all(other, game, runtime, sizeof(runtime), error, sizeof(error)) == -1 && strstr(error, "where PC5"),
          "a relative runtime: %s", error);
    write_text(file, "{\"format\": \"pc5-tile/1\", \"game\": \"neon\", \"runtime\": \"/data/homebrew/../x\"}");
    CHECK(pc5_tile_read_all(other, game, runtime, sizeof(runtime), error, sizeof(error)) == -1, "no .. in it");
    write_text(file, "{\"format\": \"pc5-tile/1\", \"game\": \"neon\", \"runtime\": \"/data/My \\\"PC5\\\"\"}");
    CHECK(pc5_tile_read_all(other, game, runtime, sizeof(runtime), error, sizeof(error)) == 1 &&
          !strcmp(runtime, "/data/My \"PC5\""), "quotes in it: %s", runtime);

    /* Made again: the same ID. */
    CHECK(pc5_tile_make(&make, again, error, sizeof(error)) == PC5_TILE_MADE && !strcmp(again, id), "made again: %s", error);

    /* PC5 updated: the tile gets its new program; one already current is left. */
    CHECK(pc5_tile_refresh(tiles, pc5, root, NULL, NULL, NULL) == 0, "current: nothing to do");
    path(file, sizeof(file), "%s/eboot.bin", pc5);
    write_text(file, "PC5's new program");
    {
        char line[PC5_PATH + 200] = "";
        CHECK(pc5_tile_refresh(tiles, pc5, root, NULL, test_log_line, line) == 1 && strstr(line, "PC5 tile refreshed") &&
              strstr(line, id) && strstr(line, "program=1 art=0"), "refreshed: %s", line);
    }
    path(file, sizeof(file), "%s/%s/eboot.bin", tiles, id);
    CHECK(!pc5_read_file(file, &text, 64) && text.len == 17 && !memcmp(text.data, "PC5's new program", 17),
          "its new program");
    pc5_buf_free(&text);
    path(file, sizeof(file), "%s/%s/sce_sys/param.json", tiles, id);
    CHECK(pc5_exists(file), "its name and picture kept");
    CHECK(pc5_tile_refresh(tiles, pc5, root, NULL, NULL, NULL) == 0, "and current again");

    /* Its art: the game's background behind its tile, 1920x1080 and a 4K BC7 DDS, and its tile
     * picture in front; the console's copies of a registered tile too. */
    {
        char user[PC5_PATH], dir[PC5_PATH];
        Pc5Image background = { 0 }, picture = { 0 };
        path(user, sizeof(user), "%s/tile-user", work);
        path(dir, sizeof(dir), "%s/appmeta/%s", user, id);
        pc5_mkdirs(dir);
        path(dir, sizeof(dir), "%s/app/%s/sce_sys", user, id);
        pc5_mkdirs(dir);
        CHECK(!pc5_image_new(&background, PC5_BACKGROUND_WIDTH, PC5_BACKGROUND_HEIGHT), "a background");
        for (int i = 0; background.px && i < background.w * background.h; i++) background.px[i] = 0xff3060c0u;
        path(file, sizeof(file), "%s/prefixes/neon/pc5", root);
        pc5_mkdirs(file);
        path(file, sizeof(file), "%s/prefixes/neon/pc5/background.bin", root);
        CHECK(!pc5_cover_save(file, &background, 0), "saved as PC5 keeps it");
        CHECK(!pc5_image_new(&picture, PC5_ICON_SIZE, PC5_ICON_SIZE), "a tile picture");
        for (int i = 0; picture.px && i < picture.w * picture.h; i++) picture.px[i] = 0xffe02040u;
        path(file, sizeof(file), "%s/prefixes/neon/pc5/icon.bin", root);
        CHECK(!pc5_cover_save(file, &picture, 0), "saved as PC5 keeps it");
        pc5_image_free(&background);
        pc5_image_free(&picture);
        /* Newer than the tile's art: the refresh makes it again (the files' times, a second apart). */
        path(file, sizeof(file), "%s/%s/sce_sys/icon0.png", tiles, id);
        {
            struct timespec old[2] = { { 1000000000, 0 }, { 1000000000, 0 } };
            utimensat(AT_FDCWD, file, old, 0);
        }
        char line[PC5_PATH + 200] = "";
        CHECK(pc5_tile_refresh(tiles, pc5, root, user, test_log_line, line) == 1 && strstr(line, "program=0 art=1"),
              "its art again: %s", line);
        Pc5Image shown = { 0 };
        CHECK(!pc5_read_file(file, &text, 4u << 20) && !pc5_image_decode(text.data, text.len, &shown) &&
              shown.w == 512 && (shown.px[256 * 512 + 256] & 0xffffffu) == 0xe02040u,
              "its tile picture in front: %06x", shown.px ? (unsigned)(shown.px[256 * 512 + 256] & 0xffffffu) : 0);
        pc5_image_free(&shown);
        pc5_buf_free(&text);
        path(file, sizeof(file), "%s/%s/sce_sys/pic0.png", tiles, id);
        CHECK(!pc5_read_file(file, &text, 8u << 20) && !pc5_image_decode(text.data, text.len, &shown) &&
              shown.w == 1920 && shown.h == 1080 && (shown.px[540 * 1920 + 960] & 0xffffffu) == 0x3060c0u,
              "the game's background behind it, 1920x1080: %dx%d", shown.w, shown.h);
        pc5_image_free(&shown);
        pc5_buf_free(&text);
        path(file, sizeof(file), "%s/%s/sce_sys/pic0.dds", tiles, id);
        path(other, sizeof(other), "%s/%s/sce_sys/pic1.dds", tiles, id);
        {
            Pc5Buf dds = { 0 };
            CHECK(!pc5_read_file(file, &text, 16u << 20) && text.len == pc5_dds_size(3840, 2160) &&
                  !memcmp(text.data, "DDS ", 4) && text.data[128] == 98 && !pc5_read_file(other, &dds, 16u << 20) &&
                  dds.len == text.len && !memcmp(dds.data, text.data, text.len), "and as 4K BC7 DDS, both: %zu", text.len);
            pc5_buf_free(&dds);
        }
        pc5_buf_free(&text);
        path(file, sizeof(file), "%s/appmeta/%s/icon0.png", user, id);
        path(other, sizeof(other), "%s/app/%s/sce_sys/pic0.dds", user, id);
        CHECK(pc5_is_file(file) && pc5_is_file(other), "the console's copies too");
        CHECK(pc5_tile_refresh(tiles, pc5, root, user, NULL, NULL) == 0, "and nothing to do after");
    }

    /* PC5 without its helper: nothing made, nothing left. */
    path(file, sizeof(file), "%s/lapy.elf", pc5);
    path(other, sizeof(other), "%s/lapy.elf.kept", pc5);
    rename(file, other);
    Pc5TileMake second = { tiles, pc5, root, "plain", "plain", "Plain", NULL };
    CHECK(pc5_tile_make(&second, again, error, sizeof(error)) == PC5_TILE_FAILED && !again[0] && strstr(error, "lapy.elf"),
          "no helper: %s", error);
    CHECK(!pc5_home_find(tiles, "plain", again), "no tile for it");
    CHECK(count_entries(tiles, ".staging-") == 0, "nothing staged left after it");
    rename(other, file);
    Pc5TileMake relative = { tiles, "homebrew/PPSA11018", root, "plain", "plain", "Plain", NULL };
    CHECK(pc5_tile_make(&relative, again, error, sizeof(error)) == PC5_TILE_FAILED, "PC5's folder as an absolute path only");

    /* A refresh cut short after its other files (here: the helper, gone): the tile keeps its old
     * program, so the next refresh does it all again. */
    path(file, sizeof(file), "%s/eboot.bin", pc5);
    write_text(file, "PC5's third program");
    path(file, sizeof(file), "%s/lapy.elf", pc5);
    path(other, sizeof(other), "%s/lapy.elf.kept", pc5);
    rename(file, other);
    CHECK(pc5_tile_refresh(tiles, pc5, root, NULL, NULL, NULL) == 0, "the helper gone: not refreshed");
    path(file, sizeof(file), "%s/%s/eboot.bin", tiles, id);
    CHECK(!pc5_read_file(file, &text, 64) && text.len == 17 && !memcmp(text.data, "PC5's new program", 17),
          "its old program kept, not half new");
    pc5_buf_free(&text);
    path(file, sizeof(file), "%s/lapy.elf", pc5);
    rename(other, file);
    CHECK(pc5_tile_refresh(tiles, pc5, root, NULL, NULL, NULL) == 1, "and done whole the next time");
    path(file, sizeof(file), "%s/%s/eboot.bin", tiles, id);
    CHECK(!pc5_read_file(file, &text, 64) && text.len == 19 && !memcmp(text.data, "PC5's third program", 19),
          "its new program then");
    pc5_buf_free(&text);
    path(file, sizeof(file), "%s/%s/pc5-tile.json", tiles, id);
    CHECK(!pc5_read_file(file, &text, 4096) && !pc5_buf_terminate(&text) && strstr((char *)text.data, "\"prefix\": \"neon\""),
          "its game's prefix named: %s", text.len ? (char *)text.data : "");
    pc5_buf_free(&text);

    /* A second PC5 (a test build in another folder): its tiles stay its own while it is there,
     * and come to this one once it is gone. */
    {
        char second_pc5[PC5_PATH], from[PC5_PATH], to[PC5_PATH];
        static const char *const files[] = { "eboot.bin", "sce_module/libc.prx", "lapy.elf", "sce_sys/param.json" };
        path(second_pc5, sizeof(second_pc5), "%s/PPSA11018-test", tiles);
        for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
            path(from, sizeof(from), "%s/%s", pc5, files[i]);
            path(to, sizeof(to), "%s/%s", second_pc5, files[i]);
            copy_file(from, to);
        }
        path(to, sizeof(to), "%s/eboot.bin", second_pc5);
        write_text(to, "the test build");
        CHECK(pc5_tile_refresh(tiles, second_pc5, root, NULL, NULL, NULL) == 0, "the test build leaves this PC5's tile");
        path(file, sizeof(file), "%s/%s", tiles, id);
        CHECK(pc5_tile_read_all(file, game, runtime, sizeof(runtime), error, sizeof(error)) == 1 && !strcmp(runtime, pc5),
              "still this PC5's: %s", runtime);
        path(from, sizeof(from), "%s/eboot.bin", pc5);
        path(to, sizeof(to), "%s/eboot.bin.away", pc5);
        rename(from, to);
        CHECK(pc5_tile_refresh(tiles, second_pc5, root, NULL, NULL, NULL) == 1, "this PC5 gone: the test build's now");
        CHECK(pc5_tile_read_all(file, game, runtime, sizeof(runtime), error, sizeof(error)) == 1 &&
              !strcmp(runtime, second_pc5), "its runtime the test build: %s", runtime);
        rename(to, from);
        CHECK(pc5_tile_refresh(tiles, pc5, root, NULL, NULL, NULL) == 0, "back: the test build's while it is there");
        path(to, sizeof(to), "rm -rf '%s'", second_pc5);
        run(to);
        CHECK(pc5_tile_refresh(tiles, pc5, root, NULL, NULL, NULL) == 1, "and this PC5's again once it is gone");
    }

    /* Left by a PC5 stopped part way: a staged tile and an old one put aside, deleted. */
    path(file, sizeof(file), "%s/.staging-PPSA99777/eboot.bin", tiles);
    write_text(file, "half a tile");
    path(file, sizeof(file), "%s/.old-PPSA99778/eboot.bin", tiles);
    write_text(file, "an old tile");
    pc5_tile_refresh(tiles, pc5, root, NULL, NULL, NULL);
    CHECK(count_entries(tiles, ".staging-") == 0 && count_entries(tiles, ".old-") == 0, "leftovers deleted");

    /* A new tile's ID is none the console has registered, and its art goes only into its own
     * folder: another app's registration under /user is left alone. */
    {
        char user[PC5_PATH], dir[PC5_PATH], made_id[16];
        path(user, sizeof(user), "%s/tile-user-new", work);
        for (int n = 99201; n < 99900; n += 2) {   /* every odd one registered by another app */
            path(dir, sizeof(dir), "%s/app/PPSA%05d/sce_sys", user, n);
            pc5_mkdirs(dir);
            path(dir, sizeof(dir), "%s/appmeta/PPSA%05d", user, n);
            pc5_mkdirs(dir);
        }
        int registered = 0;
        for (int round = 0; round < 20; round++) {
            Pc5TileMake fresh = { tiles, pc5, root, "fresh", "fresh", "Fresh", user };
            made_id[0] = 0;
            if (pc5_tile_make(&fresh, made_id, error, sizeof(error)) != PC5_TILE_MADE) { registered = 1; break; }
            registered |= (made_id[8] - '0') % 2 == 1;
            path(dir, sizeof(dir), "%s/app/%s/sce_sys/icon0.png", user, made_id);
            registered |= pc5_exists(dir);
            pc5_home_remove(tiles, "fresh");
        }
        CHECK(!registered, "20 new tiles: never a registered app's ID, no art under /user: %s %s", made_id, error);
        path(dir, sizeof(dir), "%s/app/PPSA99201/sce_sys/icon0.png", user);
        CHECK(!pc5_exists(dir), "another app's registration untouched");
        path(dir, sizeof(dir), "rm -rf '%s'", user);
        run(dir);
    }

    /* A PC5 folder whose path is longer than a tile's eboot holds: refused, nothing made. */
    {
        char long_pc5[PC5_PATH];
        Pc5TileMake far = { tiles, long_pc5, root, "plain", "plain", "Plain", NULL };
        int n = snprintf(long_pc5, sizeof(long_pc5), "%s/", work);
        while (n < PC5_TILE_RUNTIME + 4) long_pc5[n++] = 'x';
        long_pc5[n] = 0;
        CHECK(pc5_tile_make(&far, again, error, sizeof(error)) == PC5_TILE_FAILED && strstr(error, "too long"),
              "too long a path: %s", error);
        CHECK(count_entries(tiles, ".staging-") == 0, "nothing staged left");
    }

    /* Taken away: the tile goes, PC5 stays. */
    CHECK(pc5_home_remove(tiles, "neon") == 1, "taken away");
    path(file, sizeof(file), "%s/%s", tiles, id);
    CHECK(!pc5_exists(file), "its folder gone");
    path(file, sizeof(file), "%s/win/wine/lib/wine/x86_64-unix/ntdll.prx", pc5);
    CHECK(!stat(file, &b) && pc5_is_file(file), "PC5's Wine whole");
    path(file, sizeof(file), "%s/eboot.bin", pc5);
    CHECK(pc5_is_file(file), "and its program");
    pc5_free(lapy);
    pc5_free(copy);
}

/* The player's own covers: a cover.jpg/.png at the top of a game's folder,
 * offered before anything online; one in a folder of pictures by name. */
static void test_own_covers(void)
{
    char dir[PC5_PATH], file[PC5_PATH], found[PC5_PATH];
    Pc5Folder *folder = pc5_alloc(sizeof(*folder));
    Pc5CoverHint hint = { 0 };
    Pc5CoverSearch *search = pc5_alloc(sizeof(*search));
    Pc5Image cover = { 0 };

    path(dir, sizeof(dir), "%s/own/Hollow Knight", work);
    path(file, sizeof(file), "%s/COVER.PNG", dir);
    write_png(file, 300, 450, 0xff33aa55u);
    path(file, sizeof(file), "%s/sub/cover.png", dir);
    write_png(file, 10, 10, 0xff000000u);
    pc5_folder_inspect(dir, "Hollow Knight", folder);
    path(file, sizeof(file), "%s/COVER.PNG", dir);
    CHECK(!strcmp(folder->picture, file), "the picture at the top, whatever its case: %s", folder->picture);
    pc5_copy(hint.folder, sizeof(hint.folder), "Hollow Knight");
    pc5_copy(hint.picture, sizeof(hint.picture), folder->picture);
    pc5_cover_search_start(search, &hint);
    CHECK(search->count == 1 && !strcmp(search->choices[0].source, "your picture") && search->answered,
          "offered first: %d %s", search->count, search->choices[0].source);
    hint.picture_only = 1;
    pc5_cover_search_start(search, &hint);
    CHECK(!pc5_cover_search_step(search, NULL) && search->count == 1, "nothing asked online when it's all there is");
    CHECK(!pc5_cover_fetch(&search->choices[0], NULL, &cover) && cover.w == PC5_COVER_WIDTH,
          "read from its file and made a cover");
    if (cover.px) CHECK(cover.px[cover.w * cover.h / 2 + cover.w / 2] == 0xff33aa55u, "its colour: %08x",
                        cover.px[cover.w * cover.h / 2 + cover.w / 2]);
    pc5_image_free(&cover);
    /* A folder of covers, by name, without regard to case or picture kind. */
    path(dir, sizeof(dir), "%s/own/covers", work);
    path(file, sizeof(file), "%s/hollow knight.JPEG", dir);
    write_text(file, "not checked here");
    path(file, sizeof(file), "%s/Hollow.png", dir);
    write_text(file, "another game");
    {
        const char *const names[] = { "", "Hollow Knight", "hollow-knight", NULL };
        CHECK(!pc5_cover_own(dir, names, found, sizeof(found)) && strstr(found, "hollow knight.JPEG"), "found: %s", found);
    }
    {
        const char *const names[] = { "Silksong", NULL };
        CHECK(pc5_cover_own(dir, names, found, sizeof(found)) == -1, "none for another game");
    }
    pc5_free(folder);
    pc5_free(search);
}

static void test_game(const char *fonts_path)
{
    char game_root[PC5_PATH], app[PC5_PATH], file[PC5_PATH];
    const Pc5GameOps ops = { .light_bar = fake_light, .light_bar_reset = fake_light_reset, .triggers = fake_triggers,
                             .cursor = fake_cursor, .cursor_magnify = fake_cursor_magnify, .cpu_time_us = fake_cpu_time_us, .cpu_count = fake_cpu_count,
                             .temperature = fake_temperature, .memory = fake_memory };
    Pc5GameSession game;
    Pc5Settings set;
    uint8_t large, small;

    path(game_root, sizeof(game_root), "%s/game-root", work);
    path(app, sizeof(app), "%s/game-app", work);
    path(file, sizeof(file), "%s/pc5/fonts.bin", app);
    copy_file(fonts_path, file);
    /* The game's cover, with its colour in the header. */
    {
        Pc5Image cover;
        pc5_image_new(&cover, 4, 6);
        path(file, sizeof(file), "%s/prefixes/liberty/pc5", game_root);
        pc5_mkdirs(file);
        path(file, sizeof(file), "%s/prefixes/liberty/pc5/cover.bin", game_root);
        pc5_cover_save(file, &cover, 0xff203040u);
        pc5_image_free(&cover);
    }
    pc5_settings_default(&set);
    set.triggers = PC5_TRIGGERS_RUMBLE;
    set.fps = 1;
    set.fps_corner = PC5_CORNER_BOTTOM_RIGHT;
    pc5_settings_save(game_root, &set);
    memset(&pad_seen, 0, sizeof(pad_seen));
    pc5_game_begin(&game, &ops, game_root, app, "liberty", "Liberty Town");
    CHECK(game.cover_tint == 0x203040u && game.settings.triggers == PC5_TRIGGERS_RUMBLE, "settings and colour: %06x",
          (unsigned)game.cover_tint);
    CHECK(!game.frame_generation && !getenv("PW_PC5_FRAMEGEN"), "no frame generation by default");
    {
        Pc5Controls controls, again;
        pc5_controls_load(game_root, "liberty", &controls);
        controls.frame_generation = 1;
        CHECK(!pc5_controls_save(game_root, "liberty", &controls) && !pc5_controls_load(game_root, "liberty", &again) &&
              again.frame_generation == 1, "Frame generation is kept with the game");
        pc5_game_begin(&game, &ops, game_root, app, "liberty", "Liberty Town");
        const char *env = getenv("PW_PC5_FRAMEGEN");
        CHECK(game.frame_generation && env && !strcmp(env, "1"), "a game with Frame generation on tells libvulkan.prx");
        controls.frame_generation = 0;
        pc5_controls_save(game_root, "liberty", &controls);
        pc5_game_begin(&game, &ops, game_root, app, "liberty", "Liberty Town");
        CHECK(!game.frame_generation && !getenv("PW_PC5_FRAMEGEN"), "and off again");
        /* A 32-bit game's Memory, which Wine reads (patch 0921): Auto by default, 4 GB only
         * for a program whose large-address flag asks for it, as on Windows; 4 GB for any. */
        env = getenv("PW_LARGE_ADDRESS_AWARE");
        CHECK(env && !strcmp(env, "0"), "Auto by default: as Windows");
        controls.memory_4gb = 1;
        CHECK(!pc5_controls_save(game_root, "liberty", &controls) && !pc5_controls_load(game_root, "liberty", &again) &&
              again.memory_4gb == 1, "Memory is kept with the game");
        pc5_game_begin(&game, &ops, game_root, app, "liberty", "Liberty Town");
        CHECK(!getenv("PW_LARGE_ADDRESS_AWARE"), "a game set to 4 GB tells Wine");
        controls.memory_4gb = 0;
        pc5_controls_save(game_root, "liberty", &controls);
        pc5_game_begin(&game, &ops, game_root, app, "liberty", "Liberty Town");
        env = getenv("PW_LARGE_ADDRESS_AWARE");
        CHECK(env && !strcmp(env, "0"), "and Auto again");
        {
            /* A game saved before 1.9.8 said 4gb whether chosen or not (then the default): Auto. */
            char saved[PC5_PATH];
            path(saved, sizeof(saved), "%s/prefixes/liberty/pc5/controls.txt", game_root);
            write_text(saved, "mode = xinput\nmemory_32bit = 4gb\n");
            CHECK(!pc5_controls_load(game_root, "liberty", &again) && !again.memory_4gb, "an old 4gb is Auto now");
            write_text(saved, "mode = xinput\nmemory = 4gb\n");
            CHECK(!pc5_controls_load(game_root, "liberty", &again) && again.memory_4gb, "4 GB chosen stays");
            pc5_controls_save(game_root, "liberty", &controls);
        }
        /* CPU sharing reaches Wine's server for any game, its profile written by PC5 or not. */
        CHECK(!getenv("WINE_PS5_SCHED"), "the console's scheduling by default");
        controls.cpu_sharing = 1;
        pc5_controls_save(game_root, "liberty", &controls);
        pc5_game_begin(&game, &ops, game_root, app, "liberty", "Liberty Town");
        env = getenv("WINE_PS5_SCHED");
        CHECK(env && !strcmp(env, "1"), "CPU sharing on tells Wine");
        controls.cpu_sharing = 0;
        pc5_controls_save(game_root, "liberty", &controls);
        pc5_game_begin(&game, &ops, game_root, app, "liberty", "Liberty Town");
        CHECK(!getenv("WINE_PS5_SCHED"), "and off again");
    }

    /* The light bar: the cover's colour, brought up to a light bar's brightness, once. */
    pc5_game_pad(&game, 7, 1000, 0);
    CHECK(pad_seen.lights == 1 && pad_seen.light == 0x73ace6u, "light bar: %06x", (unsigned)pad_seen.light);
    pc5_game_pad(&game, 7, 1200, 0);
    CHECK(pad_seen.lights == 1, "not again while it stays");
    /* The rumble reaches the motors, and the triggers follow it. */
    pc5_game_rumble(&game, 7, 65535, 0, &large, &small);
    CHECK(large == 255 && small == 0, "motors");
    CHECK(pad_seen.triggers == 1 && pad_seen.l2.mode == PC5_TRIGGER_VIBRATION && pad_seen.l2.amplitude == 8 &&
          pad_seen.r2.mode == PC5_TRIGGER_VIBRATION, "triggers vibrate: %d %d", (int)pad_seen.l2.mode, pad_seen.l2.amplitude);
    pc5_game_rumble(&game, 7, 0, 0, &large, &small);
    CHECK(pad_seen.triggers == 2 && pad_seen.l2.mode == PC5_TRIGGER_OFF, "and stop with it");
    /* The triggers' rumble at 25 %: at most 2 of 8. */
    game.settings.trigger_rumble = 25;
    pc5_game_rumble(&game, 7, 65535, 65535, &large, &small);
    CHECK(pad_seen.triggers == 3 && pad_seen.l2.amplitude == 2, "trigger rumble at 25 %%: %d", pad_seen.l2.amplitude);
    pc5_game_rumble(&game, 7, 600, 0, &large, &small);
    CHECK(pad_seen.triggers == 4 && pad_seen.l2.amplitude == 1, "a faint rumble is the faintest buzz: %d",
          pad_seen.l2.amplitude);
    game.settings.trigger_rumble = 100;
    pc5_game_rumble(&game, 7, 32768, 0, &large, &small);
    CHECK(pad_seen.l2.amplitude == 5, "half the speed at 100 %%: %d", pad_seen.l2.amplitude);
    pc5_game_rumble(&game, 7, 0, 0, &large, &small);
    /* Vibration at 50 %, then off: the motors at half, then nothing. */
    game.settings.vibration = 50;
    pc5_game_rumble(&game, 7, 65535, 40000, &large, &small);
    CHECK(large == 127 && small == 78, "vibration at 50 %%: %d %d", large, small);
    {
        /* Players 2 to 4: the same strength, and the triggers stay player 1's. */
        int triggers = pad_seen.triggers;
        pc5_game_player_rumble(&game, 65535, 40000, &large, &small);
        CHECK(large == 127 && small == 78 && pad_seen.triggers == triggers, "player 2's motors at 50 %%: %d %d",
              large, small);
    }
    game.settings.vibration = 0;
    pc5_game_rumble(&game, 7, 40000, 40000, &large, &small);
    CHECK(!large && !small, "vibration off");
    pc5_game_rumble(&game, 7, 0, 0, &large, &small);
    /* The screen's colour, when the light bar follows it: it fades over. */
    game.settings.light_bar = PC5_LIGHT_SCREEN;
    for (int i = 0; i < 40; i++) pc5_game_pad(&game, 7, 2000 + 100u * (unsigned)i, 0xff003300u);
    CHECK(pad_seen.light == 0x00e600u, "the screen's colour: %06x", (unsigned)pad_seen.light);
    /* The counter over the title's frames: once a second, in its corner. */
    pc5_game_hud(&game, 3, 0, 10000);
    pc5_game_hud(&game, 3, 60, 11000);
    CHECK(pad_seen.cursors == 1 && pad_seen.cursor_x == 1920 - 64 - 24 && pad_seen.cursor_y == 1080 - 40 - 24,
          "counter at %u,%u", pad_seen.cursor_x, pad_seen.cursor_y);
    {
        int lit = 0;
        for (int i = 0; i < 64 * 40; i++) lit += pad_seen.cursor_image[i] == 0xffffffffu;
        CHECK(lit > 100 && !pad_seen.cursor_image[63 * 64], "its digits drawn, the bottom clear: %d", lit);
    }
    /* The whole overlay: what PC5 measures once a second, a line each;
     * the chip's sensor can't be read, so its line is left out. */
    {
        PwPc5HudStats stats;
        game.settings.frame_time = game.settings.cpu_usage = game.settings.cpu_temperature = 1;
        game.settings.soc_temperature = game.settings.memory = 1;
        measured.cpu_us = 1000000;
        measured.soc_rc = (int)0x80020002u;
        game.stats_at = 0;
        CHECK(pc5_game_overlay_sample(&game, 50000, &stats) && stats.cpu_percent == -1 && stats.cpu_celsius == 61 &&
              stats.soc_celsius == -1 && stats.memory_total == ((uint64_t)125 << 30) / 10,
              "first sample: cpu %d%% cpu %d soc %d", stats.cpu_percent, stats.cpu_celsius, stats.soc_celsius);
        CHECK(!pc5_game_overlay_sample(&game, 50500, &stats), "not twice a second");
        measured.cpu_us += 4000000;     /* 4 CPUs' worth in a second, of 16 */
        CHECK(pc5_game_overlay_sample(&game, 51000, &stats) && stats.cpu_percent == 25, "cpu %d%%", stats.cpu_percent);
        measured.memory_rc = -1;
        CHECK(pc5_game_overlay_sample(&game, 52000, &stats) && !stats.memory_total, "memory unknown: left out");
        measured.memory_rc = 0;
        game.hud_second_at = 0;
        pad_seen.cursors = 0;
        pc5_game_hud(&game, 3, 0, 60000);
        pc5_game_hud(&game, 3, 30, 61000);
        /* FPS, MS, CPU, CPU temperature, RAM: 5 lines, a box 51 high */
        CHECK(pad_seen.cursors == 1 && pad_seen.cursor_x == 1920 - 64 - 24 && pad_seen.cursor_y == 1080 - 51 - 24,
              "overlay at %u,%u", pad_seen.cursor_x, pad_seen.cursor_y);
        int white = 0, label = 0;
        for (int i = 0; i < 64 * 64; i++) {
            white += pad_seen.cursor_image[i] == 0xffffffffu;
            label += pad_seen.cursor_image[i] == 0xff9aa3afu;
        }
        CHECK(white > 150 && label > 100 && !pad_seen.cursor_image[60 * 64 + 32], "its lines drawn: %d %d", white, label);
        int normal_labels = label;
        /* Large: two of the five lines at a time, their values twice the size, the next two every 3
         * seconds, then RAM alone, then the first two again. */
        game.settings.overlay_size = PC5_OVERLAY_LARGE;
        uint32_t first[64 * 64];
        int large_white = 0, turned = 0;
        for (int second = 0; second < 9; second++) {
            pad_seen.cursors = 0;
            pc5_game_hud(&game, 3, 120 + (uint64_t)second * 30, 64000 + (uint64_t)second * 1000);
            if (!pad_seen.cursors) continue;
            int page = (int)(game.hud_seconds / 3 % 3);
            CHECK(pad_seen.cursor_y == 1080 - (page == 2 ? 34 : 64) - 24, "large, page %d: a box %u high", page,
                  1080 - 24 - pad_seen.cursor_y);
            if (second == 1) memcpy(first, pad_seen.cursor_image, sizeof(first));
            else if (memcmp(first, pad_seen.cursor_image, sizeof(first))) turned = 1;
            if (second == 1)
                for (int i = 0; i < 64 * 64; i++) large_white += pad_seen.cursor_image[i] == 0xffffffffu;
        }
        CHECK(large_white > white * 2 / 3 && turned, "large values drawn (%d white pixels for two lines, %d for five), "
              "taking turns: %d", large_white, white, turned);
        CHECK(pc5_game_overlay_lines(&game.settings) & PW_PC5_HUD_LARGE, "the large flag goes with the lines");
        CHECK(!pad_seen.magnify_calls, "Large: the cursor isn't magnified");
        /* Extra large: every line at once (not two taking turns), the cursor magnified once (twice
         * the size by the display), placed by its 128x128 in the corner; refused, placed again by
         * its own size, and not asked again. */
        game.settings.overlay_size = PC5_OVERLAY_EXTRA_LARGE;
        pad_seen.cursors = 0;
        for (int second = 9; second < 12; second++) pc5_game_hud(&game, 3, 120 + (uint64_t)second * 30, 64000 + (uint64_t)second * 1000);
        CHECK(pad_seen.magnify_calls == 1 && pad_seen.magnified == 1 && pad_seen.cursor_x == 1920 - 128 - 24,
              "extra large: magnified once, at %u,%u", pad_seen.cursor_x, pad_seen.cursor_y);
        {
            int labels = 0;
            for (int i = 0; i < 64 * 64; i++) labels += pad_seen.cursor_image[i] == 0xff9aa3afu;
            CHECK(1080 - 24 - pad_seen.cursor_y == 2 * 51 && labels == normal_labels,
                  "extra large: the five lines of Normal, twice the height: %u, %d label pixels of %d",
                  1080 - 24 - pad_seen.cursor_y, labels, normal_labels);
        }
        CHECK(!(pc5_game_overlay_lines(&game.settings) & PW_PC5_HUD_LARGE), "extra large: no taking turns");
        game.settings.overlay_size = PC5_OVERLAY_LARGE;
        pc5_game_hud(&game, 3, 120 + 12 * 30, 64000 + 12 * 1000);
        CHECK(pad_seen.magnify_calls == 2 && pad_seen.magnified == 0 && pad_seen.cursor_x == 1920 - 64 - 24,
              "back to Large: its own size again");
        game.hud_magnified = 0;
        pad_seen.magnify_rc = -1;
        game.settings.overlay_size = PC5_OVERLAY_EXTRA_LARGE;
        pc5_game_hud(&game, 3, 120 + 13 * 30, 64000 + 13 * 1000);
        pc5_game_hud(&game, 3, 120 + 14 * 30, 64000 + 14 * 1000);
        CHECK(pad_seen.magnify_calls == 3 && pad_seen.cursor_x == 1920 - 64 - 24 && game.hud_magnify_refused,
              "refused: asked once, placed by its own size: %d %u", pad_seen.magnify_calls, pad_seen.cursor_x);
        pad_seen.magnify_rc = 0;
        game.hud_magnify_refused = 0;
        game.settings.overlay_size = PC5_OVERLAY_NORMAL;
        game.settings.fps = game.settings.frame_time = game.settings.cpu_usage = game.settings.memory = 0;
        game.settings.cpu_temperature = 0;   /* the chip alone, which can't be read: nothing shown */
        pad_seen.cursors = 0;
        pc5_game_hud(&game, 3, 60, 62000);
        CHECK(pad_seen.cursors == 1 && pad_seen.cursor_x == 0 && pad_seen.cursor_y == 0, "hidden while it has no line");
        game.settings.soc_temperature = 0;
        pad_seen.cursors = 0;
        pc5_game_hud(&game, 3, 90, 63000);
        CHECK(!pad_seen.cursors, "off");
        game.settings.fps = 1;
    }
    /* The loading screen: a frame every 50 ms, green code and the name. */
    const uint32_t *frame = NULL;
    for (uint64_t t = 0; t < 2000; t += 50) {
        const uint32_t *next = pc5_game_loading(&game, 20000 + t);
        if (next) frame = next;
    }
    CHECK(frame != NULL && !game.matrix_failed, "loading frames");
    if (frame) {
        int green = 0, bright_text = 0;
        for (int i = 0; i < PC5_MATRIX_WIDTH * PC5_MATRIX_HEIGHT; i++) {
            uint32_t p = frame[i], r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
            green += g > 60 && g > r * 2 && g > b;
        }
        for (int y = 330; y < 380; y++)
            for (int x = 400; x < 880; x++) bright_text += (frame[y * PC5_MATRIX_WIDTH + x] & 0xff) > 200;
        CHECK(green > 5000 && bright_text > 200, "green rain %d, the name %d", green, bright_text);
    }
    pc5_game_loading_done(&game);
    CHECK(pc5_game_loading(&game, 40000) == NULL, "none after the first picture");
    pc5_game_end(&game, 7);
    CHECK(pad_seen.resets == 1 && pad_seen.l2.mode == PC5_TRIGGER_OFF, "the controller as the console had it");

    /* Resistance: set once, at the first tick. */
    set.triggers = PC5_TRIGGERS_RESISTANCE;
    set.light_bar = PC5_LIGHT_CONSOLE;
    pc5_settings_save(game_root, &set);
    memset(&pad_seen, 0, sizeof(pad_seen));
    pc5_game_begin(&game, &ops, game_root, app, "liberty", "Liberty Town");
    pc5_game_pad(&game, 7, 1000, 0);
    pc5_game_pad(&game, 7, 2000, 0);
    CHECK(pad_seen.triggers == 1 && pad_seen.l2.mode == PC5_TRIGGER_FEEDBACK && pad_seen.l2.strength == 4 && !pad_seen.lights,
          "resistance at 50 %% (4 of 8), and the console's light bar left alone: %d", pad_seen.l2.strength);
    pc5_game_rumble(&game, 7, 65535, 65535, &large, &small);
    CHECK(pad_seen.triggers == 1, "resistance alone doesn't follow the rumble");
    pc5_game_end(&game, 7);

    /* Rumble + resistance: they push back, vibrate while the game rumbles, then push back again. */
    set.triggers = PC5_TRIGGERS_BOTH;
    set.trigger_resistance = 100;
    set.trigger_rumble = 50;
    pc5_settings_save(game_root, &set);
    memset(&pad_seen, 0, sizeof(pad_seen));
    pc5_game_begin(&game, &ops, game_root, app, "liberty", "Liberty Town");
    pc5_game_pad(&game, 7, 1000, 0);
    CHECK(pad_seen.triggers == 1 && pad_seen.l2.mode == PC5_TRIGGER_FEEDBACK && pad_seen.l2.strength == 8,
          "both: resistance at 100 %%: %d", pad_seen.l2.strength);
    pc5_game_rumble(&game, 7, 65535, 0, &large, &small);
    CHECK(pad_seen.triggers == 2 && pad_seen.l2.mode == PC5_TRIGGER_VIBRATION && pad_seen.l2.amplitude == 4,
          "both: vibrate at 50 %% while it rumbles: %d", pad_seen.l2.amplitude);
    pc5_game_pad(&game, 7, 1100, 0);
    CHECK(pad_seen.triggers == 2, "the resistance waits for the rumble to stop");
    pc5_game_rumble(&game, 7, 0, 0, &large, &small);
    pc5_game_pad(&game, 7, 1200, 0);
    CHECK(pad_seen.triggers == 3 && pad_seen.l2.mode == PC5_TRIGGER_FEEDBACK && pad_seen.r2.strength == 8,
          "both: back to pushing back");
    pc5_game_end(&game, 7);
    CHECK(pad_seen.l2.mode == PC5_TRIGGER_OFF, "off at the end");
    /* Before the first tick: the rumble isn't overwritten by the resistance. */
    memset(&pad_seen, 0, sizeof(pad_seen));
    pc5_game_begin(&game, &ops, game_root, app, "liberty", "Liberty Town");
    pc5_game_rumble(&game, 7, 65535, 0, &large, &small);
    pc5_game_pad(&game, 7, 1000, 0);
    CHECK(pad_seen.triggers == 1 && pad_seen.l2.mode == PC5_TRIGGER_VIBRATION, "a rumble before the first tick stays");
    pc5_game_end(&game, 7);

    /* DualSense mode: the game's motors at Vibration's strength, PC5's light bar and triggers until the
     * game sets its own, then the game's. */
    set.light_bar = PC5_LIGHT_GAME;
    set.vibration = 50;
    pc5_settings_save(game_root, &set);
    memset(&pad_seen, 0, sizeof(pad_seen));
    pc5_game_begin(&game, &ops, game_root, app, "liberty", "Liberty Town");
    pc5_game_pad(&game, 7, 1000, 0);
    CHECK(pad_seen.triggers == 1 && pad_seen.l2.mode == PC5_TRIGGER_FEEDBACK && pad_seen.lights == 1,
          "dualsense: PC5's resistance and light bar first");
    {
        Pc5PadOutput out;
        memset(&out, 0, sizeof(out));
        out.set = PC5_OUTPUT_MOTORS;
        out.large = 200;
        pc5_game_output(&game, 7, &out, &large, &small);
        CHECK(large == 100 && !small && pad_seen.triggers == 2 && pad_seen.l2.mode == PC5_TRIGGER_VIBRATION,
              "dualsense: the motors at 50 %%, the triggers follow them: %d", large);
        out.large = 0;
        pc5_game_output(&game, 7, &out, &large, &small);
        pc5_game_pad(&game, 7, 1100, 0);
        CHECK(pad_seen.triggers == 3 && pad_seen.l2.mode == PC5_TRIGGER_FEEDBACK, "dualsense: back to resistance");
        out.set |= PC5_OUTPUT_LIGHT;
        out.light = 0x102030;
        pc5_game_output(&game, 7, &out, &large, &small);
        CHECK(pad_seen.lights == 2 && pad_seen.light == 0x102030, "dualsense: the game's light bar: %06x",
              (unsigned)pad_seen.light);
        pc5_game_output(&game, 7, &out, &large, &small);
        pc5_game_pad(&game, 7, 1300, 0);
        pc5_game_pad(&game, 7, 1500, 0xff00ff00u);
        CHECK(pad_seen.lights == 2, "dualsense: kept, and PC5's light bar stops");
        out.set |= PC5_OUTPUT_TRIGGERS;
        out.trigger[1].mode = PC5_TRIGGER_GAME;
        out.trigger[1].effect = 2;
        out.trigger[1].data[0] = 2;
        out.trigger[1].data[1] = 5;
        out.trigger[1].data[2] = 8;
        pc5_game_output(&game, 7, &out, &large, &small);
        CHECK(pad_seen.triggers == 4 && pad_seen.r2.mode == PC5_TRIGGER_GAME && pad_seen.r2.effect == 2 &&
              pad_seen.r2.data[1] == 5 && pad_seen.l2.mode == PC5_TRIGGER_OFF, "dualsense: the game's trigger effects");
        pc5_game_output(&game, 7, &out, &large, &small);
        CHECK(pad_seen.triggers == 4, "dualsense: not sent again while they stay");
        out.large = 255;
        out.small = 255;
        pc5_game_output(&game, 7, &out, &large, &small);
        pc5_game_pad(&game, 7, 1700, 0);
        CHECK(large == 127 && small == 127 && pad_seen.triggers == 4,
              "dualsense: the rumble no longer moves the triggers, nor does the resistance come back");
        out.trigger[1].data[1] = 6;
        pc5_game_output(&game, 7, &out, &large, &small);
        CHECK(pad_seen.triggers == 5 && pad_seen.r2.data[1] == 6, "dualsense: a new effect is sent");
    }
    pc5_game_end(&game, 7);
    CHECK(pad_seen.resets == 1 && pad_seen.r2.mode == PC5_TRIGGER_OFF && pad_seen.triggers == 6,
          "dualsense: the controller as the console had it at the end");
}

/* What controls.txt says, as one string. */
static int controls_has(const char *id, const char *line)
{
    char file[PC5_PATH];
    Pc5Buf text = { 0 };
    int found;
    path(file, sizeof(file), "%s/prefixes/%s/pc5/controls.txt", root, id);
    pc5_read_file(file, &text, 16 * 1024);
    found = text.len && strstr((char *)text.data, line) != NULL;
    pc5_buf_free(&text);
    return found;
}

/* A game's controller from its menu: the three modes, Button mapping and the key picker. The
 * library shows the engine game (orbital-nine). */
static void test_controller_screens(Pc5Ui *ui, Pc5Canvas *canvas)
{
    press(ui, PC5_KEY_TRIANGLE, canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "game-menu"), "its menu again: %s", pc5_ui_screen(ui));
    press(ui, PC5_KEY_DOWN, canvas);
    press(ui, PC5_KEY_RIGHT, canvas);
    CHECK(controls_has("orbital-nine", "mode = dualsense\n"), "Controller: DualSense");
    press(ui, PC5_KEY_ACCEPT, canvas);
    CHECK(controls_has("orbital-nine", "mode = keyboard\n"), "then Keyboard & mouse");
    press(ui, PC5_KEY_LEFT, canvas);
    press(ui, PC5_KEY_LEFT, canvas);
    press(ui, PC5_KEY_LEFT, canvas);
    CHECK(controls_has("orbital-nine", "mode = keyboard\n"), "round the three, both ways");
    /* Button mapping, which only Keyboard & mouse has. */
    press(ui, PC5_KEY_DOWN, canvas);
    press(ui, PC5_KEY_ACCEPT, canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "buttons"), "button mapping: %s", pc5_ui_screen(ui));
    /* The left stick's up (W) through the key picker: one to the right is X. */
    press(ui, PC5_KEY_DOWN, canvas);
    press(ui, PC5_KEY_ACCEPT, canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "keys"), "key picker: %s", pc5_ui_screen(ui));
    press(ui, PC5_KEY_RIGHT, canvas);
    press(ui, PC5_KEY_ACCEPT, canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "buttons") && controls_has("orbital-nine", "left_stick_up = x\n"), "W became X");
    /* Back from the picker changes nothing; Square sends nothing; left and right step through keys. */
    press(ui, PC5_KEY_ACCEPT, canvas);
    press(ui, PC5_KEY_DOWN, canvas);
    press(ui, PC5_KEY_BACK, canvas);
    CHECK(controls_has("orbital-nine", "left_stick_up = x\n"), "picker left unchanged");
    press(ui, PC5_KEY_SQUARE, canvas);
    CHECK(controls_has("orbital-nine", "left_stick_up = none\n"), "Square: nothing");
    press(ui, PC5_KEY_DOWN, canvas);
    press(ui, PC5_KEY_LEFT, canvas);
    CHECK(controls_has("orbital-nine", "left_stick_down = r\n"), "S stepped back to R");
    /* The left stick becomes the pointer: the right one sends keys then. */
    press(ui, PC5_KEY_UP, canvas);
    press(ui, PC5_KEY_UP, canvas);
    press(ui, PC5_KEY_RIGHT, canvas);
    CHECK(controls_has("orbital-nine", "left_stick = pointer\n") && controls_has("orbital-nine", "right_stick = keys\n"),
          "one pointer at a time");
    /* Pointer speed, a row below the right stick's four keys. */
    for (int i = 0; i < 6; i++) press(ui, PC5_KEY_DOWN, canvas);
    press(ui, PC5_KEY_RIGHT, canvas);
    CHECK(controls_has("orbital-nine", "pointer_speed = 5\n"), "faster pointer");
    /* The slow pointer button below it, then how much slower once there is one. */
    press(ui, PC5_KEY_DOWN, canvas);
    press(ui, PC5_KEY_RIGHT, canvas);
    CHECK(controls_has("orbital-nine", "slow_button = cross\n"), "slow button: Cross");
    press(ui, PC5_KEY_DOWN, canvas);
    press(ui, PC5_KEY_ACCEPT, canvas);
    CHECK(controls_has("orbital-nine", "slow_percent = 60\n"), "slower by 60 %%");
    /* Reset to default, the last row. */
    for (int i = 0; i < 40; i++) press(ui, PC5_KEY_DOWN, canvas);
    press(ui, PC5_KEY_ACCEPT, canvas);
    CHECK(controls_has("orbital-nine", "mode = keyboard\n") && controls_has("orbital-nine", "left_stick = keys\n") &&
              controls_has("orbital-nine", "right_stick = pointer\n") && controls_has("orbital-nine", "left_stick_up = w\n") &&
              controls_has("orbital-nine", "pointer_speed = 4\n") && controls_has("orbital-nine", "slow_button = none\n"),
          "reset: the default layout, still Keyboard & mouse");
    /* The touchpad (the row above) sends nothing, the last key, found down the picker's column. */
    press(ui, PC5_KEY_UP, canvas);
    press(ui, PC5_KEY_ACCEPT, canvas);
    for (int i = 0; i < 5; i++) press(ui, PC5_KEY_DOWN, canvas);
    press(ui, PC5_KEY_ACCEPT, canvas);
    CHECK(controls_has("orbital-nine", "touchpad = none\n"), "touchpad: nothing");
    press(ui, PC5_KEY_BACK, canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "game-menu"), "back to the menu: %s", pc5_ui_screen(ui));
    press(ui, PC5_KEY_BACK, canvas);
    {
        char error[PC5_ERROR];
        PwWineLibrary *pw = pc5_alloc(sizeof(*pw));
        CHECK(!pc5_profile_refresh(root, "orbital-nine", error, sizeof(error)), "profile written");
        pw_wine_library_load(pw, root);
        const PwGameProfile *profile = pw_wine_library_find(pw, "orbital-nine");
        CHECK(profile != NULL, "prospero-win reads it");
        if (profile) {
            const PwGameInput *in = &profile->input;
            CHECK(in->mode == PW_GAME_INPUT_KEYBOARD && in->mouse == PW_GAME_STICK_RIGHT && in->mouse_speed == 1200,
                  "keyboard, the right stick the pointer: mode %d mouse %d speed %u", (int)in->mode, (int)in->mouse,
                  in->mouse_speed);
            CHECK(in->bindings[0].kind == PW_GAME_BIND_KEY && in->bindings[0].code == 0x20, "Cross: space");
            CHECK(in->bindings[7].kind == PW_GAME_BIND_MOUSE && in->bindings[7].code == 0, "R2: left click");
            CHECK(in->bindings[17].kind == PW_GAME_BIND_KEY && in->bindings[17].code == 'W', "left stick up: W");
            CHECK(in->bindings[21].kind == PW_GAME_BIND_UNSET, "the pointer's stick sends no keys");
            CHECK(in->bindings[16].kind == PW_GAME_BIND_NONE, "the touchpad sends nothing: %d", (int)in->bindings[16].kind);
        }
        pc5_free(pw);
    }
    /* Back to the Xbox controller, as the tests after this expect. */
    press(ui, PC5_KEY_TRIANGLE, canvas);
    press(ui, PC5_KEY_DOWN, canvas);
    press(ui, PC5_KEY_RIGHT, canvas);
    CHECK(controls_has("orbital-nine", "mode = xinput\n"), "the Xbox controller again");
    press(ui, PC5_KEY_BACK, canvas);
}

/* PC5 started from a home screen tile: the tile's game starts at once, and a
 * game no longer there says so. The player's own picture in the covers folder
 * becomes an installed game's cover when PC5 opens. */
static void test_tile_start(const char *fonts)
{
    FakeConsole console = { 0 };
    FakeNet net = { 0 };
    Pc5Platform platform = { &console, { &net, fake_get, NULL, fake_get_authorized }, fake_clock, fake_play, NULL, fake_notify, fake_time, 0, NULL, NULL,
                             NULL, NULL, NULL };
    const char *roots[] = { usb, NULL };
    Pc5Canvas canvas = { pc5_alloc(1920u * 1080u * 4u), 1920, 1080, 1920, 0, 0 };
    Pc5Library library;
    char file[PC5_PATH], tiles[PC5_PATH], tile_template[PC5_PATH];

    path(tiles, sizeof(tiles), "%s/screens-homebrew", work);
    make_tile_template(tile_template, sizeof(tile_template));
    pc5_library_load(root, &library);
    CHECK(library.count > 0, "games to start");
    if (!library.count) return;
    {
        Pc5UiConfig config = { root, template_prefix, fonts, roots, tiles, tile_template, library.games[0].id, NULL, NULL, NULL, 0, 0 };
        Pc5Ui *ui = pc5_ui_create(&config, &platform);
        for (int i = 0; i < 2000 && !console.plays; i++) frames(ui, 0, 0, 1, &canvas);
        CHECK(console.plays == 1 && !strcmp(console.played, library.games[0].id), "the tile's game started: %d %s",
              console.plays, console.played);
        pc5_ui_destroy(ui);
    }
    {
        Pc5UiConfig config = { root, template_prefix, fonts, roots, tiles, tile_template, "not-installed", NULL, NULL, NULL, 0, 0 };
        Pc5Ui *ui = pc5_ui_create(&config, &platform);
        until_screen(ui, "message", &canvas);
        CHECK(!strcmp(pc5_ui_screen(ui), "message") && console.plays == 1, "a game no longer there: %s",
              pc5_ui_screen(ui));
        press(ui, PC5_KEY_ACCEPT, &canvas);
        CHECK(!strcmp(pc5_ui_screen(ui), "library"), "then the library: %s", pc5_ui_screen(ui));
        pc5_ui_destroy(ui);
    }
    /* An installed game's cover from the covers folder, by the game's name. */
    {
        Pc5Image cover = { 0 };
        uint32_t tint = 0;
        char saved[PC5_PATH];
        struct timeval later[2];
        /* What an earlier PC5 kept (the picture as downloaded) and a save cut short: gone as PC5 opens. */
        char leftover[3][PC5_PATH];
        static const char *const leftovers[] = { "cover-original.jpg", "cover.bin.part", "background.bin.part" };
        for (int i = 0; i < 3; i++) {
            path(leftover[i], sizeof(leftover[i]), "%s/prefixes/%s/pc5/%s", root, library.games[0].prefix, leftovers[i]);
            write_text(leftover[i], "left over");
        }
        path(file, sizeof(file), "%s/covers/%s.png", root, library.games[0].title);
        write_png(file, 400, 600, 0xff8844ccu);
        /* newer than the cover it replaces */
        gettimeofday(&later[0], NULL);
        later[0].tv_sec += 60;
        later[1] = later[0];
        utimes(file, later);
        Pc5UiConfig config = { root, template_prefix, fonts, roots, tiles, tile_template, NULL, NULL, NULL, NULL, 0, 0 };
        Pc5Ui *ui = pc5_ui_create(&config, &platform);
        until_screen(ui, "library", &canvas);
        CHECK(!pc5_exists(leftover[0]) && !pc5_exists(leftover[1]) && !pc5_exists(leftover[2]),
              "what nothing uses deleted as PC5 opens");
        path(saved, sizeof(saved), "%s/prefixes/%s/pc5/cover.bin", root, library.games[0].prefix);
        CHECK(!pc5_cover_load(saved, &cover, &tint) && cover.px &&
              cover.px[cover.w * cover.h / 2 + cover.w / 2] == 0xff8844ccu, "the player's picture is its cover");
        pc5_image_free(&cover);
        path(saved, sizeof(saved), "%s/prefixes/%s/pc5/cover-original.png", root, library.games[0].prefix);
        CHECK(!pc5_exists(saved), "not kept as it was: only the cover is");
        pc5_ui_destroy(ui);
        /* once: the next start leaves it */
        ui = pc5_ui_create(&config, &platform);
        until_screen(ui, "library", &canvas);
        pc5_ui_destroy(ui);
    }
    pc5_free(canvas.px);
}

/* Two files with the same bytes. */
static int same_bytes(const char *a, const char *b)
{
    Pc5Buf x = { 0 }, y = { 0 };
    int same = !pc5_read_file(a, &x, 64u << 20) && !pc5_read_file(b, &y, 64u << 20) && x.len == y.len &&
               !memcmp(x.data, y.data, x.len);
    pc5_buf_free(&x);
    pc5_buf_free(&y);
    return same;
}

/* A game's cover and background chosen by hand (Appearance): what was found for its names, a
 * name typed on the keyboard, the card, and none. The game menu is on Appearance's last row. */
static void test_picture_screens(Pc5Ui *ui, Pc5Canvas *canvas, FakeConsole *console)
{
    char cover[PC5_PATH], background[PC5_PATH], note[PC5_PATH], jpg[PC5_PATH], png[PC5_PATH];
    char kept[PC5_PATH];
    Pc5Image image = { 0 };

    path(cover, sizeof(cover), "%s/prefixes/orbital-nine/pc5/cover.bin", root);
    path(background, sizeof(background), "%s/prefixes/orbital-nine/pc5/background.bin", root);
    path(note, sizeof(note), "%s/prefixes/orbital-nine/pc5/background-none.txt", root);
    path(jpg, sizeof(jpg), "%s/prefixes/orbital-nine/pc5/cover-original.jpg", root);
    path(png, sizeof(png), "%s/prefixes/orbital-nine/pc5/cover-original.png", root);
    path(kept, sizeof(kept), "%s/cover-before.bin", work);
    copy_file(cover, kept);

    /* api_keys.txt, made as PC5 opened; a key in it that SteamGridDB refuses: the other
     * sources' pictures as before, the refusal logged once, the key never. */
    char keys[PC5_PATH];
    path(keys, sizeof(keys), "%s/api_keys.txt", root);
    CHECK(pc5_is_file(keys), "api_keys.txt made as PC5 opened");
    write_text(keys, "STEAMGRIDDB_API_KEY = " TEST_WRONG_KEY "\n");
    key_refused_lines = key_shown_lines = 0;

    /* Cover: the one it has first; Back leaves it. */
    press(ui, PC5_KEY_UP, canvas);
    press(ui, PC5_KEY_UP, canvas);
    press(ui, PC5_KEY_ACCEPT, canvas);
    until_screen(ui, "picture", canvas);
    frames(ui, 0, 0, 8, canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "picture"), "the cover's picker: %s", pc5_ui_screen(ui));
    save_shot(canvas, "cover-choose");
    press(ui, PC5_KEY_BACK, canvas);   /* too soon after it opened: ignored */
    frames(ui, 0, 0, 20, canvas);
    press(ui, PC5_KEY_BACK, canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "game-menu") && same_bytes(cover, kept), "Back: the menu, the cover as it was: %s",
          pc5_ui_screen(ui));
    /* A name typed, the first found shown and used: Steam's, its picture kept beside it. */
    press(ui, PC5_KEY_ACCEPT, canvas);
    until_screen(ui, "picture", canvas);
    frames(ui, 0, 0, 20, canvas);
    console->typed = NULL;   /* cancelled: nothing searched */
    press(ui, PC5_KEY_TRIANGLE, canvas);
    CHECK(console->keyboards == 1 && !strcmp(console->keyboard_start, "Orbital Nine") &&
              strstr(console->keyboard_title, "covers") && !strcmp(pc5_ui_screen(ui), "picture"),
          "the keyboard, with the game's name; cancelled: %d %s %s", console->keyboards, console->keyboard_start,
          pc5_ui_screen(ui));
    frames(ui, 0, 0, 20, canvas);
    console->typed = "  Neon Drift ";
    press(ui, PC5_KEY_TRIANGLE, canvas);
    until_screen(ui, "picture", canvas);
    frames(ui, 0, 0, 20, canvas);
    save_shot(canvas, "cover-choose-search");
    press(ui, PC5_KEY_ACCEPT, canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "game-menu") && !same_bytes(cover, kept) && !pc5_exists(png) && !pc5_exists(jpg),
          "used: a new cover, in place of the one it had, with no picture beside it: %s", pc5_ui_screen(ui));
    CHECK(key_refused_lines == 1 && !key_shown_lines, "the refused key logged once (%d), never shown (%d)",
          key_refused_lines, key_shown_lines);
    /* The card, the last one (one back from the first): no downloaded picture then. */
    press(ui, PC5_KEY_ACCEPT, canvas);
    until_screen(ui, "picture", canvas);
    frames(ui, 0, 0, 20, canvas);
    press(ui, PC5_KEY_L1, canvas);
    frames(ui, 0, 0, 20, canvas);
    press(ui, PC5_KEY_ACCEPT, canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "game-menu") && !pc5_exists(png) && !pc5_exists(jpg) && pc5_cover_current(cover),
          "the card: %s", pc5_ui_screen(ui));

    /* Background: none to start with; a name typed gives Steam's library hero. */
    unlink(background);
    press(ui, PC5_KEY_DOWN, canvas);
    press(ui, PC5_KEY_ACCEPT, canvas);
    until_screen(ui, "picture", canvas);
    frames(ui, 0, 0, 20, canvas);
    console->typed = "Neon Drift";
    press(ui, PC5_KEY_TRIANGLE, canvas);
    CHECK(strstr(console->keyboard_title, "backgrounds") != NULL, "the keyboard's title: %s", console->keyboard_title);
    until_screen(ui, "picture", canvas);
    frames(ui, 0, 0, 20, canvas);
    save_shot(canvas, "background-choose");
    press(ui, PC5_KEY_ACCEPT, canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "game-menu") && !pc5_cover_load(background, &image, NULL) &&
              image.w == PC5_BACKGROUND_WIDTH && image.h == PC5_BACKGROUND_HEIGHT && !pc5_exists(note),
          "a background: %s %dx%d", pc5_ui_screen(ui), image.w, image.h);
    pc5_image_free(&image);
    /* None, the last: PC5's own backdrop, and none looked for again. */
    press(ui, PC5_KEY_ACCEPT, canvas);
    until_screen(ui, "picture", canvas);
    frames(ui, 0, 0, 20, canvas);
    press(ui, PC5_KEY_L1, canvas);
    frames(ui, 0, 0, 20, canvas);
    save_shot(canvas, "background-none");
    press(ui, PC5_KEY_ACCEPT, canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "game-menu") && !pc5_exists(background) && pc5_exists(note), "none: %s",
          pc5_ui_screen(ui));
}

static void test_screens(const char *fonts)
{
    FakeConsole console = { 0 };
    FakeNet net = { 0 };
    Pc5Platform platform = { &console, { &net, fake_get, NULL, fake_get_authorized }, fake_clock, fake_play, fake_log, fake_notify, fake_time, 0, NULL,
                             fake_sound, fake_keyboard_open_text, fake_keyboard_state_text, fake_keyboard_close_text };
    {
        /* The sounds to choose from: the app's, and one of the player's. */
        char from[PC5_PATH], to[PC5_PATH];
        path(from, sizeof(from), "%s/../../art/sounds/cheap chinese speaker.wav", fixtures);
        path(to, sizeof(to), "%s/app0/pc5/sounds/cheap chinese speaker.wav", work);
        copy_file(from, to);
        path(to, sizeof(to), "%s/sounds/Chime.wav", root);
        copy_file(from, to);
    }
    const char *roots[] = { usb, NULL };
    char tiles[PC5_PATH], tile_template[PC5_PATH], tile[PC5_HOME_ID];
    path(tiles, sizeof(tiles), "%s/screens-homebrew", work);
    make_tile_template(tile_template, sizeof(tile_template));
    Pc5UiConfig config = { root, template_prefix, fonts, roots, tiles, tile_template, NULL, NULL, NULL, NULL, 0, 0 };
    Pc5Canvas canvas = { pc5_alloc(1920u * 1080u * 4u), 1920, 1080, 1920, 0, 0 };
    Pc5Ui *ui = pc5_ui_create(&config, &platform);
    Pc5Library library;

    until_screen(ui, "library", &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "library"), "library after start: %s", pc5_ui_screen(ui));
    {
        Pc5UpdateMemory memory;
        pc5_update_load(root, &memory);
        CHECK(memory.checked == 1792700000 && memory.last.state == PC5_UPDATE_NONE, "asked GitHub at start: %d",
              (int)memory.last.state);
    }
    /* The covers the start fetched: Starfall had one; Orbital Nine looked online. */
    press(ui, PC5_KEY_OPTIONS, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "settings"), "settings");
    press(ui, PC5_KEY_ACCEPT, &canvas);      /* Game maintenance */
    press(ui, PC5_KEY_ACCEPT, &canvas);      /* Install games from USB */
    until_screen(ui, "sources", &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "sources"), "usb list: %s", pc5_ui_screen(ui));
    press(ui, PC5_KEY_ACCEPT, &canvas);   /* NeonDrift */
    until_screen(ui, "review", &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "review"), "review: %s", pc5_ui_screen(ui));
    CHECK(!cuts_after(ui, 0, &canvas), "review, with Play from USB: no text cut short");
    press(ui, PC5_KEY_R1, &canvas);       /* another cover */
    frames(ui, 0, 0, 5, &canvas);
    press(ui, PC5_KEY_L1, &canvas);
    frames(ui, 0, 0, 5, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);
    until_screen(ui, "message", &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "message"), "installed: %s", pc5_ui_screen(ui));
    press(ui, PC5_KEY_ACCEPT, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "library"), "back in the library");
    pc5_library_load(root, &library);
    int neon = pc5_library_find(&library, "neondrift");
    CHECK(neon >= 0, "installed from the screens");

    /* Play: the profile is written again, then the console asked. */
    press(ui, PC5_KEY_ACCEPT, &canvas);
    frames(ui, 0, 0, 3, &canvas);
    CHECK(console.plays == 1 && !strcmp(console.played, "neondrift"), "played %s", console.played);
    until_screen(ui, "message", &canvas);   /* the fake console returns: "didn't start" */
    press(ui, PC5_KEY_ACCEPT, &canvas);
    {
        char file[PC5_PATH];
        Pc5Buf text = { 0 };
        path(file, sizeof(file), "%s/pc5/last.txt", root);
        pc5_read_file(file, &text, 64);
        CHECK(text.len && !strncmp((char *)text.data, "neondrift", 9), "last played remembered");
        pc5_buf_free(&text);
    }

    /* Update, from the game's menu: the same folder again; what the game wrote stays. */
    {
        char save[PC5_PATH];
        path(save, sizeof(save), "%s/prefixes/neondrift/drive_c/Games/NeonDrift/save.dat", root);
        write_text(save, "progress");
        press(ui, PC5_KEY_TRIANGLE, &canvas);
        CHECK(!strcmp(pc5_ui_screen(ui), "game-menu"), "its menu: %s", pc5_ui_screen(ui));
        for (int i = 0; i < 40; i++) press(ui, PC5_KEY_DOWN, &canvas);   /* Manage, the last row */
        press(ui, PC5_KEY_ACCEPT, &canvas);
        press(ui, PC5_KEY_DOWN, &canvas);
        press(ui, PC5_KEY_DOWN, &canvas);        /* Update, after Change program and Game files */
        save_shot(&canvas, "game-menu-update");
        press(ui, PC5_KEY_ACCEPT, &canvas);
        until_screen(ui, "sources", &canvas);
        CHECK(!strcmp(pc5_ui_screen(ui), "sources"), "the USB folders: %s", pc5_ui_screen(ui));
        save_shot(&canvas, "update-sources");
        press(ui, PC5_KEY_ACCEPT, &canvas);      /* NeonDrift */
        until_screen(ui, "game-update-ask", &canvas);
        CHECK(!strcmp(pc5_ui_screen(ui), "game-update-ask"), "asked: %s", pc5_ui_screen(ui));
        save_shot(&canvas, "update-ask");
        frames(ui, 0, 0, 30, &canvas);
        press(ui, PC5_KEY_ACCEPT, &canvas);      /* Update, chosen first */
        until_screen(ui, "message", &canvas);
        CHECK(!strcmp(pc5_ui_screen(ui), "message"), "updated: %s", pc5_ui_screen(ui));
        save_shot(&canvas, "update-done");
        CHECK(file_is(save, "progress"), "the game's save stayed");
        press(ui, PC5_KEY_ACCEPT, &canvas);
        CHECK(!strcmp(pc5_ui_screen(ui), "library"), "back in the library: %s", pc5_ui_screen(ui));
    }

    /* Game maintenance › Game compatibility list: the wiki's list as a QR code; back on its row. */
    press(ui, PC5_KEY_OPTIONS, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);      /* Game maintenance */
    save_shot(&canvas, "settings-maintenance");
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);      /* Game compatibility list */
    CHECK(!strcmp(pc5_ui_screen(ui), "compatibility"), "the compatibility list's code: %s", pc5_ui_screen(ui));
    save_shot(&canvas, "compatibility");
    press(ui, PC5_KEY_BACK, &canvas);
    press(ui, PC5_KEY_UP, &canvas);
    press(ui, PC5_KEY_UP, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);      /* back on its row, two below Install games from USB */
    until_screen(ui, "sources", &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "sources"), "back on Game maintenance: %s", pc5_ui_screen(ui));
    press(ui, PC5_KEY_BACK, &canvas);        /* Game maintenance, on Install */
    press(ui, PC5_KEY_BACK, &canvas);        /* the first page, on Game maintenance */
    CHECK(!strcmp(pc5_ui_screen(ui), "settings"), "the first page: %s", pc5_ui_screen(ui));
    press(ui, PC5_KEY_BACK, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "library"), "and the library: %s", pc5_ui_screen(ui));

    /* Uninstall: Cancel is chosen first, and a press that came too soon does nothing. */
    press(ui, PC5_KEY_OPTIONS, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);      /* Game maintenance */
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "uninstall-list"), "uninstall list: %s", pc5_ui_screen(ui));
    press(ui, PC5_KEY_ACCEPT, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "uninstall-confirm"), "confirm");
    press(ui, PC5_KEY_RIGHT, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);    /* too soon after the dialog opened */
    CHECK(!strcmp(pc5_ui_screen(ui), "uninstall-confirm"), "an early press is ignored");
    frames(ui, 0, 0, 30, &canvas);
    press(ui, PC5_KEY_LEFT, &canvas);      /* back to Cancel */
    press(ui, PC5_KEY_ACCEPT, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "uninstall-list"), "cancel keeps the game");
    pc5_library_load(root, &library);
    CHECK(library.count == 3, "still three: %d", library.count);
    /* The game uninstalled next has a home screen tile: it goes with it. */
    char gone[PC5_ID];
    {
        char error[PC5_ERROR] = "";
        pc5_copy(gone, sizeof(gone), library.games[0].id);
        CHECK(!pc5_home_add(tiles, tile_template, root, gone, library.games[0].prefix, library.games[0].title, tile,
                            error, sizeof(error)), "a tile for %s: %s", gone, error);
    }
    /* Keep saves: the game goes, its saves stay. */
    {
        char save[PC5_PATH];
        path(save, sizeof(save), "%s/prefixes/%s/drive_c/users/prospero/Saved Games/NeonDrift/slot1.sav", root, gone);
        write_text(save, "kept");
    }
    press(ui, PC5_KEY_ACCEPT, &canvas);
    frames(ui, 0, 0, 30, &canvas);
    press(ui, PC5_KEY_RIGHT, &canvas);
    save_shot(&canvas, "uninstall-keep");
    press(ui, PC5_KEY_ACCEPT, &canvas);
    until_screen(ui, "message", &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);
    pc5_library_load(root, &library);
    CHECK(library.count == 2, "one uninstalled: %d", library.count);
    CHECK(pc5_library_find(&library, gone) < 0 && !pc5_home_find(tiles, gone, tile), "%s and its tile gone", gone);
    {
        Pc5Saves saves[4];
        CHECK(pc5_saves_list(root, saves, 4) == 1 && !strcmp(saves[0].name, gone), "its saves kept");
    }
    /* Settings › Game maintenance › Uninstall game lists them after the games. */
    press(ui, PC5_KEY_OPTIONS, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);
    for (int i = 0; i < 10; i++) press(ui, PC5_KEY_DOWN, &canvas);
    save_shot(&canvas, "uninstall-list-saves");
    press(ui, PC5_KEY_ACCEPT, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "saves-confirm"), "kept saves chosen: %s", pc5_ui_screen(ui));
    save_shot(&canvas, "saves-confirm");
    press(ui, PC5_KEY_BACK, &canvas);        /* the list */
    press(ui, PC5_KEY_BACK, &canvas);        /* Game maintenance */
    press(ui, PC5_KEY_BACK, &canvas);        /* the first page */
    press(ui, PC5_KEY_BACK, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "library"), "back in the library: %s", pc5_ui_screen(ui));
    /* Installed again: the saves are offered and put back. */
    press(ui, PC5_KEY_OPTIONS, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);
    until_screen(ui, "sources", &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);      /* NeonDrift */
    until_screen(ui, "review", &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "saves-ask"), "its saves offered: %s", pc5_ui_screen(ui));
    save_shot(&canvas, "saves-ask");
    frames(ui, 0, 0, 30, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);      /* Put them back, chosen first */
    until_screen(ui, "message", &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);
    pc5_library_load(root, &library);
    {
        char save[PC5_PATH];
        Pc5Saves saves[4];
        path(save, sizeof(save), "%s/prefixes/%s/drive_c/users/prospero/Saved Games/NeonDrift/slot1.sav", root, gone);
        CHECK(library.count == 3 && file_is(save, "kept") && pc5_saves_list(root, saves, 4) == 0,
              "installed with its saves: %d", library.count);
    }
    /* And uninstalled for good: Delete all. */
    press(ui, PC5_KEY_OPTIONS, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);
    frames(ui, 0, 0, 30, &canvas);
    press(ui, PC5_KEY_RIGHT, &canvas);
    press(ui, PC5_KEY_RIGHT, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);
    until_screen(ui, "message", &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);
    pc5_library_load(root, &library);
    {
        Pc5Saves saves[4];
        CHECK(library.count == 2 && pc5_library_find(&library, gone) < 0 && pc5_saves_list(root, saves, 4) == 0,
              "deleted with its saves: %d", library.count);
    }

    /* Settings: In games, a value changed with Cross and with right; saved. */
    press(ui, PC5_KEY_OPTIONS, &canvas);
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);      /* In games */
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_LEFT, &canvas);        /* light bar: game -> console */
    press(ui, PC5_KEY_UP, &canvas);
    press(ui, PC5_KEY_LEFT, &canvas);        /* vibration: 100 % -> 75 % */
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_DOWN, &canvas);        /* Adaptive triggers */
    press(ui, PC5_KEY_LEFT, &canvas);        /* off -> rumble + resistance */
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_LEFT, &canvas);        /* trigger rumble: 100 % -> 75 % */
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_RIGHT, &canvas);       /* trigger resistance: 50 % -> 75 % */
    press(ui, PC5_KEY_DOWN, &canvas);        /* the last row: still on it (Startup sound is in System now) */
    press(ui, PC5_KEY_RIGHT, &canvas);       /* trigger resistance: 75 % -> 100 % */
    press(ui, PC5_KEY_LEFT, &canvas);        /* and back */
    /* System: Startup sound. */
    press(ui, PC5_KEY_BACK, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "settings"), "back to the first page, on In games");
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);      /* System, after Overlay and File manager */
    press(ui, PC5_KEY_ACCEPT, &canvas);      /* Startup sound: cheap chinese speaker, the first, -> Chime */
    press(ui, PC5_KEY_ACCEPT, &canvas);      /* Chime, the last one, -> Off */
    {
        Pc5Settings set;
        pc5_settings_load(root, &set);
        CHECK(!set.fps && set.light_bar == PC5_LIGHT_CONSOLE && set.vibration == 75 && set.triggers == PC5_TRIGGERS_BOTH &&
              set.trigger_rumble == 75 && set.trigger_resistance == 75 && !strcmp(set.startup_sound, "off") &&
              console.sounds == 1 && !strcmp(console.sound, "Chime"),
              "settings saved: fps=%d light=%d vibration=%d triggers=%d rumble=%d resistance=%d sound=%s", set.fps,
              (int)set.light_bar, set.vibration, (int)set.triggers, set.trigger_rumble, set.trigger_resistance,
              set.startup_sound);
    }
    /* Picture sources: GOG off, from System. */
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);      /* Picture sources */
    press(ui, PC5_KEY_DOWN, &canvas);        /* SteamGridDB: "No key" (no api_keys.txt key in the tests) */
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);      /* GOG: off */
    {
        Pc5Settings set;
        pc5_settings_load(root, &set);
        CHECK(set.pictures_off == PC5_PICTURES_GOG, "GOG off: %#x", (unsigned)set.pictures_off);
    }
    press(ui, PC5_KEY_BACK, &canvas);        /* System, on Picture sources */
    press(ui, PC5_KEY_UP, &canvas);
    press(ui, PC5_KEY_UP, &canvas);
    press(ui, PC5_KEY_BACK, &canvas);        /* the first page, on System */
    /* Off again, then rumble: only its own strength shows; then Startup sound again. */
    press(ui, PC5_KEY_UP, &canvas);
    press(ui, PC5_KEY_UP, &canvas);
    press(ui, PC5_KEY_UP, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);      /* In games */
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_DOWN, &canvas);        /* Adaptive triggers */
    press(ui, PC5_KEY_RIGHT, &canvas);       /* both -> off */
    press(ui, PC5_KEY_RIGHT, &canvas);       /* off -> rumble */
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_RIGHT, &canvas);       /* trigger rumble: 75 % -> 100 % */
    press(ui, PC5_KEY_DOWN, &canvas);        /* the last row now */
    press(ui, PC5_KEY_RIGHT, &canvas);       /* trigger rumble: 100 % -> 25 % */
    press(ui, PC5_KEY_LEFT, &canvas);        /* and back */
    press(ui, PC5_KEY_BACK, &canvas);
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);      /* System */
    press(ui, PC5_KEY_RIGHT, &canvas);       /* Startup sound: Off -> cheap chinese speaker, the first */
    {
        Pc5Settings set;
        pc5_settings_load(root, &set);
        CHECK(set.triggers == PC5_TRIGGERS_RUMBLE && set.trigger_rumble == 100 && set.trigger_resistance == 75 &&
              !strcmp(set.startup_sound, "cheap chinese speaker") && console.sounds == 2 &&
              !strcmp(console.sound, "cheap chinese speaker") && console.sound_volume == 100,
              "a strength row only while it is used: triggers=%d rumble=%d resistance=%d sound=%s",
              (int)set.triggers, set.trigger_rumble, set.trigger_resistance, set.startup_sound);
    }
    /* Its volume, under it while there is a sound: played at the new one. */
    press(ui, PC5_KEY_DOWN, &canvas);        /* Startup sound volume */
    save_shot(&canvas, "settings-system-volume");
    press(ui, PC5_KEY_LEFT, &canvas);        /* 100 % -> 90 % */
    press(ui, PC5_KEY_LEFT, &canvas);        /* -> 80 % */
    {
        Pc5Settings set;
        pc5_settings_load(root, &set);
        CHECK(set.startup_volume == 80 && console.sounds == 4 && !strcmp(console.sound, "cheap chinese speaker") &&
              console.sound_volume == 80, "startup volume: %d, played %d times at %d", set.startup_volume,
              console.sounds, console.sound_volume);
    }
    press(ui, PC5_KEY_UP, &canvas);          /* Startup sound */
    /* Overlay: its lines, each on its own, and the corner. */
    press(ui, PC5_KEY_BACK, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "settings"), "back to the first page");
    press(ui, PC5_KEY_UP, &canvas);          /* File manager */
    press(ui, PC5_KEY_UP, &canvas);          /* Overlay */
    press(ui, PC5_KEY_ACCEPT, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);      /* Show FPS: on */
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_RIGHT, &canvas);       /* Frame time: on */
    for (int i = 0; i < 4; i++) press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);      /* Memory: on */
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_RIGHT, &canvas);       /* corner: top right -> bottom left */
    press(ui, PC5_KEY_DOWN, &canvas);        /* the last row: still the corner */
    {
        Pc5Settings set;
        pc5_settings_load(root, &set);
        CHECK(set.fps && set.frame_time && !set.cpu_usage && !set.cpu_temperature && !set.soc_temperature && set.memory &&
              set.fps_corner == PC5_CORNER_BOTTOM_LEFT,
              "overlay saved: fps=%d frame_time=%d cpu=%d cpu_temp=%d soc_temp=%d memory=%d corner=%d", set.fps,
              set.frame_time, set.cpu_usage, set.cpu_temperature, set.soc_temperature, set.memory, (int)set.fps_corner);
        CHECK(pc5_game_overlay_lines(&set) == (PW_PC5_HUD_FPS | PW_PC5_HUD_FRAME_TIME | PW_PC5_HUD_MEMORY),
              "its lines: %#x", pc5_game_overlay_lines(&set));
    }
    /* System's Check now finds a newer PC5 and says so once, with the console's notification. */
    press(ui, PC5_KEY_BACK, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "settings"), "back to the first page, on Overlay");
    press(ui, PC5_KEY_DOWN, &canvas);        /* File manager */
    press(ui, PC5_KEY_DOWN, &canvas);        /* System */
    press(ui, PC5_KEY_ACCEPT, &canvas);
    for (int i = 0; i < 5; i++) press(ui, PC5_KEY_DOWN, &canvas);   /* Check now, past the startup sound's volume */
    /* a release newer than any this test runs on */
    net.release = "{\"tag_name\":\"v99.0.0\",\"html_url\":\"https://github.com/bornaradusin/PC5/releases/tag/v99.0.0\"}";
    press(ui, PC5_KEY_ACCEPT, &canvas);
    until_screen(ui, "settings", &canvas);
    CHECK(console.notifications == 1 && strstr(console.told, "PC5 99.0.0 is available"), "told: %s", console.told);
    press(ui, PC5_KEY_ACCEPT, &canvas);
    until_screen(ui, "settings", &canvas);
    CHECK(console.notifications == 1, "once per release");
    /* With that newer PC5 known: every page of Settings, each row and value, has its text whole. */
    rows_uncut(ui, &canvas, "System", 1);
    for (int i = 0; i < 12; i++) press(ui, PC5_KEY_UP, &canvas);
    for (int i = 0; i < 3; i++) press(ui, PC5_KEY_DOWN, &canvas);   /* Picture sources */
    press(ui, PC5_KEY_ACCEPT, &canvas);
    rows_uncut(ui, &canvas, "Picture sources", 1);
    press(ui, PC5_KEY_BACK, &canvas);
    press(ui, PC5_KEY_BACK, &canvas);
    rows_uncut(ui, &canvas, "Settings", 0);
    for (int row = 0; row < 5; row++) {     /* Game maintenance, In games, Overlay, File manager's places, and System last */
        static const char *const pages[] = { "Game maintenance", "In games", "Overlay", "File manager", "System" };
        for (int i = 0; i < 12; i++) press(ui, PC5_KEY_UP, &canvas);
        for (int i = 0; i < row; i++) press(ui, PC5_KEY_DOWN, &canvas);
        press(ui, PC5_KEY_ACCEPT, &canvas);
        rows_uncut(ui, &canvas, pages[row], row > 0 && row != 3);
        if (row < 4) press(ui, PC5_KEY_BACK, &canvas);
    }
    press(ui, PC5_KEY_BACK, &canvas);
    press(ui, PC5_KEY_BACK, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "library"), "settings closed: %s", pc5_ui_screen(ui));

    /* Each game's menu, its first page and each group's, every row and value: no text cut short. */
    pc5_library_load(root, &library);
    for (int g = 0; g < library.count; g++) {
        char where[160];
        for (int i = 0; i < 10; i++) press(ui, PC5_KEY_LEFT, &canvas);
        for (int i = 0; i < g; i++) press(ui, PC5_KEY_RIGHT, &canvas);
        press(ui, PC5_KEY_TRIANGLE, &canvas);
        if (strcmp(pc5_ui_screen(ui), "game-menu")) continue;
        snprintf(where, sizeof(where), "%s's menu", library.games[g].id);
        rows_uncut(ui, &canvas, where, 1);                  /* ends on Manage, the last row */
        for (int page = 0; page < 3; page++) {               /* Manage, Appearance, Graphics */
            press(ui, PC5_KEY_ACCEPT, &canvas);
            snprintf(where, sizeof(where), "%s's menu, group %d", library.games[g].id, page);
            rows_uncut(ui, &canvas, where, page == 2);
            press(ui, PC5_KEY_BACK, &canvas);
            press(ui, PC5_KEY_UP, &canvas);
        }
        /* The Graphics API window, where the game has one. */
        press(ui, PC5_KEY_DOWN, &canvas);
        press(ui, PC5_KEY_ACCEPT, &canvas);
        press(ui, PC5_KEY_UP, &canvas);
        for (int i = 0; i < 12; i++) press(ui, PC5_KEY_UP, &canvas);
        if (!cuts_after(ui, PC5_KEY_ACCEPT, &canvas) && !strcmp(pc5_ui_screen(ui), "graphics-api")) {
            int cut = 0;
            for (int i = 0; i < 3; i++) cut += cuts_after(ui, PC5_KEY_DOWN, &canvas);
            CHECK(!cut, "%s: the Graphics API window's text whole", library.games[g].id);
            press(ui, PC5_KEY_BACK, &canvas);
        }
        press(ui, PC5_KEY_TRIANGLE, &canvas);
        CHECK(!strcmp(pc5_ui_screen(ui), "library"), "back to the library: %s", pc5_ui_screen(ui));
    }

    /* A game's menu: Change program, for the engine game's other program. */
    int orbital = pc5_library_find(&library, "orbital-nine");
    CHECK(orbital >= 0, "the engine game is there");
    for (int i = 0; i < 10; i++) press(ui, PC5_KEY_LEFT, &canvas);
    for (int i = 0; i < orbital; i++) press(ui, PC5_KEY_RIGHT, &canvas);
    press(ui, PC5_KEY_TRIANGLE, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "game-menu"), "game menu: %s", pc5_ui_screen(ui));
    press(ui, PC5_KEY_DOWN, &canvas);       /* past Controller (the Xbox controller: no Button mapping) */
    press(ui, PC5_KEY_DOWN, &canvas);       /* Graphics */
    press(ui, PC5_KEY_ACCEPT, &canvas);
    save_shot(&canvas, "game-menu-graphics");
    /* Graphics API, the first row of a Direct3D 12 game: a window with each choice, Auto in use and chosen.
       Down and accept: Wine's VKD3D; left and right don't change it; back leaves it as it is. */
    press(ui, PC5_KEY_RIGHT, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "game-menu") && !controls_has("orbital-nine", "graphics_api = wine\n"),
          "right on Graphics API changes nothing");
    press(ui, PC5_KEY_ACCEPT, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "graphics-api"), "Graphics API opens its window: %s", pc5_ui_screen(ui));
    press(ui, PC5_KEY_DOWN, &canvas);
    pc5_ui_draw(ui, &canvas);
    save_shot(&canvas, "game-menu-graphics-api");
    press(ui, PC5_KEY_ACCEPT, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "game-menu") && controls_has("orbital-nine", "graphics_api = wine\n"),
          "Graphics API: Wine's VKD3D: %s", pc5_ui_screen(ui));
    press(ui, PC5_KEY_ACCEPT, &canvas);     /* open again on Wine's VKD3D: up to Auto, but back keeps it */
    press(ui, PC5_KEY_UP, &canvas);
    press(ui, PC5_KEY_BACK, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "game-menu") && controls_has("orbital-nine", "graphics_api = wine\n"),
          "back keeps Wine's VKD3D");
    press(ui, PC5_KEY_ACCEPT, &canvas);
    press(ui, PC5_KEY_UP, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);
    CHECK(controls_has("orbital-nine", "graphics_api = auto\n"), "and Auto");
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);     /* Frame generation, the next row: on, then off again with right */
    CHECK(!strcmp(pc5_ui_screen(ui), "game-menu") && controls_has("orbital-nine", "frame_generation = on\n"),
          "Frame generation on: %s", pc5_ui_screen(ui));
    press(ui, PC5_KEY_RIGHT, &canvas);
    CHECK(controls_has("orbital-nine", "frame_generation = off\n"), "and off");
    press(ui, PC5_KEY_DOWN, &canvas);       /* Resolution: 1440p, 4K, back round to 1080p */
    press(ui, PC5_KEY_ACCEPT, &canvas);
    CHECK(controls_has("orbital-nine", "resolution = 1440p\n"), "Resolution 1440p");
    press(ui, PC5_KEY_RIGHT, &canvas);
    CHECK(controls_has("orbital-nine", "resolution = 2160p\n"), "then 4K");
    press(ui, PC5_KEY_RIGHT, &canvas);
    CHECK(controls_has("orbital-nine", "resolution = 1080p\n"), "then 1080p again");
    press(ui, PC5_KEY_DOWN, &canvas);       /* Aspect ratio: Stretch, then Keep again */
    press(ui, PC5_KEY_ACCEPT, &canvas);
    CHECK(controls_has("orbital-nine", "aspect_ratio = stretch\n"), "Aspect ratio: Stretch");
    press(ui, PC5_KEY_LEFT, &canvas);
    CHECK(controls_has("orbital-nine", "aspect_ratio = keep\n"), "then Keep");
    press(ui, PC5_KEY_DOWN, &canvas);       /* CPU sharing (a 64-bit game: no Memory): on, then off again */
    press(ui, PC5_KEY_ACCEPT, &canvas);
    CHECK(controls_has("orbital-nine", "cpu_sharing = on\n"), "CPU sharing on");
    press(ui, PC5_KEY_RIGHT, &canvas);
    CHECK(controls_has("orbital-nine", "cpu_sharing = off\n"), "then off");
    press(ui, PC5_KEY_DOWN, &canvas);       /* the last row of Graphics: still CPU sharing */
    press(ui, PC5_KEY_ACCEPT, &canvas);
    CHECK(controls_has("orbital-nine", "cpu_sharing = on\n"), "the last row: CPU sharing");
    press(ui, PC5_KEY_ACCEPT, &canvas);
    CHECK(controls_has("orbital-nine", "cpu_sharing = off\n"), "and off");
    press(ui, PC5_KEY_BACK, &canvas);       /* the first page again, on Graphics */
    CHECK(!strcmp(pc5_ui_screen(ui), "game-menu"), "Back: the first page: %s", pc5_ui_screen(ui));
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_DOWN, &canvas);       /* Manage */
    press(ui, PC5_KEY_ACCEPT, &canvas);
    save_shot(&canvas, "game-menu-manage");
    press(ui, PC5_KEY_ACCEPT, &canvas);     /* Change program, its first row */
    until_screen(ui, "programs", &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "programs"), "its programs: %s", pc5_ui_screen(ui));
    frames(ui, 0, 0, 30, &canvas);
    save_shot(&canvas, "change-program");
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "message"), "changed: %s", pc5_ui_screen(ui));
    {
        Pc5Info info;
        CHECK(!pc5_info_read(root, "orbital-nine", &info) && !strcmp(info.exe, "Orbital Nine.exe"), "the other program: %s",
              info.exe);
        PwWineLibrary *pw = pc5_alloc(sizeof(*pw));
        pw_wine_library_load(pw, root);
        const PwGameProfile *profile = pw_wine_library_find(pw, "orbital-nine");
        CHECK(profile && strstr(profile->app.executable, "\\Orbital Nine.exe"), "its profile starts it");
        pc5_free(pw);
    }
    press(ui, PC5_KEY_ACCEPT, &canvas);
    test_controller_screens(ui, &canvas);

    /* A game's menu: Home screen tile, made, then taken away. */
    press(ui, PC5_KEY_TRIANGLE, &canvas);
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_DOWN, &canvas);       /* Play, Controller, Graphics, Appearance */
    press(ui, PC5_KEY_ACCEPT, &canvas);
    save_shot(&canvas, "game-menu-appearance");
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_DOWN, &canvas);       /* Cover, Background, Home screen tile */
    press(ui, PC5_KEY_ACCEPT, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "message") && pc5_home_find(tiles, "orbital-nine", tile), "tile made: %s %s",
          pc5_ui_screen(ui), tile);
    press(ui, PC5_KEY_ACCEPT, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "game-menu"), "back to its menu: %s", pc5_ui_screen(ui));
    press(ui, PC5_KEY_ACCEPT, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "message") && !pc5_home_find(tiles, "orbital-nine", tile), "taken away");
    press(ui, PC5_KEY_ACCEPT, &canvas);
    test_picture_screens(ui, &canvas, &console);
    press(ui, PC5_KEY_TRIANGLE, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "library"), "Triangle: the library from any page: %s", pc5_ui_screen(ui));

    /* Several games in a row: two ticked folders, read, then installed one after the other. */
    pc5_library_load(root, &library);
    int before = library.count;
    press(ui, PC5_KEY_OPTIONS, &canvas);
    press(ui, PC5_KEY_UP, &canvas);
    press(ui, PC5_KEY_UP, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);      /* Game maintenance */
    press(ui, PC5_KEY_ACCEPT, &canvas);
    until_screen(ui, "sources", &canvas);
    press(ui, PC5_KEY_SQUARE, &canvas);      /* NeonDrift */
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_SQUARE, &canvas);      /* Starfall */
    press(ui, PC5_KEY_OPTIONS, &canvas);
    until_screen(ui, "queue", &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "queue"), "the list to install: %s", pc5_ui_screen(ui));
    press(ui, PC5_KEY_ACCEPT, &canvas);      /* review the first, take another cover, back */
    CHECK(!strcmp(pc5_ui_screen(ui), "review"), "one reviewed: %s", pc5_ui_screen(ui));
    press(ui, PC5_KEY_R1, &canvas);
    frames(ui, 0, 0, 5, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "queue"), "back to the list: %s", pc5_ui_screen(ui));
    press(ui, PC5_KEY_OPTIONS, &canvas);     /* install all */
    until_screen(ui, "message", &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "message"), "the summary: %s", pc5_ui_screen(ui));
    press(ui, PC5_KEY_ACCEPT, &canvas);
    pc5_library_load(root, &library);
    CHECK(library.count == before + 2, "two more games: %d then %d", before, library.count);
    pc5_ui_destroy(ui);
    pc5_free(canvas.px);
}

/* Report on GitHub: the link to GitHub's form, filled in, as a QR code; and the crash
 * message's Triangle opens it. */
static void test_report(const char *fonts)
{
    Pc5LogIssue issue;
    char url[PC5_REPORT_URL];
    static Pc5Qr qr;

    memset(&issue, 0, sizeof(issue));
    issue.problem = PC5_LOG_GAME_CRASHED;
    pc5_copy(issue.profile, sizeof(issue.profile), "grim-dawn");
    pc5_copy(issue.detail, sizeof(issue.detail), "wine: Unhandled page fault on read access to 0000000000000000 at `address`");
    pc5_copy(issue.exe, sizeof(issue.exe), "C:\\Games\\Grim Dawn\\x64\\Grim Dawn.exe");
    pc5_copy(issue.firmware, sizeof(issue.firmware), "12.40");
    pc5_copy(issue.file, sizeof(issue.file), "/data/prospero-win/logs/issue-2026-10-25_09-19_grim-dawn.zip");
    CHECK(!strcmp(pc5_report_file(&issue), "issue-2026-10-25_09-19_grim-dawn.zip"), "the zip's name");
    CHECK(!pc5_report_url(&issue, "Grim Dawn", url, sizeof(url)), "the link");
    {
        /* The fields of .github/ISSUE_TEMPLATE/1-game.yml by their ids, in this order. */
        char want[PC5_REPORT_URL];
        snprintf(want, sizeof(want),
                 "https://github.com/bornaradusin/PC5/issues/new?template=1-game.yml"
                 "&title=Grim%%20Dawn%%3A%%20stopped%%20with%%20an%%20error&game=Grim%%20Dawn&program=Grim%%20Dawn.exe"
                 "&pc5=%s&firmware=12.40&saw=stopped%%20with%%20an%%20error%%0A%%60wine%%3A%%20Unhandled%%20page%%20fault"
                 "%%20on%%20read%%20access%%20to%%200000000000000000%%20at%%20%%27address%%27%%60",
                 PC5_VERSION);
        CHECK(!strcmp(url, want), "the game's issue form, its title, game, program, PC5, firmware and what PC5 saw filled in: %s", url);
        CHECK(!strchr(url, ' ') && !strchr(url, '\n') && !strstr(url, "body="), "nothing unencoded, and no text of its own");
        Pc5LogIssue own = issue;
        pc5_copy(own.profile, sizeof(own.profile), "launcher");
        own.exe[0] = 0;
        CHECK(!pc5_report_url(&own, NULL, url, sizeof(url)) &&
                  !strncmp(url, "https://github.com/bornaradusin/PC5/issues/new?template=2-pc5.yml&title=PC5%3A%20",
                           strlen("https://github.com/bornaradusin/PC5/issues/new?template=2-pc5.yml&title=PC5%3A%20")) &&
                  !strstr(url, "&game=") && strstr(url, "&firmware=12.40&saw="),
              "PC5's own session: PC5's issue form: %s", url);
        CHECK(!pc5_report_url(&issue, NULL, url, sizeof(url)) && strstr(url, "template=1-game.yml&title=grim-dawn%3A") &&
                  strstr(url, "&game=grim-dawn&"),
              "a game not in the library: its id: %s", url);
    }
    CHECK(pc5_report_url(&issue, "Grim Dawn", url, 200) == -1, "a link that doesn't fit");
    CHECK(!pc5_report_url(&issue, "Grim Dawn", url, sizeof(url)) && !pc5_qr_make(url, &qr) && qr.size >= 21 &&
          (qr.size - 17) % 4 == 0 && qr.size <= 177, "as a QR code: %d modules", qr.size);
    int finder = 1;
    for (int y = 0; y < 7; y++)
        for (int x = 0; x < 7; x++) {
            int ring = x == 0 || y == 0 || x == 6 || y == 6, core = x >= 2 && x <= 4 && y >= 2 && y <= 4;
            finder &= pc5_qr_dark(&qr, x, y) == (ring || core) && pc5_qr_dark(&qr, qr.size - 1 - x, y) == (ring || core) &&
                      pc5_qr_dark(&qr, x, qr.size - 1 - y) == (ring || core);
        }
    CHECK(finder, "its three finder patterns");
    {
        /* tests/test.sh has it read back (zbarimg when there is one) */
        char file[PC5_PATH];
        path(file, sizeof(file), "%s/report-qr.txt", work);
        FILE *out = fopen(file, "w");
        if (out) {
            fprintf(out, "%s\n", url);
            for (int y = 0; y < qr.size; y++) {
                for (int x = 0; x < qr.size; x++) fputc(pc5_qr_dark(&qr, x, y) ? '1' : '0', out);
                fputc('\n', out);
            }
            fclose(out);
        }
    }
    static char big[5000];
    memset(big, 'x', sizeof(big) - 1);
    CHECK(pc5_qr_make(big, &qr) == -1 && !qr.size, "text too long for any code");

    /* The crash message after a game crashed: Triangle shows the code, Back goes back. */
    FakeConsole console = { 0 };
    FakeNet net = { 0 };
    Pc5Platform platform = { &console, { &net, fake_get, NULL, fake_get_authorized }, fake_clock, fake_play, NULL, fake_notify, fake_time, 0, NULL, NULL,
                             NULL, NULL, NULL };
    const char *roots[] = { usb, NULL };
    Pc5Canvas canvas = { pc5_alloc(1920u * 1080u * 4u), 1920, 1080, 1920, 0, 0 };
    Pc5Library library;
    char logs[PC5_PATH], file[PC5_PATH], text[1024], kept[PC5_PATH];
    pc5_library_load(root, &library);
    CHECK(library.count > 0, "a game to crash");
    if (!library.count || !canvas.px) { pc5_free(canvas.px); return; }
    path(logs, sizeof(logs), "%s/logs", root);
    pc5_mkdirs(logs);
    path(file, sizeof(file), "%s/next.txt", logs);
    write_text(file, "2\n");
    snprintf(text, sizeof(text), "PW_REPORT/1 build=abc profile=%s cycle=1 pid=9 time=1792699000\n"
             "REC seq=1 t=1.0 WINE wine: Unhandled page fault on read access to 00000000 at address 00401000 (thread 0024), "
             "starting debugger...\nREC seq=2 t=2.0 PW_WINE64 session_end reason=wine-exit\n", library.games[0].id);
    path(file, sizeof(file), "%s/session-0.log", logs);
    write_text(file, text);
    Pc5UpdateMemory before;
    pc5_update_load(root, &before);
    {
        /* Not asked yet, and GitHub doesn't answer: Triangle asks, then reports. */
        Pc5UpdateMemory memory;
        memset(&memory, 0, sizeof(memory));
        pc5_update_save(root, &memory);
        Pc5UiConfig config = { root, template_prefix, fonts, roots, NULL, NULL, NULL, NULL, NULL, NULL, 0, 0 };
        Pc5Ui *ui = pc5_ui_create(&config, &platform);
        until_screen(ui, "message", &canvas);
        CHECK(!strcmp(pc5_ui_screen(ui), "message"), "the crash told: %s", pc5_ui_screen(ui));
        press(ui, PC5_KEY_TRIANGLE, &canvas);
        CHECK(!strcmp(pc5_ui_screen(ui), "report"), "Triangle: Report on GitHub: %s", pc5_ui_screen(ui));
        press(ui, PC5_KEY_BACK, &canvas);
        CHECK(!strcmp(pc5_ui_screen(ui), "message"), "Back: the message again: %s", pc5_ui_screen(ui));
        press(ui, PC5_KEY_ACCEPT, &canvas);
        CHECK(!strcmp(pc5_ui_screen(ui), "library"), "then the library: %s", pc5_ui_screen(ui));
        pc5_ui_destroy(ui);
    }
    {
        /* A newer PC5 is out (asked a minute ago): Triangle offers it first; Report anyway reports, Update now
         * (PC5 can't update itself in this test) opens Settings › System. */
        Pc5UpdateMemory memory;
        memset(&memory, 0, sizeof(memory));
        memory.checked = fake_time(NULL, NULL) - 60;
        memory.last.state = PC5_UPDATE_AVAILABLE;
        pc5_copy(memory.last.version, sizeof(memory.last.version), "99.0.0");
        pc5_update_save(root, &memory);
        path(file, sizeof(file), "%s/next.txt", logs);
        write_text(file, "3\n");
        path(file, sizeof(file), "%s/session-0.log", logs);
        write_text(file, text);
        Pc5UiConfig config = { root, template_prefix, fonts, roots, NULL, NULL, NULL, NULL, NULL, NULL, 0, 0 };
        console.notifications = 0;
        Pc5Ui *ui = pc5_ui_create(&config, &platform);
        until_screen(ui, "message", &canvas);
        press(ui, PC5_KEY_TRIANGLE, &canvas);
        CHECK(!strcmp(pc5_ui_screen(ui), "report-update") && !console.notifications, "Triangle: update first: %s %d",
              pc5_ui_screen(ui), console.notifications);
        pc5_ui_draw(ui, &canvas);
        save_shot(&canvas, "report-update");
        press(ui, PC5_KEY_BACK, &canvas);
        CHECK(!strcmp(pc5_ui_screen(ui), "message"), "Back: the message: %s", pc5_ui_screen(ui));
        press(ui, PC5_KEY_TRIANGLE, &canvas);
        press(ui, PC5_KEY_RIGHT, &canvas);
        frames(ui, 0, 0, 25, &canvas);
        press(ui, PC5_KEY_ACCEPT, &canvas);
        CHECK(!strcmp(pc5_ui_screen(ui), "report"), "Report anyway: Report on GitHub: %s", pc5_ui_screen(ui));
        press(ui, PC5_KEY_BACK, &canvas);
        press(ui, PC5_KEY_TRIANGLE, &canvas);
        frames(ui, 0, 0, 25, &canvas);
        press(ui, PC5_KEY_ACCEPT, &canvas);
        CHECK(!strcmp(pc5_ui_screen(ui), "settings"), "Update now, where PC5 can't update itself: Settings › System: %s",
              pc5_ui_screen(ui));
        pc5_ui_destroy(ui);
    }
    pc5_update_save(root, &before);
    CHECK(!pc5_logs_newest(root, library.games[0].id, kept, sizeof(kept)) && pc5_ends_with_ci(kept, ".zip"), "kept: %s", kept);
    unlink(kept);
    pc5_free(canvas.px);
}


/* The screen as a PPM in WORK/shots (for a look at the new screens). */
static void save_shot(const Pc5Canvas *c, const char *name)
{
    char file[PC5_PATH];
    path(file, sizeof(file), "%s/shots", work);
    pc5_mkdirs(file);
    path(file, sizeof(file), "%s/shots/%s.ppm", work, name);
    FILE *out = fopen(file, "wb");
    if (!out) return;
    fprintf(out, "P6\n%d %d\n255\n", c->w, c->h);
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++) {
            uint32_t p = c->px[(size_t)y * (size_t)c->stride + (size_t)x];
            fputc((int)(p >> 16 & 255), out);
            fputc((int)(p >> 8 & 255), out);
            fputc((int)(p & 255), out);
        }
    fclose(out);
}

/* When PC5 opens with a newer release out: it asks; Later leaves it for Settings ›
 * System, whose Update now installs it, shows the progress, and closes PC5. */
static void test_upgrade_screens(const char *fonts)
{
    FakeConsole console = { 0 };
    static FakeNet net;
    Pc5Platform platform = { &console, { &net, fake_get, fake_download, fake_get_authorized }, fake_clock, fake_play, NULL, fake_notify,
                             fake_time, 0, fake_restart, NULL, NULL, NULL, NULL };
    const char *roots[] = { usb, NULL };
    char app[PC5_PATH], drive[PC5_PATH], folder[PC5_PATH], file[PC5_PATH], zip[PC5_PATH], digest[65];
    static char json[2048];
    Pc5Canvas canvas = { pc5_alloc(1920u * 1080u * 4u), 1920, 1080, 1920, 0, 0 };
    struct stat st;

    memset(&net, 0, sizeof(net));
    path(app, sizeof(app), "%s/app0", work);
    path(file, sizeof(file), "%s/eboot.bin", app);
    write_text(file, "PC5 SCREENS EBOOT\n");
    path(file, sizeof(file), "%s/sce_sys/param.json", app);
    write_text(file, "{\"titleId\":\"PPSA11018\"}");
    path(drive, sizeof(drive), "%s/screens-drive", work);
    path(folder, sizeof(folder), "%s/homebrew/PPSA11018", drive);
    make_installed(app, folder);
    path(zip, sizeof(zip), "%s/screens-release.zip", work);
    make_release_zip(zip, NULL);
    file_sha256(zip, digest);
    stat(zip, &st);
    release_json(json, sizeof(json), "v99.0.0", (uint64_t)st.st_size, digest);
    net.release = json;
    pc5_copy(net.zip, sizeof(net.zip), zip);
    const char *drives[] = { drive, NULL };
    {
        Pc5UpdateMemory memory;
        Pc5Settings set;
        memset(&memory, 0, sizeof(memory));
        pc5_update_save(root, &memory);
        pc5_settings_load(root, &set);
        set.check_updates = 1;
        pc5_settings_save(root, &set);
    }
    Pc5UiConfig config = { root, template_prefix, fonts, roots, NULL, NULL, NULL, drives, NULL, NULL, 0, 0 };
    Pc5Ui *ui = pc5_ui_create(&config, &platform);
    until_screen(ui, "upgrade-ask", &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "upgrade-ask") && !console.notifications, "asked when PC5 opens: %s %d",
          pc5_ui_screen(ui), console.notifications);
    pc5_ui_draw(ui, &canvas);
    save_shot(&canvas, "upgrade-ask");
    press(ui, PC5_KEY_RIGHT, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);
    until_screen(ui, "library", &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "library") && !net.downloads, "Later: %s", pc5_ui_screen(ui));

    press(ui, PC5_KEY_OPTIONS, &canvas);
    for (int i = 0; i < 4; i++) press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);      /* System */
    for (int i = 0; i < 6; i++) press(ui, PC5_KEY_DOWN, &canvas);   /* Update now, the seventh row */
    pc5_ui_draw(ui, &canvas);
    save_shot(&canvas, "updates-page");
    net.gate = 1;
    net.gate_bytes = 14000;
    press(ui, PC5_KEY_ACCEPT, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "upgrading"), "Update now: %s", pc5_ui_screen(ui));
    for (int i = 0; i < 20000 && !net.at_gate; i++) usleep(1000);
    for (int i = 0; i < 70; i++) { frames(ui, 0, 0, 1, &canvas); usleep(1000); }
    pc5_ui_draw(ui, &canvas);
    save_shot(&canvas, "upgrading");
    press(ui, PC5_KEY_ACCEPT, &canvas);      /* nothing to choose while it works */
    CHECK(!strcmp(pc5_ui_screen(ui), "upgrading") && !console.restarts, "still updating");
    net.gate = 0;
    /* Done once the new files are in place and the work folder is gone; then Restart PC5. */
    path(zip, sizeof(zip), "%s/pc5-update", drive);
    path(file, sizeof(file), "%s/eboot.bin", folder);
    for (int i = 0; i < 20000 && (pc5_exists(zip) || !file_is(file, "PC5 NEW EBOOT\n")); i++) {
        frames(ui, 0, 0, 1, &canvas);
        usleep(1000);
    }
    CHECK(file_is(file, "PC5 NEW EBOOT\n") && !pc5_exists(zip), "installed from the screens");
    for (int i = 0; i < 100 && !console.restarts; i++) {
        usleep(2000);
        frames(ui, 0, 0, 1, &canvas);
        if (i == 50) {
            pc5_ui_draw(ui, &canvas);
            save_shot(&canvas, "upgraded");
        }
        if (i >= 50) press(ui, PC5_KEY_ACCEPT, &canvas);
    }
    CHECK(console.restarts == 1, "Restart PC5: %d", console.restarts);
    pc5_ui_destroy(ui);
    /* An update finished as PC5 opened: this PC5 is the old one, and restarts into the new one. */
    config.finished = "98.0.0";
    ui = pc5_ui_create(&config, &platform);
    until_screen(ui, "upgrading", &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "upgrading"), "finished as it opened: %s", pc5_ui_screen(ui));
    pc5_ui_draw(ui, &canvas);
    save_shot(&canvas, "upgrade-finished");
    press(ui, PC5_KEY_ACCEPT, &canvas);
    CHECK(console.restarts == 2, "and it restarts: %d", console.restarts);
    pc5_ui_destroy(ui);
    /* Opened by the player, though it asked a moment ago: it asks again and shows its window,
     * with no console notification saying the same. */
    {
        Pc5UpdateMemory memory;
        memset(&memory, 0, sizeof(memory));
        memory.checked = 1792700000;
        pc5_update_save(root, &memory);
    }
    path(folder, sizeof(folder), "%s/homebrew/PPSA11018", drive);
    make_installed(app, folder);
    config.finished = NULL;
    config.opened = 1;
    console.notifications = 0;
    ui = pc5_ui_create(&config, &platform);
    until_screen(ui, "upgrade-ask", &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "upgrade-ask") && console.notifications == 0,
          "opened: asked again, the window only: %s %d '%s'", pc5_ui_screen(ui), console.notifications, console.told);
    pc5_ui_destroy(ui);
    /* Back after a game (not opened), within the hour: not asked again. */
    config.opened = 0;
    console.notifications = 0;
    ui = pc5_ui_create(&config, &platform);
    until_screen(ui, "library", &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "library") && !console.notifications, "after a game: not asked within the hour: %s",
          pc5_ui_screen(ui));
    pc5_ui_destroy(ui);
    pc5_free(canvas.px);
}

/* ---- the file manager's core (pc5_files.c) ---------------------------------------------------- */

static int read_is(const char *file, const char *text)
{
    Pc5Buf buf = {0};
    int same = !pc5_read_file(file, &buf, 1 << 20) && buf.len == strlen(text) && !memcmp(buf.data, text, buf.len);
    pc5_buf_free(&buf);
    return same;
}

static int files_run(Pc5FilesOp op, const char *const *sources, int count, const char *dest, Pc5FilesConflict conflict,
                     Pc5FilesJob *job)
{
    int rc = pc5_files_begin(job, op, sources, count, dest, conflict), steps = 0;
    if (!rc) {
        while (pc5_files_step(job, 1) && steps < 100000) steps++;
        /* once a step says there is no more, it says how it ended (never still working) */
        CHECK(job->state != PC5_FILES_WORKING, "a %d job ended, not still working", (int)op);
    }
    return rc;
}

static void test_files(void)
{
    char base[PC5_PATH], a[PC5_PATH], b[PC5_PATH], file[PC5_PATH], why[PC5_ERROR], name[PC5_FILES_NAME];
    Pc5FileList list;
    Pc5FilesJob job;

    path(base, sizeof(base), "%s/files", work);
    path(a, sizeof(a), "%s/a", base);
    path(b, sizeof(b), "%s/b", base);
    path(file, sizeof(file), "rm -rf '%s'", base);
    run(file);
    path(file, sizeof(file), "%s/Game.exe", a); write_text(file, "program");
    path(file, sizeof(file), "%s/data/level1.pak", a); write_text(file, "one");
    path(file, sizeof(file), "%s/data/deep/x.txt", a); write_text(file, "x");
    path(file, sizeof(file), "%s/readme.txt", a); write_text(file, "hello");
    path(file, sizeof(file), "%s/Zeta", a); pc5_mkdirs(file);
    pc5_mkdirs(b);

    /* listing: folders first, by name with case aside */
    CHECK(!pc5_files_list(a, &list) && list.count == 4, "a's four items listed (%d)", list.count);
    if (list.count == 4) {
        CHECK(list.items[0].dir && !strcmp(list.items[0].name, "data") && list.items[1].dir && !strcmp(list.items[1].name, "Zeta"),
              "folders first, case aside: %s %s", list.items[0].name, list.items[1].name);
        CHECK(!strcmp(list.items[2].name, "Game.exe") && list.items[2].size == 7 && !strcmp(list.items[3].name, "readme.txt"),
              "then files with their sizes");
    }
    pc5_files_list_free(&list);
    path(file, sizeof(file), "%s/none", base);
    CHECK(pc5_files_list(file, &list) == -1 && list.error == ENOENT && !list.count, "a folder that isn't there says why");
    pc5_files_list_free(&list);

    /* places and names */
    CHECK(pc5_files_within("/data", "/data") && pc5_files_within("/data", "/data/a/b") && pc5_files_within("/data/", "/data/x"),
          "inside /data");
    CHECK(!pc5_files_within("/data", "/data2") && !pc5_files_within("/data", "/mnt/usb0") && !pc5_files_within("/data", "/data/../etc") &&
          !pc5_files_within("/data", "/data/./x"), "not inside /data");
    CHECK(pc5_files_within("/", "/system") && !pc5_files_within("/", "/a/../b"), "everything is inside /, but dot parts");
    CHECK(!pc5_files_name_ok("Saves 2", why, sizeof(why)) && !pc5_files_name_ok(".hidden", why, sizeof(why)), "good names");
    CHECK(pc5_files_name_ok("", why, sizeof(why)) && pc5_files_name_ok("..", why, sizeof(why)) && pc5_files_name_ok("a/b", why, sizeof(why)) &&
          pc5_files_name_ok("a:b", why, sizeof(why)) && pc5_files_name_ok("end.", why, sizeof(why)) && pc5_files_name_ok("end ", why, sizeof(why)) &&
          pc5_files_name_ok("tab\there", why, sizeof(why)), "bad names refused");
    pc5_files_name_ok("a?b", why, sizeof(why));
    CHECK(strstr(why, "?") != NULL, "the reason names the character: %s", why);
    {
        char longer[300];
        memset(longer, 'n', 299); longer[299] = 0;
        CHECK(pc5_files_name_ok(longer, why, sizeof(why)) && strstr(why, "255"), "a name over 255 bytes refused");
    }
    CHECK(!pc5_files_free_name(a, "new.txt", name, sizeof(name)) && !strcmp(name, "new.txt"), "a free name stays");
    CHECK(!pc5_files_free_name(a, "readme.txt", name, sizeof(name)) && !strcmp(name, "readme (2).txt"), "a taken name gets (2): %s", name);
    path(file, sizeof(file), "%s/readme (2).txt", a); write_text(file, "");
    CHECK(!pc5_files_free_name(a, "readme.txt", name, sizeof(name)) && !strcmp(name, "readme (3).txt"), "then (3): %s", name);
    unlink(file);
    CHECK(!pc5_files_free_name(a, "data", name, sizeof(name)) && !strcmp(name, "data (2)"), "a folder gets (2): %s", name);

    /* copy a folder and a file into b */
    {
        char data[PC5_PATH], readme[PC5_PATH];
        path(data, sizeof(data), "%s/data", a);
        path(readme, sizeof(readme), "%s/readme.txt", a);
        const char *sources[] = { data, readme };
        CHECK(pc5_files_conflicts(sources, 2, b, 1) == 0, "nothing in b yet");
        CHECK(!files_run(PC5_FILES_COPY, sources, 2, b, PC5_FILES_REPLACE, &job) && job.state == PC5_FILES_DONE, "copied: %s", job.error);
        CHECK(job.total_bytes == 9 && job.done_bytes == 9 && job.done_items == job.total_items && job.total_items == 5,
              "progress counted (%llu/%llu bytes, %u/%u items)", (unsigned long long)job.done_bytes,
              (unsigned long long)job.total_bytes, job.done_items, job.total_items);
        pc5_files_end(&job);
        path(file, sizeof(file), "%s/data/deep/x.txt", b);
        CHECK(read_is(file, "x"), "the deep file came");
        path(file, sizeof(file), "%s/readme.txt", b);
        CHECK(read_is(file, "hello"), "the file came");
        CHECK(read_is(readme, "hello") && pc5_is_dir(data), "the sources stay");
        path(file, sizeof(file), "%s/readme.txt.pc5part", b);
        CHECK(!pc5_exists(file), "no .pc5part left");
        CHECK(pc5_files_conflicts(sources, 2, b, 1) == 2, "both names are in b now");
        CHECK(pc5_files_conflicts(sources, 2, a, 1) == 2 && pc5_files_conflicts(sources, 2, a, 0) == 0,
              "in their own folder: names taken for a copy, nothing for a move");

        /* Skip: nothing changes */
        path(file, sizeof(file), "%s/readme.txt", b); write_text(file, "mine");
        CHECK(!files_run(PC5_FILES_COPY, sources, 2, b, PC5_FILES_SKIP, &job) && job.state == PC5_FILES_DONE && job.skipped == 2,
              "skipped both (%u)", job.skipped);
        pc5_files_end(&job);
        CHECK(read_is(file, "mine"), "Skip left b's file");

        /* Replace: the file overwritten, the folder merged */
        path(file, sizeof(file), "%s/data/only-in-b.txt", b); write_text(file, "b");
        CHECK(!files_run(PC5_FILES_COPY, sources, 2, b, PC5_FILES_REPLACE, &job) && job.state == PC5_FILES_DONE, "replaced: %s", job.error);
        pc5_files_end(&job);
        CHECK(read_is(file, "b"), "Replace merges: b's own file stays");
        path(file, sizeof(file), "%s/readme.txt", b);
        CHECK(read_is(file, "hello"), "Replace overwrote the file");

        /* Keep both */
        CHECK(!files_run(PC5_FILES_COPY, sources, 2, b, PC5_FILES_KEEP_BOTH, &job) && job.state == PC5_FILES_DONE, "kept both: %s", job.error);
        pc5_files_end(&job);
        path(file, sizeof(file), "%s/readme (2).txt", b);
        CHECK(read_is(file, "hello"), "the copy is readme (2).txt");
        path(file, sizeof(file), "%s/data (2)/level1.pak", b);
        CHECK(read_is(file, "one"), "the folder copy is data (2)");

        /* a copy pasted where it is: Keep both makes a copy beside it, the others do nothing */
        CHECK(!files_run(PC5_FILES_COPY, &sources[1], 1, a, PC5_FILES_REPLACE, &job) && job.skipped == 1, "pasted on itself: nothing");
        pc5_files_end(&job);
        CHECK(read_is(readme, "hello"), "and the file is whole");
        CHECK(!files_run(PC5_FILES_COPY, &sources[1], 1, a, PC5_FILES_KEEP_BOTH, &job) && job.state == PC5_FILES_DONE, "duplicated");
        pc5_files_end(&job);
        path(file, sizeof(file), "%s/readme (2).txt", a);
        CHECK(read_is(file, "hello"), "the duplicate is readme (2).txt");
        unlink(file);

        /* a file and a folder in each other's place, under Replace */
        path(file, sizeof(file), "rm -rf '%s/data' '%s/readme.txt' && mkdir '%s/readme.txt' && printf f > '%s/data'", b, b, b, b);
        run(file);
        CHECK(!files_run(PC5_FILES_COPY, sources, 2, b, PC5_FILES_REPLACE, &job) && job.state == PC5_FILES_DONE, "kinds swapped: %s", job.error);
        pc5_files_end(&job);
        path(file, sizeof(file), "%s/data/level1.pak", b);
        CHECK(read_is(file, "one"), "a file where a folder goes is replaced by the folder");
        path(file, sizeof(file), "%s/readme.txt", b);
        CHECK(read_is(file, "hello"), "a folder where a file goes is replaced by the file");

        /* a folder into itself */
        char inner[PC5_PATH];
        path(inner, sizeof(inner), "%s/data/deep", a);
        CHECK(files_run(PC5_FILES_COPY, sources, 1, inner, PC5_FILES_REPLACE, &job) == -1 && job.state == PC5_FILES_FAILED &&
              strstr(job.error, "itself"), "a folder into itself refused: %s", job.error);
        pc5_files_end(&job);
        CHECK(files_run(PC5_FILES_MOVE, sources, 1, data, PC5_FILES_REPLACE, &job) == -1, "or moved into itself");
        pc5_files_end(&job);
        path(file, sizeof(file), "%s/gone", base);
        CHECK(files_run(PC5_FILES_COPY, sources, 1, file, PC5_FILES_REPLACE, &job) == -1 && strstr(job.error, "isn't there"),
              "pasting into a folder that's gone: %s", job.error);
        pc5_files_end(&job);
    }

    /* move within the drive: a rename; then a move that merges a folder */
    {
        char from[PC5_PATH], to[PC5_PATH];
        path(from, sizeof(from), "%s/data (2)", b);
        const char *sources[] = { from };
        CHECK(!files_run(PC5_FILES_MOVE, sources, 1, a, PC5_FILES_REPLACE, &job) && job.state == PC5_FILES_DONE, "moved: %s", job.error);
        CHECK(job.total_items == 1, "a move on one drive is one rename (%u)", job.total_items);
        pc5_files_end(&job);
        path(to, sizeof(to), "%s/data (2)/deep/x.txt", a);
        CHECK(read_is(to, "x") && !pc5_exists(from), "it is in a and not in b");
        /* cut and pasted in the same folder: nothing */
        path(from, sizeof(from), "%s/data (2)", a);
        CHECK(!files_run(PC5_FILES_MOVE, sources, 1, a, PC5_FILES_KEEP_BOTH, &job) && job.skipped == 1 && pc5_is_dir(from),
              "cut and pasted where it is: nothing");
        pc5_files_end(&job);
        /* merging into b/data: its own file stays, the source goes */
        path(to, sizeof(to), "%s/data/only-in-a.txt", a);
        write_text(to, "a");
        path(from, sizeof(from), "%s/data", a);
        CHECK(!files_run(PC5_FILES_MOVE, sources, 1, b, PC5_FILES_REPLACE, &job) && job.state == PC5_FILES_DONE, "merged: %s", job.error);
        pc5_files_end(&job);
        path(to, sizeof(to), "%s/data/only-in-a.txt", b);
        CHECK(read_is(to, "a") && !pc5_exists(from), "a merged move moves the files and deletes the source");
        path(to, sizeof(to), "%s/data/level1.pak", b);
        CHECK(read_is(to, "one"), "and keeps the rest");
    }

    /* stop: the file being copied goes, a move deletes none of its sources */
    {
        char big[PC5_PATH], dest[PC5_PATH];
        path(big, sizeof(big), "%s/big.bin", a);
        path(dest, sizeof(dest), "%s/stop", base);
        pc5_mkdirs(dest);
        {
            uint8_t *bytes = pc5_alloc(64u << 20);
            pc5_write_file(big, bytes, 64u << 20);
            pc5_free(bytes);
        }
        const char *sources[] = { big };
        /* across drives is planned as copy then delete; on one drive the test uses Copy to see a stop part way */
        CHECK(!pc5_files_begin(&job, PC5_FILES_COPY, sources, 1, dest, PC5_FILES_REPLACE), "began");
        pc5_files_step(&job, 0);   /* at least one chunk */
        pc5_files_stop(&job);
        pc5_files_step(&job, 5);
        CHECK(job.state == PC5_FILES_STOPPED, "stopped (%d)", job.state);
        pc5_files_end(&job);
        path(file, sizeof(file), "%s/big.bin", dest);
        CHECK(!pc5_exists(file), "no half file under its name");
        path(file, sizeof(file), "%s/big.bin.pc5part", dest);
        CHECK(!pc5_exists(file), "and no .pc5part");
        CHECK(pc5_exists(big), "the source stays");
    }

    /* delete */
    {
        char one[PC5_PATH], two[PC5_PATH];
        path(one, sizeof(one), "%s/data", b);
        path(two, sizeof(two), "%s/readme.txt", b);
        const char *sources[] = { one, two };
        CHECK(!files_run(PC5_FILES_DELETE, sources, 2, NULL, PC5_FILES_REPLACE, &job) && job.state == PC5_FILES_DONE, "deleted: %s", job.error);
        pc5_files_end(&job);
        CHECK(!pc5_exists(one) && !pc5_exists(two), "both gone");
        path(file, sizeof(file), "%s/readme (2).txt", b);
        CHECK(read_is(file, "hello"), "and nothing else");
    }

    /* A step that runs out of time just as the last item is done still says Done: on the console a
     * folder on a USB drive is deleted slowly enough for that ("Not everything was deleted" with
     * nothing left). Each folder here takes a few milliseconds, the steps one. */
    {
        int ended = 1;
        for (int round = 0; round < 40; round++) {
            char folder[PC5_PATH], one[PC5_PATH];
            path(folder, sizeof(folder), "%s/slow-%d", base, round);
            for (int i = 0; i < 150; i++) {
                path(one, sizeof(one), "%s/f%03d.txt", folder, i);
                write_text(one, "x");
            }
            const char *sources[] = { folder };
            int steps = 0;
            if (pc5_files_begin(&job, PC5_FILES_DELETE, sources, 1, NULL, PC5_FILES_REPLACE)) ended = 0;
            while (pc5_files_step(&job, 1) && steps < 100000) steps++;
            ended &= job.state == PC5_FILES_DONE && !pc5_exists(folder);
            pc5_files_end(&job);
        }
        CHECK(ended, "every slow delete ended Done");
    }

    /* too little room is said before anything is copied */
    {
        char big[PC5_PATH];
        uint64_t room = 0;
        path(big, sizeof(big), "%s/big.bin", a);
        const char *sources[] = { big };
        if (!pc5_free_bytes(b, &room)) {
            Pc5FilesJob plan;
            pc5_files_begin(&plan, PC5_FILES_COPY, sources, 1, b, PC5_FILES_REPLACE);
            CHECK(plan.state == PC5_FILES_WORKING && plan.total_bytes == (64u << 20), "planned 64 MB");
            pc5_files_end(&plan);
        }
    }
}

/* The file manager's screens: Settings › File manager's places, a folder, ticking, Copy and
 * Paste (with a name taken: Keep both), Rename, New folder, Delete, Cut and Paste, a read-only
 * place, Stop; and a game's own folder from its menu. */
static void files_wait(Pc5Ui *ui, Pc5Canvas *canvas)
{
    frames(ui, 0, 0, 30, canvas);   /* past the dialogs' early-press guard */
}

static void until_not(Pc5Ui *ui, const char *name, Pc5Canvas *canvas)
{
    for (int i = 0; i < 20000 && !strcmp(pc5_ui_screen(ui), name); i++) frames(ui, 0, 0, 1, canvas);
}

static void test_files_screens(const char *fonts)
{
    FakeConsole console = { 0 };
    FakeNet net = { 0 };
    Pc5Platform platform = { &console, { &net, fake_get, NULL, fake_get_authorized }, fake_clock, fake_play, fake_log, fake_notify, fake_time, 0, NULL,
                             NULL, fake_keyboard_open_text, fake_keyboard_state_text, fake_keyboard_close_text };
    const char *roots[] = { usb, NULL };
    char usb0[PC5_PATH], ext0[PC5_PATH], file[PC5_PATH];
    path(usb0, sizeof(usb0), "%s/fm/usb0", work);
    path(ext0, sizeof(ext0), "%s/fm/ext0", work);
    path(file, sizeof(file), "rm -rf '%s/fm'", work);
    run(file);
    path(file, sizeof(file), "%s/Mods/a.txt", usb0); write_text(file, "mod");
    path(file, sizeof(file), "%s/readme.txt", usb0); write_text(file, "hello");
    pc5_mkdirs(ext0);
    const char *drives[] = { usb0, ext0, NULL };
    Pc5UiConfig config = { root, template_prefix, fonts, roots, NULL, NULL, NULL, drives, NULL, NULL, 0, 0 };
    Pc5Canvas canvas = { pc5_alloc(1920u * 1080u * 4u), 1920, 1080, 1920, 0, 0 };
    {
        /* asked a minute ago: no update question as it opens */
        Pc5UpdateMemory memory;
        memset(&memory, 0, sizeof(memory));
        memory.checked = fake_time(NULL, NULL) - 60;
        memory.last.state = PC5_UPDATE_CURRENT;
        pc5_update_save(root, &memory);
    }
    Pc5Ui *ui = pc5_ui_create(&config, &platform);
    until_screen(ui, "library", &canvas);
    for (int i = 0; i < 5 && strcmp(pc5_ui_screen(ui), "library"); i++) { press(ui, PC5_KEY_ACCEPT, &canvas); until_screen(ui, "library", &canvas); }
    CHECK(!strcmp(pc5_ui_screen(ui), "library"), "the library: %s", pc5_ui_screen(ui));

    press(ui, PC5_KEY_OPTIONS, &canvas);
    for (int i = 0; i < 3; i++) press(ui, PC5_KEY_DOWN, &canvas);
    pc5_ui_draw(ui, &canvas);
    save_shot(&canvas, "settings-file-manager");
    press(ui, PC5_KEY_ACCEPT, &canvas);      /* File manager, between Overlay and System */
    CHECK(!strcmp(pc5_ui_screen(ui), "files"), "Settings › File manager: %s", pc5_ui_screen(ui));
    pc5_ui_draw(ui, &canvas);
    save_shot(&canvas, "files-places");
    CHECK(!cuts_after(ui, 0, &canvas), "the places: no text cut short");

    /* USB drive 1: Mods and readme.txt, both ticked and copied */
    press(ui, PC5_KEY_ACCEPT, &canvas);
    press(ui, PC5_KEY_SQUARE, &canvas);
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_SQUARE, &canvas);
    pc5_ui_draw(ui, &canvas);
    save_shot(&canvas, "files-folder");
    press(ui, PC5_KEY_TRIANGLE, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "files-menu"), "Triangle: what can be done: %s", pc5_ui_screen(ui));
    pc5_ui_draw(ui, &canvas);
    save_shot(&canvas, "files-menu");
    press(ui, PC5_KEY_ACCEPT, &canvas);      /* Copy */
    CHECK(!strcmp(pc5_ui_screen(ui), "files"), "copied: %s", pc5_ui_screen(ui));
    press(ui, PC5_KEY_BACK, &canvas);        /* the places */
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);      /* Extended storage 1, empty */
    press(ui, PC5_KEY_TRIANGLE, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);      /* Paste, its first row */
    until_not(ui, "files-working", &canvas);
    path(file, sizeof(file), "%s/Mods/a.txt", ext0);
    CHECK(!strcmp(pc5_ui_screen(ui), "files") && read_is(file, "mod"), "pasted: %s", pc5_ui_screen(ui));
    path(file, sizeof(file), "%s/readme.txt", ext0);
    CHECK(read_is(file, "hello"), "both");

    /* Paste again: the names are taken; Keep both */
    press(ui, PC5_KEY_TRIANGLE, &canvas);
    for (int i = 0; i < 2; i++) press(ui, PC5_KEY_DOWN, &canvas);   /* Copy, Cut, Paste */
    press(ui, PC5_KEY_ACCEPT, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "files-conflict"), "the names are taken: %s", pc5_ui_screen(ui));
    pc5_ui_draw(ui, &canvas);
    save_shot(&canvas, "files-conflict");
    press(ui, PC5_KEY_RIGHT, &canvas);       /* Keep both */
    files_wait(ui, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);
    until_not(ui, "files-working", &canvas);
    path(file, sizeof(file), "%s/readme (2).txt", ext0);
    CHECK(read_is(file, "hello"), "Keep both: readme (2).txt");
    path(file, sizeof(file), "%s/Mods (2)/a.txt", ext0);
    CHECK(read_is(file, "mod"), "and Mods (2)");

    /* Rename readme.txt, the last row (Mods, Mods (2), readme (2).txt, readme.txt) */
    for (int i = 0; i < 6; i++) press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_TRIANGLE, &canvas);
    for (int i = 0; i < 3; i++) press(ui, PC5_KEY_DOWN, &canvas);   /* Copy, Cut, Paste, Rename */
    console.typed = "notes.txt";
    press(ui, PC5_KEY_ACCEPT, &canvas);
    frames(ui, 0, 0, 3, &canvas);
    path(file, sizeof(file), "%s/notes.txt", ext0);
    CHECK(!strcmp(console.keyboard_title, "Rename") && !strcmp(console.keyboard_start, "readme.txt") && read_is(file, "hello"),
          "renamed: %s %s", console.keyboard_title, console.keyboard_start);
    path(file, sizeof(file), "%s/readme.txt", ext0);
    CHECK(!pc5_exists(file) && !strcmp(pc5_ui_screen(ui), "files"), "the old name is gone: %s", pc5_ui_screen(ui));

    /* New folder, then a name exFAT refuses */
    press(ui, PC5_KEY_TRIANGLE, &canvas);
    for (int i = 0; i < 4; i++) press(ui, PC5_KEY_DOWN, &canvas);   /* ... Rename, New folder */
    console.typed = "Saves";
    press(ui, PC5_KEY_ACCEPT, &canvas);
    frames(ui, 0, 0, 3, &canvas);
    path(file, sizeof(file), "%s/Saves", ext0);
    CHECK(!strcmp(console.keyboard_title, "New folder") && pc5_is_dir(file), "a new folder: %s", console.keyboard_title);
    press(ui, PC5_KEY_TRIANGLE, &canvas);
    for (int i = 0; i < 4; i++) press(ui, PC5_KEY_DOWN, &canvas);
    console.typed = "a:b";
    press(ui, PC5_KEY_ACCEPT, &canvas);
    frames(ui, 0, 0, 3, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "message"), "a name with ':' refused: %s", pc5_ui_screen(ui));
    press(ui, PC5_KEY_ACCEPT, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "files"), "back to the folder: %s", pc5_ui_screen(ui));
    console.typed = NULL;

    /* Delete Mods (2): Cancel first, then Delete (Mods, Mods (2), Saves, notes.txt, readme (2).txt) */
    for (int i = 0; i < 10; i++) press(ui, PC5_KEY_UP, &canvas);
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_TRIANGLE, &canvas);
    for (int i = 0; i < 5; i++) press(ui, PC5_KEY_DOWN, &canvas);   /* ... New folder, Delete */
    press(ui, PC5_KEY_ACCEPT, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "files-delete"), "Delete asks: %s", pc5_ui_screen(ui));
    pc5_ui_draw(ui, &canvas);
    save_shot(&canvas, "files-delete");
    files_wait(ui, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);      /* Cancel */
    path(file, sizeof(file), "%s/Mods (2)", ext0);
    CHECK(!strcmp(pc5_ui_screen(ui), "files") && pc5_is_dir(file), "Cancel keeps it");
    press(ui, PC5_KEY_TRIANGLE, &canvas);
    for (int i = 0; i < 5; i++) press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);
    press(ui, PC5_KEY_RIGHT, &canvas);
    files_wait(ui, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);
    until_not(ui, "files-working", &canvas);
    CHECK(!pc5_exists(file) && !strcmp(pc5_ui_screen(ui), "files"), "deleted: %s", pc5_ui_screen(ui));

    /* Cut notes.txt (Mods, Saves, notes.txt, readme (2).txt) and paste it on USB drive 1 */
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_TRIANGLE, &canvas);
    press(ui, PC5_KEY_DOWN, &canvas);        /* Cut */
    press(ui, PC5_KEY_ACCEPT, &canvas);
    press(ui, PC5_KEY_BACK, &canvas);
    press(ui, PC5_KEY_UP, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);      /* USB drive 1 */
    press(ui, PC5_KEY_TRIANGLE, &canvas);
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_DOWN, &canvas);        /* Copy, Cut, Paste */
    press(ui, PC5_KEY_ACCEPT, &canvas);
    until_not(ui, "files-working", &canvas);
    path(file, sizeof(file), "%s/notes.txt", usb0);
    CHECK(read_is(file, "hello"), "moved here");
    path(file, sizeof(file), "%s/notes.txt", ext0);
    CHECK(!pc5_exists(file), "and gone from there");

    /* Stop, part way through a big copy: nothing half copied is left */
    {
        char big[PC5_PATH];
        path(big, sizeof(big), "%s/big.bin", usb0);
        uint8_t *bytes = pc5_alloc(256u << 20);
        if (bytes) pc5_write_file(big, bytes, 256u << 20);
        pc5_free(bytes);
        press(ui, PC5_KEY_BACK, &canvas);
        press(ui, PC5_KEY_ACCEPT, &canvas);  /* USB drive 1 again: big.bin is the first file */
        for (int i = 0; i < 10; i++) press(ui, PC5_KEY_UP, &canvas);
        press(ui, PC5_KEY_DOWN, &canvas);    /* Mods, big.bin */
        press(ui, PC5_KEY_TRIANGLE, &canvas);
        press(ui, PC5_KEY_ACCEPT, &canvas);  /* Copy */
        press(ui, PC5_KEY_BACK, &canvas);
        press(ui, PC5_KEY_DOWN, &canvas);
        press(ui, PC5_KEY_ACCEPT, &canvas);  /* Extended storage 1 */
        press(ui, PC5_KEY_TRIANGLE, &canvas);
        press(ui, PC5_KEY_DOWN, &canvas);
        press(ui, PC5_KEY_DOWN, &canvas);    /* Copy, Cut, Paste */
        frames(ui, 0, PC5_KEY_ACCEPT, 1, &canvas);
        CHECK(!strcmp(pc5_ui_screen(ui), "files-working"), "copying: %s", pc5_ui_screen(ui));
        frames(ui, 0, 0, 2, &canvas);
        pc5_ui_draw(ui, &canvas);
        save_shot(&canvas, "files-working");
        frames(ui, 0, PC5_KEY_BACK, 1, &canvas);
        until_not(ui, "files-working", &canvas);
        CHECK(!strcmp(pc5_ui_screen(ui), "message"), "Stopped: %s", pc5_ui_screen(ui));
        press(ui, PC5_KEY_ACCEPT, &canvas);
        path(file, sizeof(file), "%s/big.bin", ext0);
        CHECK(!pc5_exists(file), "no half file");
        path(file, sizeof(file), "%s/big.bin.pc5part", ext0);
        CHECK(!pc5_exists(file), "and no .pc5part");
        unlink(big);
    }

    /* The whole console is read-only: Triangle has Copy and Tick all only */
    press(ui, PC5_KEY_BACK, &canvas);
    for (int i = 0; i < 10; i++) press(ui, PC5_KEY_DOWN, &canvas);   /* Whole console, the last */
    press(ui, PC5_KEY_ACCEPT, &canvas);
    pc5_ui_draw(ui, &canvas);
    save_shot(&canvas, "files-read-only");
    int cuts = files_cut_lines;
    press(ui, PC5_KEY_TRIANGLE, &canvas);
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);      /* the second row: Tick all */
    CHECK(!strcmp(pc5_ui_screen(ui), "files") && files_cut_lines == cuts && cuts == 1,
          "read-only: the second row is Tick all, not Cut: %s %d %d", pc5_ui_screen(ui), cuts, files_cut_lines);
    for (int i = 0; i < 2; i++) press(ui, PC5_KEY_BACK, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "settings"), "Back to Settings: %s", pc5_ui_screen(ui));
    press(ui, PC5_KEY_BACK, &canvas);

    /* A game's own folder, from its menu: Manage › Game files; Back from its top leaves it */
    {
        Pc5Library library;
        int g = -1;
        pc5_library_load(root, &library);
        for (int i = 0; i < library.count && g < 0; i++)
            if (library.games[i].pc5) g = i;
        CHECK(g >= 0, "a game PC5 installed");
        if (g >= 0) {
            char folder[PC5_PATH];
            pc5_game_folder(root, library.games[g].id, folder, sizeof(folder));
            path(file, sizeof(file), "%s/mod.ini", folder);
            write_text(file, "x");
            for (int i = 0; i < 10; i++) press(ui, PC5_KEY_LEFT, &canvas);
            for (int i = 0; i < g; i++) press(ui, PC5_KEY_RIGHT, &canvas);
            press(ui, PC5_KEY_TRIANGLE, &canvas);
            for (int i = 0; i < 40; i++) press(ui, PC5_KEY_DOWN, &canvas);   /* Manage, the last row */
            press(ui, PC5_KEY_ACCEPT, &canvas);
            press(ui, PC5_KEY_DOWN, &canvas);    /* Game files, after Change program */
            press(ui, PC5_KEY_ACCEPT, &canvas);
            CHECK(!strcmp(pc5_ui_screen(ui), "files"), "Game files: %s", pc5_ui_screen(ui));
            pc5_ui_draw(ui, &canvas);
            save_shot(&canvas, "files-game");
            /* delete mod.ini: it is in the list */
            for (int i = 0; i < 40; i++) press(ui, PC5_KEY_DOWN, &canvas);
            press(ui, PC5_KEY_BACK, &canvas);
            CHECK(!strcmp(pc5_ui_screen(ui), "game-menu"), "Back from its folder: its menu: %s", pc5_ui_screen(ui));
            unlink(file);
        }
    }
    pc5_ui_destroy(ui);
    pc5_free(canvas.px);
}

/* PC5's INSTALL folder as PC5 opens: asked about, listed as the INSTALL folder's, moved in. */
static void test_install_folder_screens(const char *fonts)
{
    FakeConsole console = { 0 };
    FakeNet net = { 0 };
    Pc5Platform platform = { &console, { &net, fake_get, NULL, fake_get_authorized }, fake_clock, fake_play, fake_log, fake_notify, fake_time, 0, NULL,
                             NULL, fake_keyboard_open_text, fake_keyboard_state_text, fake_keyboard_close_text };
    const char *roots[] = { usb, NULL };
    char app[PC5_PATH], drive[PC5_PATH], folder[PC5_PATH], file[PC5_PATH], install_dir[PC5_PATH];
    Pc5Canvas canvas = { pc5_alloc(1920u * 1080u * 4u), 1920, 1080, 1920, 0, 0 };
    Pc5Library library;

    path(app, sizeof(app), "%s/app0", work);
    path(file, sizeof(file), "%s/eboot.bin", app);
    if (!pc5_exists(file)) write_text(file, "PC5 SCREENS EBOOT\n");
    path(drive, sizeof(drive), "%s/install-drive", work);
    path(folder, sizeof(folder), "%s/homebrew/PPSA11018", drive);
    make_installed(app, folder);
    path(install_dir, sizeof(install_dir), "%s/INSTALL", folder);
    path(file, sizeof(file), "mkdir -p '%s' && cp -r '%s/Starfall' '%s/'", install_dir, usb, install_dir);
    run(file);
    const char *drives[] = { drive, NULL };
    Pc5UiConfig config = { root, template_prefix, fonts, roots, NULL, NULL, NULL, drives, NULL, NULL, 0, 1 };
    pc5_library_load(root, &library);
    int before = library.count;
    Pc5Ui *ui = pc5_ui_create(&config, &platform);
    until_screen(ui, "install-ask", &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "install-ask"), "as PC5 opens: the INSTALL folder's game offered: %s", pc5_ui_screen(ui));
    pc5_ui_draw(ui, &canvas);
    save_shot(&canvas, "install-ask");
    frames(ui, 0, 0, 25, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);      /* Install, chosen first */
    until_screen(ui, "sources", &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "sources"), "its games listed: %s", pc5_ui_screen(ui));
    pc5_ui_draw(ui, &canvas);
    save_shot(&canvas, "install-folder-sources");
    press(ui, PC5_KEY_ACCEPT, &canvas);      /* Starfall */
    until_screen(ui, "review", &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);
    until_screen(ui, "message", &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "message"), "installed: %s", pc5_ui_screen(ui));
    press(ui, PC5_KEY_ACCEPT, &canvas);
    pc5_library_load(root, &library);
    path(file, sizeof(file), "%s/Starfall", install_dir);
    CHECK(library.count == before + 1 && !pc5_exists(file), "in the library, moved out of the INSTALL folder: %d then %d",
          before, library.count);
    pc5_ui_destroy(ui);
    /* Opened again with nothing in it: nothing asked. */
    ui = pc5_ui_create(&config, &platform);
    until_screen(ui, "library", &canvas);
    frames(ui, 0, 0, 10, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "library"), "an empty INSTALL folder: straight to the library: %s", pc5_ui_screen(ui));
    pc5_ui_destroy(ui);
    pc5_install_take_from(NULL);
    pc5_free(canvas.px);
}

int main(int argc, char **argv)
{
    char fonts[PC5_PATH];

    if (argc != 6) { fprintf(stderr, "usage: test_pc5 WORK FIXTURES PROGRAMS COVERS FONTS\n"); return 2; }
    work = argv[1];
    fixtures = argv[2];
    programs = argv[3];
    covers = argv[4];
    pc5_copy(fonts, sizeof(fonts), argv[5]);
    test_sha256();
    test_json();
    test_names();
    test_cover_search();
    test_icon_search();
    test_pe();
    make_usb();
    test_sources();
    test_install();
    test_install_folder();
    test_uninstall();
    test_game_update();
    test_keep_saves();
    test_usb_play();
    test_controls();
    test_settings();
    test_api_keys();
    test_update();
    test_sound();
    test_upgrade();
    test_upgrade_delta();
    test_backgrounds();
    test_zip();
    test_logs();
    test_cover_fill();
    test_prefix_media();
    test_home();
    test_tile();
    test_tile_make();
    test_dds();
    test_own_covers();
    test_prefix_system();
    test_prefix_dxvk();
    test_dotnet();
    test_keyboard();
    test_named_libraries();
    test_java();
    test_batch_bits();
    test_files();
    test_game(fonts);
    test_screens(fonts);
    test_tile_start(fonts);
    test_report(fonts);
    test_upgrade_screens(fonts);
    test_background_screen(fonts);
    test_files_screens(fonts);
    test_install_folder_screens(fonts);
    printf("test_pc5: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
