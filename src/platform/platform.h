/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Plattform-Abstraktion. Die Implementierungen liegen getrennt in
 *   src/platform/linux/platform_linux.c
 *   src/platform/windows/platform_windows.c
 * Das Makefile waehlt anhand von PLATFORM genau eine aus. Alle anderen
 * Module duerfen KEINE plattformspezifischen Header (unistd.h, windows.h)
 * einbinden, sondern muessen diese Schnittstelle nutzen. */
#ifndef PNX_PLATFORM_H
#define PNX_PLATFORM_H

#include "core/types.h"

/* Verzeichnis fuer Savegames/Einstellungen (wird angelegt). Linux:
 * $XDG_DATA_HOME/panoptix bzw. ~/.local/share/panoptix; Windows:
 * %APPDATA%\Panoptix. Endet OHNE Trennzeichen. */
bool platform_get_save_dir(char *out, size_t size);
/* Verzeichnis mit den Spieldaten (data/, assets/). Suchreihenfolge:
 * 1. Umgebungsvariable PANOPTIX_DATA  2. <exe-dir>/data  3. ./data
 * 4. Linux: PNX_DATADIR (Makefile-Define, z.B. /usr/share/panoptix) */
bool platform_get_data_dir(char *out, size_t size);
/* Legt ein Verzeichnis an (inkl. Elternverzeichnisse). true wenn danach vorhanden. */
bool platform_make_dir(const char *path);
bool platform_file_exists(const char *path);
/* Ersetzt 'dest' atomar durch 'src' (rename; unter Windows MoveFileEx). */
bool platform_replace_file(const char *src, const char *dest);
/* Sekunden seit 1970-01-01 UTC. */
i64 platform_unix_time(void);
/* Lokales Datum: Jahr, Monat 1..12, Tag 1..31, Wochentag 0=Mo..6=So, Stunde. */
void platform_local_date(int *year, int *month, int *day, int *weekday, int *hour);
/* Monotone Zeit in Sekunden (fuer Frame-Timing). */
double platform_monotonic_seconds(void);

#endif
