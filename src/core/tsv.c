/* SPDX-License-Identifier: GPL-3.0-or-later */
/* TSV-Leser (siehe tsv.h und docs/DATA_FORMAT.md). */
#include "core/tsv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/util.h"

/* Liest eine ganze Datei in einen malloc'ten, nullterminierten Puffer. */
static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = '\0';
    *out_len = got;
    return buf;
}

/* Zerlegt eine Zeile in Felder (ersetzt TABs durch '\0'); liefert Anzahl. */
static int split_fields(char *line, char **fields, int max) {
    int n = 0;
    char *p = line;
    while (n < max) {
        fields[n++] = p;
        char *tab = strchr(p, '\t');
        if (!tab) {
            break;
        }
        *tab = '\0';
        p = tab + 1;
    }
    return n;
}

bool tsv_load(Tsv *t, const char *path) {
    memset(t, 0, sizeof *t);
    str_copy(t->path, sizeof t->path, path);
    size_t len = 0;
    char *buf = read_file(path, &len);
    if (!buf) {
        log_error("TSV: kann '%s' nicht lesen", path);
        return false;
    }
    t->buffer = buf;

    /* Zeilen zaehlen (obere Schranke) */
    int max_lines = 1;
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == '\n') {
            max_lines++;
        }
    }
    char **lines = (char **)malloc(sizeof(char *) * (size_t)max_lines);
    if (!lines) {
        tsv_free(t);
        return false;
    }
    int line_count = 0;
    char *p = buf;
    /* UTF-8-BOM ueberspringen */
    if ((u8)p[0] == 0xEF && (u8)p[1] == 0xBB && (u8)p[2] == 0xBF) {
        p += 3;
    }
    while (*p) {
        char *eol = strchr(p, '\n');
        char *next = eol ? eol + 1 : p + strlen(p);
        if (eol) {
            *eol = '\0';
        }
        size_t l = strlen(p);
        if (l > 0 && p[l - 1] == '\r') {
            p[l - 1] = '\0';
        }
        /* Kommentare und Leerzeilen weglassen */
        if (p[0] != '#' && p[0] != '\0') {
            lines[line_count++] = p;
        }
        p = next;
    }
    if (line_count == 0) {
        free(lines);
        log_error("TSV: '%s' hat keine Kopfzeile", path);
        tsv_free(t);
        return false;
    }

    /* Kopfzeile */
    int max_cols = 1;
    for (char *c = lines[0]; *c; c++) {
        if (*c == '\t') {
            max_cols++;
        }
    }
    t->col_count = max_cols;
    t->col_names = (char **)calloc((size_t)max_cols, sizeof(char *));
    if (!t->col_names) {
        free(lines);
        tsv_free(t);
        return false;
    }
    split_fields(lines[0], t->col_names, max_cols);
    for (int i = 0; i < max_cols; i++) {
        str_trim(t->col_names[i]);
    }

    /* Datensaetze */
    t->row_count = line_count - 1;
    t->cells = (char **)calloc((size_t)(t->row_count > 0 ? t->row_count : 1) * (size_t)max_cols,
                               sizeof(char *));
    if (!t->cells) {
        free(lines);
        tsv_free(t);
        return false;
    }
    static char empty[1] = "";
    for (int r = 0; r < t->row_count; r++) {
        char **row = t->cells + (size_t)r * (size_t)max_cols;
        int n = split_fields(lines[r + 1], row, max_cols);
        for (int c = 0; c < n; c++) {
            str_trim(row[c]);
        }
        for (int c = n; c < max_cols; c++) {
            row[c] = empty;
        }
    }
    free(lines);
    return true;
}

void tsv_free(Tsv *t) {
    free(t->buffer);
    free(t->col_names);
    free(t->cells);
    memset(t, 0, sizeof *t);
}

int tsv_col(const Tsv *t, const char *name) {
    for (int i = 0; i < t->col_count; i++) {
        if (strcmp(t->col_names[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

const char *tsv_get(const Tsv *t, int row, int col) {
    if (row < 0 || row >= t->row_count || col < 0 || col >= t->col_count) {
        return "";
    }
    return t->cells[(size_t)row * (size_t)t->col_count + (size_t)col];
}

const char *tsv_get_by_name(const Tsv *t, int row, const char *col_name) {
    return tsv_get(t, row, tsv_col(t, col_name));
}

int tsv_get_int(const Tsv *t, int row, const char *col_name, int fallback) {
    const char *s = tsv_get_by_name(t, row, col_name);
    if (!*s) {
        return fallback;
    }
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s) {
        return fallback;
    }
    return (int)v;
}

float tsv_get_float(const Tsv *t, int row, const char *col_name, float fallback) {
    const char *s = tsv_get_by_name(t, row, col_name);
    if (!*s) {
        return fallback;
    }
    char *end = NULL;
    double v = strtod(s, &end);
    if (end == s) {
        return fallback;
    }
    return (float)v;
}
