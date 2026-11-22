/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Spielzustand: Neustart, Zeitschritt (feste Ticks), Run-Verwaltung, Helden,
 * Zuweisungen, Toasts und Reset-Schluessel. Kampfregeln: game_events.c. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "core/util.h"
#include "game/game.h"
#include "platform/platform.h"
#include "rules/rules_internal.h"
#include "sim/sim.h"

#define START_GOLD 5000
#define START_DIAMONDS 100
#define START_CREDITS 60
#define MAX_TICKS_PER_UPDATE 64   /* Schutz gegen Spiral of Death bei Ruckeln/hoher Geschwindigkeit */
#define MAX_UPDATE_DT 1.0         /* groessere Frame-Zeiten werden gekappt */
#define RUN_FAIL_COOLDOWN 5.0f    /* s bis zum naechsten run_create-Versuch einer Map */
#define TOAST_TTL 5.0f

/* ======================================================================== */
/* Neustart                                                                  */
/* ======================================================================== */
void game_reset_state(Game *g, u64 seed) {
    memset(g, 0, sizeof *g);
    rng_seed(&g->rng, seed, 54u);
    g->speed_mult = 1.0f;
    g->next_hero_uid = 1;
    g->inv.capacity = (int)g_bal.inv_base_capacity;
    for (int i = 0; i < INV_MAX_ITEMS; i++) {
        g->inv.items[i] = item_empty();
    }
    for (int i = 0; i < MAX_HEROES; i++) {
        Hero *h = &g->heroes[i];
        for (int s = 0; s < SLOT_COUNT; s++) {
            h->eq[s] = item_empty();
        }
        for (int s = 0; s < SKILL_SLOTS; s++) {
            h->skill_slot[s] = -1;
        }
        h->assigned_map = -1;
    }
    for (int m = 0; m < MAX_MAPS; m++) {
        g->world.map[m].repeat = 1;
        for (int t = 0; t < MAX_TEAM; t++) {
            g->world.map[m].team[t] = -1;
        }
    }
    Player *p = &g->player;
    str_copy(p->name, sizeof p->name, "Kommandeur");
    p->level = 1;
    for (int i = 0; i < BOUNTY_PER_DAY; i++) {
        p->bounty[i].def = -1;
    }
}

/* Schaltet je Klasse die schwaechsten Skills frei (mindestens 4 je Klasse) + alle "Normal"-Skills. */
static void unlock_starting_skills(Game *g) {
    int per_class[CLASS_COUNT + 1];
    memset(per_class, 0, sizeof per_class);
    for (int grade = 0; grade <= 3; grade++) {
        for (int s = 0; s < g_skill_count; s++) {
            const SkillDef *d = &g_skills[s];
            int c = clampi(d->cls, 0, CLASS_COMMON);
            if (d->grade != grade || g->player.skill_level[s] > 0) {
                continue;
            }
            if (grade == 0 || per_class[c] < 4) {
                g->player.skill_level[s] = 1;
                per_class[c]++;
            }
        }
    }
}

void game_init_new(Game *g, u64 seed) {
    game_reset_state(g, seed);
    Player *p = &g->player;
    p->gold = START_GOLD;
    p->diamonds = START_DIAMONDS;
    p->credits = START_CREDITS;
    g->inv.chests[TIER_NORMAL] = 20;
    g->inv.chests[TIER_MAGIC] = 5;
    g->inv.chests[TIER_RARE] = 1;
    unlock_starting_skills(g);
    shop_check_resets(g); /* setzt Schluessel, Tickets und erste Kopfgelder */
    mail_add(g, "Willkommen bei Panoptix",
             "Du bist der neue Kommandeur. Erstelle Helden, weise sie Karten zu und lass sie kaempfen. "
             "Hier sind 50 Diamanten fuer den Start.",
             CUR_DIAMONDS, 50);
    g->dirty_save = true;
}

/* ======================================================================== */
/* Runs                                                                      */
/* ======================================================================== */
static int find_run_index(const Game *g, const Run *r) {
    for (int i = 0; i < g->run_count; i++) {
        if (g->runs[i] == r) {
            return i;
        }
    }
    return -1;
}

static Run *find_run_for_map(const Game *g, int map_idx) {
    for (int i = 0; i < g->run_count; i++) {
        if (g->runs[i] && g->runs[i]->map_idx == map_idx) {
            return g->runs[i];
        }
    }
    return NULL;
}

static void remove_run(Game *g, Run *r) {
    int i = find_run_index(g, r);
    for (int h = 0; h < MAX_HEROES; h++) {
        if (g->hero_run[h] == r) {
            g->hero_run[h] = NULL;
        }
    }
    if (i >= 0) {
        g->runs[i] = g->runs[g->run_count - 1];
        g->runs[g->run_count - 1] = NULL;
        g->run_count--;
    }
    run_destroy(r);
}

/* Gueltiger Teameintrag? Sonst wird er geleert. */
static bool team_entry_valid(const Game *g, int map_idx, int hero) {
    return hero >= 0 && hero < MAX_HEROES && g->heroes[hero].used && g->heroes[hero].assigned_map == map_idx;
}

/* Sammelt die Helden einer Map in Slot-Reihenfolge. */
static int build_team(Game *g, int map_idx, int out[MAX_TEAM]) {
    MapProgress *mp = &g->world.map[map_idx];
    int n = 0;
    for (int s = 0; s < MAX_TEAM; s++) {
        if (mp->team[s] >= 0 && !team_entry_valid(g, map_idx, mp->team[s])) {
            mp->team[s] = -1;
        }
        if (mp->team[s] >= 0) {
            out[n++] = mp->team[s];
        }
    }
    return n;
}

static bool run_matches_team(const Run *r, const int *team, int n) {
    if (r->hero_count != n) {
        return false;
    }
    for (int i = 0; i < n; i++) {
        if (r->heroes[i].hero_idx != team[i]) {
            return false;
        }
    }
    return true;
}

static bool map_playable(const Game *g, int map_idx) {
    const MapDef *m = &g_maps[map_idx];
    return !(m->kind == MAPK_TEMPLE && g->world.temple_tickets <= 0);
}

static void create_run_for_map(Game *g, int map_idx, const int *team, int n) {
    if (g->run_count >= MAX_RUNS) {
        return;
    }
    Run *r = run_create(g, map_idx, team, n);
    if (!r) {
        g->run_fail_t[map_idx] = RUN_FAIL_COOLDOWN;
        return;
    }
    g->runs[g->run_count++] = r;
    for (int i = 0; i < n; i++) {
        g->hero_run[team[i]] = r;
    }
}

/* Gleicht die laufenden Runs mit den Zuweisungen (world.map[].team) ab. */
static void sync_runs(Game *g) {
    int team[MAX_TEAM];
    for (int i = g->run_count - 1; i >= 0; i--) {
        Run *r = g->runs[i];
        int n = build_team(g, r->map_idx, team);
        if (n == 0 || !run_matches_team(r, team, n)) {
            remove_run(g, r);
        }
    }
    for (int m = 0; m < g_map_count; m++) {
        int n = build_team(g, m, team);
        if (n == 0 || find_run_for_map(g, m) || g->run_fail_t[m] > 0.0f || !map_playable(g, m)) {
            continue;
        }
        create_run_for_map(g, m, team, n);
    }
}

static void unassign_team(Game *g, int map_idx) {
    int team[MAX_TEAM];
    int n = build_team(g, map_idx, team);
    for (int i = 0; i < n; i++) {
        game_unassign_hero(g, team[i]);
    }
}

/* Naechste Map gleicher Art (Reihenfolge der Datentabelle) oder -1. */
static int next_map_of_kind(int map_idx) {
    int next = map_idx + 1;
    if (next < g_map_count && g_maps[next].kind == g_maps[map_idx].kind) {
        return next;
    }
    return -1;
}

/* Cleared-Run: nach Ablauf der Retry-Zeit wiederholen, weiterziehen oder Team entlassen. */
static void handle_cleared_run(Game *g, Run *r) {
    int team[MAX_TEAM];
    int m = r->map_idx;
    int n = build_team(g, m, team);
    for (int i = 0; i < n; i++) {
        if (g->heroes[team[i]].retry_timer > 0.0f) {
            return;
        }
    }
    bool repeat = g->world.map[m].repeat != 0;
    if (g_maps[m].kind == MAPK_TEMPLE && g->world.temple_tickets <= 0) {
        repeat = false;
    }
    if (repeat) {
        run_restart(r, g);
        return;
    }
    int next = next_map_of_kind(m);
    int next_team[MAX_TEAM];
    bool moved = false;
    if (next >= 0 && game_map_unlocked(g, next) && n <= g_maps[next].max_heroes &&
        build_team(g, next, next_team) == 0) {
        for (int i = 0; i < n; i++) {
            game_unassign_hero(g, team[i]);
        }
        moved = true;
        for (int i = 0; i < n; i++) {
            moved = game_assign_hero(g, team[i], next, i) && moved;
        }
    }
    if (!moved) {
        unassign_team(g, m);
        game_toast(g, -1, 0xF2C94C, "Team wartet auf neue Aufgabe (%s)", g_maps[m].name);
    }
}

/* ======================================================================== */
/* Zeitschritt                                                               */
/* ======================================================================== */
static void tick_hero_timers(Game *g, float dt) {
    for (int i = 0; i < MAX_HEROES; i++) {
        Hero *h = &g->heroes[i];
        if (!h->used) {
            continue;
        }
        if (h->dead_timer > 0.0f) {
            h->dead_timer = maxf(h->dead_timer - dt, 0.0f);
        }
        if (h->retry_timer > 0.0f) {
            h->retry_timer = maxf(h->retry_timer - dt, 0.0f);
        }
    }
}

static void run_fixed_ticks(Game *g, double dt) {
    g->tick_acc += dt * (double)maxf(g->speed_mult, 0.0f);
    int ticks = 0;
    while (g->tick_acc >= (double)SIM_DT && ticks < MAX_TICKS_PER_UPDATE) {
        for (int i = 0; i < g->run_count; i++) {
            run_tick(g->runs[i], g);
        }
        g->tick_acc -= (double)SIM_DT;
        ticks++;
    }
    if (g->tick_acc > (double)SIM_DT) {
        g->tick_acc = fmod(g->tick_acc, (double)SIM_DT); /* Rueckstand verwerfen */
    }
    if (ticks > 0) {
        tick_hero_timers(g, (float)ticks * SIM_DT);
    }
}

/* Tränke fuellen sich passiv (Kommandeur-Trait), 1x pro Sekunde fuer Helden in Runs. */
static void passive_potion_regen(Game *g) {
    CommanderMods cm;
    commander_mods(g, &cm);
    if (cm.potion_regen <= 0.0f) {
        return;
    }
    float amount = cm.potion_regen * maxf(g->speed_mult, 1.0f);
    for (int i = 0; i < MAX_HEROES; i++) {
        if (!g->heroes[i].used || !g->hero_run[i]) {
            continue;
        }
        for (int s = SLOT_POTION1; s <= SLOT_POTION3; s++) {
            Item *it = &g->heroes[i].eq[s];
            if (item_is_empty(it) || it->fill >= 100) {
                continue;
            }
            int whole = (int)amount;
            if (rng_chance(&g->rng, (double)(amount - (float)whole))) {
                whole++;
            }
            it->fill = (u8)mini(it->fill + whole, 100);
        }
    }
}

static void second_tasks(Game *g) {
    shop_check_resets(g);
    passive_potion_regen(g);
    for (int m = 0; m < MAX_MAPS; m++) {
        if (g->run_fail_t[m] > 0.0f) {
            g->run_fail_t[m] = maxf(g->run_fail_t[m] - 1.0f, 0.0f);
        }
    }
    for (int i = 0; i < g->run_count; i++) {
        if (g->runs[i]->state == RUN_CLEARED) {
            handle_cleared_run(g, g->runs[i]);
        }
    }
}

static void update_toasts(Game *g, double dt) {
    for (int i = 0; i < TOAST_MAX; i++) {
        if (g->toasts[i].ttl > 0.0f) {
            g->toasts[i].ttl -= (float)dt;
        }
    }
}

void game_update(Game *g, double dt) {
    if (!(dt > 0.0)) {
        return;
    }
    dt = dt > MAX_UPDATE_DT ? MAX_UPDATE_DT : dt;
    g->play_time += dt;
    update_toasts(g, dt);
    sync_runs(g);
    run_fixed_ticks(g, dt);
    g->second_timer += dt;
    while (g->second_timer >= 1.0) {
        g->second_timer -= 1.0;
        second_tasks(g);
    }
    g->post_timer += dt;
    if (g->post_timer >= 60.0 || g->player.post_last_unix <= 0) {
        g->post_timer = 0.0;
        commander_update_post(g, platform_unix_time());
    }
    g->autosave_timer += dt;
    if (g->autosave_timer >= g_bal.autosave_s && g->run_count > 0) {
        g->dirty_save = true; /* laufende Maps veraendern den Zustand ohnehin staendig */
    }
}

void game_shutdown(Game *g) {
    for (int i = 0; i < g->run_count; i++) {
        if (g->runs[i]) {
            run_destroy(g->runs[i]);
            g->runs[i] = NULL;
        }
    }
    g->run_count = 0;
    for (int i = 0; i < MAX_HEROES; i++) {
        g->hero_run[i] = NULL;
    }
}

/* ======================================================================== */
/* Heldenplaetze und Helden                                                  */
/* ======================================================================== */
int game_hero_slots(const Game *g) {
    static const int k_level[13] = {1, 8, 15, 22, 30, 38, 46, 54, 62, 70, 78, 86, 95};
    int slots = 4;
    for (int i = 0; i < 13; i++) {
        if (g->player.level >= k_level[i]) {
            slots = 4 + i;
        }
    }
    return mini(slots + maxi(g->player.hero_slots_bonus, 0), MAX_HEROES);
}

int game_hero_count(const Game *g) {
    int n = 0;
    for (int i = 0; i < MAX_HEROES; i++) {
        n += g->heroes[i].used ? 1 : 0;
    }
    return n;
}

int game_free_hero_slot(const Game *g) {
    int slots = game_hero_slots(g);
    for (int i = 0; i < slots; i++) {
        if (!g->heroes[i].used) {
            return i;
        }
    }
    return -1;
}

static bool skill_in_slots(const Hero *h, int skill) {
    for (int i = 0; i < SKILL_SLOTS; i++) {
        if (h->skill_slot[i] == skill) {
            return true;
        }
    }
    return false;
}

/* Fuellt leere Skill-Slots mit freigeschalteten Skills (Klasse zuerst, niedrige Grade zuerst). */
static void fill_default_skills(Game *g, Hero *h) {
    for (int pass = 0; pass < 2; pass++) {
        int want_cls = pass == 0 ? h->cls : CLASS_COMMON;
        for (int grade = 0; grade <= 3; grade++) {
            for (int s = 0; s < g_skill_count; s++) {
                if (g_skills[s].cls != want_cls || g_skills[s].grade != grade ||
                    g->player.skill_level[s] < 1 || skill_in_slots(h, s)) {
                    continue;
                }
                for (int slot = 0; slot < SKILL_SLOTS; slot++) {
                    if (h->skill_slot[slot] < 0) {
                        h->skill_slot[slot] = (i16)s;
                        break;
                    }
                }
            }
        }
    }
}

/* Klassenvorschlag (Summe 100 %) auf die Startpunkte verteilen. */
static void auto_spend_points(Game *g, int idx) {
    Hero *h = &g->heroes[idx];
    const ClassDef *c = &g_classes[h->cls];
    int total = h->free_points;
    int str = (total * c->auto_str + 50) / 100;
    int dex = (total * c->auto_dex + 50) / 100;
    int intl = (total * c->auto_int + 50) / 100;
    int rest = total - str - dex - intl;
    if (c->main_stat == 0) {
        str += rest;
    } else if (c->main_stat == 1) {
        dex += rest;
    } else {
        intl += rest;
    }
    str = maxi(str, 0);
    dex = maxi(dex, 0);
    intl = maxi(intl, 0);
    while (str + dex + intl > total) { /* Rundung nach unten korrigieren */
        if (str >= dex && str >= intl) {
            str--;
        } else if (dex >= intl) {
            dex--;
        } else {
            intl--;
        }
    }
    hero_spend_point(g, idx, 0, str);
    hero_spend_point(g, idx, 1, dex);
    hero_spend_point(g, idx, 2, intl);
}

int game_create_hero(Game *g, const char *name, int cls, u8 look, bool auto_distribute) {
    if (cls < 0 || cls >= CLASS_COUNT) {
        return -1;
    }
    int idx = game_free_hero_slot(g);
    if (idx < 0) {
        return -1;
    }
    Hero *h = &g->heroes[idx];
    memset(h, 0, sizeof *h);
    for (int s = 0; s < SLOT_COUNT; s++) {
        h->eq[s] = item_empty();
    }
    for (int s = 0; s < SKILL_SLOTS; s++) {
        h->skill_slot[s] = -1;
    }
    h->used = true;
    h->uid = g->next_hero_uid++;
    str_copy(h->name, sizeof h->name, (name && *name) ? name : g_classes[cls].name);
    h->cls = cls;
    h->look = (u8)(look & 7u);
    h->level = 1;
    h->free_points = 5;
    h->assigned_map = -1;
    h->stats_dirty = true;
    hero_give_starting_gear(g, idx);
    if (auto_distribute) {
        auto_spend_points(g, idx);
        hero_apply_build_preset(g, idx);
    }
    fill_default_skills(g, h);
    hero_ai_defaults(h);
    g->dirty_save = true;
    return idx;
}

bool game_delete_hero(Game *g, int hero_idx) {
    if (hero_idx < 0 || hero_idx >= MAX_HEROES || !g->heroes[hero_idx].used) {
        return false;
    }
    Hero *h = &g->heroes[hero_idx];
    int items = 0;
    for (int s = 0; s < SLOT_COUNT; s++) {
        items += item_is_empty(&h->eq[s]) ? 0 : 1;
    }
    if (inv_free_slots(g) < items) {
        return false; /* Ausruestung muesste verloren gehen */
    }
    game_collect_chests(g, hero_idx);
    game_unassign_hero(g, hero_idx);
    for (int s = 0; s < SLOT_COUNT; s++) {
        if (!item_is_empty(&h->eq[s])) {
            inv_add_item(g, &h->eq[s]);
        }
    }
    if (g->hero_run[hero_idx]) {
        g->hero_run[hero_idx] = NULL;
    }
    memset(h, 0, sizeof *h);
    for (int s = 0; s < SLOT_COUNT; s++) {
        h->eq[s] = item_empty();
    }
    for (int s = 0; s < SKILL_SLOTS; s++) {
        h->skill_slot[s] = -1;
    }
    h->assigned_map = -1;
    game_mark_all_dirty(g);
    g->dirty_save = true;
    return true;
}

/* ======================================================================== */
/* Zuweisung                                                                 */
/* ======================================================================== */
static bool story_map_cleared(const Game *g, int map_idx) {
    return g->world.map[map_idx].cleared != 0;
}

/* Letzte Story-Map eines Aktes. */
static int last_story_map_of_act(int act) {
    int last = -1;
    for (int i = 0; i < g_map_count; i++) {
        if (g_maps[i].kind == MAPK_STORY && g_maps[i].act == act) {
            last = i;
        }
    }
    return last;
}

bool game_map_unlocked(const Game *g, int map_idx) {
    if (map_idx < 0 || map_idx >= g_map_count) {
        return false;
    }
    const MapDef *m = &g_maps[map_idx];
    int level = g->player.level;
    if (level < m->min_player_level) {
        return false;
    }
    switch (m->kind) {
    case MAPK_TEMPLE:
        return level >= (int)g_bal.temple_unlock_level;
    case MAPK_RUINS: {
        int last = last_story_map_of_act(3);
        return last >= 0 && story_map_cleared(g, last);
    }
    case MAPK_STORY:
    default: {
        if (m->act <= 1 && m->index <= 1) {
            return true;
        }
        for (int i = map_idx - 1; i >= 0; i--) { /* Vorgaenger in Reihenfolge der Story-Karten */
            if (g_maps[i].kind == MAPK_STORY) {
                return story_map_cleared(g, i);
            }
        }
        return true;
    }
    }
}

void game_unassign_hero(Game *g, int hero_idx) {
    if (hero_idx < 0 || hero_idx >= MAX_HEROES) {
        return;
    }
    Hero *h = &g->heroes[hero_idx];
    int m = h->assigned_map;
    if (m >= 0 && m < MAX_MAPS) {
        for (int s = 0; s < MAX_TEAM; s++) {
            if (g->world.map[m].team[s] == hero_idx) {
                g->world.map[m].team[s] = -1;
            }
        }
    }
    h->assigned_map = -1;
    h->retry_timer = 0.0f;
    g->dirty_save = true;
}

bool game_assign_hero(Game *g, int hero_idx, int map_idx, int team_slot) {
    if (hero_idx < 0 || hero_idx >= MAX_HEROES || !g->heroes[hero_idx].used) {
        return false;
    }
    if (map_idx < 0 || map_idx >= g_map_count || !game_map_unlocked(g, map_idx)) {
        return false;
    }
    if (team_slot < 0 || team_slot >= g_maps[map_idx].max_heroes || team_slot >= MAX_TEAM) {
        return false;
    }
    int occupant = g->world.map[map_idx].team[team_slot];
    if (occupant == hero_idx && g->heroes[hero_idx].assigned_map == map_idx) {
        return true;
    }
    if (occupant >= 0 && team_entry_valid(g, map_idx, occupant)) {
        return false;
    }
    game_unassign_hero(g, hero_idx);
    g->world.map[map_idx].team[team_slot] = (i8)hero_idx;
    g->heroes[hero_idx].assigned_map = map_idx;
    g->heroes[hero_idx].retry_timer = 0.0f;
    g->dirty_save = true;
    return true;
}

/* ======================================================================== */
/* Toasts                                                                    */
/* ======================================================================== */
void game_toast(Game *g, int tier, u32 color, const char *fmt, ...) {
    int slot = 0;
    for (int i = 0; i < TOAST_MAX; i++) {
        if (g->toasts[i].ttl <= 0.0f) {
            slot = i;
            break;
        }
        if (g->toasts[i].ttl < g->toasts[slot].ttl) {
            slot = i;
        }
    }
    Toast *t = &g->toasts[slot];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(t->text, sizeof t->text, fmt, ap);
    va_end(ap);
    t->color = color;
    t->tier = tier;
    t->ttl = TOAST_TTL;
}

/* ======================================================================== */
/* Reset-Schluessel (Reset um daily_reset_hour Ortszeit)                      */
/* ======================================================================== */
static i64 days_from_civil(int y, int m, int d) {
    y -= m <= 2 ? 1 : 0;
    i64 era = (y >= 0 ? y : y - 399) / 400;
    int yoe = (int)(y - era * 400);
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(i64 z, int *y, int *m, int *d) {
    z += 719468;
    i64 era = (z >= 0 ? z : z - 146096) / 146097;
    int doe = (int)(z - era * 146097);
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    i64 yy = yoe + era * 400;
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp + (mp < 10 ? 3 : -9);
    *y = (int)(yy + (*m <= 2 ? 1 : 0));
}

/* Tageszaehler seit 1970 (Tage beginnen um die Resetstunde). */
static i64 reset_day_number(void) {
    int y, m, d, wd, hour;
    platform_local_date(&y, &m, &d, &wd, &hour);
    i64 days = days_from_civil(y, m, d);
    if (hour < (int)g_bal.daily_reset_hour) {
        days--;
    }
    return days;
}

int game_day_key(void) {
    int y, m, d;
    civil_from_days(reset_day_number(), &y, &m, &d);
    return y * 10000 + m * 100 + d;
}

int game_week_key(void) {
    i64 days = reset_day_number(); /* 1970-01-01 war ein Donnerstag; Wochen beginnen am Montag */
    return (int)((days + 3) / 7);
}

int game_month_key(void) {
    int y, m, d;
    civil_from_days(reset_day_number(), &y, &m, &d);
    return y * 100 + m;
}
