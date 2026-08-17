#!/usr/bin/env python3
"""Extract calibration ground truth from the acousticgender.space reference clips.

Each resources/<name>.json carries the original audio (base64 WAV) and the
official pipeline's per-phoneme results (praat F0-F3 at phoneme midpoints plus
the phoneme-normalized resonance score). Writes:

  <outdir>/<name>.wav        the audio
  <outdir>/<name>.phones.tsv time, phoneme, word, f0..f3, site resonance
  <outdir>/summary.tsv       per-clip official summary stats
"""
import base64, json, os, sys

REPO = os.path.expanduser("~/git/gender-voice-visualization")
OUTDIR = sys.argv[1] if len(sys.argv) > 1 else "."
SKIP = {"themes"}

summary = []
for fn in sorted(os.listdir(f"{REPO}/resources")):
    if not fn.endswith(".json"):
        continue
    name = fn[:-5]
    if name in SKIP:
        continue
    d = json.load(open(f"{REPO}/resources/{fn}"))
    audio = d.get("audio") or ""
    if not audio.startswith("data:audio/wav;base64,") or not d.get("phones"):
        print(f"skip {name}: no usable audio/phones")
        continue

    with open(f"{OUTDIR}/{name}.wav", "wb") as f:
        f.write(base64.b64decode(audio.split(",", 1)[1]))

    n = 0
    with open(f"{OUTDIR}/{name}.phones.tsv", "w") as f:
        f.write("time\tphoneme\tword\tf0\tf1\tf2\tf3\tresonance\n")
        for p in d["phones"]:
            F = p.get("F") or []
            F = [("" if v is None else f"{v:.2f}") for v in (F + [None] * 4)[:4]]
            res = p.get("resonance")
            word = (p.get("word") or {}).get("word", "")
            f.write(f"{p.get('time')}\t{p.get('phoneme')}\t{word}\t"
                    + "\t".join(F)
                    + f"\t{'' if res is None else f'{res:.5f}'}\n")
            n += 1
    summary.append((name, n, d.get("meanPitch"), d.get("medianPitch"),
                    d.get("meanResonance"), d.get("medianResonance"),
                    d.get("stdevResonance")))
    print(f"{name}: {n} phones")

with open(f"{OUTDIR}/summary.tsv", "w") as f:
    f.write("clip\tphones\tmeanPitch\tmedianPitch\tmeanRes\tmedianRes\tstdevRes\n")
    for row in summary:
        f.write("\t".join("" if v is None else (f"{v:.4f}" if isinstance(v, float) else str(v)) for v in row) + "\n")
print(f"{len(summary)} clips -> {OUTDIR}")
