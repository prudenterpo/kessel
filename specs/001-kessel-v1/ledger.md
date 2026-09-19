# Kessel v1 ledger

## Current handoff

- Base branch: `develop`
- Active verticals: none
- Status: done
- Active PRs: none
- Blockers: none
- Next: v1 shipped on `master` via https://github.com/prudenterpo/kessel/pull/8. Open a new plan before the next vertical.

## Vertical status

| Vertical | Status | Depends on | PR |
|---|---|---|---|
| V1 — Stable server baseline | Done | — | https://github.com/prudenterpo/kessel/pull/1 |
| V2 — Key-value store | Done | V1 | https://github.com/prudenterpo/kessel/pull/2 |
| V3 — Key expiration | Done | V2 | https://github.com/prudenterpo/kessel/pull/4 |
| V4 — Pub/Sub | Done | V1 | https://github.com/prudenterpo/kessel/pull/3 |
| V5 — CLI | Done | V1; finishes after V4 | https://github.com/prudenterpo/kessel/pull/5 |
| V6 — Operations and distribution | Done | V1; integrates after V2/V4 | https://github.com/prudenterpo/kessel/pull/6 |
| V7 — Release hardening | Done | V2–V6 | https://github.com/prudenterpo/kessel/pull/7 |

## Handoff rule

Update only the current handoff and the affected table row. Record the outcome, blocker, PR link, and next action; do not append a chronological work log.
