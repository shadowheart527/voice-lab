# DeepFilterNet evaluation

Evaluated 17 Aug 2026 against the question that actually matters for this
toolkit: **can a neural speech denoiser sit in the capture path without
corrupting the measurements?**

The engine already answered a version of this for RNNoise and the answer was no.
From `engine/src/modules/app/pipeline/pipeline.cpp`:

> RNNoise is used for its voice-activity detection ONLY; the analyses always run
> on the raw signal. Feeding the trackers RNNoise's denoised output turned out to
> be a trap: a steady held tone is exactly what a speech denoiser treats as
> stationary background, so sustained-note drills (and low drones) were
> progressively eaten -- a 110 Hz test tone came out tracking at 202 Hz.

DeepFilterNet3 is a much better model than RNNoise. It is also, for exactly that
reason, **better at recognising a held vowel as noise**. The numbers below are
worse than RNNoise's, not better.

**Bottom line:** safe for cleaning up recordings you listen back to. Not safe,
at default settings, anywhere upstream of the analysis. There is a safe operating
point (`atten_lim_db` 12-20) and it is not the default.

---

## 1. Install

```sh
uv venv --python 3.11 /var/home/shadowheart527/git/voice-lab/ml/.venv-audio
VIRTUAL_ENV=.../ml/.venv-audio uv pip install --torch-backend=cpu torch torchaudio
VIRTUAL_ENV=.../ml/.venv-audio uv pip install deepfilternet
```

Everything is confined to the venv. Nothing was layered with `rpm-ostree`, no
system audio config, default source or PipeWire setting was touched.

Two install snags, both real and both worth knowing before adopting this:

- **Python 3.11 is required.** `deepfilterlib` 0.5.6 publishes wheels only for
  cp38-cp311. On Bazzite's Python 3.14 (and on 3.12) uv falls back to building
  the Rust extension from source, which fails with `Cargo metadata failed. Do you
  have cargo in your PATH?` — there is no Rust toolchain on the host or in the
  `informant-build` distrobox. Pinning the venv to 3.11 avoids needing one.
- **DeepFilterNet 0.5.6 does not import under current torchaudio.** `df/io.py`
  does `from torchaudio.backend.common import AudioMetaData`; torchaudio removed
  the entire `torchaudio.backend` package and `torchaudio.info` in 2.x, and we
  have 2.11. Worked around with `df_compat.py` in this directory, which registers
  a stub module before `df` is imported. Only DeepFilterNet's own file loader
  needs it — the inference path is untouched, and we feed the model tensors read
  with soundfile. Upstream's last release is from the 0.5.x line and this is not
  fixed there.

Installed: `deepfilternet` 0.5.6, `deepfilterlib` 0.5.6, `torch` 2.13.0+cpu,
`torchaudio` 2.11.0+cpu. Model **DeepFilterNet3**, 2,135,484 parameters,
48 kHz only, FFT 960 / hop 480 (20 ms window, 10 ms hop).

Offline enhancement works (`run_df.py IN.wav OUT.wav [atten_lim_db]`).
Throughput on this box, batch, CPU, unloaded: **RTF 0.015-0.022, i.e. 45-65x
realtime**. Under load it degraded to 1-7x, so treat the headroom as real but
not unlimited. Upstream quotes a minimum algorithmic latency of 20 ms for the
LADSPA plugin (STFT), plus host buffering.

---

## 2. Sustained tones — the critical test

Two signals, both 48 kHz:

- `steady.wav` — 50 s of a perfectly constant 200 Hz `/a/`. No modulation at all.
  The worst case by construction: maximally "stationary background".
- `human.wav` — 30 s at 200 Hz with 4.5 Hz vibrato (±25 cents), a slow seeded
  drift, ±1.5 dB shimmer at 3.1 Hz, and a −30 dB breath-noise floor. This is what
  a *real* held-vowel drill looks like.

Harmonic levels are peak magnitudes from one long Hann FFT over a stationary
10 s excerpt. Tilt is measured with a line-for-line port of the engine's own
`Pitch::computeSpectralTilt` (Theil-Sen over harmonics 1-14, 3.2× SNR gate), fed
the *original* signal's f0 in both cases so the comparison isolates the spectral
change.

### steady.wav — obliterated

| Measure | Original | DeepFilterNet | Δ |
|---|---|---|---|
| RMS, whole file | −11.69 dBFS | −36.77 dBFS | **−25.08 dB** |
| RMS, steady-state 1 s blocks | — | — | **−56.42 dB** (min −58.21, max −56.27) |
| H1 (200 Hz) | 80.65 dB | 23.88 dB | −56.77 |
| H4 (800 Hz, F1 peak) | 85.04 dB | 6.02 dB | −79.02 |
| H1–H14 mean | — | — | **−61.54 dB** (range −41.60 to −79.18) |
| inter-harmonic floor | −113.31 dB | −74.41 dB | **+38.89** |
| engine tilt | −12.64 dB/oct | −18.50 dB/oct | −5.86 |
| ACF f0 | 200.15 Hz | **100.00 Hz** | octave error |

615 of 625 frames come back off by more than 25 % of f0. The harmonic structure
does not survive in any meaningful sense: the tone is 56 dB down, the model's own
artifact floor has come *up* by 39 dB, and the pitch estimate has fallen an
octave. This is precisely the RNNoise failure the engine already documented, one
octave in the other direction.

### human.wav — badly damaged, and it is the realistic case

| Measure | Original | DeepFilterNet | Δ |
|---|---|---|---|
| RMS, whole file | −13.43 dBFS | −28.40 dBFS | −14.97 dB |
| RMS, steady-state 1 s blocks | — | — | **−27.96 dB** |
| H1 (200 Hz) | 74.04 dB | 51.04 dB | −23.00 |
| H4 (800 Hz) | 70.01 dB | 26.16 dB | −43.85 |
| H1–H14 mean | — | — | **−32.10 dB** (range −23.00 to −43.85) |
| engine tilt | −11.52 dB/oct | −11.19 dB/oct | +0.33 |
| ACF f0 median | 200.08 Hz | 199.83 Hz | — |

Vibrato and shimmer buy the note about a second of life, and they do keep the
pitch trackable (27/375 bad frames, not 615/625). But the note still loses 28 dB,
and the loss is strongly frequency-dependent — 21 dB of spread between the
least- and most-suppressed harmonic, with the *loudest* component (H4, on the F1
peak) hit hardest. The tilt happens to come out nearly unchanged here, which is
luck, not safety: the estimator is fitting a line through harmonics that have
each moved by a different large amount.

### How long a held note survives

100 ms resolution, attenuation vs time from the start of the note:

| | −6 dB | −12 dB | −20 dB | −30 dB |
|---|---|---|---|---|
| `steady.wav` | 0.3 s | 0.5 s | 0.6 s | 0.7 s |
| `human.wav` | 1.0 s | 1.1 s | 1.1 s | 1.2 s |

**A sustained vowel gets roughly one second before DeepFilterNet decides it is
noise, and it never recovers** — `human.wav` sits 25-50 dB down for the remaining
29 seconds. Sustained-phonation drills are the one exercise this would silently
destroy, and they are a core part of voice training.

### Control: is the harness right?

If DeepFilterNet mangled real voice too, the above would just mean the test was
broken. It does not. On the ten TransVoiceLessons demonstration clips already
used to anchor the toolkit's weight scale — real human voice, real room:

| | mean | worst |
|---|---|---|
| RMS change | **−0.43 dB** | −0.82 dB |
| voiced-frame RMS change | −0.43 dB | −0.81 dB |
| tilt change | **−0.36 dB/oct** | −1.54 dB/oct |

On connected speech DeepFilterNet is close to transparent. The destruction is
specific to sustained phonation, and it is a property of the model's prior, not
of this measurement.

---

## 3. Noise removal vs spectral tilt

The toolkit's vocal-weight read is the harmonic spectral roll-off in dB/octave,
mapped by `weightP()` in `engine/src/context/genderscore.h`:

```c++
return std::clamp((tiltDbOct + 10.5) / 8.0, 0.0, 1.0);
```

So the whole 0..1 vocal-weight scale spans 8 dB/oct: **1 dB/oct of tilt error =
0.125 of the entire scale.** That is the yardstick for everything below.

### 3a. `weightpair-noisy.wav` as supplied

24 s of sustained synthetic voice at f0 = 160 Hz: 12 s heavy, then 12 s light,
over a constant noise floor (identical band levels in both halves).
`weightpair.wav` is the same pair without noise, so it is ground truth.

Noise removal itself is excellent — the 5-8 kHz / 8-16 kHz floor goes from
+11.3 dB to −30.6 dB, about **42 dB of noise gone**. But:

| Half | Clean (truth) | Noisy | Denoised | Noisy err | **Denoised err** |
|---|---|---|---|---|---|
| heavy (0.5-11.5 s) | −11.10 dB/oct | −11.49 | −11.03 | −0.39 | **+0.06** |
| light (12.5-23.5 s) | −21.23 dB/oct | −15.65 | −9.06 | +5.58 | **+12.17** |

Level tells the story: the heavy half comes out 15.46 dB below clean, the light
half **38.58 dB** below clean. The light half is quieter and steeper, so more of
it looks like background, so more of it gets removed — and the tilt error more
than doubles versus simply leaving the noise in. Usable harmonics per frame drop
from 13.6 to 4.7.

Caveat: this signal is sustained phonation, so §2 is already fatal to it and this
experiment cannot separate "denoiser moved the tilt" from "denoiser ate the
tone". Hence 3b.

### 3b. Real connected speech + a known noise floor

The realistic capture-cleanup case: the ten real TVL clips, plus synthetic pink
noise at known SNR, denoised, tilt compared against the clean original. Connected
speech is *not* something DeepFilterNet suppresses, so this isolates the tilt
question cleanly.

Tilt error vs the clean original, dB/octave, mean ± sd over 10 clips:

| SNR | No denoising | DeepFilterNet | worst DF | DF error in weightP units |
|---|---|---|---|---|
| 20 dB | **+1.00** ± 0.57 | **−1.27** ± 0.41 | 2.06 | 0.159 |
| 10 dB | **+2.65** ± 1.03 | **−2.70** ± 0.84 | 3.75 | **0.337** |
| 5 dB | **+3.65** ± 1.22 | **−2.83** ± 1.14 | 4.11 | **0.357** |

Two things here, and the second is the serious one.

**The sign flips.** Noise flattens the measured roll-off (upper harmonics drown,
the SNR gate keeps noise-floor "harmonics"); DeepFilterNet over-steepens it
(upper harmonics get shaved). Denoising does not fix the bias, it substitutes an
opposite bias of comparable size. At 10 dB SNR you trade +2.65 for −2.70 dB/oct.

**It is a gain change, not an offset.** The engine's docs note the uncorrected
tilt runs a consistent **+7 dB/oct offset (sd 1.5)** versus the true corrected
value, and that "the offset is absorbed by the `weightP` anchors". A constant
offset is harmless. This is not one — DeepFilterNet steepens light voices far
more than heavy ones. At 10 dB SNR:

| clip | clean | noisy | DeepFilterNet | DF error |
|---|---|---|---|---|
| `heavy` | −4.77 | −3.53 | −5.39 | **−0.62** |
| `light` | −9.90 | −6.16 | −13.65 | **−3.75** |

The true light-to-heavy spread is 5.13 dB/oct; after denoising it reads 8.26
dB/oct. The scale is stretched by 1.6×, and stretched by an amount that depends
on the SNR of the recording. **No single anchor constant can absorb that.** A
user whose room noise changed would see their vocal weight move without their
voice moving, which is the one failure mode a training tool must not have.

---

## 4. The mitigation that works: `atten_lim_db`

`enhance()` implements the attenuation limit as

```python
lim = 10 ** (-abs(atten_lim_db) / 20)
enhanced = original_spec * lim + enhanced * (1 - lim)
```

so wherever the model suppresses completely, the output is **the original,
scaled by `lim`** — spectrally identical, just quieter. The cap converts
annihilation into attenuation. That is exactly what the measurements show.

### Sustained notes survive

| signal | `atten_lim_db` | steady-state RMS Δ | tilt Δ |
|---|---|---|---|
| steady | None | −56.39 dB | −5.79 dB/oct |
| steady | 20 | −19.98 dB | **+0.03** |
| steady | **12** | −11.99 dB | **+0.01** |
| steady | 6 | −6.00 dB | +0.00 |
| human | None | −27.76 dB | +0.34 |
| human | 20 | −18.26 dB | +0.24 |
| human | **12** | −11.46 dB | **+0.04** |
| human | 6 | −5.82 dB | +0.02 |

At a 12 dB cap the held tone loses exactly 12.0 dB of level and **0.01 dB/oct of
tilt**. The drill is intact; it is merely quieter.

### And the tilt gets *better* than doing nothing

Real speech at 10 dB SNR, tilt error vs clean, plus how much noise is actually
removed (6-12 kHz band):

| `atten_lim_db` | tilt err | sd | worst | weightP err | noise removed |
|---|---|---|---|---|---|
| no denoising | +2.65 | 1.03 | — | 0.331 | 0 dB |
| None (default) | −2.70 | 0.84 | 3.75 | 0.337 | −15.3 dB |
| 20 | −0.81 | 0.40 | 1.42 | 0.102 | −10.6 dB |
| **12** | **+0.39** | **0.66** | **1.23** | **0.080** | −7.4 dB |
| 6 | +1.44 | 0.99 | 2.70 | 0.197 | −4.2 dB |
| 3 | +1.99 | 0.91 | 3.16 | 0.249 | −2.2 dB |

**`atten_lim_db` = 12 is the sweet spot**: the lowest tilt error of any option
including no denoising at all (0.08 vs 0.33 of the weight scale), and it still
removes 7.4 dB of noise. 20 dB is a reasonable alternative if noise reduction
matters more than the last bit of accuracy.

### This matters for the LADSPA plugin specifically

The PipeWire LADSPA plugin exposes exactly one control, **"Attenuation Limit
(dB)", and its default is 100 — i.e. no limit**, the setting measured above as
destroying sustained tones. Upstream's own README suggests 6-12 dB for "little
noise reduction" and 18-24 dB for medium, which happens to bracket the safe
range found here.

Installing it as a system-wide virtual source would silently route capped-at-100
audio into every capture, including the engine's — with no indication in the
toolkit that the signal had been altered. Do not do that on this machine.

---

## 5. Recommendation

**For cleaning up recordings a human listens back to: yes, use it.** On real
connected speech it is near transparent (−0.43 dB RMS, −0.36 dB/oct tilt) and it
removes ~42 dB of noise floor. It is fast (45-65× realtime on CPU), MIT/Apache
dual-licensed, and offline use costs nothing but a venv.

**As an input to the analysis path at default settings: no.** Two independent
disqualifiers:

1. Sustained phonation drills are destroyed after ~1 s — 56 dB down on a pure
   held tone with an octave pitch error, 28 dB down with 21 dB of harmonic
   reshaping on a realistic vibrato vowel.
2. Vocal weight is biased by up to 0.34-0.36 of its 0..1 range at 10-5 dB SNR,
   in the opposite direction from the noise it is correcting, and as an
   SNR-dependent *stretch* of the scale rather than an offset the anchors could
   absorb.

**If it is used in the capture path anyway, cap it at `atten_lim_db` 12 (or 20).**
That is measurably better than both the default and no denoising at all, and it
leaves held notes structurally intact.

**The existing architecture is still the right one.** The engine's current design
— run a denoiser for voice-activity detection only, always analyse the raw signal
— is unaffected by any of this and remains correct. DeepFilterNet3 has better VAD
material than RNNoise, so swapping it in *for gating only* is defensible; it does
not justify letting denoised audio reach the trackers. Nothing here argues for
changing what gets analysed.

---

## Files

| File | What it does |
|---|---|
| `df_compat.py` | torchaudio ≥ 2.2 import shim; import before `df` |
| `run_df.py` | offline enhancement + timing; `denoise()` helper |
| `eval_sustained.py` | §2 — RMS, harmonic levels, tilt, f0 on held tones |
| `eval_onset.py` | §2 — 100 ms suppression time course |
| `eval_control.py` | §2 — real-speech control on the TVL clips |
| `eval_weight.py` | §3 — weightpair + real speech in noise |
| `eval_attenlim.py` | §4 — `atten_lim_db` sweep |
| `../audiometrics.py` | shared metrics; port of the engine's `computeSpectralTilt` |

Outputs land in `out/`. Test signals come from the session scratchpad
(`steady.wav`, `human.wav`, `weightpair{,-noisy}.wav`, `morph.wav`, `tvldemos/`).
