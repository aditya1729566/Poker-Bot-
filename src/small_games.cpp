#include "pokerzero/small_games.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <stdexcept>
#include <vector>

namespace pokerzero {

namespace {
struct State {
  State(SmallGameKind kind, int first, int second, int public_card)
      : game(kind), card0(first), card1(second), board(public_card) {}
  SmallGameKind game;
  int card0;
  int card1;
  int board;
  int street = 0;
  int actor = 0;
  int checks = 0;
  int raises = 0;
  std::array<int, 2> contribution{{1, 1}};
  std::array<int, 2> street_contribution{};
  int current_bet = 0;
  bool terminal = false;
  int fold_winner = -1;
  std::string history;
};

int rank_of(const State& state, int card) { return state.game == SmallGameKind::Kuhn ? card : card / 2; }
int bet_size(const State& state) { return state.game == SmallGameKind::Kuhn ? 1 : state.street == 0 ? 2 : 4; }
int action_count(const State& state) {
  if (state.current_bet == state.street_contribution[state.actor]) return 2; // check, bet
  return state.game == SmallGameKind::Leduc && state.raises < 2 ? 3 : 2; // fold, call, raise
}
std::string info_key(const State& state) {
  const int private_rank = rank_of(state, state.actor == 0 ? state.card0 : state.card1);
  const int public_rank = state.street == 0 ? -1 : rank_of(state, state.board);
  return std::to_string(state.actor) + ':' + std::to_string(state.street) + ':' +
         std::to_string(private_rank) + ':' + std::to_string(public_rank) + ':' + state.history;
}
void close_round(State& state) {
  if (state.game == SmallGameKind::Kuhn || state.street == 1) {
    state.terminal = true;
    return;
  }
  state.street = 1;
  state.actor = 0;
  state.checks = 0;
  state.raises = 0;
  state.street_contribution = {0, 0};
  state.current_bet = 0;
  state.history.push_back('/');
}
State next_state(State state, int action) {
  if (state.terminal || action < 0 || action >= action_count(state)) throw std::logic_error("bad small-game action");
  const int player = state.actor, other = 1 - player;
  if (state.current_bet == state.street_contribution[player]) {
    if (action == 0) {
      state.history.push_back('x');
      if (state.checks == 1) close_round(state);
      else { state.checks = 1; state.actor = other; }
    } else {
      const int size = bet_size(state);
      state.contribution[player] += size;
      state.street_contribution[player] += size;
      state.current_bet = state.street_contribution[player];
      state.history.push_back('b');
      state.actor = other;
    }
  } else if (action == 0) {
    state.history.push_back('f');
    state.fold_winner = other;
    state.terminal = true;
  } else if (action == 1) {
    const int amount = state.current_bet - state.street_contribution[player];
    state.contribution[player] += amount;
    state.street_contribution[player] += amount;
    state.history.push_back('c');
    close_round(state);
  } else {
    const int amount = state.current_bet - state.street_contribution[player] + bet_size(state);
    state.contribution[player] += amount;
    state.street_contribution[player] += amount;
    state.current_bet = state.street_contribution[player];
    ++state.raises;
    state.history.push_back('r');
    state.actor = other;
  }
  return state;
}
double payoff0(const State& state) {
  if (!state.terminal) throw std::logic_error("nonterminal small-game payoff");
  int winner = state.fold_winner;
  if (winner == -1) {
    const int rank0 = rank_of(state, state.card0), rank1 = rank_of(state, state.card1);
    if (state.game == SmallGameKind::Leduc) {
      const int board_rank = rank_of(state, state.board);
      const bool pair0 = rank0 == board_rank, pair1 = rank1 == board_rank;
      if (pair0 != pair1) winner = pair0 ? 0 : 1;
      else if (rank0 != rank1) winner = rank0 > rank1 ? 0 : 1;
    } else {
      winner = rank0 > rank1 ? 0 : 1;
    }
  }
  if (winner == 0) return state.contribution[1];
  if (winner == 1) return -state.contribution[0];
  return 0.5 * static_cast<double>(state.contribution[0] + state.contribution[1]) - state.contribution[0];
}
template <typename Function> void for_each_deal(SmallGameKind game, Function&& function) {
  const int cards = game == SmallGameKind::Kuhn ? 3 : 6;
  const double chance = game == SmallGameKind::Kuhn ? 1.0 / 6.0 : 1.0 / 120.0;
  for (int first = 0; first < cards; ++first)
    for (int second = 0; second < cards; ++second) {
      if (second == first) continue;
      if (game == SmallGameKind::Kuhn) {
        function(State{game, first, second, -1}, chance);
      } else {
        for (int board = 0; board < cards; ++board)
          if (board != first && board != second) function(State{game, first, second, board}, chance);
      }
    }
}
State sampled_deal(SmallGameKind game, Pcg32& rng) {
  const int count = game == SmallGameKind::Kuhn ? 3 : 6;
  std::array<int, 6> deck{{0, 1, 2, 3, 4, 5}};
  const int draws = game == SmallGameKind::Kuhn ? 2 : 3;
  for (int i = 0; i < draws; ++i) {
    const int offset = static_cast<int>(rng.bounded(static_cast<std::uint32_t>(count - i)));
    std::swap(deck[i], deck[i + offset]);
  }
  return State(game, deck[0], deck[1], game == SmallGameKind::Kuhn ? -1 : deck[2]);
}
int sampled_action(const std::array<double, 3>& strategy, int count, Pcg32& rng) {
  const double draw = (static_cast<double>(rng.next()) + 0.5) / 4294967296.0;
  double cumulative = 0;
  for (int action = 0; action < count; ++action) {
    cumulative += strategy[action];
    if (draw < cumulative) return action;
  }
  return count - 1;
}
using Policy = std::function<std::array<double, 3>(const State&)>;
double expected_from(const State& state, const Policy& policy) {
  if (state.terminal) return payoff0(state);
  const auto probabilities = policy(state);
  double value = 0;
  for (int action = 0; action < action_count(state); ++action)
    value += probabilities[action] * expected_from(next_state(state, action), policy);
  return value;
}
struct WeightedState { State state; double weight; };
double best_response_from(const std::vector<WeightedState>& roots, int target, const Policy& policy) {
  double terminal_value = 0;
  std::vector<WeightedState> pending = roots, target_states;
  while (!pending.empty()) {
    std::vector<WeightedState> next;
    for (const auto& item : pending) {
      if (item.state.terminal) {
        terminal_value += item.weight * (target == 0 ? payoff0(item.state) : -payoff0(item.state));
      } else if (item.state.actor == target) {
        target_states.push_back(item);
      } else {
        const auto probabilities = policy(item.state);
        for (int action = 0; action < action_count(item.state); ++action)
          if (probabilities[action] > 0)
            next.push_back({next_state(item.state, action), item.weight * probabilities[action]});
      }
    }
    pending = std::move(next);
  }
  std::unordered_map<std::string, std::vector<WeightedState>> groups;
  for (const auto& item : target_states) groups[info_key(item.state)].push_back(item);
  for (const auto& [key, group] : groups) {
    (void)key;
    double best = -1e100;
    const int count = action_count(group.front().state);
    for (int action = 0; action < count; ++action) {
      std::vector<WeightedState> children;
      children.reserve(group.size());
      for (const auto& item : group) children.push_back({next_state(item.state, action), item.weight});
      best = std::max(best, best_response_from(children, target, policy));
    }
    terminal_value += best;
  }
  return terminal_value;
}
}  // namespace

std::string small_game_name(SmallGameKind game) { return game == SmallGameKind::Kuhn ? "kuhn" : "leduc"; }
std::string cfr_algorithm_name(CfrAlgorithm algorithm) {
  if (algorithm == CfrAlgorithm::Vanilla) return "cfr";
  if (algorithm == CfrAlgorithm::Plus) return "cfr_plus";
  return "external_sampling_mccfr";
}

SmallSolver::SmallSolver(SmallGameKind game, CfrAlgorithm algorithm, std::uint64_t seed)
    : game_(game), algorithm_(algorithm), training_rng_(seed) {}

void SmallSolver::train(std::uint64_t iterations) {
  if (algorithm_ == CfrAlgorithm::ExternalSampling) {
    std::function<double(const State&, int)> traverse;
    traverse = [&](const State& state, int traverser) -> double {
      if (state.terminal) return traverser == 0 ? payoff0(state) : -payoff0(state);
      const std::string key = info_key(state);
      auto& node = nodes_[key];
      const int count = action_count(state);
      if (node.actions == 0) node.actions = count;
      if (node.actions != count) throw std::logic_error("inconsistent infoset actions");
      std::array<double, 3> strategy{};
      double positive = 0;
      for (int action = 0; action < count; ++action) positive += std::max(0.0, node.regret[action]);
      for (int action = 0; action < count; ++action)
        strategy[action] = positive > 0 ? std::max(0.0, node.regret[action]) / positive : 1.0 / count;
      if (state.actor != traverser) {
        const int sampled = sampled_action(strategy, count, training_rng_);
        const double value = traverse(next_state(state, sampled), traverser);
        // Two-player simple averaging: update at opponent nodes on this regret pass.
        auto& updated = nodes_.at(key);
        for (int action = 0; action < count; ++action) updated.strategy_sum[action] += strategy[action];
        return value;
      }
      std::array<double, 3> values{};
      double expected = 0;
      for (int action = 0; action < count; ++action) {
        values[action] = traverse(next_state(state, action), traverser);
        expected += strategy[action] * values[action];
      }
      auto& updated = nodes_.at(key);
      for (int action = 0; action < count; ++action) updated.regret[action] += values[action] - expected;
      return expected;
    };
    for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
      for (int traverser = 0; traverser < 2; ++traverser)
        (void)traverse(sampled_deal(game_, training_rng_), traverser);
      ++iterations_;
    }
    return;
  }
  struct Delta { std::array<double, 3> regret{}, average{}; };
  for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
    std::unordered_map<std::string, Delta> deltas;
    const double average_weight = algorithm_ == CfrAlgorithm::Plus ? static_cast<double>(iterations_ + 1) : 1.0;
    std::function<double(const State&, double, double, double)> traverse;
    traverse = [&](const State& state, double reach0, double reach1, double chance) -> double {
      if (state.terminal) return payoff0(state);
      const std::string key = info_key(state);
      auto& node = nodes_[key];
      const int count = action_count(state);
      if (node.actions == 0) node.actions = count;
      if (node.actions != count) throw std::logic_error("inconsistent infoset actions");
      std::array<double, 3> strategy{};
      double positive = 0;
      for (int action = 0; action < count; ++action) positive += std::max(0.0, node.regret[action]);
      for (int action = 0; action < count; ++action)
        strategy[action] = positive > 0 ? std::max(0.0, node.regret[action]) / positive : 1.0 / count;
      std::array<double, 3> values{};
      double expected = 0;
      for (int action = 0; action < count; ++action) {
        const State child = next_state(state, action);
        values[action] = traverse(child, reach0 * (state.actor == 0 ? strategy[action] : 1.0),
                                  reach1 * (state.actor == 1 ? strategy[action] : 1.0), chance);
        expected += strategy[action] * values[action];
      }
      auto& delta = deltas[key];
      const double opponent_reach = state.actor == 0 ? reach1 : reach0;
      const double own_reach = state.actor == 0 ? reach0 : reach1;
      for (int action = 0; action < count; ++action) {
        const double regret = state.actor == 0 ? values[action] - expected : expected - values[action];
        delta.regret[action] += chance * opponent_reach * regret;
        delta.average[action] += average_weight * chance * own_reach * strategy[action];
      }
      return expected;
    };
    for_each_deal(game_, [&](const State& state, double chance) { (void)traverse(state, 1.0, 1.0, chance); });
    for (const auto& [key, delta] : deltas) {
      auto& node = nodes_.at(key);
      for (int action = 0; action < node.actions; ++action) {
        node.regret[action] += delta.regret[action];
        if (algorithm_ == CfrAlgorithm::Plus) node.regret[action] = std::max(0.0, node.regret[action]);
        node.strategy_sum[action] += delta.average[action];
      }
    }
    ++iterations_;
  }
}

double SmallSolver::expected_value() const {
  const Policy policy = [&](const State& state) {
    std::array<double, 3> result{};
    const int count = action_count(state);
    const auto found = nodes_.find(info_key(state));
    double sum = 0;
    if (found != nodes_.end()) for (int action = 0; action < count; ++action) sum += found->second.strategy_sum[action];
    for (int action = 0; action < count; ++action)
      result[action] = sum > 0 ? found->second.strategy_sum[action] / sum : 1.0 / count;
    return result;
  };
  double value = 0;
  for_each_deal(game_, [&](const State& state, double chance) { value += chance * expected_from(state, policy); });
  return value;
}

double SmallSolver::exploitability() const {
  const Policy policy = [&](const State& state) {
    std::array<double, 3> result{};
    const int count = action_count(state);
    const auto found = nodes_.find(info_key(state));
    double sum = 0;
    if (found != nodes_.end()) for (int action = 0; action < count; ++action) sum += found->second.strategy_sum[action];
    for (int action = 0; action < count; ++action)
      result[action] = sum > 0 ? found->second.strategy_sum[action] / sum : 1.0 / count;
    return result;
  };
  std::vector<WeightedState> roots;
  for_each_deal(game_, [&](const State& state, double chance) { roots.push_back({state, chance}); });
  return std::max(0.0, 0.5 * (best_response_from(roots, 0, policy) + best_response_from(roots, 1, policy)));
}

void SmallSolver::save(const std::string& path) const {
  const std::filesystem::path destination(path);
  if (!destination.parent_path().empty()) std::filesystem::create_directories(destination.parent_path());
  std::ofstream file(path);
  if (!file) throw std::runtime_error("cannot write solver artifact");
  file << std::setprecision(17) << "PZ_SMALL_V1 " << small_game_name(game_) << ' '
       << cfr_algorithm_name(algorithm_) << ' ' << iterations_ << ' ' << nodes_.size() << '\n';
  std::vector<std::string> keys;
  keys.reserve(nodes_.size());
  for (const auto& [key, node] : nodes_) { (void)node; keys.push_back(key); }
  std::sort(keys.begin(), keys.end());
  for (const auto& key : keys) {
    const auto& node = nodes_.at(key);
    file << key << ' ' << node.actions;
    for (int action = 0; action < node.actions; ++action) file << ' ' << node.strategy_sum[action];
    file << '\n';
  }
  if (!file) throw std::runtime_error("solver artifact write failed");
}

SmallSolver SmallSolver::load(const std::string& path) {
  std::ifstream file(path);
  std::string magic, game, algorithm;
  std::uint64_t iterations = 0;
  std::size_t count = 0;
  if (!(file >> magic >> game >> algorithm >> iterations >> count) || magic != "PZ_SMALL_V1" ||
      (game != "kuhn" && game != "leduc") ||
      (algorithm != "cfr" && algorithm != "cfr_plus" && algorithm != "external_sampling_mccfr"))
    throw std::runtime_error("invalid solver artifact header");
  const auto parsed_algorithm = algorithm == "cfr" ? CfrAlgorithm::Vanilla :
                                algorithm == "cfr_plus" ? CfrAlgorithm::Plus : CfrAlgorithm::ExternalSampling;
  SmallSolver solver(game == "kuhn" ? SmallGameKind::Kuhn : SmallGameKind::Leduc, parsed_algorithm);
  solver.iterations_ = iterations;
  for (std::size_t i = 0; i < count; ++i) {
    std::string key;
    Node node;
    if (!(file >> key >> node.actions) || node.actions < 2 || node.actions > 3)
      throw std::runtime_error("invalid solver artifact node");
    for (int action = 0; action < node.actions; ++action)
      if (!(file >> node.strategy_sum[action]) || !std::isfinite(node.strategy_sum[action]) || node.strategy_sum[action] < 0)
        throw std::runtime_error("invalid solver artifact probability");
    if (!solver.nodes_.emplace(key, node).second) throw std::runtime_error("duplicate artifact infoset");
  }
  return solver;
}

}  // namespace pokerzero
