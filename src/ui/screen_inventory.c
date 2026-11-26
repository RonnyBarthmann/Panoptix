/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Inventar: Tabs, Raster mit Suche/Filter, Detailansicht mit Aktionen, Kisten oeffnen,
 * Zerlege-Filter und Kapazitaetsanzeige. */
#include <string.h>

#include "ui/ui_internal.h"

#define TAB_COUNT 9
#define TAB_CHESTS 0
#define LVL_STEPS 13

typedef struct Entry {
    int inv; /* Index in inv.items oder -1 bei Stapeln */
    int def;
} Entry;

static int s_tab = 1;
static int s_sel_inv = -1;
static int s_sel_def = -1;
static int s_f_tier, s_f_lvl, s_f_cls;
static char s_search[32];
static bool s_conf_open;
static bool s_res_open;
static ChestResult s_res;
static int s_res_opened;
static Entry s_entries[INV_MAX_ITEMS + MAX_ITEM_DEFS];

/* ---- Tabs / Filter ------------------------------------------------------ */
static bool tab_has_kind(int tab, ItemKind k) {
    switch (tab) {
    case 1: return k == IK_MATERIAL || k == IK_SPECIAL;
    case 2: return k == IK_WEAPON;
    case 3: return k == IK_SUB;
    case 4: return k == IK_ARMOR;
    case 5: return k == IK_POTION;
    case 6: return k == IK_RING || k == IK_NECKLACE;
    case 7: return k == IK_WINGS || k == IK_BAG;
    case 8: return k == IK_STONE;
    default: return false;
    }
}

static char lower_ascii(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static bool name_matches(const char *name, const char *needle) {
    size_t nl = strlen(needle);
    if (nl == 0) {
        return true;
    }
    for (size_t i = 0; name[i]; i++) {
        size_t j = 0;
        while (j < nl && name[i + j] && lower_ascii(name[i + j]) == lower_ascii(needle[j])) {
            j++;
        }
        if (j == nl) {
            return true;
        }
    }
    return false;
}

static bool def_passes(int def) {
    const ItemDef *d = &g_items[def];
    if (s_f_tier > 0 && (int)d->tier != s_f_tier - 1) {
        return false;
    }
    if (s_f_lvl > 0 && d->min_level < s_f_lvl * 10) {
        return false;
    }
    if (s_f_cls > 0 && d->cls != CLASS_ANY && d->cls != s_f_cls - 1) {
        return false;
    }
    return name_matches(d->name, s_search);
}

static int build_entries(const Game *g) {
    int n = 0;
    for (int i = 0; i < g->inv.item_count; i++) {
        int def = g->inv.items[i].def;
        if (def < g_item_count && tab_has_kind(s_tab, g_items[def].kind) && def_passes(def)) {
            s_entries[n].inv = i;
            s_entries[n++].def = def;
        }
    }
    for (int def = 0; def < g_item_count; def++) {
        if (g->inv.stock[def] > 0 && tab_has_kind(s_tab, g_items[def].kind) && def_passes(def)) {
            s_entries[n].inv = -1;
            s_entries[n++].def = def;
        }
    }
    return n;
}

static void clear_selection(void) {
    s_sel_inv = -1;
    s_sel_def = -1;
}

static void validate_selection(const Game *g) {
    if (s_sel_inv >= 0 && (s_sel_inv >= g->inv.item_count || !tab_has_kind(s_tab, g_items[g->inv.items[s_sel_inv].def].kind))) {
        clear_selection();
    }
    if (s_sel_def >= 0 && (g->inv.stock[s_sel_def] == 0 || !tab_has_kind(s_tab, g_items[s_sel_def].kind))) {
        clear_selection();
    }
}

/* ---- Kopfzeile: Tabs + Filter -------------------------------------------- */
static void draw_tabs(Ui *ui, Rect r) {
    const char *labels[TAB_COUNT] = {tr("Kisten"), tr("Materialien"), tr("Waffen"), tr("Sekundaer"), tr("Ruestung"),
                                     tr("Traenke"), tr("Schmuck"), tr("Fluegel/Taschen"), tr("Steine")};
    if (ui_tabs(ui, r, labels, TAB_COUNT, &s_tab)) {
        clear_selection();
    }
}

static void fill_level_options(char bufs[LVL_STEPS][24], const char *opts[LVL_STEPS], const char *first, const char *fmt) {
    for (int i = 0; i < LVL_STEPS; i++) {
        if (i == 0) {
            str_copy(bufs[0], 24, first);
        } else {
            str_format(bufs[i], 24, fmt, i * 10);
        }
        opts[i] = bufs[i];
    }
}

static void fill_tier_options(char bufs[TIER_COUNT + 1][40], const char *opts[TIER_COUNT + 1], const char *first,
                              const char *prefix) {
    str_copy(bufs[0], 40, first);
    opts[0] = bufs[0];
    for (int t = 0; t < TIER_COUNT; t++) {
        str_format(bufs[t + 1], 40, "%s%s", prefix, tr(g_tiers[t].name));
        opts[t + 1] = bufs[t + 1];
    }
}

static void draw_filter_row(App *app, Rect r) {
    Ui *ui = &app->ui;
    int gap = ui_px(ui, 6);
    ui_text_input(ui, rect_cut_left(&r, ui_px(ui, 170)), s_search, sizeof s_search, "inv_search");
    rect_cut_left(&r, gap);
    char tb[TIER_COUNT + 1][40], lb[LVL_STEPS][24];
    const char *to[TIER_COUNT + 1], *lo[LVL_STEPS], *co[CLASS_COUNT + 1];
    fill_tier_options(tb, to, tr("Alle Tiers"), "");
    fill_level_options(lb, lo, tr("Alle Level"), "Lv ab %d");
    co[0] = tr("Alle Klassen");
    for (int c = 0; c < CLASS_COUNT; c++) {
        co[c + 1] = tr(g_classes[c].name);
    }
    ui_push_id(ui, 1);
    ui_cycle(ui, rect_cut_left(&r, ui_px(ui, 120)), to, TIER_COUNT + 1, &s_f_tier);
    rect_cut_left(&r, gap);
    ui_push_id(ui, 2);
    ui_cycle(ui, rect_cut_left(&r, ui_px(ui, 110)), lo, LVL_STEPS, &s_f_lvl);
    rect_cut_left(&r, gap);
    ui_pop_id(ui);
    ui_push_id(ui, 3);
    ui_cycle(ui, rect_cut_left(&r, ui_px(ui, 130)), co, CLASS_COUNT + 1, &s_f_cls);
    ui_pop_id(ui);
    ui_pop_id(ui);
    rect_cut_left(&r, gap);
    if (ui_button_tip(ui, rect_cut_left(&r, ui_px(ui, 90)), tr("Sortieren"), UI_BTN_NORMAL, true,
                      tr("Tier absteigend, Level absteigend, Name"))) {
        inv_sort(&app->game);
        clear_selection();
    }
}

/* ---- Raster ----------------------------------------------------------------- */
static void draw_lock_mark(Ui *ui, Rect r) {
    int s = ui_px(ui, 9);
    Rect m = rect_make(r.x + r.w - s - 2, r.y + 2, s, s);
    gfx_fill_rect(ui->gfx, m, COL_WAX_RED);
    gfx_draw_rect(ui->gfx, m, COL_GOLD, 1);
}

static bool equip_selected(App *app, int inv_index);

static void draw_grid_cell(App *app, Rect cell, const Entry *e) {
    Ui *ui = &app->ui;
    Game *g = &app->game;
    UiClick c;
    if (e->inv >= 0) {
        const Item *it = &g->inv.items[e->inv];
        c = ui_item_slot(ui, cell, g, app_hero(app), it, s_sel_inv == e->inv);
        if (it->flags & ITEMF_LOCKED) {
            draw_lock_mark(ui, cell);
        }
        if (c.clicked) {
            s_sel_inv = e->inv;
            s_sel_def = -1;
        }
        if (c.double_clicked || c.rclicked) {
            equip_selected(app, e->inv);
        }
    } else {
        c = ui_stack_slot(ui, cell, e->def, g->inv.stock[e->def], s_sel_def == e->def);
        if (c.clicked) {
            s_sel_def = e->def;
            s_sel_inv = -1;
        }
    }
}

static void draw_grid(App *app, Rect r) {
    Ui *ui = &app->ui;
    int n = build_entries(&app->game);
    if (n == 0) {
        ui_label_wrapped(ui, rect_make(r.x, r.y, r.w, ui_px(ui, 40)), tr("Keine passenden Gegenstaende."), COL_INK_SOFT,
                         fsz(ui, FS_NORMAL));
        return;
    }
    int cs = ui_px(ui, 52), gap = ui_px(ui, 6), bar = ui_px(ui, 12);
    int cols = maxi(1, (r.w - bar) / (cs + gap));
    int rows = (n + cols - 1) / cols;
    int off = ui_scroll_begin(ui, r, rows * (cs + gap) + gap, "inv_grid");
    for (int k = 0; k < n; k++) {
        Rect cell = rect_make(r.x + gap + (k % cols) * (cs + gap), r.y + gap + (k / cols) * (cs + gap) - off, cs, cs);
        if (cell.y + cs < r.y || cell.y > r.y + r.h) {
            continue;
        }
        ui_push_id(ui, k);
        draw_grid_cell(app, cell, &s_entries[k]);
        ui_pop_id(ui);
    }
    ui_scroll_end(ui);
}

/* ---- Aktionen ----------------------------------------------------------------- */
static bool equip_selected(App *app, int inv_index) {
    char err[96] = "";
    if (!app_has_hero(app) || inv_index < 0) {
        return false;
    }
    bool ok = hero_equip(&app->game, app->sel_hero, inv_index, SLOT_NONE, err);
    ui_result(app, ok, err, tr("Angelegt"));
    if (ok) {
        clear_selection();
    }
    return ok;
}

static void act_dismantle(App *app) {
    char err[96] = "";
    bool ok = item_dismantle_at(&app->game, s_sel_inv, err);
    ui_result(app, ok, err, tr("Zerlegt"));
    if (ok) {
        clear_selection();
    }
}

static void act_sell(App *app) {
    char err[96] = "";
    bool ok = item_sell_at(&app->game, s_sel_inv, err);
    ui_result(app, ok, err, tr("Verkauft"));
    if (ok) {
        clear_selection();
    }
}

static void draw_action_buttons(App *app, Rect r, const Item *it) {
    Ui *ui = &app->ui;
    int bh = ui_px(ui, 32), gap = ui_px(ui, 6);
    int bw = (r.w - gap) / 2;
    bool locked = (it->flags & ITEMF_LOCKED) != 0;
    char lab[64], sell[48], hero[48];
    Rect row1 = rect_make(r.x, r.y + r.h - 2 * bh - gap, r.w, bh);
    Rect row2 = rect_make(r.x, r.y + r.h - bh, r.w, bh);
    const Hero *h = app_hero(app);
    str_format(hero, sizeof hero, "%s: %s", tr("Anlegen"), h ? h->name : "-");
    if (ui_button_ex(ui, row1, hero, UI_BTN_GREEN, h != NULL)) {
        equip_selected(app, s_sel_inv);
        return;
    }
    str_format(sell, sizeof sell, "%s (%lld)", tr("Verkaufen"), (long long)item_sell_value(it));
    Rect b1 = rect_make(row2.x, row2.y - bh - gap, bw, bh);
    Rect b2 = rect_make(row2.x + bw + gap, b1.y, bw, bh);
    if (ui_button_ex(ui, b1, tr("Zerlegen"), UI_BTN_WAX, !locked)) {
        act_dismantle(app);
        return;
    }
    if (ui_button_ex(ui, b2, sell, UI_BTN_WAX, !locked)) {
        act_sell(app);
        return;
    }
    str_copy(lab, sizeof lab, locked ? tr("Entsperren") : tr("Sperren"));
    if (ui_button_ex(ui, row2, lab, UI_BTN_NORMAL, true)) {
        item_toggle_lock(&app->game.inv.items[s_sel_inv]);
    }
}

/* ---- Detail rechts ---------------------------------------------------------------- */
static void draw_detail_text(Ui *ui, Rect r, const char *name, Color col, const char *body) {
    ui_text(ui, r.x, r.y, name, col, FS_BIG);
    int y = ui_px(ui, 30);
    gfx_push_clip(ui->gfx, r);
    gfx_text_wrapped(ui->gfx, rect_make(r.x, r.y + y, r.w, r.h - y), body, COL_INK, fsz(ui, FS_SMALL), true);
    gfx_pop_clip(ui->gfx);
}

static void draw_detail_item(App *app, Rect in) {
    Game *g = &app->game;
    const Item *it = &g->inv.items[s_sel_inv];
    char name[96], body[1024];
    item_display_name(it, name, sizeof name);
    item_describe(g, it, body, sizeof body);
    Rect text = rect_make(in.x, in.y, in.w, in.h - ui_px(&app->ui, 32 * 3 + 12));
    draw_detail_text(&app->ui, text, name, ui_tier(app, g_items[it->def].tier, 255), body);
    draw_action_buttons(app, in, it);
}

static void draw_detail_stack(App *app, Rect in) {
    Ui *ui = &app->ui;
    const ItemDef *d = &g_items[s_sel_def];
    char body[1024], b2[1100], n[24];
    itemdef_describe(d, body, sizeof body);
    str_format(b2, sizeof b2, "%s: %s\n%s", tr("Anzahl"), ui_num(app->game.inv.stock[s_sel_def], n, sizeof n), body);
    draw_detail_text(ui, in, d->name, ui_tier(app, d->tier, 255), b2);
}

static void draw_detail(App *app, Rect r) {
    Ui *ui = &app->ui;
    Rect in = rect_inset(ui_panel(ui, r, tr("Details")), ui_px(ui, 10));
    if (s_sel_inv >= 0) {
        draw_detail_item(app, in);
    } else if (s_sel_def >= 0) {
        draw_detail_stack(app, in);
    } else {
        ui_label_wrapped(ui, in, tr("Gegenstand waehlen. Doppelklick oder Rechtsklick legt an den gewaehlten Helden an."),
                         COL_INK_SOFT, fsz(ui, FS_SMALL));
    }
}

/* ---- Fuss: Kapazitaet + Zerlege-Filter ------------------------------------------------ */
static void draw_capacity(App *app, Rect r) {
    Ui *ui = &app->ui;
    const Inventory *inv = &app->game.inv;
    char b[64];
    str_format(b, sizeof b, "%s %d / %d", tr("Lager"), inv->item_count, inv->capacity);
    float f = inv->capacity > 0 ? (float)inv->item_count / (float)inv->capacity : 0.0f;
    Color c = f > 0.9f ? COL_HP : COL_MOSS;
    ui_progress_bar(ui, r, f, c, b);
}

static void draw_dismantle_row(App *app, Rect r) {
    Ui *ui = &app->ui;
    Inventory *inv = &app->game.inv;
    int gap = ui_px(ui, 6);
    ui_text(ui, r.x, r.y + ui_px(ui, 8), tr("Zerlege-Filter:"), COL_PARCHMENT, FS_SMALL);
    rect_cut_left(&r, ui_px(ui, 110));
    char tb[TIER_COUNT + 1][40], lb[LVL_STEPS][24];
    const char *to[TIER_COUNT + 1], *lo[LVL_STEPS];
    fill_tier_options(tb, to, tr("Aus"), tr("unter "));
    fill_level_options(lb, lo, tr("Level egal"), "unter Lv %d");
    int t = inv->dismantle_below_tier, l = inv->dismantle_below_level;
    ui_push_id(ui, 11);
    ui_cycle(ui, rect_cut_left(&r, ui_px(ui, 150)), to, TIER_COUNT, &t);
    rect_cut_left(&r, gap);
    ui_pop_id(ui);
    ui_push_id(ui, 12);
    ui_cycle(ui, rect_cut_left(&r, ui_px(ui, 130)), lo, LVL_STEPS, &l);
    ui_pop_id(ui);
    inv->dismantle_below_tier = (u8)clampi(t, 0, TIER_COUNT);
    inv->dismantle_below_level = (u8)clampi(l, 0, LVL_STEPS - 1);
    rect_cut_left(&r, gap);
    if (ui_button_tip(ui, rect_cut_left(&r, ui_px(ui, 170)), tr("Zerlegen nach Filter"), UI_BTN_WAX,
                      inv->dismantle_below_tier > 0, tr("Zerlegt Gegenstaende unterhalb der Filterwerte (ohne gesperrte, verbesserte, verzauberte)"))) {
        s_conf_open = true;
    }
}

static void handle_dismantle_confirm(App *app) {
    int res = ui_confirm(&app->ui, &s_conf_open, tr("Zerlegen nach Filter"),
                         tr("Alle passenden, nicht gesperrten Gegenstaende werden zerlegt. Fortfahren?"));
    if (res == 1) {
        Inventory *inv = &app->game.inv;
        int n = inv_dismantle_filtered(&app->game, inv->dismantle_below_tier, inv->dismantle_below_level * 10);
        app_message(app, "%d %s", n, tr("Gegenstaende zerlegt"));
        app_play(app, SFX_CLICK);
        clear_selection();
    }
}

/* ---- Kisten-Tab ------------------------------------------------------------------------ */
static void open_chests(App *app, Tier t, int count) {
    memset(&s_res, 0, sizeof s_res);
    s_res_opened = chest_open_from_storage(&app->game, t, count, &s_res);
    s_res.tier = t;
    if (s_res_opened > 0) {
        s_res_open = true;
        app_play(app, SFX_CLICK);
    } else {
        app_message(app, "%s", tr("Keine Kisten geoeffnet"));
        app_play(app, SFX_ERROR);
    }
}

static void draw_chest_row(App *app, Rect row, int t) {
    Ui *ui = &app->ui;
    u32 have = app->game.inv.chests[t];
    char b[64];
    ui_push_id(ui, t);
    gfx_fill_rect(ui->gfx, row, COL_PARCHMENT_DARK);
    gfx_draw_rect(ui->gfx, row, COL_WOOD, 1);
    int is = row.h - ui_px(ui, 8);
    icon_draw_chest(ui->gfx, rect_make(row.x + ui_px(ui, 6), row.y + ui_px(ui, 4), is, is), (Tier)t, ui->time);
    ui_text(ui, row.x + is + ui_px(ui, 18), row.y + row.h / 2 - ui_px(ui, 10), tr(g_tiers[t].name), COL_INK, FS_BIG);
    str_format(b, sizeof b, "%s: %u / %u", tr("Vorrat"), have, inv_chest_storage_limit(&app->game, (Tier)t));
    ui_text(ui, row.x + ui_px(ui, 200), row.y + row.h / 2 - ui_px(ui, 8), b, COL_INK, FS_NORMAL);
    int bw = ui_px(ui, 130), bh = ui_px(ui, 34), by = row.y + (row.h - bh) / 2;
    Rect b_all = rect_make(row.x + row.w - bw - ui_px(ui, 8), by, bw, bh);
    Rect b_one = rect_make(b_all.x - bw - ui_px(ui, 8), by, bw, bh);
    if (ui_button_ex(ui, b_one, tr("Oeffnen (1)"), UI_BTN_GREEN, have > 0)) {
        open_chests(app, (Tier)t, 1);
    }
    if (ui_button_ex(ui, b_all, tr("Alle oeffnen"), UI_BTN_WAX, have > 0)) {
        open_chests(app, (Tier)t, (int)have);
    }
    ui_pop_id(ui);
}

static void draw_chests_tab(App *app, Rect r) {
    Ui *ui = &app->ui;
    Rect in = rect_inset(ui_panel(ui, r, tr("Kistenvorrat")), ui_px(ui, 10));
    int gap = ui_px(ui, 6);
    int rh = mini(ui_px(ui, 64), (in.h - gap * (TIER_COUNT - 1)) / TIER_COUNT);
    for (int t = 0; t < TIER_COUNT; t++) {
        draw_chest_row(app, rect_make(in.x, in.y + t * (rh + gap), in.w, rh), t);
    }
}

/* ---- Ergebnisfenster ---------------------------------------------------------------------- */
static void draw_card_frame(Ui *ui, Rect c, Color border) {
    gfx_fill_rect(ui->gfx, c, COL_WOOD_DARK);
    gfx_draw_rect(ui->gfx, c, border, 1);
}

static void draw_result_item(App *app, Rect c, const Item *it) {
    Ui *ui = &app->ui;
    char name[96];
    Color col = ui_tier(app, g_items[it->def].tier, 255);
    draw_card_frame(ui, c, col);
    int s = c.h - ui_px(ui, 8);
    ui_item_slot(ui, rect_make(c.x + ui_px(ui, 4), c.y + ui_px(ui, 4), s, s), &app->game, app_hero(app), it, false);
    gfx_push_clip(ui->gfx, rect_make(c.x + s + ui_px(ui, 10), c.y, c.w - s - ui_px(ui, 12), c.h));
    ui_text(ui, c.x + s + ui_px(ui, 10), c.y + c.h / 2 - ui_px(ui, 8), item_display_name(it, name, sizeof name), col, FS_SMALL);
    gfx_pop_clip(ui->gfx);
}

static void draw_result_stack(App *app, Rect c, int def, u32 count) {
    Ui *ui = &app->ui;
    char b[96];
    Color col = ui_tier(app, g_items[def].tier, 255);
    draw_card_frame(ui, c, col);
    int s = c.h - ui_px(ui, 8);
    ui_stack_slot(ui, rect_make(c.x + ui_px(ui, 4), c.y + ui_px(ui, 4), s, s), def, count, false);
    str_format(b, sizeof b, "%s", g_items[def].name);
    gfx_push_clip(ui->gfx, rect_make(c.x + s + ui_px(ui, 10), c.y, c.w - s - ui_px(ui, 12), c.h));
    ui_text(ui, c.x + s + ui_px(ui, 10), c.y + c.h / 2 - ui_px(ui, 8), b, col, FS_SMALL);
    gfx_pop_clip(ui->gfx);
}

static int result_card_count(void) { return s_res.item_count + s_res.stack_count; }

static void draw_result_cards(App *app, Rect r) {
    Ui *ui = &app->ui;
    int gap = ui_px(ui, 6), ch = ui_px(ui, 54), bar = ui_px(ui, 12);
    int cols = maxi(1, (r.w - bar) / ui_px(ui, 200));
    int cw = (r.w - bar - gap * (cols - 1)) / cols;
    int n = result_card_count();
    int rows = (n + cols - 1) / cols;
    int off = ui_scroll_begin(ui, r, rows * (ch + gap), "inv_result");
    for (int k = 0; k < n; k++) {
        Rect c = rect_make(r.x + (k % cols) * (cw + gap), r.y + (k / cols) * (ch + gap) - off, cw, ch);
        ui_push_id(ui, k);
        if (k < s_res.item_count) {
            draw_result_item(app, c, &s_res.items[k]);
        } else {
            int s = k - s_res.item_count;
            draw_result_stack(app, c, s_res.stacks[s].def, s_res.stacks[s].count);
        }
        ui_pop_id(ui);
    }
    ui_scroll_end(ui);
}

static void draw_result_notes(Ui *ui, Rect r) {
    char b[128];
    if (s_res.book_count > 0) {
        str_format(b, sizeof b, "%s: %d", tr("Skillbuecher gefunden"), s_res.book_count);
        ui_text(ui, r.x, r.y, b, COL_INK, FS_SMALL);
    }
    if (s_res.lost_items > 0) {
        str_format(b, sizeof b, "%s: %d", tr("Lager voll - verworfen"), s_res.lost_items);
        ui_text(ui, r.x, r.y + ui_px(ui, 16), b, COL_WAX_RED, FS_SMALL);
    }
}

static void draw_result_window(App *app) {
    Ui *ui = &app->ui;
    Gfx *g = ui->gfx;
    gfx_fill_rect(g, rect_make(0, 0, g->w, g->h), RGBA(0, 0, 0, 150));
    int w = mini(ui_px(ui, 680), g->w - ui_px(ui, 40)), h = mini(ui_px(ui, 460), g->h - ui_px(ui, 40));
    char title[96];
    str_format(title, sizeof title, "%d x %s - %s", s_res_opened, tr(g_tiers[s_res.tier].name), tr("Ergebnis"));
    Rect in = rect_inset(ui_panel(ui, rect_make((g->w - w) / 2, (g->h - h) / 2, w, h), title), ui_px(ui, 10));
    int bh = ui_px(ui, 34);
    Rect ok = rect_make(in.x + in.w / 2 - ui_px(ui, 60), in.y + in.h - bh, ui_px(ui, 120), bh);
    Rect notes = rect_make(in.x, ok.y - ui_px(ui, 38), in.w, ui_px(ui, 34));
    draw_result_cards(app, rect_make(in.x, in.y, in.w, notes.y - in.y));
    draw_result_notes(ui, notes);
    if (ui_button_ex(ui, ok, tr("OK"), UI_BTN_GREEN, true) || ui->in.key_pressed[KEY_ESCAPE] ||
        ui->in.key_pressed[KEY_ENTER]) {
        s_res_open = false;
    }
}

/* ---- Hauptfunktion ---------------------------------------------------------------------------- */
static void draw_items_tab(App *app, Rect r) {
    Ui *ui = &app->ui;
    int dw = mini(ui_px(ui, 340), r.w / 3);
    Rect right = rect_cut_right(&r, dw);
    rect_cut_right(&r, ui_px(ui, 10));
    draw_detail(app, right);
    Rect in = rect_inset(ui_panel(ui, r, tr("Lager")), ui_px(ui, 8));
    draw_filter_row(app, rect_cut_top(&in, ui_px(ui, 30)));
    rect_cut_top(&in, ui_px(ui, 6));
    draw_grid(app, in);
}

static void draw_footer(App *app, Rect r) {
    int gap = ui_px(&app->ui, 10);
    draw_capacity(app, rect_cut_left(&r, mini(ui_px(&app->ui, 260), r.w / 3)));
    rect_cut_left(&r, gap);
    draw_dismantle_row(app, r);
}

void screen_inventory(App *app, Rect area) {
    Ui *ui = &app->ui;
    gfx_fill_rect(ui->gfx, area, COL_WOOD_DARK);
    app_ensure_hero(app);
    int pad = ui_px(ui, 10);
    Rect r = rect_inset(area, pad);
    ui->modal_open = s_res_open || s_conf_open;
    ui_hero_picker(app, rect_cut_top(&r, ui_px(ui, 32)));
    rect_cut_top(&r, ui_px(ui, 6));
    draw_tabs(ui, rect_cut_top(&r, ui_px(ui, 32)));
    rect_cut_top(&r, ui_px(ui, 6));
    Rect foot = rect_cut_bottom(&r, ui_px(ui, 30));
    rect_cut_bottom(&r, ui_px(ui, 6));
    validate_selection(&app->game);
    if (s_tab == TAB_CHESTS) {
        draw_chests_tab(app, r);
    } else {
        draw_items_tab(app, r);
    }
    draw_footer(app, foot);
    if (s_res_open) {
        ui->modal_open = false;
        draw_result_window(app);
        ui->modal_open = s_res_open;
    }
    handle_dismantle_confirm(app);
}
