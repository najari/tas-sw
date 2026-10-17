"""Reconnaissance of the native-tool TAS hijack capture.

- census of timeline.log line kinds (validate parser assumptions)
- per-connection client name (from first C2S SERVER_CONNECT)
- size stats per client name
- top connections by size with time ranges
- saves conn_times.json / conn_info.json for the detailed decoder
"""
import os
import re
import struct
import json
import collections

CAP = r'C:\work\tas_server_analysis\mcds\tas_capture'

# ---------- 1) timeline census ----------
pat = re.compile(r'^\[(\d\d:\d\d:\d\d\.\d+)\] (.*)$')
kinds = collections.Counter()
examples = []
conn_times = {}
for line in open(os.path.join(CAP, 'timeline.log'), encoding='utf-8', errors='replace'):
    m = pat.match(line.rstrip('\n'))
    if not m:
        kinds['NO_TS'] += 1
        if len(examples) < 8:
            examples.append(line[:140])
        continue
    t, rest = m.groups()
    if rest.startswith('CONN'):
        head, _, body = rest.partition(' ')
        numpart = head[4:]
        if not numpart.isdigit():
            kinds['CONN_NONNUM'] += 1
            continue
        cn = int(numpart)
        tm = conn_times.setdefault(cn, [t, t])
        tm[1] = t
        if body.startswith('new client'):
            kinds['newclient'] += 1
        elif body.startswith('C2S'):
            kinds['c2s'] += 1
        elif body.startswith('S2C'):
            kinds['s2c'] += 1
        else:
            k = 'other:' + body[:24]
            kinds[k] += 1
            if len(examples) < 12:
                examples.append(rest[:140])
    else:
        kinds['nonconn'] += 1
        if len(examples) < 12:
            examples.append(rest[:140])

print('=== timeline line kinds ===')
for k, v in kinds.most_common():
    print(f'  {k}: {v}')
print('unrecognized/other examples:')
for e in examples:
    print('   ', e)

# ---------- 2) per-connection client name ----------
names = collections.Counter()
conn_info = {}
for fn in os.listdir(CAP):
    if not fn.startswith('c2s_') or not fn.endswith('.bin'):
        continue
    n = int(fn[4:-4])
    d = open(os.path.join(CAP, fn), 'rb').read()
    nm = '?'
    first_cmd = -1
    nmsg = 0
    off = 0
    while off + 4 <= len(d):
        size = struct.unpack_from('<I', d, off)[0]
        if size < 4 or off + size > len(d):
            nm = f'BADFRAME@{off}'
            break
        if off == 0:
            p = d[4:size]
            first_cmd = p[1] if len(p) > 1 else -1
            if len(p) >= 36 and p[1] == 0x80:
                nm = p[4:36].split(b'\0')[0].decode(errors='replace')
            else:
                nm = f'first_cmd=0x{first_cmd:02x}'
        off += size
        nmsg += 1
    conn_info[n] = dict(name=nm, c2s=len(d), c2s_msgs=nmsg, first_cmd=first_cmd)
    names[nm] += 1
    if off != len(d):
        conn_info[n]['c2s_trailing'] = len(d) - off

print()
print('=== client names (by first C2S message) ===')
for nm, c in names.most_common(50):
    print(f'  {c:5d}  {nm!r}')

# ---------- 3) size stats per name ----------
by = collections.defaultdict(list)
for n, info in conn_info.items():
    by[info['name']].append(info['c2s'])
print()
print('=== per-name c2s size stats ===')
for nm, arr in sorted(by.items(), key=lambda kv: -len(kv[1])):
    a = sorted(arr)
    print(f'  {nm!r}: n={len(a)} min={a[0]} med={a[len(a) // 2]} max={a[-1]} total={sum(a)}')

# ---------- 4) top connections ----------
print()
print('=== top 25 conns by c2s size ===')
for n, info in sorted(conn_info.items(), key=lambda kv: -kv[1]['c2s'])[:25]:
    t = conn_times.get(n, ['?', '?'])
    print(f"  CONN{n:<5d} c2s={info['c2s']:<8d} msgs={info['c2s_msgs']:<6d} {t[0]}..{t[1]}  {info['name']!r}")

print()
print('=== conns with c2s >= 900 B ===')
for n, info in sorted(conn_info.items()):
    if info['c2s'] >= 900:
        t = conn_times.get(n, ['?', '?'])
        print(f"  CONN{n:<5d} c2s={info['c2s']:<8d} msgs={info['c2s_msgs']:<6d} {t[0]}..{t[1]}  {info['name']!r}")

json.dump({str(k): v for k, v in conn_times.items()},
          open(os.path.join(CAP, 'conn_times.json'), 'w'))
json.dump({str(k): v for k, v in conn_info.items()},
          open(os.path.join(CAP, 'conn_info.json'), 'w'))
print('saved conn_times.json / conn_info.json')
