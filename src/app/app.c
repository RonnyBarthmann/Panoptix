/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Anwendungsschicht: Start, Hauptschleife, Hotkeys, obere Leiste, Toasts. */
#include "app/app.h"

#include <SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/input.h"
#include "app/screens.h"
#include "game/save.h"
#include "core/i18n.h"
#include "core/util.h"
#include "platform/platform.h"
#include "rules/rules.h"
#include "sim/sim.h"

#ifndef PNX_VERSION
#define PNX_VERSION "dev"
#endif

#define SIM_STEP (1.0 / 60.0)      /* fester Zeitschritt der Hauptschleife */
#define SIM_MAX_STEPS 8            /* Schutz gegen Spirale des Todes */
#define MESSAGE_TTL 3.0f
#define TOPBAR_H 40
#define PATH_MAX_LEN 1024

/* ------------------------------------------------------------------------ */
/* Kommandozeile                                                             */
/* ------------------------------------------------------------------------ */
typedef struct CliArgs {
    bool cheat;
    bool headless;
    bool version;
    char data_dir[PATH_MAX_LEN];  /* leer = platform_get_data_dir */
    const char *shot_path;
    int shot_screen;
    int shot_frames;
} CliArgs;

static bool g_cheat = false;      /* --cheat bleibt ueber Neues Spiel / Laden erhalten */

static bool parse_args(int argc, char **argv, CliArgs *a) {
    memset(a, 0, sizeof(*a));
    a->shot_frames = 120;
    for (int i = 1; i < argc; i++) {
        const char *s = argv[i];
        bool has_next = i + 1 < argc;
        if (strcmp(s, "--cheat") == 0) {
            a->cheat = true;
        } else if (strcmp(s, "--headless") == 0) {
            a->headless = true;
        } else if (strcmp(s, "--version") == 0) {
            a->version = true;
        } else if (strcmp(s, "--data") == 0 && has_next) {
            str_copy(a->data_dir, sizeof a->data_dir, argv[++i]);
        } else if (strcmp(s, "--screenshot") == 0 && has_next) {
            a->shot_path = argv[++i];
        } else if (strcmp(s, "--screen") == 0 && has_next) {
            a->shot_screen = atoi(argv[++i]);
        } else if (strcmp(s, "--frames") == 0 && has_next) {
            a->shot_frames = atoi(argv[++i]);
        } else {
            fprintf(stderr, "Unbekanntes Argument: %s\n", s);
            fprintf(stderr, "Nutzung: panoptix [--cheat] [--data DIR] [--headless] "
                            "[--screenshot out.bmp] [--screen N] [--frames N] [--version]\n");
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* Kleine App-Hilfen                                                         */
/* ------------------------------------------------------------------------ */
void app_set_screen(App *app, Screen s) {
    if (s < 0 || s >= SCR_COUNT || s == app->screen) {
        return;
    }
    app->prev_screen = app->screen;
    app->screen = s;
    app->ui.focus = 0;
    app->ui.modal_open = false;
}

void app_message(App *app, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(app->message, sizeof app->message, fmt, ap);
    va_end(ap);
    app->message_ttl = MESSAGE_TTL;
}

void app_play(App *app, Sfx sfx) {
    if (app->audio) {
        audio_play(app->audio, sfx, 0.7f);
    }
}

bool app_has_hero(const App *app) {
    int i = app->sel_hero;
    return app->have_game && i >= 0 && i < MAX_HEROES && app->game.heroes[i].used;
}

Hero *app_hero(App *app) {
    return app_has_hero(app) ? &app->game.heroes[app->sel_hero] : NULL;
}

void app_select_next_hero(App *app, int dir) {
    if (!app->have_game) {
        return;
    }
    int step = dir < 0 ? -1 : 1;
    int start = app->sel_hero < 0 ? (step > 0 ? -1 : 0) : app->sel_hero;
    for (int n = 1; n <= MAX_HEROES; n++) {
        int i = ((start + step * n) % MAX_HEROES + MAX_HEROES) % MAX_HEROES;
        if (app->game.heroes[i].used) {
            app->sel_hero = i;
            return;
        }
    }
    app->sel_hero = -1;
}

/* Standard-Layouts: Layout 1 = die ersten vier Helden, andere leer. */
static void init_layouts(App *app) {
    for (int l = 0; l < MAX_LAYOUTS; l++) {
        for (int s = 0; s < 4; s++) {
            app->layouts[l][s] = -1;
        }
        str_format(app->layout_names[l], sizeof app->layout_names[l], "Layout %d", l + 1);
    }
    int s = 0;
    for (int i = 0; i < MAX_HEROES && s < 4; i++) {
        if (app->game.heroes[i].used) {
            app->layouts[0][s++] = i;
        }
    }
    app->layout = 0;
}

static void reset_view_state(App *app) {
    for (int i = 0; i < MAX_HEROES; i++) {
        view_fx_init(&app->fx[i]);
    }
    app->sel_hero = -1;
    app_select_next_hero(app, 1);
    init_layouts(app);
    app->selected_map = 0;
    app->pause = false;
}

/* ------------------------------------------------------------------------ */
/* Speichern / Laden / Neues Spiel                                           */
/* ------------------------------------------------------------------------ */
bool app_save(App *app) {
    if (!app->have_game) {
        return false;
    }
    bool ok = save_game(&app->game, app->settings.save_slot);
    if (ok) {
        app_message(app, tr("Gespeichert (Slot %d)"), app->settings.save_slot + 1);
        app_play(app, SFX_CLICK);
    } else {
        app_message(app, "%s", tr("Speichern fehlgeschlagen"));
        app_play(app, SFX_ERROR);
    }
    return ok;
}

bool app_load(App *app, int slot) {
    if (!save_exists(slot)) {
        app_message(app, tr("Kein Spielstand in Slot %d"), slot + 1);
        app_play(app, SFX_ERROR);
        return false;
    }
    bool ok = load_game(&app->game, slot);
    if (!ok) {
        app_message(app, "%s", tr("Laden fehlgeschlagen"));
        app_play(app, SFX_ERROR);
        return false;
    }
    app->game.cheat_mode = g_cheat;
    app->have_game = true;
    app->settings.save_slot = slot;
    reset_view_state(app);
    app->screen = SCR_COMBAT;
    app->prev_screen = SCR_COMBAT;
    app_message(app, tr("Spielstand geladen (Slot %d)"), slot + 1);
    return true;
}

void app_new_game(App *app) {
    if (app->have_game) {
        game_shutdown(&app->game);
    }
    game_init_new(&app->game, (u64)platform_unix_time() ^ ((u64)SDL_GetTicks() << 20));
    app->game.cheat_mode = g_cheat;
    app->have_game = true;
    reset_view_state(app);
    app->screen = SCR_HEROLIST;   /* ohne Helden geht es mit der Heldenerstellung los */
    app->prev_screen = SCR_COMBAT;
    app_message(app, "%s", tr("Neues Spiel gestartet"));
}

/* ------------------------------------------------------------------------ */
/* Obere Leiste                                                              */
/* ------------------------------------------------------------------------ */
typedef struct NavItem {
    Screen screen;
    const char *label;
    const char *tip_title;
    const char *tip;
} NavItem;

static const NavItem NAV_ITEMS[] = {
    {SCR_HEROLIST, "Helden", "Heldenliste (F1)", "Helden erstellen, auswaehlen und Maps zuweisen."},
    {SCR_COMMAND, "Kommando", "Kommandoansicht (F2)", "Vier Helden gleichzeitig beobachten."},
    {SCR_WORLDMAP, "Karte", "Weltkarte (F3 / M)", "Maps auswaehlen und Helden zuweisen."},
    {SCR_HERO, "Held", "Heldendetails (H)", "Ausruestung und Werte des Helden."},
    {SCR_SKILLS, "Skills", "Skills und KI (K)", "Skills belegen und Held-KI einstellen."},
    {SCR_INVENTORY, "Inventar", "Inventar (I)", "Gegenstaende, Kisten und Materialien."},
    {SCR_CRAFTING, "Schmiede", "Herstellung (C)", "Herstellen, Verbessern, Verzaubern."},
    {SCR_SHOP, "Shop", "Shop und Basislager (S)", "Einkaufen und Basislager."},
    {SCR_COMMANDER, "Kommandeur", "Kommandeur (P)", "Traits, Skills, Posten, Quests, Post, Erfolge."},
    {SCR_MENU, "Menue", "Menue (Esc)", "Speichern, Laden, Einstellungen, Beenden."},
};

/* Tooltip fuer eine Fensterregion der Leiste. */
static void topbar_tip(App *app, Rect r, const char *id, const char *title, u32 rgb, const char *text) {
    UiClick c = ui_region(&app->ui, r, ui_make_id(&app->ui, id, r.x, r.y));
    if (c.hovered) {
        ui_tooltip(&app->ui, title, rgb, text);
    }
}

/* Level-Anzeige mit EXP-Balken; gibt die rechte Kante zurueck. */
static int draw_level_block(App *app, int x, int y, int h) {
    Ui *ui = &app->ui;
    const Player *p = &app->game.player;
    int w = ui_px(ui, 190);
    char buf[96], a[24], b[24];
    str_format(buf, sizeof buf, "%s %d", tr("Lv"), p->level);
    ui_label(ui, x, y + ui_px(ui, 3), buf, color_from_rgb(0xF2E3B8, 255), 16);
    int lw = gfx_text_width(ui->gfx, buf, 16) + ui_px(ui, 8);
    u64 need = p->level >= PLAYER_MAX_LEVEL ? 0 : player_exp_for_level(p->level);
    float frac = need > 0 ? (float)((double)p->exp / (double)need) : 1.0f;
    Rect bar = rect_make(x + lw, y + ui_px(ui, 6), w - lw, h - ui_px(ui, 12));
    ui_progress_bar(ui, bar, clampf(frac, 0.0f, 1.0f), color_from_rgb(0x6FB0E8, 255), NULL);
    str_format_compact(a, sizeof a, (double)p->exp);
    str_format_compact(b, sizeof b, (double)need);
    str_format(buf, sizeof buf, tr("Kommandeur-Level %d\nEXP: %s / %s"), p->level, a, b);
    topbar_tip(app, rect_make(x, y, w, h), "tb_level", tr("Kommandeur"), 0xF2E3B8, buf);
    return x + w;
}

/* Waehrungsanzeige "Name Wert"; gibt die rechte Kante zurueck. */
static int draw_currency(App *app, int x, int y, const char *name, i64 value, u32 rgb, const char *tip) {
    Ui *ui = &app->ui;
    char num[32], full[48], buf[80];
    str_format_compact(num, sizeof num, (double)value);
    str_format(buf, sizeof buf, "%s %s", name, num);
    int w = gfx_text_width(ui->gfx, buf, 14) + ui_px(ui, 4);
    ui_label(ui, x, y, name, color_from_rgb(rgb, 255), 14);
    int nw = gfx_text_width(ui->gfx, name, 14) + ui_px(ui, 6);
    ui_label(ui, x + nw, y, num, color_from_rgb(0xFFFFFF, 255), 14);
    str_format_number(num, sizeof num, value);
    str_format(full, sizeof full, "%s", num);
    topbar_tip(app, rect_make(x, y - ui_px(ui, 2), w + ui_px(ui, 4), ui_px(ui, 22)), name, name, rgb,
               full);
    (void)tip;
    return x + w + ui_px(ui, 14);
}

Rect app_draw_topbar(App *app) {
    Ui *ui = &app->ui;
    Gfx *g = &app->gfx;
    Rect full = rect_make(0, 0, g->w, g->h);
    if (!app->have_game || app->screen == SCR_MENU) {
        return full;
    }
    int bar_h = ui_px(ui, TOPBAR_H);
    gfx_fill_rect_vgrad(g, rect_make(0, 0, g->w, bar_h), color_from_rgb(0x4A3524, 255),
                        color_from_rgb(0x2E1F14, 255));
    gfx_fill_rect(g, rect_make(0, bar_h - 2, g->w, 2), color_from_rgb(0x1A100A, 255));

    int x = ui_px(ui, 10);
    int ty = (bar_h - gfx_text_height(g, 14)) / 2;
    x = draw_level_block(app, x, 0, bar_h) + ui_px(ui, 16);
    const Player *p = &app->game.player;
    x = draw_currency(app, x, ty, tr("Gold"), p->gold, 0xF2C94C, "");
    x = draw_currency(app, x, ty, tr("Diamanten"), p->diamonds, 0x6FD8F0, "");
    x = draw_currency(app, x, ty, "CR", p->credits, 0xB8E07A, "");

    /* Navigationsbuttons rechtsbuendig. */
    int n = ARRAY_LEN(NAV_ITEMS);
    int gap = ui_px(ui, 3);
    int avail = g->w - x - ui_px(ui, 10);
    int bw = clampi((avail - gap * (n - 1)) / n, ui_px(ui, 40), ui_px(ui, 86));
    int bx = g->w - ui_px(ui, 8) - n * bw - (n - 1) * gap;
    int by = ui_px(ui, 5);
    int bh = bar_h - ui_px(ui, 10);
    for (int i = 0; i < n; i++) {
        const NavItem *it = &NAV_ITEMS[i];
        Rect r = rect_make(bx + i * (bw + gap), by, bw, bh);
        UiStyle st = it->screen == app->screen ? UI_BTN_GREEN : UI_BTN_NORMAL;
        ui_push_id(ui, i);
        if (ui_button_tip(ui, r, tr(it->label), st, true, tr(it->tip))) {
            app_play(app, SFX_CLICK);
            app_set_screen(app, it->screen);
        }
        ui_pop_id(ui);
    }
    return rect_make(0, bar_h, g->w, g->h - bar_h);
}

/* Toasts (unten rechts) und Statuszeile (unten links). */
static void draw_toasts(App *app) {
    Ui *ui = &app->ui;
    Gfx *g = &app->gfx;
    int th = ui_px(ui, 24);
    int y = g->h - ui_px(ui, 10) - th;
    for (int i = 0; i < TOAST_MAX; i++) {
        const Toast *t = &app->game.toasts[i];
        if (t->ttl <= 0.0f || !app->have_game) {
            continue;
        }
        u8 alpha = (u8)(255.0f * clampf(t->ttl / 1.0f, 0.0f, 1.0f));
        int tw = gfx_text_width(g, t->text, 14) + ui_px(ui, 20);
        Rect r = rect_make(g->w - ui_px(ui, 10) - tw, y, tw, th);
        Color bg = color_from_rgb(0x1E140C, (u8)(alpha * 0.85f));
        gfx_fill_rect(g, r, bg);
        gfx_draw_rect(g, r, color_from_rgb(t->color, alpha), 1);
        gfx_text(g, r.x + ui_px(ui, 10), r.y + (th - gfx_text_height(g, 14)) / 2, t->text,
                 color_from_rgb(t->color, alpha), 14);
        y -= th + ui_px(ui, 4);
    }
    if (app->message_ttl > 0.0f && app->message[0]) {
        u8 alpha = (u8)(255.0f * clampf(app->message_ttl, 0.0f, 1.0f));
        int tw = gfx_text_width(g, app->message, 14) + ui_px(ui, 20);
        Rect r = rect_make(ui_px(ui, 10), g->h - ui_px(ui, 10) - th, tw, th);
        gfx_fill_rect(g, r, color_from_rgb(0x1E140C, (u8)(alpha * 0.85f)));
        gfx_draw_rect(g, r, color_from_rgb(0xF2E3B8, alpha), 1);
        gfx_text(g, r.x + ui_px(ui, 10), r.y + (th - gfx_text_height(g, 14)) / 2, app->message,
                 color_from_rgb(0xF2E3B8, alpha), 14);
    }
}

/* ------------------------------------------------------------------------ */
/* Ereignisse der Runs -> Effekte, Audio                                     */
/* ------------------------------------------------------------------------ */
typedef struct Visible {
    bool hero[MAX_HEROES];   /* Held wird gerade gezeichnet */
} Visible;

static void mark_visible(const App *app, Visible *v) {
    memset(v, 0, sizeof *v);
    if (!app->have_game) {
        return;
    }
    if (app->screen == SCR_COMBAT && app->sel_hero >= 0) {
        v->hero[app->sel_hero] = true;
    } else if (app->screen == SCR_COMMAND) {
        for (int s = 0; s < 4; s++) {
            int h = app->layouts[app->layout][s];
            if (h >= 0 && h < MAX_HEROES) {
                v->hero[h] = true;
            }
        }
    }
}

static bool run_visible(const Run *r, const Visible *v) {
    for (int i = 0; i < r->hero_count; i++) {
        int h = r->heroes[i].hero_idx;
        if (h >= 0 && h < MAX_HEROES && v->hero[h]) {
            return true;
        }
    }
    return false;
}

/* Wichtige Ereignisse hoert man auch von nicht sichtbaren Runs. */
static void play_event_sound(App *app, const SimEvent *ev, bool visible) {
    switch (ev->type) {
    case EV_LEVELUP: app_play(app, SFX_LEVELUP); break;
    case EV_MAP_CLEAR: app_play(app, SFX_MAP_CLEAR); break;
    case EV_HERO_DEATH: app_play(app, SFX_DEATH); break;
    case EV_BOSS_SPAWN: if (visible) app_play(app, SFX_BOSS_INTRO); break;
    default: break;
    }
    if (!visible) {
        return;
    }
    switch (ev->type) {
    case EV_DAMAGE: app_play(app, ev->flag ? SFX_CRIT : (ev->a ? SFX_HERO_HIT : SFX_HIT)); break;
    case EV_HEAL: app_play(app, SFX_HEAL); break;
    case EV_KILL: app_play(app, SFX_KILL); break;
    case EV_CHEST_PICKUP: if (app->audio) audio_play(app->audio, audio_chest_sfx((Tier)ev->tier), 0.7f); break;
    case EV_SKILL_CAST: app_play(app, SFX_SKILL); break;
    default: break;
    }
}

static void pump_run_events(App *app, const Visible *vis) {
    Game *g = &app->game;
    SimEvent ev;
    for (int i = 0; i < g->run_count; i++) {
        Run *r = g->runs[i];
        if (!r) {
            continue;
        }
        bool visible = run_visible(r, vis);
        while (run_pop_event(r, &ev)) {
            play_event_sound(app, &ev, visible);
            if (!visible) {
                continue;
            }
            for (int h = 0; h < r->hero_count; h++) {
                int hi = r->heroes[h].hero_idx;
                if (hi >= 0 && hi < MAX_HEROES && vis->hero[hi]) {
                    view_fx_feed(&app->fx[hi], &ev, r);
                }
            }
        }
    }
}

/* Neue Toasts erzeugen einen Ton (Kisten-Tier, sonst Aufnahme). */
static void toast_sounds(App *app) {
    static float prev_ttl[TOAST_MAX];
    for (int i = 0; i < TOAST_MAX; i++) {
        const Toast *t = &app->game.toasts[i];
        if (t->ttl > prev_ttl[i] + 0.01f && app->audio) {
            Sfx s = t->tier >= 0 ? audio_chest_sfx((Tier)t->tier) : SFX_PICKUP;
            audio_play(app->audio, s, 0.5f);
        }
        prev_ttl[i] = t->ttl;
    }
}

/* ------------------------------------------------------------------------ */
/* Hotkeys                                                                   */
/* ------------------------------------------------------------------------ */
static void toggle_screen(App *app, Screen s) {
    if (app->screen == s) {
        app_set_screen(app, app->prev_screen == s || app->prev_screen == SCR_MENU ? SCR_COMBAT : app->prev_screen);
    } else {
        app_set_screen(app, s);
    }
}

static void go_back(App *app) {
    if (app->screen == SCR_COMBAT || app->screen == SCR_COMMAND) {
        app_set_screen(app, SCR_MENU);
        return;
    }
    Screen t = app->prev_screen;
    if (t == app->screen || t == SCR_MENU) {
        t = SCR_COMBAT;
    }
    app_set_screen(app, t);
}

/* Handlungen auf den ausgewaehlten Helden im laufenden Run (Q W E R, 1-3, Leertaste). */
static void hero_actions(App *app) {
    const UiInput *in = &app->ui.in;
    if (!app_has_hero(app) || (app->screen != SCR_COMBAT && app->screen != SCR_COMMAND)) {
        return;
    }
    Game *g = &app->game;
    Run *run = g->hero_run[app->sel_hero];
    if (!run) {
        return;
    }
    int slot = run_slot_of_hero(run, app->sel_hero);
    if (slot < 0) {
        return;
    }
    if (in->key_pressed[KEY_SPACE]) {
        run_set_manual(run, slot, !run->heroes[slot].manual);
        app_message(app, "%s", run->heroes[slot].manual ? tr("Manuelle Steuerung") : tr("Automatik"));
    }
    if (in->ctrl) {
        return;
    }
    for (int i = 0; i < POTION_SLOTS; i++) {
        if (in->key_pressed[KEY_1 + i]) {
            run_manual_potion(run, g, slot, i);
        }
    }
    static const Key skill_keys[SKILL_SLOTS] = {KEY_Q, KEY_W, KEY_E, KEY_R};
    for (int i = 0; i < SKILL_SLOTS; i++) {
        if (in->key_pressed[skill_keys[i]]) {
            run_manual_cast(run, g, slot, i);
        }
    }
}

static void handle_collect(App *app) {
    int n = 0;
    if (app->ui.in.shift && app_has_hero(app)) {
        n = game_collect_chests(&app->game, app->sel_hero);
    } else {
        n = game_collect_all(&app->game);
    }
    app_message(app, n > 0 ? tr("%d Kisten eingesammelt") : tr("Keine Kisten zum Einsammeln"), n);
}

static void handle_screen_hotkeys(App *app) {
    const UiInput *in = &app->ui.in;
    static const struct { Key key; Screen s; } map[] = {
        {KEY_F1, SCR_HEROLIST}, {KEY_F2, SCR_COMMAND}, {KEY_F3, SCR_WORLDMAP},
        {KEY_I, SCR_INVENTORY}, {KEY_C, SCR_CRAFTING}, {KEY_K, SCR_SKILLS},
        {KEY_H, SCR_HERO}, {KEY_S, SCR_SHOP}, {KEY_P, SCR_COMMANDER}, {KEY_M, SCR_WORLDMAP},
    };
    bool typing = app->ui.focus != 0;
    for (int i = 0; i < ARRAY_LEN(map); i++) {
        bool fkey = map[i].key >= KEY_F1 && map[i].key <= KEY_F12;
        if (in->key_pressed[map[i].key] && (fkey || (!typing && !in->ctrl && !in->alt))) {
            toggle_screen(app, map[i].s);
        }
    }
    if (typing) {
        return;
    }
    if (in->key_pressed[KEY_TAB]) {
        app_select_next_hero(app, in->shift ? -1 : 1);
    }
    for (int l = 0; l < MAX_LAYOUTS; l++) {
        if (in->ctrl && in->key_pressed[KEY_1 + l]) {
            app->layout = l;
            app_set_screen(app, SCR_COMMAND);
        }
    }
    hero_actions(app);
}

static void handle_global_hotkeys(App *app) {
    const UiInput *in = &app->ui.in;
    if (in->key_pressed[KEY_F11]) {
        app->settings.fullscreen = !app->settings.fullscreen;
        settings_save(&app->settings);
    }
    if (in->key_pressed[KEY_F12]) {
        char path[PATH_MAX_LEN];
        str_format(path, sizeof path, "panoptix_%lld.bmp", (long long)platform_unix_time());
        app_message(app, gfx_screenshot(&app->gfx, path) ? tr("Screenshot: %s") : tr("Screenshot fehlgeschlagen: %s"),
                    path);
    }
    if (!app->have_game) {
        return;
    }
    if (in->key_pressed[KEY_ESCAPE] && app->ui.focus == 0 && !app->ui.modal_open) {
        go_back(app);
    }
    if (in->key_pressed[KEY_F5]) {
        app_save(app);
    }
    if (in->key_pressed[KEY_F4]) {
        handle_collect(app);
    }
    handle_screen_hotkeys(app);
}

/* ------------------------------------------------------------------------ */
/* Bildschirm-Dispatch                                                       */
/* ------------------------------------------------------------------------ */
static void dispatch_screen(App *app, Rect area) {
    switch (app->screen) {
    case SCR_COMBAT: screen_combat(app, area); break;
    case SCR_COMMAND: screen_command(app, area); break;
    case SCR_HEROLIST: screen_herolist(app, area); break;
    case SCR_HERO: screen_hero(app, area); break;
    case SCR_SKILLS: screen_skills(app, area); break;
    case SCR_INVENTORY: screen_inventory(app, area); break;
    case SCR_CRAFTING: screen_crafting(app, area); break;
    case SCR_WORLDMAP: screen_worldmap(app, area); break;
    case SCR_SHOP: screen_shop(app, area); break;
    case SCR_COMMANDER: screen_commander(app, area); break;
    case SCR_SETTINGS: screen_settings(app, area); break;
    case SCR_MENU:
    default: screen_menu(app, area); break;
    }
}

/* ------------------------------------------------------------------------ */
/* Ein Frame: Eingabe-Auswertung, Ereignisse, Zeichnen                       */
/* ------------------------------------------------------------------------ */
static void apply_settings(App *app) {
    Settings *s = &app->settings;
    app->gfx.ui_scale = clampf((float)s->ui_scale_pct / 100.0f, 0.75f, 2.0f);
    app->gfx.colorblind = (ColorblindMode)clampi(s->colorblind, 0, CB_COUNT - 1);
    if (app->audio) {
        audio_set_volume(app->audio, s->vol_master, s->vol_sfx, s->vol_ui);
    }
    if (app->have_game) {
        int speed = clampi(s->game_speed, 1, 8);
        app->game.speed_mult = app->pause ? 0.0f : (float)speed;
    }
    static bool applied_fs = false;
    if (s->fullscreen != applied_fs && !app->gfx.headless) {
        gfx_set_fullscreen(&app->gfx, s->fullscreen);
        applied_fs = s->fullscreen;
    } else if (app->gfx.headless) {
        applied_fs = s->fullscreen;
    }
}

static void draw_frame(App *app, const UiInput *in, float dt) {
    Visible vis;
    app->time += dt;
    app->frame_dt = dt;
    if (app->message_ttl > 0.0f) {
        app->message_ttl -= dt;
    }
    apply_settings(app);
    if (!app->have_game && app->screen != SCR_MENU && app->screen != SCR_SETTINGS) {
        app->screen = SCR_MENU;
    }
    ui_begin_frame(&app->ui, &app->gfx, in, app->time, dt);
    handle_global_hotkeys(app);
    if (app->have_game) {
        app_ensure_hero(app);
        mark_visible(app, &vis);
        pump_run_events(app, &vis);
        toast_sounds(app);
        for (int i = 0; i < MAX_HEROES; i++) {
            if (vis.hero[i]) {
                view_fx_update(&app->fx[i], dt);
            }
        }
    }
    gfx_begin_frame(&app->gfx, color_from_rgb(0x1E140C, 255));
    Rect area = app_draw_topbar(app);
    dispatch_screen(app, area);
    draw_toasts(app);
    if (app->show_fps && dt > 0.0f) {
        char buf[32];
        str_format(buf, sizeof buf, "%.0f FPS", 1.0f / dt);
        gfx_text_shadow(&app->gfx, ui_px(&app->ui, 8), app->gfx.h - ui_px(&app->ui, 60), buf,
                        color_from_rgb(0xFFFFFF, 255), 12);
    }
    ui_end_frame(&app->ui);
}

/* Simulation um 'dt' realer Sekunden in festen Schritten voranbringen. */
static void simulate(App *app, double *acc, double dt) {
    if (!app->have_game) {
        return;
    }
    *acc += dt;
    int steps = 0;
    while (*acc >= SIM_STEP && steps < SIM_MAX_STEPS) {
        game_update(&app->game, SIM_STEP);
        *acc -= SIM_STEP;
        steps++;
    }
    if (steps == SIM_MAX_STEPS) {
        *acc = 0.0;
    }
}

static void autosave(App *app, double *timer, double dt) {
    int period = app->settings.autosave_s;
    if (!app->have_game || period <= 0) {
        return;
    }
    *timer += dt;
    if (*timer >= period) {
        *timer = 0.0;
        if (app->game.dirty_save && save_game(&app->game, app->settings.save_slot)) {
            app_message(app, "%s", tr("Automatisch gespeichert"));
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Start / Ende                                                              */
/* ------------------------------------------------------------------------ */
static bool init_app(App *app, const CliArgs *a) {
    char data[PATH_MAX_LEN];
    if (a->data_dir[0]) {
        str_copy(data, sizeof data, a->data_dir);
    } else if (!platform_get_data_dir(data, sizeof data)) {
        fprintf(stderr, "Datenverzeichnis nicht gefunden (PANOPTIX_DATA oder --data DIR).\n");
        return false;
    }
    if (!data_load_all(data)) {
        fprintf(stderr, "Spieldaten konnten nicht geladen werden (%s).\n", data);
        return false;
    }
    settings_defaults(&app->settings);
    settings_load(&app->settings);
    i18n_load(data, app->settings.language);
    app->gfx.ui_scale = (float)app->settings.ui_scale_pct / 100.0f;
    if (!gfx_init(&app->gfx, "Panoptix", app->settings.window_w, app->settings.window_h,
                  app->settings.fullscreen, app->settings.vsync, a->headless)) {
        fprintf(stderr, "Fenster konnte nicht erstellt werden.\n");
        return false;
    }
    app->audio = audio_init(app->settings.audio_enabled && !a->headless);
    input_init();
    for (int i = 0; i < MAX_HEROES; i++) {
        view_fx_init(&app->fx[i]);
    }
    app->sel_hero = -1;
    app->screen = SCR_MENU;
    app->prev_screen = SCR_MENU;
    app->running = true;
    return true;
}

static void shutdown_app(App *app) {
    if (app->have_game) {
        game_shutdown(&app->game);
    }
    audio_shutdown(app->audio);
    gfx_shutdown(&app->gfx);
    i18n_shutdown();
}

/* Screenshot-/Testmodus: neues Spiel, Testheld auf Map 1-1, N Frames. */
static int run_screenshot(App *app, const CliArgs *a) {
    game_init_new(&app->game, 12345);
    app->game.cheat_mode = g_cheat;
    app->have_game = true;
    int hero = game_create_hero(&app->game, "Testheld", CLASS_KNIGHT, 0, true);
    int map = data_find_map("1-1");
    if (hero >= 0 && map >= 0) {
        game_assign_hero(&app->game, hero, map, 0);
    }
    reset_view_state(app);
    app->selected_map = map >= 0 ? map : 0;
    app->screen = (Screen)clampi(a->shot_screen, 0, SCR_COUNT - 1);
    app->prev_screen = SCR_COMBAT;
    UiInput in;
    memset(&in, 0, sizeof in);
    in.mx = -1;
    in.my = -1;
    int frames = a->shot_frames > 0 ? a->shot_frames : 1;
    double acc = 0.0;
    const double dt = 1.0 / 30.0;
    for (int f = 0; f < frames; f++) {
        input_begin_frame(&in);
        simulate(app, &acc, dt);
        draw_frame(app, &in, (float)dt);
        if (f == frames - 1 && a->shot_path) {
            if (!gfx_screenshot(&app->gfx, a->shot_path)) {
                fprintf(stderr, "Screenshot konnte nicht geschrieben werden: %s\n", a->shot_path);
                return 1;
            }
        }
        gfx_end_frame(&app->gfx);
    }
    return 0;
}

static void handle_window_event(App *app, const SDL_Event *ev) {
    if (ev->type == SDL_WINDOWEVENT && (ev->window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                                        ev->window.event == SDL_WINDOWEVENT_RESIZED)) {
        gfx_update_size(&app->gfx);
        if (!app->settings.fullscreen) {
            app->settings.window_w = app->gfx.w;
            app->settings.window_h = app->gfx.h;
        }
    }
}

static void main_loop(App *app) {
    UiInput in;
    memset(&in, 0, sizeof in);
    double last = platform_monotonic_seconds();
    double acc = 0.0, save_timer = 0.0;
    while (app->running) {
        double now = platform_monotonic_seconds();
        double dt = now - last;
        last = now;
        if (dt > 0.25) {
            dt = 0.25;
        }
        input_begin_frame(&in);
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            input_handle_event(&in, &ev);
            handle_window_event(app, &ev);
        }
        if (in.quit_requested) {
            app->running = false;
        }
        simulate(app, &acc, dt);
        autosave(app, &save_timer, dt);
        draw_frame(app, &in, (float)dt);
        if (in.quit_requested) {
            app->running = false;
        }
        gfx_end_frame(&app->gfx);
        if (!app->settings.vsync && platform_monotonic_seconds() - now < 0.008) {
            SDL_Delay(2);
        }
    }
    if (app->have_game) {
        save_game(&app->game, app->settings.save_slot);
    }
    settings_save(&app->settings);
}

int app_run(int argc, char **argv) {
    CliArgs args;
    if (!parse_args(argc, argv, &args)) {
        return 2;
    }
    if (args.version) {
        printf("Panoptix %s\n", PNX_VERSION);
        return 0;
    }
    g_cheat = args.cheat;
    App *app = (App *)calloc(1, sizeof(App));
    if (!app) {
        return 1;
    }
    int code = 0;
    if (!init_app(app, &args)) {
        free(app);
        return 1;
    }
    app->shot_path = args.shot_path;
    app->shot_frames = args.shot_frames;
    app->shot_screen = args.shot_screen;
    if (args.shot_path) {
        code = run_screenshot(app, &args);
    } else {
        main_loop(app);
    }
    shutdown_app(app);
    free(app);
    return code;
}
