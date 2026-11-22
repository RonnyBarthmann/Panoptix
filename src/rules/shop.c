/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Shop und Waehrungen (Spec 13): Kauf, Preise, Limits, Periodenresets. */
#include <math.h>
#include <string.h>

#include "core/util.h"
#include "rules/rules_internal.h"

#define SHOP_DISCOUNT_CAP 50.0f /* maximal 50 % Rabatt durch Traits */
#define INV_CAPACITY_MAX 1000

/* ---- Waehrungen --------------------------------------------------------------- */
static i64 *currency_ref(Player *p, Currency c) {
    switch (c) {
    case CUR_GOLD: return &p->gold;
    case CUR_DIAMONDS: return &p->diamonds;
    case CUR_CREDITS: return &p->credits;
    case CUR_RAVEN_COINS: return &p->raven_coins;
    case CUR_RAVEN_FEATHERS: return &p->raven_feathers;
    default: return NULL;
    }
}

i64 currency_get(const Game *g, Currency c) {
    switch (c) {
    case CUR_GOLD: return g->player.gold;
    case CUR_DIAMONDS: return g->player.diamonds;
    case CUR_CREDITS: return g->player.credits;
    case CUR_RAVEN_COINS: return g->player.raven_coins;
    case CUR_RAVEN_FEATHERS: return g->player.raven_feathers;
    default: return 0;
    }
}

bool currency_can_pay(const Game *g, Currency c, i64 amount) {
    return amount <= 0 || currency_get(g, c) >= amount;
}

bool currency_pay(Game *g, Currency c, i64 amount) {
    if (amount <= 0) {
        return true;
    }
    i64 *r = currency_ref(&g->player, c);
    if (!r || *r < amount) {
        return false;
    }
    *r -= amount;
    g->dirty_save = true;
    return true;
}

void currency_add(Game *g, Currency c, i64 amount) {
    i64 *r = currency_ref(&g->player, c);
    if (!r || amount <= 0) {
        return;
    }
    i64 limit = (i64)4e18;
    *r = (*r > limit - amount) ? limit : *r + amount; /* Ueberlaufschutz */
    g->dirty_save = true;
}

/* ---- Angebote: Verfuegbarkeit und Preis ------------------------------------------ */
int shop_offer_remaining(const Game *g, int offer) {
    if (offer < 0 || offer >= g_shop_count) {
        return 0;
    }
    const ShopOffer *o = &g_shop[offer];
    if (o->limit <= 0) {
        return -1;
    }
    return maxi(o->limit - (int)g->player.shop_bought[offer], 0);
}

i64 shop_offer_price(const Game *g, int offer) {
    if (offer < 0 || offer >= g_shop_count) {
        return 0;
    }
    const ShopOffer *o = &g_shop[offer];
    if (o->price <= 0) {
        return 0;
    }
    double price = (double)o->price;
    if (o->price_growth > 0.0f) {
        price *= pow(1.0 + (double)o->price_growth, (double)g->player.shop_total[offer]);
    }
    CommanderMods cm;
    commander_mods(g, &cm);
    float discount = clampf(cm.shop_discount_pct, 0.0f, SHOP_DISCOUNT_CAP);
    price *= 1.0 - (double)discount / 100.0;
    if (price > 4e18) {
        price = 4e18;
    }
    return (i64)maxf((float)ceil(price), 1.0f);
}

static bool deny(char *why, size_t why_size, const char *text) {
    if (why && why_size > 0) {
        str_copy(why, why_size, text);
    }
    return false;
}

bool shop_offer_available(const Game *g, int offer, char *why, size_t why_size) {
    if (offer < 0 || offer >= g_shop_count) {
        return deny(why, why_size, "Unbekanntes Angebot");
    }
    const ShopOffer *o = &g_shop[offer];
    if (g->player.level < o->min_player_level) {
        char t[64];
        str_format(t, sizeof t, "Ab Kommandeur-Level %d", o->min_player_level);
        return deny(why, why_size, t);
    }
    if (shop_offer_remaining(g, offer) == 0) {
        return deny(why, why_size, "Limit erreicht");
    }
    if (o->kind == SHOPK_SLOTS_INV && g->inv.capacity >= INV_CAPACITY_MAX) {
        return deny(why, why_size, "Maximale Lagergröße");
    }
    if (o->kind == SHOPK_SLOTS_HERO && game_hero_slots(g) >= MAX_HEROES) {
        return deny(why, why_size, "Maximale Heldenanzahl");
    }
    return true;
}

/* ---- Kauf: Ware gutschreiben -------------------------------------------------------- */
static const double k_ring_tier_weights[TIER_COUNT] = {50, 30, 14, 5, 1, 0};
static const double k_book_grade_weights[4] = {60, 30, 9, 1};

static int roll_weighted(Game *g, const double *w, int n) {
    float f[TIER_COUNT];
    for (int i = 0; i < n && i < TIER_COUNT; i++) {
        f[i] = (float)w[i];
    }
    return rng_weighted(&g->rng, f, mini(n, TIER_COUNT));
}

static void announce(Game *g, const ItemDef *d) {
    game_toast(g, (int)d->tier, g_tiers[d->tier].color, "Erhalten: %s", d->name);
}

/* Legt ein Exemplar ins Lager; false wenn dafuer kein Platz ist. */
static bool grant_item(Game *g, int def, u32 qty) {
    if (def < 0 || def >= g_item_count) {
        return false;
    }
    if (item_is_stackable(def)) {
        inv_add_stock(g, def, qty);
        return true;
    }
    for (u32 i = 0; i < qty; i++) {
        Item it = item_make(def);
        if (!inv_add_item(g, &it)) {
            return false;
        }
    }
    return true;
}

static bool grant_random_jewel(Game *g, ItemKind kind, char *err) {
    int tier = roll_weighted(g, k_ring_tier_weights, TIER_COUNT);
    int def = loot_pick_item_def(g, kind, (Tier)clampi(tier, 0, TIER_COUNT - 1), 0, SLOT_NONE, -2);
    if (def < 0 || !grant_item(g, def, 1)) {
        rules_err(err, "Lager voll");
        return false;
    }
    announce(g, &g_items[def]);
    return true;
}

/* Buchgrad: Angebote mit "unique"/"rare" im Namen liefern genau diesen Grad. */
static int offer_book_grade(Game *g, const ShopOffer *o, bool fixed_normal) {
    if (strstr(o->id, "unique")) {
        return 3;
    }
    if (strstr(o->id, "rare")) {
        return 2;
    }
    if (fixed_normal) {
        return 0;
    }
    return maxi(roll_weighted(g, k_book_grade_weights, 4), 0);
}

static bool grant_book(Game *g, const ShopOffer *o, bool class_book, char *err) {
    int grade = offer_book_grade(g, o, class_book);
    int sk = loot_pick_skill_book(g, grade, class_book ? o->item : -1);
    if (sk < 0) {
        rules_err(err, "Kein Skillbuch verfügbar");
        return false;
    }
    skill_add_book(g, sk, 1);
    game_toast(g, g_skills[sk].grade, g_tiers[clampi(g_skills[sk].grade, 0, 3)].color, "Skillbuch: %s", g_skills[sk].name);
    return true;
}

static bool needs_free_slots(const ShopOffer *o, const Game *g) {
    int need = 0;
    if (o->kind == SHOPK_ITEM) {
        need = (o->item >= 0 && !item_is_stackable(o->item)) ? o->qty : 0;
    } else if (o->kind == SHOPK_RANDOM_RING || o->kind == SHOPK_RANDOM_NECKLACE || o->kind == SHOPK_WINGS) {
        need = 1;
    }
    return inv_free_slots(g) >= need;
}

static bool deliver(Game *g, const ShopOffer *o, char *err) {
    switch (o->kind) {
    case SHOPK_ITEM:
    case SHOPK_WINGS:
        return grant_item(g, o->item, (u32)maxi(o->qty, 1));
    case SHOPK_RANDOM_RING: return grant_random_jewel(g, IK_RING, err);
    case SHOPK_RANDOM_NECKLACE: return grant_random_jewel(g, IK_NECKLACE, err);
    case SHOPK_RANDOM_BOOK: return grant_book(g, o, false, err);
    case SHOPK_BOOK_CLASS: return grant_book(g, o, true, err);
    case SHOPK_SLOTS_INV:
        g->inv.capacity = mini(g->inv.capacity + o->qty, INV_CAPACITY_MAX);
        return true;
    case SHOPK_SLOTS_HERO:
        g->player.hero_slots_bonus += maxi(o->qty, 1);
        return true;
    case SHOPK_CURRENCY:
        currency_add(g, (Currency)clampi(o->item, 0, CUR_COUNT - 1), o->qty);
        return true;
    case SHOPK_CHEST:
        if (o->item < 0 || o->item >= TIER_COUNT) {
            return false;
        }
        g->inv.chests[o->item] += (u32)maxi(o->qty, 1);
        return true;
    }
    return false;
}

bool shop_buy(Game *g, int offer, char *err) {
    char why[64];
    if (!shop_offer_available(g, offer, why, sizeof why)) {
        rules_err(err, "%s", why);
        return false;
    }
    const ShopOffer *o = &g_shop[offer];
    i64 price = shop_offer_price(g, offer);
    if (!currency_can_pay(g, o->currency, price)) {
        rules_err(err, "Nicht genug %s", g_currency_names[o->currency]);
        return false;
    }
    if (!needs_free_slots(o, g)) {
        rules_err(err, "Lager voll");
        return false;
    }
    if (!deliver(g, o, err)) {
        return false;
    }
    currency_pay(g, o->currency, price);
    g->player.shop_bought[offer]++;
    g->player.shop_total[offer]++;
    g->dirty_save = true;
    return true;
}

/* ---- Periodenresets ------------------------------------------------------------------ */
static void reset_period(Game *g, ShopPeriod period) {
    for (int i = 0; i < g_shop_count; i++) {
        if (g_shop[i].period == period) {
            g->player.shop_bought[i] = 0;
        }
    }
}

void shop_check_resets(Game *g) {
    Player *p = &g->player;
    int day = game_day_key(), week = game_week_key(), month = game_month_key();
    if (p->reset_month_key != month) {
        p->reset_month_key = month;
        reset_period(g, PERIOD_MONTHLY);
    }
    if (p->reset_week_key != week) {
        p->reset_week_key = week;
        reset_period(g, PERIOD_WEEKLY);
    }
    if (p->reset_day_key != day) {
        p->reset_day_key = day;
        reset_period(g, PERIOD_DAILY);
        g->world.temple_tickets = (int)g_bal.temple_tickets_per_day;
        p->bounty_refreshes = 0;
        p->post_hours_today = 0.0;
        quests_refresh_daily(g, false, NULL);
    }
}
