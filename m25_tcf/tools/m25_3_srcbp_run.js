// set a source breakpoint (File/Line - same format as the extension) and run to it
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
            pr({ err: p[2], data: p.slice(3).map((x) => x.replace(/"/g, '')) });
        } else if (p[0] === 'E' && p[1] !== 'Locator') {
            console.log(`[event] ${p[1]}/${p[2]} ${p.slice(3).join(' | ').slice(0, 200)}`);
        }
    }
});
const cmd = (service, c, ...args) => new Promise((res) => { const t = String(++token); pending.set(t, res); sock.write(tb(['C', t, service, c], args)); });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
    await sleep(300);
    const r = await cmd('Breakpoints', 'set', [{
        ID: 'm25bp', Enabled: true, BreakpointType: 'Hardware',
        File: 'm25_elf_test.c', Line: 5, ContextIds: ['core0']
    }]);
    console.log('set:', r.err || 'ok');
    await sleep(800);
    const st = await cmd('Breakpoints', 'getStatus', 'm25bp');
    console.log('status:', st.err || '', st.data.join(' '));

    /* resume if suspended */
    const s0 = await cmd('RunControl', 'getState', 'core0');
    if (s0.data[0] === 'true') {
        console.log('resuming...');
        await cmd('RunControl', 'resume', 'core0', 0, 1);
    }
    for (let i = 0; i < 40; i++) {
        await sleep(250);
        const s = await cmd('RunControl', 'getState', 'core0');
        if (s.data[0] === 'true') {
            console.log(`HIT: pc=0x${(Number(s.data[1]) >>> 0).toString(16)} reason=${s.data[2]}`);
            break;
        }
    }
    process.exit(0);
})();
