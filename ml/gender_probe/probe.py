#!/usr/bin/env python3
"""Perceived-gender probe: a continuous 0..1 read of how a voice is likely to be gendered.

The method is the one from "Voice Passing" (arXiv:2404.15176): take a binary
gender classifier trained on gender-balanced data, run it over short overlapping
windows, and treat the averaged posterior as a continuous femininity estimate
rather than a class decision. A voice that every window calls female sits near 1;
one that flips window to window sits near 0.5; the in-between values are the
useful part.

0 = reads masculine to the model, 1 = reads feminine to the model.

This is a model's read, calibrated to VoxCeleb2 (cis, binary-labelled, mostly
celebrity interview audio). It is not a verdict on anyone's voice. See README.md.

CLI:
    python -m gender_probe.probe clip.wav [more.wav ...]
    python -m gender_probe.probe --json clip.wav
"""

from __future__ import annotations

import argparse
import dataclasses
import json
import pathlib
from fractions import Fraction
from typing import Iterable, Sequence

import numpy as np

SAMPLE_RATE = 16000
DEFAULT_ONNX = pathlib.Path(__file__).resolve().parent.parent / "models" / "ecapa_gender_fp32.onnx"

# Window/hop defaults. 1.0 s is long enough for the attentive-pooling statistics
# to settle and short enough to track within-utterance variation; 0.25 s hop
# gives 4 reads/second, which is comfortable for a live meter.
DEFAULT_WINDOW_S = 1.0
DEFAULT_HOP_S = 0.25

# Windows quieter than this (dBFS RMS) are dropped: silence and room tone carry
# no gender information but do get confidently classified, which drags the mean.
DEFAULT_MIN_RMS_DBFS = -45.0


# --------------------------------------------------------------------------- #
# audio helpers
# --------------------------------------------------------------------------- #

def _kaiser_sinc(num_taps: int, cutoff: float, beta: float = 8.6) -> np.ndarray:
    n = np.arange(num_taps) - (num_taps - 1) / 2.0
    h = 2 * cutoff * np.sinc(2 * cutoff * n)
    return (h * np.kaiser(num_taps, beta)).astype(np.float64)


def resample_to_16k(x: np.ndarray, sr: int) -> np.ndarray:
    """Rational-factor resample with a Kaiser-windowed sinc lowpass.

    Dependency-free; accurate enough that the log-mel front end cannot tell the
    difference from a proper polyphase resampler at these ratios.
    """
    if sr == SAMPLE_RATE:
        return x.astype(np.float32, copy=False)

    ratio = Fraction(SAMPLE_RATE, int(sr)).limit_denominator(2000)
    up, down = ratio.numerator, ratio.denominator

    upsampled = np.zeros(len(x) * up, dtype=np.float64)
    upsampled[::up] = np.asarray(x, dtype=np.float64) * up

    cutoff = 0.5 / max(up, down)
    half = int(np.ceil(16 * max(up, down)))
    taps = _kaiser_sinc(2 * half + 1, cutoff)

    filtered = np.convolve(upsampled, taps, mode="same")
    return filtered[::down].astype(np.float32)


def load_wav(path: str | pathlib.Path) -> tuple[np.ndarray, int]:
    """Read any soundfile-supported file as mono float32 at 16 kHz."""
    import soundfile as sf

    data, sr = sf.read(str(path), dtype="float32", always_2d=True)
    mono = data.mean(axis=1)
    return resample_to_16k(mono, sr), SAMPLE_RATE


def _rms_dbfs(x: np.ndarray) -> float:
    rms = float(np.sqrt(np.mean(np.square(x, dtype=np.float64)) + 1e-20))
    return 20.0 * np.log10(max(rms, 1e-10))


# --------------------------------------------------------------------------- #
# results
# --------------------------------------------------------------------------- #

@dataclasses.dataclass
class ProbeResult:
    """A femininity read plus everything needed to judge how much to trust it."""

    score: float                # mean posterior over kept windows, 0..1
    median: float               # median posterior; robust to a few odd windows
    std: float                  # spread across windows: high = inconsistent voice
    n_windows: int              # windows actually scored
    n_dropped: int              # windows dropped as too quiet
    duration_s: float
    window_scores: np.ndarray = dataclasses.field(repr=False)
    window_times_s: np.ndarray = dataclasses.field(repr=False)

    @property
    def label(self) -> str:
        """Coarse bucket. Deliberately blunt; the number is the real output."""
        if self.score >= 0.7:
            return "reads feminine"
        if self.score <= 0.3:
            return "reads masculine"
        return "ambiguous"

    def to_dict(self) -> dict:
        return {
            "score": round(self.score, 4),
            "median": round(self.median, 4),
            "std": round(self.std, 4),
            "n_windows": self.n_windows,
            "n_dropped": self.n_dropped,
            "duration_s": round(self.duration_s, 3),
            "label": self.label,
            "window_scores": [round(float(v), 4) for v in self.window_scores],
            "window_times_s": [round(float(v), 3) for v in self.window_times_s],
        }


# --------------------------------------------------------------------------- #
# probe
# --------------------------------------------------------------------------- #

class GenderProbe:
    """Windowed inference over the ECAPA gender classifier.

    Backend is onnxruntime by default; pass backend="torch" to run the PyTorch
    module instead (useful when checking an export, slower to start).
    """

    def __init__(
        self,
        onnx_path: str | pathlib.Path | None = None,
        *,
        backend: str = "onnx",
        providers: Sequence[str] | None = None,
        window_s: float = DEFAULT_WINDOW_S,
        hop_s: float = DEFAULT_HOP_S,
        min_rms_dbfs: float = DEFAULT_MIN_RMS_DBFS,
        batch_size: int = 1,
    ) -> None:
        # batch_size defaults to 1 deliberately. The int8 graph uses dynamic
        # quantisation, which derives activation scales per tensor over whatever
        # is in the batch, so a window's score depends on its neighbours: on the
        # reference clips, batching 16 shifts clip means by up to ~0.01 against
        # single-window inference. One window at a time is both reproducible and
        # what a browser doing live analysis will do, and for this graph it is
        # also the fastest option. Raise it only for offline fp32 work.
        self.window = int(round(window_s * SAMPLE_RATE))
        self.hop = int(round(hop_s * SAMPLE_RATE))
        self.min_rms_dbfs = min_rms_dbfs
        self.batch_size = batch_size
        self.backend = backend

        if backend == "onnx":
            import onnxruntime as ort

            path = pathlib.Path(onnx_path or DEFAULT_ONNX)
            if not path.exists():
                raise FileNotFoundError(
                    f"{path} not found; run `python -m gender_probe.fetch_model` first"
                )
            opts = ort.SessionOptions()
            opts.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
            self._sess = ort.InferenceSession(
                str(path), opts,
                providers=list(providers or ["CPUExecutionProvider"]),
            )
            self._input = self._sess.get_inputs()[0].name
        elif backend == "torch":
            import torch
            from safetensors.torch import load_file

            from .ecapa_gender import ECAPA_gender

            weights = pathlib.Path(onnx_path or DEFAULT_ONNX).with_name(
                "ecapa_gender.safetensors")
            model = ECAPA_gender(C=1024)
            model.load_state_dict(load_file(str(weights)), strict=False)
            model.eval()
            self._torch = torch
            self._model = model
        else:
            raise ValueError(f"unknown backend {backend!r}")

    # -- raw model call ----------------------------------------------------- #

    def _logits(self, batch: np.ndarray) -> np.ndarray:
        if self.backend == "onnx":
            return self._sess.run(None, {self._input: batch.astype(np.float32)})[0]
        with self._torch.no_grad():
            tensor = self._torch.from_numpy(np.ascontiguousarray(batch, dtype=np.float32))
            return self._model(tensor).numpy()

    # -- windowing ---------------------------------------------------------- #

    def _frames(self, samples: np.ndarray) -> tuple[np.ndarray, np.ndarray, int]:
        """Split into windows, dropping quiet ones. Returns (frames, starts, n_dropped)."""
        n = len(samples)
        if n < self.window:
            # Pad short input by reflection rather than refusing to score it.
            pad = self.window - n
            samples = np.pad(samples, (0, pad), mode="reflect" if n > 1 else "constant")
            n = len(samples)

        starts = np.arange(0, n - self.window + 1, self.hop, dtype=np.int64)
        kept, kept_starts, dropped = [], [], 0
        for s in starts:
            chunk = samples[s:s + self.window]
            if _rms_dbfs(chunk) < self.min_rms_dbfs:
                dropped += 1
                continue
            kept.append(chunk)
            kept_starts.append(s)

        if not kept:
            # Everything was below the gate: score it anyway, flagged by n_dropped,
            # rather than returning nothing.
            kept = [samples[s:s + self.window] for s in starts]
            kept_starts = list(starts)

        return np.stack(kept).astype(np.float32), np.asarray(kept_starts), dropped

    def score_windows(self, samples: np.ndarray) -> tuple[np.ndarray, np.ndarray, int]:
        """-> (femininity per window 0..1, window start times in seconds, n_dropped)."""
        frames, starts, dropped = self._frames(np.asarray(samples, dtype=np.float32))

        probs = []
        for i in range(0, len(frames), self.batch_size):
            logits = self._logits(frames[i:i + self.batch_size])
            logits = logits - logits.max(axis=1, keepdims=True)
            exp = np.exp(logits)
            probs.append(exp[:, 1] / exp.sum(axis=1))
        return np.concatenate(probs), starts / SAMPLE_RATE, dropped

    # -- public API --------------------------------------------------------- #

    def score(self, samples: np.ndarray, sr: int = SAMPLE_RATE) -> ProbeResult:
        samples = resample_to_16k(np.asarray(samples, dtype=np.float32), sr)
        probs, times, dropped = self.score_windows(samples)
        return ProbeResult(
            score=float(probs.mean()),
            median=float(np.median(probs)),
            std=float(probs.std()),
            n_windows=int(len(probs)),
            n_dropped=int(dropped),
            duration_s=len(samples) / SAMPLE_RATE,
            window_scores=probs,
            window_times_s=times,
        )

    def score_file(self, path: str | pathlib.Path) -> ProbeResult:
        samples, sr = load_wav(path)
        return self.score(samples, sr)


class StreamingGenderProbe:
    """Push-in, read-out wrapper for live audio.

    Feed arbitrary-length chunks; every time a full window has accumulated it is
    scored and folded into an exponential moving average. `value` is the current
    smoothed femininity read, or None until the first window lands.
    """

    def __init__(self, probe: GenderProbe | None = None, *, ema_alpha: float = 0.2,
                 **probe_kwargs) -> None:
        self.probe = probe or GenderProbe(**probe_kwargs)
        self.ema_alpha = ema_alpha
        self._buf = np.zeros(0, dtype=np.float32)
        self.value: float | None = None
        self.last_raw: float | None = None
        self.history: list[float] = []

    def reset(self) -> None:
        self._buf = np.zeros(0, dtype=np.float32)
        self.value = None
        self.last_raw = None
        self.history.clear()

    def push(self, chunk: np.ndarray, sr: int = SAMPLE_RATE) -> float | None:
        """Append audio; return the smoothed score if it advanced, else None."""
        self._buf = np.concatenate([self._buf, resample_to_16k(
            np.asarray(chunk, dtype=np.float32), sr)])

        win, hop = self.probe.window, self.probe.hop
        updated = False
        while len(self._buf) >= win:
            frame = self._buf[:win]
            self._buf = self._buf[hop:]
            if _rms_dbfs(frame) < self.probe.min_rms_dbfs:
                continue  # hold the last value through silence
            probs, _, _ = self.probe.score_windows(frame)
            raw = float(probs.mean())
            self.last_raw = raw
            self.history.append(raw)
            self.value = raw if self.value is None else (
                self.ema_alpha * raw + (1 - self.ema_alpha) * self.value)
            updated = True

        return self.value if updated else None


# --------------------------------------------------------------------------- #
# CLI
# --------------------------------------------------------------------------- #

def _main(argv: Iterable[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+", type=pathlib.Path)
    ap.add_argument("--model", type=pathlib.Path, default=None)
    ap.add_argument("--window", type=float, default=DEFAULT_WINDOW_S)
    ap.add_argument("--hop", type=float, default=DEFAULT_HOP_S)
    ap.add_argument("--min-rms-dbfs", type=float, default=DEFAULT_MIN_RMS_DBFS)
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args(list(argv) if argv is not None else None)

    probe = GenderProbe(args.model, window_s=args.window, hop_s=args.hop,
                        min_rms_dbfs=args.min_rms_dbfs)

    out = {}
    for path in args.files:
        result = probe.score_file(path)
        out[str(path)] = result.to_dict()
        if not args.json:
            print(f"{path.name:<20} {result.score:6.3f}  "
                  f"median {result.median:6.3f}  sd {result.std:5.3f}  "
                  f"n={result.n_windows:3d}  {result.label}")
    if args.json:
        print(json.dumps(out, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(_main())
