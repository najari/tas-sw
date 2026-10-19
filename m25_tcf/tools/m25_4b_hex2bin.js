// m25_4b_hex2bin.js - Intel HEX -> binary + base address (for flash algorithm analysis)
'use strict';
const fs = require('fs');
const inFile = process.argv[2];
const outFile = process.argv[3] || 'C:/work/tcf_agent/_flax.bin';
const txt = fs.readFileSync(inFile, 'ascii');
let base = 0, min = Infinity, max = 0;
const chunks = [];
for (const raw of txt.split(/\r?\n/)) {
    const line = raw.trim();
    if (!line || line[0] !== ':') continue;
    const n = parseInt(line.substr(1, 2), 16);
    const addr = parseInt(line.substr(3, 4), 16);
    const type = parseInt(line.substr(7, 2), 16);
    const data = Buffer.from(line.substr(9, n * 2), 'hex');
    if (type === 0) {
        const a = base + addr;
        chunks.push({ addr: a, data });
        if (a < min) min = a;
        if (a + data.length > max) max = a + data.length;
    } else if (type === 4) {
        base = parseInt(line.substr(9, 4), 16) << 16;
    } else if (type === 1) {
        break;
    }
}
const buf = Buffer.alloc(max - min);
let written = 0;
for (const c of chunks) {
    c.data.copy(buf, c.addr - min);
    written += c.data.length;
}
fs.writeFileSync(outFile, buf);
console.log(`base=0x${min.toString(16)} end=0x${max.toString(16)} size=${buf.length} (data bytes=${written}, records=${chunks.length})`);
