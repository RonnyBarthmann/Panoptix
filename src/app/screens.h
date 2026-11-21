/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Alle Bildschirme. Signatur einheitlich: zeichnet in 'area' (Inhaltsbereich
 * unter der oberen Leiste) und verarbeitet Eingaben ueber app->ui.
 * Implementierung: src/ui/screen_*.c */
#ifndef PNX_APP_SCREENS_H
#define PNX_APP_SCREENS_H

#include "app/app.h"

/* ---- Helden-Ansichten -------------------------------------------------- */
void screen_combat(App *app, Rect area);      /* Vollbild-Held: Welt, HUD, Skill-/Trankleiste, Kistenleiste */
void screen_command(App *app, Rect area);     /* 2x2-Layouts */
void screen_herolist(App *app, Rect area);    /* Liste, Heldenerstellung */
void screen_hero(App *app, Rect area);        /* Ausruestung (14 Slots) + Stats */
void screen_skills(App *app, Rect area);      /* Skills + KI-Bedingungen + Held-KI-Einstellungen */

/* ---- Verwaltung -------------------------------------------------------- */
void screen_inventory(App *app, Rect area);
void screen_crafting(App *app, Rect area);
void screen_worldmap(App *app, Rect area);
void screen_shop(App *app, Rect area);
void screen_commander(App *app, Rect area);
void screen_settings(App *app, Rect area);
void screen_menu(App *app, Rect area);

/* Gemeinsame Helfer (src/ui/ui_common.c) */
/* Kopfzeile mit Heldenauswahl (Chips); true wenn Auswahl gewechselt. */
bool ui_hero_picker(App *app, Rect r);
/* Klassen-/Held-Kurzinfo "Name (Ritter Lv 12)" */
char *hero_label(const Hero *h, char *buf, size_t size);
/* Ausgewaehlten Helden sicherstellen (erster belegter Slot, falls keiner). */
void app_ensure_hero(App *app);

#endif
