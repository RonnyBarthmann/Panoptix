/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Herstellen, Verbessern und Umwandeln (Spec 12). Verzaubern: enchant.c. */
#include <math.h>
#include <string.h>

#include "core/util.h"
#include "rules/rules_internal.h"

#define CRAFT_MAX_QTY 999

/* ---- Rezepte ------------------------------------------------------------------ */
float craft_cost_factor(const Game *g) {
    CommanderMods cm;
    commander_mods(g, &cm);
    return maxf(1.0f + cm.craft_cost_pct / 100.0f, 0.2f);
}

static void recipe_add(Recipe *r, int def, u32 count) {
    if (def < 0 || count == 0 || r->input_n >= CRAFT_MAX_INPUTS) {
        return;
    }
    r->input_def[r->input_n] = def;
    r->input_count[r->input_n] = count;
    r->input_n++;
}

static float slot_factor(const ItemDef *d) {
    if (d->kind == IK_WEAPON) {
        return 1.5f;
    }
    if (d->kind == IK_SUB) {
        return 0.8f;
    }
    switch (d->slot) {
    case SLOT_CHEST: return 1.5f;
    case SLOT_HELM: return 1.0f;
    default: return 0.7f; /* Handschuhe, Stiefel */
    }
}

/* Boss-/Legendaer-Rezepte brauchen Reliktteile, Sekundaerwaffen einen Bauplan. */
static void add_special_inputs(Recipe *r, const ItemDef *d) {
    if (d->tier >= TIER_BOSS) {
        recipe_add(r, data_find_item("special_relic"), d->tier == TIER_LEGENDARY ? 3u : 1u);
    }
    if (d->kind == IK_SUB) {
        recipe_add(r, data_find_item("special_sub_blueprint"), 1);
    }
}

/* Waffe/Sekundaerwaffe/Ruestung: Metall + Holz|Gewebe + Kristall (+ Magiestein ab Magisch). */
static void fill_gear_recipe(Recipe *r, const ItemDef *d) {
    int lvl = maxi(d->min_level, 0);
    float sf = slot_factor(d);
    u32 metal = (u32)ceilf((float)ceil(2.0 + (double)lvl / 5.0) * sf);
    u32 body = (u32)ceil(1.0 + (double)lvl / 8.0);
    u32 crystal = (u32)ceil((double)lvl / 10.0);
    recipe_add(r, data_find_material(MAT_METAL, d->tier), maxi((int)metal, 1));
    recipe_add(r, data_find_material(d->kind == IK_ARMOR ? MAT_CLOTH : MAT_WOOD, d->tier), body);
    recipe_add(r, data_find_material(MAT_CRYSTAL, d->tier), crystal);
    if (d->tier >= TIER_MAGIC) {
        recipe_add(r, data_find_magic_stone((int)d->tier), (u32)maxi((lvl + 24) / 25, 1));
    }
    add_special_inputs(r, d);
    double mult = (double)g_tiers[d->tier].item_mult;
    r->gold = (i64)ceil(50.0 * pow((double)maxi(lvl, 1), 1.3) * mult);
}

/* Trank: Essenz(T) + Glasphiole + Kraeuter(T+1). */
static void fill_potion_recipe(Recipe *r, const ItemDef *d) {
    recipe_add(r, data_find_material(MAT_ESSENCE, d->tier), 1);
    recipe_add(r, data_find_item("special_glass_vial"), 1);
    recipe_add(r, data_find_item("special_herbs"), (u32)d->tier + 1u);
    r->gold = 100 * (i64)(d->tier + 1) * (d->tier + 1);
}

/* Tragetasche: Gewebe(T) + Holz(T) (+ Magiestein, Relikt). */
static void fill_bag_recipe(Recipe *r, const ItemDef *d) {
    recipe_add(r, data_find_material(MAT_CLOTH, d->tier), 4u + 4u * (u32)d->tier);
    recipe_add(r, data_find_material(MAT_WOOD, d->tier), 2u + 2u * (u32)d->tier);
    if (d->tier >= TIER_MAGIC) {
        recipe_add(r, data_find_magic_stone((int)d->tier), 1);
    }
    add_special_inputs(r, d);
    r->gold = 500 * (i64)(d->tier + 1) * (d->tier + 1);
}

bool craft_recipe(const Game *g, int item_def, Recipe *out) {
    memset(out, 0, sizeof *out);
    out->output = item_def;
    if (item_def < 0 || item_def >= g_item_count || !g_items[item_def].craftable) {
        return false;
    }
    const ItemDef *d = &g_items[item_def];
    switch (d->kind) {
    case IK_WEAPON:
    case IK_SUB:
    case IK_ARMOR:
        fill_gear_recipe(out, d);
        break;
    case IK_POTION:
        fill_potion_recipe(out, d);
        break;
    case IK_BAG:
        fill_bag_recipe(out, d);
        break;
    default:
        return false; /* Ringe, Halsketten, Fluegel nicht craftbar */
    }
    out->gold = (i64)ceil((double)out->gold * (double)craft_cost_factor(g));
    int need_level = (d->kind == IK_POTION || d->kind == IK_BAG) ? 1 : (int)ceil((double)d->min_level * 0.6);
    need_level = maxi(need_level, 1);
    out->unlocked = g->player.level >= need_level;
    if (!out->unlocked) {
        str_format(out->lock_reason, sizeof out->lock_reason, "Kommandeur-Level %d", need_level);
    }
    return out->input_n > 0;
}

bool craft_can_make(const Game *g, const Recipe *r, int qty) {
    if (qty < 1 || qty > CRAFT_MAX_QTY || !r->unlocked || r->input_n <= 0) {
        return false;
    }
    if (g->player.gold < r->gold * qty) {
        return false;
    }
    for (int i = 0; i < r->input_n; i++) {
        if ((i64)g->inv.stock[r->input_def[i]] < (i64)r->input_count[i] * qty) {
            return false;
        }
    }
    return item_is_stackable(r->output) || inv_free_slots(g) >= qty;
}

bool craft_make(Game *g, int item_def, int qty, char *err) {
    Recipe r;
    if (!craft_recipe(g, item_def, &r)) {
        rules_err(err, "Nicht herstellbar");
        return false;
    }
    if (qty < 1 || qty > CRAFT_MAX_QTY) {
        rules_err(err, "Ungültige Menge");
        return false;
    }
    if (!r.unlocked) {
        rules_err(err, "Benötigt %s", r.lock_reason);
        return false;
    }
    if (!craft_can_make(g, &r, qty)) {
        if (!item_is_stackable(r.output) && inv_free_slots(g) < qty) {
            rules_err(err, "Lager voll");
        } else if (g->player.gold < r.gold * qty) {
            rules_err(err, "Nicht genug Gold");
        } else {
            rules_err(err, "Zutaten fehlen");
        }
        return false;
    }
    currency_pay(g, CUR_GOLD, r.gold * qty);
    for (int i = 0; i < r.input_n; i++) {
        inv_take_stock(g, r.input_def[i], r.input_count[i] * (u32)qty);
    }
    for (int i = 0; i < qty; i++) {
        Item it = item_make(item_def);
        inv_add_item(g, &it);
    }
    g->player.counters[COUNTER_CRAFTED] += (u64)qty;
    quests_progress(g, QK_CRAFT, 0, qty);
    return true;
}

/* ---- Verbessern -------------------------------------------------------------- */
float enhance_success_chance(int next_plus) {
    static const float k_pct[ENHANCE_MAX + 1] = {0,  100, 100, 100, 100, 80, 75, 70, 65, 60, 50,
                                                 40, 30,  25,  20,  10,  9,  8,  7,  5,  3};
    if (next_plus < 1 || next_plus > ENHANCE_MAX) {
        return 0.0f;
    }
    return k_pct[next_plus];
}

int enhance_stone_cost(int next_plus) {
    return (int)ceil(1.0 + pow((double)maxi(next_plus, 1), 1.5) / 4.0);
}

i64 enhance_gold_cost(int next_plus, Tier tier) {
    double n = (double)maxi(next_plus, 1);
    double t = (double)tier;
    return (i64)ceil(100.0 * n * n * (1.0 + t * t));
}

int enhance_stone_def(const ItemDef *d) {
    StoneKind kind;
    switch (d->kind) {
    case IK_WEAPON:
    case IK_SUB: kind = STONE_ENH_WEAPON; break;
    case IK_ARMOR: kind = STONE_ENH_ARMOR; break;
    case IK_RING:
    case IK_NECKLACE: kind = STONE_ENH_JEWEL; break;
    default: return -1;
    }
    return data_find_stone(kind, d->tier);
}

EnhanceResult enhance_try(Game *g, Item *it, char *err) {
    if (!it || item_is_empty(it) || it->def >= g_item_count) {
        rules_err(err, "Kein Gegenstand");
        return ENH_BLOCKED;
    }
    const ItemDef *d = &g_items[it->def];
    int stone = enhance_stone_def(d);
    if (stone < 0 || d->min_level < 1) {
        rules_err(err, "Nicht verbesserbar");
        return ENH_BLOCKED;
    }
    if (it->plus >= ENHANCE_MAX) {
        rules_err(err, "Maximale Stufe erreicht");
        return ENH_MAX;
    }
    int next = it->plus + 1;
    int stones = enhance_stone_cost(next);
    i64 gold = enhance_gold_cost(next, d->tier);
    if (g->inv.stock[stone] < (u32)stones) {
        rules_err(err, "Benötigt %d Verbesserungssteine", stones);
        return ENH_BLOCKED;
    }
    if (g->player.gold < gold) {
        rules_err(err, "Nicht genug Gold");
        return ENH_BLOCKED;
    }
    inv_take_stock(g, stone, (u32)stones);
    currency_pay(g, CUR_GOLD, gold);
    g->player.counters[COUNTER_ENHANCES]++;
    quests_progress(g, QK_ENHANCE, 0, 1);
    g->dirty_save = true;
    if (!rng_chance(&g->rng, (double)enhance_success_chance(next) / 100.0)) {
        return ENH_FAIL; /* Steine weg, Item unveraendert */
    }
    it->plus = (i8)next;
    game_mark_all_dirty(g);
    return ENH_OK;
}

/* ---- Umwandeln ----------------------------------------------------------------- */
static u32 convert_ratio(Tier from) {
    return from == TIER_NORMAL ? 20u : 10u;
}

int convert_target_def(int from_def, u32 *need_count) {
    if (from_def < 0 || from_def >= g_item_count) {
        return -1;
    }
    const ItemDef *d = &g_items[from_def];
    if (d->tier >= TIER_LEGENDARY) {
        return -1;
    }
    Tier next = (Tier)(d->tier + 1);
    int target = -1;
    if (d->kind == IK_MATERIAL) {
        target = data_find_material((MaterialLine)d->material_line, next);
    } else if (d->kind == IK_STONE && d->stone_kind != STONE_MAGIC && d->stone_kind != STONE_NONE) {
        target = data_find_stone((StoneKind)d->stone_kind, next);
    }
    if (target >= 0 && need_count) {
        *need_count = convert_ratio(d->tier);
    }
    return target;
}

bool convert_materials(Game *g, int from_def, int times, char *err) {
    u32 need = 0;
    int target = convert_target_def(from_def, &need);
    if (target < 0 || times < 1) {
        rules_err(err, "Nicht umwandelbar");
        return false;
    }
    /* Kosten: ein Magiestein (Akt = Ziel-Tier 1..5) je Umwandlung. */
    int magic = data_find_magic_stone((int)g_items[target].tier);
    u64 total = (u64)need * (u64)times;
    if (total > g_bal.stack_max || g->inv.stock[from_def] < (u32)total) {
        rules_err(err, "Nicht genug Material");
        return false;
    }
    if (magic >= 0 && g->inv.stock[magic] < (u32)times) {
        rules_err(err, "Benötigt Magiesteine");
        return false;
    }
    inv_take_stock(g, from_def, (u32)total);
    if (magic >= 0) {
        inv_take_stock(g, magic, (u32)times);
    }
    inv_add_stock(g, target, (u32)times);
    g->dirty_save = true;
    return true;
}

bool convert_to_jewel_stone(Game *g, Tier tier, bool from_weapon, int times, char *err) {
    int from = data_find_stone(from_weapon ? STONE_ENH_WEAPON : STONE_ENH_ARMOR, tier);
    int to = data_find_stone(STONE_ENH_JEWEL, tier);
    u32 need = from_weapon ? 10u : 20u;
    if (from < 0 || to < 0 || times < 1) {
        rules_err(err, "Nicht umwandelbar");
        return false;
    }
    u64 total = (u64)need * (u64)times;
    if (total > g_bal.stack_max || g->inv.stock[from] < (u32)total) {
        rules_err(err, "Nicht genug Steine");
        return false;
    }
    inv_take_stock(g, from, (u32)total);
    inv_add_stock(g, to, (u32)times);
    g->dirty_save = true;
    return true;
}
