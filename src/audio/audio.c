/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Audio: alle Sfx werden beim Start aus einfachen Noten (Sinus/Rechteck/Rauschen
 * mit Huellkurve) in Puffer gerendert und im SDL-Callback gemischt. */
#include "audio/audio.h"

#include <SDL.h>
#include <stdlib.h>
#include <string.h>

#include "core/util.h"

#define SAMPLE_RATE 44100
#define MAX_VOICES 24
#define THROTTLE_MS 45u /* gleicher Sfx hoechstens alle 45 ms (~ pro Frame) */
#define MASTER_GAIN 0.5f

typedef enum Wave { W_SINE, W_SQUARE, W_NOISE } Wave;

/* Eine Note: Wellenform, Startzeit, Dauer (s), Frequenz Start/Ende (Hz), Amplitude. */
typedef struct Note {
    Wave wave;
    float t0, dur, f0, f1, amp;
} Note;

typedef struct SfxDef {
    const Note *notes;
    int count;
    bool ui; /* nutzt Lautstaerke "ui" statt "sfx" */
} SfxDef;

typedef struct Voice {
    const float *data;
    int len, pos;
    float vol;
} Voice;

struct Audio {
    SDL_AudioDeviceID dev;
    float *buf[SFX_COUNT];
    int len[SFX_COUNT];
    Voice voices[MAX_VOICES];
    Uint32 last_ms[SFX_COUNT];
    float master, sfx, ui;
    u32 noise_state;
};

/* ---- Notentabellen ------------------------------------------------------- */

#define S(t0, d, f0, f1, a) {W_SINE, t0, d, f0, f1, a}
#define Q(t0, d, f0, f1, a) {W_SQUARE, t0, d, f0, f1, a}
#define R(t0, d, a) {W_NOISE, t0, d, 0, 0, a}

static const Note N_CLICK[] = {S(0, 0.04f, 1200, 900, 0.5f)};
static const Note N_HIT[] = {R(0, 0.07f, 0.45f), S(0, 0.07f, 180, 90, 0.4f)};
static const Note N_CRIT[] = {R(0, 0.09f, 0.5f), Q(0, 0.12f, 520, 260, 0.25f), S(0.03f, 0.14f, 900, 700, 0.35f)};
static const Note N_HERO_HIT[] = {R(0, 0.1f, 0.4f), S(0, 0.12f, 140, 70, 0.55f)};
static const Note N_SKILL[] = {S(0, 0.18f, 300, 800, 0.4f), R(0, 0.12f, 0.15f)};
static const Note N_SPELL[] = {S(0, 0.25f, 500, 1100, 0.3f), S(0.05f, 0.25f, 750, 1650, 0.2f)};
static const Note N_HEAL[] = {S(0, 0.12f, 523, 523, 0.3f), S(0.09f, 0.12f, 659, 659, 0.3f), S(0.18f, 0.2f, 784, 784, 0.3f)};
static const Note N_KILL[] = {R(0, 0.12f, 0.35f), S(0, 0.15f, 300, 80, 0.45f)};
static const Note N_LEVELUP[] = {Q(0, 0.12f, 392, 392, 0.2f), Q(0.1f, 0.12f, 523, 523, 0.2f), Q(0.2f, 0.12f, 659, 659, 0.2f),
                                 Q(0.3f, 0.3f, 784, 784, 0.22f), S(0.3f, 0.35f, 1568, 1568, 0.15f)};
static const Note N_DEATH[] = {S(0, 0.9f, 330, 55, 0.5f), Q(0, 0.9f, 165, 40, 0.18f), R(0, 0.3f, 0.2f)};
static const Note N_CHEST_NORMAL[] = {S(0, 0.15f, 330, 300, 0.4f), R(0, 0.05f, 0.2f)};
static const Note N_CHEST_MAGIC[] = {S(0, 0.14f, 392, 392, 0.35f), S(0.1f, 0.2f, 494, 494, 0.35f)};
static const Note N_CHEST_RARE[] = {S(0, 0.12f, 440, 440, 0.35f), S(0.09f, 0.12f, 554, 554, 0.35f), S(0.18f, 0.25f, 659, 659, 0.35f)};
static const Note N_CHEST_UNIQUE[] = {S(0, 0.12f, 494, 494, 0.33f), S(0.09f, 0.12f, 622, 622, 0.33f),
                                      S(0.18f, 0.12f, 740, 740, 0.33f), S(0.27f, 0.35f, 988, 988, 0.35f)};
static const Note N_CHEST_BOSS[] = {Q(0, 0.2f, 110, 110, 0.25f), S(0.05f, 0.12f, 523, 523, 0.3f), S(0.14f, 0.12f, 659, 659, 0.3f),
                                    S(0.23f, 0.12f, 784, 784, 0.3f), S(0.32f, 0.4f, 1047, 1047, 0.35f)};
/* Legendaere Fanfare: aufsteigende Quinten, am Ende voller Dur-Akkord mit Glitzern. */
static const Note N_CHEST_LEGEND[] = {
    Q(0, 0.18f, 262, 262, 0.18f),     Q(0.15f, 0.18f, 330, 330, 0.18f), Q(0.30f, 0.18f, 392, 392, 0.18f),
    Q(0.45f, 0.25f, 523, 523, 0.2f),  S(0.45f, 0.25f, 1047, 1047, 0.2f), Q(0.65f, 0.7f, 392, 392, 0.15f),
    Q(0.65f, 0.7f, 523, 523, 0.15f),  Q(0.65f, 0.7f, 659, 659, 0.15f),   S(0.65f, 0.8f, 1319, 1319, 0.2f),
    S(0.8f, 0.6f, 1760, 1760, 0.12f), S(0.95f, 0.5f, 2093, 2093, 0.1f)};
static const Note N_PICKUP[] = {S(0, 0.06f, 880, 1320, 0.35f), S(0.05f, 0.08f, 1320, 1760, 0.3f)};
static const Note N_CRAFT_OK[] = {R(0, 0.04f, 0.3f), S(0, 0.1f, 600, 600, 0.3f), S(0.08f, 0.2f, 900, 900, 0.35f)};
static const Note N_CRAFT_FAIL[] = {R(0, 0.15f, 0.35f), Q(0, 0.25f, 220, 110, 0.25f)};
static const Note N_BOSS_INTRO[] = {Q(0, 0.8f, 55, 50, 0.35f), S(0, 0.8f, 82, 70, 0.4f), R(0.1f, 0.6f, 0.12f),
                                    Q(0.5f, 0.6f, 73, 65, 0.3f)};
static const Note N_MAP_CLEAR[] = {Q(0, 0.15f, 523, 523, 0.2f), Q(0.14f, 0.15f, 659, 659, 0.2f), Q(0.28f, 0.15f, 784, 784, 0.2f),
                                   S(0.42f, 0.5f, 1047, 1047, 0.3f)};
static const Note N_BUY[] = {S(0, 0.05f, 1800, 1800, 0.3f), S(0.06f, 0.12f, 2400, 2400, 0.3f)};
static const Note N_ERROR[] = {Q(0, 0.1f, 200, 200, 0.25f), Q(0.12f, 0.14f, 150, 150, 0.25f)};

#define DEF(arr, is_ui) {arr, ARRAY_LEN(arr), is_ui}
static const SfxDef DEFS[SFX_COUNT] = {
    [SFX_CLICK] = DEF(N_CLICK, true),
    [SFX_HIT] = DEF(N_HIT, false),
    [SFX_CRIT] = DEF(N_CRIT, false),
    [SFX_HERO_HIT] = DEF(N_HERO_HIT, false),
    [SFX_SKILL] = DEF(N_SKILL, false),
    [SFX_SPELL] = DEF(N_SPELL, false),
    [SFX_HEAL] = DEF(N_HEAL, false),
    [SFX_KILL] = DEF(N_KILL, false),
    [SFX_LEVELUP] = DEF(N_LEVELUP, false),
    [SFX_DEATH] = DEF(N_DEATH, false),
    [SFX_CHEST_NORMAL] = DEF(N_CHEST_NORMAL, false),
    [SFX_CHEST_MAGIC] = DEF(N_CHEST_MAGIC, false),
    [SFX_CHEST_RARE] = DEF(N_CHEST_RARE, false),
    [SFX_CHEST_UNIQUE] = DEF(N_CHEST_UNIQUE, false),
    [SFX_CHEST_BOSS] = DEF(N_CHEST_BOSS, false),
    [SFX_CHEST_LEGEND] = DEF(N_CHEST_LEGEND, false),
    [SFX_PICKUP] = DEF(N_PICKUP, true),
    [SFX_CRAFT_OK] = DEF(N_CRAFT_OK, true),
    [SFX_CRAFT_FAIL] = DEF(N_CRAFT_FAIL, true),
    [SFX_BOSS_INTRO] = DEF(N_BOSS_INTRO, false),
    [SFX_MAP_CLEAR] = DEF(N_MAP_CLEAR, false),
    [SFX_BUY] = DEF(N_BUY, true),
    [SFX_ERROR] = DEF(N_ERROR, true),
};

/* ---- Synthese ------------------------------------------------------------ */

static float noise_next(u32 *state) {
    *state = *state * 1664525u + 1013904223u;
    return (float)((*state >> 8) & 0xFFFF) / 32768.0f - 1.0f;
}

/* Huellkurve: kurzer Anschlag (3 ms), danach exponentieller Abfall mit weichem Ende. */
static float envelope(float t, float dur) {
    float attack = minf(t / 0.003f, 1.0f);
    float decay = expf(-3.5f * t / dur);
    float tail = clampf((dur - t) / 0.01f, 0.0f, 1.0f);
    return attack * decay * tail;
}

static float osc(Wave w, float phase, u32 *noise) {
    switch (w) {
    case W_SINE: return sinf(phase);
    case W_SQUARE: return sinf(phase) >= 0.0f ? 1.0f : -1.0f;
    default: return noise_next(noise);
    }
}

static void render_note(float *buf, int len, const Note *n, u32 *noise) {
    int start = (int)(n->t0 * SAMPLE_RATE);
    int count = (int)(n->dur * SAMPLE_RATE);
    float phase = 0.0f;
    for (int i = 0; i < count && start + i < len; i++) {
        float t = (float)i / SAMPLE_RATE;
        float f = lerpf(n->f0, n->f1, t / n->dur);
        phase += 2.0f * PNX_PI * f / SAMPLE_RATE;
        buf[start + i] += osc(n->wave, phase, noise) * n->amp * envelope(t, n->dur);
    }
}

static int def_length(const SfxDef *d) {
    float end = 0.0f;
    for (int i = 0; i < d->count; i++) end = maxf(end, d->notes[i].t0 + d->notes[i].dur);
    return (int)(end * SAMPLE_RATE) + 1;
}

static bool render_sfx(Audio *a, int id) {
    const SfxDef *d = &DEFS[id];
    int len = def_length(d);
    float *buf = calloc((size_t)len, sizeof(float));
    if (!buf) return false;
    for (int i = 0; i < d->count; i++) render_note(buf, len, &d->notes[i], &a->noise_state);
    for (int i = 0; i < len; i++) buf[i] = clampf(buf[i], -1.0f, 1.0f);
    a->buf[id] = buf;
    a->len[id] = len;
    return true;
}

/* ---- Mischer (Audio-Thread) ---------------------------------------------- */

static void audio_callback(void *user, Uint8 *stream, int bytes) {
    Audio *a = user;
    Sint16 *out = (Sint16 *)(void *)stream;
    int frames = bytes / (int)sizeof(Sint16);
    for (int i = 0; i < frames; i++) {
        float mix = 0.0f;
        for (int v = 0; v < MAX_VOICES; v++) {
            Voice *vo = &a->voices[v];
            if (!vo->data) continue;
            mix += vo->data[vo->pos] * vo->vol;
            if (++vo->pos >= vo->len) vo->data = NULL;
        }
        mix = clampf(mix * a->master * MASTER_GAIN, -1.0f, 1.0f);
        out[i] = (Sint16)(mix * 32767.0f);
    }
}

/* ---- API ------------------------------------------------------------------ */

static void free_buffers(Audio *a) {
    for (int i = 0; i < SFX_COUNT; i++) free(a->buf[i]);
}

Audio *audio_init(bool enabled) {
    if (!enabled) return NULL;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        log_warn("Audio: SDL-Audio nicht verfuegbar: %s", SDL_GetError());
        return NULL;
    }
    Audio *a = calloc(1, sizeof(*a));
    if (!a) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return NULL;
    }
    a->master = a->sfx = a->ui = 1.0f;
    a->noise_state = 12345u;
    for (int i = 0; i < SFX_COUNT; i++) {
        if (!render_sfx(a, i)) {
            free_buffers(a);
            free(a);
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            return NULL;
        }
    }
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = SAMPLE_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = audio_callback;
    want.userdata = a;
    a->dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (a->dev == 0) {
        log_warn("Audio: kein Geraet: %s", SDL_GetError());
        free_buffers(a);
        free(a);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return NULL;
    }
    SDL_PauseAudioDevice(a->dev, 0);
    return a;
}

void audio_shutdown(Audio *a) {
    if (!a) return;
    SDL_CloseAudioDevice(a->dev);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    free_buffers(a);
    free(a);
}

void audio_set_volume(Audio *a, float master, float sfx, float ui) {
    if (!a) return;
    SDL_LockAudioDevice(a->dev);
    a->master = clampf(master, 0.0f, 1.0f);
    a->sfx = clampf(sfx, 0.0f, 1.0f);
    a->ui = clampf(ui, 0.0f, 1.0f);
    SDL_UnlockAudioDevice(a->dev);
}

/* Freie Stimme suchen; sonst die am weitesten fortgeschrittene ersetzen. */
static Voice *pick_voice(Audio *a) {
    Voice *best = &a->voices[0];
    for (int v = 0; v < MAX_VOICES; v++) {
        if (!a->voices[v].data) return &a->voices[v];
        if (a->voices[v].pos > best->pos) best = &a->voices[v];
    }
    return best;
}

void audio_play(Audio *a, Sfx sfx, float vol) {
    if (!a || (int)sfx < 0 || sfx >= SFX_COUNT) return;
    Uint32 now = SDL_GetTicks();
    if (now - a->last_ms[sfx] < THROTTLE_MS && a->last_ms[sfx] != 0) return;
    a->last_ms[sfx] = now ? now : 1u;
    SDL_LockAudioDevice(a->dev);
    float channel = DEFS[sfx].ui ? a->ui : a->sfx;
    Voice *v = pick_voice(a);
    v->data = a->buf[sfx];
    v->len = a->len[sfx];
    v->pos = 0;
    v->vol = clampf(vol, 0.0f, 1.0f) * channel;
    SDL_UnlockAudioDevice(a->dev);
}

Sfx audio_chest_sfx(Tier t) {
    switch (t) {
    case TIER_MAGIC: return SFX_CHEST_MAGIC;
    case TIER_RARE: return SFX_CHEST_RARE;
    case TIER_UNIQUE: return SFX_CHEST_UNIQUE;
    case TIER_BOSS: return SFX_CHEST_BOSS;
    case TIER_LEGENDARY: return SFX_CHEST_LEGEND;
    default: return SFX_CHEST_NORMAL;
    }
}
