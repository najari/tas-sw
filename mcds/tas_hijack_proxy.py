"""TAS hijack proxy: bind 127.0.0.1:24817 over tas_server's 0.0.0.0:24817
(Windows SO_REUSEADDR specific-bind hijack), relay to real server via LAN IP.
Logs c2s/s2c streams per connection. No admin required (hopefully).
"""
import socket, threading, sys, time, os

LISTEN_IP = "127.0.0.1"
LISTEN_PORT = 24817
BACKEND_IP = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
BACKEND_PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 24817
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tas_capture")
os.makedirs(OUT, exist_ok=True)

log_lock = threading.Lock()
conn_no = 0

def log(line):
    ts = time.strftime("%H:%M:%S") + f".{int(time.time()*1000)%1000:03d}"
    with log_lock:
        print(f"[{ts}] {line}", flush=True)
        with open(os.path.join(OUT, "timeline.log"), "a", encoding="utf-8") as f:
            f.write(f"[{ts}] {line}\n")

def pipe(src, dst, tag, cf):
    total = 0
    try:
        while True:
            data = src.recv(65536)
            if not data:
                # forward FIN so the peer sees a clean close
                try:
                    dst.shutdown(socket.SHUT_WR)
                except OSError:
                    pass
                break
            cf.write(data); cf.flush()
            total += len(data)
            log(f"CONN{conn_no} {tag} {len(data)}B head={data[:24].hex()}")
            dst.sendall(data)
    except OSError as e:
        log(f"CONN{conn_no} {tag} pipe end: {e}")
        for s in (src, dst):
            try:
                s.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
    finally:
        log(f"CONN{conn_no} {tag} total {total}B")

def handle(client, addr, no):
    try:
        backend = socket.create_connection((BACKEND_IP, BACKEND_PORT), timeout=5)
        backend.settimeout(None)  # never idle-kill relayed connections
    except OSError as e:
        log(f"CONN{no} backend connect failed: {e}")
        client.close()
        return
    log(f"CONN{no} new client {addr} -> backend {BACKEND_IP}:{BACKEND_PORT}")
    client.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    backend.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    with open(os.path.join(OUT, f"c2s_{no}.bin"), "wb") as f1, \
         open(os.path.join(OUT, f"s2c_{no}.bin"), "wb") as f2:
        t1 = threading.Thread(target=pipe, args=(client, backend, "C2S", f1))
        t2 = threading.Thread(target=pipe, args=(backend, client, "S2C", f2))
        t1.start(); t2.start()
        t1.join()
        t2.join(timeout=10)
        if t2.is_alive():
            # force teardown to unblock
            for s in (backend, client):
                try:
                    s.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
            t2.join(timeout=5)
    for s in (client, backend):
        try:
            s.close()
        except OSError:
            pass
    log(f"CONN{no} closed")

def main():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        srv.bind((LISTEN_IP, LISTEN_PORT))
    except OSError as e:
        print(f"BIND FAILED on {LISTEN_IP}:{LISTEN_PORT}: {e}")
        sys.exit(1)
    srv.listen(8)
    print(f"HIJACK OK: listening {LISTEN_IP}:{LISTEN_PORT}, backend {BACKEND_IP}:{BACKEND_PORT}", flush=True)
    while True:
        c, a = srv.accept()
        global conn_no
        conn_no += 1
        threading.Thread(target=handle, args=(c, a, conn_no), daemon=True).start()

if __name__ == "__main__":
    main()
