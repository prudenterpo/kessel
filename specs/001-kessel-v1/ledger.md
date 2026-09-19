# Kessel v1 ledger

## Current handoff

- Base branch: `develop`
- Active vertical: V1 — Stable server baseline
- Status: in review
- Active PR: https://github.com/prudenterpo/kessel/pull/1
- Blockers: none
- Next: review and merge V1, then start V2 and V4 in parallel with the V5 transport/REPL foundation.

## Vertical status

| Vertical | Status | Depends on | PR |
|---|---|---|---|
| V1 — Stable server baseline | In review | — | https://github.com/prudenterpo/kessel/pull/1 |
| V2 — Key-value store | Blocked | V1 | — |
| V3 — Key expiration | Blocked | V2 | — |
| V4 — Pub/Sub | Blocked | V1 | — |
| V5 — CLI | Blocked | V1; finishes after V4 | — |
| V6 — Operations and distribution | Blocked | V1; integrates after V2/V4 | — |
| V7 — Release hardening | Blocked | V2–V6 | — |

## Handoff rule

Update only the current handoff and the affected table row. Record the outcome, blocker, PR link, and next action; do not append a chronological work log.
