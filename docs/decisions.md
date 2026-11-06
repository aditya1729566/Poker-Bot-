# Decisions and limitations

## Stage 0 rules (2026-11-05)

- Chip unit: 100 units per big blind. The default 100bb stack is 10,000 units.
- Card ID: `13 * suit + (rank - 2)`, suits clubs, diamonds, hearts, spades. The deck deals two alternating hole-card rounds starting with the button, burns before flop/turn/river, and exposes only the dealt board.
- RNG: PCG32 with the specified multiplier and stream increment in `src/core.cpp`; Fisher–Yates uses 32-bit rejection sampling. Domain-separated `derive_stream_seed` derives hand and policy seeds. No standard-library distribution is used in replay-critical draws.
- The betting engine tracks current-street and total contributions separately. `RaiseTo` means the new total current-street contribution. A raise must reach `current_bet + last_full_raise`, except a player may make a smaller raise by committing their entire effective stack. The maximum legal target is the opponent's effective current-street commitment capacity.
- An all-in call can be for less than the current bet. Unmatched contribution is returned before showdown; payoffs are net of the initial stack and sum to zero.
- For a tied pot with an odd integer chip, the button receives the odd chip. Under the current equal-stack, no-ante heads-up rules, matched contributions make tied pots even; the convention is retained for future variants.
- The five-card evaluator is checked against exhaustive published category frequencies for all 2,598,960 five-card combinations. Six- and seven-card evaluation enumerates every five-card subset and takes the best score.

## Stage 1 baselines (2026-11-05)

- Uniform-range equity excludes only the acting player's hole cards and visible board. The river enumerates all 990 possible opponent hole-card pairs exactly. Earlier streets use independent policy-RNG Monte Carlo: 512 samples for the equity agent, 256 for tight/loose heuristics, with a reported standard error from the sampled outcomes.
- Tight/loose/equity thresholds and bet fractions are fixed in `src/agents.cpp`; they are untrained reference policies, not GTO approximations.
- A duplicate block uses one shuffled deck. In the second hand, the two private-card packets swap deck positions as the button swaps, preserving each agent's packet and the full board. Each block contributes the mean of its two agent-0 outcomes to a paired normal-standard-error interval. This interval is exploratory until a sample size is predeclared from a pilot.
- The match runner applies a 25 ms wall-clock action budget and counts late or illegal proposed actions, substituting check or call to complete the hand. The report includes seat splits and action latency percentiles.

## Current limits

- The CLI's JSON summary does not yet implement the full research run manifest, frozen train/validation/final splits, or complete per-hand logs.
- Baseline match results are exploratory and do not measure solver strength.
- The trusted match runner can inject a deck for tests and, later, paired duplicate matches. Agents cannot access that interface through `IAgent` or `IStrategy`.
- The implementation is single-threaded. Replay is deterministic with the same code/build, seed, button, and actions; cross-version replay compatibility is not promised.

## Stages 2–3 small games (2026-11-05)

- Kuhn has three distinct cards, one card per player, ante 1, and one betting round with one possible bet of 1. Leduc has six cards (two per rank), one private card per player, one public card, ante 1, bet sizes 2 then 4, and at most two raises per round. These rules are fixed in `src/small_games.cpp` and are distinct from the HUNL engine.
- Vanilla CFR uses chance and opponent counterfactual reach for regret, and own reach for average strategy. CFR+ clips cumulative regrets at zero and weights each iteration's average strategy by its iteration index. Both update all infosets from a frozen strategy within an iteration.
- External-sampling MCCFR samples chance and opponent actions while traversing every action of the updated player. Two-player simple averaging updates the sampled opponent's average policy on the other player's regret pass. This follows the [original MCCFR paper](https://proceedings.neurips.cc/paper/2009/file/00411460f7c92d2124a67ea0f4cb5f85-Paper.pdf) and the [OpenSpiel implementation](https://github.com/google-deepmind/open_spiel/blob/master/open_spiel/algorithms/external_sampling_mccfr.cc). This averaging rule is appropriate to this two-player validation; it is not a general multiplayer policy estimator.
- Exploitability is computed by a separate exact information-set best-response traversal. It is reported only for Kuhn and Leduc, never as a full-HUNL exploitability claim.

## Stage 4 restricted Hold'em pilot (2026-11-06)

- Versioned v1 rules and abstraction values are in `configs/game.yaml` and `configs/action_abstraction.yaml`. The CLI reads a deliberately restricted flat-YAML subset and refuses to run if those files differ from the compiled v1 values; changing a rule requires a code and version change.
- Each postflop bucket is one of 32 equal-width uniform-range equity bins, estimated from 64 deterministic independent samples keyed only by visible cards. Preflop uses the 169 rank/suit classes. The bucket estimator never inspects the actual game deck or opponent hand.
- The restricted action set contains fold/check/call as legal, 33%, 75%, and 125% of the pot after calling when each rounded target is a distinct legal raise, plus an effective all-in target. At most two aggressive actions are available per street in the restricted tree. The full game engine retains unrestricted legal betting.
- Public off-tree raises are assigned the nearest of the three pot-size labels, except effective all-ins retain their own label. If an information set or action signature is absent from the trained table, inference uses the fixed equity baseline and counts that fallback.
- The trainer uses external sampling with independent deal and policy streams, a default cap of eight million information sets, and equal button exposure across iterations. Its abstract-game policy has no exact full-HUNL exploitability interpretation. Stage 4 remains experimental until frozen held-out paired tests meet Gate D.
- The first 50,000-iteration development artifact used an XOR-based seed schedule that allowed cohort root seeds to reuse deal inputs. That artifact and its validation report are retained in `artifacts/` with `v1_invalid` / `invalid_seed_overlap` names and are excluded from candidate selection. Version 2 uses domain-separated seed derivation, and tests audit the declared training, validation, and final seed ranges for overlap.
- The benchmark runner uses SHA-256 for source, config, candidate, and report provenance. Its confirmatory sample rule targets a 95% paired CI half-width of at most 500 mbb/hand, with at least the 10,000-hand pilot size, at most two million hands, and the eight-hour per-experiment cap. If a cap prevents that precision, the result is marked inconclusive.
- The first corrected 50,000-iteration candidate's river table had a median of one average-strategy visit per information set, and it lost to all three stronger fixed heuristics on validation. Inference variant v2 therefore falls back to the frozen equity baseline when a table entry has fewer than ten visits. This threshold is a validation-selected robustness change; all earlier results remain recorded, and only a new validation run may select v2 for final testing.

## Frozen Stage 4 outcome (2026-11-06)

- The one-time final seed-44 evaluation of the frozen v2 policy passed Gate D against the predeclared weak random and passive baselines. It lost to tight-aggressive, loose-aggressive, and equity heuristics. Full paired intervals, timeouts, and the interrupted-equity continuation are in [stage4_results.md](stage4_results.md) and the ignored local `artifacts/reports/final_82f4d59e74b6_9458b55612e0/` directory. These results do not establish general strategic competitiveness.

## E-001 diagnostic ablation (2026-11-06)

- The optional `e001_match_diagnostics_v1` observer streams public action context and post-action policy metadata after the acting agent returns. It does not change observations, action choices, RNG draws, or the aggregate mbb/hand calculation. The action log omits hole cards, board cards, deck order, and RNG state. The block log records both candidate returns within each duplicate deal.
- The experiment manifest fixes development roots 51, 52, and 53, the existing equity agent as treatment, the frozen restricted artifact as control, five benchmark opponents, 10,000 hands per arm and opponent and root, and the 25 ms action budget. Each arm is a new process with a fresh opponent instance. No validation or final seeds are used.
- The primary interval uses one value per shared seed-and-block cluster after averaging the three strong-opponent treatment-minus-control differences within that cluster. Per-opponent differences are paired by the same seed and block. Timeouts and illegal actions remain in the outcome under the runner's existing check/call completion rule and are reported explicitly.
- Post-run analysis correction: all 30 match files were complete when the initial analyzer rejected a valid 0.027 mbb/hand difference between exact block arithmetic and C++ JSON displayed to six significant digits. The corrected analyzer checks the last printed decimal place, keeps the original match commit/hash separate from its own commit/hash, and permits only analysis-code/documentation changes after the run. No match, artifact, opponent, manifest, seed, or diagnostic output was rerun or edited.
