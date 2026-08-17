#!/usr/bin/env python3
"""Turn a voice-lab session export into a written debrief.

Two paths, and the fallback is the important one: if a local OpenAI-compatible
LLM is reachable (LM Studio on :1234, Ollama on :11434) it writes the prose,
and otherwise a deterministic summariser does, so this always produces
something useful and never depends on a server being up.

The observations themselves are computed here either way. The LLM only chooses
how to say them, and is explicitly instructed not to invent numbers, so a
hallucinating model cannot fabricate progress that did not happen.

Usage:
    coach.py session.json [--llm auto|off|<base-url>] [--model NAME]
"""
import argparse, json, os, sys, urllib.request

ENDPOINTS = [
    ("http://127.0.0.1:1234/v1", "LM Studio"),
    ("http://127.0.0.1:11434/v1", "Ollama"),
]


# ----------------------------------------------------------------- analysis

def observations(s):
    """Facts about the session, as (topic, sentence) pairs. No prose flourish."""
    out = []
    dur = s.get("durationSeconds") or 0
    voiced = s.get("voicedSeconds") or 0
    pitch = s.get("pitch") or {}
    tgt = s.get("target") or {}

    out.append(("length", f"The session ran {dur // 60} min {dur % 60} s, "
                          f"with roughly {voiced} s of actual voicing."))

    if pitch.get("medianHz"):
        lo, hi = tgt.get("minHz"), tgt.get("maxHz")
        pct = pitch.get("percentInTarget")
        out.append(("pitch",
                    f"Median pitch was {pitch['medianHz']:.0f} Hz, ranging "
                    f"{pitch.get('p10Hz', 0):.0f}-{pitch.get('p90Hz', 0):.0f} Hz "
                    f"across the middle 80% of the time."))
        if pct is not None and lo and hi:
            out.append(("target",
                        f"{pct:.0f}% of voiced time sat inside the {lo}-{hi} Hz target."))

    res = (s.get("resonance") or {}).get("median")
    if res is not None:
        out.append(("resonance",
                    f"Median resonance was {res * 100:.0f}% on the population scale, "
                    f"where 50% is an average speaker."))

    gr = (s.get("genderRead") or {}).get("median")
    if gr is not None:
        label = "masculine" if gr < 0.38 else "feminine" if gr > 0.62 else "androgynous"
        out.append(("read", f"The overall gender read sat at {gr:.2f}, in the {label} range."))

    w = (s.get("weight") or {}).get("median")
    sz = (s.get("size") or {}).get("median")
    if w is not None and sz is not None:
        balance = w - (1 - sz)
        if balance > 0.28:
            tone = "heavier than the vocal size, the buzzy or overfull direction"
        elif balance < -0.28:
            tone = "lighter than the vocal size, the hollow or underfull direction"
        else:
            tone = "reasonably balanced against the vocal size"
        out.append(("fullness",
                    f"Vocal weight averaged {w * 100:.0f}% and size {sz * 100:.0f}%, "
                    f"which is {tone}."))

    cells = s.get("fullnessCells") or {}
    if cells:
        top = sorted(cells.items(), key=lambda kv: -kv[1])[:2]
        total = sum(cells.values()) or 1
        parts = ", ".join(f"{k} {v / total * 100:.0f}%" for k, v in top)
        out.append(("cells", f"Most time was spent in {parts}."))

    # Drift is the observation people cannot self-detect, so call it explicitly.
    tl = [b for b in (s.get("timeline") or []) if b.get("medianPitchHz")]
    if len(tl) >= 3:
        first, last = tl[0], tl[-1]
        dp = last["medianPitchHz"] - first["medianPitchHz"]
        if abs(dp) >= 8:
            direction = "rose" if dp > 0 else "fell"
            out.append(("drift",
                        f"Pitch {direction} about {abs(dp):.0f} Hz from the start of the "
                        f"session to the end, which usually means fatigue or settling."))
        else:
            out.append(("drift", "Pitch held steady from start to finish, no drift worth noting."))

        gr_vals = [b.get("medianGenderRead") for b in tl if b.get("medianGenderRead") is not None]
        if len(gr_vals) >= 3:
            dg = gr_vals[-1] - gr_vals[0]
            if abs(dg) >= 0.08:
                out.append(("read-drift",
                            f"The gender read {'climbed' if dg > 0 else 'dropped'} "
                            f"{abs(dg):.2f} over the session."))
    return out


def deterministic_debrief(s, obs):
    lines = ["# Session debrief", ""]
    lines += [f"- {text}" for _, text in obs]
    lines.append("")

    # A single suggestion, chosen by the largest actionable gap.
    pitch = (s.get("pitch") or {})
    pct = pitch.get("percentInTarget")
    w = (s.get("weight") or {}).get("median")
    sz = (s.get("size") or {}).get("median")
    tip = None
    # Judge fullness by where time was actually spent, not by the balance of
    # the session medians: a voice can average "balanced" while having spent
    # most of its time underfull, and the histogram is the honest answer.
    cells = s.get("fullnessCells") or {}
    if cells:
        total = sum(cells.values()) or 1
        under = sum(v for k, v in cells.items() if k.startswith("U")) / total
        over = sum(v for k, v in cells.items() if k.startswith("O")) / total
        if under > 0.5:
            tip = (f"You spent {under * 100:.0f}% of the time on the underfull side. The voice is "
                   f"already light, so the lever is resonance rather than weight: adding size, a "
                   f"smaller and more forward vocal tract, is what moves hollow toward full.")
        elif over > 0.5:
            tip = (f"You spent {over * 100:.0f}% of the time on the overfull side. The size is "
                   f"there, so the lever is weight rather than resonance: lightening the sound is "
                   f"what moves buzzy toward full.")
    if tip is None and pct is not None and pct < 50:
        tip = (f"Pitch spent {pct:.0f}% of the time in target. If that feels effortful, it is "
               f"usually easier to hold a slightly lower target consistently than to chase a "
               f"higher one intermittently.")
    if tip is None:
        tip = "Nothing stands out as a single weak point; this reads as a consistent session."
    lines += ["## What to look at next", "", tip, ""]
    return "\n".join(lines)


# ---------------------------------------------------------------------- LLM

def find_endpoint(pref):
    if pref == "off":
        return None, None
    if pref and pref != "auto":
        return pref, "custom"
    for url, name in ENDPOINTS:
        try:
            req = urllib.request.Request(url + "/models")
            with urllib.request.urlopen(req, timeout=2) as r:
                data = json.load(r)
                if data.get("data"):
                    return url, name
        except Exception:
            continue
    return None, None


def llm_debrief(base, model, obs, timeout=90):
    facts = "\n".join(f"- {t}" for _, t in obs)
    prompt = (
        "You are helping someone with their trans voice training practice. Below are "
        "measured facts from one practice session. Write a short debrief (120-180 words) "
        "in plain prose, second person, warm but not saccharine, no bullet lists.\n\n"
        "Hard rules: use ONLY the numbers given; never invent a measurement, a trend, or a "
        "comparison to a previous session. Do not moralise, do not comment on how anyone "
        "'should' sound, and do not describe the voice as passing or not passing. Close "
        "with one concrete thing to try next time.\n\n"
        f"Measured facts:\n{facts}\n"
    )
    body = json.dumps({
        "model": model or "local-model",
        "messages": [{"role": "user", "content": prompt}],
        "temperature": 0.4,
    }).encode()
    req = urllib.request.Request(base + "/chat/completions", data=body,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        data = json.load(r)
    return data["choices"][0]["message"]["content"].strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("session")
    ap.add_argument("--llm", default="auto", help="auto | off | base URL")
    ap.add_argument("--model", default=None)
    args = ap.parse_args()

    s = json.load(open(args.session))
    if s.get("schema", "").split("/")[0] != "voice-lab.session":
        print("warning: unrecognised session schema", file=sys.stderr)

    obs = observations(s)
    print(deterministic_debrief(s, obs))

    base, name = find_endpoint(args.llm)
    if not base:
        if args.llm != "off":
            print("(No local LLM reachable, so the summary above is the deterministic one. "
                  "Start LM Studio's server or Ollama for a written version.)")
        return
    try:
        print(f"\n---\n## Written debrief ({name})\n")
        print(llm_debrief(base, args.model, obs))
    except Exception as e:
        print(f"(LLM at {base} failed: {e}. The deterministic summary above still stands.)")


if __name__ == "__main__":
    main()
