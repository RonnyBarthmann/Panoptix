/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Globale Tabellen, Lookup-Funktionen, StatMods und data_load_all(). */
#include "data/data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/util.h"
#include "data/data_internal.h"

Balance g_bal;
TierDef g_tiers[TIER_COUNT];
ClassDef g_classes[CLASS_COUNT];
SkillDef g_skills[MAX_SKILLS];
int g_skill_count;
ItemDef g_items[MAX_ITEM_DEFS];
int g_item_count;
MonsterDef g_monsters[MAX_MONSTERS];
int g_monster_count;
MapDef g_maps[MAX_MAPS];
int g_map_count;
ShopOffer g_shop[MAX_SHOP_OFFERS];
int g_shop_count;
TraitDef g_traits[MAX_TRAITS];
int g_trait_count;
QuestDef g_quests[MAX_QUEST_DEFS];
int g_quest_count;
LegendEffect g_legend[32];
int g_legend_count;
SetDef g_sets[8];
int g_set_count;

static char s_data_dir[512];

const char *data_dir(void) {
    return s_data_dir;
}

/* ---- Lookups ------------------------------------------------------------ */
int data_find_item(const char *id) {
    for (int i = 0; i < g_item_count; i++) {
        if (strcmp(g_items[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

int data_find_skill(const char *id) {
    for (int i = 0; i < g_skill_count; i++) {
        if (strcmp(g_skills[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

int data_find_monster(const char *id) {
    for (int i = 0; i < g_monster_count; i++) {
        if (strcmp(g_monsters[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

int data_find_map(const char *id) {
    for (int i = 0; i < g_map_count; i++) {
        if (strcmp(g_maps[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

int data_stat_from_name(const char *name) {
    for (int i = 0; i < STAT_COUNT; i++) {
        if (strcmp(g_stat_names[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

int data_query_items(ItemKind kind, int tier, EquipSlot slot, int cls, int *out, int cap) {
    int n = 0;
    for (int i = 0; i < g_item_count && n < cap; i++) {
        const ItemDef *d = &g_items[i];
        if (kind != IK_NONE && d->kind != kind) {
            continue;
        }
        if (tier >= 0 && (int)d->tier != tier) {
            continue;
        }
        if (slot != SLOT_NONE && d->slot != slot) {
            continue;
        }
        if (cls != -2 && d->cls != CLASS_ANY && d->cls != cls) {
            continue;
        }
        out[n++] = i;
    }
    return n;
}

int data_find_material(MaterialLine line, Tier tier) {
    for (int i = 0; i < g_item_count; i++) {
        const ItemDef *d = &g_items[i];
        if (d->kind == IK_MATERIAL && d->material_line == (int)line && d->tier == tier) {
            return i;
        }
    }
    return -1;
}

int data_find_stone(StoneKind kind, Tier tier) {
    for (int i = 0; i < g_item_count; i++) {
        const ItemDef *d = &g_items[i];
        if (d->kind == IK_STONE && d->stone_kind == (int)kind && d->tier == tier) {
            return i;
        }
    }
    return -1;
}

int data_find_magic_stone(int act) {
    for (int i = 0; i < g_item_count; i++) {
        const ItemDef *d = &g_items[i];
        if (d->kind == IK_STONE && d->stone_kind == STONE_MAGIC && (int)(d->value + 0.5f) == act) {
            return i;
        }
    }
    return -1;
}

/* cls 0..6: Klassen-Skills; CLASS_COMMON: nur Allgemein; CLASS_ANY (-1): beide. */
int data_query_skills(int cls, int *out, int cap) {
    int n = 0;
    for (int i = 0; i < g_skill_count && n < cap; i++) {
        int c = g_skills[i].cls;
        if (cls == CLASS_ANY || c == cls) {
            out[n++] = i;
        }
    }
    return n;
}

/* ---- StatMods ------------------------------------------------------------ */
int statmods_parse(const char *text, StatMods *mods) {
    int unknown = 0;
    if (!text) {
        return 0;
    }
    char buf[512];
    str_copy(buf, sizeof buf, text);
    char *p = buf;
    while (*p) {
        char *end = strchr(p, ';');
        if (end) {
            *end = '\0';
        }
        str_trim(p);
        if (*p) {
            char *eq = strchr(p, '=');
            if (!eq) {
                unknown++;
            } else {
                *eq = '\0';
                str_trim(p);
                int stat = data_stat_from_name(p);
                if (stat < 0) {
                    unknown++;
                } else {
                    mods->v[stat] += (float)atof(eq + 1);
                }
            }
        }
        if (!end) {
            break;
        }
        p = end + 1;
    }
    return unknown;
}

void statmods_add(StatMods *dst, const StatMods *src) {
    for (int i = 0; i < STAT_COUNT; i++) {
        dst->v[i] += src->v[i];
    }
}

void statmods_scale(StatMods *m, float f) {
    for (int i = 0; i < STAT_COUNT; i++) {
        m->v[i] *= f;
    }
}

/* ---- Laden ---------------------------------------------------------------- */
bool data_load_all(const char *dir) {
    data_error_reset();
    memset(&g_tiers, 0, sizeof g_tiers);
    memset(&g_classes, 0, sizeof g_classes);
    g_skill_count = g_item_count = g_monster_count = g_map_count = 0;
    g_shop_count = g_trait_count = g_quest_count = 0;
    g_legend_count = g_set_count = 0;
    memset(g_legend, 0, sizeof g_legend);
    memset(g_sets, 0, sizeof g_sets);
    str_copy(s_data_dir, sizeof s_data_dir, dir);

    data_load_balance(dir);
    data_load_core_tables(dir);
    data_load_items(dir);
    data_load_world(dir);
    data_load_meta(dir);

    int errors = data_error_count();
    if (errors > 0) {
        log_error("Daten: %d Fehler beim Laden aus '%s'", errors, dir);
        return false;
    }
    return true;
}
