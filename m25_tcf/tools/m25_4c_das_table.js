// m25_4c_das_table.js - dump the DAS API function table from das_api.dll (VA 0x180013000)
'use strict';
const fs = require('fs');
const buf = fs.readFileSync('C:/WINDOWS/system32/das_api.dll');
/* PE parse */
const peOff = buf.readUInt32LE(0x3C);
const numSec = buf.readUInt16LE(peOff + 6);
const optSize = buf.readUInt16LE(peOff + 20);
const secTab = peOff + 24 + optSize;
const imageBase = Number(buf.readBigUInt64LE(peOff + 24 + 24));   // OptionalHeader.ImageBase
console.log(`imageBase=0x${imageBase.toString(16)} sections=${numSec}`);
const target = 0x180013000;
let fileOff = -1;
for (let i = 0; i < numSec; i++) {
    const s = secTab + i * 40;
    const name = buf.subarray(s, s + 8).toString('ascii').replace(/\0.*$/, '');
    const vsize = buf.readUInt32LE(s + 8);
    const vaddr = buf.readUInt32LE(s + 12);
    const raw = buf.readUInt32LE(s + 20);
    const rva = target - imageBase;
    if (rva >= vaddr && rva < vaddr + vsize) {
        fileOff = raw + (rva - vaddr);
        console.log(`section ${name}: VA 0x${(imageBase + vaddr).toString(16)} size 0x${vsize.toString(16)} -> fileOff 0x${fileOff.toString(16)}`);
    }
}
if (fileOff < 0) { console.log('table VA not found'); process.exit(1); }
console.log('--- API table entries (function pointers) ---');
let n = 0;
for (let i = 0; i < 256; i++) {
    const p = Number(buf.readBigUInt64LE(fileOff + i * 8));
    if (p === 0) { console.log(`[${i}] 0  <- table end`); break; }
    n++;
    console.log(`[${i}] 0x${p.toString(16)}`);
    if (n >= 60) { console.log('... (truncated at 60)'); break; }
}
console.log(`total non-null entries: ${n}`);
