"""Mitigation test: DeepFilterNet's `atten_lim_db` caps how much it may suppress.

If a capped setting keeps sustained tones intact and still removes useful noise
without moving the tilt, there is a safe operating point. If not, there isn't.
"""

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from audiometrics import (band_rms_db, f0_track_acf, load_wav, rms,  # noqa: E402
                          rms_db, tilt_track)
from run_df import denoise  # noqa: E402

SCRATCH = Path("/tmp/claude-1000/-var-home-shadowheart527/"
               "04778bbc-32b9-4506-a8ec-d6dd51383461/scratchpad")
LIMS = [None, 20.0, 12.0, 6.0, 3.0]


def med_tilt(x, fs, f0t):
    _, s, _ = tilt_track(x, fs, f0t)
    return float(np.median(s)) if len(s) >= 3 else np.nan


print("=" * 78)
print("A. Sustained held note vs atten_lim_db")
print("=" * 78)
print(f"  {'signal':<10} {'atten_lim':>10} {'steady-state RMS d':>20} "
      f"{'tilt orig':>10} {'tilt df':>9} {'d tilt':>8}")
for name in ("steady", "human"):
    ref, fs = load_wav(SCRATCH / f"{name}.wav")
    _, f0t = f0_track_acf(ref, fs)
    base = med_tilt(ref, fs, f0t)
    # steady-state window: skip the first 2 s so the note is established
    seg = slice(int(2 * fs), len(ref) - int(0.5 * fs))
    for lim in LIMS:
        enh, _ = denoise(ref, fs, atten_lim_db=lim)
        enh = enh[:len(ref)]
        d = rms_db(enh[seg]) - rms_db(ref[seg])
        t = med_tilt(enh, fs, f0t)
        print(f"  {name:<10} {str(lim):>10} {d:>+20.2f} "
              f"{base:>10.2f} {t:>9.2f} {t - base:>+8.2f}")

print("\n" + "=" * 78)
print("B. Real speech at 10 dB SNR vs atten_lim_db")
print("=" * 78)
rng = np.random.default_rng(527)
clips = sorted((SCRATCH / "tvldemos").glob("*.wav"))
res = {lim: [] for lim in LIMS}
noise_red = {lim: [] for lim in LIMS}
noisy_err = []
for src in clips:
    clean, fs = load_wav(src)
    _, f0t = f0_track_acf(clean, fs, fmin=70.0, fmax=400.0)
    base = med_tilt(clean, fs, f0t)
    w = rng.standard_normal(len(clean))
    W = np.fft.rfft(w)
    f = np.fft.rfftfreq(len(clean), 1 / fs)
    W *= 1.0 / np.sqrt(np.maximum(f, 20.0))
    noise = np.fft.irfft(W, len(clean))
    noise /= rms(noise)
    nz = clean + noise * rms(clean) * 10 ** (-10.0 / 20.0)
    noisy_err.append(med_tilt(nz, fs, f0t) - base)
    nz_floor = band_rms_db(nz[:int(2 * fs)], fs, 6000, 12000)
    for lim in LIMS:
        enh, _ = denoise(nz, fs, atten_lim_db=lim)
        enh = enh[:len(clean)]
        res[lim].append(med_tilt(enh, fs, f0t) - base)
        noise_red[lim].append(band_rms_db(enh[:int(2 * fs)], fs, 6000, 12000) - nz_floor)

print(f"\n  noisy, no denoise: tilt err {np.mean(noisy_err):+.2f} "
      f"+- {np.std(noisy_err):.2f} dB/oct")
print(f"  {'atten_lim':>10} {'tilt err mean':>15} {'sd':>6} {'worst':>7} "
      f"{'weightP err':>12} {'6-12k noise red':>16}")
for lim in LIMS:
    a = np.array(res[lim])
    print(f"  {str(lim):>10} {a.mean():>+15.2f} {a.std():>6.2f} "
          f"{np.abs(a).max():>7.2f} {np.abs(a).mean() / 8.0:>12.3f} "
          f"{np.mean(noise_red[lim]):>+15.1f} dB")
