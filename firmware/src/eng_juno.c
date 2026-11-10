/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Neon Summers
 * Written for the Felucca engine interface (Felucca-Engines, MIT, Leo Kuroshita / Hugelton Instruments). */
/* JUNO: a Juno-106 voice, made to carry TAL U-NO-LX patches over knob for knob.
 *
 *   One DCO: a pulse, a saw and a square sub an octave down divided from the DCO itself (locked to it), plus white
 *   noise, into a 24 dB/oct low-pass (two cascaded trapezoidal SVFs, as NEON's) and a one-pole high-pass after it.
 *
 *   OSC  PULS  the pulse: 0 off; 1..127 on, its PWM depth 0 .. full (one-sided as the Juno's: 50 % down to 5 %)
 *        MODE  what moves the width: 0 MAN (a fixed width, PULS sets it), 1..126 the voice's LFO at that rate
 *              (LFO_INC: 0.05..40 Hz as the track LFO's), 127 ENV (the amp envelope, as the Juno's ENV switch)
 *        SAW   the saw's level (the Juno's saw switch: 0 off)
 *        SUB   the sub square's level
 *   VCF  CUT   cutoff (the ENV -> FILTER amount and the routings add to it); key tracking fixed at 5/8
 *        RES   resonance (the second stage's Q: 0.77 .. ~7.6, as NEON's)
 *        HPF   the high-pass after the VCF: 0 off, 127 ~1.5 kHz
 *        NOIS  white noise into the VCF
 *
 * Chorus I / II: the track's chorus send (the stereo bus chorus, fx.c).
 *
 * Voice state: ph[0] the DCO; ph[2] the sub divider (bit 31) | ph[0] >> 1; s[0] s[1], s[6] s[7] the VCF;
 * s[2] the noise generator; s[3] the LFO's phase; s[4] the HPF's state. keep: the VCF and the HPF. */

#define JUNO_TRIM 11796          /* 0.72 (-2.9 dB), as NEON JUNO */
#define JUNO_KTR 80              /* key tracking (as NEON's KTR, 0..127): 5/8 */

static const char *const N_JUNO_MODE[] = {"MAN", "ENV"};   /* (params.c F_LFOHZ: 0 and the top named) */

static void juno_note_on(track_t *t, voice_t *v)
{
    uint32_t r = 0x9E3779B9u ^ (v->age * 2654435761u);
    (void)t;
    if (!r)
        r = 1;
    v->s[2] = (int32_t)r;                         /* the noise */
    v->s[3] = (int32_t)(r * 2654435761u);         /* the LFO starts anywhere */
    v->s[0] = v->s[1] = v->s[6] = v->s[7] = 0;    /* (kept on a retrigger) */
    v->s[4] = 0;
}

static void juno_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    int32_t puls = p[P_E0], mode = p[P_E1], sawl = p[P_E2], subl = p[P_E3];
    int32_t hpf = p[P_E6], nois = p[P_E7];
    uint32_t lph = (uint32_t)v->s[3], i;
    int32_t cut = (p[P_E4] << 8) + m->cutoff + (JUNO_KTR * (v->pitch16 - 60 * 16) >> 4);
    int32_t depth = puls > 0 ? puls - 1 : 0;                      /* 0..126 */
    int32_t mv;                                                   /* what moves the width, 0..32767 */
    int32_t lo, ow;
    uint32_t pw, inc = m->inc, inc2 = m->inc >> 1, hi = v->ph[2] & 0x80000000u, ph0 = v->ph[0];
    int32_t sw = sawl * 258, sb = subl * 258, sp = puls ? 32767 : 0, sn = nois * 200;   /* levels, Q15 */
    int32_t norm;
    int32_t a1 = v->s[0], a2 = v->s[1], b1 = v->s[6], b2 = v->s[7], hs = v->s[4], ha = 0;
    tsvf_t f1, f2;

    if (!mode)
        mv = 32767;                                               /* MAN */
    else if (mode >= 127)
        mv = clamp(m->envq15, 0, 32767);                          /* ENV */
    else
        mv = (osc_tri(lph) + 32768) >> 1;                         /* LFO */
    lo = -mv * depth * 467;                                       /* up to -45 % of the cycle */
    ow = clamp(shl(m->shape - (64 << 8), 15) + lo, -0x73000000, 0x73000000);
    pw = 0x80000000u + (uint32_t)ow;
    norm = (int32_t)((32767u * 32768u) /
                     (uint32_t)(32768 + ((sp * 11) >> 4) + ((sw * 11) >> 4) + ((sb * 11) >> 4) + ((sn * 5) >> 4)));
    tsvf_coef_k(&f1, cut, NEON_K_STAGE1);
    tsvf_coef_k(&f2, cut, NEON_K_STAGE2 - p[P_E5] * 2600 / 127);
    if (hpf) {                                                    /* one-pole (trapezoidal) a = g / (1 + g), Q15 */
        int32_t g = SVF_G[hpf * 79 / 127];                        /* (the cutoff table's ~1.5 kHz at 127) */
        ha = (int32_t)(((uint32_t)g << 15) / (uint32_t)(4096 + g));
    }

    for (i = 0; i < n; i++) {
        uint32_t spn = hi | (ph0 >> 1);
        int32_t sa = osc_saw(ph0, inc);
        int32_t pa = sp ? (sa - osc_saw(ph0 + pw, inc)) >> 1 : 0;
        int32_t sq = osc_pulse(spn, inc2, 0x80000000u);
        int32_t s = ((sw * sa) >> 15) - pa + ((sb * sq) >> 15);  /* the pulse inverted against the saw, as the DCO */
        if (sn)
            s += (sn * ((int32_t)noise32(&v->s[2]) >> 16)) >> 15;
        s = ((s >> 1) * norm) >> 14;
        if (ph0 + inc < ph0)                                      /* the DCO wrapped: the divider flips */
            hi ^= 0x80000000u;
        ph0 += inc;
        {
            int32_t y = tsvf_lp(&f2, tsvf_lp(&f1, s >> 1, &a1, &a2), &b1, &b2);
            if (ha) {                                             /* high-pass: y minus its one-pole low-pass */
                int32_t d = ((y - hs) * ha) >> 15, l = hs + d;
                hs = l + d;
                y -= l;
            }
            s = (soft_knee(y, 16000) * JUNO_TRIM) >> 13;
        }
        out[i] += voice_amp(s, m, i) * 2;
    }
    v->ph[0] = ph0;
    v->ph[2] = hi | (ph0 >> 1);
    v->s[0] = a1;
    v->s[1] = a2;
    v->s[6] = b1;
    v->s[7] = b2;
    v->s[4] = hs;
    v->s[3] = (int32_t)(lph + (mode > 0 && mode < 127 ? LFO_INC[mode] : 0u));
}

/* the presets: e = PULS MODE SAW SUB CUT RES HPF NOIS; env = ATK DEC SUS REL; fenv; FX(dist, chorus, delay, reverb) */
static const preset_t JUNO_PRESETS[] = {
    /* Neon Summers' TAL U-NO-LX patches, carried over by tools (PAD, LEAD, KEY, ARP, BASS: category.c) */
    {"AM ETERNAL", {0, 61, 100, 49, 109, 24, 3, 25}, {99, 111, 79, 102}, 12, 0, FX(0, 70, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 55)},
    {"BLISS", {95, 127, 0, 0, 45, 0, 35, 14}, {0, 122, 89, 108}, 63, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 42, P_LRATE, 70, P_LD_FLT, 1, P_LWAVE, 4)},
    {"BODY TALK", {107, 0, 0, 0, 45, 38, 0, 0}, {63, 118, 81, 101}, 47, 0, FX(0, 45, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 25, P_LRATE, 79, P_LFADE, 68, P_LD_PIT, 4, P_LWAVE, 1, P_TRANS, -12)},
    {"DECAY ATK", {1, 36, 100, 0, 52, 0, 21, 44}, {45, 97, 53, 104}, 6, 0, FX(0, 70, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 55, P_LRATE, 36, P_LFADE, 84, P_LD_PIT, 1, P_LD_FLT, 40, P_LWAVE, 1)},
    {"LONGO ATK", {1, 0, 100, 0, 82, 0, 58, 0}, {59, 103, 25, 94}, 46, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -20, P_LRATE, 58, P_LFADE, 70, P_LD_PIT, 3, P_LD_FLT, 31, P_LWAVE, 1)},
    {"LONGO ATK2", {0, 0, 100, 0, 89, 27, 0, 0}, {67, 105, 27, 98}, 43, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -20)},
    {"MIAMI SQR", {1, 67, 0, 0, 114, 20, 0, 0}, {61, 104, 82, 86}, 0, 0, FX(0, 45, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 55)},
    {"NIGHTCLOUD", {0, 127, 100, 0, 98, 0, 43, 0}, {18, 118, 12, 92}, 0, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -20, P_LRATE, 59, P_LD_PIT, 1, P_LD_FLT, 4, P_LWAVE, 1)},
    {"SLOW SOFT", {127, 0, 100, 0, 71, 0, 0, 0}, {110, 127, 70, 114}, 25, 0, FX(0, 45, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 55, P_LRATE, 13, P_LD_PIT, 1, P_LD_FLT, 40, P_LWAVE, 1)},
    {"SOFT ARRAY", {0, 0, 100, 0, 93, 0, 0, 0}, {53, 127, 31, 103}, 0, 0, FX(0, 45, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 39, P_LRATE, 25, P_LFADE, 94, P_LD_PIT, 1, P_LD_FLT, 4, P_LWAVE, 1)},
    {"SOFT PAD", {0, 0, 100, 0, 93, 38, 0, 0}, {0, 127, 0, 96}, 0, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 39, P_LRATE, 73, P_LFADE, 94, P_LD_FLT, 4, P_LWAVE, 1)},
    {"WARP SOFT", {0, 0, 100, 0, 80, 0, 33, 0}, {86, 84, 82, 104}, 12, 0, FX(0, 45, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 46, P_LRATE, 26, P_LD_PIT, 1, P_LWAVE, 1)},
    {"WE TRUST", {0, 127, 100, 0, 106, 50, 0, 0}, {0, 99, 72, 86}, 0, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 55)},
    {"JUNO FUNK", {0, 0, 100, 0, 78, 89, 0, 0}, {49, 103, 55, 0}, 45, 0, FX(0, 70, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 25, P_LRATE, 79, P_LFADE, 65, P_LD_PIT, 2, P_LWAVE, 1)},
    {"KARATE", {0, 0, 100, 0, 99, 0, 0, 0}, {0, 86, 50, 90}, 39, 0, FX(0, 70, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 46, P_LRATE, 79, P_LD_PIT, 1, P_LWAVE, 1)},
    {"MIDTOWN", {127, 0, 100, 0, 96, 18, 38, 0}, {0, 89, 50, 78}, 20, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 46, P_LRATE, 79, P_LD_PIT, 1, P_LWAVE, 1)},
    {"PEACOCK", {102, 67, 100, 110, 117, 35, 0, 10}, {0, 101, 62, 76}, 0, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 55)},
    {"PIPER LEAD", {0, 48, 0, 17, 83, 0, 0, 3}, {57, 85, 67, 86}, 29, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 11, P_LRATE, 48, P_LD_PIT, 1, P_LWAVE, 1, P_TRANS, 12)},
    {"SCHWARZ", {0, 67, 100, 0, 51, 31, 0, 19}, {10, 120, 56, 50}, 63, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 16, P_LRATE, 67, P_LFADE, 88, P_LD_PIT, 1, P_LWAVE, 1)},
    {"SUMMERTIME", {127, 63, 100, 110, 81, 44, 0, 0}, {0, 89, 82, 81}, 62, 0, FX(0, 45, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 55, P_LRATE, 63, P_LD_FLT, 31, P_LWAVE, 1)},
    {"SWEEP LEAD", {37, 127, 100, 0, 127, 12, 37, 0}, {29, 127, 25, 18}, 51, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -20, P_LRATE, 38, P_LD_PIT, 1, P_LD_FLT, 31, P_LWAVE, 1)},
    {"TEEN ROMNC", {0, 67, 100, 110, 127, 20, 0, 0}, {0, 0, 93, 67}, 0, 0, FX(0, 45, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 55)},
    {"TRANSIST", {0, 127, 100, 12, 111, 43, 9, 36}, {32, 115, 62, 101}, 8, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 55, P_LRATE, 67, P_LD_PIT, 1, P_LWAVE, 1)},
    {"VHS MODE", {95, 127, 0, 0, 45, 0, 35, 40}, {0, 122, 54, 103}, 63, 0, FX(0, 70, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 42, P_LRATE, 70, P_LD_FLT, 1, P_LWAVE, 4)},
    {"CHO CHO", {80, 127, 0, 0, 82, 0, 34, 0}, {0, 70, 51, 95}, 27, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -20, P_LRATE, 18, P_LD_PIT, 4, P_LWAVE, 2)},
    {"CHO CHO FM", {28, 81, 100, 0, 85, 0, 86, 0}, {0, 78, 28, 43}, 34, 0, FX(0, 70, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -20, P_LRATE, 81, P_LD_PIT, 1, P_LWAVE, 1, P_TRANS, -12)},
    {"CROCKETT", {0, 0, 100, 0, 88, 63, 0, 0}, {0, 80, 34, 92}, 23, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 24, P_TRANS, 12)},
    {"DOWNTOWN", {100, 0, 0, 0, 45, 48, 0, 0}, {0, 122, 75, 93}, 58, 0, FX(0, 45, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 16, P_TRANS, -12)},
    {"FIRST PAD", {0, 127, 100, 0, 92, 76, 0, 0}, {0, 106, 7, 69}, 0, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 55)},
    {"LONGO SOFT", {0, 0, 100, 0, 89, 27, 0, 0}, {0, 105, 27, 97}, 43, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -20, P_TRANS, -12)},
    {"MASKING", {1, 127, 0, 28, 100, 0, 47, 0}, {27, 52, 24, 76}, 17, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 55)},
    {"MIAMI FINE", {107, 0, 0, 0, 102, 75, 0, 0}, {0, 105, 48, 95}, 18, 0, FX(0, 70, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 25, P_LRATE, 79, P_LFADE, 71, P_LD_PIT, 1, P_LWAVE, 1, P_TRANS, 12)},
    {"PIANA", {80, 0, 100, 0, 74, 29, 0, 0}, {0, 106, 34, 87}, 39, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 55, P_LRATE, 57, P_LD_FLT, 28, P_LWAVE, 1, P_TRANS, -12)},
    {"PLONK", {1, 127, 0, 0, 80, 0, 60, 0}, {46, 73, 20, 96}, 33, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 55, P_LRATE, 48, P_LFADE, 110, P_LD_PIT, 1, P_LD_FLT, 39, P_LWAVE, 1)},
    {"PLONK 1", {92, 0, 0, 0, 63, 0, 0, 0}, {0, 86, 53, 94}, 39, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 46, P_LRATE, 75, P_LD_PIT, 1, P_LWAVE, 1)},
    {"THE 90S", {0, 78, 100, 0, 84, 34, 0, 23}, {17, 85, 36, 66}, 42, 0, FX(0, 45, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 55)},
    {"UNIVERSAL", {40, 0, 100, 110, 108, 20, 0, 19}, {0, 107, 29, 90}, 0, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 17, P_TRANS, 12)},
    {"WASH BEACH", {0, 127, 100, 0, 121, 76, 0, 0}, {0, 99, 31, 87}, 0, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 55)},
    {"16 PUMP", {77, 13, 0, 110, 76, 0, 23, 48}, {0, 86, 0, 86}, 17, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -3, P_TRANS, -12, P_AMODE, 3, P_ARATE, 2, P_AOCT, 1)},
    {"BODYWORK", {79, 127, 0, 110, 68, 81, 26, 48}, {0, 76, 0, 0}, 23, 0, FX(0, 45, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 55, P_LRATE, 26, P_LD_PIT, 1, P_LWAVE, 1, P_TRANS, 12, P_AMODE, 3, P_ARATE, 2, P_AOCT, 3)},
    {"CROCKETT A", {127, 127, 0, 0, 92, 0, 0, 0}, {0, 71, 14, 75}, 32, 0, FX(0, 70, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 48, P_LRATE, 61, P_LFADE, 110, P_LD_FLT, 32, P_LWAVE, 1, P_AMODE, 3, P_ARATE, 2, P_AOCT, 3)},
    {"DREAM ARP", {0, 48, 0, 11, 66, 0, 0, 3}, {57, 85, 67, 103}, 29, 0, FX(0, 45, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 11, P_LRATE, 48, P_LD_PIT, 1, P_LWAVE, 1, P_AMODE, 3, P_ARATE, 2, P_AOCT, 2)},
    {"DREAM SOFT", {127, 48, 0, 11, 63, 0, 0, 0}, {65, 85, 67, 103}, 29, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 11, P_LRATE, 48, P_LD_PIT, 1, P_LWAVE, 1, P_TRANS, 12, P_AMODE, 3, P_ARATE, 2, P_AOCT, 4)},
    {"DUMPLING", {57, 127, 0, 0, 77, 48, 5, 0}, {4, 67, 43, 57}, 26, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 18, P_LRATE, 46, P_LD_PIT, 8, P_LD_FLT, -40, P_LWAVE, 2, P_AMODE, 3, P_ARATE, 2, P_AOCT, 3)},
    {"DUMPLING 2", {1, 127, 100, 77, 78, 36, 33, 0}, {4, 80, 43, 63}, 27, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 18, P_LRATE, 32, P_LD_PIT, 8, P_LWAVE, 2, P_AMODE, 3, P_ARATE, 2, P_AOCT, 3)},
    {"MAGIC HOUR", {28, 51, 100, 0, 80, 0, 86, 0}, {0, 78, 28, 85}, 28, 0, FX(0, 70, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -20, P_LRATE, 51, P_LD_PIT, 3, P_LWAVE, 1, P_TRANS, -12, P_AMODE, 3, P_ARATE, 2, P_AOCT, 4)},
    {"OLYMPUMP", {40, 27, 100, 88, 100, 81, 0, 0}, {0, 82, 0, 51}, 17, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 17, P_LRATE, 27, P_LD_PIT, 1, P_LWAVE, 1, P_GLIDE, 52, P_TRANS, -12, P_AMODE, 3, P_ARATE, 2, P_AOCT, 1)},
    {"PLONK ARP", {1, 127, 0, 16, 100, 0, 0, 0}, {27, 52, 24, 82}, 17, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 55, P_AMODE, 3, P_ARATE, 2, P_AOCT, 3)},
    {"RUMPY REZ", {123, 127, 100, 110, 127, 28, 0, 0}, {0, 77, 13, 54}, 0, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -20, P_TRANS, -12, P_AMODE, 2, P_ARATE, 2, P_AOCT, 1)},
    {"SOFT ARP", {89, 0, 100, 0, 55, 0, 0, 0}, {0, 127, 127, 92}, 35, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 55, P_LRATE, 21, P_LFADE, 91, P_LD_PIT, 1, P_LD_FLT, 40, P_LWAVE, 1, P_AMODE, 3, P_ARATE, 2, P_AOCT, 3)},
    {"STRANGER", {73, 0, 100, 110, 106, 72, 0, 0}, {0, 82, 0, 63}, 0, 0, FX(0, 45, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 17, P_LRATE, 13, P_LD_PIT, 2, P_LD_FLT, 11, P_LWAVE, 1, P_TRANS, -12, P_AMODE, 3, P_ARATE, 2, P_AOCT, 1)},
    {"THING ARP", {0, 0, 100, 0, 76, 0, 110, 0}, {39, 80, 68, 107}, 0, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -20, P_LRATE, 118, P_LFADE, 98, P_LD_PIT, 24, P_LWAVE, 2, P_TRANS, -12, P_AMODE, 3, P_ARATE, 2, P_AOCT, 1)},
    {"WARP ARP", {127, 0, 100, 0, 97, 0, 29, 0}, {0, 83, 0, 83}, -8, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 55, P_LRATE, 39, P_LD_PIT, 1, P_LD_FLT, 7, P_LWAVE, 1, P_TRANS, 12, P_AMODE, 3, P_ARATE, 2, P_AOCT, 1)},
    {"WONDER ARP", {30, 0, 100, 0, 64, 2, 27, 0}, {77, 63, 127, 86}, 39, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 55, P_LRATE, 13, P_LD_FLT, 3, P_LWAVE, 1, P_AMODE, 3, P_ARATE, 2, P_AOCT, 3)},
    {"1ST BASS", {0, 127, 100, 106, 61, 0, 0, 0}, {0, 92, 19, 25}, 44, 0, FX(0, 45, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 55, P_LRATE, 74, P_LFADE, 76, P_LD_PIT, 1, P_LWAVE, 2)},
    {"BECKOFOLEY", {112, 13, 100, 110, 90, 0, 23, 48}, {17, 86, 0, 69}, 24, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 4, P_TRANS, -12)},
    {"BODY BASS", {0, 67, 100, 110, 91, 37, 0, 0}, {0, 77, 16, 74}, 25, 0, FX(0, 45, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 55, P_LRATE, 67, P_LD_FLT, 7, P_LWAVE, 1, P_TRANS, -12)},
    {"EXPLORE", {112, 13, 100, 0, 78, 0, 23, 48}, {0, 77, 0, 88}, 21, 0, FX(0, 0, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 4, P_TRANS, -12)},
    {"MANTRONICA", {0, 127, 100, 0, 121, 76, 0, 0}, {0, 99, 31, 39}, 0, 1, FX(0, 70, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 55, P_TRANS, 12)},
    {"MIAMI 2K15", {0, 0, 100, 92, 78, 39, 49, 3}, {10, 102, 38, 77}, 26, 0, FX(0, 45, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 16, P_TRANS, -12)},
    {"MORPH BASS", {127, 127, 100, 0, 82, 0, 21, 0}, {0, 60, 127, 10}, 31, 0, FX(0, 45, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 55, P_LRATE, 35, P_LD_FLT, 3, P_LWAVE, 1, P_TRANS, -12)},
    {"NORTH BASS", {36, 13, 100, 87, 75, 25, 0, 0}, {0, 87, 13, 57}, 30, 0, FX(0, 45, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -20, P_TRANS, -12)},
    {"NORTH HARD", {0, 0, 100, 83, 74, 45, 0, 3}, {0, 60, 127, 10}, 23, 1, FX(0, 45, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, -20)},
    {"RUMPY", {112, 127, 100, 110, 86, 50, 0, 73}, {0, 74, 20, 77}, 32, 1, FX(0, 45, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 46, P_LRATE, 48, P_LFADE, 110, P_LD_FLT, 28, P_LWAVE, 1)},
    {"SWEEPER", {0, 127, 100, 27, 87, 47, 11, 0}, {0, 60, 127, 10}, 27, 0, FX(0, 45, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 19, P_LRATE, 29, P_LFADE, 82, P_LD_PIT, 1, P_LD_FLT, 28, P_LWAVE, 1)},
    {"THING BASS", {81, 0, 100, 0, 79, 30, 0, 0}, {0, 89, 50, 71}, 27, 0, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 46, P_LRATE, 58, P_LD_PIT, 1, P_LD_FLT, 2, P_LWAVE, 1, P_TRANS, -12)},
    {"UNDERWATER", {0, 67, 100, 110, 92, 91, 0, 0}, {0, 100, 0, 80}, 0, 1, FX(0, 90, 0, 0), XP(P_M1SRC, 4, P_M1DST, 2, P_M1AMT, 55)},
};

static const engine_t ENG_JUNO = {
    .name = "JUNO",
    .page_title = {"DCO", "VCF"},
    .edit = {
        {"PULS", F_PCT, 0, 127, 64, 0, 0},
        {"MODE", F_LFOHZ, 0, 127, 67, N_JUNO_MODE, 0},
        {"SAW", F_PCT, 0, 127, 0, 0, 0},
        {"SUB", F_PCT, 0, 127, 50, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 90, 0, 0},
        {"RES", F_PCT, 0, 127, 10, 0, 0},
        {"HPF", F_PCT, 0, 127, 0, 0, 0},
        {"NOIS", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = JUNO_PRESETS,
    .npresets = NELEM(JUNO_PRESETS),
    .note_on = juno_note_on,
    .render = juno_render,
    .knob = {P_E4, P_E5, P_E0, P_REL},   /* HOME: CUT, RES, PULS, REL */
    .poly = 6,
    .keep = 0xD3,                        /* the VCF (bits 0, 1, 6, 7), the HPF (4) */
};
