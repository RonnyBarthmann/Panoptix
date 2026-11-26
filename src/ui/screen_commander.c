/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Kommandeur-Bildschirm: Traits (4 Baeume), Kommandeur-Skills, Kommandoposten,
 * Kopfgelder, Hauptquest, Post, Erfolge, lokale Rangliste (Spec 20, 18/13). */
#include <stdlib.h>
#include <string.h>

#include "ui/ui_internal.h"

enum { TAB_TRAITS, TAB_SKILLS, TAB_POST, TAB_BOUNTY, TAB_MAIN, TAB_MAIL, TAB_ACH, TAB_RANK, TAB_COUNT };

static int s_tab = TAB_TRAITS;
static bool s_reset_open;

static const char *const k_tab_names[TAB_COUNT] = {"Traits", "Skills", "Kommandoposten", "Kopfgeld",
                                                   "Haupt", "Post", "Erfolge", "Rangliste"};
static const char *const k_tree_names[4] = {"Exploration", "Economy", "Glaube", "Fuehrung"};
static const char *const k_skill_names[CMD_SKILL_COUNT] = {"Entdeckersicht", "Berater", "Glueck",
                                                           "Augen der Fuelle"};
static const char *const k_skill_desc[CMD_SKILL_COUNT] = {
    "Mehr Kisten pro Kill (+25 %)", "Mehr EXP", "Hoehere Kisten-Raritaet", "Mehr Gold"};
static const char *const k_order_names[CMD_ORDER_COUNT] = {"Versorgungsbefehl", "Pluenderungsbefehl",
                                                           "Suchbefehl", "Eliteausbildung"};
static const char *const k_order_desc[CMD_ORDER_COUNT] = {
    "Mehr Drop-Menge", "Hoehere Raritaet", "Mehr Magiesteine (nur Boss-Maps)", "Mehr EXP und Gold"};

/* ---- kleine Helfer ---------------------------------------------------------------- */
static Rect inner_pad(Ui *ui, Rect r) { return rect_inset(r, ui_px(ui, 8)); }

static void say_result(App *app, bool ok, const char *err, const char *ok_text) {
    ui_result(app, ok, err, ok_text);
}

/* ---- Tab Traits ------------------------------------------------------------------- */
static void draw_trait_node(App *app, Rect r, int ti) {
    Ui *ui = &app->ui;
    Game *g = &app->game;
    const TraitDef *t = &g_traits[ti];
    int pts = g->player.traits[ti];
    bool req_ok = t->requires < 0 || g->player.traits[t->requires] > 0;
    bool can = req_ok && pts < t->max_points && commander_points_available(g) > 0;
    char buf[96], tip[MAX_DESC + 64];
    ui_push_id(ui, ti);
    UiClick c = ui_region(ui, r, ui_make_id(ui, "trait", r.x, r.y));
    ui_pop_id(ui);
    Color bg = pts >= t->max_points ? COL_MOSS : (pts > 0 ? COL_PARCHMENT_LIGHT : COL_PARCHMENT_DARK);
    gfx_fill_rect(ui->gfx, r, bg);
    gfx_draw_rect(ui->gfx, r, c.hovered && can ? COL_GOLD : COL_WOOD, 1);
    if (!req_ok) {
        gfx_fill_rect(ui->gfx, r, RGBA(0, 0, 0, 60));
    }
    ui_text(ui, r.x + ui_px(ui, 6), r.y + ui_px(ui, 4), tr(t->name), pts >= t->max_points ? COL_WHITE : COL_INK,
            FS_SMALL);
    str_format(buf, sizeof buf, "%d / %d", pts, t->max_points);
    ui_text_right(ui, rect_make(r.x, r.y + ui_px(ui, 21), r.w - ui_px(ui, 6), ui_px(ui, 16)), buf,
                  pts >= t->max_points ? COL_WHITE : COL_INK_SOFT, FS_SMALL);
    if (c.hovered) {
        str_format(tip, sizeof tip, "%s\n%s", tr(t->desc),
                   req_ok ? "" : tr("Benoetigt einen Punkt im vorherigen Trait"));
        ui_tooltip(ui, tr(t->name), 0xE9DDBF, tip);
    }
    if (c.clicked) {
        char err[128] = "";
        bool ok = commander_spend_trait(g, ti, err);
        say_result(app, ok, err, NULL);
    }
}

static void draw_tree(App *app, Rect r, int tree) {
    Ui *ui = &app->ui;
    gfx_fill_rect(ui->gfx, r, RGBA(255, 255, 255, 40));
    gfx_draw_rect(ui->gfx, r, COL_WOOD_DARK, 1);
    ui_text_center(ui, rect_make(r.x, r.y + ui_px(ui, 4), r.w, ui_px(ui, 22)), tr(k_tree_names[tree]), COL_WOOD_DARK,
                   FS_NORMAL);
    int idx[MAX_TRAITS], n = 0, maxrow = 0;
    for (int i = 0; i < g_trait_count; i++) {
        if (g_traits[i].tree == tree) {
            idx[n++] = i;
            maxrow = maxi(maxrow, g_traits[i].row);
        }
    }
    int nh = ui_px(ui, 40), gap = ui_px(ui, 6), top = r.y + ui_px(ui, 32), pad = ui_px(ui, 6);
    /* Bei zu vielen Zeilen scrollen. */
    int content = (maxrow + 1) * (nh + gap) + gap;
    Rect body = rect_make(r.x, top, r.w, r.y + r.h - top - ui_px(ui, 2));
    char sid[24];
    str_format(sid, sizeof sid, "trees%d", tree);
    int off = ui_scroll_begin(ui, body, content, sid);
    for (int k = 0; k < n; k++) {
        const TraitDef *t = &g_traits[idx[k]];
        int same = 0, pos = 0;
        for (int j = 0; j < n; j++) {
            if (g_traits[idx[j]].row == t->row) {
                if (j < k) {
                    pos++;
                }
                same++;
            }
        }
        int w = (r.w - 2 * pad - (same - 1) * gap) / maxi(same, 1);
        Rect nr = rect_make(r.x + pad + pos * (w + gap), top + gap + t->row * (nh + gap) - off, w, nh);
        if (nr.y + nh >= body.y && nr.y <= body.y + body.h) {
            draw_trait_node(app, nr, idx[k]);
        }
    }
    ui_scroll_end(ui);
}

static void tab_traits(App *app, Rect r) {
    Ui *ui = &app->ui;
    Game *g = &app->game;
    char buf[96];
    int avail = commander_points_available(g);
    str_format(buf, sizeof buf, "%s: %d   (%s %d)", tr("Freie Punkte"), avail, tr("Kommandeur-Level"),
               g->player.level);
    ui_text(ui, r.x, r.y + ui_px(ui, 6), buf, avail > 0 ? COL_MOSS : COL_INK, FS_NORMAL);
    Rect rb = rect_make(r.x + r.w - ui_px(ui, 220), r.y, ui_px(ui, 220), ui_px(ui, 30));
    str_format(buf, sizeof buf, "%s (500 %s)", tr("Zuruecksetzen"), tr("Diamanten"));
    if (ui_button_ex(ui, rb, buf, UI_BTN_NORMAL, g->player.diamonds >= 500)) {
        s_reset_open = true;
    }
    int top = r.y + ui_px(ui, 38), gap = ui_px(ui, 8);
    int cw = (r.w - 3 * gap) / 4;
    for (int t = 0; t < 4; t++) {
        draw_tree(app, rect_make(r.x + t * (cw + gap), top, cw, r.y + r.h - top), t);
    }
    int res = ui_confirm(ui, &s_reset_open, tr("Traits zuruecksetzen"),
                         tr("Alle Trait-Punkte zuruecksetzen? Das kostet 500 Diamanten."));
    if (res == 1) {
        char err[128] = "";
        bool ok = commander_reset_traits(g, err);
        say_result(app, ok, err, tr("Traits zurueckgesetzt"));
    }
}

/* ---- Tab Skills ------------------------------------------------------------------- */
static void tab_skills(App *app, Rect r) {
    Ui *ui = &app->ui;
    Game *g = &app->game;
    int rh = ui_px(ui, 64), gap = ui_px(ui, 8);
    for (int i = 0; i < CMD_SKILL_COUNT; i++) {
        Rect row = rect_make(r.x, r.y + i * (rh + gap), r.w, rh);
        ui_panel_plain(ui, row);
        bool act = commander_skill_active(g, i);
        double rest = g->player.cmd_skill_end[i] - g->play_time;
        double cd = g->player.cmd_skill_cd[i] - g->play_time;
        char b[96], d[32];
        ui_text(ui, row.x + ui_px(ui, 12), row.y + ui_px(ui, 8), tr(k_skill_names[i]), COL_INK, FS_NORMAL);
        ui_text(ui, row.x + ui_px(ui, 12), row.y + ui_px(ui, 30), tr(k_skill_desc[i]), COL_INK_SOFT, FS_SMALL);
        if (act) {
            str_format_duration(d, sizeof d, rest);
            str_format(b, sizeof b, "%s: %s", tr("Aktiv"), d);
        } else if (cd > 0) {
            str_format_duration(d, sizeof d, cd);
            str_format(b, sizeof b, "%s: %s", tr("Abklingzeit"), d);
        } else {
            str_copy(b, sizeof b, tr("Bereit"));
        }
        ui_text(ui, row.x + ui_px(ui, 12), row.y + ui_px(ui, 46), b, act ? COL_MOSS : (cd > 0 ? COL_HP : COL_INK),
                FS_TINY);
        float frac = act ? (float)(rest / 3600.0) : 0.0f;
        Rect bar = rect_make(row.x + row.w * 45 / 100, row.y + ui_px(ui, 22), row.w * 28 / 100, ui_px(ui, 18));
        ui_progress_bar(ui, bar, clampf(frac, 0, 1), COL_MOSS, NULL);
        Rect br = rect_make(row.x + row.w - ui_px(ui, 130), row.y + ui_px(ui, 16), ui_px(ui, 118), ui_px(ui, 32));
        ui_push_id(ui, i);
        if (ui_button_ex(ui, br, tr("Aktivieren"), UI_BTN_WAX, !act && cd <= 0)) {
            char err[128] = "";
            bool ok = commander_activate_skill(g, i, err);
            say_result(app, ok, err, tr("Aktiviert"));
        }
        ui_pop_id(ui);
    }
}

/* ---- Tab Kommandoposten ----------------------------------------------------------- */
static void tab_post(App *app, Rect r) {
    Ui *ui = &app->ui;
    Game *g = &app->game;
    char b[128], n1[24], n2[24];
    ui_num(g->player.credits, n1, sizeof n1);
    ui_num((i64)g_bal.post_storage_cap, n2, sizeof n2);
    str_format(b, sizeof b, "%s: %s / %s", tr("CR-Speicher"), n1, n2);
    ui_text(ui, r.x, r.y, b, COL_INK, FS_NORMAL);
    Rect bar = rect_make(r.x, r.y + ui_px(ui, 24), r.w, ui_px(ui, 20));
    ui_progress_bar(ui, bar, (float)g->player.credits / (float)g_bal.post_storage_cap, COL_GOLD, NULL);
    str_format(b, sizeof b, "%s: +%d CR/h (%s %.1f / %d h)", tr("Ertrag"), (int)g_bal.post_cr_per_hour,
               tr("heute"), g->player.post_hours_today, (int)g_bal.post_max_hours_per_day);
    ui_text(ui, r.x, r.y + ui_px(ui, 50), b, COL_INK_SOFT, FS_SMALL);
    int rh = ui_px(ui, 48), gap = ui_px(ui, 6), y0 = r.y + ui_px(ui, 76);
    for (int i = 0; i < CMD_ORDER_COUNT; i++) {
        Rect row = rect_make(r.x, y0 + i * (rh + gap), r.w, rh);
        ui_panel_plain(ui, row);
        bool on = g->player.cmd_order_on[i] != 0;
        str_format(b, sizeof b, "%s (%d CR/h)", tr(k_order_names[i]), (int)g_bal.post_order_cost_per_hour);
        ui_text(ui, row.x + ui_px(ui, 12), row.y + ui_px(ui, 6), b, COL_INK, FS_NORMAL);
        ui_text(ui, row.x + ui_px(ui, 12), row.y + ui_px(ui, 26), tr(k_order_desc[i]), COL_INK_SOFT, FS_SMALL);
        Rect br = rect_make(row.x + row.w - ui_px(ui, 110), row.y + ui_px(ui, 8), ui_px(ui, 98), ui_px(ui, 32));
        ui_push_id(ui, i);
        if (ui_button_ex(ui, br, on ? tr("AN") : tr("AUS"), on ? UI_BTN_GREEN : UI_BTN_NORMAL, true)) {
            char err[128] = "";
            bool ok = commander_toggle_order(g, i, err);
            say_result(app, ok, err, NULL);
        }
        ui_pop_id(ui);
    }
    int ay = y0 + CMD_ORDER_COUNT * (rh + gap) + ui_px(ui, 6);
    Rect row = rect_make(r.x, ay, r.w, rh + ui_px(ui, 8));
    ui_panel_plain(ui, row);
    str_format(b, sizeof b, "%s (%d CR)", tr("Adjutantenbefehl"), (int)g_bal.adjutant_cost);
    ui_text(ui, row.x + ui_px(ui, 12), row.y + ui_px(ui, 8), b, COL_INK, FS_NORMAL);
    ui_text(ui, row.x + ui_px(ui, 12), row.y + ui_px(ui, 30), tr("Sofort 1 Stunde Idle-Ertrag (EXP, Gold, Kisten)"),
            COL_INK_SOFT, FS_SMALL);
    Rect br = rect_make(row.x + row.w - ui_px(ui, 130), row.y + ui_px(ui, 12), ui_px(ui, 118), ui_px(ui, 32));
    if (ui_button_ex(ui, br, tr("Ausfuehren"), UI_BTN_WAX, g->player.credits >= (i64)g_bal.adjutant_cost)) {
        char err[128] = "";
        bool ok = commander_adjutant(g, err);
        say_result(app, ok, err, NULL);
    }
}

/* ---- Quest-Text und Belohnung ------------------------------------------------------ */
static void quest_text(const QuestDef *q, int n, char *buf, size_t size) {
    const char *t = tr(q->text);
    char num[16];
    str_format(num, sizeof num, "%d", n);
    buf[0] = '\0';
    for (const char *p = t; *p;) {
        if (strncmp(p, "{n}", 3) == 0) {
            str_append(buf, size, num);
            p += 3;
        } else {
            char one[2] = {*p, 0};
            str_append(buf, size, one);
            p++;
        }
    }
}

static void reward_text(const QuestDef *q, char *buf, size_t size) {
    char a[24], b[24];
    str_format(buf, size, "%s EXP", ui_num(q->reward_cmd_exp, a, sizeof a));
    if (q->reward_gold > 0) {
        str_format(a, sizeof a, ", %s Gold", ui_num(q->reward_gold, b, sizeof b));
        str_append(buf, size, a);
    }
    if (q->reward_diamonds > 0) {
        str_format(a, sizeof a, ", %lld %s", (long long)q->reward_diamonds, tr("Diamanten"));
        str_append(buf, size, a);
    }
}

/* ---- Tab Kopfgeld ----------------------------------------------------------------- */
static void tab_bounty(App *app, Rect r) {
    Ui *ui = &app->ui;
    Game *g = &app->game;
    char b[128], num[24];
    i64 cost = quests_refresh_cost(g);
    str_format(b, sizeof b, "%s %s Gold", tr("Neu auswuerfeln:"), ui_num(cost, num, sizeof num));
    Rect rb = rect_make(r.x + r.w - ui_px(ui, 260), r.y, ui_px(ui, 260), ui_px(ui, 30));
    if (ui_button_ex(ui, rb, b, UI_BTN_NORMAL, g->player.gold >= cost)) {
        char err[128] = "";
        quests_refresh_daily(g, true, err);
        say_result(app, err[0] == '\0', err, tr("Kopfgelder erneuert"));
    }
    ui_text(ui, r.x, r.y + ui_px(ui, 6), tr("Taegliche Kopfgelder"), COL_INK, FS_NORMAL);
    Rect list = rect_make(r.x, r.y + ui_px(ui, 38), r.w, r.h - ui_px(ui, 38));
    int rh = ui_px(ui, 52), gap = ui_px(ui, 4);
    int off = ui_scroll_begin(ui, list, BOUNTY_PER_DAY * (rh + gap), "bounty");
    for (int i = 0; i < BOUNTY_PER_DAY; i++) {
        const BountyQuest *q = &g->player.bounty[i];
        if (q->def < 0 || q->def >= g_quest_count) {
            continue;
        }
        Rect row = rect_make(list.x, list.y + i * (rh + gap) - off, list.w - ui_px(ui, 14), rh);
        if (row.y + rh < list.y || row.y > list.y + list.h) {
            continue;
        }
        const QuestDef *d = &g_quests[q->def];
        ui_panel_plain(ui, row);
        quest_text(d, q->target, b, sizeof b);
        ui_text(ui, row.x + ui_px(ui, 10), row.y + ui_px(ui, 5), b, q->claimed ? COL_INK_SOFT : COL_INK, FS_SMALL);
        reward_text(d, b, sizeof b);
        ui_text(ui, row.x + ui_px(ui, 10), row.y + ui_px(ui, 31), b, COL_WOOD, FS_TINY);
        bool done = q->progress >= q->target;
        char pt[32];
        str_format(pt, sizeof pt, "%d / %d", q->progress, q->target);
        Rect bar = rect_make(row.x + row.w * 52 / 100, row.y + ui_px(ui, 16), row.w * 26 / 100, ui_px(ui, 20));
        ui_progress_bar(ui, bar, q->target > 0 ? (float)q->progress / (float)q->target : 0.0f,
                        done ? COL_MOSS : COL_SKY, pt);
        Rect br = rect_make(row.x + row.w - ui_px(ui, 118), row.y + ui_px(ui, 10), ui_px(ui, 108), ui_px(ui, 32));
        ui_push_id(ui, i);
        if (ui_button_ex(ui, br, q->claimed ? tr("Abgeholt") : tr("Abholen"), UI_BTN_WAX, done && !q->claimed)) {
            char err[128] = "";
            bool ok = quest_claim(g, i, err);
            say_result(app, ok, err, tr("Belohnung erhalten"));
        }
        ui_pop_id(ui);
    }
    ui_scroll_end(ui);
}

/* ---- Tab Hauptquest --------------------------------------------------------------- */
static int main_quest_def_index(int n) {
    for (int i = 0; i < g_quest_count; i++) {
        if (g_quests[i].main_quest && n-- == 0) {
            return i;
        }
    }
    return -1;
}

static void tab_main(App *app, Rect r) {
    Ui *ui = &app->ui;
    Game *g = &app->game;
    int m = main_quest_def_index(g->player.main_quest_index);
    char b[160];
    str_format(b, sizeof b, "%s %d", tr("Hauptquest"), g->player.main_quest_index + 1);
    ui_text(ui, r.x, r.y, b, COL_INK, FS_BIG);
    if (m < 0) {
        ui_text(ui, r.x, r.y + ui_px(ui, 40), tr("Alle Hauptquests abgeschlossen."), COL_MOSS, FS_NORMAL);
        return;
    }
    const QuestDef *d = &g_quests[m];
    Rect card = rect_make(r.x, r.y + ui_px(ui, 40), r.w, ui_px(ui, 110));
    ui_panel_plain(ui, card);
    quest_text(d, d->count_min, b, sizeof b);
    ui_text(ui, card.x + ui_px(ui, 12), card.y + ui_px(ui, 10), b, COL_INK, FS_NORMAL);
    reward_text(d, b, sizeof b);
    ui_text(ui, card.x + ui_px(ui, 12), card.y + ui_px(ui, 34), b, COL_WOOD, FS_SMALL);
    int prog = g->player.main_quest_progress;
    char pt[32];
    str_format(pt, sizeof pt, "%d / %d", prog, d->count_min);
    bool done = prog >= d->count_min;
    Rect bar = rect_make(card.x + ui_px(ui, 12), card.y + ui_px(ui, 62), card.w - ui_px(ui, 160), ui_px(ui, 26));
    ui_progress_bar(ui, bar, d->count_min > 0 ? (float)prog / (float)d->count_min : 0.0f, done ? COL_MOSS : COL_SKY, pt);
    Rect br = rect_make(card.x + card.w - ui_px(ui, 130), card.y + ui_px(ui, 60), ui_px(ui, 118), ui_px(ui, 32));
    if (ui_button_ex(ui, br, tr("Abholen"), UI_BTN_WAX, done)) {
        char err[128] = "";
        bool ok = main_quest_claim(g, err);
        say_result(app, ok, err, tr("Belohnung erhalten"));
    }
}

/* ---- Tab Post ---------------------------------------------------------------------- */
static void tab_mail(App *app, Rect r) {
    Ui *ui = &app->ui;
    Game *g = &app->game;
    int rh = ui_px(ui, 64), gap = ui_px(ui, 4);
    if (g->player.mail_count == 0) {
        ui_text(ui, r.x, r.y + ui_px(ui, 6), tr("Keine Nachrichten"), COL_INK_SOFT, FS_NORMAL);
        return;
    }
    int off = ui_scroll_begin(ui, r, g->player.mail_count * (rh + gap), "mail");
    for (int i = 0; i < g->player.mail_count; i++) {
        MailMsg *m = &g->player.mail[i];
        Rect row = rect_make(r.x, r.y + i * (rh + gap) - off, r.w - ui_px(ui, 14), rh);
        if (row.y + rh < r.y || row.y > r.y + r.h) {
            continue;
        }
        ui_panel_plain(ui, row);
        ui_text(ui, row.x + ui_px(ui, 10), row.y + ui_px(ui, 5), m->title, COL_INK, FS_NORMAL);
        ui_label_wrapped(ui, rect_make(row.x + ui_px(ui, 10), row.y + ui_px(ui, 26), row.w - ui_px(ui, 190), ui_px(ui, 34)),
                         m->text, COL_INK_SOFT, ui_px(ui, FS_SMALL));
        if (m->amount > 0) {
            char b[48], n[24];
            int s = ui_px(ui, 18);
            Rect ar = rect_make(row.x + row.w - ui_px(ui, 270), row.y + ui_px(ui, 22), ui_px(ui, 140), s);
            icon_draw_currency(ui->gfx, rect_make(ar.x, ar.y, s, s), m->cur);
            str_format(b, sizeof b, "%s", ui_num(m->amount, n, sizeof n));
            ui_text(ui, ar.x + s + ui_px(ui, 4), ar.y + ui_px(ui, 1), b, COL_INK, FS_NORMAL);
        }
        Rect br = rect_make(row.x + row.w - ui_px(ui, 118), row.y + ui_px(ui, 16), ui_px(ui, 108), ui_px(ui, 32));
        ui_push_id(ui, i);
        if (ui_button_ex(ui, br, m->claimed ? tr("Abgeholt") : tr("Abholen"), UI_BTN_WAX,
                         !m->claimed && m->amount > 0)) {
            say_result(app, mail_claim(g, i), tr("Nicht moeglich"), tr("Anhang abgeholt"));
        }
        ui_pop_id(ui);
        m->read = true;
    }
    ui_scroll_end(ui);
}

/* ---- Tab Erfolge ------------------------------------------------------------------- */
typedef struct AchDef {
    const char *name;
    int counter;
    u64 goal;
} AchDef;

static const AchDef k_ach[] = {
    {"Kopfjaeger: 1.000 Kills", COUNTER_KILLS, 1000},
    {"Schlaechter: 100.000 Kills", COUNTER_KILLS, 100000},
    {"Schatzsucher: 100 Kisten", COUNTER_CHESTS_OPENED, 100},
    {"Truhenkoenig: 5.000 Kisten", COUNTER_CHESTS_OPENED, 5000},
    {"Handwerker: 50 Herstellungen", COUNTER_CRAFTED, 50},
    {"Schmied: 500 Verbesserungen", COUNTER_ENHANCES, 500},
    {"Entdecker: 25 Maps abgeschlossen", COUNTER_MAPS_CLEARED, 25},
    {"Bossjaeger: 50 Bosse", COUNTER_BOSSES, 50},
    {"Haendler des Todes: 100 Todesfaelle", COUNTER_DEATHS, 100},
    {"Zerleger: 1.000 Zerlegungen", COUNTER_DISMANTLED, 1000},
};

static void tab_ach(App *app, Rect r) {
    Ui *ui = &app->ui;
    const Player *p = &app->game.player;
    int n = (int)ARRAY_LEN(k_ach), rh = ui_px(ui, 44), gap = ui_px(ui, 4);
    int off = ui_scroll_begin(ui, r, n * (rh + gap), "ach");
    for (int i = 0; i < n; i++) {
        Rect row = rect_make(r.x, r.y + i * (rh + gap) - off, r.w - ui_px(ui, 14), rh);
        u64 v = p->counters[k_ach[i].counter];
        bool done = v >= k_ach[i].goal;
        ui_panel_plain(ui, row);
        if (done) {
            gfx_fill_rect(ui->gfx, row, RGBA(0x5C, 0x7A, 0x3A, 60));
        }
        ui_text(ui, row.x + ui_px(ui, 10), row.y + ui_px(ui, 12), tr(k_ach[i].name), done ? COL_MOSS : COL_INK, FS_NORMAL);
        char t[40];
        str_format(t, sizeof t, "%llu / %llu", (unsigned long long)(v > k_ach[i].goal ? k_ach[i].goal : v),
                   (unsigned long long)k_ach[i].goal);
        Rect bar = rect_make(row.x + row.w * 55 / 100, row.y + ui_px(ui, 12), row.w * 40 / 100, ui_px(ui, 20));
        ui_progress_bar(ui, bar, (float)((double)(v > k_ach[i].goal ? k_ach[i].goal : v) / (double)k_ach[i].goal),
                        done ? COL_MOSS : COL_SKY, t);
    }
    ui_scroll_end(ui);
}

/* ---- Tab Rangliste ----------------------------------------------------------------- */
typedef struct RankRow {
    int hero;
    float power;
} RankRow;

/* Kampfkraft: Schaden x Haltbarkeit (grobe Kennzahl aus den Werten). */
static float combat_power(App *app, Hero *h) {
    const DerivedStats *s = hero_stats(&app->game, h);
    float dmg = maxf(s->attack_power * s->atk_mult * s->attack_speed, s->spell_power * s->spell_mult * s->cast_speed);
    dmg *= 1.0f + s->crit_chance / 100.0f * (s->crit_dmg / 100.0f - 1.0f);
    float tank = (s->max_hp + s->armor * 2.0f) / maxf(s->physical_taken_mult, 0.1f);
    return dmg * 0.6f + tank * 0.15f;
}

static int rank_cmp(const void *a, const void *b) {
    float d = ((const RankRow *)b)->power - ((const RankRow *)a)->power;
    return d > 0 ? 1 : (d < 0 ? -1 : 0);
}

static void tab_rank(App *app, Rect r) {
    Ui *ui = &app->ui;
    Game *g = &app->game;
    RankRow rows[MAX_HEROES];
    int n = 0;
    for (int i = 0; i < MAX_HEROES; i++) {
        if (g->heroes[i].used) {
            rows[n].hero = i;
            rows[n].power = combat_power(app, &g->heroes[i]);
            n++;
        }
    }
    qsort(rows, (size_t)n, sizeof rows[0], rank_cmp);
    ui_text(ui, r.x, r.y, tr("Lokale Rangliste nach Kampfkraft"), COL_INK, FS_NORMAL);
    int rh = ui_px(ui, 34), gap = ui_px(ui, 3);
    Rect list = rect_make(r.x, r.y + ui_px(ui, 30), r.w, r.h - ui_px(ui, 30));
    int off = ui_scroll_begin(ui, list, n * (rh + gap), "rank");
    for (int i = 0; i < n; i++) {
        const Hero *h = &g->heroes[rows[i].hero];
        Rect row = rect_make(list.x, list.y + i * (rh + gap) - off, list.w - ui_px(ui, 14), rh);
        ui_panel_plain(ui, row);
        char b[96], num[24];
        str_format(b, sizeof b, "%d.", i + 1);
        ui_text(ui, row.x + ui_px(ui, 10), row.y + ui_px(ui, 8), b, i == 0 ? COL_GOLD : COL_INK, FS_NORMAL);
        icon_draw_class(ui->gfx, rect_make(row.x + ui_px(ui, 44), row.y + ui_px(ui, 3), rh - ui_px(ui, 6), rh - ui_px(ui, 6)),
                        h->cls, h->look);
        hero_label(h, b, sizeof b);
        ui_text(ui, row.x + ui_px(ui, 44) + rh, row.y + ui_px(ui, 8), b, COL_INK, FS_NORMAL);
        ui_text_right(ui, rect_make(row.x, row.y + ui_px(ui, 8), row.w - ui_px(ui, 12), ui_px(ui, 20)),
                      ui_num((i64)rows[i].power, num, sizeof num), COL_WOOD_DARK, FS_NORMAL);
    }
    ui_scroll_end(ui);
}

/* ---- Screen ------------------------------------------------------------------------ */
void screen_commander(App *app, Rect area) {
    Ui *ui = &app->ui;
    const char *tabs[TAB_COUNT];
    for (int i = 0; i < TAB_COUNT; i++) {
        tabs[i] = tr(k_tab_names[i]);
    }
    Rect in = ui_panel(ui, area, tr("Kommandeur"));
    Rect tr_ = rect_make(in.x + ui_px(ui, 6), in.y + ui_px(ui, 4), in.w - ui_px(ui, 12), ui_px(ui, 32));
    ui_tabs(ui, tr_, tabs, TAB_COUNT, &s_tab);
    Rect body = inner_pad(ui, rect_make(in.x, in.y + ui_px(ui, 42), in.w, in.h - ui_px(ui, 42)));
    switch (s_tab) {
    case TAB_TRAITS: tab_traits(app, body); break;
    case TAB_SKILLS: tab_skills(app, body); break;
    case TAB_POST: tab_post(app, body); break;
    case TAB_BOUNTY: tab_bounty(app, body); break;
    case TAB_MAIN: tab_main(app, body); break;
    case TAB_MAIL: tab_mail(app, body); break;
    case TAB_ACH: tab_ach(app, body); break;
    default: tab_rank(app, body); break;
    }
}
