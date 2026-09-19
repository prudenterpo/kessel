# Kessel v1 plan

## Goal

Deliver a small in-memory database and message broker written in C17. It must be useful over TCP, safe under malformed input, easy to run in Docker, and clear enough to serve as a portfolio project.

Persistence is not part of v1.

## Product scope

- Server inspection through `PING`, `ECHO`, `HELP`, and `INFO`.
- Key-value operations through `SET`, `GET`, `DEL`, and `EXISTS`.
- Expiration through `SETEX`, `EXPIRE`, and `TTL`.
- Messaging through `PUBLISH`, `SUBSCRIBE`, and `UNSUBSCRIBE`.
- Interactive `kessel-cli` with history and subscription mode.
- CMake build, automated tests, sanitizers, Docker distribution, benchmarks, and concise documentation.

## Technical boundaries

- C17 with warnings treated as errors.
- Single-threaded `select()` event loop.
- Non-blocking client I/O with per-client input and output buffers.
- Line-based Redis-style protocol with quoted arguments and a 64 KiB request limit.
- Open-addressed hash table with linear probing and resize support.
- Lazy expiration plus a budgeted periodic reaper.
- `libedit` for CLI history.
- Linux and Docker as runtime targets; macOS supported for development.
- No persistence, replication, clustering, authentication, TLS, transactions, Redis wire compatibility, or Windows release support in v1.

## Delivery model

Work is split into vertical PRs. Each vertical has one lead thread, owns its implementation and tests, and may parallelize internal work. A vertical is only complete when its user-visible flow works end to end.

## V1 — Stable server baseline

PR: `fix: stabilize client io`

- Restructure CMake into reusable library, server, and test targets.
- Add CTest and Linux CI.
- Replace blocking line reads with per-client non-blocking buffers.
- Handle partial reads, multiple commands, partial writes, limits, and shutdown.
- Keep `PING`, `ECHO`, and `HELP` working through TCP integration tests.

This vertical is the base for all others and must merge first.

## V2 — Key-value store

PR: `feat: add key value storage`

- Implement the hash table and ownership rules.
- Add `SET`, `GET`, `DEL`, and `EXISTS` end to end.
- Cover insert, replace, collision, delete, tombstone, and resize behavior.

Depends on V1.

## V3 — Key expiration

PR: `feat: add key expiration`

- Add expiration metadata, monotonic time abstraction, lazy expiration, and reaper.
- Add `SETEX`, `EXPIRE`, and `TTL` end to end.
- Test expiration without timing-sensitive sleeps where possible.

Depends on V2.

## V4 — Pub/Sub

PR: `feat: add pubsub`

- Add channel and subscriber registries.
- Add `PUBLISH`, `SUBSCRIBE`, and `UNSUBSCRIBE` end to end.
- Cover fanout, unsubscribe, disconnect cleanup, and slow clients.

Depends on V1. It can be developed in parallel with V2/V3, then rebased before integration.

## V5 — CLI

PR: `feat: add kessel cli`

- Build `kessel-cli` with an interactive REPL and `libedit` command history.
- Parse and display every response type.
- Support subscription mode, Ctrl+C detach, reconnection errors, and `EXIT`.

The transport and REPL can start after V1 while command completion tracks V2–V4.

## V6 — Operations and distribution

PR: `feat: add server operations`

- Add validated arguments, environment fallbacks, max-clients, logging, and `INFO`.
- Add Docker multi-stage build, non-root runtime, Compose example, and smoke test.
- Document build, run, configuration, and command examples.

Starts after V1 and integrates after V2/V4 so metrics are real.

## V7 — Release hardening

PR: `chore: prepare v1 release`

- Add malformed-input and request-limit coverage.
- Run GCC/Clang, ASan/UBSan, static analysis, and concurrency integration cases.
- Add KV and Pub/Sub benchmarks.
- Finish `PROTOCOL.md`, `DESIGN.md`, and README.
- Fix only release-blocking defects found by the checks.

Depends on V2–V6.

## Definition of done

- All documented commands work over persistent TCP connections.
- Unit and integration tests pass in CI.
- ASan/UBSan jobs pass without findings.
- The Docker image starts as a non-root user and passes a TCP smoke test.
- Benchmarks produce repeatable measurements but are not release gates.
- README, `PROTOCOL.md`, and `DESIGN.md` match the implementation.
- The release PR from `develop` to `master` is ready for review.

## Parallel execution

1. Run V1 alone.
2. After V1, run V2, V4, and the V5 transport/REPL in parallel.
3. Run V3 after V2; run V6 alongside it; finish V5 after V4.
4. Run V7 after all feature verticals are integrated.

Shared-file rule: the lead for the active base owns `CMakeLists.txt`, `ks_server.c`, and central dispatch changes. Other leads keep work in new modules and tests, then ask the base lead to integrate shared-file edits. This avoids parallel branches repeatedly resolving the same conflicts.

## PR style

- Short English semantic title and commits: `feat:`, `fix:`, `test:`, `docs:`, or `chore:`.
- Keep each PR focused on one vertical.
- Description contains only `What changed` and `How I tested`, with short bullets.
- Feature PRs target `develop` and stay open for review; no automatic merge.
- `master` only receives release PRs from `develop` when a version is ready.
