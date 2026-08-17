#!/usr/bin/env python3
"""Residual analysis + refinement experiments on top of calibrate.py's dataset."""
import json, math, os, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.expanduser("~/git/gender-voice-visualization")
STATS = json.load(open(f"{REPO}/stats.json"))
W_SITE = [0.7321428571428571, 0.26785714285714285, 0.0]
PHONES_ALL = [k for k in STATS if STATS[k] and len(STATS[k]) >= 3]

rows = []
with open(f"{HERE}/dataset.tsv") as f:
    hdr = next(f).rstrip("\n").split("\t")
    for line in f:
        c = dict(zip(hdr, line.rstrip("\n").split("\t")))
        rows.append({k: (c[k] if k in ("clip", "phoneme", "word") else
                         (float(c[k]) if c[k] else None)) for k in hdr})

official_median = {}
with open(sys.argv[1] + "/summary.tsv") as f:
    next(f)
    for line in f:
        c = line.split("\t")
        official_median[c[0]] = float(c[5])

clips = sorted(set(r["clip"] for r in rows))

def fit_unitmap(sub, log=False, quad=False):
    maps = []
    for i in (1, 2, 3):
        ok = [r for r in sub if r[f"site_f{i}"]]
        x = np.array([r[f"trk_f{i}"] for r in ok])
        y = np.array([r[f"site_f{i}"] for r in ok])
        if log:
            x, y = np.log(x), np.log(y)
        cols = [x, np.ones(len(x))]
        if quad:
            cols.insert(0, x * x)
        A = np.vstack(cols).T
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

def apply_map(beta, v, log=False, quad=False):
    x = math.log(v) if log else v
    y = (beta[0] * x * x + beta[1] * x + beta[2]) if quad else (beta[0] * x + beta[1])
    return math.exp(y) if log else y

def site_res(f1, f2, ph):
    st = STATS.get(ph)
    z1 = (f1 - st[1]["mean"]) / st[1]["stdev"]
    z2 = (f2 - st[2]["mean"]) / st[2]["stdev"]
    return min(1.0, max(0.0, (W_SITE[0] * z1 + W_SITE[1] * z2) / 3 + 0.5))

def m3s(sub, maps, log=False, quad=False, temp=1.0):
    out = []
    for r in sub:
        f1 = apply_map(maps[0], r["trk_f1"], log, quad)
        f2 = apply_map(maps[1], r["trk_f2"], log, quad)
        num = den = 0.0
        for ph in PHONES_ALL:
            st = STATS[ph]
            d = (((f1 - st[1]["mean"]) / st[1]["stdev"]) ** 2
                 + ((f2 - st[2]["mean"]) / st[2]["stdev"]) ** 2)
            w = math.exp(-0.5 * d / temp)
            if w < 1e-9:
                continue
            num += w * site_res(f1, f2, ph)
            den += w
        out.append(num / den if den > 0 else 0.5)
    return np.array(out)

def loco_eval(tag, log=False, quad=False, temp=1.0, shrink=None):
    """LOCO clip-median error vs the official per-clip medians."""
    errs = {}
    phone_abs = []
    for c in clips:
        tr = [r for r in rows if r["clip"] != c]
        te = [r for r in rows if r["clip"] == c]
        maps = fit_unitmap(tr, log, quad)
        pred = m3s(te, maps, log, quad, temp)
        if shrink is not None:
            # affine post-map fit on TRAINING clip medians vs official
            tm, to = [], []
            for c2 in clips:
                if c2 == c: continue
                sub = [r for r in tr if r["clip"] == c2]
                tm.append(float(np.median(m3s(sub, maps, log, quad, temp))))
                to.append(official_median[c2])
            A = np.vstack([tm, np.ones(len(tm))]).T
            a, b = np.linalg.lstsq(A.T @ A, A.T @ np.array(to), rcond=None)[0]
            pred = np.clip(a * pred + b, 0, 1)
        errs[c] = float(np.median(pred)) - official_median[c]
        phone_abs.extend(np.abs(pred - np.array([r["site_res"] for r in te])))
    e = np.array(list(errs.values()))
    print(f"{tag:34s} clipMedMAE {np.mean(np.abs(e)):.4f}  worst {np.max(np.abs(e)):.4f}  "
          f"bias {np.mean(e):+.4f}  phoneMAE {np.mean(phone_abs):.4f}")
    return errs

print("per-clip residuals, baseline M3s (LOCO):")
errs = loco_eval("M3s linear-unitmap", log=False)
for c in sorted(errs, key=lambda k: -abs(errs[k])):
    n = len([r for r in rows if r["clip"] == c])
    print(f"  {c:10s} err {errs[c]:+.3f}  official {official_median[c]:.3f}  phones {n}")

print("\nvariants (all LOCO):")
loco_eval("M3s log-unitmap", log=True)
loco_eval("M3s quad-unitmap", quad=True)
loco_eval("M3s temp=2 (softer assign)", temp=2.0)
loco_eval("M3s temp=4", temp=4.0)
loco_eval("M3s + median shrink", shrink=True)
loco_eval("M3s log + shrink", log=True, shrink=True)
loco_eval("M3s temp=2 + shrink", temp=2.0, shrink=True)

# ---- M4: z-score against the TRACKER's own per-phoneme population stats ----
# The site's resonance is "where do your formants sit within the population,
# for this phoneme". Computing that with their praat-space stdevs attenuates
# the score (our measurement noise + different scale). Build per-phoneme
# mean/stdev of the tracker's own values across the reference speakers and
# z-score natively; dispersion is then right by construction.

def trk_stats(sub, min_n=8):
    by = {}
    for r in sub:
        by.setdefault(r["phoneme"], []).append((r["trk_f1"], r["trk_f2"]))
    pooled = np.array([(r["trk_f1"], r["trk_f2"]) for r in sub])
    pm = pooled.mean(axis=0); ps = pooled.std(axis=0)
    st = {}
    for ph, vals in by.items():
        v = np.array(vals)
        if len(v) >= min_n:
            st[ph] = (v[:, 0].mean(), max(v[:, 0].std(), 1e-3),
                      v[:, 1].mean(), max(v[:, 1].std(), 1e-3), len(v))
        else:
            st[ph] = (pm[0], ps[0], pm[1], ps[1], len(v))
    st["__pooled__"] = (pm[0], ps[0], pm[1], ps[1], len(pooled))
    return st

def m4(sub, st, temp=1.0, gain=1.0):
    out = []
    for r in sub:
        f1, f2 = r["trk_f1"], r["trk_f2"]
        num = den = 0.0
        for ph, (m1, s1, m2, s2, n) in st.items():
            if ph == "__pooled__":
                continue
            z1 = (f1 - m1) / s1
            z2 = (f2 - m2) / s2
            w = math.exp(-0.5 * (z1 * z1 + z2 * z2) / temp)
            if w < 1e-9:
                continue
            v = min(1.0, max(0.0, gain * (W_SITE[0] * z1 + W_SITE[1] * z2) / 3 + 0.5))
            num += w * v
            den += w
        if den <= 0:
            m1, s1, m2, s2, _ = st["__pooled__"]
            z1 = (f1 - m1) / s1
            z2 = (f2 - m2) / s2
            out.append(min(1.0, max(0.0, gain * (W_SITE[0] * z1 + W_SITE[1] * z2) / 3 + 0.5)))
        else:
            out.append(num / den)
    return np.array(out)

def loco_m4(tag, temp=1.0, gain=1.0, oracle=False):
    errs = {}
    phone_abs = []
    for c in clips:
        tr = [r for r in rows if r["clip"] != c]
        te = [r for r in rows if r["clip"] == c]
        st = trk_stats(tr)
        if oracle:
            pred = []
            for r in te:
                m1, s1, m2, s2, n = st.get(r["phoneme"], st["__pooled__"])
                z1 = (r["trk_f1"] - m1) / s1
                z2 = (r["trk_f2"] - m2) / s2
                pred.append(min(1.0, max(0.0, gain * (W_SITE[0] * z1 + W_SITE[1] * z2) / 3 + 0.5)))
            pred = np.array(pred)
        else:
            pred = m4(te, st, temp, gain)
        errs[c] = float(np.median(pred)) - official_median[c]
        phone_abs.extend(np.abs(pred - np.array([r["site_res"] for r in te])))
    e = np.array(list(errs.values()))
    print(f"{tag:34s} clipMedMAE {np.mean(np.abs(e)):.4f}  worst {np.max(np.abs(e)):.4f}  "
          f"bias {np.mean(e):+.4f}  phoneMAE {np.mean(phone_abs):.4f}")
    return errs

print("\nM4 tracker-native stats (all LOCO):")
loco_m4("M4 soft temp=1", temp=1.0)
loco_m4("M4 soft temp=2", temp=2.0)
e = loco_m4("M4 oracle-phoneme", oracle=True)
loco_m4("M4 soft t=1 gain=1.3", temp=1.0, gain=1.3)
loco_m4("M4 soft t=1 gain=1.6", temp=1.0, gain=1.6)
loco_m4("M4 soft t=1 gain=2.0", temp=1.0, gain=2.0)
loco_m4("M4 oracle gain=1.6", oracle=True, gain=1.6)

print("\nper-clip residuals, M4 oracle (LOCO):")
for c in sorted(e, key=lambda k: -abs(e[k])):
    print(f"  {c:10s} err {e[c]:+.3f}  official {official_median[c]:.3f}")
