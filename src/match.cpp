#include "pokerzero/match.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <stdexcept>
#include <vector>

namespace pokerzero {

namespace {
const char* action_kind_name(ActionKind kind) {
  switch (kind) {
    case ActionKind::Fold: return "fold";
    case ActionKind::Check: return "check";
    case ActionKind::Call: return "call";
    case ActionKind::RaiseTo: return "raise_to";
  }
  throw std::logic_error("unknown action kind");
}
void write_action(std::ostream& output, Action action) {
  output << "{\"kind\":\"" << action_kind_name(action.kind) << "\",\"target\":" << action.target << '}';
}
void write_optional_chips(std::ostream& output, std::optional<Chips> amount) {
  if (amount) output << *amount;
  else output << "null";
}
}  // namespace

void JsonlMatchDiagnostics::on_action(const ActionDiagnostic& action) {
  actions_ << "{\"schema_version\":1,\"type\":\"action\",\"seed\":" << action.seed
           << ",\"block\":" << action.block << ",\"leg\":" << action.leg
           << ",\"action_index\":" << action.action_index << ",\"actor\":" << action.actor
           << ",\"button\":" << action.button << ",\"street\":" << static_cast<int>(action.street)
           << ",\"pot_chips\":" << action.pot << ",\"to_call_chips\":" << action.to_call
           << ",\"stacks_chips\":[" << action.stacks[0] << ',' << action.stacks[1] << ']'
           << ",\"street_contributions_chips\":[" << action.street_contributions[0] << ','
           << action.street_contributions[1] << "],\"min_raise_to_chips\":";
  write_optional_chips(actions_, action.min_raise_to);
  actions_ << ",\"max_raise_to_chips\":";
  write_optional_chips(actions_, action.max_raise_to);
  actions_ << ",\"proposed\":";
  write_action(actions_, action.proposed);
  actions_ << ",\"chosen\":";
  write_action(actions_, action.chosen);
  actions_ << ",\"latency_ms\":" << std::setprecision(10) << action.latency_ms
           << ",\"timeout\":" << (action.timeout ? "true" : "false")
           << ",\"illegal\":" << (action.illegal ? "true" : "false")
           << ",\"policy_info_available\":" << (action.policy.available ? "true" : "false")
           << ",\"used_fallback\":" << (action.policy.used_fallback ? "true" : "false")
           << ",\"table_visits\":";
  if (action.policy.table_visits) actions_ << std::setprecision(17) << *action.policy.table_visits;
  else actions_ << "null";
  actions_ << ",\"candidate_hand_return_chips\":" << action.candidate_hand_return << "}\n";
  if (!actions_) throw std::runtime_error("failed to write action diagnostics");
}

void JsonlMatchDiagnostics::on_block(const BlockDiagnostic& block) {
  blocks_ << "{\"schema_version\":1,\"type\":\"block\",\"seed\":" << block.seed
          << ",\"block\":" << block.block << ",\"candidate_leg_returns_chips\":["
          << block.candidate_leg_returns[0] << ',' << block.candidate_leg_returns[1]
          << "],\"candidate_block_mbb_per_hand\":"
          << 5 * (block.candidate_leg_returns[0] + block.candidate_leg_returns[1]) << "}\n";
  if (!blocks_) throw std::runtime_error("failed to write block diagnostics");
}

std::pair<std::array<Card, 52>, std::array<Card, 52>> paired_decks(std::uint64_t seed) {
  auto first = shuffled_deck(seed);
  auto second = first;
  std::swap(second[0], second[1]);
  std::swap(second[2], second[3]);
  return {first, second};
}

MatchReport run_paired_match(IAgent& first, IAgent& second, std::uint64_t blocks,
                             std::uint64_t seed, double action_budget_ms,
                             MatchDiagnosticObserver* diagnostics) {
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
    std::array<Chips, 2> leg_returns{};
    for (int leg = 0; leg < 2; ++leg) {
      Game game = Game::reset_with_deck(leg == 0 ? deck_a : deck_b, {}, leg);
      Pcg32 rng0(derive_stream_seed(seed, kMatchPolicyDomain + static_cast<std::uint64_t>(2 * leg), block));
      Pcg32 rng1(derive_stream_seed(seed, kMatchPolicyDomain + static_cast<std::uint64_t>(2 * leg + 1), block));
      std::vector<ActionDiagnostic> hand_actions;
      std::uint32_t action_index = 0;
      while (!game.terminal()) {
        const int actor = game.actor();
        const auto observation = game.observe(actor);
        const auto action_start = std::chrono::steady_clock::now();
        const Action proposed = actor == 0 ? first.act(observation, rng0) : second.act(observation, rng1);
        const double latency = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - action_start).count();
        const auto policy_info = diagnostics ? (actor == 0 ? first.last_decision_info() : second.last_decision_info()) : PolicyDecisionInfo{};
        latencies.push_back(latency);
        ++report.actions;
        Action chosen = proposed;
        const bool timeout = latency > action_budget_ms;
        const bool illegal = !timeout && !observation.legal.contains(proposed);
        if (timeout) {
          ++report.timeouts;
          chosen = {observation.legal.check ? ActionKind::Check : ActionKind::Call};
        } else if (illegal) {
          ++report.illegal_actions;
          chosen = {observation.legal.check ? ActionKind::Check : ActionKind::Call};
        }
        if (diagnostics)
          hand_actions.push_back({seed, block, leg, action_index, actor, leg, observation.street,
                                  observation.pot, observation.to_call, observation.stacks,
                                  observation.street_contributions, observation.legal.min_raise_to,
                                  observation.legal.max_raise_to, proposed, chosen, latency, timeout, illegal,
                                  policy_info, 0});
        ++action_index;
        game.step(chosen);
      }
      const auto payoff = game.terminal_payoffs();
      leg_returns[leg] = payoff[0];
      if (diagnostics)
        for (auto& action : hand_actions) {
          action.candidate_hand_return = payoff[0];
          diagnostics->on_action(action);
        }
      report.net_chips[0] += payoff[0];
      report.net_chips[1] += payoff[1];
      report.agent0_by_button[leg] += payoff[0];
      block_net += payoff[0];
    }
    if (diagnostics) diagnostics->on_block({seed, block, leg_returns});
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
