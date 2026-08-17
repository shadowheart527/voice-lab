#!/usr/bin/env python3
"""Prototype the vocal-weight measure (harmonic spectral tilt) on the
reference clips: for each phone with known praat F0, measure harmonic
amplitudes at k*F0 and fit a dB-per-octave slope. Flat slope = heavy/buzzy
(strong high harmonics), steep = light. Reports per-clip medians so the
population can anchor the live 0..1 weight scale.

Usage: weight_proto.py <refclips-dn dir>
"""
import os, sys, wave
import numpy as np

CLIPDIR = sys.argv[1]

def read_wav(path):
    with wave.open(path, "rb") as w:
        sr = w.getframerate()
        n = w.getnframes()
        data = np.frombuffer(w.readframes(n), dtype=np.int16).astype(np.float64)
        if w.getnchannels() > 1:
            data = data.reshape(-1, w.getnchannels()).mean(axis=1)
    return sr, data / 32768.0

def phone_rows(path):
    rows = []
    with open(path) as f:
        next(f)
        for line in f:
            c = line.rstrip("\n").split("\t")
            fs = [float(c[i]) if c[i] else None for i in (3, 4, 5, 6)]
            rows.append((float(c[0]), c[1], fs))
    for i in range(len(rows)):
        end = rows[i + 1][0] if i + 1 < len(rows) else rows[i][0] + 0.15
        yield rows[i][0], end, rows[i][1], rows[i][2]

def formant_gain_db(f, F, B):
    """Gain of one formant resonator at frequency f (Iseli-Alwan style)."""
    # |H(f)|^2 for a two-pole resonator with center F, bandwidth B, normalized
    # to unity at f=0 so only the peak shaping remains.
    r2 = (F * F + (B / 2) ** 2)
    num = r2
    den = np.sqrt((f * f - r2) ** 2 + f * f * B * B)
    # avoid the exact-pole blowup
    return 20 * np.log10(np.maximum(num / np.maximum(den, 1e-9), 1e-9))

def tract_correction_db(fks, formants):
    corr = np.zeros(len(fks))
    for F in formants:
        if not F or F <= 0:
            continue
        B = 80.0 + 120.0 * F / 5000.0
        corr += formant_gain_db(np.array(fks), F, B)
    return corr

def tilt_at(sig, sr, t, f0, formants=None, win_s=0.040, nfft=4096):
    c = int(t * sr)
    h = int(win_s * sr / 2)
    if c - h < 0 or c + h >= len(sig):
        return None
    xw = sig[c - h:c + h] * np.hanning(2 * h)
    X = np.abs(np.fft.rfft(xw, nfft))
    freqs = np.arange(len(X)) * sr / nfft
    amps, fks = [], []
    k = 1
    while k * f0 < min(3200, sr / 2 - 100) and k <= 14:
        fk = k * f0
        if fk >= 160:  # below ~160 Hz recording highpass corrupts amplitudes
            lo = np.searchsorted(freqs, fk - 0.35 * f0)
            hi = np.searchsorted(freqs, fk + 0.35 * f0)
            if hi <= lo:
                break
            a = X[lo:hi].max()
            if a > 0:
                amps.append(20 * np.log10(a))
                fks.append(freqs[lo + int(np.argmax(X[lo:hi]))])
        k += 1
    if len(amps) < 5:
        return None
    fks = np.array(fks); amps = np.array(amps)
    if formants:
        # remove formant peak shaping analytically, keep the source tilt
        amps = amps - tract_correction_db(fks, formants)
    oct_ = np.log2(fks / fks[0])
    slope = np.polyfit(oct_, amps, 1)[0]
    return float(slope)

clips = sorted(n[:-11] for n in os.listdir(CLIPDIR) if n.endswith(".phones.tsv"))
allmed = []
print(f"{'clip':10s} {'phones':>6s} {'tilt dB/oct':>11s}  {'p25':>6s} {'p75':>6s}")
percl = {}
for c in clips:
    wavp = f"{CLIPDIR}/{c}.wav"
    if not os.path.exists(wavp):
        continue
    sr, sig = read_wav(wavp)
    tilts = []
    for t0, t1, ph, fs in phone_rows(f"{CLIPDIR}/{c}.phones.tsv"):
        f0 = fs[0]
        if not f0 or f0 < 55 or f0 > 500 or ph in ("sil", "sp", ""):
            continue
        s = tilt_at(sig, sr, (t0 + t1) / 2, f0, formants=fs[1:4])
        if s is not None and -40 < s < 10:
            tilts.append(s)
    if not tilts:
        continue
    tilts = np.array(tilts)
    med = float(np.median(tilts))
    percl[c] = med
    allmed.extend(tilts)
    print(f"{c:10s} {len(tilts):6d} {med:11.2f}  {np.percentile(tilts,25):6.2f} {np.percentile(tilts,75):6.2f}")

a = np.array(allmed)
print(f"\nphone-level population: p10 {np.percentile(a,10):.2f}  median {np.median(a):.2f}  p90 {np.percentile(a,90):.2f}")
masc = ["chuck", "david", "bob", "steve", "chris", "leonard", "aiden"]
fem = ["lucy", "cheryl", "quinn", "kristen", "morgan", "wina", "zoe", "luna", "jenn", "ashley", "lesley"]
mm = np.mean([percl[c] for c in masc if c in percl])
fm = np.mean([percl[c] for c in fem if c in percl])
print(f"masc-read clips mean tilt {mm:.2f} dB/oct vs fem-read {fm:.2f} dB/oct")
