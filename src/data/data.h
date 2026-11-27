/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Statische Spieldaten (aus data/<name>.tsv beim Start geladen, danach read-only).
 *
 * Alle Tabellen sind globale Arrays mit Zaehlern. Verweise zwischen Tabellen
 * laufen ueber ARRAY-INDIZES (int), nicht ueber Strings; Strings (id) dienen
 * nur dem Laden/Speichern/Debuggen. Fuer Lookup per ID: data_find_*().
 */
#ifndef PNX_DATA_DATA_H
#define PNX_DATA_DATA_H

#include "core/types.h"

/* ======================================================================== */
/* Balance (data/balance.cfg, Zeilen "schluessel = wert")                    */
/* ======================================================================== */
/* X-Makro: BAL(name, default). Neuer Eintrag = eine Zeile hier + (optional)
 * eine Zeile in balance.cfg. Zugriff: g_bal.name */
#define BALANCE_FIELDS(BAL)                                                                       \
    BAL(exp_hero_k, 40.0)            /* EXP bis Level L = k * L^exp */                           \
    BAL(exp_hero_pow, 2.1)                                                                       \
    BAL(exp_player_k, 120.0)         /* Kommandeur-EXP-Kurve */                                  \
    BAL(exp_player_pow, 2.1)                                                                     \
    BAL(monster_exp_k, 6.0)          /* 6 * MapLv^1.55 * TierMult */                             \
    BAL(monster_exp_pow, 1.55)                                                                   \
    BAL(exp_rate_step, 0.05)         /* -5 % je Level ueber Map-Level */                         \
    BAL(exp_rate_min, 0.20)                                                                      \
    BAL(death_wait_s, 300.0)                                                                     \
    BAL(retry_wait_s, 12.0)                                                                      \
    BAL(weapon_base, 8.0)            /* W = (base + k*L^pow) * TierMult */                       \
    BAL(weapon_k, 5.5)                                                                           \
    BAL(weapon_pow, 1.3)                                                                         \
    BAL(sub_base, 3.0)                                                                           \
    BAL(sub_k, 1.8)                                                                              \
    BAL(sub_pow, 1.2)                                                                            \
    BAL(armor_base, 4.0)                                                                         \
    BAL(armor_k, 2.2)                                                                            \
    BAL(armor_pow, 1.25)                                                                         \
    BAL(hp_base, 50.0)                                                                           \
    BAL(hp_per_level, 20.0)                                                                      \
    BAL(hp_per_str, 8.0)                                                                         \
    BAL(mana_base, 30.0)                                                                         \
    BAL(mana_per_level, 3.0)                                                                     \
    BAL(mana_per_int, 5.0)                                                                       \
    BAL(base_mana_regen_pct, 1.0)    /* % des Max-Mana pro Sekunde */                            \
    BAL(base_hp_regen_pct, 1.5)      /* % des Max-HP pro Sekunde ausserhalb Kampf */             \
    BAL(armor_k_mlv, 60.0)           /* R / (R + k*Lv + c) */                                    \
    BAL(armor_c, 300.0)                                                                          \
    BAL(armor_cap, 0.85)                                                                         \
    BAL(base_crit_chance, 5.0)                                                                   \
    BAL(base_crit_dmg, 150.0)                                                                    \
    BAL(cdr_cap, 80.0)                                                                           \
    BAL(cdr_hard_cap, 90.0)                                                                      \
    BAL(cd_floor_s, 0.5)                                                                         \
    BAL(base_move_speed, 300.0)                                                                  \
    BAL(move_speed_cap_pct, 450.0)                                                               \
    BAL(skill_level_scale, 0.15)     /* Basis% * (1 + k*(Lv-1)) */                               \
    BAL(monster_hp_base, 6.0)       /* HP(L) = base + k*L^pow */                                \
    BAL(monster_hp_k, 2.2)                                                                       \
    BAL(monster_hp_pow, 1.7)                                                                     \
    BAL(monster_dmg_base, 2.0)                                                                   \
    BAL(monster_dmg_k, 0.5)                                                                      \
    BAL(monster_dmg_pow, 1.35)                                                                   \
    BAL(monster_armor_per_lv, 8.0)                                                               \
    BAL(monster_res_per_lv, 6.0)                                                                 \
    BAL(temple_hp_mult, 3.0)                                                                     \
    BAL(temple_dmg_mult, 0.6)                                                                    \
    BAL(ruins_hp_mult, 1.0)                                                                      \
    BAL(ruins_dmg_mult, 2.5)                                                                     \
    BAL(act11_hp_mult, 2.0)                                                                      \
    BAL(aggro_radius, 700.0)                                                                     \
    BAL(pickup_radius, 80.0)                                                                     \
    BAL(target_hysteresis_s, 1.5)                                                                \
    BAL(stuck_timeout_s, 6.0)                                                                    \
    BAL(leash_dist, 900.0)                                                                       \
    BAL(dormant_dist, 1500.0)                                                                    \
    BAL(boss_enrage_s, 180.0)                                                                    \
    BAL(chest_base_capacity, 10.0)                                                               \
    BAL(chest_max_capacity, 70.0)                                                                \
    BAL(chest_roll_cap, 10.0)        /* max. Kisten pro Kill */                                  \
    BAL(chest_item_pct_potion, 4.0)                                                              \
    BAL(chest_item_pct_weapon, 5.0)                                                              \
    BAL(chest_item_pct_armor, 6.0)                                                               \
    BAL(chest_item_pct_ring, 1.5)                                                                \
    BAL(chest_item_pct_necklace, 0.75)                                                           \
    BAL(chest_item_pct_wings, 0.005)                                                             \
    BAL(chest_item_pct_bag, 1.0)                                                                 \
    BAL(chest_item_pct_book, 1.75)                                                               \
    BAL(potion_refill_pct, 4.0)      /* % Fuellung je Normal-Kill */                             \
    BAL(inv_base_capacity, 200.0)                                                                \
    BAL(inv_max_capacity, 1000.0)                                                                \
    BAL(stack_max, 9999.0)                                                                       \
    BAL(enhance_flat_pct, 3.0)       /* % der Basis je Stufe */                                  \
    BAL(jewelry_attack_affects_spells, 1.0)                                                      \
    BAL(post_cr_per_hour, 60.0)                                                                  \
    BAL(post_max_hours_per_day, 8.0)                                                             \
    BAL(post_storage_cap, 600.0)                                                                 \
    BAL(post_order_cost_per_hour, 30.0)                                                          \
    BAL(adjutant_cost, 60.0)                                                                     \
    BAL(autosave_s, 60.0)                                                                        \
    BAL(daily_reset_hour, 4.0)                                                                   \
    BAL(temple_unlock_level, 15.0)                                                               \
    BAL(temple_tickets_per_day, 30.0)                                                            \
    BAL(auto_open_unlock_level, 20.0)

typedef struct Balance {
#define BAL_DECL(name, def) double name;
    BALANCE_FIELDS(BAL_DECL)
#undef BAL_DECL
} Balance;

extern Balance g_bal;

/* ======================================================================== */
/* Tiers                                                                     */
/* ======================================================================== */
typedef struct TierDef {
    char id[MAX_ID];       /* "normal", "magic", ... */
    char name[MAX_NAME];   /* "Normal", "Magisch" ... */
    u32 color;             /* 0xRRGGBB */
    u32 drop_one_in;       /* Kistenchance: 1:n je Wurf */
    float item_mult;       /* Waffen-/Item-Wertmultiplikator */
    float exp_mult;        /* Monster-EXP-Multiplikator */
    float hp_mult;         /* Monster-HP-Multiplikator */
    float dmg_mult;        /* Monster-Schadensmultiplikator */
    float spawn_weight;    /* Spawn-Gewicht je Pack (Prozent) */
    int chest_items;       /* Items pro geoeffneter Kiste */
    int monster_rolls;     /* Kistenwuerfe pro Kill eines Monsters dieses Tiers */
    int potion_charges;    /* Traenke: Ladungen */
    int sell_per_level;    /* Verkaufswert = sell_base + sell_per_level * Level */
    int sell_base;
    int symbol;            /* Anzahl Rauten (0..5) fuer Farbenblind-Modus */
} TierDef;

/* ======================================================================== */
/* Klassen                                                                   */
/* ======================================================================== */
typedef enum AttackStyle {
    ATK_MELEE = 0,     /* Nahkampf-Treffer */
    ATK_PROJECTILE,    /* gerades Projektil */
    ATK_HOMING,        /* zielsuchendes Projektil */
    ATK_DASH_STAB      /* Dash zum Ziel + Stich (Assassine) */
} AttackStyle;

typedef enum DamageType { DMG_PHYS = 0, DMG_SPELL = 1, DMG_HYBRID = 2 } DamageType;

typedef struct ClassDef {
    char id[MAX_ID];          /* "knight" ... */
    char name[MAX_NAME];      /* "Ritter" */
    char role[MAX_NAME];      /* "Tank / physischer DPS" */
    char race[MAX_NAME];
    char desc[MAX_DESC];
    char weapon_name[MAX_NAME];  /* "Schwert" */
    char sub_name[MAX_NAME];     /* "Schild" */
    char basic_attack[MAX_NAME]; /* "Schwerthieb" */
    u32 color;                /* Token-Farbe */
    AttackStyle atk_style;
    DamageType dmg_type;
    bool ranged;
    int main_stat;            /* 0=STR 1=DEX 2=INT */
    float base_speed;         /* Angriffe pro Sekunde */
    float base_range;         /* Reichweite in px */
    float proj_speed;         /* Projektilgeschwindigkeit px/s */
    float move_factor;        /* Klassenfaktor Lauftempo (0.9..1.1) */
    float base_armor;         /* 1, Ritter 2 */
    int auto_str, auto_dex, auto_int; /* Vorschlag fuer Punkteverteilung (Summe 100) */
    float basic_dmg_pct;      /* Basisangriff in % des Waffenschadens */
} ClassDef;

/* ======================================================================== */
/* Skills                                                                    */
/* ======================================================================== */
typedef enum SkillKind {
    SK_STRIKE = 0,   /* Sofort-Treffer auf ein Ziel in Reichweite ('count' Treffer) */
    SK_CIRCLE,       /* Sofort-Flaeche um den Wirker, Radius 'radius' */
    SK_CONE,         /* Sofort-Kegel in Blickrichtung, Laenge 'range', Winkel 'angle' */
    SK_PROJECTILE,   /* 'count' Projektile (Faecher 'angle'), 'pierce', 'radius' = Explosion */
    SK_GROUND,       /* Boden-Flaeche am Ziel nach 'delay'; 'dur'>0 = bleibende Zone (Ticks) */
    SK_DASH,         /* Sprung/Dash; optional Treffer im 'radius' am Ziel */
    SK_BUFF_SELF,    /* Buff auf den Wirker */
    SK_BUFF_PARTY,   /* Buff auf alle Helden der Gruppe (im 'radius', 0 = alle) */
    SK_DEBUFF,       /* Debuff auf Gegner im 'radius' um das Ziel */
    SK_HEAL,         /* Einzelheilung (niedrigste HP) */
    SK_HEAL_PARTY,   /* Gruppenheilung */
    SK_HOT,          /* Heilung ueber Zeit auf ein Ziel ('dur') */
    SK_TOTEM,        /* Totem beschwoeren ('totem'-Typ, 'dur') */
    SK_MANA,         /* Mana wiederherstellen (Selbst/Verbuendete) */
    SK_CLEANSE,      /* Debuffs entfernen + kleine Heilung */
    SK_PULL,         /* zieht Zielgegner heran */
    SK_WARP,         /* Warp (Rueckwaerts-Teleport) + Heilung */
    SK_KIND_COUNT
} SkillKind;

/* Skill-Tags (Anzeige + KI-Filter). */
#define SKT_SINGLE 0x0001u
#define SKT_AOE 0x0002u
#define SKT_MULTI 0x0004u
#define SKT_DOT 0x0008u
#define SKT_UTILITY 0x0010u
#define SKT_BUFF 0x0020u
#define SKT_DEBUFF 0x0040u
#define SKT_HEAL 0x0080u
#define SKT_TOTEM 0x0100u

/* Skill-Flags (Verhalten). */
#define SKF_DASH_FWD 0x0001u      /* Dash nach vorn zum Ziel */
#define SKF_DASH_BACK 0x0002u     /* Sprung nach hinten */
#define SKF_DASH_SIDE 0x0004u     /* Seitschritt (links/rechts zufaellig) */
#define SKF_SLOW 0x0008u          /* Verlangsamung auf getroffene Gegner */
#define SKF_STUN 0x0010u          /* Betaeubung kurz */
#define SKF_TAUNT 0x0020u         /* Spott */
#define SKF_HEAL_SELF 0x0040u     /* heilt den Wirker mit */
#define SKF_HOMING 0x0080u        /* Projektile zielsuchend */
#define SKF_MOVE_WHILE_CAST 0x0100u
#define SKF_SPELL 0x0200u         /* verursacht Zauberschaden (sonst physisch) */
#define SKF_DOT 0x0400u           /* Schaden wird als DoT ueber 'dur' verteilt */
#define SKF_PARTY_HEAL 0x0800u    /* heilt Gruppe mit */
#define SKF_MANA_BACK 0x1000u     /* stellt dem Wirker Mana wieder her */
#define SKF_USES_TELEPORT 0x2000u /* Sprung ohne Kollision (Teleport) */
#define SKF_NO_COMBAT_REQ 0x4000u /* Utility darf auch ausserhalb Kampf */

typedef struct SkillAiDefaults {
    u8 priority;       /* 0 (zuerst) .. 8 */
    u8 tier_mask;      /* Bit je Tier: welche Zieltiers (0x3F = alle) */
    u8 min_targets;    /* Mindestanzahl Gegner in Reichweite */
    u16 max_dist;      /* max. Zielentfernung (0 = Skill-Reichweite) */
    u8 self_hp_below;  /* nur wenn eigene HP < x % (0 = ignorieren / 100) */
    u8 ally_hp_below;  /* Heilung: Verbuendete < x % */
    u8 only_in_combat; /* 1 = nur im Kampf */
    u8 allow_stack;    /* Buff nachladen bevor er ablaeuft */
} SkillAiDefaults;

typedef struct SkillDef {
    char id[MAX_ID];        /* "knight_vital_strike" */
    char name[MAX_NAME];
    char desc[MAX_DESC];
    int cls;                /* ClassId oder CLASS_COMMON */
    int grade;              /* 0..3 (Normal..Einzigartig) */
    u32 tags;               /* SKT_* */
    SkillKind kind;
    u32 flags;              /* SKF_* */
    float cooldown;         /* Basis-CD in s */
    float mana;
    float range;            /* Reichweite in px */
    float radius;           /* Flaechenradius in px */
    float cast_time;        /* Wirkzeit in s (skaliert mit Zaubertempo) */
    float dmg_pct;          /* Schaden Lv1 in % der Basis (0 = kein Schaden) */
    int count;              /* Treffer/Projektile */
    float angle;            /* Faecher-/Kegelwinkel in Grad */
    int pierce;             /* Durchdringung */
    int max_hits;           /* max. Treffer je Ziel (Mehrfachprojektile) */
    float speed;            /* Projektiltempo px/s */
    float dur;              /* Dauer in s (Buff/DoT/Totem/Zone) */
    float delay;            /* Verzoegerung in s */
    int stat;               /* Stat fuer Buff/Debuff oder -1 */
    float stat_pct;         /* Wert (Prozent bzw. flach) bei Lv1 */
    int stat2;              /* zweiter Stat oder -1 */
    float stat2_pct;
    int totem;              /* TotemType (nur SK_TOTEM) */
    SkillAiDefaults ai;
} SkillDef;

typedef enum TotemType {
    TOTEM_NONE = 0,
    TOTEM_TURTLE,       /* Spott + Ruestung im Radius */
    TOTEM_STORM_DRAGON, /* periodische Blitzschlaege */
    TOTEM_RED_DRAGON,   /* Kegel-Flaechenschaden + Schadensbuff */
    TOTEM_WAR_DRUM,     /* periodischer Flaechenschaden */
    TOTEM_CLAIRVOYANCE, /* Krit-Schaden-Buff im Radius */
    TOTEM_GALE_HAWK,    /* Tempo-Buff im Radius */
    TOTEM_COUNT
} TotemType;

/* ======================================================================== */
/* Items                                                                     */
/* ======================================================================== */
typedef enum PotionType {
    POTION_HEAL = 0,
    POTION_MANA,
    POTION_HASTE,    /* Eile: Lauftempo */
    POTION_IRON,     /* Eisenhaut: Ruestung/Zauberwiderstand */
    POTION_FRENZY,   /* Raserei: Angriffstempo */
    POTION_TYPE_COUNT
} PotionType;

typedef enum MaterialLine {
    MAT_METAL = 0,
    MAT_WOOD,
    MAT_CLOTH,
    MAT_CRYSTAL,
    MAT_ESSENCE,
    MAT_LINE_COUNT
} MaterialLine;

typedef enum StoneKind {
    STONE_NONE = 0,
    STONE_ENH_WEAPON,   /* Verbesserungsstein Waffen */
    STONE_ENH_ARMOR,
    STONE_ENH_JEWEL,
    STONE_ENCHANT,      /* Verzauberungsstein (Tier = Item-Tier) */
    STONE_MAGIC         /* Magiestein (Akt-gebunden: 'value' = Aktnummer, 10=Ahel, 11=Orpheon) */
} StoneKind;

typedef struct ItemDef {
    char id[MAX_ID];       /* eindeutig, snake_case: "knight_weapon_07" */
    char name[MAX_NAME];
    ItemKind kind;
    Tier tier;
    int min_level;         /* Mindest-Heldenlevel */
    EquipSlot slot;        /* Ausruestungs-Slot oder SLOT_NONE */
    int cls;               /* ClassId oder CLASS_ANY (-1) */
    float value;           /* Hauptwert: Waffenschaden / Ruestung / Sub-Wert / Heil-% / Kisten+ ... */
    StatMods mods;         /* feste Zusatzeffekte ("Additional Effects") */
    int potion_type;       /* PotionType (nur IK_POTION) */
    int charges;           /* Traenke: Ladungen */
    int material_line;     /* MaterialLine (nur IK_MATERIAL) */
    int stone_kind;        /* StoneKind (nur IK_STONE) */
    int family;            /* Schmuck: 0 Krieger 1 Assassine 2 Elementar 3 Leben */
    int set_id;            /* Set-Bonus (0 = keins) */
    int set_index;         /* Position innerhalb der Slot-Reihe (1..21) */
    bool craftable;
    bool enchantable;      /* ab Tier Selten, nur Waffe/Sub/Ruestung */
    u8 legendary_effect[CLASS_COUNT]; /* Effekt-ID je Klasse (0 = keiner), nur Astral-Teile */
} ItemDef;

/* ======================================================================== */
/* Monster                                                                   */
/* ======================================================================== */
typedef enum Archetype {
    ARCH_MELEE = 0, ARCH_RANGED, ARCH_SWARM, ARCH_BRUTE, ARCH_HUNTER, ARCH_SUMMONER,
    ARCH_HEALER, ARCH_EXPLOSIVE, ARCH_ELEMENTAL, ARCH_GUARDIAN, ARCH_TOTEM, ARCH_DEMON,
    ARCH_BOSS, ARCH_COUNT
} Archetype;

/* Faehigkeiten-Bits (Boss/Elite-Muster, siehe sim/enemy_ai.c) */
#define MAB_NOVA 0x0001u      /* Rundum-Schlag mit Telegraph um sich */
#define MAB_SLAM 0x0002u      /* Kreis-Telegraph auf Heldenposition */
#define MAB_CONE 0x0004u      /* Kegel-Atem */
#define MAB_LINE 0x0008u      /* Linien-Schuss/Kanone */
#define MAB_SUMMON 0x0010u    /* ruft Adds */
#define MAB_POOLS 0x0020u     /* hinterlaesst Giftpfuetzen / Feuersaeulen (Bodenzonen) */
#define MAB_CHARGE 0x0040u    /* Ansturm */
#define MAB_HEAL_ALLY 0x0080u /* heilt Verbuendete */
#define MAB_SHIELD 0x0100u    /* Schutzschild-Phase */
#define MAB_ENRAGE 0x0200u    /* Wut unter 30 % HP */
#define MAB_RAIN 0x0400u      /* Projektilregen (mehrere Kreis-Telegraphen) */

typedef struct MonsterDef {
    char id[MAX_ID];
    char name[MAX_NAME];
    Archetype arch;
    int act_min, act_max;     /* in welchen Akten (1..11) er vorkommt */
    float hp_f, dmg_f;        /* Faktoren auf Basiswerte */
    float speed;              /* px/s */
    float range;              /* Angriffsreichweite px */
    float radius;             /* Koerperradius px */
    float attack_cd;          /* Sekunden zwischen Angriffen */
    float proj_speed;         /* >0 = Fernkampf-Projektil */
    u32 color;                /* Token-Farbe */
    int shape;                /* 0 Kreis 1 Quadrat 2 Raute 3 Dreieck 4 Stern 5 Sechseck */
    u32 abilities;            /* MAB_* */
    bool is_boss;
    bool is_elite_only;       /* nur als Selten+ */
    float weight;             /* Spawn-Gewicht im Akt-Pool */
} MonsterDef;

/* ======================================================================== */
/* Maps                                                                      */
/* ======================================================================== */
typedef enum MapKind { MAPK_STORY = 0, MAPK_TEMPLE, MAPK_RUINS } MapKind;

typedef struct MapDef {
    char id[MAX_ID];         /* "1-1", "10A-3", "T-4", "R-2" */
    char name[MAX_NAME];
    MapKind kind;
    int act;                 /* 1..11 (Tempel/Ruinen: 0) */
    int index;               /* Nummer im Akt bzw. Stufe */
    int chapter;             /* 1..5 (Tempel/Ruinen: 0) */
    int level;               /* Map-Level (Monster, EXP) */
    int w, h;                /* Groesse in Kacheln */
    u32 seed;                /* fester Seed => deterministisch */
    int boss;                /* Index in g_monsters oder -1 */
    bool boss_map;           /* x-5 / x-10 / Akt-10 3,6,10 */
    int max_heroes;          /* 1, 2 (Akt 11/Tempel/Ruinen), bis 4 */
    int min_player_level;
    float density;           /* Monsterdichte-Faktor (1.0 = Standard) */
    int stone_item;          /* Item-Index des garantierten Magiesteins oder -1 */
    int stone_count;
    u32 tint;                /* Akt-Farbton 0xRRGGBB */
} MapDef;


/* ======================================================================== */
/* Legendaere Klasseneffekte und Set-Boni (Spec 08)                          */
/* ======================================================================== */
/* Verhaltens-Effekte, die die Simulation (sim/) abfragt. Reine Stat-Effekte
 * stehen als 'mods' in LegendEffect/SetDef und werden von rules/stats.c addiert. */
typedef enum LegendBehavior {
    LB_NONE = 0,
    LB_EXPLOSIVE_TRAIL,   /* Bogenschuetze/Stiefel: alle Projektile loesen Explosionen aus */
    LB_ARCANE_EMISSION,   /* Magier/Handschuhe: feuert periodisch Magieprojektile auf nahe Gegner */
    LB_MARCH_OF_LIGHT,    /* Priester/Stiefel: +Projektile, Treffer-Limit, Homing sucht neues Ziel */
    LB_ROLLING_STEEL,     /* Ritter/Brust: 15 % erlittener Treffer als Flaechenschaden zurueck */
    LB_BLOODRAGE,         /* Krieger/Helm: unter 50 % HP +30 % Angriffstempo */
    LB_EXECUTIONER,       /* Assassine/Helm: 50 % Splash-Schaden bei Treffer */
    LB_TOTEM_ECHO,        /* Schamane/Brust: Totems explodieren beim Verschwinden */
    LB_CHAIN_LIGHTNING,   /* Set Aschekoenig (4 Teile): Basisangriff loest Kettenblitz aus */
    LB_COUNT
} LegendBehavior;

typedef struct LegendEffect {
    int id;                 /* 1..28, Bit (1u << id) in StatInputs.legendary_mask */
    char name[MAX_NAME];
    char desc[MAX_DESC];
    int cls;                /* ClassId, fuer die der Effekt aktiv ist */
    EquipSlot slot;         /* SLOT_HELM / SLOT_CHEST / SLOT_GLOVES / SLOT_BOOTS (Astral-Teil) */
    StatMods mods;          /* reine Stat-Boni */
    LegendBehavior behavior;
} LegendEffect;

#define LEGEND_BIT_CHAIN_LIGHTNING 29 /* Bit fuer Set-4-Teile-Effekt Kettenblitz */
#define LEGEND_BIT_ASTRAL_SET 30      /* Bit fuer Astral-Set 4 Teile (Info) */

typedef struct SetDef {
    int id;                 /* 1.. (ItemDef.set_id) */
    char name[MAX_NAME];
    StatMods mods2;         /* Bonus ab 2 Teilen */
    StatMods mods4;         /* Bonus ab 4 Teilen */
    LegendBehavior behavior4; /* Verhaltensbonus ab 4 Teilen oder LB_NONE */
    char desc2[MAX_DESC];
    char desc4[MAX_DESC];
} SetDef;

/* ======================================================================== */
/* Shop, Kommandeur, Quests                                                  */
/* ======================================================================== */
typedef enum ShopTab { SHOP_FREE = 0, SHOP_DIAMONDS, SHOP_RANDOM, SHOP_GENERAL, SHOP_BOOKS, SHOP_RAVEN, SHOP_TAB_COUNT } ShopTab;

typedef enum ShopKind {
    SHOPK_ITEM = 0,        /* 'item' x 'qty' ins Lager */
    SHOPK_RANDOM_RING,     /* Zufalls-Ring (Tier-Verteilung angezeigt) */
    SHOPK_RANDOM_NECKLACE,
    SHOPK_RANDOM_BOOK,     /* Zufalls-Skillbuch (Grad-Verteilung) */
    SHOPK_BOOK_CLASS,      /* Skillbuch der Klasse 'item' (= ClassId) */
    SHOPK_SLOTS_INV,       /* Inventar +20 */
    SHOPK_SLOTS_HERO,      /* Heldenplatz +1 (max. 16) */
    SHOPK_CURRENCY,        /* 'qty' der Waehrung 'item' (= Currency) gutschreiben (Gratis-Pakete) */
    SHOPK_WINGS,           /* Fluegel 'item' */
    SHOPK_CHEST            /* 'qty' Kisten des Tiers 'item' */
} ShopKind;

typedef enum ShopPeriod { PERIOD_NONE = 0, PERIOD_DAILY, PERIOD_WEEKLY, PERIOD_MONTHLY } ShopPeriod;

typedef struct ShopOffer {
    char id[MAX_ID];
    char name[MAX_NAME];
    char desc[MAX_DESC];
    ShopTab tab;
    ShopKind kind;
    int item;               /* Item-Index bzw. ClassId/Currency/Tier je nach kind */
    int qty;
    Currency currency;
    i64 price;              /* 0 = gratis */
    int limit;              /* Kaeufe pro Periode (0 = unbegrenzt) */
    ShopPeriod period;
    float price_growth;     /* Preis wird je Kauf um diesen Faktor teurer (0 = konstant) */
    int min_player_level;
} ShopOffer;

typedef struct TraitDef {
    char id[MAX_ID];
    char name[MAX_NAME];
    char desc[MAX_DESC];
    int tree;               /* 0 Exploration 1 Economy 2 Faith 3 Leadership */
    int max_points;
    char effect[MAX_ID];    /* Schluessel eines CommanderMods-Feldes (siehe rules/commander.h) */
    float per_point;        /* Wert je Punkt */
    int row;                /* Position im Baum (UI) */
    int requires;           /* Index eines Traits oder -1 */
} TraitDef;

typedef enum QuestKind {
    QK_KILL_ANY = 0, QK_KILL_TIER, QK_CLEAR_MAP, QK_CRAFT, QK_OPEN_CHESTS, QK_KILL_ARCH,
    QK_ENHANCE, QK_REACH_LEVEL
} QuestKind;

typedef struct QuestDef {
    char id[MAX_ID];
    char text[MAX_DESC];    /* "Besiege {n} Magische Monster" */
    QuestKind kind;
    int param;              /* Tier / Archetyp / ... */
    int count_min, count_max;
    bool main_quest;        /* Hauptquest (linear) statt Kopfgeld */
    i64 reward_cmd_exp;
    i64 reward_gold;
    i64 reward_diamonds;
} QuestDef;

/* ======================================================================== */
/* Globale Tabellen                                                          */
/* ======================================================================== */
extern TierDef g_tiers[TIER_COUNT];
extern ClassDef g_classes[CLASS_COUNT];
extern SkillDef g_skills[MAX_SKILLS];
extern int g_skill_count;
extern ItemDef g_items[MAX_ITEM_DEFS];
extern int g_item_count;
extern MonsterDef g_monsters[MAX_MONSTERS];
extern int g_monster_count;
extern MapDef g_maps[MAX_MAPS];
extern int g_map_count;
extern ShopOffer g_shop[MAX_SHOP_OFFERS];
extern int g_shop_count;
extern TraitDef g_traits[MAX_TRAITS];
extern int g_trait_count;
extern QuestDef g_quests[MAX_QUEST_DEFS];
extern int g_quest_count;
extern LegendEffect g_legend[32];   /* Index = id (1..28), [0] unbenutzt */
extern int g_legend_count;
extern SetDef g_sets[8];            /* Index = id (1..), [0] unbenutzt */
extern int g_set_count;

extern const char *const g_stat_names[STAT_COUNT];      /* TSV-Namen, siehe types.h */
extern const char *const g_stat_labels[STAT_COUNT];     /* Anzeige ("Krit-Chance") */
extern const char *const g_equip_slot_names[SLOT_COUNT];/* "Waffe", "Sekundaerwaffe", ... */
extern const char *const g_item_kind_names[IK_KIND_COUNT];
extern const char *const g_potion_type_names[POTION_TYPE_COUNT];
extern const char *const g_currency_names[CUR_COUNT];

/* ======================================================================== */
/* API                                                                       */
/* ======================================================================== */
/* Laedt alle Tabellen aus <data_dir>/<name>.tsv und balance.cfg und validiert sie
 * (Verweise, Bereiche). Bei Fehlern: false + Logeintrag. Kann mehrfach
 * aufgerufen werden (setzt vorher alles zurueck). */
bool data_load_all(const char *data_dir);
/* Verzeichnis, aus dem geladen wurde (fuer maps/<id>.map). */
const char *data_dir(void);

int data_find_item(const char *id);      /* -1 wenn unbekannt */
int data_find_skill(const char *id);
int data_find_monster(const char *id);
int data_find_map(const char *id);
int data_stat_from_name(const char *name); /* -1 wenn unbekannt */

/* Parst "crit=5.0;crit_dmg=40" in mods (addiert). Gibt Anzahl unbekannter Namen zurueck. */
int statmods_parse(const char *text, StatMods *mods);
void statmods_add(StatMods *dst, const StatMods *src);
void statmods_scale(StatMods *m, float f);

/* Komfort-Abfragen */
static inline Color tier_color(Tier t, u8 alpha) {
    u32 c = g_tiers[t].color;
    Color col = {(u8)((c >> 16) & 0xFF), (u8)((c >> 8) & 0xFF), (u8)(c & 0xFF), alpha};
    return col;
}
/* Items einer Auswahl: schreibt Indizes in 'out' (max 'cap') und gibt Anzahl zurueck.
 * Filter: kind (IK_NONE = egal), tier (-1 = egal), slot (SLOT_NONE = egal), cls (-2 = egal). */
int data_query_items(ItemKind kind, int tier, EquipSlot slot, int cls, int *out, int cap);
/* Findet das Material/den Stein einer Linie in einem Tier; -1 wenn nicht vorhanden. */
int data_find_material(MaterialLine line, Tier tier);
int data_find_stone(StoneKind kind, Tier tier);
/* Magiestein eines Aktes (1..11) oder -1. */
int data_find_magic_stone(int act);
/* Skills einer Klasse (inkl. optional CLASS_COMMON): Indizes in out. */
int data_query_skills(int cls, int *out, int cap);

#endif /* PNX_DATA_DATA_H */
