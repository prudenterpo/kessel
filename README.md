# Kessel

Kessel is a small in-memory key-value store and message broker written in C17.
It exposes a line-based TCP protocol and keeps all data in memory.

See [PROTOCOL.md](PROTOCOL.md) for the wire format and [DESIGN.md](DESIGN.md)
for the architecture.

## Build

Requirements: CMake 3.22 or newer, a C17 compiler, and Python 3 for the
integration tests.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/bench_kv
./build/bench_pubsub
```

Sanitized Debug build:

```sh
cmake -S . -B build-sanitizers -DCMAKE_BUILD_TYPE=Debug -DKESSEL_ENABLE_SANITIZERS=ON
cmake --build build-sanitizers --parallel
ctest --test-dir build-sanitizers --output-on-failure
```

Start the server and connect with the CLI:

```sh
./build/kessel
./build/kessel-cli --host 127.0.0.1 --port 7070
```

## Docker

```sh
docker compose up --build
```

The service listens on `localhost:7070`. To use another port:

```sh
KESSEL_PORT=8080 docker compose up --build
```

Build and verify the image, including its non-root runtime user:

```sh
python3 tests/smoke_container.py
```

## Configuration

Command-line arguments override environment variables, which override the
defaults.

| Setting | Environment | Argument | Default |
| --- | --- | --- | --- |
| Listen address | `KESSEL_HOST` | `--host` | `0.0.0.0` |
| Listen port | `KESSEL_PORT` | `--port` | `7070` |
| Log level | `KESSEL_LOG_LEVEL` | `--log` | `INFO` |
| Client limit | `KESSEL_MAX_CLIENTS` | `--max-clients` | `256` |

Log levels are `DEBUG`, `INFO`, `WARN`, and `ERROR`.

## Commands

Commands are case-insensitive. Keys, values, channels, and messages are
case-sensitive. Double quotes preserve spaces in an argument.

```text
PING
ECHO "hello world"
SET user:1 Rodrigo
GET user:1
EXISTS user:1
DEL user:1
SETEX session:1 60 active
EXPIRE session:1 30
TTL session:1
SUBSCRIBE notifications
PUBLISH notifications "build finished"
UNSUBSCRIBE notifications
INFO
HELP
```

Kessel v1 is intentionally ephemeral: restarting the server removes stored
keys and subscriptions. It does not provide authentication, TLS, replication,
or Redis wire compatibility.

Benchmarks print `ops_per_sec` for local comparison. They are not release
gates.
