// derive the absolute LEA (0xC5) layout
'use strict';
const { execFileSync } = require('child_process');
const fs = require('fs');
const AS = 'C:/Infineon/AURIX-Configuration-Studio-1.0.24/eclipse/tricore-gcc11/bin/tricore-elf-as.exe';
const OD = 'C:/Infineon/AURIX-Configuration-Studio-1.0.24/eclipse/tricore-gcc11/bin/tricore-elf-objdump.exe';
function assemble(srcBody) {
    const src = '\t.section .text\n\t.global _start\n_start:\n' + srcBody;
    fs.writeFileSync('C:/work/aurix_tcf_test/_probe.s', src);
    try { execFileSync(AS, ['C:/work/aurix_tcf_test/_probe.s', '-o', 'C:/work/aurix_tcf_test/_probe.o'], { stdio: 'pipe' }); }
    catch (e) { return null; }
    const out = execFileSync(OD, ['-d', 'C:/work/aurix_tcf_test/_probe.o'], { encoding: 'utf8' });
    const res = [];
    for (const line of out.split('\n')) {
        const m = line.match(/^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2}\s+)+)([^\r\n]*)/);
        if (!m) continue;
        res.push({ addr: parseInt(m[1], 16), bytes: m[2].trim().split(/\s+/).map((x) => parseInt(x, 16)), text: m[3].trim() });
    }
    return res;
}
const hex = (v, n = 8) => v.toString(16).padStart(n, '0');
let body = '';
const vals = [0x1, 0x2, 0x4, 0x8, 0x10, 0x20, 0x40, 0x80, 0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000, 0x4000, 0x8000];
for (const x of vals) body += `\tlea %a2, ${x}\n`;
const out = assemble(body);
if (!out) { console.log('AS FAILED'); process.exit(1); }
for (const r of out) {
    const b = r.bytes;
    const v = (b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24)) >>> 0;
    console.log(`${r.text.padEnd(24)} v=0x${hex(v)} [19:16]=${((v >> 16) & 0xF).toString(16)} [23:20]=${((v >> 20) & 0xF).toString(16)} [27:24]=${((v >> 24) & 0xF).toString(16)} [31:28]=${((v >> 28) & 0xF).toString(16)}`);
}
