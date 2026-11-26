/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Weltkarte / Map-Auswahl (Spec 14, Spec 18 Abschnitt 7): Kapitel-Tabs, Akt-Leiste,
 * Knotennetz, Seitenpanel "Map-Info" mit Heldenzuweisung. */
#include <math.h>
#include <string.h>

#include "ui/ui_internal.h"

#define TAB_TEMPLE 5
#define TAB_RUINS 6
#define TAB_COUNT 7
#define NODE_COLS 5
#define MAX_ACTS 8
#define INFO_TAB_INFO 0
#define INFO_TAB_REWARDS 1
/* Abschluss-Belohnungen (Spiegel der Konstanten in game_events.c, nur Anzeige) */
#define SHOW_GOLD_MULT 10.0f
#define SHOW_EXP_BOSS_MULT 5.0f
#define SHOW_FIRST_DIAMONDS 10

static int s_tab = 0;
static int s_act = 0;               /* Index in s_acts */
static bool s_init = false;
static int s_pick_slot = -1;        /* Team-Slot, fuer den gerade ein Held gewaehlt wird */
static int s_info_tab = INFO_TAB_INFO;

static int s_view[MAX_MAPS];        /* Maps der aktuellen Ansicht (Indizes in g_maps) */
static int s_view_n;
static char s_acts[MAX_ACTS][8];    /* Akt-Schluessel des Kapitels ("1", "10A") */
static int s_act_n;

/* ---- Ansicht aufbauen ----------------------------------------------------------- */
/* Akt-Schluessel einer Story-Map = Teil der ID vor dem '-'. */
static void act_key(const MapDef *m, char *buf, size_t size) {
    size_t i = 0;
    while (m->id[i] && m->id[i] != '-' && i + 1 < size) {
        buf[i] = m->id[i];
        i++;
    }
    buf[i] = '\0';
}

static int tab_of_map(const MapDef *m) {
    if (m->kind == MAPK_TEMPLE) {
        return TAB_TEMPLE;
    }
    if (m->kind == MAPK_RUINS) {
        return TAB_RUINS;
    }
    return clampi(m->chapter - 1, 0, 4);
}

static void collect_acts(void) {
    s_act_n = 0;
    if (s_tab >= TAB_TEMPLE) {
        return;
    }
    for (int i = 0; i < g_map_count && s_act_n < MAX_ACTS; i++) {
        const MapDef *m = &g_maps[i];
        if (m->kind != MAPK_STORY || tab_of_map(m) != s_tab) {
            continue;
        }
        char k[8];
        act_key(m, k, sizeof k);
        bool have = false;
        for (int a = 0; a < s_act_n; a++) {
            have = have || strcmp(s_acts[a], k) == 0;
        }
        if (!have) {
            str_copy(s_acts[s_act_n++], sizeof s_acts[0], k);
        }
    }
    s_act = clampi(s_act, 0, maxi(s_act_n - 1, 0));
}

static void collect_view(void) {
    s_view_n = 0;
    for (int i = 0; i < g_map_count; i++) {
        const MapDef *m = &g_maps[i];
        if (tab_of_map(m) != s_tab) {
            continue;
        }
        if (m->kind == MAPK_STORY && s_act_n > 0) {
            char k[8];
            act_key(m, k, sizeof k);
            if (strcmp(k, s_acts[s_act]) != 0) {
                continue;
            }
        }
        s_view[s_view_n++] = i;
    }
}

static bool view_has(int map_idx) {
    for (int i = 0; i < s_view_n; i++) {
        if (s_view[i] == map_idx) {
            return true;
        }
    }
    return false;
}

/* Tab/Akt passend zur ausgewaehlten Map setzen. */
static void focus_map(int map_idx) {
    if (map_idx < 0 || map_idx >= g_map_count) {
        return;
    }
    const MapDef *m = &g_maps[map_idx];
    s_tab = tab_of_map(m);
    collect_acts();
    char k[8];
    act_key(m, k, sizeof k);
    for (int a = 0; a < s_act_n; a++) {
        if (strcmp(s_acts[a], k) == 0) {
            s_act = a;
        }
    }
}

/* ---- Hilfen ------------------------------------------------------------------------ */
static const char *const k_roman[5] = {"I", "II", "III", "IV", "V"};

static int team_count(const Game *g, int map_idx) {
    int n = 0;
    for (int s = 0; s < MAX_TEAM; s++) {
        int h = g->world.map[map_idx].team[s];
        if (h >= 0 && h < MAX_HEROES && g->heroes[h].used && g->heroes[h].assigned_map == map_idx) {
            n++;
        }
    }
    return n;
}

/* Held im Team-Slot oder -1 (prueft die Gegenseitigkeit der Zuweisung). */
static int slot_hero(const Game *g, int map_idx, int slot) {
    int h = g->world.map[map_idx].team[slot];
    if (h >= 0 && h < MAX_HEROES && g->heroes[h].used && g->heroes[h].assigned_map == map_idx) {
        return h;
    }
    return -1;
}

static Tier node_tier(const MapDef *m) {
    if (m->boss_map) {
        return TIER_BOSS;
    }
    if (m->stone_item >= 0 && m->stone_item < g_item_count) {
        return g_items[m->stone_item].tier;
    }
    return TIER_NORMAL;
}

static int tickets_left(const Game *g, const MapDef *m) {
    return m->kind == MAPK_TEMPLE ? g->world.temple_tickets : g->world.ruins_tickets;
}

/* Warum ist die Map gesperrt? */
static void lock_reason(const Game *g, const MapDef *m, char *buf, size_t size) {
    if (g->player.level < m->min_player_level) {
        str_format(buf, size, "%s %d", tr("Gesperrt: Spielerlevel"), m->min_player_level);
    } else if (m->kind == MAPK_TEMPLE) {
        str_format(buf, size, "%s %d", tr("Gesperrt: Spielerlevel"), (int)g_bal.temple_unlock_level);
    } else if (m->kind == MAPK_RUINS) {
        str_copy(buf, size, tr("Gesperrt: Akt 3 abschliessen"));
    } else {
        str_copy(buf, size, tr("Gesperrt: vorherige Map abschliessen"));
    }
}

/* ---- Knoten zeichnen --------------------------------------------------------------- */
static void draw_lock(Gfx *g, float cx, float cy, float s) {
    gfx_draw_circle(g, cx, cy - s * 0.35f, s * 0.35f, COL_INK, 2.0f);
    gfx_fill_rect(g, rect_make((int)(cx - s * 0.5f), (int)cy - (int)(s * 0.1f), (int)s, (int)(s * 0.75f)), COL_WOOD_DARK);
}

static void draw_check(Gfx *g, float cx, float cy, float s) {
    gfx_line(g, cx - s * 0.5f, cy, cx - s * 0.1f, cy + s * 0.4f, COL_MOSS, 3.0f);
    gfx_line(g, cx - s * 0.1f, cy + s * 0.4f, cx + s * 0.55f, cy - s * 0.45f, COL_MOSS, 3.0f);
}

static void draw_node_heroes(App *app, const MapDef *m, int map_idx, float cx, float cy, float r) {
    Ui *ui = &app->ui;
    int n = team_count(&app->game, map_idx);
    if (n == 0) {
        return;
    }
    int is = ui_px(ui, 20);
    int x = (int)(cx - (float)(n * (is + 2)) / 2.0f);
    int y = (int)(cy + r + 2.0f);
    for (int s = 0; s < MAX_TEAM && s < m->max_heroes; s++) {
        int h = slot_hero(&app->game, map_idx, s);
        if (h >= 0) {
            icon_draw_class(&app->gfx, rect_make(x, y, is, is), app->game.heroes[h].cls, app->game.heroes[h].look);
            x += is + 2;
        }
    }
}

static void node_tooltip(App *app, const MapDef *m, int map_idx, bool unlocked) {
    Ui *ui = &app->ui;
    char tip[400], line[96], rsn[80];
    str_format(tip, sizeof tip, "%s %d", tr("Level"), m->level);
    if (!unlocked) {
        lock_reason(&app->game, m, rsn, sizeof rsn);
        str_format(line, sizeof line, "\n%s", rsn);
        str_append(tip, sizeof tip, line);
    }
    for (int s = 0; s < MAX_TEAM; s++) {
        int h = slot_hero(&app->game, map_idx, s);
        if (h >= 0) {
            const Hero *hero = &app->game.heroes[h];
            str_format(line, sizeof line, "\n%s: %s %d %%", hero->name, tr("EXP-Rate"),
                       (int)(game_exp_rate(hero, m->level) * 100.0f + 0.5f));
            str_append(tip, sizeof tip, line);
        }
    }
    ui_tooltip(ui, m->name, 0x6B4A2F, tip);
}

/* Ein Knoten; true bei Klick. */
static bool draw_node(App *app, const MapDef *m, int map_idx, float cx, float cy, float r) {
    Ui *ui = &app->ui;
    Gfx *g = &app->gfx;
    const MapProgress *mp = &app->game.world.map[map_idx];
    bool unlocked = game_map_unlocked(&app->game, map_idx);
    bool selected = app->selected_map == map_idx;
    Rect hit = rect_make((int)(cx - r), (int)(cy - r), (int)(2 * r), (int)(2 * r));
    ui_push_id(ui, map_idx);
    UiClick c = ui_region(ui, hit, ui_make_id(ui, "node", hit.x, hit.y));
    ui_pop_id(ui);

    Color fill = !unlocked ? COL_PARCHMENT_DARK : (mp->cleared ? COL_PARCHMENT_LIGHT : COL_PARCHMENT);
    Color ring = unlocked ? ui_tier(app, node_tier(m), 255) : COL_INK_SOFT;
    if (selected) {
        gfx_fill_circle(g, cx, cy, r + 6.0f, COL_GOLD);
    } else if (c.hovered) {
        gfx_fill_circle(g, cx, cy, r + 4.0f, COL_SKY);
    }
    gfx_fill_circle(g, cx, cy, r + 2.0f, ring);
    gfx_fill_circle(g, cx, cy, r - 1.0f, fill);
    if (m->boss_map) {
        gfx_draw_circle(g, cx, cy, r - 4.0f, COL_WAX_RED, 2.0f);
    }
    /* Beschriftung: Nummer im Akt */
    char lab[16];
    const char *dash = strchr(m->id, '-');
    str_copy(lab, sizeof lab, dash ? m->id : m->id);
    Rect tr_ = rect_make(hit.x, (int)(cy - r * 0.45f), hit.w, (int)(r * 0.9f));
    ui_text_center(ui, tr_, lab, unlocked ? COL_INK : COL_INK_SOFT, m->boss_map ? FS_NORMAL : FS_SMALL);
    if (!unlocked) {
        draw_lock(g, cx + r * 0.75f, cy - r * 0.7f, r * 0.45f);
    } else if (mp->cleared) {
        draw_check(g, cx + r * 0.75f, cy - r * 0.7f, r * 0.4f);
    }
    draw_node_heroes(app, m, map_idx, cx, cy, r);
    if (c.hovered) {
        node_tooltip(app, m, map_idx, unlocked);
    }
    return c.clicked;
}

/* Verbindungslinien der Knoten (Schlangenpfad). */
static void node_pos(Rect in, int i, int rows, float *cx, float *cy) {
    int row = i / NODE_COLS;
    int col = i % NODE_COLS;
    if (row % 2 == 1) {
        col = NODE_COLS - 1 - col;
    }
    float cw = (float)in.w / (float)NODE_COLS;
    float ch = (float)in.h / (float)maxi(rows, 1);
    *cx = (float)in.x + cw * ((float)col + 0.5f);
    *cy = (float)in.y + ch * ((float)row + 0.5f);
}

static void draw_map_net(App *app, Rect area) {
    Ui *ui = &app->ui;
    ui_panel_plain(ui, area);
    Rect in = rect_inset(area, ui_px(ui, 14));
    int rows = (s_view_n + NODE_COLS - 1) / NODE_COLS;
    float ch = (float)in.h / (float)maxi(rows, 1);
    float cw = (float)in.w / (float)NODE_COLS;
    float r = fminf(cw, ch) * 0.28f;
    r = fminf(r, (float)ui_px(ui, 34));
    for (int i = 0; i + 1 < s_view_n; i++) {
        float x1, y1, x2, y2;
        node_pos(in, i, rows, &x1, &y1);
        node_pos(in, i + 1, rows, &x2, &y2);
        bool done = app->game.world.map[s_view[i]].cleared != 0;
        gfx_line(&app->gfx, x1, y1, x2, y2, done ? COL_MOSS : COL_WOOD, 4.0f);
    }
    for (int i = 0; i < s_view_n; i++) {
        const MapDef *m = &g_maps[s_view[i]];
        float x, y;
        node_pos(in, i, rows, &x, &y);
        float rr = m->boss_map ? r * 1.3f : r;
        if (draw_node(app, m, s_view[i], x, y, rr)) {
            app->selected_map = s_view[i];
            s_pick_slot = -1;
            app_play(app, SFX_CLICK);
        }
    }
}

/* ---- Kopf: Tabs, Akt-Leiste, Tickets ------------------------------------------------ */
static void draw_header(App *app, Rect *r) {
    Ui *ui = &app->ui;
    char names[TAB_COUNT][24];
    const char *labels[TAB_COUNT];
    for (int i = 0; i < 5; i++) {
        str_format(names[i], sizeof names[i], "%s %s", tr("Kapitel"), k_roman[i]);
    }
    str_copy(names[TAB_TEMPLE], sizeof names[0], tr("Tempel"));
    str_copy(names[TAB_RUINS], sizeof names[0], tr("Ruinen"));
    for (int i = 0; i < TAB_COUNT; i++) {
        labels[i] = names[i];
    }
    Rect row = rect_cut_top(r, ui_px(ui, 32));
    Rect tabs = rect_cut_left(&row, mini(row.w, ui_px(ui, 760)));
    if (ui_tabs(ui, tabs, labels, TAB_COUNT, &s_tab)) {
        s_act = 0;
        s_pick_slot = -1;
        collect_acts();
        collect_view();
        if (s_view_n > 0) {
            app->selected_map = s_view[0];
        }
    }
    if (s_tab >= TAB_TEMPLE) {
        char b[96], n[24];
        const Game *g = &app->game;
        int left = s_tab == TAB_TEMPLE ? g->world.temple_tickets : g->world.ruins_tickets;
        str_format(b, sizeof b, "%s %d / %d    %s %s", tr("Tickets heute"), left, (int)g_bal.temple_tickets_per_day,
                   tr("Rabenmuenzen"), ui_num(currency_get(g, CUR_RAVEN_COINS), n, sizeof n));
        ui_text_right(ui, row, b, COL_PARCHMENT, FS_NORMAL);
    }
    rect_cut_top(r, ui_px(ui, 4));
    if (s_act_n > 0) {
        Rect bar = rect_cut_top(r, ui_px(ui, 28));
        const char *al[MAX_ACTS];
        char an[MAX_ACTS][24];
        for (int a = 0; a < s_act_n; a++) {
            str_format(an[a], sizeof an[a], "%s %s", tr("Akt"), s_acts[a]);
            al[a] = an[a];
        }
        bar.w = mini(bar.w, ui_px(ui, 110) * s_act_n);
        if (ui_tabs(ui, bar, al, s_act_n, &s_act)) {
            s_pick_slot = -1;
            collect_view();
            if (s_view_n > 0) {
                app->selected_map = s_view[0];
            }
        }
        rect_cut_top(r, ui_px(ui, 4));
    }
}

/* ---- Seitenpanel -------------------------------------------------------------------- */
static void kv_line(Ui *ui, Rect r, int *y, const char *key, const char *val, Color vc) {
    ui_text(ui, r.x, *y, key, COL_INK_SOFT, FS_SMALL);
    ui_text_right(ui, rect_make(r.x, *y, r.w, ui_line_h(ui, FS_SMALL)), val, vc, FS_SMALL);
    *y += ui_line_h(ui, FS_SMALL) + ui_px(ui, 3);
}

static void draw_tiers_line(App *app, Rect r, int *y, const MapDef *m) {
    Ui *ui = &app->ui;
    ui_text(ui, r.x, *y, tr("Monster-Tiers"), COL_INK_SOFT, FS_SMALL);
    int x = r.x + r.w - ui_px(ui, 12);
    int count = m->boss_map ? 5 : 4;
    int d = ui_px(ui, 12);
    for (int t = count - 1; t >= 0; t--) {
        Tier tier = (Tier)t;
        Rect sw = rect_make(x - d, *y + ui_px(ui, 2), d, d);
        gfx_fill_rect(&app->gfx, sw, ui_tier(app, tier, 255));
        gfx_draw_rect(&app->gfx, sw, COL_INK, 1);
        x -= d + ui_px(ui, 4);
    }
    *y += ui_line_h(ui, FS_SMALL) + ui_px(ui, 3);
}

static void draw_info_tab(App *app, Rect r, int map_idx) {
    Ui *ui = &app->ui;
    const MapDef *m = &g_maps[map_idx];
    const MapProgress *mp = &app->game.world.map[map_idx];
    char b[64], n[32];
    int y = r.y;
    str_format(b, sizeof b, "%d", m->level);
    kv_line(ui, r, &y, tr("Level"), b, COL_INK);
    str_format(b, sizeof b, "%d", m->min_player_level);
    kv_line(ui, r, &y, tr("Mindest-Spielerlevel"), b, app->game.player.level >= m->min_player_level ? COL_INK : COL_HP);
    str_format(b, sizeof b, "%d %%", (int)(m->density * 100.0f + 0.5f));
    kv_line(ui, r, &y, tr("Monsterdichte"), b, COL_INK);
    str_format(b, sizeof b, "%d", m->max_heroes);
    kv_line(ui, r, &y, tr("Max. Helden"), b, COL_INK);
    draw_tiers_line(app, r, &y, m);
    bool has_boss = m->boss >= 0 && m->boss < g_monster_count;
    kv_line(ui, r, &y, tr("Boss"), has_boss ? g_monsters[m->boss].name : tr("keiner"),
            has_boss ? COL_WAX_RED : COL_INK_SOFT);
    str_format(b, sizeof b, "%s x", ui_num(mp->clear_count, n, sizeof n));
    kv_line(ui, r, &y, tr("Abgeschlossen"), mp->cleared ? b : tr("nein"), mp->cleared ? COL_MOSS : COL_INK_SOFT);
    if (mp->best_time > 0.0f) {
        str_format_duration(b, sizeof b, (double)mp->best_time);
        kv_line(ui, r, &y, tr("Bestzeit"), b, COL_INK);
    }
    if (m->kind != MAPK_STORY) {
        str_format(b, sizeof b, "%d", tickets_left(&app->game, m));
        kv_line(ui, r, &y, tr("Tickets"), b, tickets_left(&app->game, m) > 0 ? COL_INK : COL_HP);
    }
}

static void reward_line(App *app, Rect r, int *y, const char *text, Color c) {
    Ui *ui = &app->ui;
    ui_text(ui, r.x, *y, text, c, FS_SMALL);
    *y += ui_line_h(ui, FS_SMALL) + ui_px(ui, 3);
}

static void draw_rewards_tab(App *app, Rect r, int map_idx) {
    Ui *ui = &app->ui;
    const MapDef *m = &g_maps[map_idx];
    const MapProgress *mp = &app->game.world.map[map_idx];
    Tier ref = m->boss_map ? TIER_BOSS : TIER_UNIQUE;
    char b[96], n[32];
    int y = r.y;
    reward_line(app, r, &y, tr("Bei jedem Abschluss"), COL_WOOD);
    str_format(b, sizeof b, "  %s EXP / %s", ui_num((i64)monster_exp_value(m->level, ref), n, sizeof n), tr("Held"));
    reward_line(app, r, &y, b, COL_INK);
    str_format(b, sizeof b, "  %s %s", ui_num((i64)(game_kill_gold(m->level, ref) * SHOW_GOLD_MULT), n, sizeof n),
               tr("Gold"));
    reward_line(app, r, &y, b, COL_INK);
    if (m->stone_item >= 0 && m->stone_item < g_item_count) {
        str_format(b, sizeof b, "  %dx %s", maxi(m->stone_count, 1), g_items[m->stone_item].name);
        reward_line(app, r, &y, b, ui_tier(app, g_items[m->stone_item].tier, 255));
    }
    if (m->boss_map) {
        reward_line(app, r, &y, tr("  1x Boss-Kiste"), ui_tier(app, TIER_BOSS, 255));
    }
    y += ui_px(ui, 4);
    reward_line(app, r, &y, mp->first_claimed ? tr("Erstabschluss (erhalten)") : tr("Erstabschluss"),
                mp->first_claimed ? COL_INK_SOFT : COL_WOOD);
    str_format(b, sizeof b, "  %s %s", ui_num((i64)(monster_exp_value(m->level, TIER_BOSS) * SHOW_EXP_BOSS_MULT), n,
                                              sizeof n), tr("Kommandeur-EXP"));
    reward_line(app, r, &y, b, COL_INK);
    if (m->boss_map) {
        str_format(b, sizeof b, "  %d %s", SHOW_FIRST_DIAMONDS, tr("Diamanten"));
        reward_line(app, r, &y, b, COL_INK);
        if (m->kind == MAPK_STORY) {
            reward_line(app, r, &y, tr("  1 Kommandeurpunkt"), COL_INK);
            reward_line(app, r, &y, tr("  Akt-Truhe (Verbesserungssteine)"), COL_INK);
        }
    }
}

/* Auswahl eines Helden fuer einen Team-Slot. */
static void assign_choice(App *app, int map_idx, int slot, int hero) {
    Game *g = &app->game;
    int occ = slot_hero(g, map_idx, slot);
    if (occ == hero) {
        s_pick_slot = -1;
        return;
    }
    if (occ >= 0) {
        game_unassign_hero(g, occ);
    }
    bool ok = game_assign_hero(g, hero, map_idx, slot);
    ui_result(app, ok, tr("Zuweisung nicht moeglich"), ok ? tr("Held zugewiesen") : NULL);
    s_pick_slot = -1;
}

static void draw_hero_pick(App *app, Rect r, int map_idx) {
    Ui *ui = &app->ui;
    Game *g = &app->game;
    int rh = ui_px(ui, 28);
    int n = 0;
    for (int i = 0; i < MAX_HEROES; i++) {
        n += g->heroes[i].used ? 1 : 0;
    }
    ui_text(ui, r.x, r.y, tr("Held waehlen"), COL_WOOD, FS_NORMAL);
    Rect cancel = rect_make(r.x + r.w - ui_px(ui, 90), r.y - ui_px(ui, 2), ui_px(ui, 90), ui_px(ui, 24));
    if (ui_button_ex(ui, cancel, tr("Abbrechen"), UI_BTN_FLAT, true)) {
        s_pick_slot = -1;
        return;
    }
    Rect list = rect_make(r.x, r.y + ui_px(ui, 28), r.w, r.h - ui_px(ui, 28));
    if (n == 0) {
        ui_text(ui, list.x, list.y, tr("Keine Helden vorhanden"), COL_INK_SOFT, FS_SMALL);
        return;
    }
    int off = ui_scroll_begin(ui, list, n * (rh + 2), "wm_heroes");
    int row = 0;
    for (int i = 0; i < MAX_HEROES; i++) {
        const Hero *h = &g->heroes[i];
        if (!h->used) {
            continue;
        }
        Rect rr = rect_make(list.x, list.y + row * (rh + 2) - off, list.w - ui_px(ui, 10), rh);
        row++;
        char lab[96], cur[48] = "";
        hero_label(h, lab, sizeof lab);
        if (h->assigned_map >= 0 && h->assigned_map < g_map_count) {
            str_format(cur, sizeof cur, "[%s]", g_maps[h->assigned_map].id);
        }
        ui_push_id(ui, i);
        UiClick c = ui_region(ui, rr, ui_make_id(ui, "pick", rr.x, rr.y));
        ui_pop_id(ui);
        gfx_fill_rect(&app->gfx, rr, c.hovered ? COL_PARCHMENT_LIGHT : COL_PARCHMENT);
        gfx_draw_rect(&app->gfx, rr, COL_WOOD, 1);
        icon_draw_class(&app->gfx, rect_make(rr.x + 2, rr.y + 2, rh - 4, rh - 4), h->cls, h->look);
        ui_text(ui, rr.x + rh + ui_px(ui, 4), rr.y + rh / 2 - ui_px(ui, 7), lab, COL_INK, FS_SMALL);
        ui_text_right(ui, rr, cur, COL_INK_SOFT, FS_SMALL);
        if (c.clicked) {
            assign_choice(app, map_idx, s_pick_slot, i);
        }
    }
    ui_scroll_end(ui);
}

/* Team-Slots: Held anzeigen, Klick = waehlen, "Entfernen". */
static void draw_slots(App *app, Rect *r, int map_idx, bool unlocked) {
    Ui *ui = &app->ui;
    Game *g = &app->game;
    const MapDef *m = &g_maps[map_idx];
    int rh = ui_px(ui, 32);
    ui_text(ui, r->x, r->y, m->max_heroes > 1 ? tr("Team") : tr("Zugewiesener Held"), COL_WOOD, FS_NORMAL);
    rect_cut_top(r, ui_line_h(ui, FS_NORMAL) + ui_px(ui, 2));
    for (int s = 0; s < m->max_heroes && s < MAX_TEAM; s++) {
        Rect row = rect_cut_top(r, rh);
        rect_cut_top(r, ui_px(ui, 3));
        Rect rm = rect_cut_right(&row, ui_px(ui, 84));
        int h = slot_hero(g, map_idx, s);
        ui_push_id(ui, 100 + s);
        UiClick c = ui_region(ui, row, ui_make_id(ui, "slot", row.x, row.y));
        gfx_fill_rect(&app->gfx, row, c.hovered && unlocked ? COL_PARCHMENT_LIGHT : COL_PARCHMENT);
        gfx_draw_rect(&app->gfx, row, s_pick_slot == s ? COL_GOLD : COL_WOOD, s_pick_slot == s ? 2 : 1);
        if (h >= 0) {
            char lab[96];
            icon_draw_class(&app->gfx, rect_make(row.x + 2, row.y + 2, rh - 4, rh - 4), g->heroes[h].cls,
                            g->heroes[h].look);
            hero_label(&g->heroes[h], lab, sizeof lab);
            ui_text(ui, row.x + rh + ui_px(ui, 4), row.y + rh / 2 - ui_px(ui, 7), lab, COL_INK, FS_SMALL);
        } else {
            ui_text(ui, row.x + ui_px(ui, 8), row.y + rh / 2 - ui_px(ui, 7), tr("Held zuweisen ..."), COL_INK_SOFT,
                    FS_SMALL);
        }
        if (c.clicked && unlocked) {
            s_pick_slot = s_pick_slot == s ? -1 : s;
        }
        if (ui_button_ex(ui, rm, tr("Entfernen"), UI_BTN_WAX, h >= 0)) {
            game_unassign_hero(g, h);
            app_message(app, "%s", tr("Held entfernt"));
        }
        ui_pop_id(ui);
    }
}

static void draw_side_panel(App *app, Rect area) {
    Ui *ui = &app->ui;
    Game *g = &app->game;
    int mi = app->selected_map;
    if (mi < 0 || mi >= g_map_count) {
        ui_panel(ui, area, tr("Map-Info"));
        return;
    }
    const MapDef *m = &g_maps[mi];
    char title[96];
    str_format(title, sizeof title, "%s  %s", m->id, m->name);
    Rect in = ui_panel(ui, area, title);
    in = rect_inset(in, ui_px(ui, 6));
    bool unlocked = game_map_unlocked(g, mi);
    if (!unlocked) {
        char rsn[80];
        lock_reason(g, m, rsn, sizeof rsn);
        ui_text(ui, in.x, in.y, rsn, COL_HP, FS_SMALL);
        rect_cut_top(&in, ui_line_h(ui, FS_SMALL) + ui_px(ui, 4));
    }
    /* Unten: Wiederholen-Checkbox + Zum Helden */
    Rect foot = rect_cut_bottom(&in, ui_px(ui, 30));
    bool rep = g->world.map[mi].repeat != 0;
    Rect cb = rect_cut_left(&foot, ui_px(ui, 150));
    if (ui_checkbox(ui, cb, tr("Wiederholen"), &rep)) {
        g->world.map[mi].repeat = rep ? 1 : 0;
        g->dirty_save = true;
    }
    int first = -1;
    for (int s = 0; s < MAX_TEAM && first < 0; s++) {
        first = slot_hero(g, mi, s);
    }
    if (ui_button_ex(ui, foot, tr("Zum Helden"), UI_BTN_GREEN, first >= 0)) {
        app->sel_hero = first;
        app_set_screen(app, SCR_COMBAT);
    }
    rect_cut_bottom(&in, ui_px(ui, 6));

    draw_slots(app, &in, mi, unlocked);
    rect_cut_top(&in, ui_px(ui, 4));
    if (s_pick_slot >= 0 && s_pick_slot < m->max_heroes) {
        draw_hero_pick(app, in, mi);
        return;
    }
    const char *labels[2] = {tr("Info"), tr("Clear Rewards")};
    ui_tabs(ui, rect_cut_top(&in, ui_px(ui, 28)), labels, 2, &s_info_tab);
    rect_cut_top(&in, ui_px(ui, 8));
    if (s_info_tab == INFO_TAB_INFO) {
        draw_info_tab(app, in, mi);
    } else {
        draw_rewards_tab(app, in, mi);
    }
}

/* ---- Einstieg ------------------------------------------------------------------------ */
void screen_worldmap(App *app, Rect area) {
    Ui *ui = &app->ui;
    gfx_fill_rect(ui->gfx, area, COL_WOOD_DARK);
    if (!s_init || app->selected_map < 0 || app->selected_map >= g_map_count) {
        if (app->selected_map < 0 || app->selected_map >= g_map_count) {
            app->selected_map = 0;
        }
        focus_map(app->selected_map);
        s_init = true;
    }
    Rect r = rect_inset(area, ui_px(ui, 10));
    collect_acts();
    collect_view();
    draw_header(app, &r);
    collect_acts();
    collect_view();
    if (!view_has(app->selected_map) && s_view_n > 0) {
        app->selected_map = s_view[0];
    }
    int pw = clampi(r.w * 38 / 100, ui_px(ui, 300), ui_px(ui, 420));
    Rect side = rect_cut_right(&r, pw);
    rect_cut_right(&r, ui_px(ui, 8));
    draw_map_net(app, r);
    draw_side_panel(app, side);
}
