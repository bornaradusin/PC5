/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: a game's tile on the PS5's home screen (pc5_home.h). */
#include "pc5_home.h"
#include "pc5_i18n.h"
#include "pc5_image.h"
#include "pc5_tile.h"
#include "third_party/psfwd/psfwd.h"

#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#define TILE_TINT 0xff2a3442u   /* the screens' own background, for a game without a cover */

/* 1 when dir is id's tile: an app of its own for it (pc5-tile.json, pc5_tile.h), or a
 * forwarder that starts PC5 with id (its "--rom" argument). */
static int tile_of(const char *dir, const char *id, psfwd_info *info)
{
    char game[PC5_ID], error[PC5_ERROR];
    if (pc5_tile_read(dir, game, error, sizeof(error)) == 1) return !strcmp(game, id);
    if (!psfwd_read(dir, info) || strcmp(info->target, PC5_TITLE_ID)) return 0;
    for (int i = 0; i + 1 < info->argc; i++)
        if (!strcmp(info->argv[i], "--rom") && !strcmp(info->argv[i + 1], id)) return 1;
    return 0;
}

int pc5_home_find(const char *root, const char *id, char out[PC5_HOME_ID])
{
    DIR *folder;
    struct dirent *entry;
    psfwd_info *info;
    int found = 0;

    out[0] = 0;
    if (!root || !id || !id[0] || !(folder = opendir(root))) return 0;
    if (!(info = pc5_alloc(sizeof(*info)))) {
        closedir(folder);
        return 0;
    }
    while (!found && (entry = readdir(folder))) {
        char dir[PC5_PATH];
        if (!psfwd_valid_title_id(entry->d_name) || pc5_join(dir, sizeof(dir), root, entry->d_name) ||
            !pc5_is_dir(dir))
            continue;
        if (tile_of(dir, id, info)) {
            pc5_copy(out, PC5_HOME_ID, entry->d_name);
            found = 1;
        }
    }
    closedir(folder);
    pc5_free(info);
    return found;
}

int pc5_home_remove(const char *root, const char *id)
{
    char title_id[PC5_HOME_ID];
    int removed = 0;

    /* A game has one tile; more, made by hand or by another tool, go too. */
    while (removed < 64 && pc5_home_find(root, id, title_id)) {
        if (!psfwd_remove(root, title_id)) return -1;
        removed++;
    }
    return removed;
}

static uint32_t darker(uint32_t px, unsigned keep)
{
    uint32_t r = (px >> 16 & 255) * keep / 256, g = (px >> 8 & 255) * keep / 256, b = (px & 255) * keep / 256;
    return 0xff000000u | r << 16 | g << 8 | b;
}

int pc5_home_icon(const Pc5Image *cover, uint32_t tint, Pc5Image *icon)
{
    const int side = PC5_HOME_ICON, margin = 24;
    Pc5Image wide = { 0 }, front = { 0 };

    *icon = (Pc5Image){ 0 };
    if (pc5_image_new(icon, side, side)) return -1;
    if (!cover || !cover->px || cover->w <= 0 || cover->h <= 0) {
        for (int i = 0; i < side * side; i++) icon->px[i] = 0xff000000u | tint;
        return 0;
    }
    /* Behind: the cover filling the square, blurred and dimmed. */
    int wide_h = (int)((int64_t)cover->h * side / cover->w);
    if (wide_h < side) wide_h = side;
    if (pc5_image_resize(cover, side, wide_h, &wide)) {
        pc5_image_free(icon);
        return -1;
    }
    for (int y = 0; y < side; y++)
        memcpy(icon->px + (size_t)y * side, wide.px + (size_t)(y + (wide_h - side) / 2) * side, (size_t)side * 4);
    pc5_image_free(&wide);
    pc5_image_blur(icon, 18);
    for (int i = 0; i < side * side; i++) icon->px[i] = darker(icon->px[i], 110);
    /* In front: the whole cover, as tall as the square less a margin. */
    int front_h = side - 2 * margin, front_w = (int)((int64_t)cover->w * front_h / cover->h);
    if (front_w > side - 2 * margin) front_w = side - 2 * margin;
    if (pc5_image_resize(cover, front_w, front_h, &front)) {
        pc5_image_free(icon);
        return -1;
    }
    for (int y = 0; y < front_h; y++)
        for (int x = 0; x < front_w; x++)
            icon->px[(size_t)(y + margin) * side + (size_t)((side - front_w) / 2 + x)] =
                0xff000000u | front.px[(size_t)y * front_w + x];
    pc5_image_free(&front);
    return 0;
}

int pc5_home_picture(const char *library_root, const char *prefix, Pc5Buf *png)
{
    char path[PC5_PATH];
    Pc5Image cover = { 0 }, icon = { 0 };
    uint32_t tint = TILE_TINT;
    int status;

    *png = (Pc5Buf){ 0 };
    /* The tile picture chosen for it (Appearance › Tile picture), as it is. */
    if (!pc5_format(path, sizeof(path), "%s/prefixes/%s/pc5/icon.bin", library_root, prefix) &&
        !pc5_cover_load(path, &icon, NULL) && icon.w > 0 && icon.h > 0) {
        Pc5Image sized = { 0 };
        if (icon.w == PC5_HOME_ICON && icon.h == PC5_HOME_ICON) sized = icon, icon = (Pc5Image){ 0 };
        else if (pc5_image_resize(&icon, PC5_HOME_ICON, PC5_HOME_ICON, &sized)) sized = (Pc5Image){ 0 };
        pc5_image_free(&icon);
        if (sized.px) {
            status = pc5_image_png(&sized, png) ? -1 : 0;
            pc5_image_free(&sized);
            if (!status) return 0;
            pc5_buf_free(png);
        }
    }
    pc5_image_free(&icon);
    /* Otherwise one made from its cover. */
    if (pc5_format(path, sizeof(path), "%s/prefixes/%s/pc5/cover.bin", library_root, prefix) ||
        pc5_cover_load(path, &cover, &tint))
        cover = (Pc5Image){ 0 };
    status = pc5_home_icon(cover.px ? &cover : NULL, tint & 0xffffffu, &icon) || pc5_image_png(&icon, png) ? -1 : 0;
    if (status) pc5_buf_free(png);
    pc5_image_free(&cover);
    pc5_image_free(&icon);
    return status;
}

int pc5_home_add(const char *root, const char *template_dir, const char *library_root, const char *id,
                 const char *prefix, const char *name, char out[PC5_HOME_ID], char *error, size_t size)
{
    char path[PC5_PATH];
    Pc5Buf png = { 0 };
    int status = -1;

    out[0] = 0;
    if (pc5_format(path, sizeof(path), "%s/eboot.bin", template_dir) || !pc5_is_file(path) ||
        pc5_format(path, sizeof(path), "%s/sce_module/libc.prx", template_dir) || !pc5_is_file(path)) {
        pc5_format(error, size, N_("The tile's program is missing from PC5's app (pc5/forwarder)."));
        return -1;
    }
    if (!pc5_home_find(root, id, out) && (pc5_mkdirs(root) || !psfwd_new_title_id(root, out))) {
        pc5_format(error, size, N_("PC5 can't use %s: %s."), root, strerror(errno));
        return -1;
    }
    if (pc5_home_picture(library_root, prefix, &png)) {
        pc5_format(error, size, N_("PC5 ran out of memory making the tile's picture."));
        goto done;
    }
    {
        const char *const argv[] = { "--rom", id, "--exit-after-game" };
        psfwd_spec spec = { 0 };
        char reason[200] = "";
        spec.title_id = out;
        spec.name = name && name[0] ? name : id;
        spec.target = PC5_TITLE_ID;
        spec.argc = 3;
        spec.argv = argv;
        spec.creator = "PC5 " PC5_VERSION;
        spec.icon0_png = (psfwd_blob){ png.data, png.len };
        if (!psfwd_write(root, template_dir, &spec, reason, sizeof(reason))) {
            pc5_format(error, size, N_("PC5 can't write the tile in %s: %s."), root, reason);
            goto done;
        }
    }
    status = 0;
done:
    if (status) out[0] = 0;
    pc5_buf_free(&png);
    return status;
}
