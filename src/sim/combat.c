/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Kampfregeln der Simulation: Statuseffekte, Schaden (Ruestung, Krit, %mehr),
 * Heilung, Projektile, Zonen (Telegraphen, Pfuetzen, Bodenflaechen) und Totems (Spec 03, 15, 17). */
#include <math.h>
#include <string.h>

#include "sim/sim_internal.h"

#define DOT_PULSE_TICKS 15   /* DoT/HoT werden alle 0,5 s als Paket verrechnet */
#define HOMING_TURN_RATE 7.0f
#define POOL_TICK 0.5f

/* ======================================================================== */
/* Statuseffekte                                                             */
/* ======================================================================== */
Status status_make(int type, int stat, int skill, float value, float time, int source, bool spell) {
    Status s;
    memset(&s, 0, sizeof s);
    s.type = (u8)type;
    s.stat = (i8)stat;
    s.skill = (i16)skill;
    s.value = value;
    s.time = time;
    s.total = time;
    s.source = (i8)source;
    s.spell = spell;
    return s;
}

Status *status_find(Unit *u, int type, int skill, int stat) {
    for (int i = 0; i < u->st_count; i++) {
        Status *s = &u->st[i];
        if (s->type == type && s->skill == skill && s->stat == stat) {
            return s;
        }
    }
    return NULL;
}

bool status_apply(Unit *u, Status s) {
    Status *f = status_find(u, s.type, s.skill, s.stat);
    if (f) {
        bool changed = fabsf(f->value - s.value) > 1e-4f;
        *f = s;
        return changed;
    }
    if (u->st_count < MAX_STATUS) {
        u->st[u->st_count++] = s;
        return true;
    }
    int low = 0;
    for (int i = 1; i < u->st_count; i++) {
        if (u->st[i].time < u->st[low].time) {
            low = i;
        }
    }
    u->st[low] = s;
    return true;
}

bool status_expire(Unit *u, float dt) {
    bool changed = false;
    int w = 0;
    for (int i = 0; i < u->st_count; i++) {
        Status s = u->st[i];
        s.time -= dt;
        if (s.time > 0.0f) {
            u->st[w++] = s;
        } else if (s.type == ST_BUFF) {
            changed = true;
        }
    }
    u->st_count = w;
    return changed;
}

float status_max(const Unit *u, int type) {
    float m = 0.0f;
    for (int i = 0; i < u->st_count; i++) {
        if (u->st[i].type == type && u->st[i].value > m) {
            m = u->st[i].value;
        }
    }
    return m;
}

float status_stat_sum(const Unit *u, int type, int stat) {
    float sum = 0.0f;
    for (int i = 0; i < u->st_count; i++) {
        if (u->st[i].type == type && u->st[i].stat == stat) {
            sum += u->st[i].value;
        }
    }
    return sum;
}

bool unit_stunned(const Unit *u) {
    for (int i = 0; i < u->st_count; i++) {
        if (u->st[i].type == ST_STUN) {
            return true;
        }
    }
    return false;
}

void status_remove_type(Unit *u, int type) {
    int w = 0;
    for (int i = 0; i < u->st_count; i++) {
        if (u->st[i].type != type) {
            u->st[w++] = u->st[i];
        }
    }
    u->st_count = w;
}

/* ======================================================================== */
/* Schaden und Heilung                                                       */
/* ======================================================================== */
float hero_power(const RunHero *h, bool spell) {
    return spell ? h->eff.spell_power * h->eff.spell_mult : h->eff.attack_power * h->eff.atk_mult;
}

bool hero_roll_crit(Run *r, const RunHero *h) {
    return rng_chance(&r->rng, (double)h->eff.crit_chance / 100.0);
}

static int hero_level(const Run *r, const Game *g, int slot) {
    if (slot < 0 || slot >= r->hero_count) {
        return r->def ? r->def->level : 1;
    }
    return g->heroes[r->heroes[slot].hero_idx].level;
}

float combat_hit_enemy(Run *r, Game *g, int ei, const Hit *h) {
    Enemy *e = &r->enemies[ei];
    if (!e->u.alive || e->invuln > 0.0f || e->state == ES_DEAD) {
        return 0.0f;
    }
    float dmg = h->amount;
    bool crit = false;
    if (!h->nocrit && h->slot >= 0 && h->slot < r->hero_count) {
        const RunHero *hero = &r->heroes[h->slot];
        if (hero_roll_crit(r, hero)) {
            crit = true;
            dmg *= hero->eff.crit_dmg / 100.0f;
        }
    }
    float armor = h->spell ? e->spell_res : e->armor;
    dmg *= 1.0f - armor_reduction(armor, hero_level(r, g, h->slot));
    dmg *= 1.0f + status_stat_sum(&e->u, ST_DEBUFF, STAT_ENEMY_TAKEN) / 100.0f;
    dmg = maxf(dmg, 1.0f);
    if (e->shield_hp > 0.0f) {
        float absorbed = minf(e->shield_hp, dmg);
        e->shield_hp -= absorbed;
        dmg -= absorbed;
        if (e->shield_hp <= 0.0f) {
            e->shielded = false;
        }
    }
    if (dmg > 0.0f) {
        e->u.hp -= dmg;
        e->u.hit_flash = 1.0f;
    }
    if (h->slot >= 0 && h->slot < MAX_TEAM) {
        e->threat[h->slot] += h->amount;
    }
    if (e->state == ES_DORMANT || e->state == ES_IDLE) {
        enemy_aggro(r, ei, h->slot);
    }
    if (!h->dot || crit || (r->tick % DOT_PULSE_TICKS) == 0) {
        sim_emit(r, EV_DAMAGE, e->u.pos, dmg, 0, h->dot ? 1 : 0, (u8)e->tier, crit, 0);
    }
    if (e->u.hp <= 0.0f) {
        enemy_kill(r, g, ei, h->slot >= 0 && h->slot < r->hero_count ? h->slot : -1);
    }
    return dmg;
}

void combat_hit_hero(Run *r, Game *g, int slot, float raw, bool spell, int src_enemy) {
    RunHero *h = &r->heroes[slot];
    if (!h->u.alive || h->state == HS_DEAD || h->state == HS_MAP_CLEAR) {
        return;
    }
    int lv = src_enemy >= 0 ? r->enemies[src_enemy].level : (r->def ? r->def->level : 1);
    float red = armor_reduction(spell ? h->eff.spell_res : h->eff.armor, lv);
    float dmg = raw * (1.0f - red);
    dmg -= spell ? h->eff.dr_spell : h->eff.dr_attack;
    dmg = maxf(dmg, raw * 0.05f);
    dmg *= spell ? h->eff.spell_taken_mult : h->eff.physical_taken_mult;
    dmg = maxf(dmg, 1.0f);
    h->u.hp -= dmg;
    h->u.hit_flash = 1.0f;
    h->last_hit_t = 0.0f;
    h->combat_t = 0.0f;
    sim_emit(r, EV_DAMAGE, h->u.pos, dmg, 1, 0, 0, false, 0);
    if (src_enemy >= 0 && (r->enemies[src_enemy].affixes & AFFIX_VAMPIRIC)) {
        Enemy *e = &r->enemies[src_enemy];
        e->u.hp = minf(e->u.hp + dmg * 0.2f, e->u.max_hp);
    }
    if (h->u.hp <= 0.0f) {
        hero_die(r, g, slot);
    }
}

void combat_heal_hero(Run *r, int slot, float amount) {
    RunHero *h = &r->heroes[slot];
    if (!h->u.alive || amount <= 0.0f) {
        return;
    }
    float before = h->u.hp;
    h->u.hp = minf(h->u.hp + amount, h->u.max_hp);
    if (h->u.hp > before) {
        sim_emit(r, EV_HEAL, h->u.pos, h->u.hp - before, 0, 0, 0, false, 0);
    }
}

/* ======================================================================== */
/* Formtests                                                                 */
/* ======================================================================== */
static float seg_point_dist2(Vec2 a, Vec2 b, Vec2 p) {
    Vec2 ab = v2_sub(b, a);
    float len2 = v2_dot(ab, ab);
    float t = len2 > 1e-6f ? clampf(v2_dot(v2_sub(p, a), ab) / len2, 0.0f, 1.0f) : 0.0f;
    return v2_dist2(v2_add(a, v2_scale(ab, t)), p);
}

static bool zone_contains(const Zone *z, Vec2 p, float unit_radius) {
    float d = v2_dist(z->pos, p);
    switch (z->shape) {
    case SHAPE_CIRCLE:
        return d <= z->radius + unit_radius * 0.5f;
    case SHAPE_CONE: {
        if (d > z->radius + unit_radius) {
            return false;
        }
        if (d < unit_radius) {
            return true;
        }
        Vec2 to = v2_norm(v2_sub(p, z->pos));
        float c = clampf(v2_dot(to, z->dir), -1.0f, 1.0f);
        return acosf(c) <= z->width_or_angle * 0.5f * PNX_PI / 180.0f;
    }
    case SHAPE_LINE: {
        Vec2 end = v2_add(z->pos, v2_scale(z->dir, z->radius));
        float hw = z->width_or_angle * 0.5f + unit_radius * 0.5f;
        return seg_point_dist2(z->pos, end, p) <= hw * hw;
    }
    }
    return false;
}

/* ======================================================================== */
/* Projektile                                                                */
/* ======================================================================== */
Projectile *proj_alloc(Run *r) {
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!r->proj[i].active) {
            memset(&r->proj[i], 0, sizeof r->proj[i]);
            r->proj[i].active = true;
            r->proj[i].homing_target = -1;
            return &r->proj[i];
        }
    }
    return NULL;
}

static void apply_hit_flags(Run *r, int ei, const Projectile *p) {
    Enemy *e = &r->enemies[ei];
    if (!e->u.alive) {
        return;
    }
    if (p->flags & SKF_SLOW) {
        float dur = p->dot_dur > 0.0f && !(p->flags & SKF_DOT) ? p->dot_dur : 2.0f;
        status_apply(&e->u, status_make(ST_SLOW, -1, p->skill, 40.0f, dur, p->owner, false));
    }
    if ((p->flags & SKF_STUN) && e->u.alive && !e->is_boss && g_monsters[e->def].arch != ARCH_GUARDIAN) {
        status_apply(&e->u, status_make(ST_STUN, -1, p->skill, 1.0f, 1.2f, p->owner, false));
    }
    if ((p->flags & SKF_DOT) && p->dot_dps > 0.0f) {
        status_apply(&e->u, status_make(ST_DOT, -1, p->skill, p->dot_dps, p->dot_dur, p->owner, p->spell));
    }
}

static void projectile_explode(Run *r, Game *g, const Projectile *p, Vec2 at) {
    sim_emit(r, EV_EXPLOSION, at, p->explode_radius, (int)p->color, 0, 0, false, p->color);
    for (int i = 0; i < r->enemy_count; i++) {
        Enemy *e = &r->enemies[i];
        if (!e->u.alive || v2_dist(e->u.pos, at) > p->explode_radius + e->u.radius * 0.5f) {
            continue;
        }
        Hit h = {p->owner, p->damage, p->spell, false, false, p->skill};
        combat_hit_enemy(r, g, i, &h);
        apply_hit_flags(r, i, p);
    }
}

static int proj_hit_count(const Projectile *p, int ei) {
    for (int i = 0; i < p->hit_n && i < 8; i++) {
        if (p->hit_ids[i] == (u16)ei) {
            return p->hit_cnt[i];
        }
    }
    return 0;
}

static void proj_note_hit(Projectile *p, int ei) {
    for (int i = 0; i < p->hit_n && i < 8; i++) {
        if (p->hit_ids[i] == (u16)ei) {
            p->hit_cnt[i]++;
            return;
        }
    }
    int slot = p->hit_n < 8 ? p->hit_n++ : 0;
    p->hit_ids[slot] = (u16)ei;
    p->hit_cnt[slot] = 1;
}

static int nearest_enemy_to(const Run *r, Vec2 p, float max_d) {
    int best = -1;
    float bd = max_d * max_d;
    for (int i = 0; i < r->enemy_count; i++) {
        if (!r->enemies[i].u.alive) {
            continue;
        }
        float d = v2_dist2(r->enemies[i].u.pos, p);
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

static void steer_homing(Run *r, Projectile *p) {
    if (p->homing_target < 0 || !r->enemies[p->homing_target].u.alive) {
        p->homing_target = nearest_enemy_to(r, p->pos, 450.0f);
    }
    if (p->homing_target < 0) {
        return;
    }
    float speed = v2_len(p->vel);
    Vec2 want = v2_norm(v2_sub(r->enemies[p->homing_target].u.pos, p->pos));
    Vec2 cur = v2_norm(p->vel);
    float cross = cur.x * want.y - cur.y * want.x;
    float dot = clampf(v2_dot(cur, want), -1.0f, 1.0f);
    float ang = acosf(dot);
    float turn = minf(ang, HOMING_TURN_RATE * SIM_DT);
    Vec2 nd = v2_rotate(cur, cross >= 0.0f ? turn : -turn);
    p->vel = v2_scale(nd, speed);
}

static void hero_projectile_step(Run *r, Game *g, Projectile *p) {
    Vec2 from = p->pos;
    Vec2 to = v2_add(p->pos, v2_scale(p->vel, SIM_DT));
    if (world_tile_at_pos(&r->map, to) == TILE_WALL) {
        if (p->explode_radius > 0.0f) {
            projectile_explode(r, g, p, from);
        }
        p->active = false;
        return;
    }
    for (int i = 0; i < r->enemy_count && p->active; i++) {
        Enemy *e = &r->enemies[i];
        if (!e->u.alive || e->invuln > 0.0f) {
            continue;
        }
        float rr = e->u.radius + p->radius;
        if (seg_point_dist2(from, to, e->u.pos) > rr * rr) {
            continue;
        }
        if (p->hits_per_target > 0 && proj_hit_count(p, i) >= p->hits_per_target) {
            continue;
        }
        proj_note_hit(p, i);
        if (p->explode_radius > 0.0f) {
            projectile_explode(r, g, p, e->u.pos);
            p->active = false;
            return;
        }
        Hit h = {p->owner, p->damage, p->spell, false, false, p->skill};
        combat_hit_enemy(r, g, i, &h);
        apply_hit_flags(r, i, p);
        sim_emit(r, EV_HIT_FX, e->u.pos, 0, 0, 0, p->style, false, p->color);
        if (--p->pierce_left < 0) {
            p->active = false;
        }
    }
    p->pos = to;
}

static void enemy_projectile_step(Run *r, Game *g, Projectile *p) {
    Vec2 from = p->pos;
    Vec2 to = v2_add(p->pos, v2_scale(p->vel, SIM_DT));
    if (world_tile_at_pos(&r->map, to) == TILE_WALL) {
        p->active = false;
        return;
    }
    for (int s = 0; s < r->hero_count; s++) {
        RunHero *h = &r->heroes[s];
        if (!h->u.alive) {
            continue;
        }
        float rr = h->u.radius + p->radius;
        if (seg_point_dist2(from, to, h->u.pos) <= rr * rr) {
            combat_hit_hero(r, g, s, p->damage, p->spell, p->owner);
            p->active = false;
            return;
        }
    }
    p->pos = to;
}

void combat_update_projectiles(Run *r, Game *g) {
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        Projectile *p = &r->proj[i];
        if (!p->active) {
            continue;
        }
        p->life -= SIM_DT;
        if (p->life <= 0.0f) {
            if (p->kind == PK_HERO && p->explode_radius > 0.0f) {
                projectile_explode(r, g, p, p->pos);
            }
            p->active = false;
            continue;
        }
        if (p->kind == PK_HERO) {
            if (p->homing) {
                steer_homing(r, p);
            }
            hero_projectile_step(r, g, p);
        } else {
            enemy_projectile_step(r, g, p);
        }
    }
}

/* ======================================================================== */
/* Zonen                                                                     */
/* ======================================================================== */
static Zone *zone_alloc(Run *r) {
    for (int i = 0; i < MAX_ZONES; i++) {
        if (!r->zones[i].active) {
            memset(&r->zones[i], 0, sizeof r->zones[i]);
            r->zones[i].active = true;
            return &r->zones[i];
        }
    }
    return NULL;
}

void zone_spawn_enemy_tele(Run *r, int owner, ZoneShape shape, Vec2 pos, Vec2 dir, float radius, float width_angle,
                           float delay, float damage, bool spell, u32 color) {
    Zone *z = zone_alloc(r);
    if (!z) {
        return;
    }
    z->kind = ZONE_ENEMY_TELE;
    z->shape = shape;
    z->pos = pos;
    z->dir = dir;
    z->radius = radius;
    z->width_or_angle = width_angle;
    z->delay = z->delay_total = delay;
    z->damage = damage;
    z->spell = spell;
    z->owner = owner;
    z->skill = -1;
    z->color = color;
}

void zone_spawn_pool(Run *r, int owner, Vec2 pos, float radius, float life, float dps, bool spell, u32 color) {
    Zone *z = zone_alloc(r);
    if (!z) {
        return;
    }
    z->kind = ZONE_ENEMY_POOL;
    z->shape = SHAPE_CIRCLE;
    z->pos = pos;
    z->radius = radius;
    z->life = life;
    z->tick = POOL_TICK;
    z->tick_t = POOL_TICK;
    z->damage = dps * POOL_TICK;
    z->spell = spell;
    z->owner = owner;
    z->skill = -1;
    z->color = color;
}

static void zone_hit_heroes(Run *r, Game *g, const Zone *z) {
    for (int s = 0; s < r->hero_count; s++) {
        const RunHero *h = &r->heroes[s];
        if (h->u.alive && zone_contains(z, h->u.pos, h->u.radius)) {
            combat_hit_hero(r, g, s, z->damage, z->spell, z->owner);
        }
    }
}

static void zone_hit_enemies(Run *r, Game *g, const Zone *z, bool dot) {
    for (int i = 0; i < r->enemy_count; i++) {
        Enemy *e = &r->enemies[i];
        if (!e->u.alive || !zone_contains(z, e->u.pos, e->u.radius)) {
            continue;
        }
        Hit h = {z->owner, z->damage, z->spell, dot, dot, z->skill};
        combat_hit_enemy(r, g, i, &h);
        if (e->u.alive && (z->flags & SKF_SLOW)) {
            status_apply(&e->u, status_make(ST_SLOW, -1, z->skill, 35.0f, 1.0f, z->owner, false));
        }
    }
}

static void update_one_zone(Run *r, Game *g, Zone *z) {
    switch (z->kind) {
    case ZONE_ENEMY_TELE:
        z->delay -= SIM_DT;
        if (z->delay <= 0.0f) {
            zone_hit_heroes(r, g, z);
            sim_emit(r, EV_EXPLOSION, z->pos, z->radius, (int)z->color, 0, 0, false, z->color);
            z->active = false;
        }
        break;
    case ZONE_ENEMY_POOL:
        z->life -= SIM_DT;
        z->tick_t -= SIM_DT;
        if (z->tick_t <= 0.0f) {
            z->tick_t += z->tick;
            zone_hit_heroes(r, g, z);
        }
        if (z->life <= 0.0f) {
            z->active = false;
        }
        break;
    case ZONE_HERO_AREA:
        if (!z->fired) {
            z->delay -= SIM_DT;
            if (z->delay <= 0.0f) {
                z->fired = true;
                if (z->damage > 0.0f && !(z->flags & SKF_DOT)) {
                    zone_hit_enemies(r, g, z, false);
                }
                sim_emit(r, EV_EXPLOSION, z->pos, z->radius, (int)z->color, 0, 0, false, z->color);
                if (z->life <= 0.0f) {
                    z->active = false;
                }
            }
        } else {
            z->life -= SIM_DT;
            z->tick_t -= SIM_DT;
            if (z->tick_t <= 0.0f) {
                z->tick_t += z->tick;
                zone_hit_enemies(r, g, z, (z->flags & SKF_DOT) != 0);
            }
            if (z->life <= 0.0f) {
                z->active = false;
            }
        }
        break;
    case ZONE_POISON_CLOUD:
        z->life -= SIM_DT;
        z->tick_t -= SIM_DT;
        if (z->tick_t <= 0.0f) {
            z->tick_t += z->tick;
            zone_hit_enemies(r, g, z, true);
        }
        if (z->life <= 0.0f) {
            z->active = false;
        }
        break;
    case ZONE_HEAL_SPRING:
        z->active = false;
        break;
    }
}

void combat_update_zones(Run *r, Game *g) {
    for (int i = 0; i < MAX_ZONES; i++) {
        if (r->zones[i].active) {
            update_one_zone(r, g, &r->zones[i]);
        }
    }
}

/* ======================================================================== */
/* Totems                                                                    */
/* ======================================================================== */
static void totem_buff_heroes(Run *r, const Game *g, const Totem *t, int stat, float value, int stat2, float value2) {
    for (int s = 0; s < r->hero_count; s++) {
        RunHero *h = &r->heroes[s];
        if (!h->u.alive || v2_dist(h->u.pos, t->pos) > t->radius) {
            continue;
        }
        bool dirty = status_apply(&h->u, status_make(ST_BUFF, stat, t->skill, value, 0.8f, t->owner, false));
        if (stat2 >= 0) {
            dirty |= status_apply(&h->u, status_make(ST_BUFF, stat2, t->skill, value2, 0.8f, t->owner, false));
        }
        if (dirty) {
            h->eff_dirty = true;
        }
    }
    (void)g;
}

static void totem_damage_area(Run *r, Game *g, const Totem *t, Vec2 at, float radius, bool single) {
    for (int i = 0; i < r->enemy_count; i++) {
        Enemy *e = &r->enemies[i];
        if (!e->u.alive || v2_dist(e->u.pos, at) > radius + e->u.radius * 0.5f) {
            continue;
        }
        Hit h = {t->owner, t->dmg, true, false, false, t->skill};
        combat_hit_enemy(r, g, i, &h);
        if (single) {
            return;
        }
    }
}

static int random_enemy_in(Run *r, Vec2 c, float radius) {
    int cand[64], n = 0;
    for (int i = 0; i < r->enemy_count && n < 64; i++) {
        if (r->enemies[i].u.alive && v2_dist(r->enemies[i].u.pos, c) <= radius) {
            cand[n++] = i;
        }
    }
    return n > 0 ? cand[rng_range(&r->rng, 0, n - 1)] : -1;
}

static void totem_tick(Run *r, Game *g, Totem *t) {
    const SkillDef *s = &g_skills[t->skill];
    switch (t->type) {
    case TOTEM_TURTLE:
        totem_buff_heroes(r, g, t, s->stat >= 0 ? s->stat : STAT_ARMOR_PCT, skill_stat_value(s, t->skill_level, false), -1, 0);
        for (int sl = 0; sl < r->hero_count; sl++) {
            RunHero *h = &r->heroes[sl];
            if (h->u.alive && sl == t->owner && v2_dist(h->u.pos, t->pos) <= t->radius) {
                status_apply(&h->u, status_make(ST_TAUNT, -1, t->skill, 1.0f, 0.8f, t->owner, false));
            }
        }
        break;
    case TOTEM_STORM_DRAGON: {
        t->tick_t -= 0.5f;
        if (t->tick_t <= 0.0f) {
            t->tick_t += 1.2f;
            int e = random_enemy_in(r, t->pos, t->radius);
            if (e >= 0) {
                Hit h = {t->owner, t->dmg, true, false, false, t->skill};
                combat_hit_enemy(r, g, e, &h);
                sim_emit(r, EV_HIT_FX, r->enemies[e].u.pos, 0, 0, 0, 4, false, 0x9FD0FF);
            }
        }
        break;
    }
    case TOTEM_RED_DRAGON:
        t->tick_t -= 0.5f;
        if (t->tick_t <= 0.0f) {
            t->tick_t += 1.5f;
            totem_damage_area(r, g, t, t->pos, t->radius * 0.7f, false);
            sim_emit(r, EV_EXPLOSION, t->pos, t->radius * 0.7f, 0xE8541E, 0, 0, false, 0xE8541E);
        }
        totem_buff_heroes(r, g, t, s->stat >= 0 ? s->stat : STAT_MORE_ATTACK, skill_stat_value(s, t->skill_level, false),
                          s->stat2, skill_stat_value(s, t->skill_level, true));
        break;
    case TOTEM_WAR_DRUM:
        t->tick_t -= 0.5f;
        if (t->tick_t <= 0.0f) {
            t->tick_t += 1.0f;
            totem_damage_area(r, g, t, t->pos, t->radius, false);
            sim_emit(r, EV_EXPLOSION, t->pos, t->radius, 0xD8B84A, 0, 0, false, 0xD8B84A);
        }
        break;
    case TOTEM_CLAIRVOYANCE:
    case TOTEM_GALE_HAWK:
        totem_buff_heroes(r, g, t, s->stat >= 0 ? s->stat : STAT_CRIT_DMG, skill_stat_value(s, t->skill_level, false),
                          s->stat2, skill_stat_value(s, t->skill_level, true));
        break;
    default:
        break;
    }
}

void totem_expire(Run *r, Game *g, int ti) {
    (void)g;
    r->totems[ti].active = false;
}

void combat_update_totems(Run *r, Game *g) {
    for (int i = 0; i < MAX_TOTEMS; i++) {
        Totem *t = &r->totems[i];
        if (!t->active) {
            continue;
        }
        t->life -= SIM_DT;
        if (t->life <= 0.0f) {
            totem_expire(r, g, i);
            continue;
        }
        if (((r->tick + (u32)i) % (SIM_HZ / 2)) == 0) { /* alle 0,5 s */
            totem_tick(r, g, t);
        }
    }
}
