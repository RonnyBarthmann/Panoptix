/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Immediate-Mode-Widgets im Pergament-Stil (siehe ui.h). */
#include <string.h>

#include "core/util.h"
#include "render/icons.h"
#include "render/theme.h"
#include "ui/ui.h"
#include "ui/ui_internal.h"

/* ---- Clip-Stapel: begrenzt auch das Hover-Gebiet (Scrollbereiche) ------ */
#define CLIP_MAX 8
static Rect s_clip[CLIP_MAX];
static int s_clip_n = 0;
static bool s_in_modal = false;
static u32 s_last_click_id = 0;
static double s_last_click_time = -10.0;

static bool inside_clip(int x, int y) {
    for (int i = 0; i < s_clip_n; i++) {
        if (!rect_contains(s_clip[i], x, y)) {
            return false;
        }
    }
    return true;
}

/* ---- Rahmen ---------------------------------------------------------- */
void ui_begin_frame(Ui *ui, Gfx *gfx, const UiInput *in, double time, float dt) {
    ui->gfx = gfx;
    ui->in = *in;
    ui->scale = gfx->ui_scale > 0.1f ? gfx->ui_scale : 1.0f;
    ui->time = time;
    ui->anim_dt = dt;
    ui->hot = 0;
    ui->id_depth = 0;
    ui->tip_set = false;
    ui->cursor_blink = (int)(time * 2.0) & 1;
    s_clip_n = 0;
    s_in_modal = false;
    if (!in->lmb_down) {
        ui->active = 0;
    }
}

void ui_push_id(Ui *ui, int n) {
    if (ui->id_depth < UI_ID_STACK) {
        ui->id_stack[ui->id_depth++] = (u32)n * 2654435761u + 17u;
    }
}
void ui_pop_id(Ui *ui) {
    if (ui->id_depth > 0) {
        ui->id_depth--;
    }
}

u32 ui_make_id(const Ui *ui, const char *label, int x, int y) {
    u32 h = 2166136261u;
    for (const char *p = label; p && *p; p++) {
        h = (h ^ (u8)*p) * 16777619u;
    }
    h = (h ^ (u32)x) * 16777619u;
    h = (h ^ (u32)y) * 16777619u;
    for (int i = 0; i < ui->id_depth; i++) {
        h = (h ^ ui->id_stack[i]) * 16777619u;
    }
    return h ? h : 1u;
}

UiClick ui_region(Ui *ui, Rect r, u32 id) {
    UiClick c = {0};
    bool allowed = !ui->modal_open || s_in_modal;
    bool over = allowed && rect_contains(r, ui->in.mx, ui->in.my) && inside_clip(ui->in.mx, ui->in.my);
    c.hovered = over;
    if (over) {
        ui->hot = id;
        if (ui->in.lmb_pressed) {
            ui->active = id;
        }
        if (ui->in.rmb_pressed) {
            c.rclicked = true;
        }
    }
    c.down = ui->active == id && ui->in.lmb_down;
    if (ui->in.lmb_released && ui->active == id && over) {
        c.clicked = true;
        if (s_last_click_id == id && ui->time - s_last_click_time < 0.35) {
            c.double_clicked = true;
        }
        s_last_click_id = id;
        s_last_click_time = ui->time;
    }
    return c;
}

void ui_end_frame(Ui *ui) {
    if (!ui->tip_set) {
        return;
    }
    Gfx *g = ui->gfx;
    int fs = ui_px(ui, FS_SMALL);
    int pad = ui_px(ui, 8);
    int max_w = ui_px(ui, 340);
    /* Hoehe messen */
    int total_h = 0;
    int title_h = 0;
    int text_w = max_w;
    if (ui->tip_title[0]) {
        title_h = gfx_text_height(g, fs + 2) + ui_px(ui, 4);
        int tw = gfx_text_width(g, ui->tip_title, fs + 2);
        if (tw < max_w) {
            text_w = tw > ui_px(ui, 180) ? tw : ui_px(ui, 180);
        }
    }
    Rect body = rect_make(0, 0, text_w, 10000);
    /* Zeilenweise (Zeichen \n) mit Umbruch */
    const char *p = ui->tip;
    char line[160];
    int lines_h = 0;
    while (*p) {
        int n = 0;
        while (p[n] && p[n] != '\n' && n < (int)sizeof line - 1) {
            n++;
        }
        memcpy(line, p, (size_t)n);
        line[n] = 0;
        const char *txt = (line[0] == 1 || line[0] == 2) ? line + 1 : line;
        lines_h += n ? gfx_text_wrapped(g, body, txt, COL_INK, fs, false) : fs / 2;
        p += n;
        if (*p == '\n') {
            p++;
        }
    }
    total_h = title_h + lines_h + pad * 2;
    int w = text_w + pad * 2;
    int x = ui->in.mx + ui_px(ui, 16);
    int y = ui->in.my + ui_px(ui, 16);
    if (x + w > g->w - 4) {
        x = ui->in.mx - w - ui_px(ui, 8);
    }
    if (y + total_h > g->h - 4) {
        y = g->h - total_h - 4;
    }
    if (x < 4) {
        x = 4;
    }
    if (y < 4) {
        y = 4;
    }
    Rect r = rect_make(x, y, w, total_h);
    gfx_fill_rect(g, rect_inset(r, -2), COL_INK);
    gfx_fill_rect(g, r, COL_PARCHMENT_LIGHT);
    Color tc = color_from_rgb(ui->tip_color, 255);
    int cy = y + pad;
    if (ui->tip_title[0]) {
        gfx_fill_rect(g, rect_make(x, y, w, title_h + pad), color_mix(COL_PARCHMENT_DARK, tc, 0.25f));
        gfx_text(g, x + pad, cy, ui->tip_title, COL_INK, fs + 2);
        gfx_fill_rect(g, rect_make(x, y + title_h + pad / 2, w, 2), tc);
        cy += title_h;
    }
    p = ui->tip;
    while (*p) {
        int n = 0;
        while (p[n] && p[n] != '\n' && n < (int)sizeof line - 1) {
            n++;
        }
        memcpy(line, p, (size_t)n);
        line[n] = 0;
        Color col = COL_INK;
        const char *txt = line;
        if (line[0] == 1) {
            col = RGB(0x1E, 0x8E, 0x3E);
            txt = line + 1;
        } else if (line[0] == 2) {
            col = RGB(0xB0, 0x2A, 0x2A);
            txt = line + 1;
        }
        if (n) {
            cy += gfx_text_wrapped(g, rect_make(x + pad, cy, text_w, 10000), txt, col, fs, true);
        } else {
            cy += fs / 2;
        }
        p += n;
        if (*p == '\n') {
            p++;
        }
    }
}

/* ---- Text-Helfer ------------------------------------------------------- */
void ui_text(Ui *ui, int x, int y, const char *text, Color c, int base_size) {
    gfx_text(ui->gfx, x, y, text, c, ui_px(ui, base_size));
}
void ui_text_right(Ui *ui, Rect r, const char *text, Color c, int base_size) {
    int s = ui_px(ui, base_size);
    int w = gfx_text_width(ui->gfx, text, s);
    gfx_text(ui->gfx, r.x + r.w - w, r.y + (r.h - gfx_text_height(ui->gfx, s)) / 2, text, c, s);
}
void ui_text_center(Ui *ui, Rect r, const char *text, Color c, int base_size) {
    gfx_text_centered(ui->gfx, r, text, c, ui_px(ui, base_size));
}
int ui_line_h(const Ui *ui, int base_size) {
    return gfx_text_height(ui->gfx, ui_px(ui, base_size)) + ui_px(ui, 3);
}
void ui_label(Ui *ui, int x, int y, const char *text, Color c, int size) {
    gfx_text(ui->gfx, x, y, text, c, size);
}
void ui_label_right(Ui *ui, int x_right, int y, const char *text, Color c, int size) {
    gfx_text(ui->gfx, x_right - gfx_text_width(ui->gfx, text, size), y, text, c, size);
}
int ui_label_wrapped(Ui *ui, Rect r, const char *text, Color c, int size) {
    return gfx_text_wrapped(ui->gfx, r, text, c, size, true);
}

/* ---- Rect-Schneiden ---------------------------------------------------- */
Rect rect_cut_left(Rect *r, int w) {
    Rect o = rect_make(r->x, r->y, w, r->h);
    r->x += w;
    r->w -= w;
    return o;
}
Rect rect_cut_right(Rect *r, int w) {
    Rect o = rect_make(r->x + r->w - w, r->y, w, r->h);
    r->w -= w;
    return o;
}
Rect rect_cut_top(Rect *r, int h) {
    Rect o = rect_make(r->x, r->y, r->w, h);
    r->y += h;
    r->h -= h;
    return o;
}
Rect rect_cut_bottom(Rect *r, int h) {
    Rect o = rect_make(r->x, r->y + r->h - h, r->w, h);
    r->h -= h;
    return o;
}

/* ---- Panels ------------------------------------------------------------ */
void ui_panel_plain(Ui *ui, Rect r) {
    Gfx *g = ui->gfx;
    gfx_fill_rect(g, r, COL_WOOD_DARK);
    gfx_fill_rect(g, rect_inset(r, 2), COL_WOOD);
    Rect in = rect_inset(r, 4);
    gfx_fill_rect_vgrad(g, in, COL_PARCHMENT_LIGHT, COL_PARCHMENT);
    gfx_draw_rect(g, in, COL_INK_SOFT, 1);
}

Rect ui_panel(Ui *ui, Rect r, const char *title) {
    Gfx *g = ui->gfx;
    ui_panel_plain(ui, r);
    Rect in = rect_inset(r, 4);
    if (!title || !title[0]) {
        return rect_inset(in, ui_px(ui, 6));
    }
    int th = ui_px(ui, 26);
    Rect bar = rect_make(in.x, in.y, in.w, th);
    gfx_fill_rect(g, bar, COL_WOOD);
    gfx_fill_rect(g, rect_make(bar.x, bar.y + bar.h - 2, bar.w, 2), COL_GOLD);
    gfx_text(g, bar.x + ui_px(ui, 10), bar.y + (th - gfx_text_height(g, ui_px(ui, FS_NORMAL))) / 2,
             title, COL_PARCHMENT_LIGHT, ui_px(ui, FS_NORMAL));
    Rect content = rect_make(in.x, in.y + th, in.w, in.h - th);
    return rect_inset(content, ui_px(ui, 6));
}

/* ---- Buttons ----------------------------------------------------------- */
bool ui_button_ex(Ui *ui, Rect r, const char *label, UiStyle style, bool enabled) {
    Gfx *g = ui->gfx;
    u32 id = ui_make_id(ui, label, r.x, r.y);
    UiClick c = ui_region(ui, r, id);
    Color base = COL_WAX_RED;
    Color text = COL_PARCHMENT_LIGHT;
    switch (style) {
    case UI_BTN_GREEN: base = COL_MOSS; break;
    case UI_BTN_FLAT: base = COL_PARCHMENT_DARK; text = COL_INK; break;
    case UI_BTN_TAB: base = COL_WOOD; break;
    case UI_BTN_NORMAL: base = COL_WOOD; break;
    case UI_BTN_WAX: default: break;
    }
    if (!enabled) {
        base = color_mix(base, COL_PARCHMENT_DARK, 0.6f);
        text = COL_INK_SOFT;
    } else if (c.down) {
        base = color_mix(base, COL_BLACK, 0.25f);
    } else if (c.hovered) {
        base = color_mix(base, COL_WHITE, 0.18f);
    }
    gfx_fill_rect(g, rect_inset(r, -1), COL_INK);
    gfx_fill_rect_vgrad(g, r, color_mix(base, COL_WHITE, 0.12f), color_mix(base, COL_BLACK, 0.15f));
    gfx_draw_rect(g, rect_inset(r, 1), color_mix(base, COL_WHITE, 0.3f), 1);
    int fs = ui_px(ui, FS_SMALL);
    while (fs > 8 && gfx_text_width(g, label, fs) > r.w - 6) {
        fs--;
    }
    gfx_text_centered(g, r, label, text, fs);
    return enabled && c.clicked;
}
bool ui_button(Ui *ui, Rect r, const char *label) { return ui_button_ex(ui, r, label, UI_BTN_WAX, true); }
bool ui_button_tip(Ui *ui, Rect r, const char *label, UiStyle style, bool enabled, const char *tip) {
    bool clicked = ui_button_ex(ui, r, label, style, enabled);
    if (tip && tip[0] && rect_contains(r, ui->in.mx, ui->in.my) && inside_clip(ui->in.mx, ui->in.my)) {
        ui_tooltip(ui, NULL, 0, tip);
    }
    return clicked;
}

bool ui_checkbox(Ui *ui, Rect r, const char *label, bool *value) {
    Gfx *g = ui->gfx;
    u32 id = ui_make_id(ui, label, r.x, r.y);
    UiClick c = ui_region(ui, r, id);
    int b = mini(r.h, ui_px(ui, 20));
    Rect box = rect_make(r.x, r.y + (r.h - b) / 2, b, b);
    gfx_fill_rect(g, box, c.hovered ? COL_PARCHMENT_LIGHT : COL_PARCHMENT);
    gfx_draw_rect(g, box, COL_INK, 2);
    if (*value) {
        gfx_line(g, (float)box.x + 4, (float)box.y + b * 0.5f, (float)box.x + b * 0.4f, (float)box.y + b - 4, COL_WAX_RED, 3);
        gfx_line(g, (float)box.x + b * 0.4f, (float)box.y + b - 4, (float)box.x + b - 3, (float)box.y + 3, COL_WAX_RED, 3);
    }
    int fs = ui_px(ui, FS_SMALL);
    gfx_text(g, box.x + b + ui_px(ui, 8), r.y + (r.h - gfx_text_height(g, fs)) / 2, label, COL_INK, fs);
    if (c.clicked) {
        *value = !*value;
        return true;
    }
    return false;
}

bool ui_slider(Ui *ui, Rect r, float *value, float min, float max) {
    Gfx *g = ui->gfx;
    u32 id = ui_make_id(ui, "slider", r.x, r.y);
    UiClick c = ui_region(ui, r, id);
    int ty = r.y + r.h / 2;
    gfx_fill_rect(g, rect_make(r.x, ty - 3, r.w, 6), COL_INK_SOFT);
    float t = max > min ? clampf((*value - min) / (max - min), 0, 1) : 0;
    gfx_fill_rect(g, rect_make(r.x, ty - 3, (int)(r.w * t), 6), COL_GOLD);
    int kx = r.x + (int)(r.w * t);
    gfx_fill_rect(g, rect_make(kx - 5, r.y + 2, 10, r.h - 4), c.hovered || c.down ? COL_WAX_RED : COL_WOOD);
    gfx_draw_rect(g, rect_make(kx - 5, r.y + 2, 10, r.h - 4), COL_INK, 1);
    bool changed = false;
    if (c.down || (ui->active == id && ui->in.lmb_down)) {
        float nt = clampf((float)(ui->in.mx - r.x) / (float)(r.w > 0 ? r.w : 1), 0, 1);
        float nv = min + nt * (max - min);
        if (nv != *value) {
            *value = nv;
            changed = true;
        }
    }
    return changed;
}

bool ui_tabs(Ui *ui, Rect r, const char *const *labels, int count, int *active) {
    bool changed = false;
    int w = count > 0 ? r.w / count : r.w;
    for (int i = 0; i < count; i++) {
        Rect b = rect_make(r.x + i * w, r.y, w - 2, r.h);
        ui_push_id(ui, i);
        bool on = (*active == i);
        if (ui_button_ex(ui, b, labels[i], on ? UI_BTN_WAX : UI_BTN_TAB, true) && !on) {
            *active = i;
            changed = true;
        }
        ui_pop_id(ui);
    }
    return changed;
}

void ui_progress_bar(Ui *ui, Rect r, float frac, Color fill, const char *text) {
    Gfx *g = ui->gfx;
    frac = clampf(frac, 0, 1);
    gfx_fill_rect(g, rect_inset(r, -1), COL_INK);
    gfx_fill_rect(g, r, color_mix(fill, COL_BLACK, 0.7f));
    gfx_fill_rect_vgrad(g, rect_make(r.x, r.y, (int)(r.w * frac), r.h), color_mix(fill, COL_WHITE, 0.2f), fill);
    if (text && text[0]) {
        int fs = ui_px(ui, FS_TINY);
        gfx_text_centered(g, r, text, COL_WHITE, fs);
    }
}

void ui_bar(Ui *ui, Rect r, float frac, Color fill, Color back, const char *text) {
    (void)back;
    ui_progress_bar(ui, r, frac, fill, text);
}

/* ---- Scrollbereich ----------------------------------------------------- */
static float *scroll_slot(Ui *ui, u32 id) {
    for (int i = 0; i < UI_SCROLL_SLOTS; i++) {
        if (ui->scroll_state[i].id == id) {
            return &ui->scroll_state[i].v;
        }
    }
    for (int i = 0; i < UI_SCROLL_SLOTS; i++) {
        if (ui->scroll_state[i].id == 0) {
            ui->scroll_state[i].id = id;
            ui->scroll_state[i].v = 0;
            return &ui->scroll_state[i].v;
        }
    }
    ui->scroll_state[0].id = id;
    ui->scroll_state[0].v = 0;
    return &ui->scroll_state[0].v;
}

int ui_scroll_begin(Ui *ui, Rect r, int content_h, const char *id_text) {
    u32 id = ui_make_id(ui, id_text, 0, 0);
    float *v = scroll_slot(ui, id);
    int max_off = maxi(0, content_h - r.h);
    bool allowed = !ui->modal_open || s_in_modal;
    if (allowed && rect_contains(r, ui->in.mx, ui->in.my) && inside_clip(ui->in.mx, ui->in.my) && ui->in.wheel != 0) {
        *v -= (float)ui->in.wheel * (float)ui_px(ui, 40);
    }
    *v = clampf(*v, 0, (float)max_off);
    if (max_off > 0) {
        int bw = ui_px(ui, 6);
        Rect track = rect_make(r.x + r.w - bw, r.y, bw, r.h);
        gfx_fill_rect(ui->gfx, track, COL_PARCHMENT_DARK);
        int kh = maxi(ui_px(ui, 20), (int)((float)r.h * (float)r.h / (float)content_h));
        int ky = r.y + (int)((float)(r.h - kh) * (*v / (float)max_off));
        gfx_fill_rect(ui->gfx, rect_make(track.x, ky, bw, kh), COL_WOOD);
    }
    gfx_push_clip(ui->gfx, r);
    if (s_clip_n < CLIP_MAX) {
        s_clip[s_clip_n++] = r;
    }
    return (int)*v;
}
void ui_scroll_end(Ui *ui) {
    gfx_pop_clip(ui->gfx);
    if (s_clip_n > 0) {
        s_clip_n--;
    }
}

/* ---- Texteingabe ------------------------------------------------------- */
bool ui_text_input(Ui *ui, Rect r, char *buf, size_t size, const char *id_text) {
    Gfx *g = ui->gfx;
    u32 id = ui_make_id(ui, id_text, r.x, r.y);
    UiClick c = ui_region(ui, r, id);
    if (c.clicked) {
        ui->focus = id;
    } else if (ui->in.lmb_pressed && !c.hovered && ui->focus == id) {
        ui->focus = 0;
    }
    bool changed = false;
    bool focused = ui->focus == id;
    size_t len = strlen(buf);
    if (focused) {
        for (const char *p = ui->in.text; *p; p++) {
            if ((u8)*p >= 32 && *p != 127 && len + 1 < size) {
                buf[len++] = *p;
                buf[len] = 0;
                changed = true;
            }
        }
        if (ui->in.key_pressed[KEY_BACKSPACE] && len > 0) {
            do {
                len--;
            } while (len > 0 && ((u8)buf[len] & 0xC0) == 0x80);
            buf[len] = 0;
            changed = true;
        }
        if (ui->in.key_pressed[KEY_ENTER] || ui->in.key_pressed[KEY_ESCAPE]) {
            ui->focus = 0;
        }
    }
    gfx_fill_rect(g, r, focused ? COL_WHITE : COL_PARCHMENT_LIGHT);
    gfx_draw_rect(g, r, focused ? COL_WAX_RED : COL_INK, 2);
    int fs = ui_px(ui, FS_SMALL);
    int ty = r.y + (r.h - gfx_text_height(g, fs)) / 2;
    gfx_push_clip(g, rect_inset(r, 2));
    gfx_text(g, r.x + 6, ty, buf, COL_INK, fs);
    if (focused && ui->cursor_blink) {
        int cx = r.x + 6 + gfx_text_width(g, buf, fs) + 1;
        gfx_fill_rect(g, rect_make(cx, ty, 2, gfx_text_height(g, fs)), COL_INK);
    }
    gfx_pop_clip(g);
    return changed;
}

bool ui_cycle(Ui *ui, Rect r, const char *const *options, int count, int *value) {
    u32 id = ui_make_id(ui, "cycle", r.x, r.y);
    UiClick c = ui_region(ui, r, id);
    Gfx *g = ui->gfx;
    gfx_fill_rect(g, r, c.hovered ? COL_PARCHMENT_LIGHT : COL_PARCHMENT);
    gfx_draw_rect(g, r, COL_INK, 1);
    int v = clampi(*value, 0, maxi(count - 1, 0));
    char buf[96];
    str_format(buf, sizeof buf, "< %s >", count > 0 ? options[v] : "-");
    gfx_text_centered(g, r, buf, COL_INK, ui_px(ui, FS_SMALL));
    bool changed = false;
    if (count > 0 && c.clicked) {
        *value = (v + 1) % count;
        changed = true;
    } else if (count > 0 && c.rclicked) {
        *value = (v + count - 1) % count;
        changed = true;
    }
    return changed;
}

/* ---- Slots ------------------------------------------------------------- */
UiClick ui_item_slot(Ui *ui, Rect r, const Game *game, const Hero *compare_to, const Item *it,
                     bool selected) {
    Gfx *g = ui->gfx;
    u32 id = ui_make_id(ui, "islot", r.x, r.y);
    UiClick c = ui_region(ui, r, id);
    if (item_is_empty(it)) {
        icon_draw_empty_slot(g, r, SLOT_NONE);
    } else {
        icon_draw_item(g, r, it);
    }
    if (selected) {
        gfx_draw_rect(g, rect_inset(r, -1), COL_GOLD, 2);
    } else if (c.hovered) {
        gfx_draw_rect(g, r, COL_WHITE, 1);
    }
    if (c.hovered && !item_is_empty(it)) {
        ui_tooltip_item(ui, game, compare_to, it);
    }
    return c;
}

UiClick ui_stack_slot(Ui *ui, Rect r, int def, i64 count, bool selected) {
    u32 id = ui_make_id(ui, "sslot", r.x, r.y);
    UiClick c = ui_region(ui, r, id);
    icon_draw_stack(ui->gfx, r, def, count);
    if (selected) {
        gfx_draw_rect(ui->gfx, rect_inset(r, -1), COL_GOLD, 2);
    } else if (c.hovered) {
        gfx_draw_rect(ui->gfx, r, COL_WHITE, 1);
    }
    if (c.hovered && def >= 0 && def < g_item_count) {
        char buf[512];
        itemdef_describe(&g_items[def], buf, sizeof buf);
        char name[MAX_NAME + 16];
        str_format(name, sizeof name, "%s", g_items[def].name);
        ui_tooltip(ui, name, g_tiers[g_items[def].tier].color, buf);
    }
    return c;
}

/* ---- Bestaetigungsdialog ------------------------------------------------ */
int ui_confirm(Ui *ui, bool *open, const char *title, const char *text) {
    if (!*open) {
        return 0;
    }
    Gfx *g = ui->gfx;
    ui->modal_open = true;
    s_in_modal = true;
    gfx_fill_rect(g, rect_make(0, 0, g->w, g->h), RGBA(0, 0, 0, 140));
    int w = ui_px(ui, 420), h = ui_px(ui, 190);
    Rect r = rect_make((g->w - w) / 2, (g->h - h) / 2, w, h);
    Rect in = ui_panel(ui, r, title);
    Rect t = rect_make(in.x, in.y, in.w, in.h - ui_px(ui, 40));
    gfx_text_wrapped(g, t, text, COL_INK, ui_px(ui, FS_SMALL), true);
    int bw = ui_px(ui, 110), bh = ui_px(ui, 32);
    int res = 0;
    if (ui_button_ex(ui, rect_make(in.x + in.w / 2 - bw - 10, in.y + in.h - bh, bw, bh), tr("Ja"), UI_BTN_GREEN, true)) {
        res = 1;
    }
    if (ui_button_ex(ui, rect_make(in.x + in.w / 2 + 10, in.y + in.h - bh, bw, bh), tr("Nein"), UI_BTN_WAX, true)) {
        res = -1;
    }
    if (ui->in.key_pressed[KEY_ESCAPE]) {
        res = -1;
    }
    s_in_modal = false;
    if (res != 0) {
        *open = false;
        ui->modal_open = false;
    }
    return res;
}

/* ---- Tooltip setzen ------------------------------------------------------ */
void ui_tooltip(Ui *ui, const char *title, u32 title_rgb, const char *text) {
    ui->tip_set = true;
    str_copy(ui->tip_title, sizeof ui->tip_title, title ? title : "");
    ui->tip_color = title_rgb ? title_rgb : 0x6B4A2F;
    str_copy(ui->tip, sizeof ui->tip, text ? text : "");
}
