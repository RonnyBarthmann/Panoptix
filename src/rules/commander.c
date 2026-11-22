/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Kommandeur (Spec 20): Traits, Buffs, Kommandoposten, Kopfgeld-/Hauptquests, Post. */
#include <math.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

#include "core/util.h"
#include "rules/rules_internal.h"

#define TRAIT_RESET_DIAMONDS 500   /* Spec 13: Trait-Reset 500 Diamanten */
#define CMD_BUFF_SECONDS 3600.0    /* Kommandeur-Skills: 1 h Dauer */
#define CMD_BUFF_COOLDOWN 5400.0   /* Abklingzeit 1,5 h */
#define BOUNTY_BASE_SLOTS 10       /* + Economy-Trait bis BOUNTY_PER_DAY */
#define ADJUTANT_KILLS_PER_HOUR 900
#define ADJUTANT_BOSS_RUNS_PER_HOUR 6

/* ---- Trait-Wirkungen ------------------------------------------------------------ */
typedef struct EffectField {
    const char *name;
    size_t offset;
    bool is_int;
} EffectField;

#define FIELD_F(n) {#n, offsetof(CommanderMods, n), false}
#define FIELD_I(n) {#n, offsetof(CommanderMods, n), true}
static const EffectField k_fields[] = {
    FIELD_F(drop_qty_pct), FIELD_F(drop_quality_pct), FIELD_F(exp_pct),      FIELD_F(gold_pct),
    FIELD_F(density_pct),  FIELD_F(dupe_chest_pct),   FIELD_F(unique_double_pct), FIELD_F(craft_cost_pct),
    FIELD_F(shop_discount_pct), FIELD_I(bounty_slots), FIELD_F(death_time_pct), FIELD_F(retry_time_pct),
    FIELD_I(chest_cap_flat), FIELD_I(storage_limit_bonus), FIELD_F(move_speed_pct), FIELD_F(flat_attack),
    FIELD_F(armor_pct),    FIELD_F(spellres_pct),     FIELD_F(cdr_pct),      FIELD_F(potion_regen),
    FIELD_F(magic_stone_pct),
};

static void add_effect(CommanderMods *m, const char *effect, float amount) {
    for (int i = 0; i < ARRAY_LEN(k_fields); i++) {
        if (strcmp(k_fields[i].name, effect) != 0) {
            continue;
        }
        char *base = (char *)m + k_fields[i].offset;
        if (k_fields[i].is_int) {
            *(int *)(void *)base += (int)lroundf(amount);
        } else {
            *(float *)(void *)base += amount;
        }
        return;
    }
}

bool commander_skill_active(const Game *g, int skill) {
    return skill >= 0 && skill < CMD_SKILL_COUNT && g->play_time < g->player.cmd_skill_end[skill];
}

/* Buffs (Spec 20 Abschnitt 2) und Befehle (Abschnitt 3) als feste Zuschlaege. */
static void add_buffs_and_orders(const Game *g, CommanderMods *m) {
    if (commander_skill_active(g, 0)) {
        m->drop_qty_pct += 25.0f;     /* Entdeckersicht */
    }
    if (commander_skill_active(g, 1)) {
        m->exp_pct += 25.0f;          /* Verborgener Berater */
    }
    if (commander_skill_active(g, 2)) {
        m->drop_quality_pct += 25.0f; /* Kommandeurs-Glueck */
    }
    if (commander_skill_active(g, 3)) {
        m->gold_pct += 25.0f;         /* Augen der Fuelle */
    }
    const u8 *on = g->player.cmd_order_on;
    if (on[0]) {
        m->drop_qty_pct += 20.0f;     /* Versorgungsbefehl */
    }
    if (on[1]) {
        m->drop_quality_pct += 20.0f; /* Pluenderungsbefehl */
    }
    if (on[2]) {
        m->magic_stone_pct += 50.0f;  /* Suchbefehl */
    }
    if (on[3]) {
        m->exp_pct += 15.0f;          /* Eliteausbildung */
        m->gold_pct += 15.0f;
    }
}

void commander_mods(const Game *g, CommanderMods *out) {
    memset(out, 0, sizeof *out);
    for (int i = 0; i < g_trait_count; i++) {
        int pts = g->player.traits[i];
        if (pts > 0) {
            add_effect(out, g_traits[i].effect, g_traits[i].per_point * (float)pts);
        }
    }
    add_buffs_and_orders(g, out);
}

/* ---- Traits -------------------------------------------------------------------------- */
static int traits_spent(const Game *g) {
    int sum = 0;
    for (int i = 0; i < g_trait_count; i++) {
        sum += g->player.traits[i];
    }
    return sum;
}

int commander_points_available(const Game *g) {
    return maxi(g->player.trait_points_total - traits_spent(g), 0);
}

bool commander_spend_trait(Game *g, int trait, char *err) {
    if (trait < 0 || trait >= g_trait_count) {
        rules_err(err, "Unbekannter Trait");
        return false;
    }
    const TraitDef *t = &g_traits[trait];
    if (commander_points_available(g) < 1) {
        rules_err(err, "Keine Punkte verfügbar");
        return false;
    }
    if (g->player.traits[trait] >= t->max_points) {
        rules_err(err, "Maximale Stufe erreicht");
        return false;
    }
    if (t->requires >= 0 && t->requires < g_trait_count && g->player.traits[t->requires] < 1) {
        rules_err(err, "Benötigt %s", g_traits[t->requires].name);
        return false;
    }
    g->player.traits[trait]++;
    game_mark_all_dirty(g);
    g->dirty_save = true;
    return true;
}

bool commander_reset_traits(Game *g, char *err) {
    if (traits_spent(g) == 0) {
        rules_err(err, "Keine Punkte vergeben");
        return false;
    }
    if (!currency_pay(g, CUR_DIAMONDS, TRAIT_RESET_DIAMONDS)) {
        rules_err(err, "Benötigt %d Diamanten", TRAIT_RESET_DIAMONDS);
        return false;
    }
    memset(g->player.traits, 0, sizeof g->player.traits);
    game_mark_all_dirty(g);
    g->dirty_save = true;
    return true;
}

/* ---- Kommandeur-Skills ----------------------------------------------------------------- */
bool commander_activate_skill(Game *g, int skill, char *err) {
    if (skill < 0 || skill >= CMD_SKILL_COUNT) {
        rules_err(err, "Unbekannter Skill");
        return false;
    }
    Player *p = &g->player;
    if (g->play_time < p->cmd_skill_cd[skill]) {
        rules_err(err, "Noch nicht bereit");
        return false;
    }
    p->cmd_skill_end[skill] = g->play_time + CMD_BUFF_SECONDS;
    p->cmd_skill_cd[skill] = g->play_time + CMD_BUFF_COOLDOWN;
    game_mark_all_dirty(g);
    g->dirty_save = true;
    return true;
}

/* ---- Kommandoposten ---------------------------------------------------------------------- */
/* Kumulative Menge floor(t * rate / 3600): Differenzen driften nicht (Rundung ueber Aufrufe hinweg). */
static i64 cum_units(i64 unix_s, double rate_per_hour) {
    return (i64)floor((double)unix_s * rate_per_hour / 3600.0);
}

static int orders_on(const Game *g) {
    int n = 0;
    for (int i = 0; i < CMD_ORDER_COUNT; i++) {
        n += g->player.cmd_order_on[i] ? 1 : 0;
    }
    return n;
}

void commander_update_post(Game *g, i64 now_unix) {
    Player *p = &g->player;
    shop_check_resets(g);
    if (p->post_last_unix <= 0 || now_unix <= p->post_last_unix) {
        if (p->post_last_unix <= 0) {
            p->post_last_unix = now_unix;
        }
        p->last_seen_unix = now_unix;
        return;
    }
    double hours = (double)(now_unix - p->post_last_unix) / 3600.0;
    double room = maxf((float)(g_bal.post_max_hours_per_day - p->post_hours_today), 0.0f);
    double earn_h = hours < room ? hours : room;
    i64 earned = (i64)floor((p->post_hours_today + earn_h) * g_bal.post_cr_per_hour) -
                 (i64)floor(p->post_hours_today * g_bal.post_cr_per_hour);
    p->post_hours_today += earn_h;
    p->credits = (i64)mini((int)(p->credits + earned), (int)g_bal.post_storage_cap);
    int n = orders_on(g);
    if (n > 0) {
        double rate = g_bal.post_order_cost_per_hour * (double)n;
        i64 cost = cum_units(now_unix, rate) - cum_units(p->post_last_unix, rate);
        if (p->credits >= cost) {
            p->credits -= cost;
        } else { /* Credits aufgebraucht: Befehle enden */
            p->credits = 0;
            memset(p->cmd_order_on, 0, sizeof p->cmd_order_on);
            game_mark_all_dirty(g);
        }
    }
    p->post_last_unix = now_unix;
    p->last_seen_unix = now_unix;
    g->dirty_save = true;
}

bool commander_toggle_order(Game *g, int order, char *err) {
    if (order < 0 || order >= CMD_ORDER_COUNT) {
        rules_err(err, "Unbekannter Befehl");
        return false;
    }
    u8 *on = &g->player.cmd_order_on[order];
    if (!*on && g->player.credits < 1) {
        rules_err(err, "Keine Credits");
        return false;
    }
    *on = *on ? 0 : 1;
    game_mark_all_dirty(g);
    g->dirty_save = true;
    return true;
}

/* ---- Adjutant: 1 h Idle-Ertrag der besten Solo-Map -------------------------------------------- */
typedef struct IdleHaul {
    double exp;
    double gold;
    int chests;
} IdleHaul;

static int adjutant_hero(const Game *g) {
    int best = -1, best_level = -1;
    for (int i = 0; i < MAX_HEROES; i++) {
        const Hero *h = &g->heroes[i];
        if (!h->used || h->assigned_map < 0 || h->assigned_map >= g_map_count) {
            continue;
        }
        if (g_maps[h->assigned_map].level > best_level) {
            best_level = g_maps[h->assigned_map].level;
            best = i;
        }
    }
    return best;
}

static IdleHaul simulate_idle_hour(Game *g, const Hero *h, const MapDef *map, const CommanderMods *cm) {
    IdleHaul out = {0.0, 0.0, 0};
    float weights[TIER_COUNT];
    for (int t = 0; t < TIER_COUNT; t++) {
        weights[t] = g_tiers[t].spawn_weight;
    }
    float rate = clampf(1.0f - (float)g_bal.exp_rate_step * (float)maxi(h->level - map->level, 0),
                        (float)g_bal.exp_rate_min, 1.0f);
    for (int i = 0; i < ADJUTANT_KILLS_PER_HOUR; i++) {
        Tier tier = (Tier)clampi(rng_weighted(&g->rng, weights, TIER_COUNT), 0, TIER_COUNT - 1);
        out.exp += (double)monster_exp_value(map->level, tier) * (double)rate;
        out.gold += (double)game_kill_gold(map->level, tier);
        KillLoot kl;
        loot_roll_kill(g, tier, map->level, &kl);
        for (int c = 0; c < kl.chest_count; c++) {
            g->inv.chests[kl.chest_tier[c]]++; /* unabhaengig vom Kistenlimit */
            out.chests++;
        }
    }
    out.exp *= 1.0 + (double)cm->exp_pct / 100.0;
    out.gold *= 1.0 + (double)cm->gold_pct / 100.0;
    return out;
}

bool commander_adjutant(Game *g, char *err) {
    int hi = adjutant_hero(g);
    if (hi < 0) {
        rules_err(err, "Kein Held auf einer Map");
        return false;
    }
    if (!currency_pay(g, CUR_CREDITS, (i64)g_bal.adjutant_cost)) {
        rules_err(err, "Benötigt %d Credits", (int)g_bal.adjutant_cost);
        return false;
    }
    const Hero *h = &g->heroes[hi];
    const MapDef *map = &g_maps[h->assigned_map];
    CommanderMods cm;
    commander_mods(g, &cm);
    IdleHaul haul = simulate_idle_hour(g, h, map, &cm);
    hero_gain_exp(g, hi, (u64)haul.exp);
    player_gain_exp(g, (u64)haul.exp);
    currency_add(g, CUR_GOLD, (i64)haul.gold);
    if (map->boss_map && map->stone_item >= 0) {
        double mult = 1.0 + (double)cm.magic_stone_pct / 100.0;
        inv_add_stock(g, map->stone_item,
                      (u32)((double)(map->stone_count * ADJUTANT_BOSS_RUNS_PER_HOUR) * mult));
    }
    game_toast(g, -1, 0xF2C94C, "Adjutant: +%.0f EXP, +%.0f Gold, %d Kisten", haul.exp, haul.gold, haul.chests);
    return true;
}

/* ---- Kopfgelder und Hauptquests ------------------------------------------------------------------- */
i64 quests_refresh_cost(const Game *g) {
    i64 n = (i64)g->player.bounty_refreshes + 1;
    return 5000 * n * n;
}

static int bounty_slot_count(const Game *g) {
    CommanderMods cm;
    commander_mods(g, &cm);
    return clampi(BOUNTY_BASE_SLOTS + cm.bounty_slots, 1, BOUNTY_PER_DAY);
}

static void clear_bounties(Player *p) {
    for (int i = 0; i < BOUNTY_PER_DAY; i++) {
        memset(&p->bounty[i], 0, sizeof p->bounty[i]);
        p->bounty[i].def = -1;
    }
}

/* Zieht n verschiedene Kopfgeld-Vorlagen (keine Hauptquests). */
static void draw_bounties(Game *g, int n) {
    int pool[MAX_QUEST_DEFS], count = 0;
    for (int i = 0; i < g_quest_count; i++) {
        if (!g_quests[i].main_quest) {
            pool[count++] = i;
        }
    }
    clear_bounties(&g->player);
    for (int slot = 0; slot < n && count > 0; slot++) {
        int pick = rng_range(&g->rng, 0, count - 1);
        const QuestDef *q = &g_quests[pool[pick]];
        BountyQuest *b = &g->player.bounty[slot];
        b->def = (i16)pool[pick];
        b->target = rng_range(&g->rng, q->count_min, maxi(q->count_max, q->count_min));
        b->param = q->param;
        pool[pick] = pool[--count];
    }
}

void quests_refresh_daily(Game *g, bool paid, char *err_unused) {
    if (paid) {
        i64 cost = quests_refresh_cost(g);
        if (!currency_pay(g, CUR_GOLD, cost)) {
            rules_err(err_unused, "Benötigt %lld Gold", (long long)cost);
            return;
        }
        g->player.bounty_refreshes++;
    }
    draw_bounties(g, bounty_slot_count(g));
    g->dirty_save = true;
}

static bool quest_matches(const QuestDef *q, QuestKind kind, int param) {
    if (q->kind != kind) {
        return false;
    }
    return (kind == QK_KILL_TIER || kind == QK_KILL_ARCH) ? q->param == param : true;
}

static void add_progress(i32 *progress, i32 target, QuestKind kind, int amount) {
    if (kind == QK_REACH_LEVEL) {
        *progress = (i32)maxi(*progress, amount);
    } else {
        *progress += amount;
    }
    *progress = (i32)mini(*progress, target);
}

/* Index der n-ten Hauptquest in g_quests oder -1. */
static int main_quest_def(int n) {
    for (int i = 0; i < g_quest_count; i++) {
        if (g_quests[i].main_quest && n-- == 0) {
            return i;
        }
    }
    return -1;
}

void quests_progress(Game *g, QuestKind kind, int param, int amount) {
    if (amount <= 0) {
        return;
    }
    Player *p = &g->player;
    for (int i = 0; i < BOUNTY_PER_DAY; i++) {
        BountyQuest *b = &p->bounty[i];
        if (b->def >= 0 && !b->claimed && quest_matches(&g_quests[b->def], kind, param)) {
            add_progress(&b->progress, b->target, kind, amount);
        }
    }
    int m = main_quest_def(p->main_quest_index);
    if (m >= 0 && quest_matches(&g_quests[m], kind, param)) {
        add_progress(&p->main_quest_progress, g_quests[m].count_min, kind, amount);
    }
}

static void give_quest_reward(Game *g, const QuestDef *q) {
    player_gain_exp(g, (u64)(q->reward_cmd_exp > 0 ? q->reward_cmd_exp : 0));
    currency_add(g, CUR_GOLD, q->reward_gold);
    currency_add(g, CUR_DIAMONDS, q->reward_diamonds);
}

bool quest_claim(Game *g, int bounty_index, char *err) {
    if (bounty_index < 0 || bounty_index >= BOUNTY_PER_DAY || g->player.bounty[bounty_index].def < 0) {
        rules_err(err, "Kein Kopfgeld");
        return false;
    }
    BountyQuest *b = &g->player.bounty[bounty_index];
    if (b->claimed) {
        rules_err(err, "Bereits abgeholt");
        return false;
    }
    if (b->progress < b->target) {
        rules_err(err, "Noch nicht erfüllt");
        return false;
    }
    b->claimed = 1;
    give_quest_reward(g, &g_quests[b->def]);
    g->dirty_save = true;
    return true;
}

bool main_quest_claim(Game *g, char *err) {
    Player *p = &g->player;
    int m = main_quest_def(p->main_quest_index);
    if (m < 0) {
        rules_err(err, "Keine weitere Hauptquest");
        return false;
    }
    if (p->main_quest_progress < g_quests[m].count_min) {
        rules_err(err, "Noch nicht erfüllt");
        return false;
    }
    give_quest_reward(g, &g_quests[m]);
    p->main_quest_index++;
    p->main_quest_progress = 0;
    int next = main_quest_def(p->main_quest_index);
    if (next >= 0 && g_quests[next].kind == QK_REACH_LEVEL) {
        quests_progress(g, QK_REACH_LEVEL, 0, p->level);
    }
    g->dirty_save = true;
    return true;
}

/* ---- Post --------------------------------------------------------------------------------------------- */
void mail_add(Game *g, const char *title, const char *text, Currency cur, i64 amount) {
    Player *p = &g->player;
    if (p->mail_count >= MAX_MAIL) { /* aelteste Nachricht verdraengen */
        memmove(&p->mail[0], &p->mail[1], sizeof(MailMsg) * (size_t)(MAX_MAIL - 1));
        p->mail_count = MAX_MAIL - 1;
    }
    MailMsg *m = &p->mail[p->mail_count++];
    memset(m, 0, sizeof *m);
    str_copy(m->title, sizeof m->title, title ? title : "");
    str_copy(m->text, sizeof m->text, text ? text : "");
    m->cur = cur;
    m->amount = amount;
    m->claimed = amount <= 0;
    m->time = (i64)time(NULL);
    g->dirty_save = true;
}

bool mail_claim(Game *g, int index) {
    Player *p = &g->player;
    if (index < 0 || index >= p->mail_count) {
        return false;
    }
    MailMsg *m = &p->mail[index];
    m->read = true;
    if (m->claimed || m->amount <= 0) {
        return false;
    }
    currency_add(g, m->cur, m->amount);
    m->claimed = true;
    g->dirty_save = true;
    return true;
}
