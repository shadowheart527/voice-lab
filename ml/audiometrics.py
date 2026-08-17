"""Shared measurement helpers for the ml/ evaluations.

The spectral-tilt routine is a deliberate line-by-line port of the engine's
``Pitch::computeSpectralTilt`` (engine/src/modules/app/pipeline/processors/
pitch.cpp): 4096-point FFT, Hann window over the raw 40 ms analysis frame,
harmonics 1..14, 160 Hz low cut, 3.2 kHz high cut, a 3.2x inter-harmonic-valley
SNR gate, and a Theil-Sen slope in dB/octave. Using the same estimator means the
before/after numbers here are the numbers the toolkit would actually show, not a
parallel invention.
"""

from __future__ import annotations

import numpy as np
import soundfile as sf

FS_DEFAULT = 48000
NFFT = 4096
FRAME_MS = 40.0    # engine default analysis pitch window
HOP_MS = 80.0      # engine default analysis pitch spacing


def load_wav(path, want_fs=None):
    x, fs = sf.read(str(path), dtype="float64", always_2d=False)
    if x.ndim > 1:
        x = x.mean(axis=1)
    if want_fs is not None and fs != want_fs:
        raise ValueError(f"{path}: expected {want_fs} Hz, got {fs}")
    return x, fs


def db(x):
    return 20.0 * np.log10(np.maximum(np.asarray(x, dtype=float), 1e-300))


def rms(x):
    return float(np.sqrt(np.mean(np.square(np.asarray(x, dtype=float)))))


def rms_db(x):
    return db(rms(x))


# ----------------------------------------------------------------- tilt ----

def spectral_tilt(frame, fs, f0, nfft=NFFT, kmax=14,
                  fmin=160.0, fmax=3200.0, snr_gate=3.2, min_harmonics=3):
    """Port of the engine's computeSpectralTilt. Returns (slope_db_per_oct,
    n_harmonics_used) or (None, n_used) when the estimator would abstain."""
    frame = np.asarray(frame, dtype=float)
    n = min(len(frame), nfft)
    w = 0.5 - 0.5 * np.cos(2.0 * np.pi * np.arange(n) / (n - 1))
    buf = np.zeros(nfft)
    buf[:n] = frame[:n] * w
    spec = np.abs(np.fft.rfft(buf))
    n_out = len(spec)
    bin_hz = fs / nfft

    octs, amps = [], []
    first_f = -1.0
    for k in range(1, kmax + 1):
        fk = k * f0
        if fk >= min(fmax, fs / 2.0 - 100.0):
            break
        if fk < fmin:
            continue
        lo = max(1, int((fk - 0.35 * f0) / bin_hz))
        hi = min(n_out - 1, int((fk + 0.35 * f0) / bin_hz) + 1)
        if hi <= lo:
            continue
        seg = spec[lo:hi + 1]
        bi = int(np.argmax(seg))
        best = float(seg[bi])
        best_f = (lo + bi) * bin_hz
        if best <= 1e-12:
            continue

        floor_vals = []
        for half in (fk - 0.5 * f0, fk + 0.5 * f0):
            flo = max(1, int((half - 0.15 * f0) / bin_hz))
            fhi = min(n_out - 1, int((half + 0.15 * f0) / bin_hz) + 1)
            if fhi >= flo:
                floor_vals.append(spec[flo:fhi + 1])
        if floor_vals:
            floor_mag = float(np.mean(np.concatenate(floor_vals)))
            if best < snr_gate * floor_mag:
                continue

        if first_f < 0.0:
            first_f = best_f
        octs.append(np.log2(best_f / first_f))
        amps.append(db(best))

    nused = len(octs)
    if nused < min_harmonics:
        return None, nused

    octs = np.asarray(octs)
    amps = np.asarray(amps)
    slopes = []
    for i in range(nused):
        for j in range(i + 1, nused):
            dx = octs[j] - octs[i]
            if abs(dx) > 1e-9:
                slopes.append((amps[j] - amps[i]) / dx)
    if not slopes:
        return None, nused
    slope = float(np.median(slopes))
    if slope < -40.0 or slope > 15.0:
        return None, nused
    return slope, nused


def tilt_track(x, fs, f0_track, frame_ms=FRAME_MS, hop_ms=HOP_MS, **kw):
    """Per-frame tilt using a caller-supplied f0 per frame (so the same f0 can
    be reused across clean/denoised versions, isolating the spectral change)."""
    nwin = int(round(frame_ms * fs / 1000.0))
    nhop = int(round(hop_ms * fs / 1000.0))
    out_t, out_s, out_n = [], [], []
    for fi, start in enumerate(range(0, len(x) - nwin + 1, nhop)):
        if fi >= len(f0_track):
            break
        f0 = f0_track[fi]
        if not np.isfinite(f0) or f0 <= 40.0:
            continue
        s, nused = spectral_tilt(x[start:start + nwin], fs, f0, **kw)
        if s is None:
            continue
        out_t.append((start + nwin / 2.0) / fs)
        out_s.append(s)
        out_n.append(nused)
    return np.asarray(out_t), np.asarray(out_s), np.asarray(out_n)


def frame_times(x, fs, frame_ms=FRAME_MS, hop_ms=HOP_MS):
    nwin = int(round(frame_ms * fs / 1000.0))
    nhop = int(round(hop_ms * fs / 1000.0))
    starts = np.arange(0, len(x) - nwin + 1, nhop)
    return starts, (starts + nwin / 2.0) / fs


# ------------------------------------------------------------- f0 (ACF) ----

def f0_acf(frame, fs, fmin=60.0, fmax=500.0):
    """Plain autocorrelation f0 with parabolic interpolation. Only used to
    supply a common f0 to the tilt estimator; not a tracker under test."""
    x = np.asarray(frame, dtype=float)
    x = x - x.mean()
    if np.max(np.abs(x)) < 1e-9:
        return np.nan
    n = len(x)
    nfft = 1 << int(np.ceil(np.log2(2 * n)))
    X = np.fft.rfft(x, nfft)
    r = np.fft.irfft(X * np.conj(X), nfft)[:n]
    if r[0] <= 0:
        return np.nan
    r = r / r[0]
    lo = int(fs / fmax)
    hi = min(n - 2, int(fs / fmin))
    if hi <= lo + 1:
        return np.nan
    seg = r[lo:hi]
    k = int(np.argmax(seg)) + lo
    if k <= 0 or k >= n - 1:
        return np.nan
    a, b, c = r[k - 1], r[k], r[k + 1]
    denom = a - 2 * b + c
    shift = 0.5 * (a - c) / denom if abs(denom) > 1e-12 else 0.0
    return fs / (k + shift)


def f0_track_acf(x, fs, frame_ms=FRAME_MS, hop_ms=HOP_MS, **kw):
    starts, times = frame_times(x, fs, frame_ms, hop_ms)
    nwin = int(round(frame_ms * fs / 1000.0))
    return times, np.array([f0_acf(x[s:s + nwin], fs, **kw) for s in starts])


# ------------------------------------------------- long-window harmonics ----

def harmonic_levels(x, fs, f0, kmax=20, nfft=1 << 18, search_cents=60.0):
    """Harmonic peak levels in dB from one long Hann-windowed FFT. Suitable for
    stationary signals (steady.wav); each level is the max magnitude within
    +-`search_cents` of k*f0, normalised so it is directly comparable between
    two versions of the same recording."""
    x = np.asarray(x, dtype=float)
    n = min(len(x), nfft)
    w = np.hanning(n)
    buf = np.zeros(nfft)
    buf[:n] = x[:n] * w
    spec = np.abs(np.fft.rfft(buf))
    bin_hz = fs / nfft
    out = {}
    for k in range(1, kmax + 1):
        fk = k * f0
        if fk >= fs / 2 - 100:
            break
        lo = max(1, int((fk * 2 ** (-search_cents / 1200.0)) / bin_hz))
        hi = min(len(spec) - 1, int((fk * 2 ** (search_cents / 1200.0)) / bin_hz) + 1)
        if hi <= lo:
            continue
        out[k] = db(float(np.max(spec[lo:hi + 1])))
    return out


def noise_floor_db(x, fs, f0, kmax=20, nfft=1 << 18):
    """Median magnitude in the inter-harmonic valleys, same FFT scaling as
    harmonic_levels, so floor and harmonics can be differenced directly."""
    x = np.asarray(x, dtype=float)
    n = min(len(x), nfft)
    w = np.hanning(n)
    buf = np.zeros(nfft)
    buf[:n] = x[:n] * w
    spec = np.abs(np.fft.rfft(buf))
    bin_hz = fs / nfft
    vals = []
    for k in range(1, kmax + 1):
        fk = (k + 0.5) * f0
        if fk >= fs / 2 - 100:
            break
        lo = max(1, int((fk - 0.15 * f0) / bin_hz))
        hi = min(len(spec) - 1, int((fk + 0.15 * f0) / bin_hz) + 1)
        if hi > lo:
            vals.append(spec[lo:hi + 1])
    if not vals:
        return np.nan
    return db(float(np.median(np.concatenate(vals))))


def band_rms_db(x, fs, lo, hi):
    X = np.fft.rfft(x * np.hanning(len(x)))
    f = np.fft.rfftfreq(len(x), 1.0 / fs)
    m = (f >= lo) & (f < hi)
    if not np.any(m):
        return np.nan
    return db(np.sqrt(np.mean(np.abs(X[m]) ** 2)))
