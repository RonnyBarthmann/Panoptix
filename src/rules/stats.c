/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Stat-Berechnung (Spec 03): Ausruestung, Verbesserung, Verzauberung, Sets,
 * Legendaer-Effekte und Kommandeur-Traits werden zu StatInputs summiert und
 * danach in DerivedStats umgerechnet. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "core/util.h"
#include "rules/rules_internal.h"

/* ---- Kleine Helfer -------------------------------------------------------- */
void rules_err(char *err, const char *fmt, ...) {
    if (!err) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, 96, fmt, ap);
    va_end(ap);
}

void game_mark_all_dirty(Game *g) {
    for (int i = 0; i < MAX_HEROES; i++) {
        g->heroes[i].stats_dirty = true;
    }
}

bool item_kind_is_equipment(ItemKind k) {
    return k == IK_WEAPON || k == IK_SUB || k == IK_ARMOR || k == IK_RING || k == IK_NECKLACE ||
           k == IK_WINGS || k == IK_BAG;
}

/* %Mehr-Schaden-Meilensteine der Waffen-Verbesserung (Spec 12), Index = Stufe. */
static const float k_weapon_more_pct[ENHANCE_MAX + 1] = {0,  0,  0,  0,  0,  6,  7,  8,  9,  10, 15,
                                                         17, 19, 22, 25, 33, 37, 41, 45, 49, 59};

float enhance_main_factor(int plus) {
    plus = clampi(plus, 0, ENHANCE_MAX);
    float f = 1.0f + 0.04f * (float)plus;
    for (int m = 5; m <= ENHANCE_MAX; m += 5) {
        if (plus >= m) {
            f += 0.10f;
        }
    }
    return f;
}

float item_main_value(const Item *it) {
    if (!it || item_is_empty(it) || it->def >= g_item_count) {
        return 0.0f;
    }
    const ItemDef *d = &g_items[it->def];
    int plus = clampi(it->plus, 0, ENHANCE_MAX);
    if (d->kind == IK_WEAPON) {
        return d->value * (1.0f + (float)g_bal.enhance_flat_pct / 100.0f * (float)plus);
    }
    return d->value * enhance_main_factor(plus);
}

void item_add_mods(const Item *it, StatMods *out) {
    if (!it || item_is_empty(it) || it->def >= g_item_count) {
        return;
    }
    const ItemDef *d = &g_items[it->def];
    StatMods m = d->mods;
    if (d->kind == IK_RING || d->kind == IK_NECKLACE) {
        /* Schmuck: Hauptwerte skalieren mit der Verbesserung. */
        statmods_scale(&m, enhance_main_factor(it->plus));
    }
    statmods_add(out, &m);
    if (d->kind == IK_WEAPON && it->plus > 0) {
        float more = k_weapon_more_pct[clampi(it->plus, 0, ENHANCE_MAX)];
        out->v[STAT_MORE_ATTACK] += more;
        out->v[STAT_MORE_SPELL] += more;
    }
    enchant_apply_mods(it, out);
}

/* ---- Kurven --------------------------------------------------------------- */
u64 hero_exp_for_level(int level) {
    level = maxi(level, 1);
    return (u64)llround(g_bal.exp_hero_k * pow((double)level, g_bal.exp_hero_pow));
}

u64 player_exp_for_level(int level) {
    level = maxi(level, 1);
    return (u64)llround(g_bal.exp_player_k * pow((double)level, g_bal.exp_player_pow));
}

float monster_base_hp(int level) {
    return (float)(g_bal.monster_hp_base + g_bal.monster_hp_k * pow((double)maxi(level, 1), g_bal.monster_hp_pow));
}

float monster_base_damage(int level) {
    return (float)(g_bal.monster_dmg_base +
                   g_bal.monster_dmg_k * pow((double)maxi(level, 1), g_bal.monster_dmg_pow));
}

float monster_base_armor(int level) {
    return (float)(g_bal.monster_armor_per_lv * (double)maxi(level, 1));
}

float monster_base_spellres(int level) {
    return (float)(g_bal.monster_res_per_lv * (double)maxi(level, 1));
}

float monster_exp_value(int map_level, Tier tier) {
    return (float)(g_bal.monster_exp_k * pow((double)maxi(map_level, 1), g_bal.monster_exp_pow) *
                   (double)g_tiers[tier].exp_mult);
}

float armor_reduction(float armor, int attacker_level) {
    if (armor <= 0.0f) {
        return 0.0f;
    }
    float r = armor / (armor + (float)g_bal.armor_k_mlv * (float)maxi(attacker_level, 0) + (float)g_bal.armor_c);
    return minf(r, (float)g_bal.armor_cap);
}

/* ---- Eingaben ------------------------------------------------------------- */
/* Ausruestungsteile (ohne Traenke) summieren; Kistenkapazitaet je Teil auf 30 deckeln. */
static void sum_equipment(const Hero *h, StatInputs *in) {
    for (int s = 0; s < SLOT_COUNT; s++) {
        if (s == SLOT_POTION1 || s == SLOT_POTION2 || s == SLOT_POTION3) {
            continue;
        }
        const Item *it = &h->eq[s];
        if (item_is_empty(it) || it->def >= g_item_count) {
            continue;
        }
        const ItemDef *d = &g_items[it->def];
        StatMods tmp;
        memset(&tmp, 0, sizeof tmp);
        item_add_mods(it, &tmp);
        if (s == SLOT_CHEST || s == SLOT_BAG) {
            tmp.v[STAT_CHEST_CAP] = minf(tmp.v[STAT_CHEST_CAP], 30.0f);
        }
        statmods_add(&in->mods, &tmp);
        if (s == SLOT_WEAPON) {
            in->weapon_dmg += item_main_value(it);
        } else if (d->kind == IK_ARMOR || d->kind == IK_SUB) {
            in->armor_items += item_main_value(it);
        }
    }
}

static void add_sets_and_legend(const Hero *h, StatInputs *in) {
    int set_count[8] = {0};
    for (int s = 0; s < SLOT_COUNT; s++) {
        const Item *it = &h->eq[s];
        if (item_is_empty(it) || it->def >= g_item_count) {
            continue;
        }
        const ItemDef *d = &g_items[it->def];
        if (d->set_id > 0 && d->set_id < 8) {
            set_count[d->set_id]++;
        }
        int eff = (h->cls >= 0 && h->cls < CLASS_COUNT) ? d->legendary_effect[h->cls] : 0;
        if (eff > 0 && eff < 32 && g_legend[eff].id == eff) {
            in->legendary_mask |= 1u << eff;
            statmods_add(&in->mods, &g_legend[eff].mods);
        }
    }
    for (int sid = 1; sid < 8; sid++) {
        const SetDef *sd = &g_sets[sid];
        if (sd->id != sid) {
            continue;
        }
        if (set_count[sid] >= 2) {
            statmods_add(&in->mods, &sd->mods2);
        }
        if (set_count[sid] >= 4) {
            statmods_add(&in->mods, &sd->mods4);
            if (sd->behavior4 == LB_CHAIN_LIGHTNING) {
                in->legendary_mask |= 1u << LEGEND_BIT_CHAIN_LIGHTNING;
            } else {
                in->legendary_mask |= 1u << LEGEND_BIT_ASTRAL_SET;
            }
        }
    }
}

static void add_commander(const Game *g, StatInputs *in) {
    CommanderMods cm;
    commander_mods(g, &cm);
    in->mods.v[STAT_ATTACK] += cm.flat_attack;
    in->mods.v[STAT_ARMOR_PCT] += cm.armor_pct;
    in->mods.v[STAT_SPELLRES_PCT] += cm.spellres_pct;
    in->mods.v[STAT_CDR] += cm.cdr_pct;
    in->mods.v[STAT_MOVE_SPEED] += cm.move_speed_pct;
    in->mods.v[STAT_CHEST_CAP] += (float)cm.chest_cap_flat;
}

void hero_recalc_stats(const Game *g, Hero *h) {
    StatInputs *in = &h->inputs;
    memset(in, 0, sizeof *in);
    int cls = clampi(h->cls, 0, CLASS_COUNT - 1);
    const ClassDef *c = &g_classes[cls];
    in->cls = cls;
    in->level = h->level;
    in->str = (float)h->str;
    in->dex = (float)h->dex;
    in->intel = (float)h->intel;
    in->base_attack_speed = c->base_speed;
    in->range = c->base_range;
    sum_equipment(h, in);
    add_sets_and_legend(h, in);
    add_commander(g, in);
    stats_finalize(in, &h->stats);
    h->stats_dirty = false;
}

const DerivedStats *hero_stats(const Game *g, Hero *h) {
    if (h->stats_dirty) {
        hero_recalc_stats(g, h);
    }
    return &h->stats;
}

/* ---- Abgeleitete Werte ---------------------------------------------------- */
void stats_finalize(const StatInputs *in, DerivedStats *out) {
    const float *m = in->mods.v;
    const ClassDef *c = &g_classes[clampi(in->cls, 0, CLASS_COUNT - 1)];
    memset(out, 0, sizeof *out);

    out->max_hp = ((float)g_bal.hp_base + (float)g_bal.hp_per_level * (float)in->level +
                   (float)g_bal.hp_per_str * in->str + m[STAT_HP_FLAT]) *
                  (1.0f + m[STAT_HP_PCT] / 100.0f);
    out->max_mana = ((float)g_bal.mana_base + (float)g_bal.mana_per_level * (float)in->level +
                     (float)g_bal.mana_per_int * in->intel + m[STAT_MANA_FLAT]) *
                    (1.0f + m[STAT_MANA_PCT] / 100.0f);
    out->hp_regen = m[STAT_HP_REGEN];
    out->mana_regen = out->max_mana * (float)g_bal.base_mana_regen_pct / 100.0f + m[STAT_MANA_REGEN];

    /* Ruestung (STR) bzw. Zauberwiderstand (INT): (Basis + Items) * (1 + 0.02*Stat + %) */
    out->armor = (c->base_armor + in->armor_items + m[STAT_ARMOR_FLAT]) *
                 (1.0f + 0.02f * in->str + m[STAT_ARMOR_PCT] / 100.0f);
    out->spell_res = (c->base_armor + 0.5f * in->armor_items) * (1.0f + 0.02f * in->intel + m[STAT_SPELLRES_PCT] / 100.0f);
    out->dr_attack = m[STAT_DR_ATTACK];
    out->dr_spell = m[STAT_DR_SPELL];
    out->dmg_reduce = clampf(m[STAT_DMG_REDUCE] / 100.0f, 0.0f, 0.9f);
    out->physical_taken_mult = 1.0f - out->dmg_reduce;
    out->spell_taken_mult = 1.0f - out->dmg_reduce;

    /* Schaden: flacher "dmg_flat" wirkt auf beide Arten (per Balance abschaltbar fuer Zauber). */
    float flat_both = m[STAT_ATTACK];
    float spell_flat_both = g_bal.jewelry_attack_affects_spells > 0.5 ? flat_both : 0.0f;
    out->attack_power = in->weapon_dmg + in->str + flat_both + m[STAT_ATTACK_PHYS];
    out->spell_power = in->weapon_dmg + in->intel + spell_flat_both + m[STAT_SPELL_FLAT] + in->spell_extra_flat;
    out->atk_mult = (1.0f + m[STAT_ATTACK_PCT] / 100.0f) * (1.0f + m[STAT_MORE_ATTACK] / 100.0f);
    out->spell_mult = (1.0f + m[STAT_SPELL_PCT] / 100.0f) * (1.0f + m[STAT_MORE_SPELL] / 100.0f);

    out->crit_chance = clampf((float)g_bal.base_crit_chance + m[STAT_CRIT_CHANCE] + 0.2f * in->intel, 0.0f, 100.0f);
    out->crit_dmg = (float)g_bal.base_crit_dmg + 1.5f * in->dex + m[STAT_CRIT_DMG];

    float dex_speed = (c == &g_classes[CLASS_ASSASSIN]) ? 1.8f : 1.5f;
    out->attack_speed = in->base_attack_speed * (1.0f + (dex_speed * in->dex + m[STAT_ATTACK_SPEED]) / 100.0f);
    out->cast_speed = 1.0f + m[STAT_CAST_SPEED] / 100.0f;

    float base_move = (float)g_bal.base_move_speed * c->move_factor;
    float move = base_move * (1.0f + (0.5f * in->dex + m[STAT_MOVE_SPEED]) / 100.0f);
    out->move_speed = minf(move, (float)g_bal.base_move_speed * (float)g_bal.move_speed_cap_pct / 100.0f);

    float cdr_cap = minf((float)g_bal.cdr_cap + m[STAT_CDR_CAP], (float)g_bal.cdr_hard_cap);
    out->cdr = clampf(0.2f * in->intel + m[STAT_CDR], 0.0f, cdr_cap);
    out->heal_mult = 1.0f + m[STAT_HEAL_PCT] / 100.0f;

    out->chest_capacity = clampi((int)((float)g_bal.chest_base_capacity + m[STAT_CHEST_CAP]), 1,
                                 (int)g_bal.chest_max_capacity + 64);
    out->extra_projectiles = (int)m[STAT_PROJECTILES];
    out->extra_pierce = (int)m[STAT_PIERCE];
    out->range = in->range;
}
