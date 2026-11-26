/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Shop: Tabs, Angebotskarten, Kauf mit Bestaetigung (Spec 13). */
#include <string.h>

#include "ui/ui_internal.h"

#define CONFIRM_DIAMONDS 1000
#define CARD_W 250
#define CARD_H 176

static int s_tab = SHOP_FREE;
static bool s_confirm_open;
static int s_confirm_offer = -1;

/* Anzeige-Verteilung der Zufallsangebote (entspricht den Gewichten in rules/shop.c). */
static const int k_jewel_pct[TIER_COUNT] = {50, 30, 14, 5, 1, 0};
static const int k_book_pct[4] = {60, 30, 9, 1};

/* ---- Wahrscheinlichkeiten ---------------------------------------------------- */
static void odds_text(const ShopOffer *o, char *buf, size_t size) {
    char part[64];
    buf[0] = '\0';
    if (o->kind == SHOPK_RANDOM_RING || o->kind == SHOPK_RANDOM_NECKLACE) {
        for (int t = 0; t < TIER_COUNT; t++) {
            if (k_jewel_pct[t] > 0) {
                str_format(part, sizeof part, "%s%s %d %%", buf[0] ? ", " : "", tr(g_tiers[t].name), k_jewel_pct[t]);
                str_append(buf, size, part);
            }
        }
    } else if (o->kind == SHOPK_RANDOM_BOOK) {
        for (int t = 0; t < 4; t++) {
            str_format(part, sizeof part, "%s%s %d %%", buf[0] ? ", " : "", tr(g_tiers[t].name), k_book_pct[t]);
            str_append(buf, size, part);
        }
    }
}

/* ---- Preis / Limit ------------------------------------------------------------ */
static void draw_price(App *app, Rect r, const ShopOffer *o, i64 price, bool affordable) {
    Ui *ui = &app->ui;
    int s = ui_px(ui, 18);
    if (price <= 0) {
        ui_text(ui, r.x, r.y, tr("Gratis"), COL_MOSS, FS_NORMAL);
        return;
    }
    char n[32];
    icon_draw_currency(&app->gfx, rect_make(r.x, r.y, s, s), o->currency);
    ui_text(ui, r.x + s + ui_px(ui, 4), r.y + ui_px(ui, 1), ui_num(price, n, sizeof n),
            affordable ? COL_INK : COL_HP, FS_NORMAL);
}

static void limit_text(const Game *g, const ShopOffer *o, int idx, char *buf, size_t size) {
    static const char *const per[] = {"", "pro Tag", "pro Woche", "pro Monat"};
    int rem = shop_offer_remaining(g, idx);
    buf[0] = '\0';
    if (rem < 0) {
        str_copy(buf, size, tr("Unbegrenzt"));
    } else if (o->period == PERIOD_NONE) {
        str_format(buf, size, "%s: %d / %d", tr("Rest"), rem, o->limit);
    } else {
        str_format(buf, size, "%s: %d / %d (%s)", tr("Rest"), rem, o->limit, tr(per[o->period]));
    }
}

/* ---- Kauf ---------------------------------------------------------------------- */
static void do_buy(App *app, int idx) {
    char err[128] = "";
    bool ok = shop_buy(&app->game, idx, err);
    char msg[160];
    str_format(msg, sizeof msg, "%s: %s", tr("Gekauft"), tr(g_shop[idx].name));
    ui_result(app, ok, err, msg);
}

static bool needs_confirm(const ShopOffer *o, i64 price) {
    return o->currency == CUR_DIAMONDS && price >= CONFIRM_DIAMONDS;
}

static void handle_confirm(App *app) {
    if (!s_confirm_open || s_confirm_offer < 0) {
        return;
    }
    const ShopOffer *o = &g_shop[s_confirm_offer];
    char text[200], n[32];
    str_format(text, sizeof text, "%s fuer %s Diamanten kaufen?", tr(o->name),
               ui_num(shop_offer_price(&app->game, s_confirm_offer), n, sizeof n));
    int res = ui_confirm(&app->ui, &s_confirm_open, tr("Kauf bestaetigen"), text);
    if (res == 1) {
        do_buy(app, s_confirm_offer);
    }
    if (res != 0) {
        s_confirm_open = false;
        s_confirm_offer = -1;
    }
}

/* ---- Karte ----------------------------------------------------------------------- */
static void draw_card(App *app, Rect c, int idx) {
    Ui *ui = &app->ui;
    Game *g = &app->game;
    const ShopOffer *o = &g_shop[idx];
    char why[96] = "", b[160];
    bool avail = shop_offer_available(g, idx, why, sizeof why);
    i64 price = shop_offer_price(g, idx);
    bool affordable = price <= 0 || currency_can_pay(g, o->currency, price);
    int pad = ui_px(ui, 8);
    ui_panel_plain(ui, c);
    ui_text(ui, c.x + pad, c.y + pad, tr(o->name), COL_INK, FS_NORMAL);
    Rect dr = rect_make(c.x + pad, c.y + pad + ui_px(ui, 22), c.w - 2 * pad, ui_px(ui, 44));
    ui_label_wrapped(ui, dr, tr(o->desc), COL_WOOD, FS_SMALL);
    int y = c.y + pad + ui_px(ui, 66);
    char odds[200];
    odds_text(o, odds, sizeof odds);
    if (odds[0]) {
        ui_label_wrapped(ui, rect_make(c.x + pad, y, c.w - 2 * pad, ui_px(ui, 30)), odds, COL_SKY, FS_TINY);
    }
    y = c.y + c.h - pad - ui_px(ui, 48);
    limit_text(g, o, idx, b, sizeof b);
    ui_text(ui, c.x + pad, y, b, COL_WOOD, FS_TINY);
    if (o->min_player_level > 1) {
        str_format(b, sizeof b, "%s %d", tr("Ab Level"), o->min_player_level);
        ui_text_right(ui, rect_make(c.x, y - ui_px(ui, 1), c.w - pad, ui_px(ui, 14)), b,
                      g->player.level >= o->min_player_level ? COL_WOOD : COL_HP, FS_TINY);
    }
    int by = c.y + c.h - pad - ui_px(ui, 28);
    draw_price(app, rect_make(c.x + pad, by + ui_px(ui, 4), ui_px(ui, 110), ui_px(ui, 20)), o, price, affordable);
    Rect br = rect_make(c.x + c.w - pad - ui_px(ui, 90), by, ui_px(ui, 90), ui_px(ui, 28));
    ui_push_id(ui, idx);
    const char *tip = avail ? NULL : (why[0] ? why : NULL);
    if (ui_button_tip(ui, br, price <= 0 ? tr("Einfordern") : tr("Kaufen"), UI_BTN_WAX,
                      avail && affordable, tip)) {
        if (needs_confirm(o, price)) {
            s_confirm_offer = idx;
            s_confirm_open = true;
        } else {
            do_buy(app, idx);
        }
    }
    ui_pop_id(ui);
    if (odds[0] && rect_contains(c, ui->in.mx, ui->in.my)) {
        ui_tooltip(ui, tr("Wahrscheinlichkeiten"), 0xE9DDBF, odds);
    }
}

static void draw_offers(App *app, Rect r) {
    Ui *ui = &app->ui;
    int cw = ui_px(ui, CARD_W), ch = ui_px(ui, CARD_H), gap = ui_px(ui, 8), bar = ui_px(ui, 12);
    int cols = maxi(1, (r.w - bar) / (cw + gap));
    int idxs[MAX_SHOP_OFFERS], n = 0;
    for (int i = 0; i < g_shop_count; i++) {
        if ((int)g_shop[i].tab == s_tab) {
            idxs[n++] = i;
        }
    }
    int rows = (n + cols - 1) / cols;
    int off = ui_scroll_begin(ui, r, rows * (ch + gap) + gap, "shop_grid");
    for (int k = 0; k < n; k++) {
        Rect c = rect_make(r.x + gap + (k % cols) * (cw + gap), r.y + gap + (k / cols) * (ch + gap) - off, cw, ch);
        if (c.y + ch >= r.y && c.y <= r.y + r.h) {
            draw_card(app, c, idxs[k]);
        }
    }
    ui_scroll_end(ui);
    if (n == 0) {
        ui_text(ui, r.x + gap, r.y + gap, tr("Keine Angebote"), COL_WOOD, FS_NORMAL);
    }
}

/* ---- Screen ------------------------------------------------------------------------ */
void screen_shop(App *app, Rect area) {
    Ui *ui = &app->ui;
    static const char *const labels[SHOP_TAB_COUNT] = {"Gratis", "Diamanten", "Zufallskisten",
                                                       "Allgemein", "Skillbuecher", "Raben"};
    const char *tabs[SHOP_TAB_COUNT];
    shop_check_resets(&app->game);
    for (int i = 0; i < SHOP_TAB_COUNT; i++) {
        tabs[i] = tr(labels[i]);
    }
    Rect in = ui_panel(ui, area, tr("Shop"));
    int th = ui_px(ui, 32);
    Rect tr_ = rect_make(in.x + ui_px(ui, 6), in.y + ui_px(ui, 4), in.w - ui_px(ui, 12), th);
    ui_tabs(ui, tr_, tabs, SHOP_TAB_COUNT, &s_tab);
    int cy = in.y + th + ui_px(ui, 10);
    int cur_h = ui_px(ui, 22);
    int x = in.x + ui_px(ui, 10);
    const Currency cs[] = {CUR_GOLD, CUR_DIAMONDS, CUR_RAVEN_COINS};
    for (int i = 0; i < 3; i++) {
        ui_draw_currency_line(app, x, cy, cs[i], FS_SMALL);
        x += ui_px(ui, 190);
    }
    Rect grid = rect_make(in.x + ui_px(ui, 4), cy + cur_h + ui_px(ui, 4), in.w - ui_px(ui, 8),
                          in.y + in.h - (cy + cur_h + ui_px(ui, 8)));
    draw_offers(app, grid);
    handle_confirm(app);
}
