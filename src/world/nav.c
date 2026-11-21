/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A*-Pfadsuche auf dem Kachelraster (8 Nachbarn) mit Pfadglaettung
 * ("String Pulling" ueber Sichtlinien). */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "core/util.h"
#include "world/world.h"

/* Arbeitsspeicher wird wiederverwendet (Single-Thread-Simulation). */
static float *s_g = NULL;
static int *s_parent = NULL;
static u8 *s_state = NULL; /* 0 unbesucht, 1 offen, 2 geschlossen */
static int *s_heap = NULL;
static float *s_f = NULL;
static int s_cap = 0;
static int s_heap_n = 0;

static void ensure_capacity(int n) {
    if (n <= s_cap) {
        return;
    }
    free(s_g);
    free(s_parent);
    free(s_state);
    free(s_heap);
    free(s_f);
    s_g = malloc(sizeof(float) * (size_t)n);
    s_parent = malloc(sizeof(int) * (size_t)n);
    s_state = malloc((size_t)n);
    s_heap = malloc(sizeof(int) * (size_t)n * 2);
    s_f = malloc(sizeof(float) * (size_t)n);
    s_cap = n;
}

static void heap_push(int node) {
    int i = s_heap_n++;
    s_heap[i] = node;
    while (i > 0) {
        int p = (i - 1) / 2;
        if (s_f[s_heap[p]] <= s_f[s_heap[i]]) {
            break;
        }
        int t = s_heap[p];
        s_heap[p] = s_heap[i];
        s_heap[i] = t;
        i = p;
    }
}

static int heap_pop(void) {
    int top = s_heap[0];
    s_heap[0] = s_heap[--s_heap_n];
    int i = 0;
    for (;;) {
        int l = i * 2 + 1, r = l + 1, s = i;
        if (l < s_heap_n && s_f[s_heap[l]] < s_f[s_heap[s]]) {
            s = l;
        }
        if (r < s_heap_n && s_f[s_heap[r]] < s_f[s_heap[s]]) {
            s = r;
        }
        if (s == i) {
            break;
        }
        int t = s_heap[s];
        s_heap[s] = s_heap[i];
        s_heap[i] = t;
        i = s;
    }
    return top;
}

static Vec2 tile_center(int tx, int ty) {
    return v2((float)tx * TILE_SIZE + TILE_SIZE * 0.5f, (float)ty * TILE_SIZE + TILE_SIZE * 0.5f);
}

static bool tile_ok(const GameMap *m, int tx, int ty, float r) {
    if (world_tile_solid(world_tile_at(m, tx, ty))) {
        return false;
    }
    /* Grosse Einheiten brauchen Abstand zu Waenden */
    return world_walkable(m, tile_center(tx, ty), r);
}

int nav_find_path(const GameMap *m, Vec2 from, Vec2 to, float r, Vec2 *out, int max_points) {
    if (r > 20.0f) {
        r = 20.0f;
    }
    int n = m->w * m->h;
    ensure_capacity(n);
    Vec2 f2 = world_nearest_walkable(m, from, r);
    Vec2 t2 = world_nearest_walkable(m, to, r);
    int sx = clampi((int)(f2.x / TILE_SIZE), 0, m->w - 1);
    int sy = clampi((int)(f2.y / TILE_SIZE), 0, m->h - 1);
    int gx = clampi((int)(t2.x / TILE_SIZE), 0, m->w - 1);
    int gy = clampi((int)(t2.y / TILE_SIZE), 0, m->h - 1);
    int start = sy * m->w + sx, goal = gy * m->w + gx;

    memset(s_state, 0, (size_t)n);
    s_heap_n = 0;
    s_g[start] = 0.0f;
    s_parent[start] = -1;
    s_f[start] = 0.0f;
    s_state[start] = 1;
    heap_push(start);
    bool found = false;
    static const int dx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int dy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    while (s_heap_n > 0) {
        int cur = heap_pop();
        if (s_state[cur] == 2) {
            continue;
        }
        s_state[cur] = 2;
        if (cur == goal) {
            found = true;
            break;
        }
        int cx = cur % m->w, cy = cur / m->w;
        for (int k = 0; k < 8; k++) {
            int nx = cx + dx[k], ny = cy + dy[k];
            if (nx < 0 || ny < 0 || nx >= m->w || ny >= m->h) {
                continue;
            }
            int ni = ny * m->w + nx;
            if (s_state[ni] == 2) {
                continue;
            }
            if (!tile_ok(m, nx, ny, r)) {
                continue;
            }
            float step = 1.0f;
            if (k >= 4) {
                if (!tile_ok(m, cx + dx[k], cy, r) || !tile_ok(m, cx, cy + dy[k], r)) {
                    continue;
                }
                step = 1.4142f;
            }
            if (m->tiles[ni] == TILE_SLOW) {
                step *= 1.5f;
            }
            float ng = s_g[cur] + step;
            if (s_state[ni] == 0 || ng < s_g[ni]) {
                s_g[ni] = ng;
                s_parent[ni] = cur;
                float ddx = (float)abs(nx - gx), ddy = (float)abs(ny - gy);
                float h = (ddx + ddy) + (1.4142f - 2.0f) * (ddx < ddy ? ddx : ddy);
                s_f[ni] = ng + h;
                s_state[ni] = 1;
                if (s_heap_n < n * 2 - 1) {
                    heap_push(ni);
                }
            }
        }
    }
    if (!found) {
        return 0;
    }
    /* Pfad rueckwaerts einsammeln */
    int count = 0;
    for (int c = goal; c != -1; c = s_parent[c]) {
        count++;
    }
    Vec2 *raw = malloc(sizeof(Vec2) * (size_t)(count + 1));
    if (!raw) {
        return 0;
    }
    int idx = count - 1;
    for (int c = goal; c != -1; c = s_parent[c]) {
        raw[idx--] = tile_center(c % m->w, c / m->w);
    }
    raw[0] = f2;
    raw[count - 1] = t2;
    /* Glaettung */
    int out_n = 0;
    int anchor = 0;
    while (anchor < count - 1 && out_n < max_points) {
        int far = anchor + 1;
        for (int j = count - 1; j > anchor + 1; j--) {
            if (world_line_clear(m, raw[anchor], raw[j], r)) {
                far = j;
                break;
            }
        }
        out[out_n++] = raw[far];
        anchor = far;
    }
    free(raw);
    return out_n;
}
