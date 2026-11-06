# Stage 4 frozen final evaluation

The restricted 100bb Hold'em candidate was trained for 50,000 external-sampling iterations and frozen at Git commit `331ff3da046ad75e0354c4a4752777e4a44e8673`. The policy artifact SHA-256 is `82f4d59e74b674b249c2acd5b2a626373e1b565fbd9741888fb030d8570f5eeb`. The final cohort uses seed 44, separate from training seed 42 and validation seed 43. Four candidate variants were considered before freeze; no changes were selected from final outcomes.

Each sample is a duplicate two-hand deal block with the button swapped and each agent's private-card packet and board retained. The table shows the candidate's rate in milli-big-blinds per hand and a normal-standard-error 95% interval across independent blocks. All five opponents in the frozen manifest are included.

| Opponent | Hands | mbb/hand | Paired 95% interval | Timeouts | Illegal actions | Outcome |
|---|---:|---:|---:|---:|---:|---|
| Random | 48,834 | +5,620.53 | [+5,115.63, +6,125.43] | 3 | 0 | Win; observed half-width 504.90 exceeds 500 target |
| Passive | 103,404 | +6,139.41 | [+5,643.70, +6,635.11] | 2 | 0 | Win |
| Tight-aggressive | 30,746 | −4,093.02 | [−4,585.44, −3,600.61] | 1 | 0 | Loss |
| Loose-aggressive | 77,144 | −2,972.57 | [−3,470.11, −2,475.04] | 1 | 0 | Loss |
| Equity heuristic | 46,720 | −4,254.22 | [−4,746.91, −3,761.53] | 0 | 0 | Loss |

Gate D is met only for the predeclared weak baselines, random and passive: both lower interval bounds exceed zero. The losses to the other three policies show that the restricted candidate is not a competitive general poker agent. The random comparison narrowly misses the prespecified 500 mbb/hand precision target, although its interval still excludes zero.

The original final command was interrupted during the equity matchup before it produced a result. That matchup was restarted once using the same frozen build, artifact, seed, and predeclared 46,720 hands. No interim equity result was available or used for tuning. The complete machine-readable results and per-match latency, memory, seat splits, fallback counts, and report hashes are under `artifacts/reports/final_82f4d59e74b6_9458b55612e0/`; the human-readable `report.md` is in that directory. Artifacts are ignored by Git because of their size and remain on the local machine.

The next gated experiment is a causal range update and bounded one-street resolver. Its first development implementation is isolated in branch `codex/pokerzero-stage5`. It missed the 25 ms action budget on 66 of 1,618 resolver actions in a 1,000-hand pilot against this frozen candidate, so Gate E and Stage 6 are not claimed.
