/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "core/rng.h"
#include "data/data.h"
#include "testing.h"

TEST(rng_determinism) {
    Rng a, b;
    rng_seed(&a, 42, 1);
    rng_seed(&b, 42, 1);
    for (int i = 0; i < 100; i++) {
        CHECK_EQ_INT(rng_u32(&a), rng_u32(&b));
    }
}

TEST(item_counts) {
    int n = 0;
    int ids[MAX_ITEM_DEFS];
    n = data_query_items(IK_WEAPON, -1, SLOT_NONE, -2, ids, MAX_ITEM_DEFS);
    CHECK_EQ_INT(n, 147);
    n = data_query_items(IK_SUB, -1, SLOT_NONE, -2, ids, MAX_ITEM_DEFS);
    CHECK_EQ_INT(n, 42);
    n = data_query_items(IK_ARMOR, -1, SLOT_NONE, -2, ids, MAX_ITEM_DEFS);
    CHECK_EQ_INT(n, 84);
    n = data_query_items(IK_POTION, -1, SLOT_NONE, -2, ids, MAX_ITEM_DEFS);
    CHECK_EQ_INT(n, 30);
}

TEST(statmods) {
    StatMods m = {{0}};
    CHECK_EQ_INT(statmods_parse("crit=5.0;crit_dmg=40", &m), 0);
    CHECK_NEAR(m.v[STAT_CRIT_CHANCE], 5.0, 0.001);
    CHECK_NEAR(m.v[STAT_CRIT_DMG], 40.0, 0.001);
}

void run_tests_core_data(void) {
    test_rng_determinism();
    test_item_counts();
    test_statmods();
}
