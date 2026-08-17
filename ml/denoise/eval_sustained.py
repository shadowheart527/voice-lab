"""Does DeepFilterNet survive a sustained held tone?

A speech denoiser's prior is that anything stationary is background. A held
vowel drill is stationary by construction, which is exactly why RNNoise ate one
(engine/src/modules/app/pipeline/pipeline.cpp: a 110 Hz tone came out tracking
at 202 Hz). This measures whether DeepFilterNet does the same, over time.
"""

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from audiometrics import (db, f0_track_acf, harmonic_levels, load_wav,  # noqa: E402
                          noise_floor_db, rms, rms_db, tilt_track)

SCRATCH = Path("/tmp/claude-1000/-var-home-shadowheart527/"
               "04778bbc-32b9-4506-a8ec-d6dd51383461/scratchpad")
OUT = Path(__file__).resolve().parent / "out"


def block_rms_db(x, fs, sec=1.0):
    n = int(sec * fs)
    return np.array([rms_db(x[i:i + n]) for i in range(0, len(x) - n + 1, n)])


def report(name, f0, kmax=14):
    ref, fs = load_wav(SCRATCH / f"{name}.wav")
    enh, _ = load_wav(OUT / f"{name}.df.wav")
    n = min(len(ref), len(enh))
    ref, enh = ref[:n], enh[:n]

    print(f"\n{'=' * 72}\n{name}.wav   f0={f0} Hz   {n / fs:.1f} s @ {fs} Hz\n{'=' * 72}")

    print(f"\n-- Overall level --")
    print(f"  RMS  original {rms_db(ref):8.2f} dBFS")
    print(f"  RMS  denoised {rms_db(enh):8.2f} dBFS   "
          f"delta {rms_db(enh) - rms_db(ref):+.2f} dB")

    # Time course: progressive suppression is the failure mode that matters.
    br, be = block_rms_db(ref, fs), block_rms_db(enh, fs)
    d = be - br
    print(f"\n-- RMS delta per 1 s block (denoised - original), dB --")
    print("  " + " ".join(f"{v:+6.2f}" for v in d[:20]))
    if len(d) > 20:
        print("  " + " ".join(f"{v:+6.2f}" for v in d[20:40]))
    if len(d) > 40:
        print("  " + " ".join(f"{v:+6.2f}" for v in d[40:]))
    mid = slice(1, len(d) - 1)  # ignore fade in/out blocks
    print(f"  steady-state blocks: mean {d[mid].mean():+.2f} dB, "
          f"min {d[mid].min():+.2f}, max {d[mid].max():+.2f}, "
          f"first->last {d[mid][0]:+.2f} -> {d[mid][-1]:+.2f} "
          f"(drift {d[mid][-1] - d[mid][0]:+.2f} dB)")

    # Harmonic structure, measured on a stationary 10 s excerpt near the middle.
    a = int(len(ref) * 0.35)
    b = a + min(int(10 * fs), len(ref) - a)
    hr = harmonic_levels(ref[a:b], fs, f0, kmax=kmax)
    he = harmonic_levels(enh[a:b], fs, f0, kmax=kmax)
    fr = noise_floor_db(ref[a:b], fs, f0, kmax=kmax)
    fe = noise_floor_db(enh[a:b], fs, f0, kmax=kmax)
    print(f"\n-- Harmonic peak levels on a 10 s stationary excerpt "
          f"({a / fs:.1f}-{b / fs:.1f} s), dB --")
    print(f"  {'k':>3} {'f (Hz)':>8} {'orig':>9} {'denoised':>9} {'delta':>8}")
    deltas = []
    for k in sorted(set(hr) & set(he)):
        dlt = he[k] - hr[k]
        deltas.append(dlt)
        print(f"  {k:>3} {k * f0:>8.0f} {hr[k]:>9.2f} {he[k]:>9.2f} {dlt:>+8.2f}")
    deltas = np.array(deltas)
    print(f"  harmonic delta: mean {deltas.mean():+.2f} dB, "
          f"min {deltas.min():+.2f}, max {deltas.max():+.2f}")
    print(f"  inter-harmonic floor: {fr:.2f} -> {fe:.2f} dB "
          f"({fe - fr:+.2f} dB)")
    print(f"  harmonic-to-floor (H1): {hr[1] - fr:.1f} -> {he[1] - fe:.1f} dB")

    # Engine-identical tilt, using the ORIGINAL signal's f0 for both, so any
    # change is spectral and not a knock-on of pitch tracking.
    _, f0t = f0_track_acf(ref, fs)
    tr_t, tr_s, tr_n = tilt_track(ref, fs, f0t)
    te_t, te_s, te_n = tilt_track(enh, fs, f0t)
    print(f"\n-- Engine spectral tilt (Theil-Sen, dB/octave) --")
    print(f"  original: {np.median(tr_s):+.2f} median, sd {tr_s.std():.2f}, "
          f"{len(tr_s)} frames, {tr_n.mean():.1f} harmonics/frame")
    print(f"  denoised: {np.median(te_s):+.2f} median, sd {te_s.std():.2f}, "
          f"{len(te_s)} frames, {te_n.mean():.1f} harmonics/frame")
    print(f"  tilt shift: {np.median(te_s) - np.median(tr_s):+.2f} dB/oct "
          f"-> vocal-weight percept shift {(np.median(te_s) - np.median(tr_s)) / 8.0:+.3f} "
          f"(0..1 scale)")

    # Pitch: does the tone survive as the same tone?
    _, f0e = f0_track_acf(enh, fs)
    good = np.isfinite(f0t) & np.isfinite(f0e)
    err = f0e[good] - f0t[good]
    print(f"\n-- f0 (ACF, same frames) --")
    print(f"  original median {np.nanmedian(f0t):.2f} Hz, "
          f"denoised median {np.nanmedian(f0e):.2f} Hz")
    print(f"  per-frame |delta|: median {np.median(np.abs(err)):.3f} Hz, "
          f"p95 {np.percentile(np.abs(err), 95):.3f} Hz, "
          f"max {np.abs(err).max():.3f} Hz")
    octave_errs = int(np.sum(np.abs(err) > 0.25 * np.nanmedian(f0t)))
    print(f"  frames off by >25% of f0 (octave/harmonic slips): {octave_errs}"
          f" / {good.sum()}")


if __name__ == "__main__":
    report("steady", 200.0)
    report("human", 200.0)
