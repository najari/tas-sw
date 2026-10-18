// M25-3: compare our disassembler against the toolchain reference (objdump)
'use strict';
const net = require('net');
const fs = require('fs');
const { execFileSync } = require('child_process');

const OD = 'C:/Infineon/AURIX-Configuration-Studio-1.0.24/eclipse/tricore-gcc11/bin/tricore-elf-objdump.exe';
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
            pr({ raw: p, err: p[2], data: p.slice(3) });
        }
    }
});
const cmd = (service, c, ...args) => new Promise((res) => { const t = String(++token); pending.set(t, res); sock.write(tb(['C', t, service, c], args)); });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
    await sleep(300);
    const base = Number(process.argv[2] || 0x8003A140) >>> 0;
    const size = Number(process.argv[3] || 64);

    const mem = await cmd('Memory', 'get', 'core0', base, 1, size, 0);
    const b64raw = mem.raw.slice(2).find((x) => x.replace(/"/g, '').length > 8 && /^"?[A-Za-z0-9+/=]+"?$/.test(x)) || '';
    const bytes = Buffer.from(b64raw.replace(/"/g, ''), 'base64');
    console.log('bytes read:', bytes.length);

    // reference
    fs.writeFileSync('C:/work/tcf_agent/_ref.bin', bytes);
    const refOut = execFileSync(OD, ['-D', '-b', 'binary', '-m', 'tricore', '--adjust-vma=0x' + base.toString(16), 'C:/work/tcf_agent/_ref.bin'], { encoding: 'utf8' });
    const refLines = [];
    for (const line of refOut.split('\n')) {
        const m = line.match(/^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2}\s+)+)([^\r\n]*)/);
        if (m) refLines.push({ addr: m[1], text: m[3].trim() });
    }

    // ours
    const dis = await cmd('Disassembly', 'disassemble', 'core0', base, size, {});
    const disJson = dis.raw.slice(2).filter((x) => x.indexOf('Address') >= 0 || x.indexOf('Instruction') >= 0 || x === '[' || x === ']').join('');
    if (disJson.length < 3) { console.log('disassemble raw:', JSON.stringify(dis.raw.slice(0, 4))); process.exit(1); }
    const json = JSON.parse(disJson);
    const ours = [];
    for (const item of json) {
        let t = '';
        for (const i of (item.Instruction || [])) {
            const mt = (i.Text || '').match(/^"?(.*?)"?$/);
            t += (mt ? mt[1] : '');
        }
        ours.push({ addr: item.Address.toString(16), size: item.Size, text: t });
    }

    console.log('\n--- reference (objdump) ---');
    for (const r of refLines) console.log(`${r.addr}: ${r.text}`);
    console.log('\n--- ours (TCF Disassembly) ---');
    for (const o of ours) console.log(`${o.addr}: ${o.text}`);

    // compare
    console.log('\n--- comparison ---');
    const norm = (s) => s.replace(/<[^>]*>/g, '').replace(/\s+/g, '').toLowerCase()
        .replace(/\.hword/g, '.half')
        .replace(/-?0x[0-9a-f]+|-?\d+/g, (m) => {
            const v = /^0x/.test(m.replace('-', '')) ? parseInt(m.replace('-', ''), 16) : parseInt(m, 10);
            return (m.startsWith('-') ? '-' : '') + v.toString(10);
        });
    let n = Math.min(refLines.length, ours.length), same = 0, checked = 0;
    const refByAddr = new Map();
    for (const r of refLines) refByAddr.set(r.addr, r.text);
    for (const o of ours) {
        const rt = refByAddr.get(o.addr);
        if (rt === undefined) continue;
        checked++;
        if (norm(rt) === norm(o.text)) same++;
        else console.log(`DIFF @${o.addr}:\n  ref : ${rt}\n  ours: ${o.text}`);
    }
    console.log(`match ${same}/${checked} (address-aligned; ref lines ${refLines.length}, ours ${ours.length})`);
    process.exit(0);
})();
