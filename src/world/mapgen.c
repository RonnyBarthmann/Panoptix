/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Kartenaufbau: Generator mit festem Seed (Raumkette + Nebenpfade + Boss-Arena)
 * und Datei-Override data/maps/<id>.map (ASCII-Raster, siehe docs/DATA_FORMAT.md).
 * Gleiche MapDef => gleiche Karte (nur core/rng, keine globalen Zufallsquellen). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/rng.h"
#include "core/util.h"
#include "world/world.h"

#define CORRIDOR_HALF 1       /* Korridorbreite = 2*half+1 = 3 Kacheln */
#define ROOM_MARGIN 2
#define MAX_CELLS 128
#define MAX_CHAIN 28
#define FILE_MAX_DIM 512

/* ======================================================================== */
/* Hilfen: Kacheln setzen                                                    */
/* ======================================================================== */
static void set_tile(GameMap *m, int x, int y, int t) {
    if (x >= 0 && y >= 0 && x < m->w && y < m->h) {
        m->tiles[y * m->w + x] = (u8)t;
    }
}

static void fill_rect(GameMap *m, int x, int y, int w, int h, int t) {
    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) {
            set_tile(m, xx, yy, t);
        }
    }
}

static Vec2 tile_to_world(int tx, int ty) {
    return v2((float)tx * TILE_SIZE + TILE_SIZE * 0.5f, (float)ty * TILE_SIZE + TILE_SIZE * 0.5f);
}

static bool alloc_map(GameMap *m, int w, int h) {
    m->w = w;
    m->h = h;
    m->tiles = malloc((size_t)w * (size_t)h);
    m->variant = malloc((size_t)w * (size_t)h);
    if (!m->tiles || !m->variant) {
        world_free(m);
        return false;
    }
    memset(m->tiles, TILE_WALL, (size_t)w * (size_t)h);
    memset(m->variant, 0, (size_t)w * (size_t)h);
    return true;
}

/* Zufallswerte fuer die Optik (eigener Strom, beeinflusst die Karte nicht) */
static void fill_variants(GameMap *m, u32 seed) {
    Rng r;
    rng_seed(&r, seed, 0x7A71);
    for (int i = 0; i < m->w * m->h; i++) {
        m->variant[i] = (u8)(rng_u32(&r) & 0xFF);
    }
}

/* ======================================================================== */
/* Erreichbarkeit (4er-Nachbarschaft, Kachel-Ebene)                          */
/* ======================================================================== */
/* Fuellt 'seen' ab Startkachel; gibt Anzahl erreichter Kacheln zurueck. */
static int flood_fill(const GameMap *m, int sx, int sy, u8 *seen, int *stack) {
    memset(seen, 0, (size_t)m->w * (size_t)m->h);
    int sp = 0, count = 0;
    stack[sp++] = sy * m->w + sx;
    seen[sy * m->w + sx] = 1;
    while (sp > 0) {
        int cur = stack[--sp];
        count++;
        int cx = cur % m->w, cy = cur / m->w;
        static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; k++) {
            int nx = cx + dx[k], ny = cy + dy[k];
            if (nx < 0 || ny < 0 || nx >= m->w || ny >= m->h) {
                continue;
            }
            int ni = ny * m->w + nx;
            if (seen[ni] || world_tile_solid(m->tiles[ni])) {
                continue;
            }
            seen[ni] = 1;
            stack[sp++] = ni;
        }
    }
    return count;
}

/* ======================================================================== */
/* Generator                                                                 */
/* ======================================================================== */
typedef struct Gen {
    GameMap *m;
    const MapDef *def;
    Rng rng;
    int cols, rows, cw, ch;
    int cell_room[MAX_CELLS];   /* Raumindex je Zelle oder -1 */
    int chain[MAX_CHAIN];       /* Zellen der Hauptkette */
    int chain_len;
    int room_cell[MAX_ROOMS];
    int room_parent[MAX_ROOMS]; /* Raum, mit dem verbunden wurde (-1 Start) */
    bool room_side[MAX_ROOMS];
} Gen;

static void cell_xy(const Gen *g, int cell, int *cx, int *cy) {
    *cx = cell % g->cols;
    *cy = cell / g->cols;
}

static bool dfs_chain(Gen *g, int cell, int depth, int want, bool *used) {
    g->chain[depth] = cell;
    used[cell] = true;
    if (depth + 1 == want) {
        return true;
    }
    int cx, cy;
    cell_xy(g, cell, &cx, &cy);
    int order[4] = {0, 1, 2, 3};
    for (int i = 3; i > 0; i--) { /* Mischen; Rechts bevorzugen (steigt am Ende nach vorn) */
        int j = rng_range(&g->rng, 0, i);
        int t = order[i];
        order[i] = order[j];
        order[j] = t;
    }
    static const int dx[4] = {1, 0, 0, -1}, dy[4] = {0, 1, -1, 0};
    for (int i = 0; i < 4; i++) {
        int nx = cx + dx[order[i]], ny = cy + dy[order[i]];
        if (nx < 0 || ny < 0 || nx >= g->cols || ny >= g->rows) {
            continue;
        }
        int nc = ny * g->cols + nx;
        if (used[nc]) {
            continue;
        }
        if (dfs_chain(g, nc, depth + 1, want, used)) {
            return true;
        }
    }
    used[cell] = false;
    return false;
}

static void plan_chain(Gen *g) {
    int cells = g->cols * g->rows;
    int want = clampi(cells / 2 + 1, 5, MAX_CHAIN);
    want = mini(want, cells);
    bool used[MAX_CELLS];
    for (int attempt = 0; attempt < 40; attempt++) {
        memset(used, 0, sizeof used);
        int start = rng_range(&g->rng, 0, g->rows - 1) * g->cols; /* linke Spalte */
        if (dfs_chain(g, start, 0, want, used)) {
            g->chain_len = want;
            return;
        }
        if (attempt == 20 && want > 4) {
            want--;
        }
    }
    /* Notfall: Zeile fuer Zeile */
    g->chain_len = mini(g->cols, MAX_CHAIN);
    for (int i = 0; i < g->chain_len; i++) {
        g->chain[i] = i;
    }
}

/* Legt einen Raum in einer Zelle an. 'full' = Zelle fast komplett (Boss-Arena). */
static int make_room(Gen *g, int cell, bool full) {
    GameMap *m = g->m;
    if (m->room_count >= MAX_ROOMS) {
        return -1;
    }
    int cx, cy;
    cell_xy(g, cell, &cx, &cy);
    int x0 = cx * g->cw, y0 = cy * g->ch;
    int maxw = g->cw - 2 * ROOM_MARGIN, maxh = g->ch - 2 * ROOM_MARGIN;
    int rw = full ? maxw : rng_range(&g->rng, maxi(8, maxw * 6 / 10), maxw);
    int rh = full ? maxh : rng_range(&g->rng, maxi(7, maxh * 6 / 10), maxh);
    int rx = x0 + ROOM_MARGIN + (maxw - rw > 0 ? rng_range(&g->rng, 0, maxw - rw) : 0);
    int ry = y0 + ROOM_MARGIN + (maxh - rh > 0 ? rng_range(&g->rng, 0, maxh - rh) : 0);
    fill_rect(m, rx, ry, rw, rh, TILE_FLOOR);
    int idx = m->room_count++;
    Room *r = &m->rooms[idx];
    memset(r, 0, sizeof *r);
    r->x = rx;
    r->y = ry;
    r->w = rw;
    r->h = rh;
    r->center = tile_to_world(rx + rw / 2, ry + rh / 2);
    g->room_cell[idx] = cell;
    g->cell_room[cell] = idx;
    return idx;
}

static void carve_line(GameMap *m, int x0, int y0, int x1, int y1) {
    int dx = x1 > x0 ? 1 : -1, dy = y1 > y0 ? 1 : -1;
    for (int x = x0; x != x1 + dx; x += dx) {
        fill_rect(m, x - CORRIDOR_HALF, y0 - CORRIDOR_HALF, 2 * CORRIDOR_HALF + 1, 2 * CORRIDOR_HALF + 1, TILE_FLOOR);
        if (x == x1) {
            break;
        }
    }
    for (int y = y0; y != y1 + dy; y += dy) {
        fill_rect(m, x1 - CORRIDOR_HALF, y - CORRIDOR_HALF, 2 * CORRIDOR_HALF + 1, 2 * CORRIDOR_HALF + 1, TILE_FLOOR);
        if (y == y1) {
            break;
        }
    }
}

static void room_center_tile(const Room *r, int *tx, int *ty) {
    *tx = r->x + r->w / 2;
    *ty = r->y + r->h / 2;
}

static void connect_rooms(Gen *g, int a, int b) {
    int ax, ay, bx, by;
    room_center_tile(&g->m->rooms[a], &ax, &ay);
    room_center_tile(&g->m->rooms[b], &bx, &by);
    carve_line(g->m, ax, ay, bx, by); /* waagerecht bei ay, dann senkrecht bei bx */
}

/* Nebenraeume: Sackgassen an freien Nachbarzellen der Kette (nicht Start/Arena). */
static void add_side_rooms(Gen *g) {
    static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    int budget = MAX_ROOMS - g->m->room_count;
    for (int pass = 0; pass < 2 && budget > 0; pass++) {
        int n = g->m->room_count;
        for (int r = pass == 0 ? 1 : 0; r < n && budget > 0; r++) {
            if (r == 0 || r == g->chain_len - 1 || (pass == 1 && !g->room_side[r])) {
                continue;
            }
            int cx, cy;
            cell_xy(g, g->room_cell[r], &cx, &cy);
            for (int k = 0; k < 4 && budget > 0; k++) {
                int nx = cx + dx[k], ny = cy + dy[k];
                if (nx < 0 || ny < 0 || nx >= g->cols || ny >= g->rows) {
                    continue;
                }
                int nc = ny * g->cols + nx;
                if (g->cell_room[nc] >= 0 || !rng_chance(&g->rng, pass == 0 ? 0.55 : 0.4)) {
                    continue;
                }
                int ri = make_room(g, nc, false);
                if (ri < 0) {
                    return;
                }
                g->room_side[ri] = true;
                g->room_parent[ri] = r;
                connect_rooms(g, r, ri);
                budget--;
            }
        }
    }
}

/* ---- Hindernisse und Bodenarten -------------------------------------------- */
static bool all_reachable(Gen *g, u8 *seen, int *stack) {
    GameMap *m = g->m;
    int sx = (int)(m->start.x / TILE_SIZE), sy = (int)(m->start.y / TILE_SIZE);
    flood_fill(m, sx, sy, seen, stack);
    for (int i = 0; i < m->room_count; i++) {
        int tx, ty;
        room_center_tile(&m->rooms[i], &tx, &ty);
        if (!seen[ty * m->w + tx]) {
            return false;
        }
    }
    return true;
}

static void add_obstacles(Gen *g) {
    GameMap *m = g->m;
    size_t n = (size_t)m->w * (size_t)m->h;
    u8 *seen = malloc(n);
    int *stack = malloc(sizeof(int) * n);
    if (!seen || !stack) {
        free(seen);
        free(stack);
        return;
    }
    for (int r = 1; r < m->room_count; r++) {
        Room *rm = &m->rooms[r];
        if (rm->is_boss || rm->w * rm->h < 80) {
            continue;
        }
        int tries = rng_range(&g->rng, 0, 3);
        for (int t = 0; t < tries; t++) {
            int ow = rng_range(&g->rng, 1, 3), oh = rng_range(&g->rng, 1, 3);
            int ox = rm->x + 3 + rng_range(&g->rng, 0, maxi(rm->w - 6 - ow, 0));
            int oy = rm->y + 3 + rng_range(&g->rng, 0, maxi(rm->h - 6 - oh, 0));
            int ctx, cty;
            room_center_tile(rm, &ctx, &cty);
            if (abs(ox + ow / 2 - ctx) <= 3 || abs(oy + oh / 2 - cty) <= 3) {
                continue; /* Kreuz durch den Raum frei lassen */
            }
            int kind = rng_chance(&g->rng, 0.35) ? TILE_WATER : TILE_WALL;
            u8 backup[9];
            int bn = 0;
            for (int yy = oy; yy < oy + oh && bn < 9; yy++) {
                for (int xx = ox; xx < ox + ow && bn < 9; xx++) {
                    backup[bn++] = m->tiles[yy * m->w + xx];
                }
            }
            fill_rect(m, ox, oy, ow, oh, kind);
            if (!all_reachable(g, seen, stack)) {
                bn = 0;
                for (int yy = oy; yy < oy + oh && bn < 9; yy++) {
                    for (int xx = ox; xx < ox + ow && bn < 9; xx++) {
                        m->tiles[yy * m->w + xx] = backup[bn++];
                    }
                }
            }
        }
    }
    free(seen);
    free(stack);
}

static void add_slow_and_heal(Gen *g) {
    GameMap *m = g->m;
    for (int r = 1; r < m->room_count; r++) {
        Room *rm = &m->rooms[r];
        if (rm->is_boss || rm->w < 9 || rm->h < 8) {
            continue;
        }
        if (rng_chance(&g->rng, 0.2)) { /* Daempfungsboden-Fleck */
            fill_rect(m, rm->x + 2, rm->y + 2, 4, 3, TILE_SLOW);
        }
        bool main_room = !g->room_side[r];
        double p = main_room ? 0.34 : 0.2;
        if (m->heal_count < 8 && rng_chance(&g->rng, p)) {
            int hx = rm->x + rm->w - 4, hy = rm->y + rm->h - 4;
            fill_rect(m, hx, hy, 2, 2, TILE_HEAL);
            m->heal_springs[m->heal_count++] = v2((float)(hx + 1) * TILE_SIZE, (float)(hy + 1) * TILE_SIZE);
        }
    }
}

/* ---- Wegpunkte, Packpunkte -------------------------------------------------- */
static bool tile_is_floor(const GameMap *m, int tx, int ty) {
    int t = world_tile_at(m, tx, ty);
    return t == TILE_FLOOR || t == TILE_SLOW || t == TILE_ARENA;
}

static void add_waypoint(GameMap *m, Vec2 pos, bool side) {
    if (m->wp_count < MAX_WAYPOINTS) {
        m->waypoints[m->wp_count].pos = pos;
        m->waypoints[m->wp_count].side_path = side;
        m->wp_count++;
    }
}

static void add_pack(GameMap *m, Vec2 pos, int room, bool side) {
    if (m->pack_count < MAX_PACK_POINTS) {
        PackPoint *p = &m->packs[m->pack_count++];
        p->pos = pos;
        p->room = room;
        p->side_path = side;
    }
}

static bool pack_too_close(const GameMap *m, Vec2 p) {
    for (int i = 0; i < m->pack_count; i++) {
        if (v2_dist2(m->packs[i].pos, p) < 220.0f * 220.0f) {
            return true;
        }
    }
    return false;
}

static void place_packs_in_room(Gen *g, int ri, int count) {
    GameMap *m = g->m;
    const Room *rm = &m->rooms[ri];
    for (int i = 0; i < count; i++) {
        for (int t = 0; t < 30; t++) {
            int tx = rm->x + 2 + rng_range(&g->rng, 0, maxi(rm->w - 5, 0));
            int ty = rm->y + 2 + rng_range(&g->rng, 0, maxi(rm->h - 5, 0));
            Vec2 p = tile_to_world(tx, ty);
            if (!tile_is_floor(m, tx, ty) || !world_walkable(m, p, 40.0f) ||
                v2_dist2(p, m->start) < 350.0f * 350.0f || (t < 25 && pack_too_close(m, p))) {
                continue;
            }
            add_pack(m, p, ri, g->room_side[ri]);
            break;
        }
    }
}

static void plan_waypoints_and_packs(Gen *g) {
    GameMap *m = g->m;
    bool boss = m->has_boss_arena;
    int last = g->chain_len - 1;
    int first_wp = 1;
    int end_wp = boss ? last - 1 : last;
    for (int r = first_wp; r <= end_wp; r++) {
        add_waypoint(m, m->rooms[r].center, false);
    }
    m->main_wp_count = m->wp_count;
    for (int r = g->chain_len; r < m->room_count; r++) { /* Nebenraeume (in Anlegereihenfolge) */
        add_waypoint(m, m->rooms[r].center, true);
    }
    double density = (double)maxf(g->def->density, 0.1f);
    for (int r = 1; r < m->room_count; r++) {
        if (m->rooms[r].is_boss) {
            continue;
        }
        double want = (double)(m->rooms[r].w * m->rooms[r].h) / 120.0 * density;
        int count = (int)want;
        if (rng_chance(&g->rng, want - (double)count)) {
            count++;
        }
        place_packs_in_room(g, r, maxi(count, 1));
    }
}

/* ---- Hauptablauf ------------------------------------------------------------- */
static bool generate(const MapDef *def, GameMap *m) {
    Gen *g = calloc(1, sizeof *g);
    if (!g) {
        return false;
    }
    g->m = m;
    g->def = def;
    rng_seed(&g->rng, def->seed, 0xA11CE);
    g->cols = clampi(def->w / 20, 3, 10);
    g->rows = clampi(def->h / 16, 2, 7);
    g->cw = def->w / g->cols;
    g->ch = def->h / g->rows;
    for (int i = 0; i < MAX_CELLS; i++) {
        g->cell_room[i] = -1;
    }
    memset(g->room_parent, -1, sizeof g->room_parent);
    plan_chain(g);
    bool boss = def->boss >= 0;
    for (int i = 0; i < g->chain_len; i++) {
        bool arena = boss && i == g->chain_len - 1;
        int ri = make_room(g, g->chain[i], arena);
        if (ri < 0) {
            free(g);
            return false;
        }
        g->room_parent[ri] = i > 0 ? ri - 1 : -1;
        if (i > 0) {
            connect_rooms(g, ri - 1, ri);
        }
    }
    Room *start = &m->rooms[0], *last = &m->rooms[g->chain_len - 1];
    for (int i = 0; i < g->chain_len; i++) {
        m->rooms[i].on_main_path = true;
    }
    start->is_start = true;
    m->start = start->center;
    m->has_boss_arena = boss;
    last->is_boss = boss;
    if (boss) {
        m->arena_center = last->center;
        m->arena = rect_make(last->x * TILE_SIZE, last->y * TILE_SIZE, last->w * TILE_SIZE, last->h * TILE_SIZE);
        fill_rect(m, last->x, last->y, last->w, last->h, TILE_ARENA);
        m->exit = v2(last->center.x + (float)(last->w / 2 - 2) * TILE_SIZE, last->center.y);
    } else {
        m->exit = v2(last->center.x + (float)(last->w / 2 - 2) * TILE_SIZE, last->center.y);
    }
    add_side_rooms(g);
    add_obstacles(g);
    add_slow_and_heal(g);
    plan_waypoints_and_packs(g);
    free(g);
    return true;
}

/* ======================================================================== */
/* Datei-Override                                                            */
/* ======================================================================== */
static char *read_whole_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 4 * 1024 * 1024) {
        fclose(f);
        return NULL;
    }
    char *buf = malloc((size_t)sz + 1);
    if (buf && fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf) {
        buf[sz] = 0;
        *len = (size_t)sz;
    }
    return buf;
}

typedef struct FileWp {
    int order;   /* 0..8 Hauptpfad, 100.. Nebenpfad */
    Vec2 pos;
} FileWp;

static int cmp_filewp(const void *a, const void *b) {
    return ((const FileWp *)a)->order - ((const FileWp *)b)->order;
}

static int map_char_tile(char c) {
    switch (c) {
    case '#': return TILE_WALL;
    case '~': return TILE_WATER;
    case ':': return TILE_SLOW;
    case 'h': return TILE_HEAL;
    default: return TILE_FLOOR;
    }
}

/* Findet Zeilen; liefert Anzahl und Zeilenanfaenge (Text wird in-place terminiert). */
static int split_lines(char *text, char **lines, int max_lines, int *max_len) {
    int n = 0;
    *max_len = 0;
    char *p = text;
    while (*p && n < max_lines) {
        char *start = p;
        while (*p && *p != '\n') {
            p++;
        }
        char *end = p;
        if (*p) {
            *p++ = 0;
        }
        if (end > start && end[-1] == '\r') {
            end[-1] = 0;
            end--;
        }
        lines[n++] = start;
        *max_len = maxi(*max_len, (int)(end - start));
    }
    while (n > 0 && lines[n - 1][0] == 0) {
        n--;
    }
    return n;
}

static void file_mark_arena(GameMap *m, int bx, int by) {
    int x0 = clampi(bx - 6, 0, m->w - 1), x1 = clampi(bx + 6, 0, m->w - 1);
    int y0 = clampi(by - 5, 0, m->h - 1), y1 = clampi(by + 5, 0, m->h - 1);
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (m->tiles[y * m->w + x] == TILE_FLOOR) {
                m->tiles[y * m->w + x] = TILE_ARENA;
            }
        }
    }
    m->arena = rect_make(x0 * TILE_SIZE, y0 * TILE_SIZE, (x1 - x0 + 1) * TILE_SIZE, (y1 - y0 + 1) * TILE_SIZE);
}

static void file_finish_waypoints(GameMap *m, FileWp *wps, int n) {
    qsort(wps, (size_t)n, sizeof wps[0], cmp_filewp);
    for (int i = 0; i < n; i++) {
        bool side = wps[i].order >= 100;
        add_waypoint(m, wps[i].pos, side);
        if (!side) {
            m->main_wp_count++;
        }
    }
}

static bool parse_map_text(char *text, GameMap *m) {
    char *lines[FILE_MAX_DIM];
    int max_len;
    int h = split_lines(text, lines, FILE_MAX_DIM, &max_len);
    if (h < 3 || max_len < 3 || !alloc_map(m, max_len, h)) {
        return false;
    }
    FileWp wps[MAX_WAYPOINTS];
    int wn = 0;
    bool has_start = false, has_exit = false;
    int bx = -1, by = -1;
    for (int y = 0; y < h; y++) {
        int len = (int)strlen(lines[y]);
        for (int x = 0; x < len; x++) {
            char c = lines[y][x];
            int tile = map_char_tile(c);
            m->tiles[y * m->w + x] = (u8)tile;
            Vec2 pos = tile_to_world(x, y);
            if (c == 'S') {
                m->start = pos;
                has_start = true;
            } else if (c == 'X') {
                m->exit = pos;
                has_exit = true;
            } else if (c == 'B') {
                bx = x;
                by = y;
            } else if (c == 'M') {
                add_pack(m, pos, -1, false);
            } else if (c == 'h' && m->heal_count < 8) {
                m->heal_springs[m->heal_count++] = pos;
            } else if (((c >= '1' && c <= '9') || (c >= 'a' && c <= 'f')) && wn < MAX_WAYPOINTS) {
                wps[wn].order = c <= '9' ? c - '1' : 100 + (c - 'a');
                wps[wn].pos = pos;
                wn++;
            }
        }
    }
    if (!has_start) {
        return false;
    }
    file_finish_waypoints(m, wps, wn);
    if (bx >= 0) {
        m->has_boss_arena = true;
        m->arena_center = tile_to_world(bx, by);
        file_mark_arena(m, bx, by);
    }
    if (!has_exit) {
        m->exit = m->has_boss_arena ? m->arena_center : (wn > 0 ? m->waypoints[m->wp_count - 1].pos : m->start);
    }
    for (int i = 0; i < m->pack_count; i++) { /* Packs naeher an Nebenpfad-Wegpunkten = Nebenpfad */
        float best_main = 1e9f, best_side = 1e9f;
        for (int k = 0; k < m->wp_count; k++) {
            float d = v2_dist(m->packs[i].pos, m->waypoints[k].pos);
            if (m->waypoints[k].side_path) {
                best_side = minf(best_side, d);
            } else {
                best_main = minf(best_main, d);
            }
        }
        m->packs[i].side_path = best_side < best_main * 0.6f;
    }
    return true;
}

static bool load_map_file(const MapDef *def, GameMap *m) {
    const char *dir = data_dir();
    if (!dir || !*dir) {
        return false;
    }
    char path[600];
    str_format(path, sizeof path, "%s/maps/%s.map", dir, def->id);
    size_t len = 0;
    char *text = read_whole_file(path, &len);
    if (!text) {
        return false;
    }
    bool ok = parse_map_text(text, m);
    free(text);
    if (!ok) {
        log_warn("Kartendatei %s ungueltig, nutze Generator", path);
        world_free(m);
        memset(m, 0, sizeof *m);
    }
    return ok;
}

/* ======================================================================== */
/* API                                                                       */
/* ======================================================================== */
bool world_build(const MapDef *def, int map_idx, GameMap *out) {
    memset(out, 0, sizeof *out);
    out->map_idx = map_idx;
    if (!def || def->w < 24 || def->h < 24) {
        return false;
    }
    if (load_map_file(def, out)) {
        out->map_idx = map_idx;
        out->from_file = true;
        fill_variants(out, def->seed);
        return true;
    }
    memset(out, 0, sizeof *out);
    out->map_idx = map_idx;
    if (!alloc_map(out, def->w, def->h)) {
        return false;
    }
    if (!generate(def, out)) {
        world_free(out);
        return false;
    }
    fill_variants(out, def->seed);
    return true;
}
