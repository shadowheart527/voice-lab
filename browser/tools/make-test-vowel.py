#!/usr/bin/env python3
"""Synthesise a vowel with known pitch and formants, for testing without a voice.

A source-filter vowel: a Rosenberg glottal pulse train with jitter and vibrato,
differentiated for the -6 dB/octave source slope, through four formant
resonators. Thin signals are not good enough for this -- a two-formant pulse
train reads a correct pitch and no resonance at all, because there is nothing for
the LPC fit to work with above F2 -- so the fourth formant and the source tilt
are the point, not decoration.

    browser/tools/make-test-vowel.py out.wav [--f0 140] [--f1 620] [--f2 1180]

Defaults land where a masculine /a/ does, which is a deliberate choice: it puts
the expected reading well away from the middle of every scale, so a tracker that
silently returns nothing is not mistaken for one that works.
"""
import argparse
import math
import random
import struct
import wave

SR = 48000


def resonator(x, freq, bw, gain, sr=SR):
    """Two-pole resonator, the standard Klatt formant section."""
    r = math.exp(-math.pi * bw / sr)
    theta = 2 * math.pi * freq / sr
    a1, a2 = 2 * r * math.cos(theta), -r * r
    y1 = y2 = 0.0
    out = [0.0] * len(x)
    for i, v in enumerate(x):
        y = v * (1 - r) + a1 * y1 + a2 * y2
        out[i] = y * gain
        y2, y1 = y1, y
    return out


def synthesise(f0, formants, seconds, sr=SR, seed=7):
    rng = random.Random(seed)
    n = int(seconds * sr)

    flow = [0.0] * n
    phase = 0.0
    open_quotient = 0.6
    for i in range(n):
        t = i / sr
        # Vibrato plus a little cycle-to-cycle jitter, so the tracker sees a
        # voice rather than a perfectly periodic signal it can cheat on.
        f = f0 * (1 + 0.012 * math.sin(2 * math.pi * 4.5 * t)) * (1 + 0.004 * rng.uniform(-1, 1))
        phase += f / sr
        if phase >= 1.0:
            phase -= 1.0
        if phase < open_quotient:
            x = phase / open_quotient
            g = 3 * x * x - 2 * x * x * x
        else:
            x = (phase - open_quotient) / (1 - open_quotient)
            g = 1 - x * x
        flow[i] = g - 0.5 + 0.002 * rng.uniform(-1, 1)

    deriv = [0.0] + [flow[i] - flow[i - 1] for i in range(1, n)]

    mix = [0.0] * n
    for freq, bw, gain in formants:
        section = resonator(deriv, freq, bw, gain, sr)
        for i in range(n):
            mix[i] += section[i]

    peak = max(abs(v) for v in mix) or 1.0
    return [0.6 * v / peak for v in mix]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--f0', type=float, default=140.0)
    ap.add_argument('--f1', type=float, default=620.0)
    ap.add_argument('--f2', type=float, default=1180.0)
    ap.add_argument('--seconds', type=float, default=10.0)
    args = ap.parse_args()

    formants = [(args.f1, 70, 1.0), (args.f2, 90, 0.7), (2600, 140, 0.35), (3500, 200, 0.2)]
    samples = synthesise(args.f0, formants, args.seconds)

    with wave.open(args.out, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(b''.join(struct.pack('<h', int(32767 * v)) for v in samples))

    print(f'{args.out}: {args.seconds:g} s, F0 {args.f0:g} Hz, '
          f'F1 {args.f1:g} Hz, F2 {args.f2:g} Hz')


if __name__ == '__main__':
    main()
