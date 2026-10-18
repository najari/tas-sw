// fix m25_3_ram_state.json: restore the true input-state values
//  R0 (PC) = 0x8003A336, R27 (A10/SP) = 0x70009000, R37 (ICR) = 0x8000
//  DSPR region 0x7000A000 -> zeros (verified zero before the demo)
'use strict';
const fs = require('fs');
const state = JSON.parse(fs.readFileSync('m25_3_ram_state.json', 'utf8'));

const b64 = (v) => { const b = Buffer.alloc(4); b.writeUInt32LE(v >>> 0, 0); return b.toString('base64'); };

console.log('before: R0 =', Buffer.from(state.regs[0], 'base64').readUInt32LE(0).toString(16));
console.log('before: R27 =', Buffer.from(state.regs[27], 'base64').readUInt32LE(0).toString(16));
console.log('before: R37 =', Buffer.from(state.regs[37], 'base64').readUInt32LE(0).toString(16));

state.regs[0] = b64(0x8003A336);
state.regs[27] = b64(0x70009000);
state.regs[37] = b64(0x8000);

for (const r of state.regions) {
    const mem = Buffer.from(r.b64, 'base64');
    let nz = 0;
    for (let i = 0; i < mem.length; i++) if (mem[i] !== 0) nz++;
    console.log(`region @0x${r.base.toString(16)}: nonzero bytes = ${nz}`);
    if (r.base === 0x7000A000 && nz > 0) {
        r.b64 = Buffer.alloc(r.size).toString('base64');
        console.log('  -> zeroed (restores the verified input state)');
    }
}

fs.writeFileSync('m25_3_ram_state.json', JSON.stringify(state));
console.log('state file fixed');
