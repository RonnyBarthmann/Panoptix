/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Lader fuer shop.tsv, traits.tsv und quests.tsv. */
#include <string.h>

#include "core/util.h"
#include "data/data_internal.h"

static const char *const k_shop_tabs[SHOP_TAB_COUNT] = {"free", "diamonds", "random",
                                                        "general", "books", "raven"};
static const char *const k_shop_kinds[] = {"item",       "random_ring", "random_necklace",
                                           "random_book", "book_class", "slots_inv",
                                           "slots_hero", "currency",    "wings", "chest"};
#define SHOP_KIND_COUNT 10
static const char *const k_currencies[CUR_COUNT] = {"gold", "diamonds", "credits", "raven_coins",
                                                    "raven_feathers"};
static const char *const k_periods[] = {"none", "daily", "weekly", "monthly"};
static const char *const k_quest_kinds[] = {"kill_any", "kill_tier", "clear_map", "craft",
                                            "open_chests", "kill_arch", "enhance", "reach_level"};
static const char *const k_archetypes[ARCH_COUNT] = {
    "melee", "ranged", "swarm", "brute",  "hunter",   "summoner", "healer",
    "explosive", "elemental", "guardian", "totem", "demon", "boss"};

static void load_shop(const char *dir) {
    Tsv t;
    if (!data_open_tsv(&t, dir, "shop.tsv", false)) {
        return;
    }
    for (int r = 0; r < t.row_count; r++) {
        if (g_shop_count >= MAX_SHOP_OFFERS) {
            data_error("shop.tsv", r, "zu viele Angebote (max %d)", MAX_SHOP_OFFERS);
            break;
        }
        ShopOffer *o = &g_shop[g_shop_count];
        memset(o, 0, sizeof *o);
        str_copy(o->id, sizeof o->id, tsv_get_by_name(&t, r, "id"));
        str_copy(o->name, sizeof o->name, tsv_get_by_name(&t, r, "name"));
        str_copy(o->desc, sizeof o->desc, tsv_get_by_name(&t, r, "desc"));
        o->tab = (ShopTab)data_parse_enum(&t, r, "tab", k_shop_tabs, SHOP_TAB_COUNT, SHOP_GENERAL);
        o->kind = (ShopKind)data_parse_enum(&t, r, "kind", k_shop_kinds, SHOP_KIND_COUNT, SHOPK_ITEM);
        o->qty = tsv_get_int(&t, r, "qty", 1);
        o->currency = (Currency)data_parse_enum(&t, r, "currency", k_currencies, CUR_COUNT, CUR_GOLD);
        o->price = (i64)tsv_get_float(&t, r, "price", 0.0f);
        o->limit = tsv_get_int(&t, r, "limit", 0);
        o->period = (ShopPeriod)data_parse_enum(&t, r, "period", k_periods, 4, PERIOD_NONE);
        o->price_growth = tsv_get_float(&t, r, "price_growth", 0.0f);
        o->min_player_level = tsv_get_int(&t, r, "min_player_level", 1);

        /* Spalte 'item' wird je nach Angebotsart aufgeloest */
        const char *item = tsv_get_by_name(&t, r, "item");
        o->item = -1;
        switch (o->kind) {
        case SHOPK_ITEM:
        case SHOPK_WINGS:
            o->item = data_find_item(item);
            if (o->item < 0) {
                data_error("shop.tsv", r, "Item '%s' unbekannt", item);
            }
            break;
        case SHOPK_BOOK_CLASS:
            o->item = data_enum_index(item, k_class_ids, CLASS_COUNT);
            if (o->item < 0) {
                data_error("shop.tsv", r, "Klasse '%s' unbekannt", item);
            }
            break;
        case SHOPK_CURRENCY:
            o->item = data_enum_index(item, k_currencies, CUR_COUNT);
            if (o->item < 0) {
                data_error("shop.tsv", r, "Waehrung '%s' unbekannt", item);
            }
            break;
        case SHOPK_CHEST:
            o->item = data_enum_index(item, k_tier_ids, TIER_COUNT);
            if (o->item < 0) {
                data_error("shop.tsv", r, "Tier '%s' unbekannt", item);
            }
            break;
        default:
            break;
        }
        g_shop_count++;
    }
    tsv_free(&t);
}

static void load_traits(const char *dir) {
    Tsv t;
    if (!data_open_tsv(&t, dir, "traits.tsv", false)) {
        return;
    }
    for (int r = 0; r < t.row_count; r++) {
        if (g_trait_count >= MAX_TRAITS) {
            data_error("traits.tsv", r, "zu viele Traits (max %d)", MAX_TRAITS);
            break;
        }
        TraitDef *d = &g_traits[g_trait_count];
        memset(d, 0, sizeof *d);
        str_copy(d->id, sizeof d->id, tsv_get_by_name(&t, r, "id"));
        str_copy(d->name, sizeof d->name, tsv_get_by_name(&t, r, "name"));
        str_copy(d->desc, sizeof d->desc, tsv_get_by_name(&t, r, "desc"));
        d->tree = clampi(tsv_get_int(&t, r, "tree", 0), 0, 3);
        d->max_points = tsv_get_int(&t, r, "max_points", 1);
        str_copy(d->effect, sizeof d->effect, tsv_get_by_name(&t, r, "effect"));
        d->per_point = tsv_get_float(&t, r, "per_point", 0.0f);
        d->row = tsv_get_int(&t, r, "row", 0);
        d->requires = -1;
        g_trait_count++;
    }
    /* Zweiter Durchlauf: 'requires' (ID) in Index aufloesen */
    for (int r = 0; r < g_trait_count; r++) {
        const char *req = tsv_get_by_name(&t, r, "requires");
        if (!*req) {
            continue;
        }
        for (int i = 0; i < g_trait_count; i++) {
            if (strcmp(g_traits[i].id, req) == 0) {
                g_traits[r].requires = i;
                break;
            }
        }
        if (g_traits[r].requires < 0) {
            data_error("traits.tsv", r, "requires '%s' unbekannt", req);
        }
    }
    tsv_free(&t);
}

static void load_quests(const char *dir) {
    Tsv t;
    if (!data_open_tsv(&t, dir, "quests.tsv", false)) {
        return;
    }
    for (int r = 0; r < t.row_count; r++) {
        if (g_quest_count >= MAX_QUEST_DEFS) {
            data_error("quests.tsv", r, "zu viele Quests (max %d)", MAX_QUEST_DEFS);
            break;
        }
        QuestDef *q = &g_quests[g_quest_count];
        memset(q, 0, sizeof *q);
        str_copy(q->id, sizeof q->id, tsv_get_by_name(&t, r, "id"));
        str_copy(q->text, sizeof q->text, tsv_get_by_name(&t, r, "text"));
        q->kind = (QuestKind)data_parse_enum(&t, r, "kind", k_quest_kinds, 8, QK_KILL_ANY);
        const char *param = tsv_get_by_name(&t, r, "param");
        q->param = 0;
        if (*param) {
            if (q->kind == QK_KILL_TIER) {
                q->param = data_enum_index(param, k_tier_ids, TIER_COUNT);
            } else if (q->kind == QK_KILL_ARCH) {
                q->param = data_enum_index(param, k_archetypes, ARCH_COUNT);
            } else {
                q->param = tsv_get_int(&t, r, "param", 0);
            }
            if (q->param < 0) {
                data_error("quests.tsv", r, "param '%s' unbekannt", param);
                q->param = 0;
            }
        }
        q->count_min = tsv_get_int(&t, r, "count_min", 1);
        q->count_max = tsv_get_int(&t, r, "count_max", q->count_min);
        q->main_quest = data_parse_bool(tsv_get_by_name(&t, r, "main"));
        q->reward_cmd_exp = (i64)tsv_get_float(&t, r, "reward_cmd_exp", 0.0f);
        q->reward_gold = (i64)tsv_get_float(&t, r, "reward_gold", 0.0f);
        q->reward_diamonds = (i64)tsv_get_float(&t, r, "reward_diamonds", 0.0f);
        if (q->count_max < q->count_min) {
            data_error("quests.tsv", r, "count_max < count_min");
        }
        g_quest_count++;
    }
    tsv_free(&t);
}

bool data_load_meta(const char *dir) {
    load_shop(dir);
    load_traits(dir);
    load_quests(dir);
    return true;
}
