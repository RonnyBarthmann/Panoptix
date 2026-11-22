/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Interne Helfer der Regel-Module (nur innerhalb src/rules/ und src/game/ nutzen). */
#ifndef PNX_RULES_RULES_INTERNAL_H
#define PNX_RULES_RULES_INTERNAL_H

#include "rules/rules.h"

/* Markiert die Stat-Caches aller Helden als veraltet (nach Aenderung an Items/Traits). */
void game_mark_all_dirty(Game *g);
/* Schreibt (falls err != NULL) einen deutschen Fehlergrund. */
void rules_err(char *err, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
/* Verbesserungs-Faktor auf den Hauptwert (Ruestung/Sub/Schmuck) bei Stufe 'plus'. */
float enhance_main_factor(int plus);
/* Kategorie-Hilfen */
bool item_kind_is_equipment(ItemKind k);
/* Grad-Faktor fuer Skill-Merge-Kosten (Normal 1, Magisch 2, Selten 4, Einzigartig 8). */
int skill_grade_factor(int grade);
/* Zufaelliges Skillbuch (Skill-Index) des Grades 'grade' (0..3, faellt bei Mangel nach unten);
 * cls < 0: Klassen- und Allgemein-Skills, sonst nur Skills dieser Klasse. -1 = nichts gefunden. */
int loot_pick_skill_book(Game *g, int grade, int cls);
/* Standard-Loot-Level fuer Kisten aus dem Lager (hoechste zugewiesene Map der Helden). */
int loot_default_level(const Game *g);
/* Craft-Kostenfaktor (Kommandeur-Trait), z. B. 0.8. */
float craft_cost_factor(const Game *g);
/* Verzauberungs-Optionen (enchant.c): Anzahl/Zugriff auf den Pool eines Gegenstands. */
int enchant_option_pool(const ItemDef *d, int *out, int cap);

#endif
