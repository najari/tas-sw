"""Final dumps: poll cycle raw frames (3341 #15-19, #400; 4085 #5-8, #68-69)."""
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


print('== 3341 MCD poll cycle + fail ==')
show(3341, [15, 16, 17, 18, 19, 400], [15, 16, 17, 18, 19, 400])
print()
print('== 4085 MTV registers ==')
show(4085, [5, 6, 7, 8, 68, 69], [5, 6, 7, 8, 68, 69])
