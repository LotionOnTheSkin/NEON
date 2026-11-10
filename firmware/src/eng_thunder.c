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
    /* Sylenth1 patches (Neon Summers' own and the Retro Thunder bank), approximated by tools (category.c) */
    {"80S", {0, -12, 32, 63, 20, 15, 9, 1}, {103, 121, 65, 109}, 0, 0, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 16, P_TRANS, 12)},
    {"CLASSIC", {0, 0, 72, 64, 123, 25, 14, 1}, {104, 108, 127, 105}, 0, 0, FX(0, 50, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16)},
    {"LA NIGHTS", {6, -12, 28, 58, 56, 38, 12, 1}, {74, 116, 36, 106}, 45, 0, FX(0, 50, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16)},
    {"LA NIGHTS2", {0, 0, 76, 64, 54, 0, 12, 0}, {99, 104, 77, 104}, 28, 0, FX(0, 50, 0, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16)},
    {"LA NIGHTS3", {0, 0, 73, 64, 49, 44, 0, 0}, {97, 127, 127, 109}, 0, 0, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 12)},
    {"MEGATRON", {0, 0, 56, 57, 47, 25, 26, 0}, {0, 0, 127, 3}, 0, 0, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 19, P_LD_FLT, 2, P_LRATE, 44)},
    {"OPTIMUS", {0, 0, 70, 61, 92, 9, 18, 1}, {72, 64, 64, 99}, 32, 0, FX(0, 50, 35, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 21)},
    {"ROBOMURPHY", {0, 0, 58, 0, 104, 11, 18, 1}, {23, 0, 127, 113}, 0, 0, FX(0, 50, 35, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16)},
    {"ROLLRBLADE", {0, 0, 62, 64, 13, 61, 12, 1}, {83, 126, 127, 107}, 39, 0, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -2, P_LD_FLT, 11, P_LRATE, 42)},
    {"STRING PAD", {0, 0, 38, 0, 13, 0, 12, 1}, {92, 127, 87, 88}, 0, 0, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 59, P_TRANS, 24)},
    {"STRNG THNG", {0, 0, 74, 63, 85, 0, 12, 0}, {53, 110, 103, 114}, 22, 0, FX(0, 50, 0, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_LD_PIT, 1, P_LRATE, 73)},
    {"SUNSET 1", {0, 0, 51, 64, 118, 0, 12, 0}, {76, 127, 127, 115}, 0, 0, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_LD_FLT, 14, P_LRATE, 53)},
    {"SUNSET 2", {0, -24, 48, 66, 8, 43, 12, 0}, {81, 119, 81, 93}, 30, 0, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 59, P_TRANS, 12)},
    {"TOMMY GUN2", {0, 0, 44, 64, 113, 0, 38, 1}, {31, 57, 127, 96}, 0, 0, FX(0, 0, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16)},
    {"TOMMY GUNN", {7, 12, 57, 46, 11, 48, 8, 1}, {81, 117, 67, 109}, 39, 0, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -1, P_TRANS, -12)},
    {"TRANSFER", {0, 0, 62, 64, 13, 61, 12, 1}, {83, 126, 127, 107}, 39, 0, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -2, P_LD_FLT, 11, P_LRATE, 42)},
    {"UNIVERSE", {0, -12, 27, 57, 52, 21, 0, 0}, {40, 127, 127, 110}, 32, 0, FX(0, 50, 0, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_LD_FLT, -2, P_LRATE, 47, P_TRANS, 12)},
    {"VAN DAMME", {0, 0, 56, 57, 80, 17, 0, 0}, {66, 127, 115, 94}, 0, 0, FX(0, 50, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 3)},
    {"BEETLEJCE", {0, 0, 45, 0, 102, 0, 5, 0}, {0, 0, 127, 0}, 0, 1, FX(25, 50, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16)},
    {"BEV HILLS", {1, 0, 66, 64, 8, 0, 8, 0}, {11, 127, 127, 100}, 63, 0, FX(0, 0, 0, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 21)},
    {"BEV HILLS2", {0, 0, 44, 64, 62, 31, 64, 0}, {43, 99, 127, 47}, 63, 0, FX(0, 50, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 59)},
    {"BRASS MCH1", {0, 12, 61, 35, 50, 0, 28, 0}, {47, 112, 74, 93}, 42, 0, FX(0, 50, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16)},
    {"BRASS MCH2", {0, 0, 0, 0, 44, 0, 13, 0}, {53, 0, 127, 96}, 38, 1, FX(0, 50, 0, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -3)},
    {"BRASS YOU", {0, -12, 82, 54, 23, 0, 14, 1}, {0, 77, 76, 74}, 61, 0, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -6)},
    {"COCKTAIL", {3, 0, 0, 56, 75, 12, 22, 0}, {0, 127, 127, 32}, 0, 1, FX(25, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16)},
    {"DBL IMPACT", {0, 0, 51, 0, 28, 0, 22, 0}, {0, 115, 101, 109}, 46, 0, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 7)},
    {"DIE HARD", {0, 0, 56, 64, 49, 6, 21, 0}, {20, 87, 82, 50}, 42, 0, FX(0, 50, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 21, P_LD_PIT, 1, P_LRATE, 75)},
    {"DIE HARD 2", {1, 0, 31, 63, 52, 69, 26, 0}, {75, 104, 87, 104}, 19, 1, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 2)},
    {"DIE HARD 3", {3, 0, 36, 0, 103, 54, 28, 1}, {0, 64, 64, 0}, 19, 1, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 4, P_LD_PIT, 1, P_LRATE, 76)},
    {"DISTORT", {0, 0, 51, 63, 58, 24, 12, 0}, {0, 87, 126, 0}, 0, 1, FX(25, 50, 35, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 24, P_TRANS, -12)},
    {"MONO MUCH", {0, 0, 30, 64, 36, 32, 24, 1}, {0, 88, 0, 85}, 0, 1, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 25, P_LD_PIT, 1, P_LRATE, 92, P_TRANS, 24)},
    {"NOT SQUARE", {1, 0, 45, 64, 124, 12, 22, 1}, {0, 127, 127, 89}, 0, 1, FX(0, 50, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16)},
    {"ORGAN LD", {3, 24, 51, 64, 26, 19, 8, 0}, {0, 127, 127, 23}, 63, 0, FX(0, 50, 35, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 26)},
    {"STALLONE", {0, 0, 51, 64, 40, 15, 14, 0}, {0, 103, 127, 67}, 0, 0, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 19, P_TRANS, 12)},
    {"STALLONE 2", {2, 0, 21, 62, 120, 31, 28, 0}, {81, 0, 127, 64}, 0, 1, FX(25, 50, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16)},
    {"TOP GUN", {0, 0, 45, 64, 124, 12, 0, 0}, {0, 98, 76, 111}, 0, 0, FX(0, 50, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_LD_PIT, 1, P_LRATE, 72)},
    {"TOT RECALL", {0, 0, 45, 64, 124, 0, 0, 1}, {0, 111, 62, 107}, 23, 0, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_LD_PIT, 1, P_LRATE, 78)},
    {"VENGEANCE", {1, 12, 30, 64, 65, 58, 12, 0}, {0, 118, 76, 98}, 0, 1, FX(25, 50, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_LD_FLT, 2, P_LRATE, 42, P_TRANS, -12)},
    {"48 HOURS", {3, 0, 0, 0, 54, 0, 0, 1}, {0, 92, 0, 81}, 63, 0, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16)},
    {"80S 1", {3, 12, 40, 50, 17, 0, 12, 0}, {0, 88, 30, 95}, 15, 0, FX(0, 50, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 59, P_TRANS, 24)},
    {"80S 2", {6, -24, 0, 96, 67, 30, 17, 0}, {0, 88, 12, 70}, 47, 0, FX(0, 50, 0, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_TRANS, 24)},
    {"ADRIAN", {7, 24, 48, 64, 78, 0, 85, 0}, {0, 99, 0, 97}, 0, 0, FX(25, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_LD_PIT, 1, P_LRATE, 95)},
    {"ANOTHER 48", {0, 0, 0, 0, 15, 0, 7, 1}, {0, 71, 64, 108}, 22, 0, FX(0, 50, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_TRANS, 12)},
    {"GIMME BEL1", {5, 24, 51, 52, 19, 2, 21, 1}, {0, 109, 0, 103}, 53, 0, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 21, P_TRANS, 12)},
    {"GIMME BEL2", {0, 0, 12, 64, 104, 27, 21, 1}, {0, 116, 0, 51}, -64, 0, FX(0, 0, 35, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 6, P_AMODE, 14, P_ARATE, 2, P_AOCT, 1)},
    {"GIMME BEL3", {0, 0, 12, 63, 16, 37, 6, 0}, {0, 98, 64, 60}, 39, 1, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 14, P_TRANS, 24)},
    {"GIMME BEL4", {5, -24, 42, 64, 78, 0, 16, 1}, {0, 79, 0, 60}, 47, 0, FX(0, 0, 0, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_TRANS, 24)},
    {"HIGH", {3, 0, 12, 42, 57, 13, 34, 0}, {0, 79, 29, 0}, 62, 0, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 6, P_TRANS, 24)},
    {"LITTLE", {7, -24, 1, 63, 127, 0, 0, 0}, {0, 78, 0, 87}, 0, 0, FX(25, 0, 0, 40), XP(P_TRANS, 20)},
    {"MALLET", {4, 12, 7, 64, 30, 21, 0, 0}, {0, 96, 0, 72}, 38, 0, FX(0, 50, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_LD_PIT, 1, P_LRATE, 70)},
    {"ROCKY I", {0, 0, 49, 24, 80, 0, 20, 0}, {0, 60, 64, 94}, 11, 0, FX(0, 50, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -9)},
    {"ROCKY II", {0, 0, 25, 41, 76, 26, 20, 0}, {0, 62, 104, 66}, 0, 1, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16)},
    {"ROCKY III", {0, 0, 38, 64, 71, 0, 12, 0}, {0, 89, 73, 97}, 63, 1, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_LD_PIT, 1, P_LRATE, 77)},
    {"ROCKY IV", {0, 12, 72, 64, 127, 0, 12, 0}, {0, 0, 127, 106}, 0, 0, FX(0, 0, 35, 40)},
    {"ROCKY V", {3, 0, 55, 63, 28, 72, 41, 0}, {0, 99, 0, 93}, 20, 0, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 43)},
    {"ROSEWOOD", {3, 24, 24, 53, 6, 2, 20, 1}, {0, 106, 0, 94}, 47, 0, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 44)},
    {"SAVED BELL", {3, 24, 40, 63, 22, 28, 22, 0}, {0, 110, 0, 101}, 0, 0, FX(0, 0, 0, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 21, P_TRANS, 12)},
    {"SOFT", {3, -24, 0, 64, 127, 0, 21, 1}, {0, 86, 0, 73}, 0, 0, FX(25, 50, 35, 40), XP(P_TRANS, 24)},
    {"STARS", {0, 0, 49, 63, 37, 7, 13, 1}, {0, 97, 78, 107}, 63, 0, FX(25, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 12)},
    {"TAGGART", {0, 0, 0, 0, 127, 0, 0, 0}, {0, 109, 0, 107}, 0, 0, FX(0, 0, 35, 0)},
    {"TERMINATOR", {0, 12, 26, 31, 8, 58, 0, 0}, {0, 101, 0, 88}, 52, 0, FX(0, 50, 35, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16)},
    {"WISHBONE", {0, 24, 0, 45, 46, 62, 25, 1}, {41, 73, 0, 45}, 18, 0, FX(0, 50, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 21)},
    {"ANDRE", {0, 0, 51, 64, 77, 12, 12, 1}, {0, 77, 64, 56}, 23, 0, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_LD_FLT, -4, P_LRATE, 42)},
    {"BOSS MAN", {0, 0, 24, 0, 43, 0, 5, 0}, {0, 86, 2, 99}, 28, 0, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_TRANS, 12, P_AMODE, 14, P_ARATE, 2, P_AOCT, 1)},
    {"BOSS MAN 2", {0, 0, 24, 0, 43, 0, 5, 0}, {0, 86, 2, 99}, 28, 0, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_TRANS, 12)},
    {"BRASS ARP", {0, 0, 12, 64, 95, 0, 23, 0}, {47, 105, 89, 57}, 63, 0, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16)},
    {"DISPATCH", {3, 0, 0, 0, 60, 0, 12, 0}, {0, 104, 79, 58}, 63, 0, FX(0, 0, 35, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_AMODE, 8, P_ARATE, 2, P_AOCT, 1)},
    {"ELM ST", {0, 7, 0, 64, 58, 0, 0, 1}, {0, 84, 70, 0}, 63, 0, FX(0, 0, 35, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_TRANS, -12, P_AMODE, 8, P_ARATE, 2, P_AOCT, 1)},
    {"HOGAN 2", {0, 0, 42, 0, 87, 0, 27, 0}, {0, 100, 76, 106}, 45, 0, FX(0, 50, 35, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_TRANS, 12)},
    {"HULK HOGAN", {0, 0, 42, 0, 87, 0, 27, 0}, {0, 100, 76, 106}, 45, 0, FX(0, 50, 35, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_TRANS, 12, P_AMODE, 14, P_ARATE, 2, P_AOCT, 1)},
    {"JUSTICIA", {0, 0, 23, 0, 58, 27, 33, 0}, {0, 91, 64, 97}, 63, 0, FX(0, 50, 35, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_TRANS, -24, P_AMODE, 7, P_ARATE, 2, P_AOCT, 3)},
    {"KICKBOXER", {0, 0, 41, 64, 58, 0, 66, 0}, {39, 86, 83, 74}, 63, 0, FX(0, 0, 0, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 59, P_AMODE, 14, P_ARATE, 2, P_AOCT, 1)},
    {"KICKBOXER2", {0, 0, 41, 64, 58, 0, 66, 0}, {39, 86, 83, 74}, 63, 0, FX(0, 0, 0, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 59)},
    {"KIDS", {3, 0, 48, 0, 58, 48, 57, 0}, {0, 95, 74, 0}, 63, 0, FX(0, 0, 35, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_AMODE, 1, P_ARATE, 2, P_AOCT, 1)},
    {"LEGION", {0, 0, 57, 64, 40, 19, 0, 1}, {0, 78, 71, 105}, 61, 0, FX(0, 50, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_TRANS, 12)},
    {"MACHO 2", {0, 0, 59, 64, 82, 0, 12, 0}, {0, 88, 67, 98}, 31, 0, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_TRANS, 12)},
    {"MACHO MAN", {0, 0, 59, 64, 82, 0, 12, 0}, {0, 88, 67, 98}, 31, 0, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_TRANS, 12, P_AMODE, 14, P_ARATE, 2, P_AOCT, 1)},
    {"MELROSE", {0, 0, 47, 0, 58, 82, 12, 0}, {45, 115, 96, 106}, 63, 0, FX(0, 50, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -8, P_TRANS, -24, P_AMODE, 7, P_ARATE, 2, P_AOCT, 4)},
    {"RIC FLAIR", {0, 0, 34, 54, 11, 0, 0, 0}, {0, 94, 64, 97}, 45, 0, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16)},
    {"SIDD", {3, 12, 0, 64, 58, 52, 12, 0}, {0, 98, 84, 74}, 63, 0, FX(25, 50, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 14, P_TRANS, -24, P_AMODE, 3, P_ARATE, 2, P_AOCT, 3)},
    {"STACKER", {3, 7, 0, 64, 58, 0, 0, 1}, {0, 91, 86, 90}, 63, 0, FX(0, 0, 35, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_TRANS, -24, P_AMODE, 8, P_ARATE, 2, P_AOCT, 1)},
    {"STRANGEST", {0, 12, 40, 64, 58, 7, 12, 0}, {0, 97, 64, 85}, 63, 0, FX(0, 0, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_LD_FLT, 24, P_LRATE, 36, P_TRANS, -12, P_AMODE, 8, P_ARATE, 2, P_AOCT, 1)},
    {"SYNTHIPOP", {3, 12, 55, 46, 12, 17, 12, 1}, {0, 112, 64, 115}, 34, 0, FX(0, 0, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 21, P_AMODE, 8, P_ARATE, 2, P_AOCT, 1)},
    {"TORNADO", {0, 0, 51, 64, 89, 0, 12, 0}, {0, 106, 29, 103}, 0, 0, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_LD_FLT, 14, P_LRATE, 53)},
    {"VHS", {0, 0, 45, 0, 58, 54, 12, 0}, {45, 115, 96, 106}, 63, 0, FX(0, 0, 35, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -8, P_AMODE, 7, P_ARATE, 2, P_AOCT, 3)},
    {"ALIEN", {0, 12, 67, 64, 53, 0, 25, 0}, {0, 90, 0, 92}, 35, 0, FX(25, 50, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_TRANS, -12)},
    {"ALIEN II", {4, 0, 17, 36, 31, 37, 38, 0}, {0, 94, 64, 79}, 63, 0, FX(25, 50, 0, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 21, P_TRANS, -12)},
    {"ALIEN III", {2, 12, 87, 64, 74, 0, 0, 0}, {0, 87, 64, 113}, 63, 0, FX(25, 50, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_TRANS, -24)},
    {"ARNIE", {6, 12, 20, 64, 46, 0, 25, 0}, {0, 94, 64, 70}, 63, 0, FX(0, 50, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_TRANS, -12)},
    {"AXEL F", {1, -12, 39, 64, 50, 17, 29, 1}, {0, 81, 78, 82}, 63, 0, FX(25, 0, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 13)},
    {"COMMANDO", {1, 12, 0, 64, 46, 10, 26, 1}, {0, 95, 55, 81}, 57, 0, FX(0, 0, 0, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_TRANS, -12)},
    {"CREED", {1, 12, 57, 66, 25, 5, 28, 0}, {0, 102, 39, 48}, 63, 0, FX(25, 0, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_TRANS, -12)},
    {"DISTORT 2", {0, -7, 0, 64, 99, 0, 20, 0}, {0, 71, 64, 60}, 63, 0, FX(25, 50, 0, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_TRANS, -12)},
    {"DISTORT 3", {0, 0, 11, 64, 69, 25, 85, 0}, {44, 57, 64, 78}, 33, 1, FX(25, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_TRANS, -12)},
    {"DISTORT 4", {6, 0, 0, 64, 124, 4, 24, 1}, {0, 82, 64, 0}, 38, 1, FX(0, 50, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_TRANS, -12)},
    {"DISTORT B", {3, 7, 15, 64, 127, 127, 56, 0}, {0, 0, 127, 0}, 63, 1, FX(25, 50, 0, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_LD_PIT, 1, P_LRATE, 79, P_TRANS, -10)},
    {"JEANCLAUDE", {0, 12, 51, 64, 58, 0, 42, 0}, {0, 93, 78, 63}, 63, 0, FX(0, 0, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_TRANS, -24)},
    {"MIAMI 1", {2, 0, 51, 64, 53, 22, 25, 0}, {0, 108, 27, 33}, 63, 0, FX(25, 50, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_TRANS, -24)},
    {"MIAMI 2", {3, -24, 0, 47, 35, 0, 0, 0}, {0, 0, 127, 81}, -64, 0, FX(0, 0, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_TRANS, 19)},
    {"MIAMI 3", {1, -12, 28, 64, 21, 90, 64, 0}, {0, 100, 0, 35}, 63, 0, FX(0, 50, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 17, P_LD_FLT, 6, P_LRATE, 57, P_TRANS, -12)},
    {"MOOG ME", {1, 12, 29, 64, 6, 35, 12, 1}, {0, 0, 127, 65}, -44, 1, FX(0, 0, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 12, P_TRANS, -12)},
    {"POLY FILL1", {0, 0, 34, 0, 48, 18, 21, 1}, {0, 73, 69, 81}, 38, 0, FX(25, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16)},
    {"POLY FILL2", {0, 0, 12, 64, 22, 0, 24, 0}, {0, 94, 38, 89}, 51, 0, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_TRANS, -12)},
    {"POLY FILL3", {5, 0, 0, 64, 66, 11, 20, 0}, {0, 88, 58, 88}, 49, 0, FX(25, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_TRANS, -12)},
    {"POLY FILL4", {0, 0, 0, 0, 40, 0, 22, 0}, {0, 96, 0, 78}, 55, 0, FX(0, 0, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_TRANS, -12)},
    {"POLY PHAT1", {1, -12, 0, 64, 33, 39, 12, 0}, {0, 79, 80, 82}, 32, 0, FX(25, 50, 0, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 22, P_TRANS, -12)},
    {"POLY PHAT2", {0, 0, 46, 64, 68, 9, 12, 0}, {0, 73, 64, 43}, 63, 0, FX(0, 0, 35, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_TRANS, -24)},
    {"POLY PHAT3", {1, -12, 15, 64, 48, 0, 32, 0}, {0, 88, 76, 77}, 63, 0, FX(25, 50, 35, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_TRANS, -12)},
    {"POLY PHAT4", {0, 0, 48, 42, 45, 12, 31, 0}, {16, 84, 64, 32}, 63, 0, FX(25, 50, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_TRANS, -24)},
    {"SLAP", {7, 12, 15, 64, 127, 127, 34, 1}, {0, 88, 0, 62}, 63, 0, FX(25, 0, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 59, P_TRANS, -24)},
    {"SLAP ME", {6, 12, 0, 64, 34, 15, 34, 0}, {0, 102, 0, 54}, 58, 1, FX(0, 0, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -14, P_TRANS, -24)},
    {"WARRIOR", {0, 0, 76, 64, 80, 0, 5, 0}, {0, 73, 64, 103}, 63, 0, FX(25, 0, 0, 40), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -16, P_TRANS, -24)},
    {"YAMAHA ME", {6, 0, 0, 63, 14, 27, 12, 1}, {0, 115, 26, 60}, 38, 1, FX(0, 0, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 7, P_TRANS, -12)},
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
