"""Side finding: the engine's MPM stops tracking on quiet input.

engine/src/analysis/pitch/mpm.cpp normalises the autocorrelation by

    double max = 0.02;
    for (...) if (fabs(audio_buffer[i]) > max) max = fabs(audio_buffer[i]);
    for (...) audio_buffer[i] /= max;

The 0.02 seed is an ABSOLUTE floor on a quantity that scales with signal power,
so the normalisation stops normalising once the input is quiet. Every peak then
falls below MPM_SMALL_CUTOFF (0.5), `estimates` comes back empty, and MPM
reports unvoiced. Removing the floor makes it scale-invariant, which is what it
was presumably meant to be.
"""

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from audiometrics import load_wav, rms_db  # noqa: E402
import trackers as T  # noqa: E402

SCRATCH = Path("/tmp/claude-1000/-var-home-shadowheart527/"
               "04778bbc-32b9-4506-a8ec-d6dd51383461/scratchpad")


def mpm_with_floor(frame, fs, floor):
    buf = T._acorr(np.asarray(frame, dtype=float))
    m = max(floor, float(np.max(np.abs(buf))))
    buf = buf / m
    peaks = T._peak_picking(buf)
    est, hi = [], -np.inf
    for i in peaks:
        hi = max(hi, buf[i])
        if buf[i] > T.MPM_SMALL_CUTOFF:
            x, y = T._parabolic(buf, i)
            est.append((x, y))
            hi = max(hi, y)
    if not est:
        return np.nan
    cut = T.MPM_CUTOFF * hi
    period = 0.0
    for x, y in est:
        if y >= cut:
            period = x
            break
    if period <= 0:
        return np.nan
    p = fs / period
    return p if p > T.MPM_LOWER_PITCH_CUTOFF else np.nan


if __name__ == "__main__":
    x, fs = load_wav(SCRATCH / "human.wav")
    x = x[:int(3 * fs)]
    nwin = int(0.040 * fs)
    starts = list(range(0, len(x) - nwin + 1, int(0.020 * fs)))

    print("engine MPM voiced rate vs input level, human.wav (true f0 ~200 Hz)")
    print(f"  {'atten':>6} {'RMS dBFS':>10} {'voiced%: floor=0.02':>21} "
          f"{'voiced%: floor=0':>18} {'median f0 (fixed)':>18}")
    for att in (0, 2, 3, 4, 5, 6, 10, 20, 40, 60):
        y = x * 10 ** (-att / 20.0)
        a = np.array([mpm_with_floor(y[s:s + nwin], fs, 0.02) for s in starts])
        b = np.array([mpm_with_floor(y[s:s + nwin], fs, 0.0) for s in starts])
        print(f"  {-att:>5}d {rms_db(y):>10.2f} {np.isfinite(a).mean() * 100:>20.1f}%"
              f" {np.isfinite(b).mean() * 100:>17.1f}% {np.nanmedian(b):>18.2f}")
