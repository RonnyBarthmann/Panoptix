/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Spielregeln: Formeln, Loot, Crafting, Verbessern, Verzaubern, Shop,
 * Kommandeur. Alles hier ist "reine Logik" auf dem Game-Zustand: kein SDL,
 * kein Rendering. Die UI ruft diese Funktionen auf, die Simulation ebenfalls.
 *
 * Konvention: Funktionen, die etwas kaufen/herstellen/aendern, geben bool
 * zurueck (true = Erfolg) und schreiben bei Misserfolg einen kurzen,
 * deutschen Grund in 'err' (falls err != NULL, Puffer >= 96 Bytes).
 */
#ifndef PNX_RULES_RULES_H
#define PNX_RULES_RULES_H

#include "game/game.h"

/* ======================================================================== */
/* stats.c - Stat-Berechnung (Spec 03)                                       */
/* ======================================================================== */
/* Summiert Ausruestung (inkl. Verbesserung +n und Verzauberung), Level,
 * Kommandeur-Traits, Set-Boni und Legendaer-Effekte in h->inputs und
 * berechnet h->stats. Setzt stats_dirty = false. */
void hero_recalc_stats(const Game *g, Hero *h);
/* Wie hero_recalc_stats, aber nur sicherstellen dass es aktuell ist. */
const DerivedStats *hero_stats(const Game *g, Hero *h);
/* Berechnet aus 'in' die abgeleiteten Werte. Reine Funktion; wird auch von
 * der Simulation fuer Buff-Berechnung genutzt. */
void stats_finalize(const StatInputs *in, DerivedStats *out);
/* Waffen-/Rüstungs-Hauptwert eines Item-Exemplars inkl. Verbesserung. */
float item_main_value(const Item *it);
/* Summe der Zusatz-Stats eines Exemplars: feste Effekte + Verbesserung
 * (%mehr-Meilensteine) + Verzauberung. Wird in 'out' addiert. */
void item_add_mods(const Item *it, StatMods *out);
/* EXP, die ein Held fuer Level L -> L+1 braucht. */
u64 hero_exp_for_level(int level);
u64 player_exp_for_level(int level);
/* Monster-Werte (Spec 15): Basis-HP/Schaden/Ruestung/Widerstand fuer ein Level. */
float monster_base_hp(int level);
float monster_base_damage(int level);
float monster_base_armor(int level);
float monster_base_spellres(int level);
float monster_exp_value(int map_level, Tier tier);
/* Schadensminderung R/(R + k*Lv + c), gedeckelt. */
float armor_reduction(float armor, int attacker_level);

/* ======================================================================== */
/* progression.c - Level, Punkte, Skills                                     */
/* ======================================================================== */
/* EXP gutschreiben (Level-Ups, Stat-Punkte). Gibt Anzahl Level-Ups zurueck. */
int hero_gain_exp(Game *g, int hero_idx, u64 amount);
int player_gain_exp(Game *g, u64 amount);
bool hero_spend_point(Game *g, int hero_idx, int stat /*0 STR 1 DEX 2 INT*/, int amount);
bool hero_reset_stats(Game *g, int hero_idx, char *err); /* kostet Diamanten (g_bal) */
/* Skillbuch gutschreiben: erster Fund schaltet Lv1 frei, weitere werden
 * Buecher zum Mergen. */
void skill_add_book(Game *g, int skill, int count);
/* Merge-Kosten: Buecher (= Level) + Gold 1000*Lv^2*Gradfaktor. */
bool skill_can_level_up(const Game *g, int skill, i64 *gold_cost, int *books_needed);
bool skill_level_up(Game *g, int skill, char *err);
/* Skill in Slot setzen (Klasse/Freischaltung pruefen). skill = -1 leert den Slot. */
bool hero_set_skill(Game *g, int hero_idx, int slot, int skill, char *err);
/* Skill-Schaden in % bei Level: dmg_pct * (1 + skill_level_scale*(lv-1)) */
float skill_dmg_pct(const SkillDef *s, int level);
float skill_stat_value(const SkillDef *s, int level, bool second);
/* Standard-KI-Einstellungen (Klassen-Preset "Empfohlen") fuer einen Helden. */
void hero_ai_defaults(Hero *h);
/* Build-Vorlage anwenden (Spec 06 "BUILD-VORLAGEN"): 0..CLASS_COUNT-1 je Klasse genau eine. */
bool hero_apply_build_preset(Game *g, int hero_idx);
/* Startausruestung (Level-0-Waffe, 1 Normal-Heiltrank). */
void hero_give_starting_gear(Game *g, int hero_idx);

/* ======================================================================== */
/* inventory.c - Lager, Anlegen, Zerlegen, Verkaufen                         */
/* ======================================================================== */
/* Fuegt ein Item (nicht stapelbar) ein; stapelbare gehen in stock[]. false = Lager voll. */
bool inv_add_item(Game *g, const Item *it);
void inv_add_stock(Game *g, int def, u32 count); /* kappt bei stack_max */
bool inv_take_stock(Game *g, int def, u32 count); /* false wenn zu wenig */
void inv_remove_at(Game *g, int index);          /* entfernt (nach hinten aufruecken) */
int inv_free_slots(const Game *g);
/* Ist das Item stapelbar (Material/Stein/Spezial)? */
bool item_is_stackable(int def);
/* Sortierung: Tier absteigend, Level absteigend, Name. */
void inv_sort(Game *g);
/* Anlegen aus dem Lager (inv index) an Held; tauscht ggf. mit Angelegtem.
 * Prueft Klasse (Waffen/Sub), Mindestlevel, Slot (Ringe: erster freier Ring-Slot,
 * 'slot' >= 0 erzwingt Slot, SLOT_NONE = automatisch). */
bool hero_equip(Game *g, int hero_idx, int inv_index, EquipSlot slot, char *err);
bool hero_unequip(Game *g, int hero_idx, EquipSlot slot, char *err);
/* Kann der Held dieses Item (Klasse/Level) tragen? */
bool hero_can_use(const Hero *h, const ItemDef *d, char *err);
/* Zerlegen: gibt Materialien/Steine/Gold zurueck, entfernt Item. Gesperrte nicht. */
bool item_dismantle_at(Game *g, int inv_index, char *err);
/* Zerlegt alle Items unterhalb der Filter (Tier/Level), ohne gesperrte. Gibt Anzahl zurueck. */
int inv_dismantle_filtered(Game *g, int below_tier, int below_level);
i64 item_sell_value(const Item *it);
bool item_sell_at(Game *g, int inv_index, char *err);
void item_toggle_lock(Item *it);
/* Anzeigename inkl. "+n": "Eisenschwert +5" */
char *item_display_name(const Item *it, char *buf, size_t size);

/* ======================================================================== */
/* loot.c - Kisten (Spec 11)                                                 */
/* ======================================================================== */
typedef struct ChestResult {
    Item items[16];       /* erzeugte nicht stapelbare Items (bereits ins Lager gelegt) */
    int item_count;
    struct { int def; u32 count; } stacks[16]; /* Materialien/Steine (bereits im Lager) */
    int stack_count;
    int skill_books[4];   /* gefundene Skillbuecher (Skill-Index) */
    int book_count;
    int lost_items;       /* wegen vollem Lager verworfen */
    Tier tier;
} ChestResult;

/* Wuerfelt Kisten fuer einen Kill (hierarchisch Legendaer -> Normal, siehe
 * Spec 11 Abschnitt 1) inkl. Kommandeur-Modifikatoren. Schreibt Tiers in out. */
void loot_roll_kill(Game *g, Tier monster_tier, int map_level, KillLoot *out);
/* Oeffnet eine Kiste des Tiers (nimmt sie NICHT aus dem Lager; Aufrufer tut das). */
void chest_open_one(Game *g, Tier tier, int map_level, ChestResult *out);
/* Oeffnet n Kisten des Tiers aus dem Lager; Ergebnisse werden summiert in out
 * (stacks/items begrenzt - Rest geht trotzdem ins Lager). Gibt geoeffnete Anzahl zurueck. */
int chest_open_from_storage(Game *g, Tier tier, int count, ChestResult *out);
/* Wuerfelt ein einzelnes Item fuer Kiste/Shop: kind-Gruppe, Tier, Ziel-Level. -1 = kein Treffer. */
int loot_pick_item_def(Game *g, ItemKind kind, Tier tier, int target_level, EquipSlot slot, int cls);
/* Kistenkapazitaet des Helden (10 + Brust + Tasche + Traits). */
int hero_chest_capacity(const Game *g, const Hero *h);
/* Wie viele Normal-/Magisch-Kisten darf das Lager halten? (Faith-Traits) */
u32 inv_chest_storage_limit(const Game *g, Tier tier);

/* ======================================================================== */
/* craft.c - Herstellen, Verbessern, Verzaubern, Umwandeln (Spec 12)         */
/* ======================================================================== */
#define CRAFT_MAX_INPUTS 6
typedef struct Recipe {
    int output;                       /* Item-Index */
    int input_def[CRAFT_MAX_INPUTS];
    u32 input_count[CRAFT_MAX_INPUTS];
    int input_n;
    i64 gold;
    bool unlocked;                    /* freigeschaltet (Spielerlevel/Bauplan)? */
    char lock_reason[64];
} Recipe;

bool craft_recipe(const Game *g, int item_def, Recipe *out);
bool craft_can_make(const Game *g, const Recipe *r, int qty);
/* Stellt qty Exemplare her (Zutaten + Gold abziehen, Items ins Lager). */
bool craft_make(Game *g, int item_def, int qty, char *err);

/* Verbessern */
float enhance_success_chance(int next_plus);          /* in % */
int enhance_stone_cost(int next_plus);
i64 enhance_gold_cost(int next_plus, Tier tier);
int enhance_stone_def(const ItemDef *d);              /* Item-Index des benoetigten Steins */
typedef enum EnhanceResult { ENH_OK = 0, ENH_FAIL, ENH_BLOCKED, ENH_MAX } EnhanceResult;
EnhanceResult enhance_try(Game *g, Item *it, char *err);

/* Verzaubern */
bool enchant_can(const Game *g, const Item *it, char *err);
int enchant_stone_def(const ItemDef *d);
i64 enchant_gold_cost(const ItemDef *d);
bool enchant_roll(Game *g, Item *it, char *err);      /* wuerfelt neu (Siegel beachten) */
void enchant_set_seal(Item *it, bool sealed);
/* Text der Verzauberung: "+12 % Schaden auf Vollmondschnitt" */
char *enchant_describe(const Item *it, char *buf, size_t size);
void enchant_apply_mods(const Item *it, StatMods *out);

/* Umwandeln (Stein-Synthese). 'tier_to_make' = Zieltier (1..5). */
bool convert_materials(Game *g, int from_def, int times, char *err);
/* Welcher Gegenstand entsteht beim Umwandeln von from_def? (-1 wenn nicht moeglich) */
int convert_target_def(int from_def, u32 *need_count);
/* Waffen-/Ruestungsstein -> Schmuckstein */
bool convert_to_jewel_stone(Game *g, Tier tier, bool from_weapon, int times, char *err);

/* ======================================================================== */
/* shop.c - Shop, Basislager-Haendler                                        */
/* ======================================================================== */
bool shop_offer_available(const Game *g, int offer, char *why, size_t why_size);
i64 shop_offer_price(const Game *g, int offer);
int shop_offer_remaining(const Game *g, int offer);  /* -1 = unbegrenzt */
bool shop_buy(Game *g, int offer, char *err);
bool currency_can_pay(const Game *g, Currency c, i64 amount);
bool currency_pay(Game *g, Currency c, i64 amount);
void currency_add(Game *g, Currency c, i64 amount);
i64 currency_get(const Game *g, Currency c);
/* Setzt Tages-/Wochen-/Monatskontingente zurueck, falls Schluessel abweicht. */
void shop_check_resets(Game *g);

/* ======================================================================== */
/* commander.c - Traits, Kommandeur-Skills, Kommandoposten, Quests, Post     */
/* ======================================================================== */
typedef struct CommanderMods {
    float drop_qty_pct;       /* mehr Kistenwuerfe */
    float drop_quality_pct;   /* hoehere Kisten-Raritaet (Chancen-Multiplikator) */
    float exp_pct, gold_pct;
    float density_pct;        /* Monsterdichte */
    float dupe_chest_pct;     /* Doppelkisten-Chance */
    float unique_double_pct;  /* Chance auf 2 Einzigartige */
    float craft_cost_pct;     /* negativ = guenstiger */
    float shop_discount_pct;
    int bounty_slots;
    float death_time_pct;     /* negativ = kuerzere Todeszeit */
    float retry_time_pct;     /* negativ = kuerzerer Retry */
    int chest_cap_flat;
    int storage_limit_bonus;  /* Lagergrenze Normal/Magisch-Kisten */
    float move_speed_pct;
    float flat_attack;
    float armor_pct;
    float spellres_pct;
    float cdr_pct;
    float potion_regen;       /* Traenke fuellen sich passiv (% pro Sekunde) */
    float magic_stone_pct;    /* mehr Magiesteine (Suchbefehl) */
} CommanderMods;

/* Berechnet alle Kommandeur-Modifikatoren (Traits + aktive Buffs + Befehle). */
void commander_mods(const Game *g, CommanderMods *out);
int commander_points_available(const Game *g);
bool commander_spend_trait(Game *g, int trait, char *err);
bool commander_reset_traits(Game *g, char *err);
/* Kommandeur-Skills (Buffs 1 h): 0 Entdeckersicht (Kisten) 1 Berater (EXP)
 * 2 Glueck (Raritaet) 3 Augen der Fuelle (Gold). */
bool commander_skill_active(const Game *g, int skill);
bool commander_activate_skill(Game *g, int skill, char *err);
/* Kommandoposten */
void commander_update_post(Game *g, i64 now_unix);  /* Abwesenheit/Idle verrechnen */
bool commander_toggle_order(Game *g, int order, char *err);
bool commander_adjutant(Game *g, char *err);        /* 60 CR: 1 h Idle-Ertrag */
/* Kopfgeld-/Hauptquests */
void quests_refresh_daily(Game *g, bool paid, char *err_unused);
void quests_progress(Game *g, QuestKind kind, int param, int amount);
bool quest_claim(Game *g, int bounty_index, char *err);
bool main_quest_claim(Game *g, char *err);
/* Post */
void mail_add(Game *g, const char *title, const char *text, Currency cur, i64 amount);
bool mail_claim(Game *g, int index);

/* ======================================================================== */
/* Zusaetze (additiv)                                                        */
/* ======================================================================== */
/* Gold pro Kill (vor Kommandeur-Bonus). */
float game_kill_gold(int monster_level, Tier tier);
/* Skill-AI-Standardwerte eines Skills in ein SkillAi uebertragen. */
void skill_ai_from_def(SkillAi *out, const SkillDef *s);
/* Map-Level -> Akt (1..11), fuer Magiesteine. */
int act_for_level(int level);

#endif /* PNX_RULES_RULES_H */
