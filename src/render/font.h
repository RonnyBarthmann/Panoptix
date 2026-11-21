/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Interner Glyphenatlas (erzeugt von tools/gen_font.py). Nur von gfx.c/font.c genutzt. */
#ifndef PNX_RENDER_FONT_H
#define PNX_RENDER_FONT_H

typedef struct FontGlyph {
    int codepoint;
    int x, y, w, h;   /* Position im Atlas */
    int xoff, yoff;   /* Versatz relativ zur Grundlinie (yoff negativ = ueber der Linie) */
    float advance;
} FontGlyph;

extern const int g_font_base_size, g_font_ascent, g_font_atlas_w, g_font_atlas_h, g_font_glyph_count;
extern const FontGlyph g_font_glyphs[];
extern const unsigned char g_font_atlas[];

/* Glyphe per Codepoint (NULL wenn nicht vorhanden -> Aufrufer nimmt '?'). */
const FontGlyph *font_find_glyph(int codepoint);
/* Liest ein UTF-8-Zeichen, gibt Codepoint zurueck und rueckt *s vor. */
int font_next_codepoint(const char **s);

#endif
