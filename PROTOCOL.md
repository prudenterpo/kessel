# Kessel protocol

Kessel speaks a line-based, Redis-inspired protocol over a persistent TCP
connection. v1 is not Redis wire compatible.

## Framing

Each request is one line terminated by `\r\n` or `\n`. The server reads until
`EAGAIN`, extracts every complete line, and keeps an incomplete tail in the
client input buffer.

A request body may be at most 64 KiB after the line terminator is removed.
Larger requests receive `-ERR request too large` and the connection is closed
after that error is flushed.

Embedded NUL bytes in a request are rejected with `-ERR invalid command`.
Closing a connection with a partial line queued produces `-ERR incomplete
command` before the server drops the client.

## Arguments

The command name is case-insensitive and stored in uppercase. Keys, values,
channels, and messages are case-sensitive.

Arguments are separated by spaces or tabs. An argument may be wrapped in
double quotes to preserve whitespace. Inside quotes, only `\"` and `\\` are
valid escapes. Unterminated quotes, unknown escapes, a quote in an unquoted
token, or extra text immediately after a closing quote make the request
invalid.

A request may include at most 16 arguments after the command name. Empty
commands, malformed quoting, and excess arguments return `-ERR invalid
command`.

## Responses

| Kind | Form | Meaning |
| --- | --- | --- |
| Simple | `+text\r\n` | Status such as `PONG` or `OK` |
| Error | `-ERR text\r\n` | Client or server error |
| Integer | `:number\r\n` | Counts, TTL, and boolean-like 0/1 results |
| Bulk | `$length\r\ndata\r\n` | Binary-safe payload; length is the data size in bytes |
| Nil | `$-1\r\n` | Missing value |
| Array | `*<n>\r\n` followed by `n` bulk strings | Pub/Sub delivery |

Bulk payloads may contain NUL bytes. The CLI prints them as received.

## Commands

| Command | Arguments | Success response |
| --- | --- | --- |
| `PING` | none | `+PONG` |
| `ECHO` | string | bulk echo of the argument |
| `HELP` | none | bulk command list |
| `INFO` | none | bulk server metrics |
| `SET` | key, value | `+OK` |
| `GET` | key | bulk value or nil |
| `DEL` | key | `:1` if removed, `:0` otherwise |
| `EXISTS` | key | `:1` if present, `:0` otherwise |
| `SETEX` | key, seconds, value | `+OK` |
| `EXPIRE` | key, seconds | `:1` if updated or deleted, `:0` if missing |
| `TTL` | key | remaining whole seconds, `-1` if no expiry, `-2` if missing |
| `SUBSCRIBE` | channel | `+OK` |
| `UNSUBSCRIBE` | channel | `:1` if removed, `:0` if not subscribed |
| `PUBLISH` | channel, message | recipient count |

Wrong argument counts return `-ERR wrong number of arguments to 'NAME'`.
Unknown names return `-ERR unknown command`.

`SETEX` requires a positive integer expiry. `EXPIRE` accepts any integer;
zero or negative values delete the key. Invalid integers return
`-ERR invalid expire time`.

## Pub/Sub delivery

A published message is pushed to each live subscriber as a three-element
array of bulk strings: event type, channel, then payload. Example:

```text
*3\r\n$7\r\nmessage\r\n$4\r\nroom\r\n$2\r\nhi\r\n
```

`SUBSCRIBE` is idempotent for the same channel. `PUBLISH` returns the number
of subscribers that accepted the message. Slow subscribers that exceed the
output-queue limit are disconnected instead of growing memory without bound.

## Limits

- Request body: 64 KiB
- Arguments after the command: 16
- Default listen address: `0.0.0.0:7070`
- Default client cap: 256, always below `FD_SETSIZE`
