'use strict';
const assert = require('node:assert/strict');
const {PcmResampler} = require('../../main/car/web/capture.js');
function resample(rate, frequency, chunks) {
    const converter = new PcmResampler(rate), output=[];
    let offset=0;
    while(offset<rate){
        const count=Math.min(chunks[offset%chunks.length],rate-offset);
        const input=Float32Array.from({length:count},(_,i)=>.6*Math.sin(2*Math.PI*frequency*(offset+i)/rate));
        for(const pcm of converter.push(input))output.push(...pcm);
        offset+=count;
    }
    for(const pcm of converter.finish())output.push(...pcm);
    return output;
}
function rms(samples){return Math.sqrt(samples.slice(100,-100).reduce((sum,x)=>sum+x*x,0)/(samples.length-200));}
for(const rate of [16000,44100,48000]){
    const actual=resample(rate,1000,[128]);
    const fragmented=resample(rate,1000,[37,128,511,79]);
    assert.equal(actual.length,16000,`one-second sample count at ${rate}`);
    assert.deepEqual(actual,fragmented,`chunk boundary invariance at ${rate}`);
    assert(actual.every(Number.isInteger));assert(actual.every(x=>x>=-32768&&x<=32767));
    assert(Math.abs(rms(actual)-.6*32767/Math.sqrt(2))<600,`voice-band amplitude at ${rate}`);
    if(rate>16000){const alias=resample(rate,12000,[128]);assert(rms(alias)<rms(actual)*.03,`anti-alias rejection at ${rate}`);}
}
assert.throws(()=>new PcmResampler(0));
assert.throws(()=>new PcmResampler(8000));
console.log('Resampler: sample counts, streaming boundaries, PCM range, voice-band amplitude and anti-alias checks passed');
