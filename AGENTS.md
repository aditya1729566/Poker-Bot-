# AGENTS.md — PokerZero

## Mission and scope
Build **PokerZero**, a reproducible, competitive **offline heads-up no-limit Texas Hold'em (HUNL)** research agent. Optimize for strategic strength **and scientific validity**, not attractive backtests. The program must never automate play on real-money sites, bypass site controls, or interact with third-party poker clients. No TypeSafe AI Jev integration in this version.

The agent implementing this repository must follow this document. Do not claim results, performance, GTO convergence, or tests that were not actually run. Prefer a correct, measured baseline over a sophisticated but unvalidated model. Keep the core game engine independent of any strategy.

## Non-negotiable principles
1. **Information-set integrity:** an acting policy may observe only its hole cards, public board, stacks, pot, legal actions, positions, public betting history, and public past-hand observations available at decision time. It must never receive opponents' unrevealed cards, undealt board cards, deck order, future actions, terminal rewards, or simulator RNG state.
2. **Strict training/evaluation isolation:** separate training, validation, and final test seeds, deals, opponent instances, and hand histories. Do not tune on final test outcomes. Freeze candidate code and parameters before final evaluation.
3. **No future information:** all features, opponent statistics, and ranges must be computed from information available *before* the action. Historical statistics update only after the relevant action or hand has occurred, never retroactively.
4. **Correctness before optimization:** exhaustive or independently cross-checked tests for hand evaluation and rules; deterministic replay; invariant checks for chip conservation and legal actions.
5. **Honest statistical reporting:** paired seat-swapped evaluation, confidence intervals with the pairing respected, adequate sample sizes, and no cherry-picked opponents, seeds, or checkpoints.
6. **Reproducibility:** version configs, source commits, seeds, trained artifacts, build options, hardware, and environment. Record failures as well as successes.
7. **No hidden shortcuts:** never substitute access to simulator internals for strategic reasoning. Never use opponent's private state for feature engineering, labels at inference time, or training inputs that will not be available in deployment.
8. **Offline-only:** simulation and local bot-vs-bot competition only; no real-money platform automation.

## Game specification and conventions
- Two players, 52-card deck, no jokers, no rake, no antes in initial benchmark.
- Initial benchmark: 100 big blinds effective, blinds 0.5/1 in abstract units; button is small blind and acts first preflop, big blind acts first postflop.
- Support all streets, fold/check/call/bet/raise/all-in, correct minimum full-raise rules, short all-in raises and reopening restrictions, pot accounting, side-pot-safe design, showdown, and split pots.
- A hand is a terminal zero-sum game in net chips; sum of player net payoffs must be zero within integer chip units.
- **Use integer chips**, e.g. 1 big blind = 100 units, so the 0.5 blind is exact. No floating-point accounting of pots.
- Expose a pure or deterministically reproducible transition API: `reset(seed, config)`, `observe(player)`, `legal_actions(state)`, `step(action)`, `terminal_payoffs()`; preserve hidden information boundaries.
- The dealer/deck implementation and the agent must be isolated behind different interfaces; the observation object must be explicitly whitelisted, not produced by serializing full state and deleting fields.
- Keep game variants and action abstractions in versioned configuration files.

## Threat model: leakage, bias, and evaluation failure
Treat every item below as a failure mode requiring an explicit test or audit.

| Risk | Required defense |
|---|---|
| Look-ahead / future-card leakage | Agent observations exclude undealt cards and future board; unit test with alternate future decks sharing identical current observations: action distributions must match with identical policy RNG. |
| Opponent-hole-card leakage | Test identical observations with different opponent hole cards; policy output unchanged. Only revealed cards from *completed* hands may enter future opponent histories. |
| Target / label leakage | No terminal payoff, final board, best-response action, or future betting event in inference features; supervise only with labels explicitly marked as training-only. |
| Train-test contamination | Distinct seeded deal streams, independent opponent evaluation instances, immutable held-out test suites; hash datasets and configs. |
| Seed / benchmark overfitting | Multiple predeclared seeds and opponent styles; hold back final seeds; report mean and uncertainty, not best run. |
| Selection / survivorship bias | Log all runs including failures and weak checkpoints; predeclare checkpoint selection on validation only. |
| Seat / position bias | Equal seat exposure and duplicate deals with swapped positions; report by seat and overall. |
| Card / RNG bias | Uniform Fisher–Yates shuffle with unbiased bounded integer sampling; frequency and duplicate-card tests; seed control. |
| Opponent-model hindsight | Online opponent features must be timestamped and updated causally; no stats computed using later hands. |
| Abstraction bias | Document action/hand bucketing and translation errors; benchmark against off-tree bet sizes and unseen policies. |
| Simulator-specific exploits | Independently test rules; use at least one independent evaluator/reference; vary opponent implementations and configurations. |
| Multiple comparisons / p-hacking | Record number of candidates; use validation for selection and a one-time frozen final test; avoid interpreting uncorrected repeated tests as confirmatory. |
| Nonstationary opponent bias | Separate stationary benchmark from online adaptation experiments; reset histories between independent matches. |
| Variance / false discoveries | Pair duplicate deals, report effect size and uncertainty; estimate sample requirements using observed paired variance. |
| Compute / latency bias | Enforce identical action time budgets and hardware where relevant; count timeouts and illegal actions as failures. |

**Critical subtlety:** paired duplicate evaluation must preserve the same *chance deal* while swapping seats/agent assignments under a documented protocol. Swapping both agents' private hands and seat positions requires careful definition; test that the pairing procedure does not accidentally give an agent the same privileged information or unfair button frequency. A single hand's outcome is not independent of its paired counterpart. Use paired differences (or clustered bootstrap by duplicate-match block), not a naive independent-hand confidence interval.

## Architecture
```text
pokerzero/
├── AGENTS.md
├── README.md
├── CMakeLists.txt
├── cmake/
├── configs/                 # game, action abstractions, experiments
├── include/pokerzero/       # stable public C++ interfaces
├── src/
│   ├── cards/               # deck, card encoding, shuffle
│   ├── evaluator/           # 5/6/7-card evaluator
│   ├── game/                # state, blinds, betting, payouts
│   ├── observation/         # information-set safe view
│   ├── agents/              # random, rule-based, equity, trained
│   ├── equity/              # exact enumeration, Monte Carlo
│   ├── solver/              # CFR, CFR+, MCCFR, abstractions
│   ├── resolving/           # optional bounded subgame solver
│   ├── match/               # tournaments, duplicate deals
│   ├── metrics/             # win rate, uncertainty, exploitability
│   └── cli/
├── python/                  # analysis, plotting, experiments
├── dashboard/               # optional Next.js research UI
├── tests/
│   ├── unit/
│   ├── property/
│   ├── integration/
│   ├── leakage/
│   └── regression/
├── benchmarks/
├── experiments/             # versioned manifests, NOT cherry-picked
├── artifacts/               # checkpoints, reports (large files ignored)
└── docs/                    # rules, algorithms, evaluation, decisions
```

### Agent interfaces
- `GameState` contains all private simulator state; **never** pass it to an agent.
- `Observation` is a restricted, immutable value object containing acting player's hole cards, board so far, public betting actions, stack and pot data, position, legal actions, and previously observed public history.
- `IAgent::act(const Observation&, AgentRng&) -> Action` has no access to game RNG, deck, other private cards, or terminal outcome.
- `IAgent::reset_match()` clears state as required by the experiment; persistent opponent modeling must be explicit, causal, and separately evaluated.
- `IStrategy` returns a probability distribution over *legal* actions; validate finite, nonnegative probabilities summing to one, then sample with independent policy RNG.
- Keep solver training state separate from inference state. A trained policy must run without access to training-only labels or oracle APIs.

## Algorithms and training progression
### Stage 0 — game correctness
Implement cards, unbiased shuffle, integer chip ledger, 5–7-card evaluator, full betting engine, immutable observations, seeded replay, and exhaustive unit/property tests. No AI work until this passes.

### Stage 1 — credible baselines
Implement random-legal, always-check/call, tight-aggressive heuristic, loose-aggressive heuristic, and equity/pot-odds agents. Equity engine supports exact enumeration when feasible and Monte Carlo sampling with explicit confidence/error estimates. Never condition simulations on the *actual* hidden deck; sample only from distributions compatible with the visible cards and an explicitly modeled opponent range.

### Stage 2 — small-game CFR validation
Implement Kuhn Poker first, then Leduc. Implement vanilla CFR and CFR+ with correctly defined counterfactual reach probabilities, regret updates, and reach-weighted average strategies. Validate exploitability against an exact best-response oracle on these small games. Use established analytical properties as regression checks. Do not call a solver converged based only on self-play win rate.

### Stage 3 — scalable solver
Implement external-sampling MCCFR; compare its convergence and wall-clock performance to exact CFR on small games. Measure regret/exploitability versus iterations, CPU time, and memory. Maintain separate train/eval RNG streams. Profile before parallelizing; parallel updates require documented synchronization and reproducibility tradeoffs.

### Stage 4 — restricted HUNL
Start with 100bb heads-up no-limit and a documented, limited action abstraction (e.g. check/fold/call; 33%, 75%, 125% pot bets/raises when legal; all-in). Define pot-relative sizing precisely, legal minimum raises, off-tree action translation, and rounding to integer chips. Use preflop hand abstraction and board-aware postflop bucketing where necessary. Avoid claiming full-game equilibrium from abstract-game convergence.

### Stage 5 — stronger decision-making
Investigate depth-limited subgame resolving, range updates from observed actions, and optional learned value functions. Any value function must be trained using separate data and assessed on held-out public states. Compare each enhancement to the previous frozen baseline through ablation experiments. Opponent exploitation is optional and must not destroy robustness to unseen opponents.

### Stage 6 — competitive offline tournament
Evaluate against fixed and unseen bots, alternate action abstractions, earlier checkpoints, and independently implemented opponents. Publish win rates with confidence intervals, timeouts, illegal-action counts, seat splits, and compute costs.

## Benchmark protocol
- Main metric: **mbb/hand** = `1000 * (net big blinds won / hands)`; also report `bb/100 = mbb/hand / 10`.
- Use paired duplicate matches with balanced button/seat exposure. Unit of resampling is a paired deal block or larger independent match block, not an individual correlated hand.
- Record per-agent net chips, blinds, seat, deck seed, public actions, terminal outcome, and whether showdown cards became public. Protect private cards in policy-visible logs.
- For each opponent, run exploratory 10k–20k hands for debugging; use power/precision analysis from pilot variance to size confirmatory evaluation (potentially hundreds of thousands or millions of hands).
- Estimate paired confidence intervals using a prespecified clustered bootstrap or paired standard-error method. Report CI method, sample unit, number of blocks, and 95% CI. Avoid asserting significance from a point estimate.
- Split experiments into **development**, **validation**, and **final held-out** cohorts. Only validation can choose hyperparameters/checkpoints. Final evaluation is run after freeze; any post-test changes create a new experiment requiring new held-out data.
- Report results against *every* predeclared benchmark bot, not only opponents PokerZero beats.
- Maintain a `benchmarks/manifest.yaml` containing opponent versions, rules, stack sizes, action-time budgets, sample targets, seed allocations, and evaluation scripts.
- Keep zero-sum payoffs consistent; report tie handling and unfinished/timeout hand policy.
- Exploitability: compute exact best response only for tractable small games. For abstract HUNL, label restricted best-response or local tests as **approximations/bounds only when mathematically justified**; do not mislabel them exact exploitability.

### Benchmark opponents
1. Random legal-action bot (smoke test, not meaningful strength).
2. Passive check/call bot (value extraction).
3. Fixed tight-aggressive bot.
4. Fixed loose-aggressive bot.
5. Equity-based heuristic bot.
6. Frozen prior PokerZero checkpoints.
7. Independently implemented solver/bot when available.
8. Unseen out-of-distribution styles and off-tree bet sizes.

### Performance and compute
- Track hands/sec, nodes/sec, evaluator calls/sec, CPU time, peak RSS, checkpoint size, and action latency (p50/p95/p99).
- Set an explicit per-action compute budget in benchmark configs; record overruns, fallbacks, and timeouts.
- Optimize only after correctness: profile hand evaluator, range enumeration, regret tables, memory layout, cache behavior, and parallelism.
- MacBook Air M4 is the initial local development target; do not assume large-memory GPU resources or feasible exact full HUNL solving.

## Test matrix and acceptance gates
### Engine tests
- All 52 cards unique; every legal 5-card rank category and kicker ordering correct; wheel straight, flush, full house, board-playing, and tied showdowns handled.
- Independently compare evaluator to a trusted reference or exhaustive enumerations where practical.
- Blind/button ordering, preflop/postflop actor order, bet sizing, min-raise, short all-in reopening, all-in runout, split pots, and stack exhaustion.
- Property tests: chip conservation, no negative stacks, no duplicate cards, no illegal state transitions, correct pot awards, determinism under fixed seeds.
- Fuzz legal action sequences and validate every intermediate state.

### Leakage tests (mandatory)
- Construct two simulator states with identical acting-player observations but different opponent private cards and future decks. With identical policy RNG and model state, action probabilities and sampled action **must be identical**.
- For any opponent statistic, replay a hand prefix and verify it matches the same prefix of a full match; adding future hands cannot alter earlier features.
- Ensure training/evaluation datasets do not overlap by split identifiers and content hashes; verify no checkpoint trained on final test seeds.
- Audit every agent API, serialization path, and debug hook for access to full `GameState`.
- Explicitly distinguish training-only privileged **targets** (e.g. revealed showdown information used after the hand) from inference inputs; prohibit any inference-time oracle features.

### Solver tests
- Kuhn exact exploitability approaches zero under a documented tolerance; CFR and CFR+ match expected small-game behavior.
- Average strategy computed with appropriate reach weighting; probabilities normalized; regret updates numerically stable.
- Leduc small-game tests against independently implemented best response.
- Fixed-seed reproducibility for single-threaded training; document nondeterminism if parallel.

### Acceptance gates
- **Gate A:** engine rules, invariants, leakage tests, and replay pass.
- **Gate B:** baseline agents finish at least 10k seeded hands without invalid state, with credible evaluation output.
- **Gate C:** small-game CFR passes exact best-response convergence checks.
- **Gate D:** restricted HUNL bot beats weak baselines on held-out paired tests with intervals excluding zero where claimed.
- **Gate E:** stronger PokerZero version improves on a frozen predecessor and is robust to unseen styles; publish complete metrics and limitations.

## CLI contracts (target interfaces)
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure

./build/pokerzero match --agent1 random --agent2 heuristic --hands 10000 --seed 42 --output artifacts/reports/smoke.json
./build/pokerzero train --game kuhn --algorithm cfr_plus --iterations 100000 --seed 42 --output artifacts/strategies/kuhn.bin
./build/pokerzero evaluate --candidate artifacts/strategies/kuhn.bin --game kuhn --metric exploitability
./build/pokerzero benchmark --config configs/benchmark.yaml --output artifacts/reports/benchmark.json
```
Do not expose these as implemented until they exist and pass tests.

## Logging and artifact schema
Every run should include: run ID, UTC timestamp, git commit, dirty-tree flag, build flags, OS/CPU, game rules hash, action abstraction hash, model/checkpoint hash, RNG stream seeds, opponent IDs/versions, training/validation/test split, iteration count, sample count, outcome summary, uncertainty method, runtime, memory, invalid actions, timeouts, and failure reason. Use machine-readable JSON/Parquet and a human-readable Markdown report. Store reproducibility manifests with artifacts.

## Working protocol for coding agents
1. Read `AGENTS.md`, `README.md`, and the current implementation before making changes.
2. For each task, write a short implementation plan and identify relevant invariants, leakage risks, and tests.
3. Implement one bounded milestone at a time. Do not scaffold the entire product with placeholder logic that appears functional.
4. Add or update tests **in the same change**. Prefer independent or property-based oracles to tests that duplicate production logic.
5. Run build, relevant tests, leakage checks, and benchmarks; report exact commands and outcomes. If unable to run, say why.
6. Keep numerical assumptions, abstraction compromises, and known limitations in `docs/decisions.md`.
7. Avoid new dependencies unless justified; pin versions and licenses. Never download opaque strategy models or datasets without provenance.
8. Never silently weaken tests, remove safeguards, change metric definitions, or alter test seeds to improve reported performance.
9. Keep interfaces stable and code modular; prefer explicit types, `const`, RAII, bounded memory, and deterministic seed injection.
10. At the end of each milestone, provide: files changed, tests run, results, unresolved risks, and next recommended task.

## Immediate first task
Implement **Stage 0 only**: repository skeleton, CMake, C++20 cards/deck, integer-chip game state, betting rules, evaluator, observation whitelist, legal actions, deterministic match replay, and comprehensive tests. Build a tiny random-vs-random CLI smoke test. **Do not start CFR, dashboards, neural networks, Jev, or opponent modeling until Gate A passes.**

## Definition of done
PokerZero is successful only when its rules are correct, its policies cannot see hidden or future information, its experiments can be reproduced, its improvements survive held-out paired evaluation, and its limitations are reported candidly. A visually polished interface or a positive backtest is not evidence of poker strength.
