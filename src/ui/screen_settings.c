/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Einstellungen: Grafik, Ton, Barrierefreiheit, Sprache, Spiel (Spec 18/13).
 * Aenderungen wirken sofort (app.c wendet Settings jedes Frame an) und werden
 * bei jeder Aenderung sowie beim Verlassen gespeichert. */
#include <stdlib.h>
#include <string.h>

#include "ui/ui_internal.h"

#define ROW_H 30

typedef struct Col {
    Ui *ui;
    int x, y, w;
    bool changed;
} Col;

static Rect row_begin(Col *c, const char *label, int *ctrl_w) {
    Ui *ui = c->ui;
    Rect r = rect_make(c->x, c->y, c->w, ui_px(ui, ROW_H));
    c->y += r.h + ui_px(ui, 4);
    int lw = r.w * 40 / 100;
    ui_text(ui, r.x, r.y + ui_px(ui, 7), label, COL_INK, FS_NORMAL);
    *ctrl_w = r.w - lw;
    return rect_make(r.x + lw, r.y, r.w - lw, r.h);
}

static void heading(Col *c, const char *text) {
    Ui *ui = c->ui;
    c->y += ui_px(ui, 6);
    ui_text(ui, c->x, c->y, text, COL_WOOD_DARK, FS_BIG);
    c->y += ui_px(ui, 28);
    gfx_fill_rect(ui->gfx, rect_make(c->x, c->y - ui_px(ui, 4), c->w, 1), COL_WOOD);
}

static void row_check(Col *c, const char *label, bool *v) {
    Rect r = rect_make(c->x, c->y, c->w, ui_px(c->ui, ROW_H));
    c->y += r.h + ui_px(c->ui, 4);
    ui_push_id(c->ui, (int)(intptr_t)label & 0xffff);
    if (ui_checkbox(c->ui, r, label, v)) {
        c->changed = true;
    }
    ui_pop_id(c->ui);
}

/* Slider 0..1 mit Prozentanzeige. */
static void row_volume(Col *c, const char *label, float *v) {
    int cw;
    Rect r = row_begin(c, label, &cw);
    Rect sl = rect_make(r.x, r.y, r.w - ui_px(c->ui, 56), r.h);
    float old = *v;
    ui_push_id(c->ui, (int)(intptr_t)label & 0xffff);
    ui_slider(c->ui, sl, v, 0.0f, 1.0f);
    ui_pop_id(c->ui);
    char b[16];
    str_format(b, sizeof b, "%d %%", (int)(*v * 100.0f + 0.5f));
    ui_text_right(c->ui, rect_make(r.x, r.y + ui_px(c->ui, 7), r.w, r.h), b, COL_INK, FS_NORMAL);
    if (*v != old) {
        c->changed = true;
    }
}

/* Ganzzahl-Slider (Prozent) mit Rasterung auf 'step'. */
static void row_percent(Col *c, const char *label, int *v, int lo, int hi, int step) {
    int cw;
    Rect r = row_begin(c, label, &cw);
    Rect sl = rect_make(r.x, r.y, r.w - ui_px(c->ui, 64), r.h);
    float f = (float)*v;
    ui_push_id(c->ui, (int)(intptr_t)label & 0xffff);
    ui_slider(c->ui, sl, &f, (float)lo, (float)hi);
    ui_pop_id(c->ui);
    int nv = clampi(((int)(f + (float)step / 2.0f) / step) * step, lo, hi);
    char b[16];
    str_format(b, sizeof b, "%d %%", nv);
    ui_text_right(c->ui, rect_make(r.x, r.y + ui_px(c->ui, 7), r.w, r.h), b, COL_INK, FS_NORMAL);
    if (nv != *v && !c->ui->in.lmb_down) {
        *v = nv;
        c->changed = true;
    } else if (nv != *v) {
        *v = nv; /* beim Ziehen laufend uebernehmen, gespeichert wird beim Loslassen */
        c->changed = true;
    }
}

static void row_choice(Col *c, const char *label, const char *const *opts, int n, int *v) {
    int cw;
    Rect r = row_begin(c, label, &cw);
    ui_push_id(c->ui, (int)(intptr_t)label & 0xffff);
    if (ui_cycle(c->ui, r, opts, n, v)) {
        c->changed = true;
    }
    ui_pop_id(c->ui);
}

/* ---- Spalten ------------------------------------------------------------------- */
static void column_graphics(App *app, Col *c) {
    Settings *s = &app->settings;
    heading(c, tr("Grafik"));
    const char *modes[2] = {tr("Fenster"), tr("Vollbild")};
    int fs = s->fullscreen ? 1 : 0;
    row_choice(c, tr("Anzeigemodus"), modes, 2, &fs);
    s->fullscreen = fs != 0;
    int old = s->ui_scale_pct;
    row_percent(c, tr("UI-Skalierung"), &s->ui_scale_pct, 75, 200, 5);
    if (s->ui_scale_pct != old) {
        app->gfx.ui_scale = (float)s->ui_scale_pct / 100.0f;
    }
    row_percent(c, tr("Zoom"), &s->zoom_pct, 75, 150, 5);
    row_check(c, tr("Vertikale Synchronisation"), &s->vsync);
    row_check(c, tr("Namen anzeigen"), &s->show_names);
    row_check(c, tr("Schadenszahlen anzeigen"), &s->show_damage_numbers);
}

static void column_audio_access(App *app, Col *c) {
    Settings *s = &app->settings;
    heading(c, tr("Ton"));
    row_check(c, tr("Ton aktiv"), &s->audio_enabled);
    row_volume(c, tr("Gesamtlautstaerke"), &s->vol_master);
    row_volume(c, tr("Effekte"), &s->vol_sfx);
    row_volume(c, tr("Oberflaeche"), &s->vol_ui);
    if (c->changed && app->audio) {
        audio_set_volume(app->audio, s->vol_master, s->vol_sfx, s->vol_ui);
    }
    heading(c, tr("Barrierefreiheit"));
    const char *cb[CB_COUNT] = {tr("Aus"), tr("Deuteranopie"), tr("Protanopie"), tr("Tritanopie")};
    row_choice(c, tr("Farbenblind-Modus"), cb, CB_COUNT, &s->colorblind);
    app->gfx.colorblind = (ColorblindMode)clampi(s->colorblind, 0, CB_COUNT - 1);
}

static void change_language(App *app, int idx) {
    static const char *const codes[2] = {"de", "en"};
    str_copy(app->settings.language, sizeof app->settings.language, codes[idx]);
    i18n_load(data_dir(), codes[idx]);
}

static void column_game(App *app, Col *c) {
    Settings *s = &app->settings;
    heading(c, tr("Sprache"));
    const char *langs[2] = {"Deutsch", "English"};
    int li = strcmp(s->language, "en") == 0 ? 1 : 0, old = li;
    row_choice(c, tr("Sprache"), langs, 2, &li);
    if (li != old) {
        change_language(app, li);
    }
    heading(c, tr("Spiel"));
    static const int speeds[4] = {1, 2, 4, 8};
    int sp = 0;
    for (int i = 0; i < 4; i++) {
        if (s->game_speed == speeds[i]) {
            sp = i;
        }
    }
    const char *sn[4] = {"1x", "2x", "4x", "8x"};
    int old_sp = sp;
    row_choice(c, tr("Spielgeschwindigkeit"), sn, 4, &sp);
    if (sp != old_sp || s->game_speed != speeds[sp]) {
        s->game_speed = speeds[sp];
        if (app->have_game && !app->pause) {
            app->game.speed_mult = (float)speeds[sp];
        }
    }
    static const int autos[5] = {0, 30, 60, 120, 300};
    const char *an[5] = {tr("Aus"), "30 s", "60 s", "2 min", "5 min"};
    int ai = 2;
    for (int i = 0; i < 5; i++) {
        if (s->autosave_s == autos[i]) {
            ai = i;
        }
    }
    int old_ai = ai;
    row_choice(c, tr("Autospeichern"), an, 5, &ai);
    s->autosave_s = autos[ai];
    if (ai != old_ai) {
        c->changed = true;
    }
}

/* ---- Screen ------------------------------------------------------------------------ */
void screen_settings(App *app, Rect area) {
    Ui *ui = &app->ui;
    static Settings s_last;
    static bool s_have_last;
    Rect in = ui_panel(ui, area, tr("Einstellungen"));
    if (!s_have_last) {
        s_last = app->settings;
        s_have_last = true;
    }
    int pad = ui_px(ui, 14), gap = ui_px(ui, 24);
    int cols = in.w > ui_px(ui, 900) ? 3 : 1;
    int cw = (in.w - 2 * pad - (cols - 1) * gap) / cols;
    Col c[3];
    for (int i = 0; i < 3; i++) {
        c[i] = (Col){ui, in.x + pad + (cols == 1 ? 0 : i) * (cw + gap), in.y + pad, cw, false};
    }
    column_graphics(app, &c[0]);
    if (cols == 1) {
        c[1] = c[0];
        c[2] = c[0];
    }
    column_audio_access(app, &c[cols == 1 ? 0 : 1]);
    column_game(app, &c[cols == 1 ? 0 : 2]);
    bool changed = c[0].changed || c[1].changed || c[2].changed;
    /* Speichern: bei Aenderung, aber nicht waehrend ein Slider gezogen wird. */
    if ((changed || memcmp(&s_last, &app->settings, sizeof s_last) != 0) && !ui->in.lmb_down) {
        settings_save(&app->settings);
        s_last = app->settings;
    }
    int by = in.y + in.h - pad - ui_px(ui, 34);
    if (ui_button(ui, rect_make(in.x + pad, by, ui_px(ui, 160), ui_px(ui, 34)), tr("Zurueck"))) {
        settings_save(&app->settings);
        s_last = app->settings;
        app_set_screen(app, app->prev_screen == SCR_SETTINGS ? SCR_MENU : app->prev_screen);
    }
}
