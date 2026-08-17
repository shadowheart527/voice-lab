"""Latency: what it would actually cost to put PESTO in the real-time path.

Two separate things, both of which matter and are often conflated:

  algorithmic lookahead -- how much FUTURE audio the estimator needs before it
      can emit a value for time t. Irreducible; sets the floor on how stale the
      on-screen dot is.
  compute per frame -- wall-clock to produce one estimate, single-threaded,
      measured as if called once per hop from an audio callback.
"""

import sys
import time
from pathlib import Path

import numpy as np
import torch

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from audiometrics import load_wav  # noqa: E402
import trackers as T  # noqa: E402

SCRATCH = Path("/tmp/claude-1000/-var-home-shadowheart527/"
               "04778bbc-32b9-4506-a8ec-d6dd51383461/scratchpad")
FS = 48000
HOP_MS = 10.0


def percentiles(v):
    v = np.asarray(v) * 1000.0
    return v.mean(), np.median(v), np.percentile(v, 95), v.max()


def bench_pesto(x, n_calls=200, threads=1):
    torch.set_num_threads(threads)
    model = T._PESTO_CACHE.setdefault(
        ("mir-1k_g7", HOP_MS, FS),
        __import__("pesto").load_model("mir-1k_g7", step_size=HOP_MS,
                                       sampling_rate=FS))
    win = 8192  # PESTO's CQT kernel length
    times = []
    for i in range(n_calls):
        off = (i * 480) % (len(x) - win)
        buf = torch.from_numpy(x[off:off + win].astype(np.float32))
        t0 = time.perf_counter()
        with torch.inference_mode():
            model(buf, convert_to_freq=True, return_activations=False)
        times.append(time.perf_counter() - t0)
    return times


def bench_classical(x, fn, win_samples, n_calls=200):
    times = []
    for i in range(n_calls):
        off = (i * 480) % (len(x) - win_samples)
        buf = x[off:off + win_samples]
        t0 = time.perf_counter()
        fn(buf, FS)
        times.append(time.perf_counter() - t0)
    return times


if __name__ == "__main__":
    x, fs = load_wav(SCRATCH / "human.wav")
    assert fs == FS

    print("=" * 78)
    print("Algorithmic lookahead (irreducible)")
    print("=" * 78)
    print(f"  PESTO           CQT Conv1d kernel 8192 @ 48 kHz, centre-padded")
    print(f"                  window {8192 / FS * 1000:.1f} ms, "
          f"lookahead {4096 / FS * 1000:.1f} ms")
    print(f"                  the encoder is frame-independent (Resnet1d over the")
    print(f"                  frequency axis only), so there is NO extra temporal")
    print(f"                  context beyond the CQT window -- it can stream.")
    print(f"  engine MPM/YIN  40 ms window (Config default), "
          f"lookahead {40 / 2:.1f} ms")

    print("\n" + "=" * 78)
    print("Compute per frame, single thread (200 calls, ms)")
    print("=" * 78)
    print(f"  {'estimator':<26}{'mean':>8}{'median':>9}{'p95':>8}{'max':>8}"
          f"{'x realtime@10ms hop':>22}")

    rows = []
    tp = bench_pesto(x, threads=1)
    rows.append(("PESTO (1 thread)", tp))
    torch.set_num_threads(1)
    nwin40 = int(0.040 * FS)
    rows.append(("engine MPM (40 ms)",
                 bench_classical(x, T.mpm_solve, nwin40)))
    rows.append(("engine YIN (40 ms)",
                 bench_classical(x, T.yin_solve, nwin40)))

    for label, v in rows:
        m, md, p95, mx = percentiles(v)
        print(f"  {label:<26}{m:>8.2f}{md:>9.2f}{p95:>8.2f}{mx:>8.2f}"
              f"{HOP_MS / m:>21.1f}x")

    tp8 = bench_pesto(x, threads=8)
    m, md, p95, mx = percentiles(tp8)
    print(f"  {'PESTO (8 threads)':<26}{m:>8.2f}{md:>9.2f}{p95:>8.2f}{mx:>8.2f}"
          f"{HOP_MS / m:>21.1f}x")
    print("\n  NOTE: the python MPM/YIN ports are numpy, with a python loop in the")
    print("  YIN cumulative-mean; the engine's C++ versions are faster still, so")
    print("  the classical numbers here are an upper bound on their cost.")
