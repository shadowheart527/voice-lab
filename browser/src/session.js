// Session recording: accumulates measurements into a summary you can export
// and feed to the coach (ml/coach/coach.py). Everything stays on the device
// until you explicitly export it.

import { fullnessCell } from './dsp/gender.js';

const q = (arr, p) => {
    if (!arr.length) return null;
    const s = Float64Array.from(arr).sort();
    return s[Math.min(s.length - 1, Math.floor(p * s.length))];
};

export class SessionRecorder {
    constructor(targetMin = 165, targetMax = 220) {
        this.targetMin = targetMin;
        this.targetMax = targetMax;
        this.reset();
    }

    reset() {
        this.startedWall = Date.now();
        this.startedPerf = performance.now();
        this.frames = 0;
        this.voiced = 0;
        this.inBand = 0;
        this.pitch = [];
        this.resonance = [];
        this.weight = [];
        this.size = [];
        this.score = [];
        this.cells = {};
        // Coarse timeline for drift: one bucket per 30 s.
        this.buckets = [];
    }

    add(m) {
        this.frames++;
        if (!m.voiced || !(m.pitch > 0)) return;
        this.voiced++;
        this.pitch.push(m.pitch);
        if (m.pitch >= this.targetMin && m.pitch <= this.targetMax) this.inBand++;
        if (m.resonance >= 0) this.resonance.push(m.resonance);
        if (m.score >= 0) this.score.push(m.score);
        if (m.weight >= 0) this.weight.push(m.weight);
        if (Number.isFinite(m.size)) this.size.push(m.size);

        if (m.weight >= 0 && Number.isFinite(m.size)) {
            const c = fullnessCell(m.weight, 1 - m.size).code;
            this.cells[c] = (this.cells[c] || 0) + 1;
        }

        const idx = Math.floor((performance.now() - this.startedPerf) / 30000);
        while (this.buckets.length <= idx) {
            this.buckets.push({ t: this.buckets.length * 30, pitch: [], score: [], weight: [] });
        }
        const b = this.buckets[idx];
        b.pitch.push(m.pitch);
        if (m.score >= 0) b.score.push(m.score);
        if (m.weight >= 0) b.weight.push(m.weight);
    }

    summary() {
        const dur = (performance.now() - this.startedPerf) / 1000;
        const med = (a) => q(a, 0.5);
        return {
            schema: 'voice-lab.session/1',
            startedAt: new Date(this.startedWall).toISOString(),
            durationSeconds: Math.round(dur),
            voicedSeconds: Math.round((this.voiced / Math.max(1, this.frames)) * dur),
            target: { minHz: this.targetMin, maxHz: this.targetMax },
            pitch: {
                medianHz: med(this.pitch),
                p10Hz: q(this.pitch, 0.1),
                p90Hz: q(this.pitch, 0.9),
                percentInTarget: this.voiced ? (this.inBand / this.voiced) * 100 : null,
            },
            resonance: { median: med(this.resonance) },
            genderRead: { median: med(this.score) },
            weight: { median: med(this.weight) },
            size: { median: med(this.size) },
            fullnessCells: this.cells,
            timeline: this.buckets.map((b) => ({
                atSeconds: b.t,
                medianPitchHz: med(b.pitch),
                medianGenderRead: med(b.score),
                medianWeight: med(b.weight),
            })),
        };
    }

    download() {
        const blob = new Blob([JSON.stringify(this.summary(), null, 2)],
            { type: 'application/json' });
        const a = document.createElement('a');
        a.href = URL.createObjectURL(blob);
        a.download = `voice-session-${new Date().toISOString().slice(0, 19).replace(/[:T]/g, '-')}.json`;
        a.click();
        URL.revokeObjectURL(a.href);
    }
}
