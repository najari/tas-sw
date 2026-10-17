# -*- coding: utf-8 -*-
"""List PE export names of a DLL (python, no deps)."""
import struct
import sys

def exports(path):
    f = open(path, "rb").read()
    e = struct.unpack_from("<I", f, 0x3C)[0]
    assert f[e:e + 4] == b"PE\x00\x00"
    coff = e + 4
    machine, nsect, _, _, _, optsz, _ = struct.unpack_from("<HHIIIHH", f, coff)
    opt = coff + 20
    magic = struct.unpack_from("<H", f, opt)[0]
    if magic == 0x20B:
        dd = opt + 112
    else:
        dd = opt + 96
    exp_rva, exp_size = struct.unpack_from("<II", f, dd)
    sects = []
    so = opt + optsz
    for i in range(nsect):
        name = f[so:so + 8].rstrip(b"\x00").decode("latin-1")
        vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", f, so + 8)
        sects.append((name, vaddr, vsize, rawptr, rawsize))
        so += 40

    def rva2off(rva):
        for name, vaddr, vsize, rawptr, rawsize in sects:
            if vaddr <= rva < vaddr + max(vsize, rawsize):
                return rawptr + (rva - vaddr)
        return None

    off = rva2off(exp_rva)
    (char, tstamp, vmaj, vmin, name_rva, base, nfunc, nname, addr_funcs,
     addr_names, addr_ord) = struct.unpack_from("<IIHHIIIIIII", f, off)
    names = []
    noff = rva2off(addr_names)
    for i in range(nname):
        nrva = struct.unpack_from("<I", f, noff + i * 4)[0]
        fo = rva2off(nrva)
        names.append(f[fo:].split(b"\x00")[0].decode("latin-1"))
    return names


if __name__ == "__main__":
    for p in sys.argv[1:]:
        ns = exports(p)
        print("%s: %d exports" % (p, len(ns)))
        for n in sorted(ns):
            print("   ", n)
