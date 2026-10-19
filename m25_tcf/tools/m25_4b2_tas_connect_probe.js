// m25_4b2_tas_connect_probe.js - try the full TAS connect sequence with different
// DEVICE_CONNECT options (0x0000..0x0003), each on a fresh TCP connection.
'use strict';
const net = require('net');
const ID = 'DAS JTAG AURDUINO 46YPEADW';

function conn() {
    return new Promise((resolve, reject) => {
        const sock = net.connect(24817, '127.0.0.1');
        let rx = Buffer.alloc(0);
        const q = [];
        sock.on('data', (d) => {
            rx = Buffer.concat([rx, d]);
            for (;;) {
                if (rx.length < 4) return;
                const total = rx.readUInt32LE(0);
                if (rx.length < total) return;
                const payload = rx.subarray(4, total);
                rx = rx.subarray(total);
                if (q.length) q.shift()(payload);
            }
        });
        sock.on('error', reject);
        sock.on('connect', () => resolve({
            sock,
            rpc: (p) => new Promise((res) => { q.push(res); const h = Buffer.alloc(4); h.writeUInt32LE(p.length + 4, 0); sock.write(Buffer.concat([h, p])); }),
        }));
    });
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
    await sleep(200);
    for (const opt of [0x0000, 0x0001, 0x0002, 0x0003]) {
        const { sock, rpc } = await conn();
        try {
            await sleep(150);
            /* SERVER_CONNECT */
            const sc = Buffer.alloc(56);
            sc[0] = 0x0D; sc[1] = 0x80; sc.write('M25 probe', 4, 'ascii'); sc.write('admin', 36, 'ascii'); sc.writeUInt32LE(process.pid, 52);
            const rsc = await rpc(sc);
            if (rsc[3] !== 0x08) { console.log(`opt 0x${opt.toString(16)}: SERVER_CONNECT err=0x${rsc[3].toString(16)}`); sock.destroy(); continue; }

            /* SESSION_START */
            const ss = Buffer.alloc(112);
            ss[0] = 0x1B; ss[1] = 0xA1; ss[3] = 1; ss.write(ID, 16, 'ascii');
            const rss = await rpc(ss);
            if (rss[3] !== 0x08) { console.log(`opt 0x${opt.toString(16)}: SESSION_START err=0x${rss[3].toString(16)}`); sock.destroy(); continue; }

            /* SET_DEVICE_KEY (hot attach) */
            const sk = Buffer.alloc(8);
            sk[0] = 0x01; sk[1] = 0xAD; sk.writeUInt16LE(0x0001, 4); sk.writeUInt16LE(0, 6);
            const rsk = await rpc(sk);
            if (rsk[3] !== 0x08) { console.log(`opt 0x${opt.toString(16)}: SET_DEVICE_KEY err=0x${rsk[3].toString(16)}`); sock.destroy(); continue; }

            /* DEVICE_CONNECT with the option */
            const dc = Buffer.alloc(8);
            dc[0] = 0x01; dc[1] = 0xA8; dc.writeUInt16LE(opt, 4);
            const rdc = await rpc(dc);
            const err = rdc[3];
            const dtype = rdc.length >= 12 ? rdc.readUInt32LE(8) : 0;
            console.log(`opt 0x${opt.toString(16).padStart(4, '0')}: DEVICE_CONNECT err=0x${err.toString(16)} device_type=0x${dtype.toString(16)}`);
        } catch (e) {
            console.log(`opt 0x${opt.toString(16)}: error ${e.message}`);
        }
        sock.destroy();
        await sleep(400);
    }
    process.exit(0);
})();
