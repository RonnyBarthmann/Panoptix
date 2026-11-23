/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Run-Verwaltung: Anlegen/Zerstoeren/Neustart, Besetzung der Karte (Monsterpool je Akt,
 * Tier-Gewichte, Affixe, Einzigartige auf Nebenpfaden), Tick-Reihenfolge, Gegner-/Helden-
 * Lebenszyklus, Kisten-Pickups, Boss-Arena, Map-Abschluss und Ereignisring. */
#include <stdlib.h>
#include <string.h>

#include "sim/sim_internal.h"

#define POPULATION_CAP 300          /* Platz fuer Adds/Splitter bleibt frei */
#define UNIQUE_SIDE_CHANCE 0.40     /* Einzigartig am Nebenpfad-Packpunkt */
#define BOSS_NO_ARENA_RADIUS 250.0f /* ohne Arena: Boss beim Ausgang */
#define PICKUP_JITTER 26.0f
#define AFFIX_COUNT 11
#define AFFIX_FAST_MULT 1.4f
#define AFFIX_ARMOR_MULT 1.5f
#define AFFIX_SHIELD_PCT 0.2f

/* ======================================================================== */
/* Ereignisse                                                                */
/* ======================================================================== */
void sim_emit(Run *r, SimEventType t, Vec2 pos, float value, int a, int b, u8 tier, bool flag, u32 color) {
    SimEvent *e = &r->ev[r->ev_head];
    e->type = t;
    e->pos = pos;
    e->value = value;
    e->a = a;
    e->b = b;
    e->tier = tier;
    e->flag = flag;
    e->color = color;
    r->ev_head = (r->ev_head + 1) % SIM_EVENT_RING;
    if (r->ev_head == r->ev_tail) { /* Ring voll: aeltestes Ereignis verwerfen */
        r->ev_tail = (r->ev_tail + 1) % SIM_EVENT_RING;
    }
}

bool run_pop_event(Run *r, SimEvent *out) {
    if (r->ev_tail == r->ev_head) {
        return false;
    }
    *out = r->ev[r->ev_tail];
    r->ev_tail = (r->ev_tail + 1) % SIM_EVENT_RING;
    return true;
}

void run_clear_events(Run *r) { r->ev_head = r->ev_tail = 0; }

/* ======================================================================== */
/* Abfragen                                                                  */
/* ======================================================================== */
RunHero *run_find_hero(Run *r, int hero_idx) {
    for (int i = 0; i < r->hero_count; i++) {
        if (r->heroes[i].hero_idx == hero_idx) {
            return &r->heroes[i];
        }
    }
    return NULL;
}

int run_slot_of_hero(const Run *r, int hero_idx) {
    for (int i = 0; i < r->hero_count; i++) {
        if (r->heroes[i].hero_idx == hero_idx) {
            return i;
        }
    }
    return -1;
}

int run_heroes_alive(const Run *r) {
    int n = 0;
    for (int i = 0; i < r->hero_count; i++) {
        n += r->heroes[i].u.alive ? 1 : 0;
    }
    return n;
}

int run_enemies_alive(const Run *r) {
    int n = 0;
    for (int i = 0; i < r->enemy_count; i++) {
        n += r->enemies[i].u.alive ? 1 : 0;
    }
    return n;
}

int run_enemy_at(const Run *r, Vec2 p, float extra) {
    int best = -1;
    float bd = 1e18f;
    for (int i = 0; i < r->enemy_count; i++) {
        const Enemy *e = &r->enemies[i];
        if (!e->u.alive) {
            continue;
        }
        float lim = e->u.radius + extra;
        float d2 = v2_dist2(e->u.pos, p);
        if (d2 <= lim * lim && d2 < bd) {
            bd = d2;
            best = i;
        }
    }
    return best;
}

float run_progress(const Run *r) {
    if (r->state != RUN_ACTIVE && r->boss_dead) {
        return 1.0f;
    }
    if (r->state == RUN_CLEARED) {
        return 1.0f;
    }
    float f = r->main_spawned > 0 ? (float)r->main_killed / (float)r->main_spawned : 0.0f;
    if (r->def && r->def->boss >= 0) {
        f = f * 0.9f + (r->boss_dead ? 0.1f : 0.0f);
    }
    return clampf(f, 0.0f, 1.0f);
}

const char *hero_state_name(HeroState s) {
    switch (s) {
    case HS_SPAWN: return "Erscheint";
    case HS_EXPLORE: return "Erkundet";
    case HS_ENGAGE: return "Kaempft";
    case HS_BOSS_ENTER: return "Naehert sich dem Boss";
    case HS_BOSS_FIGHT: return "Bosskampf";
    case HS_MAP_CLEAR: return "Map abgeschlossen";
    case HS_RETRY_WAIT: return "Wartet auf Wiederholung";
    case HS_DEAD: return "Gefallen";
    case HS_STUCK: return "Haengt fest";
    case HS_MANUAL: return "Manuell";
    }
    return "?";
}

bool run_in_arena(const Run *r, Vec2 p) {
    if (r->map.has_boss_arena) {
        const Rect *a = &r->map.arena;
        return p.x >= (float)a->x && p.y >= (float)a->y && p.x < (float)(a->x + a->w) && p.y < (float)(a->y + a->h);
    }
    return r->def && r->def->boss >= 0 && v2_dist(p, r->map.exit) < BOSS_NO_ARENA_RADIUS;
}

/* Akt fuer den Monsterpool: Story = Map-Akt; Tempel/Ruinen leiten ihn aus dem Level ab. */
int run_act(const Run *r) {
    if (r->def->act >= 1) {
        return clampi(r->def->act, 1, 11);
    }
    return clampi(r->def->level / 10 + 1, 1, 11);
}

bool hero_wants_chest(const Game *g, int hero_idx, Tier tier) {
    if (hero_idx < 0 || hero_idx >= MAX_HEROES || !g->heroes[hero_idx].used) {
        return false;
    }
    const HeroAi *ai = &g->heroes[hero_idx].ai;
    if (tier == TIER_NORMAL) {
        return ai->take_normal_chests != 0;
    }
    if (tier == TIER_MAGIC) {
        return ai->take_magic_chests != 0;
    }
    return true;
}

/* ======================================================================== */
/* Gegner                                                                    */
/* ======================================================================== */
static int free_enemy_slot(Run *r) {
    if (r->enemy_count < MAX_ENEMIES) {
        return r->enemy_count++;
    }
    for (int i = 0; i < MAX_ENEMIES; i++) { /* Pool voll: toten Eintrag wiederverwenden */
        if (!r->enemies[i].u.alive) {
            return i;
        }
    }
    return -1;
}

static float kind_hp_mult(const Run *r) {
    switch (r->def->kind) {
    case MAPK_TEMPLE: return (float)g_bal.temple_hp_mult;
    case MAPK_RUINS: return (float)g_bal.ruins_hp_mult;
    default: return r->def->act == 11 ? (float)g_bal.act11_hp_mult : 1.0f;
    }
}

static float kind_dmg_mult(const Run *r) {
    switch (r->def->kind) {
    case MAPK_TEMPLE: return (float)g_bal.temple_dmg_mult;
    case MAPK_RUINS: return (float)g_bal.ruins_dmg_mult;
    default: return 1.0f;
    }
}

static void apply_affix_stats(Enemy *e) {
    if (e->affixes & AFFIX_FAST) {
        e->speed *= AFFIX_FAST_MULT;
    }
    if (e->affixes & AFFIX_ARMORED) {
        e->armor *= AFFIX_ARMOR_MULT;
    }
    if (e->affixes & AFFIX_SHIELDED) {
        e->shield_hp = e->u.max_hp * AFFIX_SHIELD_PCT;
        e->shielded = true;
    }
}

int enemy_spawn(Run *r, int def, Tier tier, int level, Vec2 pos, u32 affixes, int pack_id, int parent) {
    if (def < 0 || def >= g_monster_count) {
        return -1;
    }
    int idx = free_enemy_slot(r);
    if (idx < 0) {
        return -1;
    }
    const MonsterDef *d = &g_monsters[def];
    const TierDef *t = &g_tiers[clampi((int)tier, 0, TIER_COUNT - 1)];
    Enemy *e = &r->enemies[idx];
    memset(e, 0, sizeof *e);
    e->def = def;
    e->tier = tier;
    e->level = level;
    e->affixes = affixes;
    e->u.pos = pos;
    e->u.face = v2(1.0f, 0.0f);
    e->u.radius = d->radius > 0.0f ? d->radius : 20.0f;
    e->u.max_hp = maxf(monster_base_hp(level) * d->hp_f * t->hp_mult * kind_hp_mult(r), 1.0f);
    e->u.hp = e->u.max_hp;
    e->u.alive = true;
    e->u.anim_t = (float)((u32)idx * 7u % 100u) * 0.01f;
    e->dmg = monster_base_damage(level) * d->dmg_f * t->dmg_mult * kind_dmg_mult(r);
    e->armor = monster_base_armor(level);
    e->spell_res = monster_base_spellres(level);
    e->speed = d->speed;
    e->spawn = pos;
    e->pack_id = pack_id;
    e->parent = parent;
    e->target_hero = -1;
    e->atk_speed_mult = 1.0f;
    e->state = ES_DORMANT;
    e->aux = pos;
    apply_affix_stats(e);
    if (r->populating && parent < 0) {
        r->main_spawned++;
    }
    r->total_spawned++;
    r->alive_count++;
    return idx;
}

static void kill_children(Run *r, int parent) {
    for (int i = 0; i < r->enemy_count; i++) {
        Enemy *c = &r->enemies[i];
        if (c->u.alive && c->parent == parent) {
            c->u.alive = false;
            c->u.hp = 0.0f;
            c->state = ES_DEAD;
        }
    }
}

static bool anyone_wants(const Run *r, const Game *g, Tier tier) {
    for (int i = 0; i < r->hero_count; i++) {
        if (hero_wants_chest(g, r->heroes[i].hero_idx, tier)) {
            return true;
        }
    }
    return false;
}

static void drop_kill_loot(Run *r, Game *g, const Enemy *e, int killer_slot) {
    int hero_idx = r->heroes[killer_slot].hero_idx;
    KillLoot loot;
    game_on_kill(g, hero_idx, e->def, e->tier, e->level, r->map_idx, &loot);
    for (int i = 0; i < loot.chest_count && i < 16; i++) {
        float a = (float)i * 2.4f;
        Vec2 p = v2_add(e->u.pos, v2(cosf(a) * PICKUP_JITTER * (float)(i > 0), sinf(a) * PICKUP_JITTER * (float)(i > 0)));
        if (anyone_wants(r, g, (Tier)loot.chest_tier[i])) { /* nur Kisten, die jemand aufnehmen wuerde */
            pickup_spawn(r, world_walkable(&r->map, p, 6.0f) ? p : e->u.pos, (Tier)loot.chest_tier[i]);
        }
    }
}

void enemy_kill(Run *r, Game *g, int ei, int killer_slot) {
    if (ei < 0 || ei >= r->enemy_count) {
        return;
    }
    Enemy *e = &r->enemies[ei];
    if (!e->u.alive) {
        return;
    }
    e->u.hp = 0.0f;
    enemy_apply_affix_death(r, g, ei);
    e = &r->enemies[ei];
    e->u.alive = false;
    e->state = ES_DEAD;
    r->alive_count = maxi(r->alive_count - 1, 0);
    r->kills++;
    if (!e->no_loot && !e->is_boss && e->parent < 0) {
        r->main_killed++;
    }
    sim_emit(r, EV_KILL, e->u.pos, 0, ei, 0, (u8)e->tier, e->is_boss, 0);
    if (killer_slot >= 0 && killer_slot < r->hero_count && !e->no_loot) {
        drop_kill_loot(r, g, e, killer_slot);
    }
    if (e->is_boss) {
        r->boss_dead = true;
        r->arena_sealed = false;
        kill_children(r, ei);
    }
}

/* ======================================================================== */
/* Kisten                                                                    */
/* ======================================================================== */
static int pickup_slot_for(Run *r, Tier tier) {
    int worst = -1;
    for (int i = 0; i < MAX_PICKUPS; i++) {
        if (!r->pickups[i].active) {
            return i;
        }
        if (r->pickups[i].tier <= tier && (worst < 0 || r->pickups[i].tier < r->pickups[worst].tier ||
                                           (r->pickups[i].tier == r->pickups[worst].tier &&
                                            r->pickups[i].age > r->pickups[worst].age))) {
            worst = i;
        }
    }
    return worst; /* -1: alles hoeherwertig, Kiste verfaellt */
}

void pickup_spawn(Run *r, Vec2 pos, Tier tier) {
    int slot = pickup_slot_for(r, tier);
    if (slot < 0) {
        return;
    }
    Pickup *p = &r->pickups[slot];
    p->active = true;
    p->pos = pos;
    p->tier = tier;
    p->age = 0.0f;
    sim_emit(r, EV_CHEST_DROP, pos, 0, 0, 0, (u8)tier, false, 0);
}

/* ======================================================================== */
/* Helden                                                                    */
/* ======================================================================== */
void hero_die(Run *r, Game *g, int slot) {
    RunHero *h = &r->heroes[slot];
    if (!h->u.alive) {
        return;
    }
    h->u.alive = false;
    h->u.hp = 0.0f;
    h->state = HS_DEAD;
    h->target = -1;
    h->in_combat = false;
    h->resting = false;
    h->hunting = false;
    h->u.st_count = 0;
    r->hero_dead_count++;
    sim_emit(r, EV_HERO_DEATH, h->u.pos, 0, slot, 0, 0, false, 0);
    game_on_hero_death(g, h->hero_idx, r->map_idx);
}

/* ======================================================================== */
/* Besetzung                                                                 */
/* ======================================================================== */
typedef struct Pool {
    int def[MAX_MONSTERS];
    float weight[MAX_MONSTERS];
    int count;
} Pool;

static void pool_add(Pool *p, int def, float w) {
    if (p->count < MAX_MONSTERS && w > 0.0f) {
        p->def[p->count] = def;
        p->weight[p->count] = w;
        p->count++;
    }
}

/* Monsterpool des Akts. Tempel/Ruinen bevorzugen ihre Sondermonster. */
static void build_pool(const Run *r, Pool *p) {
    int act = run_act(r);
    p->count = 0;
    for (int i = 0; i < g_monster_count; i++) {
        const MonsterDef *d = &g_monsters[i];
        if (d->is_boss || d->arch == ARCH_BOSS || act < d->act_min || act > d->act_max) {
            continue;
        }
        bool special = strcmp(d->id, "raven_shade") == 0 || strcmp(d->id, "relic_construct") == 0;
        if (special) {
            bool wanted = (r->def->kind == MAPK_TEMPLE && strcmp(d->id, "raven_shade") == 0) ||
                          (r->def->kind == MAPK_RUINS && strcmp(d->id, "relic_construct") == 0);
            if (!wanted) {
                continue;
            }
            pool_add(p, i, d->weight * 4.0f);
            continue;
        }
        pool_add(p, i, d->weight > 0.0f ? d->weight : 1.0f);
    }
    if (p->count == 0) { /* Notfall: irgendein Nicht-Boss */
        for (int i = 0; i < g_monster_count; i++) {
            if (!g_monsters[i].is_boss) {
                pool_add(p, i, 1.0f);
            }
        }
    }
}

static int pool_pick(Rng *rng, const Pool *p, bool elite) {
    float w[MAX_MONSTERS];
    for (int i = 0; i < p->count; i++) {
        const MonsterDef *d = &g_monsters[p->def[i]];
        w[i] = (!elite && d->is_elite_only) ? 0.0f : p->weight[i];
    }
    int k = rng_weighted(rng, w, p->count);
    return p->def[k < 0 ? 0 : k];
}

/* Tier einer Pack-Spitze nach g_tiers[].spawn_weight (Boss nie; Legendaer ab Akt 11). */
static Tier roll_tier(Rng *rng, int act, bool side) {
    float w[TIER_COUNT];
    for (int t = 0; t < TIER_COUNT; t++) {
        w[t] = g_tiers[t].spawn_weight;
    }
    w[TIER_BOSS] = 0.0f;
    w[TIER_UNIQUE] = 0.0f; /* Einzigartige werden gesondert (Nebenpfade) gewuerfelt */
    if (act < 11) {
        w[TIER_LEGENDARY] = 0.0f;
    }
    (void)side;
    int k = rng_weighted(rng, w, TIER_COUNT);
    return k < 0 ? TIER_NORMAL : (Tier)k;
}

static u32 roll_affixes(Rng *rng, int count) {
    u32 mask = 0;
    for (int n = 0; n < count; n++) {
        for (int tries = 0; tries < 12; tries++) {
            u32 bit = 1u << (u32)rng_range(rng, 0, AFFIX_COUNT - 1);
            if (!(mask & bit)) {
                mask |= bit;
                break;
            }
        }
    }
    return mask;
}

static int affix_count_for(Tier t) {
    switch (t) {
    case TIER_MAGIC: return 1;
    case TIER_RARE: return 2;
    case TIER_UNIQUE:
    case TIER_LEGENDARY: return 3;
    default: return 0;
    }
}

/* Fruehe Maps: kleinere Packs (Spec 14: Level-1-Held schafft Akt 1). */
static float level_pack_factor(int level) { return clampf(0.5f + 0.05f * (float)level, 0.5f, 1.0f); }

static int pack_size_for(Rng *rng, Tier lead, float density, int level) {
    int base;
    switch (lead) {
    case TIER_NORMAL: base = rng_range(rng, 3, 8); break;
    case TIER_MAGIC: base = rng_range(rng, 3, 5); break;
    case TIER_RARE: base = rng_range(rng, 3, 4); break;
    default: base = rng_range(rng, 2, 4); break;
    }
    float f = clampf(density, 0.3f, 3.0f) * (lead == TIER_NORMAL ? level_pack_factor(level) : 1.0f);
    return clampi((int)((float)base * (lead == TIER_NORMAL ? f : sqrtf(f)) + 0.5f), lead == TIER_NORMAL ? 2 : 1, 12);
}

static Vec2 member_pos(Run *r, Rng *rng, Vec2 center, int k) {
    float a = rng_frange(rng, 0.0f, 2.0f * PNX_PI);
    float d = k == 0 ? 0.0f : rng_frange(rng, 24.0f, 70.0f);
    Vec2 p = v2_add(center, v2(cosf(a) * d, sinf(a) * d));
    return world_nearest_walkable(&r->map, p, 16.0f);
}

static void spawn_pack(Run *r, Rng *rng, const Pool *pool, Vec2 at, Tier lead, float density, int pack_id, bool side) {
    int n = pack_size_for(rng, lead, density, r->def->level);
    int lead_def = pool_pick(rng, pool, lead >= TIER_RARE);
    int grunt_def = lead == TIER_NORMAL ? lead_def : pool_pick(rng, pool, false);
    for (int k = 0; k < n && r->enemy_count < POPULATION_CAP; k++) {
        Tier t = (k == 0) ? lead : TIER_NORMAL;
        int def = (k == 0) ? lead_def : (lead == TIER_NORMAL || rng_chance(rng, 0.7) ? grunt_def : pool_pick(rng, pool, false));
        u32 aff = (k == 0) ? roll_affixes(rng, affix_count_for(t)) : 0u;
        int idx = enemy_spawn(r, def, t, r->def->level, member_pos(r, rng, at, k), aff, pack_id, -1);
        if (idx >= 0) {
            r->enemies[idx].side = side;
        }
    }
}

static void populate(Run *r, Game *g) {
    CommanderMods cm;
    commander_mods(g, &cm);
    Rng rng;
    rng_seed(&rng, r->def->seed, (u64)r->map_idx + 77u);
    Pool pool;
    build_pool(r, &pool);
    int act = run_act(r);
    float density = maxf(r->def->density, 0.1f) * (1.0f + cm.density_pct / 100.0f);
    double unique_chance = UNIQUE_SIDE_CHANCE;
    int uniques_left = 1 + (rng_chance(&rng, (double)cm.unique_double_pct / 100.0) ? 1 : 0);
    r->populating = true;
    r->next_pack_id = 1;
    for (int i = 0; i < r->map.pack_count && r->enemy_count < POPULATION_CAP; i++) {
        const PackPoint *pp = &r->map.packs[i];
        Tier lead = roll_tier(&rng, act, pp->side_path);
        if (pp->side_path && uniques_left > 0 && rng_chance(&rng, unique_chance)) {
            lead = TIER_UNIQUE;
            uniques_left--;
        }
        spawn_pack(r, &rng, &pool, pp->pos, lead, density, (int)r->next_pack_id++, pp->side_path);
    }
    r->populating = false;
}

/* ======================================================================== */
/* Anlegen / Neustart                                                        */
/* ======================================================================== */
static void reset_world_state(Run *r, Game *g) {
    r->enemy_count = 0;
    memset(r->enemies, 0, sizeof r->enemies);
    memset(r->proj, 0, sizeof r->proj);
    memset(r->zones, 0, sizeof r->zones);
    memset(r->totems, 0, sizeof r->totems);
    memset(r->pickups, 0, sizeof r->pickups);
    r->state = RUN_ACTIVE;
    r->time = 0.0f;
    r->clear_timer = 0.0f;
    r->boss_spawned = r->boss_dead = false;
    r->boss_enemy = -1;
    r->enrage_t = 0.0f;
    r->kills = 0;
    r->alive_count = 0;
    r->total_spawned = 0;
    r->arena_sealed = false;
    r->main_spawned = r->main_killed = 0;
    r->boss_reset_t = 0.0f;
    r->clear_time = 0.0f;
    r->hero_dead_count = 0;
    r->clear_reported = false;
    r->exit_hold = 0.0f;
    r->pickups_taken = 0;
    populate(r, g);
}

static void spawn_heroes(Run *r, Game *g) {
    for (int s = 0; s < r->hero_count; s++) {
        RunHero *h = &r->heroes[s];
        int idx = h->hero_idx;
        bool manual = h->manual;
        if (g->heroes[idx].dead_timer > 0.0f) { /* noch gefallen: bleibt bis zum Wiedereintritt tot */
            memset(h, 0, sizeof *h);
            h->hero_idx = idx;
            h->manual = manual;
            h->state = HS_DEAD;
            h->target = -1;
            h->manual_enemy = -1;
            h->hunt_enemy = -1;
            h->u.radius = HERO_RADIUS;
            h->u.pos = r->map.start;
            r->hero_dead_count++;
            continue;
        }
        hero_spawn_at_start(r, g, s);
    }
}

static void seed_run_rng(Run *r, Game *g) {
    u64 s = (u64)r->def->seed ^ ((u64)rng_u32(&g->rng) << 32) ^ (u64)rng_u32(&g->rng);
    rng_seed(&r->rng, s, (u64)r->map_idx * 2u + 1u);
}

Run *run_create(Game *g, int map_idx, const int *hero_idx, int hero_count) {
    if (!g || map_idx < 0 || map_idx >= g_map_count || !hero_idx || hero_count < 1 || hero_count > MAX_TEAM) {
        return NULL;
    }
    const MapDef *def = &g_maps[map_idx];
    if (hero_count > maxi(def->max_heroes, 1)) {
        return NULL;
    }
    for (int i = 0; i < hero_count; i++) {
        if (hero_idx[i] < 0 || hero_idx[i] >= MAX_HEROES || !g->heroes[hero_idx[i]].used) {
            return NULL;
        }
    }
    Run *r = (Run *)calloc(1, sizeof *r);
    if (!r) {
        return NULL;
    }
    r->map_idx = map_idx;
    r->def = def;
    if (!world_build(def, map_idx, &r->map)) {
        free(r);
        return NULL;
    }
    r->hero_count = hero_count;
    for (int i = 0; i < hero_count; i++) {
        r->heroes[i].hero_idx = hero_idx[i];
    }
    seed_run_rng(r, g);
    reset_world_state(r, g);
    spawn_heroes(r, g);
    return r;
}

void run_destroy(Run *r) {
    if (!r) {
        return;
    }
    world_free(&r->map);
    free(r);
}

void run_restart(Run *r, Game *g) {
    r->runs_completed++;
    seed_run_rng(r, g);
    run_clear_events(r);
    reset_world_state(r, g);
    spawn_heroes(r, g);
}

/* ======================================================================== */
/* Tick                                                                      */
/* ======================================================================== */
static void age_pickups(Run *r) {
    for (int i = 0; i < MAX_PICKUPS; i++) {
        if (r->pickups[i].active) {
            r->pickups[i].age += SIM_DT;
        }
    }
}

static void check_boss_spawn(Run *r, Game *g) {
    if (r->boss_spawned || !r->def || r->def->boss < 0) {
        return;
    }
    for (int s = 0; s < r->hero_count; s++) {
        const RunHero *h = &r->heroes[s];
        if (h->u.alive && run_in_arena(r, h->u.pos)) {
            boss_spawn(r, g);
            return;
        }
    }
}

static void finish_run(Run *r, Game *g) {
    int team[MAX_TEAM];
    for (int i = 0; i < r->hero_count; i++) {
        team[i] = r->heroes[i].hero_idx;
        RunHero *h = &r->heroes[i];
        if (h->u.alive) {
            h->state = HS_MAP_CLEAR;
            h->target = -1;
            h->clear_wait = 0.0f;
        }
    }
    r->state = RUN_CLEARED;
    r->clear_time = r->time;
    r->arena_sealed = false;
    if (!r->clear_reported) {
        r->clear_reported = true;
        game_on_map_clear(g, r->map_idx, team, r->hero_count, r->clear_time);
        sim_emit(r, EV_MAP_CLEAR, r->heroes[0].u.pos, r->clear_time, 0, 0, 0, false, 0);
    }
}

static bool exit_reached(const Run *r) {
    int alive = 0;
    for (int i = 0; i < r->hero_count; i++) {
        if (r->heroes[i].u.alive) {
            alive++;
            if (r->heroes[i].state != HS_MAP_CLEAR && r->heroes[i].state != HS_RETRY_WAIT) {
                return false;
            }
        }
    }
    return alive > 0;
}

static void check_clear(Run *r, Game *g) {
    if (r->state != RUN_ACTIVE) {
        return;
    }
    bool boss_map = r->def->boss >= 0;
    if ((boss_map && r->boss_dead) || (!boss_map && exit_reached(r))) {
        finish_run(r, g);
    }
}

static void tick_enemies(Run *r, Game *g) {
    enemy_ai_prepare(r);
    int n = r->active_n;
    int list[MAX_ENEMIES];
    memcpy(list, r->active_list, (size_t)n * sizeof list[0]);
    for (int k = 0; k < n; k++) {
        enemy_ai_update(r, g, list[k]);
    }
    enemy_separate_all(r);
}

void run_tick(Run *r, Game *g) {
    if (!r || !g) {
        return;
    }
    if (r->state == RUN_RESTARTING) {
        run_restart(r, g);
        return;
    }
    r->tick++;
    if (r->state == RUN_ACTIVE) {
        r->time += SIM_DT;
    }
    for (int s = 0; s < r->hero_count; s++) {
        hero_ai_update(r, g, s);
        if (r->state == RUN_RESTARTING) {
            return;
        }
    }
    check_boss_spawn(r, g);
    tick_enemies(r, g);
    combat_update_projectiles(r, g);
    combat_update_zones(r, g);
    combat_update_totems(r, g);
    if (r->state == RUN_ACTIVE) {
        enemy_reset_boss(r, g);
    }
    age_pickups(r);
    check_clear(r, g);
}
