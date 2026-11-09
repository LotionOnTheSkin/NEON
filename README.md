# NEON — Felucca with a synthwave engine

This fork of [Felucca](https://github.com/hugelton/Felucca) adds **NEON**, a synthwave sound engine for the M-VAVE FM-1:
lush supersaw pads, Juno-style PWM strings and deep pulsing bass, through a 24 dB/oct filter with warmth and slow
analog drift (MOVE). Everything else is Felucca as it is upstream.

- **Download:** [Releases › neon-latest › `neon.fwsc`](../../releases/tag/neon-latest), built by GitHub Actions on every push
  (`.github/workflows/build-neon.yml`).
- **Try it in the browser:** open the [FM-1 emulator](https://joakimeriksson.github.io/fm1-emu), load `neon.fwsc`, press Start.
- **Play it:** NEON is the last engine. On the PRESETS page, turn the ENG knob to NEON. Its presets: NEON SKY,
  SUMMERS, DRIVE BS, NIGHT BS, HERO LD, BRASS 85, STAB, ARP PLK, VHS STR, MIDNIGHT, SUB DRV, OUTRUN, then Neon Summers'
  own patches rebuilt from recordings (VOLT PAD, MIAMI PLK, VOLT BASS, ANALOG PAD; and DBL IMPACT on ANALOG), then
  JUNO ARP, PWM STRNG and SYNTH BRAS.
- **Synthwave presets everywhere:** every engine's factory presets are replaced with synthwave sounds (ANALOG's
  MIAMI LEAD and VAPOR PAD, PHASE's CZ sounds, LOFI's chip leads, TRIO's MEGA BASS, WHEEL's CATHEDRAL, NOISE's
  UPLIFTER riser, VOICE's VOX PAD, GRAIN's DREAM CLOUD, PHYS's STAR BELLS and SIMMONS, the 80s FM sounds on FM6,
  DRUM's GATED 85 / 808 NIGHT / DRIVE KIT / NEON KIT / PERC 84; DRUM KIT stays first for older projects).
  Felucca-Engines is vendored in `firmware/engines` (MIT, its LICENSE kept) so its presets can change.
- **The look:** Sunset Drive. A sun-and-grid boot screen, the SUNSET palette (deep indigo, magenta, cyan) as the default
  (MENU > COLOR still offers Felucca's palettes), and Chakra Petch type throughout.
- **SUNSET DRIVE:** tap HOME from HOME to step HOME > MIXER > CLOCK > **DRIVE**: a visualizer where the grid rolls
  toward you one line a beat (it follows the tempo and stops when you stop), the sun swells with the loudest track,
  the horizon glows with the bass, and a light per track shines in the sky.
- **HARMONY: one progression drives the song.** SCL > CHORD > **PROG**: pick a MOOD (DRIVE i-VI-III-VII, DAWN,
  NIGHT, MYST, HEART, RISE) and every melodic track follows it (QNT **PROG**): its steps, keys and arp are written in C
  and move with each chord, by scale steps, the shorter way, so a bass, a pad and an arp written once play the whole
  progression. ROOT is the key. SLOT / CHORD edit the eight chords (OWN), BARS sets 1, 2 or 4 bars a chord; stopped,
  the keys play SLOT's chord. A MOOD on a new song drops starter phrases into the empty tracks (a 16th bass, a held
  triad, a 16th arp, a beat): pick a mood, press PLAY. The progression is saved with the project, and in a SONG
  (GLO) each section plays the progression of its slot, from its first chord.
- **Long patterns:** up to 64 steps a track (hold SEQ: LEN; DOUBLE / HALF). On STEP, the PRESETS knob turns a page
  of 16 a detent when the pattern is longer (KNOB 1 a step), and pills over the piano roll show the page.
- **PUMP:** the SLICER's fourth mode: the track ducks on every beat (DEPTH) and swells back (PAT: how slowly): the
  sidechain pump, in time at any tempo.
- **Stereo chorus:** the chorus bus is a Juno's now: two taps swept against each other, one left, one right.
- **Phrases:** SAVE > PHRASES adds DRIVE BS, PULSE 8, PAD BAR, STABS, ARP 16, LEAD 80, DRIVE BT (PAD BAR and STABS
  load triads in the track's scale).
- **The engine:** `firmware/src/eng_neon.c` (MIT). WAVE (SUPER / PWM / SUB / JUNO), SPRD, BLND, MOVE, CUT, RES, WARM,
  KTR. JUNO is one Juno-60 DCO: pulse with LFO PWM, saw, a locked sub; its knobs read SUB, SAW, PWM and RATE.
- **Status:** experimental. It passes Felucca's host test suite; it has not yet been played on a real FM-1. Its CPU
  cost was estimated on a computer (about 2 to 2.5 ANALOG voices a voice), so it is capped at 6 voices.

Engine numbers: NEON is engine 15; 14 stays reserved for Felucca's ADPCM, so projects move between this firmware and
upstream Felucca without a mismatch. A project or user preset with a NEON sound does not load on upstream Felucca.

---

# Felucca

[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)

Felucca is open-source custom synthesizer firmware for the **M-VAVE FM-1**.

The FM-1 is the only supported hardware target. This project does not provide, maintain, or accept requests for ports to other devices or software platforms.

## Install

**Stable release: 1.8.1.** Connect your FM-1 by USB and use the [web installer](https://hugelton.github.io/Felucca/) in Chrome or Edge. The installer offers a way back to the official V15 firmware. Back up your projects and presets before changing firmware.

**Newer FM-1 flash: 1.8.2 beta.** Some newer FM-1 units use UC25HQ80 flash (JEDEC ID `0xB36014`). [1.8.2-beta](https://github.com/hugelton/Felucca/releases/tag/v1.8.2-beta) adds recognition for that chip; testing on the revised hardware is still pending. See [issue #320](https://github.com/hugelton/Felucca/issues/320). It is not the stable release.

## Project resources

- [Web installer](https://hugelton.github.io/Felucca/)
- [Web editor](https://hugelton.github.io/Felucca/webapp/editor/) ([source](https://github.com/hugelton/Felucca-WebApp))
- [Manual and release notes](https://github.com/hugelton/Felucca/wiki/Home)
- [Building from source](BUILDING.md)
- [Issue tracker](https://github.com/hugelton/Felucca/issues)

The firmware uses [Felucca-Engines](https://github.com/hugelton/Felucca-Engines) (MIT) for its built-in sound engines and [felucca-bsp](https://github.com/hugelton/felucca-bsp) (MPL-2.0) for the FM-1 hardware layer. Neither repository implies support for Felucca on other hardware.

## Development and support

Felucca focuses on the FM-1. Third-party modifications and ports are independent projects and are not tested or supported here. Contributions are considered at the maintainer's discretion; no development schedule or ongoing support is promised.

Felucca is developed with the assistance of AI coding agents (code, tests, documentation, translation, maintenance). Design, features, sounds and direction are the maintainer's or the community's; no generative AI is used for music, icons or artwork. See [On AI-Assisted Development and Responsibility](https://github.com/hugelton/Felucca/discussions/166).

## Credits and licence

Felucca is free software under [GPL-3.0-only](LICENSE); the Hügelton Sample Pack is CC0 1.0. The engines and hardware layer retain their own licences. See [CREDITS.md](CREDITS.md) and [LICENSING.md](LICENSING.md).

M-VAVE and FM-1 are trademarks of their respective owners. Felucca is not affiliated with, endorsed by or supported by M-VAVE.

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
