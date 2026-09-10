#!/usr/bin/env python3
"""Benchmark suite for kvstore - Multithreaded In-Memory Key-Value Store"""

import socket
import threading
import time
import sys
import statistics

DEFAULT_HOST = "localhost"
DEFAULT_PORT = 6399

class KVClient:
    def __init__(self, host, port):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((host, port))
        self.sock.settimeout(5.0)
    
    def send_command(self, cmd):
        self.sock.sendall((cmd + "\r\n").encode())
        return self.sock.recv(4096).decode().strip()
    
    def close(self):
        self.sock.close()

def benchmark_single_thread_set(host, port, num_ops):
    """Benchmark: Single-threaded SET operations"""
    client = KVClient(host, port)
    
    start = time.perf_counter()
    for i in range(num_ops):
        client.send_command(f"SET bench_st_key{i} value{i}")
    elapsed = time.perf_counter() - start
    
    client.close()
    return num_ops / elapsed

def benchmark_single_thread_get(host, port, num_ops):
    """Benchmark: Single-threaded GET operations (keys must exist)"""
    client = KVClient(host, port)
    
    # Pre-populate
    for i in range(num_ops):
        client.send_command(f"SET bench_stg_key{i} value{i}")
    
    start = time.perf_counter()
    for i in range(num_ops):
        client.send_command(f"GET bench_stg_key{i}")
    elapsed = time.perf_counter() - start
    
    client.close()
    return num_ops / elapsed

def benchmark_multi_thread(host, port, num_threads, ops_per_thread, command_type="SET"):
    """Benchmark: Multi-threaded operations"""
    barrier = threading.Barrier(num_threads + 1)
    results = [0.0] * num_threads
    
    def worker(thread_id):
        client = KVClient(host, port)
        
        # Pre-populate for GET test
        if command_type == "GET":
            for i in range(ops_per_thread):
                client.send_command(f"SET mt_{thread_id}_{i} val{i}")
        
        barrier.wait()  # Synchronize start
        
        start = time.perf_counter()
        for i in range(ops_per_thread):
            if command_type == "SET":
                client.send_command(f"SET mt_{thread_id}_{i} value{i}")
            else:
                client.send_command(f"GET mt_{thread_id}_{i}")
        elapsed = time.perf_counter() - start
        
        results[thread_id] = ops_per_thread / elapsed
        client.close()
    
    threads = []
    for t in range(num_threads):
        th = threading.Thread(target=worker, args=(t,))
        threads.append(th)
        th.start()
    
    barrier.wait()  # Release all threads simultaneously
    
    for th in threads:
        th.join()
    
    return sum(results)  # Aggregate throughput

def benchmark_latency(host, port, num_ops):
    """Benchmark: Measure per-operation latency"""
    client = KVClient(host, port)
    latencies_set = []
    latencies_get = []
    
    for i in range(num_ops):
        start = time.perf_counter()
        client.send_command(f"SET lat_key{i} value{i}")
        latencies_set.append((time.perf_counter() - start) * 1_000_000)  # microseconds
    
    for i in range(num_ops):
        start = time.perf_counter()
        client.send_command(f"GET lat_key{i}")
        latencies_get.append((time.perf_counter() - start) * 1_000_000)
    
    client.close()
    return latencies_set, latencies_get

def benchmark_mixed_workload(host, port, num_threads, ops_per_thread):
    """Benchmark: 80% GET / 20% SET mixed workload"""
    import random
    barrier = threading.Barrier(num_threads + 1)
    results = [0.0] * num_threads
    
    # Pre-populate keys
    client = KVClient(host, port)
    for i in range(1000):
        client.send_command(f"SET mixed_key{i} value{i}")
    client.close()
    
    def worker(thread_id):
        client = KVClient(host, port)
        rng = random.Random(thread_id)
        barrier.wait()
        
        start = time.perf_counter()
        for i in range(ops_per_thread):
            if rng.random() < 0.8:  # 80% reads
                client.send_command(f"GET mixed_key{rng.randint(0, 999)}")
            else:  # 20% writes
                client.send_command(f"SET mixed_key{rng.randint(0, 999)} newval{i}")
        elapsed = time.perf_counter() - start
        
        results[thread_id] = ops_per_thread / elapsed
        client.close()
    
    threads = []
    for t in range(num_threads):
        th = threading.Thread(target=worker, args=(t,))
        threads.append(th)
        th.start()
    
    barrier.wait()
    for th in threads:
        th.join()
    
    return sum(results)

def main():
    host = DEFAULT_HOST
    port = DEFAULT_PORT
    
    if len(sys.argv) > 1:
        port = int(sys.argv[1])
    
    print("=" * 65)
    print("  kvstore Benchmark Suite")
    print("=" * 65)
    print(f"  Server: {host}:{port}")
    print()
    
    # Verify connection
    try:
        test = KVClient(host, port)
        resp = test.send_command("PING")
        assert resp == "PONG", f"Unexpected response: {resp}"
        test.close()
    except Exception as e:
        print(f"ERROR: Cannot connect to server at {host}:{port}")
        print(f"Start server first: ./build/kvstore -p {port}")
        sys.exit(1)
    
    print("--- Throughput Benchmarks ---")
    print()
    
    # Single-threaded SET
    ops_sec = benchmark_single_thread_set(host, port, 1000)
    print(f"  Single-thread SET (1000 ops):     {ops_sec:>10,.0f} ops/sec")
    
    # Single-threaded GET
    ops_sec = benchmark_single_thread_get(host, port, 1000)
    print(f"  Single-thread GET (1000 ops):     {ops_sec:>10,.0f} ops/sec")
    
    # Multi-threaded SET
    for threads in [4, 8]:
        ops_sec = benchmark_multi_thread(host, port, threads, 500, "SET")
        total = threads * 500
        print(f"  {threads}-thread SET ({total} ops):          {ops_sec:>10,.0f} ops/sec")
    
    # Multi-threaded GET
    for threads in [4, 8]:
        ops_sec = benchmark_multi_thread(host, port, threads, 500, "GET")
        total = threads * 500
        print(f"  {threads}-thread GET ({total} ops):          {ops_sec:>10,.0f} ops/sec")
    
    # Mixed workload
    ops_sec = benchmark_mixed_workload(host, port, 8, 500)
    print(f"  8-thread mixed 80/20 (4000 ops): {ops_sec:>10,.0f} ops/sec")
    
    print()
    print("--- Latency Benchmarks ---")
    print()
    
    lat_set, lat_get = benchmark_latency(host, port, 500)
    
    print(f"  SET latency (500 ops):")
    print(f"    p50: {statistics.median(lat_set):>8.0f} μs")
    print(f"    p95: {sorted(lat_set)[int(len(lat_set)*0.95)]:>8.0f} μs")
    print(f"    p99: {sorted(lat_set)[int(len(lat_set)*0.99)]:>8.0f} μs")
    print(f"    avg: {statistics.mean(lat_set):>8.0f} μs")
    print()
    print(f"  GET latency (500 ops):")
    print(f"    p50: {statistics.median(lat_get):>8.0f} μs")
    print(f"    p95: {sorted(lat_get)[int(len(lat_get)*0.95)]:>8.0f} μs")
    print(f"    p99: {sorted(lat_get)[int(len(lat_get)*0.99)]:>8.0f} μs")
    print(f"    avg: {statistics.mean(lat_get):>8.0f} μs")
    
    print()
    print("=" * 65)
    print("  Benchmark complete.")
    print("=" * 65)

if __name__ == "__main__":
    main()
