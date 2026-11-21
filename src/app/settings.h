/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PNX_APP_SETTINGS_H
#define PNX_APP_SETTINGS_H

#include "core/types.h"

typedef struct Settings {
    int window_w, window_h;
    bool fullscreen;
    bool vsync;
    int ui_scale_pct;        /* 75..200 */
    int colorblind;          /* ColorblindMode */
    char language[8];        /* "de", "en" */
    float vol_master, vol_sfx, vol_ui;
    bool audio_enabled;
    bool show_damage_numbers;
    bool show_names;
    int autosave_s;          /* 0 = aus */
    int game_speed;          /* 1,2,4,8 (Standard 1) */
    int save_slot;           /* 0..2 */
    int zoom_pct;            /* 75..150 */
} Settings;

void settings_defaults(Settings *s);
/* settings.cfg im Save-Verzeichnis ("schluessel = wert"). */
bool settings_load(Settings *s);
bool settings_save(const Settings *s);

#endif
