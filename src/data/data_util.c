/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Gemeinsame Helfer der Datenlader: Fehlerzaehler, Enum-/Flag-/Farb-Parser. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/util.h"
#include "data/data_internal.h"
#include "platform/platform.h"

/* Portabler strtok_r-Ersatz (C11 hat keins). */
static char *strtok_r_compat(char *s, const char *delim, char **save) {
    if (!s) {
        s = *save;
    }
    if (!s) {
        return NULL;
    }
    s += strspn(s, delim);
    if (!*s) {
        *save = NULL;
        return NULL;
    }
    char *end = s + strcspn(s, delim);
    if (*end) {
        *end = '\0';
        *save = end + 1;
    } else {
        *save = NULL;
    }
    return s;
}

static int s_errors = 0;

void data_error(const char *file, int row, const char *fmt, ...) {
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    if (row >= 0) {
        log_error("Daten %s (Datensatz %d): %s", file, row + 1, msg);
    } else {
        log_error("Daten %s: %s", file, msg);
    }
    s_errors++;
}

int data_error_count(void) {
    return s_errors;
}

void data_error_reset(void) {
    s_errors = 0;
}

bool data_open_tsv(Tsv *t, const char *dir, const char *name, bool required) {
    char path[600];
    str_format(path, sizeof path, "%s/%s", dir, name);
    if (!platform_file_exists(path)) {
        if (required) {
            data_error(name, -1, "Datei fehlt (%s)", path);
        } else {
            log_warn("Daten: optionale Datei %s fehlt", path);
        }
        return false;
    }
    if (!tsv_load(t, path)) {
        data_error(name, -1, "nicht lesbar");
        return false;
    }
    return true;
}

int data_enum_index(const char *name, const char *const *names, int count) {
    for (int i = 0; i < count; i++) {
        if (names[i] && str_icmp(names[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

int data_parse_enum(const Tsv *t, int row, const char *col, const char *const *names, int count,
                    int fallback) {
    const char *s = tsv_get_by_name(t, row, col);
    if (!*s) {
        return fallback;
    }
    int i = data_enum_index(s, names, count);
    if (i < 0) {
        data_error(t->path, row, "Spalte '%s': unbekannter Wert '%s'", col, s);
        return fallback;
    }
    return i;
}

u32 data_parse_flags(const Tsv *t, int row, const char *col, const char *const *names, int count) {
    const char *s = tsv_get_by_name(t, row, col);
    u32 mask = 0;
    char buf[200];
    str_copy(buf, sizeof buf, s);
    char *save = NULL;
    for (char *tok = strtok_r_compat(buf, ",", &save); tok; tok = strtok_r_compat(NULL, ",", &save)) {
        str_trim(tok);
        if (!*tok) {
            continue;
        }
        int i = data_enum_index(tok, names, count);
        if (i < 0) {
            data_error(t->path, row, "Spalte '%s': unbekannter Wert '%s'", col, tok);
        } else {
            mask |= 1u << i;
        }
    }
    return mask;
}

u32 data_parse_color(const char *s, u32 fallback) {
    if (!s || !*s) {
        return fallback;
    }
    if (*s == '#') {
        s++;
    } else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 16);
    if (end == s) {
        return fallback;
    }
    return (u32)(v & 0xFFFFFFu);
}

bool data_parse_bool(const char *s) {
    return s && (s[0] == '1' || s[0] == 'y' || s[0] == 'Y' || s[0] == 't' || s[0] == 'T' || s[0] == 'j' ||
                 s[0] == 'J');
}
