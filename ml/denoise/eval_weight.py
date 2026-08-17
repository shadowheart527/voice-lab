"""Does DeepFilterNet change the harmonic spectral roll-off (vocal weight)?

Two experiments:

1. weightpair-noisy.wav as supplied -- a synthetic sustained voice, 12 s heavy
   then 12 s light, over a constant noise floor. weightpair.wav is the same pair
   without noise, so it is the ground truth.

2. The realistic capture-cleanup case: real connected speech (TVL demo clips)
   plus a synthetic noise floor at known SNRs, cleaned by DeepFilterNet, tilt
   compared against the clean original. Experiment 1 cannot separate "denoiser
   changed the tilt" from "denoiser ate the sustained tone"; this one can,
   because connected speech is not something DeepFilterNet suppresses.
"""

import sys
from pathlib import Path

import numpy as np
import soundfile as sf

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from audiometrics import (band_rms_db, f0_track_acf, load_wav,  # noqa: E402
                          rms, rms_db, tilt_track)
from run_df import denoise  # noqa: E402

SCRATCH = Path("/tmp/claude-1000/-var-home-shadowheart527/"
               "04778bbc-32b9-4506-a8ec-d6dd51383461/scratchpad")
OUT = Path(__file__).resolve().parent / "out"
WEIGHT_SENS = 8.0  # weightP() spans 8 dB/oct over its 0..1 range


def half_tilt(x, fs, f0t, a, b):
    t, s, n = tilt_track(x, fs, f0t)
    m = (t >= a) & (t < b)
    if m.sum() < 3:
        return np.nan, np.nan, 0
    return float(np.median(s[m])), float(s[m].std()), float(n[m].mean())


def exp1():
    print("=" * 78)
    print("EXPERIMENT 1 -- weightpair-noisy.wav (sustained synthetic voice)")
    print("=" * 78)
    clean, fs = load_wav(SCRATCH / "weightpair.wav")
    noisy, _ = load_wav(SCRATCH / "weightpair-noisy.wav")
    df, _ = load_wav(OUT / "weightpair-noisy.df.wav")
    n = min(len(clean), len(noisy), len(df))
    clean, noisy, df = clean[:n], noisy[:n], df[:n]

    # f0 from the clean signal for every version, so the tilt comparison is
    # purely spectral.
    _, f0t = f0_track_acf(clean, fs, fmin=70.0, fmax=400.0)

    print(f"\nNoise floor (band RMS, dB) -- 5-8 kHz / 8-16 kHz, "
          f"regions with no voice harmonics")
    for lbl, sig in (("clean", clean), ("noisy", noisy), ("denoised", df)):
        h = sig[int(1 * fs):int(11 * fs)]
        l = sig[int(13 * fs):int(23 * fs)]
        print(f"  {lbl:<9} heavy-half {band_rms_db(h, fs, 5000, 8000):7.1f} /"
              f" {band_rms_db(h, fs, 8000, 16000):7.1f}   "
              f"light-half {band_rms_db(l, fs, 5000, 8000):7.1f} /"
              f" {band_rms_db(l, fs, 8000, 16000):7.1f}")

    print(f"\nLevel: clean {rms_db(clean):.2f} / noisy {rms_db(noisy):.2f} / "
          f"denoised {rms_db(df):.2f} dBFS")
    for lbl, a, b in (("heavy half (0.5-11.5 s)", 0.5, 11.5),
                      ("light half (12.5-23.5 s)", 12.5, 23.5)):
        seg = slice(int(a * fs), int(b * fs))
        print(f"  {lbl}: clean {rms_db(clean[seg]):.2f}, "
              f"noisy {rms_db(noisy[seg]):.2f}, denoised {rms_db(df[seg]):.2f} dBFS "
              f"(denoised - clean {rms_db(df[seg]) - rms_db(clean[seg]):+.2f} dB)")

    print(f"\nHarmonic spectral roll-off (engine Theil-Sen, dB/octave)")
    print(f"  {'half':<12} {'clean (truth)':>14} {'noisy':>10} {'denoised':>10} "
          f"{'noisy err':>10} {'denoi err':>10}")
    for lbl, a, b in (("heavy", 0.5, 11.5), ("light", 12.5, 23.5)):
        c, csd, cn = half_tilt(clean, fs, f0t, a, b)
        nz, nsd, nn = half_tilt(noisy, fs, f0t, a, b)
        d, dsd, dn = half_tilt(df, fs, f0t, a, b)
        print(f"  {lbl:<12} {c:>14.2f} {nz:>10.2f} {d:>10.2f} "
              f"{nz - c:>+10.2f} {d - c:>+10.2f}")
        print(f"  {'  sd / harmonics per frame':<12} "
              f"{csd:>8.2f}/{cn:.1f} {nsd:>6.2f}/{nn:.1f} {dsd:>6.2f}/{dn:.1f}")
    print(f"\n  In weightP() units (tilt/{WEIGHT_SENS:.0f}): 1 dB/oct = "
          f"{1 / WEIGHT_SENS:.3f} of the 0..1 vocal-weight scale.")


def exp2(snrs=(20.0, 10.0, 5.0), seed=527):
    print("\n" + "=" * 78)
    print("EXPERIMENT 2 -- real connected speech + known noise floor")
    print("=" * 78)
    rng = np.random.default_rng(seed)
    clips = sorted((SCRATCH / "tvldemos").glob("*.wav"))
    rows = {s: [] for s in snrs}
    raw = {s: [] for s in snrs}

    for src in clips:
        clean, fs = load_wav(src)
        _, f0t = f0_track_acf(clean, fs, fmin=70.0, fmax=400.0)
        base, _, _ = half_tilt(clean, fs, f0t, 0.0, 1e9)
        if not np.isfinite(base):
            continue
        # pink-ish room noise: white through a 1/f magnitude shaping
        w = rng.standard_normal(len(clean))
        W = np.fft.rfft(w)
        f = np.fft.rfftfreq(len(clean), 1 / fs)
        W *= 1.0 / np.sqrt(np.maximum(f, 20.0))
        noise = np.fft.irfft(W, len(clean))
        noise /= rms(noise)
        for snr in snrs:
            nz = clean + noise * rms(clean) * 10 ** (-snr / 20.0)
            t_nz, _, _ = half_tilt(nz, fs, f0t, 0.0, 1e9)
            enh, _ = denoise(nz, fs)
            enh = enh[:len(clean)]
            t_df, _, _ = half_tilt(enh, fs, f0t, 0.0, 1e9)
            rows[snr].append((t_nz - base, t_df - base))
            raw[snr].append((src.stem, base, t_nz, t_df))

    print(f"\nTilt error vs the clean original, dB/octave "
          f"(mean +- sd over {len(clips)} real clips)")
    print(f"  {'SNR':>6} {'noisy, no denoise':>22} {'DeepFilterNet':>18} "
          f"{'|err| worst DF':>16} {'weightP err (DF)':>18}")
    for snr in snrs:
        a = np.array(rows[snr])
        print(f"  {snr:>4.0f}dB {a[:, 0].mean():>+11.2f} +-{a[:, 0].std():>5.2f}"
              f"      {a[:, 1].mean():>+8.2f} +-{a[:, 1].std():>5.2f}"
              f"   {np.abs(a[:, 1]).max():>13.2f} "
              f"{np.abs(a[:, 1]).mean() / WEIGHT_SENS:>17.3f}")

    print(f"\nPer-clip detail (tilt in dB/oct: clean -> noisy -> DeepFilterNet)")
    for snr in snrs:
        print(f"  --- SNR {snr:.0f} dB ---")
        for name, b, tn, td in raw[snr]:
            print(f"    {name:<24} {b:>7.2f} -> {tn:>7.2f} -> {td:>7.2f}"
                  f"   (DF err {td - b:+.2f})")


if __name__ == "__main__":
    exp1()
    exp2()
