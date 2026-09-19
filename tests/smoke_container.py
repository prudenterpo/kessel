#!/usr/bin/env python3

import argparse
import socket
import subprocess
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


def run(*args, capture_output=False):
    return subprocess.run(
        args,
        check=True,
        capture_output=capture_output,
        text=True,
    )


def published_port(container):
    output = run(
        "docker", "port", container, "7070/tcp", capture_output=True
    ).stdout
    for line in output.splitlines():
        host, separator, port = line.rpartition(":")
        if separator and host in {"127.0.0.1", "0.0.0.0", "::"}:
            return int(port)
    raise AssertionError(f"container has no published TCP port: {output!r}")


def wait_for_ping(port):
    deadline = time.monotonic() + 10
    last_error = None
    while time.monotonic() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=1) as sock:
                sock.sendall(b"PING\r\n")
                response = sock.recv(64)
                if response == b"+PONG\r\n":
                    return
                last_error = AssertionError(f"unexpected response: {response!r}")
        except OSError as error:
            last_error = error
        time.sleep(0.1)
    raise AssertionError(f"container did not answer PING: {last_error}")


def main():
    parser = argparse.ArgumentParser(description="Build and smoke-test Kessel in Docker")
    parser.add_argument("--image", default="kessel:smoke")
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()

    if not args.skip_build:
        run("docker", "build", "--tag", args.image, str(ROOT))

    container = run(
        "docker",
        "run",
        "--detach",
        "--rm",
        "--publish",
        "127.0.0.1::7070",
        args.image,
        capture_output=True,
    ).stdout.strip()

    try:
        uid = run("docker", "exec", container, "id", "-u", capture_output=True).stdout.strip()
        assert uid != "0", "container process is running as root"
        wait_for_ping(published_port(container))
        print(f"container smoke test passed (uid={uid})")
    finally:
        subprocess.run(
            ("docker", "rm", "--force", container),
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )


if __name__ == "__main__":
    main()
