/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Namenstabellen: TSV-Bezeichner und Anzeigenamen der Enums. */
#include "data/data.h"
#include "data/data_internal.h"

const char *const k_tier_ids[TIER_COUNT] = {"normal", "magic", "rare", "unique", "boss", "legendary"};
const char *const k_class_ids[CLASS_COUNT + 1] = {"knight", "warrior", "wizard", "archer",
                                                  "assassin", "shaman", "priest", "common"};
const char *const k_item_kind_ids[IK_KIND_COUNT] = {"none",   "weapon", "sub",      "armor",
                                                    "ring",   "necklace", "wings",  "bag",
                                                    "potion", "material", "stone",  "special"};
const char *const k_slot_ids[K_SLOT_ID_COUNT] = {"weapon", "sub",      "helm",   "chest",
                                                 "gloves", "boots",    "bag",    "necklace",
                                                 "ring",   "potion",   "wings"};
const char *const k_potion_ids[POTION_TYPE_COUNT] = {"heal", "mana", "haste", "iron", "frenzy"};
const char *const k_material_ids[MAT_LINE_COUNT] = {"metal", "wood", "cloth", "crystal", "essence"};
const char *const k_stone_ids[K_STONE_ID_COUNT] = {"none",       "enh_weapon", "enh_armor",
                                                   "enh_jewel",  "enchant",    "magic"};

/* TSV-Namen der Stats (Reihenfolge = enum Stat in core/types.h) */
const char *const g_stat_names[STAT_COUNT] = {
    "dmg_flat", "attack_flat", "spell_flat", "attack_pct", "spell_pct", "more_attack",
    "more_spell", "crit", "crit_dmg", "atk_speed", "cast_speed", "move_speed", "cdr", "armor",
    "armor_pct", "spellres_pct", "hp", "hp_pct", "mana", "mana_pct", "hp_regen", "mana_regen",
    "chest_cap", "dr_attack", "dr_spell", "dmg_reduce", "projectiles", "pierce", "heal_pct",
    "enemy_taken", "enemy_atk_down", "enemy_spell_down", "mana_cost", "cdr_cap"};

const char *const g_stat_labels[STAT_COUNT] = {
    "Angriff & Zauber (flach)", "Angriffsschaden (flach)", "Zauberschaden (flach)",
    "Angriffsschaden", "Zauberschaden", "Mehr Angriffsschaden", "Mehr Zauberschaden",
    "Krit-Chance", "Krit-Schaden", "Angriffstempo", "Zaubertempo", "Lauftempo",
    "Abklingzeit-Red.", "Rüstung", "Rüstung", "Zauberwiderstand", "Max-Leben", "Max-Leben",
    "Max-Mana", "Max-Mana", "Lebensregeneration", "Manaregeneration", "Kistenkapazität",
    "Angriffs-Abzug", "Zauber-Abzug", "Schadensminderung", "Projektile", "Durchdringung",
    "Heilkraft", "Gegner erleiden mehr", "Gegner: weniger Angriff", "Gegner: weniger Zauber",
    "Manakosten", "Max. CDR"};

const char *const g_equip_slot_names[SLOT_COUNT] = {
    "Waffe",    "Sekundärwaffe", "Helm",  "Brustpanzer", "Handschuhe", "Stiefel", "Tragetasche",
    "Halskette", "Ring 1",       "Ring 2", "Trank 1",      "Trank 2",    "Trank 3", "Flügel"};

const char *const g_item_kind_names[IK_KIND_COUNT] = {
    "-", "Waffe", "Sekundärwaffe", "Rüstung", "Ring", "Halskette", "Flügel",
    "Tragetasche", "Trank", "Material", "Stein", "Spezial"};

const char *const g_potion_type_names[POTION_TYPE_COUNT] = {"Heiltrank", "Manatrank", "Eile",
                                                            "Eisenhaut", "Raserei"};

const char *const g_currency_names[CUR_COUNT] = {"Gold", "Diamanten", "Kommando-Credits",
                                                 "Raben-Münzen", "Rabenfedern"};
