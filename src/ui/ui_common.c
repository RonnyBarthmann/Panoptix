/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Gemeinsame Helfer fuer alle Screens. */
#include <string.h>

#include "ui/ui_internal.h"

int g_ui_inv_select = -1;

char *hero_label(const Hero *h, char *buf, size_t size) {
    str_format(buf, size, "%s (%s %s %d)", h->name, g_classes[h->cls].name, tr("Lv"), h->level);
    return buf;
}

void app_ensure_hero(App *app) {
    Game *g = &app->game;
    if (app->sel_hero >= 0 && app->sel_hero < MAX_HEROES && g->heroes[app->sel_hero].used) {
        return;
    }
    app->sel_hero = -1;
    for (int i = 0; i < MAX_HEROES; i++) {
        if (g->heroes[i].used) {
            app->sel_hero = i;
            return;
        }
    }
}

bool ui_hero_picker(App *app, Rect r) {
    Ui *ui = &app->ui;
    Game *g = &app->game;
    bool changed = false;
    int n = 0;
    for (int i = 0; i < MAX_HEROES; i++) {
        n += g->heroes[i].used ? 1 : 0;
    }
    if (n == 0) {
        ui_text(ui, r.x + 4, r.y + 4, tr("Kein Held vorhanden - in der Heldenliste (F1) erstellen."), COL_INK, FS_SMALL);
        return false;
    }
    int cw = mini(ui_px(ui, 150), r.w / n);
    int x = r.x;
    for (int i = 0; i < MAX_HEROES; i++) {
        if (!g->heroes[i].used) {
            continue;
        }
        Rect b = rect_make(x, r.y, cw - 2, r.h);
        ui_push_id(ui, i);
        char lab[64];
        str_format(lab, sizeof lab, "%s %d", g->heroes[i].name, g->heroes[i].level);
        bool on = app->sel_hero == i;
        if (ui_button_ex(ui, b, lab, on ? UI_BTN_WAX : UI_BTN_TAB, true) && !on) {
            app->sel_hero = i;
            changed = true;
        }
        ui_pop_id(ui);
        x += cw;
    }
    return changed;
}

void ui_result(App *app, bool ok, const char *err, const char *ok_text) {
    if (ok) {
        if (ok_text && ok_text[0]) {
            app_message(app, "%s", ok_text);
        }
        app_play(app, SFX_CLICK);
    } else {
        app_message(app, "%s", err && err[0] ? err : tr("Nicht moeglich"));
        app_play(app, SFX_ERROR);
    }
}

bool ui_btn(App *app, Rect r, const char *label, bool enabled) {
    bool c = ui_button_ex(&app->ui, r, label, UI_BTN_WAX, enabled);
    if (c) {
        app_play(app, SFX_CLICK);
    }
    return c;
}


Color ui_tier(const App *app, int tier, u8 alpha) {
    return gfx_tier_color(&app->gfx, (Tier)clampi(tier, 0, TIER_COUNT - 1), alpha);
}

char *ui_map_label(int map_idx, char *buf, size_t size) {
    if (map_idx < 0 || map_idx >= g_map_count) {
        str_copy(buf, size, "-");
        return buf;
    }
    const MapDef *m = &g_maps[map_idx];
    str_format(buf, size, "%s (Lv %d)", m->id, m->level);
    return buf;
}

const char *ui_class_name(int cls) {
    return (cls >= 0 && cls < CLASS_COUNT) ? g_classes[cls].name : tr("Allgemein");
}

char *ui_num(i64 v, char *buf, size_t size) { return str_format_number(buf, size, v); }

void ui_draw_currency_line(App *app, int x, int y, Currency c, int base_size) {
    Ui *ui = &app->ui;
    char b[40], line[80];
    str_format(line, sizeof line, "%s %s", tr(g_currency_names[c]), ui_num(currency_get(&app->game, c), b, sizeof b));
    int s = ui_px(ui, base_size);
    icon_draw_currency(&app->gfx, rect_make(x, y, s, s), c);
    gfx_text(&app->gfx, x + s + 4, y, line, COL_INK, s);
}
