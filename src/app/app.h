/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Anwendungsschicht: Fenster, Hauptschleife, Bildschirm-Verwaltung. */
#ifndef PNX_APP_APP_H
#define PNX_APP_APP_H

#include "app/settings.h"
#include "audio/audio.h"
#include "game/game.h"
#include "render/gfx.h"
#include "render/view.h"
#include "ui/ui.h"

typedef enum Screen {
    SCR_COMBAT = 0,   /* Helden-Vollbild (Combat View) */
    SCR_COMMAND,      /* 2x2-Kommandoansicht (F2) */
    SCR_HEROLIST,     /* Heldenliste / Squad (F1) */
    SCR_HERO,         /* Heldendetails: Ausruestung + Stats (H) */
    SCR_SKILLS,       /* Skills + KI-Konfiguration (K) */
    SCR_INVENTORY,    /* Inventar (I) */
    SCR_CRAFTING,     /* Herstellung/Verbessern/Verzaubern/Umwandeln (C) */
    SCR_WORLDMAP,     /* Weltkarte / Map-Auswahl (M, F3) */
    SCR_SHOP,         /* Shop + Basislager (S) */
    SCR_COMMANDER,    /* Kommandeur: Traits, Skills, Posten, Quests, Post, Erfolge (P) */
    SCR_SETTINGS,
    SCR_MENU,         /* ESC-Menue / Titelmenue: Weiter, Neu, Laden, Speichern, Einstellungen, Beenden */
    SCR_COUNT
} Screen;

#define MAX_LAYOUTS 4

typedef struct App {
    Gfx gfx;
    Ui ui;
    Game game;
    Settings settings;
    Audio *audio;               /* kann NULL sein */
    ViewFx fx[MAX_HEROES];      /* Kamera/Effekte je Held */
    Screen screen;              /* aktuelle Ansicht */
    Screen prev_screen;         /* fuer ESC/Zurueck */
    int sel_hero;               /* ausgewaehlter Held (Index in game.heroes) oder -1 */
    int layout;                 /* aktives 2x2-Layout 0..3 */
    int layouts[MAX_LAYOUTS][4];/* Helden-Indizes je Feld oder -1 */
    char layout_names[MAX_LAYOUTS][16];
    bool running;
    bool have_game;             /* false = noch kein Spiel geladen (Titelmenue) */
    double time;                /* Echtzeit seit Start in s */
    float frame_dt;
    int selected_map;           /* Weltkarte: ausgewaehlte Map */
    char message[128];          /* Statuszeile (z.B. "Gespeichert") */
    float message_ttl;
    bool show_fps;
    bool pause;                 /* Simulation pausiert */
    /* Screenshot-/Testmodus (CLI): */
    const char *shot_path;
    int shot_frames;
    int shot_screen;
} App;

/* Hilfsfunktionen, die Screens nutzen duerfen. */
void app_set_screen(App *app, Screen s);
void app_message(App *app, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
void app_play(App *app, Sfx sfx);                 /* Sound (NULL-sicher) */
/* Ist ein Held ausgewaehlt und gueltig? */
bool app_has_hero(const App *app);
Hero *app_hero(App *app);                         /* ausgewaehlter Held oder NULL */
/* Waehlt naechsten/vorherigen belegten Heldenplatz. */
void app_select_next_hero(App *app, int dir);
/* Speichern/Laden ueber Menue (zeigt Meldung). */
bool app_save(App *app);
bool app_load(App *app, int slot);
void app_new_game(App *app);
/* Zeichnet obere Leiste (Level, Gold, Diamanten, CR, Navigations-Buttons) und
 * Toasts; liefert den freien Inhaltsbereich darunter. Wird von app.c VOR jedem Screen aufgerufen. */
Rect app_draw_topbar(App *app);

/* Haupteinstieg (von main.c). Gibt Exit-Code zurueck. */
int app_run(int argc, char **argv);

#endif
