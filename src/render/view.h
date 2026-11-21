/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Darstellung eines Runs (Welt, Einheiten, Effekte) in ein Rechteck.
 * Wird fuer Vollbild-Held-Ansicht (grosses Rect) UND fuer die 2x2-Kommandoansicht
 * (4 kleine Rects) benutzt.
 */
#ifndef PNX_RENDER_VIEW_H
#define PNX_RENDER_VIEW_H

#include "game/game.h"
#include "render/gfx.h"
#include "sim/sim.h"

#define FX_MAX_TEXTS 96
#define FX_MAX_PARTICLES 384

typedef struct FloatText {
    bool active;
    Vec2 pos;          /* Weltpixel */
    float age, life;
    float vy;
    char text[16];
    Color color;
    bool crit;
    int size;
} FloatText;

typedef struct Particle {
    bool active;
    Vec2 pos, vel;
    float age, life, size;
    Color color;
    bool additive;
} Particle;

/* Darstellungszustand EINES Helden-Fensters (Kamera + Effekte). Liegt in App. */
typedef struct ViewFx {
    FloatText texts[FX_MAX_TEXTS];
    Particle parts[FX_MAX_PARTICLES];
    Vec2 cam;           /* geglaettete Kameraposition (Weltpixel) */
    bool cam_init;
    float zoom;         /* 0.75 .. 1.5 */
    float shake;
    int last_map;
    double time;
} ViewFx;

typedef struct ViewOpts {
    bool mini;            /* kleine Ansicht (2x2): weniger Details */
    bool show_hp_bars;
    bool show_names;
    bool show_damage;
    bool show_boss_bar;   /* grosse Bossleiste oben im Viewport */
    bool show_hud_bars;   /* HP/Mana-Balken ueber dem Helden */
    bool colorblind_symbols; /* Rauten-Symbole zusaetzlich zur Tierfarbe */
} ViewOpts;

void view_fx_init(ViewFx *fx);
/* Verarbeitet die Events des Runs fuer diesen Helden-Slot (leert die Event-Queue
 * NICHT global: nutze run_pop_event im App-Loop einmal pro Frame und gib die Events
 * mit view_fx_feed an alle betroffenen ViewFx weiter - siehe app.c). */
void view_fx_feed(ViewFx *fx, const SimEvent *ev, const Run *run);
void view_fx_update(ViewFx *fx, double dt);
/* Zeichnet den Run mit Kamera auf Held 'slot' in den Viewport. */
void view_draw_run(Gfx *g, const Game *game, const Run *run, int slot, ViewFx *fx, Rect vp,
                   const ViewOpts *opts);
/* Bildschirm -> Welt (fuer Klicks): gleiche Kamera wie view_draw_run. */
Vec2 view_screen_to_world(const ViewFx *fx, Rect vp, int sx, int sy);
Vec2 view_world_to_screen(const ViewFx *fx, Rect vp, Vec2 w);

#endif
