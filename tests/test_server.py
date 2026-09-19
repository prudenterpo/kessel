#!/usr/bin/env python3

import os
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


def receive_line(sock):
    data = bytearray()
    while not data.endswith(b"\r\n"):
        data.extend(receive_exact(sock, 1))
    return bytes(data)


def receive_bulk(sock):
    header = receive_line(sock)
    assert header.startswith(b"$") and header.endswith(b"\r\n"), header
    size = int(header[1:-2])
    payload = receive_exact(sock, size)
    assert receive_exact(sock, 2) == b"\r\n"
    return payload


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
            b" SETEX <key> <seconds> <value>\n"
            b" EXPIRE <key> <seconds>\n"
            b" TTL <key>\n"
            b" PUBLISH <channel> <message>\n"
            b" SUBSCRIBE <channel>\n"
            b" UNSUBSCRIBE <channel>\n"
            b" INFO\n"
            b" HELP\n"
        )
        expect(primary, b"HELP\r\n", b"$%d\r\n%s\r\n" % (len(help_text), help_text))
        primary.sendall(b"INFO\r\n")
        info = receive_bulk(primary)
        assert b"connected_clients:1\n" in info, info
        assert b"max_clients:256\n" in info, info
        assert b"total_connections_received:1\n" in info, info
        assert b"total_commands_processed:4\n" in info, info
        assert b"keys:0\n" in info, info
        assert b"channels:0\n" in info, info
        assert b"subscriptions:0\n" in info, info
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

        expect(primary, b"SETEX temporary 30 value\r\n", b"+OK\r\n")
        expect(primary, b"GET temporary\r\n", b"$5\r\nvalue\r\n")
        primary.sendall(b"TTL temporary\r\n")
        ttl_response = bytearray()
        while not ttl_response.endswith(b"\r\n"):
            ttl_response.extend(primary.recv(32))
        assert int(ttl_response[1:-2]) in (29, 30), ttl_response
        expect(primary, b"EXPIRE temporary 60\r\n", b":1\r\n")
        expect(primary, b"EXPIRE missing 60\r\n", b":0\r\n")
        expect(primary, b"EXPIRE temporary 0\r\n", b":1\r\n")
        expect(primary, b"TTL temporary\r\n", b":-2\r\n")
        expect(primary, b"SET persistent value\r\n", b"+OK\r\n")
        expect(primary, b"TTL persistent\r\n", b":-1\r\n")
        expect(primary, b"SETEX bad 0 value\r\n", b"-ERR invalid expire time\r\n")
        expect(primary, b"EXPIRE persistent nope\r\n", b"-ERR invalid expire time\r\n")

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

        subscriber = connect(port)
        publisher = connect(port)
        sockets.extend([subscriber, publisher])
        expect(subscriber, b"SUBSCRIBE room\r\n", b"+OK\r\n")
        expect(subscriber, b"SUBSCRIBE room\r\n", b"+OK\r\n")
        expect(publisher, b'PUBLISH room "Player joined"\r\n', b":1\r\n")
        message = (
            b"*3\r\n"
            b"$7\r\nmessage\r\n"
            b"$4\r\nroom\r\n"
            b"$13\r\nPlayer joined\r\n"
        )
        assert receive_exact(subscriber, len(message)) == message
        expect(subscriber, b"UNSUBSCRIBE room\r\n", b":1\r\n")
        expect(subscriber, b"UNSUBSCRIBE room\r\n", b":0\r\n")
        expect(publisher, b"PUBLISH room ignored\r\n", b":0\r\n")

        expect(subscriber, b"SUBSCRIBE cleanup\r\n", b"+OK\r\n")
        sockets.remove(subscriber)
        subscriber.close()
        time.sleep(0.05)
        expect(publisher, b"PUBLISH cleanup removed\r\n", b":0\r\n")

        slow = connect(port)
        slow.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)
        sockets.append(slow)
        expect(slow, b"SUBSCRIBE slow\r\n", b"+OK\r\n")
        large_message = b"x" * 60000
        dropped = False
        for _ in range(256):
            publisher.sendall(b"PUBLISH slow " + large_message + b"\r\n")
            publish_result = receive_exact(publisher, 4)
            assert publish_result in (b":1\r\n", b":0\r\n"), publish_result
            if publish_result == b":0\r\n":
                dropped = True
                break
        assert dropped, "slow subscriber was not removed"
        expect(publisher, b"PUBLISH slow ignored\r\n", b":0\r\n")

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


def run_max_clients(server):
    port = reserve_port()
    env = os.environ.copy()
    env.update(
        KESSEL_HOST="127.0.0.1",
        KESSEL_PORT=str(port),
        KESSEL_LOG_LEVEL="WARN",
        KESSEL_MAX_CLIENTS="1",
    )
    process = subprocess.Popen(
        [server], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, env=env
    )
    primary = None
    rejected = None
    try:
        primary = wait_until_ready(process, port)
        rejected = connect(port)
        expect_closed(rejected)

        primary.sendall(b"INFO\r\n")
        info = receive_bulk(primary)
        assert b"connected_clients:1\n" in info, info
        assert b"max_clients:1\n" in info, info
        assert b"rejected_connections:1\n" in info, info
    finally:
        if rejected is not None:
            rejected.close()
        if primary is not None:
            primary.close()
        if process.poll() is None:
            process.send_signal(signal.SIGINT)
        _, stderr = process.communicate(timeout=3)

    assert process.returncode == 0, process.returncode
    assert b"[WARN] connection rejected (max-clients=1)" in stderr, stderr


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_server.py /path/to/kessel")
    run(sys.argv[1])
    run_max_clients(sys.argv[1])
