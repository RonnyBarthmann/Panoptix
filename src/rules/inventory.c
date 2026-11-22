/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Lager: Einfuegen, Stapel, Sortieren, Anlegen/Ablegen, Zerlegen, Verkaufen. */
#include <stdlib.h>
#include <string.h>

#include "core/util.h"
#include "rules/rules_internal.h"

/* ---- Stapel --------------------------------------------------------------- */
bool item_is_stackable(int def) {
    if (def < 0 || def >= g_item_count) {
        return false;
    }
    ItemKind k = g_items[def].kind;
    return k == IK_MATERIAL || k == IK_STONE || k == IK_SPECIAL;
}

void inv_add_stock(Game *g, int def, u32 count) {
    if (def < 0 || def >= g_item_count || count == 0) {
        return;
    }
    u64 sum = (u64)g->inv.stock[def] + (u64)count;
    u64 cap = (u64)g_bal.stack_max;
    g->inv.stock[def] = (u32)(sum > cap ? cap : sum);
}

bool inv_take_stock(Game *g, int def, u32 count) {
    if (def < 0 || def >= g_item_count || g->inv.stock[def] < count) {
        return false;
    }
    g->inv.stock[def] -= count;
    return true;
}

/* ---- Nicht stapelbare Items ---------------------------------------------- */
int inv_free_slots(const Game *g) {
    return maxi(0, g->inv.capacity - g->inv.item_count);
}

bool inv_add_item(Game *g, const Item *it) {
    if (!it || item_is_empty(it)) {
        return false;
    }
    if (item_is_stackable(it->def)) {
        inv_add_stock(g, it->def, 1);
        return true;
    }
    if (g->inv.item_count >= g->inv.capacity || g->inv.item_count >= INV_MAX_ITEMS) {
        return false;
    }
    g->inv.items[g->inv.item_count++] = *it;
    g->dirty_save = true;
    return true;
}

void inv_remove_at(Game *g, int index) {
    Inventory *inv = &g->inv;
    if (index < 0 || index >= inv->item_count) {
        return;
    }
    memmove(&inv->items[index], &inv->items[index + 1], (size_t)(inv->item_count - index - 1) * sizeof(Item));
    inv->item_count--;
    memset(&inv->items[inv->item_count], 0, sizeof(Item));
    inv->items[inv->item_count].def = ITEM_NONE;
}

static int cmp_items(const void *pa, const void *pb) {
    const Item *a = (const Item *)pa, *b = (const Item *)pb;
    const ItemDef *da = &g_items[a->def], *db = &g_items[b->def];
    if (da->tier != db->tier) {
        return (int)db->tier - (int)da->tier;
    }
    if (da->min_level != db->min_level) {
        return db->min_level - da->min_level;
    }
    int c = strcmp(da->name, db->name);
    if (c != 0) {
        return c;
    }
    return (int)b->plus - (int)a->plus;
}

void inv_sort(Game *g) {
    if (g->inv.item_count > 1) {
        qsort(g->inv.items, (size_t)g->inv.item_count, sizeof(Item), cmp_items);
    }
}

/* ---- Anlegen -------------------------------------------------------------- */
bool hero_can_use(const Hero *h, const ItemDef *d, char *err) {
    if (!d || !item_kind_is_equipment(d->kind)) {
        if (d && d->kind == IK_POTION) {
            return true;
        }
        rules_err(err, "Nicht ausrüstbar");
        return false;
    }
    if ((d->kind == IK_WEAPON || d->kind == IK_SUB) && d->cls != CLASS_ANY && d->cls != h->cls) {
        rules_err(err, "Falsche Klasse");
        return false;
    }
    if (h->level < d->min_level) {
        rules_err(err, "Benötigt Level %d", d->min_level);
        return false;
    }
    return true;
}

/* Bestimmt den Ziel-Slot; Ringe/Traenke: erster freier, sonst der erste. */
static bool resolve_slot(const Hero *h, const ItemDef *d, EquipSlot want, EquipSlot *out) {
    int first, last;
    if (d->kind == IK_RING) {
        first = SLOT_RING1;
        last = SLOT_RING2;
    } else if (d->kind == IK_POTION) {
        first = SLOT_POTION1;
        last = SLOT_POTION3;
    } else {
        if (want != SLOT_NONE && want != d->slot) {
            return false;
        }
        *out = d->slot;
        return d->slot != SLOT_NONE;
    }
    if ((int)want >= first && (int)want <= last) {
        *out = want;
        return true;
    }
    if (want != SLOT_NONE) {
        return false;
    }
    for (int s = first; s <= last; s++) {
        if (item_is_empty(&h->eq[s])) {
            *out = (EquipSlot)s;
            return true;
        }
    }
    *out = (EquipSlot)first;
    return true;
}

bool hero_equip(Game *g, int hero_idx, int inv_index, EquipSlot slot, char *err) {
    if (hero_idx < 0 || hero_idx >= MAX_HEROES || !g->heroes[hero_idx].used) {
        rules_err(err, "Kein Held");
        return false;
    }
    if (inv_index < 0 || inv_index >= g->inv.item_count) {
        rules_err(err, "Kein Gegenstand");
        return false;
    }
    Hero *h = &g->heroes[hero_idx];
    Item it = g->inv.items[inv_index];
    const ItemDef *d = &g_items[it.def];
    if (!hero_can_use(h, d, err)) {
        return false;
    }
    EquipSlot target;
    if (!resolve_slot(h, d, slot, &target)) {
        rules_err(err, "Falscher Slot");
        return false;
    }
    Item old = h->eq[target];
    inv_remove_at(g, inv_index);
    h->eq[target] = it;
    if (!item_is_empty(&old)) {
        inv_add_item(g, &old); /* Platz ist garantiert: wir haben gerade einen Slot freigemacht */
    }
    h->stats_dirty = true;
    g->dirty_save = true;
    return true;
}

bool hero_unequip(Game *g, int hero_idx, EquipSlot slot, char *err) {
    if (hero_idx < 0 || hero_idx >= MAX_HEROES || !g->heroes[hero_idx].used || slot < 0 || slot >= SLOT_COUNT) {
        rules_err(err, "Ungültig");
        return false;
    }
    Hero *h = &g->heroes[hero_idx];
    if (item_is_empty(&h->eq[slot])) {
        rules_err(err, "Slot ist leer");
        return false;
    }
    if (!inv_add_item(g, &h->eq[slot])) {
        rules_err(err, "Lager voll");
        return false;
    }
    h->eq[slot] = item_empty();
    h->stats_dirty = true;
    return true;
}

/* ---- Verkaufen / Zerlegen ------------------------------------------------- */
i64 item_sell_value(const Item *it) {
    if (!it || item_is_empty(it) || it->def >= g_item_count) {
        return 0;
    }
    const ItemDef *d = &g_items[it->def];
    const TierDef *t = &g_tiers[d->tier];
    i64 level = maxi(d->min_level, 1);
    return (i64)t->sell_base + (i64)t->sell_per_level * level;
}

bool item_sell_at(Game *g, int inv_index, char *err) {
    if (inv_index < 0 || inv_index >= g->inv.item_count) {
        rules_err(err, "Kein Gegenstand");
        return false;
    }
    const Item *it = &g->inv.items[inv_index];
    if (it->flags & ITEMF_LOCKED) {
        rules_err(err, "Gegenstand ist gesperrt");
        return false;
    }
    currency_add(g, CUR_GOLD, item_sell_value(it));
    inv_remove_at(g, inv_index);
    return true;
}

typedef struct Yield {
    int def[3];
    u32 count[3];
    int n;
} Yield;

static void yield_add(Yield *y, int def, u32 count) {
    if (def >= 0 && count > 0 && y->n < 3) {
        y->def[y->n] = def;
        y->count[y->n] = count;
        y->n++;
    }
}

/* Zerlege-Ertrag: Metall + Verbesserungsstein (Waffe/Ruestung), Astralteile 50 Steine. */
static Yield dismantle_yield(const Item *it) {
    Yield y;
    memset(&y, 0, sizeof y);
    const ItemDef *d = &g_items[it->def];
    u32 lvl = (u32)maxi(d->min_level, 0);
    switch (d->kind) {
    case IK_WEAPON:
    case IK_SUB:
        yield_add(&y, data_find_material(MAT_METAL, d->tier), 1 + lvl / 25);
        yield_add(&y, data_find_stone(STONE_ENH_WEAPON, d->tier), 1 + lvl / 20);
        break;
    case IK_ARMOR:
        yield_add(&y, data_find_material(MAT_CLOTH, d->tier), 1 + lvl / 25);
        yield_add(&y, data_find_stone(STONE_ENH_ARMOR, d->tier), d->tier == TIER_LEGENDARY ? 50u : 1 + lvl / 20);
        break;
    case IK_RING:
        yield_add(&y, data_find_stone(STONE_ENH_JEWEL, d->tier), 1);
        break;
    case IK_NECKLACE:
        yield_add(&y, data_find_stone(STONE_ENH_JEWEL, d->tier), 2);
        break;
    case IK_BAG:
        yield_add(&y, data_find_material(MAT_CLOTH, d->tier), 2);
        break;
    case IK_POTION:
        yield_add(&y, data_find_material(MAT_ESSENCE, d->tier), 1);
        break;
    default:
        break;
    }
    return y;
}

bool item_dismantle_at(Game *g, int inv_index, char *err) {
    if (inv_index < 0 || inv_index >= g->inv.item_count) {
        rules_err(err, "Kein Gegenstand");
        return false;
    }
    const Item it = g->inv.items[inv_index];
    if (it.flags & ITEMF_LOCKED) {
        rules_err(err, "Gegenstand ist gesperrt");
        return false;
    }
    Yield y = dismantle_yield(&it);
    for (int i = 0; i < y.n; i++) {
        inv_add_stock(g, y.def[i], y.count[i]);
    }
    currency_add(g, CUR_GOLD, item_sell_value(&it) / 10);
    inv_remove_at(g, inv_index);
    g->player.counters[COUNTER_DISMANTLED]++;
    return true;
}

int inv_dismantle_filtered(Game *g, int below_tier, int below_level) {
    int done = 0;
    if (below_tier <= 0) {
        return 0;
    }
    for (int i = g->inv.item_count - 1; i >= 0; i--) {
        const Item *it = &g->inv.items[i];
        const ItemDef *d = &g_items[it->def];
        if ((it->flags & ITEMF_LOCKED) || it->plus > 0 || it->ench_opt != ENCH_NONE) {
            continue;
        }
        if (d->kind != IK_WEAPON && d->kind != IK_SUB && d->kind != IK_ARMOR && d->kind != IK_RING &&
            d->kind != IK_NECKLACE) {
            continue;
        }
        if ((int)d->tier >= below_tier || (below_level > 0 && d->min_level >= below_level)) {
            continue;
        }
        if (item_dismantle_at(g, i, NULL)) {
            done++;
        }
    }
    return done;
}

void item_toggle_lock(Item *it) {
    it->flags ^= ITEMF_LOCKED;
}

char *item_display_name(const Item *it, char *buf, size_t size) {
    if (!it || item_is_empty(it) || it->def >= g_item_count) {
        str_copy(buf, size, "");
        return buf;
    }
    const ItemDef *d = &g_items[it->def];
    if (it->plus > 0) {
        str_format(buf, size, "%s +%d", d->name, (int)it->plus);
    } else {
        str_copy(buf, size, d->name);
    }
    return buf;
}
