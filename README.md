# PokerZero

PokerZero is an offline heads-up no-limit Texas Hold'em research project. It does not connect to poker sites or third-party clients.

## Implemented

Stage 0 provides a C++20 game engine, uniform seeded shuffle, integer-chip betting, 5–7-card evaluation, explicitly whitelisted observations, and deterministic replay from deck seed and action sequence. Stage 1 adds random, passive, tight-aggressive, loose-aggressive, and equity-based baseline agents. The match runner uses duplicate deals with swapped button positions and reports a paired-block confidence interval. Stages 2–3 add Kuhn and Leduc CFR, CFR+, external-sampling MCCFR, and exact best-response evaluation. Stage 4 has an experimental restricted Hold'em trainer and policy loader. It has not passed Gate D. Resolving and final tournament stages are not implemented.

## Build and test

Requires CMake 3.25 or newer and a C++20 compiler. On macOS with Homebrew, install CMake with `brew install cmake`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/pokerzero match --agent1 random --agent2 random --hands 10000 --seed 42 --output artifacts/reports/smoke.json
./build/pokerzero match --agent1 tight_aggressive --agent2 passive --hands 10000 --seed 42 --output artifacts/reports/baseline.json
./build/pokerzero train --game kuhn --algorithm cfr_plus --iterations 100000 --seed 42 --output artifacts/strategies/kuhn.bin
./build/pokerzero evaluate --candidate artifacts/strategies/kuhn.bin --game kuhn --metric exploitability
./build/pokerzero train --game hunl --algorithm external_sampling_mccfr --iterations 1000 --seed 42 --output artifacts/strategies/hunl_pilot.bin
./build/pokerzero match --agent1 restricted:artifacts/strategies/hunl_pilot.bin --agent2 passive --hands 1000 --seed 43 --cohort validation
python3 python/benchmark.py validation --candidate artifacts/strategies/hunl_pilot.bin
```

The `match` command requires an even hand count: each chance deal is played twice, preserving each agent's private-card packet and board while swapping the button. Available names are `random`, `passive`, `tight_aggressive`, `loose_aggressive`, `equity`, and `restricted:PATH`. The report's 95% interval treats each duplicate pair as one sample. Fixed baselines and pilots are not confirmatory evidence of strategic strength. A matching deck seed, button, and action transcript reproduce the same hand; the test suite exercises this replay path.

The Python benchmark runner reads the predeclared flat-YAML manifest, runs every opponent on validation, records SHA-256 hashes, and sizes final tests from validation variance. Its `freeze` step requires a clean source commit; `final` refuses a second run against the same frozen candidate. Do not use `final` until a candidate has passed validation and the build has been regenerated from the clean commit.

## Rules and information boundary

The initial game uses two 10,000-unit stacks, small blind 50, big blind 100, no antes or rake. The button posts the small blind and acts first before the flop; the big blind acts first afterward. `RaiseTo` is the player's total commitment on the current street. The engine limits raises to effective stacks and supports short all-in raises, uncalled-wager refunds, all-in runouts, and tied showdowns.

An agent receives only a copied `Observation` for the acting player and its own RNG. The observation contains its hole cards, current board, public betting history, stacks, pot, position, and legal actions. It contains no opponent hole cards, future board, deck, game RNG, or terminal payoff. Deck injection is a trusted simulator/test interface and is never passed to an agent.

See [decisions.md](docs/decisions.md) for the conventions and limits. The attached project charter is preserved as [AGENTS.md](AGENTS.md).
