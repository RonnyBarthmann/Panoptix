/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Lokalisierung: deutscher Text -> Uebersetzung (Hashtabelle, offene Adressierung). */
#include "core/i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/tsv.h"
#include "core/util.h"

static Tsv s_tsv;
static bool s_loaded = false;
static char s_lang[8] = "de";
static const char **s_keys = NULL;   /* Zeiger in s_tsv */
static const char **s_vals = NULL;
static int s_cap = 0;

static u32 hash_str(const char *s) {
    u32 h = 2166136261u;
    while (*s) {
        h = (h ^ (u8)*s++) * 16777619u;
    }
    return h;
}

void i18n_shutdown(void) {
    if (s_loaded) {
        tsv_free(&s_tsv);
    }
    free((void *)s_keys);
    free((void *)s_vals);
    s_keys = NULL;
    s_vals = NULL;
    s_cap = 0;
    s_loaded = false;
    str_copy(s_lang, sizeof s_lang, "de");
}

bool i18n_load(const char *data_dir, const char *lang) {
    i18n_shutdown();
    if (!lang || strcmp(lang, "de") == 0) {
        return true; /* Quelltext ist bereits deutsch */
    }
    char path[512];
    str_format(path, sizeof path, "%s/lang/%s.tsv", data_dir, lang);
    if (!tsv_load(&s_tsv, path)) {
        return false;
    }
    s_loaded = true;
    str_copy(s_lang, sizeof s_lang, lang);
    int cde = tsv_col(&s_tsv, "de");
    int cen = tsv_col(&s_tsv, lang);
    if (cen < 0) {
        cen = tsv_col(&s_tsv, "en");
    }
    if (cde < 0 || cen < 0) {
        log_error("i18n: Spalten 'de'/'%s' fehlen in %s", lang, path);
        return false;
    }
    s_cap = 64;
    while (s_cap < s_tsv.row_count * 2 + 1) {
        s_cap *= 2;
    }
    s_keys = (const char **)calloc((size_t)s_cap, sizeof(char *));
    s_vals = (const char **)calloc((size_t)s_cap, sizeof(char *));
    if (!s_keys || !s_vals) {
        return false;
    }
    for (int r = 0; r < s_tsv.row_count; r++) {
        const char *k = tsv_get(&s_tsv, r, cde);
        const char *v = tsv_get(&s_tsv, r, cen);
        if (!*k || !*v) {
            continue;
        }
        u32 i = hash_str(k) & (u32)(s_cap - 1);
        while (s_keys[i]) {
            i = (i + 1) & (u32)(s_cap - 1);
        }
        s_keys[i] = k;
        s_vals[i] = v;
    }
    return true;
}

const char *i18n_current(void) {
    return s_lang;
}

const char *tr(const char *german) {
    if (!german) {
        return "";
    }
    if (!s_loaded || s_cap == 0) {
        return german;
    }
    u32 i = hash_str(german) & (u32)(s_cap - 1);
    while (s_keys[i]) {
        if (strcmp(s_keys[i], german) == 0) {
            return s_vals[i];
        }
        i = (i + 1) & (u32)(s_cap - 1);
    }
    return german;
}
