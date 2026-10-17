"""Detailed decoder for the native-tool TAS hijack capture (part 1: helpers)."""
import os
import re
import struct
import collections
import json

CAP = r'C:\work\tas_server_analysis\mcds\tas_capture'
OUT = r'C:\work\tas_server_analysis\mcds\native_report'
os.makedirs(OUT, exist_ok=True)

PL1_NAMES = {
    0x80: 'SERVER_CONNECT', 0x86: 'GET_TARGETS', 0x87: 'GET_CLIENTS',
    0xA1: 'SESSION_START', 0xA2: 'PING', 0xA8: 'DEVICE_CONNECT',
    0xAA: 'DEVICE_RESET_COUNT', 0xAC: 'GET_CHALLENGE', 0xAD: 'SET_DEVICE_KEY',
    0xB0: 'PL0_START', 0xB1: 'PL0_END', 0xEE: 'ERROR',
}
RD_BITS = {0x21: 8, 0x23: 16, 0x25: 32, 0x27: 64}
WR_BITS = {0x20: 8, 0x22: 16, 0x24: 32, 0x26: 64}
ERR_NAMES = {0x08: 'OK', 0x10: 'PARAM', 0x12: 'NOT_SUPPORTED', 0x14: 'USAGE',
             0x1E: 'PROTOCOL', 0x20: 'PL1_CMD_FAILED', 0x32: 'SERVER_LOCKED',
             0x38: 'SESSION', 0x44: 'DEV_RESET', 0x46: 'DEV_LOCKED', 0x48: 'DEV_ACCESS',
             0x80: 'PL0_OK', 0x89: 'PL0_ACC_MODE', 0x8A: 'PL0_ADDR_MAP',
             0x8B: 'PL0_ADDR_BLOCKED', 0x8D: 'PL0_DATA', 0x98: 'PL0_CONSEQUENTIAL'}


def u16(b, o):
    return struct.unpack_from('<H', b, o)[0]


def u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


def u64(b, o):
    return struct.unpack_from('<Q', b, o)[0]


def cstr(b):
    return b.split(b'\0')[0].decode(errors='replace')


def ename(e):
    return ERR_NAMES.get(e, f'ERR_{e:02X}')


def parse_bin(path):
    """Returns list of payload bytes (size word stripped); notes BAD frames."""
    if not os.path.exists(path):
        return [], 'missing'
    data = open(path, 'rb').read()
    msgs = []
    off = 0
    note = ''
    while off < len(data):
        if off + 4 > len(data):
            note = f'trail@{off}'
            break
        size = u32(data, off)
        if size < 4 or off + size > len(data):
            note = f'bad@{off}(size={size})'
            break
        msgs.append(data[off + 4:off + size])
        off += size
    return msgs, note


def rq_elems(p):
    out = []
    off = 0
    while off < len(p):
        wl = p[off]
        size = (wl + 1) * 4
        out.append(p[off:off + size])
        off += size
    return out


def rsp_elems(p):
    out = []
    off = 0
    while off < len(p):
        wl = p[off]
        cmd = p[off + 1] if off + 1 < len(p) else 0
        size = 4 + 1024 if (cmd == 0x2D and wl == 0) else (wl + 1) * 4
        out.append(p[off:off + size])
        off += size
    return out


def pl0_rq_item(e):
    if len(e) < 2:
        return f'SHORT({e.hex()})'
    cmd = e[1]
    try:
        if cmd == 0x10:
            return f'ACCESS_MODE={u16(e, 2):#06x}'
        if cmd == 0x11:
            return f'ADDR_MAP={e[2]}'
        if cmd == 0x15:
            v = u16(e, 2)
            return f'BASE32={v:#06x}->0x{v:04X}0000'
        if cmd == 0x16:
            return f'BASE64={u16(e, 2):#06x}/{u32(e, 4):#x}'
        if cmd in WR_BITS:
            bits = WR_BITS[cmd]
            if bits == 64:
                return f'WR64 @{u16(e, 2):#06x} data={u64(e, 4):#018x}'
            return f'WR{bits} @{u16(e, 2):#06x} data={u32(e, 4):#010x}'
        if cmd in RD_BITS:
            return f'RD{RD_BITS[cmd]} @{u16(e, 2):#06x}'
        if cmd == 0x2A:
            return f'WRBLK @{u16(e, 2):#06x} words={(len(e) - 4) // 4}'
        if cmd == 0x2B:
            return f'FILL @{u16(e, 2):#06x} wlwr={e[4]} val={u64(e, 8):#018x}'
        if cmd == 0x2C:
            return f'RDBLK @{u16(e, 2):#06x} wlrd={e[4]}'
        if cmd == 0x2D:
            return f'RDBLK1KB @{u16(e, 2):#06x}'
    except struct.error:
        return f'op{cmd:#04x} malformed={e.hex()}'
    return f'op{cmd:#04x} raw={e.hex()}'


def pl0_rsp_item(e):
    if len(e) < 2:
        return f'SHORT({e.hex()})'
    cmd = e[1]
    try:
        if cmd in RD_BITS:
            bits = RD_BITS[cmd]
            wlrd = e[2] if len(e) > 2 else -1
            err = e[3] if len(e) > 3 else -1
            data = e[4:]
            if bits == 64:
                v = u64(data, 0) if len(data) >= 8 else 0
            elif bits == 32:
                v = u32(data, 0) if len(data) >= 4 else 0
            elif bits == 16:
                v = u16(data, 0) if len(data) >= 2 else 0
            else:
                v = data[0] if data else 0
            return f'RD{bits} rsp wlrd={wlrd} err={ename(err)} data={v:#x}'
        if cmd in WR_BITS or cmd in (0x2A, 0x2B):
            wlwr = e[2] if len(e) > 2 else -1
            err = e[3] if len(e) > 3 else -1
            return f'WR rsp wlwr={wlwr} err={ename(err)}'
        if cmd == 0x2C:
            wlrd = e[2] if len(e) > 2 else -1
            err = e[3] if len(e) > 3 else -1
            words = len(e[4:]) // 4
            vals = struct.unpack_from(f'<{words}I', e, 4) if words else ()
            head = ' '.join(f'{v:#010x}' for v in vals[:4])
            return f'RDBLK rsp wlrd={wlrd} err={ename(err)} words={words} first={head}'
    except struct.error:
        return f'op{cmd:#04x} malformed={e.hex()}'
    return f'op{cmd:#04x} raw={e.hex()}'


# ---------------- full-message decoders ----------------
def full_c2s(p):
    cmd = p[1] if len(p) > 1 else -1
    if cmd in (0xB0, 0xB1):
        out = []
        for e in rq_elems(p):
            if e[1] == 0xB0:
                out.append(f'PL0_START con_id={e[2]:#04x} ver={e[3]} cnt={u16(e,4):#06x} mask={u16(e,6):#06x}')
            elif e[1] == 0xB1:
                out.append(f'PL0_END n_rw={u16(e,2)}')
            else:
                out.append(pl0_rq_item(e))
        return '; '.join(out)
    if cmd == 0x80:
        return f'SERVER_CONNECT name={cstr(p[4:36])!r} user={cstr(p[36:52])!r} pid={u32(p,52)}'
    if cmd == 0x86:
        return f'GET_TARGETS start={p[2]} res={p[3]}'
    if cmd == 0xA1:
        return (f'SESSION_START con_id={p[2]} ctype={p[3]} p8={p[4:8].hex()} p64={u64(p,8):#x} '
                f'id={cstr(p[16:80])!r} sname={cstr(p[80:96])!r} spw={cstr(p[96:112])!r}')
    if cmd == 0xA8:
        return f'DEVICE_CONNECT con_id={p[2]} opt={u16(p,4):#06x}'
    return f'{PL1_NAMES.get(cmd, "CMD")} cmd={cmd:#04x} raw={p.hex()}'


def full_s2c(p):
    cmd = p[1] if len(p) > 1 else -1
    if cmd in (0xB0, 0xB1):
        out = []
        for e in rsp_elems(p):
            if e[1] == 0xB0:
                out.append(f'PL0_START rsp con_id={e[2]:#04x} err={ename(e[3])}')
            elif e[1] == 0xB1:
                out.append(f'PL0_END rsp cnt={u16(e,2):#06x}')
            else:
                out.append(pl0_rsp_item(e))
        return '; '.join(out)
    if cmd == 0x80:
        si = p[8:8 + 128]
        return (f'SERVER_CONNECT rsp err={ename(p[3])} server={cstr(si[0:64])!r} ver={u16(si,64)}.{u16(si,66)} '
                f'protos={u32(si,68):#x} chl={u32(si,72):#x} trc={u32(si,76):#x} date={cstr(si[96:112])!r}')
    if cmd == 0x86:
        parts = [f'GET_TARGETS rsp err={ename(p[3])} n={p[6]}']
        o = 8
        for i in range(p[6]):
            t = p[o:o + 88]
            parts.append(f'target[{i}] id={cstr(t[0:64])!r} devtype={u32(t,64):#010x} '
                         f'devid={u32(t,68):#010x}:{u32(t,72):#010x}:{u32(t,76):#010x}:{u32(t,80):#010x} '
                         f'phys={t[84]:#04x} nclient={t[85]}')
            o += 88
        return '; '.join(parts)
    if cmd == 0xA1:
        ci = p[8:8 + 124]
        return (f'SESSION_START rsp con_id={p[2]} err={ename(p[3])} vmin={p[4]} vmax={p[5]} ninst={p[6]} '
                f'maxrq={u32(ci,0)} maxrsp={u32(ci,4)} devtype={u32(ci,8):#010x} '
                f'devid={u32(ci,12):#010x}:{u32(ci,16):#010x}:{u32(ci,20):#010x}:{u32(ci,24):#010x} '
                f'feat={u16(ci,32):#06x} phys={ci[34]:#04x} maxrw={ci[35]} rwmask={u16(ci,36):#06x} '
                f'addrmap={u16(ci,38):#06x} msz={u16(ci,40)}/{u16(ci,42)} id={cstr(ci[48:112])!r} '
                f'hash={u32(ci,112):#010x}/{cstr(ci[116:124])!r}')
    if cmd == 0xA8:
        return (f'DEVICE_CONNECT rsp con_id={p[2]} err={ename(p[3])} feat={u16(p,4):#06x} '
                f'devtype={u32(p,8):#010x}')
    return f'{PL1_NAMES.get(cmd, "CMD")} rsp cmd={cmd:#04x} raw={p.hex()}'


# ---------------- compact signatures ----------------
def sig_c2s(p):
    cmd = p[1] if len(p) > 1 else -1
    if cmd in (0xB0, 0xB1):
        parts = []
        for e in rq_elems(p):
            c2 = e[1]
            if c2 == 0xB0:
                parts.append(f'START(ver={e[3]},mask={u16(e,6):#06x})')
            elif c2 == 0xB1:
                parts.append(f'END({u16(e,2)})')
            elif c2 == 0x15:
                parts.append(f'B32:{u16(e,2):04X}')
            elif c2 == 0x11:
                parts.append(f'MAP{e[2]}')
            elif c2 == 0x10:
                parts.append(f'ACC{u16(e,2):#x}')
            elif c2 in RD_BITS:
                parts.append(f'R{RD_BITS[c2]}:{u16(e,2):04X}')
            elif c2 in WR_BITS:
                parts.append(f'W{WR_BITS[c2]}:{u16(e,2):04X}')
            elif c2 == 0x2C:
                parts.append(f'RDBLK:{u16(e,2):04X}')
            elif c2 == 0x2A:
                parts.append(f'WRBLK:{u16(e,2):04X}')
            else:
                parts.append(f'OP{c2:02X}')
        return '|'.join(parts)
    if cmd == 0x80:
        return f'SVR({cstr(p[4:36])!r})'
    if cmd == 0x86:
        return 'GT'
    if cmd == 0xA1:
        return f'SS(id={cstr(p[16:80])!r},name={cstr(p[80:96])!r})'
    if cmd == 0xA8:
        return f'DC(opt={u16(p,4):#06x})'
    return f'C{cmd:02X}({len(p)}B)'


def sig_s2c(p):
    cmd = p[1] if len(p) > 1 else -1
    if cmd in (0xB0, 0xB1):
        parts = []
        for e in rsp_elems(p):
            c2 = e[1]
            if c2 == 0xB0:
                parts.append(f'START(err={ename(e[3])})')
            elif c2 == 0xB1:
                parts.append('END')
            elif c2 in RD_BITS:
                bits = RD_BITS[c2]
                err = e[3] if len(e) > 3 else -1
                data = e[4:]
                if len(data) >= bits // 8:
                    v = int.from_bytes(data[:bits // 8], 'little')
                    parts.append(f'R{bits}:{v:#x}' if err in (0x80, 0x08) else f'R{bits}!{ename(err)}')
                else:
                    parts.append(f'R{bits}!{ename(err)}')
            elif c2 in WR_BITS or c2 in (0x2A, 0x2B):
                parts.append(f'W ack(err={ename(e[3])})')
            elif c2 == 0x2C:
                parts.append(f'RDBLK(err={ename(e[3])},w={len(e[4:]) // 4})')
            else:
                parts.append(f'OP{c2:02X}')
        return '|'.join(parts)
    if cmd == 0x80:
        return 'SVRrsp'
    if cmd == 0x86:
        return f'GTrsp(n={p[6]})'
    if cmd == 0xA1:
        err = p[3]
        if err == 0x08:
            return f'SSrsp(dt={u32(p,16):#x})'
        return f'SSrsp(err={ename(err)})'
    if cmd == 0xA8:
        return f'DCrsp(err={ename(p[3])})'
    return f'R{cmd:02X}({len(p)}B)'


# ---------------- timeline + load ----------------
conn_times_json = json.load(open(os.path.join(CAP, 'conn_times.json')))
conn_info_json = {int(k): v for k, v in json.load(open(os.path.join(CAP, 'conn_info.json'))).items()}

tl = collections.defaultdict(lambda: {'C2S': [], 'S2C': []})
tlre = re.compile(r'^\[(\d\d:\d\d:\d\d\.\d+)\] CONN(\d+) (C2S|S2C) (.*)$')
for line in open(os.path.join(CAP, 'timeline.log'), encoding='utf-8', errors='replace'):
    m = tlre.match(line.strip())
    if not m:
        continue
    t, cn, dr, rest = m.group(1), int(m.group(2)), m.group(3), m.group(4)
    if 'head=' in rest:
        tl[cn][dr].append(t)

nums = sorted(conn_info_json)
print(f'connections: {len(nums)}, range {nums[0]}..{nums[-1]}')

conn = {}
anom = []
for n in nums:
    try:
        c2s, n1 = parse_bin(os.path.join(CAP, f'c2s_{n}.bin'))
        s2c, n2 = parse_bin(os.path.join(CAP, f's2c_{n}.bin'))
        conn[n] = {
            'info': conn_info_json[n], 'c2s': c2s, 's2c': s2c,
            't_c2s': tl[n]['C2S'], 't_s2c': tl[n]['S2C'], 'note': (n1, n2),
            'sc2s': [sig_c2s(p) for p in c2s], 'ss2c': [sig_s2c(p) for p in s2c],
        }
    except Exception as ex:  # noqa
        anom.append(f'CONN{n}: {ex}')

rep = []


def R(s=''):
    rep.append(s)


# ---------------- aggregates ----------------
ACTIONS = [
    ('18:59:23.7', 'MCD Basic Client restart (enumeration begins)'),
    ('19:02:36.2', 'Click "Change"'),
    ('19:03:07.8', 'Click Core 0 "Select" -> session'),
    ('19:03:38.0', 'Click "Read" (addr 0) -> failure popup'),
    ('19:04:03.9', 'Close error popup'),
    ('19:04:06.6', 'Click "Core Info"'),
    ('19:04:32.8', 'Close Core Info'),
    ('19:04:35.5', 'Click "Help"'),
    ('19:05:02.6', 'Close Help'),
    ('19:05:05.3', 'Launch MCDS Trace Viewer'),
    ('19:07:05.9', 'Device>Connect click attempt (missed)'),
    ('19:07:43.9', 'Alt+D -> Target Select opens'),
    ('19:08:12.9', 'Click target "Info"'),
    ('19:11:36.4', 'Click target "Select"'),
    ('19:12:46.3', 'Close frequency notice'),
    ('19:13:14.7', 'MCDS>General (Ctrl+G)'),
    ('19:13:37.9', 'Alt+R (Start Recording)'),
    ('19:15:02.8', 'Close "No trace data" / stop proxy'),
]

R('== per-window connections (new conns within action +/-2s) ==')
for at, desc in ACTIONS:
    R(f'-- {at} {desc}')
    lo = at[:6] + f'{max(0, int(at[6:8]) - 2):02d}'
    hi = at[:6] + f'{min(59, int(at[6:8]) + 3):02d}'
    for n in nums:
        t = conn_times_json.get(str(n), ['', ''])[0]
        if lo[:-2] == t[:-6] and lo[3:5] == t[3:5]:
            hms = t[:8]
            if lo <= hms <= hi:
                inf = conn_info_json[n]
                R(f'     {t}  CONN{n:<5d} {inf["name"]!r} c2s={inf["c2s"]}B msgs={inf["c2s_msgs"]}')
R('')

# session start census
ss_list = []
for n in nums:
    c = conn[n]
    rqs = [p for p in c['c2s'] if len(p) > 1 and p[1] == 0xA1]
    rsps = [p for p in c['s2c'] if len(p) > 1 and p[1] == 0xA1]
    for i, (rq, rp) in enumerate(zip(rqs, rsps)):
        t = c['t_s2c'][c['s2c'].index(rp)] if rp in c['s2c'] and len(c['t_s2c']) == len(c['s2c']) else ''
        err = rp[3]
        dt = u32(rp, 16) if err == 0x08 and len(rp) >= 20 else 0
        ss_list.append((str(t), n, err, dt, len(rqs), len(rsps)))
err_cnt = collections.Counter(x[2] for x in ss_list)
R(f'== SESSION_START census: {len(ss_list)} attempts ==')
R('  err histogram: ' + ', '.join(f'{ename(k)}={v}' for k, v in err_cnt.most_common()))
R('  first 8:')
for x in ss_list[:8]:
    R(f'    {x[0]} CONN{x[1]} err={ename(x[2])} devtype={x[3]:#x} rq/rsp={x[4]}/{x[5]}')
R('  last 4:')
for x in ss_list[-4:]:
    R(f'    {x[0]} CONN{x[1]} err={ename(x[2])} devtype={x[3]:#x} rq/rsp={x[4]}/{x[5]}')
odd = [x for x in ss_list if x[2] != 0x08]
R(f'  non-OK attempts: {len(odd)}')
for x in odd[:12]:
    R(f'    {x[0]} CONN{x[1]} err={ename(x[2])} rq/rsp={x[4]}/{x[5]}')
R('')

# GET_TARGETS census
gt = collections.Counter()
for n in nums:
    for p in conn[n]['s2c']:
        if len(p) > 1 and p[1] == 0x86 and len(p) >= 8:
            key = f'n={p[6]} v0={cstr(p[8:72])!r} dt={u32(p,72) if p[6] else 0:#x}'
            gt[key] += 1
R('== GET_TARGETS responses ==')
for k, v in gt.most_common(10):
    R(f'  x{v}  {k}')
R('')

# anomaly census
anoms = collections.Counter()
samples = {}
for n in nums:
    c = conn[n]
    for i, p in enumerate(c['s2c']):
        if len(p) < 4 or p[1] not in (0x80, 0x86, 0xA1, 0xA8) and p[1] not in (0xB0, 0xB1) and p[1] != 0xEE:
            continue
        if p[1] == 0xEE:
            anoms[('TAS_ERROR', n)] += 1
            continue
        if p[1] == 0xB0:
            for e in rsp_elems(p):
                if len(e) >= 4 and e[1] in RD_BITS and e[3] not in (0x80,):
                    anoms[('PL0_RD', ename(e[3]))] += 1
                    samples.setdefault(('PL0_RD', ename(e[3])), f'CONN{n} {full_s2c(p)[:120]}')
                if len(e) >= 4 and e[1] == 0xB0 and e[3] not in (0x08,):
                    anoms[('PL0_START', ename(e[3]))] += 1
        elif len(p) >= 4 and p[3] not in (0x08, 0x80):
            anoms[('PL1', ename(p[3]))] += 1
            samples.setdefault(('PL1', ename(p[3])), f'CONN{n} {full_s2c(p)[:120]}')
R('== anomaly census (err != OK) ==')
for k, v in anoms.most_common(20):
    R(f'  x{v:<6d} {k[0]} {k[1]}   sample: {samples.get(k, "")}')
if anom:
    R('parse anomalies: ' + '; '.join(anom[:5]))
R('')

# per-name flow census
def flow_of(c):
    seen = []
    npl0 = 0
    for p in c['c2s']:
        cmd = p[1] if len(p) > 1 else -1
        if cmd in (0xB0, 0xB1):
            npl0 += 1
            k = 'PL0'
        else:
            k = PL1_NAMES.get(cmd, f'C{cmd:02X}')
        if k not in seen:
            seen.append(k)
    s = '+'.join(seen)
    if npl0:
        s += f' x{npl0}'
    return s
by_name = collections.defaultdict(lambda: collections.Counter())
for n in nums:
    by_name[conn_info_json[n]['name']][flow_of(conn[n])] += 1
R('== flows per client name ==')
for nm, ctr in by_name.items():
    R(f'-- {nm!r} total conns={sum(ctr.values())}')
    for f, v in ctr.most_common(12):
        R(f'     x{v:<6d} {f}')
R('')

# MCD churn cadence + activity after 19:07:44
mcd = [n for n in nums if conn_info_json[n]['name'] == 'MCD API (legacy devices)']
mcd_dialog = [n for n in mcd if conn_times_json[str(n)][0] <= '19:03:07.9']
tsecs = []
for n in mcd_dialog:
    t = conn_times_json[str(n)][0]
    tsecs.append(int(t[3:5]) * 60 + int(t[6:8]) + int(t[9:12]) / 1000.0)
tsecs.sort()
gaps = [round(b - a, 3) for a, b in zip(tsecs, tsecs[1:])]
R(f'== MCD churn: {len(mcd_dialog)} conns during 18:59:24-19:03:07 (={len(mcd_dialog)/223:.1f}/s) ==')
R(f'  inter-conn gap min/med/max = {gaps[0]}/{gaps[len(gaps)//2]}/{gaps[-1]} s')
after = [n for n in mcd if conn_times_json[str(n)][0] > '19:07:44']
R(f'== MCD conns created after 19:07:44: {len(after)} ==')
for n in after[:10]:
    R(f'   {conn_times_json[str(n)][0]} CONN{n} c2s={conn_info_json[n]["c2s"]}B')
R('')

# activity buckets (10 s)
buckets = collections.defaultdict(collections.Counter)
for n in nums:
    t = conn_times_json[str(n)][0]
    b = t[:6] + f'{int(t[6:8]) // 10 * 10:02d}'
    buckets[b][conn_info_json[n]['name']] += 1
R('== new-conn buckets (10 s) ==')
for b in sorted(buckets):
    cc = buckets[b]
    R(f'  {b}  ' + '  '.join(f'{k.split()[0][:5]}={v}' for k, v in cc.most_common()))
R('')

open(os.path.join(OUT, 'summary.txt'), 'w', encoding='utf-8').write('\n'.join(rep) + '\n')

# ---------------- conn_list.txt ----------------
lines = []
for n in nums:
    c = conn[n]
    inf = c['info']
    t0, t1 = conn_times_json[str(n)]
    errs = []
    for p in c['s2c']:
        if len(p) > 3 and p[1] in (0x80, 0x86, 0xA1, 0xA8) and p[3] not in (0x08,):
            errs.append(ename(p[3]))
    el = (' errs=' + ','.join(sorted(set(errs)))) if errs else ''
    lines.append(f'CONN{n:<5d} {t0}..{t1} {inf["name"]!r} c2s={inf["c2s"]}B/{len(c["c2s"])}m '
                 f's2c={len(c["s2c"])}m [{flow_of(c)}]{el}')
open(os.path.join(OUT, 'conn_list.txt'), 'w', encoding='utf-8').write('\n'.join(lines) + '\n')

# ---------------- detail files ----------------
def fmt_time(c, dr, i):
    arr = c['t_c2s'] if dr == 'C2S' else c['t_s2c']
    return arr[i] if i < len(arr) else ''


def write_detail(n):
    c = conn[n]
    out = [f'CONN{n} name={c["info"]["name"]!r} c2s={len(c["c2s"])}m s2c={len(c["s2c"])}m notes={c["note"]}']
    if len(c['c2s']) <= 200:
        out.append('-- C2S messages --')
        for i, p in enumerate(c['c2s']):
            out.append(f'[{fmt_time(c, "C2S", i)}] #{i:<4d} {len(p) + 4:>5d}B  {full_c2s(p)}')
        out.append('-- S2C messages --')
        for i, p in enumerate(c['s2c']):
            out.append(f'[{fmt_time(c, "S2C", i)}] #{i:<4d} {len(p) + 4:>5d}B  {full_s2c(p)}')
    else:
        ct = collections.Counter(c['sc2s'])
        first = {}
        for i, s in enumerate(c['sc2s']):
            first.setdefault(s, i)
        out.append('== c2s signature frequency (first-occurrence order) ==')
        for s in sorted(ct, key=lambda x: first[x]):
            k = ct[s]
            i = first[s]
            out.append(f'  x{k:<6d} first@{i} ({fmt_time(c, "C2S", i)})  {s}')
            if k <= 3:
                out.append(f'          full: {full_c2s(c["c2s"][i])[:300]}')
        out.append('')
        out.append('== paired s2c values per c2s signature ==')
        aligned = len(c['c2s']) == len(c['s2c'])
        out.append(f'  aligned={aligned} (c2s[i] <-> s2c[i])')
        if aligned:
            for s in sorted(ct, key=lambda x: first[x]):
                vals = collections.Counter(
                    c['ss2c'][i] for i, s2 in enumerate(c['sc2s']) if s2 == s)
                out.append(f'  SIG {s}  (x{ct[s]}) values:')
                for v, vk in vals.most_common(6):
                    out.append(f'      x{vk:<6d} {v}')
    open(os.path.join(OUT, f'detail_CONN{n}.txt'), 'w', encoding='utf-8').write('\n'.join(out) + '\n')


sel = {3341, 3342, 4085, 424, 425, 426, 1047}
for n in nums:
    if conn_info_json[n]['name'] in ('con_info',):
        sel.add(n)
    t = conn_times_json[str(n)][0]
    for a, b in [('19:03:06', '19:03:12'), ('19:03:36', '19:03:44'), ('19:04:04', '19:04:35'),
                 ('19:05:04', '19:05:12'), ('19:08:10', '19:08:16'), ('19:11:35', '19:11:39'),
                 ('19:12:44', '19:12:56'), ('19:13:35', '19:13:45')]:
        if a <= t <= b:
            sel.add(n)
ts_small = [n for n in nums if conn_info_json[n]['name'] == 'Target Select' and conn_info_json[n]['c2s'] < 400]
if ts_small:
    sel.add(ts_small[0])

for n in sorted(sel):
    try:
        write_detail(n)
    except Exception as ex:  # noqa
        print(f'detail error CONN{n}: {ex}')

print('wrote summary.txt, conn_list.txt, detail_CONN*.txt for', len(sel), 'conns')
print('---- summary head ----')
print('\n'.join(rep[:60]))
print('---- summary tail ----')
print('\n'.join(rep[-30:]))
