/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Helden-KI (Spec 16): Zustandsautomat, Wegpunkte, Zielwahl mit Hysterese,
 * Skill-Auswahl nach Prioritaet und Bedingungen, Traenke, Stuck-Recovery,
 * Spielersteuerung (manuell). Alles deterministisch (nur Run.rng). */
#include <math.h>
#include <string.h>

#include "sim/sim_internal.h"

#define WP_REACH 70.0f
#define EXIT_REACH 60.0f
#define STUCK_MIN_MOVE 20.0f
#define LOST_TARGET_S 3.0f
#define REST_START_PCT 50.0f
#define REST_DONE_PCT 95.0f
#define REST_MAX_S 25.0f
#define SPRING_RADIUS 48.0f
#define PICKUP_DETOUR 350.0f
#define FOLLOW_NEAR 250.0f
#define FOLLOW_FAR 400.0f
#define NAV_REPATH_S 0.6f
#define TARGET_SWITCH_MARGIN 30.0f
#define TIER_SCORE 60.0f
#define IGNORE_CLEAR_S 40.0f
#define HEAL_DEFAULT_PCT 70.0f
#define DOT_PULSE_TICKS 15

static const float k_potion_dur[TIER_COUNT] = {8.0f, 10.0f, 14.0f, 18.0f, 24.0f, 30.0f};

/* ======================================================================== */
/* Kleine Helfer                                                             */
/* ======================================================================== */
static bool enemy_ok(const Run *r, int ei) {
    return ei >= 0 && ei < r->enemy_count && r->enemies[ei].u.alive;
}

static float hp_pct(const RunHero *h) { return h->u.hp * 100.0f / maxf(h->u.max_hp, 1.0f); }
static float mana_pct(const RunHero *h) { return h->mana * 100.0f / maxf(h->max_mana, 1.0f); }

bool hero_has_behavior(const RunHero *h, LegendBehavior b) {
    return (int)b > 0 && (int)b < 32 && ((h->behavior_mask >> (unsigned)b) & 1u) != 0u;
}

static bool is_boss_map(const Run *r) { return r->def && r->def->boss >= 0; }

static bool tier_allowed(const Hero *hero, Tier t) { return ((hero->ai.target_tier_mask >> (unsigned)t) & 1u) != 0u; }

static bool is_ignored(const RunHero *h, int ei) {
    for (int i = 0; i < h->ignore_n; i++) {
        if (h->ignore_enemy[i] == ei) {
            return true;
        }
    }
    return false;
}

static void ignore_add(RunHero *h, int ei) {
    if (ei < 0 || is_ignored(h, ei)) {
        return;
    }
    if (h->ignore_n >= 8) {
        memmove(h->ignore_enemy, h->ignore_enemy + 1, sizeof(int) * 7);
        h->ignore_n = 7;
    }
    h->ignore_enemy[h->ignore_n++] = ei;
}

/* ======================================================================== */
/* Effektive Werte (Buffs, Traenke, Legendaer-Effekte)                       */
/* ======================================================================== */
static u32 behavior_mask_from(const StatInputs *in) {
    u32 mask = 0;
    for (int id = 1; id <= 28; id++) {
        if (((in->legendary_mask >> (unsigned)id) & 1u) && g_legend[id].id == id && g_legend[id].behavior > LB_NONE) {
            mask |= 1u << (unsigned)g_legend[id].behavior;
        }
    }
    if ((in->legendary_mask >> LEGEND_BIT_CHAIN_LIGHTNING) & 1u) {
        mask |= 1u << (unsigned)LB_CHAIN_LIGHTNING;
    }
    return mask;
}

static void add_potion_mods(const RunHero *h, StatInputs *in) {
    for (int s = 0; s < POTION_SLOTS; s++) {
        if (h->potion_effect_t[s] <= 0.0f) {
            continue;
        }
        float v = h->potion_value[s];
        switch (h->potion_kind[s]) {
        case POTION_HASTE: in->mods.v[STAT_MOVE_SPEED] += v; break;
        case POTION_IRON:
            in->mods.v[STAT_ARMOR_PCT] += v;
            in->mods.v[STAT_SPELLRES_PCT] += v;
            break;
        case POTION_FRENZY: in->mods.v[STAT_ATTACK_SPEED] += v; break;
        default: break;
        }
    }
}

void hero_recompute_eff(Run *r, Game *g, int slot) {
    RunHero *h = &r->heroes[slot];
    Hero *hero = &g->heroes[h->hero_idx];
    if (hero->stats_dirty) {
        hero_recalc_stats(g, hero);
    }
    StatInputs in = hero->inputs;
    for (int i = 0; i < h->u.st_count; i++) {
        const Status *s = &h->u.st[i];
        if (s->type == ST_BUFF && s->stat >= 0 && s->stat < STAT_COUNT) {
            in.mods.v[s->stat] += s->value;
        }
    }
    add_potion_mods(h, &in);
    h->behavior_mask = behavior_mask_from(&in);
    h->bloodrage_on = hero_has_behavior(h, LB_BLOODRAGE) && h->u.max_hp > 0.0f && h->u.hp < h->u.max_hp * 0.5f;
    if (h->bloodrage_on) {
        in.mods.v[STAT_ATTACK_SPEED] += 30.0f;
    }
    float hp_frac = h->u.max_hp > 0.0f ? h->u.hp / h->u.max_hp : 1.0f;
    float mana_frac = h->max_mana > 0.0f ? h->mana / h->max_mana : 1.0f;
    stats_finalize(&in, &h->eff);
    h->u.max_hp = h->eff.max_hp;
    h->max_mana = h->eff.max_mana;
    h->mana_cost_pct = in.mods.v[STAT_MANA_COST];
    h->u.radius = HERO_RADIUS;
    if (h->u.alive) {
        h->u.hp = clampf(hp_frac * h->u.max_hp, minf(1.0f, h->u.max_hp), h->u.max_hp);
        h->mana = clampf(mana_frac * h->max_mana, 0.0f, h->max_mana);
    }
    h->eff_dirty = false;
}

/* ======================================================================== */
/* Navigation                                                                */
/* ======================================================================== */
bool nav_follow(const GameMap *m, Vec2 *p, float radius, Vec2 goal, float step, NavAgent *ns, float dt, float stop) {
    float dist = v2_dist(*p, goal);
    if (dist <= stop) {
        *ns->len = 0;
        return false;
    }
    *ns->timer -= dt;
    Vec2 target = goal;
    if (!world_line_clear(m, *p, goal, radius)) {
        bool need = *ns->timer <= 0.0f || v2_dist2(*ns->goal, goal) > 48.0f * 48.0f;
        if (need) {
            int n = nav_find_path(m, *p, goal, radius, ns->path, ns->cap);
            *ns->len = n;
            *ns->pos = 0;
            *ns->goal = goal;
            *ns->timer = NAV_REPATH_S;
        }
        while (*ns->pos < *ns->len && v2_dist(*p, ns->path[*ns->pos]) < maxf(step, 6.0f)) {
            (*ns->pos)++;
        }
        if (*ns->pos < *ns->len) {
            target = ns->path[*ns->pos];
        }
    } else {
        *ns->len = 0;
        *ns->pos = 0;
    }
    float d = v2_dist(*p, target);
    float move = minf(step, d);
    if (target.x == goal.x && target.y == goal.y) {
        move = minf(move, maxf(dist - stop + 0.5f, 0.0f));
    }
    if (move <= 0.0f) {
        return true;
    }
    *p = world_move(m, *p, v2_scale(v2_norm(v2_sub(target, *p)), move), radius);
    return true;
}

float hero_move_speed(const Run *r, const RunHero *h) {
    if (unit_stunned(&h->u)) {
        return 0.0f;
    }
    float v = h->eff.move_speed * world_speed_factor(&r->map, h->u.pos);
    v *= 1.0f - clampf(status_max(&h->u, ST_SLOW), 0.0f, 80.0f) / 100.0f;
    return maxf(v, 0.0f);
}

static bool casting_blocks_move(const Run *r, const Game *g, const RunHero *h) {
    if (h->cast_t <= 0.0f) {
        return false;
    }
    int sk = g->heroes[h->hero_idx].skill_slot[clampi(h->casting_slot, 0, SKILL_SLOTS - 1)];
    (void)r;
    return !(sk >= 0 && sk < g_skill_count && (g_skills[sk].flags & SKF_MOVE_WHILE_CAST));
}

static float walk_speed(const Run *r, const Game *g, int slot) {
    const RunHero *h = &r->heroes[slot];
    float v = hero_move_speed(r, h);
    if (g->heroes[h->hero_idx].ai.speed_mode == SPEED_MATCH_ALLY) {
        for (int i = 0; i < r->hero_count; i++) {
            if (i != slot && r->heroes[i].u.alive) {
                v = minf(v, r->heroes[i].eff.move_speed);
            }
        }
    }
    return v;
}

/* Bewegt den Helden Richtung Ziel (Pfad/gerade). true = unterwegs. */
static bool hero_walk(Run *r, Game *g, int slot, Vec2 goal, float stop) {
    RunHero *h = &r->heroes[slot];
    if (casting_blocks_move(r, g, h)) {
        return false;
    }
    float speed = walk_speed(r, g, slot);
    if (speed <= 0.0f) {
        return false;
    }
    NavAgent ag = {h->path, PATH_MAX_POINTS, &h->path_len, &h->path_pos, &h->path_goal, &h->repath_t};
    Vec2 before = h->u.pos;
    bool moving = nav_follow(&r->map, &h->u.pos, h->u.radius, goal, speed * SIM_DT, &ag, SIM_DT, stop);
    if (!moving) {
        return false;
    }
    Vec2 d = v2_sub(h->u.pos, before);
    if (v2_len(d) > 0.01f) {
        h->u.face = v2_norm(d);
    }
    h->u.anim_t += SIM_DT;
    h->moved = true;
    if (v2_dist(h->u.pos, h->stuck_ref) >= STUCK_MIN_MOVE) {
        h->stuck_ref = h->u.pos;
        h->stuck_t = 0.0f;
        h->last_safe_pos = h->u.pos;
    } else {
        h->stuck_t += SIM_DT;
    }
    return true;
}

/* ======================================================================== */
/* Wegpunkte                                                                 */
/* ======================================================================== */
static int nearest_main_wp(const GameMap *m, Vec2 p) {
    int best = 0;
    float bd = 1e18f;
    for (int i = 0; i < m->main_wp_count; i++) {
        float d = v2_dist2(m->waypoints[i].pos, p);
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

/* Wegpunktfolge je Erkundungsmodus (Spec 16, Abschnitt 3). */
static void build_waypoints(const Run *r, RunHero *h, const Hero *hero) {
    const GameMap *m = &r->map;
    h->wp_count = 0;
    h->wp_pos = 0;
    if (hero->ai.explore == EXPLORE_MINIMAL) {
        return;
    }
    bool full = hero->ai.explore == EXPLORE_FULL;
    for (int i = 0; i < m->main_wp_count && h->wp_count < MAX_WAYPOINTS; i++) {
        h->wp_order[h->wp_count++] = i;
        if (!full) {
            continue;
        }
        /* Nebenpfad-Wegpunkte, deren naechster Hauptpunkt dieser ist, direkt danach (Nachbar-Reihenfolge) */
        Vec2 at = m->waypoints[i].pos;
        bool used[MAX_WAYPOINTS] = {false};
        for (;;) {
            int pick = -1;
            float bd = 1e18f;
            for (int k = m->main_wp_count; k < m->wp_count; k++) {
                if (used[k] || nearest_main_wp(m, m->waypoints[k].pos) != i) {
                    continue;
                }
                float d = v2_dist2(m->waypoints[k].pos, at);
                if (d < bd) {
                    bd = d;
                    pick = k;
                }
            }
            if (pick < 0 || h->wp_count >= MAX_WAYPOINTS) {
                break;
            }
            used[pick] = true;
            h->wp_order[h->wp_count++] = pick;
            at = m->waypoints[pick].pos;
        }
    }
    if (full) { /* Nebenpunkte ohne Hauptpunkt-Zuordnung (Karten ohne Hauptpfad) */
        for (int k = m->main_wp_count; k < m->wp_count && m->main_wp_count == 0 && h->wp_count < MAX_WAYPOINTS; k++) {
            h->wp_order[h->wp_count++] = k;
        }
    }
}

/* Nach Wechsel Manuell -> Auto: naechstgelegener Wegpunkt statt Zurueckspringen. */
static void reposition_waypoint(const Run *r, RunHero *h) {
    float bd = 1e18f;
    int best = h->wp_count;
    for (int i = 0; i < h->wp_count; i++) {
        float d = v2_dist2(r->map.waypoints[h->wp_order[i]].pos, h->u.pos);
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    h->wp_pos = best;
}

typedef enum GoalKind { GOAL_WP, GOAL_HUNT, GOAL_ARENA, GOAL_EXIT, GOAL_FOLLOW, GOAL_DONE } GoalKind;

static int find_hunt_target(const Run *r, const RunHero *h, const Hero *hero) {
    if (h->hunting && enemy_ok(r, h->hunt_enemy) && !is_ignored(h, h->hunt_enemy)) {
        return h->hunt_enemy;
    }
    int best = -1;
    float bd = 1e18f;
    for (int i = 0; i < r->enemy_count; i++) {
        const Enemy *e = &r->enemies[i];
        if (!e->u.alive || e->no_loot || e->is_boss || is_ignored(h, i) || !tier_allowed(hero, e->tier)) {
            continue;
        }
        float d = v2_dist2(e->u.pos, h->u.pos);
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

/* Anker des Teams: lebender Held mit den meisten Lebenspunkten (Tank). */
static int team_anchor(const Run *r) {
    int best = -1;
    for (int i = 0; i < r->hero_count; i++) {
        if (r->heroes[i].u.alive && (best < 0 || r->heroes[i].eff.max_hp > r->heroes[best].eff.max_hp + 0.5f)) {
            best = i;
        }
    }
    return best;
}

static GoalKind next_goal(Run *r, Game *g, int slot, Vec2 *goal, float *stop) {
    RunHero *h = &r->heroes[slot];
    const Hero *hero = &g->heroes[h->hero_idx];
    *stop = 20.0f;
    h->hunting = false;
    if (r->hero_count > 1) {
        int anchor = team_anchor(r);
        if (anchor >= 0 && anchor != slot) {
            float d = v2_dist(h->u.pos, r->heroes[anchor].u.pos);
            if (d > FOLLOW_FAR || (d > FOLLOW_NEAR && h->path_len > 0)) {
                *goal = r->heroes[anchor].u.pos;
                *stop = FOLLOW_NEAR;
                return GOAL_FOLLOW;
            }
            if (d <= FOLLOW_FAR && r->heroes[anchor].state != HS_MAP_CLEAR) {
                return GOAL_DONE; /* Folgende warten im Abstand */
            }
        }
    }
    while (h->wp_pos < h->wp_count) {
        Vec2 wp = r->map.waypoints[h->wp_order[h->wp_pos]].pos;
        if (v2_dist(h->u.pos, wp) < WP_REACH) {
            h->wp_pos++;
            h->stuck_count = 0;
            continue;
        }
        *goal = wp;
        return GOAL_WP;
    }
    if (hero->ai.clear == CLEAR_FULL_SWEEP && hero->ai.explore != EXPLORE_MINIMAL) {
        int en = find_hunt_target(r, h, hero);
        if (en >= 0) {
            h->hunting = true;
            h->hunt_enemy = en;
            *goal = r->enemies[en].u.pos;
            *stop = 40.0f;
            return GOAL_HUNT;
        }
    }
    if (is_boss_map(r)) {
        *goal = r->map.has_boss_arena ? r->map.arena_center : r->map.exit;
        *stop = 40.0f;
        return GOAL_ARENA;
    }
    *goal = r->map.exit;
    *stop = EXIT_REACH * 0.5f;
    return v2_dist(h->u.pos, r->map.exit) < EXIT_REACH ? GOAL_DONE : GOAL_EXIT;
}

/* ======================================================================== */
/* Traenke                                                                   */
/* ======================================================================== */
static int potion_cost(const ItemDef *d) {
    int c = d->charges > 0 ? d->charges : g_tiers[d->tier].potion_charges;
    c = maxi(c, 1);
    return (100 + c - 1) / c;
}

static bool potion_effect_active(const RunHero *h, int kind) {
    for (int s = 0; s < POTION_SLOTS; s++) {
        if (h->potion_effect_t[s] > 0.0f && h->potion_kind[s] == kind) {
            return true;
        }
    }
    return false;
}

static const ItemDef *potion_def(const Hero *hero, int ps) {
    const Item *it = &hero->eq[SLOT_POTION1 + ps];
    if (item_is_empty(it) || it->def >= g_item_count || g_items[it->def].kind != IK_POTION) {
        return NULL;
    }
    return &g_items[it->def];
}

/* Trank benutzen (ohne KI-Schwellen). true = verbraucht. */
static bool potion_apply(Run *r, Game *g, int slot, int ps) {
    RunHero *h = &r->heroes[slot];
    Hero *hero = &g->heroes[h->hero_idx];
    const ItemDef *d = potion_def(hero, ps);
    if (!d || !h->u.alive || h->potion_cd[ps] > 0.0f) {
        return false;
    }
    Item *it = &hero->eq[SLOT_POTION1 + ps];
    int cost = potion_cost(d);
    if ((int)it->fill < cost) {
        return false;
    }
    float pct = d->value;
    switch (d->potion_type) {
    case POTION_HEAL:
        if (h->u.hp >= h->u.max_hp - 0.5f) {
            return false;
        }
        combat_heal_hero(r, slot, h->u.max_hp * pct / 100.0f * h->eff.heal_mult);
        break;
    case POTION_MANA:
        if (h->mana >= h->max_mana - 0.5f) {
            return false;
        }
        h->mana = minf(h->mana + h->max_mana * pct / 100.0f, h->max_mana);
        sim_emit(r, EV_MANA, h->u.pos, h->max_mana * pct / 100.0f, 0, 0, 0, false, 0);
        break;
    case POTION_HASTE:
    case POTION_IRON:
    case POTION_FRENZY:
        if (potion_effect_active(h, d->potion_type)) {
            return false;
        }
        h->potion_effect_t[ps] = k_potion_dur[clampi((int)d->tier, 0, TIER_COUNT - 1)];
        h->potion_value[ps] = pct;
        h->potion_kind[ps] = d->potion_type;
        h->eff_dirty = true;
        break;
    default:
        return false;
    }
    it->fill = (u8)maxi((int)it->fill - cost, 0);
    h->potion_cd[ps] = maxf(BASE_POTION_CD, (float)hero->ai.potion[ps].min_cd_x10 / 10.0f);
    return true;
}

/* KI: Schwellen je Slot (Spec 16 Abschnitt 2a). */
static void auto_potions(Run *r, Game *g, int slot) {
    RunHero *h = &r->heroes[slot];
    const Hero *hero = &g->heroes[h->hero_idx];
    for (int ps = 0; ps < POTION_SLOTS; ps++) {
        const ItemDef *d = potion_def(hero, ps);
        if (!d || h->potion_cd[ps] > 0.0f) {
            continue;
        }
        const PotionAi *pa = &hero->ai.potion[ps];
        bool want = false;
        switch (d->potion_type) {
        case POTION_HEAL: want = pa->hp_below > 0 && hp_pct(h) < (float)pa->hp_below; break;
        case POTION_MANA: want = pa->mana_below > 0 && mana_pct(h) < (float)pa->mana_below; break;
        default: want = !pa->only_in_combat || h->in_combat; break;
        }
        if (want && potion_apply(r, g, slot, ps)) {
            return; /* hoechstens ein Trank je Tick */
        }
    }
}

/* ======================================================================== */
/* Zielwahl                                                                  */
/* ======================================================================== */
static float target_score(const Run *r, const Hero *hero, const RunHero *h, int slot, int ei) {
    const Enemy *e = &r->enemies[ei];
    float aggro = (float)g_bal.aggro_radius;
    float d = v2_dist(e->u.pos, h->u.pos);
    float tw = 0.0f;
    if (hero->ai.target_prio == PRIO_HIGHER_TIER) {
        tw = (float)e->tier * TIER_SCORE;
    } else if (hero->ai.target_prio == PRIO_LOWER_TIER) {
        tw = -(float)e->tier * TIER_SCORE;
    }
    float score = tw + (aggro - d) * 0.1f;
    if (e->target_hero >= 0 && e->target_hero != slot && (e->state == ES_CHASE || e->state == ES_ATTACK)) {
        score += 25.0f; /* greift Verbuendeten an */
    } else if (e->target_hero == slot && d < 220.0f) {
        score += 10.0f;
    }
    Archetype a = g_monsters[e->def].arch;
    if (a == ARCH_HEALER) {
        score += 30.0f;
    } else if (a == ARCH_SUMMONER) {
        score += 20.0f;
    }
    if (e->invuln > 0.0f) {
        score -= 1000.0f;
    }
    return score;
}

static int select_target(Run *r, const Game *g, int slot) {
    RunHero *h = &r->heroes[slot];
    const Hero *hero = &g->heroes[h->hero_idx];
    float aggro = (float)g_bal.aggro_radius;
    bool boss_live = r->boss_spawned && enemy_ok(r, r->boss_enemy);
    bool in_arena = run_in_arena(r, h->u.pos);
    if (boss_live && in_arena && r->enemies[r->boss_enemy].invuln <= 0.0f) {
        return r->boss_enemy;
    }
    int cur = h->target;
    if (cur >= 0 && (!enemy_ok(r, cur) || is_ignored(h, cur))) {
        cur = -1;
    }
    if (cur >= 0) { /* Fokus ausser Reichweite: nach 3 s verwerfen */
        float d = v2_dist(r->enemies[cur].u.pos, h->u.pos);
        if (d > aggro * 1.3f) {
            h->lost_target_t += SIM_DT;
            if (hero->ai.refocus_lost && h->lost_target_t >= LOST_TARGET_S) {
                cur = -1;
            }
        } else {
            h->lost_target_t = 0.0f;
        }
    }
    int best = -1;
    float bscore = -1e18f;
    for (int i = 0; i < r->enemy_count; i++) {
        const Enemy *e = &r->enemies[i];
        if (!e->u.alive || is_ignored(h, i)) {
            continue;
        }
        float d = v2_dist(e->u.pos, h->u.pos);
        if (d > aggro) {
            continue;
        }
        bool blocking = e->target_hero == slot && (e->state == ES_CHASE || e->state == ES_ATTACK) && d < 300.0f;
        if (!tier_allowed(hero, e->tier) && !blocking) {
            continue;
        }
        float s = target_score(r, hero, h, slot, i);
        if (s > bscore) {
            bscore = s;
            best = i;
        }
    }
    if (cur >= 0) {
        if (h->target_since < (float)g_bal.target_hysteresis_s) {
            return cur;
        }
        if (best < 0 || bscore < target_score(r, hero, h, slot, cur) + TARGET_SWITCH_MARGIN) {
            return cur;
        }
    }
    return best;
}

/* ======================================================================== */
/* Skill-Auswahl (Spec 05 Abschnitt 4, Spec 16 Abschnitt 2b)                 */
/* ======================================================================== */
static float skill_reach(const SkillDef *s, const SkillAi *ai) {
    float range = s->range;
    if (s->kind == SK_CIRCLE) {
        range = s->radius;
    }
    if (ai->max_dist > 0 && (range <= 0.0f || (float)ai->max_dist < range)) {
        range = (float)ai->max_dist;
    }
    return range;
}

static int lowest_ally(const Run *r, float *frac_out) {
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
    if (frac_out) {
        *frac_out = bf;
    }
    return best;
}

static int allies_below(const Run *r, float pct) {
    int n = 0;
    for (int i = 0; i < r->hero_count; i++) {
        const RunHero *t = &r->heroes[i];
        if (t->u.alive && t->u.hp * 100.0f < pct * t->u.max_hp) {
            n++;
        }
    }
    return n;
}

static bool buff_missing(const RunHero *h, const SkillDef *s, int sk, const SkillAi *ai) {
    if (s->stat < 0) {
        return true;
    }
    const Status *st = status_find((Unit *)&h->u, ST_BUFF, sk, s->stat);
    if (!st) {
        return true;
    }
    return ai->allow_stack && st->time < st->total * 0.25f;
}

static bool wants_support(const Run *r, const RunHero *h, const SkillDef *s, int sk, const SkillAi *ai) {
    float thr = ai->ally_hp_below > 0 ? (float)ai->ally_hp_below : HEAL_DEFAULT_PCT;
    float frac;
    int t = lowest_ally(r, &frac);
    switch (s->kind) {
    case SK_HEAL: return t >= 0 && frac * 100.0f < thr;
    case SK_HEAL_PARTY: return allies_below(r, thr) >= maxi(ai->min_targets, 1);
    case SK_HOT:
        return t >= 0 && frac * 100.0f < thr && !status_find((Unit *)&r->heroes[t].u, ST_HOT, sk, -1);
    case SK_CLEANSE: {
        if (t < 0) {
            return false;
        }
        const Unit *u = &r->heroes[t].u;
        bool bad = status_max(u, ST_DOT) > 0.0f || status_max(u, ST_STUN) > 0.0f;
        return bad || frac * 100.0f < thr;
    }
    case SK_MANA:
        return ai->self_mana_max < 100 ? true : mana_pct(h) < 60.0f;
    default: return false;
    }
}

/* true, wenn die Bedingungen des Skills in Slot ss erfuellt sind (Cooldown/Mana prueft skill_ready). */
static bool skill_wanted(const Run *r, const Game *g, int slot, int ss, int target, bool moving) {
    const RunHero *h = &r->heroes[slot];
    const Hero *hero = &g->heroes[h->hero_idx];
    int sk = hero->skill_slot[ss];
    if (sk < 0 || sk >= g_skill_count || !skill_ready(r, g, slot, ss)) {
        return false;
    }
    const SkillDef *s = &g_skills[sk];
    const SkillAi *ai = &hero->ai.skill[ss];
    float hp = hp_pct(h), mp = mana_pct(h);
    if (hp < (float)ai->self_hp_min || hp > (float)ai->self_hp_max + 0.01f) {
        return false;
    }
    if (mp < (float)ai->self_mana_min || mp > (float)ai->self_mana_max + 0.01f) {
        return false;
    }
    bool have_t = enemy_ok(r, target);
    if (ai->use_mode == USE_COMBAT && !h->in_combat && !(s->flags & SKF_NO_COMBAT_REQ)) {
        return false;
    }
    if (ai->use_mode == USE_MOVING && !moving) {
        return false;
    }
    const Enemy *e = have_t ? &r->enemies[target] : NULL;
    if (e && !((ai->tier_mask >> (unsigned)e->tier) & 1u) && ai->tier_mask != 0) {
        return false;
    }
    float edge = e ? v2_dist(e->u.pos, h->u.pos) - e->u.radius : 1e9f;
    float reach = skill_reach(s, ai);
    int mt = maxi(ai->min_targets, 1);
    switch (s->kind) {
    case SK_STRIKE:
    case SK_PULL:
        if (!have_t || edge > reach + 8.0f) {
            return false;
        }
        return s->kind != SK_PULL || edge > 150.0f;
    case SK_PROJECTILE:
        if (!have_t || edge > reach + 8.0f || !world_line_clear_proj(&r->map, h->u.pos, e->u.pos)) {
            return false;
        }
        return mt <= 1 || sim_count_enemies(r, e->u.pos, 220.0f) >= mt;
    case SK_GROUND:
    case SK_DEBUFF:
        if (s->range > 0.0f && (!have_t || edge > reach + 8.0f)) {
            return false;
        }
        if (!have_t) {
            return false;
        }
        if (s->kind == SK_DEBUFF && s->stat >= 0 && status_find((Unit *)&e->u, ST_DEBUFF, sk, s->stat)) {
            return false;
        }
        return sim_count_enemies(r, e->u.pos, maxf(s->radius, 1.0f)) >= mt;
    case SK_CIRCLE:
        return have_t && sim_count_enemies(r, h->u.pos, s->radius) >= mt;
    case SK_CONE:
        return have_t && edge <= reach + 8.0f && sim_count_enemies(r, h->u.pos, s->range) >= mt;
    case SK_DASH:
        if (s->flags & SKF_DASH_FWD) {
            return have_t && edge > hero_basic_reach(r, g, slot) + 60.0f && edge <= s->range + 60.0f;
        }
        if (s->flags & (SKF_DASH_BACK | SKF_DASH_SIDE)) {
            return ai->self_hp_max < 100 && have_t && edge < 250.0f;
        }
        return false;
    case SK_WARP: return ai->self_hp_max < 100 && have_t && edge < 400.0f;
    case SK_BUFF_SELF:
    case SK_BUFF_PARTY: return buff_missing(h, s, sk, ai);
    case SK_TOTEM: {
        if (!have_t || edge > 450.0f) {
            return false;
        }
        for (int i = 0; i < MAX_TOTEMS; i++) {
            const Totem *t = &r->totems[i];
            if (t->active && t->owner == slot && t->skill == sk && t->life > t->life_total * 0.3f) {
                return false;
            }
        }
        return true;
    }
    case SK_HEAL:
    case SK_HEAL_PARTY:
    case SK_HOT:
    case SK_CLEANSE:
    case SK_MANA: return wants_support(r, h, s, sk, ai);
    default: return false;
    }
}

/* Skills nach Prioritaet (0 zuerst) durchgehen; erster erfuellter wird gewirkt. */
static bool try_skills(Run *r, Game *g, int slot, int target, bool moving) {
    RunHero *h = &r->heroes[slot];
    const Hero *hero = &g->heroes[h->hero_idx];
    if (h->skill_gcd > 0.0f || h->cast_t > 0.0f || unit_stunned(&h->u)) {
        return false;
    }
    int order[SKILL_SLOTS];
    for (int i = 0; i < SKILL_SLOTS; i++) {
        order[i] = i;
    }
    for (int i = 1; i < SKILL_SLOTS; i++) { /* stabile Einfuegesortierung */
        int v = order[i], j = i - 1;
        while (j >= 0 && hero->ai.skill[order[j]].priority > hero->ai.skill[v].priority) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = v;
    }
    for (int i = 0; i < SKILL_SLOTS; i++) {
        int ss = order[i];
        if (skill_wanted(r, g, slot, ss, target, moving) && skill_cast(r, g, slot, ss, target)) {
            return true;
        }
    }
    return false;
}

/* ======================================================================== */
/* Kampf                                                                     */
/* ======================================================================== */
static bool class_is_ranged(const Game *g, const RunHero *h) {
    return g_classes[clampi(g->heroes[h->hero_idx].cls, 0, CLASS_COUNT - 1)].ranged;
}

static void approach_target(Run *r, Game *g, int slot, int t) {
    RunHero *h = &r->heroes[slot];
    const Enemy *e = &r->enemies[t];
    float reach = hero_basic_reach(r, g, slot);
    float desired;
    if (class_is_ranged(g, h)) {
        desired = e->u.radius + minf(reach * 0.8f, 650.0f);
        if (!world_line_clear_proj(&r->map, h->u.pos, e->u.pos)) {
            desired = 60.0f;
        }
    } else {
        desired = e->u.radius + reach - 12.0f;
    }
    hero_walk(r, g, slot, e->u.pos, maxf(desired, 20.0f));
}

static void fight(Run *r, Game *g, int slot, int t) {
    RunHero *h = &r->heroes[slot];
    const Enemy *e = &r->enemies[t];
    h->in_combat = true;
    h->combat_t = 0.0f;
    if (unit_stunned(&h->u)) {
        return;
    }
    if (h->cast_t <= 0.0f) {
        h->u.face = v2_norm(v2_sub(e->u.pos, h->u.pos));
    }
    if (e->invuln <= 0.0f && h->cast_t <= 0.0f) {
        if (try_skills(r, g, slot, t, h->moved)) {
            return;
        }
        if (h->atk_timer <= 0.0f) {
            hero_basic_attack(r, g, slot, t);
        }
    }
    approach_target(r, g, slot, t);
}

/* ======================================================================== */
/* Erholung, Kisten                                                          */
/* ======================================================================== */
static bool nearest_spring(const Run *r, Vec2 p, Vec2 *out) {
    float bd = 1e18f;
    bool found = false;
    for (int i = 0; i < r->map.heal_count; i++) {
        float d = v2_dist2(r->map.heal_springs[i], p);
        if (d < bd) {
            bd = d;
            *out = r->map.heal_springs[i];
            found = true;
        }
    }
    return found;
}

static bool on_spring(const Run *r, Vec2 p) {
    for (int i = 0; i < r->map.heal_count; i++) {
        if (v2_dist(r->map.heal_springs[i], p) <= SPRING_RADIUS) {
            return true;
        }
    }
    return false;
}

/* Heilquelle aufsuchen, wenn die Gesundheit niedrig ist und kein Gegner in der Naehe ist. */
static bool do_rest(Run *r, Game *g, int slot) {
    RunHero *h = &r->heroes[slot];
    Vec2 spring;
    if (!h->resting) {
        if (hp_pct(h) >= REST_START_PCT || h->combat_t < 3.0f || !nearest_spring(r, h->u.pos, &spring)) {
            return false;
        }
        h->resting = true;
        h->rest_t = 0.0f;
        h->rest_spot = spring;
        h->rest_spot_set = true;
    }
    h->rest_t += SIM_DT;
    if (hp_pct(h) >= REST_DONE_PCT || h->rest_t > REST_MAX_S + 20.0f || !h->rest_spot_set) {
        h->resting = false;
        return false;
    }
    if (v2_dist(h->u.pos, h->rest_spot) > SPRING_RADIUS * 0.6f) {
        hero_walk(r, g, slot, h->rest_spot, SPRING_RADIUS * 0.4f);
    } else if (h->rest_t > REST_MAX_S) {
        h->resting = false;
        return false;
    }
    return true;
}

static bool wants_pickup(const Game *g, const RunHero *h, const Pickup *p) {
    return p->active && hero_wants_chest(g, h->hero_idx, p->tier);
}

static bool nearest_pickup(const Run *r, const Game *g, const RunHero *h, float max_d, Vec2 *out) {
    float bd = max_d * max_d;
    bool found = false;
    for (int i = 0; i < MAX_PICKUPS; i++) {
        const Pickup *p = &r->pickups[i];
        if (!wants_pickup(g, h, p)) {
            continue;
        }
        float d = v2_dist2(p->pos, h->u.pos);
        if (d < bd) {
            bd = d;
            *out = p->pos;
            found = true;
        }
    }
    return found;
}

/* Kisten im Vorbeigehen aufnehmen (Pickup-Radius, Spec 16 Abschnitt 1). */
static void collect_pickups(Run *r, Game *g, int slot) {
    RunHero *h = &r->heroes[slot];
    float rad = (float)g_bal.pickup_radius;
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *p = &r->pickups[i];
        if (!wants_pickup(g, h, p) || v2_dist(p->pos, h->u.pos) > rad) {
            continue;
        }
        if (game_hero_pickup_chest(g, h->hero_idx, p->tier)) {
            p->active = false;
            r->pickups_taken++;
            sim_emit(r, EV_CHEST_PICKUP, p->pos, 0, slot, 0, (u8)p->tier, false, 0);
        }
    }
}

/* ======================================================================== */
/* Stuck-Recovery                                                            */
/* ======================================================================== */
static void begin_stuck(Run *r, int slot) {
    RunHero *h = &r->heroes[slot];
    if (h->target >= 0) {
        ignore_add(h, h->target);
        h->target = -1;
    }
    if (h->hunting) {
        ignore_add(h, h->hunt_enemy);
        h->hunting = false;
    }
    h->stuck_count++;
    h->stuck_t = 0.0f;
    h->clear_wait = 0.3f;
    h->state = HS_STUCK;
    h->path_len = 0;
    (void)r;
}

static void state_stuck(Run *r, Game *g, int slot) {
    (void)g;
    RunHero *h = &r->heroes[slot];
    h->clear_wait -= SIM_DT;
    if (h->clear_wait > 0.0f) {
        return;
    }
    Vec2 dest = h->last_safe_pos;
    if (h->stuck_count >= 3 && h->wp_pos < h->wp_count) { /* Mikro-Teleport zum Wegpunkt */
        dest = r->map.waypoints[h->wp_order[h->wp_pos]].pos;
        h->wp_pos++;
        h->stuck_count = 0;
    } else if (h->stuck_count >= 2 && h->wp_pos < h->wp_count) {
        h->wp_pos++;
    }
    h->u.pos = world_nearest_walkable(&r->map, dest, h->u.radius);
    h->stuck_ref = h->u.pos;
    h->path_len = 0;
    h->state = HS_EXPLORE;
}

/* ======================================================================== */
/* Zustaende                                                                 */
/* ======================================================================== */
static void state_explore(Run *r, Game *g, int slot) {
    RunHero *h = &r->heroes[slot];
    int t = select_target(r, g, slot);
    if (t >= 0) {
        h->resting = false;
        if (t != h->target) {
            h->target = t;
            h->target_since = 0.0f;
        }
        h->state = r->enemies[t].is_boss ? HS_BOSS_FIGHT : HS_ENGAGE;
        fight(r, g, slot, t);
        return;
    }
    h->target = -1;
    try_skills(r, g, slot, -1, h->moved);
    if (h->spawn_grace > 0.0f) {
        return;
    }
    if (do_rest(r, g, slot)) {
        return;
    }
    Vec2 goal;
    float stop;
    GoalKind k = next_goal(r, g, slot, &goal, &stop);
    h->state = (k == GOAL_ARENA) ? HS_BOSS_ENTER : HS_EXPLORE;
    if (k == GOAL_DONE) {
        if (!is_boss_map(r) && v2_dist(h->u.pos, r->map.exit) < EXIT_REACH) {
            h->state = HS_MAP_CLEAR;
        }
        return;
    }
    h->hunt_t += SIM_DT;
    Vec2 pick;
    if (k != GOAL_HUNT && nearest_pickup(r, g, h, PICKUP_DETOUR, &pick)) {
        goal = pick;
        stop = 20.0f;
    }
    hero_walk(r, g, slot, goal, stop);
}

static void state_engage(Run *r, Game *g, int slot) {
    RunHero *h = &r->heroes[slot];
    int t = select_target(r, g, slot);
    if (t < 0) {
        h->target = -1;
        h->state = HS_EXPLORE;
        return;
    }
    if (t != h->target) {
        h->target = t;
        h->target_since = 0.0f;
    }
    h->state = r->enemies[t].is_boss ? HS_BOSS_FIGHT : HS_ENGAGE;
    fight(r, g, slot, t);
}

static void state_map_clear(Run *r, Game *g, int slot) {
    RunHero *h = &r->heroes[slot];
    h->state = g->heroes[h->hero_idx].retry_timer > 0.0f ? HS_RETRY_WAIT : HS_MAP_CLEAR;
    h->in_combat = false;
    Vec2 pick;
    if (h->clear_wait < 8.0f && nearest_pickup(r, g, h, 600.0f, &pick)) {
        h->clear_wait += SIM_DT;
        hero_walk(r, g, slot, pick, 20.0f);
    }
}

static void state_manual(Run *r, Game *g, int slot) {
    RunHero *h = &r->heroes[slot];
    if (unit_stunned(&h->u)) {
        return;
    }
    if (enemy_ok(r, h->manual_enemy)) {
        const Enemy *e = &r->enemies[h->manual_enemy];
        h->in_combat = true;
        h->combat_t = 0.0f;
        if (h->cast_t <= 0.0f) {
            h->u.face = v2_norm(v2_sub(e->u.pos, h->u.pos));
            if (h->atk_timer <= 0.0f && e->invuln <= 0.0f) {
                hero_basic_attack(r, g, slot, h->manual_enemy);
            }
        }
        approach_target(r, g, slot, h->manual_enemy);
        return;
    }
    h->manual_enemy = -1;
    if (h->manual_has_goal) {
        if (!hero_walk(r, g, slot, h->manual_goal, 8.0f)) {
            h->manual_has_goal = false;
        }
        return;
    }
    int near = sim_nearest_enemy(r, h->u.pos, hero_basic_reach(r, g, slot) + 40.0f);
    if (near >= 0 && h->cast_t <= 0.0f && h->atk_timer <= 0.0f) {
        hero_basic_attack(r, g, slot, near);
    }
}

/* ======================================================================== */
/* Spawn, Tod, Tick                                                          */
/* ======================================================================== */
void hero_spawn_at_start(Run *r, Game *g, int slot) {
    RunHero *h = &r->heroes[slot];
    Hero *hero = &g->heroes[h->hero_idx];
    int idx = h->hero_idx;
    bool manual = h->manual;
    memset(h, 0, sizeof *h);
    h->hero_idx = idx;
    h->manual = manual;
    Vec2 base = r->map.start;
    if (slot > 0) {
        float a = (float)slot * PNX_PI * 0.5f;
        base = v2_add(base, v2(cosf(a) * 34.0f, sinf(a) * 34.0f));
    }
    h->u.pos = world_nearest_walkable(&r->map, base, HERO_RADIUS);
    h->u.face = v2(1.0f, 0.0f);
    h->u.radius = HERO_RADIUS;
    h->u.alive = true;
    h->target = -1;
    h->manual_enemy = -1;
    h->hunt_enemy = -1;
    h->casting_slot = 0;
    h->exp_rate = game_exp_rate(hero, r->def->level);
    hero->stats_dirty = true;
    hero_recompute_eff(r, g, slot);
    h->u.hp = h->u.max_hp;
    h->mana = h->max_mana;
    h->last_level = hero->level;
    h->last_safe_pos = h->u.pos;
    h->stuck_ref = h->u.pos;
    h->spawn_grace = 1.0f;
    build_waypoints(r, h, hero);
    h->state = manual ? HS_MANUAL : HS_EXPLORE;
}

static void update_dead(Run *r, Game *g, int slot) {
    RunHero *h = &r->heroes[slot];
    if (g->heroes[h->hero_idx].dead_timer > 0.0f || r->state == RUN_CLEARED) {
        return;
    }
    if (run_heroes_alive(r) == 0) {
        r->state = RUN_RESTARTING; /* Team gefallen: Karte neu starten */
        return;
    }
    hero_spawn_at_start(r, g, slot);
    r->hero_dead_count = maxi(r->hero_dead_count - 1, 0);
}

static void tick_timers(Run *r, RunHero *h) {
    h->u.hit_flash = maxf(h->u.hit_flash - SIM_DT * 4.0f, 0.0f);
    h->u.attack_anim = maxf(h->u.attack_anim - SIM_DT * 3.5f, 0.0f);
    for (int i = 0; i < SKILL_SLOTS; i++) {
        h->cd[i] = maxf(h->cd[i] - SIM_DT, 0.0f);
    }
    h->cast_t = maxf(h->cast_t - SIM_DT, 0.0f);
    h->atk_timer = maxf(h->atk_timer - SIM_DT, 0.0f);
    h->skill_gcd = maxf(h->skill_gcd - SIM_DT, 0.0f);
    h->spawn_grace = maxf(h->spawn_grace - SIM_DT, 0.0f);
    h->target_since += SIM_DT;
    h->combat_t += SIM_DT;
    h->last_hit_t += SIM_DT;
    h->ignore_clear_t += SIM_DT;
    if (h->ignore_clear_t > IGNORE_CLEAR_S) {
        h->ignore_clear_t = 0.0f;
        h->ignore_n = 0;
    }
    for (int i = 0; i < POTION_SLOTS; i++) {
        h->potion_cd[i] = maxf(h->potion_cd[i] - SIM_DT, 0.0f);
        if (h->potion_effect_t[i] > 0.0f) {
            h->potion_effect_t[i] -= SIM_DT;
            if (h->potion_effect_t[i] <= 0.0f) {
                h->potion_effect_t[i] = 0.0f;
                h->eff_dirty = true;
            }
        }
    }
    h->in_combat = h->combat_t < 1.5f;
    (void)r;
}

static void tick_status_and_stats(Run *r, Game *g, int slot) {
    RunHero *h = &r->heroes[slot];
    Hero *hero = &g->heroes[h->hero_idx];
    if (status_expire(&h->u, SIM_DT)) {
        h->eff_dirty = true;
    }
    if ((r->tick % DOT_PULSE_TICKS) == 0) { /* HoT als Paket alle 0,5 s */
        for (int i = 0; i < h->u.st_count; i++) {
            if (h->u.st[i].type == ST_HOT) {
                combat_heal_hero(r, slot, h->u.st[i].value * (float)DOT_PULSE_TICKS * SIM_DT);
            }
        }
    }
    if (hero->level != h->last_level) {
        bool up = hero->level > h->last_level;
        h->last_level = hero->level;
        h->eff_dirty = true;
        h->exp_rate = game_exp_rate(hero, r->def->level);
        if (up) {
            sim_emit(r, EV_LEVELUP, h->u.pos, 0, slot, hero->level, 0, false, 0);
        }
    }
    bool rage = hero_has_behavior(h, LB_BLOODRAGE) && h->u.hp < h->u.max_hp * 0.5f;
    if (hero->stats_dirty || rage != h->bloodrage_on) {
        h->eff_dirty = true;
    }
    if (h->eff_dirty) {
        hero_recompute_eff(r, g, slot);
    }
}

static void regenerate(Run *r, RunHero *h) {
    float hp_gain = h->eff.hp_regen * SIM_DT;
    if (h->combat_t > HERO_HP_REGEN_COMBAT_WAIT) {
        hp_gain += h->u.max_hp * (float)g_bal.base_hp_regen_pct / 100.0f * SIM_DT;
    }
    if (on_spring(r, h->u.pos)) {
        hp_gain += h->u.max_hp * HEAL_SPRING_PCT_PER_S / 100.0f * SIM_DT;
    }
    h->u.hp = minf(h->u.hp + hp_gain, h->u.max_hp);
    h->mana = minf(h->mana + h->eff.mana_regen * SIM_DT, h->max_mana);
}

/* Legendaer "Arkane Emission": periodisch Magieprojektil auf einen nahen Gegner. */
static void legend_periodic(Run *r, int slot) {
    RunHero *h = &r->heroes[slot];
    if (!hero_has_behavior(h, LB_ARCANE_EMISSION)) {
        return;
    }
    h->lightning_t -= SIM_DT;
    if (h->lightning_t > 0.0f) {
        return;
    }
    int t = sim_nearest_enemy(r, h->u.pos, 550.0f);
    if (t < 0) {
        return;
    }
    h->lightning_t = 1.5f;
    Projectile *p = proj_alloc(r);
    if (!p) {
        return;
    }
    Vec2 d = v2_norm(v2_sub(r->enemies[t].u.pos, h->u.pos));
    p->kind = PK_HERO;
    p->pos = h->u.pos;
    p->vel = v2_scale(d, 650.0f);
    p->radius = 9.0f;
    p->damage = hero_power(h, true) * 0.5f;
    p->life = 1.4f;
    p->owner = slot;
    p->skill = -1;
    p->spell = true;
    p->homing = true;
    p->homing_target = t;
    p->hits_per_target = 1;
    p->color = 0xB58CFF;
    p->style = 1;
}

void hero_ai_update(Run *r, Game *g, int slot) {
    RunHero *h = &r->heroes[slot];
    if (!g->heroes[h->hero_idx].used) {
        return;
    }
    tick_timers(r, h);
    if (h->state == HS_DEAD) {
        update_dead(r, g, slot);
        return;
    }
    if (!h->u.alive) {
        return;
    }
    bool was_moving = h->moved;
    h->moved = false;
    tick_status_and_stats(r, g, slot);
    regenerate(r, h);
    collect_pickups(r, g, slot);
    legend_periodic(r, slot);
    if (h->state != HS_MANUAL || h->in_combat) {
        auto_potions(r, g, slot);
    }
    h->moved = was_moving && false; /* wird unten durch hero_walk neu gesetzt */
    bool prev_moved = was_moving;
    switch (h->state) {
    case HS_SPAWN: hero_spawn_at_start(r, g, slot); break;
    case HS_EXPLORE:
    case HS_BOSS_ENTER:
        h->moved = prev_moved;
        state_explore(r, g, slot);
        break;
    case HS_ENGAGE:
    case HS_BOSS_FIGHT:
        h->moved = prev_moved;
        state_engage(r, g, slot);
        break;
    case HS_MAP_CLEAR:
    case HS_RETRY_WAIT: state_map_clear(r, g, slot); break;
    case HS_STUCK: state_stuck(r, g, slot); break;
    case HS_MANUAL: state_manual(r, g, slot); break;
    default: break;
    }
    if (!h->moved) { /* Stillstand ist kein "Haengen" */
        h->stuck_t = 0.0f;
        h->stuck_ref = h->u.pos;
    } else if (h->stuck_t >= (float)g_bal.stuck_timeout_s && h->state != HS_STUCK) {
        begin_stuck(r, slot);
    }
}

/* ======================================================================== */
/* Spielersteuerung (manueller Modus, Spec 16 Abschnitt 8)                   */
/* ======================================================================== */
static bool slot_valid(const Run *r, int slot) { return slot >= 0 && slot < r->hero_count; }

void run_set_manual(Run *r, int slot, bool manual) {
    if (!slot_valid(r, slot)) {
        return;
    }
    RunHero *h = &r->heroes[slot];
    if (h->manual == manual) {
        return;
    }
    h->manual = manual;
    if (!h->u.alive || h->state == HS_DEAD || h->state == HS_MAP_CLEAR || h->state == HS_RETRY_WAIT) {
        return;
    }
    h->path_len = 0;
    h->target = -1;
    if (manual) {
        h->state = HS_MANUAL;
        h->manual_has_goal = false;
        h->manual_enemy = -1;
    } else {
        h->state = HS_EXPLORE;
        reposition_waypoint(r, h);
    }
}

void run_manual_move_to(Run *r, int slot, Vec2 world_pos) {
    if (!slot_valid(r, slot) || !r->heroes[slot].manual) {
        return;
    }
    RunHero *h = &r->heroes[slot];
    h->manual_goal = world_nearest_walkable(&r->map, world_pos, h->u.radius);
    h->manual_has_goal = true;
    h->manual_enemy = -1;
}

void run_manual_attack(Run *r, int slot, int enemy_idx) {
    if (!slot_valid(r, slot) || !r->heroes[slot].manual) {
        return;
    }
    RunHero *h = &r->heroes[slot];
    h->manual_enemy = enemy_ok(r, enemy_idx) ? enemy_idx : -1;
    h->manual_has_goal = false;
}

bool run_manual_cast(Run *r, Game *g, int slot, int skill_slot) {
    if (!slot_valid(r, slot)) {
        return false;
    }
    RunHero *h = &r->heroes[slot];
    if (!h->u.alive || h->cast_t > 0.0f || unit_stunned(&h->u)) {
        return false;
    }
    int target = enemy_ok(r, h->manual_enemy) ? h->manual_enemy : sim_nearest_enemy(r, h->u.pos, 700.0f);
    return skill_cast(r, g, slot, skill_slot, target);
}

bool run_manual_potion(Run *r, Game *g, int slot, int potion_slot) {
    if (!slot_valid(r, slot) || potion_slot < 0 || potion_slot >= POTION_SLOTS) {
        return false;
    }
    return potion_apply(r, g, slot, potion_slot);
}
