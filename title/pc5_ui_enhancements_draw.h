/* SPDX-License-Identifier: LGPL-2.1-or-later */
static uint32_t compatibility_color(int status)
{
    return status == 1 ? 0xff46d99au : status == 2 ? C_WARN : status == 7 ? C_BAD : C_ACCENT;
}
/* pc5_text_wrap treats a newline as a glyph. Reports need real paragraph
 * breaks and a scroll measured in visible lines, without changing other UI. */
static int compatibility_body(Pc5Ui *ui, Pc5Canvas *c, const char *text, int skip)
{
    char line[2048]; size_t length = 0; int row = 0;
    while (*text || length) {
        if (!*text || *text == '\n') {
            line[length] = 0;
            if (c && row >= skip && row < skip + 13)
                pc5_text(c, &ui->fonts, PC5_FACE_BODY, 220, 290 + (row - skip) * 40, C_TEXT, line, 1460, 0);
            ++row; length = 0; if (*text) ++text; continue;
        }
        if (*text == ' ') { ++text; continue; }
        size_t word = 0; while (text[word] && text[word] != ' ' && text[word] != '\n') ++word;
        if (word > sizeof(line) - 2) word = sizeof(line) - 2;
        size_t old = length;
        if (length) line[length++] = ' ';
        if (length + word >= sizeof(line)) {
            length = old; line[length] = 0;
            if (c && row >= skip && row < skip + 13)
                pc5_text(c, &ui->fonts, PC5_FACE_BODY, 220, 290 + (row - skip) * 40, C_TEXT, line, 1460, 0);
            ++row; length = 0; continue;
        }
        memcpy(line + length, text, word); length += word; line[length] = 0;
        if (old && pc5_text_width(&ui->fonts, PC5_FACE_BODY, line) > 1460) {
            line[old] = 0;
            if (c && row >= skip && row < skip + 13)
                pc5_text(c, &ui->fonts, PC5_FACE_BODY, 220, 290 + (row - skip) * 40, C_TEXT, line, 1460, 0);
            ++row; length = 0; continue;
        }
        text += word;
    }
    return row;
}
static void compatibility_screen(Pc5Ui *ui, Pc5Canvas *c)
{
    int indices[PC5_COMPAT_MAX], count = compatibility_indices(ui, indices);
    dim(c, 210);
    top_bar(ui, c); /* This is now a tab: keep its navigation fully readable. */
    panel(c, 48, 120, 1824, 850);
    pc5_text(c, &ui->fonts, PC5_FACE_HEAD, 90, 182, C_TEXT, T("Game compatibility"), 1600, 0);
    pc5_round_rect(c, 1010, 140, 480, 56, 14, C_ROW);
    pc5_round_frame(c, 1010, 140, 480, 56, 14, 2, C_ACCENT);
    Hint share[] = { { G_OPTIONS_PAD, T("Share your experience") } };
    int share_width = glyph_width(ui, G_OPTIONS_PAD) + 12 +
                      pc5_text_width(&ui->fonts, PC5_FACE_BODY, share[0].label);
    hints(ui, c, 1250 - share_width / 2, 176, share, 1);
    if (!ui->compat_detail)
        pc5_text(c, &ui->fonts, PC5_FACE_SMALL, 1820, 182, C_DIM, T("L1 / R1: Change tab"), 500, 2);
    char line[512];
    snprintf(line, sizeof(line), "%s: %s   |   %s: %s   |   %d / %d",
        T("Search"), ui->compat_query[0] ? ui->compat_query : T("All games"), T("Filter"),
        ui->compat_filter ? T(pc5_compat_status(ui->compat_filter)) : T("All"), count, ui->compatibility.count);
    pc5_text(c, &ui->fonts, PC5_FACE_SMALL, 90, 232, C_DIM, line, 1700, 0);
    if (ui->compat_at >= count) ui->compat_at = count ? count - 1 : 0;
    int from = ui->compat_at / 8 * 8;
    for (int n = from; n < count && n < from + 8; ++n) {
        const Pc5CompatEntry *e = &ui->compatibility.entries[indices[n]];
        int y = 260 + (n - from) * 72;
        pc5_round_rect(c, 80, (float)y, 860, 64, 12, n == ui->compat_at ? C_ROW : C_PANEL);
        if (n == ui->compat_at) pc5_round_frame(c, 80, (float)y, 860, 64, 12, 2, C_ACCENT);
        pc5_circle(c, 105, (float)y + 32, 7, compatibility_color(e->status));
        pc5_text(c, &ui->fonts, PC5_FACE_LABEL, 126, y + 41, C_TEXT, e->title, 790, 0);
    }
    if (!count) pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, 100, 340, 780, 4, 44, C_DIM,
        ui->compatibility.running ? T("Loading community reports...") : T("No matching games. Refresh or change the search and filter."), 0);
    if (count) {
        const Pc5CompatEntry *e = &ui->compatibility.entries[indices[ui->compat_at]];
        pc5_text_wrap(c, &ui->fonts, PC5_FACE_HEAD, 985, 305, 800, 2, 48, C_TEXT, e->title, 0);
        pc5_text(c, &ui->fonts, PC5_FACE_LABEL, 985, 415, compatibility_color(e->status), T(pc5_compat_status(e->status)), 800, 0);
        snprintf(line, sizeof(line), "PC5: %s  |  %s", e->version[0] ? e->version : "—", e->controller);
        pc5_text(c, &ui->fonts, PC5_FACE_BODY, 985, 468, C_DIM, line, 800, 0);
        pc5_text_wrap(c, &ui->fonts, PC5_FACE_SMALL, 985, 515, 800, 2, 36, C_DIM, e->store, 0);
        pc5_text(c, &ui->fonts, PC5_FACE_LABEL, 985, 605, C_ACCENT, T("Settings that helped"), 800, 0);
        pc5_text_wrap(c, &ui->fonts, PC5_FACE_BODY, 985, 650, 800, 4, 40, C_TEXT,
            e->settings[0] ? e->settings : T("No settings reported"), 0);
        pc5_text(c, &ui->fonts, PC5_FACE_SMALL, 985, 842, C_DIM, e->author, 800, 0);
    }
    pc5_text(c, &ui->fonts, PC5_FACE_SMALL, 90, 880, C_DIM,
        ui->compatibility.running ? T("Checking for updates...") : ui->compatibility.error[0] ? T(ui->compatibility.error) :
        ui->compatibility.cached ? T("Saved list • Refresh to check for changes") : T("Last checked this session • Automatic refresh every minute"), 1700, 0);
    if (ui->compatibility.checked) {
        int64_t checked = ui->compatibility.checked;
        int64_t z = checked / 86400 + 719468, era = z / 146097, doe = z - era * 146097;
        int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
        int month = (int)(mp < 10 ? mp + 3 : mp - 9), day = (int)(doy - (153 * mp + 2) / 5 + 1);
        int year = (int)(yoe + era * 400 + (month <= 2));
        char date[64], stamp[128];
        snprintf(date, sizeof(date), "%04d-%02d-%02d %02d:%02d UTC", year, month, day, (int)(checked % 86400 / 3600), (int)(checked % 3600 / 60));
        snprintf(stamp, sizeof(stamp), "%s: %s", T("Last checked"), date);
        pc5_text(c, &ui->fonts, PC5_FACE_SMALL, 90, 914, C_DIM, stamp, 1700, 0);
    }
    pc5_text(c, &ui->fonts, PC5_FACE_SMALL, 90, 950, C_DIM, T("Community reports: results depend on the game build, settings and PC5 version."), 1700, 0);
    if (!ui->compat_detail)
        pc5_text(c, &ui->fonts, PC5_FACE_SMALL, 1810, 914, C_DIM, T("Left / Right: Page"), 600, 2);
    Hint list[] = { { accept_glyph(ui), T("Details") }, { G_SQUARE, T("Filter") }, { G_L2, T("Search") },
        { G_R2, T("Clear search") }, { G_TRIANGLE, T("Refresh") }, { G_OPTIONS_PAD, T("Share") } };
    hints(ui, c, 90, 1024, list, 6);
    if (ui->compat_detail && count) {
        const Pc5CompatEntry *e = &ui->compatibility.entries[indices[ui->compat_at]];
        dim(c, 225); panel(c, 160, 150, 1600, 790);
        pc5_text(c, &ui->fonts, PC5_FACE_HEAD, 220, 220, C_TEXT, e->title, 1470, 0);
        Pc5Buf b = { 0 };
        pc5_buf_printf(&b, "%s\nPC5: %s\n%s: %s\n%s: %s\n\n%s\n%s\n\n%s\n%s\n\n%s",
            T(pc5_compat_status(e->status)), e->version, T("Store and version"), e->store, T("Controller"), e->controller,
            T("Settings that helped"), e->settings, T("Notes"), e->notes, e->author);
        pc5_buf_terminate(&b);
        const char *text = b.data ? (const char *)b.data : "";
        int lines = compatibility_body(ui, NULL, text, 0);
        if (ui->compat_scroll > lines - 13) ui->compat_scroll = lines > 13 ? lines - 13 : 0;
        compatibility_body(ui, c, text, ui->compat_scroll);
        char position[80]; snprintf(position, sizeof(position), "%d / %d", ui->compat_scroll + 1, lines);
        pc5_text(c, &ui->fonts, PC5_FACE_SMALL, 1680, 858, C_DIM, position, 200, 2);
        pc5_buf_free(&b);
        Hint detail[] = { { accept_glyph(ui), T("Read user report") }, { back_glyph(ui), T("Back") } };
        hints(ui, c, 220, 908, detail, 2);
    }
}
static void compatibility_report_screen(Pc5Ui *ui, Pc5Canvas *c)
{
    dim(c, 220); panel(c, 300, 150, 1320, 800);
    int share = ui->screen == S_COMPAT_SHARE;
    pc5_text(c, &ui->fonts, PC5_FACE_HEAD, 370, 235, C_TEXT, share ? T("Share your experience") : T("Read user report"), 1150, 0);
    qr_draw(c, &ui->compatibility_qr, 705, 290, 510);
    pc5_text(c, &ui->fonts, PC5_FACE_BODY, 960, 860, C_DIM,
             share ? T("Scan to share your game's compatibility on GitHub") : T("Scan to read the original report on GitHub"), 1100, 1);
    Hint list[] = { { back_glyph(ui), T("Back") } }; hints(ui, c, 370, 916, list, 1);
}
static void media_options_screen(Pc5Ui *ui, Pc5Canvas *c)
{
    dim(c, 185); panel(c, 220, 135, 1480, 800);
    pc5_text(c, &ui->fonts, PC5_FACE_HEAD, 285, 220, C_TEXT, T("Media settings"), 1300, 0);
    const char *labels[] = { T("Music volume"), T("Music starts at"), T("Video starts at"), T("Video ends at"), T("Video framing"), T("Additional video dimming"), T("Export appearance backup"), T("Restore appearance backup") };
    char values[8][64]; Pc5Appearance *o = &ui->appearance;
    snprintf(values[0], 64, "%d %%", o->volume);
    snprintf(values[1], 64, "%d:%02d", o->music_start / 60, o->music_start % 60);
    snprintf(values[2], 64, "%d:%02d", o->video_start / 60, o->video_start % 60);
    if (o->video_end) snprintf(values[3], 64, "%d:%02d", o->video_end / 60, o->video_end % 60);
    else pc5_copy(values[3], 64, T("End of file"));
    pc5_copy(values[4], 64, o->fit == 1 ? T("Fit (keep whole picture)") : o->fit == 2 ? T("Fill (crop edges)") : T("Stretch"));
    snprintf(values[5], 64, "%d %%", o->darkness);
    pc5_copy(values[6], 64, T("Choose folder / internal / USB"));
    pc5_copy(values[7], 64, T("Choose from files / USB"));
    for (int i = 0; i < 8; ++i) {
        int y = 245 + i * 67;
        pc5_round_rect(c, 270, (float)y, 1380, 60, 12, i == ui->media_row ? C_ROW : C_PANEL);
        if (i == ui->media_row) pc5_round_frame(c, 270, (float)y, 1380, 60, 12, 2, C_ACCENT);
        pc5_text(c, &ui->fonts, PC5_FACE_LABEL, 300, y + 46, C_TEXT, labels[i], 700, 0);
        pc5_text(c, &ui->fonts, PC5_FACE_BODY, 1610, y + 46, C_ACCENT, values[i], 550, 2);
    }
    pc5_text(c, &ui->fonts, PC5_FACE_SMALL, 285, 810, C_DIM, T("Left / Right to adjust. Saved automatically for this game."), 1300, 0);
    pc5_text(c, &ui->fonts, PC5_FACE_SMALL, 285, 850, C_DIM, T("Music fades in and out. Video stays silent. An offset beyond the file uses its beginning."), 1300, 0);
    Hint list[] = { { back_glyph(ui), T("Back") } }; hints(ui, c, 285, 910, list, 1);
}
