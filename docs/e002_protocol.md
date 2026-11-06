# E-002 mirror calibration protocol

E-001's equity-versus-identical-equity development mirror measured +883.586
mbb/hand for player 0 across seeds 51–53. E-002 is a diagnostic, not a
candidate selection or Gate E test. The fresh seeds 54–59 and 10,000 hands per
seed were declared in `experiments/e002.json` before execution.

## Runner transformation and RNG

For each root seed and block, the runner derives a deal seed in
`kMatchDealDomain` and shuffles a 52-card deck. In leg 0, player 0 is button and
receives deck positions 0 and 2; player 1 receives 1 and 3. In leg 1, it swaps
deck positions 0↔1 and 2↔3 and sets player 1 as button. This preserves each
player's private-card packet and the board (positions 5, 6, 7, 9, 11) while
balancing button exposure. Each agent instance remains attached to its player
number. Four independently derived policy RNG streams per block use domains
`kMatchPolicyDomain + 0, +1, +2, +3` for (leg 0, player 0), (leg 0, player 1),
(leg 1, player 0), and (leg 1, player 1), respectively. The opponent is a new
instance of the same equity class, initialized with identical parameters. The
four RNG streams are independent, so duplicate outcomes are correlated through
cards, not necessarily exact negatives under identical policies.

The deterministic **player-label-swap** test uses a different transformation:
it leaves the physical deck in the same order, swaps player labels and the
button, and assigns original player 0's policy RNG state to relabeled player 1
and vice versa. Thus each role keeps its private packet, blind, action order,
and random draw sequence under the new label. Every observation, legal action,
public action, and final payoff must transform accordingly. In particular,
player 0's payoff negates. This tests label symmetry of the game and policy;
it does not assert the statistical duplicate pair has zero return block by
block.

The prespecified analysis pools 30,000 independent seed-and-deal blocks. Each
block value is `5 * (player_0_leg_0_net_chips + player_0_leg_1_net_chips)` in
mbb/hand. The 95% interval is mean ± 1.96 × sample standard deviation /
sqrt(number of blocks). Seat rates are `10 * seat_net_chips / 5,000` for each
seed and `10 * seat_net_chips / 30,000` pooled. Action failures are counted by
acting player from JSONL diagnostics. A single timeout can change a 100bb
player's hand payoff by at most 20,000 chips (from -10,000 to +10,000), or
200,000 mbb for that hand. The conservative pooled effect bound is therefore
`timeout_count * 200,000 / 60,000` mbb/hand. This is a worst-case bound, not an
estimated bias correction.
