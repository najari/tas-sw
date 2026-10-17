"""Precise field segmentation for docs: con_info blocks, SS rsp, GT rsp, GET_CLIENTS."""
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


def u16(b, o):
    return struct.unpack_from('<H', b, o)[0]


def u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


def u64(b, o):
    return struct.unpack_from('<Q', b, o)[0]


def rows(b, base=0):
    for i in range(0, len(b), 16):
        chunk = b[i:i + 16]
        asc = ''.join(chr(x) if 32 <= x < 127 else '.' for x in chunk)
        print(f'   {base + i:04x}  {chunk.hex(" ")}  {asc}')


s41 = parse_bin(os.path.join(CAP, 's2c_3341.bin'))
s85 = parse_bin(os.path.join(CAP, 's2c_4085.bin'))
s429 = parse_bin(os.path.join(CAP, 's2c_429.bin'))
s38 = parse_bin(os.path.join(CAP, 's2c_3438.bin'))

print('== 3341 s2c#4 : RDBLK@0xD000 data (104B) ==')
d = s41[4][8:8 + 104]
print('u32@0', hex(u32(d, 0)), 'u32@4', hex(u32(d, 4)), 'family', repr(d[8:72].rstrip(b'\0')), 'u32@72', hex(u32(d, 72)))
rows(d[72:], 72)

print('== 3341 s2c#5 : RDBLK@0x0000 data (96B) ==')
d = s41[5][8:8 + 96]
rows(d)

print('== 3341 s2c#5 : RDBLK@0x8000 data (168B) ==')
d2 = s41[5][108:108 + 168]
rows(d2)
print('id64', repr(d2[0:64].rstrip(b'\0')))
print('vnd64', repr(d2[64:128].rstrip(b'\0')))
print('tag16', repr(d2[128:144].rstrip(b'\0')))
print('tail24', d2[144:168].hex(' '))

print('== 4085 s2c#3 : SESSION_START rsp (ok) ==')
p = s85[3]
ci = p[8:]
print('hdr', p[:8].hex(' '), '-> err=0x%02x vmin=%d vmax=%d ninst=%d' % (p[3], p[4], p[5], p[6]))
print('maxrq', u32(ci, 0), 'maxrsp', u32(ci, 4), 'devtype', hex(u32(ci, 8)),
      'devid', ':'.join(f'{u32(ci, 12 + 4 * i):08x}' for i in range(4)), 'ipv4', u32(ci, 28))
print('feat', hex(u16(ci, 32)), 'phys', ci[34], 'maxrw', ci[35],
      'rwmask', hex(u16(ci, 36)), 'addrmap', hex(u16(ci, 38)),
      'msz', u16(ci, 40), u16(ci, 42), 'num', ci[44], ci[45], 'resv', hex(u16(ci, 46)))
print('id', repr(ci[48:112].rstrip(b'\0')), 'hash', hex(u32(ci, 112)), repr(ci[116:124].rstrip(b'\0')))
rows(ci[28:52], 28)

print('== 4085 s2c#2 : SESSION_START rsp (sname=admin, fail) ==')
f = s85[2]
print('hdr', f[:8].hex(' '), 'nonzero in struct:', any(f[8:]))

print('== 429 s2c#3 : GET_TARGETS rsp ==')
p = s429[3]
print('hdr', p[:8].hex(' '), 'u16@4..12:', [u16(p, i) for i in (4, 6, 8, 10)])
t = p[8:8 + 88]
print('id', repr(t[0:64].rstrip(b'\0')), 'dt', hex(u32(t, 64)),
      'devid', ':'.join(f'{u32(t, 68 + 4 * i):08x}' for i in range(4)),
      'phys', t[84], 'nclient', t[85], 'resv', t[86:88].hex())
print('426 GT rsp nclient check:')
s426 = parse_bin(os.path.join(CAP, 's2c_426.bin'))
p = s426[1]
t = p[8:8 + 88]
print('   nclient', t[85], 'phys', t[84])
print('3341 GT rsp nclient check:')
p = s41[1]
t = p[8:8 + 88]
print('   nclient', t[85], 'phys', t[84])

print('== 3438 s2c#2 : GET_CLIENTS rsp (196B) ==')
p = s38[2]
print('hdr', p[:8].hex(' '))
rows(p[8:])

print('== 3438 s2c#0 : SERVER_CONNECT rsp ==')
p = s38[0]
print('hdr', p[:8].hex(' '), 'name', repr(p[8:72].rstrip(b'\0')),
      'vmin', u16(p, 72), 'vmaj', u16(p, 74), 'protos', hex(u32(p, 76)),
      'chl', hex(u32(p, 80)), 'trc', hex(u32(p, 84)))
print('resv', p[88:104].hex(' '), 'date', repr(p[104:120].rstrip(b'\0')), 'start_us', u64(p, 120), 'challenge', u64(p, 128))
