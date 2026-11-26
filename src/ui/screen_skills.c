/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Skills-Bildschirm: Skill-Liste (Klasse + Allgemein), 4 Slots, Detail mit
 * KI-Bedingungen je Slot und Held-KI-Einstellungen (Tabs). Spec 05/4, 16/3, 18/12. */
#include <string.h>

#include "ui/ui_internal.h"

#define LIST_MAX 64
#define ROW_H 26

static int s_slot = 0;       /* gewaehlter Skill-Slot */
static int s_focus = -1;     /* Skill im Detail (-1 = Skill des Slots) */
static int s_tab = 0;        /* Tab der Held-KI */
static int s_copy_target = 0;/* Zielheld fuer "Kopieren" (Heldenindex) */

/* ---- Zeilen-Helfer (Label links, Steuerelement rechts) ----------------- */
typedef struct Col {
    Ui *ui;
    int x, y, w;
} Col;

static Rect col_row(Col *c, int h) {
    Rect r = rect_make(c->x, c->y, c->w, h);
    c->y += h + ui_px(c->ui, 3);
    return r;
}

static Rect col_label(Col *c, const char *label, int *ctrl_w) {
    Ui *ui = c->ui;
    Rect r = col_row(c, ui_px(ui, ROW_H));
    int lw = r.w * 45 / 100;
    ui_text(ui, r.x, r.y + ui_px(ui, 5), label, COL_INK, FS_SMALL);
    *ctrl_w = r.w - lw;
    return rect_make(r.x + lw, r.y, r.w - lw, r.h);
}

/* Wert mit -/+ Knoepfen; zero_text (optional) ersetzt die Anzeige bei 0. */
static bool row_stepper(Col *c, const char *label, int *v, int lo, int hi, int step, const char *unit,
                        const char *zero_text) {
    Ui *ui = c->ui;
    int cw;
    Rect r = col_label(c, label, &cw);
    int bs = r.h;
    Rect m = rect_make(r.x, r.y, bs, bs), p = rect_make(r.x + r.w - bs, r.y, bs, bs);
    Rect mid = rect_make(r.x + bs, r.y, r.w - 2 * bs, r.h);
    char b[40];
    if (zero_text && *v == 0) {
        str_copy(b, sizeof b, zero_text);
    } else {
        str_format(b, sizeof b, "%d%s", *v, unit);
    }
    ui_push_id(ui, (int)(intptr_t)label & 0xffff);
    int old = *v;
    int mult = ui->in.shift ? 5 : 1;
    if (ui_button_ex(ui, m, "-", UI_BTN_FLAT, *v > lo)) {
        *v = maxi(lo, *v - step * mult);
    }
    if (ui_button_ex(ui, p, "+", UI_BTN_FLAT, *v < hi)) {
        *v = mini(hi, *v + step * mult);
    }
    ui_pop_id(ui);
    gfx_fill_rect(ui->gfx, mid, COL_PARCHMENT_LIGHT);
    ui_text_center(ui, mid, b, COL_INK, FS_SMALL);
    return *v != old;
}

static bool row_cycle(Col *c, const char *label, const char *const *opts, int n, u8 *v) {
    int cw;
    Rect r = col_label(c, label, &cw);
    int tmp = *v;
    bool ch = ui_cycle(c->ui, r, opts, n, &tmp);
    *v = (u8)tmp;
    return ch;
}

static bool row_check(Col *c, const char *label, u8 *v) {
    Rect r = col_row(c, ui_px(c->ui, ROW_H));
    bool b = *v != 0;
    bool ch = ui_checkbox(c->ui, r, label, &b);
    *v = b ? 1 : 0;
    return ch;
}

static void row_text(Col *c, const char *text, Color col) {
    Rect r = col_row(c, ui_px(c->ui, 20));
    ui_text(c->ui, r.x, r.y + ui_px(c->ui, 2), text, col, FS_SMALL);
}

/* Tier-Filter als Reihe von Schaltern (Bit je Tier). */
static void row_tier_mask(App *app, Col *c, u8 *mask) {
    Ui *ui = &app->ui;
    int per = 3;
    int bw = c->w / per;
    for (int row = 0; row < TIER_COUNT / per; row++) {
        Rect line = col_row(c, ui_px(ui, 24));
        for (int k = 0; k < per; k++) {
            int t = row * per + k;
            Rect b = rect_make(line.x + k * bw, line.y, bw - ui_px(ui, 3), line.h);
            bool on = (*mask >> t) & 1;
            ui_push_id(ui, 100 + t);
            if (ui_button_ex(ui, b, g_tiers[t].name, on ? UI_BTN_GREEN : UI_BTN_FLAT, true)) {
                *mask ^= (u8)(1u << t);
            }
            ui_pop_id(ui);
            if (!on) {
                gfx_fill_rect(ui->gfx, b, RGBA(0, 0, 0, 70));
            }
        }
    }
}

/* ---- Skill-Liste (links) ------------------------------------------------ */
static int collect_skills(int cls, int *out) {
    int n = data_query_skills(cls, out, LIST_MAX);
    n += data_query_skills(CLASS_COMMON, out + n, LIST_MAX - n);
    return n;
}

static bool skill_in_slots(const Hero *h, int skill) {
    for (int i = 0; i < SKILL_SLOTS; i++) {
        if (h->skill_slot[i] == skill) {
            return true;
        }
    }
    return false;
}

static void draw_level_up(App *app, Rect r, int sk) {
    Ui *ui = &app->ui;
    i64 gold = 0;
    int books = 0;
    int lv = app->game.player.skill_level[sk];
    bool can = skill_can_level_up(&app->game, sk, &gold, &books);
    char tip[160], g[24];
    if (lv >= SKILL_MAX_LEVEL) {
        str_copy(tip, sizeof tip, tr("Maximalstufe erreicht"));
    } else {
        str_format(tip, sizeof tip, "%s: %d %s + %s %s", tr("Kosten"), books, tr("Buecher"),
                   ui_num(gold, g, sizeof g), tr("Gold"));
    }
    if (ui_button_tip(ui, r, tr("Merge"), UI_BTN_GREEN, can, tip)) {
        char err[96] = "";
        bool ok = skill_level_up(&app->game, sk, err);
        ui_result(app, ok, err, tr("Skill verbessert"));
    }
}

static void draw_skill_row(App *app, Hero *h, Rect r, int sk) {
    Ui *ui = &app->ui;
    const SkillDef *d = &g_skills[sk];
    int lv = app->game.player.skill_level[sk];
    bool locked = lv < 1;
    ui_push_id(ui, sk);
    UiClick c = ui_region(ui, r, ui_make_id(ui, "skrow", r.x, r.y));
    gfx_fill_rect(ui->gfx, r, c.hovered ? COL_PARCHMENT_LIGHT : COL_PARCHMENT);
    if (skill_in_slots(h, sk)) {
        gfx_draw_rect(ui->gfx, r, COL_MOSS, 2);
    } else if (s_focus == sk) {
        gfx_draw_rect(ui->gfx, r, COL_GOLD, 2);
    }
    int is = ui_px(ui, 38);
    Rect ic = rect_make(r.x + ui_px(ui, 4), r.y + (r.h - is) / 2, is, is);
    icon_draw_skill(ui->gfx, ic, sk, lv);
    int bw = ui_px(ui, 62);
    int tx = ic.x + is + ui_px(ui, 8);
    Color nc = locked ? COL_INK_SOFT : ui_tier(app, d->grade, 255);
    gfx_push_clip(ui->gfx, rect_make(tx, r.y, r.x + r.w - bw - tx - ui_px(ui, 8), r.h));
    ui_text(ui, tx, r.y + ui_px(ui, 5), d->name, nc, FS_SMALL);
    char b[80];
    if (locked) {
        str_copy(b, sizeof b, tr("gesperrt - Skillbuch noetig"));
    } else if (lv >= SKILL_MAX_LEVEL) {
        str_format(b, sizeof b, "%s %d (max)", tr("Lv"), lv);
    } else {
        str_format(b, sizeof b, "%s %d  %s %d/%d", tr("Lv"), lv, tr("Buecher"),
                   (int)app->game.player.skill_books[sk], lv);
    }
    ui_text(ui, tx, r.y + ui_px(ui, 24), b, locked ? COL_WAX_RED : COL_INK_SOFT, FS_TINY);
    gfx_pop_clip(ui->gfx);
    if (locked) {
        gfx_fill_rect(ui->gfx, r, RGBA(40, 30, 20, 60));
    } else {
        draw_level_up(app, rect_make(r.x + r.w - bw - ui_px(ui, 4), r.y + (r.h - ui_px(ui, 26)) / 2, bw, ui_px(ui, 26)), sk);
    }
    if (c.hovered) {
        ui_tooltip_skill(ui, &app->game, sk);
    }
    if (c.clicked) {
        s_focus = sk;
        if (locked) {
            app_message(app, "%s", tr("Skill noch nicht freigeschaltet"));
        } else {
            char err[96] = "";
            bool ok = hero_set_skill(&app->game, app->sel_hero, s_slot, sk, err);
            ui_result(app, ok, err, NULL);
        }
    }
    ui_pop_id(ui);
}

static void draw_skill_list(App *app, Hero *h, Rect r) {
    Ui *ui = &app->ui;
    Rect in = rect_inset(ui_panel(ui, r, tr("Skills")), ui_px(ui, 6));
    static int idx[LIST_MAX * 2];
    int n = collect_skills(h->cls, idx);
    int rh = ui_px(ui, 50), gap = ui_px(ui, 3);
    int bar = ui_px(ui, 10);
    int off = ui_scroll_begin(ui, in, n * (rh + gap), "skill_list");
    for (int i = 0; i < n; i++) {
        Rect row = rect_make(in.x, in.y + i * (rh + gap) - off, in.w - bar, rh);
        if (row.y + rh < in.y || row.y > in.y + in.h) {
            continue;
        }
        draw_skill_row(app, h, row, idx[i]);
    }
    ui_scroll_end(ui);
}

/* ---- Slots (Mitte oben) -------------------------------------------------- */
static void draw_slot(App *app, Hero *h, Rect r, int slot) {
    Ui *ui = &app->ui;
    int sk = h->skill_slot[slot];
    ui_push_id(ui, slot);
    UiClick c = ui_region(ui, r, ui_make_id(ui, "slot", r.x, r.y));
    gfx_fill_rect(ui->gfx, r, c.hovered ? COL_PARCHMENT_LIGHT : COL_PARCHMENT);
    gfx_draw_rect(ui->gfx, r, s_slot == slot ? COL_GOLD : COL_WOOD, s_slot == slot ? 3 : 1);
    int is = ui_px(ui, 44);
    Rect ic = rect_make(r.x + ui_px(ui, 6), r.y + (r.h - is) / 2, is, is);
    char b[96];
    if (sk >= 0 && sk < g_skill_count) {
        icon_draw_skill(ui->gfx, ic, sk, app->game.player.skill_level[sk]);
        const SkillDef *d = &g_skills[sk];
        int tx = ic.x + is + ui_px(ui, 8);
        ui_text(ui, tx, r.y + ui_px(ui, 8), d->name, ui_tier(app, d->grade, 255), FS_NORMAL);
        str_format(b, sizeof b, "%s %d  %.0f Mana  %.1fs CD", tr("Lv"), app->game.player.skill_level[sk], d->mana, d->cooldown);
        ui_text(ui, tx, r.y + ui_px(ui, 30), b, COL_INK_SOFT, FS_TINY);
        Rect x = rect_make(r.x + r.w - ui_px(ui, 30), r.y + ui_px(ui, 6), ui_px(ui, 24), ui_px(ui, 24));
        if (ui_button_tip(ui, x, "x", UI_BTN_WAX, true, tr("Slot leeren"))) {
            char err[96] = "";
            ui_result(app, hero_set_skill(&app->game, app->sel_hero, slot, -1, err), err, NULL);
        }
        if (c.hovered) {
            ui_tooltip_skill(ui, &app->game, sk);
        }
    } else {
        gfx_draw_rect(ui->gfx, ic, COL_WOOD, 1);
        str_format(b, sizeof b, "%s %d: %s", tr("Slot"), slot + 1, tr("leer - Skill in der Liste anklicken"));
        ui_text(ui, ic.x + is + ui_px(ui, 8), r.y + (r.h - ui_px(ui, 14)) / 2, b, COL_INK_SOFT, FS_SMALL);
    }
    if (c.clicked) {
        s_slot = slot;
        s_focus = sk;
    }
    ui_pop_id(ui);
}

static void draw_slots(App *app, Hero *h, Rect r) {
    Ui *ui = &app->ui;
    Rect in = rect_inset(ui_panel(ui, r, tr("Skill-Slots")), ui_px(ui, 6));
    int rh = (in.h - 3 * ui_px(ui, 4)) / SKILL_SLOTS;
    for (int i = 0; i < SKILL_SLOTS; i++) {
        draw_slot(app, h, rect_make(in.x, in.y + i * (rh + ui_px(ui, 4)), in.w, rh), i);
    }
}

/* ---- KI je Skill-Slot (rechts) --------------------------------------------- */
static void draw_skill_ai(App *app, Hero *h, Rect r) {
    Ui *ui = &app->ui;
    SkillAi *a = &h->ai.skill[s_slot];
    int sk = h->skill_slot[s_slot];
    char b[80];
    str_format(b, sizeof b, "%s - %s %d", tr("KI-Bedingungen"), tr("Slot"), s_slot + 1);
    Rect in = rect_inset(ui_panel(ui, r, b), ui_px(ui, 8));
    Rect top = rect_cut_top(&in, ui_px(ui, 30));
    if (ui_button_tip(ui, rect_make(top.x + top.w - ui_px(ui, 130), top.y, ui_px(ui, 130), top.h), tr("Empfohlen"),
                      UI_BTN_WAX, sk >= 0, tr("Standardwerte des Skills uebernehmen"))) {
        skill_ai_from_def(a, &g_skills[sk]);
        app->game.dirty_save = true;
    }
    if (sk < 0) {
        ui_label_wrapped(ui, in, tr("Slot ist leer."), COL_INK_SOFT, fsz(ui, FS_SMALL));
        return;
    }
    int rows = 17;
    int content = rows * ui_px(ui, ROW_H + 3) + ui_px(ui, 40);
    int off = ui_scroll_begin(ui, in, content, "skill_ai");
    Col c = {ui, in.x, in.y - off, in.w - ui_px(ui, 12)};
    static const char *const use_names[] = {"Immer", "Im Kampf", "Beim Laufen"};
    const char *use_tr[3] = {tr(use_names[0]), tr(use_names[1]), tr(use_names[2])};
    const char *tgt_tr[2] = {tr("Selbst"), tr("Verbuendete")};
    int v;
#define STEP(label, field, lo, hi, st, unit, zero) \
    v = a->field; row_stepper(&c, tr(label), &v, lo, hi, st, unit, zero); a->field = (__typeof__(a->field))v
    STEP("Prioritaet (0 = zuerst)", priority, 0, 8, 1, "", NULL);
    row_text(&c, tr("Zieltier-Filter"), COL_INK);
    row_tier_mask(app, &c, &a->tier_mask);
    STEP("Mindestgegner", min_targets, 0, 10, 1, "", tr("egal"));
    STEP("Max. Distanz", max_dist, 0, 1000, 25, " px", tr("egal"));
    STEP("Eigene HP min", self_hp_min, 0, 100, 5, " %", NULL);
    STEP("Eigene HP max", self_hp_max, 0, 100, 5, " %", NULL);
    STEP("Eigenes Mana min", self_mana_min, 0, 100, 5, " %", NULL);
    STEP("Eigenes Mana max", self_mana_max, 0, 100, 5, " %", NULL);
    STEP("Verbuendete HP unter", ally_hp_below, 0, 100, 5, " %", tr("egal"));
    row_cycle(&c, tr("Bevorzugtes Ziel"), tgt_tr, 2, &a->target_allies);
    row_cycle(&c, tr("Nutzung"), use_tr, 3, &a->use_mode);
    row_check(&c, tr("Buff nachlegen (stapeln)"), &a->allow_stack);
    v = a->min_cd_x10;
    row_stepper(&c, tr("Mindest-Abklingzeit"), &v, 0, 900, 5, " /10 s", tr("Skill-CD"));
    a->min_cd_x10 = (u16)v;
    STEP("Streuwinkel", spread_angle, 0, 90, 5, " Grad", tr("Standard"));
#undef STEP
    ui_scroll_end(ui);
}

/* ---- Detail (rechts oben) -------------------------------------------------- */
static void draw_detail(App *app, Hero *h, Rect r) {
    Ui *ui = &app->ui;
    int sk = s_focus >= 0 ? s_focus : h->skill_slot[s_slot];
    Rect in = rect_inset(ui_panel(ui, r, tr("Detail")), ui_px(ui, 8));
    if (sk < 0 || sk >= g_skill_count) {
        ui_label_wrapped(ui, in, tr("Skill waehlen."), COL_INK_SOFT, fsz(ui, FS_SMALL));
        return;
    }
    ui_text(ui, in.x, in.y, g_skills[sk].name, ui_tier(app, g_skills[sk].grade, 255), FS_BIG);
    in.y += ui_px(ui, 28);
    in.h -= ui_px(ui, 28);
    char buf[900];
    skill_describe(&app->game, sk, buf, sizeof buf);
    int off = ui_scroll_begin(ui, in, ui_px(ui, 18) * 14, "skill_detail");
    ui_label_wrapped(ui, rect_make(in.x, in.y - off, in.w - ui_px(ui, 12), ui_px(ui, 18) * 14), buf, COL_INK,
                     fsz(ui, FS_SMALL));
    ui_scroll_end(ui);
}

/* ---- Held-KI (Tabs) ---------------------------------------------------------- */
static void tab_move(App *app, Hero *h, Col *c) {
    const char *ex[3] = {tr("Kuerzester Weg"), tr("Voller Pfad"), tr("Minimal (Boss-Rush)")};
    const char *cl[2] = {tr("Vollstaendige Vernichtung"), tr("Boss direkt")};
    const char *sp[2] = {tr("Keine Anpassung"), tr("An Verbuendete angleichen")};
    (void)app;
    row_cycle(c, tr("Erkundung"), ex, 3, &h->ai.explore);
    row_cycle(c, tr("Raeumung"), cl, 2, &h->ai.clear);
    row_cycle(c, tr("Tempo"), sp, 2, &h->ai.speed_mode);
    row_check(c, tr("Gleiche Map wiederholen"), &h->ai.repeat_map);
    row_check(c, tr("Tempel/Ruinen automatisch erneut"), &h->ai.auto_repeat_special);
}

static void tab_targets(App *app, Hero *h, Col *c) {
    const char *pr[3] = {tr("Hoeheres Tier zuerst"), tr("Niedrigeres Tier zuerst"), tr("Naechster zuerst")};
    row_cycle(c, tr("Zielpriorisierung"), pr, 3, &h->ai.target_prio);
    row_text(c, tr("Tier-Filter (angreifen)"), COL_INK);
    row_tier_mask(app, c, &h->ai.target_tier_mask);
    row_check(c, tr("Fokus nach 3 s ausser Reichweite verlieren"), &h->ai.refocus_lost);
}

static void tab_potions(App *app, Hero *h, Col *c) {
    (void)app;
    for (int i = 0; i < POTION_SLOTS; i++) {
        PotionAi *p = &h->ai.potion[i];
        char b[48];
        str_format(b, sizeof b, "%s %d", tr("Trank-Slot"), i + 1);
        row_text(c, b, COL_WAX_RED);
        int v = p->hp_below;
        row_stepper(c, tr("HP unter"), &v, 0, 100, 5, " %", tr("nie"));
        p->hp_below = (u8)v;
        v = p->mana_below;
        row_stepper(c, tr("Mana unter"), &v, 0, 100, 5, " %", tr("nie"));
        p->mana_below = (u8)v;
        ui_push_id(c->ui, i);
        row_check(c, tr("Nur im Kampf"), &p->only_in_combat);
        ui_pop_id(c->ui);
    }
}

static void tab_chests(App *app, Hero *h, Col *c) {
    (void)app;
    row_check(c, tr("Normal-Kisten aufnehmen"), &h->ai.take_normal_chests);
    row_check(c, tr("Magische Kisten aufnehmen"), &h->ai.take_magic_chests);
    row_check(c, tr("Kisten sofort oeffnen"), &h->ai.auto_open);
    row_check(c, tr("Auto-Zerlegen (ab Spielerlevel 20)"), &h->ai.auto_dismantle);
    int v = h->ai.collect_at_pct;
    row_stepper(c, tr("Einsammeln bei voll"), &v, 0, 100, 5, " %", tr("aus"));
    h->ai.collect_at_pct = (u8)v;
}

static void draw_preset_buttons(App *app, Hero *h, Rect r) {
    Ui *ui = &app->ui;
    int bw = (r.w - 2 * ui_px(ui, 4)) / 3;
    Rect b1 = rect_make(r.x, r.y, bw, ui_px(ui, 28));
    Rect b2 = rect_make(r.x + bw + ui_px(ui, 4), r.y, bw, b1.h);
    if (ui_button_tip(ui, b1, tr("Klassen-Preset"), UI_BTN_WAX, true, tr("Skills und KI der Klassen-Build-Vorlage"))) {
        bool ok = hero_apply_build_preset(&app->game, app->sel_hero);
        ui_result(app, ok, tr("Vorlage nicht anwendbar"), tr("Klassen-Preset angewendet"));
    }
    if (ui_button_tip(ui, b2, tr("Standard"), UI_BTN_NORMAL, true, tr("KI-Einstellungen zuruecksetzen"))) {
        hero_ai_defaults(h);
        app->game.dirty_save = true;
        app_message(app, "%s", tr("KI auf Standard"));
    }
    /* Kopieren auf anderen Helden */
    int others[MAX_HEROES], n = 0;
    const char *names[MAX_HEROES];
    for (int i = 0; i < MAX_HEROES; i++) {
        if (app->game.heroes[i].used && i != app->sel_hero) {
            others[n] = i;
            names[n++] = app->game.heroes[i].name;
        }
    }
    Rect b3 = rect_make(r.x + 2 * (bw + ui_px(ui, 4)), r.y, bw, b1.h);
    if (n == 0) {
        ui_button_ex(ui, b3, tr("Kopieren"), UI_BTN_FLAT, false);
        return;
    }
    s_copy_target = clampi(s_copy_target, 0, n - 1);
    Rect cyc = rect_make(b3.x, b3.y, b3.w - ui_px(ui, 70), b3.h);
    Rect go = rect_make(b3.x + cyc.w + ui_px(ui, 2), b3.y, ui_px(ui, 68), b3.h);
    ui_cycle(ui, cyc, names, n, &s_copy_target);
    if (ui_button_tip(ui, go, tr("Kopieren"), UI_BTN_GREEN, true, tr("KI-Einstellungen auf den gewaehlten Helden kopieren"))) {
        Hero *dst = &app->game.heroes[others[s_copy_target]];
        dst->ai = h->ai;
        if (dst->cls == h->cls) {
            memcpy(dst->skill_slot, h->skill_slot, sizeof dst->skill_slot);
        }
        dst->stats_dirty = true;
        app->game.dirty_save = true;
        app_message(app, "%s -> %s", tr("KI kopiert"), dst->name);
    }
}

void screen_skills_ai_panel(App *app, Rect r, Hero *h) {
    Ui *ui = &app->ui;
    Rect in = rect_inset(ui_panel(ui, r, tr("Held-KI")), ui_px(ui, 8));
    static const char *const tabs[4] = {"Bewegung", "Ziele", "Traenke", "Kisten"};
    const char *tt[4] = {tr(tabs[0]), tr(tabs[1]), tr(tabs[2]), tr(tabs[3])};
    ui_tabs(ui, rect_cut_top(&in, ui_px(ui, 28)), tt, 4, &s_tab);
    Rect bottom = rect_cut_bottom(&in, ui_px(ui, 32));
    in.y += ui_px(ui, 4);
    in.h -= ui_px(ui, 4);
    int off = ui_scroll_begin(ui, in, ui_px(ui, 400), "hero_ai");
    Col c = {ui, in.x, in.y - off, in.w - ui_px(ui, 12)};
    switch (s_tab) {
    case 0: tab_move(app, h, &c); break;
    case 1: tab_targets(app, h, &c); break;
    case 2: tab_potions(app, h, &c); break;
    default: tab_chests(app, h, &c); break;
    }
    ui_scroll_end(ui);
    draw_preset_buttons(app, h, rect_make(bottom.x, bottom.y + ui_px(ui, 4), bottom.w, bottom.h));
}

/* Popup (z.B. aus dem Kampfbildschirm): zentriertes Panel; false sobald geschlossen. */
bool screen_combat_ai_popup(App *app, Rect area, Hero *h) {
    Ui *ui = &app->ui;
    int w = mini(area.w - ui_px(ui, 20), ui_px(ui, 560));
    int hh = mini(area.h - ui_px(ui, 20), ui_px(ui, 480));
    Rect r = rect_make(area.x + (area.w - w) / 2, area.y + (area.h - hh) / 2, w, hh);
    Rect body = r;
    Rect close = rect_cut_bottom(&body, ui_px(ui, 34));
    gfx_fill_rect(ui->gfx, area, RGBA(0, 0, 0, 120));
    screen_skills_ai_panel(app, body, h);
    if (ui_button_ex(ui, rect_make(close.x + close.w - ui_px(ui, 120), close.y + ui_px(ui, 4), ui_px(ui, 120), ui_px(ui, 28)),
                     tr("Schliessen"), UI_BTN_NORMAL, true) ||
        ui->in.key_pressed[KEY_ESCAPE]) {
        return false;
    }
    return true;
}

/* ---- Bildschirm ------------------------------------------------------------------ */
void screen_skills(App *app, Rect area) {
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
    s_slot = clampi(s_slot, 0, SKILL_SLOTS - 1);
    int lw = mini(ui_px(ui, 330), r.w * 28 / 100);
    int rw = mini(ui_px(ui, 420), r.w * 34 / 100);
    Rect left = rect_cut_left(&r, lw);
    rect_cut_left(&r, pad);
    Rect right = rect_cut_right(&r, rw);
    rect_cut_right(&r, pad);
    draw_skill_list(app, h, left);
    Rect slots = rect_cut_top(&r, ui_px(ui, 4 * 62 + 40));
    r.y += pad;
    r.h -= pad;
    draw_slots(app, h, slots);
    screen_skills_ai_panel(app, r, h);
    Rect detail = rect_cut_top(&right, mini(ui_px(ui, 190), right.h / 3));
    right.y += pad;
    right.h -= pad;
    draw_detail(app, h, detail);
    draw_skill_ai(app, h, right);
}
