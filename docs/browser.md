# The browser build

Everything runs client-side: microphone → AudioWorklet tap → analysis Worker →
canvas. No audio and no measurements leave the device, and there is no server
component beyond static file hosting.

## Running it

Locally it is served by the `voicelab-browser.service` user unit at
<http://127.0.0.1:8181/>. To serve it by hand:

```sh
cd browser && python3 -m http.server 8181 --bind 127.0.0.1
```

**On a phone you need HTTPS.** Browsers only grant microphone access in a secure
context, and `localhost` is the sole exemption, so a plain `http://192.168.x.x`
page will silently fail to get the mic. Options, easiest first: put it behind
Tailscale (which gives real certificates via `tailscale serve`), run a reverse
proxy with a self-signed cert and trust it on the phone, or host it somewhere
with TLS. The page detects an insecure context and says so rather than looking
broken.

## Why a reimplementation rather than a WASM port

The earlier assessment argued for compiling the C++ engine to WebAssembly, on
the grounds that a rewrite would reopen weeks of calibration work. That was the
right concern and the wrong conclusion, for one reason: the calibration is
reproducible on demand. `browser/tools/validate-dsp.mjs` re-runs the entire
calibration procedure against the same ground truth, so a fresh implementation
can be *measured* rather than trusted. Given that, a dependency-free JavaScript
implementation avoids an Emscripten toolchain, an FFTW substitution and the
extraction of an analysis core from a Qt-entangled codebase, and it stays
readable and debuggable.

The validation vindicated the caution, though: it immediately caught two real
bugs (see below), either of which would have shipped silently under a
"port it and assume it works" approach.

## What the analysis does

Deliberately the same chain as the desktop engine:

| Quantity | Method |
|---|---|
| Pitch | YIN with parabolic interpolation |
| Formants | 200 Hz pre-emphasis → Gaussian window → resample to 11 kHz → Burg LPC (order 12) → Durand-Kerner roots → radius and bandwidth criteria |
| Vocal weight | Harmonic level in a fixed low band vs a fixed high band, SNR-gated |
| Scores | The calibrated gender model, ported from `genderscore.h` |

Smoothing windows match the desktop HUD (0.35 s median pitch, 1 s formants,
2 s for the gender read) so numbers are directly comparable across the two.

## Calibration is re-derived, not inherited

Calibration constants encode a *particular tracker's* measurement biases, so
they do not transfer between implementations. `tools/validate-dsp.mjs` runs the
browser tracker over acousticgender.space's 21 reference clips at the phoneme
midpoints where the original pipeline recorded praat's formants, fits the
tracker→praat unit maps with the same robust procedure the desktop calibration
used, checks the result leave-one-clip-out against the official per-clip
medians, and derives the vocal-weight anchors from the TransVoiceLessons
light/heavy demonstrations. It writes `src/dsp/calibration.js`.

```sh
node tools/validate-dsp.mjs <refclips-dir> <tvl-demos-dir>
```

Measured on the current implementation:

- Pitch vs praat: **6.5 Hz mean absolute error** (desktop engine: 6.4 Hz)
- Resonance scale vs official clip medians: **0.066 MAE**, leave-one-clip-out
  (desktop engine: 0.073, so slightly better)

## Two bugs the validator caught

Worth recording, because both were invisible to inspection and obvious to
measurement.

**An aliasing bug in the Burg recursion.** The backward prediction error array
was being updated in place while still being read for later indices, so the
recursion fed on its own partially-updated state. LPC coefficients came out
around −300 where they should be order 1, and formant extraction returned
essentially nothing. Fixed by snapshotting both error arrays per iteration.

**An inverted vocal-weight measure — in both engines.** The original approach
collected every harmonic that cleared the noise floor and fitted a slope across
them. A light voice's upper harmonics fall below the floor, so its slope ends
up fitted only across the surviving low harmonics, where the F1 resonance is
*rising*, while a heavy voice is fitted across the whole falling spectrum. The
two are measured over different frequency ranges and are not comparable: on a
synthetic pair with known source tilts, the light half was reported as flatter
(heavier) than the heavy half. This is the same defect that made a light,
breathy voice read as heavy in real use.

Replaced with fixed low (190–900 Hz) and high (1500–3200 Hz) comparison bands,
so every voice is measured on the same ruler, and a voice with nothing in the
high band floors out as *very light* rather than returning no reading. Verified
three ways: clean and noisy synthetic pairs now give identical answers, the
TransVoiceLessons light/heavy demonstrations separate correctly, and the C++
and JavaScript implementations agree to within 0.2 dB/oct on the same signal.
The fix was ported back into the desktop engine.

## Session recording

The **Save** button exports a session summary as JSON: pitch distribution and
time in target, resonance, gender read, weight and size, a histogram of
fullness cells, and a 30-second-bucket timeline for drift. Feed it to the
coach:

```sh
python3 ml/coach/coach.py voice-session-*.json
```

## Verification harness

`test-browser-app.sh` in the scratchpad drives the real page in headless
Chrome with a known WAV injected as a fake microphone
(`--use-file-for-fake-audio-capture`), so the whole client-side chain is
testable against ground truth without anyone speaking into a microphone.
