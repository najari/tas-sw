"""Decode the TAS protocol capture (c2s.bin / s2c.bin) into a readable log.

Wire format (see tas_pkt.h):
  PL2 packet : [u32 LE total size incl. this word][PL1 or PL0 payload]
  PL1 frame  : [PL0_START][PL0 items...][PL0_END]  (all inside ONE PL2 packet)
               each sub-struct is self-delimiting: size = (wl+1)*4 bytes
  PL1 header : rq  = [wl u8][cmd u8][con_id u8][...]
               rsp = [wl u8][cmd u8][...]
"""
import struct
import os
import datetime

OUT = os.path.dirname(os.path.abspath(__file__))

PL1_NAMES = {
    0x80: 'SERVER_CONNECT', 0x86: 'GET_TARGETS', 0x87: 'GET_CLIENTS',
    0xA1: 'SESSION_START', 0xA2: 'PING', 0xA8: 'DEVICE_CONNECT',
    0xAA: 'DEVICE_RESET_COUNT', 0xAC: 'GET_CHALLENGE', 0xAD: 'SET_DEVICE_KEY',
    0xB0: 'PL0_START', 0xB1: 'PL0_END', 0xEE: 'ERROR',
}
PL0_NAMES = {
    0x10: 'ACCESS_MODE', 0x11: 'ADDR_MAP', 0x15: 'BASE_ADDR32', 0x16: 'BASE_ADDR64',
    0x20: 'WR8', 0x21: 'RD8', 0x22: 'WR16', 0x23: 'RD16', 0x24: 'WR32', 0x25: 'RD32',
    0x26: 'WR64', 0x27: 'RD64', 0x2A: 'WRBLK', 0x2B: 'FILL', 0x2C: 'RDBLK', 0x2D: 'RDBLK1KB',
}
ERR_NAMES = {
    0x08: 'NO_ERROR(0x08)', 0x10: 'PARAM(0x10)', 0x12: 'NOT_SUPPORTED(0x12)',
    0x14: 'USAGE(0x14)', 0x1E: 'PROTOCOL(0x1E)', 0x20: 'PL1_CMD_FAILED(0x20)',
    0x32: 'SERVER_LOCKED(0x32)', 0x38: 'SESSION(0x38)', 0x44: 'DEV_RESET(0x44)',
    0x46: 'DEV_LOCKED(0x46)', 0x48: 'DEV_ACCESS(0x48)',
    0x80: 'PL0_NO_ERROR(0x80)', 0x89: 'PL0_ACC_MODE(0x89)', 0x8A: 'PL0_ADDR_MAP(0x8A)',
    0x8B: 'PL0_ADDR_BLOCKED(0x8B)', 0x8D: 'PL0_DATA(0x8D)', 0x98: 'PL0_CONSEQUENTIAL(0x98)',
}
RW_MODE_BITS = {0x0000: 'DEFAULT', 0x0001: 'BIT0', 0x0002: 'BIT1', 0x0004: 'BIT2'}

lines = []


def w(s=''):
    lines.append(s)


def hexdump(b, indent='      '):
    for i in range(0, len(b), 16):
        chunk = b[i:i + 16]
        hx = ' '.join(f'{x:02X}' for x in chunk)
        asc = ''.join(chr(x) if 32 <= x < 127 else '.' for x in chunk)
        w(f'{indent}{i:04X}: {hx:<47} {asc}')


def cstr(b):
    return b.split(b'\0')[0].decode(errors='replace')


def u16(b, o):
    return struct.unpack_from('<H', b, o)[0]


def u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


def u64(b, o):
    return struct.unpack_from('<Q', b, o)[0]


def err_name(e):
    return ERR_NAMES.get(e, f'{e:#04x}')


def pl1_rq_elems(p):
    """Split a PL1 request frame into sub-structs by (wl+1)*4 walking."""
    elems = []
    off = 0
    while off < len(p):
        wl = p[off]
        size = (wl + 1) * 4
        elems.append(p[off:off + size])
        off += size
    return elems


def pl1_rsp_elems(p):
    """Split a PL1 response frame. Note RDBLK1KB exception: cmd 0x2D, wl=0 -> 4+1024."""
    elems = []
    off = 0
    while off < len(p):
        wl = p[off]
        cmd = p[off + 1]
        if cmd == 0x2D and wl == 0:
            size = 4 + 1024
        else:
            size = (wl + 1) * 4
        elems.append(p[off:off + size])
        off += size
    return elems


def fmt_time_us(v):
    if v == 0:
        return '0'
    try:
        dt = datetime.datetime.fromtimestamp(v / 1e6, datetime.timezone.utc)
        return f'{v} ({dt.strftime("%Y-%m-%d %H:%M:%S.%f")} UTC)'
    except (OverflowError, OSError, ValueError):
        return f'{v}'


def decode_simple_rq(p):
    cmd = p[1]
    if cmd == 0x80:
        return [f'reserved={u16(p, 2)}', f'client_name={cstr(p[4:36])!r}',
                f'user_name={cstr(p[36:52])!r}', f'client_pid={u32(p, 52)}']
    if cmd == 0x86:
        return [f'start_index={p[2]}', f'reserved={p[3]}']
    if cmd == 0xA1:
        return [f'con_id={p[2]}', f'client_type={p[3]} (1=RW)', f'param8={p[4:8].hex()}',
                f'param64={u64(p, 8):#x}', f'identifier={cstr(p[16:80])!r}',
                f'session_name={cstr(p[80:96])!r}', f'session_pw={cstr(p[96:112])!r}']
    if cmd == 0xA8:
        return [f'con_id={p[2]}', f'reserved={p[3]}', f'option={u16(p, 4):#06x} (3=RESET_AND_HALT)']
    return []


def decode_pl0_rq(p):
    cmd = p[1]
    if cmd == 0x10:
        return [f'ACCESS_MODE acc_mode={u16(p, 2):#06x}']
    if cmd == 0x11:
        return [f'ADDR_MAP addr_map={p[2]}']
    if cmd == 0x15:
        return [f'BASE_ADDR32 ba31to16={u16(p, 2):#06x} (-> 0x{u16(p, 2):04X}0000)']
    if cmd == 0x16:
        return [f'BASE_ADDR64 ba31to16={u16(p, 2):#06x} ba63to32={u32(p, 4):#010x}']
    if cmd in (0x20, 0x22, 0x24):
        return [f'WR{ {0x20:8, 0x22:16, 0x24:32}[cmd] } a15to0={u16(p, 2):#06x} data={u32(p, 4):#010x}']
    if cmd == 0x26:
        return [f'WR64 a15to0={u16(p, 2):#06x} data={u64(p, 4):#018x}']
    if cmd in (0x21, 0x23, 0x25, 0x27):
        return [f'RD{ {0x21:8, 0x23:16, 0x25:32, 0x27:64}[cmd] } a15to0={u16(p, 2):#06x}']
    if cmd == 0x2A:
        n = (len(p) - 4) // 4
        return [f'WRBLK a15to0={u16(p, 2):#06x} num_words={n}']
    if cmd == 0x2B:
        return [f'FILL a15to0={u16(p, 2):#06x} wlwr={p[4]} value={u64(p, 8):#018x}']
    if cmd == 0x2C:
        return [f'RDBLK a15to0={u16(p, 2):#06x} wlrd={p[4]} (words; 0=256)']
    return []


def decode_pl0_rsp(p):
    cmd = p[1]
    if cmd == 0x2C or cmd == 0x2D:
        wlrd, err = p[2], p[3]
        data = p[4:]
        words = len(data) // 4
        head = f'RDBLK rsp wlrd={wlrd} err={err_name(err)} data_words={words}'
        if words:
            vals = struct.unpack_from(f'<{words}I', data)
            head += ' first_words=' + ' '.join(f'{v:#010x}' for v in vals[:4]) + (' ...' if words > 4 else '')
        return [head]
    if cmd in (0x21, 0x23, 0x25, 0x27):
        wlrd, err = p[2], p[3]
        data = p[4:]
        vals = struct.unpack_from(f'<{len(data)//4}I', data) if data else ()
        return [f'RD rsp wlrd={wlrd} err={err_name(err)} data=' + ' '.join(f'{v:#010x}' for v in vals)]
    if cmd == 0x24 or cmd in (0x20, 0x22, 0x26, 0x2A, 0x2B):
        return [f'WR rsp wlwr={p[2]} err={err_name(p[3])}']
    return []


def dump_pl1_frame_rq(elems, tag):
    out = []
    for i, e in enumerate(elems):
        cmd = e[1]
        if cmd == 0xB0:
            out.append(f'  PL1 rq  PL0_START wl={e[0]} con_id={e[2]:#04x} protoc_ver={e[3]}'
                       f' pl1_cnt={u16(e, 4):#06x} addr_map_mask={u16(e, 6):#06x}')
        elif cmd == 0xB1:
            out.append(f'  PL1 rq  PL0_END   wl={e[0]} num_pl0_rw={u16(e, 2)}')
        else:
            sub = decode_pl0_rq(e)
            out.append(f'  PL0 rq  {sub[0]}')
    return out


def dump_pl1_frame_rsp(elems):
    out = []
    for e in elems:
        cmd = e[1]
        if cmd == 0xB0:
            out.append(f'  PL1 rsp PL0_START wl={e[0]} con_id={e[2]:#04x} err={err_name(e[3])}')
        elif cmd == 0xB1:
            out.append(f'  PL1 rsp PL0_END   wl={e[0]} pl1_cnt={u16(e, 2):#06x}')
        else:
            sub = decode_pl0_rsp(e)
            if sub:
                out.append(f'  PL0 rsp {sub[0]}')
    return out


def process(fname, direction):
    data = open(os.path.join(OUT, fname), 'rb').read()
    w(f'===== {fname} ({direction}), {len(data)} bytes =====')
    off = 0
    idx = 0
    while off < len(data):
        size = u32(data, off)
        p = data[off + 4:off + size]
        cmd = p[1]
        w()
        w(f'[{idx:02d}] @{off:#06x}  {direction}  PL2_size={size}')
        if cmd in (0xB0, 0xB1):
            # PL0 frame (concatenated structs)
            elems = pl1_rq_elems(p) if direction == 'c2s' else pl1_rsp_elems(p)
            npl0 = sum(1 for e in elems if e[1] not in (0xB0, 0xB1))
            w(f'   -> PL0 RW frame ({npl0} PL0 item(s))')
            for line in (dump_pl1_frame_rq(elems, direction) if direction == 'c2s'
                         else dump_pl1_frame_rsp(elems)):
                w(line)
        elif cmd == 0x80 and direction == 'c2s':
            w('   -> PL1 SERVER_CONNECT request')
            for x in decode_simple_rq(p):
                w(f'      {x}')
        elif cmd == 0x80:
            w('   -> PL1 SERVER_CONNECT response')
            w(f'      wl={p[0]} err={err_name(p[3])} reserved1={u32(p, 4)}')
            si = p[8:8 + 120]
            w(f'      server_name={cstr(si[0:64])!r} v_major={u16(si, 66)} v_minor={u16(si, 64)}')
            w(f'      supp_protoc_ver={u32(si, 68):#x} supp_chl_target={u32(si, 72):#x} supp_trc_type={u32(si, 76):#010x}')
            w(f'      date={cstr(si[96:112])!r}')
            w(f'      start_time_us={fmt_time_us(u64(si, 112))}')
            w(f'      challenge={u64(p, 128):#018x}')
        elif cmd == 0x86 and direction == 'c2s':
            w('   -> PL1 GET_TARGETS request')
            for x in decode_simple_rq(p):
                w(f'      {x}')
        elif cmd == 0x86:
            w('   -> PL1 GET_TARGETS response')
            w(f'      wl={p[0]} err={err_name(p[3])} num_target={p[4]} start_index={p[5]} num_now={p[6]}')
            o = 8
            for i in range(p[6]):
                t = p[o:o + 88]
                w(f'      target[{i}] identifier={cstr(t[0:64])!r}')
                w(f'                 device_type={u32(t, 64):#010x} device_id={u32(t,68):#010x}:{u32(t,72):#010x}:{u32(t,76):#010x}:{u32(t,80):#010x}')
                w(f'                 dev_con_phys={t[84]:#04x} num_client={t[85]}')
                o += 88
        elif cmd == 0xA1 and direction == 'c2s':
            w('   -> PL1 SESSION_START request')
            for x in decode_simple_rq(p):
                w(f'      {x}')
        elif cmd == 0xA1:
            w('   -> PL1 SESSION_START response (tas_pl1rsp_ping_st)')
            w(f'      wl={p[0]} con_id={p[2]:#04x} err={err_name(p[3])}'
              f' protoc_ver_min={p[4]} protoc_ver_max={p[5]} num_instances={p[6]}')
            ci = p[8:8 + 124]
            w(f'      con_info: max_pl2rq={u32(ci,0)} max_pl2rsp={u32(ci,4)} device_type={u32(ci,8):#010x}')
            w(f'                device_id={u32(ci,12):#010x}:{u32(ci,16):#010x}:{u32(ci,20):#010x}:{u32(ci,24):#010x}')
            w(f'                ipv4={u32(ci,28):#010x} dev_con_feat={u16(ci,32):#06x} dev_con_phys={ci[34]:#04x}'
              f' pl0_max_num_rw={ci[35]}')
            w(f'                pl0_rw_mode_mask={u16(ci,36):#06x} pl0_addr_map_mask={u16(ci,38):#06x}')
            w(f'                msg_length_c2d={u16(ci,40)} msg_length_d2c={u16(ci,42)} msg_num_c2d={ci[44]} msg_num_d2c={ci[45]}')
            w(f'                identifier={cstr(ci[48:112])!r}')
            w(f'                device_id_hash={u32(ci,112):#010x} device_id_hash_str={cstr(ci[116:124])!r}')
        elif cmd == 0xA8 and direction == 'c2s':
            w('   -> PL1 DEVICE_CONNECT request')
            for x in decode_simple_rq(p):
                w(f'      {x}')
        elif cmd == 0xA8:
            w('   -> PL1 DEVICE_CONNECT response')
            w(f'      wl={p[0]} con_id={p[2]:#04x} err={err_name(p[3])}'
              f' feat_used={u16(p, 4):#06x} reserved={u16(p, 6)} device_type={u32(p, 8):#010x}')
        else:
            w(f'   -> {PL1_NAMES.get(cmd, "UNKNOWN")} (cmd={cmd:#04x}) raw={p.hex()}')
        if size <= 32:
            hexdump(p)
        off += size
        idx += 1


process('c2s.bin', 'c2s')
w()
w()
process('s2c.bin', 's2c')

with open(os.path.join(OUT, 'capture_decoded.txt'), 'w', encoding='utf-8') as f:
    f.write('\n'.join(lines) + '\n')
print('\n'.join(lines))
