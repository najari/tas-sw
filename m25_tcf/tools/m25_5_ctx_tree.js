// M25-5: enumerate context tree (Processes.getChildren) to find symbol contexts
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
        if (p[0] === 'R') { const pr = pending.get(p[1]); if (pr) { pending.delete(p[1]); pr(p); } }
    }
});
const cmd = (service, c, ...args) => new Promise((res) => { const t = String(++token); pending.set(t, res); sock.write(tb(['C', t, service, c], args)); });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const show = (tag, r) => { const json = r.slice(2).find((x) => x.startsWith('[')); console.log(`--- ${tag} ---`); console.log(json ? json.slice(0, 1200) : JSON.stringify(r.slice(2, 6))); };

(async () => {
    await sleep(300);
    show('root children', await cmd('Processes', 'getChildren', ''));
    show('core0 children', await cmd('Processes', 'getChildren', 'core0'));
    process.exit(0);
})();
