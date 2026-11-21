#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Erzeugt src/render/font_data.c: Graustufen-Glyphenatlas aus DejaVu Sans.

Aufruf:  python3 tools/gen_font.py [pfad/zur/DejaVuSans.ttf]
DejaVu Fonts: freie Bitstream-Vera-Lizenz (GPL-kompatibel), siehe THIRD_PARTY.md.
Der Atlas wird bei BASE_SIZE Pixeln Zeilenhoehe gerastert; der Renderer skaliert.
"""
import sys
from PIL import Image, ImageDraw, ImageFont

BASE_SIZE = 24
ATLAS_W = 512
PAD = 1
SYMBOLS = "→←↑↓✓×±•◆★▲▼♥♦✦≥≤…–—„“”‚‘’«»€"
TTF = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"

font = ImageFont.truetype(TTF, BASE_SIZE)
codes = list(range(32, 127)) + list(range(160, 256)) + [ord(c) for c in SYMBOLS]
ascent, descent = font.getmetrics()

glyphs = []
x = y = row_h = 0
rects = []
for cp in codes:
    ch = chr(cp)
    adv = font.getlength(ch)
    l, t, r, b = font.getbbox(ch, anchor="ls")
    w, h = max(r - l, 0), max(b - t, 0)
    if w > 0 and cp != 32 and font.getmask(ch).getbbox() is None:
        w = h = 0
    if x + w + PAD > ATLAS_W:
        x, y, row_h = 0, y + row_h + PAD, 0
    rects.append((cp, x, y, w, h, l, t, adv, ch))
    x += w + PAD
    row_h = max(row_h, h)
atlas_h = y + row_h + PAD
img = Image.new("L", (ATLAS_W, atlas_h), 0)
d = ImageDraw.Draw(img)
for cp, gx, gy, w, h, l, t, adv, ch in rects:
    if w and h:
        d.text((gx - l, gy - t), ch, font=font, fill=255, anchor="ls")

out = []
out.append("/* SPDX-License-Identifier: GPL-3.0-or-later */")
out.append("/* GENERIERT von tools/gen_font.py - nicht von Hand aendern.")
out.append(" * Glyphen aus DejaVu Sans (Bitstream-Vera-Lizenz), siehe THIRD_PARTY.md. */")
out.append('#include "render/font.h"')
out.append("")
out.append("const int g_font_base_size = %d;" % BASE_SIZE)
out.append("const int g_font_ascent = %d;" % ascent)
out.append("const int g_font_atlas_w = %d;" % ATLAS_W)
out.append("const int g_font_atlas_h = %d;" % atlas_h)
out.append("const int g_font_glyph_count = %d;" % len(rects))
out.append("")
out.append("const FontGlyph g_font_glyphs[] = {")
for cp, gx, gy, w, h, l, t, adv, ch in rects:
    out.append("    {%d, %d, %d, %d, %d, %d, %d, %.2ff}," % (cp, gx, gy, w, h, l, t, adv))
out.append("};")
out.append("")
out.append("const unsigned char g_font_atlas[] = {")
data = img.tobytes()
for i in range(0, len(data), 24):
    out.append("    " + ",".join(str(v) for v in data[i:i + 24]) + ",")
out.append("};")
open("src/render/font_data.c", "w").write("\n".join(out) + "\n")
print("Glyphen:", len(rects), "Atlas:", ATLAS_W, "x", atlas_h)
