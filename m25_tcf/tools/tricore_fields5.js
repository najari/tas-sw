// final probe: a8-a15 16-bit [a] variants + mov const4 variants
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
function one(c) {
    const o = assemble('\t' + c + '\n');
    if (!o || !o[0]) { console.log(`${c.padEnd(30)} INVALID`); return; }
    const r = o[0];
    const b = r.bytes;
    const v = (b[0] | (b[1] << 8) | ((b[2] || 0) << 16) | ((b[3] || 0) << 24)) >>> 0;
    if (b.length === 2)
        console.log(`${c.padEnd(30)} h=0x${hex(v, 4)} op=${hex(v & 0xFF, 2)} [11:8]=${((v >> 8) & 0xF).toString(16)} [15:12]=${((v >> 12) & 0xF).toString(16)} | ${r.text}`);
    else
        console.log(`${c.padEnd(30)} v=0x${hex(v)} op=${hex(v & 0xFF, 2)} [11:8]=${((v >> 8) & 0xF).toString(16)} [15:12]=${((v >> 12) & 0xF).toString(16)} [19:16]=${((v >> 16) & 0xF).toString(16)} [23:20]=${((v >> 20) & 0xF).toString(16)} [27:24]=${((v >> 24) & 0xF).toString(16)} [31:28]=${((v >> 28) & 0xF).toString(16)} | ${r.text}`);
}
console.log('=== a8-a15 variants of 16-bit [a] loads/stores ===');
for (const a of ['%a15', '%a14', '%a8', '%a7', '%a1', '%a0']) {
    one(`ld.w %d2, [${a}]`);
    one(`st.w [${a}], %d2`);
    one(`ld.h %d2, [${a}]`);
    one(`st.h [${a}], %d2`);
    one(`ld.bu %d2, [${a}]`);
    one(`st.b [${a}], %d2`);
    one(`ld.w %d2, [${a}+]`);
}
console.log('=== mov const4 variants ===');
for (const c of [0, 1, 7, 8, 9, 12, 14, 15]) one(`mov %d15, ${c}`);
for (const c of [0, 8, 15]) one(`mov %d2, ${c}`);
console.log('=== sh/sha/misc const variants ===');
for (const c of [0, 8, 15]) one(`sh %d2, ${c}`);
for (const c of [0, 8, 15]) one(`sha %d2, ${c}`);
for (const c of [0, 7, 8, 15]) one(`add %d2, ${c}`);
for (const c of [0, 7, 8, 15]) one(`sub.a %sp, ${c}`);
