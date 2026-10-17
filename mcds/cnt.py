"""Count register-read families in CONN4085 (MTV) c2s stream."""
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


c = parse_bin(os.path.join(CAP, 'c2s_4085.bin'))
pats = {
    'm_a R32@F003601C': b'\x00\x25\x1c\x60',
    'm_d R32@F0036034': b'\x00\x25\x34\x60',
    'm_d R32@F0036040': b'\x00\x25\x40\x60',
    'm_b R64@50000090': b'\x00\x27\x90\x00',
    'm_c R32@F0000010': b'\x00\x25\x10\x00',
}
counts = {k: 0 for k in pats}
end1 = end3 = end_other = 0
mb_frames = 0
for p in c:
    for k, pat in pats.items():
        if pat in p:
            counts[k] += 1
    if pats['m_b R64@50000090'] in p:
        mb_frames += 1
        if p.endswith(b'\xb1\x01\x00'):
            end1 += 1
        elif p.endswith(b'\xb1\x03\x00'):
            end3 += 1
        else:
            end_other += 1
print('total c2s msgs:', len(c))
for k in pats:
    print(f'  {k}: {counts[k]}')
print('m_b END variants: END(1)=%d END(3)=%d other=%d' % (end1, end3, end_other))
