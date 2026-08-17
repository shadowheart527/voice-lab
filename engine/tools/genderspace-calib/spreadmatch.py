#!/usr/bin/env python3
"""Spread-matching dispersion correction on top of the M3s soft-vowel predictor.

Both trackers under-disperse relative to the official numbers (their per-clip
medians span ~0.37-0.50 where the official ones span 0.23-0.69). Least-squares
fits of that correction get diluted by noise in the predicted medians, so match
robust spreads directly instead:

    a = spread(official medians) / spread(predicted training medians)
    b = center(official) - a * center(predicted)
    r' = clip01(a * r + b)

Usage: spreadmatch.py <dataset.tsv> <refclips-dir>
"""
import json, math, os, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.expanduser("~/git/gender-voice-visualization")
STATS = json.load(open(f"{REPO}/stats.json"))
W_SITE = [0.7321428571428571, 0.26785714285714285, 0.0]
PHONES_ALL = [k for k in STATS if STATS[k] and len(STATS[k]) >= 3]

rows = []
with open(sys.argv[1]) as f:
    hdr = next(f).rstrip("\n").split("\t")
    for line in f:
        c = dict(zip(hdr, line.rstrip("\n").split("\t")))
        rows.append({k: (c[k] if k in ("clip", "phoneme", "word") else
                         (float(c[k]) if c[k] else None)) for k in hdr})

official = {}
with open(sys.argv[2] + "/summary.tsv") as f:
    next(f)
    for line in f:
        c = line.split("\t")
        official[c[0]] = float(c[5])

clips = sorted(set(r["clip"] for r in rows))

def fit_unitmap(sub):
    maps = []
    for i in (1, 2):
        ok = [r for r in sub if r[f"site_f{i}"]]
        x = np.array([r[f"trk_f{i}"] for r in ok])
        y = np.array([r[f"site_f{i}"] for r in ok])
        A = np.vstack([x, np.ones(len(x))]).T
        w = np.ones(len(y))
        beta = None
        for _ in range(6):
            Aw = A * w[:, None]
            beta = np.linalg.lstsq(Aw.T @ A, Aw.T @ y, rcond=None)[0]
            resid = np.abs(A @ beta - y)
            s = max(np.median(resid) * 1.4826, 1e-6)
            w = np.clip(1.5 * s / np.maximum(resid, 1e-9), None, 1.0)
        maps.append(beta)
    return maps

def site_res(f1, f2, ph):
    st = STATS.get(ph)
    z1 = (f1 - st[1]["mean"]) / st[1]["stdev"]
    z2 = (f2 - st[2]["mean"]) / st[2]["stdev"]
    return min(1.0, max(0.0, (W_SITE[0] * z1 + W_SITE[1] * z2) / 3 + 0.5))

def m3s(sub, maps):
    out = []
    for r in sub:
        f1 = maps[0][0] * r["trk_f1"] + maps[0][1]
        f2 = maps[1][0] * r["trk_f2"] + maps[1][1]
        num = den = 0.0
        for ph in PHONES_ALL:
            st = STATS[ph]
            d = (((f1 - st[1]["mean"]) / st[1]["stdev"]) ** 2
                 + ((f2 - st[2]["mean"]) / st[2]["stdev"]) ** 2)
            w = math.exp(-0.5 * d)
            if w < 1e-9:
                continue
            num += w * site_res(f1, f2, ph)
            den += w
        out.append(num / den if den > 0 else 0.5)
    return np.array(out)

def spread(v):
    q = np.percentile(v, [10, 90])
    return q[1] - q[0]

# LOCO evaluation with per-fold spread matching
errs, word_abs, phone_abs = {}, [], []
for c in clips:
    tr = [r for r in rows if r["clip"] != c]
    te = [r for r in rows if r["clip"] == c]
    maps = fit_unitmap(tr)

    tm, to = [], []
    for c2 in clips:
        if c2 == c:
            continue
        sub = [r for r in tr if r["clip"] == c2]
        tm.append(float(np.median(m3s(sub, maps))))
        to.append(official[c2])
    tm, to = np.array(tm), np.array(to)
    a = spread(to) / max(spread(tm), 1e-6)
    b = float(np.median(to)) - a * float(np.median(tm))

    pred = np.clip(a * m3s(te, maps) + b, 0, 1)
    errs[c] = float(np.median(pred)) - official[c]
    y = np.array([r["site_res"] for r in te])
    phone_abs.extend(np.abs(pred - y))
    wkey = {}
    for i, r in enumerate(te):
        wkey.setdefault((r["word"], round(r["mid"], 1)), []).append(i)
    word_abs.extend(abs(float(np.mean(pred[ix])) - float(np.mean(y[ix]))) for ix in wkey.values())

e = np.array(list(errs.values()))
print(f"spread-matched M3s (LOCO): clipMedMAE {np.mean(np.abs(e)):.4f}  worst {np.max(np.abs(e)):.4f}  "
      f"bias {np.mean(e):+.4f}  phoneMAE {np.mean(phone_abs):.4f}  wordMAE {np.mean(word_abs):.4f}")
print(f"typical fold: a={a:.3f} b={b:+.3f}")
for c in sorted(errs, key=lambda k: -abs(errs[k]))[:8]:
    print(f"  {c:10s} err {errs[c]:+.3f}  official {official[c]:.3f}")
