// M25-4a: full reset verification - running check + suspend=true + restore
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
const pc = async () => { const r = await cmd('Registers', 'get', 'R0.core0'); return r[3] ? Buffer.from(r[3].replace(/"/g, ''), 'base64').readUInt32LE(0) : null; };
const hx = v => v == null ? '?' : '0x' + (v >>> 0).toString(16).padStart(8, '0');

(async () => {
    await sleep(400);
    // 1) running check (PC must change over time)
    const a = await pc(); await sleep(400); const b = await pc();
    console.log(`running check: ${hx(a)} -> ${hx(b)}  ${a !== b ? 'RUNNING ✓' : '(same - maybe halted)'}`);

    // 2) reset + suspend=true (stay halted at the reset vector)
    const r1 = await cmd('ContextReset', 'reset', 'core0', 'reset', { Suspend: true });
    console.log('reset(suspend=true) err:', JSON.stringify(r1[2]));
    await sleep(1200);
    console.log('PC after reset+halt:', hx(await pc()), '(expect 0xa0000020)');

    // 3) restore: reset + run (the natural state)
    const r2 = await cmd('ContextReset', 'reset', 'core0', 'reset', { Suspend: false });
    console.log('reset(suspend=false) err:', JSON.stringify(r2[2]));
    await sleep(1500);
    const c = await pc(); await sleep(400); const d = await pc();
    console.log(`restored: ${hx(c)} -> ${hx(d)}  ${c !== d ? 'RUNNING ✓' : '(same)'}`);
    process.exit(0);
})();
