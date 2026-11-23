/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Gegner-KI (Spec 15, 17): Zustandsautomat (Dormant/Idle/Aggro/Chase/Attack/Leash),
 * Pack-Aggro, Bedrohungstabelle, Archetyp-Verhalten, Affixe und Boss-Phasen
 * (100/60/30 %) mit Telegraphen, Adds und Enrage. */
#include <math.h>
#include <string.h>

#include "sim/sim_internal.h"

#define DOT_PULSE_TICKS 15
#define AGGRO_NORMAL 500.0f
#define AGGRO_ELITE 600.0f
#define AGGRO_GUARDIAN 280.0f
#define AGGRO_TOTEM 600.0f
#define PACK_RADIUS 400.0f
#define TARGET_DROP_FACTOR 2.2f     /* Ziel weiter als Aggro*Faktor => verwerfen */
#define THREAT_SWITCH 1.5f          /* Zielwechsel ab 150 % Bedrohung */
#define LEASH_SPEED_MULT 2.5f
#define LEASH_MAX_S 8.0f
#define LEASH_HEAL_PCT 25.0f
#define BOSS_RESET_S 5.0f
#define BOSS_INTRO_S 1.5f
#define PHASE_INVULN_S 2.0f
#define MAX_ADDS_PER_PARENT 8
#define ADD_HP_FACTOR 0.6f
#define DASH_SPEED_BOSS 1100.0f
#define TELE_PHYS 0xE5484Du
#define TELE_SPELL 0xB070FFu
#define AFFIX_AURA_R 115.0f
#define AFFIX_PULL_R 450.0f

typedef struct Ctx {
    Run *r;
    Game *g;
    int ei;
    Enemy *e;
    const MonsterDef *d;
    int ts;          /* Heldenslot des Ziels */
    RunHero *t;      /* Ziel */
    float dist;      /* Mittelpunktsabstand */
} Ctx;

/* ======================================================================== */
/* Kleine Helfer                                                             */
/* ======================================================================== */
static const MonsterDef *mdef(const Enemy *e) { return &g_monsters[e->def]; }

static bool hero_targetable(const RunHero *h) {
    return h->u.alive && h->state != HS_DEAD && h->state != HS_MAP_CLEAR && h->state != HS_RETRY_WAIT;
}

static bool is_immobile(const Enemy *e) {
    const MonsterDef *d = mdef(e);
    return d->arch == ARCH_TOTEM || d->speed <= 0.0f;
}

static float aggro_radius(const Enemy *e) {
    Archetype a = mdef(e)->arch;
    if (a == ARCH_TOTEM) {
        return AGGRO_TOTEM;
    }
    if (a == ARCH_GUARDIAN) {
        return AGGRO_GUARDIAN;
    }
    return e->tier >= TIER_RARE ? AGGRO_ELITE : AGGRO_NORMAL;
}

static float leash_radius(const Enemy *e) {
    return mdef(e)->arch == ARCH_GUARDIAN ? 600.0f : (float)g_bal.leash_dist;
}

bool enemy_is_spell(const Enemy *e) {
    const MonsterDef *d = mdef(e);
    switch (d->arch) {
    case ARCH_ELEMENTAL:
    case ARCH_SUMMONER:
    case ARCH_HEALER:
    case ARCH_TOTEM:
        return true;
    default:
        break;
    }
    return (d->abilities & (MAB_RAIN | MAB_POOLS | MAB_CONE)) != 0 && d->proj_speed > 0.0f;
}

float enemy_current_damage(const Run *r, const Enemy *e) {
    bool spell = enemy_is_spell(e);
    float down = status_stat_sum(&e->u, ST_DEBUFF, spell ? STAT_ENEMY_SPELL_DOWN : STAT_ENEMY_ATK_DOWN);
    float dmg = e->dmg * clampf(1.0f - down / 100.0f, 0.2f, 1.0f);
    if (e->is_boss) {
        if (r->enrage_t >= (float)g_bal.boss_enrage_s) {
            dmg *= 2.0f;
        }
        if (e->phase >= 2 && (mdef(e)->abilities & MAB_ENRAGE)) {
            dmg *= 1.25f;
        }
    }
    return dmg;
}

static float dec(float v) { return v > 0.0f ? maxf(v - SIM_DT, 0.0f) : 0.0f; }

static int zones_free(const Run *r) {
    int n = 0;
    for (int i = 0; i < MAX_ZONES; i++) {
        n += r->zones[i].active ? 0 : 1;
    }
    return n;
}

static int random_target_hero(Run *r) {
    int cand[MAX_TEAM], n = 0;
    for (int s = 0; s < r->hero_count; s++) {
        if (hero_targetable(&r->heroes[s])) {
            cand[n++] = s;
        }
    }
    return n > 0 ? cand[rng_range(&r->rng, 0, n - 1)] : -1;
}

static int nearest_hero(const Run *r, Vec2 p, float max_d, bool need_arena) {
    int best = -1;
    float bd = max_d * max_d;
    for (int s = 0; s < r->hero_count; s++) {
        const RunHero *h = &r->heroes[s];
        if (!hero_targetable(h) || (need_arena && !run_in_arena(r, h->u.pos))) {
            continue;
        }
        float d = v2_dist2(h->u.pos, p);
        if (d <= bd) {
            bd = d;
            best = s;
        }
    }
    return best;
}

/* ======================================================================== */
/* Bewegung                                                                  */
/* ======================================================================== */
static float move_speed(const Run *r, const Enemy *e) {
    if (unit_stunned(&e->u) || e->cast_lock > 0.0f || is_immobile(e)) {
        return 0.0f;
    }
    float v = e->speed * (1.0f - clampf(status_max(&e->u, ST_SLOW), 0.0f, 80.0f) / 100.0f);
    v *= world_speed_factor(&r->map, e->u.pos);
    if (e->is_boss && e->phase >= 2 && (mdef(e)->abilities & MAB_ENRAGE)) {
        v *= 1.2f;
    }
    return v;
}

static float nav_radius(const Enemy *e) { return minf(e->u.radius, 16.0f); }

static void clamp_to_arena(const Run *r, Enemy *e) {
    if (!e->is_boss || !r->map.has_boss_arena) {
        return;
    }
    const Rect *a = &r->map.arena;
    float m = nav_radius(e) + 4.0f;
    e->u.pos.x = clampf(e->u.pos.x, (float)a->x + m, (float)(a->x + a->w) - m);
    e->u.pos.y = clampf(e->u.pos.y, (float)a->y + m, (float)(a->y + a->h) - m);
}

static void note_move(Enemy *e, Vec2 before) {
    Vec2 d = v2_sub(e->u.pos, before);
    float l = v2_len(d);
    e->u.vel = v2_scale(d, 1.0f / SIM_DT);
    if (l > 0.01f) {
        e->u.face = v2_scale(d, 1.0f / l);
        e->u.anim_t += SIM_DT;
    }
}

/* Pfadfolgen Richtung Ziel; true = unterwegs. */
static bool step_toward(Run *r, Enemy *e, Vec2 goal, float speed, float stop) {
    if (speed <= 0.0f) {
        return false;
    }
    NavAgent ag = {e->nav_path, 4, &e->nav_len, &e->nav_pos, &e->nav_goal, &e->nav_t};
    Vec2 before = e->u.pos;
    bool mv = nav_follow(&r->map, &e->u.pos, nav_radius(e), goal, speed * SIM_DT, &ag, SIM_DT, stop);
    clamp_to_arena(r, e);
    note_move(e, before);
    return mv;
}

/* Direkt vom Punkt weg (Rueckzug); weicht an Waenden seitlich aus. */
static void step_away(Run *r, Enemy *e, Vec2 from, float speed) {
    if (speed <= 0.0f) {
        return;
    }
    Vec2 before = e->u.pos;
    Vec2 dir = v2_norm(v2_sub(e->u.pos, from));
    float step = speed * SIM_DT;
    Vec2 np = world_move(&r->map, e->u.pos, v2_scale(dir, step), nav_radius(e));
    if (v2_dist(np, before) < step * 0.4f) {
        Vec2 side = v2(-dir.y, dir.x);
        np = world_move(&r->map, e->u.pos, v2_scale(side, step), nav_radius(e));
    }
    e->u.pos = np;
    clamp_to_arena(r, e);
    note_move(e, before);
    e->nav_len = 0;
}

static void face_to(Enemy *e, Vec2 p) {
    Vec2 d = v2_sub(p, e->u.pos);
    if (v2_len(d) > 0.5f) {
        e->u.face = v2_norm(d);
    }
}

/* ======================================================================== */
/* Aggro, Pack, Leash                                                        */
/* ======================================================================== */
static void wake(Enemy *e, int slot) {
    e->state = ES_AGGRO;
    e->state_t = 0.2f + 0.003f * (float)((e->pack_id * 37 + (int)e->u.pos.x) % 100);
    e->target_hero = slot;
    e->leash_t = 0.0f;
    e->out_of_combat_t = 0.0f;
    e->retarget_t = 0.0f;
}

void enemy_alert_pack(Run *r, int ei, int hero_slot) {
    const Enemy *src = &r->enemies[ei];
    if (src->pack_id <= 0) {
        return;
    }
    for (int i = 0; i < r->enemy_count; i++) {
        Enemy *e = &r->enemies[i];
        if (i == ei || !e->u.alive || e->pack_id != src->pack_id || e->is_boss) {
            continue;
        }
        if ((e->state == ES_IDLE || e->state == ES_DORMANT) && v2_dist(e->u.pos, src->u.pos) <= PACK_RADIUS) {
            wake(e, hero_slot);
        }
    }
}

void enemy_aggro(Run *r, int ei, int hero_slot) {
    if (ei < 0 || ei >= r->enemy_count) {
        return;
    }
    Enemy *e = &r->enemies[ei];
    if (!e->u.alive || (e->state != ES_IDLE && e->state != ES_DORMANT)) {
        return;
    }
    if (hero_slot < 0 || hero_slot >= r->hero_count || !hero_targetable(&r->heroes[hero_slot])) {
        hero_slot = nearest_hero(r, e->u.pos, 1e9f, false);
    }
    if (hero_slot < 0) {
        return;
    }
    wake(e, hero_slot);
    enemy_alert_pack(r, ei, hero_slot);
}

static void begin_leash(Enemy *e) {
    e->state = ES_LEASH;
    e->target_hero = -1;
    e->leash_t = 0.0f;
    e->nav_len = 0;
    e->cast_lock = 0.0f;
    e->charge_t = 0.0f;
    e->fuse_t = 0.0f;
}

static void update_leash(Run *r, Enemy *e) {
    e->leash_t += SIM_DT;
    float speed = maxf(e->speed, 120.0f) * LEASH_SPEED_MULT;
    e->u.hp = minf(e->u.hp + e->u.max_hp * LEASH_HEAL_PCT / 100.0f * SIM_DT, e->u.max_hp);
    if (!is_immobile(e)) {
        step_toward(r, e, e->spawn, speed, 6.0f);
    }
    if (v2_dist(e->u.pos, e->spawn) <= 14.0f || e->leash_t > LEASH_MAX_S) {
        if (e->leash_t > LEASH_MAX_S) {
            e->u.pos = world_nearest_walkable(&r->map, e->spawn, nav_radius(e));
        }
        e->state = ES_IDLE;
        e->u.hp = e->u.max_hp;
        e->hits_done = 0;
        e->leash_t = 0.0f;
        memset(e->threat, 0, sizeof e->threat);
    }
}

static void update_idle(Run *r, int ei, Enemy *e) {
    e->target_hero = -1;
    e->u.vel = v2(0.0f, 0.0f);
    float radius = aggro_radius(e);
    int s = nearest_hero(r, e->u.pos, radius, e->is_boss);
    if (s >= 0) {
        enemy_aggro(r, ei, s);
    }
}

/* ======================================================================== */
/* Zielwahl (Bedrohungstabelle, Spott)                                       */
/* ======================================================================== */
static bool target_valid(const Run *r, const Enemy *e, int s) {
    if (s < 0 || s >= r->hero_count || !hero_targetable(&r->heroes[s])) {
        return false;
    }
    return v2_dist(r->heroes[s].u.pos, e->u.pos) <= aggro_radius(e) * TARGET_DROP_FACTOR;
}

static int taunting_hero(const Run *r, const Enemy *e) {
    int best = -1;
    float bd = 700.0f * 700.0f;
    for (int s = 0; s < r->hero_count; s++) {
        const RunHero *h = &r->heroes[s];
        if (!hero_targetable(h) || status_max(&h->u, ST_TAUNT) <= 0.0f) {
            continue;
        }
        float d = v2_dist2(h->u.pos, e->u.pos);
        if (d < bd) {
            bd = d;
            best = s;
        }
    }
    return best;
}

static int pick_target(const Run *r, const Enemy *e) {
    int cur = e->target_hero;
    int taunt = taunting_hero(r, e);
    if (taunt >= 0) {
        return taunt;
    }
    if (!target_valid(r, e, cur)) {
        return nearest_hero(r, e->u.pos, aggro_radius(e) * TARGET_DROP_FACTOR, false);
    }
    int best = cur;
    float need = e->threat[cur] * THREAT_SWITCH + 1.0f;
    for (int s = 0; s < r->hero_count; s++) {
        if (s != cur && target_valid(r, e, s) && e->threat[s] > need) {
            need = e->threat[s];
            best = s;
        }
    }
    return best;
}

/* ======================================================================== */
/* Statuseffekte, Timer, Affixe                                              */
/* ======================================================================== */
static void tick_timers(Enemy *e) {
    e->u.hit_flash = maxf(e->u.hit_flash - SIM_DT * 4.0f, 0.0f);
    e->u.attack_anim = maxf(e->u.attack_anim - SIM_DT * 3.5f, 0.0f);
    e->atk_timer = dec(e->atk_timer);
    for (int i = 0; i < 4; i++) {
        e->ability_cd[i] = dec(e->ability_cd[i]);
    }
    e->cast_lock = dec(e->cast_lock);
    e->charge_t = dec(e->charge_t);
    e->summon_t = dec(e->summon_t);
    e->shield_cd = dec(e->shield_cd);
    e->heal_t = dec(e->heal_t);
    e->ranged_cd = dec(e->ranged_cd);
    e->aura_t = dec(e->aura_t);
    e->trail_t = dec(e->trail_t);
    e->pull_t = dec(e->pull_t);
    e->invuln = dec(e->invuln);
    e->retreat_t = dec(e->retreat_t);
    e->retarget_t = dec(e->retarget_t);
    e->alert_t = dec(e->alert_t);
    if (e->pending_t > 0.0f) {
        e->pending_t -= SIM_DT;
        if (e->pending_t <= 0.0f) {
            e->pending_t = 0.0f;
            e->charge_t = 0.35f; /* Ansturm beginnt nach dem Telegraph */
        }
    }
    e->telegraphing = e->cast_lock > 0.0f;
    e->shielded = e->shield_hp > 0.0f;
}

/* DoT-Pakete alle 0,5 s; false = Gegner dabei gestorben. */
static bool tick_statuses(Run *r, Game *g, int ei, Enemy *e) {
    status_expire(&e->u, SIM_DT);
    if (((r->tick + (u32)ei) % DOT_PULSE_TICKS) != 0) {
        return true;
    }
    for (int i = 0; i < e->u.st_count; i++) {
        Status s = e->u.st[i];
        if (s.type != ST_DOT) {
            continue;
        }
        Hit h = {s.source, s.value * (float)DOT_PULSE_TICKS * SIM_DT, s.spell, true, true, s.skill};
        combat_hit_enemy(r, g, ei, &h);
        if (!e->u.alive) {
            return false;
        }
    }
    return true;
}

static void affix_auras(Run *r, Game *g, int ei, Enemy *e) {
    if (e->aura_t <= 0.0f && (e->affixes & (AFFIX_FROST_AURA | AFFIX_FIRE_AURA))) {
        e->aura_t = 0.5f;
        for (int s = 0; s < r->hero_count; s++) {
            RunHero *h = &r->heroes[s];
            if (!hero_targetable(h) || v2_dist(h->u.pos, e->u.pos) > AFFIX_AURA_R + h->u.radius) {
                continue;
            }
            if (e->affixes & AFFIX_FROST_AURA) {
                status_apply(&h->u, status_make(ST_SLOW, -1, -3, 30.0f, 0.7f, -1, true));
            }
            if (e->affixes & AFFIX_FIRE_AURA) {
                combat_hit_hero(r, g, s, enemy_current_damage(r, e) * 0.2f, true, ei);
            }
        }
    }
    if ((e->affixes & AFFIX_PULLING) && e->pull_t <= 0.0f && e->state >= ES_CHASE && e->state <= ES_RECOVER) {
        e->pull_t = 8.0f;
        for (int s = 0; s < r->hero_count; s++) {
            RunHero *h = &r->heroes[s];
            if (hero_targetable(h) && v2_dist(h->u.pos, e->u.pos) <= AFFIX_PULL_R) {
                Vec2 dir = v2_norm(v2_sub(e->u.pos, h->u.pos));
                h->u.pos = world_move(&r->map, h->u.pos, v2_scale(dir, 90.0f), h->u.radius);
                h->path_len = 0;
            }
        }
        sim_emit(r, EV_EXPLOSION, e->u.pos, AFFIX_PULL_R * 0.5f, 0x9B59B6, 0, 0, false, 0x9B59B6);
    }
}

/* Spuren: Giftspur-Affix und Elementare (POOLS) hinterlassen Bodenzonen. */
static void trail_pools(Run *r, int ei, Enemy *e) {
    const MonsterDef *d = mdef(e);
    bool elem = d->arch == ARCH_ELEMENTAL && (d->abilities & MAB_POOLS);
    if (!(elem || (e->affixes & AFFIX_POISON_TRAIL)) || e->trail_t > 0.0f || zones_free(r) < 40) {
        return;
    }
    if (v2_len(e->u.vel) < 5.0f || e->state < ES_CHASE || e->state > ES_RECOVER) {
        return;
    }
    e->trail_t = elem ? 0.9f : 0.7f;
    u32 col = (e->affixes & AFFIX_POISON_TRAIL) ? 0x6FCF4Au : (d->color ? d->color : 0xE8541Eu);
    zone_spawn_pool(r, ei, e->u.pos, 34.0f, 4.0f, enemy_current_damage(r, e) * 0.35f, true, col);
}

static void regen_tick(Enemy *e) {
    if (e->affixes & AFFIX_REGEN) {
        e->u.hp = minf(e->u.hp + e->u.max_hp * 0.015f * SIM_DT, e->u.max_hp);
    }
}

/* ======================================================================== */
/* Angriffe                                                                  */
/* ======================================================================== */
static float attack_interval(const Enemy *e) { return mdef(e)->attack_cd / maxf(e->atk_speed_mult, 0.2f); }

static float melee_reach(const Ctx *c) { return c->d->range + c->t->u.radius * 0.5f; }

static void melee_hit(Ctx *c) {
    Enemy *e = c->e;
    e->atk_timer = attack_interval(e);
    e->u.attack_anim = 1.0f;
    e->hits_done++;
    sim_emit(c->r, EV_HIT_FX, c->t->u.pos, 0, 0, 0, 0, false, c->d->color);
    combat_hit_hero(c->r, c->g, c->ts, enemy_current_damage(c->r, e), enemy_is_spell(e), c->ei);
}

static void shoot(Ctx *c, float dmg_mult) {
    Enemy *e = c->e;
    e->atk_timer = attack_interval(e);
    e->u.attack_anim = 1.0f;
    Projectile *p = proj_alloc(c->r);
    if (!p) {
        return;
    }
    float speed = c->d->proj_speed > 0.0f ? c->d->proj_speed : 380.0f;
    Vec2 dir = v2_norm(v2_sub(c->t->u.pos, e->u.pos));
    bool spell = enemy_is_spell(e);
    p->kind = PK_ENEMY;
    p->pos = v2_add(e->u.pos, v2_scale(dir, e->u.radius * 0.6f));
    p->vel = v2_scale(dir, speed);
    p->radius = 8.0f;
    p->damage = enemy_current_damage(c->r, e) * dmg_mult;
    p->life = (c->d->range * 1.4f + 200.0f) / speed;
    p->owner = c->ei;
    p->skill = -1;
    p->spell = spell;
    p->color = c->d->color;
    p->style = spell ? 1 : 0;
    sim_emit(c->r, EV_PROJ_LAUNCH, p->pos, 0, p->style, 0, 0, false, p->color);
}

/* Boss-Nahangriff: Kegel-Telegraph in Blickrichtung (Schaden erst nach Telegraph). */
static void boss_basic_swing(Ctx *c) {
    Enemy *e = c->e;
    face_to(e, c->t->u.pos);
    zone_spawn_enemy_tele(c->r, c->ei, SHAPE_CONE, e->u.pos, e->u.face, c->d->range + e->u.radius + 30.0f, 110.0f, 0.6f,
                          enemy_current_damage(c->r, e), false, TELE_PHYS);
    e->cast_lock = 0.7f;
    e->atk_timer = attack_interval(e);
    e->u.attack_anim = 1.0f;
}

static void basic_attack(Ctx *c) {
    if (c->e->is_boss) {
        boss_basic_swing(c);
    } else {
        melee_hit(c);
    }
}

/* Nahkampf-Verhalten; ring = Umzingeln (Schwarm). */
static void ai_melee(Ctx *c, bool ring) {
    Enemy *e = c->e;
    float reach = melee_reach(c);
    float speed = move_speed(c->r, e);
    if (c->dist > reach) {
        Vec2 goal = c->t->u.pos;
        float stop = reach * 0.75f;
        if (ring && c->dist < 260.0f) {
            float a = (float)c->ei * 2.3999632f;
            goal = v2_add(goal, v2(cosf(a) * reach * 0.8f, sinf(a) * reach * 0.8f));
            stop = 5.0f;
        }
        step_toward(c->r, e, goal, speed, stop);
        e->state = ES_CHASE;
        if (v2_dist(e->u.pos, c->t->u.pos) > reach) {
            return;
        }
    }
    e->state = ES_ATTACK;
    face_to(e, c->t->u.pos);
    if (e->atk_timer <= 0.0f && e->cast_lock <= 0.0f && !unit_stunned(&e->u)) {
        basic_attack(c);
    }
}

/* Fernkampf: haelt 'hold' * Reichweite, weicht unter 'flee' * Reichweite zurueck. */
static void ai_ranged(Ctx *c, float hold, float flee) {
    Enemy *e = c->e;
    float range = c->d->range;
    float speed = move_speed(c->r, e);
    bool los = world_line_clear_proj(&c->r->map, e->u.pos, c->t->u.pos);
    if (c->dist > range || !los) {
        step_toward(c->r, e, c->t->u.pos, speed, range * hold);
        e->state = ES_CHASE;
        if (c->dist > range || !los) {
            return;
        }
    }
    e->state = ES_ATTACK;
    face_to(e, c->t->u.pos);
    if (flee > 0.0f && c->dist < range * flee && speed > 0.0f) {
        step_away(c->r, e, c->t->u.pos, speed * 0.6f);
        e->state = ES_RECOVER;
    } else if (c->dist > range * hold) {
        step_toward(c->r, e, c->t->u.pos, speed * 0.7f, range * hold);
    }
    if (e->atk_timer <= 0.0f && e->cast_lock <= 0.0f && !unit_stunned(&e->u)) {
        shoot(c, 1.0f);
    }
}

/* ======================================================================== */
/* Spezialfaehigkeiten (Boss und Elite-Muster, MAB_*)                        */
/* ======================================================================== */
typedef struct Special {
    u32 bit;
    int cat;             /* Cooldown-Kategorie 0..3 */
    float cd_boss, cd_elite;
    float min_d, max_d;
    int min_phase;       /* Boss: ab dieser Phase */
} Special;

static const Special k_specials[] = {
    {MAB_SLAM, 0, 5.0f, 7.0f, 0.0f, 520.0f, 0},   {MAB_NOVA, 0, 7.0f, 9.0f, 0.0f, 230.0f, 0},
    {MAB_CONE, 0, 8.0f, 9.0f, 0.0f, 380.0f, 0},   {MAB_LINE, 0, 6.0f, 8.0f, 120.0f, 700.0f, 0},
    {MAB_SUMMON, 1, 16.0f, 12.0f, 0.0f, 1e9f, 0}, {MAB_SHIELD, 1, 26.0f, 18.0f, 0.0f, 1e9f, 1},
    {MAB_HEAL_ALLY, 1, 7.0f, 4.0f, 0.0f, 1e9f, 1}, {MAB_POOLS, 2, 10.0f, 0.0f, 0.0f, 600.0f, 1},
    {MAB_RAIN, 2, 11.0f, 9.0f, 150.0f, 700.0f, 1}, {MAB_CHARGE, 3, 10.0f, 10.0f, 220.0f, 650.0f, 0},
};

static void cast_slam(Ctx *c, float mult) {
    zone_spawn_enemy_tele(c->r, c->ei, SHAPE_CIRCLE, c->t->u.pos, v2(0, 0), 105.0f, 0.0f, 0.9f,
                          enemy_current_damage(c->r, c->e) * mult, false, TELE_PHYS);
    c->e->cast_lock = 0.9f;
}

static void cast_nova(Ctx *c, float mult) {
    zone_spawn_enemy_tele(c->r, c->ei, SHAPE_CIRCLE, c->e->u.pos, v2(0, 0), 160.0f + c->e->u.radius, 0.0f, 1.0f,
                          enemy_current_damage(c->r, c->e) * mult, false, TELE_PHYS);
    c->e->cast_lock = 1.0f;
}

static void cast_cone(Ctx *c, float mult) {
    Enemy *e = c->e;
    face_to(e, c->t->u.pos);
    zone_spawn_enemy_tele(c->r, c->ei, SHAPE_CONE, e->u.pos, e->u.face, 360.0f, 70.0f, 1.0f,
                          enemy_current_damage(c->r, e) * mult, true, TELE_SPELL);
    e->cast_lock = 1.0f;
}

static void cast_line(Ctx *c, float mult) {
    Enemy *e = c->e;
    face_to(e, c->t->u.pos);
    zone_spawn_enemy_tele(c->r, c->ei, SHAPE_LINE, e->u.pos, e->u.face, 650.0f, 56.0f, 0.9f,
                          enemy_current_damage(c->r, e) * mult, false, TELE_PHYS);
    e->cast_lock = 0.9f;
}

static void cast_rain(Ctx *c, float mult) {
    int n = 4 + c->e->phase;
    for (int i = 0; i < n && zones_free(c->r) > 8; i++) {
        int s = random_target_hero(c->r);
        if (s < 0) {
            break;
        }
        Vec2 off = v2(rng_frange(&c->r->rng, -90.0f, 90.0f), rng_frange(&c->r->rng, -90.0f, 90.0f));
        zone_spawn_enemy_tele(c->r, c->ei, SHAPE_CIRCLE, v2_add(c->r->heroes[s].u.pos, off), v2(0, 0), 75.0f, 0.0f,
                              0.8f + 0.12f * (float)i, enemy_current_damage(c->r, c->e) * mult, true, TELE_SPELL);
    }
    c->e->cast_lock = 0.8f;
}

static void cast_pools(Ctx *c) {
    for (int i = 0; i < 3 && zones_free(c->r) > 8; i++) {
        int s = random_target_hero(c->r);
        if (s < 0) {
            break;
        }
        Vec2 off = v2(rng_frange(&c->r->rng, -90.0f, 90.0f), rng_frange(&c->r->rng, -90.0f, 90.0f));
        zone_spawn_pool(c->r, c->ei, v2_add(c->r->heroes[s].u.pos, off), 90.0f, 8.0f,
                        enemy_current_damage(c->r, c->e) * 0.35f, true, 0x6FCF4A);
    }
    c->e->cast_lock = 0.5f;
}

static void cast_charge(Ctx *c) {
    Enemy *e = c->e;
    Vec2 dir = v2_norm(v2_sub(c->t->u.pos, e->u.pos));
    float len = clampf(c->dist + 120.0f, 300.0f, 650.0f);
    e->u.face = dir;
    zone_spawn_enemy_tele(c->r, c->ei, SHAPE_LINE, e->u.pos, dir, len, 70.0f, 0.9f, enemy_current_damage(c->r, e) * 1.3f,
                          false, TELE_PHYS);
    e->aux = v2_add(e->u.pos, v2_scale(dir, len));
    e->pending_t = 0.9f;
    e->cast_lock = 0.9f;
}

static void cast_shield(Ctx *c) {
    Enemy *e = c->e;
    float pct = e->is_boss ? 0.12f : 0.25f;
    e->shield_hp = maxf(e->shield_hp, e->u.max_hp * pct);
    e->shielded = true;
    e->cast_lock = 0.5f;
}

static void cast_heal_allies(Ctx *c) {
    Run *r = c->r;
    int healed = 0;
    for (int i = 0; i < r->enemy_count; i++) {
        Enemy *a = &r->enemies[i];
        if (i == c->ei || !a->u.alive || v2_dist(a->u.pos, c->e->u.pos) > 500.0f) {
            continue;
        }
        float before = a->u.hp;
        a->u.hp = minf(a->u.hp + a->u.max_hp * 0.12f, a->u.max_hp);
        healed += a->u.hp > before ? 1 : 0;
        sim_emit(r, EV_HEAL, a->u.pos, a->u.hp - before, 1, 0, 0, false, 0x6FCF97);
    }
    Enemy *e = c->e;
    float self = e->u.max_hp * (healed > 0 ? 0.02f * (float)mini(healed, 4) : 0.03f);
    e->u.hp = minf(e->u.hp + self, e->u.max_hp);
    e->cast_lock = 0.5f;
}

static int adds_alive(const Run *r, int parent) {
    int n = 0;
    for (int i = 0; i < r->enemy_count; i++) {
        n += (r->enemies[i].u.alive && r->enemies[i].parent == parent) ? 1 : 0;
    }
    return n;
}

static void do_cast(Ctx *c, const Special *sp) {
    Enemy *e = c->e;
    bool boss = e->is_boss;
    float m = boss ? 1.0f : 0.8f;
    switch (sp->bit) {
    case MAB_SLAM: cast_slam(c, 1.6f * m); break;
    case MAB_NOVA: cast_nova(c, 1.3f * m); break;
    case MAB_CONE: cast_cone(c, 1.5f * m); break;
    case MAB_LINE: cast_line(c, 1.4f * m); break;
    case MAB_RAIN: cast_rain(c, 0.9f * m); break;
    case MAB_POOLS: cast_pools(c); break;
    case MAB_CHARGE: cast_charge(c); break;
    case MAB_SHIELD: cast_shield(c); break;
    case MAB_HEAL_ALLY: cast_heal_allies(c); break;
    case MAB_SUMMON:
        enemy_summon_adds(c->r, c->ei, 2 + rng_range(&c->r->rng, 1, 2) + (boss ? e->phase : 0));
        e->cast_lock = 0.6f;
        break;
    default: break;
    }
    float cd = boss ? sp->cd_boss : sp->cd_elite;
    if (boss && e->phase >= 2) {
        cd *= 0.75f;
    }
    e->ability_cd[sp->cat] = cd;
    e->atk_timer = boss ? 1.0f : attack_interval(e);
    e->u.attack_anim = 1.0f;
}

/* Erlaubte Faehigkeiten je Archetyp (Elite-Muster); Boss darf alle. */
static u32 allowed_abilities(const Enemy *e) {
    if (e->is_boss) {
        return ~0u;
    }
    switch (mdef(e)->arch) {
    case ARCH_GUARDIAN: return MAB_SHIELD | MAB_LINE;
    case ARCH_RANGED: return MAB_LINE | MAB_RAIN;
    case ARCH_TOTEM: return MAB_RAIN;
    case ARCH_DEMON: return MAB_NOVA | MAB_SLAM | MAB_CHARGE | MAB_RAIN | MAB_SHIELD | MAB_CONE | MAB_LINE;
    default: return 0u;
    }
}

static bool special_ok(const Ctx *c, const Special *sp, u32 mask) {
    const Enemy *e = c->e;
    if (!(mask & sp->bit) || e->ability_cd[sp->cat] > 0.0f) {
        return false;
    }
    if (e->is_boss && e->phase < sp->min_phase) {
        return false;
    }
    float max_d = sp->max_d;
    if (!e->is_boss && (sp->bit == MAB_SLAM || sp->bit == MAB_NOVA)) {
        max_d = c->d->range + 110.0f;
    }
    if (sp->bit == MAB_POOLS && !e->is_boss) {
        return false;   /* Elementare hinterlassen Spuren statt Pfuetzen */
    }
    if (sp->bit == MAB_SUMMON && adds_alive(c->r, c->ei) >= MAX_ADDS_PER_PARENT) {
        return false;
    }
    return c->dist >= sp->min_d && c->dist <= max_d;
}

/* Waehlt (gewichtet zufaellig) eine bereite Faehigkeit und wirkt sie. */
static bool try_specials(Ctx *c) {
    u32 mask = allowed_abilities(c->e) & c->d->abilities;
    if (mask == 0u || c->e->atk_timer > 0.0f || c->e->cast_lock > 0.0f) {
        return false;
    }
    int cand[16], n = 0;
    for (int i = 0; i < (int)(sizeof k_specials / sizeof k_specials[0]); i++) {
        if (special_ok(c, &k_specials[i], mask)) {
            cand[n++] = i;
            if (k_specials[i].cat != 0) {
                cand[n++] = i;  /* Support/Flaeche/Ansturm doppelt gewichtet */
            }
        }
    }
    if (n == 0) {
        return false;
    }
    do_cast(c, &k_specials[cand[rng_range(&c->r->rng, 0, n - 1)]]);
    return true;
}

/* ======================================================================== */
/* Adds (Beschwoerer, Boss, Splitter)                                        */
/* ======================================================================== */
static int map_act_of(const Run *r) { return run_act(r); }

static int pick_add_def(Run *r, const Enemy *parent) {
    int act = map_act_of(r);
    int cand[MAX_MONSTERS];
    float w[MAX_MONSTERS];
    int n = 0;
    for (int i = 0; i < g_monster_count && n < MAX_MONSTERS; i++) {
        const MonsterDef *d = &g_monsters[i];
        if (d->is_boss || d->is_elite_only || act < d->act_min || act > d->act_max) {
            continue;
        }
        if (d->arch != ARCH_SWARM && d->arch != ARCH_MELEE) {
            continue;
        }
        cand[n] = i;
        w[n++] = maxf(d->weight, 0.05f);
    }
    if (n == 0) {
        return parent->def;
    }
    return cand[rng_weighted(&r->rng, w, n)];
}

void enemy_summon_adds(Run *r, int ei, int count) {
    Enemy *p = &r->enemies[ei];
    int have = adds_alive(r, ei);
    count = mini(count, MAX_ADDS_PER_PARENT - have);
    for (int k = 0; k < count; k++) {
        float a = rng_frange(&r->rng, 0.0f, 2.0f * PNX_PI);
        float dist = p->u.radius + rng_frange(&r->rng, 30.0f, 70.0f);
        Vec2 pos = v2_add(p->u.pos, v2(cosf(a) * dist, sinf(a) * dist));
        pos = world_nearest_walkable(&r->map, pos, 14.0f);
        int idx = enemy_spawn(r, pick_add_def(r, p), TIER_NORMAL, p->level, pos, 0u, p->pack_id, ei);
        if (idx < 0) {
            return;
        }
        Enemy *e = &r->enemies[idx];
        e->no_loot = true;
        e->u.hp = e->u.max_hp = e->u.max_hp * ADD_HP_FACTOR;
        e->side = p->side;
        wake(e, p->target_hero >= 0 ? p->target_hero : nearest_hero(r, pos, 1e9f, false));
        e->state_t = 0.1f;
        sim_emit(r, EV_HIT_FX, pos, 0, 0, 0, 3, false, 0xB070FF);
    }
}

/* ======================================================================== */
/* Archetypen                                                                */
/* ======================================================================== */
static void ai_brute(Ctx *c) {
    Enemy *e = c->e;
    float reach = melee_reach(c);
    if (c->dist > reach + 30.0f) {
        step_toward(c->r, e, c->t->u.pos, move_speed(c->r, e), reach * 0.75f);
        e->state = ES_CHASE;
        return;
    }
    e->state = ES_ATTACK;
    face_to(e, c->t->u.pos);
    if (e->atk_timer <= 0.0f && e->cast_lock <= 0.0f && !unit_stunned(&e->u)) {
        Vec2 ctr = v2_add(e->u.pos, v2_scale(e->u.face, reach * 0.7f));
        zone_spawn_enemy_tele(c->r, c->ei, SHAPE_CIRCLE, ctr, v2(0, 0), 95.0f, 0.0f, 0.8f,
                              enemy_current_damage(c->r, e) * 1.4f, false, TELE_PHYS);
        e->cast_lock = 0.8f;
        e->atk_timer = attack_interval(e);
        e->u.attack_anim = 1.0f;
    }
}

static void ai_hunter(Ctx *c) {
    Enemy *e = c->e;
    float speed = move_speed(c->r, e);
    if (e->hits_done >= 3 && e->retreat_t <= 0.0f) {
        e->retreat_t = 1.4f;
        e->hits_done = 0;
    }
    if (e->retreat_t > 0.0f) {
        step_away(c->r, e, c->t->u.pos, speed * 1.1f);
        e->state = ES_RECOVER;
        return;
    }
    if (c->dist > 300.0f && e->ability_cd[3] <= 0.0f && e->charge_t <= 0.0f && (c->d->abilities & MAB_CHARGE)) {
        e->charge_t = 0.4f;
        e->ability_cd[3] = 5.0f;
        e->aux = v2_add(c->t->u.pos, v2_scale(v2_norm(v2_sub(c->t->u.pos, e->u.pos)), 80.0f));
    }
    ai_melee(c, false);
}

static void ai_summoner(Ctx *c) {
    Enemy *e = c->e;
    float speed = move_speed(c->r, e);
    if (e->summon_t <= 0.0f && e->cast_lock <= 0.0f && adds_alive(c->r, c->ei) < MAX_ADDS_PER_PARENT) {
        enemy_summon_adds(c->r, c->ei, rng_range(&c->r->rng, 2, 4));
        e->summon_t = rng_frange(&c->r->rng, 8.0f, 12.0f);
        e->cast_lock = 0.6f;
        e->u.attack_anim = 1.0f;
        return;
    }
    if (c->dist < 180.0f && speed > 0.0f) { /* flieht aus dem Nahkampf */
        step_away(c->r, e, c->t->u.pos, speed);
        e->state = ES_RECOVER;
        return;
    }
    ai_ranged(c, 0.75f, 0.0f);
}

static int weakest_ally_in_range(const Run *r, const Enemy *self, float range, float below) {
    int best = -1;
    float bf = below;
    for (int i = 0; i < r->enemy_count; i++) {
        const Enemy *a = &r->enemies[i];
        if (!a->u.alive || v2_dist(a->u.pos, self->u.pos) > range) {
            continue;
        }
        float f = a->u.hp / maxf(a->u.max_hp, 1.0f);
        if (f < bf) {
            bf = f;
            best = i;
        }
    }
    return best;
}

static void ai_healer(Ctx *c) {
    Enemy *e = c->e;
    if (e->heal_t <= 0.0f && e->cast_lock <= 0.0f) {
        int a = weakest_ally_in_range(c->r, e, 450.0f, 0.6f);
        if (a >= 0) {
            Enemy *t = &c->r->enemies[a];
            float before = t->u.hp;
            t->u.hp = minf(t->u.hp + t->u.max_hp * 0.2f, t->u.max_hp);
            sim_emit(c->r, EV_HEAL, t->u.pos, t->u.hp - before, 1, 0, 0, false, 0x6FCF97);
            e->heal_t = 2.5f;
            e->cast_lock = 0.4f;
            e->u.attack_anim = 1.0f;
            return;
        }
    }
    ai_ranged(c, 0.8f, 0.4f);
}

static void ai_explosive(Ctx *c) {
    Enemy *e = c->e;
    if (e->fuse_t > 0.0f) { /* Zuendung laeuft: stehen, dann selbst entfernen */
        e->fuse_t -= SIM_DT;
        e->state = ES_ATTACK;
        if (e->fuse_t <= 0.0f) {
            enemy_kill(c->r, c->g, c->ei, -1);
        }
        return;
    }
    float reach = 75.0f;
    if (c->dist > reach) {
        step_toward(c->r, e, c->t->u.pos, move_speed(c->r, e), reach * 0.7f);
        e->state = ES_CHASE;
        return;
    }
    e->fuse_t = 1.5f + 0.1f;  /* Telegraph (1,5 s) feuert vor der Selbstzerstoerung */
    e->cast_lock = 1.6f;
    zone_spawn_enemy_tele(c->r, c->ei, SHAPE_CIRCLE, e->u.pos, v2(0, 0), 110.0f, 0.0f, 1.5f,
                          enemy_current_damage(c->r, e) * 2.5f, false, TELE_PHYS);
}

static void ai_totem(Ctx *c) {
    Enemy *e = c->e;
    if (e->heal_t <= 0.0f) { /* schuetzt Verbuendete */
        e->heal_t = 5.0f;
        for (int i = 0; i < c->r->enemy_count; i++) {
            Enemy *a = &c->r->enemies[i];
            if (i != c->ei && a->u.alive && v2_dist(a->u.pos, e->u.pos) <= 300.0f) {
                a->shield_hp = maxf(a->shield_hp, a->u.max_hp * 0.1f);
            }
        }
    }
    e->state = ES_ATTACK;
    face_to(e, c->t->u.pos);
    if (c->dist <= c->d->range && e->atk_timer <= 0.0f && e->cast_lock <= 0.0f &&
        world_line_clear_proj(&c->r->map, e->u.pos, c->t->u.pos)) {
        shoot(c, 1.0f);
    }
}

static void ai_guardian(Ctx *c) {
    ai_melee(c, false);
}

static void ai_demon_like(Ctx *c) {
    if (c->d->proj_speed > 0.0f) {
        ai_ranged(c, 0.75f, 0.35f);
    } else {
        ai_melee(c, false);
    }
}

/* ======================================================================== */
/* Boss                                                                      */
/* ======================================================================== */
static void boss_phase_check(Ctx *c) {
    Enemy *e = c->e;
    float frac = e->u.hp / maxf(e->u.max_hp, 1.0f);
    int want = frac <= 0.30f ? 2 : (frac <= 0.60f ? 1 : 0);
    if (want <= e->phase) {
        return;
    }
    e->phase = want;
    e->invuln = PHASE_INVULN_S;
    e->cast_lock = 0.0f;
    e->pending_t = 0.0f;
    e->charge_t = 0.0f;
    sim_emit(c->r, EV_PHASE, e->u.pos, (float)want, want, 0, (u8)TIER_BOSS, false, 0xE5484D);
    if (c->d->abilities & MAB_SUMMON) {
        enemy_summon_adds(c->r, c->ei, 3 + want);
    } else if (c->d->abilities & MAB_SHIELD) {
        cast_shield(c);
    } else {
        cast_nova(c, 1.0f);
    }
    if (want >= 2 && (c->d->abilities & MAB_ENRAGE)) {
        e->atk_speed_mult = 1.35f;
    }
    e->cast_lock = maxf(e->cast_lock, 0.5f);
}

static void boss_enrage_note(Ctx *c) {
    c->r->enrage_t += SIM_DT;
    if (!c->e->enrage_said && c->r->enrage_t >= (float)g_bal.boss_enrage_s) {
        c->e->enrage_said = true;
        sim_emit(c->r, EV_TEXT, c->e->u.pos, 0, 1, 0, (u8)TIER_BOSS, false, 0xE5484D);
    }
}

static void boss_update(Ctx *c) {
    Enemy *e = c->e;
    boss_enrage_note(c);
    boss_phase_check(c);
    if (e->invuln > 0.0f) {
        face_to(e, c->t->u.pos);
        e->state = ES_ATTACK;
        return;
    }
    if (e->cast_lock > 0.0f) {
        return;
    }
    if (try_specials(c)) {
        return;
    }
    if (c->d->proj_speed > 0.0f && c->d->range > 300.0f) {
        ai_ranged(c, 0.7f, 0.0f);
    } else {
        ai_melee(c, false);
    }
}

/* Setzt den Boss zurueck, wenn 5 s lang kein Held in der Arena lebt (Spec 17 Abschnitt 5). */
bool enemy_reset_boss(Run *r, Game *g) {
    (void)g;
    if (!r->boss_spawned || r->boss_dead || r->boss_enemy < 0) {
        return false;
    }
    Enemy *b = &r->enemies[r->boss_enemy];
    if (!b->u.alive) {
        return false;
    }
    bool anyone = false;
    for (int s = 0; s < r->hero_count; s++) {
        anyone |= r->heroes[s].u.alive && run_in_arena(r, r->heroes[s].u.pos);
    }
    if (anyone) {
        r->boss_reset_t = 0.0f;
        return false;
    }
    r->boss_reset_t += SIM_DT;
    if (r->boss_reset_t < BOSS_RESET_S) {
        return false;
    }
    r->boss_reset_t = 0.0f;
    for (int i = 0; i < r->enemy_count; i++) {
        if (i != r->boss_enemy && r->enemies[i].u.alive && r->enemies[i].parent == r->boss_enemy) {
            r->enemies[i].u.alive = false;
            r->enemies[i].state = ES_DEAD;
        }
    }
    for (int z = 0; z < MAX_ZONES; z++) {
        if (r->zones[z].active && r->zones[z].kind == ZONE_ENEMY_TELE) {
            r->zones[z].active = false;
        }
    }
    b->u.hp = b->u.max_hp;
    b->phase = 0;
    b->invuln = 0.0f;
    b->shield_hp = 0.0f;
    b->cast_lock = 0.0f;
    b->pending_t = 0.0f;
    b->charge_t = 0.0f;
    b->atk_speed_mult = 1.0f;
    b->enrage_said = false;
    b->target_hero = -1;
    b->state = ES_IDLE;
    b->u.pos = b->spawn;
    memset(b->threat, 0, sizeof b->threat);
    memset(b->ability_cd, 0, sizeof b->ability_cd);
    r->enrage_t = 0.0f;
    r->arena_sealed = false;
    return true;
}

void boss_spawn(Run *r, Game *g) {
    (void)g;
    if (r->boss_spawned || !r->def || r->def->boss < 0) {
        return;
    }
    Vec2 at = r->map.has_boss_arena ? r->map.arena_center : r->map.exit;
    at = world_nearest_walkable(&r->map, at, 16.0f);
    int idx = enemy_spawn(r, r->def->boss, TIER_BOSS, r->def->level, at, 0u, 0, -1);
    if (idx < 0) {
        return;
    }
    Enemy *e = &r->enemies[idx];
    e->is_boss = true;
    e->no_loot = false;
    e->invuln = BOSS_INTRO_S;
    int s = nearest_hero(r, at, 1e9f, false);
    wake(e, s);
    e->state_t = 0.5f;
    r->boss_enemy = idx;
    r->boss_spawned = true;
    r->arena_sealed = true;
    r->enrage_t = 0.0f;
    r->boss_reset_t = 0.0f;
    sim_emit(r, EV_BOSS_SPAWN, at, 0, idx, 0, (u8)TIER_BOSS, false, 0xE5484D);
}

/* ======================================================================== */
/* Tod-Affixe                                                                */
/* ======================================================================== */
void enemy_apply_affix_death(Run *r, Game *g, int ei) {
    (void)g;
    Enemy *e = &r->enemies[ei];
    if (e->affixes & AFFIX_EXPLOSIVE) {
        zone_spawn_enemy_tele(r, -1, SHAPE_CIRCLE, e->u.pos, v2(0, 0), 120.0f, 0.0f, 0.5f, e->dmg * 2.0f, false, TELE_PHYS);
    }
    if (e->affixes & AFFIX_SPLITTING) {
        for (int k = 0; k < 2; k++) {
            float a = (float)k * PNX_PI + rng_frange(&r->rng, 0.0f, 1.0f);
            Vec2 pos = world_nearest_walkable(&r->map, v2_add(e->u.pos, v2(cosf(a) * 28.0f, sinf(a) * 28.0f)), 12.0f);
            int idx = enemy_spawn(r, e->def, e->tier, e->level, pos, e->affixes & ~(u32)AFFIX_SPLITTING, e->pack_id, -1);
            if (idx < 0) {
                return;
            }
            Enemy *c = &r->enemies[idx];
            c->no_loot = true;
            c->u.max_hp = e->u.max_hp * 0.5f;
            c->u.hp = c->u.max_hp;
            c->u.radius = e->u.radius * 0.75f;
            c->spawn = e->spawn;
            c->side = e->side;
            wake(c, nearest_hero(r, pos, 1e9f, false));
            c->state_t = 0.1f;
        }
    }
}

/* ======================================================================== */
/* Hauptschleife                                                             */
/* ======================================================================== */
static void do_dash(Run *r, Enemy *e) {
    float speed = e->is_boss ? DASH_SPEED_BOSS : e->speed * 3.2f;
    Vec2 before = e->u.pos;
    Vec2 dir = v2_norm(v2_sub(e->aux, e->u.pos));
    if (v2_dist(e->aux, e->u.pos) < 12.0f) {
        e->charge_t = 0.0f;
        return;
    }
    e->u.pos = world_move(&r->map, e->u.pos, v2_scale(dir, minf(speed * SIM_DT, v2_dist(e->aux, e->u.pos))), nav_radius(e));
    clamp_to_arena(r, e);
    note_move(e, before);
    if (v2_dist(e->u.pos, before) < 0.5f) {
        e->charge_t = 0.0f; /* Wand */
    }
}

static void update_combat(Run *r, Game *g, int ei, Enemy *e) {
    if (e->retarget_t <= 0.0f || !target_valid(r, e, e->target_hero)) {
        e->target_hero = pick_target(r, e);
        e->retarget_t = 0.4f;
    }
    if (!e->is_boss && !is_immobile(e) && v2_dist(e->u.pos, e->spawn) > leash_radius(e)) {
        begin_leash(e);
        return;
    }
    if (e->target_hero < 0) {
        e->leash_t += SIM_DT;
        if (e->leash_t > 1.5f) {
            if (e->is_boss || is_immobile(e)) {
                e->state = ES_IDLE;
            } else {
                begin_leash(e);
            }
        }
        return;
    }
    e->leash_t = 0.0f;
    Ctx c = {r, g, ei, e, mdef(e), e->target_hero, &r->heroes[e->target_hero], 0.0f};
    c.dist = v2_dist(e->u.pos, c.t->u.pos);
    if (e->state == ES_AGGRO) {
        e->state_t -= SIM_DT;
        face_to(e, c.t->u.pos);
        if (e->state_t <= 0.0f) {
            e->state = ES_CHASE;
        }
        return;
    }
    if (e->is_boss) {
        boss_update(&c);
        return;
    }
    if (e->cast_lock <= 0.0f && try_specials(&c)) {
        return;
    }
    switch (c.d->arch) {
    case ARCH_RANGED: ai_ranged(&c, 0.8f, 0.4f); break;
    case ARCH_SWARM: ai_melee(&c, true); break;
    case ARCH_BRUTE: ai_brute(&c); break;
    case ARCH_HUNTER: ai_hunter(&c); break;
    case ARCH_SUMMONER: ai_summoner(&c); break;
    case ARCH_HEALER: ai_healer(&c); break;
    case ARCH_EXPLOSIVE: ai_explosive(&c); break;
    case ARCH_TOTEM: ai_totem(&c); break;
    case ARCH_GUARDIAN: ai_guardian(&c); break;
    case ARCH_ELEMENTAL:
    case ARCH_DEMON: ai_demon_like(&c); break;
    default: ai_melee(&c, false); break;
    }
}

void enemy_ai_update(Run *r, Game *g, int ei) {
    Enemy *e = &r->enemies[ei];
    if (!e->u.alive || e->state == ES_DEAD || e->state == ES_DORMANT) {
        return;
    }
    tick_timers(e);
    e->u.vel = v2(0.0f, 0.0f);
    if (!tick_statuses(r, g, ei, e)) {
        return;
    }
    regen_tick(e);
    if (e->state == ES_IDLE) {
        update_idle(r, ei, e);
        return;
    }
    if (e->state == ES_LEASH) {
        update_leash(r, e);
        return;
    }
    if (unit_stunned(&e->u)) {
        return;
    }
    if (e->charge_t > 0.0f) {
        do_dash(r, e);
    } else {
        update_combat(r, g, ei, e);
    }
    if (e->u.alive) {
        affix_auras(r, g, ei, e);
        trail_pools(r, ei, e);
    }
}

/* Aktive Gegner dieses Ticks bestimmen (Spec 17 Abschnitt 7: nur nahe Gegner simulieren). */
void enemy_ai_prepare(Run *r) {
    float wake_d = (float)g_bal.dormant_dist;
    float w2 = wake_d * wake_d;
    r->active_n = 0;
    for (int i = 0; i < r->enemy_count; i++) {
        Enemy *e = &r->enemies[i];
        if (!e->u.alive || e->state == ES_DEAD) {
            continue;
        }
        bool near = false;
        for (int s = 0; s < r->hero_count && !near; s++) {
            const RunHero *h = &r->heroes[s];
            near = h->u.alive && v2_dist2(h->u.pos, e->u.pos) <= w2;
        }
        if (e->state == ES_DORMANT) {
            if (!near) {
                continue;
            }
            e->state = ES_IDLE;
        } else if (e->state == ES_IDLE && !near && !e->is_boss) {
            e->state = ES_DORMANT;
            continue;
        }
        r->active_list[r->active_n++] = i;
    }
}

/* Weiche Trennung ueberlappender Gegner (Separation-Steering). */
void enemy_separate_all(Run *r) {
    for (int a = 0; a < r->active_n; a++) {
        Enemy *ea = &r->enemies[r->active_list[a]];
        if (!ea->u.alive || ea->state < ES_AGGRO || ea->state > ES_RECOVER || is_immobile(ea)) {
            continue;
        }
        for (int b = a + 1; b < r->active_n; b++) {
            Enemy *eb = &r->enemies[r->active_list[b]];
            if (!eb->u.alive) {
                continue;
            }
            float rr = (ea->u.radius + eb->u.radius) * 0.8f;
            float dx = eb->u.pos.x - ea->u.pos.x;
            if (dx > rr || dx < -rr) {
                continue;
            }
            float dy = eb->u.pos.y - ea->u.pos.y;
            float d2 = dx * dx + dy * dy;
            if (d2 >= rr * rr) {
                continue;
            }
            float d = sqrtf(d2);
            Vec2 dir = d > 0.01f ? v2(dx / d, dy / d) : v2(1.0f, 0.0f);
            float push = (rr - d) * 0.25f;
            float wa = eb->is_boss ? 0.8f : (ea->is_boss ? 0.0f : 0.5f);
            float wb = ea->is_boss ? 0.8f : (eb->is_boss ? 0.0f : 0.5f);
            if (ea->state != ES_ATTACK && ea->cast_lock <= 0.0f) {
                ea->u.pos = world_move(&r->map, ea->u.pos, v2_scale(dir, -push * wa * 2.0f), nav_radius(ea));
            }
            if (eb->state != ES_ATTACK && eb->state != ES_IDLE && eb->cast_lock <= 0.0f && !is_immobile(eb)) {
                eb->u.pos = world_move(&r->map, eb->u.pos, v2_scale(dir, push * wb * 2.0f), nav_radius(eb));
            }
        }
    }
}
