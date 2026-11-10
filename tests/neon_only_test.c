/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Neon Summers */
/* NEON's own list (core.h NEON_ONLY: what the firmware ships; the other host tests build with every engine listed):
 * the engines offered, the sections of PRESETS > LIST, the power-on sounds, every factory sound loaded and sounded,
 * the presets' extras (preset_t.xp) and their arp going with the next sound.
 *   cc -O1 -w -Ibuild/gen -Ifirmware/src -o build/host/neon_only_test tests/neon_only_test.c -lm */
#define NEON_ONLY 1
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int32_t sound_peak(track_t *t)                /* a C3 held ~190 ms on track t: the loudest sample of the mix */
{
    static int32_t o[CTL * 2];
    int32_t pk = 0;
    uint32_t b, i, mq = song.master_q12;
    song.master_q12 = 4096;
    for (b = 0; b < 256u && (b < 1u || t->xf_on || t->eng_req != t->engine); b++)
        mix_block(o, CTL);                             /* (the engine change, a pending panic, done) */
    trk_note_on(t, 48, 100);
    for (b = 0; b < 260u; b++) {
        memset(o, 0, sizeof o);
        mix_block(o, CTL);
        for (i = 0; i < CTL * 2u; i++) {
            int32_t a = o[i] < 0 ? -o[i] : o[i];
            if (a > pk)
                pk = a;
        }
    }
    trk_note_off(t, 48);
    for (b = 0; b < 40u; b++)
        mix_block(o, CTL);
    song.master_q12 = mq;
    return pk;
}

int main(void)
{
    int bad = 0, ok;
    uint32_t n, e, k, total, cats = 0, silent = 0, loud = 0, xps = 0, sounds = 0;
    static const char *const ORDER[5] = {"JUNO", "THUNDER", "STRINGS", "NEON", "DRUM"};
    static const char *const LIST[8] = {"ALL", "FAV", "BASS", "LEAD", "PAD", "ARP", "KEY", "DRUMS"};
    ui_power_on();
    ok = NENG_SHOWN == 5;
    for (n = 0; n < 5u; n++)
        ok &= strcmp(ENGINES[eng_vis(n)]->name, ORDER[n]) == 0 && eng_ok(eng_vis(n));
    ok &= !eng_ok(0) && !eng_ok(ENGI_FM6) && !eng_ok(3) && !eng_ok(13);
    bad += check("NEON only: the engines offered are JUNO THUNDER STRINGS NEON DRUM; Felucca's are not", ok);
    ok = LIST_N == 8;
    for (n = 0; n < 8u; n++)
        ok &= strcmp(list_name(n), LIST[n]) == 0;
    bad += check("NEON only: PRESETS > LIST: ALL FAV BASS LEAD PAD ARP KEY DRUMS", ok);
    ok = trk[0].eng_req == 14 && trk[1].eng_req == 14 && trk[2].eng_req == 16 && trk[3].eng_req == ENGI_DRUM &&
         strcmp(ENGINES[14]->presets[trk[0].preset].name, "MIAMI 2K15") == 0 &&
         strcmp(ENGINES[14]->presets[trk[1].preset].name, "WE TRUST") == 0 &&
         strcmp(ENGINES[16]->presets[trk[2].preset].name, "DBL IMPACT") == 0 &&
         strcmp(ENGINES[ENGI_DRUM]->presets[trk[3].preset].name, "GATED 85") == 0;
    bad += check("NEON only: power-on: MIAMI 2K15, WE TRUST, DBL IMPACT, GATED 85", ok);
    ok = 1;
    for (n = 0; n < 5u; n++) {                         /* every factory sound: a section of the six, a name that fits */
        e = eng_vis(n);
        for (k = 0; k < ENGINES[e]->npresets; k++) {
            uint32_t c = preset_cat(e, k);
            ok &= c >= CAT_BASS && c <= CAT_DRUM && strlen(ENGINES[e]->presets[k].name) <= 10u;
            if (!(c >= CAT_BASS && c <= CAT_DRUM) || strlen(ENGINES[e]->presets[k].name) > 10u)
                printf("  %s %s: category %u\n", ENGINES[e]->name, ENGINES[e]->presets[k].name, c);
            cats |= 1u << c;
        }
    }
    ok &= cats == 0x7Eu;
    bad += check("NEON only: every factory sound in one of the six sections, each section filled, names of 10 or less", ok);
    for (n = 2; n < 8u; n++) {                         /* every section's list browses only its own */
        list_set(n);
        preset_pos(&total);
        ok &= total > 3u;
        for (k = 0; k < total; k++) {
            uint32_t pk, pe = preset_at(k, &pk);
            ok &= pe < NENGINES && eng_ok(pe) && preset_cat(pe, pk) == n - 1u;
        }
    }
    list_set(0);
    bad += check("NEON only: each section lists its own sounds, all of offered engines", ok);
    song.sel = 0;                                      /* every sound loaded on track 1 and played */
    ok = 1;
    for (n = 0; n < 5u; n++) {
        e = eng_vis(n);
        for (k = 0; k < ENGINES[e]->npresets; k++) {
            const int16_t *q = ENGINES[e]->presets[k].xp;
            int32_t pk, arp = 0;
            set_engine_of(TSEL, e);
            frame();
            apply_preset_to(TSEL, k);
            frame();
            for (; q && *q >= 0; q += 2) {
                ok &= *q < P_E0 && TSEL->p[*q] == clamp(q[1], TP[*q].min, TP[*q].max);
                if (*q == P_AMODE)
                    arp = q[1];
                xps++;
            }
            ok &= TSEL->p[P_AMODE] == arp;             /* (the last sound's arp is gone) */
            if (e == ENGI_DRUM)
                continue;
            TSEL->p[P_AMODE] = 0;                      /* (played as a held note) */
            pk = sound_peak(TSEL);
            TSEL->p[P_AMODE] = (int16_t)arp;
            sounds++;
            if (getenv("NEON_PEAKS")) printf("PK %s %s %d\n", ENGINES[e]->name, ENGINES[e]->presets[k].name, pk);
            if (pk < 300) {
                printf("  %s %s: peak %d (silent)\n", ENGINES[e]->name, ENGINES[e]->presets[k].name, pk);
                silent++;
            }
            if (pk > 32000)
                loud++;
        }
    }
    printf("  %u sounds played, %u extras, %u silent, %u at full scale\n", sounds, xps, silent, loud);
    bad += check("NEON only: every factory sound loads with its extras; an arp preset's arp goes with the next sound", ok);
    bad += check("NEON only: every synth sound sounds on a held C3", sounds > 180u && !silent);
    printf(bad ? "NEON ONLY: %d FAILED\n" : "NEON ONLY: all passed\n", bad);
    return bad != 0;
}
