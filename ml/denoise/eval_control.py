"""Control: does DeepFilterNet preserve REAL connected speech?

If it mangled real voice too, the sustained-tone result would just mean the
harness is wrong. These are the TransVoiceLessons demonstration clips already
used to anchor the toolkit's vocal-weight scale -- real human voice, real room.
"""

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from audiometrics import f0_track_acf, load_wav, rms_db, tilt_track  # noqa: E402

SCRATCH = Path("/tmp/claude-1000/-var-home-shadowheart527/"
               "04778bbc-32b9-4506-a8ec-d6dd51383461/scratchpad/tvldemos")
OUT = Path(__file__).resolve().parent / "out" / "ctrl"


def voiced_rms_db(x, fs, f0t, hop_ms=80.0, frame_ms=40.0):
    """RMS over frames the ACF called voiced, so silence does not dominate."""
    nwin = int(frame_ms * fs / 1000)
    nhop = int(hop_ms * fs / 1000)
    vals = []
    for i, s in enumerate(range(0, len(x) - nwin + 1, nhop)):
        if i < len(f0t) and np.isfinite(f0t[i]):
            vals.append(np.mean(x[s:s + nwin] ** 2))
    return 10 * np.log10(np.mean(vals)) if vals else np.nan


print(f"{'clip':<26} {'RMS d':>7} {'vRMS d':>7} {'tilt orig':>10} "
      f"{'tilt df':>9} {'d tilt':>7} {'d weight':>9}")
print("-" * 82)
rows = []
for src in sorted(SCRATCH.glob("*.wav")):
    enh_p = OUT / f"{src.stem}.df.wav"
    if not enh_p.exists():
        continue
    ref, fs = load_wav(src)
    enh, _ = load_wav(enh_p)
    n = min(len(ref), len(enh))
    ref, enh = ref[:n], enh[:n]
    _, f0t = f0_track_acf(ref, fs, fmin=70.0, fmax=400.0)
    _, tr, _ = tilt_track(ref, fs, f0t)
    _, te, _ = tilt_track(enh, fs, f0t)
    if len(tr) < 5 or len(te) < 5:
        continue
    mr, me = float(np.median(tr)), float(np.median(te))
    d_rms = rms_db(enh) - rms_db(ref)
    d_v = voiced_rms_db(enh, fs, f0t) - voiced_rms_db(ref, fs, f0t)
    rows.append((src.stem, d_rms, d_v, mr, me, me - mr, (me - mr) / 8.0))
    print(f"{src.stem:<26} {d_rms:>+7.2f} {d_v:>+7.2f} {mr:>10.2f} "
          f"{me:>9.2f} {me - mr:>+7.2f} {(me - mr) / 8.0:>+9.3f}")

a = np.array([r[1:] for r in rows], dtype=float)
print("-" * 82)
print(f"{'MEAN':<26} {a[:, 0].mean():>+7.2f} {a[:, 1].mean():>+7.2f} "
      f"{a[:, 2].mean():>10.2f} {a[:, 3].mean():>9.2f} "
      f"{a[:, 4].mean():>+7.2f} {a[:, 5].mean():>+9.3f}")
print(f"{'MAX |d tilt|':<26} {'':>7} {'':>7} {'':>10} {'':>9} "
      f"{np.abs(a[:, 4]).max():>7.2f} {np.abs(a[:, 5]).max():>9.3f}")
print("\nd weight = tilt shift / 8.0, the toolkit's weightP() scale "
      "(engine/src/context/genderscore.h): 1 dB/oct = 0.125 of the 0..1 range.")
