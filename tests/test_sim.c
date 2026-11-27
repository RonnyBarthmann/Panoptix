/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Tests fuer world/ und sim/: Determinismus, Pfad Start -> Boss aller Maps, Leash. */
#include <stdlib.h>
#include <string.h>

#include "core/util.h"
#include "data/data.h"
#include "game/game.h"
#include "sim/sim.h"
#include "testing.h"
#include "world/world.h"

#define TEST_HERO_RADIUS 14.0f

/* Neues Spiel mit einem Helden der Klasse 'cls' auf Map 'map'; Run existiert nach game_update. */
static Game *make_game(u64 seed, int cls, int map, int *hero_out) {
    Game *g = (Game *)calloc(1, sizeof *g);
    if (!g) {
        return NULL;
    }
    game_init_new(g, seed);
    g->speed_mult = 1.0f;
    int h = game_create_hero(g, NULL, cls, 0, true);
    if (h >= 0) {
        game_assign_hero(g, h, map, 0);
    }
    *hero_out = h;
    return g;
}

static void free_game(Game *g) {
    game_shutdown(g);
    free(g);
}

static void run_ticks(Game *g, int ticks) {
    for (int i = 0; i < ticks; i++) {
        game_update(g, (double)SIM_DT);
        for (int r = 0; r < g->run_count; r++) {
            run_clear_events(g->runs[r]);
        }
    }
}

TEST(sim_determinism) {
    int h1, h2;
    Game *a = make_game(7, CLASS_KNIGHT, 0, &h1);
    Game *b = make_game(7, CLASS_KNIGHT, 0, &h2);
    CHECK(a && b && h1 >= 0 && h1 == h2);
    if (!a || !b) {
        free(a);
        free(b);
        return;
    }
    run_ticks(a, 900);
    run_ticks(b, 900);
    const Run *ra = a->hero_run[h1];
    const Run *rb = b->hero_run[h2];
    CHECK(ra && rb);
    if (ra && rb) {
        CHECK_EQ_INT(ra->kills, rb->kills);
        CHECK_EQ_INT(ra->tick, rb->tick);
        CHECK_EQ_INT(ra->total_spawned, rb->total_spawned);
        CHECK_EQ_INT(ra->rng.state == rb->rng.state, 1);
        CHECK_NEAR(ra->heroes[0].u.pos.x, rb->heroes[0].u.pos.x, 0.0001);
        CHECK_NEAR(ra->heroes[0].u.pos.y, rb->heroes[0].u.pos.y, 0.0001);
        CHECK_NEAR(ra->heroes[0].u.hp, rb->heroes[0].u.hp, 0.0001);
        CHECK_EQ_INT(g_maps[0].seed == ra->def->seed, 1);
    }
    CHECK_EQ_INT(a->player.counters[COUNTER_KILLS], b->player.counters[COUNTER_KILLS]);
    free_game(a);
    free_game(b);
}

/* Pfad vom Start ueber alle Hauptpfad-Wegpunkte bis zur Arena bzw. zum Ausgang. */
static bool path_exists(const GameMap *m, Vec2 from, Vec2 to) {
    Vec2 pts[PATH_MAX_POINTS];
    if (v2_dist(from, to) < 8.0f) {
        return true;
    }
    return nav_find_path(m, from, to, TEST_HERO_RADIUS, pts, PATH_MAX_POINTS) > 0;
}

TEST(sim_path_start_to_boss_all_maps) {
    int bad = 0;
    for (int i = 0; i < g_map_count; i++) {
        GameMap m;
        if (!world_build(&g_maps[i], i, &m)) {
            printf("  Map %s nicht baubar\n", g_maps[i].id);
            bad++;
            continue;
        }
        Vec2 cur = world_nearest_walkable(&m, m.start, TEST_HERO_RADIUS);
        bool ok = true;
        for (int w = 0; w < m.main_wp_count && ok; w++) {
            ok = path_exists(&m, cur, m.waypoints[w].pos);
            cur = m.waypoints[w].pos;
        }
        Vec2 goal = (g_maps[i].boss >= 0 && m.has_boss_arena) ? m.arena_center : m.exit;
        ok = ok && path_exists(&m, cur, world_nearest_walkable(&m, goal, TEST_HERO_RADIUS));
        if (!ok) {
            printf("  Kein Pfad Start->%s auf Map %s\n", g_maps[i].boss >= 0 ? "Boss" : "Ausgang", g_maps[i].id);
            bad++;
        }
        world_free(&m);
    }
    CHECK_EQ_INT(bad, 0);
}

/* Gegner jenseits der Leash-Distanz laufen zurueck zum Spawn. */
TEST(sim_leash_returns_to_spawn) {
    int h;
    Game *g = make_game(3, CLASS_KNIGHT, 0, &h);
    CHECK(g && h >= 0);
    if (!g) {
        return;
    }
    game_update(g, (double)SIM_DT);
    Run *r = g->hero_run[h];
    CHECK(r != NULL);
    if (r && r->enemy_count > 0) {
        int ei = r->enemy_count - 1; /* letzter Gegner, weit vom Start */
        Enemy *e = &r->enemies[ei];
        Vec2 away = world_nearest_walkable(&r->map, v2_add(e->spawn, v2(500.0f, 0.0f)), 16.0f);
        e->u.pos = away;
        e->state = ES_LEASH;
        e->u.hp = e->u.max_hp * 0.5f;
        CHECK(v2_dist(away, e->spawn) > 100.0f);
        for (int t = 0; t < 10 * 30 && r->enemies[ei].state == ES_LEASH; t++) {
            run_tick(r, g);
        }
        e = &r->enemies[ei];
        CHECK(e->state != ES_LEASH);
        if (e->u.alive) { /* schwache Monster koennen vom Helden schon erschlagen sein */
            CHECK(v2_dist(e->u.pos, e->spawn) < 40.0f);
            CHECK_NEAR(e->u.hp, e->u.max_hp, 0.01);
        }
    }
    free_game(g);
}

TEST(sim_knight_clears_first_map) {
    int h;
    Game *g = make_game(1, CLASS_KNIGHT, 0, &h);
    CHECK(g && h >= 0);
    if (!g) {
        return;
    }
    for (int i = 0; i < 5 * 60 * 30 && g->world.map[0].clear_count == 0; i++) {
        game_update(g, (double)SIM_DT);
        for (int r = 0; r < g->run_count; r++) {
            run_clear_events(g->runs[r]);
        }
    }
    CHECK(g->world.map[0].clear_count > 0);
    CHECK_EQ_INT(g->player.counters[COUNTER_DEATHS], 0);
    free_game(g);
}

void run_tests_sim(void) {
    test_sim_determinism();
    test_sim_path_start_to_boss_all_maps();
    test_sim_leash_returns_to_spawn();
    test_sim_knight_clears_first_map();
}
