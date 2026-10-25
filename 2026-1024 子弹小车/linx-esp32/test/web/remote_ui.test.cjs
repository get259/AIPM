'use strict';
const vm=require('node:vm'),fs=require('node:fs'),assert=require('node:assert/strict');
class Element{
    constructor(state){this.dataset={state:String(state)};this.listeners={};this.disabled=false;this.tagName='BUTTON';this.classList={toggle(){},add(){},remove(){}};}
    addEventListener(type,callback){(this.listeners[type]??=[]).push(callback);}
    setPointerCapture(){} setAttribute(){}
    fire(type,extra={}){const e={pointerId:1,target:this,preventDefault(){},...extra};for(const f of this.listeners[type]||[])f(e);}
}
const ids=Object.fromEntries(['notice','connection','toggle','source','stop','fire','voice','mic-state','reconnect'].map(id=>[id,new Element(0)]));
const buttons=[1,8,4,2].map(x=>new Element(x));
const document={getElementById:id=>ids[id],querySelectorAll:()=>buttons,addEventListener(type,f){this[type]=f;},hidden:false};
let socket,worklet,trackStops=0,microphoneRequests=0, hold=false, waiting=[];
const sent=[];
let state={enabled:false,mine:false,owned:false,platform:true};
class Socket{
    static OPEN=1;static CLOSING=2;
    constructor(){socket=this;this.readyState=1;this.bufferedAmount=0;queueMicrotask(()=>this.onopen());}
    send(raw){
        if(typeof raw!=='string'){sent.push({op:'pcm',raw});const v=new DataView(raw);this.reply(v.getUint32(0,true));return;}
        const data=JSON.parse(raw);sent.push(data);
        if(data.op==='enable')state={...state,enabled:true,mine:true,owned:true};
        if(data.op==='disable')state={...state,enabled:false,mine:false,owned:false};
        this.reply(data.id,data.op==='start'&& !state.platform);
    }
    reply(id,unavailable=false){const callback=()=>this.onmessage({data:JSON.stringify({id,ok:!unavailable,message:unavailable?'平台尚未就绪，方向按钮仍可使用':'ok',...state})});if(hold)waiting.push(callback);else queueMicrotask(callback);}
}
class Context{
    constructor(){this.state='running';this.audioWorklet={addModule:async()=>{}};this.destination={};}
    async resume(){} async close(){this.state='closed';}
    createMediaStreamSource(){return {connect(){},disconnect(){}};}
    createGain(){return {gain:{value:1},connect(){},disconnect(){}};}
}
class Worklet{
    constructor(){worklet=this;this.port={onmessage:null,postMessage:()=>queueMicrotask(()=>this.port.onmessage({data:{done:true}}))};}
    connect(){} disconnect(){}
    pcm(){this.port.onmessage({data:{pcm:new Int16Array(960)}});}
}
const events={};const window={isSecureContext:true,AudioWorkletNode:Worklet,addEventListener(type,f){events[type]=f;}};
const sandbox={document,window,location:{host:'192.168.4.1'},WebSocket:Socket,AudioContext:Context,AudioWorkletNode:Worklet,
    navigator:{mediaDevices:{getUserMedia:async()=>{microphoneRequests++;return {getTracks:()=>[{stop(){trackStops++;}}]};}}},
    setTimeout,clearTimeout,setInterval(){},console,ArrayBuffer,DataView,Int16Array};
vm.runInNewContext(fs.readFileSync(require.resolve('../../main/car/web/remote.js'),'utf8'),sandbox);
const settle=async()=>{for(let i=0;i<30;i++)await Promise.resolve();await new Promise(resolve=>setImmediate(resolve));};
(async()=>{
    await settle();assert(ids.toggle.disabled===false);assert(ids.fire.disabled);
    ids.fire.onclick();await settle();assert(!sent.some(x=>x.op==='fire'));
    await ids.toggle.onclick();assert(state.enabled);
    assert(!ids.fire.disabled);ids.fire.onclick();await settle();assert.equal(sent.at(-1).op,'fire');
    state={...state,platform:false};await sandbox.request('status');await settle();
    assert(!ids.voice.disabled); // 平台尚未就绪时仍允许点击申请浏览器权限。
    ids.voice.onclick();
    assert.match(ids['mic-state'].textContent,/正在申请麦克风权限/); // 点击后立刻提示，授权弹窗不再静默。
    await settle();
    assert.equal(microphoneRequests,1);
    assert.match(ids['mic-state'].textContent,/平台尚未就绪/);
    assert(!ids.voice.disabled);
    state={...state,platform:true};await sandbox.request('status');await settle();
    window.isSecureContext=false;
    ids.voice.onclick();await settle();
    assert.equal(microphoneRequests,1);
    assert.match(ids['mic-state'].textContent,/安全页面/);
    window.isSecureContext=true;
    buttons[0].fire('pointerdown');await settle();buttons[0].fire('pointerup');await settle();
    assert.deepEqual(sent.filter(x=>x.op==='move').map(x=>x.state),[1,0]);
    events.keydown({code:'KeyA',target:buttons[1],preventDefault(){},repeat:false});await settle();
    events.keyup({code:'KeyA',preventDefault(){}});await settle();assert.deepEqual(sent.filter(x=>x.op==='move').slice(-2).map(x=>x.state),[8,0]);
    // 缓慢连接下，多个未发送方向被最后的停止合并，不能回放积压动作。
    hold=true;buttons[0].fire('pointerdown');await settle();buttons[0].fire('pointerup');buttons[2].fire('pointerdown');buttons[2].fire('pointerup');
    assert.equal(sent.filter(x=>x.op==='move').at(-1).state,0);
    hold=false;waiting.splice(0).forEach(f=>f());await settle();assert.equal(sent.filter(x=>x.op==='move').at(-1).state,0);
    ids.voice.onclick();await settle();assert(sent.some(x=>x.op==='start'));worklet.pcm();await settle();
    // 延迟 ACK 时仍能发三帧，第四帧留在有界队列；停止不受影响。
    hold=true;const before=sent.filter(x=>x.op==='pcm').length;
    for(let i=0;i<4;i++)worklet.pcm();await settle();
    assert.equal(sent.filter(x=>x.op==='pcm').length-before,3);
    ids.stop.onclick();await settle();assert.equal(sent.at(-1).op,'move');assert.equal(sent.at(-1).state,0);
    hold=false;waiting.splice(0).forEach(f=>f());await settle();
    assert.equal(sent.filter(x=>x.op==='pcm').length-before,4);
    ids.voice.onclick();await settle();
    const start=sent.findLast(x=>x.op==='start'),pcm=sent.findLast(x=>x.op==='pcm');
    assert.equal(new DataView(pcm.raw).getUint32(4,true),start.id);
    assert.equal(new DataView(pcm.raw).getUint32(8,true),4);
    assert(sent.findLast(x=>x.op==='end').recording===start.id);assert(trackStops>0);
    ids.voice.onclick();await settle();worklet.pcm();await settle();
    const stopsBefore=trackStops,ends=sent.filter(x=>x.op==='end').length;
    buttons[3].fire('pointerdown');await settle();buttons[3].fire('pointerup');await settle();
    worklet.pcm();await settle();worklet.pcm();await settle();
    assert.equal(trackStops,stopsBefore);assert.equal(sent.filter(x=>x.op==='end').length,ends);
    ids.voice.onclick();await settle();assert.equal(sent.filter(x=>x.op==='end').length,ends+1);
    assert(state.enabled); // 关闭收听仍然独占。
    buttons[0].fire('pointerdown');await settle();events.blur();await settle();assert.equal(sent.filter(x=>x.op==='move').at(-1).state,0);
    await ids.toggle.onclick();assert(!state.enabled);assert.equal(sent.at(-1).op,'disable');
    socket.readyState=3;socket.onclose();await settle();assert(ids.toggle.disabled);assert(ids.fire.disabled);assert(buttons.every(b=>b.disabled));
    console.log('UI: touch/mouse release, keyboard, stop without waiting for ACK, continuous recording, headers/end, cancellation, blur, switch and disconnect checks passed');
})().catch(error=>{console.error(error);process.exitCode=1;});
