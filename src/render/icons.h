/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Prozedurale Icons (Platzhalter-Prinzip, Spec 19 Abschnitt 5): Form + Farbe.
 * Hintergrund = Tierfarbe (Boss: rot). Austauschbar durch Atlas-Grafiken. */
#ifndef PNX_RENDER_ICONS_H
#define PNX_RENDER_ICONS_H

#include "game/game.h"
#include "render/gfx.h"

/* Item-Icon (mit Tierhintergrund, Rautensymbole bei Farbenblind-Modus,
 * "+n"-Badge wenn it->plus > 0, Schloss wenn gesperrt, Fuellstand bei Traenken). */
void icon_draw_item(Gfx *g, Rect r, const Item *it);
void icon_draw_item_def(Gfx *g, Rect r, int def, int plus);
/* Material/Stein-Icon mit Stapelzahl unten rechts (count < 0 = keine Zahl). */
void icon_draw_stack(Gfx *g, Rect r, int def, i64 count);
/* Skill-Icon (48 px-Stil) mit Gradrahmen; level > 0 zeigt "Lv". */
void icon_draw_skill(Gfx *g, Rect r, int skill, int level);
/* Kisten-Sprite in Tierfarbe (Boss rot leuchtend, Legendaer rosa mit Glitzer). */
void icon_draw_chest(Gfx *g, Rect r, Tier tier, double time);
/* Klassen-/Helden-Token (Kreis in Klassenfarbe, Klassen-Glyphe, Look-Variante). */
void icon_draw_class(Gfx *g, Rect r, int cls, int look);
/* Leerer Slot (gestrichelte Umrandung + Slot-Symbol). */
void icon_draw_empty_slot(Gfx *g, Rect r, EquipSlot slot);
/* Waehrungs-Icon (Gold, Diamant, Credits, Rabenmuenze, Rabenfeder). */
void icon_draw_currency(Gfx *g, Rect r, Currency c);
/* Tier-Symbol: n Rauten (0..5) */
void icon_draw_tier_diamonds(Gfx *g, int x, int y, int size, Tier tier, Color c);

#endif
