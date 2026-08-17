// The InFormant view: a scrolling spectrogram with the pitch and formant
// tracks over it, target and formant bands, and the live readout.
//
// This is the desktop application's main screen, drawn on a canvas. The
// measurements behind it come from the same compiled engine the desktop runs,
// so what differs here is only presentation.

import { genderColor, noteName } from '../dsp/gender.js';

// Frequency axis: mel, matching the desktop's default view scale. Linear space
// wastes most of the screen on frequencies no voice uses.
const mel = (f) => 2595 * Math.log10(1 + f / 700);
const melInv = (m) => 700 * (10 ** (m / 2595) - 1);

const TRACK_COLORS = {
    pitch: { light: '#2a2632', dark: '#ffffff' },
    f1: '#ffab40',
    f2: '#00c4b4',
    f3: '#abcd3c',
};

export class InformantView {
    constructor(opts = {}) {
        this.minF = opts.minF ?? 55;
        this.maxF = opts.maxF ?? 5000;
        this.spanSeconds = opts.spanSeconds ?? 6;
        this.targetMin = opts.targetMin ?? 165;
        this.targetMax = opts.targetMax ?? 220;

        this.show = {
            spectrogram: true, pitch: true, formants: true,
            targetBand: true, formantBands: true, hud: true, genderColors: true,
        };

        // Spectrogram history as columns of dB values, plus the track history.
        this.columns = [];
        this.tracks = [];
        this.hud = null;
        this.hudAt = -1;
    }

    /** One analysis frame: spectrum column (Float32Array of dB) plus measures. */
    push(spectrum, m, wallSeconds) {
        if (spectrum) this.columns.push({ t: wallSeconds, db: spectrum });
        this.tracks.push({
            t: wallSeconds,
            pitch: m.pitch > 0 ? m.pitch : null,
            f1: m.f1 > 0 ? m.f1 : null,
            f2: m.f2 > 0 ? m.f2 : null,
            f3: m.f3 > 0 ? m.f3 : null,
            score: m.score, pitchScore: m.pitchScore, resScore: m.resScore,
            voiced: m.voiced,
        });
        const cutoff = wallSeconds - this.spanSeconds - 1;
        while (this.columns.length && this.columns[0].t < cutoff) this.columns.shift();
        while (this.tracks.length && this.tracks[0].t < cutoff) this.tracks.shift();

        // The readout holds its last real value and dims when stale, rather
        // than blanking: values flickering to "---" between syllables reads as
        // the panel breaking.
        if (m.voiced && m.pitch > 0) {
            this.hud = m;
            this.hudAt = wallSeconds;
        }
    }

    reset() { this.columns.length = 0; this.tracks.length = 0; this.hud = null; }

    /** Map a frequency to a y pixel on the mel axis. */
    y(f, top, height) {
        const a = mel(this.minF), b = mel(this.maxF);
        const v = (mel(Math.max(f, 1)) - a) / (b - a);
        return top + (1 - v) * height;
    }

    draw(ctx, W, H, now, dark) {
        const axisW = Math.max(38, W * 0.075);
        const left = axisW, top = 6;
        const plotW = W - axisW - 4, plotH = H - top - 22;

        ctx.fillStyle = dark ? '#0d0c11' : '#ffffff';
        ctx.fillRect(0, 0, W, H);

        const t1 = now, t0 = now - this.spanSeconds;
        const xOf = (t) => left + ((t - t0) / this.spanSeconds) * plotW;

        if (this.show.spectrogram) this.drawSpectrogram(ctx, left, top, plotW, plotH, t0, t1, dark);
        if (this.show.targetBand) this.drawBands(ctx, left, top, plotW, plotH, dark);
        this.drawAxis(ctx, left, top, plotH, dark);
        if (this.show.formants) {
            for (const [key, color] of [['f3', TRACK_COLORS.f3], ['f2', TRACK_COLORS.f2], ['f1', TRACK_COLORS.f1]]) {
                this.drawTrack(ctx, key, color, xOf, top, plotH, t0);
            }
        }
        if (this.show.pitch) {
            this.drawTrack(ctx, 'pitch', dark ? TRACK_COLORS.pitch.dark : TRACK_COLORS.pitch.light,
                           xOf, top, plotH, t0, true);
        }
        if (this.show.hud) this.drawHud(ctx, left + 8, top + 8, now, dark);
    }

    drawSpectrogram(ctx, left, top, w, h, t0, t1, dark) {
        if (!this.columns.length) return;
        const img = ctx.createImageData(Math.max(1, Math.round(w)), Math.round(h));
        const iw = img.width, ih = img.height;

        // Precompute which spectrum bin each row samples, so the vertical mel
        // mapping costs nothing per column.
        const first = this.columns[0].db;
        const nBins = first.length;
        const binOf = new Int32Array(ih);
        const a = mel(this.minF), b = mel(this.maxF);
        for (let r = 0; r < ih; r++) {
            const f = melInv(a + (1 - r / ih) * (b - a));
            binOf[r] = Math.max(0, Math.min(nBins - 1, Math.round((f / this.maxF) * (nBins - 1))));
        }

        // Normalise against the loudest bin currently on screen rather than a
        // fixed dB window: absolute levels depend entirely on input gain, so a
        // fixed window either saturates to a solid block or goes black.
        let peak = -Infinity;
        for (const col of this.columns) {
            for (let i = 0; i < col.db.length; i++) if (col.db[i] > peak) peak = col.db[i];
        }
        if (!isFinite(peak)) return;
        const ceilDb = peak - 6, floorDb = peak - 68;
        for (let c = 0; c < iw; c++) {
            const t = t0 + (c / iw) * (t1 - t0);
            // Nearest column in time.
            let lo = 0, hi = this.columns.length - 1;
            while (lo < hi) {
                const mid = (lo + hi) >> 1;
                if (this.columns[mid].t < t) lo = mid + 1; else hi = mid;
            }
            const col = this.columns[lo];
            if (!col || Math.abs(col.t - t) > 0.35) continue;
            const db = col.db;
            for (let r = 0; r < ih; r++) {
                let v = (db[binOf[r]] - floorDb) / (ceilDb - floorDb);
                v = v < 0 ? 0 : v > 1 ? 1 : v;
                const i = (r * iw + c) * 4;
                // Dark: black through violet to white. Light: white through
                // violet to near-black, so both themes stay legible.
                let R, G, B;
                if (dark) {
                    R = Math.round(255 * Math.min(1, v * 1.6) * (0.35 + 0.65 * v));
                    G = Math.round(180 * v * v);
                    B = Math.round(255 * Math.min(1, v * 1.9));
                } else {
                    const k = 1 - v;
                    R = Math.round(255 * (0.35 + 0.65 * k));
                    G = Math.round(255 * k * 0.92);
                    B = Math.round(255 * (0.55 + 0.45 * k));
                }
                img.data[i] = R; img.data[i + 1] = G; img.data[i + 2] = B; img.data[i + 3] = 255;
            }
        }
        ctx.putImageData(img, Math.round(left), Math.round(top));
    }

    drawBands(ctx, left, top, w, h, dark) {
        const band = (lo, hi, fill, edge) => {
            const y1 = this.y(hi, top, h), y2 = this.y(lo, top, h);
            ctx.fillStyle = fill;
            ctx.fillRect(left, y1, w, y2 - y1);
            ctx.strokeStyle = edge;
            ctx.lineWidth = 2;
            ctx.beginPath();
            ctx.moveTo(left, y1); ctx.lineTo(left + w, y1);
            ctx.moveTo(left, y2); ctx.lineTo(left + w, y2);
            ctx.stroke();
        };
        // Pitch: masculine reference band and the fem target band.
        band(85, 155, 'rgba(77,127,214,0.20)', 'rgba(77,127,214,0.75)');
        band(this.targetMin, this.targetMax, 'rgba(219,39,119,0.20)', 'rgba(219,39,119,0.75)');

        if (this.show.formantBands) {
            // Per-formant masc/fem zones, the same anchors the gender model uses.
            const anchors = [[420, 550], [1350, 1650], [2700, 3150]];
            for (const [m, f] of anchors) {
                band(m * 0.94, m * 1.06, 'rgba(77,127,214,0.10)', 'rgba(77,127,214,0.28)');
                band(f * 0.94, f * 1.06, 'rgba(219,39,119,0.10)', 'rgba(219,39,119,0.28)');
            }
        }
    }

    drawAxis(ctx, left, top, h, dark) {
        const ink = dark ? 'rgba(230,225,240,0.75)' : 'rgba(40,36,50,0.75)';
        ctx.fillStyle = ink;
        ctx.strokeStyle = dark ? 'rgba(230,225,240,0.25)' : 'rgba(40,36,50,0.2)';
        ctx.lineWidth = 1;
        ctx.font = `${Math.max(9, Math.round(left * 0.28))}px system-ui`;
        ctx.textAlign = 'right';
        for (const f of [100, 200, 500, 1000, 2000, 4000]) {
            if (f < this.minF || f > this.maxF) continue;
            const y = this.y(f, top, h);
            ctx.beginPath(); ctx.moveTo(left - 4, y); ctx.lineTo(left, y); ctx.stroke();
            ctx.fillText(f >= 1000 ? `${f / 1000}k` : String(f), left - 6, y + 3);
        }
    }

    drawTrack(ctx, key, baseColor, xOf, top, h, t0, thick = false) {
        const pts = this.tracks.filter((p) => p[key] !== null && p.t >= t0);
        if (pts.length < 2) return;
        ctx.lineWidth = thick ? 3.2 : 2.4;
        ctx.lineCap = 'round';
        for (let i = 1; i < pts.length; i++) {
            const a = pts[i - 1], b = pts[i];
            if (b.t - a.t > 0.08) continue;   // break the line across silence
            ctx.strokeStyle = this.show.genderColors && b.score >= 0
                ? genderColor(key === 'pitch' ? b.pitchScore : b.resScore)
                : baseColor;
            ctx.beginPath();
            ctx.moveTo(xOf(a.t), this.y(a[key], top, h));
            ctx.lineTo(xOf(b.t), this.y(b[key], top, h));
            ctx.stroke();
        }
        // A constant identity colour on the newest point, so the tracks stay
        // told apart even when gender colouring makes them all similar.
        const last = pts[pts.length - 1];
        ctx.fillStyle = baseColor;
        ctx.beginPath();
        ctx.arc(xOf(last.t), this.y(last[key], top, h), thick ? 4 : 3, 0, 7);
        ctx.fill();
    }

    drawHud(ctx, x, y, now, dark) {
        const m = this.hud;
        const stale = this.hudAt < 0 || now - this.hudAt > 2.5;
        const pad = 8, lineH = 17;
        const lines = [];
        if (!m) {
            lines.push(['waiting for voice', null]);
        } else {
            lines.push([`${Math.round(m.pitch)} Hz  ${noteName(m.pitch)}`,
                        m.pitch >= this.targetMin && m.pitch <= this.targetMax ? '#e879b9' : null]);
            lines.push([`F1 ${m.f1 > 0 ? Math.round(m.f1) : '---'}   F2 ${m.f2 > 0 ? Math.round(m.f2) : '---'}`, null]);
            if (m.score >= 0) {
                const label = m.score < 0.38 ? 'masculine' : m.score > 0.62 ? 'feminine' : 'androgynous';
                lines.push([`reads: ${label}`, genderColor(m.score)]);
            }
        }

        ctx.font = '13px system-ui';
        ctx.textAlign = 'left';   // drawAxis leaves it right-aligned
        const wBox = Math.max(...lines.map((l) => ctx.measureText(l[0]).width)) + pad * 2;
        const hBox = lines.length * lineH + pad * 2 + (m && m.score >= 0 ? 12 : 0);

        ctx.globalAlpha = stale ? 0.45 : 0.92;
        ctx.fillStyle = dark ? 'rgba(18,16,24,0.82)' : 'rgba(255,255,255,0.86)';
        ctx.fillRect(x, y, wBox, hBox);
        ctx.strokeStyle = dark ? 'rgba(255,255,255,0.14)' : 'rgba(0,0,0,0.12)';
        ctx.lineWidth = 1;
        ctx.strokeRect(x + 0.5, y + 0.5, wBox, hBox);

        const ink = dark ? '#eae5f2' : '#241f2e';
        lines.forEach((l, i) => {
            ctx.fillStyle = l[1] || ink;
            ctx.fillText(l[0], x + pad, y + pad + lineH * (i + 0.75));
        });

        // The masc-fem meter, matching the desktop's bar.
        if (m && m.score >= 0) {
            const bx = x + pad, by = y + hBox - pad - 7, bw = wBox - pad * 2;
            const g = ctx.createLinearGradient(bx, 0, bx + bw, 0);
            g.addColorStop(0, 'rgb(96,165,250)');
            g.addColorStop(0.5, 'rgb(158,155,166)');
            g.addColorStop(1, 'rgb(244,114,182)');
            ctx.fillStyle = g;
            ctx.fillRect(bx, by, bw, 5);
            ctx.fillStyle = ink;
            ctx.fillRect(bx + m.score * bw - 1.5, by - 2, 3, 9);
        }
        ctx.globalAlpha = 1;
    }
}
