/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Lader fuer tiers.tsv, classes.tsv und skills.tsv. */
#include <string.h>

#include "core/util.h"
#include "data/data_internal.h"

static const char *const k_atk_styles[] = {"melee", "projectile", "homing", "dash_stab"};
static const char *const k_dmg_types[] = {"phys", "spell", "hybrid"};
static const char *const k_main_stats[] = {"str", "dex", "int"};
static const char *const k_skill_kinds[SK_KIND_COUNT] = {
    "strike", "circle",     "cone",       "projectile", "ground", "dash",
    "buff_self", "buff_party", "debuff",  "heal",       "heal_party", "hot",
    "totem",  "mana",       "cleanse",    "pull",       "warp"};
static const char *const k_skill_flags[] = {
    "dash_fwd", "dash_back", "dash_side",       "slow",       "stun",      "taunt",
    "heal_self", "homing",   "move_while_cast", "spell",      "dot",       "party_heal",
    "mana_back", "teleport", "no_combat_req"};
static const char *const k_skill_tags[] = {"single", "aoe",  "multi", "dot",   "utility",
                                           "buff",   "debuff", "heal", "totem"};
static const char *const k_totems[TOTEM_COUNT] = {"none",          "turtle",   "storm_dragon",
                                                   "red_dragon",    "war_drum", "clairvoyance",
                                                   "gale_hawk"};

static void load_tiers(const char *dir) {
    Tsv t;
    if (!data_open_tsv(&t, dir, "tiers.tsv", true)) {
        return;
    }
    if (t.row_count != TIER_COUNT) {
        data_error("tiers.tsv", -1, "erwartet %d Zeilen, gefunden %d", TIER_COUNT, t.row_count);
    }
    for (int r = 0; r < t.row_count && r < TIER_COUNT; r++) {
        TierDef *d = &g_tiers[r];
        str_copy(d->id, sizeof d->id, tsv_get_by_name(&t, r, "id"));
        str_copy(d->name, sizeof d->name, tsv_get_by_name(&t, r, "name"));
        d->color = data_parse_color(tsv_get_by_name(&t, r, "color"), 0x9AA0A6);
        d->drop_one_in = (u32)tsv_get_int(&t, r, "drop_one_in", 10);
        d->item_mult = tsv_get_float(&t, r, "item_mult", 1.0f);
        d->exp_mult = tsv_get_float(&t, r, "exp_mult", 1.0f);
        d->hp_mult = tsv_get_float(&t, r, "hp_mult", 1.0f);
        d->dmg_mult = tsv_get_float(&t, r, "dmg_mult", 1.0f);
        d->spawn_weight = tsv_get_float(&t, r, "spawn_weight", 0.0f);
        d->chest_items = tsv_get_int(&t, r, "chest_items", 3);
        d->monster_rolls = tsv_get_int(&t, r, "monster_rolls", 1);
        d->potion_charges = tsv_get_int(&t, r, "potion_charges", 2);
        d->sell_base = tsv_get_int(&t, r, "sell_base", 5);
        d->sell_per_level = tsv_get_int(&t, r, "sell_per_level", 1);
        d->symbol = tsv_get_int(&t, r, "symbol", r);
        if (d->drop_one_in < 1) {
            data_error("tiers.tsv", r, "drop_one_in muss >= 1 sein");
        }
    }
    tsv_free(&t);
}

static void load_classes(const char *dir) {
    Tsv t;
    if (!data_open_tsv(&t, dir, "classes.tsv", true)) {
        return;
    }
    if (t.row_count != CLASS_COUNT) {
        data_error("classes.tsv", -1, "erwartet %d Zeilen, gefunden %d", CLASS_COUNT, t.row_count);
    }
    for (int r = 0; r < t.row_count && r < CLASS_COUNT; r++) {
        ClassDef *c = &g_classes[r];
        str_copy(c->id, sizeof c->id, tsv_get_by_name(&t, r, "id"));
        if (strcmp(c->id, k_class_ids[r]) != 0) {
            data_error("classes.tsv", r, "Klasse '%s' an Position %d, erwartet '%s'", c->id, r,
                       k_class_ids[r]);
        }
        str_copy(c->name, sizeof c->name, tsv_get_by_name(&t, r, "name"));
        str_copy(c->role, sizeof c->role, tsv_get_by_name(&t, r, "role"));
        str_copy(c->race, sizeof c->race, tsv_get_by_name(&t, r, "race"));
        str_copy(c->desc, sizeof c->desc, tsv_get_by_name(&t, r, "desc"));
        str_copy(c->weapon_name, sizeof c->weapon_name, tsv_get_by_name(&t, r, "weapon_name"));
        str_copy(c->sub_name, sizeof c->sub_name, tsv_get_by_name(&t, r, "sub_name"));
        str_copy(c->basic_attack, sizeof c->basic_attack, tsv_get_by_name(&t, r, "basic_attack"));
        c->color = data_parse_color(tsv_get_by_name(&t, r, "color"), 0x808080);
        c->atk_style = (AttackStyle)data_parse_enum(&t, r, "atk_style", k_atk_styles, 4, 0);
        c->dmg_type = (DamageType)data_parse_enum(&t, r, "dmg_type", k_dmg_types, 3, 0);
        c->ranged = data_parse_bool(tsv_get_by_name(&t, r, "ranged"));
        c->main_stat = data_parse_enum(&t, r, "main_stat", k_main_stats, 3, 0);
        c->base_speed = tsv_get_float(&t, r, "base_speed", 1.0f);
        c->base_range = tsv_get_float(&t, r, "base_range", 90.0f);
        c->proj_speed = tsv_get_float(&t, r, "proj_speed", 0.0f);
        c->move_factor = tsv_get_float(&t, r, "move_factor", 1.0f);
        c->base_armor = tsv_get_float(&t, r, "base_armor", 1.0f);
        c->auto_str = tsv_get_int(&t, r, "auto_str", 34);
        c->auto_dex = tsv_get_int(&t, r, "auto_dex", 33);
        c->auto_int = tsv_get_int(&t, r, "auto_int", 33);
        c->basic_dmg_pct = tsv_get_float(&t, r, "basic_dmg_pct", 100.0f);
    }
    tsv_free(&t);
}

static void load_skills(const char *dir) {
    Tsv t;
    if (!data_open_tsv(&t, dir, "skills.tsv", true)) {
        return;
    }
    for (int r = 0; r < t.row_count; r++) {
        if (g_skill_count >= MAX_SKILLS) {
            data_error("skills.tsv", r, "zu viele Skills (max %d)", MAX_SKILLS);
            break;
        }
        SkillDef *s = &g_skills[g_skill_count];
        memset(s, 0, sizeof *s);
        str_copy(s->id, sizeof s->id, tsv_get_by_name(&t, r, "id"));
        if (!*s->id) {
            data_error("skills.tsv", r, "id fehlt");
            continue;
        }
        if (data_find_skill(s->id) >= 0) {
            data_error("skills.tsv", r, "doppelte id '%s'", s->id);
            continue;
        }
        str_copy(s->name, sizeof s->name, tsv_get_by_name(&t, r, "name"));
        str_copy(s->desc, sizeof s->desc, tsv_get_by_name(&t, r, "desc"));
        s->cls = data_parse_enum(&t, r, "class", k_class_ids, CLASS_COUNT + 1, CLASS_COMMON);
        s->grade = clampi(tsv_get_int(&t, r, "grade", 0), 0, 3);
        s->tags = data_parse_flags(&t, r, "tags", k_skill_tags, 9);
        s->kind = (SkillKind)data_parse_enum(&t, r, "kind", k_skill_kinds, SK_KIND_COUNT, SK_STRIKE);
        s->flags = data_parse_flags(&t, r, "flags", k_skill_flags, 15);
        s->cooldown = tsv_get_float(&t, r, "cooldown", 5.0f);
        s->mana = tsv_get_float(&t, r, "mana", 0.0f);
        s->range = tsv_get_float(&t, r, "range", 0.0f);
        s->radius = tsv_get_float(&t, r, "radius", 0.0f);
        s->cast_time = tsv_get_float(&t, r, "cast_time", 0.3f);
        s->dmg_pct = tsv_get_float(&t, r, "dmg_pct", 0.0f);
        s->count = tsv_get_int(&t, r, "count", 1);
        s->angle = tsv_get_float(&t, r, "angle", 0.0f);
        s->pierce = tsv_get_int(&t, r, "pierce", 0);
        s->max_hits = tsv_get_int(&t, r, "max_hits", 1);
        s->speed = tsv_get_float(&t, r, "speed", 0.0f);
        s->dur = tsv_get_float(&t, r, "dur", 0.0f);
        s->delay = tsv_get_float(&t, r, "delay", 0.0f);
        const char *st = tsv_get_by_name(&t, r, "stat");
        s->stat = -1;
        if (*st) {
            s->stat = data_stat_from_name(st);
            if (s->stat < 0) {
                data_error("skills.tsv", r, "unbekannter Stat '%s'", st);
            }
        }
        s->stat_pct = tsv_get_float(&t, r, "stat_pct", 0.0f);
        const char *st2 = tsv_get_by_name(&t, r, "stat2");
        s->stat2 = -1;
        if (*st2) {
            s->stat2 = data_stat_from_name(st2);
            if (s->stat2 < 0) {
                data_error("skills.tsv", r, "unbekannter Stat '%s'", st2);
            }
        }
        s->stat2_pct = tsv_get_float(&t, r, "stat2_pct", 0.0f);
        s->totem = data_parse_enum(&t, r, "totem", k_totems, TOTEM_COUNT, TOTEM_NONE);
        s->ai.priority = (u8)clampi(tsv_get_int(&t, r, "ai_priority", 4), 0, 8);
        s->ai.tier_mask = (u8)tsv_get_int(&t, r, "ai_tier_mask", 63);
        s->ai.min_targets = (u8)tsv_get_int(&t, r, "ai_min_targets", 0);
        s->ai.max_dist = (u16)tsv_get_int(&t, r, "ai_max_dist", 0);
        s->ai.self_hp_below = (u8)tsv_get_int(&t, r, "ai_self_hp_below", 0);
        s->ai.ally_hp_below = (u8)tsv_get_int(&t, r, "ai_ally_hp_below", 0);
        s->ai.only_in_combat = (u8)tsv_get_int(&t, r, "ai_only_in_combat", 0);
        s->ai.allow_stack = (u8)tsv_get_int(&t, r, "ai_allow_stack", 0);
        if (s->kind == SK_TOTEM && s->totem == TOTEM_NONE) {
            data_error("skills.tsv", r, "Totem-Skill '%s' ohne totem-Typ", s->id);
        }
        g_skill_count++;
    }
    tsv_free(&t);
}

bool data_load_core_tables(const char *dir) {
    load_tiers(dir);
    load_classes(dir);
    load_skills(dir);
    return true;
}
