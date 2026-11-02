/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Private UI helpers. These run only on the UI thread. */
static void media_delete_begin(Pc5Ui *ui, int video)
{
    ui->media_delete_video = video;
    ui->button = 0;  /* Cancel is selected until Delete is explicitly chosen. */
    go(ui, S_MEDIA_DELETE);
}
static void media_delete_input(Pc5Ui *ui, uint32_t k, uint32_t back, uint32_t accept)
{
    if (k & PC5_KEY_TRIANGLE) { go(ui, S_LIBRARY); return; }
    if (k & back) { go(ui, S_GAME_MENU); return; }
    if (k & PC5_KEY_LEFT) ui->button = 0;
    if (k & PC5_KEY_RIGHT) ui->button = 1;
    if (k & accept) {
        if (!ui->button) { go(ui, S_GAME_MENU); return; }
        int video = ui->media_delete_video;
        if (video ? video_save(ui, "") : music_save(ui, "")) {
            message(ui, 0, video ? T("Video wasn't saved") : T("Music wasn't saved"),
                    video ? T("PC5 couldn't remove the video selection. Try again.")
                          : T("PC5 couldn't save the music selection. Try again."), S_GAME_MENU);
            return;
        }
        if (!video) { library_music_stop(ui); music_cleanup(ui, NULL); }
        go(ui, S_GAME_MENU);
    }
    if (k) ui->dirty = 1;
}
static int compatibility_indices(Pc5Ui *ui, int indices[PC5_COMPAT_MAX])
{
    int n = 0;
    for (int i = 0; i < ui->compatibility.count; ++i)
        if (pc5_compat_matches(&ui->compatibility.entries[i], ui->compat_query, ui->compat_filter)) indices[n++] = i;
    return n;
}
static void compatibility_input(Pc5Ui *ui, uint32_t k, uint32_t back, uint32_t accept)
{
    if (ui->now - ui->compat_refresh_at >= 60000) {
        pc5_compat_refresh(&ui->compatibility); ui->compat_refresh_at = ui->now;
    }
    if (k & back) {
        if (ui->compat_detail) { ui->compat_detail = ui->compat_scroll = 0; ui->dirty = 1; }
        return;
    }
    if (k & PC5_KEY_OPTIONS) {
        if (pc5_qr_make(PC5_COMPATIBILITY_NEW, &ui->compatibility_qr))
            message(ui, 0, T("QR code unavailable"), T("Try opening the compatibility report again."), S_COMPATIBILITY);
        else go(ui, S_COMPAT_SHARE);
        return;
    }
    if (k & PC5_KEY_L2) {
        int rc = ui->platform.keyboard_open ?
            ui->platform.keyboard_open(ui->platform.context, T("Search games"), ui->compat_query) : -1;
        say(ui, "PC5 compatibility search keyboard rc=%d", rc);
        if (rc >= 0) { ui->compat_detail = ui->compat_scroll = 0; go(ui, S_COMPAT_SEARCH); }
        else message(ui, 0, T("The keyboard didn't open"), T("The console's on-screen keyboard can't be used just now."), S_COMPATIBILITY);
        return;
    }
    if (k & PC5_KEY_R2) {
        ui->compat_query[0] = 0;
        ui->compat_at = ui->compat_detail = ui->compat_scroll = 0;
        ui->dirty = 1;
        return;
    }
    int indices[PC5_COMPAT_MAX], count = compatibility_indices(ui, indices);
    if (ui->compat_at >= count) ui->compat_at = count ? count - 1 : 0;
    if (ui->compat_detail) {
        if ((k & PC5_KEY_DOWN) && ui->compat_scroll < 240) ++ui->compat_scroll;
        if ((k & PC5_KEY_UP) && ui->compat_scroll) --ui->compat_scroll;
        if ((k & accept) && count) {
            const char *url = ui->compatibility.entries[indices[ui->compat_at]].report;
            pc5_qr_make(url[0] ? url : PC5_COMPATIBILITY, &ui->compatibility_qr);
            go(ui, S_COMPAT_REPORT);
        }
    } else {
        list_move(&ui->compat_at, count, k);
        if (count && (k & PC5_KEY_LEFT))
            ui->compat_at = ui->compat_at >= 8 ? ui->compat_at - 8 : 0;
        if (count && (k & PC5_KEY_RIGHT))
            ui->compat_at = ui->compat_at + 8 < count ? ui->compat_at + 8 : count - 1;
        if (k & PC5_KEY_SQUARE) { ui->compat_filter = (ui->compat_filter + 1) % 8; ui->compat_at = 0; }
        if ((k & accept) && count) { ui->compat_detail = 1; ui->compat_scroll = 0; }
    }
    if (k & PC5_KEY_TRIANGLE) { pc5_compat_refresh(&ui->compatibility); ui->compat_refresh_at = ui->now; }
    if (k) ui->dirty = 1;
}
static void look_begin(Pc5Ui *ui, const char *archive, int restore)
{
    int i = pc5_library_find(&ui->library, ui->launch_id);
    library_music_stop(ui);
    pc5_video_stop(&ui->video);
    pc5_backgrounds_end(&ui->backgrounds, 1); ui->backgrounds_for = -1;
    if (i < 0 || pc5_look_start(&ui->look, ui->root, ui->library.games[i].prefix, archive, restore)) {
        message(ui, 0, T("Appearance operation couldn't start"), T("Check the game's folder and free space."), S_MEDIA_OPTIONS); return;
    }
    go(ui, S_LOOK_WORK);
}
static void media_options_input(Pc5Ui *ui, uint32_t k, uint32_t back, uint32_t accept)
{
    if (k & back) { game_menu_at(ui, GR_MEDIA_OPTIONS); go(ui, S_GAME_MENU); return; }
    list_move(&ui->media_row, 8, k);
    int step = k & PC5_KEY_LEFT ? -1 : 1;
    if (!(k & (PC5_KEY_LEFT | PC5_KEY_RIGHT | accept))) { if (k) ui->dirty = 1; return; }
    if (ui->media_row == 6) {
        if (k & accept) {
            files_open(ui, 0); ui->files_look = 2; files_places(ui); files_load(ui, NULL);
        }
        return;
    }
    if (ui->media_row == 7) { if (k & accept) { files_open(ui, 0); ui->files_look = 1; files_places(ui); files_load(ui, NULL); } return; }
    Pc5Appearance previous = ui->appearance, *o = &ui->appearance;
    switch (ui->media_row) {
    case 0: o->volume += step * 5; if (o->volume < 0) o->volume = 0; if (o->volume > 100) o->volume = 100; break;
    case 1: o->music_start += step * ((k & PC5_KEY_R2) ? 10 : 1); if (o->music_start < 0) o->music_start = 0; break;
    case 2: o->video_start += step; if (o->video_start < 0) o->video_start = 0;
        if (o->video_end && o->video_end <= o->video_start) o->video_end = o->video_start + 1;
        break;
    case 3: o->video_end = !o->video_end && step > 0 ? o->video_start + 1 : o->video_end + step;
        if (o->video_end <= o->video_start) o->video_end = 0;
        break;
    case 4: o->fit = (o->fit + 3 + step) % 3; break;
    case 5: o->darkness += step * 5; if (o->darkness < 0) o->darkness = 0; if (o->darkness > 80) o->darkness = 80; break;
    }
    int index = pc5_library_find(&ui->library, ui->launch_id);
    if (index < 0 || pc5_appearance_save(ui->root, ui->library.games[index].prefix, o)) {
        *o = previous;
        message(ui, 0, T("Settings weren't saved"), T("Check free space and try again."), S_MEDIA_OPTIONS);
    }
    ui->video_id[0] = ui->music_id[0] = 0;
    ui->dirty = 1;
}
