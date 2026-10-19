// M25-3: CSA stack crawl probe v2 - correct register read format + PCXI decode + CSA dump
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
            pr(p.slice(3).map((x) => x.replace(/"/g, '')));
        }
    }
});
const cmd = (service, c, ...args) => new Promise((res) => { const t = String(++token); pending.set(t, res); sock.write(tb(['C', t, service, c], args)); });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const hex = (v, n = 8) => (v >>> 0).toString(16).padStart(n, '0');

// PCXI/FCX -> CSA address (bits [15:0] = addr[21:6], bits [19:16] = addr[31:28])
const dec = (v) => (((v & 0xFFFF) << 6) | ((v & 0xF0000) << 12)) >>> 0;

const REGS = { PC: 0, D8: 9, D9: 10, D10: 11, D11: 12, D12: 13, D13: 14, D14: 15, D15: 16,
    A10: 27, A11: 28, A12: 29, A13: 30, A14: 31, A15: 32, PSW: 33, PCXI: 34, FCX: 35, ISP: 36 };

(async () => {
    await sleep(300);
    const vals = {};
    for (const [n, id] of Object.entries(REGS)) {
        const r = await cmd('Registers', 'get', `R${id}.core0`);
        if (!r[0]) { vals[n] = 0; continue; }
        vals[n] = Buffer.from(r[0], 'base64').readUInt32LE(0);
    }
    console.log('=== registers ===');
    for (const n of Object.keys(REGS)) console.log(`${n.padEnd(5)} 0x${hex(vals[n])}`);
    const csa = dec(vals.PCXI), fcx = dec(vals.FCX);
    console.log(`\nPCXI 0x${hex(vals.PCXI)} -> CSA 0x${hex(csa)}`);
    console.log(`FCX  0x${hex(vals.FCX)} -> CSA 0x${hex(fcx)}`);

    // dump the CSA chain: start at csa, follow the saved PCXI inside each block
    // (read via the bus alias 0x70000000 + offset, since local SPR reads may be rejected)
    let addr = csa;
    for (let depth = 0; depth < 6 && addr !== 0; depth++) {
        const bus = 0x70000000 + (addr - 0xD0000000);
        let m = await cmd('Memory', 'get', 'core0', bus, 1, 64, 0);
        let b64 = m.find((x) => x.replace(/"/g, '').length > 8 && /^"?[A-Za-z0-9+/=]+"?$/.test(x)) || '';
        let bytes = Buffer.from(b64.replace(/"/g, ''), 'base64');
        if (bytes.length < 64) {
            m = await cmd('Memory', 'get', 'core0', addr, 1, 64, 0);
            b64 = m.find((x) => x.replace(/"/g, '').length > 8 && /^"?[A-Za-z0-9+/=]+"?$/.test(x)) || '';
            bytes = Buffer.from(b64.replace(/"/g, ''), 'base64');
        }
        if (bytes.length < 64) { console.log(`\nCSA[${depth}] 0x${hex(addr)} (bus 0x${hex(bus)}): (read failed, len=${bytes.length})`); break; }
        const w = [];
        for (let k = 0; k < 16; k++) w.push(bytes.readUInt32LE(k * 4));
        console.log(`\nCSA[${depth}] @0x${hex(addr)}:`);
        for (let k = 0; k < 16; k += 4)
            console.log('   +' + (k * 4).toString(16).padStart(2, '0') + ': ' + w.slice(k, k + 4).map((x) => '0x' + hex(x)).join(' '));
        const nextPcxi = w[0];
        console.log(`   [0]PCXI=0x${hex(nextPcxi)} -> next 0x${hex(dec(nextPcxi))}`);
        console.log(`   [1]PSW=0x${hex(w[1])} [2]A10=0x${hex(w[2])} [3]A11=0x${hex(w[3])} [12]PC=0x${hex(w[12])}`);
        addr = dec(nextPcxi);
        if (nextPcxi === 0) break;
    }
    process.exit(0);
})();
