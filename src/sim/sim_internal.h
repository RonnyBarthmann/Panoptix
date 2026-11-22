/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Interne Schnittstellen zwischen den Dateien in src/sim/ (nicht ausserhalb nutzen). */
#ifndef PNX_SIM_SIM_INTERNAL_H
#define PNX_SIM_SIM_INTERNAL_H

#include "core/util.h"
#include "rules/rules.h"
#include "sim/sim.h"

#define HERO_RADIUS 14.0f
#define MELEE_REACH_SLACK 6.0f
#define HEAL_SPRING_PCT_PER_S 12.0f   /* Heilquelle: % Max-HP pro Sekunde */
#define HERO_HP_REGEN_COMBAT_WAIT 4.0f
#define BASE_POTION_CD 3.0f

/* ---- run.c ----------------------------------------------------------------- */
void sim_emit(Run *r, SimEventType t, Vec2 pos, float value, int a, int b, u8 tier, bool flag, u32 color);
/* Neuen Gegner anlegen; -1 wenn der Pool voll ist. */
int enemy_spawn(Run *r, int def, Tier tier, int level, Vec2 pos, u32 affixes, int pack_id, int parent);
/* Gegner stirbt (killer_slot < 0: kein Held => keine Belohnung). */
void enemy_kill(Run *r, Game *g, int ei, int killer_slot);
void pickup_spawn(Run *r, Vec2 pos, Tier tier);
void hero_die(Run *r, Game *g, int slot);
/* Anzahl lebender Helden. */
int run_heroes_alive(const Run *r);
bool run_in_arena(const Run *r, Vec2 p);

/* ---- combat.c --------------------------------------------------------------- */
typedef struct Hit {
    int slot;        /* Heldenslot der Quelle */
    float amount;    /* Schaden vor Ruestung (inkl. Multiplikatoren) */
    bool spell;
    bool nocrit;
    bool dot;
    int skill;
} Hit;
float combat_hit_enemy(Run *r, Game *g, int ei, const Hit *h);
void combat_hit_hero(Run *r, Game *g, int slot, float raw, bool spell, int src_enemy);
void combat_heal_hero(Run *r, int slot, float amount);
float hero_power(const RunHero *h, bool spell);
bool hero_roll_crit(Run *r, const RunHero *h);
void combat_update_projectiles(Run *r, Game *g);
void combat_update_zones(Run *r, Game *g);
void combat_update_totems(Run *r, Game *g);
Projectile *proj_alloc(Run *r);
void zone_spawn_enemy_tele(Run *r, int owner, ZoneShape shape, Vec2 pos, Vec2 dir, float radius, float width_angle,
                           float delay, float damage, bool spell, u32 color);
void zone_spawn_pool(Run *r, int owner, Vec2 pos, float radius, float life, float dps, bool spell, u32 color);

/* Statuseffekte */
Status *status_find(Unit *u, int type, int skill, int stat);
/* Setzt/ersetzt einen Status. true = neu oder Wert geaendert (Stat-Cache ungueltig). */
bool status_apply(Unit *u, Status s);
/* Zeit abziehen, abgelaufene entfernen. true = ein Stat-relevanter Status ist verschwunden. */
bool status_expire(Unit *u, float dt);
float status_max(const Unit *u, int type);   /* groesster Wert (z. B. Verlangsamung) */
float status_stat_sum(const Unit *u, int type, int stat);
bool unit_stunned(const Unit *u);
void status_remove_type(Unit *u, int type);
Status status_make(int type, int stat, int skill, float value, float time, int source, bool spell);

/* ---- hero_ai.c --------------------------------------------------------------- */
void hero_recompute_eff(Run *r, Game *g, int slot);
void hero_ai_update(Run *r, Game *g, int slot);
void hero_spawn_at_start(Run *r, Game *g, int slot);
float hero_move_speed(const Run *r, const RunHero *h);
/* Bewegung auf ein Ziel (Pfad/gerade). true = unterwegs. */
typedef struct NavAgent {
    Vec2 *path;
    int cap;
    int *len, *pos;
    Vec2 *goal;
    float *timer;
} NavAgent;
bool nav_follow(const GameMap *m, Vec2 *p, float radius, Vec2 goal, float step, NavAgent *ns, float dt, float stop);

/* ---- enemy_ai.c --------------------------------------------------------------- */
void enemy_ai_update(Run *r, Game *g, int ei);
void enemy_ai_prepare(Run *r);
bool enemy_is_spell(const Enemy *e);
float enemy_current_damage(const Run *r, const Enemy *e);
void enemy_alert_pack(Run *r, int ei, int hero_slot);
void enemy_aggro(Run *r, int ei, int hero_slot);
void boss_spawn(Run *r, Game *g);
void enemy_apply_affix_death(Run *r, Game *g, int ei);

/* ---- skills_exec.c ------------------------------------------------------------- */
/* Prueft Cooldown/Mana/Reichweite und fuehrt den Skill aus. target = Gegnerindex oder -1. */
bool skill_cast(Run *r, Game *g, int slot, int skill_slot, int target);
bool skill_ready(const Run *r, const Game *g, int slot, int skill_slot);
int skill_level_of(const Game *g, int skill);
float skill_cooldown_of(const RunHero *h, const SkillDef *s);
float skill_mana_cost(const RunHero *h, const SkillDef *s);
/* Nutzt Basisangriff; true wenn ausgefuehrt. */
bool hero_basic_attack(Run *r, Game *g, int slot, int target);
float hero_basic_reach(const Run *r, const Game *g, int slot);
/* Totem-Hilfen */
void totem_expire(Run *r, Game *g, int ti);

#endif
