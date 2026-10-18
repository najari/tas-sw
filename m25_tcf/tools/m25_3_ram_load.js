/*
 * m25_3_ram_load.js - M25-3 live demo: load the RAM test program into the
 * target's DSPR (core0), save the state first (registers + memory region).
 *
 * Usage: node m25_3_ram_load.js <text.bin> <load_addr> <entry_addr> <sp>
 * The saved state is written to m25_3_ram_state.json for the restore step.
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
        const t = p[0].toString();
        if (t !== 'R') continue;
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

const CORE = 'core0';
const MEM_BASE = 0x7000A000;
const MEM_SIZE = 0x2000;   /* 8 KiB saved/restored region */

(async () => {
    const textFile = process.argv[2] || 'C:/work/aurix_tcf_test/m25_ram_text.bin';
    const loadAddr = Number(process.argv[3] || 0x7000A000) >>> 0;
    const entry = Number(process.argv[4] || 0x7000A03A) >>> 0;
    const sp = Number(process.argv[5] || 0x7000C000) >>> 0;

    await sleep(400);

    /* --- save state --- */
    console.log('=== saving state ===');
    const regs = [];
    for (let i = 0; i < 40; i++) {
        const v = await cmd('Registers', 'get', `R${i}.${CORE}`);
        regs.push(j(v));
    }
    const mem = Buffer.from((await cmdRaw('Memory', 'get', CORE, MEM_BASE, 1, MEM_SIZE, 0))[0] || '', 'base64');
    const state = { core: CORE, regs, mem_base: MEM_BASE, mem_size: MEM_SIZE, mem_b64: mem.toString('base64') };
    fs.writeFileSync('m25_3_ram_state.json', JSON.stringify(state));
    console.log(`saved ${regs.length} registers + ${mem.length} bytes -> m25_3_ram_state.json`);

    /* --- load the code --- */
    const code = fs.readFileSync(textFile);
    console.log(`=== loading ${code.length} bytes @0x${loadAddr.toString(16)} ===`);
    await cmdRaw('Memory', 'set', CORE, loadAddr, 1, code.length, 0, code.toString('base64'));
    const rb = Buffer.from((await cmdRaw('Memory', 'get', CORE, loadAddr, 1, code.length, 0))[0] || '', 'base64');
    console.log('verify readback:', rb.equals(code) ? 'MATCH' : 'MISMATCH');

    /* --- prepare the core: PC = entry, SP (A10) = sp, ICR.IE off --- */
    const writeReg = async (idx, val) => {
        const b = Buffer.alloc(4); b.writeUInt32LE(val >>> 0, 0);
        await cmd('Registers', 'set', `R${idx}.${CORE}`, b.toString('base64'));
    };
    await writeReg(0, entry);       /* PC */
    await writeReg(27, sp);         /* A10 = SP */
    /* ICR (index 35) - keep the value but clear IE (bit 15) */
    const icr = Buffer.from(j(await cmd('Registers', 'get', `R35.${CORE}`)), 'base64');
    const icrVal = icr.readUInt32LE(0);
    await writeReg(35, icrVal & ~(1 << 15));
    console.log(`entry=0x${entry.toString(16)} sp=0x${sp.toString(16)} icr 0x${icrVal.toString(16)} -> 0x${(icrVal & ~(1 << 15)).toString(16)}`);

    const st = j(await cmd('RunControl', 'getState', CORE));
    console.log('state (should be suspended=true):', st);
    console.log('=== ram load done; run with RunControl resume ===');
    process.exit(0);
})().catch((e) => { console.error('FAILED', e.message); process.exit(1); });
