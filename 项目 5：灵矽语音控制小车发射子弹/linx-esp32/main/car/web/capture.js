/* 连续分块重采样，32 抽头加窗 sinc 抑制降采样混叠；采样率使用实际音频上下文值。 */
class PcmResampler {
    constructor(rate) {
        if (!Number.isFinite(rate) || rate < 16000 || rate > 192000) throw Error('unsupported sample rate');
        this.ratio = rate / 16000;
        this.cutoff = Math.min(1, 16000 / rate) * .9;
        this.data = Array(16).fill(0);
        this.position = 16;
        this.base = 0;
        this.produced = 0;
        this.inputSamples = 0;
        this.limit = Infinity;
        this.output = [];
    }
    push(input) {
        this.inputSamples += input.length;
        for (const sample of input) this.data.push(Number.isFinite(sample) ? sample : 0);
        const frames = [];
        while (this.position + 16 < this.data.length && this.produced < this.limit) {
            const centre = Math.floor(this.position);
            let sum = 0, norm = 0;
            for (let k = centre - 15; k <= centre + 16; k++) {
                const x = k - this.position;
                const z = Math.PI * x * this.cutoff;
                const sinc = Math.abs(z) < 1e-9 ? 1 : Math.sin(z) / z;
                const window = .5 + .5 * Math.cos(Math.PI * x / 16);
                const weight = this.cutoff * sinc * window;
                sum += this.data[k] * weight; norm += weight;
            }
            const value = Math.max(-1, Math.min(1, sum / norm));
            this.output.push(Math.round(value < 0 ? value * 32768 : value * 32767));
            ++this.produced;
            this.position = 16 + this.produced * this.ratio - this.base;
            if (this.output.length === 960) { frames.push(Int16Array.from(this.output)); this.output = []; }
        }
        const consumed = Math.max(0, Math.floor(this.position) - 16);
        this.data.splice(0, consumed); this.position -= consumed; this.base += consumed;
        return frames;
    }
    finish() {
        // 滤波器尾部补零，保留尚未输出的真实采样。
        this.limit = Math.round(this.inputSamples / this.ratio);
        const frames = this.push(new Float32Array(17));
        if (this.output.length) { frames.push(Int16Array.from(this.output)); this.output = []; }
        return frames;
    }
}
if (typeof AudioWorkletProcessor !== 'undefined') {
    class CarCapture extends AudioWorkletProcessor {
        constructor() {
            super(); this.resampler = new PcmResampler(sampleRate); this.active = true;
            this.port.onmessage = event => {
                if (event.data === 'finish' && this.active) {
                    this.active = false;
                    for (const pcm of this.resampler.finish()) this.port.postMessage({pcm}, [pcm.buffer]);
                    this.port.postMessage({done:true});
                }
            };
        }
        process(inputs) {
            if (this.active && inputs[0]?.[0]) {
                const channels = inputs[0]; const mono = new Float32Array(channels[0].length);
                for (const channel of channels) for (let i=0;i<mono.length;i++) mono[i] += channel[i]/channels.length;
                for (const pcm of this.resampler.push(mono)) this.port.postMessage({pcm}, [pcm.buffer]);
            }
            return true;
        }
    }
    registerProcessor('car-capture', CarCapture);
}
if (typeof module !== 'undefined') module.exports = {PcmResampler};
