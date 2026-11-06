#pragma once

#include "pokerzero/agents.hpp"
#include "pokerzero/core.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace pokerzero {

struct AbstractChoice {
  Action action;
  char label;
};

[[nodiscard]] std::vector<AbstractChoice> restricted_actions(const Observation& observation);
[[nodiscard]] std::string abstract_public_history(const Observation& observation);

class EquityBuckets {
 public:
  [[nodiscard]] int bucket(const Observation& observation);
  [[nodiscard]] std::size_t cache_size() const { return cache_.size(); }

 private:
  std::unordered_map<std::uint64_t, int> cache_;
};

[[nodiscard]] std::string restricted_info_key(const Observation& observation,
                                              const std::vector<AbstractChoice>& choices,
                                              EquityBuckets& buckets);

class RestrictedSolver {
 public:
  explicit RestrictedSolver(std::uint64_t seed = 0);
  void train(std::uint64_t iterations, std::size_t max_infosets = 8000000);
  [[nodiscard]] std::size_t infosets() const { return nodes_.size(); }
  [[nodiscard]] std::uint64_t iterations() const { return iterations_; }
  [[nodiscard]] std::size_t bucket_cache_size() const { return buckets_.cache_size(); }
  void save(const std::string& path) const;

 private:
  struct Node {
    int actions = 0;
    std::array<double, 6> regrets{};
    std::array<double, 6> strategy_sum{};
  };
  std::uint64_t seed_;
  std::uint64_t iterations_ = 0;
  Pcg32 policy_rng_;
  EquityBuckets buckets_;
  std::unordered_map<std::string, Node> nodes_;
};

class RestrictedAgent final : public IAgent {
 public:
  explicit RestrictedAgent(const std::string& artifact_path);
  Action act(const Observation& observation, Pcg32& policy_rng) override;
  void reset_match() override { fallbacks_ = 0; last_decision_ = {}; }
  [[nodiscard]] std::uint64_t fallback_count() const override { return fallbacks_; }
  [[nodiscard]] PolicyDecisionInfo last_decision_info() const override { return last_decision_; }

 private:
  struct PolicyNode {
    int actions = 0;
    std::array<double, 6> probabilities{};
    double visits = 0;
  };
  EquityBuckets buckets_;
  BaselineAgent fallback_{BaselineStyle::Equity};
  std::unordered_map<std::string, PolicyNode> policy_;
  std::uint64_t fallbacks_ = 0;
  PolicyDecisionInfo last_decision_{};
};

}  // namespace pokerzero
