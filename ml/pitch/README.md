# PESTO neural pitch evaluation

Evaluated 17 Aug 2026 against exact synthetic ground truth, alongside the
classical trackers the engine already ships (`engine/src/analysis/pitch/`).

**Bottom line: not worth adopting as the primary tracker.** On clean, decently
recorded audio PESTO is **6-10× less accurate** than the engine's MPM (8.2-8.6
cents median error vs 0.8-1.3) and, more damagingly, has **8-18× the frame-to-frame
jitter** — the exact "visibly bouncing dots" problem the engine has already been
tuned to avoid. Its real strength is robustness, and it is genuinely impressive
there; but the one robustness gap that matters in practice turned out to be a
one-line bug in the engine's MPM, not a reason to add a 2 MB neural model and
85 ms of lookahead to the real-time path.

The side finding in §5 is arguably worth more than the PESTO verdict itself.

---

## 1. Install

```sh
VIRTUAL_ENV=.../ml/.venv-audio uv pip install pesto-pitch
```

Installed cleanly: `pesto-pitch` 2.0.1 (+ `omegaconf`, `antlr4-python3-runtime`)
into the same Python 3.11 venv as DeepFilterNet. No shims needed, unlike
DeepFilterNet. Same venv, no system changes.

One caveat: **only the `mir-1k_g7` checkpoint loads.** The bundled legacy
`mir-1k` checkpoint raises a `state_dict` mismatch against the 2.x architecture
(`Missing key(s) ... preprocessor.hcqt_kernels.cqt_kernels.0.conv.weight,
confidence.conv.weight ...`) and then dies with a shape error in the confidence
head. Everything below uses `mir-1k_g7`.

What the model actually is:

- A **VQT/CQT front end** implemented as a single `Conv1d(1, 502, kernel=8192,
  stride=480, padding=4096, padding_mode=reflect)`: `fmin` 27.5 Hz, 251 bins,
  **3 bins per semitone**, `gamma=7`, first harmonic only.
- A `Resnet1d` encoder that runs **per CQT frame, over the frequency axis only**.
  There is no temporal model at all — frames are independent.
- Output decoded from the activation grid by the `alwa` (local weighted average)
  reduction, plus a separate confidence head.
- Trained self-supervised on MIR-1K (singing voice) via equivariance to pitch
  shift. Singing voice is a fair match for sustained-vowel drills.

The 3-bins-per-semitone grid means a **native resolution of 33.3 cents**,
interpolated down to roughly 8 cents by the reduction. That is the structural
reason it cannot compete with autocorrelation on clean periodic signals, and it
is not a tuning problem.

---

## 2. Method

Ground truth is exact, not estimated. `groundtruth.py` reconstructs the
per-sample instantaneous f0 from the generators that made the wavs, including
`human.wav`'s seeded random drift (same RNG seed, the smoothing loop rewritten
as the equivalent one-pole `lfilter`).

| signal | ground truth |
|---|---|
| `steady.wav` | 50 s, constant **200.0 Hz** |
| `human.wav` | 30 s, 200 Hz ± 25 cent vibrato at 4.5 Hz + seeded drift (193.5-205.4 Hz) |
| `morph.wav` | 45 s glide **115 → 215 Hz** ± 15 cent vibrato (114.3-216.7 Hz) |

Baselines. `trackers.py` ports **the engine's own MPM and YIN line-for-line**
from `mpm.cpp` and `yin.cpp`, quirks included, so these are the engine's
algorithms and not generic reimplementations. librosa YIN/pYIN are included as
neutral references. All trackers run on a common 10 ms grid; ground truth is
reduced to each frame as the geometric mean of instantaneous f0 over the
tracker's window.

Metrics are in cents. **jitter** is the sd of (estimate step − *true* step)
between consecutive frames: motion the tracker invents. That is the number that
governs how much the on-screen dot shakes.

Two caveats stated plainly:

- **RAPT is not benchmarked.** It is the engine's configured default
  (`Config::getPitchAlgorithm` → `PitchAlgorithm::RAPT`), but it is 570 lines of
  C++ and porting it faithfully was out of scope. MPM is used as the strongest
  classical baseline; it is selectable in the engine's UI. Treat the classical
  column as "what a good classical tracker does here", not specifically as RAPT.
- **Voicing decisions are barely tested.** All three signals are 100 % voiced, so
  the voiced-rate column below measures false *negatives* only. Nothing here says
  anything about how the trackers handle onsets, offsets or unvoiced consonants.

---

## 3. Accuracy on clean signals

| tracker | med\|err\| | p95 | RPA50 | gross | oct | **jitter** | voiced | RTF |
|---|---|---|---|---|---|---|---|---|
| **`steady.wav`** — constant 200 Hz | | | | | | | | |
| PESTO mir-1k_g7 | 1.7 | 1.7 | 100 % | 0 % | 0 % | 0.0 | 100 % | 0.091 |
| engine MPM (40 ms) | 1.3 | 1.3 | 100 % | 0 % | 0 % | 0.0 | 100 % | 0.143 |
| engine YIN (40 ms) | 0.0 | 0.0 | 100 % | 0 % | 0 % | 0.0 | 100 % | 0.045 |
| librosa YIN | 0.9 | 0.9 | 100 % | 0 % | 0 % | 0.0 | 100 % | 0.289 |
| librosa pYIN | 4.2 | 4.2 | 100 % | 0 % | 0 % | 0.0 | 100 % | 0.203 |
| **`human.wav`** — vibrato vowel | | | | | | | | |
| PESTO mir-1k_g7 | **8.6** | 18.4 | 100 % | 0 % | 0 % | **10.7** | 100 % | 0.094 |
| engine MPM (40 ms) | **1.3** | 3.1 | 100 % | 0 % | 0 % | **0.6** | 100 % | 0.115 |
| engine YIN (40 ms) | 2.8 | 7.3 | 100 % | 0 % | 0 % | 3.4 | **67.2 %** | 0.052 |
| engine YIN + interp | 2.7 | 5.7 | 100 % | 0 % | 0 % | 1.5 | 67.2 % | 0.053 |
| librosa YIN | 1.3 | 3.0 | 100 % | 0 % | 0 % | 0.6 | 100 % | 0.008 |
| librosa pYIN | 2.5 | 6.2 | 100 % | 0 % | 0 % | 4.5 | 100 % | 0.157 |
| **`morph.wav`** — 115→215 Hz glide | | | | | | | | |
| PESTO mir-1k_g7 | **8.2** | 19.1 | 100 % | 0 % | 0 % | **8.3** | 100 % | 0.076 |
| engine MPM (40 ms) | **0.8** | 3.2 | 100 % | 0 % | 0 % | **1.4** | 100 % | 0.116 |
| engine YIN (40 ms) | 3.6 | 10.0 | 100 % | 0 % | 0 % | 3.5 | **42.9 %** | 0.045 |
| engine YIN + interp | 3.4 | 9.8 | 100 % | 0 % | 0 % | 2.2 | 42.9 % | 0.043 |
| librosa YIN | 0.8 | 2.5 | 100 % | 0 % | 0 % | 1.0 | 100 % | 0.008 |
| librosa pYIN | 2.5 | 5.6 | 100 % | 0 % | 0 % | 4.3 | 100 % | 0.133 |

Nobody makes a single octave error on clean input. The differences are all in
precision, and PESTO loses:

- **Accuracy.** 8.2-8.6 cents median vs MPM's 0.8-1.3. `steady.wav` flatters
  PESTO (1.7 cents) because a perfectly constant tone lands near a bin centre;
  the moment the pitch moves at all, the error goes up 5×. (The engine YIN's
  0.0 cents on `steady.wav` is luck of a different kind: 48000/240 = exactly
  200.000 Hz. It does no parabolic interpolation on the chosen lag, so its
  resolution is quantised to fs/k — about 7 cents at 200 Hz, 4 cents at 115 Hz.
  Adding interpolation is the "+ interp" row; it helps, and would be a cheap
  improvement to `yin.cpp` if YIN is ever used.)
- **Jitter, which matters more.** PESTO invents 8-11 cents of frame-to-frame
  motion; MPM invents 0.6-1.4. At 200 Hz, 10.7 cents is **1.24 Hz of fake
  movement every 10 ms**. For scale, `make_human.py` puts the *true* maximum
  vibrato slope at 1.6 Hz per 20 ms frame. PESTO's invented motion is the same
  order as the real vibrato it is supposed to be showing — it would visibly blur
  exactly the "hold a steady note" drill the toolkit exists to support.

This is not an artefact of comparing a 170 ms-window model against a 40 ms
ground-truth window. Re-scoring PESTO against truth averaged over its own window
makes it **worse**, not better (`human.wav`: 8.6 cents at 40 ms → 10.9 at
170.7 ms; jitter unchanged at 10.7 → 11.3). PESTO is genuinely tracking
instantaneous pitch, and genuinely tracking it less precisely.

---

## 4. Robustness — where PESTO actually wins

`human.wav`, 15 s, plus pink noise / level attenuation.

### Additive pink noise

| condition | tracker | med\|err\| | gross | jitter | voiced |
|---|---|---|---|---|---|
| clean | PESTO | 8.4 | 0 % | 10.7 | 100 % |
| | engine MPM | 1.3 | 0 % | 0.6 | 100 % |
| | engine YIN | 2.9 | 0 % | 3.4 | 67.0 % |
| | librosa pYIN | 2.5 | 0 % | 4.6 | 100 % |
| SNR 10 dB | PESTO | 8.5 | 0 % | 11.4 | 100 % |
| | engine MPM | 1.4 | 0 % | 1.3 | 100 % |
| | engine YIN | 2.8 | 0 % | 4.1 | **2.6 %** |
| | librosa pYIN | 2.6 | 0 % | 4.6 | 100 % |
| SNR 5 dB | PESTO | 8.7 | 0 % | 12.5 | 100 % |
| | engine MPM | 2.0 | 0 % | 2.7 | 100 % |
| | engine YIN | — | 100 % | — | **0 %** |
| | librosa pYIN | 3.1 | 0 % | 5.0 | 100 % |
| SNR 0 dB | **PESTO** | **9.1** | **0.1 %** | 47.9 | **100 %** |
| | engine MPM | 3.4 | 0.5 % | **115.7** | **40.9 %** |
| | engine YIN | — | 100 % | — | 0 % |
| | librosa pYIN | — | 100 % | — | **0 %** |

PESTO barely notices noise: 8.4 → 9.1 cents from clean to 0 dB SNR, still 100 %
voiced and 99.9 % RPA. At 0 dB it is the only tracker still working — MPM keeps
its accuracy but drops 59 % of frames and its jitter explodes to 115.7 cents,
and both YIN variants have already given up. The engine's YIN is the weak one
throughout: it sheds voiced frames from 10 dB SNR and is dead by 5 dB.

### Input level

| condition | PESTO | engine MPM | engine YIN | librosa pYIN |
|---|---|---|---|---|
| −0 dB | 8.4 cents, 100 % | 1.3 cents, 100 % | 2.9, 67 % | 2.5, 100 % |
| −20 dB | 8.4 cents, 100 % | **— , 0 %** | 2.9, 67 % | 2.5, 100 % |
| −40 dB | 8.4 cents, 100 % | **— , 0 %** | 2.9, 67 % | 2.5, 100 % |
| −60 dB | 8.4 cents, 100 % | **— , 0 %** | 2.9, 67 % | 2.5, 100 % |

PESTO is exactly scale-invariant. So are both YIN variants. **The engine's MPM
is not**, and that is a bug — see §5.

### Low f0 (a 40 ms window holds only ~4.6 periods at 118 Hz)

| condition | PESTO | engine MPM | engine YIN | librosa pYIN |
|---|---|---|---|---|
| morph 0-10 s, ~118 Hz | 8.2, 100 % | **0.8**, 100 % | 5.7, **18.5 %** | 2.5, 100 % |
| morph 35-45 s, ~205 Hz | 8.3, 100 % | **0.7**, 100 % | 3.0, 79.2 % | 2.5, 100 % |
| ~118 Hz at SNR 10 dB | 8.4, 100 % | **1.5**, 100 % | 6.5, **8.1 %** (jitter 169) | 2.7, 100 % |

Low f0 is not a problem for PESTO, MPM or pYIN. It is a serious problem for the
engine's YIN, which holds only 8-18 % of frames in the masc range with noise —
i.e. precisely the starting condition for transfem voice work.

---

## 5. Side finding: the engine's MPM silently dies on quiet input

`engine/src/analysis/pitch/mpm.cpp` normalises the autocorrelation like this:

```c++
double max = 0.02;
for (int i = 0; i < length; ++i)
    if (fabs(audio_buffer[i]) > max) max = fabs(audio_buffer[i]);
for (int i = 0; i < length; ++i) audio_buffer[i] /= max;
```

The `0.02` is an **absolute floor on a quantity that scales with signal power**.
Once the input is quiet enough that every autocorrelation value falls below it,
the division stops normalising, every peak lands under `MPM_SMALL_CUTOFF` (0.5),
`estimates` comes back empty and MPM reports **unvoiced**. Measured on
`human.wav` (true f0 ≈ 200 Hz):

| attenuation | RMS | voiced, `floor = 0.02` | voiced, `floor = 0` | median f0 (fixed) |
|---|---|---|---|---|
| 0 dB | −13.40 dBFS | 100.0 % | 100.0 % | 199.81 |
| −2 dB | −15.40 dBFS | **69.8 %** | 100.0 % | 199.81 |
| −3 dB | −16.40 dBFS | **47.0 %** | 100.0 % | 199.81 |
| −4 dB | −17.40 dBFS | **20.1 %** | 100.0 % | 199.81 |
| −5 dB | −18.40 dBFS | **0.0 %** | 100.0 % | 199.81 |
| −20 dB | −33.40 dBFS | 0.0 % | 100.0 % | 199.81 |
| −60 dB | −73.40 dBFS | 0.0 % | 100.0 % | 199.81 |

Degradation starts at about **−15 dBFS RMS and it is dead by −18 dBFS**. A
well-recorded voice normally sits around −20 dBFS RMS, so **MPM as shipped stops
tracking at ordinary recording levels**. Deleting the `0.02` seed makes it fully
scale-invariant with an identical estimate (199.81 Hz) all the way down to
−73 dBFS.

This is the only robustness gap in §4 that would plausibly bite a real user, and
it costs one line to fix. Reproduce with `eval_mpm_floor.py`.

---

## 6. Latency

**Algorithmic lookahead** — future audio needed before a value for time *t* can
be emitted. Irreducible; sets how stale the dot is.

| | window | lookahead |
|---|---|---|
| PESTO | 8192 samples @ 48 kHz = **170.7 ms** | **85.3 ms** |
| engine MPM / YIN | 40 ms (`Config` default) | **20.0 ms** |

PESTO's encoder is frame-independent, so there is no *extra* temporal context
beyond the CQT window and it can stream from a 170 ms ring buffer. But 85.3 ms
of lookahead is **4.3× the engine's current 20 ms**, on top of capture and paint
latency, for a feedback tool where the delay between making a sound and seeing
it is the whole product.

**Compute per frame**, single call with a full window, 200 calls:

| estimator | mean | median | p95 | max | headroom at a 10 ms hop |
|---|---|---|---|---|---|
| PESTO, 1 thread | **26.57 ms** | 26.07 | 28.29 | 43.25 | **0.4×** (cannot keep up) |
| PESTO, 8 threads | 10.50 ms | 10.51 | 10.96 | 13.76 | 1.0× (no headroom) |
| engine MPM (40 ms) | **0.92 ms** | 0.90 | 0.98 | 1.73 | 10.9× |
| engine YIN (40 ms) | 0.45 ms | 0.41 | 0.66 | 0.85 | 22.3× |

The MPM/YIN figures are numpy ports and so are an *upper bound* on the C++ cost.

Batched, PESTO amortises far better (RTF 0.076-0.094, ~11× realtime) because one
CQT convolution covers every frame. So the practical picture is: at the engine's
actual 80 ms analysis spacing, PESTO costs ~26 ms per update on one core — about
a third of a core, workable — but that is **29× MPM's cost** for a worse answer.
Per-frame at a 10 ms hop it does not fit on one core at all.

---

## 7. Recommendation

**Do not adopt PESTO as the engine's pitch tracker.** Against the strongest
classical baseline it is 6-10× less accurate (8.2-8.6 vs 0.8-1.3 cents), has
8-18× the jitter, needs 4.3× the lookahead, and costs ~29× the compute. The
jitter alone disqualifies it: 1.2 Hz of invented movement per frame at 200 Hz is
the same order as real vibrato, and smooth dots are a feature this toolkit has
already spent effort on.

**Its robustness is real and worth remembering.** Exact scale invariance,
essentially flat accuracy from clean down to 0 dB SNR, no voicing dropouts, no
octave errors anywhere in this battery. If there is ever a need to analyse
genuinely bad audio — old phone recordings, archival clips, a laptop mic across
a room — PESTO offline is a sound choice, and `pesto-pitch` is already installed
in `ml/.venv-audio` for that. It is not a real-time answer.

**Do these instead, in order:**

1. **Delete the `0.02` floor in `mpm.cpp`.** One line; turns MPM from "fails
   below −18 dBFS" into scale-invariant. This is the highest-value change found
   in this whole evaluation.
2. **Add parabolic interpolation to `yin.cpp`'s chosen lag** if YIN stays
   selectable — it cuts `morph.wav` jitter from 3.5 to 2.2 cents for a few lines.
   Better still, note that the engine's YIN sheds 33-57 % of voiced frames on
   clean signals and 91 % in the masc range with noise; it is the weakest option
   in the box and probably should not be the one a new user lands on.
3. **Benchmark RAPT**, since it is the actual default and is the one tracker here
   that was not measured.

---

## Files

| File | What it does |
|---|---|
| `groundtruth.py` | exact per-sample f0 for the three synthetics |
| `trackers.py` | engine MPM/YIN ports, librosa refs, PESTO wrapper |
| `eval_pitch.py` | §3 — accuracy/jitter vs ground truth |
| `eval_robust.py` | §4 — noise, level, low-f0 stress |
| `eval_mpm_floor.py` | §5 — the MPM level bug, with the fix demonstrated |
| `eval_latency.py` | §6 — lookahead and per-frame compute |

Run any of them with `../.venv-audio/bin/python <script>`. Test signals come from
the session scratchpad.
