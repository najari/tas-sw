// derive field layouts for more TriCore instruction families
'use strict';
const { execFileSync } = require('child_process');
const fs = require('fs');
const AS = 'C:/Infineon/AURIX-Configuration-Studio-1.0.24/eclipse/tricore-gcc11/bin/tricore-elf-as.exe';
const OD = 'C:/Infineon/AURIX-Configuration-Studio-1.0.24/eclipse/tricore-gcc11/bin/tricore-elf-objdump.exe';

function assemble(lines) {
    const src = '\t.section .text\n\t.global _start\n_start:\n' + lines.map((l) => '\t' + l + '\n').join('');
    fs.writeFileSync('C:/work/aurix_tcf_test/_probe.s', src);
    try { execFileSync(AS, ['C:/work/aurix_tcf_test/_probe.s', '-o', 'C:/work/aurix_tcf_test/_probe.o'], { stdio: 'pipe' }); }
    catch (e) { console.log('AS FAILED:', String(e.stdout || '').split('\n').slice(0, 3).join(' / ')); return []; }
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

function show(title, cases) {
    console.log('===== ' + title + ' =====');
    for (const r of assemble(cases)) {
        const b = r.bytes;
        let v = (b[0] | (b[1] << 8) | ((b[2] || 0) << 16) | ((b[3] || 0) << 24)) >>> 0;
        if (b.length === 2) console.log(`${r.text.padEnd(26)} h=0x${hex(v, 4)} [11:8]=${((v >> 8) & 0xF).toString(16)} [15:12]=${((v >> 12) & 0xF).toString(16)}`);
        else console.log(`${r.text.padEnd(26)} v=0x${hex(v)} [15:8]=${hex((v >> 8) & 0xFF, 2)} [19:16]=${((v >> 16) & 0xF).toString(16)} [23:20]=${((v >> 20) & 0xF).toString(16)} [27:24]=${((v >> 24) & 0xF).toString(16)} [31:28]=${((v >> 28) & 0xF).toString(16)}`);
    }
}

show('movh.a', ['movh.a %a2, 0x7001', 'movh.a %a0, 0x1234', 'movh.a %a15, 0x1234', 'movh.a %a5, 0xABCD']);
show('lea', ['lea %a2, [%a2]-20480', 'lea %a0, [%a0]100', 'lea %a15, [%a3]2000', 'lea %a2, [%a14]-4']);
show('ld/st 32-bit', ['ld.w %d2, [%a2]16', 'ld.w %d5, [%a3]1024', 'st.w [%a2]16, %d2', 'st.w [%a3]1024, %d5',
    'ld.b %d2, [%a2]16', 'ld.bu %d2, [%a2]16', 'ld.h %d2, [%a2]16', 'ld.hu %d2, [%a2]16',
    'st.b [%a2]16, %d2', 'st.h [%a2]16, %d2']);
show('RR ops 32-bit', ['add %d2,%d3,%d4', 'add %d5,%d6,%d7', 'sub %d2,%d3,%d4', 'mul %d2,%d3,%d4', 'and %d2,%d3,%d4', 'or %d2,%d3,%d4', 'xor %d2,%d3,%d4', 'sh %d2,%d3,%d4', 'sha %d2,%d3,%d4']);
show('branches 32-bit', ['j 0x100', 'call 0x100', 'jeq %d2,%d3,0x100', 'jne %d2,%d3,0x100', 'jlt %d2,%d3,0x100', 'jz %d2,0x200', 'jnz %d2,0x200', 'ji %a2', 'jl 0x100']);
show('misc 32-bit', ['mov %d2, 0x1234', 'mov %d2, 1000', 'mov.a %a2, 100', 'mov.d %d2, %a2', 'addi %a2, %a3, 100']);
