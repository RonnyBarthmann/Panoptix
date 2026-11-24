/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Eingabe: uebersetzt SDL-Ereignisse in UiInput. */
#ifndef PNX_APP_INPUT_H
#define PNX_APP_INPUT_H

#include <SDL.h>

#include "ui/ui.h"

/* Aktiviert SDL-Texteingabe (einmalig nach dem Fensterstart). */
void input_init(void);
/* Beginn eines Frames: leert pressed/released-Flags, Text und Mausrad. */
void input_begin_frame(UiInput *in);
/* Wertet ein SDL-Ereignis aus. */
void input_handle_event(UiInput *in, const SDL_Event *ev);

#endif
