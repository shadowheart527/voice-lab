"""Exact per-sample f0 ground truth for the synthetic test signals.

Reconstructed from the generators in the scratchpad (make_steady.py,
make_morph.py, make_human.py), including human.wav's seeded random drift, so
these are the true instantaneous frequencies used to build the phase, not an
approximation.
"""

import numpy as np
from scipy.signal import lfilter

FS = 48000


def steady():
    """50 s of a constant 200 Hz vowel."""
    n = int(50.0 * FS)
    return np.full(n, 200.0)


def morph():
    """45 s glide 115 -> 215 Hz with +-15 cent vibrato at 4.5 Hz."""
    n = int(45.0 * FS)
    t = np.arange(n) / FS
    f0 = 115.0 * (215.0 / 115.0) ** (t / 45.0)
    vib = 15.0 * np.sin(2 * np.pi * 4.5 * t)
    return f0 * 2 ** (vib / 1200.0)


def human(seed=527):
    """30 s at 200 Hz with +-25 cent vibrato at 4.5 Hz plus a seeded slow drift.

    make_human.py smooths white noise with acc = a*acc + (1-a)*drift[i], which
    is the one-pole IIR b=[1-a], a=[1,-a]; then rescales to sd 10 cents.
    """
    n = int(30.0 * FS)
    t = np.arange(n) / FS
    rng = np.random.default_rng(seed)
    drift = rng.standard_normal(n)
    a = np.exp(-1.0 / (0.5 * FS))
    drift = lfilter([1 - a], [1.0, -a], drift)
    drift = drift * (10.0 / max(1e-9, float(np.std(drift))))
    vib = 25.0 * np.sin(2 * np.pi * 4.5 * t)
    return 200.0 * 2 ** ((vib + drift) / 1200.0)


GROUND_TRUTH = {"steady": steady, "morph": morph, "human": human}


def window_truth(f0_samples, times, fs, win_ms):
    """Ground truth as a tracker with a `win_ms` window should see it: the
    geometric mean of the instantaneous f0 across that window."""
    nwin = int(round(win_ms * fs / 1000.0))
    log_f = np.log(f0_samples)
    csum = np.concatenate([[0.0], np.cumsum(log_f)])
    out = np.full(len(times), np.nan)
    for i, tc in enumerate(times):
        c = int(round(tc * fs))
        a = c - nwin // 2
        b = a + nwin
        if a < 0 or b > len(f0_samples):
            continue
        out[i] = np.exp((csum[b] - csum[a]) / nwin)
    return out


def cents(est, ref):
    return 1200.0 * np.log2(np.asarray(est, dtype=float) / np.asarray(ref, dtype=float))
