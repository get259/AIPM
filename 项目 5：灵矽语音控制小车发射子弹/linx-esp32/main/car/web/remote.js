'use strict';
const elements = new Map();
const el = id => { if(!elements.has(id))elements.set(id,document.getElementById(id)); return elements.get(id); };
const directions = [...document.querySelectorAll('[data-state]')];
let socket, requestId = 0, pending = new Map(), info = {}, startingVoice = false;
let moving = false, heldDirection = null, voiceHeld = false, capture = null, captureToken = 0;
let voiceFinishing = false;
let toggling = false;
let recordingId = 0, audioSequence = 0, audioQueue = [], audioFlights = new Set(), audioWaiters = [];
let lastRender = "";
function notice(message, error=false) { el('notice').textContent = message; el('notice').classList.toggle('error', error); }
function render(data) {
    info = data;
    const signature=JSON.stringify([data.enabled,data.mine,data.owned,data.platform,toggling,socket?.readyState]);
    if(signature===lastRender)return; lastRender=signature;
    const connected = socket?.readyState === WebSocket.OPEN;
    const mine = connected && data.enabled && data.mine;
    el('toggle').disabled = toggling || !connected || (data.owned && !data.mine);
    el('toggle').textContent = mine ? '关闭遥控' : data.enabled && !data.owned ? '接管遥控' : '开启遥控';
    el('toggle').classList.toggle('on', !!mine); el('toggle').setAttribute('aria-pressed', String(!!mine));
    el('source').textContent = data.enabled ? data.mine ? '当前来源：本页独占' : data.owned ? '当前来源：其他设备' : '网页模式，等待重新接管' : '当前来源：INMP441';
    if(!connected)el('source').textContent='连接断开，控制来源待确认';
    for (const button of directions) button.disabled = !mine;
    el('stop').disabled = !mine;
    el('fire').disabled = !mine;
    // 先允许用户点击，浏览器才有机会弹出麦克风授权；录音条件在点击后明确提示。
    el('voice').disabled = !capture && !mine;
}
function request(op, fields={}, binary=null) {
    if (socket?.readyState !== WebSocket.OPEN) return Promise.reject(Error('连接已断开，请重新连接'));
    if (pending.size >= 8 && op !== 'disable' && !(op === 'move' && fields.state === 0)) return Promise.reject(Error('设备忙，请稍后重试'));
    const id = ++requestId;
    return new Promise((resolve,reject) => {
        const timer = setTimeout(() => { pending.delete(id); reject(Error('设备未确认，请检查连接')); }, 20000);
        pending.set(id,{resolve,reject,timer});
        if (binary) {
            const bytes = new ArrayBuffer(12 + binary.length*2); const view = new DataView(bytes);
            view.setUint32(0,id,true); view.setUint32(4,recordingId,true); view.setUint32(8,audioSequence++,true);
            binary.forEach((sample,i) => view.setInt16(12+i*2,sample,true)); socket.send(bytes);
        } else socket.send(JSON.stringify({id,op,...fields}));
        if(op === 'start') recordingId = id;
    });
}
function connect() {
    if (socket && socket.readyState < WebSocket.CLOSING) return;
    const current = new WebSocket(`wss://192.168.4.1:8443/control`); socket = current;
    el('connection').textContent = '正在连接…';
    current.onopen = () => { el('connection').textContent='● 已连接'; request('status').then(data=>notice(data.message)).catch(fail); };
    current.onmessage = event => {
        let data; try { data=JSON.parse(event.data); } catch { return; }
        if('enabled' in data)render(data);
        if(capture&&data.recording===false)cancelCapture(false);
        const item=pending.get(data.id); if (!item) return;
        clearTimeout(item.timer); pending.delete(data.id);
        if(data.ok) item.resolve(data); else item.reject(Error(data.message));
    };
    current.onclose = () => {
        if (socket !== current) return;
        for(const item of pending.values()){clearTimeout(item.timer);item.reject(Error('连接已断开'));} pending.clear();
        heldDirection=null; moving=false; render({});
        el('connection').textContent='○ 已断开'; notice('连接已断开，请重新连接并读取控制状态。',true);
    };
    current.onerror = () => notice('连接失败，请确认已连接热点并信任本地根证书。',true);
}
function fail(error){
    const messages={NotAllowedError:'未获麦克风权限，请在浏览器设置中允许本页录音，并确认已信任证书。',NotFoundError:'未找到可用麦克风，请检查设备。',NotReadableError:'麦克风暂时无法使用，可能被其他程序占用。'};
    notice(messages[error.name] || error.message || String(error),true);
}
function enqueueMove(state) {
    // 停止立即写入有序连接，不等待前一条确认。
    request('move',{state}).then(data=>notice(data.message)).catch(fail);
}
function clearPressed(){directions.forEach(button=>button.classList.remove('pressed'));}
function stopMovement(force=false) {
    if(!moving&&!force)return;
    moving=false;heldDirection=null;clearPressed();
    if(info.enabled&&info.mine) enqueueMove(0);
}
function pressDirection(state, button, identity) {
    if(!info.enabled||!info.mine||heldDirection)return;
    heldDirection=identity; moving=true; clearPressed();button?.classList.add('pressed');enqueueMove(state);
}
for(const button of directions){
    button.addEventListener('contextmenu',event=>event.preventDefault());
    button.addEventListener('pointerdown',event=>{event.preventDefault();button.setPointerCapture(event.pointerId);pressDirection(Number(button.dataset.state),button,`p${event.pointerId}`);});
    for(const type of ['pointerup','pointercancel','lostpointercapture'])button.addEventListener(type,event=>{if(heldDirection===`p${event.pointerId}`)stopMovement();});
}
el('stop').onclick=()=>stopMovement(true);
el('fire').onclick=()=>{
    if(!info.enabled||!info.mine)return;
    request('fire').then(data=>notice(data.message)).catch(fail);
};
const keys={KeyW:1,ArrowUp:1,KeyS:2,ArrowDown:2,KeyA:8,ArrowLeft:8,KeyD:4,ArrowRight:4};
window.addEventListener('keydown',event=>{
    if(/INPUT|TEXTAREA|SELECT/.test(event.target.tagName)||event.target.isContentEditable||!info.mine)return;
    if(event.code==='Space'){event.preventDefault();if(!event.repeat)stopMovement(true);}
    if(keys[event.code]){event.preventDefault();if(!event.repeat)pressDirection(keys[event.code],directions.find(b=>Number(b.dataset.state)===keys[event.code]),event.code);}
});
window.addEventListener('keyup',event=>{if(heldDirection===event.code){event.preventDefault();stopMovement();}});
el('toggle').onclick=async()=>{
    if(toggling)return;toggling=true;render(info);
    try{stopMovement();const result=request(info.enabled&&info.mine?'disable':'enable');await cancelCapture(false);const data=await result;notice(data.message);}
    catch(error){fail(error);}finally{toggling=false;render(info);}
};
el('reconnect').onclick=connect;
// 有界流水上传：最多三帧等待确认，给控制请求保留处理余量。
function pumpAudio() {
    const token=captureToken, flights=audioFlights;
    while(audioQueue.length&&flights.size<3&&pending.size<7){
        if(socket?.readyState!==WebSocket.OPEN||socket.bufferedAmount>8192){
            cancelCapture(true);fail(Error('音频连接拥塞，收听已关闭'));return;
        }
        const packet=request('pcm',{},audioQueue.shift());
        flights.add(packet);
        packet.catch(error=>{
            if(token===captureToken){cancelCapture(true);fail(error);}
        }).finally(()=>{
            flights.delete(packet);
            if(token===captureToken)pumpAudio();
            const waiters=audioWaiters.splice(0);waiters.forEach(resolve=>resolve());
        });
    }
}
function queuePcm(pcm,token){
    if(token!==captureToken)return;
    // 最多 480 ms 待发音频，吸收短时调度抖动，不无限积压。
    if(audioQueue.length>=8){cancelCapture(true);fail(Error('音频持续积压，收听已关闭，请检查热点连接'));return;}
    audioQueue.push(pcm);pumpAudio();
}
async function waitAudio(){while(audioFlights.size||audioQueue.length)await new Promise(resolve=>audioWaiters.push(resolve));}
async function releaseCapture(resources){
    if(!resources)return;
    resources.stream?.getTracks().forEach(track=>track.stop());
    resources.source?.disconnect();resources.node?.disconnect();resources.sink?.disconnect();
    if(resources.context&&resources.context.state!=='closed')await resources.context.close();
}
async function beginVoice(){
    if(startingVoice||voiceFinishing||capture||!voiceHeld)return;
    stopMovement();voiceHeld=true;
    startingVoice=true;const token=++captureToken;let resources={};
    try{
        if(!window.isSecureContext)throw Error('浏览器未将本页视为安全页面，请信任本地根证书后重新打开 HTTPS 页面。');
        if(!navigator.mediaDevices?.getUserMedia)throw Error('此浏览器无法申请麦克风权限，请用支持录音的完整浏览器打开本页。');
        if(!window.AudioWorkletNode)throw Error('此浏览器不支持音频处理，请换用支持录音的完整浏览器。');
        // 授权弹窗出现前先给出可见反馈，避免点击后页面看起来没有响应。
        el('mic-state').textContent='正在申请麦克风权限，请在浏览器提示中选择允许…';
        const context=new AudioContext();resources.context=context;
        await context.resume();
        resources.stream=await navigator.mediaDevices.getUserMedia({audio:{channelCount:1,echoCancellation:true,noiseSuppression:true},video:false});
        if(!voiceHeld||token!==captureToken){await releaseCapture(resources);return;}
        await context.audioWorklet.addModule('/capture.js');
        const node=new AudioWorkletNode(context,'car-capture');resources.node=node;
        resources.source=context.createMediaStreamSource(resources.stream);
        resources.sink=context.createGain();resources.sink.gain.value=0;
        await request('start');
        if(!voiceHeld||token!==captureToken){await releaseCapture(resources);await request('cancel');return;}
        audioSequence=0;audioQueue=[];capture=resources;
        resources.done=new Promise(resolve=>resources.finish=resolve);
        node.port.onmessage=event=>{if(event.data.pcm)queuePcm(event.data.pcm,token);if(event.data.done)resources.finish();};
        resources.source.connect(node);node.connect(resources.sink);resources.sink.connect(context.destination);
        el('voice').classList.add('active');el('voice').textContent='关闭收听';el('mic-state').textContent='正在接收浏览器音频…';notice('录音中');
    }catch(error){await releaseCapture(resources);await cancelCapture(true);fail(error);el('mic-state').textContent=el('notice').textContent;}
    finally{startingVoice=false;}
}
async function endVoice(){
    voiceHeld=false;const resources=capture;if(!resources)return;
    voiceFinishing=true;
    capture=null;const token=captureToken;
    try{
        resources.node.port.postMessage('finish');await resources.done;await releaseCapture(resources);await waitAudio();
        if(token!==captureToken)return;
        const data=await request('end',{recording:recordingId});notice(data.message);el('mic-state').textContent='收听已关闭，网页继续独占控制。';
    }catch(error){if(token===captureToken){await cancelCapture(true);fail(error);}}
    finally{if(token===captureToken){voiceFinishing=false;el('voice').classList.remove('active');el('voice').textContent='开始收听';}}
}
async function cancelCapture(send){
    voiceHeld=false;voiceFinishing=false;++captureToken;audioQueue=[];audioFlights=new Set();const waiters=audioWaiters.splice(0);waiters.forEach(resolve=>resolve());const resources=capture;capture=null;
    el('voice').classList.remove('active');el('voice').textContent='开始收听';
    if(resources)resources.finish?.();await releaseCapture(resources);
    if(send&&socket?.readyState===WebSocket.OPEN&&info.mine){try{await request('cancel');}catch{}}
}
el('voice').addEventListener('contextmenu',event=>event.preventDefault());
el('voice').onclick=()=>{
    if(voiceFinishing)return;
    if(startingVoice){cancelCapture(true);return;}
    if(capture)endVoice();
    else{voiceHeld=true;beginVoice();}
};
function loseFocus(){stopMovement();}
window.addEventListener('blur',loseFocus);
document.addEventListener('visibilitychange',()=>{if(document.hidden)loseFocus();});
window.addEventListener('pagehide',()=>{stopMovement();cancelCapture(true);});
setInterval(()=>{if(!document.hidden&&!capture&&!startingVoice&&socket?.readyState===WebSocket.OPEN&&pending.size===0)request('status').catch(fail);},5000);
if(!window.isSecureContext)notice('录音需要 HTTPS 和已信任的本地证书。',true);
connect();
