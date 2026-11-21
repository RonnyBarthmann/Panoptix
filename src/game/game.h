/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Persistenter Spielzustand: Kommandeur (Spieler), Helden, Lager, Fortschritt.
 * Alles hier wird gespeichert (src/game/save.c). Laufzeit-Kampfzustand
 * (Positionen, Gegner, Projektile) liegt NICHT hier, sondern in sim/sim.h (Run).
 */
#ifndef PNX_GAME_GAME_H
#define PNX_GAME_GAME_H

#include "core/rng.h"
#include "core/types.h"
#include "data/data.h"

struct Run; /* sim/sim.h */

/* ======================================================================== */
/* Items (Instanzen)                                                         */
/* ======================================================================== */
#define ITEMF_LOCKED 0x01u
#define ITEMF_SEALED 0x02u /* Verzauberung ist "gesiegelt" */

typedef enum EnchantOpt {
    ENCH_NONE = 0,
    ENCH_FLAT_DMG,      /* flacher Angriffs-/Zauberschaden */
    ENCH_DMG_PCT,       /* % Schaden */
    ENCH_ATK_SPEED,
    ENCH_CAST_SPEED,
    ENCH_CDR,
    ENCH_CRIT,
    ENCH_ARMOR_FLAT,
    ENCH_ARMOR_PCT,
    ENCH_HP_PCT,
    ENCH_DR_ATTACK,     /* % weniger physischer Schaden */
    ENCH_DR_SPELL,      /* % weniger Zauberschaden */
    ENCH_MOVE_FLAT,
    ENCH_MOVE_PCT,
    ENCH_SKILL_DMG,     /* Sekundaerwaffe: % mehr Schaden auf Skill 'ench_skill' */
    ENCH_SKILL_CAST,    /* Sekundaerwaffe: % Zaubertempo fuer Skill */
    ENCH_SKILL_RANGE,   /* Sekundaerwaffe: Reichweite fuer Skill */
    ENCH_SKILL_PROJ,    /* Sekundaerwaffe: Projektilgeschwindigkeit */
    ENCH_SKILL_DUR,     /* Sekundaerwaffe: Dauer (Totems, Buffs) */
    ENCH_COUNT
} EnchantOpt;

typedef struct Item {
    u16 def;         /* Index in g_items oder ITEM_NONE */
    i8 plus;         /* Verbesserungsstufe 0..20 */
    u8 flags;        /* ITEMF_* */
    u8 ench_opt;     /* EnchantOpt */
    u8 ench_grade;   /* 1..n (hoeher = besser) */
    u8 fill;         /* Traenke: Fuellung 0..100 */
    i16 ench_skill;  /* Skill-Index fuer ENCH_SKILL_* (sonst -1) */
    float ench_val;  /* gewuerfelter Wert */
} Item;

static inline Item item_make(int def) {
    Item it = {(u16)def, 0, 0, 0, 0, 100, -1, 0.0f};
    return it;
}
static inline Item item_empty(void) { return item_make(ITEM_NONE); }
static inline bool item_is_empty(const Item *it) { return it->def == ITEM_NONE; }

/* ======================================================================== */
/* Helden-KI-Einstellungen (Spec 16 Abschnitt 3, Spec 05 Abschnitt 4)        */
/* ======================================================================== */
typedef enum ExploreMode { EXPLORE_SHORTEST = 0, EXPLORE_FULL, EXPLORE_MINIMAL } ExploreMode;
typedef enum ClearMode { CLEAR_FULL_SWEEP = 0, CLEAR_BOSS_DIRECT } ClearMode;
typedef enum TargetPrio { PRIO_HIGHER_TIER = 0, PRIO_LOWER_TIER, PRIO_NEAREST } TargetPrio;
typedef enum SpeedMode { SPEED_NONE = 0, SPEED_MATCH_ALLY } SpeedMode;
typedef enum SkillUseMode { USE_ALWAYS = 0, USE_COMBAT, USE_MOVING } SkillUseMode;

typedef struct SkillAi {
    u8 priority;        /* 0..8, 0 = zuerst */
    u8 tier_mask;       /* Zieltier-Filter (Bit je Tier) */
    u8 min_targets;     /* Gegner in Reichweite >= N (0/1 = egal) */
    u16 max_dist;       /* Zielentfernung <= D (0 = egal) */
    u8 self_hp_min, self_hp_max;   /* eigene HP in % (0..100); max 100 = egal */
    u8 self_mana_min, self_mana_max;
    u8 ally_hp_below;   /* Heilung/Support: Verbuendete unter x % (0 = egal) */
    u8 target_allies;   /* 0 = Selbst bevorzugen, 1 = Verbuendete bevorzugen */
    u8 use_mode;        /* SkillUseMode */
    u8 allow_stack;     /* Buff nachlegen bevor er ablaeuft */
    u16 min_cd_x10;     /* eigener Mindest-Cooldown in 1/10 s (0 = Skill-CD) */
    u8 spread_angle;    /* Streuwinkel Mehrfachprojektile (0 = Skill-Standard) */
} SkillAi;

typedef struct PotionAi {
    u8 hp_below;        /* Heil/Eisenhaut/...: nutzen wenn HP < x % (0 = nie wegen HP) */
    u8 mana_below;      /* Manatrank: nutzen wenn Mana < y % */
    u8 only_in_combat;  /* Effekttraenke nur im Kampf */
    u16 min_cd_x10;
} PotionAi;

typedef struct HeroAi {
    u8 explore;         /* ExploreMode */
    u8 clear;           /* ClearMode */
    u8 target_prio;     /* TargetPrio */
    u8 target_tier_mask;/* welche Monster-Tiers aktiv angegriffen werden */
    u8 speed_mode;      /* SpeedMode */
    u8 repeat_map;      /* "Gleiche Map wiederholen" */
    u8 auto_repeat_special; /* Tempel/Ruinen automatisch erneut */
    u8 take_normal_chests;  /* Normal-Kisten aufnehmen */
    u8 take_magic_chests;
    u8 auto_dismantle;      /* Kisten sofort oeffnen + zerlegen (ab Spielerlevel 20) */
    u8 auto_open;
    u8 collect_at_pct;      /* 0 = aus; sonst Kisten automatisch einsammeln bei N % voll */
    u8 refocus_lost;        /* Fokus verlieren nach 3 s ausser Reichweite */
    SkillAi skill[SKILL_SLOTS];
    PotionAi potion[POTION_SLOTS];
} HeroAi;

/* ======================================================================== */
/* Berechnete Werte (rules/stats.h)                                          */
/* ======================================================================== */
/* Eingaben fuer die Stat-Berechnung. Die Simulation kopiert hero->inputs,
 * addiert Buff-Modifikatoren auf 'mods' und ruft stats_finalize() erneut auf. */
typedef struct StatInputs {
    int cls;
    int level;
    float str, dex, intel;      /* Punkte */
    float weapon_dmg;           /* Waffenschaden inkl. Verbesserung (Basis, ohne %mehr) */
    float armor_items;          /* Summe Ruestungsteile inkl. Verbesserung/Sub */
    float base_attack_speed;    /* Klasse * Waffe */
    float range;
    StatMods mods;              /* Summe aller Quellen (Ausruestung, Traits, Set, Level) */
    u32 legendary_mask;         /* aktive Legendaer-Effekte (Bit je Effekt-ID) */
    float spell_extra_flat;
} StatInputs;

typedef struct DerivedStats {
    float max_hp, max_mana;
    float hp_regen, mana_regen;       /* pro Sekunde */
    float armor, spell_res;           /* Werte */
    float dr_attack, dr_spell;        /* flache Abzuege */
    float dmg_reduce;                 /* 0..0.9 Faktor weniger Schaden */
    float attack_power;               /* Basis-Angriffsschaden (Waffe + STR + flach) vor % */
    float spell_power;                /* Basis-Zauberschaden (Waffe + INT + flach) vor % */
    float atk_mult, spell_mult;       /* (1+%erhoeht) * Produkt(%mehr) */
    float crit_chance;                /* 0..100 */
    float crit_dmg;                   /* z.B. 150..400 (in %) */
    float attack_speed;               /* Angriffe pro Sekunde */
    float cast_speed;                 /* Faktor, 1.0 = normal */
    float move_speed;                 /* px/s */
    float cdr;                        /* 0..90 */
    float heal_mult;                  /* Faktor */
    int chest_capacity;
    int extra_projectiles, extra_pierce;
    float range;                      /* Basis-Angriffsreichweite */
    float physical_taken_mult, spell_taken_mult; /* fuer Gegnertreffer: (1-red) */
} DerivedStats;

/* ======================================================================== */
/* Held                                                                      */
/* ======================================================================== */
typedef struct Hero {
    bool used;
    int uid;                    /* eindeutige, nie wiederverwendete ID */
    char name[24];
    int cls;                    /* ClassId */
    u8 look;                    /* Aussehensvariante 0..7 */
    int level;
    u64 exp;
    int str, dex, intel, free_points;
    Item eq[SLOT_COUNT];        /* angelegte Ausruestung */
    i16 skill_slot[SKILL_SLOTS];/* Skill-Index oder -1 */
    HeroAi ai;
    u16 chest_bar[TIER_COUNT];  /* gesammelte, noch nicht eingesammelte Kisten */
    int assigned_map;           /* Map-Index oder -1 */
    float dead_timer;           /* >0: tot, Sekunden bis Wiedereintritt */
    float retry_timer;          /* >0: wartet auf Retry */
    u32 kills;
    u64 total_exp_earned;
    /* ---- Laufzeit-Cache (wird nicht gespeichert) ---- */
    bool stats_dirty;
    StatInputs inputs;
    DerivedStats stats;
} Hero;

/* ======================================================================== */
/* Kommandeur (Spieler)                                                      */
/* ======================================================================== */
#define PLAYER_TRAIT_MAX MAX_TRAITS
#define BOUNTY_PER_DAY 15
#define MAX_MAIL 32
#define CMD_SKILL_COUNT 4
#define CMD_ORDER_COUNT 4
#define STAT_COUNTERS 8

typedef struct BountyQuest {
    i16 def;            /* Index in g_quests oder -1 */
    i32 target;
    i32 progress;
    u8 claimed;
    i32 param;
} BountyQuest;

typedef struct MailMsg {
    char title[48];
    char text[160];
    Currency cur;
    i64 amount;
    bool claimed;
    bool read;
    i64 time;
} MailMsg;

typedef struct Player {
    char name[24];
    int level;
    u64 exp;
    i64 gold;
    i64 diamonds;
    i64 credits;          /* Kommando-Credits (Speicher, max. post_storage_cap) */
    i64 raven_coins;
    i64 raven_feathers;
    int hero_slots_bonus; /* gekaufte Zusatz-Heldenplaetze */
    int trait_points_total;
    u8 traits[PLAYER_TRAIT_MAX];           /* Punkte je Trait */
    double cmd_skill_end[CMD_SKILL_COUNT]; /* Spielzeit (play_time), bis wann der Buff laeuft */
    double cmd_skill_cd[CMD_SKILL_COUNT];  /* Spielzeit, ab wann wieder nutzbar */
    u8 cmd_order_on[CMD_ORDER_COUNT];      /* Kommandoposten-Befehle an/aus */
    i64 post_last_unix;                    /* letzte Verrechnung des Kommandopostens */
    double post_hours_today;               /* heute verrechnete Stunden */
    i64 last_seen_unix;
    int reset_day_key;                     /* Tages-Reset-Schluessel (YYYYMMDD ab 04:00) */
    int reset_week_key;
    int reset_month_key;
    u16 skill_level[MAX_SKILLS];           /* 0 = nicht freigeschaltet, 1..20 */
    u16 skill_books[MAX_SKILLS];           /* ueberzaehlige Buecher (zum Mergen) */
    u16 shop_bought[MAX_SHOP_OFFERS];      /* Kaeufe in aktueller Periode */
    u16 shop_total[MAX_SHOP_OFFERS];       /* Kaeufe insgesamt (Preissteigerung) */
    BountyQuest bounty[BOUNTY_PER_DAY];
    int bounty_refreshes;
    int main_quest_index;                  /* naechste Hauptquest */
    i32 main_quest_progress;
    u32 achievements;                      /* Bitmaske */
    u64 counters[STAT_COUNTERS];           /* Kills, Kisten geoeffnet, Crafts, ... (siehe COUNTER_*) */
    u64 kills_by_tier[TIER_COUNT];
    MailMsg mail[MAX_MAIL];
    int mail_count;
    int free_gacha_days;                   /* gestapelte Gratis-Zuege */
} Player;

enum { COUNTER_KILLS = 0, COUNTER_CHESTS_OPENED, COUNTER_CRAFTED, COUNTER_ENHANCES, COUNTER_MAPS_CLEARED,
       COUNTER_DEATHS, COUNTER_DISMANTLED, COUNTER_BOSSES };

/* ======================================================================== */
/* Lager / Inventar                                                          */
/* ======================================================================== */
#define INV_MAX_ITEMS 1000

typedef struct Inventory {
    Item items[INV_MAX_ITEMS];      /* nicht stapelbare Gegenstaende (Waffen, Ruestung, Schmuck ...) */
    int item_count;
    u32 stock[MAX_ITEM_DEFS];       /* stapelbare (Materialien, Steine, Spezial) je Item-Index */
    u32 chests[TIER_COUNT];         /* eingesammelte, ungeoeffnete Kisten */
    int capacity;                   /* aktuelle Slot-Kapazitaet (200..1000) */
    u8 dismantle_below_tier;        /* Zerlege-Filter: Items unter diesem Tier (0 = aus) */
    u8 dismantle_below_level;       /* ... und unter diesem Level/10 */
} Inventory;

/* ======================================================================== */
/* Welt-Fortschritt und Zuweisungen                                          */
/* ======================================================================== */
typedef struct MapProgress {
    u8 cleared;
    u8 first_claimed;
    u8 repeat;               /* "Wiederholen" fuer diese Map */
    u32 clear_count;
    float best_time;         /* Sekunden, 0 = nie */
    i8 team[MAX_TEAM];       /* Helden-Indizes oder -1 */
} MapProgress;

typedef struct World {
    MapProgress map[MAX_MAPS];
    u8 act_chest_claimed[16];
    int temple_tickets;       /* verbleibende Tickets heute */
    int ruins_tickets;
    int command_points_spent;
} World;

/* ======================================================================== */
/* Spiel-Gesamtzustand                                                       */
/* ======================================================================== */
#define TOAST_MAX 8

typedef struct Toast {
    char text[112];
    u32 color;     /* 0xRRGGBB */
    float ttl;     /* verbleibende Sekunden, <= 0 = frei */
    int tier;      /* -1 = keiner, sonst Tier (fuer Sound/Symbol) */
} Toast;

typedef struct Game {
    Player player;
    Hero heroes[MAX_HEROES];     /* Slot belegt wenn .used */
    int next_hero_uid;
    Inventory inv;
    World world;
    Rng rng;                     /* globaler RNG fuer Loot/Crafting (nicht Sim) */
    double play_time;            /* Spielzeit in Sekunden (gespeichert) */
    float speed_mult;            /* Simulationsgeschwindigkeit (1,2,4,8; nicht gespeichert) */
    struct Run *runs[MAX_RUNS];  /* aktive Maps (Laufzeit) */
    int run_count;
    struct Run *hero_run[MAX_HEROES]; /* Zeiger auf den Run des Helden oder NULL (Laufzeit) */
    Toast toasts[TOAST_MAX];     /* Benachrichtigungen (Laufzeit) */
    bool dirty_save;             /* Aenderung seit letztem Speichern */
    bool cheat_mode;             /* --cheat: Debug-Funktionen in der UI */
    double autosave_timer;
} Game;

/* ======================================================================== */
/* Spiel-API (src/game/game.c) - wird von UI, Sim und App genutzt            */
/* ======================================================================== */
/* Neues Spiel: leerer Spielstand mit Startinhalt (1 Held? nein: Spieler
 * erstellt Helden selbst; Startgeld, Startkisten, Willkommenspost). */
void game_init_new(Game *g, u64 seed);
/* Gibt Runs frei (vor dem Verwerfen/Neuladen aufrufen). */
void game_shutdown(Game *g);

/* Ein Simulationsschritt mit realer Zeit dt (Sekunden). Fuehrt abhaengig von
 * speed_mult mehrere feste Ticks (SIM_DT) aus, verwaltet Runs (anlegen,
 * entfernen, Retry), Timer (Tod, Retry, Kommandoposten, Tages-Reset, Toasts),
 * Autosave-Flag. */
void game_update(Game *g, double dt);

/* Anzahl freigeschalteter Heldenplaetze (Spielerlevel-Tabelle + Tickets, max 16). */
int game_hero_slots(const Game *g);
int game_hero_count(const Game *g);
/* Findet freien Held-Slot oder -1. */
int game_free_hero_slot(const Game *g);

/* Held anlegen (Startausruestung: Level-0-Waffe, 1 Normal-Heiltrank, 5 freie
 * Punkte + Klassenvorschlag optional verteilt). Gibt Index oder -1 (kein Platz). */
int game_create_hero(Game *g, const char *name, int cls, u8 look, bool auto_distribute);
/* Held loeschen; Ausruestung geht zurueck ins Lager. */
bool game_delete_hero(Game *g, int hero_idx);

/* Zuweisung Held -> Map (Slot 0..max_heroes-1). Entfernt den Helden vorher
 * von seiner alten Map. false wenn Map gesperrt/Slot belegt/ungueltig. */
bool game_assign_hero(Game *g, int hero_idx, int map_idx, int team_slot);
void game_unassign_hero(Game *g, int hero_idx);
/* Ist die Map fuer den Spieler freigeschaltet (Fortschritt, Level)? */
bool game_map_unlocked(const Game *g, int map_idx);

/* Toast anzeigen (Benachrichtigung unten rechts). tier = -1 fuer neutral. */
void game_toast(Game *g, int tier, u32 color, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 5, 6)))
#endif
    ;

/* Aktuelle Zeit-Schluessel fuer Tages-/Wochen-/Monats-Reset (Reset 04:00). */
int game_day_key(void);
int game_week_key(void);
int game_month_key(void);

/* ----- Rueckrufe aus der Simulation (src/game/game_events.c) -------------- */
typedef struct KillLoot {
    int chest_count;
    u8 chest_tier[16];
} KillLoot;

/* Ein Held hat ein Monster getoetet: EXP/Gold/Quests/Kisten-Wuerfe/Trank-
 * Auffuellung. 'monster_level' = Map-Level, 'tier' = Monster-Tier. */
void game_on_kill(Game *g, int hero_idx, int monster_def, Tier tier, int monster_level,
                  int map_idx, KillLoot *out);
/* Ein Held hebt eine Kiste auf. false = Kistenleiste voll / abgelehnt. */
bool game_hero_pickup_chest(Game *g, int hero_idx, Tier tier);
void game_on_hero_death(Game *g, int hero_idx, int map_idx);
/* Boss besiegt / Map abgeschlossen: Belohnungen (EXP, Gold, Magiesteine,
 * Boss-Truhe, Erstabschluss-Boni, Kommandeurpunkt). */
void game_on_map_clear(Game *g, int map_idx, const int *hero_idx, int hero_count, float clear_time);
/* Kistenleiste eines Helden ins Lager (Taste F4). Gibt Anzahl zurueck. */
int game_collect_chests(Game *g, int hero_idx);
int game_collect_all(Game *g);
/* EXP-Rate (0.2..1.0) des Helden auf einer Map mit diesem Level. */
float game_exp_rate(const Hero *h, int map_level);

#endif /* PNX_GAME_GAME_H */
