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
    {"INIT", {64, 67, 0, 50, 90, 10, 0, 0}, {0, 70, 100, 60}, 0, 0, FX(0, 60, 0, 0)},
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
