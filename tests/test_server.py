#!/usr/bin/env python3

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
    sock.settimeout(3)
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


def run(server):
    port = reserve_port()
    process = subprocess.Popen(
        [server, "--host", "127.0.0.1", "--port", str(port), "--log", "ERROR"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    sockets = []
    try:
        primary = wait_until_ready(process, port)
        sockets.append(primary)

        expect(primary, b"PING\r\n", b"+PONG\r\n")
        expect(primary, b'ECHO "hello world"\r\n', b"$11\r\nhello world\r\n")
        help_text = (
            b"Kessel commands:\n"
            b" PING\n"
            b" ECHO <string>\n"
            b" SET <key> <value>\n"
            b" GET <key>\n"
            b" DEL <key>\n"
            b" EXISTS <key>\n"
            b" HELP\n"
        )
        expect(primary, b"HELP\r\n", b"$%d\r\n%s\r\n" % (len(help_text), help_text))
        expect(primary, b"NOPE\r\n", b"-ERR unknown command\r\n")
        expect(primary, b"PING\0JUNK\r\n", b"-ERR invalid command\r\n")

        expect(primary, b'SET greeting "hello world"\r\n', b"+OK\r\n")
        expect(primary, b"GET greeting\r\n", b"$11\r\nhello world\r\n")
        expect(primary, b"EXISTS greeting\r\n", b":1\r\n")
        expect(primary, b"SET greeting updated\r\n", b"+OK\r\n")
        expect(primary, b"GET greeting\r\n", b"$7\r\nupdated\r\n")
        expect(primary, b"DEL greeting\r\n", b":1\r\n")
        expect(primary, b"DEL greeting\r\n", b":0\r\n")
        expect(primary, b"EXISTS greeting\r\n", b":0\r\n")
        expect(primary, b"GET greeting\r\n", b"$-1\r\n")

        primary.sendall(b"PING\r\nECHO pipelined\r\n")
        expected = b"+PONG\r\n$9\r\npipelined\r\n"
        assert receive_exact(primary, len(expected)) == expected

        partial = connect(port)
        concurrent = connect(port)
        sockets.extend([partial, concurrent])
        expect(primary, b"SET shared visible\r\n", b"+OK\r\n")
        expect(concurrent, b"GET shared\r\n", b"$7\r\nvisible\r\n")
        partial.sendall(b"PI")
        expect(concurrent, b"PING\r\n", b"+PONG\r\n")
        expect(partial, b"NG\r\n", b"+PONG\r\n")

        half_closed = connect(port)
        sockets.append(half_closed)
        half_closed.sendall(b"PING\r\n")
        half_closed.shutdown(socket.SHUT_WR)
        assert receive_exact(half_closed, 7) == b"+PONG\r\n"
        expect_closed(half_closed)

        boundary = connect(port)
        sockets.append(boundary)
        large_value = b"x" * ((64 * 1024) - len(b"ECHO "))
        boundary.sendall(b"ECHO " + large_value + b"\r\n")
        large_response = b"$%d\r\n%s\r\n" % (len(large_value), large_value)
        assert receive_exact(boundary, len(large_response)) == large_response

        burst = connect(port)
        sockets.append(burst)
        repetitions = 10000
        burst.sendall(b"HELP\r\n" * repetitions)
        one_help = b"$%d\r\n%s\r\n" % (len(help_text), help_text)
        assert receive_exact(burst, len(one_help) * repetitions) == one_help * repetitions

        oversized = connect(port)
        sockets.append(oversized)
        oversized.sendall(b"ECHO " + (b"x" * (64 * 1024)) + b"\r\n")
        response = oversized.recv(128)
        assert response.startswith(b"-ERR request too large\r\n"), response
        expect_closed(oversized)
    finally:
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


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_server.py /path/to/kessel")
    run(sys.argv[1])
