#!/usr/bin/env python3

import socket
import subprocess
import sys
import threading


def serve(responses, requests):
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    port = listener.getsockname()[1]

    def run():
        connection, _ = listener.accept()
        with connection:
            received = bytearray()
            while len(requests) > 0:
                chunk = connection.recv(4096)
                if not chunk:
                    break
                received.extend(chunk)
                while b"\r\n" in received and requests:
                    line, _, tail = received.partition(b"\r\n")
                    received[:] = tail
                    assert line + b"\r\n" == requests.pop(0)
                    response = responses.pop(0)
                    for byte in response:
                        connection.sendall(bytes([byte]))
        listener.close()

    thread = threading.Thread(target=run)
    thread.start()
    return port, thread


def check_responses(cli):
    requests = [
        b"PING\r\n",
        b"BAD\r\n",
        b"COUNT\r\n",
        b"GET key\r\n",
        b"GET missing\r\n",
    ]
    responses = [
        b"+PONG\r\n",
        b"-ERR unknown command\r\n",
        b":42\r\n",
        b"$11\r\nhello\nworld\r\n",
        b"$-1\r\n",
    ]
    port, thread = serve(responses, requests)
    result = subprocess.run(
        [cli, "-h", "127.0.0.1", "-p", str(port)],
        input="PING\nBAD\nCOUNT\nGET key\nGET missing\nEXIT\n",
        text=True,
        capture_output=True,
        timeout=5,
    )
    thread.join(timeout=2)
    assert not thread.is_alive()
    assert result.returncode == 0, result
    assert result.stdout == (
        "PONG\n(error) ERR unknown command\n(integer) 42\n"
        "hello\nworld\n(nil)\n"
    ), result.stdout
    assert result.stderr == "", result.stderr
    assert requests == [], requests
    assert responses == [], responses


def check_disconnect(cli):
    port, thread = serve([b""], [b"PING\r\n"])
    result = subprocess.run(
        [cli, "-p", str(port)],
        input="PING\n",
        text=True,
        capture_output=True,
        timeout=5,
    )
    thread.join(timeout=2)
    assert not thread.is_alive()
    assert result.returncode != 0, result
    assert "server disconnected" in result.stderr, result.stderr


def check_arguments(cli):
    result = subprocess.run(
        [cli, "-p", "invalid"], capture_output=True, text=True, timeout=5
    )
    assert result.returncode != 0, result
    assert "invalid port" in result.stderr, result.stderr


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_cli.py /path/to/kessel-cli")
    check_responses(sys.argv[1])
    check_disconnect(sys.argv[1])
    check_arguments(sys.argv[1])
