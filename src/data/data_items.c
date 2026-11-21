/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Lader fuer items.tsv, legendary.tsv und sets.tsv. */
#include <stdlib.h>
#include <string.h>

#include "core/util.h"
#include "data/data_internal.h"

static const char *const k_behaviors[LB_COUNT] = {
    "none",      "explosive_trail", "arcane_emission", "march_of_light",
    "rolling_steel", "bloodrage",   "executioner",     "totem_echo", "chain_lightning"};

/* Slot-Spalte: "ring" -> SLOT_RING1, "potion" -> SLOT_POTION1 */
static EquipSlot slot_from_name(const char *s) {
    static const EquipSlot map[K_SLOT_ID_COUNT] = {
        SLOT_WEAPON, SLOT_SUB,  SLOT_HELM,    SLOT_CHEST,   SLOT_GLOVES, SLOT_BOOTS,
        SLOT_BAG,    SLOT_NECKLACE, SLOT_RING1, SLOT_POTION1, SLOT_WINGS};
    int i = data_enum_index(s, k_slot_ids, K_SLOT_ID_COUNT);
    return i < 0 ? SLOT_NONE : map[i];
}

/* "knight:3;warrior:7" -> legendary_effect[] */
static void parse_legendary_column(const char *text, ItemDef *d, int row, const char *file) {
    char buf[200];
    str_copy(buf, sizeof buf, text);
    char *p = buf;
    while (*p) {
        char *end = strchr(p, ';');
        if (end) {
            *end = '\0';
        }
        str_trim(p);
        char *colon = strchr(p, ':');
        if (*p && colon) {
            *colon = '\0';
            int cls = data_enum_index(p, k_class_ids, CLASS_COUNT);
            int id = atoi(colon + 1);
            if (cls < 0 || id < 1 || id > 28) {
                data_error(file, row, "ungueltiger legendary-Eintrag '%s:%s'", p, colon + 1);
            } else {
                d->legendary_effect[cls] = (u8)id;
            }
        } else if (*p) {
            data_error(file, row, "ungueltiger legendary-Eintrag '%s'", p);
        }
        if (!end) {
            break;
        }
        p = end + 1;
    }
}

static void load_item_rows(const char *dir) {
    Tsv t;
    if (!data_open_tsv(&t, dir, "items.tsv", true)) {
        return;
    }
    for (int r = 0; r < t.row_count; r++) {
        if (g_item_count >= MAX_ITEM_DEFS) {
            data_error("items.tsv", r, "zu viele Items (max %d)", MAX_ITEM_DEFS);
            break;
        }
        ItemDef *d = &g_items[g_item_count];
        memset(d, 0, sizeof *d);
        str_copy(d->id, sizeof d->id, tsv_get_by_name(&t, r, "id"));
        if (!*d->id) {
            data_error("items.tsv", r, "id fehlt");
            continue;
        }
        if (data_find_item(d->id) >= 0) {
            data_error("items.tsv", r, "doppelte id '%s'", d->id);
            continue;
        }
        str_copy(d->name, sizeof d->name, tsv_get_by_name(&t, r, "name"));
        d->kind = (ItemKind)data_parse_enum(&t, r, "kind", k_item_kind_ids, IK_KIND_COUNT, IK_NONE);
        d->tier = (Tier)data_parse_enum(&t, r, "tier", k_tier_ids, TIER_COUNT, TIER_NORMAL);
        d->min_level = tsv_get_int(&t, r, "level", 0);
        const char *slot = tsv_get_by_name(&t, r, "slot");
        d->slot = SLOT_NONE;
        if (*slot) {
            d->slot = slot_from_name(slot);
            if (d->slot == SLOT_NONE) {
                data_error("items.tsv", r, "unbekannter slot '%s'", slot);
            }
        }
        const char *cls = tsv_get_by_name(&t, r, "class");
        d->cls = CLASS_ANY;
        if (*cls && str_icmp(cls, "any") != 0) {
            d->cls = data_enum_index(cls, k_class_ids, CLASS_COUNT);
            if (d->cls < 0) {
                data_error("items.tsv", r, "unbekannte class '%s'", cls);
                d->cls = CLASS_ANY;
            }
        }
        d->value = tsv_get_float(&t, r, "value", 0.0f);
        int unknown = statmods_parse(tsv_get_by_name(&t, r, "mods"), &d->mods);
        if (unknown > 0) {
            data_error("items.tsv", r, "mods '%s': %d unbekannte Stat-Namen", tsv_get_by_name(&t, r, "mods"),
                       unknown);
        }
        d->potion_type = data_parse_enum(&t, r, "potion_type", k_potion_ids, POTION_TYPE_COUNT, 0);
        d->charges = tsv_get_int(&t, r, "charges", 0);
        d->material_line = data_parse_enum(&t, r, "material_line", k_material_ids, MAT_LINE_COUNT, 0);
        d->stone_kind = data_parse_enum(&t, r, "stone_kind", k_stone_ids, K_STONE_ID_COUNT, 0);
        d->family = tsv_get_int(&t, r, "family", 0);
        d->set_id = tsv_get_int(&t, r, "set", 0);
        d->set_index = tsv_get_int(&t, r, "index", 0);
        d->craftable = data_parse_bool(tsv_get_by_name(&t, r, "craftable"));
        d->enchantable = data_parse_bool(tsv_get_by_name(&t, r, "enchantable"));
        parse_legendary_column(tsv_get_by_name(&t, r, "legendary"), d, r, "items.tsv");
        if (d->kind == IK_POTION && d->charges <= 0) {
            d->charges = g_tiers[d->tier].potion_charges;
        }
        if ((d->kind == IK_WEAPON || d->kind == IK_SUB || d->kind == IK_ARMOR || d->kind == IK_RING ||
             d->kind == IK_NECKLACE || d->kind == IK_WINGS || d->kind == IK_BAG || d->kind == IK_POTION) &&
            d->slot == SLOT_NONE) {
            data_error("items.tsv", r, "Item '%s' braucht einen slot", d->id);
        }
        if (d->set_id < 0 || d->set_id > 7) {
            data_error("items.tsv", r, "set-ID %d ausserhalb 0..7", d->set_id);
            d->set_id = 0;
        }
        g_item_count++;
    }
    tsv_free(&t);
}

static void load_legendary(const char *dir) {
    Tsv t;
    if (!data_open_tsv(&t, dir, "legendary.tsv", false)) {
        return;
    }
    for (int r = 0; r < t.row_count; r++) {
        int id = tsv_get_int(&t, r, "id", 0);
        if (id < 1 || id > 28) {
            data_error("legendary.tsv", r, "id %d ausserhalb 1..28", id);
            continue;
        }
        LegendEffect *e = &g_legend[id];
        memset(e, 0, sizeof *e);
        e->id = id;
        str_copy(e->name, sizeof e->name, tsv_get_by_name(&t, r, "name"));
        str_copy(e->desc, sizeof e->desc, tsv_get_by_name(&t, r, "desc"));
        e->cls = data_parse_enum(&t, r, "class", k_class_ids, CLASS_COUNT, 0);
        e->slot = slot_from_name(tsv_get_by_name(&t, r, "slot"));
        if (e->slot != SLOT_HELM && e->slot != SLOT_CHEST && e->slot != SLOT_GLOVES &&
            e->slot != SLOT_BOOTS) {
            data_error("legendary.tsv", r, "slot muss helm/chest/gloves/boots sein");
        }
        statmods_parse(tsv_get_by_name(&t, r, "mods"), &e->mods);
        e->behavior = (LegendBehavior)data_parse_enum(&t, r, "behavior", k_behaviors, LB_COUNT, LB_NONE);
        g_legend_count++;
    }
    tsv_free(&t);
}

static void load_sets(const char *dir) {
    Tsv t;
    if (!data_open_tsv(&t, dir, "sets.tsv", false)) {
        return;
    }
    for (int r = 0; r < t.row_count; r++) {
        int id = tsv_get_int(&t, r, "id", 0);
        if (id < 1 || id > 7) {
            data_error("sets.tsv", r, "id %d ausserhalb 1..7", id);
            continue;
        }
        SetDef *s = &g_sets[id];
        memset(s, 0, sizeof *s);
        s->id = id;
        str_copy(s->name, sizeof s->name, tsv_get_by_name(&t, r, "name"));
        statmods_parse(tsv_get_by_name(&t, r, "mods2"), &s->mods2);
        statmods_parse(tsv_get_by_name(&t, r, "mods4"), &s->mods4);
        s->behavior4 = (LegendBehavior)data_parse_enum(&t, r, "behavior4", k_behaviors, LB_COUNT, LB_NONE);
        str_copy(s->desc2, sizeof s->desc2, tsv_get_by_name(&t, r, "desc2"));
        str_copy(s->desc4, sizeof s->desc4, tsv_get_by_name(&t, r, "desc4"));
        g_set_count++;
    }
    tsv_free(&t);
}

bool data_load_items(const char *dir) {
    load_item_rows(dir);
    load_legendary(dir);
    load_sets(dir);
    /* Querpruefung: Items verweisen auf definierte Legendaer-Effekte/Sets */
    for (int i = 0; i < g_item_count; i++) {
        const ItemDef *d = &g_items[i];
        for (int c = 0; c < CLASS_COUNT; c++) {
            if (d->legendary_effect[c] && g_legend[d->legendary_effect[c]].id == 0 && g_legend_count > 0) {
                data_error("items.tsv", -1, "Item '%s': Legendaer-Effekt %d nicht in legendary.tsv",
                           d->id, d->legendary_effect[c]);
            }
        }
        if (d->set_id > 0 && g_sets[d->set_id].id == 0 && g_set_count > 0) {
            data_error("items.tsv", -1, "Item '%s': Set %d nicht in sets.tsv", d->id, d->set_id);
        }
    }
    return true;
}
