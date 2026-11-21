/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Lokalisierung nach dem gettext-Prinzip: Quelltext und Daten sind DEUTSCH.
 * tr("Deutscher Text") liefert in der Sprache "en" die Uebersetzung aus
 * data/lang/en.tsv (Spalten: de, en) und sonst den Originaltext. */
#ifndef PNX_CORE_I18N_H
#define PNX_CORE_I18N_H

#include "core/types.h"

/* lang: "de" oder "en" (oder weitere Dateien data/lang/<lang>.tsv). */
bool i18n_load(const char *data_dir, const char *lang);
void i18n_shutdown(void);
const char *i18n_current(void);
/* Gibt NIE NULL zurueck. Der Zeiger bleibt bis zum naechsten i18n_load gueltig. */
const char *tr(const char *german);

#endif
