"""Focused probe: counter progression, cadence, raw bytes for CONN3341/4085/3342."""
import os
import re
import struct
import collections

CAP = r'C:\work\tas_server_analysis\mcds\tas_capture'


def parse_bin(path):
    d = open(path, 'rb').read()
    msgs = []
    off = 0
    while off < len(d):
        size = struct.unpack_from('<I', d, off)[0]
        msgs.append(d[off + 4:off + size])
        off += size
    return msgs


tl = collections.defaultdict(lambda: {'C2S': [], 'S2C': []})
re_ = re.compile(r'^\[(\d\d:\d\d:\d\d\.\d+)\] CONN(\d+) (C2S|S2C) (.*)$')
for line in open(os.path.join(CAP, 'timeline.log'), encoding='utf-8', errors='replace'):
    m = re_.match(line.strip())
    if m and 'head=' in m.group(4):
        tl[int(m.group(2))][m.group(3)].append(m.group(1))


def probe(n, show_idx):
    c2s = parse_bin(os.path.join(CAP, f'c2s_{n}.bin'))
    s2c = parse_bin(os.path.join(CAP, f's2c_{n}.bin'))
    tc = tl[n]['C2S']
    print(f'===== CONN{n} c2s={len(c2s)} s2c={len(s2c)} t0={tc[0]} t1={tc[-1]}')
    cnts = []
    for i, p in enumerate(c2s):
        if len(p) > 8 and p[1] in (0xB0, 0xB1):
            cnts.append((i, struct.unpack_from('<H', p, 4)[0]))
    deltas = collections.Counter((c2 - c1) % 65536 for (_, c1), (_, c2) in zip(cnts, cnts[1:]))
    print('PL0 count:', len(cnts), ' first:', [(i, hex(c)) for i, c in cnts[:8]])
    print('delta histogram (top12):', deltas.most_common(12))
    print('last:', [(i, hex(c)) for i, c in cnts[-6:]])
    print(f'-- full messages --')
    for i in show_idx:
        p = c2s[i]
        print(f'c2s #{i} t={tc[i] if i < len(tc) else "?"} {len(p) + 4}B: {p.hex()}')
        if i < len(s2c):
            q = s2c[i]
            print(f's2c #{i} {len(q) + 4}B: {q.hex()}')
    buckets = collections.Counter()
    for i in range(len(c2s)):
        t = tc[i] if i < len(tc) else None
        if t:
            buckets[t[:6] + f'{int(t[6:8]) // 10 * 10:02d}'] += 1
    print('msg/10s buckets: ' + '  '.join(f'{b}={v}' for b, v in sorted(buckets.items())))
    return c2s, s2c, tc


c41, s41, t41 = probe(3341, [0, 1, 2, 3, 4, 15, 16, 17, 18, 400])

# value series for 4085
c85, s85, t85 = probe(4085, [5, 6, 7, 8, 9, 68, 69, 70])

mb_rq = bytes([0, 0x15, 0, 0x50])
mb_item = bytes([0, 0x27, 0x90, 0x00])
mc_rq = bytes([0, 0x15, 0, 0xf0])
seq_b = []
seq_c = []
for i, p in enumerate(c85):
    if p[1] == 0xB0 and len(p) >= 20:
        if mb_rq in p[8:16] and mb_item in p[16:24]:
            q = s85[i]
            if len(q) >= 16:
                seq_b.append((t85[i] if i < len(t85) else '', struct.unpack_from('<Q', q, 8)[0]))
        if mc_rq in p[8:16] and bytes([0, 0x25, 0x10, 0x00]) in p[12:20]:
            q = s85[i]
            if len(q) >= 12:
                seq_c.append((t85[i] if i < len(t85) else '', struct.unpack_from('<I', q, 8)[0]))
print('=== 4085 m_b (R64 @50000090) first 20 values ===')
for t, v in seq_b[:20]:
    print(f'   {t}  {v:#x}')
print('   ... last 5:')
for t, v in seq_b[-5:]:
    print(f'   {t}  {v:#x}')
print('=== 4085 m_c (R32 @F0000010) first 20 values ===')
for t, v in seq_c[:20]:
    print(f'   {t}  {v:#x}')
print('   ... last 5:')
for t, v in seq_c[-5:]:
    print(f'   {t}  {v:#x}')
