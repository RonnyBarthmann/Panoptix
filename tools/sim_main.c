/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Headless Simulation: panoptix_sim --class N --map ID --minutes M --seed S [--level L] [--heroes K] [--keep]
 * Legt Held(en) an, weist sie einer Map zu und simuliert M Spielminuten ohne SDL.
 * Druckt Kills, Tode, Kisten, Level und Clear-Zeit. Endet vorzeitig beim Map-Abschluss (ausser --keep). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/util.h"
#include "data/data.h"
#include "game/game.h"
#include "platform/platform.h"
#include "sim/sim.h"

typedef struct Options {
    int cls;
    char map[32];
    double minutes;
    u64 seed;
    int level;
    int heroes;
    bool keep;
    int trace;   /* Sekunden-Intervall fuer Zustandsausgabe (0 = aus) */
} Options;

static void usage(void) {
    printf("Aufruf: panoptix_sim --class N --map ID --minutes M --seed S [--level L] [--heroes K] [--keep] [--trace S]\n");
}

static bool parse_args(int argc, char **argv, Options *o) {
    o->cls = 0;
    str_copy(o->map, sizeof o->map, "1-1");
    o->minutes = 10.0;
    o->seed = 1;
    o->level = 1;
    o->heroes = 1;
    o->keep = false;
    o->trace = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        bool has = i + 1 < argc;
        if (strcmp(a, "--class") == 0 && has) {
            o->cls = atoi(argv[++i]);
        } else if (strcmp(a, "--map") == 0 && has) {
            str_copy(o->map, sizeof o->map, argv[++i]);
        } else if (strcmp(a, "--minutes") == 0 && has) {
            o->minutes = atof(argv[++i]);
        } else if (strcmp(a, "--seed") == 0 && has) {
            o->seed = (u64)strtoull(argv[++i], NULL, 10);
        } else if (strcmp(a, "--level") == 0 && has) {
            o->level = atoi(argv[++i]);
        } else if (strcmp(a, "--heroes") == 0 && has) {
            o->heroes = atoi(argv[++i]);
        } else if (strcmp(a, "--trace") == 0 && has) {
            o->trace = atoi(argv[++i]);
        } else if (strcmp(a, "--keep") == 0) {
            o->keep = true;
        } else {
            return false;
        }
    }
    return true;
}

/* Karte freischalten, ohne Spielfortschritt zu simulieren. */
static void unlock_for_sim(Game *g, int map_idx) {
    g->player.level = 99;
    for (int i = 0; i < map_idx; i++) {
        g->world.map[i].cleared = 1;
    }
    g->world.temple_tickets = 99;
    g->world.ruins_tickets = 99;
}

static int chest_total(const Game *g, int hero) {
    int n = 0;
    for (int t = 0; t < TIER_COUNT; t++) {
        n += g->heroes[hero].chest_bar[t];
    }
    return n;
}

int main(int argc, char **argv) {
    Options o;
    if (!parse_args(argc, argv, &o)) {
        usage();
        return 2;
    }
    char dir[512];
    if (!platform_get_data_dir(dir, sizeof dir) || !data_load_all(dir)) {
        printf("FEHLER: Daten nicht ladbar (PANOPTIX_DATA setzen)\n");
        return 1;
    }
    int map = data_find_map(o.map);
    if (map < 0) {
        map = atoi(o.map);
    }
    if (map < 0 || map >= g_map_count || o.cls < 0 || o.cls >= CLASS_COUNT) {
        printf("FEHLER: Map oder Klasse ungueltig\n");
        return 2;
    }
    Game *g = (Game *)calloc(1, sizeof *g);
    if (!g) {
        return 1;
    }
    game_init_new(g, o.seed);
    unlock_for_sim(g, map);
    g->speed_mult = 1.0f;
    int n = clampi(o.heroes, 1, g_maps[map].max_heroes);
    int idx[MAX_TEAM];
    for (int i = 0; i < n; i++) {
        idx[i] = game_create_hero(g, NULL, o.cls, (u8)i, true);
        if (idx[i] < 0) {
            printf("FEHLER: Held nicht anlegbar\n");
            return 1;
        }
        g->heroes[idx[i]].level = maxi(o.level, 1);
        g->heroes[idx[i]].stats_dirty = true;
        if (!game_assign_hero(g, idx[i], map, i)) {
            printf("FEHLER: Zuweisung an Map %s fehlgeschlagen\n", g_maps[map].id);
            return 1;
        }
    }
    long max_ticks = (long)(o.minutes * 60.0 / (double)SIM_DT);
    long ticks = 0;
    bool cleared = false;
    float clear_time = 0.0f;
    for (; ticks < max_ticks; ticks++) {
        game_update(g, (double)SIM_DT);
        Run *r = g->hero_run[idx[0]];
        if (r && o.trace > 0 && ticks % (long)(o.trace * 30) == 0) {
            const RunHero *h = &r->heroes[0];
            if (ticks == 0) printf("[atk %.1f spell %.1f hp %.0f armor %.1f aspd %.2f]\n", (double)h->eff.attack_power, (double)h->eff.spell_power, (double)h->u.max_hp, (double)h->eff.armor, (double)h->eff.attack_speed);
            printf("t=%6.1f %-10s hp %5.0f/%5.0f pos %4.0f,%4.0f feinde %3d kills %d wp %d/%d\n", (double)r->time,
                   hero_state_name(h->state), (double)h->u.hp, (double)h->u.max_hp, (double)h->u.pos.x,
                   (double)h->u.pos.y, run_enemies_alive(r), r->kills, h->wp_pos, h->wp_count);
        }
        if (r) {
            run_clear_events(r);
        }
        if (g->world.map[map].clear_count > 0 && !cleared) {
            cleared = true;
            clear_time = g->world.map[map].best_time;
            if (!o.keep) {
                break;
            }
        }
    }
    printf("Map %s (%s), Klasse %d, Seed %llu, %d Held(en)\n", g_maps[map].id, g_maps[map].name, o.cls,
           (unsigned long long)o.seed, n);
    printf("Simulierte Zeit: %.1f s\n", (double)ticks * (double)SIM_DT);
    printf("Kills: %llu  Tode: %llu  Maps abgeschlossen: %llu\n",
           (unsigned long long)g->player.counters[COUNTER_KILLS], (unsigned long long)g->player.counters[COUNTER_DEATHS],
           (unsigned long long)g->player.counters[COUNTER_MAPS_CLEARED]);
    for (int i = 0; i < n; i++) {
        const Hero *h = &g->heroes[idx[i]];
        printf("  %s: Level %d, Kisten %d, Kills %u\n", h->name, h->level, chest_total(g, idx[i]), (unsigned)h->kills);
    }
    if (cleared) {
        printf("Clear-Zeit: %.1f s\n", (double)clear_time);
    } else {
        printf("Map NICHT abgeschlossen\n");
    }
    game_shutdown(g);
    free(g);
    return cleared ? 0 : 3;
}
