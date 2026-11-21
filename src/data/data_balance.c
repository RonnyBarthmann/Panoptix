/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Laden von balance.cfg (Zeilen "schluessel = wert"). Defaults stehen im X-Makro. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/util.h"
#include "data/data_internal.h"

static bool set_balance(const char *key, double value) {
#define BAL_SET(name, def)                                                                        \
    if (strcmp(key, #name) == 0) {                                                                \
        g_bal.name = value;                                                                       \
        return true;                                                                              \
    }
    BALANCE_FIELDS(BAL_SET)
#undef BAL_SET
    return false;
}

bool data_load_balance(const char *dir) {
    /* Standardwerte */
#define BAL_DEFAULT(name, def) g_bal.name = (def);
    BALANCE_FIELDS(BAL_DEFAULT)
#undef BAL_DEFAULT

    char path[600];
    str_format(path, sizeof path, "%s/balance.cfg", dir);
    FILE *f = fopen(path, "r");
    if (!f) {
        log_warn("Daten: balance.cfg fehlt, verwende Standardwerte");
        return true;
    }
    char line[256];
    int lineno = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *hash = strchr(line, '#');
        if (hash) {
            *hash = '\0';
        }
        str_trim(line);
        if (!*line) {
            continue;
        }
        char *eq = strchr(line, '=');
        if (!eq) {
            data_error("balance.cfg", lineno - 1, "ungueltige Zeile '%s'", line);
            continue;
        }
        *eq = '\0';
        char *key = line;
        str_trim(key);
        char *val = eq + 1;
        str_trim(val);
        if (!set_balance(key, atof(val))) {
            log_warn("balance.cfg Zeile %d: unbekannter Schluessel '%s'", lineno, key);
        }
    }
    fclose(f);
    return true;
}
