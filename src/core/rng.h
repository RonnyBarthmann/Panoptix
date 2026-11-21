/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Deterministischer Zufallsgenerator (PCG32). Gleicher Seed => gleiche Folge.
 * Die Simulation nutzt AUSSCHLIESSLICH diesen RNG, nie rand(). */
#ifndef PNX_CORE_RNG_H
#define PNX_CORE_RNG_H

#include "core/types.h"

typedef struct Rng {
    u64 state;
    u64 inc;
} Rng;

void rng_seed(Rng *r, u64 seed, u64 stream);
u32 rng_u32(Rng *r);
/* Gleichverteilt in [0,1) */
float rng_float(Rng *r);
/* Gleichverteilt in [lo, hi] (ganzzahlig, inklusive). hi < lo => lo */
int rng_range(Rng *r, int lo, int hi);
/* Gleichverteilt in [lo, hi) */
float rng_frange(Rng *r, float lo, float hi);
/* true mit Wahrscheinlichkeit p (0..1) */
bool rng_chance(Rng *r, double p);
/* true mit Wahrscheinlichkeit 1:n (n >= 1) */
bool rng_one_in(Rng *r, u32 n);
/* Zufaelliger Index nach Gewichten (Summe > 0), -1 bei leerer Summe */
int rng_weighted(Rng *r, const float *weights, int count);

#endif
