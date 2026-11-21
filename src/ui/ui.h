/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Immediate-Mode-UI im Pergament-Stil. Jede Funktion zeichnet UND wertet Eingaben
 * aus; es gibt keinen Widget-Baum. IDs werden aus Label + Position + ID-Stack
 * gebildet (ui_push_id fuer Schleifen).
 *
 * Layout-Konvention: Alle Rects sind in Bildschirmpixeln. Die UI rechnet Masse
 * mit ui_px(ui, n) (n * ui_scale) selbst um, wenn sie feste Groessen braucht.
 */
#ifndef PNX_UI_UI_H
#define PNX_UI_UI_H

#include "game/game.h"
#include "render/gfx.h"

/* ---- Eingabe ----------------------------------------------------------- */
typedef enum Key {
    KEY_NONE = 0,
    KEY_ESCAPE, KEY_ENTER, KEY_BACKSPACE, KEY_TAB, KEY_SPACE, KEY_DELETE,
    KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_PAGEUP, KEY_PAGEDOWN,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
    KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M,
    KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
    KEY_COUNT
} Key;

typedef struct UiInput {
    int mx, my;
    bool lmb_down, lmb_pressed, lmb_released;
    bool rmb_down, rmb_pressed;
    bool mmb_pressed;
    int wheel;                       /* + = hoch gescrollt */
    bool key_down[KEY_COUNT];
    bool key_pressed[KEY_COUNT];     /* in diesem Frame neu gedrueckt (inkl. Auto-Repeat) */
    bool ctrl, shift, alt;
    char text[32];                   /* eingegebener Text dieses Frames (UTF-8) */
    bool quit_requested;
} UiInput;

/* ---- Kontext ----------------------------------------------------------- */
#define UI_ID_STACK 16
#define UI_TIP_MAX 600
#define UI_SCROLL_SLOTS 32

typedef struct Ui {
    Gfx *gfx;
    UiInput in;
    float scale;                     /* = gfx->ui_scale */
    u32 hot, active;                 /* Widget unter der Maus / gerade gedrueckt */
    u32 focus;                       /* Textfeld mit Tastaturfokus */
    u32 id_stack[UI_ID_STACK];
    int id_depth;
    char tip[UI_TIP_MAX];            /* Tooltip fuer dieses Frame */
    char tip_title[64];
    u32 tip_color;
    int tip_x, tip_y;
    bool tip_set;
    bool modal_open;                 /* true: Widgets ausserhalb des Modals reagieren nicht */
    struct { u32 id; float v; } scroll_state[UI_SCROLL_SLOTS];
    double time;
    int cursor_blink;
    float anim_dt;
} Ui;

void ui_begin_frame(Ui *ui, Gfx *gfx, const UiInput *in, double time, float dt);
void ui_end_frame(Ui *ui);          /* zeichnet Tooltip */
static inline int ui_px(const Ui *ui, int n) { return (int)((float)n * ui->scale + 0.5f); }
void ui_push_id(Ui *ui, int n);
void ui_pop_id(Ui *ui);
u32 ui_make_id(const Ui *ui, const char *label, int x, int y);

/* ---- Widgets ----------------------------------------------------------- */
typedef struct UiClick {
    bool hovered, clicked, rclicked, down, double_clicked;
} UiClick;
/* Klickflaeche ohne Zeichnen. */
UiClick ui_region(Ui *ui, Rect r, u32 id);

typedef enum UiStyle { UI_BTN_NORMAL = 0, UI_BTN_WAX, UI_BTN_GREEN, UI_BTN_FLAT, UI_BTN_TAB } UiStyle;

/* Holz-/Pergament-Panel mit Titelleiste. Gibt den Innenbereich (unter dem Titel) zurueck. */
Rect ui_panel(Ui *ui, Rect r, const char *title);
void ui_panel_plain(Ui *ui, Rect r);                       /* nur Pergamentflaeche mit Rahmen */
bool ui_button(Ui *ui, Rect r, const char *label);          /* Wachssiegel-Button */
bool ui_button_ex(Ui *ui, Rect r, const char *label, UiStyle style, bool enabled);
bool ui_button_tip(Ui *ui, Rect r, const char *label, UiStyle style, bool enabled, const char *tip);
void ui_label(Ui *ui, int x, int y, const char *text, Color c, int size);
void ui_label_right(Ui *ui, int x_right, int y, const char *text, Color c, int size);
/* Label mit Zeilenumbruch; gibt Hoehe zurueck. */
int ui_label_wrapped(Ui *ui, Rect r, const char *text, Color c, int size);
bool ui_checkbox(Ui *ui, Rect r, const char *label, bool *value);
bool ui_slider(Ui *ui, Rect r, float *value, float min, float max);
/* Tabs nebeneinander; *active wird gesetzt; true wenn gewechselt. */
bool ui_tabs(Ui *ui, Rect r, const char *const *labels, int count, int *active);
void ui_progress_bar(Ui *ui, Rect r, float frac, Color fill, const char *text);
/* Vertikaler Scrollbereich. content_h = Gesamthoehe des Inhalts in px. Gibt den
 * y-Offset zurueck, den der Aufrufer von allen Zeichen-y-Werten abziehen muss.
 * Setzt Clip auf r. Immer mit ui_scroll_end beenden. Mausrad scrollt wenn Maus ueber r. */
int ui_scroll_begin(Ui *ui, Rect r, int content_h, const char *id);
void ui_scroll_end(Ui *ui);
/* Textfeld; gibt true bei Aenderung zurueck. */
bool ui_text_input(Ui *ui, Rect r, char *buf, size_t size, const char *id);
/* Dropdown-aehnliche Auswahl durch Weiterschalten (Klick = naechster Wert, Rechtsklick = vorheriger). */
bool ui_cycle(Ui *ui, Rect r, const char *const *options, int count, int *value);
/* Item-Slot mit Icon, Tooltip (ruft ui_tooltip_item), Auswahlrahmen. */
UiClick ui_item_slot(Ui *ui, Rect r, const Game *game, const Hero *compare_to, const Item *it,
                     bool selected);
/* Stapel-Slot (Material/Stein) mit Zahl. */
UiClick ui_stack_slot(Ui *ui, Rect r, int def, i64 count, bool selected);
/* Bestaetigungsdialog (modal). Gibt 0 = offen, 1 = Ja, -1 = Nein zurueck. 'open' wird gesetzt/geloescht. */
int ui_confirm(Ui *ui, bool *open, const char *title, const char *text);

/* ---- Tooltips ---------------------------------------------------------- */
/* Einfacher Text-Tooltip fuer dieses Frame (erste Zeile = Titel optional). */
void ui_tooltip(Ui *ui, const char *title, u32 title_rgb, const char *text);
/* Detaillierter Item-Tooltip (Name in Tierfarbe, Slot, Level, Werte, Effekte,
 * Verzauberung, Vergleich mit dem Angelegten von compare_to). */
void ui_tooltip_item(Ui *ui, const Game *game, const Hero *compare_to, const Item *it);
void ui_tooltip_skill(Ui *ui, const Game *game, int skill);
/* Item-Beschreibung als Text (mehrzeilig, '\n'); wird vom Tooltip und von UI-Detailfeldern genutzt. */
void item_describe(const Game *game, const Item *it, char *buf, size_t size);
void itemdef_describe(const ItemDef *d, char *buf, size_t size);
void skill_describe(const Game *game, int skill, char *buf, size_t size);
/* Kurzbeschreibung einer Stat-Zeile "Krit-Chance +5.0 %". */
void stat_line(int stat, float value, char *buf, size_t size);

#endif
