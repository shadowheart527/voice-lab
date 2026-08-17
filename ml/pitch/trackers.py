"""Classical pitch trackers, ported faithfully from the engine, plus librosa
reference implementations, plus PESTO. Same interface for all of them:

    track(x, fs, hop_ms) -> (times, f0_hz)   with NaN where unvoiced.

The MPM and YIN ports are line-for-line from engine/src/analysis/pitch/mpm.cpp
and yin.cpp, including their quirks (notably: the engine's YIN does no parabolic
interpolation on the final lag, so its resolution is quantised to fs/k).
"""

from __future__ import annotations

import numpy as np

ENGINE_WINDOW_MS = 40.0  # Config::getAnalysisPitchWindow() default


# ---------------------------------------------------------------- helpers ----

def _frames(x, fs, win_ms, hop_ms):
    nwin = int(round(win_ms * fs / 1000.0))
    nhop = int(round(hop_ms * fs / 1000.0))
    starts = np.arange(0, len(x) - nwin + 1, nhop)
    times = (starts + nwin / 2.0) / fs
    return starts, nwin, times


def _acorr(buf):
    """Engine's acorr_r: FFT autocorrelation, nfft = 2N-1, scaled by 1/nfft."""
    n = len(buf)
    nfft = 2 * n - 1
    X = np.fft.rfft(buf, nfft)
    r = np.fft.irfft(X * np.conj(X), nfft) / nfft
    return r[:n]


def _parabolic(arr, i):
    """Engine util parabolicInterpolation -> (x, y) with x in samples."""
    if i <= 0 or i >= len(arr) - 1:
        return float(i), float(arr[i])
    a, b, c = arr[i - 1], arr[i], arr[i + 1]
    denom = a - 2.0 * b + c
    if abs(denom) < 1e-30:
        return float(i), float(b)
    shift = 0.5 * (a - c) / denom
    return float(i) + shift, float(b - 0.25 * (a - c) * shift)


# ------------------------------------------------------- engine MPM port ----

MPM_CUTOFF = 0.93
MPM_SMALL_CUTOFF = 0.5
MPM_LOWER_PITCH_CUTOFF = 80.0


def _peak_picking(nsdf):
    max_positions = []
    size = len(nsdf)
    pos = 0
    cur_max_pos = 0
    while pos < (size - 1) // 3 and nsdf[pos] > 0:
        pos += 1
    while pos < size - 1 and nsdf[pos] <= 0.0:
        pos += 1
    if pos == 0:
        pos = 1
    while pos < size - 1:
        if (nsdf[pos] > nsdf[pos - 1] and nsdf[pos] >= nsdf[pos + 1]
                and (cur_max_pos == 0 or nsdf[pos] > nsdf[cur_max_pos])):
            cur_max_pos = pos
        pos += 1
        if pos < size - 1 and nsdf[pos] <= 0:
            if cur_max_pos > 0:
                max_positions.append(cur_max_pos)
                cur_max_pos = 0
            while pos < size - 1 and nsdf[pos] <= 0.0:
                pos += 1
    if cur_max_pos > 0:
        max_positions.append(cur_max_pos)
    return max_positions


def mpm_solve(frame, fs):
    buf = _acorr(np.asarray(frame, dtype=float))
    m = max(0.02, float(np.max(np.abs(buf))))
    buf = buf / m
    peaks = _peak_picking(buf)
    estimates = []
    highest = -np.inf
    for i in peaks:
        highest = max(highest, buf[i])
        if buf[i] > MPM_SMALL_CUTOFF:
            x, y = _parabolic(buf, i)
            estimates.append((x, y))
            highest = max(highest, y)
    if not estimates:
        return np.nan
    cutoff = MPM_CUTOFF * highest
    period = 0.0
    for x, y in estimates:
        if y >= cutoff:
            period = x
            break
    if period <= 0:
        return np.nan
    p = fs / period
    return p if p > MPM_LOWER_PITCH_CUTOFF else np.nan


# ------------------------------------------------------- engine YIN port ----

def yin_solve(frame, fs, threshold=0.15):
    x = np.asarray(frame, dtype=float)
    length = len(x)
    nfft = 1 << int(np.ceil(np.log2(length)))
    X = np.fft.fft(x, nfft)
    r = np.real(np.fft.ifft(X * np.conj(X) / nfft))[:length]
    half = length // 2
    diff = r[0] + r[1] - 2 * r[:half]
    cmnd = np.ones(half)
    running = 0.0
    for tau in range(1, half):
        running += diff[tau]
        cmnd[tau] = (tau * diff[tau]) / running if running != 0 else 1.0
    k = half
    for kk in range(2, half):
        if cmnd[kk] < threshold:
            k = kk
            while k + 1 < half and cmnd[k + 1] < cmnd[k]:
                k += 1
            break
    if k >= half or cmnd[k] >= threshold or k == 0:
        return np.nan
    # NOTE: no interpolation here -- this is exactly what the engine does.
    return fs / k


def yin_solve_interp(frame, fs, threshold=0.15):
    """The same YIN with parabolic interpolation on the chosen lag, to separate
    'YIN is inaccurate' from 'the engine's YIN is quantised'."""
    x = np.asarray(frame, dtype=float)
    length = len(x)
    nfft = 1 << int(np.ceil(np.log2(length)))
    X = np.fft.fft(x, nfft)
    r = np.real(np.fft.ifft(X * np.conj(X) / nfft))[:length]
    half = length // 2
    diff = r[0] + r[1] - 2 * r[:half]
    cmnd = np.ones(half)
    running = 0.0
    for tau in range(1, half):
        running += diff[tau]
        cmnd[tau] = (tau * diff[tau]) / running if running != 0 else 1.0
    k = half
    for kk in range(2, half):
        if cmnd[kk] < threshold:
            k = kk
            while k + 1 < half and cmnd[k + 1] < cmnd[k]:
                k += 1
            break
    if k >= half or cmnd[k] >= threshold or k == 0:
        return np.nan
    if 0 < k < half - 1:
        a, b, c = cmnd[k - 1], cmnd[k], cmnd[k + 1]
        denom = a - 2 * b + c
        if abs(denom) > 1e-30:
            k = k + 0.5 * (a - c) / denom
    return fs / k


# ------------------------------------------------------------- wrappers ----

def track_engine(x, fs, hop_ms=10.0, win_ms=ENGINE_WINDOW_MS, solver="mpm"):
    fn = {"mpm": mpm_solve, "yin": yin_solve, "yin_interp": yin_solve_interp}[solver]
    starts, nwin, times = _frames(x, fs, win_ms, hop_ms)
    out = np.array([fn(x[s:s + nwin], fs) for s in starts])
    return times, out


def track_librosa(x, fs, hop_ms=10.0, method="yin", fmin=65.0, fmax=500.0,
                  frame_ms=ENGINE_WINDOW_MS):
    import librosa
    hop = int(round(hop_ms * fs / 1000.0))
    fl = int(round(frame_ms * fs / 1000.0))
    if method == "yin":
        f0 = librosa.yin(np.asarray(x, dtype=np.float32), fmin=fmin, fmax=fmax,
                         sr=fs, frame_length=fl, hop_length=hop, center=True)
        voiced = np.ones_like(f0, dtype=bool)
    else:
        f0, voiced, _ = librosa.pyin(np.asarray(x, dtype=np.float32), fmin=fmin,
                                     fmax=fmax, sr=fs, frame_length=fl,
                                     hop_length=hop, center=True)
    t = np.arange(len(f0)) * hop / fs
    f0 = np.where(voiced, f0, np.nan)
    return t, f0


_PESTO_CACHE = {}


def track_pesto(x, fs, hop_ms=10.0, model_name="mir-1k_g7", conf_thresh=0.0):
    import torch
    import pesto
    key = (model_name, hop_ms, fs)
    if key not in _PESTO_CACHE:
        _PESTO_CACHE[key] = pesto.load_model(model_name, step_size=hop_ms,
                                             sampling_rate=fs)
    model = _PESTO_CACHE[key]
    xt = torch.from_numpy(np.asarray(x, dtype=np.float32))
    with torch.inference_mode():
        # PESTO.forward returns (preds, confidence, volume); pesto.predict then
        # builds timesteps as arange * hop_size, and the CQT is center=True, so
        # frame i is centred on i * hop.
        preds, conf, _vol = model(xt, convert_to_freq=True,
                                  return_activations=False)
    f0 = preds.numpy().astype(float)
    c = conf.numpy().astype(float)
    t = np.arange(len(f0)) * (model.hop_size / 1000.0)
    if conf_thresh > 0:
        f0 = np.where(c >= conf_thresh, f0, np.nan)
    return t, f0, c
