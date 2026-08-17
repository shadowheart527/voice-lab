#!/usr/bin/env python3
"""Score a directory of reference clips and report separation against known labels.

Used to decide whether the probe is fit to ship. Prints a per-clip table, ROC AUC,
and the best single-threshold accuracy over the labelled subset.

    python -m gender_probe.validate /path/to/refclips-dn
"""

from __future__ import annotations

import argparse
import pathlib
import sys

import numpy as np

if __package__ in (None, ""):
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
    from gender_probe.probe import GenderProbe
else:
    from .probe import GenderProbe

# Ground truth for the voice-lab reference set: how each clip reads to listeners,
# per the source project. Clips absent from both lists are scored but excluded
# from the AUC.
MASCULINE = ["chuck", "david", "bob", "steve", "chris", "leonard", "aiden"]
FEMININE = ["lucy", "cheryl", "quinn", "kristen", "morgan", "wina", "zoe",
            "luna", "jenn", "ashley", "lesley"]


def roc_auc(labels: np.ndarray, scores: np.ndarray) -> float:
    """AUC via the Mann-Whitney U identity, with ties counted as half."""
    pos, neg = scores[labels == 1], scores[labels == 0]
    if len(pos) == 0 or len(neg) == 0:
        return float("nan")
    diff = pos[:, None] - neg[None, :]
    return float(((diff > 0).sum() + 0.5 * (diff == 0).sum()) / (len(pos) * len(neg)))


def best_threshold(labels: np.ndarray, scores: np.ndarray) -> tuple[float, float]:
    order = np.unique(scores)
    cuts = np.concatenate([[order[0] - 1e-6], (order[:-1] + order[1:]) / 2,
                           [order[-1] + 1e-6]])
    accs = [( (scores >= c).astype(int) == labels).mean() for c in cuts]
    i = int(np.argmax(accs))
    return float(cuts[i]), float(accs[i])


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("clipdir", type=pathlib.Path)
    ap.add_argument("--model", type=pathlib.Path, default=None)
    ap.add_argument("--window", type=float, default=1.0)
    ap.add_argument("--hop", type=float, default=0.25)
    args = ap.parse_args(argv)

    probe = GenderProbe(args.model, window_s=args.window, hop_s=args.hop)

    rows = []
    for wav in sorted(args.clipdir.glob("*.wav")):
        name = wav.stem
        truth = 1 if name in FEMININE else 0 if name in MASCULINE else None
        r = probe.score_file(wav)
        rows.append((name, truth, r))

    print(f"window {args.window}s  hop {args.hop}s  model {probe.backend}")
    print(f"{'clip':<12} {'truth':<6} {'score':>7} {'median':>7} {'sd':>6} "
          f"{'n':>4} {'dur':>6}")
    print("-" * 54)
    for name, truth, r in sorted(rows, key=lambda t: t[2].score):
        t = "-" if truth is None else ("fem" if truth else "masc")
        print(f"{name:<12} {t:<6} {r.score:7.3f} {r.median:7.3f} {r.std:6.3f} "
              f"{r.n_windows:4d} {r.duration_s:6.1f}")

    labelled = [(t, r.score) for _, t, r in rows if t is not None]
    labels = np.array([t for t, _ in labelled])
    scores = np.array([s for _, s in labelled])

    auc = roc_auc(labels, scores)
    thr, acc = best_threshold(labels, scores)
    masc_max = scores[labels == 0].max()
    fem_min = scores[labels == 1].min()

    print("-" * 54)
    print(f"labelled clips     : {len(labelled)}  "
          f"({int((labels == 0).sum())} masc / {int((labels == 1).sum())} fem)")
    print(f"ROC AUC            : {auc:.4f}")
    print(f"best threshold     : {thr:.3f} -> accuracy {acc:.4f}")
    print(f"highest masculine  : {masc_max:.4f}")
    print(f"lowest feminine    : {fem_min:.4f}")
    print(f"separation margin  : {fem_min - masc_max:+.4f} "
          f"({'clean split' if fem_min > masc_max else 'groups overlap'})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
