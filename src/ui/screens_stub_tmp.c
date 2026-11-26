/* SPDX-License-Identifier: GPL-3.0-or-later */
/* TEMP: Platzhalter fuer alle Bildschirme, damit 'make' linkt.
 * Wird geloescht, sobald die echten src/ui/screen_*.c vorhanden sind. */
#include "app/screens.h"

/* TEMP */
void screen_combat(App *app, Rect area) {
    if (!app_has_hero(app)) {
        return;
    }
    int hero = app->sel_hero;
    Run *run = app->game.hero_run[hero];
    if (!run) {
        return;
    }
    ViewOpts o = {false, true, true, true, true, true, false};
    view_draw_run(&app->gfx, &app->game, run, run_slot_of_hero(run, hero), &app->fx[hero], area, &o);
}

/* TEMP */
void screen_command(App *app, Rect area) { (void)app; (void)area; }
/* TEMP */
void screen_skills(App *app, Rect area) { (void)app; (void)area; }
/* TEMP */
void screen_crafting(App *app, Rect area) { (void)app; (void)area; }
/* TEMP */
void screen_worldmap(App *app, Rect area) { (void)app; (void)area; }
/* TEMP */
void screen_shop(App *app, Rect area) { (void)app; (void)area; }
/* TEMP */
void screen_commander(App *app, Rect area) { (void)app; (void)area; }
/* TEMP */
void screen_settings(App *app, Rect area) { (void)app; (void)area; }
