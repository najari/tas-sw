// M25-3: run from PSPR with a breakpoint a few instructions ahead
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
        const t = p[0];
        if (t === 'R') {
            const pr = pending.get(p[1]);
            if (!pr) continue;
            pending.delete(p[1]);
            pr({ err: p[2], data: p.slice(3).map((x) => x.replace(/"/g, '')) });
        } else if (t === 'E' && p[1] !== 'Locator') {
            console.log(`[event] ${p[1]}/${p[2]} ${p.slice(3).join(' | ').slice(0, 160)}`);
        }
    }
});
const cmd = (service, c, ...args) => new Promise((res) => { const t = String(++token); pending.set(t, res); sock.write(tb(['C', t, service, c], args)); });
const b64 = (v) => { const b = Buffer.alloc(4); b.writeUInt32LE(v >>> 0, 0); return b.toString('base64'); };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
    await sleep(300);
    /* set PC = 0x70100000 */
    await cmd('Registers', 'set', 'R0.core0', b64(0x70100000));
    await sleep(300);
    /* set a HW breakpoint at 0x70100004 (a few instructions ahead) */
    const r = await cmd('Breakpoints', 'set', [{
        ID: 'psprbp', Enabled: true, BreakpointType: 'Hardware',
        Location: '0x70100004', AccessMode: 4, ContextIds: ['core0']
    }]);
    console.log('bp set:', r.err || 'ok');
    await sleep(500);
    /* resume */
    const st0 = await cmd('RunControl', 'getState', 'core0');
    console.log('state before resume:', st0.data.join('|'));
    if (st0.data[0] !== 'true') {
        await cmd('RunControl', 'suspend', 'core0');
        await sleep(400);
    }
    await cmd('RunControl', 'resume', 'core0', 0, 1);
    /* wait for the hit */
    for (let i = 0; i < 40; i++) {
        await sleep(250);
        const st = await cmd('RunControl', 'getState', 'core0');
        if (st.data[0] === 'true') {
            console.log(`HIT: pc=0x${(Number(st.data[1]) >>> 0).toString(16)} reason=${st.data[2]}`);
            break;
        }
    }
    const pc = await cmd('Registers', 'get', 'R0.core0');
    console.log('PC now: 0x' + Buffer.from(pc.data[0], 'base64').readUInt32LE(0).toString(16));
    process.exit(0);
})();
