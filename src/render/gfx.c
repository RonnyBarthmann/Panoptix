/* SPDX-License-Identifier: GPL-3.0-or-later */
/* 2D-Zeichenschicht ueber SDL2 (siehe gfx.h). */
#include "render/gfx.h"

#include <SDL.h>
#include <math.h>
#include <stdlib.h>

#include <string.h>

#include "core/util.h"
#include "data/data.h"
#include "render/font.h"
#include "render/theme.h"

#define CLIP_STACK_MAX 8
#define MAX_FAN_SEGMENTS 96

struct GfxImpl {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *font_tex;
    Rect clip_stack[CLIP_STACK_MAX];
    int clip_depth;
    bool additive;
};

/* Zeilenhoehe des Atlas (Ascent + Descent) in Atlas-Pixeln. */
static float font_line_height(void) { return (float)g_font_ascent + (float)g_font_base_size * 0.25f; }

/* ------------------------------------------------------------------------ */
/* Init / Shutdown                                                            */
/* ------------------------------------------------------------------------ */

static SDL_Texture *build_font_texture(SDL_Renderer *ren) {
    int w = g_font_atlas_w, h = g_font_atlas_h;
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surf) {
        return NULL;
    }
    u8 *pixels = (u8 *)surf->pixels;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            u8 *p = pixels + y * surf->pitch + x * 4;
            p[0] = p[1] = p[2] = 255;
            p[3] = g_font_atlas[y * w + x];
        }
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_Texture *tex = SDL_CreateTextureFromSurface(ren, surf);
    SDL_FreeSurface(surf);
    if (tex) {
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    }
    return tex;
}

bool gfx_init(Gfx *g, const char *title, int w, int h, bool fullscreen, bool vsync, bool headless) {
    g->impl = (struct GfxImpl *)calloc(1, sizeof(struct GfxImpl));
    if (!g->impl) {
        return false;
    }
    g->ui_scale = 1.0f;
    g->colorblind = CB_OFF;
    g->headless = headless;
    if (headless) {
        SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
        SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    }
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0 && SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        log_error("SDL Video-Init fehlgeschlagen: %s", SDL_GetError());
        free(g->impl);
        g->impl = NULL;
        return false;
    }
    Uint32 wflags = SDL_WINDOW_RESIZABLE;
    if (fullscreen && !headless) {
        wflags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
    g->impl->window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, wflags);
    if (!g->impl->window) {
        log_error("Fenster konnte nicht erstellt werden: %s", SDL_GetError());
        return false;
    }
    Uint32 rflags = headless ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED;
    if (vsync && !headless) {
        rflags |= SDL_RENDERER_PRESENTVSYNC;
    }
    g->impl->renderer = SDL_CreateRenderer(g->impl->window, -1, rflags);
    if (!g->impl->renderer) {
        /* Fallback auf Software-Renderer */
        g->impl->renderer = SDL_CreateRenderer(g->impl->window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!g->impl->renderer) {
        log_error("Renderer konnte nicht erstellt werden: %s", SDL_GetError());
        return false;
    }
    SDL_SetRenderDrawBlendMode(g->impl->renderer, SDL_BLENDMODE_BLEND);
    g->impl->font_tex = build_font_texture(g->impl->renderer);
    if (!g->impl->font_tex) {
        log_error("Schrift-Textur fehlgeschlagen: %s", SDL_GetError());
        return false;
    }
    gfx_update_size(g);
    return true;
}

void gfx_shutdown(Gfx *g) {
    if (!g->impl) {
        return;
    }
    if (g->impl->font_tex) {
        SDL_DestroyTexture(g->impl->font_tex);
    }
    if (g->impl->renderer) {
        SDL_DestroyRenderer(g->impl->renderer);
    }
    if (g->impl->window) {
        SDL_DestroyWindow(g->impl->window);
    }
    free(g->impl);
    g->impl = NULL;
}

void gfx_set_fullscreen(Gfx *g, bool fullscreen) {
    if (g->headless) {
        return;
    }
    SDL_SetWindowFullscreen(g->impl->window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    gfx_update_size(g);
}

void gfx_update_size(Gfx *g) {
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(g->impl->renderer, &w, &h);
    g->w = w;
    g->h = h;
}

void gfx_begin_frame(Gfx *g, Color clear) {
    SDL_Renderer *r = g->impl->renderer;
    g->impl->clip_depth = 0;
    SDL_RenderSetClipRect(r, NULL);
    g->impl->additive = false;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, clear.r, clear.g, clear.b, 255);
    SDL_RenderClear(r);
}

void gfx_end_frame(Gfx *g) { SDL_RenderPresent(g->impl->renderer); }

/* ------------------------------------------------------------------------ */
/* Clipping                                                                   */
/* ------------------------------------------------------------------------ */

static void apply_clip(Gfx *g) {
    if (g->impl->clip_depth == 0) {
        SDL_RenderSetClipRect(g->impl->renderer, NULL);
    } else {
        Rect c = g->impl->clip_stack[g->impl->clip_depth - 1];
        SDL_Rect sr = {c.x, c.y, c.w < 0 ? 0 : c.w, c.h < 0 ? 0 : c.h};
        SDL_RenderSetClipRect(g->impl->renderer, &sr);
    }
}

void gfx_push_clip(Gfx *g, Rect r) {
    struct GfxImpl *im = g->impl;
    if (im->clip_depth > 0) {
        Rect p = im->clip_stack[im->clip_depth - 1];
        int x0 = maxi(r.x, p.x), y0 = maxi(r.y, p.y);
        int x1 = mini(r.x + r.w, p.x + p.w), y1 = mini(r.y + r.h, p.y + p.h);
        r = rect_make(x0, y0, maxi(0, x1 - x0), maxi(0, y1 - y0));
    }
    if (im->clip_depth < CLIP_STACK_MAX) {
        im->clip_stack[im->clip_depth++] = r;
    }
    apply_clip(g);
}

void gfx_pop_clip(Gfx *g) {
    if (g->impl->clip_depth > 0) {
        g->impl->clip_depth--;
    }
    apply_clip(g);
}

/* ------------------------------------------------------------------------ */
/* Primitive                                                                  */
/* ------------------------------------------------------------------------ */

static SDL_Vertex make_vertex(float x, float y, Color c) {
    SDL_Vertex v;
    v.position.x = x;
    v.position.y = y;
    v.color.r = c.r;
    v.color.g = c.g;
    v.color.b = c.b;
    v.color.a = c.a;
    v.tex_coord.x = 0;
    v.tex_coord.y = 0;
    return v;
}

void gfx_set_additive(Gfx *g, bool additive) {
    g->impl->additive = additive;
    SDL_SetRenderDrawBlendMode(g->impl->renderer, additive ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
}

void gfx_fill_rect(Gfx *g, Rect r, Color c) {
    if (c.a == 0 || r.w <= 0 || r.h <= 0) {
        return;
    }
    SDL_SetRenderDrawColor(g->impl->renderer, c.r, c.g, c.b, c.a);
    SDL_Rect sr = {r.x, r.y, r.w, r.h};
    SDL_RenderFillRect(g->impl->renderer, &sr);
}

void gfx_draw_rect(Gfx *g, Rect r, Color c, int t) {
    if (t < 1) {
        t = 1;
    }
    gfx_fill_rect(g, rect_make(r.x, r.y, r.w, t), c);
    gfx_fill_rect(g, rect_make(r.x, r.y + r.h - t, r.w, t), c);
    gfx_fill_rect(g, rect_make(r.x, r.y + t, t, r.h - 2 * t), c);
    gfx_fill_rect(g, rect_make(r.x + r.w - t, r.y + t, t, r.h - 2 * t), c);
}

void gfx_fill_rect_vgrad(Gfx *g, Rect r, Color top, Color bottom) {
    if (r.w <= 0 || r.h <= 0) {
        return;
    }
    SDL_Vertex v[4] = {make_vertex((float)r.x, (float)r.y, top),
                       make_vertex((float)(r.x + r.w), (float)r.y, top),
                       make_vertex((float)(r.x + r.w), (float)(r.y + r.h), bottom),
                       make_vertex((float)r.x, (float)(r.y + r.h), bottom)};
    int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(g->impl->renderer, NULL, v, 4, idx, 6);
}

void gfx_line(Gfx *g, float x1, float y1, float x2, float y2, Color c, float thickness) {
    float dx = x2 - x1, dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) {
        return;
    }
    if (thickness <= 1.0f) {
        SDL_SetRenderDrawColor(g->impl->renderer, c.r, c.g, c.b, c.a);
        SDL_RenderDrawLineF(g->impl->renderer, x1, y1, x2, y2);
        return;
    }
    float nx = -dy / len * thickness * 0.5f, ny = dx / len * thickness * 0.5f;
    SDL_Vertex v[4] = {make_vertex(x1 + nx, y1 + ny, c), make_vertex(x1 - nx, y1 - ny, c),
                       make_vertex(x2 - nx, y2 - ny, c), make_vertex(x2 + nx, y2 + ny, c)};
    int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(g->impl->renderer, NULL, v, 4, idx, 6);
}

static int segments_for_radius(float r) {
    int n = (int)(r * 1.2f) + 12;
    return clampi(n, 12, MAX_FAN_SEGMENTS);
}

void gfx_fill_pie(Gfx *g, float cx, float cy, float radius, float a0, float a1, Color c) {
    if (radius <= 0.5f || c.a == 0) {
        return;
    }
    float span = a1 - a0;
    if (span <= 0) {
        return;
    }
    if (span > 360.0f) {
        span = 360.0f;
    }
    int segs = (int)(segments_for_radius(radius) * (span / 360.0f)) + 1;
    segs = clampi(segs, 1, MAX_FAN_SEGMENTS);
    SDL_Vertex v[MAX_FAN_SEGMENTS + 2];
    int idx[MAX_FAN_SEGMENTS * 3];
    v[0] = make_vertex(cx, cy, c);
    for (int i = 0; i <= segs; i++) {
        float a = (a0 + span * (float)i / (float)segs) * PNX_PI / 180.0f;
        v[i + 1] = make_vertex(cx + cosf(a) * radius, cy + sinf(a) * radius, c);
    }
    for (int i = 0; i < segs; i++) {
        idx[i * 3] = 0;
        idx[i * 3 + 1] = i + 1;
        idx[i * 3 + 2] = i + 2;
    }
    SDL_RenderGeometry(g->impl->renderer, NULL, v, segs + 2, idx, segs * 3);
}

void gfx_fill_circle(Gfx *g, float cx, float cy, float radius, Color c) {
    gfx_fill_pie(g, cx, cy, radius, 0.0f, 360.0f, c);
}

void gfx_arc(Gfx *g, float cx, float cy, float radius, float a0, float a1, Color c, float thickness) {
    float span = a1 - a0;
    if (span <= 0 || radius <= 0.5f || c.a == 0) {
        return;
    }
    if (span > 360.0f) {
        span = 360.0f;
    }
    int segs = clampi((int)(segments_for_radius(radius) * (span / 360.0f)) + 1, 1, MAX_FAN_SEGMENTS);
    float r_out = radius + thickness * 0.5f, r_in = maxf(0.0f, radius - thickness * 0.5f);
    SDL_Vertex v[(MAX_FAN_SEGMENTS + 1) * 2];
    int idx[MAX_FAN_SEGMENTS * 6];
    for (int i = 0; i <= segs; i++) {
        float a = (a0 + span * (float)i / (float)segs) * PNX_PI / 180.0f;
        float ca = cosf(a), sa = sinf(a);
        v[i * 2] = make_vertex(cx + ca * r_out, cy + sa * r_out, c);
        v[i * 2 + 1] = make_vertex(cx + ca * r_in, cy + sa * r_in, c);
    }
    for (int i = 0; i < segs; i++) {
        int b = i * 2;
        idx[i * 6 + 0] = b;
        idx[i * 6 + 1] = b + 1;
        idx[i * 6 + 2] = b + 2;
        idx[i * 6 + 3] = b + 1;
        idx[i * 6 + 4] = b + 3;
        idx[i * 6 + 5] = b + 2;
    }
    SDL_RenderGeometry(g->impl->renderer, NULL, v, (segs + 1) * 2, idx, segs * 6);
}

void gfx_draw_circle(Gfx *g, float cx, float cy, float radius, Color c, float thickness) {
    gfx_arc(g, cx, cy, radius, 0.0f, 360.0f, c, thickness < 1.0f ? 1.0f : thickness);
}

void gfx_fill_triangle(Gfx *g, Vec2 a, Vec2 b, Vec2 c, Color col) {
    SDL_Vertex v[3] = {make_vertex(a.x, a.y, col), make_vertex(b.x, b.y, col), make_vertex(c.x, c.y, col)};
    SDL_RenderGeometry(g->impl->renderer, NULL, v, 3, NULL, 0);
}

void gfx_fill_poly(Gfx *g, const Vec2 *pts, int n, Color c) {
    if (n < 3 || n > 32) {
        return;
    }
    SDL_Vertex v[32];
    int idx[30 * 3];
    for (int i = 0; i < n; i++) {
        v[i] = make_vertex(pts[i].x, pts[i].y, c);
    }
    for (int i = 0; i < n - 2; i++) {
        idx[i * 3] = 0;
        idx[i * 3 + 1] = i + 1;
        idx[i * 3 + 2] = i + 2;
    }
    SDL_RenderGeometry(g->impl->renderer, NULL, v, n, idx, (n - 2) * 3);
}

/* ------------------------------------------------------------------------ */
/* Text                                                                       */
/* ------------------------------------------------------------------------ */

static float text_scale(int size) { return (float)size / font_line_height(); }

int gfx_text_height(Gfx *g, int size) {
    (void)g;
    return size;
}

int gfx_text_width(Gfx *g, const char *text, int size) {
    (void)g;
    float scale = text_scale(size), w = 0.0f;
    const char *p = text;
    int cp;
    while ((cp = font_next_codepoint(&p)) != 0) {
        const FontGlyph *gl = font_find_glyph(cp);
        if (!gl) {
            gl = font_find_glyph('?');
        }
        w += gl->advance * scale;
    }
    return (int)(w + 0.5f);
}

void gfx_text(Gfx *g, int x, int y, const char *text, Color c, int size) {
    if (!text || c.a == 0) {
        return;
    }
    SDL_Renderer *ren = g->impl->renderer;
    SDL_Texture *tex = g->impl->font_tex;
    float scale = text_scale(size);
    float pen = (float)x;
    float baseline = (float)y + (float)g_font_ascent * scale;
    SDL_SetTextureColorMod(tex, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(tex, c.a);
    const char *p = text;
    int cp;
    while ((cp = font_next_codepoint(&p)) != 0) {
        const FontGlyph *gl = font_find_glyph(cp);
        if (!gl) {
            gl = font_find_glyph('?');
        }
        if (gl->w > 0 && gl->h > 0) {
            SDL_Rect src = {gl->x, gl->y, gl->w, gl->h};
            SDL_FRect dst = {floorf(pen + (float)gl->xoff * scale), floorf(baseline + (float)gl->yoff * scale),
                             (float)gl->w * scale, (float)gl->h * scale};
            SDL_RenderCopyF(ren, tex, &src, &dst);
        }
        pen += gl->advance * scale;
    }
}

void gfx_text_shadow(Gfx *g, int x, int y, const char *text, Color c, int size) {
    int off = size >= 20 ? 2 : 1;
    gfx_text(g, x + off, y + off, text, RGBA(0, 0, 0, (u8)(c.a * 3 / 4)), size);
    gfx_text(g, x, y, text, c, size);
}

void gfx_text_centered(Gfx *g, Rect r, const char *text, Color c, int size) {
    int w = gfx_text_width(g, text, size);
    gfx_text(g, r.x + (r.w - w) / 2, r.y + (r.h - size) / 2, text, c, size);
}

/* Umbruch an Leerzeichen und '\n'; zeichnet optional. Gibt Hoehe zurueck. */
int gfx_text_wrapped(Gfx *g, Rect r, const char *text, Color c, int size, bool draw) {
    int line_h = size + size / 5;
    int y = 0;
    char line[256];
    int len = 0;
    const char *p = text;
    while (*p) {
        /* naechstes Wort bis Leerzeichen/Newline/Ende */
        const char *w = p;
        while (*p && *p != ' ' && *p != '\n') {
            p++;
        }
        int wlen = (int)(p - w);
        char word[128];
        wlen = mini(wlen, (int)sizeof word - 1);
        for (int i = 0; i < wlen; i++) {
            word[i] = w[i];
        }
        word[wlen] = 0;
        char trial[256];
        str_format(trial, sizeof trial, "%s%s%s", line, len > 0 ? " " : "", word);
        if (len > 0 && gfx_text_width(g, trial, size) > r.w) {
            if (draw) {
                gfx_text(g, r.x, r.y + y, line, c, size);
            }
            y += line_h;
            str_copy(line, sizeof line, word);
            len = (int)strlen(line);
        } else {
            str_copy(line, sizeof line, trial);
            len = (int)strlen(line);
        }
        if (*p == '\n') {
            if (draw) {
                gfx_text(g, r.x, r.y + y, line, c, size);
            }
            y += line_h;
            line[0] = 0;
            len = 0;
            p++;
        } else if (*p == ' ') {
            p++;
        }
    }
    if (len > 0) {
        if (draw) {
            gfx_text(g, r.x, r.y + y, line, c, size);
        }
        y += line_h;
    }
    return y;
}

/* ------------------------------------------------------------------------ */
/* Farbenblind-Modi                                                           */
/* ------------------------------------------------------------------------ */

/* Farbtransformationen verschieben Rot/Gruen-Unterschiede in Blau/Gelb. */
Color gfx_cb(const Gfx *g, Color c) {
    float r = c.r, gr = c.g, b = c.b, nr, ng, nb;
    switch (g->colorblind) {
    case CB_DEUTERAN:
    case CB_PROTAN:
        nr = 0.35f * r + 0.65f * gr;
        ng = 0.35f * r + 0.65f * gr;
        nb = b + 0.7f * (r - gr);
        break;
    case CB_TRITAN:
        nr = r;
        ng = 0.6f * gr + 0.4f * b;
        nb = 0.6f * b + 0.4f * gr;
        break;
    default:
        return c;
    }
    c.r = (u8)clampf(nr, 0, 255);
    c.g = (u8)clampf(ng, 0, 255);
    c.b = (u8)clampf(nb, 0, 255);
    return c;
}

/* Okabe-Ito-nahe, gut unterscheidbare Tier-Palette fuer Farbenblind-Modi. */
Color gfx_tier_color(const Gfx *g, Tier t, u8 alpha) {
    if (g->colorblind == CB_OFF) {
        return tier_color(t, alpha);
    }
    static const u32 pal[TIER_COUNT] = {0x9AA0A6, 0x0072B2, 0xF0E442, 0xE69F00, 0xD55E00, 0xCC79A7};
    return color_from_rgb(pal[t], alpha);
}

/* ------------------------------------------------------------------------ */
/* Screenshot                                                                 */
/* ------------------------------------------------------------------------ */

bool gfx_screenshot(Gfx *g, const char *path) {
    int w = g->w, h = g->h;
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surf) {
        return false;
    }
    bool ok = false;
    if (SDL_RenderReadPixels(g->impl->renderer, NULL, SDL_PIXELFORMAT_ARGB8888, surf->pixels, surf->pitch) == 0) {
        ok = SDL_SaveBMP(surf, path) == 0;
    }
    SDL_FreeSurface(surf);
    return ok;
}
