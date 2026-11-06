#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pokerzero {

using Chips = std::int64_t;

struct Card {
  std::uint8_t id = 255;
  Card() = default;
  explicit Card(int value);
  [[nodiscard]] int rank() const;
  [[nodiscard]] int suit() const;
  friend bool operator==(Card, Card) = default;
};

class Pcg32 {
 public:
  explicit Pcg32(std::uint64_t seed, std::uint64_t sequence = 1);
  [[nodiscard]] std::uint32_t next();
  [[nodiscard]] std::uint32_t bounded(std::uint32_t bound);

 private:
  std::uint64_t state_ = 0;
  std::uint64_t increment_ = 0;
};

[[nodiscard]] std::uint64_t splitmix64(std::uint64_t value);
[[nodiscard]] std::uint64_t derive_stream_seed(std::uint64_t root, std::uint64_t domain,
                                               std::uint64_t index);
inline constexpr std::uint64_t kMatchDealDomain = 0x4d41544348444541ULL;
inline constexpr std::uint64_t kMatchPolicyDomain = 0x4d41544348504f4cULL;
inline constexpr std::uint64_t kTrainingDealDomain = 0x545241494e444541ULL;
inline constexpr std::uint64_t kTrainingPolicyDomain = 0x545241494e504f4cULL;
[[nodiscard]] std::array<Card, 52> shuffled_deck(std::uint64_t seed);
[[nodiscard]] std::uint64_t evaluate(const std::vector<Card>& cards);

enum class Street { Preflop, Flop, Turn, River, Terminal };
enum class ActionKind { Fold, Check, Call, RaiseTo };

struct Action {
  ActionKind kind;
  Chips target = 0;  // Total chips committed on the current street for RaiseTo.
  friend bool operator==(const Action&, const Action&) = default;
};

struct LegalActions {
  bool fold = false;
  bool check = false;
  bool call = false;
  std::optional<Chips> min_raise_to;
  std::optional<Chips> max_raise_to;
  [[nodiscard]] bool contains(Action action) const;
};

struct PublicAction {
  Street street;
  int player;
  Action action;
  Chips committed;
  Chips pot_after;
  friend bool operator==(const PublicAction&, const PublicAction&) = default;
};

struct Observation {
  int player;
  std::array<Card, 2> hole;
  std::vector<Card> board;
  Street street;
  int button;
  std::array<Chips, 2> stacks;
  std::array<Chips, 2> street_contributions;
  Chips pot;
  Chips to_call;
  LegalActions legal;
  std::vector<PublicAction> history;
};

struct WeightedAction {
  Action action;
  double probability;
};

// Policy-side metadata about the most recent action. It contains no simulator
// state and is read only by the trusted match runner after act() returns.
struct PolicyDecisionInfo {
  bool available = false;
  bool used_fallback = false;
  std::optional<double> table_visits;
};

class IAgent {
 public:
  virtual ~IAgent() = default;
  virtual Action act(const Observation& observation, Pcg32& policy_rng) = 0;
  virtual void reset_match() {}
  [[nodiscard]] virtual std::uint64_t fallback_count() const { return 0; }
  [[nodiscard]] virtual PolicyDecisionInfo last_decision_info() const { return {}; }
};

class IStrategy {
 public:
  virtual ~IStrategy() = default;
  [[nodiscard]] virtual std::vector<WeightedAction> distribution(const Observation& observation) const = 0;
};

[[nodiscard]] Action sample_action(const Observation& observation, const std::vector<WeightedAction>& distribution, Pcg32& policy_rng);

class RandomAgent final : public IAgent, public IStrategy {
 public:
  [[nodiscard]] std::vector<WeightedAction> distribution(const Observation& observation) const override;
  Action act(const Observation& observation, Pcg32& policy_rng) override;
};

struct GameConfig {
  Chips initial_stack = 10000;
  Chips small_blind = 50;
  Chips big_blind = 100;
};

class Game {
 public:
  static Game reset(std::uint64_t seed, GameConfig config = {}, int button = 0);
  static Game reset_with_deck(const std::array<Card, 52>& deck, GameConfig config = {}, int button = 0);
  [[nodiscard]] Observation observe(int player) const;
  [[nodiscard]] LegalActions legal_actions() const;
  void step(Action action);
  [[nodiscard]] bool terminal() const { return street_ == Street::Terminal; }
  [[nodiscard]] std::array<Chips, 2> terminal_payoffs() const;
  [[nodiscard]] int actor() const { return actor_; }
  [[nodiscard]] Street street() const { return street_; }
  [[nodiscard]] std::array<Chips, 2> stacks() const { return stacks_; }
  [[nodiscard]] Chips pot() const { return pot_; }

 private:
  Game() = default;
  void commit(int player, Chips amount);
  void close_round();
  void showdown();
  void award_fold(int winner);
  void check_invariants() const;

  GameConfig config_;
  std::array<Card, 52> deck_{};
  std::array<std::array<Card, 2>, 2> hole_{};
  std::array<Card, 5> full_board_{};
  std::vector<Card> board_;
  std::array<Chips, 2> stacks_{};
  std::array<Chips, 2> street_contributions_{};
  std::array<Chips, 2> total_contributions_{};
  std::array<bool, 2> acted_{};
  std::array<bool, 2> can_raise_{{true, true}};
  std::vector<PublicAction> history_;
  Chips pot_ = 0;
  Chips current_bet_ = 0;
  Chips last_full_raise_ = 0;
  int button_ = 0;
  int actor_ = 0;
  Street street_ = Street::Preflop;
};

[[nodiscard]] Action random_legal_action(const Observation& observation, Pcg32& rng);
[[nodiscard]] std::string card_string(Card card);
[[nodiscard]] std::string action_string(Action action);

}  // namespace pokerzero
