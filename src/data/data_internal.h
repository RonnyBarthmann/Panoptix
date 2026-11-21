/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Interne Helfer der Datenlader (nur innerhalb src/data/ verwenden). */
#ifndef PNX_DATA_INTERNAL_H
#define PNX_DATA_INTERNAL_H

#include "core/tsv.h"
#include "data/data.h"

/* Fehlerzaehler: jeder Aufruf von data_error() loggt und zaehlt. Am Ende von
 * data_load_all() fuehrt ein Zaehler > 0 zu "false". */
void data_error(const char *file, int row, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;
int data_error_count(void);
void data_error_reset(void);

/* Liest <dir>/<name>; required=false => fehlende Datei nur Warnung. */
bool data_open_tsv(Tsv *t, const char *dir, const char *name, bool required);

/* Namensliste -> Index. Gibt -1 zurueck wenn nicht gefunden. */
int data_enum_index(const char *name, const char *const *names, int count);
/* Wie data_enum_index, meldet bei unbekanntem Namen einen Fehler (leer => fallback). */
int data_parse_enum(const Tsv *t, int row, const char *col, const char *const *names, int count,
                    int fallback);
/* Komma-Liste -> Bitmaske ueber Namensliste (Bit i = names[i]). */
u32 data_parse_flags(const Tsv *t, int row, const char *col, const char *const *names, int count);
/* "#RRGGBB" oder "RRGGBB" oder "0xRRGGBB" -> 0xRRGGBB. */
u32 data_parse_color(const char *s, u32 fallback);
bool data_parse_bool(const char *s);

/* Namenslisten (siehe data_names.c) */
extern const char *const k_tier_ids[TIER_COUNT];
extern const char *const k_class_ids[CLASS_COUNT + 1]; /* + "common" */
extern const char *const k_item_kind_ids[IK_KIND_COUNT];
extern const char *const k_slot_ids[];                 /* weapon..wings (11) */
#define K_SLOT_ID_COUNT 11
extern const char *const k_potion_ids[POTION_TYPE_COUNT];
extern const char *const k_material_ids[MAT_LINE_COUNT];
extern const char *const k_stone_ids[];                /* none, enh_weapon ... magic */
#define K_STONE_ID_COUNT 6

/* Lader der einzelnen Dateien */
bool data_load_balance(const char *dir);
bool data_load_core_tables(const char *dir);   /* tiers, classes, skills */
bool data_load_items(const char *dir);         /* items, legendary, sets */
bool data_load_world(const char *dir);         /* monsters, maps */
bool data_load_meta(const char *dir);          /* shop, traits, quests */

#endif
