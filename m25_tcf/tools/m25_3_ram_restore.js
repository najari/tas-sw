/*
 * m25_3_ram_restore.js - restore the target state saved by m25_3_ram_load.js
 * (all 40 registers + the DSPR region) and leave the core suspended.
 */
'use strict';
const net = require('net');
const fs = require('fs');
const EOM = Buffer.from([3, 1]);

function tb(w, a) {
    const p = [Buffer.from(w.join('\0') + '\0', 'utf8')];
    for (const x of (a || [])) { p.push(Buffer.from(JSON.stringify(x), 'utf8')); p.push(Buffer.from([0])); }
    p.push(EOM);
    return Buffer.concat(p);
}
function splitZeros(buf) {
    const out = []; let s = 0;
    for (let i = 0; i < buf.length; i++) if (buf[i] === 0) { out.push(buf.subarray(s, i)); s = i + 1; }
    if (s < buf.length) out.push(buf.subarray(s));
    return out;
}
const pending = new Map(); let token = 0; let buf = Buffer.alloc(0);
const sock = net.connect(1534, '127.0.0.1', () => sock.write(tb(['E', 'Locator', 'Hello', JSON.stringify(['Locator'])])));
sock.on('data', (d) => {
    buf = Buffer.concat([buf, d]);
    for (;;) {
        const i = buf.indexOf(EOM);
        if (i < 0) return;
        const p = splitZeros(buf.subarray(0, i));
        buf = buf.subarray(i + 2);
        if (p[0].toString() !== 'R') continue;
        const pr = pending.get(p[1].toString());
        if (!pr) continue;
        pending.delete(p[1].toString());
        const err = p[2] ? p[2].toString() : '';
        if (pr.raw) pr.resolve(p.slice(2).map((x) => x.toString()));
        else if (err && err !== 'null') pr.reject(new Error('TCF error: ' + err));
        else pr.resolve(p.slice(3).map((x) => x.toString()));
    }
});
const cmd = (service, c, ...args) => new Promise((res, rej) => { const t = String(++token); pending.set(t, { resolve: res, reject: rej }); sock.write(tb(['C', t, service, c], args)); });
const cmdRaw = (service, c, ...args) => new Promise((res, rej) => { const t = String(++token); pending.set(t, { resolve: res, reject: rej, raw: true }); sock.write(tb(['C', t, service, c], args)); });
const j = (d) => (d && d.length ? JSON.parse(d[0]) : null);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
    const state = JSON.parse(fs.readFileSync('m25_3_ram_state.json', 'utf8'));
    await sleep(400);

    /* make sure the core is suspended (intercepted) */
    try { await cmd('RunControl', 'suspend', state.core); } catch (e) { }
    await sleep(400);

    console.log('=== restoring registers ===');
    let regFail = 0;
    for (let i = 0; i < state.regs.length; i++) {
        try {
            await cmd('Registers', 'set', `R${i}.${state.core}`, state.regs[i]);
        } catch (e) { regFail++; console.log(`  reg R${i} restore failed: ${e.message}`); }
    }
    console.log(`registers restored (${state.regs.length - regFail}/${state.regs.length})`);

    console.log('=== restoring memory ===');
    const mem = Buffer.from(state.mem_b64, 'base64');
    await cmdRaw('Memory', 'set', state.core, state.mem_base, 1, mem.length, 0, mem.toString('base64'));
    const rb = Buffer.from((await cmdRaw('Memory', 'get', state.core, state.mem_base, 1, mem.length, 0))[0] || '', 'base64');
    console.log(`memory restored: ${rb.equals(mem) ? 'MATCH' : 'MISMATCH'}`);

    /* verify the PC */
    const st = await cmd('RunControl', 'getState', state.core);
    const pc = st.length > 1 ? (Number(st[1]) >>> 0) : 0;
    console.log(`final: suspended=${st[0]} pc=0x${pc.toString(16)}`);
    console.log('=== restore done ===');
    process.exit(0);
})().catch((e) => { console.error('FAILED', e.message); process.exit(1); });
