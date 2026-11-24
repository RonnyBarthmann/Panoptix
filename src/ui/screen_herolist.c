/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Heldenliste: alle Heldenplaetze (belegt/frei/gesperrt), Heldenerstellung, Loeschen. */
#include <string.h>

#include "ui/ui_internal.h"

/* Spielerlevel, ab dem Heldenplatz 'slot' (0-basiert) verfuegbar ist (siehe game_hero_slots). */
static int slot_unlock_level(int slot) {
    static const int k_level[13] = {1, 8, 15, 22, 30, 38, 46, 54, 62, 70, 78, 86, 95};
    return slot < 4 ? 1 : k_level[clampi(slot - 3, 0, 12)];
}

/* ---- Zustand --------------------------------------------------------- */
static bool s_dlg_open = false;
static int s_dlg_cls = 0;
static int s_dlg_look = 0;
static bool s_dlg_auto = true;
static char s_dlg_name[24] = "";
static bool s_name_touched = false;
static bool s_del_open = false;
static int s_del_hero = -1;

/* ---- Statusdaten eines Helden ----------------------------------------- */
static void hero_hp_text(const App *app, int i, char *buf, size_t size, float *frac) {
    const Hero *h = &app->game.heroes[i];
    const Run *run = app->game.hero_run[i];
    *frac = 1.0f;
    if (run) {
        int s = run_slot_of_hero(run, i);
        if (s >= 0) {
            const Unit *u = &run->heroes[s].u;
            *frac = u->max_hp > 0 ? clampf(u->hp / u->max_hp, 0.0f, 1.0f) : 0.0f;
            str_format(buf, size, "%d / %d", (int)u->hp, (int)u->max_hp);
            return;
        }
    }
    str_format(buf, size, "%d", (int)h->stats.max_hp);
}

static void hero_status_text(const Hero *h, char *buf, size_t size, Color *col) {
    *col = COL_INK;
    if (h->dead_timer > 0) {
        char d[24];
        str_format(buf, size, "%s %s", tr("Tot"), str_format_duration(d, sizeof d, h->dead_timer));
        *col = COL_WAX_RED;
    } else if (h->retry_timer > 0) {
        str_copy(buf, size, tr("Wartet"));
        *col = COL_INK_SOFT;
    } else if (h->assigned_map >= 0) {
        str_copy(buf, size, tr("Unterwegs"));
        *col = COL_MOSS;
    } else {
        str_copy(buf, size, tr("Frei"));
        *col = COL_INK_SOFT;
    }
}

static int hero_chest_total(const Hero *h) {
    int n = 0;
    for (int t = 0; t < TIER_COUNT; t++) {
        n += h->chest_bar[t];
    }
    return n;
}

/* ---- Spaltenaufteilung ------------------------------------------------ */
enum { COL_ICON, COL_NAME, COL_CLASS, COL_LEVEL, COL_MAP, COL_HPC, COL_CHEST, COL_STAT, COL_N };
static const int k_col_w[COL_N] = {40, 130, 90, 50, 170, 130, 70, 120}; /* Basisbreiten */

static void col_rects(const Ui *ui, Rect row, Rect out[COL_N]) {
    int total = 0;
    for (int i = 0; i < COL_N; i++) {
        total += k_col_w[i];
    }
    int btns = ui_px(ui, 3 * 92 + 8);
    int avail = row.w - btns - ui_px(ui, 8);
    int x = row.x + ui_px(ui, 4);
    for (int i = 0; i < COL_N; i++) {
        int w = avail * k_col_w[i] / total;
        out[i] = rect_make(x, row.y, w, row.h);
        x += w;
    }
}

static void draw_cell(Ui *ui, Rect c, const char *text, Color col) {
    gfx_push_clip(ui->gfx, c);
    int y = c.y + (c.h - gfx_text_height(ui->gfx, fsz(ui, FS_SMALL))) / 2;
    gfx_text(ui->gfx, c.x, y, text, col, fsz(ui, FS_SMALL));
    gfx_pop_clip(ui->gfx);
}

static void draw_header(Ui *ui, Rect row) {
    static const char *const names[COL_N] = {"", "Name", "Klasse", "Lv", "Map", "HP", "Kisten", "Status"};
    Rect c[COL_N];
    col_rects(ui, row, c);
    gfx_fill_rect(ui->gfx, row, COL_WOOD_DARK);
    for (int i = 1; i < COL_N; i++) {
        draw_cell(ui, c[i], tr(names[i]), COL_PARCHMENT);
    }
}

/* ---- Zeilen ------------------------------------------------------------ */
static void row_buttons(App *app, Rect row, int i) {
    Ui *ui = &app->ui;
    int bw = ui_px(ui, 88), bh = ui_px(ui, 28), gap = ui_px(ui, 4);
    int x = row.x + row.w - 3 * (bw + gap) - ui_px(ui, 4);
    int y = row.y + (row.h - bh) / 2;
    static const char *const labels[3] = {"Vollbild", "Ausruestung", "Skills"};
    static const Screen targets[3] = {SCR_COMBAT, SCR_HERO, SCR_SKILLS};
    for (int b = 0; b < 3; b++) {
        ui_push_id(ui, b);
        if (ui_button_ex(ui, rect_make(x + b * (bw + gap), y, bw, bh), tr(labels[b]), UI_BTN_NORMAL, true)) {
            app->sel_hero = i;
            app_play(app, SFX_CLICK);
            app_set_screen(app, targets[b]);
        }
        ui_pop_id(ui);
    }
    Rect del = rect_make(row.x + row.w - ui_px(ui, 4) - 3 * (bw + gap) - ui_px(ui, 32), y, ui_px(ui, 28), bh);
    if (ui_button_tip(ui, del, "X", UI_BTN_WAX, true, tr("Held loeschen"))) {
        s_del_hero = i;
        s_del_open = true;
    }
}

static void draw_hero_row(App *app, Rect row, int i) {
    Ui *ui = &app->ui;
    const Hero *h = &app->game.heroes[i];
    Rect c[COL_N];
    col_rects(ui, row, c);
    ui_push_id(ui, i);
    bool sel = app->sel_hero == i;
    gfx_fill_rect(ui->gfx, row, sel ? COL_PARCHMENT_LIGHT : COL_PARCHMENT);
    gfx_draw_rect(ui->gfx, row, sel ? COL_GOLD : COL_PARCHMENT_DARK, sel ? 2 : 1);
    UiClick click = ui_region(ui, row, ui_make_id(ui, "row", row.x, row.y));
    if (click.clicked) {
        app->sel_hero = i;
    }
    int is = row.h - ui_px(ui, 6);
    icon_draw_class(ui->gfx, rect_make(c[COL_ICON].x, row.y + (row.h - is) / 2, is, is), h->cls, h->look);
    char buf[96], num[40];
    draw_cell(ui, c[COL_NAME], h->name, COL_INK);
    draw_cell(ui, c[COL_CLASS], ui_class_name(h->cls), COL_INK);
    draw_cell(ui, c[COL_LEVEL], ui_num(h->level, num, sizeof num), COL_INK);
    draw_cell(ui, c[COL_MAP], ui_map_label(h->assigned_map, buf, sizeof buf), COL_INK);
    float frac;
    hero_hp_text(app, i, buf, sizeof buf, &frac);
    Rect hb = rect_inset(c[COL_HPC], ui_px(ui, 6));
    hb.w -= ui_px(ui, 6);
    ui_bar(ui, hb, frac, COL_HP, COL_HP_DARK, buf);
    str_format(buf, sizeof buf, "%d / %d", hero_chest_total(h), hero_chest_capacity(&app->game, h));
    draw_cell(ui, c[COL_CHEST], buf, COL_INK);
    Color sc;
    hero_status_text(h, buf, sizeof buf, &sc);
    draw_cell(ui, c[COL_STAT], buf, sc);
    row_buttons(app, row, i);
    ui_pop_id(ui);
}

static void draw_free_row(App *app, Rect row, int slot) {
    Ui *ui = &app->ui;
    ui_push_id(ui, slot);
    gfx_fill_rect(ui->gfx, row, COL_PARCHMENT_DARK);
    gfx_draw_rect(ui->gfx, row, COL_WOOD, 1);
    char b[80];
    str_format(b, sizeof b, "%s %d - %s", tr("Platz"), slot + 1, tr("frei"));
    Rect t = rect_make(row.x + ui_px(ui, 12), row.y, row.w / 2, row.h);
    draw_cell(ui, t, b, COL_INK_SOFT);
    int bw = ui_px(ui, 160), bh = ui_px(ui, 28);
    Rect br = rect_make(row.x + row.w - bw - ui_px(ui, 8), row.y + (row.h - bh) / 2, bw, bh);
    if (ui_button_ex(ui, br, tr("Held erstellen"), UI_BTN_GREEN, true)) {
        s_dlg_open = true;
        s_name_touched = false;
        str_copy(s_dlg_name, sizeof s_dlg_name, g_classes[s_dlg_cls].name);
        app_play(app, SFX_CLICK);
    }
    ui_pop_id(ui);
}

static void draw_locked_row(Ui *ui, Rect row, int slot) {
    gfx_fill_rect(ui->gfx, row, RGBA(0, 0, 0, 70));
    gfx_draw_rect(ui->gfx, row, COL_WOOD_DARK, 1);
    char b[96];
    str_format(b, sizeof b, "%s %d - %s %s %d", tr("Platz"), slot + 1, tr("gesperrt"), tr("ab Spielerlevel"),
               slot_unlock_level(slot));
    draw_cell(ui, rect_make(row.x + ui_px(ui, 12), row.y, row.w - ui_px(ui, 24), row.h), b, COL_PARCHMENT_DARK);
}

/* ---- Heldenerstellung -------------------------------------------------- */
static void dlg_class_tiles(App *app, Rect r) {
    Ui *ui = &app->ui;
    int gap = ui_px(ui, 8);
    int cols = 4;
    int tw = (r.w - gap * (cols - 1)) / cols;
    int th = ui_px(ui, 88);
    for (int c = 0; c < CLASS_COUNT; c++) {
        Rect t = rect_make(r.x + (c % cols) * (tw + gap), r.y + (c / cols) * (th + gap), tw, th);
        ui_push_id(ui, c);
        UiClick k = ui_region(ui, t, ui_make_id(ui, "cls", t.x, t.y));
        bool on = c == s_dlg_cls;
        gfx_fill_rect(ui->gfx, t, on ? COL_PARCHMENT_LIGHT : (k.hovered ? COL_PARCHMENT : COL_PARCHMENT_DARK));
        gfx_draw_rect(ui->gfx, t, on ? COL_WAX_RED : COL_WOOD, on ? 3 : 1);
        int is = th - ui_px(ui, 34);
        icon_draw_class(ui->gfx, rect_make(t.x + (t.w - is) / 2, t.y + ui_px(ui, 4), is, is), c, s_dlg_look);
        ui_text_center(ui, rect_make(t.x, t.y + th - ui_px(ui, 26), t.w, ui_px(ui, 22)), g_classes[c].name, COL_INK,
                       FS_SMALL);
        if (k.clicked) {
            if (!s_name_touched) {
                str_copy(s_dlg_name, sizeof s_dlg_name, g_classes[c].name);
            }
            s_dlg_cls = c;
        }
        ui_pop_id(ui);
    }
}

static void dlg_class_info(Ui *ui, Rect r) {
    const ClassDef *cd = &g_classes[s_dlg_cls];
    char b[160];
    ui_text(ui, r.x, r.y, cd->name, COL_WAX_RED, FS_BIG);
    int y = r.y + ui_px(ui, 26);
    str_format(b, sizeof b, "%s: %s | %s: %s", tr("Rolle"), cd->role, tr("Volk"), cd->race);
    ui_text(ui, r.x, y, b, COL_INK_SOFT, FS_SMALL);
    y += ui_px(ui, 20);
    str_format(b, sizeof b, "%s: %s | %s: %s | %s: %s", tr("Waffe"), cd->weapon_name, tr("Zweit"), cd->sub_name,
               tr("Angriff"), cd->basic_attack);
    ui_text(ui, r.x, y, b, COL_INK_SOFT, FS_SMALL);
    y += ui_px(ui, 24);
    ui_label_wrapped(ui, rect_make(r.x, y, r.w, r.y + r.h - y), cd->desc, COL_INK, fsz(ui, FS_SMALL));
}

static void dlg_create(App *app) {
    int idx = game_create_hero(&app->game, s_dlg_name, s_dlg_cls, (u8)s_dlg_look, s_dlg_auto);
    if (idx < 0) {
        ui_result(app, false, tr("Kein freier Heldenplatz"), NULL);
        return;
    }
    app->sel_hero = idx;
    s_dlg_open = false;
    ui_result(app, true, NULL, tr("Held erstellt"));
}

static void draw_dialog(App *app, Rect area) {
    Ui *ui = &app->ui;
    gfx_fill_rect(ui->gfx, rect_make(0, 0, app->gfx.w, app->gfx.h), RGBA(0, 0, 0, 150));
    int w = mini(ui_px(ui, 760), area.w - ui_px(ui, 20));
    int h = mini(ui_px(ui, 540), area.h - ui_px(ui, 20));
    Rect r = rect_make(area.x + (area.w - w) / 2, area.y + (area.h - h) / 2, w, h);
    Rect in = ui_panel(ui, r, tr("Held erstellen"));
    in = rect_inset(in, ui_px(ui, 10));
    int gap = ui_px(ui, 10);
    Rect tiles = rect_make(in.x, in.y, in.w, ui_px(ui, 88) * 2 + ui_px(ui, 8));
    dlg_class_tiles(app, tiles);
    int y = tiles.y + tiles.h + gap;
    int foot = ui_px(ui, 40);
    dlg_class_info(ui, rect_make(in.x, y, in.w, in.y + in.h - foot - ui_px(ui, 44) - y));
    /* Name / Look / Auto-Verteilung */
    int fy = in.y + in.h - foot - ui_px(ui, 44);
    int fh = ui_px(ui, 32);
    ui_text(ui, in.x, fy + ui_px(ui, 8), tr("Name"), COL_INK, FS_SMALL);
    Rect nr = rect_make(in.x + ui_px(ui, 56), fy, ui_px(ui, 200), fh);
    if (ui_text_input(ui, nr, s_dlg_name, sizeof s_dlg_name, "heronme")) {
        s_name_touched = true;
    }
    Rect lr = rect_make(nr.x + nr.w + gap, fy, ui_px(ui, 110), fh);
    char lb[32], opt[8][16];
    const char *opts[8];
    for (int i = 0; i < 8; i++) {
        str_format(opt[i], sizeof opt[i], "%s %d", tr("Look"), i + 1);
        opts[i] = opt[i];
    }
    (void)lb;
    ui_cycle(ui, lr, opts, 8, &s_dlg_look);
    Rect cb = rect_make(lr.x + lr.w + gap, fy, in.x + in.w - lr.x - lr.w - gap, fh);
    ui_checkbox(ui, cb, tr("Punkte automatisch verteilen"), &s_dlg_auto);
    /* Knoepfe */
    int bw = ui_px(ui, 150), by = in.y + in.h - foot + ui_px(ui, 4), bh = ui_px(ui, 34);
    bool ok = s_dlg_name[0] != 0;
    if (ui_button_ex(ui, rect_make(in.x + in.w - 2 * bw - gap, by, bw, bh), tr("Erstellen"), UI_BTN_GREEN, ok)) {
        dlg_create(app);
    }
    if (ui_button_ex(ui, rect_make(in.x + in.w - bw, by, bw, bh), tr("Abbrechen"), UI_BTN_WAX, true) ||
        ui->in.key_pressed[KEY_ESCAPE]) {
        s_dlg_open = false;
    }
}

/* ---- Hauptfunktion ----------------------------------------------------- */
static void draw_list(App *app, Rect area) {
    Ui *ui = &app->ui;
    Game *g = &app->game;
    int unlocked = game_hero_slots(g);
    char b[120];
    str_format(b, sizeof b, "%s: %d / %d  (%s %d)", tr("Heldenplaetze"), game_hero_count(g), unlocked,
               tr("maximal"), MAX_HEROES);
    int pad = ui_px(ui, 12);
    ui_text(ui, area.x + pad, area.y + ui_px(ui, 8), b, COL_PARCHMENT, FS_BIG);
    Rect list = rect_make(area.x + pad, area.y + ui_px(ui, 42), area.w - 2 * pad, area.h - ui_px(ui, 54));
    Rect head = rect_make(list.x, list.y, list.w, ui_px(ui, 24));
    draw_header(ui, head);
    int rh = ui_px(ui, 44), gap = ui_px(ui, 3);
    Rect body = rect_make(list.x, head.y + head.h + gap, list.w, list.h - head.h - gap);
    int content = MAX_HEROES * (rh + gap);
    int off = ui_scroll_begin(ui, body, content, "herolist");
    int rw = body.w - (content > body.h ? ui_px(ui, 10) : 0);
    for (int i = 0; i < MAX_HEROES; i++) {
        Rect row = rect_make(body.x, body.y + i * (rh + gap) - off, rw, rh);
        if (g->heroes[i].used) {
            draw_hero_row(app, row, i);
        } else if (i < unlocked) {
            draw_free_row(app, row, i);
        } else {
            draw_locked_row(ui, row, i);
        }
    }
    ui_scroll_end(ui);
}

static void handle_delete(App *app) {
    if (!s_del_open) {
        return;
    }
    char t[128];
    str_format(t, sizeof t, "%s \"%s\" %s", tr("Held"),
               s_del_hero >= 0 ? app->game.heroes[s_del_hero].name : "", tr("wirklich loeschen? Das kann nicht rueckgaengig gemacht werden."));
    int r = ui_confirm(&app->ui, &s_del_open, tr("Held loeschen"), t);
    if (r == 1 && s_del_hero >= 0) {
        game_delete_hero(&app->game, s_del_hero);
        app_ensure_hero(app);
        app_message(app, "%s", tr("Held geloescht"));
    }
}

void screen_herolist(App *app, Rect area) {
    Ui *ui = &app->ui;
    gfx_fill_rect(ui->gfx, area, COL_WOOD_DARK);
    ui->modal_open = s_dlg_open;
    draw_list(app, area);
    if (s_dlg_open) {
        ui->modal_open = false;
        draw_dialog(app, area);
        ui->modal_open = s_dlg_open;
        return;
    }
    handle_delete(app);
}
