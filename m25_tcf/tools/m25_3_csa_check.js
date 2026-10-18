// check the CSA (context save area) state: saved vs current FCX
'use strict';
const fs = require('fs');
const net = require('net');
const EOM = Buffer.from([3, 1]);
function tb(w, a) {
    const p = [Buffer.from(w.join('\0') + '\0', 'utf8')];
    for (const x of (a || [])) { p.push(Buffer.from(JSON.stringify(x), 'utf8')); p.push(Buffer.from([0])); }
    p.push(EOM);
    return Buffer.concat(p);
}
const dec = (v) => ((v & 0xFFFF) << 6 | (v & 0xF0000) << 12) >>> 0;
const state = JSON.parse(fs.readFileSync('m25_3_ram_state.json', 'utf8'));
const rd = (i) => Buffer.from(state.regs[i], 'base64').readUInt32LE(0);

console.log('saved PCXI: 0x' + rd(34).toString(16), '-> CSA 0x' + dec(rd(34)).toString(16));
console.log('saved FCX : 0x' + rd(35).toString(16), '-> CSA 0x' + dec(rd(35)).toString(16));
console.log('saved ISP : 0x' + rd(36).toString(16));
console.log('saved ICR : 0x' + rd(37).toString(16));
console.log('saved BIV : 0x' + rd(38).toString(16));
console.log('saved BTV : 0x' + rd(39).toString(16));

const pending = new Map(); let token = 0; let buf = Buffer.alloc(0);
const sock = net.connect(1534, '127.0.0.1', () => sock.write(tb(['E', 'Locator', 'Hello', JSON.stringify(['Locator'])])));
sock.on('data', (d) => {
    buf = Buffer.concat([buf, d]);
    for (;;) {
        const i = buf.indexOf(EOM);
        if (i < 0) return;
        const p = buf.subarray(0, i).toString().split('\0');
        buf = buf.subarray(i + 2);
        if (p[0] !== 'R') continue;
        const pr = pending.get(p[1]);
        if (!pr) continue;
        pending.delete(p[1]);
        pr(p.slice(3).map((x) => x.replace(/"/g, '')));
    }
});
const cmd = (service, c, ...args) => new Promise((res) => { const t = String(++token); pending.set(t, res); sock.write(tb(['C', t, service, c], args)); });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
    await sleep(300);
    for (const [name, idx] of [['FCX', 35], ['PCXI', 34], ['ISP', 36], ['ICR', 37]]) {
        const r = await cmd('Registers', 'get', `R${idx}.core0`);
        const v = Buffer.from(r[0], 'base64').readUInt32LE(0);
        console.log(`current ${name}: 0x${v.toString(16)}` + (name === 'FCX' || name === 'PCXI' ? ` -> CSA 0x${dec(v).toString(16)}` : ''));
    }
    process.exit(0);
})();
