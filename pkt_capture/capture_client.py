"""Capture client: exercises a fixed set of TAS operations through the
proxy (127.0.0.1:24900) so both directions of the plaintext protocol
stream are recorded.

Run AFTER proxy_capture.py is listening.
"""
from PyTAS.PyTAS import TasClientRw, TasClntDco

ADDR, PORT = "127.0.0.1", 24900

c = TasClientRw("PktCaptureDemo")
print("server_connect   :", c.server_connect(ADDR, PORT))

targets, n = c.get_targets()
print("get_targets      :", [(t.identifier, hex(t.device_type)) for t in targets])

print("session_start    :", c.session_start(targets[0].identifier, "PktSession"))
print("device_connect   :", c.device_connect(TasClntDco.TAS_CLNT_DCO_RESET_AND_HALT))

v, r = c.read32(0x70000000)
print("read32 @70000000 :", hex(v), r)
v, r = c.read32(0x70000004)
print("read32 @70000004 :", hex(v), r)

print("write32 @70000000:", c.write32(0x70000000, 0x12345678))
v, r = c.read32(0x70000000)
print("read32 back      :", hex(v), r)

v, r = c.read8(0x70000003)
print("read8  @70000003 :", hex(v), r)

data, r = c.read(0x70000000, 240)
print("read 240 bytes   :", len(data), r)

v, r = c.read32(0xDEADBEEF)
print("read32 @DEADBEEF :", hex(v), r)

# close the connection so the proxy can finish cleanly
del c
print("done")
