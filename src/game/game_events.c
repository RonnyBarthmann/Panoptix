/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Rueckrufe der Simulation: Kills, Kisten, Tod und Map-Abschluss (Spec 03, 11, 14). */
#include <math.h>
#include <string.h>

#include "core/util.h"
#include "game/game.h"
#include "rules/rules_internal.h"

#define CLEAR_EXP_BOSS_MULT 5.0f     /* Abschluss-EXP = Boss-Kill-EXP * Faktor (Erstabschluss: Kommandeur) */
#define CLEAR_GOLD_MULT 10.0f
#define FIRST_CLEAR_DIAMONDS_BOSS 10
#define BEST_LOOT_ONE_IN 200000u     /* rote "Beste Beute"-Kiste (< 0,001 %) */
#define ACT_CHEST_STONES 5

static bool hero_ok(const Game *g, int idx) {
    return idx >= 0 && idx < MAX_HEROES && g->heroes[idx].used;
}

/* ---- Formeln ---------------------------------------------------------------- */
float game_exp_rate(const Hero *h, int map_level) {
    float rate = 1.0f - (float)g_bal.exp_rate_step * (float)(h->level - map_level);
    return clampf(rate, (float)g_bal.exp_rate_min, 1.0f);
}

/* Gold pro Kill: wachst mit dem Level, Tier skaliert wie die EXP. */
float game_kill_gold(int monster_level, Tier tier) {
    double lv = (double)maxi(monster_level, 1);
    double base = 3.0 + 1.5 * pow(lv, 1.25);
    return (float)(base * (double)g_tiers[clampi((int)tier, 0, TIER_COUNT - 1)].exp_mult);
}

/* ---- Kill ---------------------------------------------------------------------- */
static void grant_kill_exp(Game *g, int hero_idx, Tier tier, int level, const CommanderMods *cm) {
    Hero *h = &g->heroes[hero_idx];
    double base = (double)monster_exp_value(level, tier) * (1.0 + (double)cm->exp_pct / 100.0);
    int ups = hero_gain_exp(g, hero_idx, (u64)(base * (double)game_exp_rate(h, level)));
    player_gain_exp(g, (u64)base); /* Spieler: gleicher Basiswert vor der EXP-Rate */
    if (ups > 0) {
        game_toast(g, -1, 0x6FCF97, "%s erreicht Level %d", h->name, h->level);
    }
}

static void count_kill(Game *g, int hero_idx, int monster_def, Tier tier) {
    Player *p = &g->player;
    p->counters[COUNTER_KILLS]++;
    p->kills_by_tier[tier]++;
    g->heroes[hero_idx].kills++;
    quests_progress(g, QK_KILL_ANY, 0, 1);
    quests_progress(g, QK_KILL_TIER, (int)tier, 1);
    if (monster_def >= 0 && monster_def < g_monster_count) {
        quests_progress(g, QK_KILL_ARCH, (int)g_monsters[monster_def].arch, 1);
    }
}

/* Normal-Kills fuellen die Traenke des Killers ein Stueck auf. */
static void refill_potions(Game *g, int hero_idx) {
    int add = (int)lround(g_bal.potion_refill_pct);
    for (int s = SLOT_POTION1; s <= SLOT_POTION3; s++) {
        Item *it = &g->heroes[hero_idx].eq[s];
        if (!item_is_empty(it)) {
            it->fill = (u8)mini(it->fill + add, 100);
        }
    }
}

void game_on_kill(Game *g, int hero_idx, int monster_def, Tier tier, int monster_level, int map_idx,
                  KillLoot *out) {
    KillLoot local;
    if (!out) {
        out = &local;
    }
    memset(out, 0, sizeof *out);
    tier = (Tier)clampi((int)tier, 0, TIER_COUNT - 1);
    if (!hero_ok(g, hero_idx)) {
        return;
    }
    CommanderMods cm;
    commander_mods(g, &cm);
    grant_kill_exp(g, hero_idx, tier, monster_level, &cm);
    currency_add(g, CUR_GOLD, (i64)((double)game_kill_gold(monster_level, tier) * (1.0 + (double)cm.gold_pct / 100.0)));
    count_kill(g, hero_idx, monster_def, tier);
    loot_roll_kill(g, tier, monster_level, out);
    if (tier == TIER_NORMAL) {
        refill_potions(g, hero_idx);
    }
    (void)map_idx;
    g->dirty_save = true;
}

/* ---- Kisten --------------------------------------------------------------------- */
static int chest_bar_total(const Hero *h) {
    int sum = 0;
    for (int t = 0; t < TIER_COUNT; t++) {
        sum += h->chest_bar[t];
    }
    return sum;
}

static bool chest_refused(const Hero *h, Tier tier) {
    return (tier == TIER_NORMAL && !h->ai.take_normal_chests) || (tier == TIER_MAGIC && !h->ai.take_magic_chests);
}

bool game_hero_pickup_chest(Game *g, int hero_idx, Tier tier) {
    if (!hero_ok(g, hero_idx) || tier < 0 || tier >= TIER_COUNT) {
        return false;
    }
    Hero *h = &g->heroes[hero_idx];
    if (chest_refused(h, tier)) {
        return false;
    }
    int cap = hero_chest_capacity(g, h);
    if (chest_bar_total(h) >= cap) {
        int lowest = -1;
        for (int t = 0; t < TIER_COUNT; t++) {
            if (h->chest_bar[t] > 0) {
                lowest = t;
                break;
            }
        }
        if (lowest < 0 || (int)tier <= lowest) {
            return false; /* nie eine hoehere Kiste ersetzen; Neue ist die niedrigste: verfaellt */
        }
        h->chest_bar[lowest]--;
    }
    h->chest_bar[tier]++;
    g->dirty_save = true;
    if (h->ai.collect_at_pct > 0 && chest_bar_total(h) * 100 >= cap * (int)h->ai.collect_at_pct) {
        game_collect_chests(g, hero_idx);
    }
    return true;
}

int game_collect_chests(Game *g, int hero_idx) {
    if (!hero_ok(g, hero_idx)) {
        return 0;
    }
    Hero *h = &g->heroes[hero_idx];
    int moved = 0;
    for (int t = 0; t < TIER_COUNT; t++) {
        u64 have = h->chest_bar[t];
        u64 limit = inv_chest_storage_limit(g, (Tier)t);
        u64 stored = g->inv.chests[t];
        u64 room = limit > stored ? limit - stored : 0;
        u64 take = have < room ? have : room;
        g->inv.chests[t] += (u32)take;
        h->chest_bar[t] = (u16)(have - take); /* Ueberschuss bleibt in der Leiste */
        moved += (int)take;
    }
    if (moved > 0) {
        g->dirty_save = true;
    }
    return moved;
}

int game_collect_all(Game *g) {
    int sum = 0;
    for (int i = 0; i < MAX_HEROES; i++) {
        sum += game_collect_chests(g, i);
    }
    return sum;
}

/* ---- Tod ----------------------------------------------------------------------------- */
void game_on_hero_death(Game *g, int hero_idx, int map_idx) {
    if (!hero_ok(g, hero_idx)) {
        return;
    }
    CommanderMods cm;
    commander_mods(g, &cm);
    float factor = maxf(1.0f + cm.death_time_pct / 100.0f, 0.1f);
    Hero *h = &g->heroes[hero_idx];
    h->dead_timer = (float)g_bal.death_wait_s * factor;
    g->player.counters[COUNTER_DEATHS]++;
    if (map_idx >= 0 && map_idx < g_map_count) {
        game_toast(g, -1, 0xEB5757, "%s ist gefallen (%s)", h->name, g_maps[map_idx].name);
    }
    g->dirty_save = true;
}

/* ---- Map-Abschluss ------------------------------------------------------------------- */
static void grant_magic_stones(Game *g, const MapDef *map, const CommanderMods *cm) {
    if (map->stone_item < 0 || map->stone_count <= 0) {
        return;
    }
    double amount = (double)map->stone_count * (1.0 + (double)cm->magic_stone_pct / 100.0);
    u32 n = (u32)amount;
    if (rng_chance(&g->rng, amount - (double)n)) {
        n++;
    }
    inv_add_stock(g, map->stone_item, n);
    game_toast(g, -1, 0x9B8CFF, "%s x%u", g_items[map->stone_item].name, (unsigned)n);
}

static void grant_boss_chest(Game *g) {
    g->inv.chests[TIER_BOSS]++;
    if (rng_one_in(&g->rng, BEST_LOOT_ONE_IN)) {
        g->inv.chests[TIER_LEGENDARY]++;
        game_toast(g, TIER_LEGENDARY, g_tiers[TIER_LEGENDARY].color, "Beste Beute!");
    }
}

/* Akt-Truhe: Verbesserungssteine beim ersten Abschluss des Akt-Bosses. */
static void grant_act_chest(Game *g, const MapDef *map) {
    int act = map->act;
    if (map->kind != MAPK_STORY || !map->boss_map || act < 1 || act >= (int)sizeof g->world.act_chest_claimed) {
        return;
    }
    if (g->world.act_chest_claimed[act]) {
        return;
    }
    g->world.act_chest_claimed[act] = 1;
    Tier tier = (Tier)clampi(act / 3, 0, TIER_BOSS);
    static const StoneKind k_kinds[3] = {STONE_ENH_WEAPON, STONE_ENH_ARMOR, STONE_ENH_JEWEL};
    for (int i = 0; i < 3; i++) {
        inv_add_stock(g, data_find_stone(k_kinds[i], tier), ACT_CHEST_STONES);
    }
    game_toast(g, (int)tier, g_tiers[tier].color, "Akt-Truhe: Verbesserungssteine");
}

static void grant_first_clear(Game *g, const MapDef *map) {
    float boss_exp = monster_exp_value(map->level, TIER_BOSS);
    player_gain_exp(g, (u64)(boss_exp * CLEAR_EXP_BOSS_MULT));
    if (map->boss_map) {
        currency_add(g, CUR_DIAMONDS, FIRST_CLEAR_DIAMONDS_BOSS);
        if (map->kind == MAPK_STORY) {
            g->player.trait_points_total++; /* Kommandeurpunkt */
            game_toast(g, -1, 0xF2C94C, "Kommandeurpunkt erhalten");
        }
        grant_act_chest(g, map);
    }
}

static void update_progress(Game *g, int map_idx, float clear_time) {
    MapProgress *mp = &g->world.map[map_idx];
    mp->cleared = 1;
    mp->clear_count++;
    if (clear_time > 0.0f && (mp->best_time <= 0.0f || clear_time < mp->best_time)) {
        mp->best_time = clear_time;
    }
}

static void use_ticket(Game *g, const MapDef *map) {
    if (map->kind == MAPK_TEMPLE && g->world.temple_tickets > 0) {
        g->world.temple_tickets--;
    } else if (map->kind == MAPK_RUINS && g->world.ruins_tickets > 0) {
        g->world.ruins_tickets--;
    }
}

void game_on_map_clear(Game *g, int map_idx, const int *hero_idx, int hero_count, float clear_time) {
    if (map_idx < 0 || map_idx >= g_map_count) {
        return;
    }
    const MapDef *map = &g_maps[map_idx];
    MapProgress *mp = &g->world.map[map_idx];
    CommanderMods cm;
    commander_mods(g, &cm);
    Tier ref = map->boss_map ? TIER_BOSS : TIER_UNIQUE;
    float retry = (float)g_bal.retry_wait_s * maxf(1.0f + cm.retry_time_pct / 100.0f, 0.1f);
    for (int i = 0; i < hero_count; i++) {
        if (!hero_ok(g, hero_idx[i])) {
            continue;
        }
        Hero *h = &g->heroes[hero_idx[i]];
        double exp = (double)monster_exp_value(map->level, ref) * (1.0 + (double)cm.exp_pct / 100.0);
        hero_gain_exp(g, hero_idx[i], (u64)(exp * (double)game_exp_rate(h, map->level)));
        h->retry_timer = maxf(retry, 1.0f);
    }
    currency_add(g, CUR_GOLD, (i64)((double)game_kill_gold(map->level, ref) * (double)CLEAR_GOLD_MULT *
                                    (1.0 + (double)cm.gold_pct / 100.0)));
    grant_magic_stones(g, map, &cm);
    if (map->boss_map) {
        grant_boss_chest(g);
        g->player.counters[COUNTER_BOSSES]++;
    }
    if (!mp->first_claimed) {
        mp->first_claimed = 1;
        grant_first_clear(g, map);
    }
    update_progress(g, map_idx, clear_time);
    use_ticket(g, map);
    g->player.counters[COUNTER_MAPS_CLEARED]++;
    quests_progress(g, QK_CLEAR_MAP, 0, 1);
    game_toast(g, -1, 0xF2C94C, "Map abgeschlossen: %s", map->name);
    g->dirty_save = true;
}
