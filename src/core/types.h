/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Panoptix - Grundtypen, Konstanten und Enums, die im ganzen Projekt gelten.
 *
 * Diese Datei ist der "gemeinsame Wortschatz" aller Module. Aenderungen hier
 * sind nur additiv erlaubt (neue Enum-Werte VOR *_COUNT einfuegen), weil
 * Savegames und Datendateien Enum-Werte als Zahlen/Namen referenzieren.
 */
#ifndef PNX_CORE_TYPES_H
#define PNX_CORE_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef struct Vec2 {
    float x, y;
} Vec2;

typedef struct Rect {
    int x, y, w, h;
} Rect;

/* Farbe als 0xRRGGBB (Daten) bzw. Color (Rendering). */
typedef struct Color {
    u8 r, g, b, a;
} Color;

/* ------------------------------------------------------------------------ */
/* Harte Obergrenzen (statische Arrays statt verstreutem malloc)             */
/* ------------------------------------------------------------------------ */
#define MAX_HEROES 16       /* Heldenplaetze maximal */
#define MAX_TEAM 4          /* Helden pro Map/Team maximal */
#define MAX_RUNS 16         /* gleichzeitig laufende Maps */
#define SKILL_SLOTS 4       /* aktive Skills je Held */
#define POTION_SLOTS 3      /* Trank-Slots je Held */
#define MAX_ITEM_DEFS 1024  /* Eintraege in g_items */
#define MAX_SKILLS 128      /* Eintraege in g_skills */
#define MAX_MONSTERS 128    /* Eintraege in g_monsters */
#define MAX_MAPS 192        /* Eintraege in g_maps */
#define MAX_SHOP_OFFERS 96  /* Eintraege in g_shop */
#define MAX_TRAITS 64       /* Eintraege in g_traits */
#define MAX_QUEST_DEFS 64   /* Eintraege in g_quests */
#define MAX_NAME 48         /* Anzeigenamen (inkl. Nullbyte) */
#define MAX_ID 40           /* Daten-IDs (inkl. Nullbyte) */
#define MAX_DESC 200        /* Beschreibungstexte */
#define PLAYER_MAX_LEVEL 100
#define HERO_MAX_LEVEL 100
#define SKILL_MAX_LEVEL 20
#define ENHANCE_MAX 20
#define SIM_HZ 30           /* feste Simulationsrate */
#define SIM_DT (1.0f / 30.0f)
#define TILE_SIZE 32        /* Pixel pro Kachel */

/* ------------------------------------------------------------------------ */
/* Tier-System (roter Faden, Spec 02)                                        */
/* ------------------------------------------------------------------------ */
typedef enum Tier {
    TIER_NORMAL = 0,
    TIER_MAGIC = 1,
    TIER_RARE = 2,
    TIER_UNIQUE = 3,
    TIER_BOSS = 4,
    TIER_LEGENDARY = 5,
    TIER_COUNT = 6
} Tier;

/* ------------------------------------------------------------------------ */
/* Klassen (Spec 04)                                                         */
/* ------------------------------------------------------------------------ */
typedef enum ClassId {
    CLASS_KNIGHT = 0,
    CLASS_WARRIOR = 1,
    CLASS_WIZARD = 2,
    CLASS_ARCHER = 3,
    CLASS_ASSASSIN = 4,
    CLASS_SHAMAN = 5,
    CLASS_PRIEST = 6,
    CLASS_COUNT = 7,
    CLASS_COMMON = 7,  /* nur fuer Skills: "Allgemein", fuer alle Klassen */
    CLASS_ANY = -1     /* nur fuer Items: von jeder Klasse tragbar */
} ClassId;

/* ------------------------------------------------------------------------ */
/* Ausruestungs-Slots (14, Spec 11 Abschnitt 6)                              */
/* ------------------------------------------------------------------------ */
typedef enum EquipSlot {
    SLOT_WEAPON = 0,
    SLOT_SUB = 1,
    SLOT_HELM = 2,
    SLOT_CHEST = 3,
    SLOT_GLOVES = 4,
    SLOT_BOOTS = 5,
    SLOT_BAG = 6,
    SLOT_NECKLACE = 7,
    SLOT_RING1 = 8,
    SLOT_RING2 = 9,
    SLOT_POTION1 = 10,
    SLOT_POTION2 = 11,
    SLOT_POTION3 = 12,
    SLOT_WINGS = 13,
    SLOT_COUNT = 14,
    SLOT_NONE = -1
} EquipSlot;

/* ------------------------------------------------------------------------ */
/* Stat-Modifikatoren                                                        */
/*                                                                          */
/* EINHEITEN: Alle Prozent-Stats werden als Prozentzahl gespeichert          */
/* (5.0 = 5 %). Flache Stats (hp, mana, armor, attack...) als absolute Zahl. */
/* Regeneration in Punkten pro Sekunde.                                      */
/* ------------------------------------------------------------------------ */
typedef enum Stat {
    STAT_ATTACK = 0,      /* "dmg_flat"   flacher Schaden, wirkt auf Angriff UND Zauber */
    STAT_ATTACK_PHYS,     /* "attack_flat" flacher physischer Schaden */
    STAT_SPELL_FLAT,      /* "spell_flat"  flacher Zauberschaden */
    STAT_ATTACK_PCT,      /* "attack_pct"  % erhoehter physischer Schaden (additiv) */
    STAT_SPELL_PCT,       /* "spell_pct"   % erhoehter Zauberschaden (additiv) */
    STAT_MORE_ATTACK,     /* "more_attack" % MEHR physischer Schaden (multiplikativ je Quelle) */
    STAT_MORE_SPELL,      /* "more_spell"  % MEHR Zauberschaden (multiplikativ je Quelle) */
    STAT_CRIT_CHANCE,     /* "crit"        kritische Trefferchance in % */
    STAT_CRIT_DMG,        /* "crit_dmg"    kritischer Schaden in % (zusaetzlich zur Basis 150) */
    STAT_ATTACK_SPEED,    /* "atk_speed"   % Angriffstempo */
    STAT_CAST_SPEED,      /* "cast_speed"  % Zaubertempo */
    STAT_MOVE_SPEED,      /* "move_speed"  % Lauftempo */
    STAT_CDR,             /* "cdr"         % Abklingzeitreduktion */
    STAT_ARMOR_FLAT,      /* "armor"       flache Ruestung */
    STAT_ARMOR_PCT,       /* "armor_pct"   % Ruestung */
    STAT_SPELLRES_PCT,    /* "spellres_pct" % Zauberwiderstand */
    STAT_HP_FLAT,         /* "hp"          flaches Max-Leben */
    STAT_HP_PCT,          /* "hp_pct"      % Max-Leben */
    STAT_MANA_FLAT,       /* "mana"        flaches Max-Mana */
    STAT_MANA_PCT,        /* "mana_pct"    % Max-Mana */
    STAT_HP_REGEN,        /* "hp_regen"    Leben pro Sekunde */
    STAT_MANA_REGEN,      /* "mana_regen"  Mana pro Sekunde */
    STAT_CHEST_CAP,       /* "chest_cap"   Kistenkapazitaet */
    STAT_DR_ATTACK,       /* "dr_attack"   flacher Abzug je physischem Treffer */
    STAT_DR_SPELL,        /* "dr_spell"    flacher Abzug je Zaubertreffer */
    STAT_DMG_REDUCE,      /* "dmg_reduce"  % weniger erlittener Schaden (alle) */
    STAT_PROJECTILES,     /* "projectiles" zusaetzliche Projektile */
    STAT_PIERCE,          /* "pierce"      zusaetzliche Durchdringung */
    STAT_HEAL_PCT,        /* "heal_pct"    % mehr Heilkraft */
    STAT_ENEMY_TAKEN,     /* "enemy_taken" (Debuff auf Gegner) % mehr erlittener Schaden */
    STAT_ENEMY_ATK_DOWN,  /* "enemy_atk_down" (Debuff) % weniger Angriffsschaden der Gegner */
    STAT_ENEMY_SPELL_DOWN,/* "enemy_spell_down" (Debuff) % weniger Zauberschaden der Gegner */
    STAT_MANA_COST,       /* "mana_cost"   % Manakosten (negativ = guenstiger) */
    STAT_COUNT
} Stat;

typedef struct StatMods {
    float v[STAT_COUNT];
} StatMods;

/* ------------------------------------------------------------------------ */
/* Item-Arten                                                                */
/* ------------------------------------------------------------------------ */
typedef enum ItemKind {
    IK_NONE = 0,
    IK_WEAPON,
    IK_SUB,        /* Sekundaerwaffe */
    IK_ARMOR,      /* Helm, Brustpanzer, Handschuhe, Stiefel (slot gibt an welches) */
    IK_RING,
    IK_NECKLACE,
    IK_WINGS,
    IK_BAG,
    IK_POTION,
    IK_MATERIAL,   /* Metall, Holz, Gewebe, Kristall, Essenz (stapelbar) */
    IK_STONE,      /* Verbesserungs-, Verzauberungs-, Magiesteine (stapelbar) */
    IK_SPECIAL,    /* Reliktteile, Federn, Tickets ... (stapelbar) */
    IK_KIND_COUNT
} ItemKind;

#define ITEM_NONE 0xFFFFu  /* Item.def-Wert fuer "leer" */

/* Waehrungen */
typedef enum Currency {
    CUR_GOLD = 0,
    CUR_DIAMONDS,
    CUR_CREDITS,
    CUR_RAVEN_COINS,
    CUR_RAVEN_FEATHERS,
    CUR_COUNT
} Currency;

#endif /* PNX_CORE_TYPES_H */
