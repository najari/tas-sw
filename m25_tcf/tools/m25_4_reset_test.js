// M25-4a: ContextReset service check (getCapabilities only - non destructive)
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
        if (p[0] === 'R') {
            const pr = pending.get(p[1]);
            if (!pr) continue;
            pending.delete(p[1]);
            pr(p);
        }
    }
});
const cmd = (service, c, ...args) => new Promise((res) => { const t = String(++token); pending.set(t, res); sock.write(tb(['C', t, service, c], args)); });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
    await sleep(300);
    const r = await cmd('ContextReset', 'getCapabilities', 'core0');
    console.log('err:', JSON.stringify(r[2]));
    console.log('caps:', r.slice(3).join('').slice(0, 300));
    if (process.argv[2] === 'reset') {
        const suspend = process.argv[3] !== 'run';
        console.log(`\n--- performing reset (suspend=${suspend}) ---`);
        const t0 = Date.now();
        const rr = await cmd('ContextReset', 'reset', 'core0', 'reset', { Suspend: suspend });
        console.log('reset err:', JSON.stringify(rr[2]), `(${Date.now() - t0} ms)`);
        await sleep(1500);
        const pc = await cmd('Registers', 'get', 'R0.core0');
        if (pc[3]) console.log('PC after reset: 0x' + Buffer.from(pc[3].replace(/"/g, ''), 'base64').readUInt32LE(0).toString(16));
    }
    process.exit(0);
})();
