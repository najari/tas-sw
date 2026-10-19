// M25-3: test all run-control modes (continue / step into / over / line / out)
'use strict';
const net = require('net');
const EOM = Buffer.from([3, 1]);
function tb(w, a) {
    const p = [Buffer.from(w.join('\0') + '\0', 'utf8')];
    for (const x of (a || [])) { p.push(Buffer.from(JSON.stringify(x), 'utf8')); p.push(Buffer.from([0])); }
    p.push(EOM);
    return Buffer.concat(p);
}
const pending = new Map(); let token = 0; let buf = Buffer.alloc(0);
const events = [];
const sock = net.connect(1534, '127.0.0.1', () => sock.write(tb(['E', 'Locator', 'Hello', JSON.stringify(['Locator'])])));
sock.on('data', (d) => {
    buf = Buffer.concat([buf, d]);
    for (;;) {
        const i = buf.indexOf(EOM);
        if (i < 0) return;
        const p = buf.subarray(0, i).toString().split('\0');
        buf = buf.subarray(i + 2);
        if (p[0] === 'R') {
            const pr = pending.get(p[1]);
            if (!pr) continue;
            pending.delete(p[1]);
            pr(p);
        } else if (p[0] === 'E') {
            events.push(p.slice(1).join(' ').slice(0, 100));
        }
    }
});
const cmd = (service, c, ...args) => new Promise((res) => { const t = String(++token); pending.set(t, res); sock.write(tb(['C', t, service, c], args)); });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const hex = (v) => (v >>> 0).toString(16).padStart(8, '0');

async function readPC() {
    const r = await cmd('Registers', 'get', 'R0.core0');
    if (!r[3]) return null;
    return Buffer.from(r[3].replace(/"/g, ''), 'base64').readUInt32LE(0);
}
async function readReg(id) {
    const r = await cmd('Registers', 'get', `R${id}.core0`);
    if (!r[3]) return null;
    return Buffer.from(r[3].replace(/"/g, ''), 'base64').readUInt32LE(0);
}
async function resume(mode) {
    const r = await cmd('RunControl', 'resume', 'core0', mode, 1);
    return r[2] === 'null' || r[2] === '' ? null : r[2];
}
async function waitStopped(ms) {
    const t0 = Date.now();
    while (Date.now() - t0 < ms) {
        await sleep(150);
        const s = await cmd('RunControl', 'getState', 'core0');
        const st = s[3] ? JSON.parse(s[3]) : null;
        if (st === 0) return true;      /* stopped */
    }
    return false;
}

(async () => {
    await sleep(300);
    const mode = process.argv[2] || 'all';
    const names = { 0: 'continue', 1: 'stepOver', 2: 'stepInto', 3: 'nextLine', 4: 'stepInLine', 5: 'stepOut' };

    let pc0 = await readPC();
    console.log(`PC start = 0x${hex(pc0)}`);

    const tests = mode === 'all' ? [2, 3, 4, 1, 5] : [Number(mode)];
    for (const m of tests) {
        const before = await readPC();
        const err = await resume(m);
        if (err) { console.log(`mode ${m} (${names[m]}): RESUME ERROR ${err}`); continue; }
        const ok = await waitStopped(4000);
        const after = await readPC();
        const a11 = await readReg(28);
        console.log(`mode ${m} (${names[m]}): stopped=${ok} PC 0x${hex(before)} -> 0x${hex(after)} A11=0x${hex(a11)}`);
    }

    /* continue test: run then suspend */
    const c0 = await readPC();
    const err = await resume(0);
    if (!err) {
        await sleep(800);
        const st = await cmd('RunControl', 'getState', 'core0');
        const running = st[3] ? JSON.parse(st[3]) : null;
        await cmd('RunControl', 'suspend', 'core0');
        await waitStopped(3000);
        const c1 = await readPC();
        console.log(`mode 0 (continue): state-after-800ms=${running} PC 0x${hex(c0)} -> 0x${hex(c1)}`);
    }
    console.log('events:', events.slice(-6).join(' | '));
    process.exit(0);
})();
