/*
 * tcf_test_client.js - minimal TCF protocol client for M25 smoke tests.
 *
 * Usage: node tcf_test_client.js [host] [port]
 *
 * Queries the agent: Locator hello, RunControl context tree (getChildren /
 * getContext / getState), Registers (getChildren / getContext for PC..),
 * Memory read at the current PC, and prints everything.
 */
'use strict';

const net = require('net');

const host = process.argv[2] || '127.0.0.1';
const port = parseInt(process.argv[3] || '1534', 10);

const EOM = Buffer.from([3, 1]);

function toBuffer(words, args) {
    const parts = [];
    parts.push(Buffer.from(words.join('\0') + '\0', 'utf8'));
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
        if (buf[i] === 0) {
            out.push(buf.subarray(start, i));
            start = i + 1;
        }
    }
    if (start < buf.length) out.push(buf.subarray(start));
    return out;
}

class Client {
    constructor() {
        this.pending = new Map();
        this.token = 0;
        this.buf = Buffer.alloc(0);
        this.sock = null;
        this.events = [];
    }

    connect() {
        return new Promise((resolve, reject) => {
            this.sock = net.connect(port, host, () => resolve());
            this.sock.on('error', reject);
            this.sock.on('data', (d) => this.onData(d));
        });
    }

    onData(d) {
        this.buf = Buffer.concat([this.buf, d]);
        for (;;) {
            const idx = this.buf.indexOf(EOM);
            if (idx < 0) return;
            const packet = this.buf.subarray(0, idx);
            this.buf = this.buf.subarray(idx + 2);
            this.onPacket(packet);
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
            if (p) {
                this.pending.delete(token);
                if (p.raw) {
                    p.resolve(parts.slice(2).map((b) => b.toString()));
                } else if (err && err.length > 0 && err !== 'null') {
                    p.reject(new Error('TCF error: ' + err));
                } else {
                    p.resolve(data);
                }
            } else {
                console.log('[unknown result]', packet.toString());
            }
        } else if (type === 'E') {
            const service = parts[1].toString();
            const event = parts[2].toString();
            const data = parts.slice(3).map((b) => b.toString());
            this.events.push({ service, event, data });
            console.log(`[event] ${service}/${event} ${data.join(' | ')}`);
        } else {
            console.log('[packet]', type, packet.toString());
        }
    }

    command(service, cmd, ...args) {
        return new Promise((resolve, reject) => {
            const token = String(++this.token);
            this.pending.set(token, { resolve, reject });
            this.sock.write(toBuffer(['C', token, service, cmd], args));
        });
    }

    /* For commands whose response has no error field (e.g. Memory get). */
    commandRaw(service, cmd, ...args) {
        return new Promise((resolve, reject) => {
            const token = String(++this.token);
            this.pending.set(token, { resolve, reject, raw: true });
            this.sock.write(toBuffer(['C', token, service, cmd], args));
        });
    }

    sendEvent(service, event, ...args) {
        this.sock.write(toBuffer(['E', service, event], args));
    }
}

function j(data) {
    if (!data || data.length === 0) return null;
    try { return JSON.parse(data[0]); } catch (e) { return data[0]; }
}

(async () => {
    const c = new Client();
    await c.connect();
    console.log(`connected to ${host}:${port}`);

    // 1. Locator hello handshake
    c.sendEvent('Locator', 'Hello', ['Locator']);
    await new Promise((r) => setTimeout(r, 300));
    const hello = c.events.find((e) => e.service === 'Locator' && e.event === 'Hello');
    if (hello) {
        console.log('=== agent services ===');
        console.log(j(hello.data));
    } else {
        console.log('(no Locator/Hello event received)');
    }

    // 2. RunControl context tree
    console.log('=== RunControl getChildren("") ===');
    let roots = j(await c.command('RunControl', 'getChildren', ''));
    console.log(roots);

    for (const id of (roots || [])) {
        const ctx = j(await c.command('RunControl', 'getContext', id));
        console.log(`--- context ${id} ---`);
        console.log(ctx);
        const st = j(await c.command('RunControl', 'getState', id));
        console.log(`state ${id}:`, st);

        // children
        const kids = j(await c.command('RunControl', 'getChildren', id));
        if (kids && kids.length) console.log(`children of ${id}:`, kids);

        // 3. Registers
        const regs = j(await c.command('Registers', 'getChildren', id));
        console.log(`registers of ${id}: ${regs ? regs.length : 0}`);
        let pcId = null;
        if (regs) {
            for (const rid of regs) {
                const rdef = j(await c.command('Registers', 'getContext', rid));
                if (rdef && rdef.Name === 'PC') {
                    pcId = rid;
                    console.log('   PC def:', JSON.stringify(rdef));
                }
            }
            // read a few register values (Registers get -> base64)
            for (const rid of regs.slice(0, 4)) {
                const v = await c.command('Registers', 'get', rid);
                const b64 = j(v);
                const buf = Buffer.from(b64, 'base64');
                console.log(`   ${rid} = 0x${buf.readUInt32LE(0).toString(16).padStart(8, '0')}`);
            }
            if (pcId) {
                const v = await c.command('Registers', 'get', pcId);
                const buf = Buffer.from(j(v), 'base64');
                console.log(`   ${pcId} (PC) = 0x${buf.readUInt32LE(0).toString(16).padStart(8, '0')}`);
            }
        }

        // 4. Memory read at PC
        if (pcId) {
            const v = await c.command('Registers', 'get', pcId);
            const addr = Buffer.from(j(v), 'base64').readUInt32LE(0);
            console.log(`memory get at PC 0x${addr.toString(16)} (16 bytes):`);
            const memResp = await c.commandRaw('Memory', 'get', id, addr, 1, 16, 0);
            const b64 = (memResp && memResp[0]) || '';
            console.log('  ', Buffer.from(b64, 'base64').toString('hex'));
        }

        // 5. Suspend (halts the core and intercepts it)
        console.log(`suspend ${id} ...`);
        await c.command('RunControl', 'suspend', id);
        await new Promise((r) => setTimeout(r, 500));
        const st2 = j(await c.command('RunControl', 'getState', id));
        console.log(`state after suspend ${id}:`, st2);
        if (pcId) {
            const v2 = await c.command('Registers', 'get', pcId);
            const pc2 = Buffer.from(j(v2), 'base64').readUInt32LE(0);
            console.log(`   PC after suspend = 0x${pc2.toString(16).padStart(8, '0')}`);
            // unaligned read (6 bytes at PC+2): alignment handling check
            const memResp2 = await c.commandRaw('Memory', 'get', id, pc2 + 2, 1, 6, 0);
            const b64b = (memResp2 && memResp2[0]) || '';
            console.log(`   unaligned read @0x${(pc2 + 2).toString(16)}:`, Buffer.from(b64b, 'base64').toString('hex'));
        }
    }

    console.log('=== done ===');
    c.sock.end();
    process.exit(0);
})().catch((e) => {
    console.error('FAILED:', e.message);
    process.exit(1);
});
