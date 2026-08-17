# voice-lab

A real-time acoustic voice-training workbench: pitch, resonance, vocal weight and
gender-perception feedback, built for trans voice training.

Two upstream projects are vendored here as forks, with their full histories preserved:

| Directory | Origin | What it is |
|---|---|---|
| `engine/` | [in-formant/in-formant](https://github.com/in-formant/in-formant) (Apache-2.0) | The C++ real-time analysis engine and desktop app, heavily patched |
| `genderspace/` | [lmcnulty/gender-voice-visualization](https://github.com/lmcnulty/gender-voice-visualization) (AGPL-3.0) | Luna McNulty's acoustic genderspace visualisation, plus live real-time overlays |
| `browser/` | new | The whole toolkit running client-side in a browser |
| `ml/` | new | Neural models: perceived-gender read, phoneme-aware resonance, coaching |
| `docs/` | new | Write-ups, calibration notes, findings |

## What works

**Desktop.** `engine/` builds inside the `informant-build` Fedora distrobox (Bazzite is
immutable and ships no Qt6/FFTW/Pulse headers). Menu entries **InFormant** and **InFormant
(DeepFormants)**; rebuild with `engine/launcher/rebuild.sh [df]`. It publishes live
measurements on `ws://127.0.0.1:8765`.

**Local genderspace.** `genderspace/` is served at <http://127.0.0.1:8180/> by the
`acousticgender-local.service` user unit. Two visualisations:

- `index.html` — Luna's pitch × resonance genderspace, with a live dot fed by the engine,
  on the site's own resonance scale (calibrated against its reference clips)
- `tvl-space.html` — a TransVoiceLessons-style fullness space: vocal weight × vocal size,
  pitch deliberately excluded

**Browser.** `browser/` runs the same analysis client-side (AudioWorklet + WASM), so the
tools work on a phone with no install and no desktop engine running.

## Measurement notes

The numbers are calibrated, not guessed, and the harnesses that calibrated them are in
`engine/tools/genderspace-calib/`:

- **Resonance** is mapped onto acousticgender.space's own scale by soft phoneme assignment
  against its population statistics; validated leave-one-clip-out against all 21 reference
  clips (~0.07 median absolute error on clip medians).
- **Vocal weight** is an SNR-gated Theil-Sen fit of harmonic spectral roll-off, anchored to
  the labelled light/heavy demonstrations in TransVoiceLessons' fullness video.
- **Gender read** blends an F0 logistic with a resonance term (55/45), after Hillenbrand &
  Clark (2009) and Gelfer & Schofield.

Anchors are deliberately not over-tuned; they are single constants, documented where they
live, meant to be adjusted against your own voice and microphone.

## Setup on a fresh machine

```sh
scripts/bootstrap.sh          # fetch large deps (libtorch, ML models)
engine/launcher/rebuild.sh    # build the desktop engine
```

## Licensing

`engine/` is Apache-2.0 (upstream). `genderspace/` is AGPL-3.0 (upstream), so any
network-served deployment of that part must offer its source. New work in `browser/`,
`ml/`, `docs/` follows whichever of the two it derives from; see `docs/LICENSING.md`.
