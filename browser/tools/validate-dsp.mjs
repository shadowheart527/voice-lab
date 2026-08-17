// Validate and calibrate the browser DSP against ground truth.
//
// The desktop engine's calibration constants are tied to ITS tracker's
// measurement biases and must not simply be inherited by a different
// implementation. This runs the browser tracker over acousticgender.space's
// 21 reference clips at the phoneme midpoints where the original pipeline
// recorded praat's F1/F2, fits the tracker->praat unit maps by the same robust
// procedure the desktop calibration used, then checks the resulting resonance
// scale against the official per-clip medians. It also derives the vocal
// weight anchors from TransVoiceLessons' own labelled light/heavy demos.
//
// Usage: node validate-dsp.mjs <refclips-dir> [tvl-demos-dir]
// Writes ../src/dsp/calibration.js

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { formants, yinPitch, spectralTilt, makeFFT } from '../src/dsp/core.js';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const CLIPDIR = process.argv[2];
const DEMODIR = process.argv[3];

// ------------------------------------------------------------------ wav I/O

function readWav(file) {
    const buf = fs.readFileSync(file);
    if (buf.toString('ascii', 0, 4) !== 'RIFF') throw new Error('not RIFF: ' + file);
    let pos = 12, fmt = null, dataOff = 0, dataLen = 0;
    while (pos + 8 <= buf.length) {
        const id = buf.toString('ascii', pos, pos + 4);
        const size = buf.readUInt32LE(pos + 4);
        if (id === 'fmt ') {
            fmt = {
                format: buf.readUInt16LE(pos + 8),
                channels: buf.readUInt16LE(pos + 10),
                rate: buf.readUInt32LE(pos + 12),
                bits: buf.readUInt16LE(pos + 22),
            };
        } else if (id === 'data') { dataOff = pos + 8; dataLen = size; }
        pos += 8 + size + (size % 2);
    }
    if (!fmt || !dataOff) throw new Error('malformed wav: ' + file);
    if (fmt.bits !== 16) throw new Error('expected 16-bit: ' + file);
    const n = Math.floor(dataLen / 2 / fmt.channels);
    const out = new Float64Array(n);
    for (let i = 0; i < n; i++) {
        let acc = 0;
        for (let c = 0; c < fmt.channels; c++) {
            acc += buf.readInt16LE(dataOff + (i * fmt.channels + c) * 2);
        }
        out[i] = acc / fmt.channels / 32768;
    }
    return { rate: fmt.rate, samples: out };
}

function readPhones(file) {
    const lines = fs.readFileSync(file, 'utf8').trim().split('\n').slice(1);
    const rows = lines.map((l) => {
        const c = l.split('\t');
        return {
            t: parseFloat(c[0]),
            phoneme: c[1],
            word: c[2],
            f0: c[3] ? parseFloat(c[3]) : null,
            f1: c[4] ? parseFloat(c[4]) : null,
            f2: c[5] ? parseFloat(c[5]) : null,
            f3: c[6] ? parseFloat(c[6]) : null,
            res: c[7] ? parseFloat(c[7]) : null,
        };
    });
    for (let i = 0; i < rows.length; i++) {
        rows[i].end = i + 1 < rows.length ? rows[i + 1].t : rows[i].t + 0.15;
        rows[i].mid = (rows[i].t + rows[i].end) / 2;
    }
    return rows;
}

// ------------------------------------------------------------------- stats

const median = (a) => {
    if (!a.length) return NaN;
    const s = Float64Array.from(a).sort();
    return s[s.length >> 1];
};

/** Robust (IRLS/Huber) straight-line fit, matching the desktop calibration. */
function robustFit(xs, ys) {
    let w = new Array(xs.length).fill(1);
    let m = 0, b = 0;
    for (let iter = 0; iter < 6; iter++) {
        let sw = 0, swx = 0, swy = 0, swxx = 0, swxy = 0;
        for (let i = 0; i < xs.length; i++) {
            sw += w[i]; swx += w[i] * xs[i]; swy += w[i] * ys[i];
            swxx += w[i] * xs[i] * xs[i]; swxy += w[i] * xs[i] * ys[i];
        }
        const den = sw * swxx - swx * swx;
        if (Math.abs(den) < 1e-12) break;
        m = (sw * swxy - swx * swy) / den;
        b = (swy - m * swx) / sw;
        const resid = xs.map((x, i) => Math.abs(m * x + b - ys[i]));
        const s = Math.max(median(resid) * 1.4826, 1e-6);
        w = resid.map((r) => Math.min(1, (1.5 * s) / Math.max(r, 1e-9)));
    }
    return [m, b];
}

// ------------------------------------------------------- measure the clips

const WIN_S = 0.030; // analysis window for formants at the phone midpoint

function measureClip(wav, phones) {
    const { rate, samples } = wav;
    const half = Math.round((WIN_S * rate) / 2);
    const rows = [];
    for (const p of phones) {
        if (p.res === null || p.f1 === null || p.f2 === null) continue;
        if (!p.f0 || p.f0 < 55 || p.f0 > 500) continue;
        if (['sil', 'sp', ''].includes(p.phoneme)) continue;
        const c = Math.round(p.mid * rate);
        if (c - half < 0 || c + half >= samples.length) continue;
        const frame = samples.subarray(c - half, c + half);
        const f = formants(frame, rate);
        if (f.length < 2) continue;
        rows.push({
            phoneme: p.phoneme, word: p.word, mid: p.mid,
            praatF1: p.f1, praatF2: p.f2, praatF0: p.f0, siteRes: p.res,
            trkF1: f[0].frequency, trkF2: f[1].frequency,
            trkF3: f.length > 2 ? f[2].frequency : null,
        });
    }
    return rows;
}

// --------------------------------------------------------------------- run

const clips = fs.readdirSync(CLIPDIR).filter((f) => f.endsWith('.phones.tsv'))
    .map((f) => f.replace('.phones.tsv', ''));

const official = {};
for (const line of fs.readFileSync(path.join(CLIPDIR, 'summary.tsv'), 'utf8').trim().split('\n').slice(1)) {
    const c = line.split('\t');
    official[c[0]] = parseFloat(c[5]);
}

console.log('measuring browser tracker over reference clips...');
const all = [];
const perClip = {};
for (const c of clips) {
    const wavPath = path.join(CLIPDIR, `${c}.wav`);
    if (!fs.existsSync(wavPath)) continue;
    const rows = measureClip(readWav(wavPath), readPhones(path.join(CLIPDIR, `${c}.phones.tsv`)));
    rows.forEach((r) => { r.clip = c; });
    perClip[c] = rows;
    all.push(...rows);
    console.log(`  ${c.padEnd(10)} ${String(rows.length).padStart(4)} phones`);
}
console.log(`total paired phones: ${all.length}`);

// Pitch sanity: browser tracker vs praat.
const pitchErr = [];
for (const c of clips) {
    const wavPath = path.join(CLIPDIR, `${c}.wav`);
    if (!fs.existsSync(wavPath)) continue;
    const { rate, samples } = readWav(wavPath);
    const half = Math.round(0.04 * rate);
    for (const p of readPhones(path.join(CLIPDIR, `${c}.phones.tsv`))) {
        if (!p.f0 || p.f0 < 55 || p.f0 > 500) continue;
        const ctr = Math.round(p.mid * rate);
        if (ctr - half < 0 || ctr + half >= samples.length) continue;
        const r = yinPitch(samples.subarray(ctr - half, ctr + half), rate);
        if (r.voiced) pitchErr.push(Math.abs(r.pitch - p.f0));
    }
}
console.log(`pitch vs praat: MAE ${(pitchErr.reduce((a, b) => a + b, 0) / pitchErr.length).toFixed(1)} Hz, median ${median(pitchErr).toFixed(1)} Hz (n=${pitchErr.length})`);

// Fit tracker -> praat unit maps.
const mapF1 = robustFit(all.map((r) => r.trkF1), all.map((r) => r.praatF1));
const mapF2 = robustFit(all.map((r) => r.trkF2), all.map((r) => r.praatF2));
console.log(`F1: praat = ${mapF1[0].toFixed(4)} * trk ${mapF1[1] >= 0 ? '+' : ''}${mapF1[1].toFixed(1)}`);
console.log(`F2: praat = ${mapF2[0].toFixed(4)} * trk ${mapF2[1] >= 0 ? '+' : ''}${mapF2[1].toFixed(1)}`);

// Evaluate the resulting site-resonance scale against official clip medians.
const { PHONE_STATS, SITE_W1, SITE_W2 } = await import('../src/dsp/phone-stats.js');
function siteResWith(f1t, f2t, m1, m2) {
    const f1 = m1[0] * f1t + m1[1];
    const f2 = m2[0] * f2t + m2[1];
    let num = 0, den = 0;
    for (const [pm1, ps1, pm2, ps2] of PHONE_STATS) {
        const z1 = (f1 - pm1) / ps1, z2 = (f2 - pm2) / ps2;
        const w = Math.exp(-0.5 * (z1 * z1 + z2 * z2));
        if (w < 1e-9) continue;
        num += w * Math.max(0, Math.min(1, (SITE_W1 * z1 + SITE_W2 * z2) / 3 + 0.5));
        den += w;
    }
    return den > 0 ? num / den : 0.5;
}

// Leave-one-clip-out: refit the maps without each clip, then score it.
console.log('\nleave-one-clip-out check against official medians:');
const errs = [];
for (const c of Object.keys(perClip)) {
    if (!perClip[c].length || official[c] === undefined) continue;
    const tr = all.filter((r) => r.clip !== c);
    const m1 = robustFit(tr.map((r) => r.trkF1), tr.map((r) => r.praatF1));
    const m2 = robustFit(tr.map((r) => r.trkF2), tr.map((r) => r.praatF2));
    const pred = median(perClip[c].map((r) => siteResWith(r.trkF1, r.trkF2, m1, m2)));
    const err = pred - official[c];
    errs.push(Math.abs(err));
    console.log(`  ${c.padEnd(10)} predicted ${pred.toFixed(3)}  official ${official[c].toFixed(3)}  err ${err >= 0 ? '+' : ''}${err.toFixed(3)}`);
}
console.log(`clip-median MAE ${(errs.reduce((a, b) => a + b, 0) / errs.length).toFixed(4)}  worst ${Math.max(...errs).toFixed(4)}`);

// ------------------------------------------- weight anchors from TVL demos
let weight = { light: -10.5, heavy: -2.5 };
if (DEMODIR && fs.existsSync(DEMODIR)) {
    console.log('\nvocal weight from the TransVoiceLessons demonstrations:');
    const fft = makeFFT(4096);
    const measureTilt = (file) => {
        const { rate, samples } = readWav(file);
        const half = Math.round(0.02 * rate);
        const step = Math.round(0.02 * rate);
        const vals = [];
        for (let c = half; c + half < samples.length; c += step) {
            const frame = samples.subarray(c - half, c + half);
            const p = yinPitch(frame, rate);
            if (!p.voiced) continue;
            const t = spectralTilt(frame, rate, p.pitch, fft);
            if (t !== null) vals.push(t);
        }
        return vals;
    };
    for (const name of ['light', 'heavy']) {
        const f = path.join(DEMODIR, `${name}.wav`);
        if (!fs.existsSync(f)) continue;
        const v = measureTilt(f);
        if (v.length) {
            weight[name] = median(v);
            console.log(`  ${name.padEnd(6)} ${weight[name].toFixed(2)} dB/oct (n=${v.length})`);
        }
    }
    // Keep light < heavy; fall back if the demos disagree.
    if (!(weight.heavy > weight.light)) {
        console.log('  demos did not separate; keeping provisional anchors');
        weight = { light: -10.5, heavy: -2.5 };
    }
}

// ------------------------------------------------------------------- emit
const outPath = path.join(__dirname, '..', 'src', 'dsp', 'calibration.js');
fs.writeFileSync(outPath, `// GENERATED by tools/validate-dsp.mjs -- do not edit by hand.
//
// Derived for THIS tracker (browser implementation) rather than inherited from
// the desktop engine: calibration constants encode a tracker's measurement
// biases, so they do not transfer between implementations.
//
// Measured on acousticgender.space's ${Object.keys(perClip).length} reference clips,
// ${all.length} paired phones. Leave-one-clip-out agreement with the official
// per-clip resonance medians: ${(errs.reduce((a, b) => a + b, 0) / errs.length).toFixed(4)} mean absolute error,
// worst clip ${Math.max(...errs).toFixed(4)}. Pitch vs praat: ${(pitchErr.reduce((a, b) => a + b, 0) / pitchErr.length).toFixed(1)} Hz MAE.
// Weight anchors from the TransVoiceLessons light/heavy demonstrations.
export const WEB_CALIBRATION = {
    formantAnchors: [[420.0, 550.0], [1350.0, 1650.0], [2700.0, 3150.0]],
    praatMapF1: [${mapF1[0].toFixed(6)}, ${mapF1[1].toFixed(4)}],
    praatMapF2: [${mapF2[0].toFixed(6)}, ${mapF2[1].toFixed(4)}],
    weight: { light: ${weight.light.toFixed(2)}, heavy: ${weight.heavy.toFixed(2)} },
    provisional: false,
};
`);
console.log(`\nwrote ${outPath}`);
