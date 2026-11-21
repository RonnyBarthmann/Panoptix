/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Spielwelt einer Map: Kachelkarte, Raeume, Generator, Kollision, Pfadfindung.
 * Kein SDL. Die Karten sind deterministisch: gleiche MapDef (seed) => gleiche Karte.
 *
 * Handgebaute Karten: liegt data/maps/<map-id>.map vor (ASCII-Raster, siehe
 * docs/DATA_FORMAT.md), wird diese statt des Generators benutzt:
 *   #  Wand           .  Boden        ~  Wasser (nicht begehbar)   :  Daempfungsboden
 *   h  Heilquelle     S  Start        B  Boss-Arena-Mitte          X  Ausgang
 *   M  Monsterpack-Punkt    1..9 und a..f  Wegpunkte (Hauptpfad in Zahlenreihenfolge,
 *   danach Nebenpfade in Buchstabenreihenfolge)
 */
#ifndef PNX_WORLD_WORLD_H
#define PNX_WORLD_WORLD_H

#include "core/types.h"
#include "data/data.h"

typedef enum Tile {
    TILE_FLOOR = 0,
    TILE_WALL = 1,
    TILE_WATER = 2,   /* nicht begehbar, Projektile fliegen darueber */
    TILE_SLOW = 3,    /* begehbar, -30 % Tempo */
    TILE_HEAL = 4,    /* Heilquelle, begehbar */
    TILE_ARENA = 5    /* Boss-Arena-Boden (begehbar, nur Optik/Logik) */
} Tile;

#define MAX_ROOMS 64
#define MAX_PACK_POINTS 256
#define MAX_WAYPOINTS 64

typedef struct Room {
    int x, y, w, h;      /* Kacheln */
    Vec2 center;         /* Weltpixel */
    bool on_main_path;
    bool is_boss;
    bool is_start;
} Room;

typedef struct PackPoint {
    Vec2 pos;            /* Weltpixel */
    int room;            /* Raumindex oder -1 */
    bool side_path;      /* liegt auf einem Nebenpfad (Einzigartige!) */
} PackPoint;

typedef struct Waypoint {
    Vec2 pos;
    bool side_path;
} Waypoint;

typedef struct GameMap {
    int map_idx;
    int w, h;                 /* Kacheln */
    u8 *tiles;                /* w*h */
    u8 *variant;              /* w*h: Zufallswert 0..255 fuer Optik (Kachel-Variation, Deko) */
    Room rooms[MAX_ROOMS];
    int room_count;
    Waypoint waypoints[MAX_WAYPOINTS]; /* Hauptpfad zuerst (Start -> Arena), dann Nebenpfade */
    int main_wp_count;        /* Anzahl Wegpunkte des Hauptpfads (ohne Boss-Arena) */
    int wp_count;
    PackPoint packs[MAX_PACK_POINTS];
    int pack_count;
    Vec2 start;               /* Startpunkt in Weltpixeln */
    Vec2 exit;                /* Ausgang/Abschlusstor */
    bool has_boss_arena;
    Vec2 arena_center;        /* Weltpixel */
    Rect arena;               /* Arena-Rechteck in Weltpixeln */
    Vec2 heal_springs[8];
    int heal_count;
    bool from_file;           /* aus data/maps/<id>.map geladen */
} GameMap;

/* Baut die Karte (Datei-Override oder Generator). false bei Fehler. */
bool world_build(const MapDef *def, int map_idx, GameMap *out);
void world_free(GameMap *m);

static inline int world_tile_at(const GameMap *m, int tx, int ty) {
    if (tx < 0 || ty < 0 || tx >= m->w || ty >= m->h) {
        return TILE_WALL;
    }
    return m->tiles[ty * m->w + tx];
}
/* Weltpixel -> Tile-Typ */
int world_tile_at_pos(const GameMap *m, Vec2 p);
bool world_tile_solid(int tile);   /* Wand oder Wasser */

/* Passt ein Kreis (Mittelpunkt p, Radius r) an p, ohne Wand/Wasser zu beruehren? */
bool world_walkable(const GameMap *m, Vec2 p, float r);
/* Freie Sicht-/Laufstrecke fuer einen Kreis entlang a->b? */
bool world_line_clear(const GameMap *m, Vec2 a, Vec2 b, float r);
/* Projektil-Sichtlinie (nur Waende blockieren, Wasser nicht). */
bool world_line_clear_proj(const GameMap *m, Vec2 a, Vec2 b);
/* Bewegt pos um delta mit Wand-Gleiten (achsenweise). Gibt neue Position zurueck. */
Vec2 world_move(const GameMap *m, Vec2 pos, Vec2 delta, float r);
/* Naechster begehbarer Punkt in der Naehe von p (Spiral-Suche); bei Misserfolg p. */
Vec2 world_nearest_walkable(const GameMap *m, Vec2 p, float r);
/* Tempofaktor am Ort (1.0 normal, 0.7 auf TILE_SLOW). */
float world_speed_factor(const GameMap *m, Vec2 p);

/* A*-Pfad von 'from' nach 'to' (Weltpixel) fuer einen Kreis mit Radius r.
 * Schreibt geglaettete Wegpunkte (ohne den Startpunkt, inkl. Ziel) nach out.
 * Gibt Anzahl zurueck (0 = kein Pfad). */
int nav_find_path(const GameMap *m, Vec2 from, Vec2 to, float r, Vec2 *out, int max_points);

#endif
