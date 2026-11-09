/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Neon Summers
 * Written for the Felucca engine interface (Felucca-Engines, MIT, Leo Kuroshita / Hugelton Instruments). */
/* NEON: a synthwave voice. Three oscillator modes into a 24 dB/oct low-pass (two cascaded trapezoidal SVFs),
 * with warmth (drive before the filter) and MOVE, a slow per-voice motion that makes it breathe like an old poly.
 *
 *   WAVE SUPER  five saws, a centre and two detuned pairs (the 80s/JP "supersaw" pad and brass stack).
 *               SPRD sets the outer pair's detune (0..60 cents, the inner pair a third of it), SIDE the level of
 *               the four side saws against the centre. MOVE breathes the spread +-25 % with the voice's LFO.
 *   WAVE PWM    a Juno-style DCO: two pulse waves (the second detuned by SPRD, up to 30 cents) whose width the voice's
 *               LFO sweeps (MOVE: up to +-25 % of the cycle, the two in opposite directions) on top of the shape
 *               routings, plus SAW, the same DCOs' saws mixed in (Juno 106: saw and pulse at once).
 *   WAVE JUNO   one Juno-60 DCO: a pulse whose width the voice's LFO sweeps (PWM: up to +-45 % of the cycle around
 *               50 %), its saw (SAW) and the sub square an octave down, divided from the DCO itself (SUB), so it is locked
 *               to it. No detune and no drift: a DCO. RATE (in place of WARM) is the LFO's rate, 0.05..40 Hz as the
 *               track LFO's; the LFO runs free, every note from its own point. (Made to rebuild a TAL U-NO-LX pad.)
 *   WAVE SUB    a bass voice: two saws (the second detuned by SPRD) and a square an octave below (SUB: up to twice
 *               the saws' level, for a Juno-style sub-heavy bass). The phases
 *               restart with every note, so each one hits the same way: punchy, sequenced 8th/16th-note bass.
 *
 *   MOVE also gives each note its own small tuning offset (up to +-3.3 cents) and a slow pitch wobble (up to +-3.4
 *   cents, tape-like at 127). The LFO runs at 0.5..1 Hz, a different rate for every note, so chords shimmer.
 *   WARM drives the oscillators into a soft clipper (1x..3x) before the filter.
 *   RES raises the second filter stage's Q (0.77 .. ~7.6); at RES 0 the two stages are a Butterworth 4-pole.
 *   KTR is the filter's key tracking, as ANALOG's.
 *
 * Voice state (felucca_engine.h's voice_t):
 *   ph[0]       centre saw (SUPER) / DCO A (PWM) / saw A (SUB)
 *   ph[1]       inner side saw + (SUPER) / DCO B (PWM) / saw B (SUB)
 *   ph[2]       inner side saw - (SUPER) / -    (PWM) / the sub square (SUB)
 *   s[0], s[1]  filter stage 1          s[6], s[7]  filter stage 2
 *   s[4], s[5]  outer side saws (SUPER), as phases (uint32 in int32)
 *   s[2]        the note's random seed (its drift and LFO rate)      s[3]  the LFO's phase
 * keep: the filters and the outer saws' phases (bits 0, 1, 4..7): a retriggered voice glides on, as on an analog. */
static const char *const N_NEON_WAVE[] = {"SUPER", "PWM", "SUB", "JUNO"};
enum { NW_SUPER, NW_PWM, NW_SUB, NW_JUNO };
static const param_desc_t NEON_SIDE = {"SIDE", F_PCT, 0, 127, 90, 0, 0};   /* E2's range and default, per WAVE */
static const param_desc_t NEON_SAW = {"SAW", F_PCT, 0, 127, 90, 0, 0};
static const param_desc_t NEON_SUB = {"SUB", F_PCT, 0, 127, 45, 0, 0};    /* 64: the saws' level, 127: twice it */
static const param_desc_t NEON_J_SUB = {"SUB", F_PCT, 0, 127, 50, 0, 0};    /* JUNO: E1, E3 and E6 too */
static const param_desc_t NEON_J_PWM = {"PWM", F_PCT, 0, 127, 40, 0, 0};
static const param_desc_t NEON_J_RATE = {"RATE", F_LFOHZ, 0, 127, 67, 0, 0};

/* the LFO: 0.5 Hz a block (CTL samples) plus up to 0.5 Hz more, a note's own (its seed) */
#define NEON_LFO_BASE ((uint32_t)(((uint64_t)CTL << 31) / (uint64_t)FS))
#define NEON_K_STAGE1 7568       /* stage 1 damping, Q12: Q 0.54 (4-pole Butterworth's first pair) */
#define NEON_K_STAGE2 3135       /* stage 2 at RES 0: Q 1.31 (the second pair) */
/* the output trim per WAVE, after the filter (so WARM drives the same): matched to ANALOG's levels through the same
 * phrases (a voice near ANALOG's single-note peaks, a 4-note chord near its pads'), Q14 */
#define NEON_TRIM_SUPER 10322    /* 0.63 (-4.0 dB) */
#define NEON_TRIM_PWM 11796      /* 0.72 (-2.9 dB) */
#define NEON_TRIM_SUB 13926      /* 0.85 (-1.4 dB) */
#define NEON_TRIM_JUNO 11796     /* 0.72 (-2.9 dB), as PWM */

static const param_desc_t *neon_desc(const track_t *t, uint32_t k)
{
    if (t->p[P_E0] == NW_JUNO)
        return k == 1u ? &NEON_J_SUB : k == 2u ? &NEON_SAW : k == 3u ? &NEON_J_PWM : k == 6u ? &NEON_J_RATE : 0;
    if (k != 2u)
        return 0;
    switch (t->p[P_E0]) {
    case NW_PWM:
        return &NEON_SAW;
    case NW_SUB:
        return &NEON_SUB;
    default:
        return &NEON_SIDE;
    }
}

static uint32_t neon_rnd(uint32_t *x)            /* xorshift32 */
{
    uint32_t s = *x;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *x = s;
    return s;
}

static void neon_note_on(track_t *t, voice_t *v)
{
    uint32_t r = 0x9E3779B9u ^ (v->age * 2654435761u);
    if (!r)
        r = 1;
    neon_rnd(&r);
    v->s[2] = (int32_t)r;                         /* this note's drift and LFO rate */
    v->s[3] = (int32_t)neon_rnd(&r);              /* the LFO starts anywhere: no two notes move together */
    v->s[0] = v->s[1] = v->s[6] = v->s[7] = 0;    /* the filters (kept on a retrigger) */
    if (t->p[P_E0] == NW_SUB) {                   /* bass: every note starts its waves at the same point */
        v->ph[0] = v->ph[1] = 0x80000000u;      /* the saws start at their zero crossing */
        v->ph[2] = 0x40000000u;                 /* the sub square mid-way through a half-cycle, off its edge */
        v->s[4] = v->s[5] = 0;
    } else {                                      /* free-running oscillators: random phases, no attack click */
        v->ph[0] = neon_rnd(&r);
        v->ph[1] = neon_rnd(&r);
        v->ph[2] = neon_rnd(&r);
        v->s[4] = (int32_t)neon_rnd(&r);
        v->s[5] = (int32_t)neon_rnd(&r);
    }
}

/* inc * (1 + f / 4096): f in 1/4096 (1 cent = 2.367) */
static inline uint32_t neon_det(uint32_t inc, int32_t f)
{
    return inc + (uint32_t)((int32_t)(inc >> 12) * f);
}

static void neon_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t wave = (uint32_t)p[P_E0], i;
    int32_t sprd = p[P_E1], blnd = p[P_E2], move = p[P_E3], warm = p[P_E6];
    uint32_t seed = (uint32_t)v->s[2];
    uint32_t lph = (uint32_t)v->s[3];
    int32_t lt = osc_tri(lph);                                    /* the LFO, -32768..32767 */
    /* drift: the note's own offset (+-16 x MOVE / 256: up to +-3.3 ct) and the slow wobble (up to +-3.4 ct) */
    int32_t drift = (((int32_t)((seed >> 4) & 31u) - 16) * move) >> 8;
    int32_t wob = ((lt >> 10) * move) >> 9;
    uint32_t inc0 = neon_det(m->inc, drift + wob);
    int32_t cut = (p[P_E4] << 8) + m->cutoff + (p[P_E7] * (v->pitch16 - 60 * 16) >> 4);
    tsvf_t f1, f2;
    int32_t drive = 32768 + warm * 512;                          /* 1x .. 3x */
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
    uint32_t ph3 = (uint32_t)v->s[4], ph4 = (uint32_t)v->s[5];
    int32_t a1 = v->s[0], a2 = v->s[1], b1 = v->s[6], b2 = v->s[7];
    tsvf_coef_k(&f1, cut, NEON_K_STAGE1);
    tsvf_coef_k(&f2, cut, NEON_K_STAGE2 - p[P_E5] * 2600 / 127);

    if (wave == NW_SUPER) {
        /* the outer pair's detune: SPRD 0..127 -> 0..142 (60 ct), breathing +-25 % with MOVE; the inner a third */
        int32_t fo = sprd * 142 / 127;
        int32_t fi;
        uint32_t inc1, inc2, inc3, inc4;
        int32_t sg = blnd * 258;                                 /* the side saws' level, Q15 */
        int32_t norm = (int32_t)((32767u * 32768u) / (uint32_t)(32768 + ((sg * 13) >> 3)));   /* 1 / (1 + 1.6 sg) */
        fo += (fo * ((((lt >> 7) * move) >> 7))) >> 10;
        fi = (fo * 21) >> 6;
        inc1 = neon_det(inc0, fi);
        inc2 = neon_det(inc0, -fi - (fi >> 4));                  /* a little asymmetric: no perfect beat */
        inc3 = neon_det(inc0, fo);
        inc4 = neon_det(inc0, -fo + (fo >> 4));
        for (i = 0; i < n; i++) {
            int32_t c = osc_saw(ph0, inc0);
            int32_t sd = osc_saw(ph1, inc1) + osc_saw(ph2, inc2) + osc_saw(ph3, inc3) + osc_saw(ph4, inc4);
            int32_t s = c + ((sg * (sd >> 2)) >> 13);            /* sd >> 2 x 4: the four sides at sg */
            s = ((s >> 2) * norm) >> 13;
            ph0 += inc0;
            ph1 += inc1;
            ph2 += inc2;
            ph3 += inc3;
            ph4 += inc4;
            if (warm)
                s = softclip(((s >> 2) * (drive >> 2)) >> 11);
            {
                int32_t y = tsvf_lp(&f2, tsvf_lp(&f1, s >> 1, &a1, &a2), &b1, &b2);
                s = (soft_knee(y, 16000) * NEON_TRIM_SUPER) >> 13;
            }
            out[i] += voice_amp(s, m, i) * 2;
        }
    } else if (wave == NW_JUNO) {
        /* one DCO: the pulse's width 50 % + the shape routings (+-12.5 %) + the LFO x PWM (+-45 %); its saw at SAW;
         * the sub a square an octave down, its phase the DCO's with one more bit on top (ph2's bit 31): locked to it */
        int32_t so = shl(m->shape - (64 << 8), 15), lo = lt * move * 463;   /* +-0x7300_0000 at 127 */
        int32_t ow = clamp(so + lo, -0x73000000, 0x73000000);
        uint32_t pw = 0x80000000u + (uint32_t)ow, inc = m->inc, inc2 = m->inc >> 1, hi = ph2 & 0x80000000u;
        int32_t sw = blnd * 258, sb = sprd * 258;                /* SAW, SUB: Q15 */
        int32_t norm = (int32_t)((32767u * 32768u) / (uint32_t)(32768 + ((sw * 11) >> 4) + ((sb * 11) >> 4)));
        for (i = 0; i < n; i++) {
            uint32_t sp = hi | (ph0 >> 1);
            int32_t sa = osc_saw(ph0, inc);
            int32_t pa = (sa - osc_saw(ph0 + pw, inc)) >> 1;
            int32_t sq = osc_pulse(sp, inc2, 0x80000000u);
            int32_t s = ((sw * sa) >> 15) - pa + ((sb * sq) >> 15);   /* the pulse inverted against the saw, as the DCO */
            s = ((s >> 1) * norm) >> 14;
            if (ph0 + inc < ph0)                                 /* the DCO wrapped: the divider flips */
                hi ^= 0x80000000u;
            ph0 += inc;
            {
                int32_t y = tsvf_lp(&f2, tsvf_lp(&f1, s >> 1, &a1, &a2), &b1, &b2);
                s = (soft_knee(y, 16000) * NEON_TRIM_JUNO) >> 13;
            }
            out[i] += voice_amp(s, m, i) * 2;
        }
        ph2 = hi | (ph0 >> 1);
    } else if (wave == NW_PWM) {
        /* the width: 50 % + the shape routings (+-12.5 %) + the LFO x MOVE (+-25 %), B swept the other way */
        int32_t so = shl(m->shape - (64 << 8), 15), lo = shl(((lt * move) >> 7), 15);
        int32_t oa = clamp(so + lo, -0x70000000, 0x70000000), ob = clamp(so - lo, -0x70000000, 0x70000000);
        uint32_t pwa = 0x80000000u + (uint32_t)oa, pwb = 0x80000000u + (uint32_t)ob;
        uint32_t inc1 = neon_det(inc0, sprd * 71 / 127);         /* B: up to +30 ct */
        int32_t sw = blnd * 258;                                 /* the saws' level, Q15 */
        int32_t norm = (int32_t)((32767u * 32768u) / (uint32_t)(32768 + ((sw * 11) >> 4)));   /* 1 / (1 + 0.7 sw) */
        for (i = 0; i < n; i++) {
            int32_t sa = osc_saw(ph0, inc0), sb = osc_saw(ph1, inc1);
            int32_t pa = (sa - osc_saw(ph0 + pwa, inc0)) >> 1;   /* osc_pulse, its saw shared with SAW */
            int32_t pb = (sb - osc_saw(ph1 + pwb, inc1)) >> 1;
            int32_t s = ((pa + pb) >> 1) + ((sw * ((sa + sb) >> 1)) >> 15);
            s = ((s >> 1) * norm) >> 14;
            ph0 += inc0;
            ph1 += inc1;
            if (warm)
                s = softclip(((s >> 2) * (drive >> 2)) >> 11);
            {
                int32_t y = tsvf_lp(&f2, tsvf_lp(&f1, s >> 1, &a1, &a2), &b1, &b2);
                s = (soft_knee(y, 16000) * NEON_TRIM_PWM) >> 13;
            }
            out[i] += voice_amp(s, m, i) * 2;
        }
    } else {
        /* SUB: saw A, saw B (+SPRD: up to +30 ct, wobbling a little the other way), a square an octave down */
        uint32_t inc1 = neon_det(m->inc, sprd * 71 / 127 - wob);
        uint32_t inc2 = inc0 >> 1;
        int32_t sg = blnd * 516;                                 /* SUB: 0..2, Q15 */
        int32_t norm = (int32_t)((32767u * 32768u) / (uint32_t)(32768 + ((sg * 5) >> 3)));    /* 1 / (1 + 0.6 sg) */
        for (i = 0; i < n; i++) {
            int32_t sa = osc_saw(ph0, inc0), sb = osc_saw(ph1, inc1);
            int32_t sq = osc_pulse(ph2, inc2, 0x80000000u);
            int32_t s = ((sa + sb) >> 1) + ((sg >> 1) * sq >> 14);
            s = ((s >> 1) * norm) >> 14;
            ph0 += inc0;
            ph1 += inc1;
            ph2 += inc2;
            if (warm)
                s = softclip(((s >> 2) * (drive >> 2)) >> 11);
            {
                int32_t y = tsvf_lp(&f2, tsvf_lp(&f1, s >> 1, &a1, &a2), &b1, &b2);
                s = (soft_knee(y, 16000) * NEON_TRIM_SUB) >> 13;
            }
            out[i] += voice_amp(s, m, i) * 2;
        }
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    v->s[4] = (int32_t)ph3;
    v->s[5] = (int32_t)ph4;
    v->s[0] = a1;
    v->s[1] = a2;
    v->s[6] = b1;
    v->s[7] = b2;
    v->s[3] = (int32_t)(lph + (wave == NW_JUNO ? LFO_INC[p[P_E6] & 127]
                                               : NEON_LFO_BASE + (NEON_LFO_BASE >> 16) * ((seed >> 8) & 0xFFFFu)));
}

/* the presets: e = WAVE SPRD BLND MOVE CUT RES WARM KTR; env = ATK DEC SUS REL (1 ms x 10000^(v/127): 64 ~0.1 s,
 * 90 ~0.65 s, 100 ~1.4 s, 110 ~2.9 s); FX(dist, chorus, delay, reverb) sends */
static const preset_t NEON_PRESETS[] = {
    {"NEON SKY", {0, 70, 100, 40, 72, 15, 15, 40}, {95, 90, 120, 105}, 12, 0, FX(0, 70, 25, 85), PAT(5)},
    {"SUMMERS", {1, 40, 50, 70, 78, 10, 20, 48}, {85, 90, 115, 98}, 8, 0, FX(0, 90, 20, 70), PAT(5)},
    {"DRIVE BS", {2, 8, 45, 5, 38, 35, 45, 64}, {0, 62, 20, 40}, 40, 1, FX(10, 0, 0, 10), PAT(2)},
    {"NIGHT BS", {1, 0, 0, 10, 30, 20, 30, 64}, {0, 70, 70, 35}, 25, 1, FX(0, 10, 0, 5), PAT(8)},
    {"HERO LD", {0, 35, 60, 25, 95, 25, 35, 64}, {10, 80, 100, 70}, 15, 1, FX(5, 30, 60, 45), PAT(4)},
    {"BRASS 85", {0, 30, 70, 15, 50, 15, 30, 64}, {40, 75, 95, 55}, 40, 0, FX(0, 30, 15, 45), PAT(6)},
    {"STAB", {0, 55, 100, 10, 70, 30, 25, 64}, {0, 72, 0, 60}, 30, 0, FX(0, 40, 50, 50), PAT(6)},
    {"ARP PLK", {1, 20, 30, 20, 45, 40, 15, 64}, {0, 68, 0, 55}, 45, 0, FX(0, 40, 65, 40), PAT(13)},
    {"VHS STR", {1, 60, 70, 127, 70, 5, 40, 48}, {90, 90, 118, 100}, 0, 0, FX(5, 100, 15, 75), PAT(5)},
    {"MIDNIGHT", {0, 90, 110, 60, 45, 25, 20, 30}, {105, 100, 120, 110}, 20, 0, FX(0, 60, 30, 100), PAT(5)},
    {"SUB DRV", {2, 0, 64, 0, 28, 0, 60, 64}, {0, 60, 100, 30}, 10, 1, FX(0, 0, 0, 0), PAT(8)},
    {"OUTRUN", {2, 20, 30, 8, 55, 50, 50, 64}, {0, 58, 10, 35}, 50, 1, FX(15, 0, 10, 10), PAT(1)},
    /* Neon Summers' own patches, rebuilt from recordings of them on a TAL U-NO-LX (fitted against the
     * recordings, rendered with this code): "VOLT_PAD", a Juno PWM pad at 1.7 Hz, and "KEY Miami Plucks" at 2 Hz */
    {"VOLT PAD", {3, 0, 23, 65, 39, 0, 67, 103}, {0, 116, 67, 112}, 48, 0, FX(0, 80, 0, 0), PAT(5)},
    {"MIAMI PLK", {3, 0, 36, 38, 48, 61, 70, 125}, {0, 102, 58, 119}, 63, 0, FX(0, 70, 0, 0), PAT(6)},
    /* and "VOLT BASS I" (TAL U-NO-LX): saw and a loud sub, a 24 dB filter snapping shut, Juno chorus II */
    {"VOLT BASS", {2, 0, 82, 0, 25, 5, 40, 66}, {0, 89, 35, 71}, 63, 1, FX(0, 50, 0, 0), PAT(2)},
    /* and "PAD - Analog Pad": pulse, saw and sub, the pulse swept full depth at 0.8 Hz, the filter left alone */
    {"ANALOG PAD", {3, 28, 94, 126, 71, 0, 53, 28}, {0, 46, 95, 100}, 4, 0, FX(0, 70, 0, 20), PAT(5)},
    /* more JUNO: a chorused arp pluck, PWM strings and a swelling synth brass */
    {"JUNO ARP", {3, 30, 80, 40, 40, 40, 70, 64}, {0, 70, 0, 60}, 50, 0, FX(0, 60, 55, 30), PAT(13)},
    {"PWM STRNG", {3, 0, 110, 70, 64, 10, 60, 40}, {70, 90, 115, 95}, 6, 0, FX(0, 85, 0, 60), PAT(5)},
    {"SYNTH BRAS", {3, 20, 120, 20, 38, 20, 64, 64}, {30, 74, 100, 50}, 40, 0, FX(0, 40, 20, 40), PAT(6)},
};

static const engine_t ENG_NEON = {
    .name = "NEON",
    .page_title = {"OSC", "FLT"},
    .edit = {
        {"WAVE", F_ENUM, 0, 3, 0, N_NEON_WAVE, 0},
        {"SPRD", F_PCT, 0, 127, 50, 0, 0},
        {"BLND", F_PCT, 0, 127, 90, 0, 0},
        {"MOVE", F_PCT, 0, 127, 30, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 85, 0, 0},
        {"RES", F_PCT, 0, 127, 20, 0, 0},
        {"WARM", F_PCT, 0, 127, 20, 0, 0},
        {"KTR", F_PCT, 0, 127, 64, 0, 0},
    },
    .presets = NEON_PRESETS,
    .npresets = NELEM(NEON_PRESETS),
    .note_on = neon_note_on,
    .render = neon_render,
    .desc = neon_desc,
    .knob = {P_E4, P_E1, P_E3, P_REL},   /* HOME: CUT, SPRD, MOVE, REL */
    .poly = 6,                           /* ~2-2.5x ANALOG a voice (host estimate): 6 keeps a part near 30 % */
    .keep = 0xF3,                        /* the filters, the outer saws */
};
