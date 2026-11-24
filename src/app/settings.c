/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Einstellungen: settings.cfg im Save-Verzeichnis, Zeilenformat "schluessel = wert". */
#include "app/settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/util.h"
#include "platform/platform.h"

#define CFG_NAME "settings.cfg"
#define CFG_PATH_MAX 1024
#define CFG_LINE_MAX 256

void settings_defaults(Settings *s) {
    memset(s, 0, sizeof(*s));
    s->window_w = 1280;
    s->window_h = 720;
    s->fullscreen = false;
    s->vsync = true;
    s->ui_scale_pct = 100;
    s->colorblind = 0;
    str_copy(s->language, sizeof(s->language), "de");
    s->vol_master = 0.8f;
    s->vol_sfx = 0.8f;
    s->vol_ui = 0.8f;
    s->audio_enabled = true;
    s->show_damage_numbers = true;
    s->show_names = true;
    s->autosave_s = 60;
    s->game_speed = 1;
    s->save_slot = 0;
    s->zoom_pct = 100;
}

/* ---- Wert-Parser (tolerant: ungueltige Werte werden ignoriert) ---------- */

static bool parse_int(const char *v, int *out) {
    char *end = NULL;
    long n = strtol(v, &end, 10);
    if (end == v || *end != '\0') return false;
    if (n > 1000000L) n = 1000000L;
    if (n < -1000000L) n = -1000000L;
    *out = (int)n;
    return true;
}

static bool parse_float(const char *v, float *out) {
    char buf[32];
    str_copy(buf, sizeof(buf), v);
    for (char *p = buf; *p; p++)
        if (*p == ',') *p = '.'; /* Dezimalkomma tolerieren */
    char *end = NULL;
    float f = strtof(buf, &end);
    if (end == buf || *end != '\0' || !isfinite(f)) return false;
    *out = f;
    return true;
}

static bool parse_bool(const char *v, bool *out) {
    static const char *const yes[] = {"1", "true", "ja", "on", "yes"};
    static const char *const no[] = {"0", "false", "nein", "off", "no"};
    for (int i = 0; i < ARRAY_LEN(yes); i++)
        if (str_icmp(v, yes[i]) == 0) { *out = true; return true; }
    for (int i = 0; i < ARRAY_LEN(no); i++)
        if (str_icmp(v, no[i]) == 0) { *out = false; return true; }
    return false;
}

static int snap_speed(int v) {
    if (v >= 8) return 8;
    if (v >= 4) return 4;
    if (v >= 2) return 2;
    return 1;
}

/* ---- Schluessel zuordnen ------------------------------------------------- */

static void apply_int(Settings *s, const char *key, int n) {
    if (strcmp(key, "window_w") == 0) s->window_w = clampi(n, 640, 16384);
    else if (strcmp(key, "window_h") == 0) s->window_h = clampi(n, 360, 16384);
    else if (strcmp(key, "ui_scale_pct") == 0) s->ui_scale_pct = clampi(n, 75, 200);
    else if (strcmp(key, "colorblind") == 0) s->colorblind = clampi(n, 0, 3);
    else if (strcmp(key, "autosave_s") == 0) s->autosave_s = clampi(n, 0, 3600);
    else if (strcmp(key, "game_speed") == 0) s->game_speed = snap_speed(n);
    else if (strcmp(key, "save_slot") == 0) s->save_slot = clampi(n, 0, 2);
    else if (strcmp(key, "zoom_pct") == 0) s->zoom_pct = clampi(n, 75, 150);
}

static void apply_float(Settings *s, const char *key, float f) {
    f = clampf(f, 0.0f, 1.0f);
    if (strcmp(key, "vol_master") == 0) s->vol_master = f;
    else if (strcmp(key, "vol_sfx") == 0) s->vol_sfx = f;
    else if (strcmp(key, "vol_ui") == 0) s->vol_ui = f;
}

static void apply_bool(Settings *s, const char *key, bool b) {
    if (strcmp(key, "fullscreen") == 0) s->fullscreen = b;
    else if (strcmp(key, "vsync") == 0) s->vsync = b;
    else if (strcmp(key, "audio_enabled") == 0) s->audio_enabled = b;
    else if (strcmp(key, "show_damage_numbers") == 0) s->show_damage_numbers = b;
    else if (strcmp(key, "show_names") == 0) s->show_names = b;
}

static void apply_entry(Settings *s, const char *key, const char *val) {
    if (strcmp(key, "language") == 0) {
        if (str_icmp(val, "de") == 0) str_copy(s->language, sizeof(s->language), "de");
        else if (str_icmp(val, "en") == 0) str_copy(s->language, sizeof(s->language), "en");
        return;
    }
    int n;
    float f;
    bool b;
    if (strncmp(key, "vol_", 4) == 0) {
        if (parse_float(val, &f)) apply_float(s, key, f);
    } else if (parse_bool(val, &b) && (strcmp(key, "fullscreen") == 0 || strcmp(key, "vsync") == 0 ||
                                        strcmp(key, "audio_enabled") == 0 ||
                                        strcmp(key, "show_damage_numbers") == 0 ||
                                        strcmp(key, "show_names") == 0)) {
        apply_bool(s, key, b);
    } else if (parse_int(val, &n)) {
        apply_int(s, key, n);
    }
}

/* Eine Zeile verarbeiten: Kommentare (# ;) und Zeilen ohne '=' werden uebersprungen. */
static void parse_line(Settings *s, char *line) {
    str_trim(line);
    if (line[0] == '\0' || line[0] == '#' || line[0] == ';') return;
    char *eq = strchr(line, '=');
    if (!eq) return;
    *eq = '\0';
    char *key = line;
    char *val = eq + 1;
    str_trim(key);
    str_trim(val);
    if (key[0] == '\0') return;
    apply_entry(s, key, val);
}

static bool cfg_path(char *out, size_t size) {
    char dir[CFG_PATH_MAX];
    if (!platform_get_save_dir(dir, sizeof(dir))) return false;
    str_format(out, size, "%s/%s", dir, CFG_NAME);
    return true;
}

bool settings_load(Settings *s) {
    char path[CFG_PATH_MAX];
    if (!cfg_path(path, sizeof(path))) return false;
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[CFG_LINE_MAX];
    while (fgets(line, sizeof(line), f)) parse_line(s, line);
    fclose(f);
    return true;
}

bool settings_save(const Settings *s) {
    char path[CFG_PATH_MAX], tmp[CFG_PATH_MAX + 8];
    if (!cfg_path(path, sizeof(path))) return false;
    str_format(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return false;
    fprintf(f, "# Panoptix Einstellungen\n");
    fprintf(f, "window_w = %d\nwindow_h = %d\n", s->window_w, s->window_h);
    fprintf(f, "fullscreen = %d\nvsync = %d\n", s->fullscreen, s->vsync);
    fprintf(f, "ui_scale_pct = %d\ncolorblind = %d\n", s->ui_scale_pct, s->colorblind);
    fprintf(f, "language = %s\n", s->language);
    fprintf(f, "vol_master = %.2f\nvol_sfx = %.2f\nvol_ui = %.2f\n", (double)s->vol_master,
            (double)s->vol_sfx, (double)s->vol_ui);
    fprintf(f, "audio_enabled = %d\n", s->audio_enabled);
    fprintf(f, "show_damage_numbers = %d\nshow_names = %d\n", s->show_damage_numbers, s->show_names);
    fprintf(f, "autosave_s = %d\ngame_speed = %d\nsave_slot = %d\nzoom_pct = %d\n", s->autosave_s,
            s->game_speed, s->save_slot, s->zoom_pct);
    bool ok = (fflush(f) == 0) && !ferror(f);
    ok = (fclose(f) == 0) && ok;
    if (!ok) return false;
    return platform_replace_file(tmp, path);
}
