"""PESTO vs the engine's classical trackers, against exact synthetic ground truth.

Metrics, all on the same 10 ms grid:
  med|err|   median absolute error, cents
  p95        95th percentile absolute error, cents
  RPA50      raw pitch accuracy: fraction of frames within 50 cents
  gross      fraction of frames off by more than 100 cents
  oct        fraction of frames off by more than 600 cents (octave-class slips)
  jitter     sd of (frame-to-frame estimate step minus the TRUE step), cents.
             This is the "bouncing dot" number: motion the tracker invents.
  voiced     fraction of frames the tracker declared voiced (truth: 100%)
"""

import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from audiometrics import load_wav  # noqa: E402
from groundtruth import GROUND_TRUTH, cents, window_truth  # noqa: E402
import trackers as T  # noqa: E402

SCRATCH = Path("/tmp/claude-1000/-var-home-shadowheart527/"
               "04778bbc-32b9-4506-a8ec-d6dd51383461/scratchpad")
HOP_MS = 10.0
EDGE = 0.5  # seconds trimmed at each end (fades, tracker warm-up)


def metrics(t, est, truth_samples, fs, win_ms, dur):
    ref = window_truth(truth_samples, t, fs, win_ms)
    keep = (t > EDGE) & (t < dur - EDGE) & np.isfinite(ref)
    t, est, ref = t[keep], est[keep], ref[keep]
    voiced = np.isfinite(est)
    frac_voiced = float(voiced.mean()) if len(voiced) else 0.0

    e = np.full(len(est), np.nan)
    e[voiced] = cents(est[voiced], ref[voiced])
    ae = np.abs(e[voiced])
    if len(ae) == 0:
        return dict(med=np.nan, p95=np.nan, rpa=0.0, gross=1.0, oct=1.0,
                    jitter=np.nan, voiced=0.0)

    # jitter: estimate step minus true step, over consecutive voiced pairs
    est_c = np.where(voiced, 1200 * np.log2(np.where(voiced, est, 1.0)), np.nan)
    ref_c = 1200 * np.log2(ref)
    d_est = np.diff(est_c)
    d_ref = np.diff(ref_c)
    ok = np.isfinite(d_est)
    jit = float(np.std((d_est - d_ref)[ok])) if ok.sum() > 2 else np.nan

    return dict(
        med=float(np.median(ae)),
        p95=float(np.percentile(ae, 95)),
        rpa=float((ae < 50).mean()),
        gross=float((ae > 100).mean()),
        oct=float((ae > 600).mean()),
        jitter=jit,
        voiced=frac_voiced,
    )


def run_all(name):
    x, fs = load_wav(SCRATCH / f"{name}.wav")
    dur = len(x) / fs
    truth = GROUND_TRUTH[name]()
    truth = truth[:len(x)] if len(truth) >= len(x) else np.pad(
        truth, (0, len(x) - len(truth)), mode="edge")

    runs = []

    def timed(label, fn, win_ms):
        t0 = time.perf_counter()
        out = fn()
        dt = time.perf_counter() - t0
        t, est = out[0], out[1]
        m = metrics(np.asarray(t), np.asarray(est), truth, fs, win_ms, dur)
        m["label"] = label
        m["sec"] = dt
        m["rtf"] = dt / dur
        m["nframes"] = len(t)
        runs.append(m)

    # Only the mir-1k_g7 checkpoint loads under pesto 2.0.1; the bundled legacy
    # `mir-1k` checkpoint raises a state_dict mismatch against the 2.x model.
    timed("PESTO mir-1k_g7", lambda: T.track_pesto(x, fs, HOP_MS, "mir-1k_g7"), 40.0)
    timed("engine MPM (40ms)",
          lambda: T.track_engine(x, fs, HOP_MS, 40.0, "mpm"), 40.0)
    timed("engine YIN (40ms)",
          lambda: T.track_engine(x, fs, HOP_MS, 40.0, "yin"), 40.0)
    timed("engine YIN +interp",
          lambda: T.track_engine(x, fs, HOP_MS, 40.0, "yin_interp"), 40.0)
    timed("librosa YIN (43ms)",
          lambda: T.track_librosa(x, fs, HOP_MS, "yin"), 42.7)
    timed("librosa pYIN (43ms)",
          lambda: T.track_librosa(x, fs, HOP_MS, "pyin"), 42.7)

    print(f"\n{'=' * 100}")
    print(f"{name}.wav   {dur:.0f} s @ {fs} Hz   "
          f"truth {truth.min():.1f}-{truth.max():.1f} Hz   hop {HOP_MS:.0f} ms")
    print("=" * 100)
    hdr = (f"{'tracker':<22}{'med|err|':>9}{'p95':>8}{'RPA50':>8}{'gross':>8}"
           f"{'oct':>7}{'jitter':>8}{'voiced':>8}{'RTF':>8}")
    print(hdr)
    print("-" * len(hdr))
    for m in runs:
        print(f"{m['label']:<22}{m['med']:>9.1f}{m['p95']:>8.1f}"
              f"{m['rpa'] * 100:>7.1f}%{m['gross'] * 100:>7.1f}%"
              f"{m['oct'] * 100:>6.1f}%{m['jitter']:>8.1f}"
              f"{m['voiced'] * 100:>7.1f}%{m['rtf']:>8.3f}")
    print("  (cents; RTF = compute seconds per audio second, batch, CPU)")
    return runs


if __name__ == "__main__":
    for n in ("steady", "human", "morph"):
        run_all(n)
