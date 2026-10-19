// M25 UI rehearsal: read core0 PC (and state) from the running agent
'use strict';
const net = require('net');
const EOM = Buffer.from([3, 1]);
function tb(w, a) {
    const p = [Buffer.from(w.join('\0') + '\0', 'utf8')];
    for (const x of (a || [])) { p.push(Buffer.from(JSON.stringify(x), 'utf8')); p.push(Buffer.from([0])); }
    p.push(EOM);
    return Buffer.concat(p);
}
const pend = new Map(); let tok = 0; let buf = Buffer.alloc(0);
const s = net.connect(1534, '127.0.0.1', () => s.write(tb(['E', 'Locator', 'Hello', JSON.stringify(['Locator'])])));
s.on('data', d => { buf = Buffer.concat([buf, d]); for (;;) { const i = buf.indexOf(EOM); if (i < 0) return; const p = buf.subarray(0, i).toString().split('\0'); buf = buf.subarray(i + 2); if (p[0] === 'R') { const pr = pend.get(p[1]); if (pr) { pend.delete(p[1]); pr(p); } } } });
const cmd = (sv, c, ...a) => new Promise(r => { const t = String(++tok); pend.set(t, r); s.write(tb(['C', t, sv, c], a)); });
const sleep = ms => new Promise(r => setTimeout(r, ms));
(async () => {
    await sleep(400);
    const r = await cmd('Registers', 'get', 'R0.core0');
    const v = r[3] ? Buffer.from(r[3].replace(/"/g, ''), 'base64').readUInt32LE(0) : null;
    console.log('core0 PC = 0x' + (v >>> 0).toString(16).padStart(8, '0'));
    process.exit(0);
})();
