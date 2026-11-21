/* SPDX-License-Identifier: GPL-3.0-or-later */
/* PCG32 - kleiner, schneller, deterministischer Zufallsgenerator. */
#include "core/rng.h"

void rng_seed(Rng *r, u64 seed, u64 stream) {
    r->state = 0u;
    r->inc = (stream << 1u) | 1u;
    rng_u32(r);
    r->state += seed;
    rng_u32(r);
}

u32 rng_u32(Rng *r) {
    u64 old = r->state;
    r->state = old * 6364136223846793005ULL + r->inc;
    u32 xorshifted = (u32)(((old >> 18u) ^ old) >> 27u);
    u32 rot = (u32)(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
}

float rng_float(Rng *r) {
    /* 24 Bit Mantisse => exakt in [0,1) */
    return (float)(rng_u32(r) >> 8) * (1.0f / 16777216.0f);
}

int rng_range(Rng *r, int lo, int hi) {
    if (hi <= lo) {
        return lo;
    }
    u32 span = (u32)(hi - lo) + 1u;
    return lo + (int)(rng_u32(r) % span);
}

float rng_frange(Rng *r, float lo, float hi) {
    return lo + (hi - lo) * rng_float(r);
}

bool rng_chance(Rng *r, double p) {
    if (p <= 0.0) {
        return false;
    }
    if (p >= 1.0) {
        return true;
    }
    /* 32 Bit Aufloesung reicht fuer Chancen bis ca. 1:4 Mrd. */
    return (double)rng_u32(r) < p * 4294967296.0;
}

bool rng_one_in(Rng *r, u32 n) {
    if (n <= 1u) {
        return true;
    }
    return rng_u32(r) % n == 0u;
}

int rng_weighted(Rng *r, const float *weights, int count) {
    float sum = 0.0f;
    for (int i = 0; i < count; i++) {
        if (weights[i] > 0.0f) {
            sum += weights[i];
        }
    }
    if (sum <= 0.0f) {
        return -1;
    }
    float pick = rng_float(r) * sum;
    int last = -1;
    for (int i = 0; i < count; i++) {
        if (weights[i] <= 0.0f) {
            continue;
        }
        last = i;
        if (pick < weights[i]) {
            return i;
        }
        pick -= weights[i];
    }
    return last;
}
