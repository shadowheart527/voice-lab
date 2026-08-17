// Analysis worker.
//
// This runs the desktop engine's own analysis code, compiled to WebAssembly:
// the same RAPT pitch solver, the same Burg linear prediction, the same
// FilteredLP formant solver, the same spectral-tilt measure and the same
// gender model, linked against the same FFTW and libsamplerate. There is no
// second implementation to drift, and no separate calibration, because the
// constants are compiled in from engine/src/context/genderspace_table.h.
//
// The pure-JavaScript implementation that used to live here is kept in
// src/dsp/ as a fallback for browsers where WebAssembly is unavailable, and as
// an independent cross-check in the validation harness.

import createVoiceLab from '../../wasm/voicelab.mjs';

let mod = null;
let api = null;
let sampleRate = 48000;
let winSamples = 0;
let framePtr = 0;
let specPtr = 0;
let window_ = null;

const history = [];
let t = 0;

const HOP_S = 0.02;
const WIN_S = 0.04;

// Desktop config enum values: RAPT pitch, Burg linear prediction, FilteredLP
// formants. These are the desktop application's defaults.
const ALG_PITCH = 2, ALG_LINPRED = 2, ALG_FORMANT = 1;

// Spectrogram view parameters, matching the desktop defaults.
const SPEC_MAX_HZ = 5000, SPEC_FFT = 2048, SPEC_BINS = SPEC_FFT / 2 + 1;

function medianIn(key, span) {
    const cutoff = t - span;
    const vals = [];
    for (let i = history.length - 1; i >= 0; i--) {
        if (history[i].t < cutoff) break;
        const v = history[i][key];
        if (v !== null && v !== undefined && v > 0) vals.push(v);
    }
    if (!vals.length) return null;
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

async function init(rate) {
    sampleRate = rate;
    winSamples = Math.round(WIN_S * sampleRate);
    window_ = new Float32Array(winSamples);

    mod = await createVoiceLab();
    api = {
        init: mod.cwrap('vl_init', null, ['number', 'number', 'number', 'number']),
        analyze: mod.cwrap('vl_analyze', null, ['number', 'number']),
        voiced: mod.cwrap('vl_voiced', 'number', []),
        pitch: mod.cwrap('vl_pitch', 'number', []),
        f1: mod.cwrap('vl_f1', 'number', []),
        f2: mod.cwrap('vl_f2', 'number', []),
        f3: mod.cwrap('vl_f3', 'number', []),
        tilt: mod.cwrap('vl_tilt', 'number', []),
        pitchScore: mod.cwrap('vl_pitch_score', 'number', ['number']),
        resScore: mod.cwrap('vl_resonance_score', 'number', ['number', 'number', 'number']),
        resR: mod.cwrap('vl_resonance_r', 'number', ['number', 'number', 'number']),
        overall: mod.cwrap('vl_overall_score', 'number', ['number', 'number']),
        siteRes: mod.cwrap('vl_site_resonance', 'number', ['number', 'number', 'number']),
        weight: mod.cwrap('vl_weight', 'number', ['number']),
        spectrogram: mod.cwrap('vl_spectrogram', 'number',
                ['number', 'number', 'number', 'number', 'number', 'number']),
    };
    framePtr = mod._malloc(winSamples * 4);
    specPtr = mod._malloc(SPEC_BINS * 4);
    api.init(sampleRate, ALG_PITCH, ALG_LINPRED, ALG_FORMANT);
    self.postMessage({ type: 'ready', engine: 'wasm' });
}

self.onmessage = async (e) => {
    const msg = e.data;

    if (msg.type === 'init') {
        await init(msg.sampleRate);
        return;
    }

    if (msg.type === 'reset') {
        history.length = 0;
        t = 0;
        return;
    }

    if (msg.type !== 'block' || !api) return;

    // Slide the analysis window.
    const block = msg.block;
    const n = block.length;
    if (n >= winSamples) {
        window_.set(block.subarray(n - winSamples));
    } else {
        window_.copyWithin(0, n);
        window_.set(block, winSamples - n);
    }
    t += n / sampleRate;

    mod.HEAPF32.set(window_, framePtr >> 2);
    api.analyze(framePtr, winSamples);

    const nBins = api.spectrogram(framePtr, winSamples, SPEC_MAX_HZ, SPEC_FFT, specPtr, SPEC_BINS);
    const spectrum = nBins > 0
        ? Float32Array.from(mod.HEAPF32.subarray(specPtr >> 2, (specPtr >> 2) + nBins))
        : null;

    const voiced = api.voiced() !== 0;
    const rawTilt = api.tilt();
    history.push({
        t,
        pitch: voiced ? api.pitch() : null,
        f1: api.f1() > 0 ? api.f1() : null,
        f2: api.f2() > 0 ? api.f2() : null,
        f3: api.f3() > 0 ? api.f3() : null,
        tilt: rawTilt > -900 ? rawTilt : null,
    });
    while (history.length && history[0].t < t - 12) history.shift();

    // Same smoothing windows the desktop HUD uses, so the two show the same
    // numbers rather than merely computing them the same way.
    const dPitch = medianIn('pitch', 0.35);
    const gPitch = medianIn('pitch', 2.0);
    const gF1 = medianIn('f1', 2.0), gF2 = medianIn('f2', 2.0), gF3 = medianIn('f3', 2.0);
    const dTilt = medianTilt(1.0);

    const out = {
        type: 'measurement',
        t,
        voiced,
        pitch: dPitch ?? -1,
        f1: medianIn('f1', 1.0) ?? -1,
        f2: medianIn('f2', 1.0) ?? -1,
        f3: medianIn('f3', 1.0) ?? -1,
        pitchScore: -1, resScore: -1, score: -1,
        resonance: -1, sizeR: -999, tilt: -999, weight: -1,
    };

    if (gPitch && gF1 && gF2 && gF3) {
        const pF0 = api.pitchScore(gPitch);
        const pRes = api.resScore(gF1, gF2, gF3);
        out.pitchScore = pF0;
        out.resScore = pRes;
        out.score = api.overall(pF0, pRes);
        out.resonance = api.siteRes(gF1, gF2, 0);
        out.sizeR = api.resR(gF1, gF2, gF3);
    }
    if (dTilt !== null && dPitch) {
        out.tilt = dTilt;
        out.weight = api.weight(dTilt);
    }

    if (spectrum) {
        out.spectrum = spectrum;
        out.specMaxHz = SPEC_MAX_HZ;
        self.postMessage(out, [spectrum.buffer]);
    } else {
        self.postMessage(out);
    }
};
