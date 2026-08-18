// Runs the analysis engine locally, in this page, and hands the existing
// overlays exactly the message stream they already understand.
//
// These pages were written against the desktop app's WebSocket feed
// (ws://127.0.0.1:8765). That works beautifully at the desk and not at all on a
// phone, where there is no desktop app. Since the engine now compiles to
// WebAssembly, the page can simply run it: same measurements, same JSON shape,
// no server.
//
// The overlays are unchanged apart from asking here first. If this fails or the
// engine is not built, they fall back to the WebSocket, so the desk setup keeps
// working exactly as before.

const ENGINE = new URL('./engine/', import.meta.url);

class LocalFeed {
    constructor() {
        this.onopen = null;
        this.onmessage = null;
        this.onclose = null;
        this.onerror = null;
        this.started = false;
        this._ctx = null;
        this._stream = null;
        this._worker = null;
    }

    /** Must be called from a user gesture: browsers gate microphone access. */
    async start() {
        if (this.started) return;
        this.started = true;

        this._stream = await navigator.mediaDevices.getUserMedia({
            audio: {
                // Browsers process voice by default in ways that wreck these
                // measurements: auto-gain rescales the very levels the weight
                // measure reads, and the noise suppressor eats sustained tones,
                // which are training drills.
                echoCancellation: false,
                noiseSuppression: false,
                autoGainControl: false,
                channelCount: 1,
            },
        });

        this._ctx = new AudioContext();
        await this._ctx.audioWorklet.addModule(new URL('tap-worklet.js', ENGINE));

        this._worker = new Worker(new URL('analyzer.worker.js', ENGINE), { type: 'module' });
        this._worker.postMessage({ type: 'init', sampleRate: this._ctx.sampleRate });
        this._worker.onmessage = (e) => {
            if (e.data && e.data.type === 'ready') {
                if (this.onopen) this.onopen();
                return;
            }
            // The overlays parse event.data as JSON, exactly as they do for the
            // WebSocket, so hand them a string rather than an object.
            if (this.onmessage) this.onmessage({ data: JSON.stringify(e.data) });
        };

        const src = this._ctx.createMediaStreamSource(this._stream);
        const hop = Math.round(0.02 * this._ctx.sampleRate);
        const node = new AudioWorkletNode(this._ctx, 'tap-processor', {
            processorOptions: { hopSize: hop },
            numberOfInputs: 1,
            numberOfOutputs: 0,
        });
        node.port.onmessage = (e) =>
            this._worker.postMessage({ type: 'block', block: e.data }, [e.data.buffer]);
        src.connect(node);
    }

    close() {
        if (this._stream) this._stream.getTracks().forEach((t) => t.stop());
        if (this._ctx) this._ctx.close();
        if (this._worker) this._worker.terminate();
        this._stream = this._ctx = this._worker = null;
        this.started = false;
        if (this.onclose) this.onclose();
    }
}

// Is the engine actually served alongside this page?
async function engineAvailable() {
    try {
        const r = await fetch(new URL('voicelab.wasm', ENGINE), { method: 'HEAD' });
        return r.ok;
    } catch {
        return false;
    }
}

window.VoiceLabLocalEngine = {
    available: engineAvailable(),
    create: () => new LocalFeed(),
};

// Module scripts are deferred, so the overlays (classic scripts) run first and
// would otherwise see no engine and fall through to the WebSocket.
window.dispatchEvent(new Event('voicelab-engine-ready'));
