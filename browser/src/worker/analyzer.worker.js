// Analysis worker: the same measurement chain the desktop engine runs, off the
// audio thread. Receives hop-sized blocks, maintains a sliding window, and
// emits one measurement per hop with the same smoothing windows the desktop
// HUD uses (0.35 s pitch median, 1 s formants, 2 s for the gender read) so the
// numbers are directly comparable.

import { yinPitch, formants, spectralTilt, makeFFT } from '../dsp/core.js';
import {
    pitchP, resonanceP, resonanceR, overallP, siteResonance, weightP,
} from '../dsp/gender.js';

let sampleRate = 48000;
let winSamples = 0;
let window_ = null;   // sliding analysis window
let fft = null;

const history = [];   // {t, pitch, f1, f2, f3, tilt}
let t = 0;

const HOP_S = 0.02;
const WIN_S = 0.04;

function medianIn(key, span, minCount = 1) {
    const cutoff = t - span;
    const vals = [];
    for (let i = history.length - 1; i >= 0; i--) {
        if (history[i].t < cutoff) break;
        const v = history[i][key];
        if (v !== null && v !== undefined && v > 0) vals.push(v);
    }
    if (vals.length < minCount) return null;
    vals.sort((a, b) => a - b);
    return vals[vals.length >> 1];
}

function medianTilt(span) {
    const cutoff = t - span;
    const vals = [];
    for (let i = history.length - 1; i >= 0; i--) {
        if (history[i].t < cutoff) break;
        if (history[i].tilt !== null && history[i].tilt !== undefined) vals.push(history[i].tilt);
    }
    if (!vals.length) return null;
    vals.sort((a, b) => a - b);
    return vals[vals.length >> 1];
}

self.onmessage = (e) => {
    const msg = e.data;

    if (msg.type === 'init') {
        sampleRate = msg.sampleRate;
        winSamples = Math.round(WIN_S * sampleRate);
        window_ = new Float64Array(winSamples);
        fft = makeFFT(4096);
        return;
    }

    if (msg.type === 'block') {
        const block = msg.block;
        // Slide the window.
        const n = block.length;
        if (n >= winSamples) {
            for (let i = 0; i < winSamples; i++) window_[i] = block[n - winSamples + i];
        } else {
            window_.copyWithin(0, n);
            for (let i = 0; i < n; i++) window_[winSamples - n + i] = block[i];
        }
        t += n / sampleRate;

        const p = yinPitch(window_, sampleRate);
        let f1 = null, f2 = null, f3 = null, tilt = null;

        if (p.voiced) {
            const f = formants(window_, sampleRate);
            if (f.length > 0) f1 = f[0].frequency;
            if (f.length > 1) f2 = f[1].frequency;
            if (f.length > 2) f3 = f[2].frequency;
            tilt = spectralTilt(window_, sampleRate, p.pitch, fft);
        }

        history.push({ t, pitch: p.voiced ? p.pitch : null, f1, f2, f3, tilt });
        while (history.length && history[0].t < t - 12) history.shift();

        // Displayed values, on the desktop engine's windows.
        const dPitch = medianIn('pitch', 0.35);
        const dF1 = medianIn('f1', 1.0);
        const dF2 = medianIn('f2', 1.0);
        const dF3 = medianIn('f3', 1.0);
        const gPitch = medianIn('pitch', 2.0);
        const gF1 = medianIn('f1', 2.0);
        const gF2 = medianIn('f2', 2.0);
        const gF3 = medianIn('f3', 2.0);
        const dTilt = medianTilt(1.0);

        const out = {
            type: 'measurement',
            t,
            voiced: p.voiced,
            clarity: p.clarity,
            pitch: dPitch ?? -1,
            f1: dF1 ?? -1, f2: dF2 ?? -1, f3: dF3 ?? -1,
            pitchScore: -1, resScore: -1, score: -1,
            resonance: -1, sizeR: -999, tilt: -999, weight: -1,
        };

        if (gPitch && gF1 && gF2 && gF3) {
            const pF0 = pitchP(gPitch);
            const pRes = resonanceP(gF1, gF2, gF3);
            out.pitchScore = pF0;
            out.resScore = pRes;
            out.score = overallP(pF0, pRes);
            out.resonance = siteResonance(gF1, gF2);
            out.sizeR = resonanceR(gF1, gF2, gF3);
        }
        if (dTilt !== null && dPitch) {
            out.tilt = dTilt;
            out.weight = weightP(dTilt);
        }

        self.postMessage(out);
    }

    if (msg.type === 'reset') {
        history.length = 0;
        t = 0;
    }
};
