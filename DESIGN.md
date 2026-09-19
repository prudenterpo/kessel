# Kessel design

Kessel is a single-threaded in-memory key-value store and message broker.
The event loop owns sockets and buffers. Command handlers never read from or
write to the network themselves.

```text
TCP clients
    |
    v
event loop + client buffers
    |
    v
protocol parser -> command dispatcher
                       |-- key-value + TTL
                       |-- Pub/Sub
                       `-- server metrics
```

## Modules

| Module | Responsibility |
| --- | --- |
| `ks_net` | Listen, accept, non-blocking recv/send, close |
| `ks_protocol` | Line framing, quoting, response formatting |
| `ks_server` | Event loop, client lifecycle, dispatch, shutdown |
| `ks_ds` | Open-addressed hash table |
| `ks_kv` | Owned keys/values and expiration metadata |
| `ks_ttl` | Monotonic clock and TTL deadlines |
| `ks_pubsub` | Channel registry and subscriber membership |
| `ks_config` | Defaults, environment, arguments, validation |
| `kessel-cli` | Transport, response decoding, REPL, subscription mode |

Data modules may use `ks_ds`. They must not depend on the server, sockets, or
CLI.

## I/O model

Each client has a bounded input buffer and an output queue. Accepted sockets
are non-blocking.

- Read until `EAGAIN`, split complete lines, keep the incomplete tail.
- Reject a request above 64 KiB, flush the error, then close.
- Write until the queue is empty or send returns `EAGAIN`.
- Never block the event loop on one client.
- Remove Pub/Sub memberships before freeing a disconnected client.
- Reject connections above the configured client cap.

## Memory

Parsed arguments point into the request buffer and are valid only during
dispatch. The key-value store copies keys and values. Hash-table entries own
their allocations; replace, delete, expire, and destroy free them once.
Pub/Sub owns channel names and membership nodes, never client objects.
Handlers copy response bytes into the client output queue before returning.
Allocation failure returns an error when possible and must not leave partial
state.

## Key-value and TTL

The table uses open addressing, cached hashes, linear probing, and
tombstones. It grows above a 0.70 occupied load factor and may rebuild to
clear tombstones.

Expiration is an absolute monotonic deadline in milliseconds. Reads and
mutations lazily drop expired keys. The event loop also runs a budgeted
reaper so expiry work cannot starve clients.

## Pub/Sub

A channel maps to a subscriber set. Duplicate subscriptions are ignored.
Publishing queues one message per live subscriber and returns the accepted
recipient count. Output-queue overflow disconnects the slow client.

## Build and tests

CMake builds `kessel_core`, the server, `kessel-cli`, unit tests, TCP
integration tests, and optional benchmarks. Tests use public module
interfaces. TCP tests start the server on an ephemeral port.

The TTL clock is injectable in unit tests. CI builds with GCC and Clang,
runs ASan/UBSan, and applies clang-tidy. Benchmarks print repeatable
measurements and are not release gates.

## Operations

Configuration precedence is command-line arguments, then `KESSEL_*`
environment variables, then defaults. The server validates host, port, log
level, and client cap before listening. `INFO` reports uptime, clients,
accepted and rejected connections, processed commands, live keys, and
Pub/Sub counts.

v1 does not persist data, replicate, authenticate, or speak the Redis wire
protocol.
