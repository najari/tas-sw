// M25-3: reload the PSPR code + reset the PC/SP + remap the ELF + clear old BPs
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
            console.log(`[event] ${p[1]}/${p[2]} ${p.slice(3).join(' | ').slice(0, 140)}`);
        }
    }
});
const cmd = (service, c, ...args) => new Promise((res) => { const t = String(++token); pending.set(t, res); sock.write(tb(['C', t, service, c], args)); });
const b64 = (v) => { const b = Buffer.alloc(4); b.writeUInt32LE(v >>> 0, 0); return b.toString('base64'); };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
    await sleep(300);
    /* 1. clear the old breakpoints */
    const ids = (await cmd('Breakpoints', 'getIDs')).data[0];
    console.log('existing bps:', ids);
    const list = JSON.parse(ids || '[]');
    if (list.length) await cmd('Breakpoints', 'remove', list);
    console.log('old bps removed');

    /* 2. reload the code */
    const code = fs.readFileSync('C:/work/aurix_tcf_test/m25_pspr_text.bin');
    await cmd('Memory', 'set', 'core0', 0x70100000, 1, code.length, 0, code.toString('base64'));
    const rb = (await cmd('Memory', 'get', 'core0', 0x70100000, 1, code.length, 0)).data[0];
    console.log(`code reloaded (${code.length} bytes), readback ${Buffer.from(rb, 'base64').equals(code) ? 'MATCH' : 'MISMATCH'}`);

    /* 3. PC/SP */
    await cmd('Registers', 'set', 'R0.core0', b64(0x7010003A));
    await cmd('Registers', 'set', 'R27.core0', b64(0x7000C000));
    await cmd('Registers', 'set', 'R37.core0', b64(0x0));

    /* 4. remap the ELF */
    await cmd('MemoryMap', 'set', 'core0', [{ FileName: 'C:/work/aurix_tcf_test/m25_pspr.elf', ID: 'core0' }]);
    console.log('memory map set to m25_pspr.elf');
    process.exit(0);
})();
