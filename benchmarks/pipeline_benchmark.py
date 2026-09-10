#!/usr/bin/env python3
"""Pipelining benchmark for kvstore.

Compares two client strategies against the same server:
  1. Sequential   — send one command, wait for its reply, repeat (round-trip bound).
  2. Pipelined    — send a batch of N commands at once, then read N replies.

The server already supports pipelining: it processes every complete command in
its receive buffer before reading more. Pipelining amortizes the TCP round-trip
across many commands, which is where the big throughput win comes from.
"""

import socket
import sys
import time

HOST = "localhost"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 6399


def connect():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    s.connect((HOST, PORT))
    return s


def bench_sequential(n):
    s = connect()
    start = time.perf_counter()
    for i in range(n):
        s.sendall(f"SET seq:{i} value{i}\r\n".encode())
        s.recv(256)                      # wait for each reply (round-trip per op)
    elapsed = time.perf_counter() - start
    s.close()
    return n / elapsed


def bench_pipelined(n, batch=1000):
    s = connect()
    start = time.perf_counter()
    sent = 0
    while sent < n:
        b = min(batch, n - sent)
        # Fire a whole batch of commands in one shot...
        payload = "".join(f"SET pipe:{sent+i} value{i}\r\n" for i in range(b))
        s.sendall(payload.encode())
        # ...then drain the b replies (each is "OK\n").
        need = b
        buf = b""
        while need > 0:
            chunk = s.recv(65536)
            if not chunk:
                break
            buf += chunk
            got = buf.count(b"\n")
            need = b - got
        sent += b
    elapsed = time.perf_counter() - start
    s.close()
    return n / elapsed


def main():
    # Verify connection.
    try:
        s = connect()
        s.sendall(b"PING\r\n")
        assert b"PONG" in s.recv(64)
        s.close()
    except Exception:
        print(f"ERROR: cannot connect to server on {HOST}:{PORT}")
        print(f"Start it first:  ./build/kvstore -p {PORT}")
        sys.exit(1)

    N = 20000
    print("=" * 60)
    print("  kvstore Pipelining Benchmark")
    print("=" * 60)
    print(f"  Server: {HOST}:{PORT}   Operations: {N} SETs\n")

    seq = bench_sequential(N)
    print(f"  Sequential (1 command / round-trip):  {seq:>10,.0f} ops/sec")

    for batch in (100, 1000):
        pipe = bench_pipelined(N, batch)
        print(f"  Pipelined (batch={batch:>4}):              {pipe:>10,.0f} ops/sec"
              f"   ({pipe/seq:.1f}x)")

    print("=" * 60)


if __name__ == "__main__":
    main()
