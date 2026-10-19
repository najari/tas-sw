// M25-3: query the StackTrace service (CSA crawl) and print the frames
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
const hex = (v) => (v >>> 0).toString(16).padStart(8, '0');

(async () => {
    await sleep(300);
    const nf = Number(process.argv[2] || 8);
    for (let f = 0; f < nf; f++) {
        const r = await cmd('StackTrace', 'getContext', [`FP${f}.core0`]);
        const json = r.slice(2).find((x) => x.startsWith('['));
        if (!json) { console.log(`FP${f}: (no data) raw=${JSON.stringify(r.slice(2, 5)).slice(0, 120)}`); continue; }
        const arr = JSON.parse(json);
        if (!arr[0]) { console.log(`FP${f}: null (end of stack)`); continue; }
        const d = arr[0];
        const ip = d.IP !== undefined ? '0x' + hex(d.IP) : '(no ip)';
        const rp = d.RP !== undefined ? '0x' + hex(d.RP) : '';
        console.log(`FP${f}: IP=${ip} RP=${rp} Walk=${d.Walk === true} Index=${d.Index} ${d.Level !== undefined ? 'Level=' + d.Level : ''} Top=${d.TopFrame === true}`);
        if (d.Registers) {
            const names = Object.keys(d.Registers);
            const show = ['A10', 'A11', 'A14', 'PCXI', 'PC'].filter((n) => names.includes(n));
            console.log('     ' + show.map((n) => `${n}=0x${hex(d.Registers[n])}`).join(' '));
        }
    }
    process.exit(0);
})();
