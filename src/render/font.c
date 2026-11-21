/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Glyphen-Lookup und UTF-8-Dekodierung fuer den eingebetteten Atlas. */
#include "render/font.h"

const FontGlyph *font_find_glyph(int codepoint) {
    /* Binaere Suche: Glyphen sind nach Codepoint aufsteigend, Symbole dahinter ungeordnet,
     * daher zuerst Bereich 32..255 direkt, sonst linear. */
    if (codepoint >= 32 && codepoint <= 126) {
        return &g_font_glyphs[codepoint - 32];
    }
    for (int i = 0; i < g_font_glyph_count; i++) {
        if (g_font_glyphs[i].codepoint == codepoint) {
            return &g_font_glyphs[i];
        }
    }
    return 0;
}

int font_next_codepoint(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    int cp = p[0];
    int len = 1;
    if (cp == 0) {
        return 0;
    }
    if (cp >= 0xF0) {
        cp &= 0x07;
        len = 4;
    } else if (cp >= 0xE0) {
        cp &= 0x0F;
        len = 3;
    } else if (cp >= 0xC0) {
        cp &= 0x1F;
        len = 2;
    }
    for (int i = 1; i < len; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            *s += i;
            return '?';
        }
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    *s += len;
    return cp;
}
