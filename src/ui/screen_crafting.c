/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Schmiede: Herstellen, Verbessern, Verzaubern, Umwandeln (Spec 12, Spec 18 Abschnitt 8). */
#include <string.h>

#include "ui/ui_internal.h"

#define TAB_MAKE 0
#define TAB_ENHANCE 1
#define TAB_ENCHANT 2
#define TAB_CONVERT 3
#define CAT_COUNT 5
#define LVL_STEPS 13
#define QTY_MAX 999
#define ROLL_TIME 1.1f
#define CONV_SLOTS 64

static int s_tab = TAB_MAKE;

/* Herstellen */
static int s_cat, s_f_tier, s_f_cls, s_f_lvl;
static int s_sel_def = -1;
static int s_qty = 1;
static int s_list[MAX_ITEM_DEFS];

/* Verbessern / Verzaubern: Ziel = Angelegtes (Slot) oder Lagerindex */
static int s_tgt_inv = -1;
static int s_tgt_slot = -1;
static char s_enh_msg[96];
static Color s_enh_col;
static float s_enh_ttl;
static float s_roll_t;
static bool s_seal;

/* Umwandeln */
static int s_conv_def = -1;
static int s_conv_times = 1;

static const ItemKind k_cat_kind[CAT_COUNT] = {IK_WEAPON, IK_SUB, IK_ARMOR, IK_POTION, IK_BAG};

/* ---- Gemeinsame Helfer ------------------------------------------------------ */
static void set_enh_msg(const char *text, Color c) {
    str_copy(s_enh_msg, sizeof s_enh_msg, text);
    s_enh_col = c;
    s_enh_ttl = 6.0f;
}

static void clear_target(void) {
    s_tgt_inv = -1;
    s_tgt_slot = -1;
    s_enh_msg[0] = '\0';
    s_roll_t = 0.0f;
}

static void draw_count_text(Ui *ui, Rect r, i64 have, i64 need, Color tier_col) {
    char b[64], h[24], n[24];
    str_format(b, sizeof b, "%s / %s", ui_num(have, h, sizeof h), ui_num(need, n, sizeof n));
    ui_text_right(ui, r, b, have >= need ? tier_col : COL_HP, FS_NORMAL);
}

/* Zeile "Name ... x/y" mit Stapel-Symbol. */
static void draw_stock_row(App *app, Rect row, int def, i64 need) {
    Ui *ui = &app->ui;
    const ItemDef *d = &g_items[def];
    i64 have = app->game.inv.stock[def];
    Color col = ui_tier(app, d->tier, 255);
    int is = row.h - ui_px(ui, 4);
    ui_stack_slot(ui, rect_make(row.x + 2, row.y + 2, is, is), def, have, false);
    ui_text(ui, row.x + is + ui_px(ui, 10), row.y + row.h / 2 - ui_px(ui, 8), d->name, col, FS_SMALL);
    draw_count_text(ui, rect_make(row.x, row.y, row.w - ui_px(ui, 6), row.h), have, need, col);
}

static void draw_gold_row(App *app, Rect row, i64 cost) {
    Ui *ui = &app->ui;
    char b[48], h[24], n[24];
    ui_text(ui, row.x + ui_px(ui, 4), row.y + row.h / 2 - ui_px(ui, 8), tr("Gold"), COL_WOOD, FS_NORMAL);
    str_format(b, sizeof b, "%s / %s", ui_num(app->game.player.gold, h, sizeof h), ui_num(cost, n, sizeof n));
    ui_text_right(ui, rect_make(row.x, row.y, row.w - ui_px(ui, 6), row.h), b,
                  app->game.player.gold >= cost ? COL_INK : COL_HP, FS_NORMAL);
}

/* Mengenzeile: [-] Zahl [+] [x10] [Max]; liefert true bei Aenderung. */
static void draw_qty_row(App *app, Rect r, int *qty, int max_qty) {
    Ui *ui = &app->ui;
    int bw = ui_px(ui, 30), gap = ui_px(ui, 4);
    char b[24];
    max_qty = clampi(max_qty, 1, QTY_MAX);
    ui_push_id(ui, 500);
    if (ui_button_ex(ui, rect_cut_left(&r, bw), "-", UI_BTN_NORMAL, *qty > 1)) {
        *qty -= (ui->in.shift ? 10 : 1);
    }
    str_format(b, sizeof b, "%d", *qty);
    ui_text_center(ui, rect_cut_left(&r, ui_px(ui, 56)), b, COL_INK, FS_BIG);
    if (ui_button_ex(ui, rect_cut_left(&r, bw), "+", UI_BTN_NORMAL, *qty < max_qty)) {
        *qty += (ui->in.shift ? 10 : 1);
    }
    rect_cut_left(&r, gap);
    if (ui_button_ex(ui, rect_cut_left(&r, ui_px(ui, 40)), "x10", UI_BTN_NORMAL, *qty < max_qty)) {
        *qty += 10;
    }
    rect_cut_left(&r, gap);
    if (ui_button_ex(ui, rect_cut_left(&r, ui_px(ui, 50)), tr("Max"), UI_BTN_NORMAL, true)) {
        *qty = max_qty;
    }
    ui_pop_id(ui);
    *qty = clampi(*qty, 1, max_qty);
}

static void fill_tier_options(char bufs[TIER_COUNT + 1][40], const char *opts[TIER_COUNT + 1]) {
    str_copy(bufs[0], 40, tr("Alle Tiers"));
    opts[0] = bufs[0];
    for (int t = 0; t < TIER_COUNT; t++) {
        str_copy(bufs[t + 1], 40, tr(g_tiers[t].name));
        opts[t + 1] = bufs[t + 1];
    }
}

/* ---- Herstellen: Liste -------------------------------------------------------- */
static bool make_def_passes(int def) {
    const ItemDef *d = &g_items[def];
    if (!d->craftable || d->kind != k_cat_kind[s_cat]) {
        return false;
    }
    if (s_f_tier > 0 && (int)d->tier != s_f_tier - 1) {
        return false;
    }
    if (s_f_lvl > 0 && d->min_level < s_f_lvl * 10) {
        return false;
    }
    return !(s_f_cls > 0 && d->cls != CLASS_ANY && d->cls != s_f_cls - 1);
}

static int build_make_list(void) {
    int n = 0;
    for (int def = 0; def < g_item_count; def++) {
        if (make_def_passes(def)) {
            s_list[n++] = def;
        }
    }
    return n;
}

static void draw_make_filters(Ui *ui, Rect r) {
    char tb[TIER_COUNT + 1][40], lb[LVL_STEPS][24];
    const char *to[TIER_COUNT + 1], *lo[LVL_STEPS], *co[CLASS_COUNT + 1];
    int gap = ui_px(ui, 4), w = (r.w - 2 * gap) / 3;
    fill_tier_options(tb, to);
    for (int i = 0; i < LVL_STEPS; i++) {
        if (i == 0) {
            str_copy(lb[0], 24, tr("Alle Level"));
        } else {
            str_format(lb[i], 24, "Lv ab %d", i * 10);
        }
        lo[i] = lb[i];
    }
    co[0] = tr("Alle Klassen");
    for (int c = 0; c < CLASS_COUNT; c++) {
        co[c + 1] = tr(g_classes[c].name);
    }
    ui_push_id(ui, 601);
    ui_cycle(ui, rect_cut_left(&r, w), to, TIER_COUNT + 1, &s_f_tier);
    rect_cut_left(&r, gap);
    ui_pop_id(ui);
    ui_push_id(ui, 602);
    ui_cycle(ui, rect_cut_left(&r, w), co, CLASS_COUNT + 1, &s_f_cls);
    rect_cut_left(&r, gap);
    ui_pop_id(ui);
    ui_push_id(ui, 603);
    ui_cycle(ui, r, lo, LVL_STEPS, &s_f_lvl);
    ui_pop_id(ui);
}

static void draw_make_row(App *app, Rect row, int def) {
    Ui *ui = &app->ui;
    const ItemDef *d = &g_items[def];
    Recipe rc;
    bool ok = craft_recipe(&app->game, def, &rc);
    bool locked = ok && !rc.unlocked;
    UiClick c = ui_region(ui, row, ui_make_id(ui, "mkrow", row.x, row.y));
    Color col = ui_tier(app, d->tier, 255);
    char b[24];
    gfx_fill_rect(ui->gfx, row, s_sel_def == def ? COL_PARCHMENT_LIGHT : (c.hovered ? COL_PARCHMENT : COL_PARCHMENT_DARK));
    gfx_draw_rect(ui->gfx, row, s_sel_def == def ? COL_GOLD : COL_WOOD, 1);
    gfx_fill_rect(ui->gfx, rect_make(row.x + 2, row.y + 2, ui_px(ui, 5), row.h - 4), col);
    ui_text(ui, row.x + ui_px(ui, 14), row.y + row.h / 2 - ui_px(ui, 8), d->name, locked ? COL_INK_SOFT : COL_INK, FS_SMALL);
    str_format(b, sizeof b, "%s%d", locked ? "x " : "Lv ", d->min_level);
    ui_text_right(ui, rect_make(row.x, row.y, row.w - ui_px(ui, 6), row.h), b, locked ? COL_WAX_RED : COL_INK_SOFT, FS_SMALL);
    if (c.clicked) {
        s_sel_def = def;
        s_qty = 1;
    }
}

static void draw_make_list(App *app, Rect r) {
    Ui *ui = &app->ui;
    int n = build_make_list();
    if (s_sel_def < 0 && n > 0) {
        s_sel_def = s_list[0];
    }
    if (n == 0) {
        ui_label_wrapped(ui, r, tr("Keine herstellbaren Gegenstaende."), COL_INK_SOFT, fsz(ui, FS_NORMAL));
        return;
    }
    int rh = ui_px(ui, 26), bar = ui_px(ui, 12);
    int off = ui_scroll_begin(ui, r, n * (rh + 2), "mk_list");
    for (int k = 0; k < n; k++) {
        Rect row = rect_make(r.x, r.y + k * (rh + 2) - off, r.w - bar, rh);
        if (row.y + rh < r.y || row.y > r.y + r.h) {
            continue;
        }
        ui_push_id(ui, k);
        draw_make_row(app, row, s_list[k]);
        ui_pop_id(ui);
    }
    ui_scroll_end(ui);
}

static void draw_make_left(App *app, Rect r) {
    Ui *ui = &app->ui;
    Rect in = rect_inset(ui_panel(ui, r, tr("Rezepte")), ui_px(ui, 8));
    const char *cats[CAT_COUNT] = {tr("Waffe"), tr("Sekundaer"), tr("Ruestung"), tr("Traenke"), tr("Taschen")};
    ui_push_id(ui, 600);
    if (ui_tabs(ui, rect_cut_top(&in, ui_px(ui, 28)), cats, CAT_COUNT, &s_cat)) {
        s_sel_def = -1;
    }
    ui_pop_id(ui);
    rect_cut_top(&in, ui_px(ui, 6));
    draw_make_filters(ui, rect_cut_top(&in, ui_px(ui, 26)));
    rect_cut_top(&in, ui_px(ui, 6));
    draw_make_list(app, in);
}

/* ---- Herstellen: Detail + Zutaten ---------------------------------------------- */
static void draw_item_preview(App *app, Rect r, int def) {
    Ui *ui = &app->ui;
    Item it = item_make(def);
    char body[1024];
    int s = ui_px(ui, 56);
    ui_item_slot(ui, rect_make(r.x, r.y, s, s), &app->game, app_hero(app), &it, false);
    ui_text(ui, r.x + s + ui_px(ui, 10), r.y + ui_px(ui, 6), g_items[def].name, ui_tier(app, g_items[def].tier, 255), FS_BIG);
    char sub[64];
    str_format(sub, sizeof sub, "%s - Lv %d", tr(g_tiers[g_items[def].tier].name), g_items[def].min_level);
    ui_text(ui, r.x + s + ui_px(ui, 10), r.y + ui_px(ui, 32), sub, COL_INK, FS_SMALL);
    item_describe(&app->game, &it, body, sizeof body);
    Rect t = rect_make(r.x, r.y + s + ui_px(ui, 8), r.w, r.h - s - ui_px(ui, 8));
    gfx_push_clip(ui->gfx, t);
    gfx_text_wrapped(ui->gfx, t, body, COL_INK, fsz(ui, FS_SMALL), true);
    gfx_pop_clip(ui->gfx);
}

static void draw_make_detail(App *app, Rect r) {
    Ui *ui = &app->ui;
    Rect in = rect_inset(ui_panel(ui, r, tr("Vorschau")), ui_px(ui, 10));
    if (s_sel_def < 0) {
        ui_label_wrapped(ui, in, tr("Rezept in der Liste waehlen."), COL_INK, fsz(ui, FS_NORMAL));
        return;
    }
    draw_item_preview(app, in, s_sel_def);
}

static int max_craftable(const Game *g, const Recipe *rc) {
    int best = 1;
    for (int q = 1; q <= 99; q++) {
        if (craft_can_make(g, rc, q)) {
            best = q;
        }
    }
    return best;
}

static void act_make(App *app, int def) {
    char err[96] = "";
    bool ok = craft_make(&app->game, def, s_qty, err);
    ui_result(app, ok, err, tr("Hergestellt"));
    if (ok) {
        app_play(app, SFX_CRAFT_OK);
    }
}

static void draw_make_ingredients(App *app, Rect r) {
    Ui *ui = &app->ui;
    Rect in = rect_inset(ui_panel(ui, r, tr("Zutaten")), ui_px(ui, 10));
    Recipe rc;
    if (s_sel_def < 0 || !craft_recipe(&app->game, s_sel_def, &rc)) {
        return;
    }
    int rh = ui_px(ui, 34), gap = ui_px(ui, 4);
    for (int i = 0; i < rc.input_n; i++) {
        ui_push_id(ui, i);
        draw_stock_row(app, rect_cut_top(&in, rh), rc.input_def[i], (i64)rc.input_count[i] * s_qty);
        ui_pop_id(ui);
        rect_cut_top(&in, gap);
    }
    draw_gold_row(app, rect_cut_top(&in, rh), rc.gold * s_qty);
    Rect bottom = rect_cut_bottom(&in, ui_px(ui, 38));
    rect_cut_bottom(&in, ui_px(ui, 6));
    Rect qrow = rect_cut_bottom(&in, ui_px(ui, 30));
    if (!rc.unlocked) {
        char b[96];
        str_format(b, sizeof b, "%s: %s", tr("Gesperrt"), tr(rc.lock_reason));
        ui_label_wrapped(ui, in, b, COL_HP, fsz(ui, FS_SMALL));
    }
    draw_qty_row(app, qrow, &s_qty, max_craftable(&app->game, &rc));
    bool can = craft_can_make(&app->game, &rc, s_qty);
    if (ui_button_ex(ui, bottom, tr("Herstellen"), UI_BTN_GREEN, can)) {
        act_make(app, s_sel_def);
    }
}

static void draw_make_tab(App *app, Rect r) {
    Ui *ui = &app->ui;
    int gap = ui_px(ui, 10);
    int lw = mini(ui_px(ui, 400), r.w * 2 / 5);
    int rw = mini(ui_px(ui, 330), r.w / 3);
    Rect left = rect_cut_left(&r, lw);
    rect_cut_left(&r, gap);
    Rect right = rect_cut_right(&r, rw);
    rect_cut_right(&r, gap);
    draw_make_left(app, left);
    draw_make_detail(app, r);
    draw_make_ingredients(app, right);
}

/* ---- Ziel-Auswahl (Verbessern / Verzaubern) ----------------------------------------- */
static Item *target_item(App *app) {
    Game *g = &app->game;
    Hero *h = app_hero(app);
    Item *it = NULL;
    if (s_tgt_slot >= 0 && h && s_tgt_slot < SLOT_COUNT) {
        it = &h->eq[s_tgt_slot];
    } else if (s_tgt_inv >= 0 && s_tgt_inv < g->inv.item_count) {
        it = &g->inv.items[s_tgt_inv];
    }
    return (it && !item_is_empty(it) && it->def < g_item_count) ? it : NULL;
}

static bool target_ok_for_tab(const Game *g, const Item *it) {
    const ItemDef *d = &g_items[it->def];
    if (s_tab == TAB_ENHANCE) {
        return enhance_stone_def(d) >= 0 && d->min_level >= 1;
    }
    return enchant_can(g, it, NULL);
}

static void draw_target_cell(App *app, Rect cell, Item *it, int inv_idx, int slot) {
    Ui *ui = &app->ui;
    bool sel = (slot >= 0) ? s_tgt_slot == slot : (s_tgt_slot < 0 && s_tgt_inv == inv_idx);
    UiClick c = ui_item_slot(ui, cell, &app->game, app_hero(app), it, sel);
    if (slot >= 0) {
        int s = ui_px(ui, 9);
        gfx_fill_rect(ui->gfx, rect_make(cell.x + 2, cell.y + 2, s, s), COL_MOSS);
    }
    if (c.clicked) {
        s_tgt_inv = inv_idx;
        s_tgt_slot = slot;
        s_enh_msg[0] = '\0';
        s_roll_t = 0.0f;
    }
}

static void draw_target_grid(App *app, Rect r) {
    Ui *ui = &app->ui;
    Game *g = &app->game;
    Hero *h = app_hero(app);
    int cs = ui_px(ui, 52), gap = ui_px(ui, 6), bar = ui_px(ui, 12);
    int cols = maxi(1, (r.w - bar) / (cs + gap));
    int off = ui_scroll_begin(ui, r, ui_px(ui, 3000), "tgt_grid");
    int k = 0;
    for (int pass = 0; pass < 2; pass++) {
        int n = pass == 0 ? SLOT_COUNT : g->inv.item_count;
        for (int i = 0; i < n; i++) {
            Item *it = pass == 0 ? (h ? &h->eq[i] : NULL) : &g->inv.items[i];
            if (!it || item_is_empty(it) || it->def >= g_item_count || !target_ok_for_tab(g, it)) {
                continue;
            }
            Rect cell = rect_make(r.x + gap + (k % cols) * (cs + gap), r.y + gap + (k / cols) * (cs + gap) - off, cs, cs);
            k++;
            if (cell.y + cs < r.y || cell.y > r.y + r.h) {
                continue;
            }
            ui_push_id(ui, k);
            draw_target_cell(app, cell, it, pass == 0 ? -1 : i, pass == 0 ? i : -1);
            ui_pop_id(ui);
        }
    }
    ui_scroll_end(ui);
    if (k == 0) {
        ui_label_wrapped(ui, r, tr("Keine passenden Gegenstaende."), COL_INK_SOFT, fsz(ui, FS_NORMAL));
    }
}

static void draw_target_list(App *app, Rect r, const char *title) {
    Ui *ui = &app->ui;
    Rect in = rect_inset(ui_panel(ui, r, title), ui_px(ui, 8));
    ui_text(ui, in.x, in.y, tr("Gruen markiert = angelegt"), COL_INK_SOFT, FS_TINY);
    rect_cut_top(&in, ui_px(ui, 18));
    draw_target_grid(app, in);
}

static void draw_target_head(App *app, Rect r, const Item *it) {
    Ui *ui = &app->ui;
    char name[96], body[1024];
    item_display_name(it, name, sizeof name);
    int s = ui_px(ui, 56);
    ui_item_slot(ui, rect_make(r.x, r.y, s, s), &app->game, app_hero(app), it, false);
    ui_text(ui, r.x + s + ui_px(ui, 10), r.y + ui_px(ui, 6), name, ui_tier(app, g_items[it->def].tier, 255), FS_BIG);
    char sub[64];
    str_format(sub, sizeof sub, "%s - Lv %d", tr(g_tiers[g_items[it->def].tier].name), g_items[it->def].min_level);
    ui_text(ui, r.x + s + ui_px(ui, 10), r.y + ui_px(ui, 32), sub, COL_INK, FS_SMALL);
    item_describe(&app->game, it, body, sizeof body);
    Rect t = rect_make(r.x, r.y + s + ui_px(ui, 8), r.w, r.h - s - ui_px(ui, 8));
    gfx_push_clip(ui->gfx, t);
    gfx_text_wrapped(ui->gfx, t, body, COL_INK, fsz(ui, FS_SMALL), true);
    gfx_pop_clip(ui->gfx);
}

/* ---- Verbessern ---------------------------------------------------------------------- */
static void act_enhance(App *app, Item *it) {
    char err[96] = "";
    int before = it->plus;
    EnhanceResult res = enhance_try(&app->game, it, err);
    Hero *h = app_hero(app);
    if (h && s_tgt_slot >= 0) {
        h->stats_dirty = true;
    }
    char b[96];
    if (res == ENH_OK) {
        str_format(b, sizeof b, "%s +%d -> +%d", tr("Erfolg!"), before, it->plus);
        set_enh_msg(b, COL_MOSS);
        app_play(app, SFX_CRAFT_OK);
    } else if (res == ENH_FAIL) {
        set_enh_msg(tr("Fehlschlag - Steine verbraucht"), COL_HP);
        app_play(app, SFX_CRAFT_FAIL);
    } else {
        set_enh_msg(err[0] ? err : tr("Nicht moeglich"), COL_HP);
        app_play(app, SFX_ERROR);
    }
}

static void draw_enhance_costs(App *app, Rect r, const Item *it) {
    Ui *ui = &app->ui;
    const ItemDef *d = &g_items[it->def];
    int next = it->plus + 1, stone = enhance_stone_def(d);
    char b[96];
    int rh = ui_px(ui, 34);
    str_format(b, sizeof b, "%s: %.0f %%", tr("Erfolgschance"), (double)enhance_success_chance(next));
    ui_text(ui, r.x, r.y, b, COL_INK, FS_NORMAL);
    rect_cut_top(&r, ui_px(ui, 24));
    draw_stock_row(app, rect_cut_top(&r, rh), stone, enhance_stone_cost(next));
    rect_cut_top(&r, ui_px(ui, 4));
    draw_gold_row(app, rect_cut_top(&r, rh), enhance_gold_cost(next, d->tier));
}

static void draw_enhance_detail(App *app, Rect r) {
    Ui *ui = &app->ui;
    Rect in = rect_inset(ui_panel(ui, r, tr("Verbessern")), ui_px(ui, 10));
    Item *it = target_item(app);
    if (!it) {
        ui_label_wrapped(ui, in, tr("Gegenstand links waehlen (Lager oder Angelegtes)."), COL_INK, fsz(ui, FS_NORMAL));
        return;
    }
    Rect bottom = rect_cut_bottom(&in, ui_px(ui, 40));
    if (it->plus >= ENHANCE_MAX) {
        ui_text(ui, in.x, in.y + in.h - ui_px(ui, 20), tr("Maximale Stufe erreicht"), COL_WOOD, FS_NORMAL);
    } else {
        Rect costs = rect_cut_bottom(&in, ui_px(ui, 104));
        draw_enhance_costs(app, costs, it);
    }
    draw_target_head(app, in, it);
    if (s_enh_ttl > 0.0f && s_enh_msg[0]) {
        ui_text_center(ui, rect_make(bottom.x, bottom.y - ui_px(ui, 22), bottom.w, ui_px(ui, 20)), s_enh_msg, s_enh_col, FS_NORMAL);
    }
    char lab[48];
    str_format(lab, sizeof lab, "%s +%d", tr("Verbessern auf"), it->plus + 1);
    if (ui_button_ex(ui, bottom, lab, UI_BTN_GREEN, it->plus < ENHANCE_MAX)) {
        act_enhance(app, it);
    }
}

static void draw_enhance_tab(App *app, Rect r) {
    Ui *ui = &app->ui;
    int lw = mini(ui_px(ui, 420), r.w / 2);
    Rect left = rect_cut_left(&r, lw);
    rect_cut_left(&r, ui_px(ui, 10));
    draw_target_list(app, left, tr("Gegenstand"));
    draw_enhance_detail(app, r);
}

/* ---- Verzaubern ---------------------------------------------------------------------- */
static void act_enchant(App *app, Item *it) {
    char err[96] = "";
    enchant_set_seal(it, s_seal);
    bool ok = enchant_roll(&app->game, it, err);
    Hero *h = app_hero(app);
    if (h && s_tgt_slot >= 0) {
        h->stats_dirty = true;
    }
    if (ok) {
        s_roll_t = ROLL_TIME;
        app_play(app, SFX_CLICK);
    } else {
        app_message(app, "%s", err[0] ? err : tr("Nicht moeglich"));
        app_play(app, SFX_ERROR);
    }
}

/* Kosmetik: zufaellige Optionen durchlaufen lassen (Roulette). */
static void roulette_text(const Ui *ui, const Item *it, char *buf, size_t size) {
    Item fake = *it;
    u32 step = (u32)(ui->time * 16.0);
    u32 h = step * 2654435761u;
    fake.ench_opt = (u8)(ENCH_FLAT_DMG + (h >> 8) % (ENCH_MOVE_PCT - ENCH_FLAT_DMG + 1));
    fake.ench_val = (float)(3 + (h >> 16) % 40);
    fake.flags &= (u8)~ITEMF_SEALED;
    enchant_describe(&fake, buf, size);
}

static void draw_enchant_current(App *app, Rect r, const Item *it) {
    Ui *ui = &app->ui;
    char b[128];
    Rect box = rect_cut_top(&r, ui_px(ui, 56));
    gfx_fill_rect(ui->gfx, box, COL_WOOD_DARK);
    gfx_draw_rect(ui->gfx, box, s_roll_t > 0.0f ? COL_GOLD : COL_PARCHMENT_DARK, 2);
    if (s_roll_t > 0.0f) {
        roulette_text(ui, it, b, sizeof b);
        ui_text_center(ui, box, b, COL_GOLD, FS_NORMAL);
        return;
    }
    enchant_describe(it, b, sizeof b);
    Color c = it->ench_opt == ENCH_NONE ? COL_PARCHMENT_DARK : COL_HEAL;
    ui_text_center(ui, rect_make(box.x, box.y, box.w, box.h * 2 / 3), b, c, FS_NORMAL);
    if (it->ench_opt != ENCH_NONE) {
        str_format(b, sizeof b, "%s %d", tr("Grad"), it->ench_grade);
        ui_text_center(ui, rect_make(box.x, box.y + box.h / 2, box.w, box.h / 2), b, COL_PARCHMENT, FS_SMALL);
    }
}

static void draw_enchant_costs(App *app, Rect r, const Item *it) {
    Ui *ui = &app->ui;
    const ItemDef *d = &g_items[it->def];
    int stone = enchant_stone_def(d), rh = ui_px(ui, 34);
    if (stone >= 0) {
        draw_stock_row(app, rect_cut_top(&r, rh), stone, 1);
    }
    rect_cut_top(&r, ui_px(ui, 4));
    draw_gold_row(app, rect_cut_top(&r, rh), enchant_gold_cost(d));
}

static bool enchant_affordable(const App *app, const Item *it) {
    const ItemDef *d = &g_items[it->def];
    int stone = enchant_stone_def(d);
    return stone >= 0 && app->game.inv.stock[stone] >= 1 && app->game.player.gold >= enchant_gold_cost(d);
}

static void draw_enchant_detail(App *app, Rect r) {
    Ui *ui = &app->ui;
    Rect in = rect_inset(ui_panel(ui, r, tr("Verzaubern")), ui_px(ui, 10));
    Item *it = target_item(app);
    if (!it) {
        ui_label_wrapped(ui, in, tr("Gegenstand links waehlen (ab Tier Selten)."), COL_INK, fsz(ui, FS_NORMAL));
        return;
    }
    Rect bottom = rect_cut_bottom(&in, ui_px(ui, 40));
    rect_cut_bottom(&in, ui_px(ui, 6));
    Rect seal = rect_cut_bottom(&in, ui_px(ui, 24));
    rect_cut_bottom(&in, ui_px(ui, 4));
    Rect costs = rect_cut_bottom(&in, ui_px(ui, 74));
    Rect ench = rect_cut_bottom(&in, ui_px(ui, 64));
    draw_target_head(app, in, it);
    draw_enchant_current(app, ench, it);
    draw_enchant_costs(app, costs, it);
    s_seal = (it->flags & ITEMF_SEALED) != 0;
    if (ui_checkbox(ui, seal, tr("Siegel (alte Verzauberung behalten, bis eine bessere faellt)"), &s_seal)) {
        enchant_set_seal(it, s_seal);
    }
    bool can = s_roll_t <= 0.0f && enchant_affordable(app, it);
    const char *lab = it->ench_opt == ENCH_NONE ? tr("Verzaubern") : tr("Neu wuerfeln");
    if (ui_button_ex(ui, bottom, lab, UI_BTN_GREEN, can)) {
        act_enchant(app, it);
    }
}

static void draw_enchant_tab(App *app, Rect r) {
    Ui *ui = &app->ui;
    int lw = mini(ui_px(ui, 420), r.w / 2);
    Rect left = rect_cut_left(&r, lw);
    rect_cut_left(&r, ui_px(ui, 10));
    draw_target_list(app, left, tr("Gegenstand"));
    draw_enchant_detail(app, r);
}

/* ---- Umwandeln ----------------------------------------------------------------------- */
static bool convertible(int def) {
    return convert_target_def(def, NULL) >= 0;
}

static void draw_conv_grid(App *app, Rect r) {
    Ui *ui = &app->ui;
    Game *g = &app->game;
    int cs = ui_px(ui, 52), gap = ui_px(ui, 6), bar = ui_px(ui, 12);
    int cols = maxi(1, (r.w - bar) / (cs + gap)), k = 0;
    int off = ui_scroll_begin(ui, r, ui_px(ui, 3000), "conv_grid");
    for (int def = 0; def < g_item_count && k < CONV_SLOTS * 4; def++) {
        if (g->inv.stock[def] == 0 || !convertible(def)) {
            continue;
        }
        Rect cell = rect_make(r.x + gap + (k % cols) * (cs + gap), r.y + gap + (k / cols) * (cs + gap) - off, cs, cs);
        k++;
        ui_push_id(ui, def);
        if (ui_stack_slot(ui, cell, def, g->inv.stock[def], s_conv_def == def).clicked) {
            s_conv_def = def;
            s_conv_times = 1;
        }
        ui_pop_id(ui);
    }
    ui_scroll_end(ui);
    if (k == 0) {
        ui_label_wrapped(ui, r, tr("Keine umwandelbaren Materialien oder Steine im Lager."), COL_INK_SOFT, fsz(ui, FS_NORMAL));
    }
}

static int conv_max_times(const Game *g, int from, int target, u32 need) {
    int magic = data_find_magic_stone((int)g_items[target].tier);
    i64 t = need > 0 ? (i64)g->inv.stock[from] / need : 0;
    if (magic >= 0) {
        t = mini((int)t, (int)g->inv.stock[magic]);
    }
    return clampi((int)t, 1, QTY_MAX);
}

static void act_convert(App *app) {
    char err[96] = "";
    bool ok = convert_materials(&app->game, s_conv_def, s_conv_times, err);
    ui_result(app, ok, err, tr("Umgewandelt"));
    if (ok) {
        app_play(app, SFX_CRAFT_OK);
    }
}

static void act_convert_jewel(App *app) {
    char err[96] = "";
    const ItemDef *d = &g_items[s_conv_def];
    bool ok = convert_to_jewel_stone(&app->game, d->tier, d->stone_kind == STONE_ENH_WEAPON, s_conv_times, err);
    ui_result(app, ok, err, tr("Zu Schmuckstein umgewandelt"));
    if (ok) {
        app_play(app, SFX_CRAFT_OK);
    }
}

static void draw_conv_flow(App *app, Rect r, int from, int target, u32 need) {
    Ui *ui = &app->ui;
    int s = ui_px(ui, 60), rh = ui_px(ui, 34);
    char b[96];
    Rect a = rect_make(r.x, r.y, s, s), t = rect_make(r.x + r.w - s, r.y, s, s);
    ui_stack_slot(ui, a, from, app->game.inv.stock[from], false);
    ui_stack_slot(ui, t, target, app->game.inv.stock[target], false);
    str_format(b, sizeof b, "%u : 1", need);
    ui_text_center(ui, rect_make(a.x + s, r.y, r.w - 2 * s, s), b, COL_WOOD, FS_BIG);
    Color c1 = ui_tier(app, g_items[from].tier, 255), c2 = ui_tier(app, g_items[target].tier, 255);
    ui_text(ui, r.x, r.y + s + ui_px(ui, 4), g_items[from].name, c1, FS_SMALL);
    ui_text_right(ui, rect_make(r.x, r.y + s + ui_px(ui, 4), r.w, ui_px(ui, 18)), g_items[target].name, c2, FS_SMALL);
    rect_cut_top(&r, s + ui_px(ui, 30));
    draw_stock_row(app, rect_cut_top(&r, rh), from, (i64)need * s_conv_times);
    int magic = data_find_magic_stone((int)g_items[target].tier);
    if (magic >= 0) {
        rect_cut_top(&r, ui_px(ui, 4));
        draw_stock_row(app, rect_cut_top(&r, rh), magic, s_conv_times);
    }
}

static void draw_conv_buttons(App *app, Rect r, int from, int target, u32 need) {
    Ui *ui = &app->ui;
    const ItemDef *d = &g_items[from];
    int rh = ui_px(ui, 38), gap = ui_px(ui, 6);
    bool jewel = d->stone_kind == STONE_ENH_WEAPON || d->stone_kind == STONE_ENH_ARMOR;
    Rect b1 = rect_cut_bottom(&r, rh);
    rect_cut_bottom(&r, gap);
    Rect b2 = jewel ? rect_cut_bottom(&r, rh) : rect_make(0, 0, 0, 0);
    if (jewel) {
        rect_cut_bottom(&r, gap);
    }
    Rect q = rect_cut_bottom(&r, ui_px(ui, 30));
    draw_qty_row(app, q, &s_conv_times, conv_max_times(&app->game, from, target, need));
    char lab[48];
    str_format(lab, sizeof lab, "%s (%d x)", tr("Umwandeln"), s_conv_times);
    if (ui_button_ex(ui, b1, lab, UI_BTN_GREEN, true)) {
        act_convert(app);
    }
    if (jewel && ui_button_ex(ui, b2, tr("Zu Schmuckstein (10 Waffen / 20 Ruestung : 1)"), UI_BTN_WAX, true)) {
        act_convert_jewel(app);
    }
}

static void draw_conv_detail(App *app, Rect r) {
    Ui *ui = &app->ui;
    Rect in = rect_inset(ui_panel(ui, r, tr("Umwandeln")), ui_px(ui, 10));
    u32 need = 0;
    int target = s_conv_def >= 0 ? convert_target_def(s_conv_def, &need) : -1;
    if (target < 0 || app->game.inv.stock[s_conv_def] == 0) {
        s_conv_def = -1;
        ui_label_wrapped(ui, in, tr("Material oder Stein links waehlen. Je Umwandlung wird ein Magiestein verbraucht."),
                         COL_INK, fsz(ui, FS_NORMAL));
        return;
    }
    draw_conv_flow(app, in, s_conv_def, target, need);
    draw_conv_buttons(app, in, s_conv_def, target, need);
}

static void draw_convert_tab(App *app, Rect r) {
    Ui *ui = &app->ui;
    int lw = mini(ui_px(ui, 420), r.w / 2);
    Rect left = rect_cut_left(&r, lw);
    rect_cut_left(&r, ui_px(ui, 10));
    Rect in = rect_inset(ui_panel(ui, left, tr("Lager")), ui_px(ui, 8));
    draw_conv_grid(app, in);
    draw_conv_detail(app, r);
}

/* ---- Hauptfunktion -------------------------------------------------------------------- */
void screen_crafting(App *app, Rect area) {
    Ui *ui = &app->ui;
    gfx_fill_rect(ui->gfx, area, COL_WOOD_DARK);
    app_ensure_hero(app);
    Rect r = rect_inset(area, ui_px(ui, 10));
    s_enh_ttl = maxf(0.0f, s_enh_ttl - ui->anim_dt);
    s_roll_t = maxf(0.0f, s_roll_t - ui->anim_dt);
    if (ui_hero_picker(app, rect_cut_top(&r, ui_px(ui, 32)))) {
        clear_target();
    }
    rect_cut_top(&r, ui_px(ui, 6));
    const char *labels[4] = {tr("Herstellen"), tr("Verbessern"), tr("Verzaubern"), tr("Umwandeln")};
    if (ui_tabs(ui, rect_cut_top(&r, ui_px(ui, 32)), labels, 4, &s_tab)) {
        clear_target();
    }
    rect_cut_top(&r, ui_px(ui, 6));
    switch (s_tab) {
    case TAB_MAKE: draw_make_tab(app, r); break;
    case TAB_ENHANCE: draw_enhance_tab(app, r); break;
    case TAB_ENCHANT: draw_enchant_tab(app, r); break;
    default: draw_convert_tab(app, r); break;
    }
}
