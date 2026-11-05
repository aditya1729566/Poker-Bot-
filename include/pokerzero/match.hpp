#pragma once

#include "pokerzero/core.hpp"

#include <array>
#include <cstdint>
#include <utility>

namespace pokerzero {

struct MatchReport {
  std::uint64_t hands = 0;
  std::uint64_t blocks = 0;
  std::array<Chips, 2> net_chips{};
  std::array<Chips, 2> agent0_by_button{};  // index 0: agent 0 button; index 1: agent 0 big blind
  double mbb_per_hand = 0;
  double ci95_low = 0;
  double ci95_high = 0;
  double seconds = 0;
  double latency_p50_ms = 0;
  double latency_p95_ms = 0;
  double latency_p99_ms = 0;
  std::uint64_t actions = 0;
  std::uint64_t timeouts = 0;
  std::uint64_t illegal_actions = 0;
  std::array<std::uint64_t, 2> fallbacks{};
};

// The second leg preserves each agent's hole-card packet and the full board,
// while swapping its button position. Only the trusted runner uses these decks.
[[nodiscard]] std::pair<std::array<Card, 52>, std::array<Card, 52>> paired_decks(std::uint64_t seed);
[[nodiscard]] MatchReport run_paired_match(IAgent& first, IAgent& second, std::uint64_t blocks,
                                            std::uint64_t seed, double action_budget_ms = 25.0);

}  // namespace pokerzero
