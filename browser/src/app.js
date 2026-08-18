// voice-lab browser app: microphone -> worklet tap -> analysis worker -> charts.
// Everything is client-side; no audio and no measurements leave the device.

import { genderColor, noteName, fullnessCell } from './dsp/gender.js';
import { SessionRecorder } from './session.js';
import { InformantView } from './views/informant.js';
import * as probe from './ml/gender-probe.js';

const TRAIL_SECONDS = 6;
const TARGET_MIN = 165, TARGET_MAX = 220;

const el = (id) => document.getElementById(id);
const cv = el('chart');
const ctx = cv.getContext('2d');

let view = 'informant';
let running = false;
let worker = null, audioCtx = null, stream = null, node = null;
let listener = null;   // optional neural perceived-gender probe
const session = new SessionRecorder(TARGET_MIN, TARGET_MAX);
const informant = new InformantView({ targetMin: TARGET_MIN, targetMax: TARGET_MAX });
let refSpeakers = [];
fetch(new URL('../data/reference-speakers.json', import.meta.url))
    .then((r) => r.json()).then((d) => { refSpeakers = d; draw(); })
    .catch(() => {});
const isDark = () => matchMedia('(prefers-color-scheme: dark)').matches;
const trail = [];
let last = null;
let voicedFrames = 0, inBandFrames = 0;

// ------------------------------------------------------------------ charts

function fitCanvas() {
    const w = cv.parentElement.clientWidth;
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    // Square on phones, a little wider on desktop.
    const h = view === 'informant'
        ? Math.max(260, Math.min(w * 0.62, window.innerHeight * 0.52))
        : Math.min(w, Math.max(320, window.innerHeight * 0.45));
    cv.style.height = h + 'px';
    cv.width = Math.round(w * dpr);
    cv.height = Math.round(h * dpr);
    draw();
}

const pitchPct = (hz) => Math.max(0, Math.min(1, (hz - 50) / 250));

function drawGenderspace(W, H, M) {
    // Luna McNulty's genderspace plane: pitch up, resonance right, cool to warm.
    const img = ctx.createImageData(W - 2 * M, H - 2 * M);
    const iw = W - 2 * M, ih = H - 2 * M;
    for (let y = 0; y < ih; y++) {
        for (let x = 0; x < iw; x++) {
            const run = x / iw, rise = y / ih;
            const t = (2 / 3) * run + (1 / 3) * (1 - rise);
            let r = 0 * (1 - t) + 255 * t, g = 100 * (1 - t) + 40 * t, b = 255 * (1 - t) + 0 * t;
            const light = Math.max(0, 1 - Math.sqrt(1.5 * rise) * 0.62);
            const i = (y * iw + x) * 4;
            img.data[i] = r + (255 - r) * light;
            img.data[i + 1] = g + (255 - g) * light;
            img.data[i + 2] = b + (255 - b) * light;
            img.data[i + 3] = 255;
        }
    }
    ctx.putImageData(img, M, M);

    // Pitch target band.
    const y1 = M + (1 - pitchPct(TARGET_MAX)) * ih;
    const y2 = M + (1 - pitchPct(TARGET_MIN)) * ih;
    ctx.fillStyle = 'rgba(255,255,255,0.16)';
    ctx.fillRect(M, y1, iw, y2 - y1);
    ctx.strokeStyle = 'rgba(255,255,255,0.55)';
    ctx.lineWidth = 2;
    ctx.beginPath(); ctx.moveTo(M, y1); ctx.lineTo(M + iw, y1);
    ctx.moveTo(M, y2); ctx.lineTo(M + iw, y2); ctx.stroke();

    // The site's reference speakers, so the live dot has something to sit among.
    for (const sp of refSpeakers) {
        const x = M + sp.resonance * iw;
        const y = M + (1 - pitchPct(sp.pitch)) * ih;
        ctx.beginPath();
        ctx.arc(x, y, Math.max(5, W / 120), 0, 7);
        ctx.fillStyle = sp.color || 'rgba(60,60,70,0.8)';
        ctx.fill();
        ctx.lineWidth = Math.max(1.5, W / 400);
        ctx.strokeStyle = 'rgba(255,255,255,0.85)';
        ctx.stroke();
    }

    ctx.fillStyle = 'rgba(255,255,255,0.9)';
    ctx.font = `600 ${Math.round(W / 46)}px system-ui`;
    ctx.textAlign = 'center';
    ctx.fillText('← resonance →', W / 2, H - M / 3);
    ctx.save();
    ctx.translate(M / 2.4, H / 2); ctx.rotate(-Math.PI / 2);
    ctx.fillText('← pitch →', 0, 0);
    ctx.restore();
    ctx.textAlign = 'left';
    ctx.fillText('300 Hz', M + 4, M + W / 40);
    ctx.fillText('50 Hz', M + 4, H - M - 6);
}

function drawFullness(W, H, M) {
    // TransVoiceLessons' fullness graph: weight right, size up (small at top).
    ctx.fillStyle = getComputedStyle(document.documentElement)
        .getPropertyValue('--chart-bg').trim() || '#fff';
    ctx.fillRect(0, 0, W, H);

    const cx = M + (W - 2 * M) / 2, cy = M + (H - 2 * M) / 2;
    ctx.strokeStyle = '#3c3c44';
    ctx.lineWidth = Math.max(2, W / 340);
    ctx.beginPath();
    ctx.moveTo(M, cy); ctx.lineTo(W - M, cy);
    ctx.moveTo(cx, M); ctx.lineTo(cx, H - M);
    ctx.stroke();

    ctx.fillStyle = '#26262e';
    ctx.font = `${Math.round(W / 26)}px Georgia, serif`;
    ctx.textAlign = 'center';
    ctx.fillText('small', cx, M - W / 60);
    ctx.fillText('large', cx, H - M + W / 30);
    ctx.textAlign = 'right'; ctx.fillText('heavy', W - M / 8, cy + W / 70);
    ctx.textAlign = 'left'; ctx.fillText('light', M / 8, cy + W / 70);

    ctx.font = `600 ${Math.round(W / 34)}px Georgia, serif`;
    ctx.fillStyle = 'rgba(30,30,38,.85)';
    ctx.textAlign = 'left';
    ctx.fillText('fem / full', M + W / 40, M + W / 22);
    ctx.fillText('underfull', M + W / 40, H - M - W / 50);
    ctx.textAlign = 'right';
    ctx.fillText('overfull', W - M - W / 40, M + W / 22);
    ctx.fillText('masc / full', W - M - W / 40, H - M - W / 50);
}

function draw() {
    const W = cv.width, H = cv.height;
    const M = Math.round(W * 0.09);
    ctx.clearRect(0, 0, W, H);
    if (view === 'informant') {
        informant.draw(ctx, W, H, last ? last.t : 0, isDark());
        return;
    }
    if (view === 'gender') drawGenderspace(W, H, M); else drawFullness(W, H, M);

    const iw = W - 2 * M, ih = H - 2 * M;
    const now = performance.now();

    const pos = (p) => {
        if (view === 'gender') {
            return [M + p.res * iw, M + (1 - pitchPct(p.pitch)) * ih];
        }
        return [M + p.weight * iw, M + (1 - p.size) * ih];
    };
    const usable = (p) => (view === 'gender'
        ? p.res >= 0 && p.pitch > 0
        : p.weight >= 0 && p.size !== null);

    ctx.lineWidth = Math.max(3, W / 220);
    ctx.lineCap = 'round';
    for (let i = 1; i < trail.length; i++) {
        const a = trail[i - 1], b = trail[i];
        if (b.gap || !usable(a) || !usable(b)) continue;
        const age = (now - b.wall) / 1000;
        if (age > TRAIL_SECONDS) continue;
        ctx.globalAlpha = 0.85 * (1 - age / TRAIL_SECONDS);
        ctx.strokeStyle = genderColor(view === 'gender' ? b.score : 1 - b.andro);
        const [ax, ay] = pos(a), [bx, by] = pos(b);
        ctx.beginPath(); ctx.moveTo(ax, ay); ctx.lineTo(bx, by); ctx.stroke();
    }
    ctx.globalAlpha = 1;

    const p = [...trail].reverse().find(usable);
    if (p) {
        const [x, y] = pos(p);
        const stale = (now - p.wall) / 1000 > 1.2;
        ctx.globalAlpha = stale ? 0.35 : 1;
        ctx.beginPath();
        ctx.arc(x, y, Math.max(9, W / 68), 0, 7);
        ctx.fillStyle = genderColor(view === 'gender' ? p.score : 1 - p.andro);
        ctx.fill();
        ctx.lineWidth = Math.max(3, W / 260);
        ctx.strokeStyle = '#fff';
        ctx.stroke();
        ctx.globalAlpha = 1;
    }
}

// ----------------------------------------------------------------- readouts

function updateReadout(m) {
    if (view !== 'fullness') {
        el('vPitch').textContent = m.pitch > 0 ? Math.round(m.pitch) : '—';
        el('vNote').textContent = m.pitch > 0 ? noteName(m.pitch) : '';
        el('vRes').textContent = m.resonance >= 0 ? Math.round(m.resonance * 100) + '%' : '—';
        if (m.score >= 0) {
            const label = m.score < 0.38 ? 'masculine' : m.score > 0.62 ? 'feminine' : 'androgynous';
            el('vReads').textContent = label;
            el('vReads').style.color = genderColor(m.score);
        }
        el('vInBand').textContent = voicedFrames
            ? Math.round((inBandFrames / voicedFrames) * 100) + '%' : '—';
        const lv = listener && listener.last;
        if (lv !== null && lv !== undefined) {
            el('vListener').textContent = Math.round(lv * 100) + '%';
            el('vListener').style.color = genderColor(lv);
        }
    } else {
        if (m.weight >= 0 && m.size !== null && m.size !== undefined) {
            const cell = fullnessCell(m.weight, 1 - m.size);
            el('fCode').textContent = cell.code;
            el('fCode').style.color = genderColor(1 - cell.androgenization);
            el('fDesc').textContent = `${cell.fullness} · ${cell.gender}`;
        }
        el('fWeight').textContent = m.weight >= 0 ? Math.round(m.weight * 100) + '%' : '—';
        el('fSize').textContent = Number.isFinite(m.size) ? Math.round(m.size * 100) + '%' : '—';
        el('fTilt').innerHTML = m.tilt > -900
            ? `${m.tilt.toFixed(1)} <small>dB/oct</small>` : '— <small>dB/oct</small>';
    }
}

// -------------------------------------------------------------------- audio

async function start() {
    el('status').textContent = 'requesting mic…';
    try {
        stream = await navigator.mediaDevices.getUserMedia({
            audio: {
                // Browsers "helpfully" mangle voice by default. Auto-gain alone
                // would wreck the vocal-weight measurement, and the denoiser
                // eats sustained tones, which are training drills.
                echoCancellation: false,
                noiseSuppression: false,
                autoGainControl: false,
                channelCount: 1,
            },
        });
    } catch (err) {
        el('status').textContent = 'mic denied';
        el('httpWarn').classList.remove('hidden');
        return;
    }

    audioCtx = new AudioContext();
    // Both of these resolve against the document rather than this module when
    // given a bare relative string, so they are pinned to import.meta.url: the
    // app then loads the same way wherever it is hosted, root or path prefix.
    await audioCtx.audioWorklet.addModule(new URL('./worklet/tap-worklet.js', import.meta.url));

    worker = new Worker(new URL('./worker/analyzer.worker.js', import.meta.url), { type: 'module' });
    worker.postMessage({ type: 'init', sampleRate: audioCtx.sampleRate });
    worker.onmessage = (e) => {
        const m = e.data;
        if (m.type !== 'measurement') return;
        last = m;

        if (m.voiced && m.pitch > 0) {
            voicedFrames++;
            if (m.pitch >= TARGET_MIN && m.pitch <= TARGET_MAX) inBandFrames++;
        }

        // r-space runs past 1.0 for fem-of-centre voices (measured: 38% of
        // frames of the TVL 'light' demo), so scale by the real ceiling rather
        // than clamping the top of the range away.
        const SIZE_HI = 1.45;
        const size = Number.isFinite(m.sizeR) && m.sizeR > -900
            ? Math.max(0, Math.min(1, m.sizeR / SIZE_HI)) : null;
        m.size = size;   // readouts read it off the measurement
        session.add(m);
        informant.push(m.spectrum || null, m, m.t);
        const point = {
            wall: performance.now(),
            pitch: m.pitch, res: m.resonance, score: m.score,
            weight: m.weight, size,
            andro: size !== null && m.weight >= 0 ? ((1 - size) + m.weight) / 2 : 0.5,
            gap: !m.voiced,
        };
        trail.push(point);
        while (trail.length && performance.now() - trail[0].wall > TRAIL_SECONDS * 1000) trail.shift();

        updateReadout(m);
    };

    const src = audioCtx.createMediaStreamSource(stream);
    const hop = Math.round(0.02 * audioCtx.sampleRate);
    node = new AudioWorkletNode(audioCtx, 'tap-processor', {
        processorOptions: { hopSize: hop },
        numberOfInputs: 1, numberOfOutputs: 0,
    });
    node.port.onmessage = (e) => {
        if (listener) listener.push(e.data);   // copy-free read before transfer
        worker.postMessage({ type: 'block', block: e.data }, [e.data.buffer]);
    };

    // The neural probe is optional: if the model is not served, the app just
    // does not show that reading.
    probe.load().then(() => {
        listener = new probe.StreamingProbe(audioCtx.sampleRate);
        el('listenerStat').classList.remove('hidden');
    }).catch(() => { listener = null; });
    src.connect(node);

    session.reset();
    informant.reset();
    voicedFrames = inBandFrames = 0;
    running = true;
    el('mic').textContent = 'Stop';
    el('save').classList.remove('hidden');
    el('status').textContent = `live · ${Math.round(audioCtx.sampleRate / 1000)} kHz`;
    requestAnimationFrame(loop);
}

function stop() {
    running = false;
    if (stream) stream.getTracks().forEach((t) => t.stop());
    if (audioCtx) audioCtx.close();
    if (worker) worker.terminate();
    stream = audioCtx = worker = node = null;
    el('mic').textContent = 'Start';
    el('status').textContent = 'stopped';
}

function loop() {
    if (!running) return;
    draw();
    requestAnimationFrame(loop);
}

// --------------------------------------------------------------------- wire

el('mic').addEventListener('click', () => (running ? stop() : start()));
el('save').addEventListener('click', () => session.download());
el('tabInformant').addEventListener('click', () => setView('informant'));
el('tabGender').addEventListener('click', () => setView('gender'));
el('tabFullness').addEventListener('click', () => setView('fullness'));

function setView(v) {
    view = v;
    for (const [id, name] of [['tabInformant', 'informant'], ['tabGender', 'gender'], ['tabFullness', 'fullness']]) {
        el(id).setAttribute('aria-pressed', String(v === name));
    }
    el('genderReadout').classList.toggle('hidden', v === 'fullness');
    el('fullnessReadout').classList.toggle('hidden', v !== 'fullness');
    if (last) updateReadout(last);
    fitCanvas();
}

if (!window.isSecureContext) el('httpWarn').classList.remove('hidden');
window.addEventListener('resize', fitCanvas);
fitCanvas();
