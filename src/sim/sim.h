/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Simulation eines "Runs" = ein Team (1..4 Helden) auf einer Map.
 *
 * - Feste Zeitschritte (SIM_DT = 1/30 s), deterministisch (Run.rng).
 * - KEIN SDL. Die Darstellung liest nur diese Strukturen und die Events.
 * - Alle Helden simulieren immer, auch wenn sie nicht gerendert werden.
 *
 * Felder, die mit "[oeffentlich]" markiert sind, duerfen von Render/UI gelesen
 * werden. Der Rest ist Interna der Simulation (Dateien in sim/) und darf dort
 * erweitert werden (nur am Strukturende ergaenzen).
 */
#ifndef PNX_SIM_SIM_H
#define PNX_SIM_SIM_H

#include "core/rng.h"
#include "core/types.h"
#include "game/game.h"
#include "world/world.h"

#define MAX_ENEMIES 400
#define MAX_PROJECTILES 256
#define MAX_ZONES 96
#define MAX_TOTEMS 12
#define MAX_PICKUPS 64
#define MAX_STATUS 16
#define SIM_EVENT_RING 1024
#define PATH_MAX_POINTS 96

/* ---- Statuseffekte ------------------------------------------------------ */
typedef enum StatusType {
    ST_NONE = 0,
    ST_BUFF,    /* stat/value auf den Traeger (Stat-Modifikator) */
    ST_DEBUFF,  /* stat/value auf Gegner (ENEMY_*-Stats) */
    ST_DOT,     /* value = Schaden pro Sekunde */
    ST_HOT,     /* value = Heilung pro Sekunde */
    ST_SLOW,    /* value = Prozent Tempoverlust */
    ST_STUN,
    ST_TAUNT    /* Gegner greifen den Traeger bevorzugt an */
} StatusType;

typedef struct Status {
    u8 type;        /* StatusType */
    i8 stat;        /* Stat oder -1 */
    i16 skill;      /* Quell-Skill oder -1 (Stapel-Regel: gleicher Skill ersetzt) */
    float value;
    float time;     /* Restdauer */
    float total;    /* Gesamtdauer (fuer Anzeige) */
    i8 source;      /* Heldenslot der Quelle (0..3) oder -1 */
    bool spell;     /* DoT-Schadensart */
} Status;

/* ---- Einheiten ---------------------------------------------------------- */
typedef struct Unit {
    Vec2 pos;            /* [oeffentlich] Weltpixel */
    Vec2 face;           /* [oeffentlich] Blickrichtung (Einheitsvektor) */
    float radius;        /* [oeffentlich] */
    float hp, max_hp;    /* [oeffentlich] */
    bool alive;          /* [oeffentlich] */
    float hit_flash;     /* [oeffentlich] 0..1 Treffer-Aufblitzen */
    float anim_t;        /* [oeffentlich] Animationszeit */
    float attack_anim;   /* [oeffentlich] 1..0 Angriffs-/Wirkanimation */
    Status st[MAX_STATUS]; /* [oeffentlich] */
    int st_count;
    Vec2 vel;
} Unit;

typedef enum HeroState {
    HS_SPAWN = 0,
    HS_EXPLORE,      /* laeuft Wegpunkte ab */
    HS_ENGAGE,       /* verfolgt/bekaempft Ziel */
    HS_BOSS_ENTER,   /* laeuft zur Boss-Arena */
    HS_BOSS_FIGHT,
    HS_MAP_CLEAR,    /* Map abgeschlossen, wartet */
    HS_RETRY_WAIT,
    HS_DEAD,
    HS_STUCK,        /* Stuck-Recovery */
    HS_MANUAL        /* Spielersteuerung */
} HeroState;

typedef struct RunHero {
    int hero_idx;            /* [oeffentlich] Index in Game.heroes */
    Unit u;                  /* [oeffentlich] */
    float mana, max_mana;    /* [oeffentlich] */
    HeroState state;         /* [oeffentlich] */
    bool manual;             /* [oeffentlich] Spielersteuerung aktiv (Auto aus) */
    bool in_combat;          /* [oeffentlich] */
    float cd[SKILL_SLOTS];       /* [oeffentlich] Restabklingzeit */
    float cd_max[SKILL_SLOTS];   /* [oeffentlich] */
    float potion_cd[POTION_SLOTS]; /* [oeffentlich] */
    float cast_t;            /* [oeffentlich] Restzeit der aktuellen Wirkanimation */
    int casting_slot;
    float exp_rate;          /* [oeffentlich] aktuelle EXP-Rate 0.2..1 */
    int target;              /* [oeffentlich] Gegnerindex oder -1 */
    DerivedStats eff;        /* [oeffentlich] effektive Werte inkl. Buffs */
    /* ---- Interna ---- */
    float atk_timer;
    float target_since;
    float lost_target_t;
    Vec2 manual_goal;
    bool manual_has_goal;
    int manual_enemy;
    Vec2 path[PATH_MAX_POINTS];
    int path_len, path_pos;
    Vec2 goal;
    float repath_t;
    int wp_order[MAX_WAYPOINTS];
    int wp_count, wp_pos;
    Vec2 stuck_ref;
    float stuck_t;
    float potion_effect_t[POTION_SLOTS];
    float last_hit_t;
    float mana_regen_acc;
    bool eff_dirty;
    float taunt_cd;
    float clear_wait;
    u32 buff_cast_mask;
    Vec2 last_safe_pos;
    /* ---- weitere Interna (additiv) ---- */
    int ignore_enemy[8];     /* Gegner, die nicht erreichbar waren (Jagd) */
    int ignore_n;
    int stuck_count;
    float rest_t;            /* Rastzeit (Erholung ausserhalb des Kampfes) */
    bool resting;
    bool hunting;            /* Full-Sweep: laeuft gezielt zu einem Rest-Gegner */
    int hunt_enemy;
    bool arena_entered;
    float skill_gcd;         /* kurze Sperre zwischen zwei Skills */
    float combat_t;          /* Sekunden seit letztem Kampfkontakt */
    float hp_prev;
    float fountain_t;
    Vec2 path_goal;
    float lightning_t;       /* Legendaer: periodische Effekte */
    u32 last_skill_mask;
    /* ---- weitere Interna (additiv, run.c/hero_ai.c/skills_exec.c) ---- */
    int last_level;          /* Heldenlevel beim letzten Tick (Levelup-Erkennung) */
    float mana_cost_pct;     /* Manakosten-Modifikator in % (Stat MANA_COST) */
    bool moved;              /* hat sich in diesem Tick bewegt */
    Vec2 rest_spot;          /* Ziel der Erholung (Heilquelle) */
    bool rest_spot_set;
    float potion_value[POTION_SLOTS]; /* Staerke des laufenden Trank-Effekts */
    int potion_kind[POTION_SLOTS];    /* PotionType des laufenden Effekts */
    float hunt_t;
    float respawn_wait;      /* Wartezeit nach dem Wiedereintritt (kurze Schonfrist) */
    float spawn_grace;
    /* ---- weitere Interna (additiv, hero_ai.c) ---- */
    u32 behavior_mask;       /* Bit je LegendBehavior (aus Ausruestung/Set) */
    bool bloodrage_on;       /* Legendaer "Blutrausch" gerade aktiv */
    float ignore_clear_t;    /* Zeit bis die Ignorierliste geleert wird */
} RunHero;

typedef enum EnemyState {
    ES_DORMANT = 0, ES_IDLE, ES_AGGRO, ES_CHASE, ES_ATTACK, ES_RECOVER, ES_LEASH, ES_DEAD
} EnemyState;

typedef struct Enemy {
    Unit u;                  /* [oeffentlich] */
    int def;                 /* [oeffentlich] Index g_monsters */
    Tier tier;               /* [oeffentlich] */
    int level;               /* [oeffentlich] */
    u32 affixes;             /* [oeffentlich] Bitmaske AFFIX_* */
    EnemyState state;        /* [oeffentlich] */
    bool is_boss;            /* [oeffentlich] */
    int phase;               /* [oeffentlich] Bossphase 0..2 */
    bool shielded;           /* [oeffentlich] */
    bool telegraphing;       /* [oeffentlich] */
    float dmg;               /* Basisschaden (vor Ruestung) */
    float armor, spell_res;
    float speed;
    Vec2 spawn;
    int pack_id;
    int target_hero;         /* Heldenslot oder -1 */
    float atk_timer;
    float ability_cd[4];
    float state_t;
    float threat[MAX_TEAM];
    float leash_t;
    float summon_t;
    float charge_t;
    int parent;              /* fuer Adds: Index des Beschwoerers oder -1 */
    Vec2 aux;
    /* ---- weitere Interna (additiv) ---- */
    bool side;               /* stammt von einem Nebenpfad-Packpunkt */
    bool no_loot;            /* Adds/Explosionen: keine Belohnung */
    float invuln;            /* Unverwundbarkeit (Bossphase) */
    float shield_hp;         /* Schild (Affix/Faehigkeit) */
    float regen_acc;
    float aura_t, trail_t, pull_t;
    float cast_lock;         /* steht still (Telegraph/Wirken) */
    int hits_done;           /* Treffer seit letztem Rueckzug (Jaeger) */
    float shield_cd;
    float alert_t;
    float retarget_t;
    float heal_t;
    float fuse_t;            /* Explosive: Zuendung */
    float ranged_cd;
    float out_of_combat_t;
    Vec2 nav_path[4];
    int nav_len, nav_pos;
    Vec2 nav_goal;
    float nav_t;
    float atk_speed_mult;    /* Affix/Phasen */
    /* ---- weitere Interna (additiv, enemy_ai.c) ---- */
    float retreat_t;         /* Jaeger/Beschwoerer: weicht zurueck */
    float pending_t;         /* Boss: Ansturm nach Telegraph ausstehend */
    bool enrage_said;        /* Enrage-Hinweis schon gesendet */
} Enemy;

/* Affix-Bits (Spec 15/17) */
#define AFFIX_FAST 0x0001u
#define AFFIX_ARMORED 0x0002u
#define AFFIX_REGEN 0x0004u
#define AFFIX_VAMPIRIC 0x0008u
#define AFFIX_EXPLOSIVE 0x0010u
#define AFFIX_SPLITTING 0x0020u
#define AFFIX_SHIELDED 0x0040u
#define AFFIX_PULLING 0x0080u
#define AFFIX_POISON_TRAIL 0x0100u
#define AFFIX_FROST_AURA 0x0200u
#define AFFIX_FIRE_AURA 0x0400u

typedef enum ProjKind { PK_HERO = 0, PK_ENEMY } ProjKind;

typedef struct Projectile {
    bool active;             /* [oeffentlich] */
    ProjKind kind;           /* [oeffentlich] */
    Vec2 pos, vel;           /* [oeffentlich] */
    float radius;            /* [oeffentlich] Trefferradius */
    float damage;
    float life;              /* Restlebensdauer */
    int owner;               /* Heldenslot bzw. Gegnerindex */
    int skill;               /* Skill-Index oder -1 (Basisangriff) */
    int pierce_left;
    float explode_radius;
    bool spell;
    bool homing;
    int homing_target;
    int hits_per_target;
    u32 color;               /* [oeffentlich] */
    u8 style;                /* [oeffentlich] 0 Pfeil 1 Kugel 2 Dolch 3 Licht 4 Blitz 5 Feuer */
    u16 hit_ids[8];
    u8 hit_cnt[8];
    int hit_n;
    float dot_dps, dot_dur;
    u32 flags;
} Projectile;

typedef enum ZoneKind {
    ZONE_HERO_AREA = 0,   /* Heldenskill: verzoegerter/bleibender Flaechenschaden */
    ZONE_ENEMY_TELE,      /* Gegner-Telegraph: Schaden erst nach 'delay' */
    ZONE_ENEMY_POOL,      /* bleibende Gefahrenzone (Gift/Feuer) */
    ZONE_POISON_CLOUD,    /* Heldenskill Giftnebel */
    ZONE_HEAL_SPRING
} ZoneKind;
typedef enum ZoneShape { SHAPE_CIRCLE = 0, SHAPE_CONE, SHAPE_LINE } ZoneShape;

typedef struct Zone {
    bool active;             /* [oeffentlich] */
    ZoneKind kind;           /* [oeffentlich] */
    ZoneShape shape;         /* [oeffentlich] */
    Vec2 pos;                /* [oeffentlich] */
    Vec2 dir;                /* [oeffentlich] Kegel/Linie */
    float radius;            /* [oeffentlich] Radius bzw. Kegel-/Linienlaenge */
    float width_or_angle;    /* [oeffentlich] Linienbreite (px) bzw. Kegelwinkel (Grad) */
    float delay;             /* [oeffentlich] Restzeit bis Wirkung (Telegraph) */
    float delay_total;       /* [oeffentlich] */
    float life;              /* Restdauer nach Wirkung (bleibende Zonen) */
    float tick;              /* Tick-Intervall */
    float tick_t;
    float damage;            /* pro Treffer bzw. pro Tick */
    bool spell;
    int owner;
    int skill;
    u32 color;               /* [oeffentlich] */
    bool fired;
    u32 flags;
} Zone;

typedef struct Totem {
    bool active;             /* [oeffentlich] */
    int type;                /* [oeffentlich] TotemType */
    Vec2 pos;                /* [oeffentlich] */
    float radius;            /* [oeffentlich] */
    float life, life_total;  /* [oeffentlich] */
    int owner;               /* Heldenslot */
    int skill;
    int skill_level;
    float tick_t;
    float hp;
    float dmg;               /* Schaden pro Tick (vorberechnet) */
    float aux;
} Totem;

typedef struct Pickup {
    bool active;             /* [oeffentlich] */
    Vec2 pos;                /* [oeffentlich] */
    Tier tier;               /* [oeffentlich] */
    float age;               /* [oeffentlich] */
} Pickup;

/* ---- Ereignisse (fuer Render/Audio) -------------------------------------- */
typedef enum SimEventType {
    EV_NONE = 0,
    EV_DAMAGE,      /* pos, value, flag=crit, a=Ziel ist Held(1)/Gegner(0), b=poison */
    EV_HEAL,        /* pos, value */
    EV_MANA,        /* pos, value */
    EV_KILL,        /* pos, tier */
    EV_HERO_DEATH,  /* pos, a=Heldenslot */
    EV_LEVELUP,     /* pos, a=Heldenslot, b=neues Level */
    EV_CHEST_DROP,  /* pos, tier */
    EV_CHEST_PICKUP,/* pos, tier, a=Heldenslot */
    EV_SKILL_CAST,  /* pos, a=Heldenslot, b=Skill-Index */
    EV_HIT_FX,      /* pos, tier=Stil */
    EV_BOSS_SPAWN,  /* pos */
    EV_MAP_CLEAR,   /* pos */
    EV_TEXT,        /* pos, text-Index nicht noetig: a = Code (siehe sim.c) */
    EV_EXPLOSION,   /* pos, value=Radius, a=Farbe */
    EV_PROJ_LAUNCH, /* pos, a = Stil */
    EV_PHASE        /* pos, Bossphase */
} SimEventType;

typedef struct SimEvent {
    SimEventType type;
    Vec2 pos;
    float value;
    int a, b;
    u8 tier;
    bool flag;
    u32 color;
} SimEvent;

typedef enum RunState {
    RUN_ACTIVE = 0,
    RUN_CLEARED,    /* Boss tot / Ziel erreicht; wartet auf Retry-Timer */
    RUN_RESTARTING
} RunState;

typedef struct Run {
    int map_idx;                 /* [oeffentlich] */
    const MapDef *def;           /* [oeffentlich] */
    GameMap map;                 /* [oeffentlich] */
    RunHero heroes[MAX_TEAM];    /* [oeffentlich] */
    int hero_count;              /* [oeffentlich] */
    Enemy enemies[MAX_ENEMIES];  /* [oeffentlich] Pool: alive-Flag in u.alive */
    int enemy_count;             /* [oeffentlich] belegte Eintraege (hoechster Index + 1) */
    Projectile proj[MAX_PROJECTILES]; /* [oeffentlich] */
    Zone zones[MAX_ZONES];       /* [oeffentlich] */
    Totem totems[MAX_TOTEMS];    /* [oeffentlich] */
    Pickup pickups[MAX_PICKUPS]; /* [oeffentlich] */
    SimEvent ev[SIM_EVENT_RING];
    int ev_head, ev_tail;
    Rng rng;
    u32 tick;
    float time;                  /* [oeffentlich] Laufzeit dieses Durchlaufs in s */
    RunState state;              /* [oeffentlich] */
    float clear_timer;           /* Retry-Wartezeit */
    bool boss_spawned;           /* [oeffentlich] */
    bool boss_dead;              /* [oeffentlich] */
    int boss_enemy;              /* [oeffentlich] Index des Bosses oder -1 */
    float enrage_t;
    int kills;                   /* [oeffentlich] */
    int alive_count;             /* [oeffentlich] lebende Gegner */
    int total_spawned;           /* [oeffentlich] */
    int runs_completed;          /* Anzahl Durchlaeufe seit Erstellung */
    u32 next_pack_id;
    bool arena_sealed;           /* [oeffentlich] Arena abgesperrt */
    /* ---- weitere Interna (additiv) ---- */
    int main_spawned;            /* Gegner ohne Adds/Splitter bei Start */
    int main_killed;
    float boss_reset_t;          /* Zeit ohne lebenden Helden in der Arena */
    float clear_time;            /* Dauer bis zum Abschluss */
    int hero_dead_count;
    float enemy_tick_acc;
    /* ---- weitere Interna (additiv) ---- */
    bool populating;             /* Startbesetzung wird gerade angelegt */
    int active_list[MAX_ENEMIES];/* aktive (nicht schlafende) lebende Gegner dieses Ticks */
    int active_n;
    bool clear_reported;         /* game_on_map_clear wurde gerufen */
    float exit_hold;
    int pickups_taken;
} Run;

/* ======================================================================== */
/* API                                                                       */
/* ======================================================================== */
/* Legt einen Run an (Karte bauen, Gegner platzieren, Helden am Start).
 * hero_idx: Indizes in Game.heroes (1..max_heroes der Map). NULL bei Fehler. */
Run *run_create(Game *g, int map_idx, const int *hero_idx, int hero_count);
void run_destroy(Run *r);
/* Ein fester Zeitschritt (SIM_DT). Ruft game_on_kill & Co. */
void run_tick(Run *r, Game *g);
/* Startet die Map neu (Retry): Gegner neu, Helden am Start, Tränke voll. */
void run_restart(Run *r, Game *g);
/* Zeigerhelfer */
RunHero *run_find_hero(Run *r, int hero_idx);       /* NULL wenn nicht im Run */
int run_slot_of_hero(const Run *r, int hero_idx);   /* -1 wenn nicht im Run */

/* ---- Spielersteuerung (manueller Modus) --------------------------------- */
void run_set_manual(Run *r, int slot, bool manual);              /* Leertaste */
void run_manual_move_to(Run *r, int slot, Vec2 world_pos);
void run_manual_attack(Run *r, int slot, int enemy_idx);
bool run_manual_cast(Run *r, Game *g, int slot, int skill_slot); /* Tasten Q W E R */
bool run_manual_potion(Run *r, Game *g, int slot, int potion_slot); /* Tasten 1 2 3 */
/* Gegner unter Weltposition (innerhalb Radius) oder -1. */
int run_enemy_at(const Run *r, Vec2 world_pos, float extra_radius);

/* ---- Ereignisse ---------------------------------------------------------- */
bool run_pop_event(Run *r, SimEvent *out);  /* false wenn leer */
void run_clear_events(Run *r);

/* ---- Abfragen fuer UI ----------------------------------------------------- */
int run_enemies_alive(const Run *r);
/* Fortschritt 0..1 der Map (besiegte Gegner / gesamt, Boss zaehlt als Ende). */
float run_progress(const Run *r);
const char *hero_state_name(HeroState s);   /* deutsch: "Erkundet", "Kaempft" ... */

#endif /* PNX_SIM_SIM_H */
