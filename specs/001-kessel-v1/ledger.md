# Kessel v1 ledger

## Current handoff

- Base branch: `develop`
- Active verticals: V3 — Key expiration; V4 — Pub/Sub; V5 — CLI; V6 — Operations
- Status: in progress
- Active PRs: https://github.com/prudenterpo/kessel/pull/3
- Blockers: none
- Next: merge V4, then rebase and publish the completed dependent verticals.

## Vertical status

| Vertical | Status | Depends on | PR |
|---|---|---|---|
| V1 — Stable server baseline | Done | — | https://github.com/prudenterpo/kessel/pull/1 |
| V2 — Key-value store | Done | V1 | https://github.com/prudenterpo/kessel/pull/2 |
| V3 — Key expiration | Ready | V2 | — |
| V4 — Pub/Sub | In review | V1 | https://github.com/prudenterpo/kessel/pull/3 |
| V5 — CLI | In progress | V1; finishes after V4 | — |
| V6 — Operations and distribution | In progress | V1; integrates after V2/V4 | — |
| V7 — Release hardening | Blocked | V2–V6 | — |

## Handoff rule

Update only the current handoff and the affected table row. Record the outcome, blocker, PR link, and next action; do not append a chronological work log.
