/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Savegame-Serialisierung. Format (little endian):
 *   Header 16 Byte: "PNX1", u32 Version, u32 Nutzlast-Laenge, u32 CRC32(Nutzlast)
 *   Nutzlast: Info-Block (Zeitstempel, Spielerlevel, Heldenzahl, Spielzeit), danach der Spielstand.
 * Verweise auf Daten (Items, Skills, Maps, Shop, Traits, Quests) werden als ID-Texte
 * gespeichert, damit Datenaenderungen alte Spielstaende nicht entwerten. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/util.h"
#include "game/save.h"
#include "platform/platform.h"
#include "rules/rules_internal.h"

#define SAVE_HEADER_SIZE 16
#define SAVE_MAX_BYTES (64u * 1024u * 1024u)
#define ID_BUF 64

/* ======================================================================== */
/* Puffer: Schreiben                                                         */
/* ======================================================================== */
typedef struct Wr {
    u8 *data;
    size_t len, cap;
    bool fail;
} Wr;

static void wr_bytes(Wr *w, const void *src, size_t n) {
    if (w->fail) {
        return;
    }
    if (w->len + n > w->cap) {
        size_t cap = w->cap ? w->cap * 2 : 65536;
        while (cap < w->len + n) {
            cap *= 2;
        }
        u8 *p = (u8 *)realloc(w->data, cap);
        if (!p) {
            w->fail = true;
            return;
        }
        w->data = p;
        w->cap = cap;
    }
    memcpy(w->data + w->len, src, n);
    w->len += n;
}

static void wr_u8(Wr *w, u32 v) {
    u8 b = (u8)v;
    wr_bytes(w, &b, 1);
}
static void wr_u16(Wr *w, u32 v) {
    u8 b[2] = {(u8)v, (u8)(v >> 8)};
    wr_bytes(w, b, 2);
}
static void wr_u32(Wr *w, u32 v) {
    u8 b[4] = {(u8)v, (u8)(v >> 8), (u8)(v >> 16), (u8)(v >> 24)};
    wr_bytes(w, b, 4);
}
static void wr_u64(Wr *w, u64 v) {
    wr_u32(w, (u32)v);
    wr_u32(w, (u32)(v >> 32));
}
static void wr_i32(Wr *w, i32 v) { wr_u32(w, (u32)v); }
static void wr_i64(Wr *w, i64 v) { wr_u64(w, (u64)v); }
static void wr_f32(Wr *w, float v) {
    u32 u;
    memcpy(&u, &v, 4);
    wr_u32(w, u);
}
static void wr_f64(Wr *w, double v) {
    u64 u;
    memcpy(&u, &v, 8);
    wr_u64(w, u);
}
static void wr_str(Wr *w, const char *s) {
    size_t n = strlen(s);
    if (n > 65535) {
        n = 65535;
    }
    wr_u16(w, (u32)n);
    wr_bytes(w, s, n);
}

/* ======================================================================== */
/* Puffer: Lesen                                                             */
/* ======================================================================== */
typedef struct Rd {
    const u8 *p;
    size_t len, pos;
    bool fail;
} Rd;

static bool rd_need(Rd *r, size_t n) {
    if (r->fail || r->pos + n > r->len) {
        r->fail = true;
        return false;
    }
    return true;
}
static u8 rd_u8(Rd *r) { return rd_need(r, 1) ? r->p[r->pos++] : 0; }
static u16 rd_u16(Rd *r) {
    if (!rd_need(r, 2)) {
        return 0;
    }
    u16 v = (u16)(r->p[r->pos] | (r->p[r->pos + 1] << 8));
    r->pos += 2;
    return v;
}
static u32 rd_u32(Rd *r) {
    if (!rd_need(r, 4)) {
        return 0;
    }
    const u8 *b = r->p + r->pos;
    r->pos += 4;
    return (u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) | ((u32)b[3] << 24);
}
static u64 rd_u64(Rd *r) {
    u64 lo = rd_u32(r);
    u64 hi = rd_u32(r);
    return lo | (hi << 32);
}
static i32 rd_i32(Rd *r) { return (i32)rd_u32(r); }
static i64 rd_i64(Rd *r) { return (i64)rd_u64(r); }
static float rd_f32(Rd *r) {
    u32 u = rd_u32(r);
    float v;
    memcpy(&v, &u, 4);
    return v;
}
static double rd_f64(Rd *r) {
    u64 u = rd_u64(r);
    double v;
    memcpy(&v, &u, 8);
    return v;
}
static void rd_str(Rd *r, char *out, size_t cap) {
    size_t n = rd_u16(r);
    if (!rd_need(r, n)) {
        out[0] = '\0';
        return;
    }
    size_t keep = n < cap - 1 ? n : cap - 1;
    memcpy(out, r->p + r->pos, keep);
    out[keep] = '\0';
    r->pos += n;
}

/* ======================================================================== */
/* Datenverweise als ID                                                      */
/* ======================================================================== */
static void wr_item_ref(Wr *w, int def) {
    wr_str(w, (def >= 0 && def < g_item_count) ? g_items[def].id : "");
}
static int rd_item_ref(Rd *r) {
    char id[ID_BUF];
    rd_str(r, id, sizeof id);
    return id[0] ? data_find_item(id) : -1;
}
static void wr_skill_ref(Wr *w, int s) {
    wr_str(w, (s >= 0 && s < g_skill_count) ? g_skills[s].id : "");
}
static int rd_skill_ref(Rd *r) {
    char id[ID_BUF];
    rd_str(r, id, sizeof id);
    return id[0] ? data_find_skill(id) : -1;
}

static void wr_item(Wr *w, const Item *it) {
    bool empty = item_is_empty(it) || it->def >= g_item_count;
    wr_item_ref(w, empty ? -1 : it->def);
    if (empty) {
        return;
    }
    wr_u8(w, (u8)it->plus);
    wr_u8(w, it->flags);
    wr_u8(w, it->ench_opt);
    wr_u8(w, it->ench_grade);
    wr_u8(w, it->fill);
    wr_skill_ref(w, it->ench_skill);
    wr_f32(w, it->ench_val);
}

static Item rd_item(Rd *r) {
    int def = rd_item_ref(r);
    if (def < 0) {
        return item_empty();
    }
    Item it = item_make(def);
    it.plus = (i8)clampi((i8)rd_u8(r), 0, ENHANCE_MAX);
    it.flags = rd_u8(r);
    it.ench_opt = rd_u8(r);
    it.ench_grade = rd_u8(r);
    it.fill = (u8)mini(rd_u8(r), 100);
    it.ench_skill = (i16)rd_skill_ref(r);
    it.ench_val = rd_f32(r);
    if (it.ench_opt >= ENCH_COUNT) {
        it.ench_opt = ENCH_NONE;
    }
    return it;
}

/* Ein Item, dessen ID in den Daten fehlt, muss die Folgefelder trotzdem konsumieren. */
static Item rd_item_checked(Rd *r) {
    size_t save_pos = r->pos;
    char id[ID_BUF];
    rd_str(r, id, sizeof id);
    if (!id[0]) {
        return item_empty();
    }
    bool known = data_find_item(id) >= 0;
    r->pos = save_pos;
    if (known) {
        return rd_item(r);
    }
    rd_str(r, id, sizeof id);
    if (rd_need(r, 5)) {
        r->pos += 5; /* plus, flags, opt, grade, fill */
    }
    rd_skill_ref(r);
    rd_f32(r);
    return item_empty();
}

/* ======================================================================== */
/* Teilstrukturen schreiben                                                  */
/* ======================================================================== */
static void wr_skill_ai(Wr *w, const SkillAi *s) {
    wr_u8(w, s->priority);
    wr_u8(w, s->tier_mask);
    wr_u8(w, s->min_targets);
    wr_u16(w, s->max_dist);
    wr_u8(w, s->self_hp_min);
    wr_u8(w, s->self_hp_max);
    wr_u8(w, s->self_mana_min);
    wr_u8(w, s->self_mana_max);
    wr_u8(w, s->ally_hp_below);
    wr_u8(w, s->target_allies);
    wr_u8(w, s->use_mode);
    wr_u8(w, s->allow_stack);
    wr_u16(w, s->min_cd_x10);
    wr_u8(w, s->spread_angle);
}

static void rd_skill_ai(Rd *r, SkillAi *s) {
    s->priority = rd_u8(r);
    s->tier_mask = rd_u8(r);
    s->min_targets = rd_u8(r);
    s->max_dist = rd_u16(r);
    s->self_hp_min = rd_u8(r);
    s->self_hp_max = rd_u8(r);
    s->self_mana_min = rd_u8(r);
    s->self_mana_max = rd_u8(r);
    s->ally_hp_below = rd_u8(r);
    s->target_allies = rd_u8(r);
    s->use_mode = rd_u8(r);
    s->allow_stack = rd_u8(r);
    s->min_cd_x10 = rd_u16(r);
    s->spread_angle = rd_u8(r);
}

static void wr_hero_ai(Wr *w, const HeroAi *a) {
    wr_u8(w, a->explore);
    wr_u8(w, a->clear);
    wr_u8(w, a->target_prio);
    wr_u8(w, a->target_tier_mask);
    wr_u8(w, a->speed_mode);
    wr_u8(w, a->repeat_map);
    wr_u8(w, a->auto_repeat_special);
    wr_u8(w, a->take_normal_chests);
    wr_u8(w, a->take_magic_chests);
    wr_u8(w, a->auto_dismantle);
    wr_u8(w, a->auto_open);
    wr_u8(w, a->collect_at_pct);
    wr_u8(w, a->refocus_lost);
    for (int i = 0; i < SKILL_SLOTS; i++) {
        wr_skill_ai(w, &a->skill[i]);
    }
    for (int i = 0; i < POTION_SLOTS; i++) {
        wr_u8(w, a->potion[i].hp_below);
        wr_u8(w, a->potion[i].mana_below);
        wr_u8(w, a->potion[i].only_in_combat);
        wr_u16(w, a->potion[i].min_cd_x10);
    }
}

static void rd_hero_ai(Rd *r, HeroAi *a) {
    a->explore = rd_u8(r);
    a->clear = rd_u8(r);
    a->target_prio = rd_u8(r);
    a->target_tier_mask = rd_u8(r);
    a->speed_mode = rd_u8(r);
    a->repeat_map = rd_u8(r);
    a->auto_repeat_special = rd_u8(r);
    a->take_normal_chests = rd_u8(r);
    a->take_magic_chests = rd_u8(r);
    a->auto_dismantle = rd_u8(r);
    a->auto_open = rd_u8(r);
    a->collect_at_pct = rd_u8(r);
    a->refocus_lost = rd_u8(r);
    for (int i = 0; i < SKILL_SLOTS; i++) {
        rd_skill_ai(r, &a->skill[i]);
    }
    for (int i = 0; i < POTION_SLOTS; i++) {
        a->potion[i].hp_below = rd_u8(r);
        a->potion[i].mana_below = rd_u8(r);
        a->potion[i].only_in_combat = rd_u8(r);
        a->potion[i].min_cd_x10 = rd_u16(r);
    }
}

static void wr_hero(Wr *w, const Hero *h) {
    wr_i32(w, h->uid);
    wr_str(w, h->name);
    wr_i32(w, h->cls);
    wr_u8(w, h->look);
    wr_i32(w, h->level);
    wr_u64(w, h->exp);
    wr_i32(w, h->str);
    wr_i32(w, h->dex);
    wr_i32(w, h->intel);
    wr_i32(w, h->free_points);
    for (int s = 0; s < SLOT_COUNT; s++) {
        wr_item(w, &h->eq[s]);
    }
    for (int s = 0; s < SKILL_SLOTS; s++) {
        wr_skill_ref(w, h->skill_slot[s]);
    }
    wr_hero_ai(w, &h->ai);
    for (int t = 0; t < TIER_COUNT; t++) {
        wr_u16(w, h->chest_bar[t]);
    }
    wr_i32(w, h->assigned_map);
    wr_f32(w, h->dead_timer);
    wr_f32(w, h->retry_timer);
    wr_u32(w, h->kills);
    wr_u64(w, h->total_exp_earned);
}

static void rd_hero(Rd *r, Hero *h) {
    h->used = true;
    h->uid = rd_i32(r);
    rd_str(r, h->name, sizeof h->name);
    h->cls = clampi(rd_i32(r), 0, CLASS_COUNT - 1);
    h->look = (u8)(rd_u8(r) & 7u);
    h->level = clampi(rd_i32(r), 1, HERO_MAX_LEVEL);
    h->exp = rd_u64(r);
    h->str = maxi(rd_i32(r), 0);
    h->dex = maxi(rd_i32(r), 0);
    h->intel = maxi(rd_i32(r), 0);
    h->free_points = maxi(rd_i32(r), 0);
    for (int s = 0; s < SLOT_COUNT; s++) {
        h->eq[s] = rd_item_checked(r);
    }
    for (int s = 0; s < SKILL_SLOTS; s++) {
        h->skill_slot[s] = (i16)rd_skill_ref(r);
    }
    rd_hero_ai(r, &h->ai);
    for (int t = 0; t < TIER_COUNT; t++) {
        h->chest_bar[t] = rd_u16(r);
    }
    h->assigned_map = rd_i32(r);
    h->dead_timer = rd_f32(r);
    h->retry_timer = rd_f32(r);
    h->kills = rd_u32(r);
    h->total_exp_earned = rd_u64(r);
    h->stats_dirty = true;
}

/* ---- Spieler ------------------------------------------------------------------ */
static void wr_player_core(Wr *w, const Player *p) {
    wr_str(w, p->name);
    wr_i32(w, p->level);
    wr_u64(w, p->exp);
    wr_i64(w, p->gold);
    wr_i64(w, p->diamonds);
    wr_i64(w, p->credits);
    wr_i64(w, p->raven_coins);
    wr_i64(w, p->raven_feathers);
    wr_i32(w, p->hero_slots_bonus);
    wr_i32(w, p->trait_points_total);
    for (int i = 0; i < CMD_SKILL_COUNT; i++) {
        wr_f64(w, p->cmd_skill_end[i]);
        wr_f64(w, p->cmd_skill_cd[i]);
    }
    for (int i = 0; i < CMD_ORDER_COUNT; i++) {
        wr_u8(w, p->cmd_order_on[i]);
    }
    wr_i64(w, p->post_last_unix);
    wr_f64(w, p->post_hours_today);
    wr_i64(w, p->last_seen_unix);
    wr_i32(w, p->reset_day_key);
    wr_i32(w, p->reset_week_key);
    wr_i32(w, p->reset_month_key);
    wr_i32(w, p->bounty_refreshes);
    wr_i32(w, p->main_quest_index);
    wr_i32(w, p->main_quest_progress);
    wr_u32(w, p->achievements);
    wr_i32(w, p->free_gacha_days);
    for (int i = 0; i < STAT_COUNTERS; i++) {
        wr_u64(w, p->counters[i]);
    }
    for (int t = 0; t < TIER_COUNT; t++) {
        wr_u64(w, p->kills_by_tier[t]);
    }
}

static void rd_player_core(Rd *r, Player *p) {
    rd_str(r, p->name, sizeof p->name);
    p->level = clampi(rd_i32(r), 1, PLAYER_MAX_LEVEL);
    p->exp = rd_u64(r);
    p->gold = rd_i64(r);
    p->diamonds = rd_i64(r);
    p->credits = rd_i64(r);
    p->raven_coins = rd_i64(r);
    p->raven_feathers = rd_i64(r);
    p->hero_slots_bonus = clampi(rd_i32(r), 0, MAX_HEROES);
    p->trait_points_total = maxi(rd_i32(r), 0);
    for (int i = 0; i < CMD_SKILL_COUNT; i++) {
        p->cmd_skill_end[i] = rd_f64(r);
        p->cmd_skill_cd[i] = rd_f64(r);
    }
    for (int i = 0; i < CMD_ORDER_COUNT; i++) {
        p->cmd_order_on[i] = rd_u8(r);
    }
    p->post_last_unix = rd_i64(r);
    p->post_hours_today = rd_f64(r);
    p->last_seen_unix = rd_i64(r);
    p->reset_day_key = rd_i32(r);
    p->reset_week_key = rd_i32(r);
    p->reset_month_key = rd_i32(r);
    p->bounty_refreshes = rd_i32(r);
    p->main_quest_index = maxi(rd_i32(r), 0);
    p->main_quest_progress = rd_i32(r);
    p->achievements = rd_u32(r);
    p->free_gacha_days = rd_i32(r);
    for (int i = 0; i < STAT_COUNTERS; i++) {
        p->counters[i] = rd_u64(r);
    }
    for (int t = 0; t < TIER_COUNT; t++) {
        p->kills_by_tier[t] = rd_u64(r);
    }
}

static void wr_player_tables(Wr *w, const Player *p) {
    u32 n = 0;
    for (int s = 0; s < g_skill_count; s++) {
        n += (p->skill_level[s] || p->skill_books[s]) ? 1u : 0u;
    }
    wr_u32(w, n);
    for (int s = 0; s < g_skill_count; s++) {
        if (p->skill_level[s] || p->skill_books[s]) {
            wr_skill_ref(w, s);
            wr_u16(w, p->skill_level[s]);
            wr_u16(w, p->skill_books[s]);
        }
    }
    n = 0;
    for (int o = 0; o < g_shop_count; o++) {
        n += (p->shop_bought[o] || p->shop_total[o]) ? 1u : 0u;
    }
    wr_u32(w, n);
    for (int o = 0; o < g_shop_count; o++) {
        if (p->shop_bought[o] || p->shop_total[o]) {
            wr_str(w, g_shop[o].id);
            wr_u16(w, p->shop_bought[o]);
            wr_u16(w, p->shop_total[o]);
        }
    }
    n = 0;
    for (int t = 0; t < g_trait_count; t++) {
        n += p->traits[t] ? 1u : 0u;
    }
    wr_u32(w, n);
    for (int t = 0; t < g_trait_count; t++) {
        if (p->traits[t]) {
            wr_str(w, g_traits[t].id);
            wr_u8(w, p->traits[t]);
        }
    }
    for (int i = 0; i < BOUNTY_PER_DAY; i++) {
        const BountyQuest *b = &p->bounty[i];
        wr_str(w, (b->def >= 0 && b->def < g_quest_count) ? g_quests[b->def].id : "");
        wr_i32(w, b->target);
        wr_i32(w, b->progress);
        wr_u8(w, b->claimed);
        wr_i32(w, b->param);
    }
    wr_i32(w, p->mail_count);
    for (int i = 0; i < p->mail_count; i++) {
        const MailMsg *m = &p->mail[i];
        wr_str(w, m->title);
        wr_str(w, m->text);
        wr_i32(w, (i32)m->cur);
        wr_i64(w, m->amount);
        wr_u8(w, m->claimed);
        wr_u8(w, m->read);
        wr_i64(w, m->time);
    }
}

static int find_by_id(const char *id, int count, const char *(*get)(int)) {
    for (int i = 0; i < count; i++) {
        if (strcmp(get(i), id) == 0) {
            return i;
        }
    }
    return -1;
}
static const char *shop_id(int i) { return g_shop[i].id; }
static const char *trait_id(int i) { return g_traits[i].id; }
static const char *quest_id(int i) { return g_quests[i].id; }

static void rd_player_tables(Rd *r, Player *p) {
    char id[ID_BUF];
    u32 n = rd_u32(r);
    for (u32 i = 0; i < n && !r->fail; i++) {
        int s = rd_skill_ref(r);
        u16 lv = rd_u16(r), books = rd_u16(r);
        if (s >= 0) {
            p->skill_level[s] = (u16)mini(lv, SKILL_MAX_LEVEL);
            p->skill_books[s] = books;
        }
    }
    n = rd_u32(r);
    for (u32 i = 0; i < n && !r->fail; i++) {
        rd_str(r, id, sizeof id);
        u16 bought = rd_u16(r), total = rd_u16(r);
        int o = find_by_id(id, g_shop_count, shop_id);
        if (o >= 0) {
            p->shop_bought[o] = bought;
            p->shop_total[o] = total;
        }
    }
    n = rd_u32(r);
    for (u32 i = 0; i < n && !r->fail; i++) {
        rd_str(r, id, sizeof id);
        u8 pts = rd_u8(r);
        int t = find_by_id(id, g_trait_count, trait_id);
        if (t >= 0) {
            p->traits[t] = (u8)mini(pts, g_traits[t].max_points);
        }
    }
    for (int i = 0; i < BOUNTY_PER_DAY; i++) {
        BountyQuest *b = &p->bounty[i];
        rd_str(r, id, sizeof id);
        b->def = (i16)(id[0] ? find_by_id(id, g_quest_count, quest_id) : -1);
        b->target = rd_i32(r);
        b->progress = rd_i32(r);
        b->claimed = rd_u8(r);
        b->param = rd_i32(r);
    }
    p->mail_count = clampi(rd_i32(r), 0, MAX_MAIL);
    for (int i = 0; i < p->mail_count && !r->fail; i++) {
        MailMsg *m = &p->mail[i];
        rd_str(r, m->title, sizeof m->title);
        rd_str(r, m->text, sizeof m->text);
        m->cur = (Currency)clampi(rd_i32(r), 0, CUR_COUNT - 1);
        m->amount = rd_i64(r);
        m->claimed = rd_u8(r) != 0;
        m->read = rd_u8(r) != 0;
        m->time = rd_i64(r);
    }
}

/* ---- Lager ------------------------------------------------------------------------ */
static void wr_inventory(Wr *w, const Inventory *inv) {
    wr_i32(w, inv->capacity);
    wr_u8(w, inv->dismantle_below_tier);
    wr_u8(w, inv->dismantle_below_level);
    wr_i32(w, inv->item_count);
    for (int i = 0; i < inv->item_count; i++) {
        wr_item(w, &inv->items[i]);
    }
    u32 n = 0;
    for (int d = 0; d < g_item_count; d++) {
        n += inv->stock[d] ? 1u : 0u;
    }
    wr_u32(w, n);
    for (int d = 0; d < g_item_count; d++) {
        if (inv->stock[d]) {
            wr_item_ref(w, d);
            wr_u32(w, inv->stock[d]);
        }
    }
    for (int t = 0; t < TIER_COUNT; t++) {
        wr_u32(w, inv->chests[t]);
    }
}

static void rd_inventory(Rd *r, Inventory *inv) {
    inv->capacity = clampi(rd_i32(r), (int)g_bal.inv_base_capacity, INV_MAX_ITEMS);
    inv->dismantle_below_tier = rd_u8(r);
    inv->dismantle_below_level = rd_u8(r);
    int count = clampi(rd_i32(r), 0, INV_MAX_ITEMS);
    inv->item_count = 0;
    for (int i = 0; i < count && !r->fail; i++) {
        Item it = rd_item_checked(r);
        if (!item_is_empty(&it)) {
            inv->items[inv->item_count++] = it;
        }
    }
    u32 n = rd_u32(r);
    for (u32 i = 0; i < n && !r->fail; i++) {
        int d = rd_item_ref(r);
        u32 c = rd_u32(r);
        if (d >= 0) {
            inv->stock[d] = c;
        }
    }
    for (int t = 0; t < TIER_COUNT; t++) {
        inv->chests[t] = rd_u32(r);
    }
}

/* ---- Welt ---------------------------------------------------------------------------- */
static bool map_progress_is_default(const MapProgress *m) {
    if (m->cleared || m->first_claimed || m->clear_count || m->best_time > 0.0f || m->repeat != 1) {
        return false;
    }
    for (int t = 0; t < MAX_TEAM; t++) {
        if (m->team[t] >= 0) {
            return false;
        }
    }
    return true;
}

static void wr_world(Wr *w, const World *wd) {
    u32 n = 0;
    for (int m = 0; m < g_map_count; m++) {
        n += map_progress_is_default(&wd->map[m]) ? 0u : 1u;
    }
    wr_u32(w, n);
    for (int m = 0; m < g_map_count; m++) {
        const MapProgress *mp = &wd->map[m];
        if (map_progress_is_default(mp)) {
            continue;
        }
        wr_str(w, g_maps[m].id);
        wr_u8(w, mp->cleared);
        wr_u8(w, mp->first_claimed);
        wr_u8(w, mp->repeat);
        wr_u32(w, mp->clear_count);
        wr_f32(w, mp->best_time);
        for (int t = 0; t < MAX_TEAM; t++) {
            wr_u8(w, (u8)mp->team[t]);
        }
    }
    for (int i = 0; i < 16; i++) {
        wr_u8(w, wd->act_chest_claimed[i]);
    }
    wr_i32(w, wd->temple_tickets);
    wr_i32(w, wd->ruins_tickets);
    wr_i32(w, wd->command_points_spent);
}

static void rd_world(Rd *r, World *wd) {
    char id[ID_BUF];
    u32 n = rd_u32(r);
    for (u32 i = 0; i < n && !r->fail; i++) {
        rd_str(r, id, sizeof id);
        MapProgress tmp;
        memset(&tmp, 0, sizeof tmp);
        tmp.cleared = rd_u8(r);
        tmp.first_claimed = rd_u8(r);
        tmp.repeat = rd_u8(r);
        tmp.clear_count = rd_u32(r);
        tmp.best_time = rd_f32(r);
        for (int t = 0; t < MAX_TEAM; t++) {
            tmp.team[t] = (i8)rd_u8(r);
        }
        int m = data_find_map(id);
        if (m >= 0) {
            wd->map[m] = tmp;
        }
    }
    for (int i = 0; i < 16; i++) {
        wd->act_chest_claimed[i] = rd_u8(r);
    }
    wd->temple_tickets = maxi(rd_i32(r), 0);
    wd->ruins_tickets = maxi(rd_i32(r), 0);
    wd->command_points_spent = maxi(rd_i32(r), 0);
}

/* ======================================================================== */
/* Gesamtserialisierung                                                      */
/* ======================================================================== */
static void wr_info(Wr *w, const Game *g, i64 now) {
    wr_i64(w, now);
    wr_i32(w, g->player.level);
    wr_i32(w, game_hero_count(g));
    wr_f64(w, g->play_time);
}

static void wr_body(Wr *w, const Game *g) {
    wr_player_core(w, &g->player);
    wr_player_tables(w, &g->player);
    wr_i32(w, g->next_hero_uid);
    for (int i = 0; i < MAX_HEROES; i++) {
        wr_u8(w, g->heroes[i].used);
        if (g->heroes[i].used) {
            wr_hero(w, &g->heroes[i]);
        }
    }
    wr_inventory(w, &g->inv);
    wr_world(w, &g->world);
    wr_u64(w, g->rng.state);
    wr_u64(w, g->rng.inc);
    wr_f64(w, g->play_time);
}

static void rd_body(Rd *r, Game *g) {
    rd_player_core(r, &g->player);
    rd_player_tables(r, &g->player);
    g->next_hero_uid = maxi(rd_i32(r), 1);
    for (int i = 0; i < MAX_HEROES && !r->fail; i++) {
        if (rd_u8(r)) {
            rd_hero(r, &g->heroes[i]);
        }
    }
    rd_inventory(r, &g->inv);
    rd_world(r, &g->world);
    g->rng.state = rd_u64(r);
    g->rng.inc = rd_u64(r) | 1u;
    g->play_time = rd_f64(r);
}

/* Teamlisten und Heldenzuweisung nach dem Laden widerspruchsfrei machen. */
static void fix_assignments(Game *g) {
    for (int m = 0; m < g_map_count; m++) {
        for (int t = 0; t < MAX_TEAM; t++) {
            int h = g->world.map[m].team[t];
            bool ok = h >= 0 && h < MAX_HEROES && g->heroes[h].used && g->heroes[h].assigned_map == m &&
                      t < g_maps[m].max_heroes;
            if (!ok) {
                g->world.map[m].team[t] = -1;
            }
        }
    }
    for (int i = 0; i < MAX_HEROES; i++) {
        Hero *h = &g->heroes[i];
        if (!h->used) {
            continue;
        }
        bool found = false;
        int m = h->assigned_map;
        if (m >= 0 && m < g_map_count) {
            for (int t = 0; t < MAX_TEAM; t++) {
                found = found || g->world.map[m].team[t] == i;
            }
        }
        if (!found) {
            h->assigned_map = -1;
        }
    }
}

/* ---- Dateien ------------------------------------------------------------------------ */
bool save_slot_path(int slot, char *out, size_t size) {
    if (slot < 0 || slot >= SAVE_SLOTS) {
        return false;
    }
    char dir[400];
    if (!platform_get_save_dir(dir, sizeof dir)) {
        return false;
    }
    str_format(out, size, "%s/slot%d.sav", dir, slot);
    return true;
}

bool save_exists(int slot) {
    char path[460];
    return save_slot_path(slot, path, sizeof path) && platform_file_exists(path);
}

static bool write_file(const char *path, const u8 *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        return false;
    }
    bool ok = fwrite(data, 1, len, f) == len;
    ok = (fflush(f) == 0) && ok;
    ok = (fclose(f) == 0) && ok;
    return ok;
}

static u8 *read_file(const char *path, size_t *len_out) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    u8 *buf = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        long sz = ftell(f);
        if (sz > 0 && (unsigned long)sz <= SAVE_MAX_BYTES && fseek(f, 0, SEEK_SET) == 0) {
            buf = (u8 *)malloc((size_t)sz);
            if (buf && fread(buf, 1, (size_t)sz, f) == (size_t)sz) {
                *len_out = (size_t)sz;
            } else {
                free(buf);
                buf = NULL;
            }
        }
    }
    fclose(f);
    return buf;
}

/* Prueft Header + CRC; setzt Rd auf die Nutzlast. */
static bool check_file(const u8 *file, size_t len, Rd *payload, u32 *version) {
    if (len < SAVE_HEADER_SIZE || memcmp(file, "PNX1", 4) != 0) {
        return false;
    }
    Rd h = {file, len, 4, false};
    *version = rd_u32(&h);
    u32 plen = rd_u32(&h);
    u32 crc = rd_u32(&h);
    if (*version < 1 || *version > SAVE_VERSION || (size_t)plen != len - SAVE_HEADER_SIZE) {
        return false;
    }
    if (crc32_update(0, file + SAVE_HEADER_SIZE, plen) != crc) {
        return false;
    }
    payload->p = file + SAVE_HEADER_SIZE;
    payload->len = plen;
    payload->pos = 0;
    payload->fail = false;
    return true;
}

bool save_game(Game *g, int slot) {
    char path[460], tmp[470], bak[470];
    if (!save_slot_path(slot, path, sizeof path)) {
        return false;
    }
    str_format(tmp, sizeof tmp, "%s.tmp", path);
    str_format(bak, sizeof bak, "%s.bak", path);
    i64 now = platform_unix_time();
    g->player.last_seen_unix = now;
    Wr w = {NULL, 0, 0, false};
    wr_info(&w, g, now);
    wr_body(&w, g);
    if (w.fail) {
        free(w.data);
        return false;
    }
    Wr file = {NULL, 0, 0, false};
    wr_bytes(&file, "PNX1", 4);
    wr_u32(&file, SAVE_VERSION);
    wr_u32(&file, (u32)w.len);
    wr_u32(&file, crc32_update(0, w.data, w.len));
    wr_bytes(&file, w.data, w.len);
    free(w.data);
    bool ok = !file.fail && write_file(tmp, file.data, file.len);
    free(file.data);
    if (!ok) {
        remove(tmp);
        return false;
    }
    if (platform_file_exists(path)) {
        platform_replace_file(path, bak); /* Vorgaenger als Sicherung behalten */
    }
    if (!platform_replace_file(tmp, path)) {
        return false;
    }
    g->dirty_save = false;
    g->autosave_timer = 0.0;
    return true;
}

bool save_peek(int slot, SaveInfo *info) {
    memset(info, 0, sizeof *info);
    char path[460];
    if (!save_slot_path(slot, path, sizeof path)) {
        return false;
    }
    for (int attempt = 0; attempt < 2; attempt++) {
        char alt[470];
        str_format(alt, sizeof alt, attempt == 0 ? "%s" : "%s.bak", path);
        size_t len = 0;
        u8 *file = read_file(alt, &len);
        Rd r;
        u32 version;
        if (file && check_file(file, len, &r, &version)) {
            info->saved_unix = rd_i64(&r);
            info->player_level = rd_i32(&r);
            info->hero_count = rd_i32(&r);
            info->play_time = rd_f64(&r);
            info->valid = !r.fail;
            free(file);
            return info->valid;
        }
        free(file);
    }
    return false;
}

/* Liest eine Datei in einen frischen Spielstand. */
static bool read_into(Game *t, const char *path) {
    size_t len = 0;
    u8 *file = read_file(path, &len);
    if (!file) {
        return false;
    }
    Rd r;
    u32 version;
    bool ok = check_file(file, len, &r, &version);
    if (ok) {
        rd_i64(&r); /* Info-Block ueberspringen */
        rd_i32(&r);
        rd_i32(&r);
        rd_f64(&r);
        game_reset_state(t, 1);
        rd_body(&r, t);
        ok = !r.fail;
    }
    free(file);
    return ok;
}

bool load_game(Game *g, int slot) {
    char path[460], bak[470];
    if (!save_slot_path(slot, path, sizeof path)) {
        return false;
    }
    str_format(bak, sizeof bak, "%s.bak", path);
    Game *t = (Game *)malloc(sizeof *t);
    if (!t) {
        return false;
    }
    bool ok = read_into(t, path) || read_into(t, bak);
    if (ok) {
        float speed = g->speed_mult > 0.0f ? g->speed_mult : 1.0f;
        bool cheat = g->cheat_mode;
        game_shutdown(g);
        *g = *t;
        g->speed_mult = speed;
        g->cheat_mode = cheat;
        fix_assignments(g);
        game_mark_all_dirty(g);
        g->dirty_save = false;
        commander_update_post(g, platform_unix_time()); /* Abwesenheit verrechnen, Resets pruefen */
        g->dirty_save = false;
    }
    free(t);
    return ok;
}

/* ======================================================================== */
/* Textexport (Bugreports)                                                   */
/* ======================================================================== */
static void txt_item(FILE *f, const char *key, const Item *it) {
    if (item_is_empty(it) || it->def >= g_item_count) {
        return;
    }
    fprintf(f, "%s=%s +%d flags=%u ench=%u/%u fill=%u\n", key, g_items[it->def].id, it->plus, it->flags,
            it->ench_opt, it->ench_grade, it->fill);
}

static void txt_hero(FILE *f, int idx, const Hero *h) {
    fprintf(f, "\n[hero %d]\nname=%s\nuid=%d\nclass=%s\nlevel=%d\nexp=%llu\nstr=%d\ndex=%d\nint=%d\nfree_points=%d\n",
            idx, h->name, h->uid, g_classes[h->cls].id, h->level, (unsigned long long)h->exp, h->str, h->dex,
            h->intel, h->free_points);
    fprintf(f, "assigned_map=%s\nkills=%u\n", (h->assigned_map >= 0 && h->assigned_map < g_map_count) ? g_maps[h->assigned_map].id : "-", h->kills);
    for (int s = 0; s < SLOT_COUNT; s++) {
        char key[32];
        str_format(key, sizeof key, "eq%d", s);
        txt_item(f, key, &h->eq[s]);
    }
    for (int s = 0; s < SKILL_SLOTS; s++) {
        fprintf(f, "skill%d=%s\n", s, (h->skill_slot[s] >= 0 && h->skill_slot[s] < g_skill_count) ? g_skills[h->skill_slot[s]].id : "-");
    }
    fprintf(f, "chest_bar=");
    for (int t = 0; t < TIER_COUNT; t++) {
        fprintf(f, "%u%s", h->chest_bar[t], t + 1 < TIER_COUNT ? "," : "\n");
    }
}

bool save_export_text(const Game *g, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) {
        return false;
    }
    const Player *p = &g->player;
    fprintf(f, "# Panoptix Spielstand-Export (version %d)\n[player]\nname=%s\nlevel=%d\nexp=%llu\n", SAVE_VERSION, p->name,
            p->level, (unsigned long long)p->exp);
    fprintf(f, "gold=%lld\ndiamonds=%lld\ncredits=%lld\nraven_coins=%lld\nraven_feathers=%lld\n", (long long)p->gold,
            (long long)p->diamonds, (long long)p->credits, (long long)p->raven_coins, (long long)p->raven_feathers);
    fprintf(f, "hero_slots_bonus=%d\ntrait_points_total=%d\nmain_quest=%d/%d\nplay_time=%.3f\n", p->hero_slots_bonus,
            p->trait_points_total, p->main_quest_index, p->main_quest_progress, g->play_time);
    for (int i = 0; i < STAT_COUNTERS; i++) {
        fprintf(f, "counter%d=%llu\n", i, (unsigned long long)p->counters[i]);
    }
    for (int i = 0; i < g_skill_count; i++) {
        if (p->skill_level[i]) {
            fprintf(f, "skill_level.%s=%u books=%u\n", g_skills[i].id, p->skill_level[i], p->skill_books[i]);
        }
    }
    for (int i = 0; i < g_trait_count; i++) {
        if (p->traits[i]) {
            fprintf(f, "trait.%s=%u\n", g_traits[i].id, p->traits[i]);
        }
    }
    for (int i = 0; i < MAX_HEROES; i++) {
        if (g->heroes[i].used) {
            txt_hero(f, i, &g->heroes[i]);
        }
    }
    fprintf(f, "\n[inventory]\ncapacity=%d\nitems=%d\n", g->inv.capacity, g->inv.item_count);
    for (int i = 0; i < g->inv.item_count; i++) {
        txt_item(f, "item", &g->inv.items[i]);
    }
    for (int d = 0; d < g_item_count; d++) {
        if (g->inv.stock[d]) {
            fprintf(f, "stock.%s=%u\n", g_items[d].id, g->inv.stock[d]);
        }
    }
    for (int t = 0; t < TIER_COUNT; t++) {
        fprintf(f, "chests.%s=%u\n", g_tiers[t].id, g->inv.chests[t]);
    }
    fprintf(f, "\n[world]\ntemple_tickets=%d\nruins_tickets=%d\n", g->world.temple_tickets, g->world.ruins_tickets);
    for (int m = 0; m < g_map_count; m++) {
        const MapProgress *mp = &g->world.map[m];
        if (!map_progress_is_default(mp)) {
            fprintf(f, "map.%s=cleared:%u first:%u repeat:%u count:%u best:%.2f\n", g_maps[m].id, mp->cleared,
                    mp->first_claimed, mp->repeat, mp->clear_count, mp->best_time);
        }
    }
    return fclose(f) == 0;
}
