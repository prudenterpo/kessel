#!/usr/bin/env python3

import concurrent.futures
import signal
import socket
import subprocess
import sys
import time


def reserve_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def connect(port):
    sock = socket.create_connection(("127.0.0.1", port), timeout=1)
    sock.settimeout(5)
    return sock


def receive_exact(sock, size):
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise AssertionError(f"connection closed after {len(data)} of {size} bytes")
        data.extend(chunk)
    return bytes(data)


def expect(sock, request, response):
    sock.sendall(request)
    actual = receive_exact(sock, len(response))
    assert actual == response, (actual, response)


def expect_closed(sock):
    try:
        assert sock.recv(1) == b""
    except ConnectionResetError:
        pass


def wait_until_ready(process, port):
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise AssertionError(f"server exited early with {process.returncode}")
        try:
            return connect(port)
        except OSError:
            time.sleep(0.02)
    raise AssertionError("server did not accept connections")


def start_server(server):
    port = reserve_port()
    process = subprocess.Popen(
        [server, "--host", "127.0.0.1", "--port", str(port), "--log", "ERROR"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return process, port


def stop_server(process, sockets):
    for sock in sockets:
        sock.close()
    if process.poll() is None:
        process.send_signal(signal.SIGINT)
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)
    assert process.returncode == 0, process.returncode


def run_malformed(server):
    process, port = start_server(server)
    sockets = []
    try:
        primary = wait_until_ready(process, port)
        sockets.append(primary)

        expect(primary, b"\r\n", b"-ERR invalid command\r\n")
        expect(primary, b"   \t  \r\n", b"-ERR invalid command\r\n")
        expect(primary, b'SET key "unterminated\r\n', b"-ERR invalid command\r\n")
        expect(primary, b'SET key "bad\\nvalue"\r\n', b"-ERR invalid command\r\n")
        expect(primary, b'SET ke"y value\r\n', b"-ERR invalid command\r\n")
        expect(primary, b"PING extra\r\n", b"-ERR wrong number of arguments to 'PING'\r\n")
        expect(primary, b"GET\r\n", b"-ERR wrong number of arguments to 'GET'\r\n")
        expect(primary, b"NOPE\r\n", b"-ERR unknown command\r\n")
        expect(primary, b"PING\0JUNK\r\n", b"-ERR invalid command\r\n")

        too_many = b"SET" + (b" arg" * 17) + b"\r\n"
        expect(primary, too_many, b"-ERR invalid command\r\n")
        expect(primary, b"PING\r\n", b"+PONG\r\n")

        oversized = connect(port)
        sockets.append(oversized)
        oversized.sendall(b"ECHO " + (b"x" * (64 * 1024)) + b"\r\n")
        response = oversized.recv(64)
        assert response.startswith(b"-ERR request too large\r\n"), response
        expect_closed(oversized)

        unterminated = connect(port)
        sockets.append(unterminated)
        unterminated.sendall(b"x" * ((64 * 1024) + 2))
        response = unterminated.recv(64)
        assert response.startswith(b"-ERR request too large\r\n"), response
        expect_closed(unterminated)

        incomplete = connect(port)
        sockets.append(incomplete)
        incomplete.sendall(b"PING")
        incomplete.shutdown(socket.SHUT_WR)
        expected = b"-ERR incomplete command\r\n"
        actual = receive_exact(incomplete, len(expected))
        assert actual == expected, actual
        expect_closed(incomplete)
    finally:
        stop_server(process, sockets)


def kv_worker(port, worker_id, operations):
    sock = connect(port)
    try:
        for i in range(operations):
            key = f"w{worker_id}-{i}".encode()
            value = f"v{worker_id}-{i}".encode()
            expect(sock, b"SET " + key + b" " + value + b"\r\n", b"+OK\r\n")
            expect(
                sock,
                b"GET " + key + b"\r\n",
                b"$%d\r\n%s\r\n" % (len(value), value),
            )
            expect(sock, b"EXISTS " + key + b"\r\n", b":1\r\n")
        return worker_id
    finally:
        sock.close()


def publish_worker(port, count):
    sock = connect(port)
    try:
        for _ in range(count):
            expect(sock, b"PUBLISH room payload\r\n", b":1\r\n")
        return count
    finally:
        sock.close()


def run_concurrency(server):
    process, port = start_server(server)
    sockets = []
    try:
        primary = wait_until_ready(process, port)
        sockets.append(primary)
        expect(primary, b"PING\r\n", b"+PONG\r\n")

        subscriber = connect(port)
        sockets.append(subscriber)
        expect(subscriber, b"SUBSCRIBE room\r\n", b"+OK\r\n")

        workers = 8
        operations = 40
        publishers = 4
        publishes = 25
        with concurrent.futures.ThreadPoolExecutor(max_workers=workers + publishers) as pool:
            kv_futures = [
                pool.submit(kv_worker, port, worker_id, operations)
                for worker_id in range(workers)
            ]
            pub_futures = [
                pool.submit(publish_worker, port, publishes)
                for _ in range(publishers)
            ]
            for future in kv_futures:
                future.result()
            for future in pub_futures:
                assert future.result() == publishes

        expected_messages = publishers * publishes
        received = 0
        message = (
            b"*3\r\n"
            b"$7\r\nmessage\r\n"
            b"$4\r\nroom\r\n"
            b"$7\r\npayload\r\n"
        )
        while received < expected_messages:
            actual = receive_exact(subscriber, len(message))
            assert actual == message, actual
            received += 1

        expect(primary, b"GET w0-0\r\n", b"$4\r\nv0-0\r\n")
        expect(primary, b"GET w7-39\r\n", b"$5\r\nv7-39\r\n")
    finally:
        stop_server(process, sockets)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_hardening.py /path/to/kessel")
    run_malformed(sys.argv[1])
    run_concurrency(sys.argv[1])
