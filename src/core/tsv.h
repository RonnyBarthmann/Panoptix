/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Minimaler Leser fuer TSV-Datendateien (Tab-getrennt, UTF-8).
 *
 * Format (siehe docs/DATA_FORMAT.md):
 *   - Zeilen, die mit '#' beginnen, und leere Zeilen werden ignoriert.
 *   - Die erste verbleibende Zeile ist die Kopfzeile mit den Spaltennamen.
 *   - Jede weitere Zeile ist ein Datensatz; Felder sind durch TAB getrennt.
 *   - Fehlende Felder am Zeilenende zaehlen als leer.
 * Spalten werden ueber ihren NAMEN gefunden, nicht ueber die Position - so
 * koennen Mitwirkende Spalten ergaenzen, ohne den Lader zu brechen.
 */
#ifndef PNX_CORE_TSV_H
#define PNX_CORE_TSV_H

#include "core/types.h"

typedef struct Tsv {
    char *buffer;     /* gesamter Dateiinhalt (Felder sind darin nullterminiert) */
    int col_count;
    int row_count;    /* Datensaetze ohne Kopfzeile */
    char **col_names; /* [col_count] */
    char **cells;     /* [row_count * col_count] */
    char path[260];   /* fuer Fehlermeldungen */
} Tsv;

/* Laedt die Datei. false bei Fehler (Fehler wird geloggt). */
bool tsv_load(Tsv *t, const char *path);
void tsv_free(Tsv *t);
/* Index der Spalte oder -1. */
int tsv_col(const Tsv *t, const char *name);
/* Zellentext; "" wenn Zeile/Spalte ungueltig (nie NULL). */
const char *tsv_get(const Tsv *t, int row, int col);
const char *tsv_get_by_name(const Tsv *t, int row, const char *col_name);
int tsv_get_int(const Tsv *t, int row, const char *col_name, int fallback);
float tsv_get_float(const Tsv *t, int row, const char *col_name, float fallback);

#endif
