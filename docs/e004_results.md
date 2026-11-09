# E-004 development ablation results

E-004 tested whether disabling the frozen Stage 4 policy table before the flop while retaining it after the flop improved results. The predeclared four arms were full hybrid, preflop only, postflop only, and equity fallback only. This was a **development diagnostic** using seeds 60–62, not validation or final evaluation. It selected no candidate and does not satisfy Gate E.

## Prespecified outcome

Across tight-aggressive, loose-aggressive, and equity opponents, the equal-weight **postflop-only minus full-hybrid** effect was **−620.31 mbb/hand**, paired 95% normal-standard-error interval **[−1001.03, −239.60]**. The unit was a shared seed and duplicate-deal block; the interval used 15,000 such clusters. The proposed improvement failed. Against passive alone, the same contrast was +1510.60 mbb/hand, interval [+837.10, +2184.10], and met its predeclared −500 mbb/hand lower-bound margin.

All five predeclared opponents are shown below. Values are candidate mbb/hand; intervals in the last column apply to postflop-only minus full hybrid. Other arm comparisons are exploratory.

| Opponent | Full | Preflop only | Postflop only | Equity only | Postflop only − full, paired 95% CI |
|---|---:|---:|---:|---:|---:|
| Random | +4916.01 | +5396.53 | +7806.82 | +8639.51 | +2890.81 [+2402.79, +3378.83] |
| Passive | +5356.43 | +4935.92 | +6867.03 | +3336.02 | +1510.60 [+837.10, +2184.10] |
| Tight-aggressive | −4859.75 | −2148.45 | −6192.59 | −1796.07 | −1332.84 [−1766.45, −899.23] |
| Loose-aggressive | −3778.22 | −635.48 | −4082.90 | +641.55 | −304.68 [−873.95, +264.58] |
| Equity | −5252.17 | −1689.56 | −5475.59 | −595.52 | −223.42 [−699.57, +252.74] |

The exploratory strong-opponent means for full, preflop-only, postflop-only, and equity-only were −4630.05, −1491.16, −5250.36, and −583.35 mbb/hand. These results suggest further investigation of the postflop table, but do not establish a general street-specific mechanism or justify promoting another arm.

## Execution and limitations

The complete matrix was 3 seeds × 5 opponents × 4 arms × 10,000 hands: **60 matches, 600,000 hands, 300,000 paired blocks**. All reports and raw block/action files passed hash, seat, chip-net, action, fallback, timeout, and zero-sum reconciliation. There were **191 timeouts** (138 candidate, 53 opponent), **0 illegal actions**, and peak reported RSS was 340,295,680 bytes. The 47 timeouts in the primary contrast have a conservative maximum effect of ±104.44 mbb/hand; adding that bound to the primary interval upper end still gives −135.15.

Two human-interrupted partial attempts, `seed60_equity_equity_only` and `seed62_tight_aggressive_full`, were archived with hashes and failure reasons. Each arm restarted from block zero; partial outcomes were excluded. Five completed runs showed C++ elapsed time exceeding controller monotonic time by 81.9–4587.0 seconds. The cause is unconfirmed, so their latency and throughput measures are contaminated. Total C++ match time was 17,344.2 seconds; controller monotonic time including interrupted attempts was 11,669.1 seconds. Both are under the predeclared eight-hour cap. The earlier E-002 identical-equity mirror offset remains unresolved.

## Provenance and retained evidence

The evaluated source was frozen at commit `3bc1cc3b90a27fdfe969cd5541fe0ef165c0d450`; executable SHA-256 `5ee8c051c1f70cb0aea8aa092dafbfd4324a6ca6e3da1ac83626a9038a9c8843`; Stage 4 policy artifact SHA-256 `82f4d59e74b674b249c2acd5b2a626373e1b565fbd9741888fb030d8570f5eeb`; [predeclared manifest](../experiments/e004.json) SHA-256 `fa561716a8b40dd46a6f4212fe437a0cc552caf5671653da2316f7cdcfd8cf27`. The action budget was 25 ms, the RSS cap was 12 GiB, and the same ten-visit fallback threshold was used in every arm.

The ignored local directory `artifacts/reports/e004/` retains `report.md`, `analysis.json`, `progress.json`, `postrun_audit.json`, every complete raw block/action file, and both archived partial attempts. SHA-256 values for the four summary files, respectively, are `260bba87b66bade4e13316adcafc6f40c41c1f0777cdc54efc73ef75c7f7cc58`, `ac88316f9d6f23bdd4a2f9f7ea41367778d8bdac52310064c9ac990df4c4715f`, `20fda1079818de94fd5e1f30b8c998e1261ceb90e33defc94559271e3133ddf3`, and `d568c02b9f372983cf82c696e5689a230aa938e2f9945bc060b130577297e758`. The machine-readable ledger gives per-run file hashes, seeds, seat splits, action counts, and failures.

Post-run `ctest --test-dir build --output-on-failure` passed 2/2; the clean executable preflight passed; all 206 protected Stage 4/E-001/E-002 files matched their pre-run hashes. This results document was committed **after** the frozen run. It did not change the evaluated executable, experiment artifacts, rules, RNG, opponents, or outcomes.
