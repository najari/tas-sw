// M25-3 probe: ELF symbols + line numbers (memory map -> ELF path)
'use strict';
const net = require('net');
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
        if (t === 'R') {
            const pr = pending.get(p[1].toString());
            if (!pr) continue;
            pending.delete(p[1].toString());
            const err = p[2] ? p[2].toString() : '';
            if (pr.raw) pr.resolve(p.slice(2).map((x) => x.toString()));
            else if (err && err !== 'null') pr.reject(new Error('TCF error: ' + err));
            else pr.resolve(p.slice(3).map((x) => x.toString()));
        } else if (t === 'E' && p[1].toString() !== 'Locator') {
            console.log(`[event] ${p[1]}/${p[2]} ${p.slice(3).map((x) => x.toString()).join(' | ').slice(0, 200)}`);
        }
    }
});
const cmd = (service, c, ...args) => new Promise((res, rej) => { const t = String(++token); pending.set(t, { resolve: res, reject: rej }); sock.write(tb(['C', t, service, c], args)); });
const j = (d) => (d && d.length ? JSON.parse(d[0]) : null);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
    await sleep(400);
    const core = 'core0';
    const elf = (process.argv[2] || 'C:/work/aurix_tcf_test/test.elf');

    console.log(`=== MemoryMap set ${core} -> ${elf} ===`);
    const r = await cmd('MemoryMap', 'set', core, [{ FileName: elf, ID: core }]);
    console.log('set result:', JSON.stringify(r));

    // read the PC
    const st = await cmd('RunControl', 'getState', core);
    const suspended = st.length ? st[0] === 'true' : false;
    let pc = st.length > 1 ? (Number(st[1]) >>> 0) : 0;
    console.log(`suspended=${suspended} pc=0x${pc.toString(16)}`);

    // query an address inside the test ELF (add @ 0x80000010)
    const addr = process.argv[3] ? (Number(process.argv[3]) >>> 0) : 0x80000010;
    console.log(`=== Symbols findByAddr 0x${addr.toString(16)} ===`);
    const sym = j(await cmd('Symbols', 'findByAddr', core, addr));
    console.log('symbol id:', sym);
    if (sym) {
        const sc = j(await cmd('Symbols', 'getContext', sym));
        console.log('symbol:', JSON.stringify(sc));
    }

    console.log('=== LineNumbers mapToSource ===');
    const lines = j(await cmd('LineNumbers', 'mapToSource', core, addr, addr + 2));
    console.log('code area:', JSON.stringify(lines));

    console.log('=== StackTrace getChildren + getContext ===');
    const frames = j(await cmd('StackTrace', 'getChildren', core));
    console.log('frames:', frames);
    if (frames && frames.length) {
        const fc = j(await cmd('StackTrace', 'getContext', frames));
        console.log('frame data:', JSON.stringify(fc));
    }

    console.log('=== done ===');
    process.exit(0);
})().catch((e) => { console.error('FAILED', e.message); process.exit(1); });
