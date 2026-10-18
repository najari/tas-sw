// final: 0x09 family (ld.b/bu/h/hu) + 0x19/0x59 modes + unknown 16-bit ops
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

console.log('=== 0x09 family: type & offset ===');
for (const ins of ['ld.b', 'ld.bu', 'ld.h', 'ld.hu']) {
    for (const addr of ['[%a2]', '[%a2]16', '[%a2]64']) {
        one(`${ins} %d5, ${addr}`);
    }
}
console.log('=== 0x09 with post-inc? ===');
for (const ins of ['ld.b', 'ld.bu', 'ld.h', 'ld.hu']) {
    for (const addr of ['[%a2+]', '[%a2+]16', '[%a2]-16', '[%a2-]']) {
        one(`${ins} %d5, ${addr}`);
    }
}
console.log('=== 0x19/0x59 modes ===');
for (const ins of ['ld.w', 'st.w']) {
    for (const addr of ['[%a2]', '[%a2]16', '[%a2]-16', '[%a2+]', '[%a2+]-16']) {
        if (ins === 'ld.w') one(`${ins} %d5, ${addr}`); else one(`${ins} ${addr}, %d5`);
    }
}
console.log('=== unknown 16-bit candidates ===');
for (const c of ['st.t [%a15], 3', 'st.t [%a15], %d2', 'ld.b %d2, [%a15+]', 'swap.w %d2, %d3',
    'eq %d2, %d3', 'ne %d2, %d3', 'lt %d2, %d3', 'ge %d2, %d3',
    'eq %d2, 3', 'ne %d2, 3', 'lt %d2, 3', 'ge %d2, 3',
    'mov %d15, 15', 'mov %d15, 0', 'sh %d2, 15', 'sha %d2, 15',
    'add %d2, %d3', 'sub.a %sp, 8', 'ret', 'rfe', 'nop']) one(c);
console.log('=== candidates for D4/16/6E/6C ===');
for (const c of ['bisr 3', 'bisy', 'mfcr %d2, 0xFE08', 'mtcr 0xFE08, %d2',
    'ldmst [%a2], %d3', 'svlcx', 'rslcx', 'stlcx [%a2]', 'ldlcx [%a2]',
    'cachea.w [%a2]', 'crc32 %d2, %d3, %d4', 'dvinit %d2, %d3', 'dvstep %d2, %d3',
    'unpack %d2, %d3, %d4', 'pack %d2, %d3, %d4', 'min %d2, %d3, %d4', 'max %d2, %d3, %d4',
    'abs %d2, %d3', 'parity %d2', 'xnor %d2', 'not %d2']) one(c);
