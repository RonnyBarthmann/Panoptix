/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Heldendetails: 14 Ausruestungsslots, passende Lagergegenstaende, Stats und abgeleitete Werte. */
#include <string.h>

#include "ui/ui_internal.h"

#define STAT_RESET_COST 100

static int s_slot = SLOT_WEAPON; /* ausgewaehlter Ausruestungsslot */
static bool s_reset_open = false;

/* Passt ein Item-Slot zum gewaehlten Ausruestungsslot (Ringe/Traenke sind austauschbar)? */
static bool slot_compatible(int sel, int item_slot) {
    if (sel == item_slot) {
        return true;
    }
    bool ring = (sel == SLOT_RING1 || sel == SLOT_RING2) && (item_slot == SLOT_RING1 || item_slot == SLOT_RING2);
    bool pot = sel >= SLOT_POTION1 && sel <= SLOT_POTION3 && item_slot >= SLOT_POTION1 && item_slot <= SLOT_POTION3;
    return ring || pot;
}

/* ---- Ausruestung (links) ---------------------------------------------- */
static void draw_equip_cell(App *app, Hero *h, Rect cell, int slot, int cs) {
    Ui *ui = &app->ui;
    Rect r = rect_make(cell.x + (cell.w - cs) / 2, cell.y, cs, cs);
    ui_push_id(ui, slot);
    UiClick c;
    if (item_is_empty(&h->eq[slot])) {
        c = ui_region(ui, r, ui_make_id(ui, "eqs", r.x, r.y));
        icon_draw_empty_slot(ui->gfx, r, (EquipSlot)slot);
        gfx_draw_rect(ui->gfx, r, c.hovered ? COL_WHITE : COL_WOOD, 1);
    } else {
        c = ui_item_slot(ui, r, &app->game, NULL, &h->eq[slot], false);
    }
    if (s_slot == slot) {
        gfx_draw_rect(ui->gfx, rect_inset(r, -2), COL_GOLD, 3);
    }
    if (c.clicked) {
        s_slot = slot;
    }
    if (c.rclicked && !item_is_empty(&h->eq[slot])) {
        char err[96] = "";
        bool ok = hero_unequip(&app->game, app->sel_hero, (EquipSlot)slot, err);
        ui_result(app, ok, err, NULL);
    }
    ui_text_center(ui, rect_make(cell.x, cell.y + cs + ui_px(ui, 2), cell.w, ui_px(ui, 16)), tr(g_equip_slot_names[slot]),
                   COL_PARCHMENT, FS_TINY);
    ui_pop_id(ui);
}

static void draw_equipment(App *app, Hero *h, Rect r) {
    Ui *ui = &app->ui;
    Rect in = ui_panel(ui, r, tr("Ausruestung"));
    in = rect_inset(in, ui_px(ui, 8));
    int cols = 4, rows = (SLOT_COUNT + cols - 1) / cols;
    int cw = in.w / cols;
    int ch = in.h / rows;
    int cs = mini(mini(cw - ui_px(ui, 8), ch - ui_px(ui, 22)), ui_px(ui, 64));
    for (int s = 0; s < SLOT_COUNT; s++) {
        Rect cell = rect_make(in.x + (s % cols) * cw, in.y + (s / cols) * ch, cw, ch);
        draw_equip_cell(app, h, cell, s, cs);
    }
}

/* ---- Auswahl aus dem Lager (Mitte) ------------------------------------ */
static void draw_unequip_row(App *app, Hero *h, Rect r) {
    Ui *ui = &app->ui;
    char buf[96];
    const Item *cur = &h->eq[s_slot];
    if (item_is_empty(cur)) {
        str_format(buf, sizeof buf, "%s: %s", tr(g_equip_slot_names[s_slot]), tr("leer"));
    } else {
        item_display_name(cur, buf, sizeof buf);
    }
    int bw = ui_px(ui, 110);
    gfx_push_clip(ui->gfx, rect_make(r.x, r.y, r.w - bw - ui_px(ui, 6), r.h));
    ui_text(ui, r.x, r.y + ui_px(ui, 6), buf, COL_INK, FS_NORMAL);
    gfx_pop_clip(ui->gfx);
    Rect b = rect_make(r.x + r.w - bw, r.y, bw, ui_px(ui, 30));
    if (ui_button_ex(ui, b, tr("Ablegen"), UI_BTN_NORMAL, !item_is_empty(cur))) {
        char err[96] = "";
        bool ok = hero_unequip(&app->game, app->sel_hero, (EquipSlot)s_slot, err);
        ui_result(app, ok, err, NULL);
    }
}

static int collect_candidates(const Game *g, int *out, int max) {
    int n = 0;
    for (int i = 0; i < g->inv.item_count && n < max; i++) {
        const Item *it = &g->inv.items[i];
        if (it->def >= g_item_count) {
            continue;
        }
        if (slot_compatible(s_slot, (int)g_items[it->def].slot)) {
            out[n++] = i;
        }
    }
    return n;
}

static void draw_candidate(App *app, Hero *h, Rect r, int inv_i) {
    Ui *ui = &app->ui;
    const Item *it = &app->game.inv.items[inv_i];
    ui_push_id(ui, inv_i);
    UiClick c = ui_item_slot(ui, r, &app->game, &h->eq[s_slot], it, false);
    char err[96] = "";
    if (!hero_can_use(h, &g_items[it->def], err)) {
        gfx_fill_rect(ui->gfx, r, RGBA(40, 0, 0, 130));
    }
    if (c.clicked) {
        bool ok = hero_equip(&app->game, app->sel_hero, inv_i, (EquipSlot)s_slot, err);
        ui_result(app, ok, err, NULL);
    }
    ui_pop_id(ui);
}

static void draw_candidates(App *app, Hero *h, Rect r) {
    Ui *ui = &app->ui;
    char title[80];
    str_format(title, sizeof title, "%s: %s", tr("Passend im Lager"), tr(g_equip_slot_names[s_slot]));
    Rect in = rect_inset(ui_panel(ui, r, title), ui_px(ui, 8));
    draw_unequip_row(app, h, rect_cut_top(&in, ui_px(ui, 36)));
    static int idx[INV_MAX_ITEMS];
    int n = collect_candidates(&app->game, idx, INV_MAX_ITEMS);
    if (n == 0) {
        ui_label_wrapped(ui, rect_make(in.x, in.y, in.w, ui_px(ui, 60)),
                         tr("Keine passenden Gegenstaende im Lager."), COL_INK_SOFT, fsz(ui, FS_SMALL));
        return;
    }
    int cs = ui_px(ui, 52), gap = ui_px(ui, 6);
    int bar = ui_px(ui, 10);
    int cols = maxi(1, (in.w - bar) / (cs + gap));
    int rows = (n + cols - 1) / cols;
    int off = ui_scroll_begin(ui, in, rows * (cs + gap), "hero_cands");
    for (int k = 0; k < n; k++) {
        Rect cell = rect_make(in.x + (k % cols) * (cs + gap) + 2, in.y + (k / cols) * (cs + gap) + 2 - off, cs, cs);
        if (cell.y + cs < in.y || cell.y > in.y + in.h) {
            continue;
        }
        draw_candidate(app, h, cell, idx[k]);
    }
    ui_scroll_end(ui);
}

/* ---- Stats (rechts) ------------------------------------------------------ */
static void draw_stat_row(App *app, Hero *h, Rect row, int stat, const char *name, int value) {
    Ui *ui = &app->ui;
    char b[32];
    ui_push_id(ui, stat);
    ui_text(ui, row.x, row.y + ui_px(ui, 6), name, COL_INK, FS_NORMAL);
    ui_text(ui, row.x + ui_px(ui, 60), row.y + ui_px(ui, 6), ui_num(value, b, sizeof b), COL_INK, FS_NORMAL);
    int bs = ui_px(ui, 28);
    Rect br = rect_make(row.x + row.w - bs, row.y, bs, bs);
    int amount = app->ui.in.shift ? mini(10, h->free_points) : 1;
    if (ui_button_tip(ui, br, "+", UI_BTN_GREEN, h->free_points > 0,
                      tr("Klick: +1 Punkt, Umschalt: +10 Punkte"))) {
        bool ok = hero_spend_point(&app->game, app->sel_hero, stat, maxi(amount, 1));
        ui_result(app, ok, tr("Keine freien Punkte"), NULL);
    }
    ui_pop_id(ui);
}

static void draw_value_line(Ui *ui, Rect *r, const char *name, const char *val) {
    int lh = ui_px(ui, 19);
    ui_text(ui, r->x, r->y, name, COL_INK_SOFT, FS_SMALL);
    ui_text_right(ui, rect_make(r->x, r->y, r->w, lh), val, COL_INK, FS_SMALL);
    r->y += lh;
    r->h -= lh;
}

static void draw_derived(App *app, Hero *h, Rect r) {
    Ui *ui = &app->ui;
    const DerivedStats *s = hero_stats(&app->game, h);
    char b[48];
#define LINE(label, ...) draw_value_line(ui, &r, tr(label), str_format(b, sizeof b, __VA_ARGS__))
    LINE("Leben", "%.0f", s->max_hp);
    LINE("Mana", "%.0f", s->max_mana);
    LINE("Lebensregeneration", "%.1f/s", s->hp_regen);
    LINE("Manaregeneration", "%.1f/s", s->mana_regen);
    LINE("Ruestung", "%.0f", s->armor);
    LINE("Zauberwiderstand", "%.0f", s->spell_res);
    LINE("Schadensreduktion", "%.1f %%", s->dmg_reduce * 100.0f);
    LINE("Angriffsschaden", "%.0f", s->attack_power * s->atk_mult);
    LINE("Zauberschaden", "%.0f", s->spell_power * s->spell_mult);
    LINE("Krit-Chance", "%.1f %%", s->crit_chance);
    LINE("Krit-Schaden", "%.0f %%", s->crit_dmg);
    LINE("Angriffstempo", "%.2f/s", s->attack_speed);
    LINE("Zaubertempo", "x%.2f", s->cast_speed);
    LINE("Lauftempo", "%.0f", s->move_speed);
    LINE("Abklingzeit-Red.", "%.1f %%", s->cdr);
    LINE("Reichweite", "%.0f", s->range);
    LINE("Kistenkapazitaet", "%d", s->chest_capacity);
#undef LINE
}

static void draw_stats(App *app, Hero *h, Rect r) {
    Ui *ui = &app->ui;
    Rect in = rect_inset(ui_panel(ui, r, tr("Werte")), ui_px(ui, 10));
    char b[96], n1[24], n2[24];
    str_format(b, sizeof b, "%s %s - %s %d", h->name, ui_class_name(h->cls), tr("Level"), h->level);
    ui_text(ui, in.x, in.y, b, COL_INK, FS_NORMAL);
    in.y += ui_px(ui, 24);
    u64 need = hero_exp_for_level(h->level);
    str_format(b, sizeof b, "%s / %s", ui_num((i64)h->exp, n1, sizeof n1), ui_num((i64)need, n2, sizeof n2));
    ui_bar(ui, rect_make(in.x, in.y, in.w, ui_px(ui, 18)), need ? (float)h->exp / (float)need : 0.0f, COL_EXP,
           COL_WOOD_DARK, b);
    in.y += ui_px(ui, 28);
    str_format(b, sizeof b, "%s: %d", tr("Freie Punkte"), h->free_points);
    ui_text(ui, in.x, in.y, b, h->free_points > 0 ? COL_WAX_RED : COL_INK_SOFT, FS_NORMAL);
    in.y += ui_px(ui, 26);
    int rh = ui_px(ui, 30);
    draw_stat_row(app, h, rect_make(in.x, in.y, in.w, rh), 0, "STR", h->str);
    draw_stat_row(app, h, rect_make(in.x, in.y + rh, in.w, rh), 1, "DEX", h->dex);
    draw_stat_row(app, h, rect_make(in.x, in.y + 2 * rh, in.w, rh), 2, "INT", h->intel);
    in.y += 3 * rh + ui_px(ui, 8);
    gfx_fill_rect(ui->gfx, rect_make(in.x, in.y, in.w, 1), COL_INK_SOFT);
    in.y += ui_px(ui, 6);
    int reset_h = ui_px(ui, 34);
    draw_derived(app, h, rect_make(in.x, in.y, in.w, r.y + r.h - in.y - reset_h - ui_px(ui, 16)));
    Rect rb = rect_make(in.x, r.y + r.h - reset_h - ui_px(ui, 10), in.w, reset_h);
    str_format(b, sizeof b, "%s (%d %s)", tr("Punkte zuruecksetzen"), STAT_RESET_COST, tr("Diamanten"));
    if (ui_button_ex(ui, rb, b, UI_BTN_WAX, h->str + h->dex + h->intel > 0)) {
        s_reset_open = true;
    }
}

void screen_hero(App *app, Rect area) {
    Ui *ui = &app->ui;
    gfx_fill_rect(ui->gfx, area, COL_WOOD_DARK);
    app_ensure_hero(app);
    int pad = ui_px(ui, 10);
    Rect r = rect_inset(area, pad);
    ui_hero_picker(app, rect_cut_top(&r, ui_px(ui, 32)));
    r.y += pad / 2;
    r.h -= pad / 2;
    Hero *h = app_hero(app);
    if (!h) {
        return;
    }
    int lw = mini(ui_px(ui, 330), r.w / 3);
    int rw = mini(ui_px(ui, 360), r.w / 3);
    Rect left = rect_cut_left(&r, lw);
    rect_cut_left(&r, pad);
    Rect right = rect_cut_right(&r, rw);
    rect_cut_right(&r, pad);
    draw_equipment(app, h, left);
    draw_candidates(app, h, r);
    draw_stats(app, h, right);
    int res = ui_confirm(ui, &s_reset_open, tr("Punkte zuruecksetzen"),
                         tr("Alle verteilten Punkte werden gegen Diamanten zurueckgesetzt. Fortfahren?"));
    if (res == 1) {
        char err[96] = "";
        bool ok = hero_reset_stats(&app->game, app->sel_hero, err);
        ui_result(app, ok, err, tr("Punkte zurueckgesetzt"));
    }
}
