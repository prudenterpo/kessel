#!/usr/bin/env python3

import signal
import socket
import subprocess
import sys
import threading
import time


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
        b"EVENT\r\n",
    ]
    responses = [
        b"+PONG\r\n",
        b"-ERR unknown command\r\n",
        b":42\r\n",
        b"$11\r\nhello\nworld\r\n",
        b"$-1\r\n",
        (
            b"*3\r\n"
            b"$7\r\nmessage\r\n"
            b"$4\r\nroom\r\n"
            b"$2\r\nhi\r\n"
        ),
    ]
    port, thread = serve(responses, requests)
    result = subprocess.run(
        [cli, "-h", "127.0.0.1", "-p", str(port)],
        input="PING\nBAD\nCOUNT\nGET key\nGET missing\nEVENT\nEXIT\n",
        text=True,
        capture_output=True,
        timeout=5,
    )
    thread.join(timeout=2)
    assert not thread.is_alive()
    assert result.returncode == 0, result
    assert result.stdout == (
        "PONG\n(error) ERR unknown command\n(integer) 42\n"
        "hello\nworld\n(nil)\nmessage room hi\n"
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


def check_subscription(cli):
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    port = listener.getsockname()[1]
    message_sent = threading.Event()
    errors = []

    def receive_line(connection):
        data = bytearray()
        while not data.endswith(b"\r\n"):
            chunk = connection.recv(4096)
            if not chunk:
                raise AssertionError("CLI disconnected before command completed")
            data.extend(chunk)
        return bytes(data)

    def run():
        try:
            connection, _ = listener.accept()
            with connection:
                assert receive_line(connection) == b"SUBSCRIBE room\r\n"
                connection.sendall(
                    b"+OK\r\n"
                    b"*3\r\n"
                    b"$7\r\nmessage\r\n"
                    b"$4\r\nroom\r\n"
                    b"$13\r\nPlayer joined\r\n"
                )
                message_sent.set()
                assert receive_line(connection) == b"UNSUBSCRIBE room\r\n"
                connection.sendall(b":1\r\n")
        except BaseException as error:
            errors.append(error)
            message_sent.set()
        finally:
            listener.close()

    thread = threading.Thread(target=run)
    thread.start()
    process = subprocess.Popen(
        [cli, "-h", "127.0.0.1", "-p", str(port)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    assert process.stdin is not None
    process.stdin.write("SUBSCRIBE room\nEXIT\n")
    process.stdin.flush()
    assert message_sent.wait(timeout=2)
    time.sleep(0.1)
    process.send_signal(signal.SIGINT)
    stdout, stderr = process.communicate(timeout=5)
    thread.join(timeout=2)

    assert not thread.is_alive()
    assert errors == [], errors
    assert process.returncode == 0, (stdout, stderr)
    assert stdout == "OK\nmessage room Player joined\n(integer) 1\n", stdout
    assert stderr == "", stderr


def check_stalled_subscription_interrupt(cli):
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    port = listener.getsockname()[1]
    partial_sent = threading.Event()
    errors = []

    def receive_line(connection):
        data = bytearray()
        while not data.endswith(b"\r\n"):
            chunk = connection.recv(4096)
            if not chunk:
                raise AssertionError("CLI disconnected before command completed")
            data.extend(chunk)
        return bytes(data)

    def run():
        try:
            connection, _ = listener.accept()
            with connection:
                assert receive_line(connection) == b"SUBSCRIBE room\r\n"
                connection.sendall(b"+O")
                partial_sent.set()
                assert receive_line(connection) == b"UNSUBSCRIBE room\r\n"
                time.sleep(2)
        except BaseException as error:
            errors.append(error)
            partial_sent.set()
        finally:
            listener.close()

    thread = threading.Thread(target=run)
    thread.start()
    process = subprocess.Popen(
        [cli, "-h", "127.0.0.1", "-p", str(port)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    assert process.stdin is not None
    process.stdin.write("SUBSCRIBE room\nEXIT\n")
    process.stdin.flush()
    assert partial_sent.wait(timeout=2)
    process.send_signal(signal.SIGINT)
    stdout, stderr = process.communicate(timeout=3)
    thread.join(timeout=3)

    assert not thread.is_alive()
    assert errors == [], errors
    assert process.returncode == 0, (stdout, stderr)
    assert stdout == "", stdout
    assert stderr == "", stderr


def check_real_server_subscription(cli, server):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as reserved:
        reserved.bind(("127.0.0.1", 0))
        port = reserved.getsockname()[1]

    server_process = subprocess.Popen(
        [server, "--host", "127.0.0.1", "--port", str(port), "--log", "ERROR"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    publisher = None
    cli_process = None
    try:
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            try:
                publisher = socket.create_connection(("127.0.0.1", port), timeout=1)
                publisher.settimeout(1)
                break
            except OSError:
                if server_process.poll() is not None:
                    raise AssertionError("server exited before accepting connections")
                time.sleep(0.02)
        assert publisher is not None, "server did not start"

        cli_process = subprocess.Popen(
            [cli, "-h", "127.0.0.1", "-p", str(port)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        assert cli_process.stdin is not None
        cli_process.stdin.write("SUBSCRIBE room\nEXIT\n")
        cli_process.stdin.flush()

        delivered = False
        for _ in range(50):
            publisher.sendall(b"PUBLISH room hello\r\n")
            response = bytearray()
            while not response.endswith(b"\r\n"):
                response.extend(publisher.recv(16))
            if bytes(response) == b":1\r\n":
                delivered = True
                break
            assert bytes(response) == b":0\r\n", response
            time.sleep(0.02)
        assert delivered, "CLI did not subscribe"

        time.sleep(0.1)
        cli_process.send_signal(signal.SIGINT)
        stdout, stderr = cli_process.communicate(timeout=5)
        assert cli_process.returncode == 0, (stdout, stderr)
        assert stdout == "OK\nmessage room hello\n(integer) 1\n", stdout
        assert stderr == "", stderr
    finally:
        if publisher is not None:
            publisher.close()
        if cli_process is not None and cli_process.poll() is None:
            cli_process.kill()
            cli_process.wait(timeout=2)
        if server_process.poll() is None:
            server_process.send_signal(signal.SIGINT)
            server_process.wait(timeout=3)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_cli.py /path/to/kessel-cli /path/to/kessel")
    check_responses(sys.argv[1])
    check_disconnect(sys.argv[1])
    check_arguments(sys.argv[1])
    check_subscription(sys.argv[1])
    check_stalled_subscription_interrupt(sys.argv[1])
    check_real_server_subscription(sys.argv[1], sys.argv[2])
