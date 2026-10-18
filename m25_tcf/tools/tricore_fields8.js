// final: disp4 sign, extr width, 0x78 family, loop
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
    if (!o || !o[0]) { console.log(`${c.padEnd(34)} INVALID`); return; }
    const r = o[0];
    const b = r.bytes;
    const v = (b[0] | (b[1] << 8) | ((b[2] || 0) << 16) | ((b[3] || 0) << 24)) >>> 0;
    if (b.length === 2)
        console.log(`${c.padEnd(34)} h=0x${hex(v, 4)} | ${r.text}`);
    else
        console.log(`${c.padEnd(34)} v=0x${hex(v)} | ${r.text}`);
}
console.log('=== disp4 signedness: backward jz/jnz ===');
console.log(JSON.stringify(assemble('\tl1: nop\n\tnop\n\tjz %d2, l1\n\tjnz %d3, l1\n\tjz %d15, l1\n')));
console.log('=== forward jz with disp 10 ===');
console.log(JSON.stringify(assemble('\tjz %d2, l2\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\nl2:\tnop\n')));
console.log('=== extr width field ===');
one('extr %d2, %d15, 0, 8'); one('extr %d5, %d3, 3, 4'); one('extr %d0, %d0, 0, 1'); one('extr %d2, %d3, 5, 12');
console.log('=== 0x78 family (st.w/ld.w with base) ===');
one('st.w [%sp]4, %d15'); one('st.w [%a2]4, %d5'); one('st.w [%a15]4, %d5'); one('ld.w %d5, [%sp]4'); one('ld.w %d5, [%a2]8');
one('st.w [%a2]4, %d15'); one('ld.w %d15, [%a2]4');
console.log('=== loop / lea absolute ===');
one('loop %a2, l1\nl1:\tnop'); one('loop %a11, l1\nl1:\tnop'); one('lea %a2, 0x63'); one('lea %a2, 0x1000');
