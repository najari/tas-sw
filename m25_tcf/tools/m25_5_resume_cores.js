// M25-5: resume cores 1 and 2 (restore the natural 3-core running state)
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
const sock = net.connect(1534, '127.0.0.1', () => sock.write(tb(['E', 'Locator', 'Hello', JSON.stringify(['Locator'])])));
sock.on('data', (d) => {
    buf = Buffer.concat([buf, d]);
    for (;;) {
        const i = buf.indexOf(EOM);
        if (i < 0) return;
        const p = buf.subarray(0, i).toString().split('\0');
        buf = buf.subarray(i + 2);
        if (p[0] === 'R') { const pr = pending.get(p[1]); if (pr) { pending.delete(p[1]); pr(p); } }
    }
});
const cmd = (service, c, ...args) => new Promise((res) => { const t = String(++token); pending.set(t, res); sock.write(tb(['C', t, service, c], args)); });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
    await sleep(300);
    for (const core of ['core1', 'core2']) {
        const st0 = await cmd('RunControl', 'getState', core);
        console.log(`${core} before:`, JSON.stringify(st0.slice(2, 4)));
        if (st0.slice(2).some((x) => x.trim() === 'true')) {
            const r = await cmd('RunControl', 'resume', core, 0, 1);
            console.log(`${core} resume ->`, JSON.stringify(r.slice(2, 4)));
        } else {
            console.log(`${core} already running`);
        }
    }
    await sleep(500);
    for (const core of ['core0', 'core1', 'core2']) {
        const st = await cmd('RunControl', 'getState', core);
        console.log(`${core} final:`, JSON.stringify(st.slice(2, 4)));
    }
    process.exit(0);
})();
