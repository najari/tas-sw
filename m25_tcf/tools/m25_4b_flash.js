/*
 * m25_4b_flash.js - M25-4b: host driver for our own TC37x PFLASH flasher.
 *
 * Drives the M25 flash loader (m25_flash_loader.bin, runs in PSPR) through the
 * mailbox in DSPR, using the live TCF agent (TAS/DAS) for memory/register access.
 *
 * Usage:
 *   node m25_4b_flash.js save                      - save regs + PSPR/DSPR scratch -> m25_4b_state.json
 *   node m25_4b_flash.js load                      - upload loader, run it, wait for magic
 *   node m25_4b_flash.js erase  <addr> [count]     - erase N logical sectors (16 KiB each)
 *   node m25_4b_flash.js program <addr> <file>     - program file bytes at addr (32-byte pages)
 *   node m25_4b_flash.js read   <addr> <size> <out>- read flash to a file
 *   node m25_4b_flash.js verify <addr> <file>      - compare flash with a file
 *   node m25_4b_flash.js restore <file> <addr>     - erase + program a file back (restore)
 *   node m25_4b_flash.js stop                      - exit loader (ENDINIT back), halt core, restore state
 *
 * Safety: `save` must be run before `load`; `stop` restores registers and the
 * PSPR/DSPR scratch regions (the flash content is NOT restored - use backup/restore).
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

const CORE = 'core0';
const PSPR = 0x70100000;
const MAILBOX = 0x7000F000;
const PAYLOAD = 0x7000A000;          /* DSPR staging buffer (8 KiB) */
const LOADER_BIN = 'C:/work/tcf_agent/m25_flash_loader.bin';
const STATE_FILE = 'C:/work/tcf_agent/m25_4b_state.json';
const MAGIC = 0x4D32354D;
const MB = { MAGIC: 0, CMD: 1, ARG0: 2, ARG1: 3, ARG2: 4, STATUS: 5, DONE: 6, PROGRESS: 7 };
const CMD = { IDLE: 0, ERASE: 1, PROGRAM: 2, EXIT: 3 };

/* ---- low-level TCF helpers ---- */
async function memRead(addr, size) {
    const r = await cmdRaw('Memory', 'get', CORE, addr >>> 0, 1, size, 0);
    const b64 = (r[0] || '').replace(/"/g, '');
    return Buffer.from(b64, 'base64');
}
async function memWrite(addr, data) {
    await cmdRaw('Memory', 'set', CORE, addr >>> 0, 1, data.length, 0, data.toString('base64'));
    const rb = await memRead(addr, data.length);
    if (!rb.equals(data)) throw new Error(`memWrite verify failed at 0x${(addr >>> 0).toString(16)}`);
}
async function regGet(idx) {
    const r = await cmd('Registers', 'get', `R${idx}.${CORE}`);
    return j(r) ? Buffer.from(j(r).replace(/"/g, ''), 'base64').readUInt32LE(0) : 0;
}
async function regSet(idx, val) {
    const b = Buffer.alloc(4); b.writeUInt32LE(val >>> 0, 0);
    await cmd('Registers', 'set', `R${idx}.${CORE}`, b.toString('base64'));
}
async function mbRead(i) {
    const b = await memRead(MAILBOX + i * 4, 4);
    return b.readUInt32LE(0);
}
async function mbWrite(i, v) {
    const b = Buffer.alloc(4); b.writeUInt32LE(v >>> 0, 0);
    await memWrite(MAILBOX + i * 4, b);
}
async function waitMagic(timeoutMs) {
    const t0 = Date.now();
    for (;;) {
        const v = await mbRead(MB.MAGIC);
        if (v === MAGIC) return true;
        if (Date.now() - t0 > timeoutMs) return false;
        await sleep(200);
    }
}
async function runCmd(c, a0, a1, a2, timeoutMs = 120000) {
    await mbWrite(MB.DONE, 0);
    await mbWrite(MB.PROGRESS, 0);
    await mbWrite(MB.ARG0, a0 >>> 0);
    await mbWrite(MB.ARG1, a1 >>> 0);
    await mbWrite(MB.ARG2, a2 >>> 0);
    await mbWrite(MB.CMD, c);
    const t0 = Date.now();
    for (;;) {
        const done = await mbRead(MB.DONE);
        if (done === 1) {
            const status = await mbRead(MB.STATUS);
            const progress = await mbRead(MB.PROGRESS);
            return { status, progress };
        }
        if (Date.now() - t0 > timeoutMs) throw new Error(`command ${c} timeout (done=${done})`);
        await sleep(150);
    }
}

/* ---- commands ---- */
const REGIONS = [
    { base: 0x7000A000, size: 0x6000 },   /* DSPR payload + data + mailbox */
    { base: PSPR,       size: 0x2000 },   /* PSPR loader */
];

async function doSave() {
    console.log('=== saving state ===');
    const st0 = j(await cmd('RunControl', 'getState', CORE));
    const wasRunning = st0 === false || st0 === 'false' || st0 === 0;
    if (wasRunning) {
        console.log('core is running - suspending for a consistent snapshot');
        await cmd('RunControl', 'suspend', CORE);
        await sleep(300);
    }
    const regs = [];
    for (let i = 0; i < 40; i++) regs.push(j(await cmd('Registers', 'get', `R${i}.${CORE}`)));
    const regions = [];
    for (const r of REGIONS) {
        const mem = await memRead(r.base, r.size);
        regions.push({ base: r.base, size: r.size, b64: mem.toString('base64') });
        console.log(`  saved ${mem.length} bytes @0x${r.base.toString(16)}`);
    }
    fs.writeFileSync(STATE_FILE, JSON.stringify({ core: CORE, wasRunning, regs, regions }));
    console.log(`saved 40 registers + ${regions.length} regions (wasRunning=${wasRunning}) -> ${STATE_FILE}`);
    process.exit(0);
}

async function doLoad() {
    const code = fs.readFileSync(LOADER_BIN);
    console.log(`=== uploading loader (${code.length} bytes) to PSPR 0x${PSPR.toString(16)} ===`);
    await memWrite(PSPR, code);
    await regSet(0, PSPR);          /* PC = loader entry */
    await regSet(27, 0x7000C000);   /* A10 = SP (safe DSPR area) */
    const st = j(await cmd('RunControl', 'getState', CORE));
    console.log('core state before resume:', st);
    await cmd('RunControl', 'resume', CORE, 0, 1);
    const ok = await waitMagic(8000);
    console.log(ok ? 'loader ready (magic OK)' : 'MAGIC NOT FOUND - loader did not start');
    process.exit(ok ? 0 : 1);
}

async function doErase(addr, count) {
    console.log(`=== erase ${count} sector(s) @0x${addr.toString(16)} ===`);
    const r = await runCmd(CMD.ERASE, addr, count, 0);
    console.log(`erase done: status=0x${r.status.toString(16)} progress=${r.progress}/${count}`);
    process.exit(r.status === 0 ? 0 : 1);
}

async function doProgram(addr, file) {
    const data = fs.readFileSync(file);
    console.log(`=== program ${data.length} bytes from ${file} at 0x${addr.toString(16)} ===`);
    const CHUNK = 0x2000;   /* 8 KiB staging buffer */
    let off = 0;
    while (off < data.length) {
        const n = Math.min(CHUNK, data.length - off);
        const chunk = data.subarray(off, off + n);
        const padded = Buffer.alloc((n + 31) & ~31);
        chunk.copy(padded);
        await memWrite(PAYLOAD, padded);
        const r = await runCmd(CMD.PROGRAM, addr + off, padded.length, PAYLOAD);
        console.log(`  @0x${(addr + off).toString(16)}: status=0x${r.status.toString(16)} pages=${r.progress}/${padded.length / 32}`);
        if (r.status !== 0) { console.log('PROGRAM FAILED'); process.exit(1); }
        off += n;
    }
    console.log('program done');
    process.exit(0);
}

async function doRead(addr, size, out) {
    const mem = await memRead(addr, size);
    fs.writeFileSync(out, mem);
    console.log(`read ${mem.length} bytes @0x${addr.toString(16)} -> ${out}`);
    process.exit(0);
}

async function doVerify(addr, file) {
    const data = fs.readFileSync(file);
    const mem = await memRead(addr, data.length);
    if (mem.equals(data)) {
        console.log(`VERIFY OK (${data.length} bytes @0x${addr.toString(16)})`);
        process.exit(0);
    }
    let bad = -1;
    for (let i = 0; i < data.length; i++) if (mem[i] !== data[i]) { bad = i; break; }
    console.log(`VERIFY MISMATCH at offset 0x${bad.toString(16)} (addr 0x${(addr + bad).toString(16)}): flash=0x${mem[bad].toString(16)} file=0x${data[bad].toString(16)}`);
    process.exit(1);
}

async function doRestore(file, addr) {
    const data = fs.readFileSync(file);
    console.log(`=== restore ${data.length} bytes -> 0x${addr.toString(16)} ===`);
    const sectors = Math.ceil(data.length / 0x4000);
    let r = await runCmd(CMD.ERASE, addr, sectors, 0);
    console.log(`erase: status=0x${r.status.toString(16)} progress=${r.progress}/${sectors}`);
    if (r.status !== 0) process.exit(1);
    const padded = Buffer.alloc(sectors * 0x4000);
    data.copy(padded);
    const CHUNK = 0x2000;
    for (let off = 0; off < padded.length; off += CHUNK) {
        const chunk = padded.subarray(off, off + CHUNK);
        await memWrite(PAYLOAD, chunk);
        r = await runCmd(CMD.PROGRAM, addr + off, chunk.length, PAYLOAD);
        console.log(`  @0x${(addr + off).toString(16)}: status=0x${r.status.toString(16)}`);
        if (r.status !== 0) process.exit(1);
    }
    console.log('restore done');
    process.exit(0);
}

async function doStop() {
    console.log('=== stopping loader ===');
    try {
        const r = await runCmd(CMD.EXIT, 0, 0, 0, 10000);
        console.log(`loader exit: status=0x${r.status.toString(16)}`);
    } catch (e) {
        console.log('loader exit skipped:', e.message);
    }
    await cmd('RunControl', 'suspend', CORE);
    if (fs.existsSync(STATE_FILE)) {
        const st = JSON.parse(fs.readFileSync(STATE_FILE, 'utf8'));
        console.log('restoring registers + scratch regions');
        for (let i = 0; i < st.regs.length; i++) {
            if (st.regs[i]) await cmd('Registers', 'set', `R${i}.${CORE}`, st.regs[i]);
        }
        for (const r of st.regions) {
            const b = Buffer.from(r.b64, 'base64');
            await memWrite(r.base, b);
            console.log(`  restored ${b.length} bytes @0x${r.base.toString(16)}`);
        }
        console.log('state restored');
        if (st.wasRunning) {
            console.log('core was running before - resuming');
            await cmd('RunControl', 'resume', CORE, 0, 1);
        }
    } else {
        console.log('no state file - registers not restored');
    }
    process.exit(0);
}

async function doScan(start, count) {
    console.log(`=== scanning ${count} sectors from 0x${start.toString(16)} ===`);
    for (let i = 0; i < count; i++) {
        const a = start + i * 0x4000;
        const b = await memRead(a, 64);
        let ff = 0; for (const x of b) if (x === 0xFF) ff++;
        console.log(`0x${a.toString(16)}: ${ff === b.length ? 'ERASED (all 0xFF)' : 'used (first byte 0x' + b[0].toString(16) + ')'}`);
    }
    process.exit(0);
}

async function doBp() {
    const ids = j(await cmd('Breakpoints', 'getIDs'));
    console.log('breakpoint IDs:', JSON.stringify(ids));
    if (Array.isArray(ids) && ids.length) {
        const props = j(await cmd('Breakpoints', 'getProperties', ids));
        console.log('properties:', JSON.stringify(props));
        const st = await cmd('Breakpoints', 'getStatus');
        console.log('status:', JSON.stringify(st));
    }
    process.exit(0);
}

async function doBpClear() {
    const ids = j(await cmd('Breakpoints', 'getIDs'));
    console.log('breakpoint IDs:', JSON.stringify(ids));
    if (Array.isArray(ids) && ids.length) {
        for (const id of ids) {
            try {
                await cmd('Breakpoints', 'remove', id);
                console.log(`removed ${id}`);
            } catch (e) {
                console.log(`remove ${id} failed: ${e.message}`);
            }
        }
    } else {
        console.log('no breakpoints registered');
    }
    process.exit(0);
}

(async () => {
    await sleep(300);
    const c = (process.argv[2] || '').toLowerCase();
    const num = (x, d) => (x !== undefined ? Number(x) : d);
    try {
        if (c === 'save') await doSave();
        else if (c === 'load') await doLoad();
        else if (c === 'erase') await doErase(num(process.argv[3], 0xA0000000) >>> 0, num(process.argv[4], 1));
        else if (c === 'program') await doProgram(num(process.argv[3], 0) >>> 0, process.argv[4]);
        else if (c === 'read') await doRead(num(process.argv[3], 0) >>> 0, num(process.argv[4], 0x1000), process.argv[5] || 'read.bin');
        else if (c === 'verify') await doVerify(num(process.argv[3], 0) >>> 0, process.argv[4]);
        else if (c === 'restore') await doRestore(process.argv[3], num(process.argv[4], 0) >>> 0);
        else if (c === 'stop') await doStop();
        else if (c === 'bp') await doBp();
        else if (c === 'bpclear') await doBpClear();
        else if (c === 'scan') await doScan(num(process.argv[3], 0x80000000) >>> 0, num(process.argv[4], 8));
        else {
            console.log('usage: save | load | erase <addr> [count] | program <addr> <file> | read <addr> <size> <out> | verify <addr> <file> | restore <file> <addr> | stop');
            process.exit(1);
        }
    } catch (e) {
        console.error('FAILED:', e.message);
        process.exit(1);
    }
})();
