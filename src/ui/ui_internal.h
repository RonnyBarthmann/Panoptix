/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Interne Helfer, die nur die Dateien in src/ui/ gemeinsam nutzen. */
#ifndef PNX_UI_UI_INTERNAL_H
#define PNX_UI_UI_INTERNAL_H

#include "app/app.h"
#include "app/screens.h"
#include "core/util.h"
#include "data/data.h"
#include "render/icons.h"
#include "render/theme.h"
#include "rules/rules.h"
#include "sim/sim.h"

/* Schriftgroessen (vor Skalierung) */
#define FS_TINY 11
#define FS_SMALL 13
#define FS_NORMAL 15
#define FS_BIG 20
#define FS_HUGE 28

/* Skalierte Schriftgroesse */
static inline int fsz(const Ui *ui, int base) { return ui_px(ui, base); }

/* Zeichnet Text, Groesse in unskalierten Pixeln. */
void ui_text(Ui *ui, int x, int y, const char *text, Color c, int base_size);
/* Text rechtsbuendig / zentriert in einem Rect. */
void ui_text_right(Ui *ui, Rect r, const char *text, Color c, int base_size);
void ui_text_center(Ui *ui, Rect r, const char *text, Color c, int base_size);
/* Zeilenhoehe (skaliert) fuer eine Basis-Schriftgroesse + Abstand. */
int ui_line_h(const Ui *ui, int base_size);

/* Teilt ein Rect: links 'w' Pixel (skaliert) abschneiden. */
Rect rect_cut_left(Rect *r, int w);
Rect rect_cut_right(Rect *r, int w);
Rect rect_cut_top(Rect *r, int h);
Rect rect_cut_bottom(Rect *r, int h);

/* Balken mit Beschriftung (HP/Mana/EXP). */
void ui_bar(Ui *ui, Rect r, float frac, Color fill, Color back, const char *text);
/* Tierfarbe mit Farbenblind-Palette. */
Color ui_tier(const App *app, int tier, u8 alpha);
/* "Akt 3 - 3-7 (Lv 22)" */
char *ui_map_label(int map_idx, char *buf, size_t size);
/* Namen einer Klasse. */
const char *ui_class_name(int cls);
/* Zahl kompakt/mit Trennzeichen */
char *ui_num(i64 v, char *buf, size_t size);
/* Button mit Tooltip-Text, gibt true bei Klick. */
bool ui_btn(App *app, Rect r, const char *label, bool enabled);
/* Meldung nach Regelaufruf: err anzeigen (Fehlerton) oder ok */
void ui_result(App *app, bool ok, const char *err, const char *ok_text);
/* Waehrungszeile "Gold 1.234" */
void ui_draw_currency_line(App *app, int x, int y, Currency c, int base_size);
/* Gemeinsamer Zustand fuer "Item anlegen auswaehlen" zwischen Screens. */
extern int g_ui_inv_select; /* Lager-Index oder -1 */

/* Kleine Klassen-Auswahl fuer Skill-/KI-Presets (Screens teilen sich die Logik) */
void screen_skills_ai_panel(App *app, Rect r, Hero *h);   /* in screen_skills.c */
bool screen_combat_ai_popup(App *app, Rect area, Hero *h); /* in screen_combat.c, true = offen */

#endif
