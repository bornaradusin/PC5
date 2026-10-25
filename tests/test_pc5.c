/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5's host tests: the title's code on real files, against prospero-win's own
 * profile parser and library loader.
 *
 *   test_pc5 WORK_DIR FIXTURES PROGRAMS COVERS
 *
 * PROGRAMS holds tests/make-test-programs.py's Windows programs, COVERS
 * tests/make-sample-covers.py's pictures. */
#include "pc5.h"
#include "pc5_cover.h"
#include "pc5_game.h"
#include "pc5_home.h"
#include "pc5_image.h"
#include "pc5_json.h"
#include "pc5_library.h"
#include "pc5_logs.h"
#include "pc5_pe.h"
#include "pc5_settings.h"
#include "pc5_source.h"
#include "pc5_ui.h"
#include "pc5_update.h"

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
    if (!strcmp(url, PC5_RELEASES_API)) {
        if (!net->release) { *status = 404; return pc5_buf_puts(out, "{\"message\":\"Not Found\"}"); }
        return pc5_buf_puts(out, net->release);
    }
    if (!strncmp(url, "https://store.steampowered.com/api/storesearch/?term=", 53)) {
        if (strstr(url, "Neon")) path(file, sizeof(file), "%s/steam-search.json", fixtures);
        else return pc5_buf_puts(out, "{\"total\":0,\"items\":[]}");
    } else if (!strncmp(url, "https://catalog.gog.com/v1/catalog?query=like:", 46)) {
        path(file, sizeof(file), "%s/gog-catalog.json", fixtures);
    } else if (!strncmp(url, "https://en.wikipedia.org/w/api.php?", 35)) {
        if (!strstr(url, "pilicense=any") || !strstr(url, "prop=pageimages")) { *status = 400; return 0; }
        path(file, sizeof(file), "%s/wikipedia.json", fixtures);
    } else if (!strncmp(url, "https://api.steampowered.com/IStoreBrowseService/GetItems/v1/?input_json=", 73)) {
        path(file, sizeof(file), "%s/steam-items.json", fixtures);
    } else if (!strcmp(url, "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/901001/0a1b2c3d/library_600x900_2x.jpg?t=1700000000")) {
        path(file, sizeof(file), "%s/cover-00.png", covers);
    } else if (!strcmp(url, "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/901002/library_600x900_2x.jpg")) {
        path(file, sizeof(file), "%s/cover-01.png", covers);
    } else if (strstr(url, "upload.wikimedia.org")) {
        path(file, sizeof(file), "%s/wide.png", covers);
    } else if (strstr(url, "images.gog-statics.com")) {
        path(file, sizeof(file), "%s/box.jpg", covers);
    } else {
        *status = 404;   /* e.g. the plain library picture of a newer app */
        return 0;
    }
    if (pc5_read_file(file, out, max)) { *status = 500; return 0; }
    return 0;
}

static void test_cover_search(void)
{
    FakeNet net = { 0 };
    Pc5Http http = { &net, fake_get };
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
    int has_wiki = 0, has_witcher = 0, has_soundtrack = 0;
    for (int i = 0; i < search.count; i++) {
        has_wiki |= !strcmp(search.choices[i].source, "Wikipedia") && strstr(search.choices[i].title, "Neon Drift") != NULL;
        has_witcher |= strstr(search.choices[i].title, "Witcher") != NULL;
        has_soundtrack |= strstr(search.choices[i].title, "Soundtrack") != NULL;
        CHECK(!strstr(search.choices[i].title, "motorsport"), "unrelated Wikipedia page kept");
    }
    CHECK(has_wiki, "Wikipedia's page");
    CHECK(!has_witcher, "an unrelated GOG game was kept");
    CHECK(!has_soundtrack, "an unrelated Steam app was kept");
    /* The plain library picture is missing (newer app): the asset list gives the real one. */
    CHECK(!pc5_cover_fetch(&search.choices[0], &http, &cover, NULL), "Steam cover through GetItems");
    CHECK(cover.w == PC5_COVER_WIDTH && cover.h == PC5_COVER_HEIGHT, "cover size %dx%d", cover.w, cover.h);
    CHECK(strstr(net.last, "0a1b2c3d/library_600x900_2x.jpg"), "fetched the hashed name: %s", net.last);
    pc5_image_free(&cover);

    /* A wide picture sits whole on a blurred copy of itself. */
    for (int i = 0; i < search.count; i++)
        if (!strcmp(search.choices[i].source, "Wikipedia")) {
            CHECK(!pc5_cover_fetch(&search.choices[i], &http, &cover, NULL), "Wikipedia picture");
            /* top rows are the darkened blur, the middle the picture */
            uint32_t top = cover.px[5 * cover.w + cover.w / 2];
            CHECK(((top >> 16) & 255) < 200 && ((top >> 8) & 255) < 200, "blurred and darkened above");
            pc5_image_free(&cover);
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
    CHECK(install.files_total == 7 && install.bytes_total > 0, "step 1's progress: %u files", (unsigned)install.files_total);
    pc5_buf_puts(&install.original, "\xff\xd8\xff picture");
    if (id) pc5_copy(id, id_size, install.id);
    do {
        phase = pc5_install_step(&install, 1);
        steps++;
        if (cancel_after_copy_steps && phase == PC5_INSTALL_COPY && --cancel_after_copy_steps == 0) pc5_install_cancel(&install);
        if (corrupt && phase == PC5_INSTALL_VERIFY) {
            char copy[PC5_PATH];
            path(copy, sizeof(copy), "%s/drive_c/Games/%s/data/level1.pak", install.staging, name);
            write_text(copy, "LEVEL ONE DATA\n");
            corrupt = 0;
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
        path(file, sizeof(file), "%s/neondrift/pc5/cover-original.jpg", prefixes);
        CHECK(pc5_is_file(file), "the downloaded picture kept");
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
        CHECK(profile && profile->display.width == 1920, "and 1080p again");
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
    /* Installing the same folder again makes a second game, never a second copy over the first. */
    CHECK(install_folder("NeonDrift", 0, 0, id, sizeof(id)) == PC5_INSTALL_DONE && !strcmp(id, "neondrift-2"), "second: %s", id);

    /* Cancelled: nothing stays. */
    CHECK(install_folder("NeonDrift", 3, 0, id, sizeof(id)) == PC5_INSTALL_CANCELLED, "cancel");
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
        Pc5Info info = { "Broken Game", "Broken", "Broken.exe", 64, PC5_API_D3D11, 5, 1, "" };
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

/* ---- the screens, driven like a pad ------------------------------------------------------------ */

typedef struct FakeConsole {
    char played[PC5_ID];
    int plays;
    char told[256];
    int notifications;
} FakeConsole;

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
    CHECK(!strcmp(pc5_control_name(0), "Cross") && !strcmp(pc5_control_name(24), "Right stick right") &&
              !strcmp(pc5_control_name(25), "") && !strcmp(pc5_pad_mode_name(PC5_PAD_DUALSENSE), "DualSense"),
          "names");
}

/* ---- settings, updates, logs, covers ---------------------------------------------------------- */

static void test_settings(void)
{
    char dir[PC5_PATH], file[PC5_PATH];
    Pc5Settings set, back;

    path(dir, sizeof(dir), "%s/settings-root", work);
    pc5_mkdirs(dir);
    CHECK(pc5_settings_load(dir, &set) == -1 && !set.fps && !set.frame_time && !set.cpu_usage && !set.cpu_temperature &&
          !set.soc_temperature && !set.memory && !pc5_game_overlay_lines(&set) && set.vibration == 100 &&
          set.check_updates && !set.keep_logs &&
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
    CHECK(!pc5_settings_save(dir, &set), "saved");
    CHECK(!pc5_settings_load(dir, &back) && !memcmp(&set, &back, sizeof(set)), "read back the same");
    path(file, sizeof(file), "%s/pc5/settings.txt", dir);
    write_text(file, "fps = maybe\nfps_corner = middle\nnonsense line\nvibration = off\nlight_bar = console\n"
                     "trigger_rumble = 30\ntrigger_resistance = 0\ntriggers = both\n");
    CHECK(!pc5_settings_load(dir, &back) && !back.fps && back.fps_corner == PC5_CORNER_TOP_RIGHT && !back.vibration &&
          back.light_bar == PC5_LIGHT_CONSOLE && back.trigger_rumble == 100 && back.trigger_resistance == 50 &&
          back.triggers == PC5_TRIGGERS_BOTH, "what it doesn't know gives the default");
    write_text(file, "vibration = on\n");
    CHECK(!pc5_settings_load(dir, &back) && back.vibration == 100, "an earlier PC5's vibration = on is 100 %%");
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
    Pc5Http http = { &net, fake_get };
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

static void log_file(const char *dir, const char *name, const char *profile, long long time, const char *body)
{
    char file[PC5_PATH], text[4096];
    snprintf(text, sizeof(text), "PW_REPORT/1 build=abc profile=%s cycle=2 pid=276 time=%lld\n%s", profile, time, body);
    path(file, sizeof(file), "%s/%s", dir, name);
    write_text(file, text);
}

static void test_logs(void)
{
    char root_dir[PC5_PATH], logs[PC5_PATH], file[PC5_PATH];
    const long long now = 1792700000;
    Pc5LogIssue issue;
    const char *crash =
        "REC seq=1 t=100.000 PW_WINE64 args argc=3 mode=game profile=gtaiv\n"
        "REC seq=2 t=100.400 WINE wine: Unhandled page fault on read access to 0000002C at address 7985D308 (thread 0024), starting debugger...\n"
        "REC seq=3 t=100.500 PW_WINE64 session_end reason=wine-exit\n";
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

    char text[4096];
#define CLASSIFY(profile, body) (snprintf(text, sizeof(text), "PW_REPORT/1 build=abc profile=%s cycle=2 pid=1 time=5\n%s", \
                                          profile, body), pc5_log_classify(text, strlen(text), &issue))
    CHECK(CLASSIFY("gtaiv", crash) == PC5_LOG_GAME_CRASHED, "a crash");
    CHECK(!strcmp(issue.profile, "gtaiv") && issue.time == 5 && strstr(issue.detail, "Unhandled page fault"),
          "its line: %s", issue.detail);
    CHECK(CLASSIFY("launcher", crash) == PC5_LOG_FINE, "PC5's own session isn't a game's");
    CHECK(CLASSIFY("blur", played) == PC5_LOG_FINE, "a game played");
    CHECK(CLASSIFY("quick", quick) == PC5_LOG_NO_PICTURE, "closed before any picture");
    CHECK(CLASSIFY("launcher", pc5_fine) == PC5_LOG_FINE, "PC5 starting a game");
    CHECK(CLASSIFY("quick", "REC seq=1 t=1.0 PW_WINE64 close requested by=combo\nREC seq=2 t=7.0 PW_WINE64 close timeout: "
                            "leaving the game\n") == PC5_LOG_FINE, "closed by the player, ended when it didn't close");
    CHECK(CLASSIFY("launcher", pc5_problem) == PC5_LOG_PC5_PROBLEM && !strcmp(issue.detail, "install Blur: Not enough space"),
          "PC5's problem: %s", issue.detail);
#undef CLASSIFY

    path(root_dir, sizeof(root_dir), "%s/logs-root", work);
    path(logs, sizeof(logs), "%s/logs", root_dir);
    pc5_mkdirs(logs);
    path(file, sizeof(file), "%s/next.txt", logs);
    write_text(file, "2\n");                                     /* session 1 is the one running */
    log_file(logs, "session-0.log", "gtaiv", now - 600, crash);
    /* every part of a session repeats its first line, the time it began among it */
    log_file(logs, "session-0.previous.log", "gtaiv", now - 600, "REC seq=1 t=1.0 earlier\n");
    log_file(logs, "session-0.start.log", "gtaiv", now - 600, "REC seq=1 t=1.0 how it began\n");
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
    CHECK(!pc5_exists(file), "a game that went well: deleted");
    path(file, sizeof(file), "%s/session-7.start.log", logs);
    CHECK(!pc5_exists(file), "with how it began");
    path(file, sizeof(file), "%s/session-2.log", logs);
    CHECK(!pc5_exists(file), "a long session read from how it began: went well, deleted");
    path(file, sizeof(file), "%s/session-6.log", logs);
    CHECK(!pc5_exists(file), "PC5 that went well: deleted");
    path(file, sizeof(file), "%s/issue-2026-10-01_10-00_old.log", logs);
    CHECK(!pc5_exists(file), "older than a week: deleted");
    path(file, sizeof(file), "%s/pc5/crash-3.txt", root_dir);
    CHECK(!pc5_exists(file), "the crash marker is used once");
    CHECK(count_entries(logs, "issue-") == 6 && count_entries(logs, "session-") == 1, "kept under their names: %d",
          count_entries(logs, "issue-"));   /* four, and the crash's earlier part and start */
    CHECK(issue.problem == PC5_LOG_CRASHED && strstr(issue.file, "/logs/issue-") && strstr(issue.file, "_launcher.log"),
          "the newest: %d %s", (int)issue.problem, issue.file);
    {
        const char *name = "_gtaiv.log";
        DIR *d = opendir(logs);
        struct dirent *e;
        int found = 0;
        while (d && (e = readdir(d))) found |= strstr(e->d_name, "issue-") && strstr(e->d_name, name) && !strstr(e->d_name, "earlier");
        if (d) closedir(d);
        CHECK(found, "the crash kept with the game's name");
        path(file, sizeof(file), "%s/issue-2026-10-22_21-03_gtaiv.log", logs);   /* local time: UTC+1 */
        CHECK(pc5_is_file(file), "named by when it began");
        path(file, sizeof(file), "%s/issue-2026-10-22_21-03_gtaiv-earlier.log", logs);
        CHECK(pc5_is_file(file), "with its earlier part");
        path(file, sizeof(file), "%s/issue-2026-10-22_21-03_gtaiv-start.log", logs);
        CHECK(pc5_is_file(file), "and how it began");
    }
    /* Keep all: one that went well is kept too, as log-... */
    path(file, sizeof(file), "%s/next.txt", logs);
    write_text(file, "3\n");
    log_file(logs, "session-0.log", "blur", now - 100, played);
    CHECK(pc5_logs_tidy(root_dir, 1, now, 0, NULL) == 0 && count_entries(logs, "log-") == 2, "keep all: %d",
          count_entries(logs, "log-"));   /* the game, and PC5's session before it */
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
    CHECK(step == -1 && strstr(runtime.error, "can't copy"), "a missing file fails: %s", runtime.error);
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

/* The player's own covers: a cover.jpg/.png at the top of a game's folder,
 * offered before anything online; one in a folder of pictures by name. */
static void test_own_covers(void)
{
    char dir[PC5_PATH], file[PC5_PATH], found[PC5_PATH];
    Pc5Folder *folder = pc5_alloc(sizeof(*folder));
    Pc5CoverHint hint = { 0 };
    Pc5CoverSearch *search = pc5_alloc(sizeof(*search));
    Pc5Image cover = { 0 };
    Pc5Buf original = { 0 };

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
    CHECK(!pc5_cover_fetch(&search->choices[0], NULL, &cover, &original) && cover.w == PC5_COVER_WIDTH &&
          original.len > 8 && original.data[1] == 'P', "read from its file and made a cover");
    if (cover.px) CHECK(cover.px[cover.w * cover.h / 2 + cover.w / 2] == 0xff33aa55u, "its colour: %08x",
                        cover.px[cover.w * cover.h / 2 + cover.w / 2]);
    pc5_image_free(&cover);
    pc5_buf_free(&original);
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
                             .cursor = fake_cursor, .cpu_time_us = fake_cpu_time_us, .cpu_count = fake_cpu_count,
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
        /* A 32-bit game gets 4 GB unless its Memory is 2 GB, which Wine reads (patch 0921). */
        CHECK(!getenv("PW_LARGE_ADDRESS_AWARE"), "4 GB by default");
        controls.memory_2gb = 1;
        CHECK(!pc5_controls_save(game_root, "liberty", &controls) && !pc5_controls_load(game_root, "liberty", &again) &&
              again.memory_2gb == 1, "Memory is kept with the game");
        pc5_game_begin(&game, &ops, game_root, app, "liberty", "Liberty Town");
        env = getenv("PW_LARGE_ADDRESS_AWARE");
        CHECK(env && !strcmp(env, "0"), "a game set to 2 GB tells Wine");
        controls.memory_2gb = 0;
        pc5_controls_save(game_root, "liberty", &controls);
        pc5_game_begin(&game, &ops, game_root, app, "liberty", "Liberty Town");
        CHECK(!getenv("PW_LARGE_ADDRESS_AWARE"), "and 4 GB again");
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
    /* Reset to default, the last row. */
    for (int i = 0; i < 40; i++) press(ui, PC5_KEY_DOWN, canvas);
    press(ui, PC5_KEY_ACCEPT, canvas);
    CHECK(controls_has("orbital-nine", "mode = keyboard\n") && controls_has("orbital-nine", "left_stick = keys\n") &&
              controls_has("orbital-nine", "right_stick = pointer\n") && controls_has("orbital-nine", "left_stick_up = w\n") &&
              controls_has("orbital-nine", "pointer_speed = 4\n"),
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
    Pc5Platform platform = { &console, { &net, fake_get }, fake_clock, fake_play, NULL, fake_notify, fake_time, 0 };
    const char *roots[] = { usb, NULL };
    Pc5Canvas canvas = { pc5_alloc(1920u * 1080u * 4u), 1920, 1080, 1920 };
    Pc5Library library;
    char file[PC5_PATH], tiles[PC5_PATH], tile_template[PC5_PATH];

    path(tiles, sizeof(tiles), "%s/screens-homebrew", work);
    make_tile_template(tile_template, sizeof(tile_template));
    pc5_library_load(root, &library);
    CHECK(library.count > 0, "games to start");
    if (!library.count) return;
    {
        Pc5UiConfig config = { root, template_prefix, fonts, roots, tiles, tile_template, library.games[0].id };
        Pc5Ui *ui = pc5_ui_create(&config, &platform);
        for (int i = 0; i < 2000 && !console.plays; i++) frames(ui, 0, 0, 1, &canvas);
        CHECK(console.plays == 1 && !strcmp(console.played, library.games[0].id), "the tile's game started: %d %s",
              console.plays, console.played);
        pc5_ui_destroy(ui);
    }
    {
        Pc5UiConfig config = { root, template_prefix, fonts, roots, tiles, tile_template, "not-installed" };
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
        path(file, sizeof(file), "%s/covers/%s.png", root, library.games[0].title);
        write_png(file, 400, 600, 0xff8844ccu);
        /* newer than the cover it replaces */
        gettimeofday(&later[0], NULL);
        later[0].tv_sec += 60;
        later[1] = later[0];
        utimes(file, later);
        Pc5UiConfig config = { root, template_prefix, fonts, roots, tiles, tile_template, NULL };
        Pc5Ui *ui = pc5_ui_create(&config, &platform);
        until_screen(ui, "library", &canvas);
        path(saved, sizeof(saved), "%s/prefixes/%s/pc5/cover.bin", root, library.games[0].prefix);
        CHECK(!pc5_cover_load(saved, &cover, &tint) && cover.px &&
              cover.px[cover.w * cover.h / 2 + cover.w / 2] == 0xff8844ccu, "the player's picture is its cover");
        pc5_image_free(&cover);
        path(saved, sizeof(saved), "%s/prefixes/%s/pc5/cover-original.png", root, library.games[0].prefix);
        CHECK(pc5_is_file(saved), "kept as it was");
        pc5_ui_destroy(ui);
        /* once: the next start leaves it */
        ui = pc5_ui_create(&config, &platform);
        until_screen(ui, "library", &canvas);
        pc5_ui_destroy(ui);
    }
    pc5_free(canvas.px);
}

static void test_screens(const char *fonts)
{
    FakeConsole console = { 0 };
    FakeNet net = { 0 };
    Pc5Platform platform = { &console, { &net, fake_get }, fake_clock, fake_play, NULL, fake_notify, fake_time, 0 };
    const char *roots[] = { usb, NULL };
    char tiles[PC5_PATH], tile_template[PC5_PATH], tile[PC5_HOME_ID];
    path(tiles, sizeof(tiles), "%s/screens-homebrew", work);
    make_tile_template(tile_template, sizeof(tile_template));
    Pc5UiConfig config = { root, template_prefix, fonts, roots, tiles, tile_template, NULL };
    Pc5Canvas canvas = { pc5_alloc(1920u * 1080u * 4u), 1920, 1080, 1920 };
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
    press(ui, PC5_KEY_ACCEPT, &canvas);
    until_screen(ui, "sources", &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "sources"), "usb list: %s", pc5_ui_screen(ui));
    press(ui, PC5_KEY_ACCEPT, &canvas);   /* NeonDrift */
    until_screen(ui, "review", &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "review"), "review: %s", pc5_ui_screen(ui));
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

    /* Uninstall: Cancel is chosen first, and a press that came too soon does nothing. */
    press(ui, PC5_KEY_OPTIONS, &canvas);
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
    press(ui, PC5_KEY_ACCEPT, &canvas);
    frames(ui, 0, 0, 30, &canvas);
    press(ui, PC5_KEY_RIGHT, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);
    until_screen(ui, "message", &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);
    pc5_library_load(root, &library);
    CHECK(library.count == 2, "one uninstalled: %d", library.count);
    CHECK(pc5_library_find(&library, gone) < 0 && !pc5_home_find(tiles, gone, tile), "%s and its tile gone", gone);

    /* Settings: In games, a value changed with Cross and with right; saved. */
    press(ui, PC5_KEY_OPTIONS, &canvas);
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_ACCEPT, &canvas);      /* In games */
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_LEFT, &canvas);        /* light bar: game -> console */
    press(ui, PC5_KEY_UP, &canvas);
    press(ui, PC5_KEY_LEFT, &canvas);        /* vibration: 100 % -> 75 % */
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_DOWN, &canvas);        /* Adaptive triggers, the last row while they are off */
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_LEFT, &canvas);        /* still on it: off -> rumble + resistance */
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_LEFT, &canvas);        /* trigger rumble: 100 % -> 75 % */
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_RIGHT, &canvas);       /* trigger resistance: 50 % -> 75 % */
    {
        Pc5Settings set;
        pc5_settings_load(root, &set);
        CHECK(!set.fps && set.light_bar == PC5_LIGHT_CONSOLE && set.vibration == 75 && set.triggers == PC5_TRIGGERS_BOTH &&
              set.trigger_rumble == 75 && set.trigger_resistance == 75,
              "settings saved: fps=%d light=%d vibration=%d triggers=%d rumble=%d resistance=%d", set.fps,
              (int)set.light_bar, set.vibration, (int)set.triggers, set.trigger_rumble, set.trigger_resistance);
    }
    /* Off again, then rumble: only its own strength shows. */
    press(ui, PC5_KEY_UP, &canvas);
    press(ui, PC5_KEY_UP, &canvas);
    press(ui, PC5_KEY_RIGHT, &canvas);       /* both -> off */
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_RIGHT, &canvas);       /* still on it: off -> rumble */
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_DOWN, &canvas);
    press(ui, PC5_KEY_RIGHT, &canvas);       /* the last row is the rumble's: 75 % -> 100 % */
    {
        Pc5Settings set;
        pc5_settings_load(root, &set);
        CHECK(set.triggers == PC5_TRIGGERS_RUMBLE && set.trigger_rumble == 100 && set.trigger_resistance == 75,
              "a strength row only while it is used: triggers=%d rumble=%d resistance=%d", (int)set.triggers,
              set.trigger_rumble, set.trigger_resistance);
    }
    /* Overlay: its lines, each on its own, and the corner. */
    press(ui, PC5_KEY_BACK, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "settings"), "back to the first page");
    press(ui, PC5_KEY_DOWN, &canvas);        /* Overlay */
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
    /* Updates: Check now finds a newer PC5 and says so once, with the console's notification. */
    press(ui, PC5_KEY_BACK, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "settings"), "back to the first page, on Overlay");
    press(ui, PC5_KEY_DOWN, &canvas);        /* Updates */
    press(ui, PC5_KEY_ACCEPT, &canvas);
    press(ui, PC5_KEY_DOWN, &canvas);        /* Check now */
    /* a release newer than any this test runs on */
    net.release = "{\"tag_name\":\"v99.0.0\",\"html_url\":\"https://github.com/bornaradusin/PC5/releases/tag/v99.0.0\"}";
    press(ui, PC5_KEY_ACCEPT, &canvas);
    until_screen(ui, "settings", &canvas);
    CHECK(console.notifications == 1 && strstr(console.told, "PC5 99.0.0 is available"), "told: %s", console.told);
    press(ui, PC5_KEY_ACCEPT, &canvas);
    until_screen(ui, "settings", &canvas);
    CHECK(console.notifications == 1, "once per release");
    press(ui, PC5_KEY_BACK, &canvas);
    press(ui, PC5_KEY_BACK, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "library"), "settings closed: %s", pc5_ui_screen(ui));

    /* A game's menu: Change program, for the engine game's other program. */
    pc5_library_load(root, &library);
    int orbital = pc5_library_find(&library, "orbital-nine");
    CHECK(orbital >= 0, "the engine game is there");
    for (int i = 0; i < 10; i++) press(ui, PC5_KEY_LEFT, &canvas);
    for (int i = 0; i < orbital; i++) press(ui, PC5_KEY_RIGHT, &canvas);
    press(ui, PC5_KEY_TRIANGLE, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "game-menu"), "game menu: %s", pc5_ui_screen(ui));
    press(ui, PC5_KEY_DOWN, &canvas);       /* past Controller (the Xbox controller: no Button mapping) */
    press(ui, PC5_KEY_DOWN, &canvas);       /* Frame generation: on, then off again with right */
    press(ui, PC5_KEY_ACCEPT, &canvas);
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
    press(ui, PC5_KEY_DOWN, &canvas);       /* Change program */
    press(ui, PC5_KEY_ACCEPT, &canvas);
    until_screen(ui, "programs", &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "programs"), "its programs: %s", pc5_ui_screen(ui));
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
    for (int i = 0; i < 5; i++) press(ui, PC5_KEY_DOWN, &canvas);   /* Play, Controller, Frame generation, Resolution,
                                                                       Change program */
    press(ui, PC5_KEY_ACCEPT, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "message") && pc5_home_find(tiles, "orbital-nine", tile), "tile made: %s %s",
          pc5_ui_screen(ui), tile);
    press(ui, PC5_KEY_ACCEPT, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "game-menu"), "back to its menu: %s", pc5_ui_screen(ui));
    press(ui, PC5_KEY_ACCEPT, &canvas);
    CHECK(!strcmp(pc5_ui_screen(ui), "message") && !pc5_home_find(tiles, "orbital-nine", tile), "taken away");
    press(ui, PC5_KEY_ACCEPT, &canvas);
    press(ui, PC5_KEY_BACK, &canvas);

    /* Several games in a row: two ticked folders, read, then installed one after the other. */
    pc5_library_load(root, &library);
    int before = library.count;
    press(ui, PC5_KEY_OPTIONS, &canvas);
    press(ui, PC5_KEY_UP, &canvas);
    press(ui, PC5_KEY_UP, &canvas);
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
    test_pe();
    make_usb();
    test_sources();
    test_install();
    test_uninstall();
    test_controls();
    test_settings();
    test_update();
    test_logs();
    test_cover_fill();
    test_prefix_media();
    test_home();
    test_own_covers();
    test_prefix_dxvk();
    test_dotnet();
    test_game(fonts);
    test_screens(fonts);
    test_tile_start(fonts);
    printf("test_pc5: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
