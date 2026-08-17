// Core voice analysis: pitch, formants, vocal weight.
//
// This mirrors the desktop engine's analysis chain deliberately and in detail,
// because the calibration constants used downstream (formant anchors, the
// praat-space unit maps, the weight anchors) were fitted against that chain's
// specific biases. Where this file departs from the engine, the difference is
// measured rather than assumed: see tools/validate-dsp.mjs, which runs both
// over the same audio and reports the divergence.
//
// Everything here is dependency-free and side-effect-free so it can run in an
// AudioWorklet, in a Worker, or under node for validation.

// ---------------------------------------------------------------- utilities

export function gaussianWindow(n, sigmaFactor = 2.5) {
    // Matches Analysis::gaussianWindow(n, 2.5) in the engine.
    const w = new Float64Array(n);
    const mid = (n - 1) / 2;
    const sigma = mid / sigmaFactor;
    for (let i = 0; i < n; i++) {
        const d = (i - mid) / sigma;
        w[i] = Math.exp(-0.5 * d * d);
    }
    return w;
}

export function hannWindow(n) {
    const w = new Float64Array(n);
    for (let i = 0; i < n; i++) w[i] = 0.5 - 0.5 * Math.cos((2 * Math.PI * i) / (n - 1));
    return w;
}

/** Linear-phase sinc resampler, adequate for the ratios used here. */
export function resample(input, fromRate, toRate) {
    if (fromRate === toRate) return Float64Array.from(input);
    const ratio = toRate / fromRate;
    const outLen = Math.floor(input.length * ratio);
    const out = new Float64Array(outLen);
    // Anti-alias cutoff at the lower Nyquist.
    const cutoff = Math.min(0.5, 0.5 * ratio);
    const halfWidth = 8;
    for (let i = 0; i < outLen; i++) {
        const center = i / ratio;
        const i0 = Math.floor(center);
        let acc = 0, wsum = 0;
        for (let k = i0 - halfWidth; k <= i0 + halfWidth; k++) {
            if (k < 0 || k >= input.length) continue;
            const x = center - k;
            // sinc * Blackman
            const s = x === 0 ? 2 * cutoff : Math.sin(2 * Math.PI * cutoff * x) / (Math.PI * x);
            const t = (x + halfWidth) / (2 * halfWidth);
            const bw = 0.42 - 0.5 * Math.cos(2 * Math.PI * t) + 0.08 * Math.cos(4 * Math.PI * t);
            acc += input[k] * s * bw;
            wsum += s * bw;
        }
        out[i] = wsum > 1e-12 ? acc / wsum : 0;
    }
    return out;
}

// -------------------------------------------------------------------- pitch

/**
 * YIN pitch estimate with parabolic interpolation.
 * Returns {pitch, voiced, clarity}. The engine defaults to RAPT; on clean
 * signals the two agree closely, and validate-dsp.mjs quantifies it.
 */
export function yinPitch(frame, sampleRate, opts = {}) {
    const fmin = opts.fmin ?? 60;
    const fmax = opts.fmax ?? 600;
    const threshold = opts.threshold ?? 0.15;

    const tauMin = Math.max(2, Math.floor(sampleRate / fmax));
    const tauMax = Math.min(Math.floor(frame.length / 2), Math.ceil(sampleRate / fmin));
    if (tauMax <= tauMin) return { pitch: -1, voiced: false, clarity: 0 };

    const W = Math.floor(frame.length / 2);

    // Difference function.
    const d = new Float64Array(tauMax + 1);
    for (let tau = tauMin; tau <= tauMax; tau++) {
        let sum = 0;
        for (let i = 0; i < W; i++) {
            const diff = frame[i] - frame[i + tau];
            sum += diff * diff;
        }
        d[tau] = sum;
    }

    // Cumulative mean normalised difference.
    const cmnd = new Float64Array(tauMax + 1);
    cmnd[0] = 1;
    let running = 0;
    for (let tau = tauMin; tau <= tauMax; tau++) {
        running += d[tau];
        cmnd[tau] = running > 0 ? (d[tau] * (tau - tauMin + 1)) / running : 1;
    }

    // First local minimum below threshold, else global minimum.
    let bestTau = -1;
    for (let tau = tauMin + 1; tau < tauMax; tau++) {
        if (cmnd[tau] < threshold && cmnd[tau] <= cmnd[tau + 1]) { bestTau = tau; break; }
    }
    if (bestTau < 0) {
        let min = Infinity;
        for (let tau = tauMin; tau <= tauMax; tau++) {
            if (cmnd[tau] < min) { min = cmnd[tau]; bestTau = tau; }
        }
        if (min > 0.6) return { pitch: -1, voiced: false, clarity: 1 - min };
    }

    // Parabolic interpolation around the minimum.
    let tau = bestTau;
    if (tau > tauMin && tau < tauMax) {
        const a = cmnd[tau - 1], b = cmnd[tau], c = cmnd[tau + 1];
        const denom = 2 * (2 * b - a - c);
        if (Math.abs(denom) > 1e-12) tau = tau + (c - a) / denom;
    }

    const pitch = sampleRate / tau;
    const clarity = 1 - cmnd[bestTau];
    const voiced = pitch >= fmin && pitch <= fmax && clarity > 0.4;
    return { pitch: voiced ? pitch : -1, voiced, clarity };
}

// ----------------------------------------------------------------- formants

/** Burg linear prediction (the engine's default linpred algorithm). */
export function burgLPC(x, order) {
    const n = x.length;
    if (n <= order + 1) return null;

    const a = new Float64Array(order + 1);
    a[0] = 1;
    let f = Float64Array.from(x);
    let b = Float64Array.from(x);

    let dEn = 0;
    for (let i = 0; i < n; i++) dEn += 2 * x[i] * x[i];
    dEn -= x[0] * x[0] + x[n - 1] * x[n - 1];
    if (dEn <= 0) return null;

    for (let m = 1; m <= order; m++) {
        let num = 0;
        for (let i = m; i < n; i++) num += f[i] * b[i - 1];
        num *= 2;
        const k = dEn !== 0 ? num / dEn : 0;

        const aPrev = Float64Array.from(a);
        a[m] = -k;
        for (let i = 1; i < m; i++) a[i] = aPrev[i] - k * aPrev[m - i];

        // Both f and b must be read from their previous state: updating b in
        // place would feed already-updated values back into the recursion.
        const fPrev = Float64Array.from(f);
        const bPrev = Float64Array.from(b);
        for (let i = m; i < n; i++) {
            f[i] = fPrev[i] - k * bPrev[i - 1];
            b[i] = bPrev[i - 1] - k * fPrev[i];
        }

        dEn = (1 - k * k) * dEn - f[m] * f[m] - b[n - 1] * b[n - 1];
        if (dEn <= 0) break;
    }
    // Return prediction coefficients a[1..order] (a[0] == 1).
    return a;
}

/** Durand-Kerner root finding; stands in for the engine's Aberth solver. */
export function polynomialRoots(coeffs) {
    // coeffs[0] * z^n + ... + coeffs[n]
    const n = coeffs.length - 1;
    if (n < 1) return [];
    const c = coeffs.map((v) => v / coeffs[0]);

    let re = new Float64Array(n), im = new Float64Array(n);
    // Spread initial guesses on a circle (classic 0.4+0.9i powers).
    let pr = 1, pi = 0;
    for (let i = 0; i < n; i++) {
        re[i] = pr; im[i] = pi;
        const nr = pr * 0.4 - pi * 0.9;
        const ni = pr * 0.9 + pi * 0.4;
        pr = nr; pi = ni;
    }

    const evalPoly = (zr, zi) => {
        let yr = c[0], yi = 0;
        for (let k = 1; k <= n; k++) {
            const t = yr * zr - yi * zi + c[k];
            yi = yr * zi + yi * zr;
            yr = t;
        }
        return [yr, yi];
    };

    for (let iter = 0; iter < 200; iter++) {
        let maxDelta = 0;
        for (let i = 0; i < n; i++) {
            const [yr, yi] = evalPoly(re[i], im[i]);
            // denominator = prod_{j != i} (z_i - z_j)
            let dr = 1, di = 0;
            for (let j = 0; j < n; j++) {
                if (j === i) continue;
                const ar = re[i] - re[j], ai = im[i] - im[j];
                const t = dr * ar - di * ai;
                di = dr * ai + di * ar;
                dr = t;
            }
            const den = dr * dr + di * di;
            if (den < 1e-300) continue;
            const qr = (yr * dr + yi * di) / den;
            const qi = (yi * dr - yr * di) / den;
            re[i] -= qr; im[i] -= qi;
            const delta = Math.abs(qr) + Math.abs(qi);
            if (delta > maxDelta) maxDelta = delta;
        }
        if (maxDelta < 1e-14) break;
    }

    const roots = [];
    for (let i = 0; i < n; i++) roots.push({ re: re[i], im: im[i] });
    return roots;
}

/**
 * Formant estimates from a frame, following the engine's chain:
 * 200 Hz pre-emphasis, Gaussian window, resample to 11 kHz, Burg LPC,
 * root picking with radius in [0.6, 1) away from DC and Nyquist.
 */
export function formants(frame, sampleRate, opts = {}) {
    const fsLPC = opts.fsLPC ?? 11000;
    const preemphFrequency = 200.0;

    const preemphFactor = Math.exp((-2.0 * Math.PI * preemphFrequency) / sampleRate);
    const win = gaussianWindow(frame.length, 2.5);
    const x = new Float64Array(frame.length);
    for (let i = frame.length - 1; i >= 1; i--) {
        x[i] = win[i] * (frame[i] - preemphFactor * frame[i - 1]);
    }
    x[0] = win[0] * frame[0];

    const lp = resample(x, sampleRate, fsLPC);
    // Engine uses order = fs/1000 + lpOffset(1), i.e. 12 at 11 kHz.
    const order = opts.order ?? Math.round(fsLPC / 1000) + 1;
    const a = burgLPC(lp, order);
    if (!a) return [];

    const roots = polynomialRoots(Array.from(a));
    const phiDelta = (2.0 * 50.0 * Math.PI) / fsLPC;

    // Root picking. Radius alone (the engine's first-pass criterion, which it
    // then repairs with a Cauchy-integral peak-splitting stage) also admits
    // broad, shallow resonances that sit between the real formants and would
    // shift F2/F3 to the wrong root. Requiring a formant-like bandwidth is the
    // conventional criterion and separates them cleanly: on a synthetic vowel
    // the true poles measure 165-292 Hz of bandwidth while the spurious ones
    // measure 1100-1700 Hz.
    const maxBandwidth = opts.maxBandwidth ?? 600;
    const out = [];
    for (const z of roots) {
        if (z.im < 0) continue;
        const r = Math.hypot(z.re, z.im);
        const phi = Math.atan2(z.im, z.re);
        if (r >= 0.6 && r < 1.0 && phi > phiDelta && phi < Math.PI - phiDelta) {
            const frequency = (phi * fsLPC) / (2 * Math.PI);
            const bandwidth = (-Math.log(r) * fsLPC) / Math.PI;
            if (bandwidth <= maxBandwidth) out.push({ frequency, bandwidth });
        }
    }
    out.sort((p, q) => p.frequency - q.frequency);
    return out;
}

// -------------------------------------------------------------- vocal weight

/**
 * Harmonic spectral roll-off in dB/octave: flat = heavy/buzzy, steep = light.
 *
 * Two properties matter and both were learned the hard way on real recordings:
 * harmonics must clear the inter-harmonic noise floor (otherwise a light or
 * breathy voice has noise measured as its upper harmonics and reads HEAVY),
 * and the fit must be robust (Theil-Sen), so a single surviving noise peak
 * cannot drag the slope. No vocal-tract correction is applied: correcting with
 * *tracked* formants injects more error than it removes on exactly the light
 * voices that matter, and the uncorrected slope differs from the ideal by a
 * near-constant offset the anchors absorb.
 */
export function spectralTilt(frame, sampleRate, f0, fft) {
    if (!(f0 > 40)) return null;

    const mag = fft(frame);
    const nfft = (mag.length - 1) * 2;
    const binHz = sampleRate / nfft;

    const fks = [], amps = [];
    for (let k = 1; k <= 14; k++) {
        const fk = k * f0;
        if (fk >= Math.min(3200, sampleRate / 2 - 100)) break;
        if (fk < 160) continue; // recording highpass region corrupts amplitudes

        const lo = Math.max(1, Math.floor((fk - 0.35 * f0) / binHz));
        const hi = Math.min(mag.length - 1, Math.floor((fk + 0.35 * f0) / binHz) + 1);
        if (hi <= lo) continue;

        let peak = 0, peakF = fk;
        for (let b = lo; b <= hi; b++) {
            if (mag[b] > peak) { peak = mag[b]; peakF = b * binHz; }
        }
        if (peak <= 1e-12) continue;

        // Noise floor from the valleys either side.
        let floor = 0, count = 0;
        for (const half of [fk - 0.5 * f0, fk + 0.5 * f0]) {
            const flo = Math.max(1, Math.floor((half - 0.15 * f0) / binHz));
            const fhi = Math.min(mag.length - 1, Math.floor((half + 0.15 * f0) / binHz) + 1);
            for (let b = flo; b <= fhi; b++) { floor += mag[b]; count++; }
        }
        if (count > 0) {
            floor /= count;
            if (peak < 3.2 * floor) continue; // < ~10 dB over the valley: not a harmonic
        }

        fks.push(peakF);
        amps.push(20 * Math.log10(peak));
    }

    if (fks.length < 3) return null;

    // Theil-Sen: median of pairwise slopes in dB per octave.
    const slopes = [];
    for (let i = 0; i < fks.length; i++) {
        for (let j = i + 1; j < fks.length; j++) {
            const dx = Math.log2(fks[j] / fks[i]);
            if (Math.abs(dx) > 1e-9) slopes.push((amps[j] - amps[i]) / dx);
        }
    }
    if (!slopes.length) return null;
    slopes.sort((a, b) => a - b);
    const slope = slopes[slopes.length >> 1];
    if (slope < -40 || slope > 15) return null;
    return slope;
}

// ------------------------------------------------------------ tiny real FFT

/** Radix-2 magnitude spectrum. Returns nfft/2+1 magnitudes. */
export function makeFFT(nfft) {
    const cos = new Float64Array(nfft / 2), sin = new Float64Array(nfft / 2);
    for (let i = 0; i < nfft / 2; i++) {
        cos[i] = Math.cos((-2 * Math.PI * i) / nfft);
        sin[i] = Math.sin((-2 * Math.PI * i) / nfft);
    }
    const rev = new Uint32Array(nfft);
    let bits = Math.log2(nfft) | 0;
    for (let i = 0; i < nfft; i++) {
        let x = i, r = 0;
        for (let b = 0; b < bits; b++) { r = (r << 1) | (x & 1); x >>= 1; }
        rev[i] = r;
    }
    const re = new Float64Array(nfft), im = new Float64Array(nfft);
    const win = hannWindow(nfft);

    return function fft(frame) {
        const n = Math.min(frame.length, nfft);
        re.fill(0); im.fill(0);
        for (let i = 0; i < n; i++) re[rev[i]] = frame[i] * win[i];
        for (let i = n; i < nfft; i++) re[rev[i]] = 0;

        for (let size = 2; size <= nfft; size <<= 1) {
            const half = size >> 1, step = nfft / size;
            for (let i = 0; i < nfft; i += size) {
                for (let j = i, k = 0; j < i + half; j++, k += step) {
                    const tr = re[j + half] * cos[k] - im[j + half] * sin[k];
                    const ti = re[j + half] * sin[k] + im[j + half] * cos[k];
                    re[j + half] = re[j] - tr; im[j + half] = im[j] - ti;
                    re[j] += tr; im[j] += ti;
                }
            }
        }
        const out = new Float64Array(nfft / 2 + 1);
        for (let i = 0; i <= nfft / 2; i++) out[i] = Math.hypot(re[i], im[i]);
        return out;
    };
}
