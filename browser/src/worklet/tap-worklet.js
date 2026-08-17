// Audio-thread tap. Deliberately does no analysis: the LPC and root-finding
// work runs in a Worker instead, because anything slow here glitches capture.
// This only accumulates hop-sized blocks and ships them out.

class TapProcessor extends AudioWorkletProcessor {
    constructor(options) {
        super();
        const hop = (options.processorOptions && options.processorOptions.hopSize) || 960;
        this.hop = hop;
        this.buf = new Float32Array(hop);
        this.fill = 0;
    }

    process(inputs) {
        const input = inputs[0];
        if (!input || !input[0]) return true;
        const ch = input[0];
        for (let i = 0; i < ch.length; i++) {
            this.buf[this.fill++] = ch[i];
            if (this.fill === this.hop) {
                // Transfer a copy; the worklet keeps its own buffer.
                const out = new Float32Array(this.buf);
                this.port.postMessage(out, [out.buffer]);
                this.fill = 0;
            }
        }
        return true;
    }
}

registerProcessor('tap-processor', TapProcessor);
