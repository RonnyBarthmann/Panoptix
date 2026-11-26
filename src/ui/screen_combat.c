/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Helden-Vollbild (Spec 18 Abschnitt 2): Viewport, HUD, Skill-/Trank-/Kistenleiste. */
#include <math.h>

#include "ui/ui_internal.h"

static const char *const k_skill_key_names[SKILL_SLOTS] = {"Q", "W", "E", "R"};

/* Alles, was die Teilfunktionen ueber den aktuellen Helden wissen muessen. */
typedef struct CombatCtx {
    App *app;
    Ui *ui;
    Hero *hero;
    int hero_idx;
    Run *run;          /* NULL = Held ohne laufenden Run */
    int slot;          /* Slot im Run oder -1 */
    RunHero *rh;       /* NULL wenn kein Run */
} CombatCtx;

/* ---- Statustext -------------------------------------------------------- */
/* Liefert Hinweis, warum kein Viewport gezeigt wird (oder NULL). */
static const char *status_text(const CombatCtx *c, char *buf, size_t size) {
    const Hero *h = c->hero;
    if (h->dead_timer > 0.0f) {
        char d[24];
        str_format(buf, size, "%s - %s %s", tr("Held ist gefallen"), tr("Wiedereintritt in"),
                   str_format_duration(d, sizeof d, h->dead_timer));
        return buf;
    }
    if (h->assigned_map < 0) {
        return tr("Held keiner Map zugewiesen");
    }
    if (h->retry_timer > 0.0f) {
        char d[24];
        str_format(buf, size, "%s %s", tr("Map wird neu gestartet in"), str_format_duration(d, sizeof d, h->retry_timer));
        return buf;
    }
    if (!c->run || c->slot < 0) {
        return tr("Kein laufender Durchgang");
    }
    return NULL;
}

static void draw_status_overlay(CombatCtx *c, Rect vp, const char *text) {
    Ui *ui = c->ui;
    gfx_fill_rect(ui->gfx, vp, RGB(0x24, 0x1B, 0x14));
    gfx_draw_rect(ui->gfx, vp, COL_WOOD_DARK, 2);
    Rect t = rect_make(vp.x, vp.y + vp.h / 2 - ui_px(ui, 40), vp.w, ui_px(ui, 30));
    ui_text_center(ui, t, text, COL_PARCHMENT, FS_BIG);
    if (c->hero->assigned_map < 0) {
        int bw = ui_px(ui, 200), bh = ui_px(ui, 36);
        Rect b = rect_make(vp.x + (vp.w - bw) / 2, t.y + t.h + ui_px(ui, 12), bw, bh);
        if (ui_btn(c->app, b, tr("Zur Weltkarte"), true)) {
            app_set_screen(c->app, SCR_WORLDMAP);
        }
    }
}

/* ---- Kopfzeile --------------------------------------------------------- */
static void draw_top_row1(CombatCtx *c, Rect row) {
    Ui *ui = c->ui;
    const Hero *h = c->hero;
    int is = row.h;
    icon_draw_class(ui->gfx, rect_make(row.x, row.y, is, is), h->cls, h->look);
    char b[128], b2[64];
    str_format(b, sizeof b, "%s - %s %s %d", h->name, ui_class_name(h->cls), tr("Lv"), h->level);
    int x = row.x + is + ui_px(ui, 8);
    int ty = row.y + (row.h - gfx_text_height(ui->gfx, fsz(ui, FS_NORMAL))) / 2;
    ui_text(ui, x, ty, b, COL_INK, FS_NORMAL);
    x += gfx_text_width(ui->gfx, b, fsz(ui, FS_NORMAL)) + ui_px(ui, 24);

    int map = h->assigned_map;
    float rate = c->rh ? c->rh->exp_rate : 1.0f;
    if (c->run) {
        map = c->run->map_idx;
    }
    if (map >= 0 && map < g_map_count && c->rh == NULL) {
        rate = game_exp_rate(h, g_maps[map].level);
    }
    str_format(b, sizeof b, "%s %d %%", tr("EXP-Rate"), (int)(rate * 100.0f + 0.5f));
    ui_text(ui, x, ty, b, rate >= 0.99f ? COL_INK : COL_WAX_RED, FS_NORMAL);
    x += gfx_text_width(ui->gfx, b, fsz(ui, FS_NORMAL)) + ui_px(ui, 24);
    str_format(b, sizeof b, "%s %s", tr("Map"), ui_map_label(map, b2, sizeof b2));
    ui_text(ui, x, ty, b, COL_INK, FS_NORMAL);
    if (c->rh) {
        x += gfx_text_width(ui->gfx, b, fsz(ui, FS_NORMAL)) + ui_px(ui, 24);
        ui_text(ui, x, ty, tr(hero_state_name(c->rh->state)), COL_INK_SOFT, FS_SMALL);
    }
}

/* Auto-Toggle + Zahnrad rechts in der Kopfzeile. */
static void draw_top_buttons(CombatCtx *c, Rect row) {
    Ui *ui = c->ui;
    int gw = row.h, aw = ui_px(ui, 110), gap = ui_px(ui, 6);
    Rect gear = rect_make(row.x + row.w - gw, row.y, gw, row.h);
    Rect aut = rect_make(gear.x - gap - aw, row.y, aw, row.h);
    bool manual = c->rh && c->rh->manual;
    const char *lab = manual ? tr("Manuell") : tr("Auto");
    if (ui_button_tip(ui, aut, lab, manual ? UI_BTN_WAX : UI_BTN_GREEN, c->rh != NULL,
                      tr("Automatik / manuelle Steuerung (Leertaste)"))) {
        run_set_manual(c->run, c->slot, !manual);
        app_play(c->app, SFX_CLICK);
    }
    if (ui_button_tip(ui, gear, "*", UI_BTN_NORMAL, true, tr("Skills und KI-Einstellungen"))) {
        app_play(c->app, SFX_CLICK);
        app_set_screen(c->app, SCR_SKILLS);
    }
}

static void draw_top_bars(CombatCtx *c, Rect row) {
    Ui *ui = c->ui;
    const DerivedStats *st = hero_stats(&c->app->game, c->hero);
    float hp = st->max_hp, mp = st->max_mana, mhp = st->max_hp, mmp = st->max_mana;
    if (c->rh) {
        hp = c->rh->u.hp;
        mhp = c->rh->u.max_hp;
        mp = c->rh->mana;
        mmp = c->rh->max_mana;
    }
    int gap = ui_px(ui, 10);
    int w = (row.w - gap) / 2;
    char b[64], n1[24], n2[24];
    str_format(b, sizeof b, "HP %s / %s", ui_num((i64)hp, n1, sizeof n1), ui_num((i64)mhp, n2, sizeof n2));
    ui_bar(ui, rect_make(row.x, row.y, w, row.h), mhp > 0 ? hp / mhp : 0.0f, COL_HP, COL_HP_DARK, b);
    str_format(b, sizeof b, "%s %s / %s", tr("Mana"), ui_num((i64)mp, n1, sizeof n1), ui_num((i64)mmp, n2, sizeof n2));
    ui_bar(ui, rect_make(row.x + w + gap, row.y, w, row.h), mmp > 0 ? mp / mmp : 0.0f, COL_MANA_BAR, COL_HP_DARK, b);
}

static Rect draw_top(CombatCtx *c, Rect *area) {
    Ui *ui = c->ui;
    int h1 = ui_px(ui, 30), h2 = ui_px(ui, 20), pad = ui_px(ui, 6);
    Rect strip = rect_cut_top(area, h1 + h2 + pad * 3);
    gfx_fill_rect(ui->gfx, strip, COL_PARCHMENT);
    gfx_fill_rect(ui->gfx, rect_make(strip.x, strip.y + strip.h - 2, strip.w, 2), COL_WOOD);
    Rect in = rect_inset(strip, pad);
    Rect r1 = rect_cut_top(&in, h1);
    rect_cut_top(&in, pad);
    draw_top_buttons(c, r1);
    r1.w -= ui_px(ui, 110) + ui_px(ui, 6) * 2 + r1.h;
    draw_top_row1(c, r1);
    draw_top_bars(c, in);
    return strip;
}

/* ---- Skill- und Trankleiste -------------------------------------------- */
static void draw_cooldown(Ui *ui, Rect r, float cd, float cd_max) {
    if (cd <= 0.0f || cd_max <= 0.0f) {
        return;
    }
    float frac = clampf(cd / cd_max, 0.0f, 1.0f);
    float cx = (float)r.x + (float)r.w * 0.5f, cy = (float)r.y + (float)r.h * 0.5f;
    gfx_fill_pie(ui->gfx, cx, cy, (float)r.w * 0.5f, -90.0f, -90.0f + 360.0f * frac, RGBA(0, 0, 0, 160));
    char t[16];
    str_format(t, sizeof t, cd >= 10.0f ? "%.0f" : "%.1f", (double)cd);
    ui_text_center(ui, r, t, COL_WHITE, FS_NORMAL);
}

static void draw_key_badge(Ui *ui, Rect r, const char *key) {
    int s = ui_px(ui, 16);
    Rect b = rect_make(r.x + 1, r.y + 1, s, s);
    gfx_fill_rect(ui->gfx, b, RGBA(0, 0, 0, 170));
    ui_text_center(ui, b, key, COL_PARCHMENT_LIGHT, FS_TINY);
}

static void draw_skill_slot(CombatCtx *c, Rect r, int i) {
    Ui *ui = c->ui;
    int skill = c->hero->skill_slot[i];
    ui_push_id(ui, i);
    gfx_fill_rect(ui->gfx, r, COL_WOOD_DARK);
    UiClick k = ui_region(ui, r, ui_make_id(ui, "skill", r.x, r.y));
    if (skill < 0 || skill >= g_skill_count) {
        gfx_draw_rect(ui->gfx, r, COL_WOOD, 1);
        draw_key_badge(ui, r, k_skill_key_names[i]);
        ui_pop_id(ui);
        return;
    }
    Rect ic = rect_inset(r, 2);
    icon_draw_skill(ui->gfx, ic, skill, 0);
    if (c->rh) {
        draw_cooldown(ui, ic, c->rh->cd[i], c->rh->cd_max[i]);
        bool no_mana = c->rh->mana < g_skills[skill].mana;
        if (no_mana) {
            gfx_fill_rect(ui->gfx, ic, RGBA(20, 40, 120, 90));
        }
    }
    draw_key_badge(ui, r, k_skill_key_names[i]);
    if (k.hovered) {
        ui_tooltip_skill(ui, &c->app->game, skill);
    }
    if (k.clicked && c->rh) {
        run_manual_cast(c->run, &c->app->game, c->slot, i);
    }
    ui_pop_id(ui);
}

/* Trank: Icon + Fuellstand (wiederbefuellend) + Abklingzeit. */
static void draw_potion_slot(CombatCtx *c, Rect r, int i) {
    Ui *ui = c->ui;
    const Item *it = &c->hero->eq[SLOT_POTION1 + i];
    ui_push_id(ui, 100 + i);
    gfx_fill_rect(ui->gfx, r, COL_WOOD_DARK);
    Rect ic = rect_inset(r, 2);
    UiClick k;
    if (item_is_empty(it)) {
        icon_draw_empty_slot(ui->gfx, ic, (EquipSlot)(SLOT_POTION1 + i));
        k = ui_region(ui, r, ui_make_id(ui, "potion", r.x, r.y));
    } else {
        k = ui_item_slot(ui, ic, &c->app->game, c->hero, it, false);
        /* Fuellstand als Balken am unteren Rand (zusaetzlich zum Icon). */
        int bh = ui_px(ui, 5);
        Rect fb = rect_make(ic.x, ic.y + ic.h - bh, ic.w, bh);
        gfx_fill_rect(ui->gfx, fb, RGBA(0, 0, 0, 180));
        gfx_fill_rect(ui->gfx, rect_make(fb.x, fb.y, fb.w * clampi(it->fill, 0, 100) / 100, fb.h),
                      it->fill >= 100 ? COL_HEAL : COL_GOLD);
        if (c->rh) {
            draw_cooldown(ui, ic, c->rh->potion_cd[i], 5.0f);
        }
    }
    char kn[2] = {(char)('1' + i), 0};
    draw_key_badge(ui, r, kn);
    if (k.clicked && c->rh && !item_is_empty(it)) {
        run_manual_potion(c->run, &c->app->game, c->slot, i);
    }
    ui_pop_id(ui);
}

/* ---- Kistenleiste ------------------------------------------------------- */
static int chest_total(const Hero *h) {
    int n = 0;
    for (int t = 0; t < TIER_COUNT; t++) {
        n += h->chest_bar[t];
    }
    return n;
}

static void draw_chest_bar(CombatCtx *c, Rect row) {
    Ui *ui = c->ui;
    int cap = hero_chest_capacity(&c->app->game, c->hero);
    int total = chest_total(c->hero);
    bool warn = cap > 0 && total * 100 >= cap * 70;
    bool blink = warn && ((int)(ui->time * 2.0) & 1);
    char b[64];
    str_format(b, sizeof b, "%s %d / %d", tr("Kisten"), total, cap);
    int tw = gfx_text_width(ui->gfx, b, fsz(ui, FS_NORMAL)) + ui_px(ui, 12);
    ui_text(ui, row.x, row.y + (row.h - gfx_text_height(ui->gfx, fsz(ui, FS_NORMAL))) / 2, b,
            blink ? COL_WAX_RED : COL_INK, FS_NORMAL);
    int x = row.x + tw;
    int cs = row.h;
    for (int t = 0; t < TIER_COUNT; t++) {
        int n = c->hero->chest_bar[t];
        if (n <= 0) {
            continue;
        }
        Rect cr = rect_make(x, row.y, cs, cs);
        ui_push_id(ui, 200 + t);
        gfx_fill_rect(ui->gfx, cr, RGBA(0, 0, 0, 60));
        icon_draw_chest(ui->gfx, rect_inset(cr, 2), (Tier)t, ui->time);
        char nb[16];
        str_format(nb, sizeof nb, "%d", n);
        int nw = gfx_text_width(ui->gfx, nb, fsz(ui, FS_SMALL));
        gfx_text_shadow(ui->gfx, cr.x + cr.w - nw - 1, cr.y + cr.h - gfx_text_height(ui->gfx, fsz(ui, FS_SMALL)), nb,
                        COL_WHITE, fsz(ui, FS_SMALL));
        UiClick k = ui_region(ui, cr, ui_make_id(ui, "chest", cr.x, cr.y));
        if (k.hovered) {
            ui_tooltip(ui, tr(g_tiers[t].name), g_tiers[t].color, tr("Gesammelte Kisten"));
        }
        ui_pop_id(ui);
        x += cs + ui_px(ui, 4);
        if (x + cs > row.x + row.w) {
            break;
        }
    }
}

static void draw_collect_button(CombatCtx *c, Rect r) {
    int total = chest_total(c->hero);
    if (ui_button_tip(c->ui, r, tr("Einsammeln"), UI_BTN_GREEN, total > 0, tr("Kisten ins Lager (F4)"))) {
        int n = game_collect_chests(&c->app->game, c->hero_idx);
        app_message(c->app, "%d %s", n, tr("Kisten eingesammelt"));
        app_play(c->app, SFX_CLICK);
    }
}

static Rect draw_bottom(CombatCtx *c, Rect *area) {
    Ui *ui = c->ui;
    int ss = ui_px(ui, 52), pad = ui_px(ui, 6), cr_h = ui_px(ui, 28);
    Rect strip = rect_cut_bottom(area, ss + cr_h + pad * 3);
    gfx_fill_rect(ui->gfx, strip, COL_PARCHMENT);
    gfx_fill_rect(ui->gfx, rect_make(strip.x, strip.y, strip.w, 2), COL_WOOD);
    Rect in = rect_inset(strip, pad);
    Rect r1 = rect_cut_top(&in, ss);
    rect_cut_top(&in, pad);
    int gap = ui_px(ui, 6);
    int x = r1.x;
    for (int i = 0; i < SKILL_SLOTS; i++, x += ss + gap) {
        draw_skill_slot(c, rect_make(x, r1.y, ss, ss), i);
    }
    x += ui_px(ui, 18);
    for (int i = 0; i < POTION_SLOTS; i++, x += ss + gap) {
        draw_potion_slot(c, rect_make(x, r1.y, ss, ss), i);
    }
    int bw = ui_px(ui, 140);
    draw_collect_button(c, rect_make(r1.x + r1.w - bw, r1.y + (ss - ui_px(ui, 40)) / 2, bw, ui_px(ui, 40)));
    draw_chest_bar(c, in);
    return strip;
}

/* ---- Viewport und manuelle Steuerung ------------------------------------ */
static void handle_viewport_input(CombatCtx *c, Rect vp) {
    Ui *ui = c->ui;
    const UiInput *in = &ui->in;
    ViewFx *fx = &c->app->fx[c->hero_idx];
    if (ui->modal_open || !rect_contains(vp, in->mx, in->my)) {
        return;
    }
    if (in->wheel != 0) {
        float z = fx->zoom > 0.01f ? fx->zoom : 1.0f;
        fx->zoom = clampf(z + (float)in->wheel * 0.1f, 0.75f, 1.5f);
    }
    if (!in->lmb_pressed || !c->rh || !c->rh->u.alive) {
        return;
    }
    if (!c->rh->manual) {
        run_set_manual(c->run, c->slot, true);
        app_message(c->app, "%s", tr("Manuelle Steuerung"));
    }
    Vec2 w = view_screen_to_world(fx, vp, in->mx, in->my);
    int e = run_enemy_at(c->run, w, 6.0f);
    if (e >= 0) {
        run_manual_attack(c->run, c->slot, e);
    } else {
        run_manual_move_to(c->run, c->slot, w);
    }
}

static void draw_viewport(CombatCtx *c, Rect vp) {
    ViewOpts o = {false, true, true, true, true, true, false};
    view_draw_run(c->ui->gfx, &c->app->game, c->run, c->slot, &c->app->fx[c->hero_idx], vp, &o);
    handle_viewport_input(c, vp);
}

void screen_combat(App *app, Rect area) {
    app_ensure_hero(app);
    Ui *ui = &app->ui;
    if (!app_has_hero(app)) {
        gfx_fill_rect(ui->gfx, area, COL_PARCHMENT_DARK);
        ui_text_center(ui, area, tr("Kein Held vorhanden - in der Heldenliste (F1) erstellen."), COL_INK, FS_BIG);
        return;
    }
    CombatCtx c = {app, ui, app_hero(app), app->sel_hero, NULL, -1, NULL};
    c.run = app->game.hero_run[c.hero_idx];
    if (c.run) {
        c.slot = run_slot_of_hero(c.run, c.hero_idx);
        c.rh = c.slot >= 0 ? &c.run->heroes[c.slot] : NULL;
    }
    Rect rest = area;
    draw_top(&c, &rest);
    draw_bottom(&c, &rest);
    char buf[160];
    const char *st = status_text(&c, buf, sizeof buf);
    if (st) {
        draw_status_overlay(&c, rest, st);
    } else {
        draw_viewport(&c, rest);
    }
}
