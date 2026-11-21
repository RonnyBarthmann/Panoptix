/* SPDX-License-Identifier: GPL-3.0-or-later */
/* 2D-Zeichenschicht ueber SDL2: Primitive, Text, Screenshots.
 * Dies ist die EINZIGE Stelle neben app/ und audio/, die SDL anfasst.
 * Alle Koordinaten sind logische Pixel (nach UI-Skalierung wird nicht
 * automatisch skaliert - die UI rechnet mit gfx->ui_scale selbst, siehe ui.h).
 */
#ifndef PNX_RENDER_GFX_H
#define PNX_RENDER_GFX_H

#include "core/types.h"

struct GfxImpl; /* SDL-Details (nur in gfx.c sichtbar) */

typedef enum ColorblindMode { CB_OFF = 0, CB_DEUTERAN, CB_PROTAN, CB_TRITAN, CB_COUNT } ColorblindMode;

typedef struct Gfx {
    struct GfxImpl *impl;
    int w, h;                 /* aktuelle Fenstergroesse in Pixeln */
    float ui_scale;           /* 0.75 .. 2.0 (Einstellung) */
    ColorblindMode colorblind;
    bool headless;            /* true = SDL "dummy"-Treiber (Screenshot-Tests) */
} Gfx;

/* Fenster + Renderer erzeugen. headless: SDL_VIDEODRIVER=dummy + Software-Renderer. */
bool gfx_init(Gfx *g, const char *title, int w, int h, bool fullscreen, bool vsync, bool headless);
void gfx_shutdown(Gfx *g);
void gfx_set_fullscreen(Gfx *g, bool fullscreen);
void gfx_begin_frame(Gfx *g, Color clear);
void gfx_end_frame(Gfx *g);          /* Present */
void gfx_update_size(Gfx *g);        /* bei Resize-Event aufrufen */

/* Clipping (Verschachtelung: gfx_push_clip/gfx_pop_clip schneiden mit dem aktuellen Clip). */
void gfx_push_clip(Gfx *g, Rect r);
void gfx_pop_clip(Gfx *g);

/* Primitive. Alpha wird beachtet (Blending immer an). */
void gfx_fill_rect(Gfx *g, Rect r, Color c);
void gfx_draw_rect(Gfx *g, Rect r, Color c, int thickness);
void gfx_fill_rect_vgrad(Gfx *g, Rect r, Color top, Color bottom);
void gfx_line(Gfx *g, float x1, float y1, float x2, float y2, Color c, float thickness);
void gfx_fill_circle(Gfx *g, float cx, float cy, float radius, Color c);
void gfx_draw_circle(Gfx *g, float cx, float cy, float radius, Color c, float thickness);
/* Kreisbogen (Winkel in Grad, 0 = rechts, im Uhrzeigersinn) mit Dicke - fuer Cooldown-Kreise. */
void gfx_arc(Gfx *g, float cx, float cy, float radius, float a0, float a1, Color c, float thickness);
/* Gefuellter Kreisausschnitt (Kuchenstueck) - Cooldown-Overlay, Kegel-Telegraphen. */
void gfx_fill_pie(Gfx *g, float cx, float cy, float radius, float a0, float a1, Color c);
void gfx_fill_triangle(Gfx *g, Vec2 a, Vec2 b, Vec2 c, Color col);
/* Konvexes Polygon (Fan aus Punkt 0). */
void gfx_fill_poly(Gfx *g, const Vec2 *pts, int n, Color c);
/* Additives Blending an/aus (Glow-Effekte). */
void gfx_set_additive(Gfx *g, bool additive);

/* Text. 'size' ist die Zeilenhoehe in Pixeln (z.B. 12, 14, 16, 20, 28).
 * Unterstuetzt UTF-8 fuer Latin-1 (Umlaute, ß) und ein paar Symbole. Faerbung per c. */
void gfx_text(Gfx *g, int x, int y, const char *text, Color c, int size);
int gfx_text_width(Gfx *g, const char *text, int size);
int gfx_text_height(Gfx *g, int size);
/* Text mit Schlagschatten (besser lesbar auf wechselndem Hintergrund). */
void gfx_text_shadow(Gfx *g, int x, int y, const char *text, Color c, int size);
void gfx_text_centered(Gfx *g, Rect r, const char *text, Color c, int size);
/* Umbrechender Text in Rechteck; gibt benoetigte Hoehe zurueck (zeichnet nur wenn draw). */
int gfx_text_wrapped(Gfx *g, Rect r, const char *text, Color c, int size, bool draw);

/* Farbe unter Beachtung des Farbenblind-Modus transformieren. */
Color gfx_cb(const Gfx *g, Color c);
/* Tierfarbe (mit Farbenblind-Palette). */
Color gfx_tier_color(const Gfx *g, Tier t, u8 alpha);

/* Speichert den aktuellen Framebuffer als BMP. */
bool gfx_screenshot(Gfx *g, const char *path);

#endif
