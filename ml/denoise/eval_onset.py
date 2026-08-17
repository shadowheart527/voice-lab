"""How long does a held note survive before DeepFilterNet decides it is noise?

100 ms resolution, so the answer is a usable number for drill design.
"""

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from audiometrics import load_wav, rms_db  # noqa: E402

SCRATCH = Path("/tmp/claude-1000/-var-home-shadowheart527/"
               "04778bbc-32b9-4506-a8ec-d6dd51383461/scratchpad")
OUT = Path(__file__).resolve().parent / "out"


def course(name, secs=8.0, blk=0.1):
    ref, fs = load_wav(SCRATCH / f"{name}.wav")
    enh, _ = load_wav(OUT / f"{name}.df.wav")
    n = min(len(ref), len(enh), int(secs * fs))
    nb = int(blk * fs)
    print(f"\n{name}.wav -- suppression vs time into the held note "
          f"({int(blk * 1000)} ms blocks, dB)")
    line_t, line_d = [], []
    for i in range(0, n - nb + 1, nb):
        d = rms_db(enh[i:i + nb]) - rms_db(ref[i:i + nb])
        line_t.append(i / fs)
        line_d.append(d)
    for r in range(0, len(line_t), 10):
        chunk = line_d[r:r + 10]
        print(f"  t={line_t[r]:5.1f}s  " + " ".join(f"{v:+7.2f}" for v in chunk))

    d = np.array(line_d)
    t = np.array(line_t)
    for thr in (-6.0, -12.0, -20.0, -30.0):
        idx = np.where(d <= thr)[0]
        when = f"{t[idx[0]]:.1f} s" if len(idx) else "never"
        print(f"  first block attenuated by more than {-thr:>4.0f} dB: {when}")


if __name__ == "__main__":
    course("steady")
    course("human")
