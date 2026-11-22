/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Progression: Level, Stat-Punkte, Skillbuecher und -level, Skill-Slots,
 * KI-Voreinstellungen, Startausruestung (Spec 03, 05, 06). */
#include <math.h>
#include <string.h>

#include "core/util.h"
#include "rules/rules_internal.h"

#define STAT_RESET_DIAMONDS 100 /* Spec 13: Stat-Reset 100 Diamanten (je Held) */

/* ---- Hilfen ---------------------------------------------------------------- */
static bool hero_valid(const Game *g, int idx) {
    return idx >= 0 && idx < MAX_HEROES && g->heroes[idx].used;
}

int skill_grade_factor(int grade) {
    static const int k_factor[4] = {1, 2, 4, 8};
    return k_factor[clampi(grade, 0, 3)];
}

/* Map-Level -> Akt (1..11). Akt 1: Lv 1-4, Akt 2: 5-14, danach je 10 Level; Akt 10 bis 99, Akt 11 ab 100. */
int act_for_level(int level) {
    if (level < 5) {
        return 1;
    }
    if (level >= 100) {
        return 11;
    }
    if (level >= 85) {
        return 10;
    }
    return clampi((level - 5) / 10 + 2, 2, 9);
}

/* ---- EXP und Level --------------------------------------------------------- */
int hero_gain_exp(Game *g, int hero_idx, u64 amount) {
    if (!hero_valid(g, hero_idx)) {
        return 0;
    }
    Hero *h = &g->heroes[hero_idx];
    h->total_exp_earned += amount;
    if (h->level >= HERO_MAX_LEVEL) {
        return 0;
    }
    h->exp += amount;
    int ups = 0;
    while (h->level < HERO_MAX_LEVEL) {
        u64 need = hero_exp_for_level(h->level);
        if (h->exp < need) {
            break;
        }
        h->exp -= need;
        h->level++;
        h->free_points++;
        ups++;
    }
    if (h->level >= HERO_MAX_LEVEL) {
        h->exp = 0;
    }
    if (ups > 0) {
        h->stats_dirty = true;
        g->dirty_save = true;
    }
    return ups;
}

int player_gain_exp(Game *g, u64 amount) {
    Player *p = &g->player;
    if (p->level >= PLAYER_MAX_LEVEL) {
        return 0;
    }
    p->exp += amount;
    int ups = 0;
    while (p->level < PLAYER_MAX_LEVEL) {
        u64 need = player_exp_for_level(p->level);
        if (p->exp < need) {
            break;
        }
        p->exp -= need;
        p->level++;
        p->trait_points_total++; /* ein Kommandeur-Punkt je Level-Up (Spec 20) */
        ups++;
    }
    if (p->level >= PLAYER_MAX_LEVEL) {
        p->exp = 0;
    }
    if (ups > 0) {
        quests_progress(g, QK_REACH_LEVEL, 0, p->level);
        g->dirty_save = true;
    }
    return ups;
}

/* ---- Stat-Punkte ----------------------------------------------------------- */
bool hero_spend_point(Game *g, int hero_idx, int stat, int amount) {
    if (!hero_valid(g, hero_idx) || amount <= 0 || stat < 0 || stat > 2) {
        return false;
    }
    Hero *h = &g->heroes[hero_idx];
    if (h->free_points < amount) {
        return false;
    }
    int *target = stat == 0 ? &h->str : (stat == 1 ? &h->dex : &h->intel);
    *target += amount;
    h->free_points -= amount;
    h->stats_dirty = true;
    g->dirty_save = true;
    return true;
}

bool hero_reset_stats(Game *g, int hero_idx, char *err) {
    if (!hero_valid(g, hero_idx)) {
        rules_err(err, "Kein Held");
        return false;
    }
    Hero *h = &g->heroes[hero_idx];
    if (!currency_pay(g, CUR_DIAMONDS, STAT_RESET_DIAMONDS)) {
        rules_err(err, "Benötigt %d Diamanten", STAT_RESET_DIAMONDS);
        return false;
    }
    h->free_points += h->str + h->dex + h->intel;
    h->str = h->dex = h->intel = 0;
    h->stats_dirty = true;
    g->dirty_save = true;
    return true;
}

/* ---- Skillbuecher und -level ----------------------------------------------- */
void skill_add_book(Game *g, int skill, int count) {
    if (skill < 0 || skill >= g_skill_count || count <= 0) {
        return;
    }
    Player *p = &g->player;
    if (p->skill_level[skill] == 0) {
        p->skill_level[skill] = 1; /* erster Fund schaltet frei */
        count--;
    }
    int books = (int)p->skill_books[skill] + count;
    p->skill_books[skill] = (u16)mini(books, 65535);
    g->dirty_save = true;
}

bool skill_can_level_up(const Game *g, int skill, i64 *gold_cost, int *books_needed) {
    if (skill < 0 || skill >= g_skill_count) {
        return false;
    }
    int lv = g->player.skill_level[skill];
    if (lv < 1 || lv >= SKILL_MAX_LEVEL) {
        return false;
    }
    i64 gold = 1000 * (i64)lv * lv * skill_grade_factor(g_skills[skill].grade);
    if (gold_cost) {
        *gold_cost = gold;
    }
    if (books_needed) {
        *books_needed = lv;
    }
    return (int)g->player.skill_books[skill] >= lv && g->player.gold >= gold;
}

bool skill_level_up(Game *g, int skill, char *err) {
    if (skill < 0 || skill >= g_skill_count || g->player.skill_level[skill] < 1) {
        rules_err(err, "Skill nicht freigeschaltet");
        return false;
    }
    if (g->player.skill_level[skill] >= SKILL_MAX_LEVEL) {
        rules_err(err, "Maximales Level erreicht");
        return false;
    }
    i64 gold = 0;
    int books = 0;
    if (!skill_can_level_up(g, skill, &gold, &books)) {
        if ((int)g->player.skill_books[skill] < books) {
            rules_err(err, "Benötigt %d Bücher", books);
        } else {
            rules_err(err, "Nicht genug Gold");
        }
        return false;
    }
    currency_pay(g, CUR_GOLD, gold);
    g->player.skill_books[skill] = (u16)(g->player.skill_books[skill] - books);
    g->player.skill_level[skill]++;
    game_mark_all_dirty(g);
    g->dirty_save = true;
    return true;
}

float skill_dmg_pct(const SkillDef *s, int level) {
    level = clampi(level, 1, SKILL_MAX_LEVEL);
    return s->dmg_pct * (1.0f + (float)g_bal.skill_level_scale * (float)(level - 1));
}

float skill_stat_value(const SkillDef *s, int level, bool second) {
    level = clampi(level, 1, SKILL_MAX_LEVEL);
    float base = second ? s->stat2_pct : s->stat_pct;
    return base * (1.0f + (float)g_bal.skill_level_scale * (float)(level - 1));
}

/* ---- KI-Voreinstellungen ----------------------------------------------------- */
void skill_ai_from_def(SkillAi *out, const SkillDef *s) {
    memset(out, 0, sizeof *out);
    out->priority = s->ai.priority;
    out->tier_mask = s->ai.tier_mask ? s->ai.tier_mask : 0x3F;
    out->min_targets = s->ai.min_targets;
    out->max_dist = s->ai.max_dist;
    out->self_hp_min = 0;
    out->self_hp_max = s->ai.self_hp_below > 0 ? s->ai.self_hp_below : 100;
    out->self_mana_min = 0;
    out->self_mana_max = 100;
    out->ally_hp_below = s->ai.ally_hp_below;
    out->target_allies = (s->kind == SK_HEAL || s->kind == SK_HEAL_PARTY || s->kind == SK_HOT ||
                          s->kind == SK_BUFF_PARTY || s->kind == SK_CLEANSE)
                             ? 1
                             : 0;
    out->use_mode = s->ai.only_in_combat ? USE_COMBAT : USE_ALWAYS;
    out->allow_stack = s->ai.allow_stack;
}

void hero_ai_defaults(Hero *h) {
    HeroAi *ai = &h->ai;
    memset(ai, 0, sizeof *ai);
    ai->explore = EXPLORE_SHORTEST;
    ai->clear = CLEAR_FULL_SWEEP;
    ai->target_prio = PRIO_HIGHER_TIER;
    ai->target_tier_mask = 0x3F;
    ai->speed_mode = SPEED_NONE;
    ai->repeat_map = 1;
    ai->take_normal_chests = 1;
    ai->take_magic_chests = 1;
    ai->refocus_lost = 1;
    for (int i = 0; i < SKILL_SLOTS; i++) {
        int sk = h->skill_slot[i];
        if (sk >= 0 && sk < g_skill_count) {
            skill_ai_from_def(&ai->skill[i], &g_skills[sk]);
        } else {
            ai->skill[i].priority = (u8)i;
            ai->skill[i].tier_mask = 0x3F;
            ai->skill[i].self_hp_max = 100;
            ai->skill[i].self_mana_max = 100;
        }
    }
    /* Traenke: Heil bei 40 % HP, Mana bei 30 %, Effekttraenke nur im Kampf. */
    for (int i = 0; i < POTION_SLOTS; i++) {
        ai->potion[i].hp_below = 40;
        ai->potion[i].mana_below = 30;
        ai->potion[i].only_in_combat = 1;
    }
}

bool hero_set_skill(Game *g, int hero_idx, int slot, int skill, char *err) {
    if (!hero_valid(g, hero_idx) || slot < 0 || slot >= SKILL_SLOTS) {
        rules_err(err, "Ungültiger Slot");
        return false;
    }
    Hero *h = &g->heroes[hero_idx];
    if (skill < 0) {
        h->skill_slot[slot] = -1;
        memset(&h->ai.skill[slot], 0, sizeof(SkillAi));
        h->ai.skill[slot].tier_mask = 0x3F;
        h->ai.skill[slot].self_hp_max = 100;
        h->ai.skill[slot].self_mana_max = 100;
        return true;
    }
    if (skill >= g_skill_count) {
        rules_err(err, "Unbekannter Skill");
        return false;
    }
    const SkillDef *s = &g_skills[skill];
    if (g->player.skill_level[skill] < 1) {
        rules_err(err, "Skill nicht freigeschaltet");
        return false;
    }
    if (s->cls != CLASS_COMMON && s->cls != h->cls) {
        rules_err(err, "Falsche Klasse");
        return false;
    }
    for (int i = 0; i < SKILL_SLOTS; i++) {
        if (i != slot && h->skill_slot[i] == skill) {
            rules_err(err, "Skill ist schon belegt");
            return false;
        }
    }
    h->skill_slot[slot] = (i16)skill;
    skill_ai_from_def(&h->ai.skill[slot], s);
    h->ai.skill[slot].priority = (u8)mini(s->ai.priority, 8);
    h->stats_dirty = true;
    g->dirty_save = true;
    return true;
}

/* Build-Vorlagen (Spec 06): je Klasse 4 Skill-IDs. */
static const char *const k_presets[CLASS_COUNT][SKILL_SLOTS] = {
    /* Ritter "Hurry Cain"   */ {"knight_hurricane_strike", "knight_vital_strike", "knight_step_strike", "knight_moment_enlightenment"},
    /* Krieger "Spin to Win" */ {"warrior_slash", "warrior_whirlwind", "common_bless_might", "warrior_battle_cry"},
    /* Magier "Oppenheimer"  */ {"wizard_focus", "wizard_disintegrate", "wizard_nuclear_ball", "wizard_ancient_one"},
    /* Bogenschuetze "Minigun" */ {"archer_eagle_eye", "archer_multi_shot", "archer_rapid_fire", "archer_firm_grip"},
    /* Assassine "Hi Venomous" */ {"assassin_poison_mist", "assassin_venom_fang", "assassin_poison_attack", "assassin_poison_master"},
    /* Schamane "Breath of the Wild" */ {"shaman_totem_red_dragon", "shaman_totem_sea_turtle", "shaman_totem_storm_dragon", "shaman_soul_spike"},
    /* Priester "Boost-Bot"  */ {"priest_divine_bless", "priest_exaltation_fury", "priest_exaltation_swift", "priest_purifying_flame"},
};

bool hero_apply_build_preset(Game *g, int hero_idx) {
    if (!hero_valid(g, hero_idx)) {
        return false;
    }
    Hero *h = &g->heroes[hero_idx];
    if (h->cls < 0 || h->cls >= CLASS_COUNT) {
        return false;
    }
    int set = 0;
    for (int slot = 0; slot < SKILL_SLOTS; slot++) {
        int sk = data_find_skill(k_presets[h->cls][slot]);
        if (sk >= 0 && g->player.skill_level[sk] > 0) {
            h->skill_slot[slot] = (i16)sk;
            set++;
        } else {
            h->skill_slot[slot] = -1;
        }
    }
    hero_ai_defaults(h);
    h->stats_dirty = true;
    g->dirty_save = true;
    return set > 0;
}

/* ---- Startausruestung ------------------------------------------------------ */
void hero_give_starting_gear(Game *g, int hero_idx) {
    if (!hero_valid(g, hero_idx)) {
        return;
    }
    Hero *h = &g->heroes[hero_idx];
    int list[64];
    int n = data_query_items(IK_WEAPON, TIER_NORMAL, SLOT_WEAPON, h->cls, list, 64);
    int best = -1;
    for (int i = 0; i < n; i++) {
        const ItemDef *d = &g_items[list[i]];
        if (d->cls == h->cls && (best < 0 || d->min_level < g_items[best].min_level)) {
            best = list[i];
        }
    }
    if (best >= 0) {
        h->eq[SLOT_WEAPON] = item_make(best);
    }
    int potion = data_find_item("potion_heal_normal");
    if (potion >= 0) {
        h->eq[SLOT_POTION1] = item_make(potion);
    }
    h->stats_dirty = true;
}
