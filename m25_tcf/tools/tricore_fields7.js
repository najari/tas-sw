// verify remaining encodings against the toolchain
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
    if (!o || !o[0]) { console.log(`${c.padEnd(32)} INVALID`); return; }
    const r = o[0];
    const b = r.bytes;
    const v = (b[0] | (b[1] << 8) | ((b[2] || 0) << 16) | ((b[3] || 0) << 24)) >>> 0;
    if (b.length === 2)
        console.log(`${c.padEnd(32)} h=0x${hex(v, 4)} op=${hex(v & 0xFF, 2)} | ${r.text}`);
    else
        console.log(`${c.padEnd(32)} v=0x${hex(v)} op=${hex(v & 0xFF, 2)} | ${r.text}`);
}
console.log('=== 0x16 and-with-const ===');
one('and %d15, 31'); one('and %d15, 37'); one('and %d5, 37'); one('and %d5, 36'); one('and %d2, 5'); one('and %d0, 0');
console.log('=== extr ===');
one('extr %d2, %d15, 0, 8'); one('extr %d5, %d3, 3, 4'); one('extr %d0, %d0, 0, 1');
console.log('=== ld.a ===');
one('ld.a %a2, [%a15]'); one('ld.a %a5, [%a3]');
console.log('=== jz d15 / jnz d15 ===');
one('jz %d15, l1\n\tnop\nl1:\tnop'); one('jnz %d15, l1\n\tnop\nl1:\tnop');
console.log('=== mov -1 ===');
one('mov %d15, -1'); one('mov %d2, -1'); one('mov %d2, -8'); one('mov %d2, 7');
console.log('=== mov const16 (0x3B) ===');
one('mov %d2, 0x12'); one('mov %d2, 0x1234'); one('mov %d2, 0x120'); one('mov %d5, 0x0abc');
console.log('=== ld.w split-disp (word flag) ===');
one('ld.w %d15, [%a2]72'); one('ld.w %d5, [%a2]18'); one('ld.w %d5, [%a2]16'); one('st.w [%a2]72, %d15');
