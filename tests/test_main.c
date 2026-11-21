/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Test-Runner. Jedes Modul stellt eine run_tests_*()-Funktion bereit:
 *   run_tests_core_data()  tests/test_core_data.c   (core/, data/)
 *   run_tests_sim()        tests/test_sim.c         (world/, sim/)
 *   run_tests_rules()      tests/test_rules.c       (rules/, game/)
 * Aufruf:  panoptix_tests            alle Tests
 *          panoptix_tests --data-only  nur Daten laden + validieren (CI)
 */
#include <stdio.h>
#include <string.h>

#include "data/data.h"
#include "platform/platform.h"
#include "testing.h"

int g_test_failures = 0;
int g_test_checks = 0;

void run_tests_core_data(void);
void run_tests_sim(void);
void run_tests_rules(void);

int main(int argc, char **argv) {
    char dir[512];
    if (!platform_get_data_dir(dir, sizeof dir)) {
        printf("Datenverzeichnis nicht gefunden (PANOPTIX_DATA setzen)\n");
        return 2;
    }
    if (!data_load_all(dir)) {
        printf("FEHLER: Daten konnten nicht geladen werden\n");
        return 1;
    }
    printf("Daten geladen: %d Items, %d Skills, %d Monster, %d Maps\n", g_item_count, g_skill_count,
           g_monster_count, g_map_count);
    if (argc > 1 && strcmp(argv[1], "--data-only") == 0) {
        return 0;
    }
    printf("== core/data ==\n");
    run_tests_core_data();
    printf("== world/sim ==\n");
    run_tests_sim();
    printf("== rules/game ==\n");
    run_tests_rules();
    printf("%d Pruefungen, %d Fehler\n", g_test_checks, g_test_failures);
    return g_test_failures ? 1 : 0;
}
