// M25-5: verify ISA name case-insensitivity in the Disassembly service.
// The Infineon VS Code extension sends ISA:'TRICORE' (uppercase); the agent registers "TriCore".
// This script mirrors the extension's exact request and reports decoded vs fallback (.byte/.half/.word) counts.
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

(async () => {
    await sleep(300);
    const base = Number(process.argv[2] || 0x8003a300);
    const size = Number(process.argv[3] || 64);
    const isa = process.argv[4] || 'TRICORE';
    const params = { ISA: isa, Simplified: true, PseudoInstructions: true, OpcodeValue: true };
    const r = await cmd('Disassembly', 'disassemble', 'core0', base, size, params);
    const json = r.slice(2).find((x) => x.startsWith('['));
    if (!json) { console.log('no json:', JSON.stringify(r.slice(0, 6))); process.exit(1); }
    const arr = JSON.parse(json);
    let decoded = 0, fallback = 0;
    for (const line of arr) {
        const text = (line.Instruction || []).map((p) => p.Text || '').join('');
        const addr = '0x' + (line.Address >>> 0).toString(16);
        if (/^\.(byte|half|word)/.test(text.trim())) fallback++; else decoded++;
        console.log(`${addr}  ${text}`);
    }
    console.log(`\n=== ISA='${isa}': decoded=${decoded} fallback=${fallback} ===`);
    process.exit(0);
})();
