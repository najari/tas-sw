#!/usr/bin/env python3
"""Read-only TAS protocol probe for the mcd_tas prototype.

Sends only control messages (SERVER_CONNECT / GET_TARGETS / PING).
No target state is changed.

Usage:
  python tools/probe.py [--host 127.0.0.1] [--port 24817] [--raw] [cmd]
  cmd: server | targets (default) | ping
"""
import argparse
import os
import socket
import struct
import sys


def recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise EOFError("connection closed by server")
        buf += chunk
    return buf


def recv_msg(sock):
    size = struct.unpack("<I", recv_exact(sock, 4))[0]
    if size < 4:
        raise ValueError("bad frame size %d" % size)
    return recv_exact(sock, size - 4)


def send_msg(sock, payload):
    sock.sendall(struct.pack("<I", len(payload) + 4) + payload)


def cstr(b):
    return b.split(b"\0")[0].decode(errors="replace")


def server_connect(sock, name="probe.py", user="admin"):
    p = bytearray(56)
    p[0] = 0x0D          # wl = (56/4)-1
    p[1] = 0x80          # SERVER_CONNECT
    nb = name.encode()[:31]
    ub = user.encode()[:15]
    p[4:4 + len(nb)] = nb
    p[36:36 + len(ub)] = ub
    struct.pack_into("<I", p, 52, os.getpid())
    send_msg(sock, bytes(p))
    r = recv_msg(sock)
    if len(r) < 4 or r[1] != 0x80:
        raise ValueError("unexpected SERVER_CONNECT response")
    err = r[3]
    info = None
    if len(r) >= 136:
        si = r[8:136]
        info = dict(
            server=cstr(si[0:64]),
            ver=(struct.unpack_from("<H", si, 64)[0],
                 struct.unpack_from("<H", si, 66)[0]),
            protocols=struct.unpack_from("<I", si, 68)[0],
            channels=struct.unpack_from("<I", si, 72)[0],
            trace=struct.unpack_from("<I", si, 76)[0],
            date=cstr(si[96:112]),
        )
    return err, info, r


def get_targets(sock):
    send_msg(sock, bytes([0x00, 0x86, 0x00, 0x00]))  # [wl=0][GET_TARGETS][start][res]
    r = recv_msg(sock)
    if len(r) < 8 or r[1] != 0x86:
        raise ValueError("unexpected GET_TARGETS response")
    err = r[3]
    n = r[6]
    targets = []
    for i in range(n):
        t = r[8 + 88 * i: 8 + 88 * (i + 1)]
        if len(t) < 88:
            break
        targets.append(dict(
            identifier=cstr(t[0:64]),
            device_type=struct.unpack_from("<I", t, 64)[0],
            device_id=[struct.unpack_from("<I", t, 68 + 4 * k)[0] for k in range(4)],
            phys=t[84],
            nclient=t[85],
        ))
    return err, targets, r


def ping(sock):
    send_msg(sock, bytes([0x00, 0xA2, 0x00, 0x00]))  # [wl=0][PING][0][0]
    r = recv_msg(sock)
    return (r[3] if len(r) >= 4 else None), r


def main():
    ap = argparse.ArgumentParser(description="read-only TAS protocol probe")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=24817)
    ap.add_argument("--raw", action="store_true", help="also dump raw payload hex")
    ap.add_argument("cmd", nargs="?", default="targets",
                    choices=["server", "targets", "ping"])
    args = ap.parse_args()

    with socket.create_connection((args.host, args.port), timeout=5) as sock:
        err, info, r = server_connect(sock)
        print("SERVER_CONNECT err=0x%02x" % err)
        if info:
            print("  server=%r ver=%d.%d protos=0x%04x chl=0x%04x trc=0x%04x date=%r"
                  % (info["server"], info["ver"][0], info["ver"][1],
                     info["protocols"], info["channels"], info["trace"], info["date"]))
        if args.raw:
            print("  raw:", r.hex(" "))

        if args.cmd == "server":
            return 0

        if args.cmd == "targets":
            err, targets, r = get_targets(sock)
            print("GET_TARGETS err=0x%02x n=%d" % (err, len(targets)))
            for i, t in enumerate(targets):
                print("  [%d] id=%r devtype=0x%08X devid=%s phys=%d nclient=%d"
                      % (i, t["identifier"], t["device_type"],
                         ":".join("%08X" % w for w in t["device_id"]),
                         t["phys"], t["nclient"]))
        elif args.cmd == "ping":
            err, r = ping(sock)
            print("PING err=0x%02x" % (err if err is not None else -1))
        if args.raw:
            print("  raw:", r.hex(" "))
    return 0


if __name__ == "__main__":
    sys.exit(main())
