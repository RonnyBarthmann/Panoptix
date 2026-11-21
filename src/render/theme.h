/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Farbpalette im Pergament-Stil (Spec 19). Tier-Farben stehen in g_tiers[]. */
#ifndef PNX_RENDER_THEME_H
#define PNX_RENDER_THEME_H

#include "core/types.h"

#define RGB(r, g, b) ((Color){(r), (g), (b), 255})
#define RGBA(r, g, b, a) ((Color){(r), (g), (b), (a)})

#define COL_PARCHMENT RGB(0xE9, 0xDD, 0xBF)
#define COL_PARCHMENT_DARK RGB(0xCD, 0xBB, 0x92)
#define COL_PARCHMENT_LIGHT RGB(0xF4, 0xEC, 0xD6)
#define COL_INK RGB(0x2B, 0x21, 0x18)
#define COL_INK_SOFT RGB(0x5A, 0x4A, 0x3A)
#define COL_WOOD RGB(0x6B, 0x4A, 0x2F)
#define COL_WOOD_DARK RGB(0x45, 0x2F, 0x1D)
#define COL_WAX_RED RGB(0x9E, 0x2B, 0x25)
#define COL_MOSS RGB(0x5C, 0x7A, 0x3A)
#define COL_SKY RGB(0x5E, 0x8C, 0xA8)
#define COL_GOLD RGB(0xC7, 0x9B, 0x2D)
#define COL_WHITE RGB(255, 255, 255)
#define COL_BLACK RGB(0, 0, 0)
/* Regel R5 (Spec 02): Tierfarben nie fuer andere Zwecke */
#define COL_DMG RGB(255, 255, 255)
#define COL_DMG_CRIT_EDGE RGB(0xFF, 0xD7, 0x4A)
#define COL_HEAL RGB(0x4C, 0xD9, 0x64)
#define COL_MANA RGB(0x2F, 0xD6, 0xD0)
#define COL_POISON RGB(0x9B, 0x59, 0xB6)
#define COL_HP RGB(0xC0, 0x39, 0x2B)
#define COL_HP_DARK RGB(0x5E, 0x1A, 0x14)
#define COL_MANA_BAR RGB(0x2E, 0x86, 0xC1)
#define COL_EXP RGB(0xD4, 0xAC, 0x0D)

#endif
