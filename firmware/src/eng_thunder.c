/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Neon Summers
 * Written for the Felucca engine interface (Felucca-Engines, MIT, Leo Kuroshita / Hugelton Instruments). */
/* THUNDER: a two-oscillator unison voice in the manner of the big VA plug-ins of the synthwave era (made to carry
 * Sylenth-style patches over): oscillator A, three voices wide, and oscillator B, two voices wide at its own interval,
 * through a drive into a 12 or 24 dB/oct resonant low-pass.
 *
 *   OSC  WAVE  the pair: SAW+SAW, SAW+SQR, SAW+SIN, SQR+SQR, SQR+SIN, TRI+SIN, SAW+TRI, SIN+SIN (A + B)
 *        INT   B's interval to A, -24..+24 semitones (an octave up or down, a fifth: the Sylenth stacks)
 *        DET   the unison detune: A's outer pair up to +-50 cents, B's pair half of it
 *        MIX   A against B: 0 A alone, 64 even, 127 B alone
 *   FLT  CUT   cutoff (key tracking fixed at 1/2)
 *        RES   resonance
 *        DRV   drive into the filter (1x..4x, a soft clipper: Sylenth's WARM DRIVE)
 *        SLOP  the slope: 12 dB (one SVF) or 24 dB (two)
 *
 * Voice state: ph[0..2] A's three voices; s[4] s[5] B's two (phases as uint32 in int32); s[0] s[1], s[6] s[7] the
 * filter; s[2] the note's seed (the free phases). keep: the filter. */

static const char *const N_THU_WAVE[] = {"SAW+SAW", "SAW+SQR", "SAW+SIN", "SQR+SQR", "SQR+SIN", "TRI+SIN", "SAW+TRI",
                                         "SIN+SIN"};
static const char *const N_THU_SLOPE[] = {"12dB", "24dB"};
enum { TH_SAW, TH_SQR, TH_SIN, TH_TRI };
static const uint8_t THU_WA[8] = {TH_SAW, TH_SAW, TH_SAW, TH_SQR, TH_SQR, TH_TRI, TH_SAW, TH_SIN};
static const uint8_t THU_WB[8] = {TH_SAW, TH_SQR, TH_SIN, TH_SQR, TH_SIN, TH_SIN, TH_TRI, TH_SIN};
#define THU_TRIM 10322           /* 0.63 (-4 dB), as NEON SUPER */

static void thunder_note_on(track_t *t, voice_t *v)
{
    uint32_t r = 0x9E3779B9u ^ (v->age * 2654435761u);
    (void)t;
    if (!r)
        r = 1;
    v->ph[0] = neon_rnd(&r);                      /* free-running: random phases, no attack click, no comb */
    v->ph[1] = neon_rnd(&r);
    v->ph[2] = neon_rnd(&r);
    v->s[4] = (int32_t)neon_rnd(&r);
    v->s[5] = (int32_t)neon_rnd(&r);
    v->s[2] = (int32_t)r;
    v->s[0] = v->s[1] = v->s[6] = v->s[7] = 0;
}

static inline int32_t thu_osc(uint32_t w, uint32_t ph, uint32_t inc)
{
    switch (w) {
    case TH_SAW:
        return osc_saw(ph, inc);
    case TH_SQR:
        return osc_pulse(ph, inc, 0x80000000u);
    case TH_TRI:
        return osc_tri(ph);
    default:
        return osc_sine(ph);
    }
}

/* inc * 2^(st / 12), st -24..24 (a table: no pow in the ISR) */
static uint32_t thu_interval(uint32_t inc, int32_t st)
{
    static const uint16_t R[12] = {32768, 34716, 36781, 38968, 41285, 43740, 46341, 49097, 52016, 55109, 58386, 61858};
    int32_t o = st >= 0 ? st / 12 : -((11 - st) / 12), k = st - o * 12;
    uint64_t x = ((uint64_t)inc * R[k]) >> 15;
    return (uint32_t)(o >= 0 ? x << o : x >> -o);
}

static void thunder_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t w = (uint32_t)p[P_E0] & 7u, wa = THU_WA[w], wb = THU_WB[w], i;
    int32_t det = p[P_E2], mix = p[P_E3], drv = p[P_E6], slope = p[P_E7];
    int32_t fa = det * 118 / 127, fb = fa >> 1;                    /* up to 50 ct (1 ct = 2.367 / 4096) */
    uint32_t ia = m->inc, ib = thu_interval(m->inc, clamp(p[P_E1], -24, 24));
    uint32_t ia1 = neon_det(ia, fa), ia2 = neon_det(ia, -fa + (fa >> 4));
    uint32_t ib0 = neon_det(ib, fb), ib1 = neon_det(ib, -fb - (fb >> 3));
    uint32_t a0 = v->ph[0], a1p = v->ph[1], a2p = v->ph[2], b0 = (uint32_t)v->s[4], b1p = (uint32_t)v->s[5];
    int32_t ga = (127 - mix) * 258, gb = mix * 258;               /* Q15 */
    int32_t cut = (p[P_E4] << 8) + m->cutoff + (64 * (v->pitch16 - 60 * 16) >> 4);
    int32_t drive = 32768 + drv * 774;                            /* 1x .. 4x */
    int32_t f1a = v->s[0], f1b = v->s[1], f2a = v->s[6], f2b = v->s[7];
    tsvf_t c1, c2;
    if (slope) {
        tsvf_coef_k(&c1, cut, NEON_K_STAGE1);
        tsvf_coef_k(&c2, cut, NEON_K_STAGE2 - p[P_E5] * 2600 / 127);
    } else
        tsvf_coef(&c1, cut, p[P_E5]);

    for (i = 0; i < n; i++) {
        int32_t sa = (thu_osc(wa, a0, ia) + thu_osc(wa, a1p, ia1) + thu_osc(wa, a2p, ia2)) / 3;
        int32_t sb = (thu_osc(wb, b0, ib0) + thu_osc(wb, b1p, ib1)) >> 1;
        int32_t s = ((ga * sa) >> 15) + ((gb * sb) >> 15);
        a0 += ia;
        a1p += ia1;
        a2p += ia2;
        b0 += ib0;
        b1p += ib1;
        if (drv)
            s = softclip(((s >> 2) * (drive >> 2)) >> 11);
        {
            int32_t y = tsvf_lp(&c1, s >> 1, &f1a, &f1b);
            if (slope)
                y = tsvf_lp(&c2, y, &f2a, &f2b);
            s = (soft_knee(y, 16000) * THU_TRIM) >> 13;
        }
        out[i] += voice_amp(s, m, i) * 2;
    }
    v->ph[0] = a0;
    v->ph[1] = a1p;
    v->ph[2] = a2p;
    v->s[4] = (int32_t)b0;
    v->s[5] = (int32_t)b1p;
    v->s[0] = f1a;
    v->s[1] = f1b;
    v->s[6] = f2a;
    v->s[7] = f2b;
}

/* e = WAVE INT DET MIX CUT RES DRV SLOP */
static const preset_t THUNDER_PRESETS[] = {
    {"INIT", {0, 12, 40, 50, 95, 15, 0, 1}, {0, 70, 100, 60}, 0, 0, FX(0, 0, 0, 0)},
};

static const engine_t ENG_THUNDER = {
    .name = "THUNDER",
    .page_title = {"OSC", "FLT"},
    .edit = {
        {"WAVE", F_ENUM, 0, 7, 0, N_THU_WAVE, 0},
        {"INT", F_SEMI, -24, 24, 0, 0, 0},
        {"DET", F_PCT, 0, 127, 40, 0, 0},
        {"MIX", F_PCT, 0, 127, 50, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 95, 0, 0},
        {"RES", F_PCT, 0, 127, 15, 0, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
        {"SLOP", F_ENUM, 0, 1, 1, N_THU_SLOPE, 0},
    },
    .presets = THUNDER_PRESETS,
    .npresets = NELEM(THUNDER_PRESETS),
    .note_on = thunder_note_on,
    .render = thunder_render,
    .knob = {P_E4, P_E2, P_E3, P_REL},   /* HOME: CUT, DET, MIX, REL */
    .poly = 5,
    .keep = 0xC3,                        /* the filter */
};
