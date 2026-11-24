/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Darstellung eines Runs (Kacheln, Einheiten, Effekte, Kamera) in ein Rechteck.
 * Alles prozedural im Pergament-/Tintenstil (Platzhalter-Prinzip, Spec 19).
 * Reine Darstellung: liest nur die oeffentlichen Felder der Simulation. */
#include "render/view.h"

#include <math.h>
#include <string.h>

#include "core/util.h"
#include "data/data.h"
#include "render/icons.h"
#include "render/theme.h"

/* ------------------------------------------------------------------------ */
/* Kamera und kleine Helfer                                                   */
/* ------------------------------------------------------------------------ */

typedef struct Cam {
    Rect vp;
    Vec2 c;       /* Weltposition in der Viewport-Mitte */
    float z;      /* Weltpixel -> Bildschirmpixel */
    Vec2 shake;   /* Bildschirm-Versatz durch Erschuetterung */
} Cam;

static u32 hash32(u32 x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

/* Zufallswert -1..1 aus Ganzzahl-Seed. */
static float hrand(u32 seed) { return (float)(hash32(seed) & 0xFFFFu) / 32767.5f - 1.0f; }

static Color col_a(Color c, u8 a) {
    c.a = a;
    return c;
}

static Color col_alpha_f(Color c, float f) {
    c.a = (u8)clampf((float)c.a * f, 0.0f, 255.0f);
    return c;
}

static Color shade(Color c, float f) {
    c.r = (u8)clampf((float)c.r * f, 0.0f, 255.0f);
    c.g = (u8)clampf((float)c.g * f, 0.0f, 255.0f);
    c.b = (u8)clampf((float)c.b * f, 0.0f, 255.0f);
    return c;
}

static float cam_zoom(const ViewFx *fx) {
    return fx->zoom > 0.01f ? clampf(fx->zoom, 0.75f, 1.5f) : 1.0f;
}

static Vec2 shake_offset(const ViewFx *fx) {
    float s = clampf(fx->shake, 0.0f, 16.0f);
    return v2(sinf((float)fx->time * 91.0f) * s, cosf((float)fx->time * 77.0f) * s);
}

static Cam make_cam(const ViewFx *fx, Rect vp) {
    Cam c;
    c.vp = vp;
    c.c = fx->cam;
    c.z = cam_zoom(fx);
    c.shake = shake_offset(fx);
    return c;
}

static Vec2 to_scr(const Cam *c, Vec2 w) {
    return v2((float)c->vp.x + (float)c->vp.w * 0.5f + (w.x - c->c.x) * c->z + c->shake.x,
              (float)c->vp.y + (float)c->vp.h * 0.5f + (w.y - c->c.y) * c->z + c->shake.y);
}

/* Liegt der Weltpunkt (mit Rand in Weltpixeln) im Viewport? */
static bool visible(const Cam *c, Vec2 w, float margin) {
    Vec2 s = to_scr(c, w);
    float m = margin * c->z;
    return s.x >= (float)c->vp.x - m && s.x <= (float)(c->vp.x + c->vp.w) + m &&
           s.y >= (float)c->vp.y - m && s.y <= (float)(c->vp.y + c->vp.h) + m;
}

Vec2 view_screen_to_world(const ViewFx *fx, Rect vp, int sx, int sy) {
    Cam c = make_cam(fx, vp);
    return v2(fx->cam.x + ((float)sx - (float)vp.x - (float)vp.w * 0.5f - c.shake.x) / c.z,
              fx->cam.y + ((float)sy - (float)vp.y - (float)vp.h * 0.5f - c.shake.y) / c.z);
}

Vec2 view_world_to_screen(const ViewFx *fx, Rect vp, Vec2 w) {
    Cam c = make_cam(fx, vp);
    return to_scr(&c, w);
}

static Color world_color(u32 rgb, u8 a, u32 fallback) {
    return color_from_rgb(rgb ? rgb : fallback, a);
}

/* ------------------------------------------------------------------------ */
/* Effekte: Init, Events, Update                                              */
/* ------------------------------------------------------------------------ */

void view_fx_init(ViewFx *fx) {
    memset(fx, 0, sizeof(*fx));
    fx->zoom = 1.0f;
    fx->last_map = -1;
}

static void add_particle(ViewFx *fx, Vec2 pos, Vec2 vel, float life, float size, Color c, bool additive) {
    Particle *slot = NULL;
    for (int i = 0; i < FX_MAX_PARTICLES; i++) {
        if (!fx->parts[i].active) {
            slot = &fx->parts[i];
            break;
        }
    }
    if (!slot) { /* voll: aeltestes ersetzen */
        float best = -1.0f;
        for (int i = 0; i < FX_MAX_PARTICLES; i++) {
            if (fx->parts[i].age > best) {
                best = fx->parts[i].age;
                slot = &fx->parts[i];
            }
        }
    }
    slot->active = true;
    slot->pos = pos;
    slot->vel = vel;
    slot->age = 0.0f;
    slot->life = life;
    slot->size = size;
    slot->color = c;
    slot->additive = additive;
}

/* n Partikel strahlenfoermig; seed bestimmt Streuung (deterministisch). */
static void burst(ViewFx *fx, Vec2 pos, int n, float speed, float life, float size, Color c, bool additive,
                  u32 seed) {
    for (int i = 0; i < n; i++) {
        float a = ((float)i / (float)n) * 2.0f * PNX_PI + hrand(seed + (u32)i * 7u) * 0.5f;
        float sp = speed * (0.55f + 0.45f * (hrand(seed + (u32)i * 13u + 5u) * 0.5f + 0.5f));
        add_particle(fx, pos, v2(cosf(a) * sp, sinf(a) * sp), life * (0.7f + 0.3f * (hrand(seed + (u32)i) * 0.5f + 0.5f)),
                     size, c, additive);
    }
}

static void add_text(ViewFx *fx, Vec2 pos, const char *text, Color c, bool crit, int size, float life, float vy) {
    FloatText *slot = NULL;
    for (int i = 0; i < FX_MAX_TEXTS; i++) {
        if (!fx->texts[i].active) {
            slot = &fx->texts[i];
            break;
        }
    }
    if (!slot) {
        float best = -1.0f;
        for (int i = 0; i < FX_MAX_TEXTS; i++) {
            if (fx->texts[i].age > best) {
                best = fx->texts[i].age;
                slot = &fx->texts[i];
            }
        }
    }
    slot->active = true;
    slot->pos = pos;
    slot->age = 0.0f;
    slot->life = life;
    slot->vy = vy;
    str_copy(slot->text, sizeof(slot->text), text);
    slot->color = c;
    slot->crit = crit;
    slot->size = size;
}

static void add_shake(ViewFx *fx, Vec2 pos, float amount) {
    if (fx->cam_init && v2_dist(fx->cam, pos) > 700.0f) {
        return;
    }
    fx->shake = clampf(fx->shake + amount, 0.0f, 14.0f);
}

static void fmt_number(char *dst, size_t n, float v, const char *prefix, const char *suffix) {
    char num[24];
    if (v >= 100000.0f) {
        str_format_compact(num, sizeof(num), (double)v);
    } else {
        str_format(num, sizeof(num), "%.0f", (double)v);
    }
    str_format(dst, n, "%s%s%s", prefix, num, suffix);
}

static void feed_damage(ViewFx *fx, const SimEvent *ev) {
    char buf[16];
    u32 seed = (u32)(ev->pos.x * 3.0f) * 73856093u ^ (u32)(ev->pos.y * 5.0f) * 19349663u ^ (u32)ev->value;
    Vec2 p = v2(ev->pos.x + hrand(seed) * 12.0f, ev->pos.y - 14.0f + hrand(seed + 1u) * 4.0f);
    Color c = COL_DMG;
    if (ev->a == 1) {
        c = RGB(255, 150, 130);   /* Schaden am Helden */
    } else if (ev->b) {
        c = RGB(214, 160, 232);   /* Gift */
    }
    fmt_number(buf, sizeof(buf), ev->value, "", ev->flag ? "!" : "");
    add_text(fx, p, buf, c, ev->flag, ev->flag ? 22 : 14, ev->flag ? 1.2f : 0.85f, ev->flag ? -58.0f : -42.0f);
    burst(fx, ev->pos, ev->flag ? 7 : 3, ev->flag ? 120.0f : 70.0f, 0.3f, 2.0f,
          ev->flag ? COL_DMG_CRIT_EDGE : (ev->a == 1 ? COL_HP : COL_WHITE), true, seed);
}

void view_fx_feed(ViewFx *fx, const SimEvent *ev, const Run *run) {
    (void)run;
    if (!fx || !ev) {
        return;
    }
    char buf[32];
    u32 seed = (u32)(ev->pos.x * 7.0f) ^ ((u32)(ev->pos.y * 11.0f) << 8) ^ (u32)ev->type * 2654435761u;
    Tier tier = (Tier)clampi((int)ev->tier, 0, TIER_COUNT - 1);
    switch (ev->type) {
    case EV_DAMAGE:
        feed_damage(fx, ev);
        break;
    case EV_HEAL:
        if (ev->value >= 1.0f) {
            fmt_number(buf, sizeof(buf), ev->value, "+", "");
            add_text(fx, v2(ev->pos.x, ev->pos.y - 16.0f), buf, COL_HEAL, false, 14, 0.9f, -38.0f);
            burst(fx, ev->pos, 4, 40.0f, 0.6f, 2.5f, COL_HEAL, true, seed);
        }
        break;
    case EV_MANA:
        if (ev->value >= 1.0f) {
            fmt_number(buf, sizeof(buf), ev->value, "+", "");
            add_text(fx, v2(ev->pos.x, ev->pos.y - 22.0f), buf, COL_MANA, false, 12, 0.7f, -34.0f);
        }
        break;
    case EV_KILL: {
        Color tc = tier_color(tier, 255);
        burst(fx, ev->pos, 8 + (int)tier * 4, 110.0f + 25.0f * (float)tier, 0.55f, 3.0f, tc, tier >= TIER_RARE, seed);
        burst(fx, ev->pos, 6, 60.0f, 0.5f, 2.0f, COL_PARCHMENT_LIGHT, false, seed + 99u);
        if (tier == TIER_BOSS) {
            add_shake(fx, ev->pos, 10.0f);
            burst(fx, ev->pos, 40, 260.0f, 1.0f, 4.0f, tc, true, seed + 3u);
        }
        break;
    }
    case EV_HERO_DEATH:
        burst(fx, ev->pos, 14, 90.0f, 0.9f, 3.0f, RGB(120, 110, 110), false, seed);
        add_text(fx, v2(ev->pos.x, ev->pos.y - 20.0f), "Gefallen", RGB(255, 120, 110), false, 16, 1.6f, -22.0f);
        add_shake(fx, ev->pos, 6.0f);
        break;
    case EV_LEVELUP:
        str_format(buf, sizeof(buf), "Level %d!", ev->b);
        add_text(fx, v2(ev->pos.x, ev->pos.y - 26.0f), buf, COL_DMG_CRIT_EDGE, true, 18, 1.6f, -30.0f);
        burst(fx, ev->pos, 24, 140.0f, 0.9f, 3.0f, COL_GOLD, true, seed);
        break;
    case EV_CHEST_DROP:
        burst(fx, ev->pos, 14, 90.0f, 0.7f, 3.0f, tier_color(tier, 255), true, seed);
        break;
    case EV_CHEST_PICKUP:
        for (int i = 0; i < 12; i++) {
            float a = hrand(seed + (u32)i) * 0.8f - PNX_PI * 0.5f;
            add_particle(fx, ev->pos, v2(cosf(a) * 90.0f, sinf(a) * 120.0f), 0.8f, 3.0f, tier_color(tier, 255), true);
        }
        break;
    case EV_SKILL_CAST:
        burst(fx, ev->pos, 10, 80.0f, 0.4f, 2.5f, world_color(ev->color, 220, 0xFFFFFF), true, seed);
        break;
    case EV_HIT_FX:
        burst(fx, ev->pos, 4, 100.0f, 0.25f, 2.0f, world_color(ev->color, 255, 0xFFFFFF), true, seed);
        break;
    case EV_BOSS_SPAWN:
        add_shake(fx, ev->pos, 12.0f);
        burst(fx, ev->pos, 36, 200.0f, 1.0f, 4.0f, tier_color(TIER_BOSS, 255), true, seed);
        add_text(fx, v2(ev->pos.x, ev->pos.y - 40.0f), "Der Boss erwacht!", RGB(255, 110, 100), true, 20, 2.0f, -20.0f);
        break;
    case EV_MAP_CLEAR:
        burst(fx, ev->pos, 40, 220.0f, 1.2f, 3.5f, COL_GOLD, true, seed);
        add_text(fx, v2(ev->pos.x, ev->pos.y - 30.0f), "Karte abgeschlossen!", COL_DMG_CRIT_EDGE, true, 20, 2.4f, -18.0f);
        break;
    case EV_EXPLOSION: {
        float rad = maxf(ev->value, 20.0f);
        Color c = world_color((u32)ev->a, 255, 0xFF8A2A);
        burst(fx, ev->pos, 28, rad * 2.2f, 0.5f, 3.5f, c, true, seed);
        burst(fx, ev->pos, 10, rad * 0.8f, 0.5f, 4.5f, RGB(60, 45, 35), false, seed + 17u);
        if (rad > 80.0f) {
            add_shake(fx, ev->pos, 4.0f);
        }
        break;
    }
    case EV_PHASE:
        add_shake(fx, ev->pos, 8.0f);
        burst(fx, ev->pos, 24, 180.0f, 0.8f, 3.5f, tier_color(TIER_BOSS, 255), true, seed);
        add_text(fx, v2(ev->pos.x, ev->pos.y - 40.0f), "Neue Phase!", RGB(255, 110, 100), true, 18, 1.8f, -20.0f);
        break;
    default:
        break;
    }
}

void view_fx_update(ViewFx *fx, double dt) {
    float d = clampf((float)dt, 0.0f, 0.1f);
    fx->time += (double)d;
    fx->frame_dt = d;
    fx->shake *= expf(-7.0f * d);
    if (fx->shake < 0.05f) {
        fx->shake = 0.0f;
    }
    float drag = expf(-2.2f * d);
    for (int i = 0; i < FX_MAX_PARTICLES; i++) {
        Particle *p = &fx->parts[i];
        if (!p->active) {
            continue;
        }
        p->age += d;
        if (p->age >= p->life) {
            p->active = false;
            continue;
        }
        p->pos = v2_add(p->pos, v2_scale(p->vel, d));
        p->vel = v2_scale(p->vel, drag);
    }
    for (int i = 0; i < FX_MAX_TEXTS; i++) {
        FloatText *t = &fx->texts[i];
        if (!t->active) {
            continue;
        }
        t->age += d;
        if (t->age >= t->life) {
            t->active = false;
            continue;
        }
        t->pos.y += t->vy * d;
        t->vy *= expf(-1.6f * d);
    }
}

/* ------------------------------------------------------------------------ */
/* Kacheln                                                                    */
/* ------------------------------------------------------------------------ */

typedef struct TilePalette {
    Color floor, wall, water, slow, heal, arena;
} TilePalette;

static TilePalette make_palette(const MapDef *def) {
    Color tint = def ? color_from_rgb(def->tint, 255) : COL_PARCHMENT_DARK;
    TilePalette p;
    p.floor = color_mix(COL_PARCHMENT, tint, 0.28f);
    p.wall = color_mix(COL_WOOD_DARK, tint, 0.20f);
    p.water = color_mix(RGB(76, 120, 150), tint, 0.12f);
    p.slow = color_mix(p.floor, COL_MOSS, 0.55f);
    p.heal = color_mix(p.floor, RGB(120, 200, 130), 0.35f);
    p.arena = color_mix(p.floor, COL_WAX_RED, 0.28f);
    return p;
}

/* Kachelfarbe mit Zufallsvariation (variant 0..255) und leichtem Schachbrett. */
static Color vary(Color c, int variant, int tx, int ty) {
    int d = (variant % 13) - 6 + (((tx + ty) & 1) ? 3 : 0);
    c.r = (u8)clampi((int)c.r + d, 0, 255);
    c.g = (u8)clampi((int)c.g + d, 0, 255);
    c.b = (u8)clampi((int)c.b + d, 0, 255);
    return c;
}

static void draw_floor_deco(Gfx *g, Rect r, int variant, Color base) {
    Color ink = col_a(COL_INK, 38);
    int v = variant & 0x3F;
    if (v == 0) { /* Riss */
        gfx_line(g, (float)(r.x + r.w / 4), (float)(r.y + r.h / 3), (float)(r.x + r.w / 2), (float)(r.y + r.h / 2), ink, 1.0f);
        gfx_line(g, (float)(r.x + r.w / 2), (float)(r.y + r.h / 2), (float)(r.x + r.w * 3 / 4), (float)(r.y + r.h * 3 / 5), ink, 1.0f);
    } else if (v == 1) { /* Kiesel */
        gfx_fill_circle(g, (float)(r.x + r.w / 3), (float)(r.y + r.h * 2 / 3), (float)maxi(1, r.w / 12), shade(base, 0.85f));
        gfx_fill_circle(g, (float)(r.x + r.w * 2 / 3), (float)(r.y + r.h / 3), (float)maxi(1, r.w / 16), shade(base, 0.9f));
    } else if (v == 2) { /* Tintenfleck */
        gfx_fill_circle(g, (float)(r.x + r.w / 2), (float)(r.y + r.h / 2), (float)maxi(1, r.w / 9), col_a(COL_INK_SOFT, 26));
    }
}

static void draw_water_tile(Gfx *g, Rect r, int tx, int ty, double time, Color base) {
    gfx_fill_rect(g, r, base);
    float ph = (float)time * 1.4f + (float)tx * 0.8f + (float)ty * 1.3f;
    float wob = sinf(ph) * (float)r.h * 0.08f;
    Color hi = col_a(COL_WHITE, (u8)(40 + 25 * sinf(ph * 1.7f)));
    gfx_line(g, (float)r.x + (float)r.w * 0.12f, (float)r.y + (float)r.h * 0.35f + wob,
             (float)r.x + (float)r.w * 0.55f, (float)r.y + (float)r.h * 0.35f - wob, hi, 1.0f);
    gfx_line(g, (float)r.x + (float)r.w * 0.45f, (float)r.y + (float)r.h * 0.72f - wob,
             (float)r.x + (float)r.w * 0.88f, (float)r.y + (float)r.h * 0.72f + wob, hi, 1.0f);
}

static void draw_heal_tile(Gfx *g, Rect r, double time, Color base) {
    gfx_fill_rect(g, r, base);
    float cx = (float)r.x + (float)r.w * 0.5f, cy = (float)r.y + (float)r.h * 0.5f;
    float pulse = 0.5f + 0.5f * sinf((float)time * 2.6f);
    gfx_set_additive(g, true);
    gfx_fill_circle(g, cx, cy, (float)r.w * (0.45f + 0.1f * pulse), col_a(COL_HEAL, (u8)(40 + 40 * pulse)));
    gfx_set_additive(g, false);
    int arm = maxi(1, r.w / 6), len = maxi(3, r.w / 2);
    gfx_fill_rect(g, rect_make((int)cx - len / 2, (int)cy - arm / 2, len, arm), col_a(COL_HEAL, 230));
    gfx_fill_rect(g, rect_make((int)cx - arm / 2, (int)cy - len / 2, arm, len), col_a(COL_HEAL, 230));
}

static void draw_slow_tile(Gfx *g, Rect r, int variant, Color base) {
    gfx_fill_rect(g, r, base);
    Color dot = shade(base, 0.75f);
    for (int k = 0; k < 3; k++) {
        float u = 0.2f + 0.3f * (float)((variant >> (k * 2)) & 3) / 3.0f + 0.15f * (float)k;
        float v = 0.25f + 0.22f * (float)k;
        gfx_fill_circle(g, (float)r.x + u * (float)r.w, (float)r.y + v * (float)r.h, (float)maxi(1, r.w / 10), dot);
    }
}

static void draw_arena_tile(Gfx *g, Rect r, int tx, int ty, Color base) {
    gfx_fill_rect(g, r, base);
    if (((tx + ty) & 1) == 0) {
        gfx_fill_rect(g, rect_inset(r, maxi(1, r.w / 5)), col_a(COL_WAX_RED, 22));
    }
}

static void draw_wall_tile(Gfx *g, Rect r, const GameMap *m, int tx, int ty, Color base) {
    gfx_fill_rect(g, r, base);
    bool open_below = world_tile_at(m, tx, ty + 1) != TILE_WALL;
    if (open_below) { /* helle Kante unten + Schatten darunter */
        int lip = maxi(2, r.h / 5);
        gfx_fill_rect(g, rect_make(r.x, r.y + r.h - lip, r.w, lip), shade(base, 1.35f));
        gfx_fill_rect(g, rect_make(r.x, r.y + r.h - 1, r.w, 1), col_a(COL_INK, 160));
    }
    if (world_tile_at(m, tx, ty - 1) != TILE_WALL) {
        gfx_fill_rect(g, rect_make(r.x, r.y, r.w, 1), col_a(COL_INK, 120));
    }
}

static void draw_tiles(Gfx *g, const Run *run, const Cam *cam, double time, bool mini) {
    const GameMap *m = &run->map;
    TilePalette pal = make_palette(run->def);
    float halfw = (float)cam->vp.w * 0.5f / cam->z + TILE_SIZE;
    float halfh = (float)cam->vp.h * 0.5f / cam->z + TILE_SIZE;
    int tx0 = clampi((int)floorf((cam->c.x - halfw) / TILE_SIZE), 0, m->w - 1);
    int tx1 = clampi((int)floorf((cam->c.x + halfw) / TILE_SIZE), 0, m->w - 1);
    int ty0 = clampi((int)floorf((cam->c.y - halfh) / TILE_SIZE), 0, m->h - 1);
    int ty1 = clampi((int)floorf((cam->c.y + halfh) / TILE_SIZE), 0, m->h - 1);
    for (int ty = ty0; ty <= ty1; ty++) {
        for (int tx = tx0; tx <= tx1; tx++) {
            Vec2 a = to_scr(cam, v2((float)(tx * TILE_SIZE), (float)(ty * TILE_SIZE)));
            Vec2 b = to_scr(cam, v2((float)((tx + 1) * TILE_SIZE), (float)((ty + 1) * TILE_SIZE)));
            int x0 = (int)floorf(a.x), y0 = (int)floorf(a.y);
            Rect r = rect_make(x0, y0, maxi(1, (int)floorf(b.x) - x0), maxi(1, (int)floorf(b.y) - y0));
            int variant = m->variant ? m->variant[ty * m->w + tx] : 0;
            int t = m->tiles[ty * m->w + tx];
            switch (t) {
            case TILE_WALL:
                draw_wall_tile(g, r, m, tx, ty, vary(pal.wall, variant, tx, ty));
                break;
            case TILE_WATER:
                draw_water_tile(g, r, tx, ty, time, vary(pal.water, variant, tx, ty));
                break;
            case TILE_SLOW:
                draw_slow_tile(g, r, variant, vary(pal.slow, variant, tx, ty));
                break;
            case TILE_HEAL:
                draw_heal_tile(g, r, time, vary(pal.heal, variant, tx, ty));
                break;
            case TILE_ARENA:
                draw_arena_tile(g, r, tx, ty, vary(pal.arena, variant, tx, ty));
                break;
            default: {
                Color fc = vary(pal.floor, variant, tx, ty);
                gfx_fill_rect(g, r, fc);
                if (!mini) {
                    draw_floor_deco(g, r, variant, fc);
                }
                if (world_tile_at(m, tx, ty - 1) == TILE_WALL) { /* Schattenkante unter Waenden */
                    gfx_fill_rect(g, rect_make(r.x, r.y, r.w, maxi(2, r.h / 6)), col_a(COL_INK, 46));
                }
                break;
            }
            }
        }
    }
}

static void draw_arena_border(Gfx *g, const Run *run, const Cam *cam, double time) {
    const GameMap *m = &run->map;
    if (!m->has_boss_arena) {
        return;
    }
    Vec2 a = to_scr(cam, v2((float)m->arena.x, (float)m->arena.y));
    Vec2 b = to_scr(cam, v2((float)(m->arena.x + m->arena.w), (float)(m->arena.y + m->arena.h)));
    Rect r = rect_make((int)a.x, (int)a.y, (int)(b.x - a.x), (int)(b.y - a.y));
    if (run->arena_sealed) {
        float pulse = 0.5f + 0.5f * sinf((float)time * 5.0f);
        gfx_draw_rect(g, r, col_a(COL_WAX_RED, (u8)(170 + 80 * pulse)), maxi(3, (int)(4.0f * cam->z)));
    } else {
        gfx_draw_rect(g, r, col_a(COL_WAX_RED, 120), maxi(2, (int)(2.0f * cam->z)));
    }
}

/* ------------------------------------------------------------------------ */
/* Zonen und Telegraphen                                                      */
/* ------------------------------------------------------------------------ */

/* Zeichnet Kreis/Kegel/Linie; frac skaliert die innere Fuellung (Telegraph-Fortschritt). */
static void draw_zone_shape(Gfx *g, const Cam *cam, const Zone *z, Color fill, Color edge, float frac) {
    Vec2 c = to_scr(cam, z->pos);
    float rad = z->radius * cam->z;
    float ang = atan2f(z->dir.y, z->dir.x) * 180.0f / PNX_PI;
    if (z->shape == SHAPE_CONE) {
        float h = z->width_or_angle * 0.5f;
        gfx_fill_pie(g, c.x, c.y, rad, ang - h, ang + h, fill);
        gfx_fill_pie(g, c.x, c.y, rad * frac, ang - h, ang + h, col_alpha_f(fill, 1.2f));
        for (int s = -1; s <= 1; s += 2) {
            float ra = (ang + (float)s * h) * PNX_PI / 180.0f;
            gfx_line(g, c.x, c.y, c.x + cosf(ra) * rad, c.y + sinf(ra) * rad, edge, 2.0f);
        }
        gfx_arc(g, c.x, c.y, rad, ang - h, ang + h, edge, 2.0f);
    } else if (z->shape == SHAPE_LINE) {
        Vec2 d = v2_norm(z->dir), n = v2(-d.y, d.x);
        float hw = z->width_or_angle * 0.5f * cam->z;
        Vec2 q[4];
        for (int pass = 0; pass < 2; pass++) {
            float len = pass == 0 ? rad : rad * frac;
            q[0] = v2(c.x + n.x * hw, c.y + n.y * hw);
            q[1] = v2(c.x + d.x * len + n.x * hw, c.y + d.y * len + n.y * hw);
            q[2] = v2(c.x + d.x * len - n.x * hw, c.y + d.y * len - n.y * hw);
            q[3] = v2(c.x - n.x * hw, c.y - n.y * hw);
            gfx_fill_poly(g, q, 4, pass == 0 ? fill : col_alpha_f(fill, 1.2f));
        }
        q[0] = v2(c.x + n.x * hw, c.y + n.y * hw);
        q[1] = v2(c.x + d.x * rad + n.x * hw, c.y + d.y * rad + n.y * hw);
        q[2] = v2(c.x + d.x * rad - n.x * hw, c.y + d.y * rad - n.y * hw);
        q[3] = v2(c.x - n.x * hw, c.y - n.y * hw);
        for (int i = 0; i < 4; i++) {
            gfx_line(g, q[i].x, q[i].y, q[(i + 1) % 4].x, q[(i + 1) % 4].y, edge, 2.0f);
        }
    } else {
        gfx_fill_circle(g, c.x, c.y, rad, fill);
        gfx_fill_circle(g, c.x, c.y, rad * frac, col_alpha_f(fill, 1.2f));
        gfx_draw_circle(g, c.x, c.y, rad, edge, 2.0f);
    }
}

static void draw_pool_bubbles(Gfx *g, const Cam *cam, const Zone *z, double time, Color c) {
    Vec2 s = to_scr(cam, z->pos);
    for (int k = 0; k < 4; k++) {
        float ph = fmodf((float)time * 0.6f + (float)k * 0.25f, 1.0f);
        float a = (float)k * 2.1f + 0.7f;
        float rr = z->radius * cam->z * 0.55f * (0.4f + 0.6f * (float)((k * 5) % 4) / 3.0f);
        gfx_fill_circle(g, s.x + cosf(a) * rr, s.y + sinf(a) * rr - ph * 6.0f * cam->z,
                        maxf(1.5f, 3.0f * cam->z * (1.0f - ph)), col_a(c, (u8)(150 * (1.0f - ph))));
    }
}

static void draw_zones(Gfx *g, const Run *run, const Cam *cam, double time) {
    for (int i = 0; i < MAX_ZONES; i++) {
        const Zone *z = &run->zones[i];
        if (!z->active || !visible(cam, z->pos, z->radius + 40.0f)) {
            continue;
        }
        float pulse = 0.5f + 0.5f * sinf((float)time * 5.0f + (float)i);
        switch (z->kind) {
        case ZONE_ENEMY_TELE: {
            float frac = z->delay_total > 0.001f ? clampf(1.0f - z->delay / z->delay_total, 0.0f, 1.0f) : 1.0f;
            draw_zone_shape(g, cam, z, RGBA(220, 30, 30, (u8)(40 + 50 * frac)), RGBA(235, 50, 40, 200), frac);
            break;
        }
        case ZONE_ENEMY_POOL: {
            Color c = world_color(z->color, 255, 0xB0402A);
            draw_zone_shape(g, cam, z, col_a(c, (u8)(60 + 25 * pulse)), col_a(c, 150), 1.0f);
            draw_pool_bubbles(g, cam, z, time, c);
            break;
        }
        case ZONE_POISON_CLOUD: {
            Color c = world_color(z->color, 255, 0x9B59B6);
            draw_zone_shape(g, cam, z, col_a(c, (u8)(55 + 30 * pulse)), col_a(c, 120), 1.0f);
            draw_pool_bubbles(g, cam, z, time, c);
            break;
        }
        case ZONE_HEAL_SPRING: {
            Vec2 s = to_scr(cam, z->pos);
            gfx_set_additive(g, true);
            gfx_fill_circle(g, s.x, s.y, z->radius * cam->z, col_a(COL_HEAL, (u8)(35 + 30 * pulse)));
            gfx_set_additive(g, false);
            gfx_draw_circle(g, s.x, s.y, z->radius * cam->z, col_a(COL_HEAL, 140), 2.0f);
            break;
        }
        default: { /* ZONE_HERO_AREA */
            Color c = world_color(z->color, 255, 0xF4ECD6);
            float frac = z->delay > 0.0f && z->delay_total > 0.001f ? 1.0f - z->delay / z->delay_total : 1.0f;
            draw_zone_shape(g, cam, z, col_a(c, z->delay > 0.0f ? 35 : (u8)(70 + 30 * pulse)), col_a(c, 190),
                            clampf(frac, 0.0f, 1.0f));
            break;
        }
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Totems, Pickups, Projektile                                                */
/* ------------------------------------------------------------------------ */

static Color totem_color(int type) {
    switch (type) {
    case TOTEM_TURTLE: return RGB(110, 170, 90);
    case TOTEM_STORM_DRAGON: return RGB(130, 200, 255);
    case TOTEM_RED_DRAGON: return RGB(220, 80, 60);
    case TOTEM_WAR_DRUM: return RGB(200, 140, 70);
    case TOTEM_CLAIRVOYANCE: return RGB(170, 120, 220);
    case TOTEM_GALE_HAWK: return RGB(120, 200, 190);
    default: return COL_PARCHMENT_LIGHT;
    }
}

static void draw_totems(Gfx *g, const Run *run, const Cam *cam, double time) {
    for (int i = 0; i < MAX_TOTEMS; i++) {
        const Totem *t = &run->totems[i];
        if (!t->active || !visible(cam, t->pos, t->radius + 30.0f)) {
            continue;
        }
        Color c = totem_color(t->type);
        Vec2 s = to_scr(cam, t->pos);
        float pulse = 0.5f + 0.5f * sinf((float)time * 3.0f + (float)i);
        gfx_fill_circle(g, s.x, s.y, t->radius * cam->z, col_a(c, 18));
        gfx_draw_circle(g, s.x, s.y, t->radius * cam->z, col_a(c, (u8)(70 + 50 * pulse)), 1.5f);
        float w = 12.0f * cam->z, h = 22.0f * cam->z;
        Rect body = rect_make((int)(s.x - w * 0.5f), (int)(s.y - h * 0.7f), (int)w, (int)h);
        gfx_fill_rect(g, body, COL_WOOD);
        gfx_draw_rect(g, body, COL_INK, 2);
        gfx_set_additive(g, true);
        gfx_fill_circle(g, s.x, s.y - h * 0.8f, 9.0f * cam->z, col_a(c, 90));
        gfx_set_additive(g, false);
        gfx_fill_circle(g, s.x, s.y - h * 0.8f, 6.0f * cam->z, c);
        gfx_draw_circle(g, s.x, s.y - h * 0.8f, 6.0f * cam->z, COL_INK, 1.5f);
        if (t->life_total > 0.01f) { /* Restdauer */
            Rect br = rect_make((int)(s.x - 10.0f * cam->z), (int)(s.y + h * 0.35f), (int)(20.0f * cam->z), 3);
            gfx_fill_rect(g, br, col_a(COL_INK, 150));
            br.w = (int)((float)br.w * clampf(t->life / t->life_total, 0.0f, 1.0f));
            gfx_fill_rect(g, br, c);
        }
    }
}

static void draw_pickups(Gfx *g, const Run *run, const Cam *cam, double time) {
    for (int i = 0; i < MAX_PICKUPS; i++) {
        const Pickup *p = &run->pickups[i];
        if (!p->active || !visible(cam, p->pos, 40.0f)) {
            continue;
        }
        Vec2 s = to_scr(cam, p->pos);
        float pop = clampf(p->age / 0.25f, 0.0f, 1.0f);
        float size = 26.0f * cam->z * (0.4f + 0.6f * pop);
        float bob = sinf(p->age * 3.0f) * 2.0f * cam->z;
        Color tc = gfx_tier_color(g, p->tier, 255);
        gfx_set_additive(g, true);
        gfx_fill_circle(g, s.x, s.y, size * (0.8f + 0.1f * sinf(p->age * 4.0f)), col_a(tc, p->tier >= TIER_RARE ? 70 : 40));
        gfx_set_additive(g, false);
        gfx_fill_circle(g, s.x, s.y + size * 0.45f, size * 0.55f, col_a(COL_INK, 50)); /* Schatten */
        icon_draw_chest(g, rect_make((int)(s.x - size * 0.5f), (int)(s.y - size * 0.5f - bob), (int)size, (int)size),
                        p->tier, time);
    }
}

static Color proj_color(const Projectile *p) {
    if (p->color) {
        return color_from_rgb(p->color, 255);
    }
    return p->kind == PK_HERO ? COL_PARCHMENT_LIGHT : RGB(220, 70, 50);
}

static void draw_projectile(Gfx *g, const Projectile *p, const Cam *cam, double time, int idx) {
    Vec2 s = to_scr(cam, p->pos);
    Vec2 d = v2_norm(p->vel);
    Vec2 n = v2(-d.y, d.x);
    float r = maxf(3.0f, p->radius) * cam->z;
    Color c = proj_color(p);
    switch (p->style) {
    case 0: { /* Pfeil */
        float len = 16.0f * cam->z;
        Vec2 tail = v2(s.x - d.x * len, s.y - d.y * len);
        gfx_line(g, tail.x, tail.y, s.x, s.y, COL_INK, 3.0f);
        gfx_line(g, tail.x, tail.y, s.x, s.y, c, 1.5f);
        gfx_fill_triangle(g, v2(s.x + d.x * 5.0f * cam->z, s.y + d.y * 5.0f * cam->z),
                          v2(s.x + n.x * 3.0f * cam->z, s.y + n.y * 3.0f * cam->z),
                          v2(s.x - n.x * 3.0f * cam->z, s.y - n.y * 3.0f * cam->z), c);
        break;
    }
    case 2: { /* Dolch */
        Vec2 q[4] = {v2(s.x + d.x * 8.0f * cam->z, s.y + d.y * 8.0f * cam->z),
                     v2(s.x + n.x * 3.0f * cam->z, s.y + n.y * 3.0f * cam->z),
                     v2(s.x - d.x * 6.0f * cam->z, s.y - d.y * 6.0f * cam->z),
                     v2(s.x - n.x * 3.0f * cam->z, s.y - n.y * 3.0f * cam->z)};
        gfx_fill_poly(g, q, 4, c);
        for (int i = 0; i < 4; i++) {
            gfx_line(g, q[i].x, q[i].y, q[(i + 1) % 4].x, q[(i + 1) % 4].y, COL_INK, 1.5f);
        }
        break;
    }
    case 3: { /* Licht */
        gfx_set_additive(g, true);
        gfx_fill_circle(g, s.x, s.y, r * 2.4f, col_a(c, 60));
        gfx_fill_circle(g, s.x, s.y, r * 1.2f, col_a(c, 140));
        gfx_set_additive(g, false);
        gfx_fill_circle(g, s.x, s.y, r * 0.7f, COL_WHITE);
        break;
    }
    case 4: { /* Blitz: Zickzack */
        Vec2 prev = s;
        for (int k = 1; k <= 4; k++) {
            float off = hrand((u32)idx * 31u + (u32)k + (u32)(time * 20.0)) * 6.0f * cam->z;
            Vec2 nxt = v2(s.x - d.x * 7.0f * cam->z * (float)k + n.x * off, s.y - d.y * 7.0f * cam->z * (float)k + n.y * off);
            gfx_line(g, prev.x, prev.y, nxt.x, nxt.y, col_a(c, 230), 2.0f);
            prev = nxt;
        }
        gfx_set_additive(g, true);
        gfx_fill_circle(g, s.x, s.y, r * 1.6f, col_a(c, 90));
        gfx_set_additive(g, false);
        break;
    }
    case 5: { /* Feuer */
        for (int k = 3; k >= 1; k--) { /* Schweif */
            float f = 0.25f * (float)k;
            gfx_fill_circle(g, s.x - d.x * r * 1.6f * (float)k, s.y - d.y * r * 1.6f * (float)k, r * (1.0f - f),
                            RGBA(255, (u8)(120 + 30 * k), 40, (u8)(160 - 40 * k)));
        }
        float fl = 0.85f + 0.3f * sinf((float)time * 25.0f + (float)idx);
        gfx_set_additive(g, true);
        gfx_fill_circle(g, s.x, s.y, r * 2.0f * fl, RGBA(255, 120, 30, 80));
        gfx_set_additive(g, false);
        gfx_fill_circle(g, s.x, s.y, r * fl, RGB(255, 170, 50));
        gfx_fill_circle(g, s.x, s.y, r * 0.5f, RGB(255, 240, 170));
        break;
    }
    default: /* 1 Kugel */
        gfx_set_additive(g, true);
        gfx_fill_circle(g, s.x, s.y, r * 2.0f, col_a(c, 55));
        gfx_set_additive(g, false);
        gfx_fill_circle(g, s.x, s.y, r, c);
        gfx_draw_circle(g, s.x, s.y, r, COL_INK, 1.5f);
        break;
    }
}

static void draw_projectiles(Gfx *g, const Run *run, const Cam *cam, double time) {
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        const Projectile *p = &run->proj[i];
        if (p->active && visible(cam, p->pos, 40.0f)) {
            draw_projectile(g, p, cam, time, i);
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Token: Monster und Helden                                                  */
/* ------------------------------------------------------------------------ */

/* Fuellt/umrandet eine Monsterform (0 Kreis 1 Quadrat 2 Raute 3 Dreieck 4 Stern 5 Sechseck). */
static void draw_shape(Gfx *g, Vec2 c, float r, int shape, float rot, Color fill, Color ink, float th) {
    if (shape == 0) {
        gfx_fill_circle(g, c.x, c.y, r, fill);
        gfx_draw_circle(g, c.x, c.y, r, ink, th);
        return;
    }
    Vec2 pts[10];
    int n = 6;
    float rad = r;
    float start = rot;
    switch (shape) {
    case 1: n = 4; rad = r * 1.25f; start = rot + PNX_PI * 0.25f; break;
    case 2: n = 4; rad = r * 1.3f; start = rot; break;
    case 3: n = 3; rad = r * 1.3f; start = rot; break;
    case 4: n = 10; rad = r * 1.35f; start = rot - PNX_PI * 0.5f; break;
    default: n = 6; rad = r * 1.05f; start = rot; break;
    }
    for (int i = 0; i < n; i++) {
        float a = start + (float)i * 2.0f * PNX_PI / (float)n;
        float rr = (shape == 4 && (i & 1)) ? rad * 0.5f : rad;
        pts[i] = v2(c.x + cosf(a) * rr, c.y + sinf(a) * rr);
    }
    for (int i = 0; i < n; i++) { /* Faecher aus der Mitte: auch fuer den nicht-konvexen Stern */
        gfx_fill_triangle(g, c, pts[i], pts[(i + 1) % n], fill);
    }
    for (int i = 0; i < n; i++) {
        gfx_line(g, pts[i].x, pts[i].y, pts[(i + 1) % n].x, pts[(i + 1) % n].y, ink, th);
    }
}

static void draw_bar(Gfx *g, float x, float y, float w, float h, float frac, Color fill, Color back) {
    Rect r = rect_make((int)x, (int)y, maxi(2, (int)w), maxi(2, (int)h));
    gfx_fill_rect(g, r, back);
    Rect f = r;
    f.w = (int)((float)r.w * clampf(frac, 0.0f, 1.0f));
    if (f.w > 0) {
        gfx_fill_rect(g, f, fill);
    }
    gfx_draw_rect(g, r, COL_INK, 1);
}

/* Tier-Aura hinter dem Token (Glut, Puls, Schimmer). */
static void draw_tier_aura_back(Gfx *g, const Enemy *e, Vec2 s, float r, double time, int idx, const Gfx *gc) {
    float t = (float)time + (float)idx * 0.37f;
    Color tc = gfx_tier_color(gc, e->tier, 255);
    gfx_set_additive(g, true);
    switch (e->tier) {
    case TIER_RARE:
        gfx_fill_circle(g, s.x, s.y, r * (1.5f + 0.25f * sinf(t * 4.0f)), col_a(tc, 70));
        break;
    case TIER_UNIQUE:
        gfx_fill_circle(g, s.x, s.y, r * 1.6f, col_a(tc, 55));
        for (int k = 0; k < 5; k++) { /* aufsteigende Glut */
            float ph = fmodf(t * 0.8f + (float)k * 0.2f, 1.0f);
            gfx_fill_circle(g, s.x + sinf((float)k * 7.3f + t) * r * 0.9f, s.y + r * 0.4f - ph * r * 2.0f,
                            maxf(1.0f, r * 0.14f * (1.0f - ph)), RGBA(255, 150, 40, (u8)(220 * (1.0f - ph))));
        }
        break;
    case TIER_BOSS:
        gfx_fill_circle(g, s.x, s.y, r * (1.8f + 0.15f * sinf(t * 3.0f)), col_a(tc, 80));
        gfx_fill_circle(g, s.x, s.y, r * (1.3f + 0.1f * sinf(t * 5.0f + 1.0f)), col_a(tc, 70));
        break;
    default:
        break;
    }
    gfx_set_additive(g, false);
}

/* Tier-Rand und Glitzer vor dem Token. */
static void draw_tier_aura_front(Gfx *g, const Enemy *e, Vec2 s, float r, double time, int idx, const Gfx *gc) {
    float t = (float)time + (float)idx * 0.37f;
    Color tc = gfx_tier_color(gc, e->tier, 255);
    switch (e->tier) {
    case TIER_MAGIC:
        gfx_draw_circle(g, s.x, s.y, r * 1.18f, tc, 2.5f);
        break;
    case TIER_RARE:
        gfx_draw_circle(g, s.x, s.y, r * 1.18f, tc, 2.5f);
        break;
    case TIER_UNIQUE:
        gfx_draw_circle(g, s.x, s.y, r * 1.2f, tc, 2.5f);
        break;
    case TIER_BOSS:
        gfx_draw_circle(g, s.x, s.y, r * 1.2f, tc, 3.0f);
        break;
    case TIER_LEGENDARY:
        gfx_draw_circle(g, s.x, s.y, r * 1.2f, tc, 2.5f);
        gfx_set_additive(g, true);
        for (int k = 0; k < 6; k++) { /* Glitzer */
            float a = t * 1.3f + (float)k * 1.047f;
            float tw = 0.5f + 0.5f * sinf(t * 5.0f + (float)k * 2.1f);
            float px = s.x + cosf(a) * r * 1.5f, py = s.y + sinf(a) * r * 1.5f, sz = r * 0.28f * tw + 1.0f;
            Color gc = RGBA(255, 190, 235, (u8)(100 + 150 * tw));
            gfx_line(g, px - sz, py, px + sz, py, gc, 1.5f);
            gfx_line(g, px, py - sz, px, py + sz, gc, 1.5f);
        }
        gfx_set_additive(g, false);
        break;
    default:
        break;
    }
}

static void draw_affix_rings(Gfx *g, const Enemy *e, Vec2 s, float r) {
    if (e->affixes & AFFIX_FROST_AURA) {
        gfx_draw_circle(g, s.x, s.y, r * 2.4f, RGBA(140, 210, 255, 90), 1.5f);
    }
    if (e->affixes & AFFIX_FIRE_AURA) {
        gfx_draw_circle(g, s.x, s.y, r * 2.4f, RGBA(255, 130, 40, 90), 1.5f);
    }
    if (e->shielded || e->shield_hp > 0.0f) {
        gfx_draw_circle(g, s.x, s.y, r * 1.4f, RGBA(120, 230, 255, 200), 2.0f);
    }
    if (e->invuln > 0.0f) {
        gfx_draw_circle(g, s.x, s.y, r * 1.5f, RGBA(200, 200, 200, 220), 3.0f);
    }
}

static void draw_enemy(Gfx *g, const Run *run, int idx, const Cam *cam, const ViewOpts *opts, double time) {
    const Enemy *e = &run->enemies[idx];
    const MonsterDef *md = (e->def >= 0 && e->def < g_monster_count) ? &g_monsters[e->def] : NULL;
    Vec2 s = to_scr(cam, e->u.pos);
    float r = e->u.radius * cam->z * (1.0f + 0.12f * clampf(e->u.attack_anim, 0.0f, 1.0f));
    Color base = md ? gfx_cb(g, color_from_rgb(md->color, 255)) : RGB(160, 80, 80);
    int shape = md ? md->shape : 0;
    if (e->state == ES_DORMANT) {
        base = shade(base, 0.85f);
    }
    Color fill = color_mix(base, COL_WHITE, clampf(e->u.hit_flash, 0.0f, 1.0f) * 0.8f);
    draw_tier_aura_back(g, e, s, r, time, idx, g);
    gfx_fill_circle(g, s.x, s.y + r * 0.7f, r * 0.9f, col_a(COL_INK, 45)); /* Bodenschatten */
    float rot = e->u.face.x != 0.0f || e->u.face.y != 0.0f ? atan2f(e->u.face.y, e->u.face.x) : 0.0f;
    draw_shape(g, s, r, shape, shape == 3 ? rot : 0.0f, fill, COL_INK, 2.0f);
    gfx_fill_circle(g, s.x - r * 0.25f, s.y - r * 0.3f, r * 0.28f, col_a(COL_WHITE, 55)); /* Glanzpunkt */
    gfx_fill_circle(g, s.x + e->u.face.x * r * 0.5f, s.y + e->u.face.y * r * 0.5f, maxf(1.5f, r * 0.16f), COL_INK);
    draw_tier_aura_front(g, e, s, r, time, idx, g);
    draw_affix_rings(g, e, s, r);
    if (e->telegraphing) {
        gfx_draw_circle(g, s.x, s.y, r * 1.35f, RGBA(255, 60, 40, 200), 2.0f);
    }
    if (opts->colorblind_symbols && e->tier != TIER_NORMAL) {
        int n = g_tiers[e->tier].symbol;
        icon_draw_tier_diamonds(g, (int)(s.x - (float)n * 4.0f), (int)(s.y - r - 16.0f), 8, e->tier, COL_WHITE);
    }
    bool show_bar = opts->show_hp_bars && !e->is_boss && (e->u.hp < e->u.max_hp || e->tier >= TIER_RARE);
    if (show_bar && e->u.max_hp > 0.0f) {
        float w = maxf(22.0f, r * 2.2f);
        draw_bar(g, s.x - w * 0.5f, s.y - r - 9.0f, w, 4.0f, e->u.hp / e->u.max_hp, COL_HP, COL_HP_DARK);
    }
    if (opts->show_names && md && (e->tier >= TIER_RARE) && !e->is_boss) {
        int tw = gfx_text_width(g, md->name, 12);
        gfx_text_shadow(g, (int)s.x - tw / 2, (int)(s.y - r - 24.0f), md->name, gfx_tier_color(g, e->tier, 255), 12);
    }
}

static void draw_enemies(Gfx *g, const Run *run, const Cam *cam, const ViewOpts *opts, double time) {
    for (int i = 0; i < run->enemy_count && i < MAX_ENEMIES; i++) {
        const Enemy *e = &run->enemies[i];
        if (!e->u.alive || e->state == ES_DEAD || !visible(cam, e->u.pos, e->u.radius * 3.0f + 40.0f)) {
            continue;
        }
        draw_enemy(g, run, i, cam, opts, time);
    }
}

static void draw_hero_weapon(Gfx *g, const RunHero *h, Vec2 s, float r, double time) {
    float base = atan2f(h->u.face.y, h->u.face.x);
    float swing = clampf(h->u.attack_anim, 0.0f, 1.0f);
    float ang = base + (swing > 0.0f ? (swing - 0.5f) * 1.8f : sinf((float)time * 2.0f + h->u.anim_t) * 0.08f);
    float len = r * (1.6f + 0.3f * swing);
    Vec2 a = v2(s.x + cosf(ang) * r * 0.5f, s.y + sinf(ang) * r * 0.5f);
    Vec2 b = v2(s.x + cosf(ang) * len, s.y + sinf(ang) * len);
    gfx_line(g, a.x, a.y, b.x, b.y, COL_INK, 4.0f);
    gfx_line(g, a.x, a.y, b.x, b.y, COL_PARCHMENT_LIGHT, 2.0f);
}

static void draw_hero_bars(Gfx *g, const RunHero *h, Vec2 s, float r) {
    float w = maxf(30.0f, r * 2.6f);
    float x = s.x - w * 0.5f, y = s.y - r - 13.0f;
    draw_bar(g, x, y, w, 5.0f, h->u.max_hp > 0.0f ? h->u.hp / h->u.max_hp : 0.0f, COL_HP, COL_HP_DARK);
    draw_bar(g, x, y + 6.0f, w, 4.0f, h->max_mana > 0.0f ? h->mana / h->max_mana : 0.0f, COL_MANA_BAR, RGB(20, 40, 70));
}

static void draw_hero(Gfx *g, const Game *game, const Run *run, int slot, int focus, const Cam *cam,
                      const ViewOpts *opts, double time) {
    const RunHero *h = &run->heroes[slot];
    if (!visible(cam, h->u.pos, 80.0f)) {
        return;
    }
    const Hero *hd = (h->hero_idx >= 0 && h->hero_idx < MAX_HEROES) ? &game->heroes[h->hero_idx] : NULL;
    int cls = hd ? clampi(hd->cls, 0, CLASS_COUNT - 1) : 0;
    Vec2 s = to_scr(cam, h->u.pos);
    float r = h->u.radius * cam->z;
    Color cc = gfx_cb(g, color_from_rgb(g_classes[cls].color, 255));
    gfx_fill_circle(g, s.x, s.y + r * 0.7f, r * 0.9f, col_a(COL_INK, 55));
    if (!h->u.alive || h->state == HS_DEAD) { /* Grabstein-Optik */
        gfx_fill_circle(g, s.x, s.y, r, col_a(shade(cc, 0.45f), 150));
        gfx_draw_circle(g, s.x, s.y, r, col_a(COL_INK, 180), 2.0f);
        gfx_line(g, s.x - r * 0.5f, s.y - r * 0.5f, s.x + r * 0.5f, s.y + r * 0.5f, COL_INK, 2.5f);
        gfx_line(g, s.x + r * 0.5f, s.y - r * 0.5f, s.x - r * 0.5f, s.y + r * 0.5f, COL_INK, 2.5f);
        return;
    }
    if (slot == focus) { /* Fokus-Ring */
        float pulse = 0.5f + 0.5f * sinf((float)time * 4.0f);
        gfx_draw_circle(g, s.x, s.y, r * 1.45f, col_a(COL_DMG_CRIT_EDGE, (u8)(110 + 80 * pulse)), 2.0f);
    }
    if (h->cast_t > 0.0f) {
        gfx_set_additive(g, true);
        gfx_fill_circle(g, s.x, s.y, r * 1.7f, col_a(cc, 70));
        gfx_set_additive(g, false);
    }
    draw_hero_weapon(g, h, s, r, time);
    Color fill = color_mix(cc, COL_WHITE, clampf(h->u.hit_flash, 0.0f, 1.0f) * 0.8f);
    gfx_fill_circle(g, s.x, s.y, r, fill);
    gfx_fill_circle(g, s.x - r * 0.25f, s.y - r * 0.3f, r * 0.35f, col_a(COL_WHITE, 70));
    gfx_draw_circle(g, s.x, s.y, r, COL_INK, 2.5f);
    gfx_fill_circle(g, s.x + h->u.face.x * r * 0.45f, s.y + h->u.face.y * r * 0.45f, maxf(1.5f, r * 0.17f), COL_INK);
    if (h->manual) { /* Handsteuerung: kleine Marke */
        gfx_draw_circle(g, s.x, s.y, r * 1.2f, col_a(COL_SKY, 220), 1.5f);
    }
    if (opts->show_hud_bars) {
        draw_hero_bars(g, h, s, r);
    }
    if (opts->show_names && hd) {
        int tw = gfx_text_width(g, hd->name, 12);
        gfx_text_shadow(g, (int)s.x - tw / 2, (int)(s.y + r + 4.0f), hd->name, COL_PARCHMENT_LIGHT, 12);
    }
}

static void draw_heroes(Gfx *g, const Game *game, const Run *run, int focus, const Cam *cam,
                        const ViewOpts *opts, double time) {
    for (int i = 0; i < run->hero_count && i < MAX_TEAM; i++) {
        const RunHero *h = &run->heroes[i];
        if (h->manual && h->manual_has_goal && visible(cam, h->manual_goal, 20.0f)) { /* Zielmarke */
            Vec2 t = to_scr(cam, h->manual_goal);
            gfx_line(g, t.x - 5.0f, t.y - 5.0f, t.x + 5.0f, t.y + 5.0f, col_a(COL_SKY, 220), 2.0f);
            gfx_line(g, t.x + 5.0f, t.y - 5.0f, t.x - 5.0f, t.y + 5.0f, col_a(COL_SKY, 220), 2.0f);
        }
        if (i != focus) {
            draw_hero(g, game, run, i, focus, cam, opts, time);
        }
    }
    if (focus >= 0 && focus < run->hero_count) { /* Fokus-Held zuoberst */
        draw_hero(g, game, run, focus, focus, cam, opts, time);
    }
}

/* ------------------------------------------------------------------------ */
/* Partikel, Zahlen, Overlays                                                 */
/* ------------------------------------------------------------------------ */

static void draw_particles(Gfx *g, const ViewFx *fx, const Cam *cam) {
    for (int pass = 0; pass < 2; pass++) {
        gfx_set_additive(g, pass == 1);
        for (int i = 0; i < FX_MAX_PARTICLES; i++) {
            const Particle *p = &fx->parts[i];
            if (!p->active || p->additive != (pass == 1) || !visible(cam, p->pos, 20.0f)) {
                continue;
            }
            Vec2 s = to_scr(cam, p->pos);
            float t = p->age / maxf(p->life, 0.001f);
            gfx_fill_circle(g, s.x, s.y, maxf(1.0f, p->size * cam->z * (1.0f - 0.5f * t)),
                            col_alpha_f(p->color, 1.0f - t));
        }
    }
    gfx_set_additive(g, false);
}

static void draw_float_texts(Gfx *g, const ViewFx *fx, const Cam *cam, const ViewOpts *opts) {
    if (!opts->show_damage) {
        return;
    }
    for (int i = 0; i < FX_MAX_TEXTS; i++) {
        const FloatText *t = &fx->texts[i];
        if (!t->active || !visible(cam, t->pos, 60.0f)) {
            continue;
        }
        float k = t->age / maxf(t->life, 0.001f);
        float pop = t->crit && t->age < 0.15f ? 1.0f + (0.15f - t->age) * 3.0f : 1.0f;
        int size = maxi(opts->mini ? 10 : 12, (int)((float)t->size * pop * (opts->mini ? 0.8f : 1.0f)));
        u8 alpha = k < 0.65f ? 255 : (u8)(255.0f * (1.0f - k) / 0.35f);
        Vec2 s = to_scr(cam, t->pos);
        int tw = gfx_text_width(g, t->text, size);
        int x = (int)s.x - tw / 2, y = (int)s.y - size / 2;
        if (t->crit) {
            Color edge = col_a(COL_DMG_CRIT_EDGE, alpha);
            for (int d = 0; d < 4; d++) {
                gfx_text(g, x + (d & 1 ? 1 : -1) * 2 + 0, y + (d & 2 ? 1 : -1) * 2, t->text, edge, size);
            }
            gfx_text(g, x, y, t->text, col_a(t->color, alpha), size);
        } else {
            gfx_text(g, x + 1, y + 1, t->text, col_a(COL_INK, (u8)(alpha * 0.8f)), size);
            gfx_text(g, x, y, t->text, col_a(t->color, alpha), size);
        }
    }
}

/* Dunkle Raender: gestufte Streifen mit abnehmender Deckkraft. */
static void draw_vignette(Gfx *g, Rect vp) {
    int depth = mini(vp.w, vp.h) / 5;
    int steps = 14;
    for (int i = 0; i < steps; i++) {
        float f = 1.0f - (float)i / (float)steps;
        Color c = col_a(COL_INK, (u8)(95.0f * f * f));
        int x0 = depth * i / steps, x1 = depth * (i + 1) / steps;
        int th = maxi(1, x1 - x0);
        gfx_fill_rect(g, rect_make(vp.x + x0, vp.y, th, vp.h), c);
        gfx_fill_rect(g, rect_make(vp.x + vp.w - x0 - th, vp.y, th, vp.h), c);
        gfx_fill_rect(g, rect_make(vp.x + x0, vp.y + x0, vp.w - 2 * x0, th), c);
        gfx_fill_rect(g, rect_make(vp.x + x0, vp.y + vp.h - x0 - th, vp.w - 2 * x0, th), c);
    }
}

static void draw_boss_bar(Gfx *g, const Run *run, Rect vp, bool mini) {
    if (!run->boss_spawned || run->boss_dead || run->boss_enemy < 0 || run->boss_enemy >= MAX_ENEMIES) {
        return;
    }
    const Enemy *e = &run->enemies[run->boss_enemy];
    if (!e->u.alive || e->u.max_hp <= 0.0f) {
        return;
    }
    int w = mini ? vp.w * 3 / 4 : clampi(vp.w * 3 / 5, 200, 460);
    int h = mini ? 9 : 16;
    Rect r = rect_make(vp.x + (vp.w - w) / 2, vp.y + (mini ? 8 : 18), w, h);
    gfx_fill_rect(g, rect_inset(r, -3), col_a(COL_WOOD_DARK, 220));
    gfx_fill_rect(g, r, COL_HP_DARK);
    Rect f = r;
    f.w = (int)((float)r.w * clampf(e->u.hp / e->u.max_hp, 0.0f, 1.0f));
    gfx_fill_rect_vgrad(g, f, RGB(220, 70, 55), RGB(140, 30, 25));
    if (e->invuln > 0.0f || e->shielded) {
        gfx_draw_rect(g, r, RGB(150, 230, 255), 2);
    }
    gfx_draw_rect(g, rect_inset(r, -3), COL_INK, 2);
    for (int p = 1; p <= 2; p++) { /* Phasenmarken bei 2/3 und 1/3 */
        int px = r.x + r.w * (3 - p) / 3;
        gfx_fill_rect(g, rect_make(px, r.y, 1, r.h), col_a(COL_INK, 200));
    }
    if (!mini) {
        char buf[MAX_NAME + 24];
        const char *nm = (e->def >= 0 && e->def < g_monster_count) ? g_monsters[e->def].name : "Boss";
        str_format(buf, sizeof(buf), "%s  (Stufe %d, Phase %d)", nm, e->level, e->phase + 1);
        int tw = gfx_text_width(g, buf, 14);
        gfx_text_shadow(g, vp.x + (vp.w - tw) / 2, r.y + r.h + 6, buf, COL_PARCHMENT_LIGHT, 14);
    }
}

/* ------------------------------------------------------------------------ */
/* Hauptfunktion                                                              */
/* ------------------------------------------------------------------------ */

static Vec2 clamp_cam_target(const GameMap *m, Vec2 t, Rect vp, float z) {
    float half_w = (float)vp.w * 0.5f / z, half_h = (float)vp.h * 0.5f / z;
    float mw = (float)(m->w * TILE_SIZE), mh = (float)(m->h * TILE_SIZE);
    t.x = mw <= 2.0f * half_w ? mw * 0.5f : clampf(t.x, half_w, mw - half_w);
    t.y = mh <= 2.0f * half_h ? mh * 0.5f : clampf(t.y, half_h, mh - half_h);
    return t;
}

/* Smooth-Follow auf den Fokus-Helden; springt bei Kartenwechsel/Teleport. */
static void update_camera(ViewFx *fx, const Run *run, int focus, Rect vp) {
    Vec2 target = run->map.start;
    if (focus >= 0 && focus < run->hero_count) {
        target = run->heroes[focus].u.pos;
    }
    target = clamp_cam_target(&run->map, target, vp, cam_zoom(fx));
    bool snap = !fx->cam_init || fx->last_map != run->map_idx || v2_dist(fx->cam, target) > 900.0f;
    if (snap) {
        fx->cam = target;
        fx->cam_init = true;
        fx->last_map = run->map_idx;
        return;
    }
    float dt = fx->frame_dt > 0.0f ? fx->frame_dt : 1.0f / 60.0f;
    float k = 1.0f - expf(-7.0f * dt);
    fx->cam = v2(lerpf(fx->cam.x, target.x, k), lerpf(fx->cam.y, target.y, k));
}

void view_draw_run(Gfx *g, const Game *game, const Run *run, int slot, ViewFx *fx, Rect vp,
                   const ViewOpts *opts) {
    static const ViewOpts defaults = {false, true, false, true, true, true, false};
    if (!g || !game || !run || !fx || vp.w <= 0 || vp.h <= 0) {
        return;
    }
    if (!opts) {
        opts = &defaults;
    }
    if (run->map.w <= 0 || run->map.h <= 0 || !run->map.tiles) {
        return;
    }
    int focus = (slot >= 0 && slot < run->hero_count) ? slot : (run->hero_count > 0 ? 0 : -1);
    update_camera(fx, run, focus, vp);
    Cam cam = make_cam(fx, vp);

    gfx_push_clip(g, vp);
    gfx_fill_rect(g, vp, color_mix(COL_INK, run->def ? color_from_rgb(run->def->tint, 255) : COL_INK, 0.1f));
    draw_tiles(g, run, &cam, fx->time, opts->mini);
    draw_arena_border(g, run, &cam, fx->time);
    draw_zones(g, run, &cam, fx->time);
    draw_totems(g, run, &cam, fx->time);
    draw_pickups(g, run, &cam, fx->time);
    draw_enemies(g, run, &cam, opts, fx->time);
    draw_heroes(g, game, run, focus, &cam, opts, fx->time);
    draw_projectiles(g, run, &cam, fx->time);
    draw_particles(g, fx, &cam);
    draw_float_texts(g, fx, &cam, opts);
    draw_vignette(g, vp);
    if (focus >= 0 && (!run->heroes[focus].u.alive || run->heroes[focus].state == HS_DEAD)) {
        gfx_fill_rect(g, vp, RGBA(60, 20, 20, 70)); /* gefallener Held: roetlich-duester */
    }
    if (opts->show_boss_bar) {
        draw_boss_bar(g, run, vp, opts->mini);
    }
    gfx_pop_clip(g);
}
