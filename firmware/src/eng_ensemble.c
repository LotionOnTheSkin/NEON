/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Neon Summers
 * Written for the Felucca engine interface (Felucca-Engines, MIT, Leo Kuroshita / Hugelton Instruments). */
/* ENSEMBLE: a string machine. Each register is three saws whose pitches an ensemble moves as the BBD chorus of the
 * late-70s string machines does: a slow sweep and a faster shimmer, the three 120 degrees apart, so the register
 * swims without beating. Two registers (footages) at a time, a gentle 12 dB/oct low-pass, a high-pass to thin them,
 * and AIR, a breath of filtered noise.
 *
 *   OSC  REG   the footages: 16', 8', 4', 16+8, 8+4, 16+4
 *        ENS   the ensemble's depth (0: three saws in unison)
 *        SPD   its speed (the slow sweep 0.2..1.5 Hz, the shimmer nine times it)
 *        BAL   the balance of the two registers (0 the lower alone .. 127 the upper alone)
 *   FLT  CUT   the low-pass (12 dB/oct, low Q)
 *        HPF   the high-pass after it: 0 off, 127 ~1.5 kHz
 *        AIR   filtered noise with the strings
 *        KTR   the low-pass's key tracking
 *
 * Voice state: ph[0..2] the lower register's saws; s[3..5] the upper's (phases as uint32 in int32); s[2] the
 * ensemble's phase; s[0] s[1] the low-pass; s[6] the high-pass; s[7] the noise. keep: the filters. */

static const char *const N_ENS_REG[] = {"16'", "8'", "4'", "16+8", "8+4", "16+4"};
/* per REG: the lower and the upper register as octaves from the note (-1 16', 0 8', 1 4'); one register: upper = 9 */
static const int8_t ENS_LO[6] = {-1, 0, 1, -1, 0, -1}, ENS_HI[6] = {9, 9, 9, 0, 1, 1};
#define ENS_TRIM 12000

static void ensemble_note_on(track_t *t, voice_t *v)
{
    uint32_t r = 0x9E3779B9u ^ (v->age * 2654435761u);
    (void)t;
    if (!r)
        r = 1;
    v->ph[0] = neon_rnd(&r);
    v->ph[1] = neon_rnd(&r);
    v->ph[2] = neon_rnd(&r);
    v->s[3] = (int32_t)neon_rnd(&r);
    v->s[4] = (int32_t)neon_rnd(&r);
    v->s[5] = (int32_t)neon_rnd(&r);
    v->s[2] = (int32_t)neon_rnd(&r);              /* the ensemble starts anywhere */
    v->s[7] = (int32_t)(r | 1u);
    v->s[0] = v->s[1] = v->s[6] = 0;
}

static inline uint32_t ens_oct(uint32_t inc, int32_t o) { return o >= 0 ? inc << o : inc >> -o; }

static void ensemble_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t reg = (uint32_t)clamp(p[P_E0], 0, 5), i, k;
    int32_t ens = p[P_E1], spd = p[P_E2], bal = p[P_E3], hpf = p[P_E5], air = p[P_E6];
    uint32_t eph = (uint32_t)v->s[2];
    uint32_t inc[6];
    uint32_t ph[6] = {v->ph[0], v->ph[1], v->ph[2], (uint32_t)v->s[3], (uint32_t)v->s[4], (uint32_t)v->s[5]};
    int two = ENS_HI[reg] != 9;
    int32_t glo = two ? (127 - bal) * 258 : 32767, ghi = two ? bal * 258 : 0;   /* Q15 */
    int32_t cut = (p[P_E4] << 8) + m->cutoff + (p[P_E7] * (v->pitch16 - 60 * 16) >> 4);
    int32_t lp1 = v->s[0], lp2 = v->s[1], hs = v->s[6], ha = 0, sn = air * 160;
    tsvf_t f;
    tsvf_coef_k(&f, cut, 6400);                                   /* Q ~0.64: soft, no peak */
    if (hpf) {
        int32_t g = SVF_G[hpf * 79 / 127];
        ha = (int32_t)(((uint32_t)g << 15) / (uint32_t)(4096 + g));
    }
    /* the ensemble, at the block's start: three taps 120 degrees apart, slow sweep + shimmer (9x), up to ~+-22 ct */
    for (k = 0; k < 3u; k++) {
        uint32_t o = k * 0x55555555u;
        int32_t sl = osc_sine(eph + o), sh = osc_sine(eph * 9u + o);
        int32_t d = (((sl * 3 + sh) >> 2) * ens) >> 15;           /* -127..127 */
        uint32_t base = ens_oct(m->inc, ENS_LO[reg]);
        inc[k] = neon_det(base, (d * 52) >> 7 >> 0);              /* up to ~52 / 2.367 = 22 ct */
        if (two)
            inc[3 + k] = neon_det(ens_oct(m->inc, ENS_HI[reg]), -((d * 52) >> 7));
    }
    for (i = 0; i < n; i++) {
        int32_t lo = (osc_saw(ph[0], inc[0]) + osc_saw(ph[1], inc[1]) + osc_saw(ph[2], inc[2])) / 3;
        int32_t s = (glo * lo) >> 15;
        ph[0] += inc[0];
        ph[1] += inc[1];
        ph[2] += inc[2];
        if (two) {
            int32_t hi = (osc_saw(ph[3], inc[3]) + osc_saw(ph[4], inc[4]) + osc_saw(ph[5], inc[5])) / 3;
            s += (ghi * hi) >> 15;
            ph[3] += inc[3];
            ph[4] += inc[4];
            ph[5] += inc[5];
        }
        if (sn)
            s += (sn * ((int32_t)noise32(&v->s[7]) >> 16)) >> 15;
        {
            int32_t y = tsvf_lp(&f, s >> 1, &lp1, &lp2);
            if (ha) {
                int32_t d = ((y - hs) * ha) >> 15, l = hs + d;
                hs = l + d;
                y -= l;
            }
            s = (soft_knee(y, 16000) * ENS_TRIM) >> 13;
        }
        out[i] += voice_amp(s, m, i) * 2;
    }
    v->ph[0] = ph[0];
    v->ph[1] = ph[1];
    v->ph[2] = ph[2];
    v->s[3] = (int32_t)ph[3];
    v->s[4] = (int32_t)ph[4];
    v->s[5] = (int32_t)ph[5];
    v->s[0] = lp1;
    v->s[1] = lp2;
    v->s[6] = hs;
    v->s[2] = (int32_t)(eph + NEON_LFO_BASE / 3u + (NEON_LFO_BASE / 3u >> 6) * (uint32_t)spd);   /* ~0.2..1.5 Hz */
}

/* e = REG ENS SPD BAL CUT HPF AIR KTR */
static const preset_t ENSEMBLE_PRESETS[] = {
    {"SOLINA", {4, 90, 50, 45, 95, 20, 0, 64}, {60, 80, 110, 95}, 0, 0, FX(0, 0, 0, 40)},
    {"VHS STRNGS", {1, 110, 38, 0, 78, 30, 20, 50}, {75, 90, 112, 100}, 0, 0, FX(0, 30, 20, 45)},
    {"HI STRINGS", {2, 80, 60, 0, 104, 48, 0, 70}, {50, 80, 105, 90}, 0, 0, FX(0, 0, 0, 50)},
    {"DARK ENS", {3, 100, 30, 40, 62, 0, 0, 40}, {85, 95, 118, 105}, 8, 0, FX(0, 0, 0, 40)},
    {"OCTAVES", {5, 70, 55, 60, 90, 10, 0, 64}, {30, 85, 108, 85}, 0, 0, FX(0, 0, 25, 35)},
    {"AIR CHOIR", {4, 60, 45, 70, 70, 40, 60, 80}, {70, 90, 110, 100}, 0, 0, FX(0, 40, 0, 55)},
    {"STAB ENS", {4, 85, 70, 50, 100, 25, 0, 64}, {0, 78, 0, 70}, 0, 0, FX(0, 0, 30, 30)},
    {"SLOW DAWN", {3, 120, 20, 55, 72, 12, 10, 50}, {110, 100, 120, 112}, 12, 0, FX(0, 20, 0, 60)},
};

static const engine_t ENG_ENSEMBLE = {
    .name = "STRINGS",
    .page_title = {"OSC", "FLT"},
    .edit = {
        {"REG", F_ENUM, 0, 5, 4, N_ENS_REG, 0},
        {"ENS", F_PCT, 0, 127, 90, 0, 0},
        {"SPD", F_PCT, 0, 127, 50, 0, 0},
        {"BAL", F_PCT, 0, 127, 45, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 95, 0, 0},
        {"HPF", F_PCT, 0, 127, 20, 0, 0},
        {"AIR", F_PCT, 0, 127, 0, 0, 0},
        {"KTR", F_PCT, 0, 127, 64, 0, 0},
    },
    .presets = ENSEMBLE_PRESETS,
    .npresets = NELEM(ENSEMBLE_PRESETS),
    .note_on = ensemble_note_on,
    .render = ensemble_render,
    .knob = {P_E4, P_E1, P_E3, P_REL},   /* HOME: CUT, ENS, BAL, REL */
    .poly = 5,
    .keep = 0x43,                        /* the filters (bits 0, 1, 6) */
};
