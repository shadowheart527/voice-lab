// Gender-perception scoring, ported from the desktop engine's genderscore.h.
//
// Three separate quantities, deliberately kept distinct because they answer
// different questions and are on different scales:
//
//   pitchP / resonanceP / overallP  a masc-fem perception estimate, steep by
//       design so it gives decisive training feedback. F0 logistic centred at
//       162 Hz (Gelfer & Schofield's 164-199 Hz fem zone) blended 55/45 with
//       resonance (Hillenbrand & Clark 2009: F0 is the strongest single cue
//       but flipping perceived gender reliably needs the spectral envelope).
//
//   siteResonance  the SAME voice placed on acousticgender.space's population
//       scale, where 0.5 means "average speaker for this phoneme". Much
//       flatter than resonanceP; the two must never be confused, which is
//       exactly the bug that made the live dot read far too feminine.
//
//   weightP  the TransVoiceLessons vocal-weight percept from spectral tilt.
//
// WEB_ANCHORS are re-derived for the browser tracker rather than inherited
// from the desktop one: the constants are tied to a tracker's measurement
// biases, and this tracker is a different implementation. See
// tools/validate-dsp.mjs and docs/browser-calibration.md.

import { PHONE_STATS, SITE_W1, SITE_W2 } from './phone-stats.js';
import { WEB_CALIBRATION } from './calibration.js';

export const logistic = (x) => 1 / (1 + Math.exp(-x));
export const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));

/** Masc(0)..fem(1) position of a pitch value. */
export function pitchP(f) {
    return logistic((f - 162.0) / 12.0);
}

/** Normalised position of a formant between its masc and fem anchors. */
export function formantR(i, f) {
    const [lo, hi] = WEB_CALIBRATION.formantAnchors[i];
    return clamp((f - lo) / (hi - lo), -0.5, 1.5);
}

export function formantP(i, f) {
    if (i < 0 || i > 2) return 0.5;
    return logistic((formantR(i, f) - 0.5) / 0.25);
}

/**
 * Linear r-space vocal size: ~0 at the masc anchors, ~1 at the fem anchors.
 * This is the fullness page's size axis. It is used instead of siteResonance
 * there because siteResonance self-normalises sustained vowels toward 0.5
 * (its soft phoneme assignment reads a whole-tract shift as a different
 * phoneme), which flattened the maximal small/large contrast to almost
 * nothing when measured against TransVoiceLessons' own demonstrations.
 */
export function resonanceR(f1, f2, f3) {
    return 0.25 * formantR(0, f1) + 0.45 * formantR(1, f2) + 0.30 * formantR(2, f3);
}

export function resonanceP(f1, f2, f3) {
    return logistic((resonanceR(f1, f2, f3) - 0.5) / 0.25);
}

export function overallP(pF0, pRes) {
    return 0.55 * pF0 + 0.45 * pRes;
}

/**
 * Resonance on acousticgender.space's own scale, without knowing the phoneme:
 * map this tracker's F1/F2 into praat units, soft-assign a phoneme by
 * closeness in z-space, and average the site's exact score under that
 * assignment.
 */
export function siteResonance(f1t, f2t) {
    const [m1a, m1b] = WEB_CALIBRATION.praatMapF1;
    const [m2a, m2b] = WEB_CALIBRATION.praatMapF2;
    const f1 = m1a * f1t + m1b;
    const f2 = m2a * f2t + m2b;

    let num = 0, den = 0;
    for (const [pm1, ps1, pm2, ps2] of PHONE_STATS) {
        const z1 = (f1 - pm1) / ps1;
        const z2 = (f2 - pm2) / ps2;
        const w = Math.exp(-0.5 * (z1 * z1 + z2 * z2));
        if (w < 1e-9) continue;
        const r = clamp((SITE_W1 * z1 + SITE_W2 * z2) / 3 + 0.5, 0, 1);
        num += w * r;
        den += w;
    }
    return den > 0 ? clamp(num / den, 0, 1) : 0.5;
}

/** Vocal weight percept: 0 = light (steep roll-off), 1 = heavy (flat). */
export function weightP(tiltDbOct) {
    const { light, heavy } = WEB_CALIBRATION.weight;
    return clamp((tiltDbOct - light) / (heavy - light), 0, 1);
}

/** Blue-grey-pink gradient shared by every visualisation. */
export function genderColor(p) {
    const stops = [[96, 165, 250], [158, 155, 166], [244, 114, 182]];
    if (!(p >= 0 && p <= 1)) return 'rgb(158,155,166)';
    const [a, b, t] = p < 0.5 ? [stops[0], stops[1], p * 2] : [stops[1], stops[2], (p - 0.5) * 2];
    return 'rgb(' + a.map((v, i) => Math.round(v + t * (b[i] - v))).join(',') + ')';
}

const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
export function noteName(f) {
    if (!(f > 0)) return '';
    const midi = Math.round(69 + 12 * Math.log2(f / 440));
    return NOTE_NAMES[((midi % 12) + 12) % 12] + (Math.floor(midi / 12) - 1);
}

/** TransVoiceLessons fullness cell from weight and size. */
export function fullnessCell(weight, size01) {
    const a = (size01 + weight) / 2;          // 0 fem .. 1 masc
    const balance = weight - size01;          // + overfull, - underfull
    const f = balance > 0.28 ? 'O' : balance < -0.28 ? 'U' : 'F';
    const g = a < 0.4 ? 'F' : a > 0.6 ? 'M' : 'A';
    return {
        code: f + g,
        fullness: { U: 'underfull', F: 'full', O: 'overfull' }[f],
        gender: { F: 'feminine', A: 'androgynous', M: 'masculine' }[g],
        androgenization: a,
    };
}
