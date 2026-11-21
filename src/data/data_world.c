/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Lader fuer monsters.tsv und maps.tsv. */
#include <string.h>

#include "core/util.h"
#include "data/data_internal.h"

static const char *const k_archetypes[ARCH_COUNT] = {
    "melee", "ranged", "swarm", "brute",  "hunter",   "summoner", "healer",
    "explosive", "elemental", "guardian", "totem", "demon", "boss"};
static const char *const k_abilities[] = {"nova", "slam", "cone", "line", "summon", "pools",
                                          "charge", "heal_ally", "shield", "enrage", "rain"};
static const char *const k_map_kinds[] = {"story", "temple", "ruins"};

static void load_monsters(const char *dir) {
    Tsv t;
    if (!data_open_tsv(&t, dir, "monsters.tsv", true)) {
        return;
    }
    for (int r = 0; r < t.row_count; r++) {
        if (g_monster_count >= MAX_MONSTERS) {
            data_error("monsters.tsv", r, "zu viele Monster (max %d)", MAX_MONSTERS);
            break;
        }
        MonsterDef *m = &g_monsters[g_monster_count];
        memset(m, 0, sizeof *m);
        str_copy(m->id, sizeof m->id, tsv_get_by_name(&t, r, "id"));
        if (!*m->id) {
            data_error("monsters.tsv", r, "id fehlt");
            continue;
        }
        if (data_find_monster(m->id) >= 0) {
            data_error("monsters.tsv", r, "doppelte id '%s'", m->id);
            continue;
        }
        str_copy(m->name, sizeof m->name, tsv_get_by_name(&t, r, "name"));
        m->arch = (Archetype)data_parse_enum(&t, r, "arch", k_archetypes, ARCH_COUNT, ARCH_MELEE);
        m->act_min = tsv_get_int(&t, r, "act_min", 1);
        m->act_max = tsv_get_int(&t, r, "act_max", 11);
        m->hp_f = tsv_get_float(&t, r, "hp_f", 1.0f);
        m->dmg_f = tsv_get_float(&t, r, "dmg_f", 1.0f);
        m->speed = tsv_get_float(&t, r, "speed", 120.0f);
        m->range = tsv_get_float(&t, r, "range", 50.0f);
        m->radius = tsv_get_float(&t, r, "radius", 20.0f);
        m->attack_cd = tsv_get_float(&t, r, "attack_cd", 1.5f);
        m->proj_speed = tsv_get_float(&t, r, "proj_speed", 0.0f);
        m->color = data_parse_color(tsv_get_by_name(&t, r, "color"), 0x808080);
        m->shape = tsv_get_int(&t, r, "shape", 0);
        m->abilities = data_parse_flags(&t, r, "abilities", k_abilities, 11);
        m->is_boss = data_parse_bool(tsv_get_by_name(&t, r, "boss"));
        m->is_elite_only = data_parse_bool(tsv_get_by_name(&t, r, "elite_only"));
        m->weight = tsv_get_float(&t, r, "weight", 1.0f);
        if (m->act_min > m->act_max || m->act_min < 0 || m->act_max > 12) {
            data_error("monsters.tsv", r, "ungueltiger Akt-Bereich %d..%d", m->act_min, m->act_max);
        }
        if (m->hp_f <= 0.0f || m->radius <= 0.0f) {
            data_error("monsters.tsv", r, "hp_f/radius muessen > 0 sein");
        }
        g_monster_count++;
    }
    tsv_free(&t);
}

static void load_maps(const char *dir) {
    Tsv t;
    if (!data_open_tsv(&t, dir, "maps.tsv", true)) {
        return;
    }
    for (int r = 0; r < t.row_count; r++) {
        if (g_map_count >= MAX_MAPS) {
            data_error("maps.tsv", r, "zu viele Maps (max %d)", MAX_MAPS);
            break;
        }
        MapDef *m = &g_maps[g_map_count];
        memset(m, 0, sizeof *m);
        str_copy(m->id, sizeof m->id, tsv_get_by_name(&t, r, "id"));
        if (!*m->id) {
            data_error("maps.tsv", r, "id fehlt");
            continue;
        }
        if (data_find_map(m->id) >= 0) {
            data_error("maps.tsv", r, "doppelte id '%s'", m->id);
            continue;
        }
        str_copy(m->name, sizeof m->name, tsv_get_by_name(&t, r, "name"));
        m->kind = (MapKind)data_parse_enum(&t, r, "kind", k_map_kinds, 3, MAPK_STORY);
        m->act = tsv_get_int(&t, r, "act", 0);
        m->index = tsv_get_int(&t, r, "index", 1);
        m->chapter = tsv_get_int(&t, r, "chapter", 0);
        m->level = tsv_get_int(&t, r, "level", 1);
        m->w = tsv_get_int(&t, r, "w", 60);
        m->h = tsv_get_int(&t, r, "h", 40);
        m->seed = (u32)tsv_get_int(&t, r, "seed", 1);
        const char *boss = tsv_get_by_name(&t, r, "boss");
        m->boss = -1;
        if (*boss) {
            m->boss = data_find_monster(boss);
            if (m->boss < 0) {
                data_error("maps.tsv", r, "Boss '%s' nicht in monsters.tsv", boss);
            }
        }
        m->boss_map = data_parse_bool(tsv_get_by_name(&t, r, "boss_map"));
        m->max_heroes = clampi(tsv_get_int(&t, r, "max_heroes", 1), 1, MAX_TEAM);
        m->min_player_level = tsv_get_int(&t, r, "min_player_level", 1);
        m->density = tsv_get_float(&t, r, "density", 1.0f);
        const char *stone = tsv_get_by_name(&t, r, "stone");
        m->stone_item = -1;
        if (*stone) {
            m->stone_item = data_find_item(stone);
            if (m->stone_item < 0) {
                data_error("maps.tsv", r, "Stein '%s' nicht in items.tsv", stone);
            }
        }
        m->stone_count = tsv_get_int(&t, r, "stone_count", 0);
        m->tint = data_parse_color(tsv_get_by_name(&t, r, "tint"), 0x808080);
        if (m->w < 20 || m->h < 20 || m->w > 400 || m->h > 300) {
            data_error("maps.tsv", r, "Kartengroesse %dx%d ausserhalb 20..400 x 20..300", m->w, m->h);
        }
        g_map_count++;
    }
    tsv_free(&t);
}

bool data_load_world(const char *dir) {
    load_monsters(dir);
    load_maps(dir);
    return true;
}
