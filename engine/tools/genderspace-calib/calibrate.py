#!/usr/bin/env python3
"""Calibrate InFormant's live resonance against acousticgender.space's official numbers.

Ground truth: the site's reference clips, each carrying audio plus the official
pipeline's per-phoneme praat measurements and phoneme-normalized resonance
scores. InFormant measured the same audio (raw 50 Hz tracks via
IF_DEBUG_TRACKS). This script aligns the two timelines per clip via pitch
cross-correlation, pairs per-phone values, then fits and cross-validates
candidate mappings from InFormant formants to the site's resonance scale:

  M0  current meter logistic (baseline, expected to read high)
  M1  affine: r = clip(0..1, a1*f1 + a2*f2 + a3*f3 + b)
  M3s soft-vowel: map tracker formants to praat units (per-formant linear),
      soft-assign a phoneme from stats.json in (F1,F2) space, apply the site's
      exact z-score formula under that assignment
  M3o oracle variant of M3 using the true phoneme label (diagnostic upper
      bound, not deployable live)

Usage: calibrate.py <refclips-dir>
"""
import json, math, os, sys
import numpy as np

CLIPDIR = sys.argv[1]
REPO = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "..", "genderspace")
STATS = json.load(open(f"{REPO}/stats.json"))
W_SITE = [0.7321428571428571, 0.26785714285714285, 0.0]

# ---------- parsing ----------

def parse_tracks(path):
    # The app prefixes every stdout line with a timestamp, so search for the
    # probe markers rather than anchoring at the line start.
    tp, tf = [], []
    for line in open(path, errors="replace"):
        i = line.find("TRKP],")
        if i >= 0:
            c = line[i:].strip().split(",")
            if len(c) >= 3:
                try:
                    v = (float(c[1]), float(c[2]))
                    if all(math.isfinite(x) for x in v):
                        tp.append(v)
                except ValueError: pass
            continue
        i = line.find("TRKF],")
        if i >= 0:
            c = line[i:].strip().split(",")
            if len(c) >= 5:
                try:
                    v = (float(c[1]), float(c[2]), float(c[3]), float(c[4]))
                    if all(math.isfinite(x) for x in v) and 0 < v[1] < 8000:
                        tf.append(v)
                except ValueError: pass
    return np.array(tp), np.array(tf)

def parse_phones(path):
    rows = []
    with open(path) as f:
        next(f)
        for line in f:
            c = line.rstrip("\n").split("\t")
            rows.append({
                "t": float(c[0]), "ph": c[1], "word": c[2],
                "f0": float(c[3]) if c[3] else None,
                "f1": float(c[4]) if c[4] else None,
                "f2": float(c[5]) if c[5] else None,
                "f3": float(c[6]) if c[6] else None,
                "res": float(c[7]) if c[7] else None,
            })
    # phone interval = [start, next start); midpoint for sampling
    for i, r in enumerate(rows):
        end = rows[i + 1]["t"] if i + 1 < len(rows) else r["t"] + 0.15
        r["end"] = end
        r["mid"] = (r["t"] + end) / 2
    return rows

# ---------- alignment ----------

def align_offset(phones, tp):
    """Offset that best matches tracker pitch to praat f0 at phone midpoints."""
    ref = [(p["mid"], p["f0"]) for p in phones if p["f0"] and p["ph"] not in ("sil", "sp", "")]
    if not ref or not len(tp):
        return None
    tt, pp = tp[:, 0], tp[:, 1]

    def score(off):
        good, errs = 0, []
        for m, f0 in ref:
            i = np.searchsorted(tt, m + off)
            lo, hi = max(0, i - 3), min(len(tt), i + 4)
            v = pp[lo:hi]
            v = v[v > 0]
            if len(v) == 0:
                continue
            e = abs(np.median(v) - f0)
            errs.append(e)
            if e < 12:
                good += 1
        if not errs:
            return (0, 1e9)
        return (good, -float(np.mean(errs)))

    best = max((score(o) + (o,) for o in np.arange(0.0, 16.0, 0.05)),
               key=lambda s: (s[0], s[1]))
    coarse = best[2]
    best = max((score(o) + (o,) for o in np.arange(coarse - 0.06, coarse + 0.06, 0.005)),
               key=lambda s: (s[0], s[1]))
    return best[2], best[0], len(ref)

# ---------- pairing ----------

def build_rows(clip, phones, tp, tf, off):
    out = []
    for p in phones:
        if p["res"] is None:
            continue
        t0, t1 = p["t"] + off, p["end"] + off
        pv = tp[(tp[:, 0] >= t0) & (tp[:, 0] <= t1)][:, 1] if len(tp) else np.array([])
        pv = pv[pv > 0]
        fv = tf[(tf[:, 0] >= t0) & (tf[:, 0] <= t1)][:, 1:] if len(tf) else np.array([])
        if len(pv) < 2 or len(fv) < 2:
            continue
        out.append({
            "clip": clip, "ph": p["ph"], "word": p["word"], "mid": p["mid"],
            "site_f0": p["f0"], "site_f1": p["f1"], "site_f2": p["f2"], "site_f3": p["f3"],
            "site_res": p["res"],
            "trk_p": float(np.median(pv)),
            "trk_f1": float(np.median(fv[:, 0])),
            "trk_f2": float(np.median(fv[:, 1])),
            "trk_f3": float(np.median(fv[:, 2])),
        })
    return out

# ---------- models ----------

def m0_current(rows):
    A = [(420.0, 550.0), (1350.0, 1650.0), (2700.0, 3150.0)]
    W = [0.25, 0.45, 0.30]
    out = []
    for r in rows:
        rr = sum(w * min(1.5, max(-0.5, (r[f"trk_f{i+1}"] - A[i][0]) / (A[i][1] - A[i][0])))
                 for i, w in enumerate(W))
        out.append(1.0 / (1.0 + math.exp(-(rr - 0.5) / 0.25)))
    return np.array(out)

def fit_affine(rows):
    X = np.array([[r["trk_f1"], r["trk_f2"], r["trk_f3"], 1.0] for r in rows])
    y = np.array([r["site_res"] for r in rows])
    # Huber-style IRLS to keep praat/tracker blunders from dragging the fit
    w = np.ones(len(y))
    beta = None
    for _ in range(8):
        Xw = X * w[:, None]
        beta = np.linalg.lstsq(Xw.T @ X, Xw.T @ y, rcond=None)[0]
        resid = np.abs(X @ beta - y)
        s = max(np.median(resid) * 1.4826, 1e-6)
        w = np.clip(1.5 * s / np.maximum(resid, 1e-9), None, 1.0)
    return beta

def apply_affine(rows, beta):
    X = np.array([[r["trk_f1"], r["trk_f2"], r["trk_f3"], 1.0] for r in rows])
    return np.clip(X @ beta, 0.0, 1.0)

def fit_unitmap(rows):
    """Per-formant linear map tracker -> praat units."""
    maps = []
    for i in (1, 2, 3):
        ok = [r for r in rows if r[f"site_f{i}"]]
        x = np.array([r[f"trk_f{i}"] for r in ok])
        y = np.array([r[f"site_f{i}"] for r in ok])
        A = np.vstack([x, np.ones(len(x))]).T
        # robust IRLS again
        w = np.ones(len(y))
        for _ in range(6):
            Aw = A * w[:, None]
            m, b = np.linalg.lstsq(Aw.T @ A, Aw.T @ y, rcond=None)[0]
            resid = np.abs(A @ [m, b] - y)
            s = max(np.median(resid) * 1.4826, 1e-6)
            w = np.clip(1.5 * s / np.maximum(resid, 1e-9), None, 1.0)
        maps.append((m, b))
    return maps

PHONES_ALL = [k for k in STATS if STATS[k] and len(STATS[k]) >= 3]

def site_res_from_praat(f1, f2, ph):
    st = STATS.get(ph)
    if not st:
        return None
    z1 = (f1 - st[1]["mean"]) / st[1]["stdev"]
    z2 = (f2 - st[2]["mean"]) / st[2]["stdev"]
    return min(1.0, max(0.0, (W_SITE[0] * z1 + W_SITE[1] * z2) / 3 + 0.5))

def m3(rows, maps, oracle):
    out = []
    for r in rows:
        f1 = maps[0][0] * r["trk_f1"] + maps[0][1]
        f2 = maps[1][0] * r["trk_f2"] + maps[1][1]
        if oracle:
            v = site_res_from_praat(f1, f2, r["ph"])
            out.append(v if v is not None else 0.5)
            continue
        num = den = 0.0
        for ph in PHONES_ALL:
            st = STATS[ph]
            d = (((f1 - st[1]["mean"]) / st[1]["stdev"]) ** 2
                 + ((f2 - st[2]["mean"]) / st[2]["stdev"]) ** 2)
            w = math.exp(-0.5 * d)
            if w < 1e-8:
                continue
            v = site_res_from_praat(f1, f2, ph)
            num += w * v
            den += w
        out.append(num / den if den > 0 else 0.5)
    return np.array(out)

# ---------- evaluation ----------

def evaluate(tag, rows, pred):
    y = np.array([r["site_res"] for r in rows])
    mae = float(np.mean(np.abs(pred - y)))
    corr = float(np.corrcoef(pred, y)[0, 1])
    # per-clip medians vs official medians
    clips = sorted(set(r["clip"] for r in rows))
    med_err = []
    for c in clips:
        idx = [i for i, r in enumerate(rows) if r["clip"] == c]
        med_err.append(abs(float(np.median(pred[idx])) - float(np.median(y[idx]))))
    # word-level: mean per word
    wkey = {}
    for i, r in enumerate(rows):
        wkey.setdefault((r["clip"], r["word"], round(r["mid"], 1)), []).append(i)
    werr = [abs(float(np.mean(pred[ix])) - float(np.mean(y[ix]))) for ix in wkey.values()]
    print(f"{tag:22s} phoneMAE {mae:.4f}  corr {corr:.3f}  "
          f"wordMAE {np.mean(werr):.4f}  clipMedianMAE {np.mean(med_err):.4f}  worstClip {max(med_err):.4f}")
    return mae

# ---------- main ----------

clips = sorted(n[:-11] for n in os.listdir(CLIPDIR) if n.endswith(".phones.tsv"))
rows = []
print("alignment:")
for c in clips:
    tlog = f"{CLIPDIR}/{c}.tracks.log"
    if not os.path.exists(tlog):
        print(f"  {c}: no tracks log, skipped"); continue
    tp, tf = parse_tracks(tlog)
    phones = parse_phones(f"{CLIPDIR}/{c}.phones.tsv")
    al = align_offset(phones, tp)
    if not al:
        print(f"  {c}: alignment failed"); continue
    off, good, nref = al
    clip_rows = build_rows(c, phones, tp, tf, off)
    rows.extend(clip_rows)
    print(f"  {c:10s} offset {off:6.3f}s  pitch-agree {good}/{nref}  paired {len(clip_rows)}")

print(f"\ndataset: {len(rows)} paired phones from {len(set(r['clip'] for r in rows))} clips")

# pitch sanity
pe = [abs(r["trk_p"] - r["site_f0"]) for r in rows if r["site_f0"]]
print(f"pitch tracker-vs-praat MAE {np.mean(pe):.1f} Hz (median {np.median(pe):.1f})")

# formant unit map (tracker -> praat units)
maps = fit_unitmap(rows)
for i, (m, b) in enumerate(maps):
    print(f"F{i+1}: praat = {m:.4f} * trk + {b:+.1f}")

print("\nfull-data fits:")
evaluate("M0 current logistic", rows, m0_current(rows))
beta = fit_affine(rows)
evaluate("M1 affine", rows, apply_affine(rows, beta))
evaluate("M3s soft-vowel", rows, m3(rows, maps, oracle=False))
evaluate("M3o oracle-phoneme", rows, m3(rows, maps, oracle=True))
print(f"\nM1 beta: r = clip01({beta[0]:.6g}*f1 + {beta[1]:.6g}*f2 + {beta[2]:.6g}*f3 + {beta[3]:.6g})")

print("\nleave-one-clip-out CV:")
for tag, fitter, applier in [
    ("M1 affine", fit_affine, apply_affine),
]:
    preds = np.zeros(len(rows))
    for c in set(r["clip"] for r in rows):
        tr = [r for r in rows if r["clip"] != c]
        te_idx = [i for i, r in enumerate(rows) if r["clip"] == c]
        b = fitter(tr)
        preds[te_idx] = applier([rows[i] for i in te_idx], b)
    evaluate(f"{tag} (LOCO)", rows, preds)

# soft-vowel CV: unit maps refit per fold
preds = np.zeros(len(rows))
for c in set(r["clip"] for r in rows):
    tr = [r for r in rows if r["clip"] != c]
    te_idx = [i for i, r in enumerate(rows) if r["clip"] == c]
    mp = fit_unitmap(tr)
    preds[te_idx] = m3([rows[i] for i in te_idx], mp, oracle=False)
evaluate("M3s soft-vowel (LOCO)", rows, preds)

# save dataset
out = f"{os.path.dirname(os.path.abspath(__file__))}/dataset.tsv"
with open(out, "w") as f:
    f.write("clip\tphoneme\tword\tmid\tsite_f0\tsite_f1\tsite_f2\tsite_f3\tsite_res\ttrk_p\ttrk_f1\ttrk_f2\ttrk_f3\n")
    for r in rows:
        f.write("\t".join(str(r[k] if r[k] is not None else "") for k in
                ["clip", "ph", "word", "mid", "site_f0", "site_f1", "site_f2",
                 "site_f3", "site_res", "trk_p", "trk_f1", "trk_f2", "trk_f3"]) + "\n")
print(f"\ndataset saved: {out}")
