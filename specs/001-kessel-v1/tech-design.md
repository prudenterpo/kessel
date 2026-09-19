# Kessel v1 technical design

## Architecture

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

The server remains single-threaded. Modules never read from or write directly to sockets; the event loop owns connections and buffers, while command handlers produce responses or enqueue Pub/Sub messages.

## Module boundaries

- `ks_net`: socket creation, non-blocking mode, accept, recv/send wrappers, and close.
- `ks_protocol`: incremental line framing, quoted argument parsing, request validation, and response formatting.
- `ks_server`: event loop, client lifecycle, input/output buffers, command dispatch, shutdown, and periodic work budgets.
- `ks_ds`: generic open-addressed hash table primitives.
- `ks_kv`: owned string keys/values, CRUD, and expiration metadata.
- `ks_ttl`: monotonic clock access, TTL semantics, and budgeted expiration scanning.
- `ks_pubsub`: channel registry and subscriber membership; client IDs are owned by the server.
- `ks_config`: defaults, environment variables, arguments, and validation.
- `kessel-cli`: client transport, response decoding, REPL, and subscription mode.

Dependencies point inward: data modules may use `ks_ds`, but they must not depend on the server, sockets, or CLI.

## Client I/O

Each client owns a bounded input buffer and an output queue. Accepted sockets are non-blocking.

- Read until `EAGAIN`, extract every complete `\r\n` or `\n` terminated request, and retain the incomplete tail.
- Reject a request above 64 KiB with an error, then close the connection after queued output is flushed.
- Write until the queue is empty or send returns `EAGAIN`.
- Never wait for one client inside the event loop.
- Remove all Pub/Sub memberships before releasing a disconnected client.
- Cap configured clients below `FD_SETSIZE` and reject invalid values at startup.

## Protocol

Commands are case-insensitive; keys, values, channels, and messages are case-sensitive. The parser supports unquoted arguments plus double-quoted arguments with escaped quote and backslash. Malformed quoting, excessive arguments, empty commands, and invalid integer arguments return `-ERR` responses.

Response forms remain:

- Simple: `+text\r\n`
- Error: `-ERR text\r\n`
- Integer: `:number\r\n`
- Bulk: `$length\r\ndata\r\n`
- Nil: `$-1\r\n`

Pub/Sub delivery uses a RESP-style three-element array containing the bulk strings `message`, channel, and message. For example: `*3\r\n$7\r\nmessage\r\n$4\r\nroom\r\n$2\r\nhi\r\n`. Subscription commands return `+OK`; `UNSUBSCRIBE` returns `:1` or `:0`; `PUBLISH` returns the recipient count.

## Memory ownership

- Parsed command arguments point into the client's request buffer and are valid only during dispatch.
- KV copies and owns stored keys and values.
- Hash table entries own their allocations; delete, replacement, expiration, and destroy free them exactly once.
- Pub/Sub owns channel names and membership nodes, never client objects.
- Response bytes are copied into the client's output queue before a handler returns.
- Allocation failure returns an error when possible and must not leave partially mutated state.

## Hash table and TTL

The table uses open addressing, cached hashes, linear probing, and tombstones. It grows above a 0.70 occupied load factor and may rebuild to clear excessive tombstones.

Expiration is stored with the KV entry as an absolute monotonic deadline in milliseconds. Reads and mutations lazily remove expired keys. The server invokes a bounded reaper from the event loop so expiration work cannot starve clients.

`TTL` returns:

- remaining whole seconds for a live expiring key;
- `-1` for a live key without expiration;
- `-2` for a missing or expired key.

## Pub/Sub

The registry maps a channel to a subscriber set. A client cannot hold duplicate subscriptions to the same channel. Publishing queues one message per live subscriber and returns the number of recipients. Slow clients are bounded by the output-queue limit and disconnected instead of allowing unbounded memory growth.

## Build and test seams

CMake builds a reusable `kessel_core` library plus `kessel`, `kessel-cli`, unit tests, integration tests, and benchmarks. Tests use public module interfaces; TCP integration tests start the server on an ephemeral port and exercise real sockets.

The clock used by TTL is injectable in unit tests. Network fault cases use small wrapper seams rather than mocking the full event loop. CI runs the normal build/tests and a separate ASan/UBSan build on Linux.

## Integration ownership

The active base lead owns `CMakeLists.txt`, `ks_server.c`, public shared headers, and dispatcher wiring. Vertical leads add isolated modules and their tests, and coordinate shared-interface changes through that lead. The accepted contracts are recorded here before dependent verticals merge.
