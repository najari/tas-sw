// m25_4b2_tas_probe.js - query the TAS server (24817) directly: SERVER_CONNECT + GET_TARGETS
// Wire: 4-byte LE length prefix, then [wl][cmd][con_id(2)][params...]
'use strict';
const net = require('net');
const sock = net.connect(24817, '127.0.0.1');
let rx = Buffer.alloc(0);
const queue = [];
sock.on('data', (d) => {
    rx = Buffer.concat([rx, d]);
    for (;;) {
        if (rx.length < 4) return;
        const total = rx.readUInt32LE(0);
        if (rx.length < total) return;
        const payload = rx.subarray(4, total);
        rx = rx.subarray(total);
        if (queue.length) queue.shift()(payload);
    }
});
sock.on('error', (e) => { console.log('socket error:', e.message); process.exit(1); });
const frame = (p) => { const h = Buffer.alloc(4); h.writeUInt32LE(p.length + 4, 0); return Buffer.concat([h, p]); };
const rpc = (p) => new Promise((res) => { queue.push(res); sock.write(frame(p)); });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
    await sleep(300);

    /* SERVER_CONNECT (0x80): wl=0x0D, name @4 (32B), user @36 (16B), pid @52 */
    const sc = Buffer.alloc(56);
    sc[0] = 0x0D; sc[1] = 0x80;
    sc.write('M25 diag', 4, 'ascii');
    sc.write('admin', 36, 'ascii');
    sc.writeUInt32LE(process.pid, 52);
    const rsc = await rpc(sc);
    console.log('SERVER_CONNECT: cmd=0x' + rsc[1].toString(16) + ' err=0x' + rsc[3].toString(16) +
        ' server=' + rsc.subarray(8, 8 + 40).toString('ascii').replace(/\0.*$/, ''));

    /* GET_TARGETS (0x86) */
    const r = await rpc(Buffer.from([0x00, 0x86, 0x00, 0x00]));
    console.log('GET_TARGETS: cmd=0x' + r[1].toString(16) + ' err=0x' + r[3].toString(16) + ' count=' + r[6]);
    const n = r[6];
    for (let i = 0; i < n; i++) {
        const t = r.subarray(8 + 88 * i, 8 + 88 * (i + 1));
        const id = t.subarray(0, 64).toString('ascii').replace(/\0.*$/, '');
        const dtype = t.readUInt32LE(64);
        const phys = t[84], nclient = t[85];
        console.log(`  [${i}] '${id}' device_type=0x${dtype.toString(16)} phys=${phys} nclient=${nclient}`);
    }
    process.exit(0);
})();
