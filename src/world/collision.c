/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Kollision gegen die Kachelkarte: Kreis-gegen-Kachel-Tests, Linientests und
 * Bewegung mit Wand-Gleiten. */
#include <math.h>
#include <stdlib.h>

#include "core/util.h"
#include "world/world.h"

bool world_tile_solid(int tile) {
    return tile == TILE_WALL || tile == TILE_WATER;
}

int world_tile_at_pos(const GameMap *m, Vec2 p) {
    return world_tile_at(m, (int)floorf(p.x / TILE_SIZE), (int)floorf(p.y / TILE_SIZE));
}

void world_free(GameMap *m) {
    free(m->tiles);
    free(m->variant);
    m->tiles = NULL;
    m->variant = NULL;
}

bool world_walkable(const GameMap *m, Vec2 p, float r) {
    int x0 = (int)floorf((p.x - r) / TILE_SIZE);
    int x1 = (int)floorf((p.x + r) / TILE_SIZE);
    int y0 = (int)floorf((p.y - r) / TILE_SIZE);
    int y1 = (int)floorf((p.y + r) / TILE_SIZE);
    for (int ty = y0; ty <= y1; ty++) {
        for (int tx = x0; tx <= x1; tx++) {
            if (world_tile_solid(world_tile_at(m, tx, ty))) {
                return false;
            }
        }
    }
    return true;
}

bool world_line_clear(const GameMap *m, Vec2 a, Vec2 b, float r) {
    float len = v2_dist(a, b);
    int steps = (int)(len / 8.0f) + 1;
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;
        Vec2 p = v2(lerpf(a.x, b.x, t), lerpf(a.y, b.y, t));
        if (!world_walkable(m, p, r)) {
            return false;
        }
    }
    return true;
}

bool world_line_clear_proj(const GameMap *m, Vec2 a, Vec2 b) {
    float len = v2_dist(a, b);
    int steps = (int)(len / 12.0f) + 1;
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;
        Vec2 p = v2(lerpf(a.x, b.x, t), lerpf(a.y, b.y, t));
        if (world_tile_at_pos(m, p) == TILE_WALL) {
            return false;
        }
    }
    return true;
}

Vec2 world_move(const GameMap *m, Vec2 pos, Vec2 delta, float r) {
    Vec2 np = v2(pos.x + delta.x, pos.y);
    if (world_walkable(m, np, r)) {
        pos = np;
    } else {
        /* Teilschritt, um bis an die Wand zu gleiten */
        np = v2(pos.x + delta.x * 0.5f, pos.y);
        if (world_walkable(m, np, r)) {
            pos = np;
        }
    }
    np = v2(pos.x, pos.y + delta.y);
    if (world_walkable(m, np, r)) {
        pos = np;
    } else {
        np = v2(pos.x, pos.y + delta.y * 0.5f);
        if (world_walkable(m, np, r)) {
            pos = np;
        }
    }
    return pos;
}

Vec2 world_nearest_walkable(const GameMap *m, Vec2 p, float r) {
    if (world_walkable(m, p, r)) {
        return p;
    }
    for (int ring = 1; ring < 40; ring++) {
        float d = (float)ring * 16.0f;
        for (int k = 0; k < 16; k++) {
            float ang = (float)k * (PNX_PI * 2.0f / 16.0f);
            Vec2 c = v2(p.x + cosf(ang) * d, p.y + sinf(ang) * d);
            if (world_walkable(m, c, r)) {
                return c;
            }
        }
    }
    return p;
}

float world_speed_factor(const GameMap *m, Vec2 p) {
    return world_tile_at_pos(m, p) == TILE_SLOW ? 0.7f : 1.0f;
}
