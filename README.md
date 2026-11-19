# NEON: a synthwave groovebox for the M-VAVE FM-1

NEON turns the FM-1 into an all-in-one synthwave song maker: Juno pads, supersaw leads, pulsing basses, 80s drum
machines and a sequencer that knows what chord you are on. It is a fork of
[Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita, with new sound engines, about 220 synthwave
presets, songwriting tools and a sunset-and-grid look.

By **Neon Summers**. Free and open source (GPL-3.0).

## Install

1. Download **`neon.fwsc`** from the [latest release](../../releases/latest).
2. Install it with the [FM-1 Firmware Hub](https://thefirmwarehub.com/) or any FM-1 installer that accepts a
   `.fwsc` file.
3. Back up your projects and presets before changing firmware. M-VAVE's own updater takes you back to the official
   firmware.

**Try it first in your browser:** open the [FM-1 emulator](https://joakimeriksson.github.io/fm1-emu), load
`neon.fwsc` and press Start.

## Sounds

The preset list is organised by what a sound does: **BASS, LEAD, PAD, ARP, KEY, DRUMS**, each tagged in its own
colour. Turn the PRESETS knob to browse; KNOB 4 picks the section.

- **JUNO:** a Juno-106-style voice: saw, pulse with LFO, manual or envelope PWM, sub, noise, a 24 dB filter and a
  high-pass. 67 presets converted from TAL U-NO-LX patches.
- **THUNDER:** two stacked unison oscillators with drive and a 12 or 24 dB filter, for big leads, brass and basses.
  113 presets converted from Sylenth1 patches.
- **STRINGS:** a string machine with a swirling ensemble chorus.
- **NEON:** supersaw, PWM, sub and Juno modes with slow analog drift.
- **DRUMS:** ten 80s kits, including LINN, DMX, BIG GATE (a gated-reverb snare) and HEX PADS.

The converted presets are close relatives of the originals, not exact copies. A preset can also set its track's LFO,
glide, key tracking, octave and arpeggiator, so an ARP sound plays as an arp straight away.

## Songwriting

- **HARMONY:** pick a MOOD (SCL > CHORD > PROG) and one chord progression drives every melodic track. Write a bass,
  pad or arp once and it follows the chords. A new song gets starter parts: pick a mood, press PLAY.
- **WRITE:** hold SEQ and press F5 to write a one-bar part for the track's sound (bass line, pad chords, arp, lead or
  beat). Press again for the next style.
- **VARY:** hold SEQ and press G5 to turn one bar into a 4-bar phrase that answers itself and resolves (a drum track
  gets a tom fill).
- **Voice leading:** chords move to the nearest notes of the next chord instead of jumping as a block.
- **Long patterns:** up to 64 steps per track.
- **PUMP:** a tempo-synced sidechain duck on any track (FX > SLICER).
- **Stereo Juno chorus** on the chorus bus.

## Look

- **Sunset Drive:** sun-and-grid boot screen and the SUNSET palette (other palettes stay in MENU > COLOR).
- **Role colours:** bass cyan, pad violet, lead pink, arp amber, drums yellow, used on the tracks, mixer, song and
  piano roll.
- **Mixer:** each strip shows its section, its sound, a gold-to-magenta level fader with an output meter, pan and
  reverb.
- **Piano roll:** the current chord's notes are lit and named.
- **DRIVE visualizer:** tap HOME from HOME to reach it. The grid rolls with the tempo and the sun swells with the
  music.

## Saving a song

The FM-1 has four project slots, A to D. Build a pattern, then SAVE > PROJECT: KNOB 1 picks the slot, turn KNOB 4 to
SAVE and press OCT+. Save each pattern to its own slot, then arrange them on the SONG page (GLO) and save once more
into the slot that is loaded: the song is stored with it.

## Notes

- Felucca's own engines (ANALOG, FM6, SAMPLE and others) are not listed in NEON, but they stay in the firmware, so
  projects made on Felucca still play.
- Projects and user presets that use NEON's engines do not load on Felucca.
- Tested on a real FM-1. It is a young project: back up anything you care about.
- Package identity: `FM-1_1984`.
- Developed with the help of AI coding tools.

## Credits

Built on [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita (Hügelton Instruments), GPL-3.0, with
[Felucca-Engines](https://github.com/hugelton/Felucca-Engines) (MIT) and
[felucca-bsp](https://github.com/hugelton/felucca-bsp) (MPL-2.0). NEON's engines are MIT. Full details: [CREDITS.md](CREDITS.md), [LICENSING.md](LICENSING.md).

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
