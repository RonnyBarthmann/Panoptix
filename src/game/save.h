/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Savegames: Binaerformat mit Header (Magic "PNX1", Version, Laenge, CRC32),
 * explizite Feld-fuer-Feld-Serialisierung (kein fwrite von Structs => portabel
 * zwischen Windows/Linux, unabhaengig von Padding). Atomar geschrieben
 * (temp-Datei + platform_replace_file), mit Backup (.bak).
 * Wichtig fuer Mitwirkende: neue Felder nur am ENDE der jeweiligen Struktur
 * anhaengen und SAVE_VERSION erhoehen; Altes lesen bleibt in save_read_*.
 */
#ifndef PNX_GAME_SAVE_H
#define PNX_GAME_SAVE_H

#include "game/game.h"

#define SAVE_VERSION 1
#define SAVE_SLOTS 3

/* Pfad der Slot-Datei in out ("<savedir>/slot<N>.sav"). */
bool save_slot_path(int slot, char *out, size_t size);
bool save_exists(int slot);
/* Kurzinfo fuer das Menue (Spielerlevel, Helden, Spielzeit, Zeitstempel). */
typedef struct SaveInfo {
    bool valid;
    int player_level;
    int hero_count;
    double play_time;
    i64 saved_unix;
} SaveInfo;
bool save_peek(int slot, SaveInfo *info);
bool save_game(Game *g, int slot);          /* setzt last_seen_unix, dirty_save=false */
bool load_game(Game *g, int slot);          /* g wird vorher game_shutdown'd und komplett ersetzt */
/* Export/Import als Textdatei (key=value, menschenlesbar) - fuer Bugreports. */
bool save_export_text(const Game *g, const char *path);

#endif
