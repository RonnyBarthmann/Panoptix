/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "app/input.h"

#include <string.h>

void input_init(void) {
    SDL_StartTextInput();
}

void input_begin_frame(UiInput *in) {
    in->lmb_pressed = false;
    in->lmb_released = false;
    in->rmb_pressed = false;
    in->mmb_pressed = false;
    in->wheel = 0;
    in->text[0] = '\0';
    memset(in->key_pressed, 0, sizeof in->key_pressed);
}

/* SDL-Scancode -> Key-Enum (KEY_NONE wenn unbelegt). */
static Key map_key(SDL_Keycode sym) {
    if (sym >= SDLK_a && sym <= SDLK_z) return (Key)(KEY_A + (sym - SDLK_a));
    if (sym >= SDLK_0 && sym <= SDLK_9) return (Key)(KEY_0 + (sym - SDLK_0));
    if (sym >= SDLK_F1 && sym <= SDLK_F12) return (Key)(KEY_F1 + (sym - SDLK_F1));
    switch (sym) {
    case SDLK_ESCAPE: return KEY_ESCAPE;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: return KEY_ENTER;
    case SDLK_BACKSPACE: return KEY_BACKSPACE;
    case SDLK_TAB: return KEY_TAB;
    case SDLK_SPACE: return KEY_SPACE;
    case SDLK_DELETE: return KEY_DELETE;
    case SDLK_UP: return KEY_UP;
    case SDLK_DOWN: return KEY_DOWN;
    case SDLK_LEFT: return KEY_LEFT;
    case SDLK_RIGHT: return KEY_RIGHT;
    case SDLK_PAGEUP: return KEY_PAGEUP;
    case SDLK_PAGEDOWN: return KEY_PAGEDOWN;
    default: return KEY_NONE;
    }
}

static void update_modifiers(UiInput *in) {
    SDL_Keymod m = SDL_GetModState();
    in->ctrl = (m & KMOD_CTRL) != 0;
    in->shift = (m & KMOD_SHIFT) != 0;
    in->alt = (m & KMOD_ALT) != 0;
}

static void handle_key(UiInput *in, const SDL_KeyboardEvent *k) {
    Key key = map_key(k->keysym.sym);
    update_modifiers(in);
    if (key == KEY_NONE) return;
    if (k->type == SDL_KEYDOWN) {
        in->key_down[key] = true;
        in->key_pressed[key] = true;   /* Auto-Repeat zaehlt als neuer Druck */
    } else {
        in->key_down[key] = false;
    }
}

static void handle_button(UiInput *in, const SDL_MouseButtonEvent *b) {
    bool down = b->type == SDL_MOUSEBUTTONDOWN;
    in->mx = b->x;
    in->my = b->y;
    switch (b->button) {
    case SDL_BUTTON_LEFT:
        in->lmb_down = down;
        if (down) in->lmb_pressed = true; else in->lmb_released = true;
        break;
    case SDL_BUTTON_RIGHT:
        in->rmb_down = down;
        if (down) in->rmb_pressed = true;
        break;
    case SDL_BUTTON_MIDDLE:
        if (down) in->mmb_pressed = true;
        break;
    default: break;
    }
}

static void handle_text(UiInput *in, const char *txt) {
    size_t have = strlen(in->text);
    size_t add = strlen(txt);
    if (have + add >= sizeof in->text) return;   /* zu lang: verwerfen */
    memcpy(in->text + have, txt, add + 1);
}

void input_handle_event(UiInput *in, const SDL_Event *ev) {
    switch (ev->type) {
    case SDL_QUIT:
        in->quit_requested = true;
        break;
    case SDL_MOUSEMOTION:
        in->mx = ev->motion.x;
        in->my = ev->motion.y;
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        handle_button(in, &ev->button);
        break;
    case SDL_MOUSEWHEEL:
        in->wheel += ev->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -ev->wheel.y : ev->wheel.y;
        break;
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        handle_key(in, &ev->key);
        break;
    case SDL_TEXTINPUT:
        handle_text(in, ev->text.text);
        break;
    default: break;
    }
}
