"""TAS protocol capture proxy.

Listens on 127.0.0.1:24900, forwards to the real TasServer on 127.0.0.1:24817,
and records both directions of the plaintext TCP stream:
  - c2s.bin / s2c.bin : raw byte streams
  - timeline.log       : receive timing / chunk sizes

Single-shot: exits after the first connection is closed.
"""
import socket
import threading
import time
import os

OUT = os.path.dirname(os.path.abspath(__file__))
LISTEN = ("127.0.0.1", 24900)
TARGET = ("127.0.0.1", 24817)

timeline = open(os.path.join(OUT, "timeline.log"), "w")
f_c2s = open(os.path.join(OUT, "c2s.bin"), "wb")
f_s2c = open(os.path.join(OUT, "s2c.bin"), "wb")


def pump(src, dst, f, tag):
    while True:
        try:
            buf = src.recv(65536)
        except OSError:
            break
        if not buf:
            break
        f.write(buf)
        f.flush()
        timeline.write("%.6f %s %d\n" % (time.time(), tag, len(buf)))
        timeline.flush()
        try:
            dst.sendall(buf)
        except OSError:
            break
    try:
        dst.shutdown(socket.SHUT_WR)
    except OSError:
        pass


srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(LISTEN)
srv.listen(1)
print("proxy listening on %s:%d -> %s:%d" % (LISTEN + TARGET), flush=True)

conn, addr = srv.accept()
conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
up = socket.create_connection(TARGET)
up.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

t1 = threading.Thread(target=pump, args=(conn, up, f_c2s, "c2s"))
t2 = threading.Thread(target=pump, args=(up, conn, f_s2c, "s2c"))
t1.start()
t2.start()
t1.join(timeout=60)
t2.join(timeout=60)

conn.close()
up.close()
srv.close()
for f in (timeline, f_c2s, f_s2c):
    f.close()
print("proxy done: c2s=%d s2c=%d" % (
    os.path.getsize(os.path.join(OUT, "c2s.bin")),
    os.path.getsize(os.path.join(OUT, "s2c.bin"))), flush=True)
