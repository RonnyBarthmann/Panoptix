/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Kommandoansicht 2x2 (Spec 18 Abschnitt 3): Layout-Tabs, Mini-Views, Heldenuebersicht. */
#include "ui/ui_internal.h"

/* ---- Hilfen ------------------------------------------------------------ */
static int chest_total(const Hero *h) {
    int n = 0;
    for (int t = 0; t < TIER_COUNT; t++) {
        n += h->chest_bar[t];
    }
    return n;
}

static int chest_percent(const Game *g, const Hero *h) {
    int cap = hero_chest_capacity(g, h);
    return cap > 0 ? chest_total(h) * 100 / cap : 0;
}

/* Kurzer Statustext fuer Uebersicht/Feld. */
static const char *hero_state_text(const Hero *h, char *buf, size_t size, Color *col) {
    *col = COL_INK_SOFT;
    if (h->dead_timer > 0.0f) {
        char d[24];
        str_format(buf, size, "%s %s", tr("Tot"), str_format_duration(d, sizeof d, h->dead_timer));
        *col = COL_WAX_RED;
    } else if (h->retry_timer > 0.0f) {
        str_copy(buf, size, tr("Wartet"));
    } else if (h->assigned_map >= 0) {
        str_copy(buf, size, tr("Unterwegs"));
        *col = COL_MOSS;
    } else {
        str_copy(buf, size, tr("Keine Map"));
    }
    return buf;
}

/* ---- Kopfleiste: Heldenuebersicht --------------------------------------- */
static void draw_overview(App *app, Rect r) {
    Ui *ui = &app->ui;
    Game *g = &app->game;
    gfx_fill_rect(ui->gfx, r, COL_PARCHMENT);
    int n = 0;
    for (int i = 0; i < MAX_HEROES; i++) {
        n += g->heroes[i].used ? 1 : 0;
    }
    if (n == 0) {
        ui_text_center(ui, r, tr("Kein Held vorhanden - in der Heldenliste (F1) erstellen."), COL_INK, FS_SMALL);
        return;
    }
    int cw = mini(ui_px(ui, 170), r.w / n);
    int x = r.x;
    for (int i = 0; i < MAX_HEROES; i++) {
        const Hero *h = &g->heroes[i];
        if (!h->used) {
            continue;
        }
        Rect c = rect_make(x, r.y, cw - 2, r.h);
        x += cw;
        ui_push_id(ui, 300 + i);
        UiClick k = ui_region(ui, c, ui_make_id(ui, "ov", c.x, c.y));
        gfx_fill_rect(ui->gfx, c, k.hovered ? COL_PARCHMENT_LIGHT : COL_PARCHMENT_DARK);
        gfx_draw_rect(ui->gfx, c, app->sel_hero == i ? COL_GOLD : COL_WOOD, 1);
        char b[80], st[40];
        Color col;
        int pad = ui_px(ui, 4), lh = r.h / 2;
        gfx_push_clip(ui->gfx, c);
        str_format(b, sizeof b, "%s %d", h->name, h->level);
        ui_text(ui, c.x + pad, c.y + (lh - gfx_text_height(ui->gfx, fsz(ui, FS_SMALL))) / 2, b, COL_INK, FS_SMALL);
        hero_state_text(h, st, sizeof st, &col);
        str_format(b, sizeof b, "%s | %s %d%%", st, tr("K"), chest_percent(g, h));
        ui_text(ui, c.x + pad, c.y + lh + (lh - gfx_text_height(ui->gfx, fsz(ui, FS_TINY))) / 2, b, col, FS_TINY);
        gfx_pop_clip(ui->gfx);
        if (k.clicked) {
            app->sel_hero = i;
        }
        ui_pop_id(ui);
    }
}

/* ---- Layout-Tabs -------------------------------------------------------- */
static void draw_layout_tabs(App *app, Rect r) {
    Ui *ui = &app->ui;
    static char names[MAX_LAYOUTS][24];
    const char *labels[MAX_LAYOUTS];
    for (int i = 0; i < MAX_LAYOUTS; i++) {
        if (app->layout_names[i][0]) {
            str_format(names[i], sizeof names[i], "L%d %s", i + 1, app->layout_names[i]);
        } else {
            str_format(names[i], sizeof names[i], "L%d", i + 1);
        }
        labels[i] = names[i];
    }
    Rect t = rect_make(r.x, r.y, mini(r.w, ui_px(ui, 110) * MAX_LAYOUTS), r.h);
    int l = app->layout;
    if (ui_tabs(ui, t, labels, MAX_LAYOUTS, &l)) {
        app->layout = clampi(l, 0, MAX_LAYOUTS - 1);
    }
    char b[80];
    str_format(b, sizeof b, "%s", tr("Klick auf ein Feld = Vollbild"));
    ui_text_right(ui, rect_make(t.x + t.w, r.y, r.x + r.w - t.x - t.w - ui_px(ui, 4), r.h), b, COL_INK_SOFT, FS_SMALL);
}

/* ---- Einzelnes Feld ----------------------------------------------------- */
typedef struct Cell {
    App *app;
    int field;       /* 0..3 */
    int hero_idx;    /* Held oder -1 */
    Rect r;
} Cell;

/* Dropdown (Klick = weiter, Rechtsklick = zurueck) zur Heldenzuweisung. */
static void draw_hero_select(Cell *c, Rect r) {
    App *app = c->app;
    Game *g = &app->game;
    static char names[MAX_HEROES + 1][40];
    const char *opts[MAX_HEROES + 1];
    int idx_of[MAX_HEROES + 1];
    int n = 0, value = 0;
    str_copy(names[0], sizeof names[0], tr("- leer -"));
    opts[0] = names[0];
    idx_of[0] = -1;
    n = 1;
    for (int i = 0; i < MAX_HEROES; i++) {
        if (!g->heroes[i].used) {
            continue;
        }
        hero_label(&g->heroes[i], names[n], sizeof names[n]);
        opts[n] = names[n];
        idx_of[n] = i;
        if (i == c->hero_idx) {
            value = n;
        }
        n++;
    }
    int v = value;
    if (ui_cycle(&app->ui, r, opts, n, &v) && v != value) {
        app->layouts[app->layout][c->field] = idx_of[v];
        c->hero_idx = idx_of[v];
    }
}

static void draw_cell_top(Cell *c, Run *run, int slot, Rect top) {
    Ui *ui = &c->app->ui;
    int gap = ui_px(ui, 3);
    bool has_run = run && slot >= 0;
    int bw = ui_px(ui, 62);
    Rect collect = rect_cut_right(&top, bw);
    rect_cut_right(&top, gap);
    Rect aut = rect_cut_right(&top, bw);
    rect_cut_right(&top, gap);
    draw_hero_select(c, top);
    if (c->hero_idx < 0) {
        return;
    }
    Hero *h = &c->app->game.heroes[c->hero_idx];
    bool manual = has_run && run->heroes[slot].manual;
    if (ui_button_tip(ui, aut, manual ? tr("Manuell") : tr("Auto"), manual ? UI_BTN_WAX : UI_BTN_GREEN, has_run,
                      tr("Automatik / manuelle Steuerung"))) {
        run_set_manual(run, slot, !manual);
        app_play(c->app, SFX_CLICK);
    }
    if (ui_button_tip(ui, collect, tr("Sammeln"), UI_BTN_GREEN, chest_total(h) > 0, tr("Kisten ins Lager (F4)"))) {
        int n = game_collect_chests(&c->app->game, c->hero_idx);
        app_message(c->app, "%d %s", n, tr("Kisten eingesammelt"));
        app_play(c->app, SFX_CLICK);
    }
}

static void draw_cell_bottom(Cell *c, Run *run, int slot, Rect bot) {
    Ui *ui = &c->app->ui;
    Hero *h = &c->app->game.heroes[c->hero_idx];
    const DerivedStats *st = hero_stats(&c->app->game, h);
    float hp = st->max_hp, mhp = st->max_hp, mp = st->max_mana, mmp = st->max_mana;
    if (run && slot >= 0) {
        hp = run->heroes[slot].u.hp;
        mhp = run->heroes[slot].u.max_hp;
        mp = run->heroes[slot].mana;
        mmp = run->heroes[slot].max_mana;
    }
    gfx_fill_rect(ui->gfx, bot, COL_PARCHMENT);
    Rect in = rect_inset(bot, ui_px(ui, 3));
    int cw = ui_px(ui, 96);
    Rect info = rect_cut_right(&in, cw);
    rect_cut_right(&in, ui_px(ui, 4));
    int bh = (in.h - ui_px(ui, 2)) / 2;
    char b[48], n1[20], n2[20];
    str_format(b, sizeof b, "%s / %s", ui_num((i64)hp, n1, sizeof n1), ui_num((i64)mhp, n2, sizeof n2));
    ui_bar(ui, rect_make(in.x, in.y, in.w, bh), mhp > 0 ? hp / mhp : 0.0f, COL_HP, COL_HP_DARK, b);
    str_format(b, sizeof b, "%s / %s", ui_num((i64)mp, n1, sizeof n1), ui_num((i64)mmp, n2, sizeof n2));
    ui_bar(ui, rect_make(in.x, in.y + in.h - bh, in.w, bh), mmp > 0 ? mp / mmp : 0.0f, COL_MANA_BAR, COL_HP_DARK, b);
    int cap = hero_chest_capacity(&c->app->game, h);
    int total = chest_total(h);
    bool warn = cap > 0 && total * 100 >= cap * 70;
    str_format(b, sizeof b, "%s %d/%d", tr("Kisten"), total, cap);
    ui_text_right(ui, rect_make(info.x, info.y, info.w, info.h / 2), b, warn ? COL_WAX_RED : COL_INK, FS_SMALL);
    Color col;
    char st2[40];
    hero_state_text(h, st2, sizeof st2, &col);
    if (run && slot >= 0) {
        str_copy(st2, sizeof st2, tr(hero_state_name(run->heroes[slot].state)));
        col = COL_INK_SOFT;
    }
    ui_text_right(ui, rect_make(info.x, info.y + info.h / 2, info.w, info.h / 2), st2, col, FS_TINY);
}

static void draw_cell_view(Cell *c, Run *run, int slot, Rect vp) {
    Ui *ui = &c->app->ui;
    Hero *h = &c->app->game.heroes[c->hero_idx];
    bool live = run && slot >= 0 && h->dead_timer <= 0.0f && h->retry_timer <= 0.0f;
    if (live) {
        ViewOpts o = {true, true, false, true, false, false, false};
        view_draw_run(ui->gfx, &c->app->game, run, slot, &c->app->fx[c->hero_idx], vp, &o);
    } else {
        char b[80];
        Color col;
        gfx_fill_rect(ui->gfx, vp, RGB(0x24, 0x1B, 0x14));
        hero_state_text(h, b, sizeof b, &col);
        ui_text_center(ui, vp, h->assigned_map < 0 ? tr("Held keiner Map zugewiesen") : b, COL_PARCHMENT, FS_NORMAL);
    }
    ui_push_id(ui, 400 + c->field);
    UiClick k = ui_region(ui, vp, ui_make_id(ui, "view", vp.x, vp.y));
    ui_pop_id(ui);
    if (k.hovered) {
        gfx_draw_rect(ui->gfx, vp, COL_GOLD, 2);
    }
    if (k.clicked) {
        c->app->sel_hero = c->hero_idx;
        app_play(c->app, SFX_CLICK);
        app_set_screen(c->app, SCR_COMBAT);
    }
}

static void draw_cell(App *app, int field, Rect r) {
    Ui *ui = &app->ui;
    Cell c = {app, field, app->layouts[app->layout][field], r};
    if (c.hero_idx >= 0 && (c.hero_idx >= MAX_HEROES || !app->game.heroes[c.hero_idx].used)) {
        c.hero_idx = -1;
        app->layouts[app->layout][field] = -1;
    }
    ui_push_id(ui, field);
    gfx_fill_rect(ui->gfx, r, COL_WOOD_DARK);
    Rect in = rect_inset(r, 2);
    int th = ui_px(ui, 26), bh = ui_px(ui, 40);
    Rect top = rect_cut_top(&in, th);
    if (c.hero_idx < 0) {
        gfx_fill_rect(ui->gfx, in, COL_PARCHMENT_DARK);
        ui_text_center(ui, in, tr("Feld frei - Held im Auswahlfeld oben waehlen"), COL_INK_SOFT, FS_SMALL);
        draw_hero_select(&c, top);
        ui_pop_id(ui);
        return;
    }
    Run *run = app->game.hero_run[c.hero_idx];
    int slot = run ? run_slot_of_hero(run, c.hero_idx) : -1;
    Rect bot = rect_cut_bottom(&in, bh);
    draw_cell_view(&c, run, slot, in);
    draw_cell_bottom(&c, run, slot, bot);
    gfx_fill_rect(ui->gfx, top, COL_PARCHMENT);
    draw_cell_top(&c, run, slot, rect_inset(top, 1));
    ui_pop_id(ui);
}

void screen_command(App *app, Rect area) {
    Ui *ui = &app->ui;
    app->layout = clampi(app->layout, 0, MAX_LAYOUTS - 1);
    Rect rest = area;
    int gap = ui_px(ui, 4);
    draw_overview(app, rect_cut_top(&rest, ui_px(ui, 38)));
    rect_cut_top(&rest, gap);
    draw_layout_tabs(app, rect_cut_top(&rest, ui_px(ui, 28)));
    rect_cut_top(&rest, gap);
    int cw = (rest.w - gap) / 2, ch = (rest.h - gap) / 2;
    for (int f = 0; f < 4; f++) {
        Rect r = rect_make(rest.x + (f % 2) * (cw + gap), rest.y + (f / 2) * (ch + gap), cw, ch);
        draw_cell(app, f, r);
    }
}
