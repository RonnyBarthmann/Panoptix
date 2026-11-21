/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Kleine Hilfsfunktionen: sichere Strings, Mathe, Logging.
 * Regel im ganzen Projekt: KEIN strcpy/strcat/sprintf/gets. Nur diese Helfer
 * oder snprintf. */
#ifndef PNX_CORE_UTIL_H
#define PNX_CORE_UTIL_H

#include <math.h>

#include "core/types.h"

/* ---- Strings ---------------------------------------------------------- */
/* Kopiert src nach dst (immer nullterminiert, schneidet ab). */
void str_copy(char *dst, size_t dst_size, const char *src);
/* Haengt src an dst an (immer nullterminiert, schneidet ab). */
void str_append(char *dst, size_t dst_size, const char *src);
/* printf-artig mit Laengenpruefung; gibt dst zurueck. */
char *str_format(char *dst, size_t dst_size, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;
/* Gross-/Kleinschreibung ignorierender Vergleich (ASCII). */
int str_icmp(const char *a, const char *b);
/* Entfernt fuehrende/nachfolgende Leerzeichen in-place. */
void str_trim(char *s);
/* Zahl mit Tausendertrennzeichen ("1.234.567") in dst. */
char *str_format_number(char *dst, size_t dst_size, i64 value);
/* Kompakte Zahl ("1.2k", "3.4M", "5.6B") fuer enge UI-Stellen. */
char *str_format_compact(char *dst, size_t dst_size, double value);
/* Dauer als "1h 02m 03s" bzw. "02m 03s" bzw. "45s". */
char *str_format_duration(char *dst, size_t dst_size, double seconds);

/* ---- Mathe ------------------------------------------------------------ */
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float minf(float a, float b) { return a < b ? a : b; }
static inline float maxf(float a, float b) { return a > b ? a : b; }
static inline int mini(int a, int b) { return a < b ? a : b; }
static inline int maxi(int a, int b) { return a > b ? a : b; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline Vec2 v2(float x, float y) { Vec2 v = {x, y}; return v; }
static inline Vec2 v2_add(Vec2 a, Vec2 b) { return v2(a.x + b.x, a.y + b.y); }
static inline Vec2 v2_sub(Vec2 a, Vec2 b) { return v2(a.x - b.x, a.y - b.y); }
static inline Vec2 v2_scale(Vec2 a, float s) { return v2(a.x * s, a.y * s); }
static inline float v2_dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
static inline float v2_len(Vec2 a) { return sqrtf(a.x * a.x + a.y * a.y); }
static inline float v2_dist(Vec2 a, Vec2 b) { return v2_len(v2_sub(a, b)); }
static inline float v2_dist2(Vec2 a, Vec2 b) {
    float dx = a.x - b.x, dy = a.y - b.y;
    return dx * dx + dy * dy;
}
/* Einheitsvektor; bei Nulllaenge (1,0). */
static inline Vec2 v2_norm(Vec2 a) {
    float l = v2_len(a);
    return l > 1e-6f ? v2(a.x / l, a.y / l) : v2(1.0f, 0.0f);
}
static inline Vec2 v2_rotate(Vec2 a, float rad) {
    float c = cosf(rad), s = sinf(rad);
    return v2(a.x * c - a.y * s, a.x * s + a.y * c);
}
static inline bool rect_contains(Rect r, int x, int y) {
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}
static inline Rect rect_make(int x, int y, int w, int h) { Rect r = {x, y, w, h}; return r; }
/* Rect um 'pad' Pixel verkleinern (negativ = vergroessern). */
static inline Rect rect_inset(Rect r, int pad) {
    return rect_make(r.x + pad, r.y + pad, r.w - 2 * pad, r.h - 2 * pad);
}
static inline Color color_from_rgb(u32 rgb, u8 a) {
    Color c = {(u8)((rgb >> 16) & 0xFF), (u8)((rgb >> 8) & 0xFF), (u8)(rgb & 0xFF), a};
    return c;
}
/* Mischt zwei Farben (t=0 -> a, t=1 -> b). */
Color color_mix(Color a, Color b, float t);

#define PNX_PI 3.14159265358979f
#define ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* ---- Logging ---------------------------------------------------------- */
void log_info(const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
void log_warn(const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
void log_error(const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

/* ---- CRC32 (Savegame-Pruefsumme) -------------------------------------- */
u32 crc32_update(u32 crc, const void *data, size_t len);

#endif
