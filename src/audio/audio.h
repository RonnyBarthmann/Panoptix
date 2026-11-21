/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Audio: alle Klaenge werden zur Laufzeit synthetisiert (keine Dateien, keine
 * Zusatzabhaengigkeit). Ersetzbar durch OGG-Dateien (siehe docs/ROADMAP). */
#ifndef PNX_AUDIO_AUDIO_H
#define PNX_AUDIO_AUDIO_H

#include "core/types.h"

typedef enum Sfx {
    SFX_CLICK = 0,
    SFX_HIT,
    SFX_CRIT,
    SFX_HERO_HIT,
    SFX_SKILL,
    SFX_SPELL,
    SFX_HEAL,
    SFX_KILL,
    SFX_LEVELUP,
    SFX_DEATH,
    SFX_CHEST_NORMAL,
    SFX_CHEST_MAGIC,
    SFX_CHEST_RARE,
    SFX_CHEST_UNIQUE,
    SFX_CHEST_BOSS,
    SFX_CHEST_LEGEND,
    SFX_PICKUP,
    SFX_CRAFT_OK,
    SFX_CRAFT_FAIL,
    SFX_BOSS_INTRO,
    SFX_MAP_CLEAR,
    SFX_BUY,
    SFX_ERROR,
    SFX_COUNT
} Sfx;

typedef struct Audio Audio;

/* NULL, wenn kein Audiogeraet (Spiel laeuft dann stumm weiter). */
Audio *audio_init(bool enabled);
void audio_shutdown(Audio *a);
void audio_set_volume(Audio *a, float master, float sfx, float ui);
/* vol 0..1; pan -1..1; Mehrfachaufrufe im selben Frame werden gedrosselt. */
void audio_play(Audio *a, Sfx sfx, float vol);
/* Passender Kisten-Sound zum Tier. */
Sfx audio_chest_sfx(Tier t);

#endif
