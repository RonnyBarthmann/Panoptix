/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Kisten (Spec 11): hierarchische Wuerfe beim Kill, Kistenoeffnung, Item-Auswahl. */
#include <math.h>
#include <string.h>

#include "core/util.h"
#include "rules/rules_internal.h"

#define CHEST_LIST_MAX 16
#define CHEST_ITEM_LEVEL_SIGMA 8.0f

/* ---- Kistenkapazitaet und Lagergrenzen ---------------------------------------- */
int hero_chest_capacity(const Game *g, const Hero *h) {
    int cap = (int)g_bal.chest_base_capacity;
    static const EquipSlot k_slots[2] = {SLOT_CHEST, SLOT_BAG};
    for (int i = 0; i < 2; i++) {
        const Item *it = &h->eq[k_slots[i]];
        if (item_is_empty(it)) {
            continue;
        }
        StatMods m;
        memset(&m, 0, sizeof m);
        item_add_mods(it, &m);
        cap += (int)minf(m.v[STAT_CHEST_CAP], 30.0f);
    }
    cap = mini(cap, (int)g_bal.chest_max_capacity);
    CommanderMods cm;
    commander_mods(g, &cm);
    return maxi(cap + cm.chest_cap_flat, 1);
}

u32 inv_chest_storage_limit(const Game *g, Tier tier) {
    if (tier != TIER_NORMAL && tier != TIER_MAGIC) {
        return (u32)g_bal.stack_max;
    }
    CommanderMods cm;
    commander_mods(g, &cm);
    return (u32)mini(100 + cm.storage_limit_bonus, 1000); /* 100 -> 500 -> 1000 */
}

int loot_default_level(const Game *g) {
    int best = 1;
    for (int i = 0; i < MAX_HEROES; i++) {
        const Hero *h = &g->heroes[i];
        if (!h->used) {
            continue;
        }
        int lv = h->level;
        if (h->assigned_map >= 0 && h->assigned_map < g_map_count) {
            lv = g_maps[h->assigned_map].level;
        }
        best = maxi(best, lv);
    }
    return best;
}

/* ---- Kill-Wuerfe -------------------------------------------------------------- */
/* Anzahl Wuerfe: Monster-Tier-Rollenzahl * (1 + DropMenge), Bruchteil stochastisch. */
static int roll_count(Game *g, Tier monster_tier, const CommanderMods *cm) {
    double expect = (double)g_tiers[monster_tier].monster_rolls * (1.0 + (double)cm->drop_qty_pct / 100.0);
    int n = (int)expect;
    if (rng_chance(&g->rng, expect - (double)n)) {
        n++;
    }
    return n;
}

/* Ein Wurf von HOCH nach NIEDRIG; erster Treffer gewinnt. -1 = keine Kiste. */
static int roll_one_chest(Game *g, const CommanderMods *cm) {
    double quality = 1.0 + (double)cm->drop_quality_pct / 100.0;
    for (int t = TIER_LEGENDARY; t >= TIER_NORMAL; t--) {
        double p = 1.0 / (double)maxi((int)g_tiers[t].drop_one_in, 1);
        if (t >= TIER_MAGIC) {
            p *= quality;
        }
        if (rng_chance(&g->rng, p)) {
            return t;
        }
    }
    return -1;
}

static void add_chest(KillLoot *out, int cap, Tier tier) {
    if (out->chest_count < cap) {
        out->chest_tier[out->chest_count++] = (u8)tier;
    }
}

void loot_roll_kill(Game *g, Tier monster_tier, int map_level, KillLoot *out) {
    (void)map_level; /* Kisteninhalt wird erst beim Oeffnen vom Level abhaengig */
    memset(out, 0, sizeof *out);
    CommanderMods cm;
    commander_mods(g, &cm);
    int cap = mini((int)g_bal.chest_roll_cap, CHEST_LIST_MAX);
    int rolls = roll_count(g, monster_tier, &cm);
    for (int i = 0; i < rolls && out->chest_count < cap; i++) {
        int t = roll_one_chest(g, &cm);
        if (t < 0) {
            continue;
        }
        add_chest(out, cap, (Tier)t);
        if (rng_chance(&g->rng, (double)cm.dupe_chest_pct / 100.0)) {
            add_chest(out, cap, (Tier)t);
        }
        if (t == TIER_UNIQUE && rng_chance(&g->rng, (double)cm.unique_double_pct / 100.0)) {
            add_chest(out, cap, (Tier)t);
        }
    }
}

/* ---- Item-Auswahl --------------------------------------------------------------- */
int loot_pick_item_def(Game *g, ItemKind kind, Tier tier, int target_level, EquipSlot slot, int cls) {
    int list[MAX_ITEM_DEFS];
    int n = data_query_items(kind, (int)tier, slot, cls, list, MAX_ITEM_DEFS);
    if (n <= 0) {
        return -1;
    }
    float w[MAX_ITEM_DEFS];
    for (int i = 0; i < n; i++) {
        float dl = (float)(g_items[list[i]].min_level - target_level) / CHEST_ITEM_LEVEL_SIGMA;
        w[i] = expf(-0.5f * dl * dl) + 0.01f; /* auf Map-Level gewichtet, nie ganz ausgeschlossen */
    }
    int k = rng_weighted(&g->rng, w, n);
    return k < 0 ? -1 : list[k];
}

int loot_pick_skill_book(Game *g, int grade, int cls) {
    for (int gr = clampi(grade, 0, 3); gr >= 0; gr--) {
        int list[MAX_SKILLS];
        int n = 0;
        for (int i = 0; i < g_skill_count && n < MAX_SKILLS; i++) {
            const SkillDef *s = &g_skills[i];
            if (s->grade != gr) {
                continue;
            }
            if (cls >= 0 && s->cls != cls) {
                continue;
            }
            list[n++] = i;
        }
        if (n > 0) {
            return list[rng_range(&g->rng, 0, n - 1)];
        }
    }
    return -1;
}

/* ---- Kisteninhalt --------------------------------------------------------------- */
static void result_add_stack(ChestResult *r, int def, u32 count) {
    for (int i = 0; i < r->stack_count; i++) {
        if (r->stacks[i].def == def) {
            r->stacks[i].count += count;
            return;
        }
    }
    if (r->stack_count < CHEST_LIST_MAX) {
        r->stacks[r->stack_count].def = def;
        r->stacks[r->stack_count].count = count;
        r->stack_count++;
    }
}

static void give_stack(Game *g, ChestResult *r, int def, u32 count) {
    if (def < 0) {
        return;
    }
    inv_add_stock(g, def, count);
    result_add_stack(r, def, count);
}

static void give_item(Game *g, ChestResult *r, int def) {
    if (def < 0) {
        return;
    }
    Item it = item_make(def);
    if (g_items[def].kind == IK_POTION) {
        it.fill = 100;
    }
    if (!inv_add_item(g, &it)) {
        r->lost_items++;
        return;
    }
    if (r->item_count < CHEST_LIST_MAX) {
        r->items[r->item_count++] = it;
    }
}

static void give_book(Game *g, ChestResult *r, Tier tier) {
    int grade = clampi((int)tier, 0, 3);
    if (grade > 0 && rng_chance(&g->rng, 0.5)) {
        grade--;
    }
    int sk = loot_pick_skill_book(g, grade, -1);
    if (sk < 0) {
        return;
    }
    skill_add_book(g, sk, 1);
    if (r->book_count < 4) {
        r->skill_books[r->book_count++] = sk;
    }
}

/* Material oder Verbesserungsstein des Kisten-Tiers. */
static void give_material(Game *g, ChestResult *r, Tier tier, int level) {
    u32 count = (u32)rng_range(&g->rng, 1, 3) + (u32)(level / 50);
    if (rng_chance(&g->rng, 0.30)) {
        static const StoneKind k_kinds[3] = {STONE_ENH_WEAPON, STONE_ENH_ARMOR, STONE_ENH_JEWEL};
        give_stack(g, r, data_find_stone(k_kinds[rng_range(&g->rng, 0, 2)], tier), 1 + count / 2);
    } else {
        give_stack(g, r, data_find_material((MaterialLine)rng_range(&g->rng, 0, MAT_LINE_COUNT - 1), tier), count);
    }
}

static int pick_equipment(Game *g, ItemKind kind, Tier tier, int level) {
    EquipSlot slot = SLOT_NONE;
    int cls = -2;
    if (kind == IK_WEAPON) {
        slot = SLOT_WEAPON;
        cls = rng_range(&g->rng, 0, CLASS_COUNT - 1);
    } else if (kind == IK_ARMOR) {
        static const EquipSlot k_slots[4] = {SLOT_HELM, SLOT_CHEST, SLOT_GLOVES, SLOT_BOOTS};
        slot = k_slots[rng_range(&g->rng, 0, 3)];
    }
    return loot_pick_item_def(g, kind, tier, level, slot, cls);
}

typedef struct DropEntry {
    ItemKind kind;
    double pct;
} DropEntry;

/* Wuerfelt eine Kategorie nach den Prozentwerten; IK_NONE = Material, IK_SPECIAL = Skillbuch. */
static ItemKind roll_category(Game *g, bool equipment_only) {
    const DropEntry table[8] = {{IK_POTION, g_bal.chest_item_pct_potion},     {IK_WEAPON, g_bal.chest_item_pct_weapon},
                                {IK_ARMOR, g_bal.chest_item_pct_armor},       {IK_RING, g_bal.chest_item_pct_ring},
                                {IK_NECKLACE, g_bal.chest_item_pct_necklace}, {IK_WINGS, g_bal.chest_item_pct_wings},
                                {IK_BAG, g_bal.chest_item_pct_bag},           {IK_SPECIAL, g_bal.chest_item_pct_book}};
    double total = 0.0;
    for (int i = 0; i < 8; i++) {
        if (!equipment_only || (table[i].kind != IK_POTION && table[i].kind != IK_SPECIAL)) {
            total += table[i].pct;
        }
    }
    double r = (double)rng_float(&g->rng) * (equipment_only ? total : 100.0);
    for (int i = 0; i < 8; i++) {
        if (equipment_only && (table[i].kind == IK_POTION || table[i].kind == IK_SPECIAL)) {
            continue;
        }
        if (r < table[i].pct) {
            return table[i].kind;
        }
        r -= table[i].pct;
    }
    return equipment_only ? IK_WEAPON : IK_NONE;
}

/* Ein Item-Wurf der Kiste. Legendaer: nur Ausruestung (+ Legendaere Steine). */
static void roll_chest_item(Game *g, Tier tier, int level, ChestResult *r) {
    bool legendary = tier == TIER_LEGENDARY;
    if (legendary && rng_chance(&g->rng, 0.20)) {
        static const StoneKind k_kinds[3] = {STONE_ENH_WEAPON, STONE_ENH_ARMOR, STONE_ENH_JEWEL};
        give_stack(g, r, data_find_stone(k_kinds[rng_range(&g->rng, 0, 2)], tier), (u32)rng_range(&g->rng, 1, 3));
        return;
    }
    ItemKind k = roll_category(g, legendary);
    switch (k) {
    case IK_NONE:
        give_material(g, r, tier, level);
        break;
    case IK_SPECIAL:
        give_book(g, r, tier);
        break;
    default:
        give_item(g, r, pick_equipment(g, k, tier, level));
        break;
    }
}

void chest_open_one(Game *g, Tier tier, int map_level, ChestResult *out) {
    memset(out, 0, sizeof *out);
    out->tier = tier;
    if ((int)tier < 0 || tier >= TIER_COUNT) {
        return;
    }
    int n = g_tiers[tier].chest_items;
    for (int i = 0; i < n; i++) {
        roll_chest_item(g, tier, map_level, out);
    }
    g->player.counters[COUNTER_CHESTS_OPENED]++;
    quests_progress(g, QK_OPEN_CHESTS, 0, 1);
    g->dirty_save = true;
}

static void merge_result(ChestResult *dst, const ChestResult *src) {
    for (int i = 0; i < src->item_count && dst->item_count < CHEST_LIST_MAX; i++) {
        dst->items[dst->item_count++] = src->items[i];
    }
    for (int i = 0; i < src->stack_count; i++) {
        result_add_stack(dst, src->stacks[i].def, src->stacks[i].count);
    }
    for (int i = 0; i < src->book_count && dst->book_count < 4; i++) {
        dst->skill_books[dst->book_count++] = src->skill_books[i];
    }
    dst->lost_items += src->lost_items;
}

int chest_open_from_storage(Game *g, Tier tier, int count, ChestResult *out) {
    memset(out, 0, sizeof *out);
    out->tier = tier;
    if ((int)tier < 0 || tier >= TIER_COUNT || count <= 0) {
        return 0;
    }
    int n = (int)mini(count, (int)mini((int)g->inv.chests[tier], 1 << 20));
    int level = loot_default_level(g);
    for (int i = 0; i < n; i++) {
        ChestResult one;
        chest_open_one(g, tier, level, &one);
        g->inv.chests[tier]--;
        merge_result(out, &one);
    }
    return n;
}
