/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Prozedurale Icons (Platzhalter bis echte Atlas-Grafiken existieren).
 * Aufbau: Tierfarben-Hintergrund + neutral gefaerbtes Motiv mit Tinten-Umriss.
 * Alle Motive zeichnen in normierten Koordinaten (0..1) innerhalb eines Rects. */
#include "render/icons.h"

#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/util.h"
#include "data/data.h"
#include "render/theme.h"

/* ------------------------------------------------------------------------ */
/* Zeichenhelfer in normierten Koordinaten                                    */
/* ------------------------------------------------------------------------ */

typedef struct Pen {
    Gfx *g;
    Rect r;
    Color fill;   /* Fuellfarbe des Motivs */
    Color ink;    /* Umrissfarbe */
    float th;     /* Umrissstaerke in px */
} Pen;

static Vec2 pt(const Pen *p, float u, float v) {
    return v2((float)p->r.x + u * (float)p->r.w, (float)p->r.y + v * (float)p->r.h);
}
static float sz(const Pen *p, float u) { return u * (float)p->r.w; }

/* Konvexes Polygon fuellen + Umriss. */
static void poly(const Pen *p, const float (*uv)[2], int n, Color fill) {
    Vec2 pts[16];
    for (int i = 0; i < n && i < 16; i++) {
        pts[i] = pt(p, uv[i][0], uv[i][1]);
    }
    gfx_fill_poly(p->g, pts, n, fill);
    for (int i = 0; i < n; i++) {
        Vec2 a = pts[i], b = pts[(i + 1) % n];
        gfx_line(p->g, a.x, a.y, b.x, b.y, p->ink, p->th);
    }
}

static void line(const Pen *p, float u1, float v1, float u2, float v2, Color c, float thick_u) {
    Vec2 a = pt(p, u1, v1), b = pt(p, u2, v2);
    gfx_line(p->g, a.x, a.y, b.x, b.y, c, maxf(1.0f, sz(p, thick_u)));
}

static void box(const Pen *p, float u, float v, float w, float h, Color fill) {
    Rect r = rect_make((int)((float)p->r.x + u * (float)p->r.w), (int)((float)p->r.y + v * (float)p->r.h),
                       maxi(1, (int)(w * (float)p->r.w)), maxi(1, (int)(h * (float)p->r.h)));
    gfx_fill_rect(p->g, r, fill);
    gfx_draw_rect(p->g, r, p->ink, maxi(1, (int)p->th));
}

static void disc(const Pen *p, float u, float v, float rad, Color fill) {
    Vec2 c = pt(p, u, v);
    gfx_fill_circle(p->g, c.x, c.y, sz(p, rad), fill);
    gfx_draw_circle(p->g, c.x, c.y, sz(p, rad), p->ink, p->th);
}

/* ------------------------------------------------------------------------ */
/* Motive                                                                     */
/* ------------------------------------------------------------------------ */

static void motif_weapon(const Pen *p, int cls) {
    Color steel = RGB(0xD8, 0xDC, 0xE0), wood = RGB(0x9A, 0x6B, 0x3A);
    switch (cls) {
    case CLASS_KNIGHT: {
        static const float blade[5][2] = {{0.5f, 0.06f}, {0.6f, 0.2f}, {0.6f, 0.64f}, {0.4f, 0.64f}, {0.4f, 0.2f}};
        poly(p, blade, 5, steel);
        line(p, 0.28f, 0.68f, 0.72f, 0.68f, p->ink, 0.09f);
        line(p, 0.5f, 0.7f, 0.5f, 0.88f, wood, 0.07f);
        disc(p, 0.5f, 0.92f, 0.06f, p->fill);
        break;
    }
    case CLASS_WARRIOR: {
        line(p, 0.32f, 0.92f, 0.62f, 0.12f, wood, 0.08f);
        static const float head[4][2] = {{0.56f, 0.12f}, {0.9f, 0.22f}, {0.82f, 0.52f}, {0.5f, 0.38f}};
        poly(p, head, 4, steel);
        break;
    }
    case CLASS_WIZARD:
        line(p, 0.3f, 0.94f, 0.64f, 0.26f, wood, 0.08f);
        disc(p, 0.68f, 0.2f, 0.13f, RGB(0xB0, 0x90, 0xE0));
        break;
    case CLASS_ARCHER: {
        Vec2 c = pt(p, 0.3f, 0.5f);
        gfx_arc(p->g, c.x, c.y, sz(p, 0.42f), -65.0f, 65.0f, wood, maxf(2.0f, sz(p, 0.09f)));
        Vec2 a = pt(p, 0.3f + 0.42f * cosf(65.0f * PNX_PI / 180.0f), 0.5f - 0.42f * sinf(65.0f * PNX_PI / 180.0f));
        Vec2 b = pt(p, 0.3f + 0.42f * cosf(65.0f * PNX_PI / 180.0f), 0.5f + 0.42f * sinf(65.0f * PNX_PI / 180.0f));
        gfx_line(p->g, a.x, a.y, b.x, b.y, p->ink, 1.0f);
        line(p, 0.3f, 0.5f, 0.92f, 0.5f, p->ink, 0.03f);
        break;
    }
    case CLASS_ASSASSIN: {
        static const float blade[3][2] = {{0.5f, 0.1f}, {0.62f, 0.55f}, {0.38f, 0.55f}};
        poly(p, blade, 3, steel);
        line(p, 0.3f, 0.58f, 0.7f, 0.58f, p->ink, 0.07f);
        line(p, 0.5f, 0.6f, 0.5f, 0.86f, RGB(0x4A, 0x2E, 0x2E), 0.08f);
        break;
    }
    case CLASS_SHAMAN:
        line(p, 0.5f, 0.94f, 0.5f, 0.3f, wood, 0.08f);
        disc(p, 0.5f, 0.22f, 0.13f, RGB(0xC8, 0xB0, 0x80));
        line(p, 0.3f, 0.16f, 0.2f, 0.06f, RGB(0xE0, 0xE0, 0xE0), 0.04f);
        line(p, 0.7f, 0.16f, 0.8f, 0.06f, RGB(0xE0, 0xE0, 0xE0), 0.04f);
        break;
    default: /* Priester: Zepter */
        line(p, 0.5f, 0.94f, 0.5f, 0.3f, RGB(0xD8, 0xB8, 0x50), 0.08f);
        line(p, 0.5f, 0.06f, 0.5f, 0.34f, RGB(0xF4, 0xE0, 0x80), 0.09f);
        line(p, 0.36f, 0.18f, 0.64f, 0.18f, RGB(0xF4, 0xE0, 0x80), 0.09f);
        break;
    }
}

static void motif_sub(const Pen *p, int cls) {
    Color wood = RGB(0x9A, 0x6B, 0x3A);
    switch (cls) {
    case CLASS_KNIGHT: {
        static const float sh[5][2] = {{0.2f, 0.14f}, {0.8f, 0.14f}, {0.8f, 0.56f}, {0.5f, 0.92f}, {0.2f, 0.56f}};
        poly(p, sh, 5, RGB(0xC8, 0xCC, 0xD0));
        line(p, 0.5f, 0.2f, 0.5f, 0.8f, p->ink, 0.05f);
        line(p, 0.26f, 0.4f, 0.74f, 0.4f, p->ink, 0.05f);
        break;
    }
    case CLASS_WARRIOR: {
        line(p, 0.28f, 0.94f, 0.28f, 0.08f, wood, 0.06f);
        static const float fl[3][2] = {{0.3f, 0.1f}, {0.86f, 0.28f}, {0.3f, 0.5f}};
        poly(p, fl, 3, RGB(0xC0, 0x50, 0x50));
        break;
    }
    case CLASS_WIZARD:
        disc(p, 0.5f, 0.5f, 0.32f, RGB(0xA8, 0xC8, 0xF0));
        disc(p, 0.4f, 0.4f, 0.08f, RGB(0xFF, 0xFF, 0xFF));
        break;
    case CLASS_ARCHER:
        box(p, 0.35f, 0.3f, 0.3f, 0.62f, wood);
        line(p, 0.42f, 0.3f, 0.4f, 0.08f, p->ink, 0.04f);
        line(p, 0.5f, 0.3f, 0.5f, 0.06f, p->ink, 0.04f);
        line(p, 0.58f, 0.3f, 0.6f, 0.08f, p->ink, 0.04f);
        break;
    case CLASS_ASSASSIN:
        box(p, 0.1f, 0.5f, 0.8f, 0.18f, wood);
        for (int i = 0; i < 3; i++) {
            float u = 0.25f + 0.25f * (float)i;
            const float k[3][2] = {{u, 0.2f}, {u + 0.06f, 0.5f}, {u - 0.06f, 0.5f}};
            poly(p, k, 3, RGB(0xD8, 0xDC, 0xE0));
        }
        break;
    case CLASS_SHAMAN:
        box(p, 0.3f, 0.7f, 0.4f, 0.22f, wood);
        box(p, 0.34f, 0.46f, 0.32f, 0.22f, RGB(0xC8, 0xB0, 0x80));
        box(p, 0.38f, 0.22f, 0.24f, 0.22f, RGB(0xA0, 0x80, 0x50));
        break;
    default:
        box(p, 0.2f, 0.12f, 0.6f, 0.78f, RGB(0x8A, 0x3A, 0x30));
        line(p, 0.5f, 0.25f, 0.5f, 0.7f, RGB(0xF4, 0xE0, 0x80), 0.06f);
        line(p, 0.38f, 0.4f, 0.62f, 0.4f, RGB(0xF4, 0xE0, 0x80), 0.06f);
        break;
    }
}

static void motif_armor(const Pen *p, int slot) {
    Color metal = RGB(0xC8, 0xCC, 0xD0);
    switch (slot) {
    case SLOT_HELM: {
        Vec2 c = pt(p, 0.5f, 0.55f);
        gfx_fill_pie(p->g, c.x, c.y, sz(p, 0.36f), 180.0f, 360.0f, metal);
        gfx_arc(p->g, c.x, c.y, sz(p, 0.36f), 180.0f, 360.0f, p->ink, p->th);
        box(p, 0.14f, 0.55f, 0.72f, 0.14f, metal);
        box(p, 0.44f, 0.55f, 0.12f, 0.3f, metal);
        break;
    }
    case SLOT_CHEST: {
        box(p, 0.28f, 0.28f, 0.44f, 0.6f, metal);
        static const float l[4][2] = {{0.28f, 0.16f}, {0.1f, 0.3f}, {0.18f, 0.5f}, {0.28f, 0.42f}};
        static const float r[4][2] = {{0.72f, 0.16f}, {0.9f, 0.3f}, {0.82f, 0.5f}, {0.72f, 0.42f}};
        poly(p, l, 4, metal);
        poly(p, r, 4, metal);
        box(p, 0.28f, 0.16f, 0.44f, 0.14f, metal);
        break;
    }
    case SLOT_GLOVES:
        box(p, 0.28f, 0.45f, 0.44f, 0.4f, metal);
        for (int i = 0; i < 4; i++) {
            box(p, 0.28f + 0.11f * (float)i, 0.12f + 0.03f * (float)(i == 1 || i == 2 ? 0 : 3), 0.1f, 0.34f, metal);
        }
        box(p, 0.12f, 0.5f, 0.16f, 0.1f, metal);
        break;
    default: /* Stiefel */
        box(p, 0.3f, 0.12f, 0.28f, 0.52f, metal);
        box(p, 0.3f, 0.6f, 0.52f, 0.24f, metal);
        break;
    }
}

static void motif_potion(const Pen *p, float fill_frac, Color liquid) {
    box(p, 0.4f, 0.1f, 0.2f, 0.2f, RGB(0xE8, 0xE8, 0xE8));
    Vec2 c = pt(p, 0.5f, 0.62f);
    float rad = sz(p, 0.3f);
    gfx_fill_circle(p->g, c.x, c.y, rad, RGBA(0xF0, 0xF0, 0xF0, 200));
    float level = clampf(fill_frac, 0.0f, 1.0f);
    int top = (int)(c.y + rad - 2.0f * rad * level);
    Rect clip = rect_make((int)(c.x - rad), top, (int)(2.0f * rad) + 1, (int)(c.y + rad) - top + 1);
    gfx_push_clip(p->g, clip);
    gfx_fill_circle(p->g, c.x, c.y, rad - 1.0f, liquid);
    gfx_pop_clip(p->g);
    gfx_draw_circle(p->g, c.x, c.y, rad, p->ink, p->th);
}

static Color potion_color(int type) {
    switch (type) {
    case POTION_HEAL: return RGB(0xE0, 0x40, 0x48);
    case POTION_MANA: return RGB(0x30, 0xA0, 0xE8);
    case POTION_HASTE: return RGB(0x90, 0xE0, 0x50);
    case POTION_IRON: return RGB(0x80, 0x88, 0xA0);
    default: return RGB(0xF0, 0x90, 0x30);
    }
}

static void motif_material(const Pen *p, int ml) {
    switch (ml) {
    case MAT_METAL: {
        static const float top[4][2] = {{0.12f, 0.7f}, {0.3f, 0.36f}, {0.9f, 0.36f}, {0.72f, 0.7f}};
        poly(p, top, 4, RGB(0xD0, 0xD4, 0xD8));
        box(p, 0.12f, 0.7f, 0.6f, 0.14f, RGB(0xA0, 0xA4, 0xA8));
        break;
    }
    case MAT_WOOD:
        box(p, 0.15f, 0.34f, 0.66f, 0.32f, RGB(0xA0, 0x70, 0x40));
        disc(p, 0.82f, 0.5f, 0.17f, RGB(0xC8, 0x9A, 0x60));
        disc(p, 0.82f, 0.5f, 0.07f, RGB(0xA0, 0x70, 0x40));
        break;
    case MAT_CLOTH:
        box(p, 0.15f, 0.25f, 0.7f, 0.5f, RGB(0xE8, 0xE4, 0xD8));
        line(p, 0.15f, 0.42f, 0.85f, 0.42f, p->ink, 0.03f);
        line(p, 0.15f, 0.58f, 0.85f, 0.58f, p->ink, 0.03f);
        break;
    case MAT_CRYSTAL: {
        static const float d[4][2] = {{0.5f, 0.08f}, {0.8f, 0.4f}, {0.5f, 0.92f}, {0.2f, 0.4f}};
        poly(p, d, 4, RGB(0xE0, 0xF0, 0xFF));
        line(p, 0.2f, 0.4f, 0.8f, 0.4f, p->ink, 0.03f);
        break;
    }
    default: { /* Essenz: Tropfen */
        static const float tip[3][2] = {{0.5f, 0.08f}, {0.76f, 0.55f}, {0.24f, 0.55f}};
        poly(p, tip, 3, RGB(0xE8, 0xF8, 0xE0));
        disc(p, 0.5f, 0.64f, 0.26f, RGB(0xE8, 0xF8, 0xE0));
        break;
    }
    }
}

static void motif_stone(const Pen *p, int kind) {
    static const float hex[6][2] = {{0.3f, 0.12f}, {0.7f, 0.12f}, {0.92f, 0.5f}, {0.7f, 0.88f}, {0.3f, 0.88f}, {0.08f, 0.5f}};
    Color c = kind == STONE_MAGIC ? RGB(0xE8, 0xB0, 0xF8) : (kind == STONE_ENCHANT ? RGB(0xA8, 0xE8, 0xE8) : RGB(0xF0, 0xE0, 0xB0));
    poly(p, hex, 6, c);
    line(p, 0.3f, 0.12f, 0.5f, 0.5f, p->ink, 0.03f);
    line(p, 0.7f, 0.12f, 0.5f, 0.5f, p->ink, 0.03f);
    line(p, 0.5f, 0.5f, 0.5f, 0.88f, p->ink, 0.03f);
}

static void motif_item(const Pen *p, const ItemDef *d, float potion_fill) {
    switch (d->kind) {
    case IK_WEAPON: motif_weapon(p, d->cls < 0 ? CLASS_KNIGHT : d->cls); break;
    case IK_SUB: motif_sub(p, d->cls < 0 ? CLASS_KNIGHT : d->cls); break;
    case IK_ARMOR: motif_armor(p, d->slot); break;
    case IK_RING: {
        Vec2 c = pt(p, 0.5f, 0.6f);
        gfx_draw_circle(p->g, c.x, c.y, sz(p, 0.28f), RGB(0xE8, 0xC8, 0x50), maxf(2.0f, sz(p, 0.1f)));
        gfx_draw_circle(p->g, c.x, c.y, sz(p, 0.34f), p->ink, 1.0f);
        disc(p, 0.5f, 0.26f, 0.1f, RGB(0xF0, 0x60, 0x60));
        break;
    }
    case IK_NECKLACE: {
        Vec2 c = pt(p, 0.5f, 0.2f);
        gfx_arc(p->g, c.x, c.y, sz(p, 0.4f), 20.0f, 160.0f, RGB(0xE8, 0xC8, 0x50), maxf(2.0f, sz(p, 0.06f)));
        static const float g2[4][2] = {{0.5f, 0.6f}, {0.62f, 0.74f}, {0.5f, 0.92f}, {0.38f, 0.74f}};
        poly(p, g2, 4, RGB(0x60, 0xD0, 0xF0));
        break;
    }
    case IK_WINGS: {
        static const float l[4][2] = {{0.5f, 0.55f}, {0.06f, 0.18f}, {0.1f, 0.62f}, {0.5f, 0.78f}};
        static const float r[4][2] = {{0.5f, 0.55f}, {0.94f, 0.18f}, {0.9f, 0.62f}, {0.5f, 0.78f}};
        poly(p, l, 4, RGB(0xF0, 0xF0, 0xF8));
        poly(p, r, 4, RGB(0xF0, 0xF0, 0xF8));
        break;
    }
    case IK_BAG:
        disc(p, 0.5f, 0.64f, 0.3f, RGB(0xA0, 0x70, 0x40));
        box(p, 0.4f, 0.2f, 0.2f, 0.18f, RGB(0xA0, 0x70, 0x40));
        line(p, 0.34f, 0.36f, 0.66f, 0.36f, p->ink, 0.05f);
        break;
    case IK_POTION: motif_potion(p, potion_fill, potion_color(d->potion_type)); break;
    case IK_MATERIAL: motif_material(p, d->material_line); break;
    case IK_STONE: motif_stone(p, d->stone_kind); break;
    default: {
        static const float st[8][2] = {{0.5f, 0.06f}, {0.6f, 0.4f}, {0.94f, 0.5f}, {0.6f, 0.6f}, {0.5f, 0.94f}, {0.4f, 0.6f}, {0.06f, 0.5f}, {0.4f, 0.4f}};
        poly(p, st, 8, RGB(0xF4, 0xE0, 0x80));
        break;
    }
    }
}

/* ------------------------------------------------------------------------ */
/* Oeffentliche Funktionen                                                    */
/* ------------------------------------------------------------------------ */

void icon_draw_tier_diamonds(Gfx *g, int x, int y, int size, Tier tier, Color c) {
    int n = (int)tier;
    for (int i = 0; i < n; i++) {
        float cx = (float)(x + i * (size + 1) + size / 2), cy = (float)(y + size / 2), h = (float)size * 0.5f;
        Vec2 pts[4] = {v2(cx, cy - h), v2(cx + h, cy), v2(cx, cy + h), v2(cx - h, cy)};
        gfx_fill_poly(g, pts, 4, c);
    }
}

static void tier_background(Gfx *g, Rect r, Tier tier) {
    Color base = gfx_tier_color(g, tier, 255);
    Color top = color_mix(base, COL_PARCHMENT_LIGHT, 0.25f);
    Color bottom = color_mix(base, COL_INK, 0.35f);
    gfx_fill_rect_vgrad(g, r, top, bottom);
    Color edge = tier >= TIER_RARE ? color_mix(base, COL_WHITE, 0.3f + 0.2f * sinf((float)SDL_GetTicks() / 300.0f)) : COL_INK;
    gfx_draw_rect(g, r, COL_INK, 1);
    if (tier >= TIER_RARE) {
        gfx_draw_rect(g, rect_inset(r, 1), edge, 1);
    }
}

void icon_draw_item_def(Gfx *g, Rect r, int def, int plus) {
    if (def < 0 || def >= g_item_count) {
        return;
    }
    const ItemDef *d = &g_items[def];
    tier_background(g, r, d->tier);
    Pen p = {g, rect_inset(r, maxi(2, r.w / 8)), RGB(0xE8, 0xE8, 0xE8), COL_INK, (float)maxi(1, r.w / 28)};
    motif_item(&p, d, 1.0f);
    if (g->colorblind != CB_OFF) {
        icon_draw_tier_diamonds(g, r.x + 2, r.y + r.h - maxi(5, r.w / 6) - 2, maxi(4, r.w / 8), d->tier, COL_WHITE);
    }
    if (plus > 0) {
        char buf[8];
        str_format(buf, sizeof buf, "+%d", plus);
        int fs = maxi(9, r.h / 4);
        int tw = gfx_text_width(g, buf, fs);
        gfx_fill_rect(g, rect_make(r.x + r.w - tw - 3, r.y, tw + 3, fs + 1), RGBA(0, 0, 0, 170));
        gfx_text(g, r.x + r.w - tw - 1, r.y, buf, plus >= 15 ? COL_GOLD : COL_WHITE, fs);
    }
}

void icon_draw_item(Gfx *g, Rect r, const Item *it) {
    if (!it || it->def == ITEM_NONE || it->def >= g_item_count) {
        return;
    }
    const ItemDef *d = &g_items[it->def];
    tier_background(g, r, d->tier);
    Pen p = {g, rect_inset(r, maxi(2, r.w / 8)), RGB(0xE8, 0xE8, 0xE8), COL_INK, (float)maxi(1, r.w / 28)};
    motif_item(&p, d, (float)it->fill / 100.0f);
    if (g->colorblind != CB_OFF) {
        icon_draw_tier_diamonds(g, r.x + 2, r.y + r.h - maxi(5, r.w / 6) - 2, maxi(4, r.w / 8), d->tier, COL_WHITE);
    }
    int fs = maxi(9, r.h / 4);
    if (it->plus > 0) {
        char buf[8];
        str_format(buf, sizeof buf, "+%d", it->plus);
        int tw = gfx_text_width(g, buf, fs);
        gfx_fill_rect(g, rect_make(r.x + r.w - tw - 3, r.y, tw + 3, fs + 1), RGBA(0, 0, 0, 170));
        gfx_text(g, r.x + r.w - tw - 1, r.y, buf, it->plus >= 15 ? COL_GOLD : COL_WHITE, fs);
    }
    if (it->ench_opt != ENCH_NONE) {
        gfx_fill_circle(g, (float)(r.x + 4), (float)(r.y + 4), 2.5f, RGB(0x6F, 0xE0, 0xF0));
    }
    if (it->flags & ITEMF_LOCKED) {
        /* kleines Schloss unten rechts */
        int s = maxi(6, r.w / 4);
        Rect body = rect_make(r.x + r.w - s - 2, r.y + r.h - s * 2 / 3 - 2, s, s * 2 / 3);
        gfx_fill_rect(g, body, COL_GOLD);
        gfx_draw_rect(g, body, COL_INK, 1);
        gfx_draw_circle(g, (float)(body.x + s / 2), (float)body.y, (float)s / 3.0f, COL_INK, 1.5f);
    }
}

void icon_draw_stack(Gfx *g, Rect r, int def, i64 count) {
    icon_draw_item_def(g, r, def, 0);
    if (count < 0) {
        return;
    }
    char buf[24];
    str_format_compact(buf, sizeof buf, (double)count);
    int fs = maxi(9, r.h / 4);
    int tw = gfx_text_width(g, buf, fs);
    gfx_fill_rect(g, rect_make(r.x + r.w - tw - 3, r.y + r.h - fs - 1, tw + 3, fs + 1), RGBA(0, 0, 0, 170));
    gfx_text(g, r.x + r.w - tw - 1, r.y + r.h - fs - 1, buf, COL_WHITE, fs);
}

/* Skill-Glyphe je Art. */
static void skill_glyph(const Pen *p, const SkillDef *s) {
    Color w = COL_WHITE;
    float cx = 0.5f, cy = 0.5f;
    Vec2 c = pt(p, cx, cy);
    switch (s->kind) {
    case SK_STRIKE:
        line(p, 0.2f, 0.8f, 0.8f, 0.2f, w, 0.12f);
        line(p, 0.3f, 0.2f, 0.7f, 0.8f, w, 0.06f);
        break;
    case SK_CIRCLE:
        gfx_draw_circle(p->g, c.x, c.y, sz(p, 0.34f), w, maxf(2.0f, sz(p, 0.1f)));
        break;
    case SK_CONE:
        gfx_fill_pie(p->g, sz(p, 0.0f) + pt(p, 0.2f, 0.5f).x, c.y, sz(p, 0.7f), -35.0f, 35.0f, w);
        break;
    case SK_PROJECTILE:
        line(p, 0.15f, 0.5f, 0.8f, 0.5f, w, 0.08f);
        line(p, 0.8f, 0.5f, 0.62f, 0.32f, w, 0.08f);
        line(p, 0.8f, 0.5f, 0.62f, 0.68f, w, 0.08f);
        break;
    case SK_GROUND:
        gfx_fill_circle(p->g, c.x, pt(p, 0, 0.7f).y, sz(p, 0.3f), RGBA(255, 255, 255, 200));
        line(p, 0.5f, 0.1f, 0.5f, 0.55f, w, 0.1f);
        break;
    case SK_DASH:
    case SK_WARP:
        line(p, 0.15f, 0.5f, 0.85f, 0.5f, w, 0.09f);
        line(p, 0.2f, 0.3f, 0.5f, 0.3f, w, 0.06f);
        line(p, 0.2f, 0.7f, 0.5f, 0.7f, w, 0.06f);
        break;
    case SK_BUFF_SELF:
    case SK_BUFF_PARTY:
        line(p, 0.5f, 0.85f, 0.5f, 0.2f, w, 0.12f);
        line(p, 0.5f, 0.2f, 0.28f, 0.45f, w, 0.1f);
        line(p, 0.5f, 0.2f, 0.72f, 0.45f, w, 0.1f);
        break;
    case SK_DEBUFF:
        line(p, 0.5f, 0.15f, 0.5f, 0.8f, w, 0.12f);
        line(p, 0.5f, 0.8f, 0.28f, 0.55f, w, 0.1f);
        line(p, 0.5f, 0.8f, 0.72f, 0.55f, w, 0.1f);
        break;
    case SK_HEAL:
    case SK_HEAL_PARTY:
    case SK_HOT:
    case SK_CLEANSE:
        line(p, 0.5f, 0.18f, 0.5f, 0.82f, RGB(0x70, 0xF0, 0x80), 0.2f);
        line(p, 0.18f, 0.5f, 0.82f, 0.5f, RGB(0x70, 0xF0, 0x80), 0.2f);
        break;
    case SK_TOTEM:
        line(p, 0.5f, 0.85f, 0.5f, 0.15f, w, 0.2f);
        line(p, 0.3f, 0.35f, 0.7f, 0.35f, w, 0.1f);
        line(p, 0.3f, 0.6f, 0.7f, 0.6f, w, 0.1f);
        break;
    case SK_MANA:
        disc(p, 0.5f, 0.5f, 0.26f, RGB(0x2F, 0xD6, 0xD0));
        break;
    case SK_PULL:
        line(p, 0.85f, 0.5f, 0.25f, 0.5f, w, 0.09f);
        line(p, 0.25f, 0.5f, 0.45f, 0.3f, w, 0.09f);
        line(p, 0.25f, 0.5f, 0.45f, 0.7f, w, 0.09f);
        break;
    default:
        break;
    }
}

void icon_draw_skill(Gfx *g, Rect r, int skill, int level) {
    if (skill < 0 || skill >= g_skill_count) {
        gfx_fill_rect(g, r, RGBA(0, 0, 0, 60));
        gfx_draw_rect(g, r, COL_INK_SOFT, 1);
        return;
    }
    const SkillDef *s = &g_skills[skill];
    Color body = s->cls >= 0 && s->cls < CLASS_COUNT ? color_from_rgb(g_classes[s->cls].color, 255) : COL_GOLD;
    body = color_mix(body, COL_INK, 0.35f);
    gfx_fill_rect_vgrad(g, r, color_mix(body, COL_WHITE, 0.2f), body);
    Pen p = {g, rect_inset(r, maxi(3, r.w / 8)), COL_WHITE, COL_INK, 1.0f};
    skill_glyph(&p, s);
    Tier grade = (Tier)clampi(s->grade, 0, 3);
    gfx_draw_rect(g, r, gfx_tier_color(g, grade, 255), maxi(2, r.w / 16));
    gfx_draw_rect(g, r, COL_INK, 1);
    if (level > 0) {
        char buf[8];
        str_format(buf, sizeof buf, "%d", level);
        int fs = maxi(9, r.h / 4);
        int tw = gfx_text_width(g, buf, fs);
        gfx_fill_rect(g, rect_make(r.x + r.w - tw - 3, r.y + r.h - fs - 1, tw + 3, fs + 1), RGBA(0, 0, 0, 170));
        gfx_text(g, r.x + r.w - tw - 1, r.y + r.h - fs - 1, buf, level >= SKILL_MAX_LEVEL ? COL_GOLD : COL_WHITE, fs);
    }
}

void icon_draw_chest(Gfx *g, Rect r, Tier tier, double time) {
    Color tc = gfx_tier_color(g, tier, 255);
    float cx = (float)r.x + (float)r.w * 0.5f, cy = (float)r.y + (float)r.h * 0.5f;
    if (tier >= TIER_BOSS) {
        float pulse = 0.5f + 0.5f * sinf((float)time * 4.0f);
        gfx_set_additive(g, true);
        gfx_fill_circle(g, cx, cy, (float)r.w * (0.55f + 0.1f * pulse), RGBA(tc.r, tc.g, tc.b, 70));
        gfx_set_additive(g, false);
    }
    Rect body = rect_make(r.x + r.w / 8, r.y + r.h * 4 / 10, r.w * 3 / 4, r.h * 4 / 10);
    Rect lid = rect_make(r.x + r.w / 8, r.y + r.h / 5, r.w * 3 / 4, r.h / 4);
    Color wood = RGB(0x8A, 0x5A, 0x32);
    gfx_fill_rect(g, body, wood);
    gfx_fill_rect(g, lid, color_mix(wood, COL_WHITE, 0.15f));
    gfx_draw_rect(g, body, COL_INK, 1);
    gfx_draw_rect(g, lid, COL_INK, 1);
    /* Beschlag in Tierfarbe */
    gfx_fill_rect(g, rect_make(body.x + body.w / 2 - r.w / 14, lid.y, r.w / 7, body.y + body.h - lid.y), tc);
    gfx_fill_rect(g, rect_make(body.x, body.y - r.h / 40, body.w, maxi(2, r.h / 14)), tc);
    gfx_fill_rect(g, rect_make(body.x + body.w / 2 - r.w / 22, body.y + body.h / 5, r.w / 11, r.w / 11), COL_GOLD);
    if (tier == TIER_LEGENDARY) {
        for (int i = 0; i < 4; i++) {
            float a = (float)time * 2.0f + (float)i * 1.7f;
            float sx = cx + cosf(a) * (float)r.w * 0.45f, sy = cy + sinf(a * 1.3f) * (float)r.h * 0.4f;
            float s = 1.5f + 1.5f * sinf((float)time * 6.0f + (float)i);
            gfx_fill_circle(g, sx, sy, maxf(1.0f, s), RGBA(255, 220, 245, 230));
        }
    }
    if (g->colorblind != CB_OFF) {
        icon_draw_tier_diamonds(g, r.x + 1, r.y + r.h - maxi(5, r.w / 6), maxi(4, r.w / 8), tier, COL_WHITE);
    }
}

void icon_draw_class(Gfx *g, Rect r, int cls, int look) {
    if (cls < 0 || cls >= CLASS_COUNT) {
        return;
    }
    float cx = (float)r.x + (float)r.w * 0.5f, cy = (float)r.y + (float)r.h * 0.5f, rad = (float)mini(r.w, r.h) * 0.45f;
    Color cc = color_from_rgb(g_classes[cls].color, 255);
    gfx_fill_circle(g, cx, cy, rad, COL_INK);
    gfx_fill_circle(g, cx, cy, rad - 2.0f, cc);
    static const u32 skin[8] = {0xF2CDA4, 0xE0AC86, 0xC68E63, 0x8E5B3A, 0x7DB06A, 0xB9A0D0, 0xD8D8E0, 0xE8B890};
    static const u32 hair[8] = {0x3A2A1A, 0xA0602A, 0xE0C060, 0x202020, 0xB02A20, 0x808080, 0xF0F0F0, 0x6A3A8A};
    float hr = rad * 0.42f;
    gfx_fill_circle(g, cx, cy - rad * 0.12f, hr, color_from_rgb(skin[look & 7], 255));
    gfx_fill_pie(g, cx, cy - rad * 0.12f, hr, 180.0f, 360.0f, color_from_rgb(hair[(look >> 1) & 7], 255));
    gfx_draw_circle(g, cx, cy - rad * 0.12f, hr, COL_INK, 1.0f);
    Pen p = {g, rect_make((int)(cx - rad * 0.5f), (int)(cy + rad * 0.05f), (int)(rad), (int)(rad * 0.9f)), RGB(0xE8, 0xE8, 0xE8), COL_INK, 1.0f};
    motif_weapon(&p, cls);
}

void icon_draw_empty_slot(Gfx *g, Rect r, EquipSlot slot) {
    gfx_fill_rect(g, r, RGBA(0, 0, 0, 40));
    /* gestrichelter Rahmen */
    int dash = maxi(3, r.w / 10);
    for (int i = 0; i < r.w; i += dash * 2) {
        int w = mini(dash, r.w - i);
        gfx_fill_rect(g, rect_make(r.x + i, r.y, w, 1), COL_INK_SOFT);
        gfx_fill_rect(g, rect_make(r.x + i, r.y + r.h - 1, w, 1), COL_INK_SOFT);
    }
    for (int i = 0; i < r.h; i += dash * 2) {
        int h = mini(dash, r.h - i);
        gfx_fill_rect(g, rect_make(r.x, r.y + i, 1, h), COL_INK_SOFT);
        gfx_fill_rect(g, rect_make(r.x + r.w - 1, r.y + i, 1, h), COL_INK_SOFT);
    }
    ItemDef d;
    memset(&d, 0, sizeof d);
    d.cls = CLASS_KNIGHT;
    d.slot = slot;
    switch (slot) {
    case SLOT_WEAPON: d.kind = IK_WEAPON; break;
    case SLOT_SUB: d.kind = IK_SUB; break;
    case SLOT_HELM: case SLOT_CHEST: case SLOT_GLOVES: case SLOT_BOOTS: d.kind = IK_ARMOR; break;
    case SLOT_BAG: d.kind = IK_BAG; break;
    case SLOT_NECKLACE: d.kind = IK_NECKLACE; break;
    case SLOT_RING1: case SLOT_RING2: d.kind = IK_RING; break;
    case SLOT_WINGS: d.kind = IK_WINGS; break;
    default: d.kind = IK_POTION; d.potion_type = POTION_HEAL; break;
    }
    Pen p = {g, rect_inset(r, maxi(3, r.w / 6)), RGBA(120, 100, 80, 90), RGBA(80, 60, 40, 110), 1.0f};
    motif_item(&p, &d, 0.0f);
}

void icon_draw_currency(Gfx *g, Rect r, Currency c) {
    float cx = (float)r.x + (float)r.w * 0.5f, cy = (float)r.y + (float)r.h * 0.5f, rad = (float)mini(r.w, r.h) * 0.45f;
    switch (c) {
    case CUR_GOLD:
        gfx_fill_circle(g, cx, cy, rad, COL_INK);
        gfx_fill_circle(g, cx, cy, rad - 1.5f, RGB(0xF2, 0xC9, 0x4C));
        gfx_draw_circle(g, cx, cy, rad * 0.6f, RGB(0xC0, 0x90, 0x20), 1.5f);
        break;
    case CUR_DIAMONDS: {
        Vec2 pts[5] = {v2(cx - rad * 0.8f, cy - rad * 0.3f), v2(cx - rad * 0.4f, cy - rad), v2(cx + rad * 0.4f, cy - rad),
                       v2(cx + rad * 0.8f, cy - rad * 0.3f), v2(cx, cy + rad)};
        gfx_fill_poly(g, pts, 5, RGB(0x6E, 0xE0, 0xF5));
        for (int i = 0; i < 5; i++) {
            gfx_line(g, pts[i].x, pts[i].y, pts[(i + 1) % 5].x, pts[(i + 1) % 5].y, COL_INK, 1.5f);
        }
        break;
    }
    case CUR_CREDITS: {
        Rect b = rect_make((int)(cx - rad), (int)(cy - rad * 0.7f), (int)(rad * 2), (int)(rad * 1.4f));
        gfx_fill_rect(g, b, RGB(0x5E, 0x8C, 0xA8));
        gfx_draw_rect(g, b, COL_INK, 1);
        gfx_text_centered(g, b, "CR", COL_WHITE, maxi(8, (int)(rad * 1.0f)));
        break;
    }
    case CUR_RAVEN_COINS:
        gfx_fill_circle(g, cx, cy, rad, COL_INK);
        gfx_fill_circle(g, cx, cy, rad - 1.5f, RGB(0x4A, 0x40, 0x68));
        gfx_line(g, cx - rad * 0.4f, cy + rad * 0.4f, cx + rad * 0.4f, cy - rad * 0.4f, RGB(0xE0, 0xE0, 0xF0), 2.0f);
        break;
    default:
        gfx_line(g, cx - rad * 0.6f, cy + rad * 0.8f, cx + rad * 0.6f, cy - rad * 0.8f, COL_INK, 2.0f);
        gfx_fill_pie(g, cx + rad * 0.1f, cy - rad * 0.2f, rad * 0.7f, -90.0f, 30.0f, RGB(0x30, 0x2A, 0x48));
        break;
    }
}
