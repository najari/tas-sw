"""Targeted dumps: con_info RDBLK blocks, session-start fail rsp, churn archetypes."""
import os
import struct

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


def show(n, cidx, sidx):
    c = parse_bin(os.path.join(CAP, f'c2s_{n}.bin'))
    s = parse_bin(os.path.join(CAP, f's2c_{n}.bin'))
    print(f'##### CONN{n} (c2s={len(c)} s2c={len(s)}) #####')
    for i in cidx:
        if i < len(c):
            print(f'  c2s #{i} ({len(c[i]) + 4}B): {c[i].hex()}')
    for i in sidx:
        if i < len(s):
            print(f'  s2c #{i} ({len(s[i]) + 4}B): {s[i].hex()}')


print('== 3341 MCD: con_info blocks + init writes ==')
show(3341, list(range(4, 15)), list(range(4, 15)))
print('== 4085 MTV: SS(fail) / SS(ok) / DC ==')
show(4085, [2, 3, 4], [2, 3, 4])
print('== 429 (churn: SVR+SS+PL0x2) ==')
show(429, list(range(6)), list(range(6)))
print('== 431 (churn: 248B) ==')
show(431, list(range(6)), list(range(6)))
print('== 424 (churn: 60B probe) ==')
show(424, [0], [0])
print('== 3343 (TS dialog tiny conn) ==')
show(3343, list(range(5)), list(range(5)))
print('== 3437 con_info (SS failed) ==')
show(3437, list(range(5)), list(range(5)))
print('== 3438 con_info (SS ok) ==')
show(3438, list(range(5)), list(range(5)))
