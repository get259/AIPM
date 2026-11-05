const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const html = fs.readFileSync(path.join(__dirname, '../third_party/esp-wifi-connect/assets/wifi_configuration_done.html'), 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];

function page(states, startOk = true) {
    const elements = new Map();
    const timers = [];
    const calls = [];
    let loaded;
    let copied;
    let input;
    const element = () => ({
        textContent: '', hidden: false, disabled: false, listeners: {},
        addEventListener(name, fn) { this.listeners[name] = fn; },
        set innerHTML(_) { throw new Error('Untrusted HTML must not be rendered'); },
    });
    const document = {
        getElementById(id) { if (!elements.has(id)) elements.set(id, element()); return elements.get(id); },
        createElement() { input = { value: '', select() {}, remove() {} }; return input; },
        body: { appendChild() {} },
        execCommand(command) { assert.equal(command, 'copy'); copied = input.value; return true; },
    };
    const context = vm.createContext({
        document, navigator: {},
        window: { isSecureContext: false, addEventListener(_, fn) { loaded = fn; } },
        setTimeout(fn, delay) { timers.push({ fn, delay }); },
        async fetch(url, options = {}) {
            calls.push({ url, options });
            if (url === '/activation/status') {
                const state = states.shift();
                if (state instanceof Error) throw state;
                return { ok: true, async json() { return state; } };
            }
            return { ok: url !== '/activation/start' || startOk };
        },
    });
    vm.runInContext(script, context);
    return {
        elements, timers, calls,
        load: () => loaded(),
        tick: async () => { assert.ok(timers.length); await timers.shift().fn(); },
        copy: async () => elements.get('copy-code').listeners.click(),
        copied: () => copied,
    };
}

test('等待激活时保留页面，激活后清除激活码并退出', async () => {
    const p = page([
        { enabled: true, state: 'connecting', code: '', message: '连接中' },
        { enabled: true, state: 'waiting', code: '001234', message: '请绑定' },
        { enabled: true, state: 'ready', code: '', message: '完成' },
    ]);
    await p.load();
    assert.equal(p.calls.filter(x => x.url === '/exit').length, 0);
    await p.tick();
    assert.equal(p.elements.get('activation-code').textContent, '001234');
    assert.equal(p.elements.get('activation-panel').hidden, false);
    await p.copy();
    assert.equal(p.copied(), '001234');
    await p.tick();
    assert.equal(p.elements.get('activation-panel').hidden, true);
    assert.equal(p.elements.get('activation-code').textContent, '');
    assert.equal(p.elements.get('status').textContent, '设备绑定成功');
    assert.equal(p.calls.filter(x => x.url === '/activation/start').length, 1);
    assert.equal(p.timers[0].delay, 1500);
    await p.tick();
    assert.equal(p.calls.filter(x => x.url === '/exit').length, 1);
    await p.load();
    assert.equal(p.timers.length, 0);
});

test('不开启激活页时保留原自动退出行为', async () => {
    const p = page([{ enabled: false }]);
    await p.load();
    assert.equal(p.timers[0].delay, 1000);
    await p.tick();
    assert.equal(p.calls.filter(x => x.url === '/exit').length, 1);
    assert.equal(p.calls.filter(x => x.url === '/activation/start').length, 0);
});

test('网络失败或无效响应时继续轮询，不误报完成或关闭热点', async () => {
    const p = page([new Error('offline'), {}, { enabled: true, state: 'error', code: '', message: '平台暂不可用' }]);
    await p.load();
    await p.tick();
    await p.tick();
    assert.equal(p.calls.filter(x => x.url === '/exit').length, 0);
    assert.equal(p.elements.get('status').textContent, '暂时未能完成激活');
    assert.equal(p.elements.get('activation-message').textContent, '平台暂不可用');
    assert.equal(p.timers.length, 1);
});

test('激活请求失败可重试，平台内容仅作为文本展示', async () => {
    const state = { enabled: true, state: 'waiting', code: '012345', message: '<img src=x onerror=alert(1)>' };
    const p = page([state, state], false);
    await p.load();
    await p.tick();
    assert.equal(p.calls.filter(x => x.url === '/activation/start').length, 2);
    assert.equal(p.elements.get('activation-message').textContent, state.message);
    assert.equal(p.calls.filter(x => x.url === '/exit').length, 0);
    assert.ok(p.calls.every(x => x.options.cache === 'no-store'));
});
