# Kessel v1 ledger

## Current handoff

- Base branch: `develop`
- Active verticals: V2 — Key-value store; V4 — Pub/Sub; V5 — CLI foundation
- Status: in progress
- Active PRs: none
- Blockers: none
- Next: finish the isolated modules in parallel, integrate each vertical end to end, and open separate PRs against `develop`.

## Vertical status

| Vertical | Status | Depends on | PR |
|---|---|---|---|
| V1 — Stable server baseline | Done | — | https://github.com/prudenterpo/kessel/pull/1 |
| V2 — Key-value store | In progress | V1 | — |
| V3 — Key expiration | Blocked | V2 | — |
| V4 — Pub/Sub | In progress | V1 | — |
| V5 — CLI | In progress | V1; finishes after V4 | — |
| V6 — Operations and distribution | Blocked | V1; integrates after V2/V4 | — |
| V7 — Release hardening | Blocked | V2–V6 | — |

## Handoff rule

Update only the current handoff and the affected table row. Record the outcome, blocker, PR link, and next action; do not append a chronological work log.
