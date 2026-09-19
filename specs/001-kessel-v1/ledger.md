# Kessel v1 ledger

## Current handoff

- Base branch: `develop`
- Active vertical: V1 — Stable server baseline
- Status: ready to start
- Active PR: none
- Blockers: none
- Next: create the V1 feature branch, implement the stable server baseline, verify it, and open `fix: stabilize client io` against `develop`.

## Vertical status

| Vertical | Status | Depends on | PR |
|---|---|---|---|
| V1 — Stable server baseline | Ready | — | — |
| V2 — Key-value store | Blocked | V1 | — |
| V3 — Key expiration | Blocked | V2 | — |
| V4 — Pub/Sub | Blocked | V1 | — |
| V5 — CLI | Blocked | V1; finishes after V4 | — |
| V6 — Operations and distribution | Blocked | V1; integrates after V2/V4 | — |
| V7 — Release hardening | Blocked | V2–V6 | — |

## Handoff rule

Update only the current handoff and the affected table row. Record the outcome, blocker, PR link, and next action; do not append a chronological work log.
