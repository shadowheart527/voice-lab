// Perceived-gender probe in the browser: an ECAPA-TDNN gender classifier run
// over overlapping windows, its posterior averaged into a continuous read
// (the Voice Passing method, arXiv 2404.15176).
//
// This answers a different question from the acoustic measures elsewhere.
// Those say what your voice is doing; this says what a machine listener
// trained on a lot of voices does with the result, which is the part the
// two-cue acoustic model cannot see (prosody, phonation, everything at once).
//
// Everything is optional and lazy: if the model file is absent the app simply
// does not show the reading. Nothing here blocks the DSP path.
//
// Caveats live in ml/gender_probe/README.md and are not decoration. The model
// is binary by construction, trained on VoxCeleb2 celebrity audio with
// cis-normative labels, has effectively never seen a voice mid-transition, and
// its decision boundary on the reference corpus sits near 0.65, not 0.5. It is
// an ordinal signal to watch move, not a verdict and not a percentage of
// listeners.

const MODEL_URL = './models/ecapa_gender_int8.onnx';
const TARGET_RATE = 16000;
const WINDOW_S = 1.0;
const SILENCE_DBFS = -45;

let ort = null;
let session = null;
let loading = null;

/** Kaiser-windowed sinc resample to 16 kHz, matching the Python probe. */
function resampleTo16k(input, rate) {
    if (rate === TARGET_RATE) return Float32Array.from(input);
    const ratio = TARGET_RATE / rate;
    const outLen = Math.floor(input.length * ratio);
    const out = new Float32Array(outLen);
    const cutoff = Math.min(0.5, 0.5 * ratio);
    const half = 12;
    for (let i = 0; i < outLen; i++) {
        const center = i / ratio;
        const i0 = Math.floor(center);
        let acc = 0, wsum = 0;
        for (let k = i0 - half; k <= i0 + half; k++) {
            if (k < 0 || k >= input.length) continue;
            const x = center - k;
            const s = x === 0 ? 2 * cutoff : Math.sin(2 * Math.PI * cutoff * x) / (Math.PI * x);
            const t = (x + half) / (2 * half);
            const w = 0.42 - 0.5 * Math.cos(2 * Math.PI * t) + 0.08 * Math.cos(4 * Math.PI * t);
            acc += input[k] * s * w;
            wsum += s * w;
        }
        out[i] = wsum > 1e-12 ? acc / wsum : 0;
    }
    return out;
}

export async function load() {
    if (session) return session;
    if (loading) return loading;
    loading = (async () => {
        // Probe for the model first: a 16 MB download should not start unless
        // it is actually there.
        const head = await fetch(MODEL_URL, { method: 'HEAD' });
        if (!head.ok) throw new Error('model not present');

        ort = await import('../../vendor/ort.wasm.min.mjs');
        ort.env.wasm.wasmPaths = '../vendor/';
        ort.env.wasm.numThreads = 1;
        // WASM execution provider only. The WebGPU provider creates a session
        // successfully on this quantised graph and then fails at run time on
        // Concat validation, so a try-WebGPU-then-fall-back pattern selects the
        // broken path instead of avoiding it.
        session = await ort.InferenceSession.create(MODEL_URL, { executionProviders: ['wasm'] });
        return session;
    })();
    return loading;
}

/** Score one window of audio. Returns 0..1, or null if too quiet. */
export async function scoreWindow(samples, rate) {
    if (!session) return null;
    const x = resampleTo16k(samples, rate);

    let sum = 0;
    for (let i = 0; i < x.length; i++) sum += x[i] * x[i];
    const dbfs = 10 * Math.log10(Math.max(sum / x.length, 1e-12));
    if (dbfs < SILENCE_DBFS) return null;   // silence classifies confidently and drags the mean

    const feeds = {};
    feeds[session.inputNames[0]] = new ort.Tensor('float32', x, [1, x.length]);
    const out = await session.run(feeds);
    const logits = out[session.outputNames[0]].data;
    const [m, f] = [logits[0], logits[1]];
    return 1 / (1 + Math.exp(m - f));
}

/** Rolling probe fed arbitrary chunks; emits a smoothed value per window. */
export class StreamingProbe {
    constructor(rate, { emaAlpha = 0.2 } = {}) {
        this.rate = rate;
        this.need = Math.round(WINDOW_S * rate);
        this.buf = new Float32Array(this.need);
        this.fill = 0;
        this.ema = null;
        this.last = null;
        this.busy = false;
    }

    /** Returns the current smoothed read, or null before the first window. */
    async push(chunk) {
        for (let i = 0; i < chunk.length; i++) {
            this.buf[this.fill++] = chunk[i];
            if (this.fill === this.need) {
                this.fill = 0;
                if (!this.busy) {
                    this.busy = true;
                    const frame = Float32Array.from(this.buf);
                    scoreWindow(frame, this.rate)
                        .then((v) => {
                            if (v !== null) {
                                this.ema = this.ema === null ? v : this.ema + 0.2 * (v - this.ema);
                                this.last = this.ema;
                            }
                        })
                        .catch(() => {})
                        .finally(() => { this.busy = false; });
                }
            }
        }
        return this.last;
    }
}
