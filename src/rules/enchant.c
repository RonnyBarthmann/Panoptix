/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Verzaubern (Spec 12, Abschnitt 4): Optionspool je Slot, Grade nach Tier,
 * Neu wuerfeln mit Stein + Gold, "Siegel" behaelt die bessere Verzauberung. */
#include <math.h>
#include <string.h>

#include "core/util.h"
#include "rules/rules_internal.h"

#define ENCH_MAX_GRADE 6 /* Grad des Legendaer-Tiers; Optionswerte skalieren mit grade / 6 */

/* ---- Optionspool -------------------------------------------------------------- */
int enchant_option_pool(const ItemDef *d, int *out, int cap) {
    static const int k_weapon[] = {ENCH_FLAT_DMG, ENCH_DMG_PCT, ENCH_ATK_SPEED, ENCH_CAST_SPEED};
    static const int k_helm[] = {ENCH_CDR, ENCH_CRIT, ENCH_FLAT_DMG, ENCH_ARMOR_FLAT};
    static const int k_chest[] = {ENCH_DR_ATTACK, ENCH_DR_SPELL, ENCH_ARMOR_PCT, ENCH_HP_PCT};
    static const int k_gloves[] = {ENCH_FLAT_DMG, ENCH_CRIT, ENCH_ATK_SPEED, ENCH_CAST_SPEED};
    static const int k_boots[] = {ENCH_CDR, ENCH_MOVE_FLAT, ENCH_MOVE_PCT, ENCH_FLAT_DMG};
    static const int k_sub[] = {ENCH_SKILL_DMG, ENCH_SKILL_CAST, ENCH_SKILL_RANGE, ENCH_SKILL_PROJ, ENCH_SKILL_DUR};
    const int *src = NULL;
    int n = 0;
    if (d->kind == IK_WEAPON) {
        src = k_weapon, n = ARRAY_LEN(k_weapon);
    } else if (d->kind == IK_SUB) {
        src = k_sub, n = ARRAY_LEN(k_sub);
    } else if (d->kind == IK_ARMOR) {
        switch (d->slot) {
        case SLOT_HELM: src = k_helm, n = ARRAY_LEN(k_helm); break;
        case SLOT_CHEST: src = k_chest, n = ARRAY_LEN(k_chest); break;
        case SLOT_GLOVES: src = k_gloves, n = ARRAY_LEN(k_gloves); break;
        case SLOT_BOOTS: src = k_boots, n = ARRAY_LEN(k_boots); break;
        default: break;
        }
    }
    n = mini(n, cap);
    for (int i = 0; i < n; i++) {
        out[i] = src[i];
    }
    return n;
}

/* Maximalwert (Grad 6) einer Option auf diesem Gegenstand. */
static float option_max(int opt, const ItemDef *d) {
    switch (opt) {
    case ENCH_FLAT_DMG: return d->kind == IK_WEAPON ? 40.0f : (d->slot == SLOT_HELM ? 50.0f : (d->slot == SLOT_GLOVES ? 20.0f : 15.0f));
    case ENCH_DMG_PCT: return 25.0f;
    case ENCH_ATK_SPEED: return 20.0f;
    case ENCH_CAST_SPEED: return 20.0f;
    case ENCH_CDR: return d->slot == SLOT_HELM ? 10.0f : 8.0f;
    case ENCH_CRIT: return d->slot == SLOT_HELM ? 12.0f : 10.0f;
    case ENCH_ARMOR_FLAT: return 150.0f;
    case ENCH_ARMOR_PCT: return 20.0f;
    case ENCH_HP_PCT: return 20.0f;
    case ENCH_DR_ATTACK: return 12.0f;
    case ENCH_DR_SPELL: return 15.0f;
    case ENCH_MOVE_FLAT: return 60.0f;
    case ENCH_MOVE_PCT: return 12.0f;
    case ENCH_SKILL_DMG: return 40.0f;
    case ENCH_SKILL_CAST: return 25.0f;
    case ENCH_SKILL_RANGE: return 60.0f;
    case ENCH_SKILL_PROJ: return 30.0f;
    case ENCH_SKILL_DUR: return 30.0f;
    default: return 0.0f;
    }
}

/* ---- Voraussetzungen und Kosten ----------------------------------------------- */
int enchant_stone_def(const ItemDef *d) {
    return data_find_stone(STONE_ENCHANT, d->tier);
}

i64 enchant_gold_cost(const ItemDef *d) {
    double tier_f = pow(2.5, (double)(d->tier - TIER_RARE));
    return (i64)ceil(200.0 * (double)(maxi(d->min_level, 0) + 10) * tier_f);
}

bool enchant_can(const Game *g, const Item *it, char *err) {
    (void)g;
    if (!it || item_is_empty(it) || it->def >= g_item_count) {
        rules_err(err, "Kein Gegenstand");
        return false;
    }
    const ItemDef *d = &g_items[it->def];
    if (!d->enchantable || d->tier < TIER_RARE) {
        rules_err(err, "Nicht verzauberbar");
        return false;
    }
    if (enchant_stone_def(d) < 0) {
        rules_err(err, "Kein Verzauberungsstein für dieses Tier");
        return false;
    }
    return true;
}

/* ---- Wuerfeln ------------------------------------------------------------------- */
static bool skill_fits_option(const SkillDef *s, int opt) {
    switch (opt) {
    case ENCH_SKILL_DMG: return s->dmg_pct > 0.0f;
    case ENCH_SKILL_CAST: return s->cast_time > 0.0f;
    case ENCH_SKILL_RANGE: return s->range > 0.0f;
    case ENCH_SKILL_PROJ: return s->kind == SK_PROJECTILE;
    case ENCH_SKILL_DUR: return s->dur > 0.0f;
    default: return true;
    }
}

/* Waehlt einen passenden Skill der Klasse; -1 wenn keiner zur Option passt. */
static int pick_skill_for(Game *g, int cls, int opt) {
    int list[MAX_SKILLS], fit[MAX_SKILLS];
    int n = data_query_skills(cls, list, MAX_SKILLS), m = 0;
    for (int i = 0; i < n; i++) {
        if (skill_fits_option(&g_skills[list[i]], opt)) {
            fit[m++] = list[i];
        }
    }
    return m > 0 ? fit[rng_range(&g->rng, 0, m - 1)] : -1;
}

/* Grad 1..tier+1; niedrige Grade sind haeufiger. */
static int roll_grade(Game *g, const ItemDef *d) {
    int max_grade = mini((int)d->tier + 1, ENCH_MAX_GRADE);
    float u = rng_float(&g->rng);
    return clampi(1 + (int)((float)max_grade * u * sqrtf(u)), 1, max_grade);
}

static bool roll_new(Game *g, const ItemDef *d, Item *out) {
    int pool[8];
    int n = enchant_option_pool(d, pool, 8);
    for (int attempt = 0; attempt < 16 && n > 0; attempt++) {
        int opt = pool[rng_range(&g->rng, 0, n - 1)];
        int skill = -1;
        if (d->kind == IK_SUB) {
            skill = pick_skill_for(g, d->cls, opt);
            if (skill < 0) {
                continue;
            }
        }
        out->ench_opt = (u8)opt;
        out->ench_skill = (i16)skill;
        out->ench_grade = (u8)roll_grade(g, d);
        out->ench_val = option_max(opt, d) * (float)out->ench_grade / (float)ENCH_MAX_GRADE;
        return true;
    }
    return false;
}

bool enchant_roll(Game *g, Item *it, char *err) {
    if (!enchant_can(g, it, err)) {
        return false;
    }
    const ItemDef *d = &g_items[it->def];
    int stone = enchant_stone_def(d);
    i64 gold = enchant_gold_cost(d);
    if (g->inv.stock[stone] < 1) {
        rules_err(err, "Verzauberungsstein fehlt");
        return false;
    }
    if (g->player.gold < gold) {
        rules_err(err, "Nicht genug Gold");
        return false;
    }
    Item fresh = *it;
    if (!roll_new(g, d, &fresh)) {
        rules_err(err, "Keine Verzauberung möglich");
        return false;
    }
    inv_take_stock(g, stone, 1);
    currency_pay(g, CUR_GOLD, gold);
    bool keep_old = (it->flags & ITEMF_SEALED) && it->ench_opt != ENCH_NONE && fresh.ench_grade <= it->ench_grade;
    if (!keep_old) {
        it->ench_opt = fresh.ench_opt;
        it->ench_grade = fresh.ench_grade;
        it->ench_skill = fresh.ench_skill;
        it->ench_val = fresh.ench_val;
    }
    game_mark_all_dirty(g);
    g->dirty_save = true;
    return true;
}

void enchant_set_seal(Item *it, bool sealed) {
    if (sealed) {
        it->flags |= ITEMF_SEALED;
    } else {
        it->flags &= (u8)~ITEMF_SEALED;
    }
}

/* ---- Anzeige und Wirkung ----------------------------------------------------------- */
char *enchant_describe(const Item *it, char *buf, size_t size) {
    if (!it || it->ench_opt == ENCH_NONE || it->ench_opt >= ENCH_COUNT) {
        str_copy(buf, size, "Keine Verzauberung");
        return buf;
    }
    float v = it->ench_val;
    const char *sk = (it->ench_skill >= 0 && it->ench_skill < g_skill_count) ? g_skills[it->ench_skill].name : "";
    switch (it->ench_opt) {
    case ENCH_FLAT_DMG: str_format(buf, size, "+%.0f Schaden", v); break;
    case ENCH_DMG_PCT: str_format(buf, size, "+%.0f %% Schaden", v); break;
    case ENCH_ATK_SPEED: str_format(buf, size, "+%.0f %% Angriffstempo", v); break;
    case ENCH_CAST_SPEED: str_format(buf, size, "+%.0f %% Zaubertempo", v); break;
    case ENCH_CDR: str_format(buf, size, "+%.1f %% Abklingzeitreduktion", v); break;
    case ENCH_CRIT: str_format(buf, size, "+%.1f %% Krit-Chance", v); break;
    case ENCH_ARMOR_FLAT: str_format(buf, size, "+%.0f Rüstung", v); break;
    case ENCH_ARMOR_PCT: str_format(buf, size, "+%.0f %% Rüstung", v); break;
    case ENCH_HP_PCT: str_format(buf, size, "+%.0f %% Max-Leben", v); break;
    case ENCH_DR_ATTACK: str_format(buf, size, "-%.0f %% physischer Schaden", v); break;
    case ENCH_DR_SPELL: str_format(buf, size, "-%.0f %% Zauberschaden", v); break;
    case ENCH_MOVE_FLAT: str_format(buf, size, "+%.0f Lauftempo", v); break;
    case ENCH_MOVE_PCT: str_format(buf, size, "+%.0f %% Lauftempo", v); break;
    case ENCH_SKILL_DMG: str_format(buf, size, "+%.0f %% Schaden auf %s", v, sk); break;
    case ENCH_SKILL_CAST: str_format(buf, size, "+%.0f %% Zaubertempo auf %s", v, sk); break;
    case ENCH_SKILL_RANGE: str_format(buf, size, "+%.0f Reichweite auf %s", v, sk); break;
    case ENCH_SKILL_PROJ: str_format(buf, size, "+%.0f %% Projektiltempo auf %s", v, sk); break;
    default: str_format(buf, size, "+%.0f %% Dauer auf %s", v, sk); break;
    }
    if (it->flags & ITEMF_SEALED) {
        str_append(buf, size, " (Gesiegelt)");
    }
    return buf;
}

/* Skill-spezifische Optionen (ENCH_SKILL_*) wirkt die Simulation direkt am Skill. */
void enchant_apply_mods(const Item *it, StatMods *out) {
    if (!it || it->ench_opt == ENCH_NONE || it->ench_opt >= ENCH_COUNT) {
        return;
    }
    float v = it->ench_val;
    float *m = out->v;
    switch (it->ench_opt) {
    case ENCH_FLAT_DMG: m[STAT_ATTACK] += v; break;
    case ENCH_DMG_PCT:
        m[STAT_ATTACK_PCT] += v;
        m[STAT_SPELL_PCT] += v;
        break;
    case ENCH_ATK_SPEED: m[STAT_ATTACK_SPEED] += v; break;
    case ENCH_CAST_SPEED: m[STAT_CAST_SPEED] += v; break;
    case ENCH_CDR: m[STAT_CDR] += v; break;
    case ENCH_CRIT: m[STAT_CRIT_CHANCE] += v; break;
    case ENCH_ARMOR_FLAT: m[STAT_ARMOR_FLAT] += v; break;
    case ENCH_ARMOR_PCT: m[STAT_ARMOR_PCT] += v; break;
    case ENCH_HP_PCT: m[STAT_HP_PCT] += v; break;
    case ENCH_DR_ATTACK: m[STAT_DMG_REDUCE] += 0.6f * v; break; /* physisch: schwaecher als Zauber */
    case ENCH_DR_SPELL: m[STAT_DMG_REDUCE] += v; break;
    case ENCH_MOVE_FLAT: m[STAT_MOVE_SPEED] += v / (float)g_bal.base_move_speed * 100.0f; break;
    case ENCH_MOVE_PCT: m[STAT_MOVE_SPEED] += v; break;
    default: break;
    }
}
