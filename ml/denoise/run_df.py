"""Offline DeepFilterNet enhancement + timing.

Usage: run_df.py IN.wav OUT.wav [atten_lim_db]
"""

import sys
import time
from pathlib import Path

import df_compat  # noqa: F401  (must precede `import df`)

import numpy as np
import soundfile as sf
import torch
from df.enhance import enhance, init_df

_MODEL = None


def get_model():
    global _MODEL
    if _MODEL is None:
        _MODEL = init_df(log_level="ERROR")
    return _MODEL


def denoise(x, fs, atten_lim_db=None):
    """x: mono float array at 48 kHz. Returns (enhanced, seconds_of_compute)."""
    model, state, _ = get_model()
    if fs != state.sr():
        raise ValueError(f"DeepFilterNet wants {state.sr()} Hz, got {fs}")
    t = torch.from_numpy(np.asarray(x, dtype=np.float32)).unsqueeze(0)
    t0 = time.perf_counter()
    y = enhance(model, state, t, atten_lim_db=atten_lim_db)
    dt = time.perf_counter() - t0
    return y.squeeze(0).numpy().astype(np.float64), dt


def main():
    src, dst = Path(sys.argv[1]), Path(sys.argv[2])
    atten = float(sys.argv[3]) if len(sys.argv) > 3 else None
    x, fs = sf.read(str(src), dtype="float64")
    if x.ndim > 1:
        x = x.mean(axis=1)
    y, dt = denoise(x, fs, atten_lim_db=atten)
    n = min(len(x), len(y))
    # float, not PCM_16: the suppressed output can land far below the 16-bit
    # noise floor and we must not confuse quantisation with the model.
    sf.write(str(dst), y[:n], fs, subtype="FLOAT")
    dur = len(x) / fs
    print(f"{src.name}: {dur:.1f}s audio, {dt:.2f}s compute, "
          f"RTF={dt / dur:.4f} ({dur / dt:.1f}x realtime), atten_lim={atten}")


if __name__ == "__main__":
    main()
