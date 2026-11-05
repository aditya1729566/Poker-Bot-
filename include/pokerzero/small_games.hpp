#pragma once

#include "pokerzero/core.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <array>

namespace pokerzero {

enum class SmallGameKind { Kuhn, Leduc };
enum class CfrAlgorithm { Vanilla, Plus, ExternalSampling };

class SmallSolver {
 public:
  SmallSolver(SmallGameKind game, CfrAlgorithm algorithm = CfrAlgorithm::Vanilla, std::uint64_t seed = 0);
  void train(std::uint64_t iterations);
  [[nodiscard]] double expected_value() const;
  [[nodiscard]] double exploitability() const;
  [[nodiscard]] std::size_t infosets() const { return nodes_.size(); }
  [[nodiscard]] std::uint64_t iterations() const { return iterations_; }
  [[nodiscard]] SmallGameKind game() const { return game_; }
  [[nodiscard]] CfrAlgorithm algorithm() const { return algorithm_; }
  void save(const std::string& path) const;
  static SmallSolver load(const std::string& path);

 private:
  struct Node {
    int actions = 0;
    std::array<double, 3> regret{};
    std::array<double, 3> strategy_sum{};
  };
  SmallGameKind game_;
  CfrAlgorithm algorithm_;
  std::uint64_t iterations_ = 0;
  Pcg32 training_rng_;
  std::unordered_map<std::string, Node> nodes_;
};

[[nodiscard]] std::string small_game_name(SmallGameKind game);
[[nodiscard]] std::string cfr_algorithm_name(CfrAlgorithm algorithm);

}  // namespace pokerzero
