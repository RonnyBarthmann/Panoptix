/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Ausfuehrung aller Skill-Arten und des Basisangriffs der Helden (Spec 05, 06, 16).
 * Die Skills sind datengetrieben: SkillKind + SKF_*-Flags beschreiben das Verhalten,
 * es gibt keine Einzelfall-Skripte pro Skill. */
#include <math.h>
#include <string.h>

#include "sim/sim_internal.h"

#define DASH_STEP 8.0f
#define MIN_CAST_TIME 0.12f
#define GCD_AFTER_CAST 0.15f
#define RANGE_SLACK 10.0f
#define DOT_DIRECT_SHARE 0.2f      /* DoT-Skills: Anteil des Schadens als Sofort-Treffer */
#define HEAL_SELF_PCT 8.0f         /* SKF_HEAL_SELF: % Max-HP pro Wirkung */
#define MAX_PROJ_PER_CAST 12
#define HERO_PROJ_RADIUS 9.0f
#define TICK_POISON 0.5f
#define BASIC_PROJ_SPREAD 9.0f
#define CHAIN_RADIUS 250.0f

/* ======================================================================== */
/* Kleine Helfer                                                             */
/* ======================================================================== */
static bool enemy_ok(const Run *r, int ei) {
    return ei >= 0 && ei < r->enemy_count && r->enemies[ei].u.alive;
}

int sim_nearest_enemy(const Run *r, Vec2 p, float max_d) {
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

int sim_count_enemies(const Run *r, Vec2 p, float radius) {
    int n = 0;
    for (int i = 0; i < r->enemy_count; i++) {
        const Enemy *e = &r->enemies[i];
        if (e->u.alive && v2_dist(e->u.pos, p) <= radius + e->u.radius * 0.5f) {
            n++;
        }
    }
    return n;
}

Vec2 sim_dash_dest(const Run *r, Vec2 from, Vec2 dir, float dist, bool teleport, float radius) {
    if (teleport) {
        return world_nearest_walkable(&r->map, v2_add(from, v2_scale(dir, dist)), radius);
    }
    Vec2 p = from;
    float walked = 0.0f;
    while (walked < dist) {
        float st = minf(DASH_STEP, dist - walked);
        Vec2 np = v2_add(p, v2_scale(dir, st));
        if (!world_walkable(&r->map, np, radius)) {
            break;
        }
        p = np;
        walked += st;
    }
    return p;
}

int skill_level_of(const Game *g, int skill) {
    if (skill < 0 || skill >= g_skill_count) {
        return 1;
    }
    int lv = g->player.skill_level[skill];
    return lv < 1 ? 1 : lv;
}

float skill_cooldown_of(const RunHero *h, const SkillDef *s) {
    float cd = s->cooldown * (1.0f - h->eff.cdr / 100.0f);
    return maxf(cd, (float)g_bal.cd_floor_s);
}

float skill_mana_cost(const RunHero *h, const SkillDef *s) {
    return maxf(s->mana * (1.0f + h->mana_cost_pct / 100.0f), 0.0f);
}

/* Verzauberung der Sekundaerwaffe fuer genau diesen Skill (Spec 12): Summe des Wertes. */
static float ench_bonus(const Hero *hh, int skill, EnchantOpt opt) {
    const Item *it = &hh->eq[SLOT_SUB];
    if (item_is_empty(it) || it->ench_opt != (u8)opt || it->ench_skill != skill) {
        return 0.0f;
    }
    return it->ench_val;
}

bool skill_ready(const Run *r, const Game *g, int slot, int skill_slot) {
    if (slot < 0 || slot >= r->hero_count || skill_slot < 0 || skill_slot >= SKILL_SLOTS) {
        return false;
    }
    const RunHero *h = &r->heroes[slot];
    int sk = g->heroes[h->hero_idx].skill_slot[skill_slot];
    if (!h->u.alive || sk < 0 || sk >= g_skill_count) {
        return false;
    }
    return h->cd[skill_slot] <= 0.0f && h->mana + 0.001f >= skill_mana_cost(h, &g_skills[sk]);
}

static float hit_damage(const RunHero *h, const Game *g, const SkillDef *s, int sk) {
    bool spell = (s->flags & SKF_SPELL) != 0;
    float base = hero_power(h, spell) * skill_dmg_pct(s, skill_level_of(g, sk)) / 100.0f;
    return base * (1.0f + ench_bonus(&g->heroes[h->hero_idx], sk, ENCH_SKILL_DMG) / 100.0f);
}

static float heal_base(const RunHero *h) {
    return maxf(hero_power(h, true), hero_power(h, false));
}

static float skill_dur(const Game *g, const RunHero *h, const SkillDef *s, int sk) {
    return s->dur * (1.0f + ench_bonus(&g->heroes[h->hero_idx], sk, ENCH_SKILL_DUR) / 100.0f);
}

static u32 class_color(const Game *g, const RunHero *h) {
    return g_classes[clampi(g->heroes[h->hero_idx].cls, 0, CLASS_COUNT - 1)].color;
}

static u8 class_style(const Game *g, const RunHero *h) {
    switch (g->heroes[h->hero_idx].cls) {
    case CLASS_ARCHER: return 0;
    case CLASS_WIZARD: return 5;
    case CLASS_ASSASSIN: return 2;
    case CLASS_SHAMAN: return 4;
    case CLASS_PRIEST: return 3;
    default: return 1;
    }
}

static void hit_enemy(Run *r, Game *g, int slot, int ei, float amount, bool spell, int sk) {
    Hit hit = {slot, amount, spell, false, false, sk};
    combat_hit_enemy(r, g, ei, &hit);
}

static void apply_dot(Run *r, int ei, int slot, const SkillDef *s, int sk, float total, float dur, bool spell) {
    if (!enemy_ok(r, ei) || dur <= 0.0f) {
        return;
    }
    status_apply(&r->enemies[ei].u, status_make(ST_DOT, -1, sk, total / dur, dur, slot, spell));
    (void)s;
}

/* Verlangsamung, Betaeubung und Gegner-Debuffs eines Skills auf einen getroffenen Gegner. */
static void apply_secondary(Run *r, int ei, int slot, const SkillDef *s, int sk, int lv) {
    if (!enemy_ok(r, ei)) {
        return;
    }
    Enemy *e = &r->enemies[ei];
    bool plain_dur = s->dur > 0.0f && !(s->flags & SKF_DOT);
    if (s->flags & SKF_SLOW) {
        status_apply(&e->u, status_make(ST_SLOW, -1, sk, 40.0f, plain_dur ? s->dur : 2.0f, slot, false));
    }
    if ((s->flags & SKF_STUN) && !e->is_boss && g_monsters[e->def].arch != ARCH_GUARDIAN) {
        status_apply(&e->u, status_make(ST_STUN, -1, sk, 1.0f, plain_dur ? minf(s->dur, 3.0f) : 1.2f, slot, false));
    }
    if (s->dur > 0.0f && s->kind != SK_DEBUFF) {
        if (s->stat >= STAT_ENEMY_TAKEN && s->stat <= STAT_ENEMY_SPELL_DOWN) {
            status_apply(&e->u, status_make(ST_DEBUFF, s->stat, sk, skill_stat_value(s, lv, false), s->dur, slot, false));
        }
    }
}

static void heal_self_flag(Run *r, int slot, const SkillDef *s) {
    if (s->flags & SKF_HEAL_SELF) {
        RunHero *h = &r->heroes[slot];
        combat_heal_hero(r, slot, h->u.max_hp * HEAL_SELF_PCT / 100.0f * h->eff.heal_mult);
    }
}

static bool in_cone(Vec2 origin, Vec2 dir, float reach, float angle_deg, Vec2 p, float unit_radius) {
    float d = v2_dist(origin, p);
    if (d > reach + unit_radius) {
        return false;
    }
    if (d < unit_radius + 8.0f) {
        return true;
    }
    float c = clampf(v2_dot(v2_norm(v2_sub(p, origin)), dir), -1.0f, 1.0f);
    return acosf(c) <= angle_deg * 0.5f * PNX_PI / 180.0f;
}

/* ======================================================================== */
/* Bewegung (Dash/Warp)                                                      */
/* ======================================================================== */
static void move_hero_to(Run *r, RunHero *h, Vec2 dest) {
    h->u.pos = dest;
    h->u.vel = v2(0, 0);
    h->last_safe_pos = dest;
    (void)r;
}

/* Flaechentreffer um 'center' (Dash-Ankunft, Rundumschlag, Ausweichsprung). */
static void area_hit(Run *r, Game *g, int slot, const SkillDef *s, int sk, Vec2 center, float radius, int hits) {
    float dmg = hit_damage(&r->heroes[slot], g, s, sk);
    int lv = skill_level_of(g, sk);
    bool spell = (s->flags & SKF_SPELL) != 0;
    for (int i = 0; i < r->enemy_count; i++) {
        Enemy *e = &r->enemies[i];
        if (!e->u.alive || v2_dist(e->u.pos, center) > radius + e->u.radius * 0.5f) {
            continue;
        }
        for (int n = 0; n < hits && e->u.alive; n++) {
            hit_enemy(r, g, slot, i, dmg, spell, sk);
        }
        apply_secondary(r, i, slot, s, sk, lv);
    }
    sim_emit(r, EV_EXPLOSION, center, radius, (int)class_color(g, &r->heroes[slot]), 0, 0, false, class_color(g, &r->heroes[slot]));
}

static Vec2 away_dir(const Run *r, const RunHero *h, int target) {
    int ref = enemy_ok(r, target) ? target : sim_nearest_enemy(r, h->u.pos, 400.0f);
    if (ref >= 0) {
        return v2_norm(v2_sub(h->u.pos, r->enemies[ref].u.pos));
    }
    return v2_scale(h->u.face, -1.0f);
}

static bool exec_dash(Run *r, Game *g, int slot, const SkillDef *s, int sk, int target) {
    RunHero *h = &r->heroes[slot];
    bool tele = (s->flags & SKF_USES_TELEPORT) != 0;
    Vec2 dir = h->u.face;
    float dist = s->range;
    Vec2 origin = h->u.pos;
    if (s->flags & SKF_DASH_FWD) {
        if (enemy_ok(r, target)) {
            const Enemy *e = &r->enemies[target];
            dir = v2_norm(v2_sub(e->u.pos, h->u.pos));
            dist = clampf(v2_dist(e->u.pos, h->u.pos) - h->u.radius - e->u.radius - 10.0f, 0.0f, s->range);
        } else {
            dist = s->range * 0.6f;
        }
    } else if (s->flags & SKF_DASH_BACK) {
        dir = away_dir(r, h, target);
        dist = s->range * 0.9f;
        if (s->dmg_pct > 0.0f && s->radius > 0.0f) {
            area_hit(r, g, slot, s, sk, origin, s->radius, maxi(s->count, 1));
        }
    } else if (s->flags & SKF_DASH_SIDE) {
        Vec2 perp = v2(-h->u.face.y, h->u.face.x);
        dir = rng_chance(&r->rng, 0.5) ? perp : v2_scale(perp, -1.0f);
        dist = s->range;
    }
    move_hero_to(r, h, sim_dash_dest(r, origin, dir, dist, tele, h->u.radius));
    if (s->flags & SKF_DASH_FWD) {
        if (s->dmg_pct > 0.0f && s->radius > 0.0f) {
            area_hit(r, g, slot, s, sk, h->u.pos, s->radius, maxi(s->count, 1));
        }
    }
    if (s->flags & SKF_TAUNT) {
        status_apply(&h->u, status_make(ST_TAUNT, -1, sk, 1.0f, s->dur > 0.0f ? s->dur : 3.0f, slot, false));
    }
    heal_self_flag(r, slot, s);
    return true;
}

static bool exec_warp(Run *r, Game *g, int slot, const SkillDef *s, int sk, int target) {
    RunHero *h = &r->heroes[slot];
    Vec2 dir = away_dir(r, h, target);
    move_hero_to(r, h, sim_dash_dest(r, h->u.pos, dir, s->range, false, h->u.radius));
    float amount = heal_base(h) * skill_dmg_pct(s, skill_level_of(g, sk)) / 100.0f * h->eff.heal_mult;
    combat_heal_hero(r, slot, amount);
    return true;
}

/* ======================================================================== */
/* Treffer-Skills                                                            */
/* ======================================================================== */
static bool target_in_reach(const Run *r, const RunHero *h, int target, float range) {
    if (!enemy_ok(r, target)) {
        return false;
    }
    const Enemy *e = &r->enemies[target];
    return v2_dist(e->u.pos, h->u.pos) - e->u.radius <= range + RANGE_SLACK;
}

static bool exec_strike(Run *r, Game *g, int slot, const SkillDef *s, int sk, int target) {
    RunHero *h = &r->heroes[slot];
    if (!target_in_reach(r, h, target, s->range)) {
        return false;
    }
    int lv = skill_level_of(g, sk);
    bool spell = (s->flags & SKF_SPELL) != 0;
    float dmg = hit_damage(h, g, s, sk);
    int hits = maxi(s->count, 1);
    if (s->flags & SKF_DOT) {
        hit_enemy(r, g, slot, target, dmg * (float)hits * DOT_DIRECT_SHARE, spell, sk);
        apply_dot(r, target, slot, s, sk, dmg * (float)hits * (1.0f - DOT_DIRECT_SHARE), skill_dur(g, h, s, sk), spell);
    } else {
        for (int n = 0; n < hits && enemy_ok(r, target); n++) {
            hit_enemy(r, g, slot, target, dmg, spell, sk);
        }
    }
    if (enemy_ok(r, target)) {
        sim_emit(r, EV_HIT_FX, r->enemies[target].u.pos, 0, 0, 0, class_style(g, h), false, class_color(g, h));
    }
    apply_secondary(r, target, slot, s, sk, lv);
    if (s->flags & SKF_TAUNT) {
        status_apply(&h->u, status_make(ST_TAUNT, -1, sk, 1.0f, s->dur > 0.0f ? s->dur : 3.0f, slot, false));
    }
    heal_self_flag(r, slot, s);
    return true;
}

static bool exec_circle(Run *r, Game *g, int slot, const SkillDef *s, int sk) {
    RunHero *h = &r->heroes[slot];
    area_hit(r, g, slot, s, sk, h->u.pos, s->radius, maxi(s->count, 1));
    heal_self_flag(r, slot, s);
    return true;
}

static bool exec_cone(Run *r, Game *g, int slot, const SkillDef *s, int sk) {
    RunHero *h = &r->heroes[slot];
    float dmg = hit_damage(h, g, s, sk);
    int lv = skill_level_of(g, sk);
    bool spell = (s->flags & SKF_SPELL) != 0;
    bool dot = (s->flags & SKF_DOT) != 0;
    for (int i = 0; i < r->enemy_count; i++) {
        Enemy *e = &r->enemies[i];
        if (!e->u.alive || !in_cone(h->u.pos, h->u.face, s->range, s->angle, e->u.pos, e->u.radius)) {
            continue;
        }
        float direct = dot ? dmg * DOT_DIRECT_SHARE : dmg;
        for (int n = 0; n < maxi(s->count, 1) && e->u.alive; n++) {
            hit_enemy(r, g, slot, i, direct, spell, sk);
        }
        if (dot) {
            apply_dot(r, i, slot, s, sk, dmg * (1.0f - DOT_DIRECT_SHARE), skill_dur(g, h, s, sk), spell);
        }
        apply_secondary(r, i, slot, s, sk, lv);
    }
    sim_emit(r, EV_HIT_FX, v2_add(h->u.pos, v2_scale(h->u.face, s->range * 0.5f)), 0, 0, 0, class_style(g, h), false,
             class_color(g, h));
    return true;
}

static bool exec_projectile(Run *r, Game *g, int slot, const SkillDef *s, int sk, int target) {
    RunHero *h = &r->heroes[slot];
    const Hero *hh = &g->heroes[h->hero_idx];
    if (s->flags & SKF_DASH_FWD) {
        float want = enemy_ok(r, target) ? v2_dist(r->enemies[target].u.pos, h->u.pos) * 0.3f : 100.0f;
        move_hero_to(r, h, sim_dash_dest(r, h->u.pos, h->u.face, minf(want, 150.0f), false, h->u.radius));
    } else if (s->flags & SKF_DASH_BACK) {
        move_hero_to(r, h, sim_dash_dest(r, h->u.pos, away_dir(r, h, target), 200.0f, false, h->u.radius));
        if (enemy_ok(r, target)) {
            h->u.face = v2_norm(v2_sub(r->enemies[target].u.pos, h->u.pos));
        }
    }
    int count = maxi(s->count, 1) + h->eff.extra_projectiles;
    if (hero_has_behavior(h, LB_MARCH_OF_LIGHT)) {
        count++;
    }
    count = mini(count, MAX_PROJ_PER_CAST);
    float spread = hh->ai.skill[0].spread_angle; /* Platzhalter, unten ersetzt */
    spread = s->angle;
    for (int i = 0; i < SKILL_SLOTS; i++) {
        if (hh->skill_slot[i] == sk && hh->ai.skill[i].spread_angle > 0) {
            spread = hh->ai.skill[i].spread_angle;
        }
    }
    bool spell = (s->flags & SKF_SPELL) != 0;
    bool dot = (s->flags & SKF_DOT) != 0;
    float dmg = hit_damage(h, g, s, sk);
    float speed = (s->speed > 0.0f ? s->speed : 700.0f) * (1.0f + ench_bonus(hh, sk, ENCH_SKILL_PROJ) / 100.0f);
    float range = s->range * (1.0f + ench_bonus(hh, sk, ENCH_SKILL_RANGE) / 100.0f);
    float expl = s->radius;
    if (expl <= 0.0f && hero_has_behavior(h, LB_EXPLOSIVE_TRAIL)) {
        expl = 70.0f;
    }
    Vec2 base_dir = h->u.face;
    if (enemy_ok(r, target)) {
        base_dir = v2_norm(v2_sub(r->enemies[target].u.pos, h->u.pos));
    }
    h->u.face = base_dir;
    for (int i = 0; i < count; i++) {
        Projectile *p = proj_alloc(r);
        if (!p) {
            break;
        }
        float a = count > 1 ? (-0.5f + (float)i / (float)(count - 1)) * spread * PNX_PI / 180.0f : 0.0f;
        Vec2 d = v2_rotate(base_dir, a);
        p->kind = PK_HERO;
        p->pos = v2_add(h->u.pos, v2_scale(d, h->u.radius + 6.0f));
        p->vel = v2_scale(d, speed);
        p->radius = expl > 0.0f ? HERO_PROJ_RADIUS + 3.0f : HERO_PROJ_RADIUS;
        p->damage = dot ? dmg * DOT_DIRECT_SHARE : dmg;
        p->life = range / speed * 1.2f + 0.1f;
        p->owner = slot;
        p->skill = sk;
        p->pierce_left = s->pierce + h->eff.extra_pierce;
        p->explode_radius = expl;
        p->spell = spell;
        p->homing = (s->flags & SKF_HOMING) != 0;
        p->homing_target = enemy_ok(r, target) ? target : -1;
        p->hits_per_target = maxi(s->max_hits, 1);
        p->color = class_color(g, h);
        p->style = class_style(g, h);
        p->dot_dur = s->dur;
        p->dot_dps = dot ? dmg * (1.0f - DOT_DIRECT_SHARE) / maxf(s->dur, 0.1f) : 0.0f;
        p->flags = s->flags;
    }
    sim_emit(r, EV_PROJ_LAUNCH, h->u.pos, 0, class_style(g, h), 0, 0, false, class_color(g, h));
    return true;
}

static Zone *alloc_zone(Run *r) {
    for (int i = 0; i < MAX_ZONES; i++) {
        if (!r->zones[i].active) {
            memset(&r->zones[i], 0, sizeof r->zones[i]);
            r->zones[i].active = true;
            return &r->zones[i];
        }
    }
    return NULL;
}

static bool exec_ground(Run *r, Game *g, int slot, const SkillDef *s, int sk, int target) {
    RunHero *h = &r->heroes[slot];
    if (!target_in_reach(r, h, target, s->range)) {
        return false;
    }
    Zone *z = alloc_zone(r);
    if (!z) {
        return false;
    }
    float dmg = hit_damage(h, g, s, sk);
    bool dot = (s->flags & SKF_DOT) != 0;
    z->shape = SHAPE_CIRCLE;
    z->pos = r->enemies[target].u.pos;
    z->radius = s->radius;
    z->owner = slot;
    z->skill = sk;
    z->spell = (s->flags & SKF_SPELL) != 0;
    z->color = class_color(g, h);
    z->flags = s->flags;
    if (dot) {
        z->kind = ZONE_POISON_CLOUD;
        z->life = maxf(skill_dur(g, h, s, sk), 1.0f);
        z->tick = TICK_POISON;
        z->tick_t = TICK_POISON;
        z->damage = dmg * TICK_POISON / z->life;
        z->fired = true;
    } else {
        z->kind = ZONE_HERO_AREA;
        z->delay = z->delay_total = s->delay;
        z->damage = dmg;
        z->life = 0.0f;
    }
    sim_emit(r, EV_HIT_FX, z->pos, 0, 0, 0, class_style(g, h), false, z->color);
    return true;
}

static bool exec_pull(Run *r, Game *g, int slot, const SkillDef *s, int sk, int target) {
    RunHero *h = &r->heroes[slot];
    if (!target_in_reach(r, h, target, s->range)) {
        return false;
    }
    Enemy *e = &r->enemies[target];
    if (!e->is_boss) {
        Vec2 to_t = v2_norm(v2_sub(e->u.pos, h->u.pos));
        Vec2 dest = v2_add(h->u.pos, v2_scale(to_t, h->u.radius + e->u.radius + 14.0f));
        if (world_walkable(&r->map, dest, minf(e->u.radius, 20.0f))) {
            e->u.pos = dest;
        }
        status_apply(&e->u, status_make(ST_STUN, -1, sk, 1.0f, 0.6f, slot, false));
    }
    hit_enemy(r, g, slot, target, hit_damage(h, g, s, sk), (s->flags & SKF_SPELL) != 0, sk);
    return true;
}

/* ======================================================================== */
/* Buffs, Debuffs, Heilung, Totems                                           */
/* ======================================================================== */
static void apply_buff(Run *r, int to_slot, int from_slot, const SkillDef *s, int sk, int lv, float dur) {
    RunHero *t = &r->heroes[to_slot];
    if (!t->u.alive) {
        return;
    }
    bool dirty = false;
    if (s->stat >= 0) {
        dirty |= status_apply(&t->u, status_make(ST_BUFF, s->stat, sk, skill_stat_value(s, lv, false), dur, from_slot, false));
    }
    if (s->stat2 >= 0) {
        dirty |= status_apply(&t->u, status_make(ST_BUFF, s->stat2, sk, skill_stat_value(s, lv, true), dur, from_slot, false));
    }
    if (dirty) {
        t->eff_dirty = true;
    }
}

static bool exec_buff(Run *r, Game *g, int slot, const SkillDef *s, int sk, bool party) {
    RunHero *h = &r->heroes[slot];
    int lv = skill_level_of(g, sk);
    float dur = skill_dur(g, h, s, sk);
    if (!party) {
        apply_buff(r, slot, slot, s, sk, lv, dur);
        return true;
    }
    for (int i = 0; i < r->hero_count; i++) {
        if (s->radius <= 0.0f || v2_dist(r->heroes[i].u.pos, h->u.pos) <= s->radius) {
            apply_buff(r, i, slot, s, sk, lv, dur);
        }
    }
    return true;
}

static bool exec_debuff(Run *r, Game *g, int slot, const SkillDef *s, int sk, int target) {
    RunHero *h = &r->heroes[slot];
    Vec2 center = h->u.pos;
    if (s->range > 0.0f) {
        if (!target_in_reach(r, h, target, s->range)) {
            return false;
        }
        center = r->enemies[target].u.pos;
    }
    int lv = skill_level_of(g, sk);
    float dur = skill_dur(g, h, s, sk);
    for (int i = 0; i < r->enemy_count; i++) {
        Enemy *e = &r->enemies[i];
        if (!e->u.alive || v2_dist(e->u.pos, center) > s->radius + e->u.radius * 0.5f) {
            continue;
        }
        if (s->stat >= 0) {
            status_apply(&e->u, status_make(ST_DEBUFF, s->stat, sk, skill_stat_value(s, lv, false), dur, slot, false));
        }
        if (s->stat2 >= 0) {
            status_apply(&e->u, status_make(ST_DEBUFF, s->stat2, sk, skill_stat_value(s, lv, true), dur, slot, false));
        }
    }
    sim_emit(r, EV_EXPLOSION, center, s->radius, (int)class_color(g, h), 0, 0, false, class_color(g, h));
    return true;
}

/* Verbuendeter (inkl. Wirker) mit dem niedrigsten HP-Anteil, -1 wenn keiner lebt. */
static int lowest_hp_ally(const Run *r) {
    int best = -1;
    float bf = 2.0f;
    for (int i = 0; i < r->hero_count; i++) {
        const RunHero *t = &r->heroes[i];
        if (!t->u.alive) {
            continue;
        }
        float f = t->u.hp / maxf(t->u.max_hp, 1.0f);
        if (f < bf) {
            bf = f;
            best = i;
        }
    }
    return best;
}

static bool exec_heal(Run *r, Game *g, int slot, const SkillDef *s, int sk, bool party) {
    RunHero *h = &r->heroes[slot];
    float amount = heal_base(h) * skill_dmg_pct(s, skill_level_of(g, sk)) / 100.0f * h->eff.heal_mult;
    if (party) {
        for (int i = 0; i < r->hero_count; i++) {
            combat_heal_hero(r, i, amount);
        }
        return true;
    }
    int t = lowest_hp_ally(r);
    if (t < 0) {
        return false;
    }
    combat_heal_hero(r, t, amount);
    return true;
}

static bool exec_hot(Run *r, Game *g, int slot, const SkillDef *s, int sk) {
    RunHero *h = &r->heroes[slot];
    int t = lowest_hp_ally(r);
    if (t < 0) {
        return false;
    }
    float dur = maxf(skill_dur(g, h, s, sk), 1.0f);
    float total = heal_base(h) * skill_dmg_pct(s, skill_level_of(g, sk)) / 100.0f * h->eff.heal_mult;
    status_apply(&r->heroes[t].u, status_make(ST_HOT, -1, sk, total / dur, dur, slot, true));
    return true;
}

static void give_mana(Run *r, int slot, float amount) {
    RunHero *t = &r->heroes[slot];
    if (!t->u.alive) {
        return;
    }
    float before = t->mana;
    t->mana = minf(t->mana + amount, t->max_mana);
    if (t->mana > before) {
        sim_emit(r, EV_MANA, t->u.pos, t->mana - before, 0, 0, 0, false, 0);
    }
}

static bool exec_mana(Run *r, Game *g, int slot, const SkillDef *s, int sk) {
    RunHero *h = &r->heroes[slot];
    float pct = skill_dmg_pct(s, skill_level_of(g, sk));
    if (s->flags & SKF_PARTY_HEAL) {
        for (int i = 0; i < r->hero_count; i++) {
            give_mana(r, i, r->heroes[i].max_mana * pct / 100.0f);
        }
    } else {
        give_mana(r, slot, h->max_mana * pct / 100.0f);
    }
    return true;
}

static bool exec_cleanse(Run *r, Game *g, int slot, const SkillDef *s, int sk) {
    RunHero *h = &r->heroes[slot];
    int t = lowest_hp_ally(r);
    if (t < 0) {
        return false;
    }
    RunHero *th = &r->heroes[t];
    status_remove_type(&th->u, ST_DOT);
    status_remove_type(&th->u, ST_SLOW);
    status_remove_type(&th->u, ST_STUN);
    combat_heal_hero(r, t, heal_base(h) * skill_dmg_pct(s, skill_level_of(g, sk)) / 100.0f * h->eff.heal_mult);
    return true;
}

static bool exec_totem(Run *r, Game *g, int slot, const SkillDef *s, int sk) {
    RunHero *h = &r->heroes[slot];
    int idx = -1;
    for (int i = 0; i < MAX_TOTEMS; i++) {
        const Totem *t = &r->totems[i];
        if (t->active && t->owner == slot && t->skill == sk) {
            idx = i;
            break;
        }
    }
    for (int i = 0; i < MAX_TOTEMS && idx < 0; i++) {
        if (!r->totems[i].active) {
            idx = i;
        }
    }
    if (idx < 0) {
        return false;
    }
    Totem *t = &r->totems[idx];
    memset(t, 0, sizeof *t);
    t->active = true;
    t->type = s->totem;
    t->pos = h->u.pos;
    t->radius = s->radius;
    t->life = t->life_total = maxf(skill_dur(g, h, s, sk), 1.0f);
    t->owner = slot;
    t->skill = sk;
    t->skill_level = skill_level_of(g, sk);
    t->tick_t = 0.4f;
    t->hp = 100.0f;
    t->dmg = hit_damage(h, g, s, sk);
    sim_emit(r, EV_HIT_FX, t->pos, 0, 0, 0, 4, false, class_color(g, h));
    return true;
}

/* ======================================================================== */
/* Haupteinstieg                                                             */
/* ======================================================================== */
static bool exec_kind(Run *r, Game *g, int slot, const SkillDef *s, int sk, int target) {
    switch (s->kind) {
    case SK_STRIKE: return exec_strike(r, g, slot, s, sk, target);
    case SK_CIRCLE: return exec_circle(r, g, slot, s, sk);
    case SK_CONE: return exec_cone(r, g, slot, s, sk);
    case SK_PROJECTILE: return exec_projectile(r, g, slot, s, sk, target);
    case SK_GROUND: return exec_ground(r, g, slot, s, sk, target);
    case SK_DASH: return exec_dash(r, g, slot, s, sk, target);
    case SK_BUFF_SELF: return exec_buff(r, g, slot, s, sk, false);
    case SK_BUFF_PARTY: return exec_buff(r, g, slot, s, sk, true);
    case SK_DEBUFF: return exec_debuff(r, g, slot, s, sk, target);
    case SK_HEAL: return exec_heal(r, g, slot, s, sk, false);
    case SK_HEAL_PARTY: return exec_heal(r, g, slot, s, sk, true);
    case SK_HOT: return exec_hot(r, g, slot, s, sk);
    case SK_TOTEM: return exec_totem(r, g, slot, s, sk);
    case SK_MANA: return exec_mana(r, g, slot, s, sk);
    case SK_CLEANSE: return exec_cleanse(r, g, slot, s, sk);
    case SK_PULL: return exec_pull(r, g, slot, s, sk, target);
    case SK_WARP: return exec_warp(r, g, slot, s, sk, target);
    default: return false;
    }
}

bool skill_cast(Run *r, Game *g, int slot, int skill_slot, int target) {
    if (!skill_ready(r, g, slot, skill_slot)) {
        return false;
    }
    RunHero *h = &r->heroes[slot];
    Hero *hh = &g->heroes[h->hero_idx];
    int sk = hh->skill_slot[skill_slot];
    const SkillDef *s = &g_skills[sk];
    if (!enemy_ok(r, target) && s->kind != SK_BUFF_SELF && s->kind != SK_BUFF_PARTY) {
        target = sim_nearest_enemy(r, h->u.pos, s->range > 0.0f ? s->range + 60.0f : 400.0f);
    }
    if (enemy_ok(r, target)) {
        h->u.face = v2_norm(v2_sub(r->enemies[target].u.pos, h->u.pos));
    }
    if (!exec_kind(r, g, slot, s, sk, target)) {
        return false;
    }
    h->mana -= skill_mana_cost(h, s);
    float cd = skill_cooldown_of(h, s);
    cd = maxf(cd, (float)hh->ai.skill[skill_slot].min_cd_x10 / 10.0f);
    h->cd[skill_slot] = cd;
    h->cd_max[skill_slot] = cd;
    float speed = h->eff.cast_speed * (1.0f + ench_bonus(hh, sk, ENCH_SKILL_CAST) / 100.0f);
    h->cast_t = maxf(s->cast_time / maxf(speed, 0.1f), MIN_CAST_TIME);
    h->casting_slot = skill_slot;
    h->u.attack_anim = 1.0f;
    h->skill_gcd = GCD_AFTER_CAST;
    h->atk_timer = maxf(h->atk_timer, h->cast_t * 0.5f);
    h->last_skill_mask |= 1u << skill_slot;
    sim_emit(r, EV_SKILL_CAST, h->u.pos, 0, slot, sk, 0, false, class_color(g, h));
    return true;
}

/* ======================================================================== */
/* Basisangriff                                                              */
/* ======================================================================== */
float hero_basic_reach(const Run *r, const Game *g, int slot) {
    (void)g;
    return r->heroes[slot].eff.range;
}

/* Kettenblitz (Set Aschekoenig): Basisangriff springt auf bis zu 2 weitere Gegner. */
static void chain_lightning(Run *r, Game *g, int slot, int from, float dmg) {
    int done = 0;
    for (int i = 0; i < r->enemy_count && done < 2; i++) {
        Enemy *e = &r->enemies[i];
        if (i == from || !e->u.alive || v2_dist(e->u.pos, r->enemies[from].u.pos) > CHAIN_RADIUS) {
            continue;
        }
        hit_enemy(r, g, slot, i, dmg * 0.4f, true, -1);
        sim_emit(r, EV_HIT_FX, e->u.pos, 0, 0, 0, 4, false, 0x9FD0FF);
        done++;
    }
}

static void legendary_on_hit(Run *r, Game *g, int slot, int target, float dmg) {
    const RunHero *h = &r->heroes[slot];
    if (!enemy_ok(r, target)) {
        return;
    }
    if (hero_has_behavior(h, LB_CHAIN_LIGHTNING)) {
        chain_lightning(r, g, slot, target, dmg);
    }
    if (hero_has_behavior(h, LB_EXECUTIONER)) {
        Vec2 c = r->enemies[target].u.pos;
        for (int i = 0; i < r->enemy_count; i++) {
            Enemy *e = &r->enemies[i];
            if (i != target && e->u.alive && v2_dist(e->u.pos, c) <= 110.0f) {
                hit_enemy(r, g, slot, i, dmg * 0.5f, false, -1);
            }
        }
    }
}

static void basic_projectile(Run *r, Game *g, int slot, int target, float dmg, bool spell) {
    RunHero *h = &r->heroes[slot];
    const ClassDef *c = &g_classes[clampi(g->heroes[h->hero_idx].cls, 0, CLASS_COUNT - 1)];
    int count = 1 + h->eff.extra_projectiles + (hero_has_behavior(h, LB_MARCH_OF_LIGHT) ? 1 : 0);
    count = mini(count, 7);
    float speed = c->proj_speed > 0.0f ? c->proj_speed : 800.0f;
    Vec2 base = v2_norm(v2_sub(r->enemies[target].u.pos, h->u.pos));
    for (int i = 0; i < count; i++) {
        Projectile *p = proj_alloc(r);
        if (!p) {
            return;
        }
        float a = count > 1 ? (-0.5f + (float)i / (float)(count - 1)) * BASIC_PROJ_SPREAD * (float)(count - 1) * PNX_PI / 180.0f : 0.0f;
        Vec2 d = v2_rotate(base, a);
        p->kind = PK_HERO;
        p->pos = v2_add(h->u.pos, v2_scale(d, h->u.radius + 6.0f));
        p->vel = v2_scale(d, speed);
        p->radius = HERO_PROJ_RADIUS;
        p->damage = dmg;
        p->life = h->eff.range / speed * 1.25f + 0.2f;
        p->owner = slot;
        p->skill = -1;
        p->pierce_left = h->eff.extra_pierce;
        p->explode_radius = hero_has_behavior(h, LB_EXPLOSIVE_TRAIL) ? 70.0f : 0.0f;
        p->spell = spell;
        p->homing = c->atk_style == ATK_HOMING;
        p->homing_target = target;
        p->hits_per_target = 1;
        p->color = c->color;
        p->style = class_style(g, h);
    }
    sim_emit(r, EV_PROJ_LAUNCH, h->u.pos, 0, class_style(g, h), 0, 0, false, c->color);
}

bool hero_basic_attack(Run *r, Game *g, int slot, int target) {
    if (!enemy_ok(r, target) || slot < 0 || slot >= r->hero_count) {
        return false;
    }
    RunHero *h = &r->heroes[slot];
    const ClassDef *c = &g_classes[clampi(g->heroes[h->hero_idx].cls, 0, CLASS_COUNT - 1)];
    Enemy *e = &r->enemies[target];
    float reach = hero_basic_reach(r, g, slot);
    float dist = v2_dist(e->u.pos, h->u.pos);
    bool melee = c->atk_style == ATK_MELEE || c->atk_style == ATK_DASH_STAB;
    if (c->atk_style == ATK_DASH_STAB && dist - e->u.radius > reach + MELEE_REACH_SLACK && dist - e->u.radius <= reach + 170.0f) {
        Vec2 d = v2_norm(v2_sub(e->u.pos, h->u.pos));
        float go = dist - e->u.radius - h->u.radius - 8.0f;
        move_hero_to(r, h, sim_dash_dest(r, h->u.pos, d, maxf(go, 0.0f), false, h->u.radius));
        dist = v2_dist(e->u.pos, h->u.pos);
    }
    if (dist - e->u.radius > reach + MELEE_REACH_SLACK) {
        return false;
    }
    if (!melee && !world_line_clear_proj(&r->map, h->u.pos, e->u.pos)) {
        return false;
    }
    bool spell = c->dmg_type == DMG_SPELL;
    float pct = c->basic_dmg_pct > 0.0f ? c->basic_dmg_pct : 100.0f;
    float dmg = hero_power(h, spell) * pct / 100.0f;
    h->u.face = v2_norm(v2_sub(e->u.pos, h->u.pos));
    if (melee) {
        hit_enemy(r, g, slot, target, dmg, spell, -1);
        legendary_on_hit(r, g, slot, target, dmg);
        sim_emit(r, EV_HIT_FX, e->u.pos, 0, 0, 0, class_style(g, h), false, c->color);
    } else {
        basic_projectile(r, g, slot, target, dmg, spell);
        if (hero_has_behavior(h, LB_CHAIN_LIGHTNING)) {
            chain_lightning(r, g, slot, target, dmg);
        }
    }
    h->atk_timer = 1.0f / maxf(h->eff.attack_speed, 0.1f);
    h->u.attack_anim = 1.0f;
    return true;
}
