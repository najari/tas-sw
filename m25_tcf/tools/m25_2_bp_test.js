/*
 * m25_2_bp_test.js - M25-2 breakpoint test (SW + HW) against our TCF agent.
 *
 * Flow: resume core0 -> halt -> read PC -> set SW breakpoint at PC+4 ->
 * resume -> wait for the stop event -> report.  Then the same with a
 * hardware breakpoint (OCDS TR unit).
 */
'use strict';

const net = require('net');
const EOM = Buffer.from([3, 1]);

function toBuffer(words, args) {
    const parts = [Buffer.from(words.join('\0') + '\0', 'utf8')];
    for (const a of (args || [])) {
        parts.push(Buffer.from(JSON.stringify(a), 'utf8'));
        parts.push(Buffer.from([0]));
    }
    parts.push(EOM);
    return Buffer.concat(parts);
}

function splitZeros(buf) {
    const out = [];
    let start = 0;
    for (let i = 0; i < buf.length; i++) {
        if (buf[i] === 0) { out.push(buf.subarray(start, i)); start = i + 1; }
    }
    if (start < buf.length) out.push(buf.subarray(start));
    return out;
}

class Client {
    constructor() { this.pending = new Map(); this.token = 0; this.buf = Buffer.alloc(0); this.events = []; }
    connect() {
        return new Promise((resolve, reject) => {
            this.sock = net.connect(1534, '127.0.0.1', () => resolve());
            this.sock.on('error', reject);
            this.sock.on('data', (d) => this.onData(d));
        });
    }
    onData(d) {
        this.buf = Buffer.concat([this.buf, d]);
        for (;;) {
            const i = this.buf.indexOf(EOM);
            if (i < 0) return;
            const p = this.buf.subarray(0, i);
            this.buf = this.buf.subarray(i + 2);
            this.onPacket(p);
        }
    }
    onPacket(packet) {
        const parts = splitZeros(packet);
        const type = parts[0].toString();
        if (type === 'R' || type === 'P') {
            const token = parts[1].toString();
            const err = parts[2] ? parts[2].toString() : '';
            const data = parts.slice(3).map((b) => b.toString());
            const p = this.pending.get(token);
            if (!p) { console.log('[unknown result]', packet.toString()); return; }
            this.pending.delete(token);
            if (err && err.length > 0 && err !== 'null') p.reject(new Error('TCF error: ' + err));
            else p.resolve(data);
        }
        else if (type === 'E') {
            const ev = { service: parts[1].toString(), event: parts[2].toString(), data: parts.slice(3).map((b) => b.toString()) };
            this.events.push(ev);
            console.log(`[event] ${ev.service}/${ev.event} ${ev.data.join(' | ')}`);
        }
        else console.log('[packet]', packet.toString());
    }
    command(service, cmd, ...args) {
        return new Promise((resolve, reject) => {
            const token = String(++this.token);
            this.pending.set(token, { resolve, reject });
            this.sock.write(toBuffer(['C', token, service, cmd], args));
        });
    }
    sendEvent(service, event, ...args) { this.sock.write(toBuffer(['E', service, event], args)); }
    close() { try { this.sock.end(); } catch (e) { } }
}

const j = (d) => (d && d.length ? JSON.parse(d[0]) : null);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function getPC(c, id) {
    // prefer the RunControl state (it carries the PC when the context is
    // intercepted and does not disturb the target)
    const st = await c.command('RunControl', 'getState', id);
    const suspended = st.length ? st[0] === 'true' : false;
    if (suspended && st.length > 1) {
        const pc = Number(st[1]);
        if (Number.isFinite(pc) && pc > 0) return pc >>> 0;
    }
    const regs = j(await c.command('Registers', 'getChildren', id));
    for (const rid of regs) {
        const rd = j(await c.command('Registers', 'getContext', rid));
        if (rd && rd.Name === 'PC') {
            const v = await c.command('Registers', 'get', rid);
            return Buffer.from(j(v), 'base64').readUInt32LE(0);
        }
    }
    return 0;
}

async function getState(c, id) {
    const st = j(await c.command('RunControl', 'getState', id));
    return st;
}

async function waitStop(c, id, ms) {
    const deadline = Date.now() + ms;
    while (Date.now() < deadline) {
        await sleep(200);
        const st = j(await c.command('RunControl', 'getState', id));
        if (st === true) return true;
    }
    return false;
}
(async () => {
    const c = new Client();
    await c.connect();
    c.sendEvent('Locator', 'Hello', ['Locator']);
    await sleep(300);
    console.log('connected');

    const threads = j(await c.command('RunControl', 'getChildren', ''));
    const core = threads[0];
    console.log('core:', core);

    // ensure suspended
    try { await c.command('RunControl', 'suspend', core); } catch (e) { }
    await sleep(400);

    const pc0 = await getPC(c, core);
    console.log(`PC (suspended) = 0x${pc0.toString(16)}`);
    const bpAddr = (pc0 + 4) >>> 0;
    console.log(`breakpoint address = 0x${bpAddr.toString(16)}`);

    // ---------- software breakpoint ----------
    console.log('--- SW breakpoint test ---');
    try {
        await c.command('Breakpoints', 'set', [{
            ID: 'swbp1', Enabled: true, BreakpointType: 'Software',
            Location: '0x' + bpAddr.toString(16), AccessMode: 4, ContextIds: [core]
        }]);
        console.log('  SW breakpoint set');
    } catch (e) { console.log('  SW breakpoint set error:', e.message); }
    await sleep(400);

    try { await c.command('RunControl', 'suspend', core); } catch (e) { }   /* ensure intercepted */
    await sleep(300);
    try { await c.command('RunControl', 'resume', core, 0, 1); } catch (e) { console.log('  resume error:', e.message); }
    const hitSW = await waitStop(c, core, 25000);
    const pc1 = await getPC(c, core);
    console.log(`  stopped=${hitSW} PC=0x${pc1.toString(16)} (bp=0x${bpAddr.toString(16)}, delta=${pc1 - bpAddr})`);
    // read breakpoint status
    try {
        const ids = j(await c.command('Breakpoints', 'getIDs'));
        console.log('  breakpoint ids:', ids);
        if (ids && ids.length) {
            const props = j(await c.command('Breakpoints', 'getProperties', ids[0]));
            console.log('  bp props:', JSON.stringify(props));
        }
    } catch (e) { console.log('  bp query error:', e.message); }

    // cleanup SW bp
    try { await c.command('Breakpoints', 'remove', ['swbp1']); } catch (e) { }
    await sleep(300);

    // ---------- hardware breakpoint ----------
    console.log('--- HW breakpoint test ---');
    const pc2 = await getPC(c, core);
    const bpAddr2 = (pc2 + 4) >>> 0;
    console.log(`PC = 0x${pc2.toString(16)}, HW bp = 0x${bpAddr2.toString(16)}`);
    try {
        await c.command('Breakpoints', 'set', [{
            ID: 'hwb1', Enabled: true, BreakpointType: 'Hardware',
            Location: '0x' + bpAddr2.toString(16), AccessMode: 4, ContextIds: [core]
        }]);
        console.log('  HW breakpoint set');
    } catch (e) { console.log('  HW breakpoint set error:', e.message); }
    await sleep(400);
    try {
        const props = j(await c.command('Breakpoints', 'getProperties', 'hwb1'));
        console.log('  hwb1 props:', JSON.stringify(props));
    } catch (e) { console.log('  hwb1 query error:', e.message); }

    try { await c.command('RunControl', 'suspend', core); } catch (e) { }   /* ensure intercepted */
    await sleep(300);
    try { await c.command('RunControl', 'resume', core, 0, 1); } catch (e) { console.log('  resume error:', e.message); }
    const hitHW = await waitStop(c, core, 25000);
    const pc3 = await getPC(c, core);
    console.log(`  stopped=${hitHW} PC=0x${pc3.toString(16)} (bp=0x${bpAddr2.toString(16)}, delta=${pc3 - bpAddr2})`);

    try { await c.command('Breakpoints', 'remove', ['hwb1']); } catch (e) { }
    await sleep(300);

    // final state: ensure suspended (restore)
    try { await c.command('RunControl', 'suspend', core); } catch (e) { }
    await sleep(400);
    const st = await getState(c, core);
    const pcF = await getPC(c, core);
    console.log(`final: suspended=${st} PC=0x${pcF.toString(16)}`);
    console.log('=== M25-2 bp test done ===');
    c.close();
    process.exit(0);
})().catch((e) => { console.error('FAILED:', e); process.exit(1); });
