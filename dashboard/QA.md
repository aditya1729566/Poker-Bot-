# Arena acceptance checks

Run after the native harness build and E-004 timed benchmark finish. Record the
exact commands, source commits, browser version, and any failure in the final
milestone report.

1. `npm ci`, `npm run typecheck`, `npm test`, `npm run build` from `dashboard/`.
2. Start the trusted C++/Python adapter with its documented local policy
   allowlist. Start Next on `127.0.0.1:3000` with
   `ARENA_ADAPTER_URL=http://127.0.0.1:8765`.
3. Run `ARENA_INTEGRATION_URL=http://127.0.0.1:3000 npm run test:integration`.
   Confirm a full check/call showdown, state conservation, hidden cards, exact
   replay, stale/illegal action handling, and button alternation.
4. In a browser, play a full human-versus-passive hand. Compare every visible
   legal action, pot, stack, board, and payout to the adapter responses. Check
   that Raise to accepts exact integer chips only within engine limits.
5. Open Study after completion. Confirm replay and policy metadata come from
   the adapter; action probabilities and equity remain unavailable when absent.
   Fold a hand and confirm the opponent's mucked cards never appear in state,
   completed-hand detail, Study, or replay.
6. Open Research with no report directory and with a verified local summary.
   Confirm the empty state, cohort, all opponents, paired intervals, seat splits,
   failures, and source SHA-256. Confirm source bytes do not change.
7. Inspect at 375, 768, 1024, and 1440 px. Use keyboard only: focus order,
   action controls, exact raise entry, hand-matrix arrows, and Study link. Check
   screen-reader labels, 44 px touch targets, reduced motion, and no body-wide
   horizontal overflow.
8. Stop the adapter. Confirm a clear unavailable state and successful reconnect
   when it restarts. Expire a session and confirm the UI can start a new one.

Do not claim the Arena frontend complete until steps 1–5 pass against the real
engine. Keep user-play sessions out of training and benchmark artifacts.
