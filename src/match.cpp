#include "pokerzero/match.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace pokerzero {

std::pair<std::array<Card, 52>, std::array<Card, 52>> paired_decks(std::uint64_t seed) {
  auto first = shuffled_deck(seed);
  auto second = first;
  std::swap(second[0], second[1]);
  std::swap(second[2], second[3]);
  return {first, second};
}

MatchReport run_paired_match(IAgent& first, IAgent& second, std::uint64_t blocks,
                             std::uint64_t seed, double action_budget_ms) {
  if (blocks < 2 || !std::isfinite(action_budget_ms) || action_budget_ms <= 0)
    throw std::invalid_argument("invalid match blocks or action budget");
  first.reset_match(); second.reset_match();
  MatchReport report;
  report.blocks = blocks;
  report.hands = 2 * blocks;
  std::vector<double> latencies;
  double mean = 0, m2 = 0;
  const auto match_start = std::chrono::steady_clock::now();
  for (std::uint64_t block = 0; block < blocks; ++block) {
    const auto [deck_a, deck_b] = paired_decks(derive_stream_seed(seed, kMatchDealDomain, block));
    Chips block_net = 0;
    for (int leg = 0; leg < 2; ++leg) {
      Game game = Game::reset_with_deck(leg == 0 ? deck_a : deck_b, {}, leg);
      Pcg32 rng0(derive_stream_seed(seed, kMatchPolicyDomain + static_cast<std::uint64_t>(2 * leg), block));
      Pcg32 rng1(derive_stream_seed(seed, kMatchPolicyDomain + static_cast<std::uint64_t>(2 * leg + 1), block));
      while (!game.terminal()) {
        const int actor = game.actor();
        const auto observation = game.observe(actor);
        const auto action_start = std::chrono::steady_clock::now();
        const Action proposed = actor == 0 ? first.act(observation, rng0) : second.act(observation, rng1);
        const double latency = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - action_start).count();
        latencies.push_back(latency);
        ++report.actions;
        Action chosen = proposed;
        if (latency > action_budget_ms) {
          ++report.timeouts;
          chosen = {observation.legal.check ? ActionKind::Check : ActionKind::Call};
        } else if (!observation.legal.contains(chosen)) {
          ++report.illegal_actions;
          chosen = {observation.legal.check ? ActionKind::Check : ActionKind::Call};
        }
        game.step(chosen);
      }
      const auto payoff = game.terminal_payoffs();
      report.net_chips[0] += payoff[0];
      report.net_chips[1] += payoff[1];
      report.agent0_by_button[leg] += payoff[0];
      block_net += payoff[0];
    }
    // One sample is the mean agent-0 return over a duplicate two-hand block, in mbb/hand.
    const double block_mbb = 5.0 * static_cast<double>(block_net);
    const double delta = block_mbb - mean;
    mean += delta / static_cast<double>(block + 1);
    m2 += delta * (block_mbb - mean);
  }
  report.mbb_per_hand = mean;
  const double standard_error = std::sqrt(m2 / static_cast<double>(blocks - 1) / static_cast<double>(blocks));
  report.ci95_low = mean - 1.96 * standard_error;
  report.ci95_high = mean + 1.96 * standard_error;
  report.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - match_start).count();
  std::sort(latencies.begin(), latencies.end());
  const auto percentile = [&](double fraction) {
    return latencies[std::min(latencies.size() - 1, static_cast<std::size_t>(std::ceil(fraction * latencies.size()) - 1))];
  };
  report.latency_p50_ms = percentile(0.50);
  report.latency_p95_ms = percentile(0.95);
  report.latency_p99_ms = percentile(0.99);
  report.fallbacks = {first.fallback_count(), second.fallback_count()};
  return report;
}

}  // namespace pokerzero
