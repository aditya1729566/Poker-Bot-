#pragma once

#include "pokerzero/core.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace pokerzero {

struct EquityEstimate {
  double equity = 0;
  double standard_error = 0;
  std::uint64_t outcomes = 0;
  bool exact = false;
};

// Opponent's unrevealed hand is uniform among cards compatible with the observation.
[[nodiscard]] EquityEstimate estimate_uniform_equity(const Observation& observation, Pcg32& rng,
                                                     std::uint32_t samples = 512,
                                                     bool exact_when_feasible = true);

enum class BaselineStyle { Passive, TightAggressive, LooseAggressive, Equity };

class BaselineAgent final : public IAgent {
 public:
  explicit BaselineAgent(BaselineStyle style) : style_(style) {}
  Action act(const Observation& observation, Pcg32& policy_rng) override;

 private:
  BaselineStyle style_;
};

[[nodiscard]] std::unique_ptr<IAgent> make_agent(const std::string& name);

}  // namespace pokerzero
