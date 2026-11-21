/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Hilfsfunktionen: sichere Strings, Zahlenformatierung, CRC32, Farben. */
#include "core/util.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void str_copy(char *dst, size_t dst_size, const char *src) {
    if (dst_size == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t n = strlen(src);
    if (n >= dst_size) {
        n = dst_size - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void str_append(char *dst, size_t dst_size, const char *src) {
    if (dst_size == 0 || !src) {
        return;
    }
    size_t len = strlen(dst);
    if (len >= dst_size) {
        dst[dst_size - 1] = '\0';
        return;
    }
    str_copy(dst + len, dst_size - len, src);
}

char *str_format(char *dst, size_t dst_size, const char *fmt, ...) {
    if (dst_size == 0) {
        return dst;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(dst, dst_size, fmt, ap);
    va_end(ap);
    dst[dst_size - 1] = '\0';
    return dst;
}

int str_icmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) {
            return ca - cb;
        }
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

void str_trim(char *s) {
    size_t len = strlen(s);
    size_t start = 0;
    while (start < len && isspace((unsigned char)s[start])) {
        start++;
    }
    while (len > start && isspace((unsigned char)s[len - 1])) {
        len--;
    }
    memmove(s, s + start, len - start);
    s[len - start] = '\0';
}

char *str_format_number(char *dst, size_t dst_size, i64 value) {
    char digits[32];
    bool neg = value < 0;
    unsigned long long mag = neg ? (unsigned long long)(-(value + 1)) + 1ULL : (unsigned long long)value;
    snprintf(digits, sizeof digits, "%llu", mag);
    size_t n = strlen(digits);
    char out[48];
    size_t o = 0;
    if (neg) {
        out[o++] = '-';
    }
    for (size_t i = 0; i < n; i++) {
        out[o++] = digits[i];
        size_t rest = n - i - 1;
        if (rest > 0 && rest % 3 == 0) {
            out[o++] = '.';
        }
    }
    out[o] = '\0';
    str_copy(dst, dst_size, out);
    return dst;
}

char *str_format_compact(char *dst, size_t dst_size, double value) {
    double a = value < 0 ? -value : value;
    if (a >= 1e12) {
        str_format(dst, dst_size, "%.1fT", value / 1e12);
    } else if (a >= 1e9) {
        str_format(dst, dst_size, "%.1fB", value / 1e9);
    } else if (a >= 1e6) {
        str_format(dst, dst_size, "%.1fM", value / 1e6);
    } else if (a >= 1e4) {
        str_format(dst, dst_size, "%.1fk", value / 1e3);
    } else {
        str_format(dst, dst_size, "%.0f", value);
    }
    return dst;
}

char *str_format_duration(char *dst, size_t dst_size, double seconds) {
    if (seconds < 0) {
        seconds = 0;
    }
    long total = (long)seconds;
    long h = total / 3600;
    long m = (total % 3600) / 60;
    long s = total % 60;
    if (h > 0) {
        str_format(dst, dst_size, "%ldh %02ldm %02lds", h, m, s);
    } else if (m > 0) {
        str_format(dst, dst_size, "%02ldm %02lds", m, s);
    } else {
        str_format(dst, dst_size, "%lds", s);
    }
    return dst;
}

Color color_mix(Color a, Color b, float t) {
    t = clampf(t, 0.0f, 1.0f);
    Color c;
    c.r = (u8)(a.r + (b.r - a.r) * t);
    c.g = (u8)(a.g + (b.g - a.g) * t);
    c.b = (u8)(a.b + (b.b - a.b) * t);
    c.a = (u8)(a.a + (b.a - a.a) * t);
    return c;
}

u32 crc32_update(u32 crc, const void *data, size_t len) {
    static u32 table[256];
    static bool ready = false;
    if (!ready) {
        for (u32 i = 0; i < 256; i++) {
            u32 c = i;
            for (int k = 0; k < 8; k++) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        ready = true;
    }
    const u8 *p = (const u8 *)data;
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc = table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    }
    return ~crc;
}
