/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Screenshots of PC5's screens, drawn by its own code on the host, with
 * made-up games (tests/make-sample-covers.py) and no network.
 *
 *   shots OUT_DIR WORK_DIR COVERS PROGRAMS FONTS
 *
 * Writes OUT_DIR/<name>.ppm for each screen. */
#include "pc5.h"
#include "pc5_image.h"
#include "pc5_library.h"
#include "pc5_ui.h"
#include "pc5_game.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *out_dir, *work, *covers, *programs, *fonts;
static char titles[64][PC5_NAME];
static int title_count;
static uint64_t now = 5000;
static Pc5Canvas canvas;

static void fmt(char *out, size_t size, const char *format, ...) __attribute__((format(printf, 3, 4)));
static void fmt(char *out, size_t size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(out, size, format, args);
    va_end(args);
}

static void put(const char *file, const char *text)
{
    char dir[PC5_PATH];
    pc5_copy(dir, sizeof(dir), file);
    *strrchr(dir, '/') = 0;
    pc5_mkdirs(dir);
    pc5_write_file(file, text, strlen(text));
}

static void save(const char *name)
{
    char file[PC5_PATH];
    FILE *f;
    fmt(file, sizeof(file), "%s/%s.ppm", out_dir, name);
    if (!(f = fopen(file, "wb"))) exit(2);
    fprintf(f, "P6\n%d %d\n255\n", canvas.w, canvas.h);
    for (int i = 0; i < canvas.w * canvas.h; i++) {
        uint32_t p = canvas.px[i];
        unsigned char rgb[3] = { (unsigned char)(p >> 16), (unsigned char)(p >> 8), (unsigned char)p };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("shots: %s\n", name);
}

/* A library of count made-up games, as PC5 would have installed them. */
static void make_library(const char *root, int count, int with_covers)
{
    char command[PC5_PATH * 2];
    fmt(command, sizeof(command), "rm -rf '%s'", root);
    if (system(command)) exit(2);
    for (int i = 0; i < count; i++) {
        static const int bits[] = { 64, 64, 32, 64, 32 };
        static const Pc5Api apis[] = { PC5_API_D3D11, PC5_API_D3D12, PC5_API_D3D9, PC5_API_VULKAN, PC5_API_OPENGL };
        Pc5Info info = { 0 };
        char id[PC5_ID], file[PC5_PATH], text[4096];
        int n = 0;
        pc5_copy(info.title, sizeof(info.title), titles[i % title_count]);
        pc5_copy(info.folder, sizeof(info.folder), info.title);
        fmt(info.exe, sizeof(info.exe), "Game.exe");
        info.bits = bits[i % 5];
        info.api = apis[i % 5];
        info.bytes = (uint64_t)(3 + (i * 7) % 60) * 1024 * 1024 * 1024 + (uint64_t)i * 77777777u;
        info.files = 1000 + (uint32_t)i * 37;
        for (const char *p = info.title; *p; p++)
            if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) id[n++] = (char)pc5_lower(*p);
        fmt(id + n, sizeof(id) - (size_t)n, "%d", i);
        fmt(file, sizeof(file), "%s/prefixes/%s/system.reg", root, id);
        put(file, "x");
        pc5_info_text(&info, text, sizeof(text));
        fmt(file, sizeof(file), "%s/prefixes/%s/pc5/info.txt", root, id);
        put(file, text);
        pc5_profile_text(id, &info, "", NULL, NULL, text, sizeof(text));
        fmt(file, sizeof(file), "%s/profiles/%s.profile", root, id);
        put(file, text);
        if (with_covers) {
            Pc5Buf png = { 0 };
            Pc5Image picture, cover;
            fmt(file, sizeof(file), "%s/cover-%02d.png", covers, i % title_count);
            pc5_read_file(file, &png, 1 << 24);
            pc5_image_decode(png.data, png.len, &picture);
            pc5_cover_compose(&picture, &cover);
            fmt(file, sizeof(file), "%s/prefixes/%s/pc5/cover.bin", root, id);
            pc5_cover_save(file, &cover, pc5_image_tint(&cover));
            pc5_image_free(&picture);
            pc5_image_free(&cover);
            pc5_buf_free(&png);
        }
    }
}

static void fake_clock(void *context, char *out, size_t size) { (void)context; pc5_copy(out, size, "14:32"); }
static void fake_play(void *context, const char *id) { (void)context; (void)id; }
static int offline(void *context, const char *url, Pc5Buf *out, size_t max, int *status)
{
    (void)context; (void)url; (void)out; (void)max; (void)status;
    return -1;
}

/* Answers like the cover sources, from the sample pictures. */
static int sample_net(void *context, const char *url, Pc5Buf *out, size_t max, int *status)
{
    char file[PC5_PATH];
    (void)context;
    *status = 200;
    out->len = 0;
    if (strstr(url, "storesearch"))
        return pc5_buf_puts(out, "{\"total\":1,\"items\":[{\"type\":\"app\",\"name\":\"Neon Drift\",\"id\":901001}]}");
    if (strstr(url, "catalog.gog.com"))
        return pc5_buf_puts(out, "{\"products\":[{\"title\":\"Neon Drift\",\"coverVertical\":\"https://images.gog-statics.com/x.jpg\"}]}");
    if (strstr(url, "wikipedia.org"))
        return pc5_buf_puts(out, "{\"query\":{\"pages\":[]}}");
    if (strstr(url, "api.github.com"))
        return pc5_buf_puts(out, "{\"tag_name\":\"v1.1.0\",\"name\":\"PC5 1.1.0\","
                                 "\"html_url\":\"https://github.com/bornaradusin/PC5/releases/tag/v1.1.0\"}");
    if (strstr(url, "steam/apps/901001/library_600x900_2x.jpg")) fmt(file, sizeof(file), "%s/cover-00.png", covers);
    else if (strstr(url, "gog-statics")) fmt(file, sizeof(file), "%s/box.jpg", covers);
    else { *status = 404; return 0; }
    return pc5_read_file(file, out, max) ? -1 : 0;
}

static void step(Pc5Ui *ui, uint32_t pressed, int count)
{
    for (int i = 0; i < count; i++) {
        Pc5Input input = { pressed, i == 0 ? pressed : 0 };
        now += 16;
        pc5_ui_update(ui, &input, now);
        pc5_ui_draw(ui, &canvas);
    }
}

static void settle(Pc5Ui *ui) { step(ui, 0, 90); }

static void until(Pc5Ui *ui, const char *screen)
{
    for (int i = 0; i < 50000 && strcmp(pc5_ui_screen(ui), screen); i++) step(ui, 0, 1);
}

static Pc5Ui *open_ui(const char *root, int network)
{
    static const char *roots[2];
    static char usb[PC5_PATH], template_prefix[PC5_PATH], tiles[PC5_PATH], tile_template[PC5_PATH];
    static Pc5Platform platform;
    char file[PC5_PATH];
    Pc5UiConfig config;

    fmt(usb, sizeof(usb), "%s/usb0", work);
    roots[0] = usb;
    fmt(template_prefix, sizeof(template_prefix), "%s/app0/pc5/prefix", work);
    /* The home screen tiles' folder, and the tile's program as the app has it */
    fmt(tiles, sizeof(tiles), "%s/data/homebrew", work);
    fmt(tile_template, sizeof(tile_template), "%s/app0/pc5/forwarder/sce_module", work);
    pc5_mkdirs(tile_template);
    fmt(file, sizeof(file), "%s/libc.prx", tile_template);
    pc5_write_file(file, "libc", 4);
    fmt(tile_template, sizeof(tile_template), "%s/app0/pc5/forwarder", work);
    fmt(file, sizeof(file), "%s/eboot.bin", tile_template);
    pc5_write_file(file, "eboot", 5);
    platform = (Pc5Platform){ NULL, { NULL, network ? sample_net : offline }, fake_clock, fake_play, NULL, NULL, NULL, 0 };
    config = (Pc5UiConfig){ root, template_prefix, fonts, roots, tiles, tile_template, NULL };
    Pc5Ui *ui = pc5_ui_create(&config, &platform);
    until(ui, "library");
    settle(ui);
    return ui;
}

int main(int argc, char **argv)
{
    char root[PC5_PATH], file[PC5_PATH], from[PC5_PATH];
    Pc5Buf names = { 0 };
    Pc5Ui *ui;

    if (argc != 6) { fprintf(stderr, "usage: shots OUT WORK COVERS PROGRAMS FONTS\n"); return 2; }
    out_dir = argv[1]; work = argv[2]; covers = argv[3]; programs = argv[4]; fonts = argv[5];
    canvas = (Pc5Canvas){ pc5_alloc(1920u * 1080u * 4u), 1920, 1080, 1920 };
    fmt(file, sizeof(file), "%s/titles.txt", covers);
    pc5_read_file(file, &names, 65536);
    for (char *line = (char *)names.data, *next; line && *line && title_count < 64; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        pc5_copy(titles[title_count++], PC5_NAME, line);
    }
    fmt(root, sizeof(root), "%s/data/prospero-win", work);

    make_library(root, 0, 1);
    ui = open_ui(root, 0);
    save("01-library-empty");
    pc5_ui_destroy(ui);

    make_library(root, 1, 1);
    ui = open_ui(root, 0);
    save("02-library-1-game");
    pc5_ui_destroy(ui);

    make_library(root, 2, 1);
    ui = open_ui(root, 0);
    step(ui, PC5_KEY_RIGHT, 1);
    settle(ui);
    save("03-library-2-games");
    pc5_ui_destroy(ui);

    make_library(root, 12, 1);
    ui = open_ui(root, 0);
    for (int i = 0; i < 5; i++) { step(ui, PC5_KEY_RIGHT, 1); step(ui, 0, 4); }
    settle(ui);
    save("04-library-12-games");
    /* half way between two games, as it slides */
    step(ui, PC5_KEY_RIGHT, 1);
    step(ui, 0, 3);
    save("05-library-sliding");
    settle(ui);
    pc5_ui_destroy(ui);

    make_library(root, 40, 1);
    ui = open_ui(root, 1);   /* online: GitHub has a newer PC5 for the Updates page */
    for (int i = 0; i < 20; i++) { step(ui, PC5_KEY_RIGHT, 1); step(ui, 0, 3); }
    settle(ui);
    save("06-library-40-games");
    step(ui, PC5_KEY_TRIANGLE, 1);
    settle(ui);
    save("15-game-menu");
    /* Its controller: Keyboard & mouse, its buttons, and the key picker. */
    step(ui, PC5_KEY_DOWN, 1);
    step(ui, PC5_KEY_LEFT, 1);
    settle(ui);
    save("15b-game-menu-keyboard");
    step(ui, PC5_KEY_DOWN, 1);
    step(ui, PC5_KEY_ACCEPT, 1);
    for (int i = 0; i < 7; i++) step(ui, PC5_KEY_DOWN, 1);   /* Cross */
    settle(ui);
    save("15c-button-mapping");
    step(ui, PC5_KEY_ACCEPT, 1);
    settle(ui);
    save("15d-key-picker");
    step(ui, PC5_KEY_BACK, 1);
    step(ui, PC5_KEY_BACK, 1);
    step(ui, PC5_KEY_UP, 1);
    step(ui, PC5_KEY_RIGHT, 1);            /* back to the Xbox controller */
    step(ui, PC5_KEY_DOWN, 1);
    step(ui, PC5_KEY_ACCEPT, 1);           /* Frame generation on */
    settle(ui);
    save("15e-game-menu-frame-generation");
    step(ui, PC5_KEY_ACCEPT, 1);           /* and off again */
    step(ui, PC5_KEY_DOWN, 1);
    step(ui, PC5_KEY_DOWN, 1);
    step(ui, PC5_KEY_ACCEPT, 1);           /* Home screen tile on */
    settle(ui);
    save("15f-home-tile-added");
    step(ui, PC5_KEY_ACCEPT, 1);
    settle(ui);
    save("15g-game-menu-home-tile");
    step(ui, PC5_KEY_ACCEPT, 1);           /* and off again */
    settle(ui);
    save("15h-home-tile-removed");
    step(ui, PC5_KEY_ACCEPT, 1);
    step(ui, PC5_KEY_BACK, 1);
    step(ui, PC5_KEY_OPTIONS, 1);
    settle(ui);
    save("07-settings");
    step(ui, PC5_KEY_DOWN, 1);
    step(ui, PC5_KEY_DOWN, 1);
    step(ui, PC5_KEY_ACCEPT, 1);           /* In games */
    step(ui, PC5_KEY_DOWN, 1);
    step(ui, PC5_KEY_DOWN, 1);
    settle(ui);
    save("07b-settings-in-games");
    step(ui, PC5_KEY_DOWN, 1);
    step(ui, PC5_KEY_LEFT, 1);             /* triggers: rumble + resistance */
    step(ui, PC5_KEY_DOWN, 1);
    step(ui, PC5_KEY_LEFT, 1);             /* trigger rumble: 75 % */
    settle(ui);
    save("07d-settings-trigger-strengths");
    step(ui, PC5_KEY_BACK, 1);
    step(ui, PC5_KEY_DOWN, 1);             /* Overlay */
    step(ui, PC5_KEY_ACCEPT, 1);
    step(ui, PC5_KEY_ACCEPT, 1);           /* Show FPS on */
    for (int i = 0; i < 5; i++) {          /* and every line after it */
        step(ui, PC5_KEY_DOWN, 1);
        step(ui, PC5_KEY_ACCEPT, 1);
    }
    settle(ui);
    save("07e-settings-overlay");
    step(ui, PC5_KEY_BACK, 1);
    step(ui, PC5_KEY_DOWN, 1);             /* Updates */
    step(ui, PC5_KEY_ACCEPT, 1);
    settle(ui);
    save("07c-settings-updates");
    step(ui, PC5_KEY_BACK, 1);
    step(ui, PC5_KEY_DOWN, 1);
    step(ui, PC5_KEY_DOWN, 1);             /* About */
    step(ui, PC5_KEY_ACCEPT, 1);
    settle(ui);
    save("08-about");
    step(ui, PC5_KEY_BACK, 1);
    for (int i = 0; i < 5; i++) step(ui, PC5_KEY_UP, 1);
    step(ui, PC5_KEY_ACCEPT, 1);           /* Uninstall game */
    settle(ui);
    for (int i = 0; i < 3; i++) { step(ui, PC5_KEY_DOWN, 1); step(ui, 0, 2); }
    settle(ui);
    save("13-uninstall-list");
    step(ui, PC5_KEY_ACCEPT, 1);
    settle(ui);
    save("14-uninstall-confirm");
    pc5_ui_destroy(ui);

    /* Installing: a USB drive with a game folder (a real program and 600 MB of data). */
    make_library(root, 6, 1);
    {
        static char chunk[1 << 20];
        fmt(file, sizeof(file), "%s/usb0/Neon Drift/NeonDrift.exe", work);
        fmt(from, sizeof(from), "%s/game64.exe", programs);
        {
            Pc5Buf exe = { 0 };
            pc5_read_file(from, &exe, 1 << 24);
            char dir[PC5_PATH];
            pc5_copy(dir, sizeof(dir), file);
            *strrchr(dir, '/') = 0;
            pc5_mkdirs(dir);
            pc5_write_file(file, exe.data, exe.len);
            pc5_buf_free(&exe);
        }
        fmt(file, sizeof(file), "%s/usb0/Neon Drift/data", work);
        pc5_mkdirs(file);
        fmt(file, sizeof(file), "%s/usb0/Neon Drift/data/world.pak", work);
        FILE *f = fopen(file, "wb");
        for (int i = 0; f && i < 600; i++) { memset(chunk, i, sizeof(chunk)); fwrite(chunk, 1, sizeof(chunk), f); }
        if (f) fclose(f);
        fmt(file, sizeof(file), "%s/usb0/Hollow Peaks/bin/HollowPeaks.exe", work);
        fmt(from, sizeof(from), "%s/game32.exe", programs);
        {
            Pc5Buf exe = { 0 };
            pc5_read_file(from, &exe, 1 << 24);
            char dir[PC5_PATH];
            pc5_copy(dir, sizeof(dir), file);
            *strrchr(dir, '/') = 0;
            pc5_mkdirs(dir);
            pc5_write_file(file, exe.data, exe.len);
            pc5_buf_free(&exe);
        }
        /* The app's Windows template: a few files stand in for it. */
        fmt(file, sizeof(file), "%s/app0/pc5/prefix/system.reg", work);
        put(file, "system");
        fmt(file, sizeof(file), "%s/app0/pc5/prefix/drive_c/windows/system32/kernel32.dll", work);
        put(file, "k");
        fmt(file, sizeof(file), "%s/app0/pc5/prefix.list", work);
        put(file, "drive_c/\ndrive_c/windows/system32/kernel32.dll\nsystem.reg\n");
    }
    ui = open_ui(root, 1);
    step(ui, PC5_KEY_OPTIONS, 1);
    step(ui, PC5_KEY_ACCEPT, 1);
    until(ui, "sources");
    step(ui, PC5_KEY_SQUARE, 1);
    step(ui, PC5_KEY_DOWN, 1);
    step(ui, PC5_KEY_SQUARE, 1);
    settle(ui);
    save("09-install-usb-folders");
    step(ui, PC5_KEY_OPTIONS, 1);
    until(ui, "queue");
    settle(ui);
    save("09b-install-queue");
    step(ui, PC5_KEY_DOWN, 1);
    step(ui, PC5_KEY_ACCEPT, 1);
    settle(ui);
    save("10-install-review");
    step(ui, PC5_KEY_ACCEPT, 1);
    step(ui, PC5_KEY_OPTIONS, 1);
    until(ui, "install");
    /* catch it copying */
    for (int i = 0; i < 400 && (!strcmp(pc5_ui_screen(ui), "install") ? 1 : 0); i++) {
        step(ui, 0, 1);
        if (i == 60) save("11-installing");
    }
    until(ui, "message");
    settle(ui);
    save("12-installed");
    pc5_ui_destroy(ui);

    /* The loading screen, as a game starts. */
    {
        Pc5GameSession game;
        const Pc5GameOps none = { 0 };
        const uint32_t *frame = NULL;
        char app[PC5_PATH];
        fmt(app, sizeof(app), "%s/app0", work);
        fmt(file, sizeof(file), "%s/pc5/fonts.bin", app);
        {
            Pc5Buf atlas = { 0 };
            pc5_read_file(fonts, &atlas, 64u << 20);
            pc5_write_file(file, atlas.data, atlas.len);
            pc5_buf_free(&atlas);
        }
        pc5_game_begin(&game, &none, root, app, "", "Grand Theft Auto IV");
        for (uint64_t t = 0; t < 3000; t += 50) {
            const uint32_t *next = pc5_game_loading(&game, 1000 + t);
            if (next) frame = next;
        }
        if (frame) {
            /* 1280x720 to the screen's 1920x1080, as the title shows it */
            for (int y = 0; y < 1080; y++)
                for (int x = 0; x < 1920; x++) canvas.px[(size_t)y * 1920 + (size_t)x] = frame[(y * 2 / 3) * PC5_MATRIX_WIDTH + x * 2 / 3];
            save("16-loading-game");
        }
        pc5_game_end(&game, -1);
    }
    pc5_free(canvas.px);
    pc5_buf_free(&names);
    return 0;
}
