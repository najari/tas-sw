// derive the TriCore instruction field layouts by assembling variations (debug)
'use strict';
const { execFileSync } = require('child_process');
const fs = require('fs');

const AS = 'C:/Infineon/AURIX-Configuration-Studio-1.0.24/eclipse/tricore-gcc11/bin/tricore-elf-as.exe';
const OD = 'C:/Infineon/AURIX-Configuration-Studio-1.0.24/eclipse/tricore-gcc11/bin/tricore-elf-objdump.exe';

function assemble(lines) {
    const src = '\t.section .text\n\t.global _start\n_start:\n' + lines.map((l) => '\t' + l + '\n').join('');
    fs.writeFileSync('C:/work/aurix_tcf_test/_probe.s', src);
    try {
        execFileSync(AS, ['C:/work/aurix_tcf_test/_probe.s', '-o', 'C:/work/aurix_tcf_test/_probe.o'], { stdio: 'pipe' });
    } catch (e) {
        console.log('ASSEMBLER FAILED:', String(e.stdout || '').slice(0, 500));
        return [];
    }
    const out = execFileSync(OD, ['-d', 'C:/work/aurix_tcf_test/_probe.o'], { encoding: 'utf8' });
    const res = [];
    for (const line of out.split('\n')) {
        const m = line.match(/^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2}\s+)+)([^\r\n]*)/);
        if (!m) continue;
        const bytes = m[2].trim().split(/\s+/).map((x) => parseInt(x, 16));
        res.push({ addr: parseInt(m[1], 16), bytes, text: m[3].trim() });
    }
    return res;
}

function le32(bytes) { return (bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24)) >>> 0; }

const cases = [];
for (const c of [0, 1, 2, 4, 8, 16, 32, 64, 128, 256, 511]) cases.push(`addi %d2, %d3, ${c}`);
for (let d = 0; d < 16; d += 5) cases.push(`addi %d${d}, %d3, 100`);
for (let s = 0; s < 16; s += 5) cases.push(`addi %d2, %d${s}, 100`);

const out = assemble(cases);
console.log('parsed', out.length, 'instructions');
for (const r of out) {
    const v = le32(r.bytes);
    console.log(`${r.text.padEnd(22)} v=0x${v.toString(16).padStart(8, '0')} [15:8]=${((v >> 8) & 0xFF).toString(16).padStart(2, '0')} [19:16]=${((v >> 16) & 0xF).toString(16)} [23:20]=${((v >> 20) & 0xF).toString(16)} [27:24]=${((v >> 24) & 0xF).toString(16)} [31:28]=${((v >> 28) & 0xF).toString(16)}`);
}
