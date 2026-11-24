/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Titel-/ESC-Menue: Weiter, Neues Spiel, Laden/Speichern (3 Slots), Einstellungen, Beenden. */
#include <string.h>
#include <time.h>

#include "game/save.h"
#include "ui/ui_internal.h"

static bool s_confirm_new = false;
static bool s_confirm_save = false;
static int s_save_target = 0;

/* Zeichnet den Titel und gibt die y-Position darunter zurueck. */
static int draw_title(App *app, Rect area) {
    Ui *ui = &app->ui;
    Gfx *g = &app->gfx;
    gfx_fill_rect_vgrad(g, area, RGB(0x2E, 0x1F, 0x14), RGB(0x14, 0x0D, 0x08));
    Rect t = rect_make(area.x, area.y + ui_px(ui, 24), area.w, ui_px(ui, 56));
    ui_text_center(ui, t, "PANOPTIX", COL_GOLD, FS_HUGE + 12);
    Rect s = rect_make(area.x, t.y + t.h, area.w, ui_px(ui, 24));
    ui_text_center(ui, s, tr("Kommandiere Deine Helden"), COL_PARCHMENT_DARK, FS_NORMAL);
    return s.y + s.h + ui_px(ui, 12);
}

/* Slot-Zeile: Info links, Button rechts. */
static void slot_info_text(int slot, char *buf, size_t size) {
    SaveInfo si;
    if (!save_peek(slot, &si) || !si.valid) {
        str_copy(buf, size, tr("Leer"));
        return;
    }
    char dur[32], when[40] = "";
    str_format_duration(dur, sizeof dur, si.play_time);
    time_t tt = (time_t)si.saved_unix;
    struct tm *tmv = localtime(&tt);
    if (tmv) {
        strftime(when, sizeof when, "%d.%m.%Y %H:%M", tmv);
    }
    str_format(buf, size, "%s %d | %d %s | %s | %s", tr("Level"), si.player_level, si.hero_count,
               tr("Helden"), dur, when);
}

static void draw_slot_row(App *app, Rect row, int slot) {
    Ui *ui = &app->ui;
    ui_push_id(ui, slot);
    gfx_fill_rect(&app->gfx, row, RGBA(0, 0, 0, 40));
    gfx_draw_rect(&app->gfx, row, COL_WOOD, 1);
    char head[40], info[160];
    str_format(head, sizeof head, "%s %d%s", tr("Slot"), slot + 1,
               app->settings.save_slot == slot ? " *" : "");
    slot_info_text(slot, info, sizeof info);
    int pad = ui_px(ui, 8);
    int bw = ui_px(ui, 90), bh = ui_px(ui, 28);
    int by = row.y + (row.h - bh) / 2;
    int bx = row.x + row.w - pad - bw;
    ui_text(ui, row.x + pad, row.y + ui_px(ui, 4), head, COL_PARCHMENT, FS_NORMAL);
    gfx_push_clip(&app->gfx, rect_make(row.x, row.y, bx - row.x - pad, row.h));
    ui_text(ui, row.x + pad, row.y + ui_px(ui, 24), info, COL_PARCHMENT_DARK, FS_SMALL);
    gfx_pop_clip(&app->gfx);
    bool exists = save_exists(slot);
    if (app->have_game &&
        ui_button_ex(ui, rect_make(bx, by, bw, bh), tr("Speichern"), UI_BTN_NORMAL, true)) {
        s_save_target = slot;
        if (exists) {
            s_confirm_save = true;
        } else {
            app->settings.save_slot = slot;
            app_save(app);
        }
    }
    bx -= bw + pad;
    if (ui_button_ex(ui, rect_make(bx, by, bw, bh), tr("Laden"), UI_BTN_GREEN, exists)) {
        app_play(app, SFX_CLICK);
        app_load(app, slot);
    }
    ui_pop_id(ui);
}

static void handle_confirms(App *app) {
    int r = ui_confirm(&app->ui, &s_confirm_new, tr("Neues Spiel"),
                       tr("Das laufende Spiel wird verworfen (nicht gespeicherter Fortschritt geht verloren). Fortfahren?"));
    if (r == 1) {
        app_new_game(app);
    }
    r = ui_confirm(&app->ui, &s_confirm_save, tr("Speichern"),
                   tr("Der vorhandene Spielstand in diesem Slot wird ueberschrieben. Fortfahren?"));
    if (r == 1) {
        app->settings.save_slot = s_save_target;
        app_save(app);
    }
}

void screen_menu(App *app, Rect area) {
    Ui *ui = &app->ui;
    int y = draw_title(app, area);
    int w = mini(ui_px(ui, 560), area.w - ui_px(ui, 32));
    int x = area.x + (area.w - w) / 2;
    int bh = ui_px(ui, 38), gap = ui_px(ui, 8);
    bool modal = s_confirm_new || s_confirm_save;
    if (app->have_game) {
        if (ui_button_ex(ui, rect_make(x, y, w, bh), tr("Weiter"), UI_BTN_GREEN, !modal)) {
            app_play(app, SFX_CLICK);
            app_set_screen(app, app->prev_screen == SCR_MENU ? SCR_COMBAT : app->prev_screen);
        }
        y += bh + gap;
    }
    if (ui_button_ex(ui, rect_make(x, y, w, bh), tr("Neues Spiel"), UI_BTN_WAX, !modal)) {
        if (app->have_game) {
            s_confirm_new = true;
        } else {
            app_new_game(app);
        }
    }
    y += bh + gap * 2;
    ui_text(ui, x, y, app->have_game ? tr("Laden / Speichern") : tr("Spiel laden"), COL_GOLD, FS_BIG);
    y += ui_px(ui, 30);
    int rh = ui_px(ui, 50);
    for (int i = 0; i < SAVE_SLOTS; i++) {
        draw_slot_row(app, rect_make(x, y, w, rh), i);
        y += rh + gap;
    }
    y += gap;
    int hw = (w - gap) / 2;
    if (ui_button_ex(ui, rect_make(x, y, hw, bh), tr("Einstellungen"), UI_BTN_NORMAL, !modal)) {
        app_set_screen(app, SCR_SETTINGS);
    }
    if (ui_button_ex(ui, rect_make(x + hw + gap, y, hw, bh), tr("Beenden"), UI_BTN_WAX, !modal)) {
        app->running = false;
    }
    handle_confirms(app);
}
