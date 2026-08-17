"""Robustness: accuracy is only half the question. The reason to consider a
neural tracker is that classical ones fall apart on quiet, breathy, noisy or
low-pitched input -- exactly the conditions early transfem voice work happens in.

Three stressors on human.wav (200 Hz vibrato vowel) and a masc-range variant:
  1. additive pink noise at descending SNR
  2. level: the whole signal scaled down (quiet speaker, far mic)
  3. a low-f0 signal (morph.wav's first seconds, ~115 Hz) where 40 ms windows
     hold only ~4.6 periods
"""

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from audiometrics import load_wav, rms  # noqa: E402
from eval_pitch import metrics  # noqa: E402
from groundtruth import GROUND_TRUTH  # noqa: E402
import trackers as T  # noqa: E402

SCRATCH = Path("/tmp/claude-1000/-var-home-shadowheart527/"
               "04778bbc-32b9-4506-a8ec-d6dd51383461/scratchpad")
HOP = 10.0

METHODS = [
    ("PESTO", lambda x, fs: T.track_pesto(x, fs, HOP)[:2]),
    ("engine MPM", lambda x, fs: T.track_engine(x, fs, HOP, 40.0, "mpm")),
    ("engine YIN", lambda x, fs: T.track_engine(x, fs, HOP, 40.0, "yin")),
    ("librosa pYIN", lambda x, fs: T.track_librosa(x, fs, HOP, "pyin")),
]


def pink(n, fs, rng):
    w = rng.standard_normal(n)
    W = np.fft.rfft(w)
    f = np.fft.rfftfreq(n, 1 / fs)
    W *= 1.0 / np.sqrt(np.maximum(f, 20.0))
    y = np.fft.irfft(W, n)
    return y / rms(y)


def run(tag, x, fs, truth, dur):
    out = {}
    for name, fn in METHODS:
        try:
            t, est = fn(x, fs)
            m = metrics(np.asarray(t), np.asarray(est), truth, fs, 40.0, dur)
        except Exception as e:  # noqa: BLE001
            m = dict(med=np.nan, rpa=0.0, gross=1.0, oct=1.0, jitter=np.nan,
                     voiced=0.0, err=str(e)[:40])
        out[name] = m
    return out


def table(title, cases):
    print("\n" + "=" * 92)
    print(title)
    print("=" * 92)
    print(f"  {'condition':<16}{'tracker':<15}{'med|err|':>9}{'RPA50':>8}"
          f"{'gross':>8}{'oct':>7}{'jitter':>8}{'voiced':>8}")
    for cond, res in cases:
        for name, m in res.items():
            print(f"  {cond:<16}{name:<15}{m['med']:>9.1f}{m['rpa'] * 100:>7.1f}%"
                  f"{m['gross'] * 100:>7.1f}%{m['oct'] * 100:>6.1f}%"
                  f"{m['jitter']:>8.1f}{m['voiced'] * 100:>7.1f}%")
        print()


if __name__ == "__main__":
    rng = np.random.default_rng(527)

    # --- 1. noise -----------------------------------------------------------
    x, fs = load_wav(SCRATCH / "human.wav")
    x = x[:int(15 * fs)]
    truth = GROUND_TRUTH["human"]()[:len(x)]
    dur = len(x) / fs
    n = pink(len(x), fs, rng)
    cases = [("clean", run("clean", x, fs, truth, dur))]
    for snr in (20, 10, 5, 0):
        y = x + n * rms(x) * 10 ** (-snr / 20.0)
        cases.append((f"pink SNR {snr} dB", run("n", y, fs, truth, dur)))
    table("1. human.wav (200 Hz vibrato vowel) + pink noise", cases)

    # --- 2. level -----------------------------------------------------------
    cases = []
    for att in (0, 20, 40, 60):
        y = x * 10 ** (-att / 20.0)
        cases.append((f"-{att} dB level", run("l", y, fs, truth, dur)))
    table("2. human.wav attenuated (quiet speaker / distant mic)", cases)

    # --- 3. low f0 ----------------------------------------------------------
    xm, fs = load_wav(SCRATCH / "morph.wav")
    tm = GROUND_TRUTH["morph"]()
    seg = slice(0, int(10 * fs))          # ~115-125 Hz
    seg2 = slice(int(35 * fs), int(45 * fs))  # ~195-215 Hz
    cases = [
        ("morph 0-10 s ~118Hz", run("lo", xm[seg], fs, tm[seg], 10.0)),
        ("morph 35-45 s ~205Hz", run("hi", xm[seg2], fs, tm[seg2], 10.0)),
    ]
    nseg = pink(int(10 * fs), fs, rng)
    y = xm[seg] + nseg * rms(xm[seg]) * 10 ** (-10 / 20.0)
    cases.append(("~118Hz SNR 10dB", run("lon", y, fs, tm[seg], 10.0)))
    table("3. low f0 vs high f0 (40 ms window holds ~4.6 vs ~8 periods)", cases)
