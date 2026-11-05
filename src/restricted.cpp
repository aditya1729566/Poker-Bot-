#include "pokerzero/restricted.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <set>
#include <stdexcept>

namespace pokerzero {

namespace {
int aggressive_actions_this_street(const Observation& observation) {
  int count = 0;
  for (const auto& record : observation.history)
    if (record.street == observation.street && record.action.kind == ActionKind::RaiseTo) ++count;
  return count;
}
Chips rounded_fraction(Chips amount, Chips numerator, Chips denominator) {
  return (amount * numerator + denominator / 2) / denominator;
}
std::uint64_t visible_key(const Observation& observation) {
  std::array<int, 2> hole{{observation.hole[0].id, observation.hole[1].id}};
  std::sort(hole.begin(), hole.end());
  std::vector<int> board;
  for (const auto card : observation.board) board.push_back(card.id);
  std::sort(board.begin(), board.end());
  std::uint64_t value = board.size();
  for (const int id : hole) value = (value << 6U) | static_cast<std::uint64_t>(id);
  for (const int id : board) value = (value << 6U) | static_cast<std::uint64_t>(id);
  return value;
}
std::array<double, 6> regret_strategy(const std::array<double, 6>& regrets, int count) {
  std::array<double, 6> strategy{};
  double positive = 0;
  for (int i = 0; i < count; ++i) positive += std::max(0.0, regrets[i]);
  for (int i = 0; i < count; ++i)
    strategy[i] = positive > 0 ? std::max(0.0, regrets[i]) / positive : 1.0 / count;
  return strategy;
}
int sample_index(const std::array<double, 6>& strategy, int count, Pcg32& rng) {
  const double draw = (static_cast<double>(rng.next()) + 0.5) / 4294967296.0;
  double cumulative = 0;
  for (int i = 0; i < count; ++i) {
    cumulative += strategy[i];
    if (draw < cumulative) return i;
  }
  return count - 1;
}
}  // namespace

std::vector<AbstractChoice> restricted_actions(const Observation& observation) {
  std::vector<AbstractChoice> choices;
  if (observation.legal.fold) choices.push_back({{ActionKind::Fold}, 'f'});
  if (observation.legal.check) choices.push_back({{ActionKind::Check}, 'x'});
  if (observation.legal.call) choices.push_back({{ActionKind::Call}, 'c'});
  if (observation.legal.min_raise_to && aggressive_actions_this_street(observation) < 2) {
    const Chips minimum = *observation.legal.min_raise_to;
    const Chips maximum = *observation.legal.max_raise_to;
    const Chips base = observation.street_contributions[observation.player] + observation.to_call;
    const Chips pot_after_call = observation.pot + observation.to_call;
    const std::array<std::pair<Chips, Chips>, 3> fractions{{{33, 100}, {3, 4}, {5, 4}}};
    const std::array<char, 3> labels{{'s', 'm', 'l'}};
    std::set<Chips> seen;
    for (int i = 0; i < 3; ++i) {
      const Chips target = base + rounded_fraction(pot_after_call, fractions[i].first, fractions[i].second);
      if (target >= minimum && target < maximum && seen.insert(target).second)
        choices.push_back({{ActionKind::RaiseTo, target}, labels[i]});
    }
    choices.push_back({{ActionKind::RaiseTo, maximum}, 'a'});
  }
  if (choices.empty() || choices.size() > 6) throw std::logic_error("invalid restricted action set");
  for (const auto& choice : choices)
    if (!observation.legal.contains(choice.action)) throw std::logic_error("restricted action is illegal");
  return choices;
}

std::string abstract_public_history(const Observation& observation) {
  std::string result;
  std::array<Chips, 2> stacks{{9950, 9900}}, contributions{{50, 100}};
  if (observation.button == 1) { stacks = {9900, 9950}; contributions = {100, 50}; }
  Street current_street = Street::Preflop;
  for (const auto& record : observation.history) {
    if (record.street != current_street) {
      result.push_back('/');
      current_street = record.street;
      contributions = {0, 0};
    }
    char label = '?';
    if (record.action.kind == ActionKind::Fold) label = 'f';
    else if (record.action.kind == ActionKind::Check) label = 'x';
    else if (record.action.kind == ActionKind::Call) label = 'c';
    else {
      const int actor = record.player, other = 1 - actor;
      const Chips maximum = std::min(contributions[actor] + stacks[actor], contributions[other] + stacks[other]);
      if (record.action.target == maximum) {
        label = 'a';
      } else {
        const Chips to_call = std::max<Chips>(0, contributions[other] - contributions[actor]);
        const Chips increment = record.action.target - contributions[actor] - to_call;
        const Chips pot_after_call = record.pot_after - record.committed + to_call;
        const double ratio = pot_after_call > 0 ? static_cast<double>(increment) / pot_after_call : 0;
        const std::array<double, 3> fractions{{0.33, 0.75, 1.25}};
        const std::array<char, 3> labels{{'s', 'm', 'l'}};
        int nearest = 0;
        for (int i = 1; i < 3; ++i)
          if (std::abs(ratio - fractions[i]) < std::abs(ratio - fractions[nearest])) nearest = i;
        label = labels[nearest];
      }
    }
    result.push_back(label);
    stacks[record.player] -= record.committed;
    contributions[record.player] += record.committed;
  }
  if (current_street != observation.street) result.push_back('/');
  return result;
}

int EquityBuckets::bucket(const Observation& observation) {
  if (observation.street == Street::Preflop) {
    const int first = observation.hole[0].rank(), second = observation.hole[1].rank();
    if (first == second) return first - 2;
    const int high = std::max(first, second), low = std::min(first, second);
    const int pair_index = (high - 2) * (high - 3) / 2 + (low - 2);
    return (observation.hole[0].suit() == observation.hole[1].suit() ? 13 : 91) + pair_index;
  }
  const auto key = visible_key(observation);
  const auto found = cache_.find(key);
  if (found != cache_.end()) return found->second;
  Pcg32 bucket_rng(splitmix64(key ^ 0xB0C7E7ULL));
  const auto estimate = estimate_uniform_equity(observation, bucket_rng, 64, false);
  const int result = std::clamp(static_cast<int>(estimate.equity * 32), 0, 31);
  cache_.emplace(key, result);
  return result;
}

std::string restricted_info_key(const Observation& observation,
                                const std::vector<AbstractChoice>& choices,
                                EquityBuckets& buckets) {
  std::string key = std::to_string(observation.player) + ':' + std::to_string(observation.button) + ':' +
                    std::to_string(static_cast<int>(observation.street)) + ':' +
                    std::to_string(buckets.bucket(observation)) + ':' + abstract_public_history(observation) + ':';
  for (const auto& choice : choices) key.push_back(choice.label);
  return key;
}

RestrictedSolver::RestrictedSolver(std::uint64_t seed)
    : seed_(seed), policy_rng_(derive_stream_seed(seed, kTrainingPolicyDomain, 0)) {}

void RestrictedSolver::train(std::uint64_t iterations, std::size_t max_infosets) {
  if (max_infosets == 0) throw std::invalid_argument("max infosets must be positive");
  std::function<double(const Game&, int)> traverse;
  traverse = [&](const Game& game, int traverser) -> double {
    if (game.terminal()) return static_cast<double>(game.terminal_payoffs()[traverser]) / 100.0;
    const auto observation = game.observe(game.actor());
    const auto choices = restricted_actions(observation);
    const std::string key = restricted_info_key(observation, choices, buckets_);
    auto [iterator, inserted] = nodes_.try_emplace(key);
    if (inserted && nodes_.size() > max_infosets) throw std::runtime_error("restricted solver infoset cap exceeded");
    auto& node = iterator->second;
    const int count = static_cast<int>(choices.size());
    if (node.actions == 0) node.actions = count;
    if (node.actions != count) throw std::logic_error("inconsistent restricted action count");
    const auto strategy = regret_strategy(node.regrets, count);
    if (game.actor() != traverser) {
      const int selected = sample_index(strategy, count, policy_rng_);
      Game child = game;
      child.step(choices[selected].action);
      const double value = traverse(child, traverser);
      auto& updated = nodes_.at(key);
      for (int i = 0; i < count; ++i) updated.strategy_sum[i] += strategy[i];
      return value;
    }
    std::array<double, 6> values{};
    double expected = 0;
    for (int i = 0; i < count; ++i) {
      Game child = game;
      child.step(choices[i].action);
      values[i] = traverse(child, traverser);
      expected += strategy[i] * values[i];
    }
    auto& updated = nodes_.at(key);
    for (int i = 0; i < count; ++i) updated.regrets[i] += values[i] - expected;
    return expected;
  };
  for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
    for (int traverser = 0; traverser < 2; ++traverser) {
      const auto deal_seed = derive_stream_seed(seed_, kTrainingDealDomain + static_cast<std::uint64_t>(traverser), iterations_);
      const Game game = Game::reset(deal_seed, {}, static_cast<int>(iterations_ & 1U));
      (void)traverse(game, traverser);
    }
    ++iterations_;
  }
}

void RestrictedSolver::save(const std::string& path) const {
  const std::filesystem::path destination(path);
  if (!destination.parent_path().empty()) std::filesystem::create_directories(destination.parent_path());
  std::ofstream file(path);
  if (!file) throw std::runtime_error("cannot write restricted artifact");
  file << std::setprecision(17) << "PZ_HUNL_ABS_V1 " << seed_ << ' ' << iterations_ << ' ' << nodes_.size() << '\n';
  std::vector<std::string> keys;
  keys.reserve(nodes_.size());
  for (const auto& [key, node] : nodes_) { (void)node; keys.push_back(key); }
  std::sort(keys.begin(), keys.end());
  for (const auto& key : keys) {
    const auto& node = nodes_.at(key);
    file << key << ' ' << node.actions;
    for (int i = 0; i < node.actions; ++i) file << ' ' << node.strategy_sum[i];
    file << '\n';
  }
  if (!file) throw std::runtime_error("restricted artifact write failed");
}

RestrictedAgent::RestrictedAgent(const std::string& artifact_path) {
  std::ifstream file(artifact_path);
  std::string magic;
  std::uint64_t seed = 0, iterations = 0;
  std::size_t count = 0;
  if (!(file >> magic >> seed >> iterations >> count) || magic != "PZ_HUNL_ABS_V1" || count > 8000000)
    throw std::runtime_error("invalid restricted artifact header");
  (void)seed;
  (void)iterations;
  for (std::size_t index = 0; index < count; ++index) {
    std::string key;
    PolicyNode node;
    if (!(file >> key >> node.actions) || node.actions < 1 || node.actions > 6)
      throw std::runtime_error("invalid restricted artifact node");
    double sum = 0;
    for (int action = 0; action < node.actions; ++action) {
      if (!(file >> node.probabilities[action]) || !std::isfinite(node.probabilities[action]) || node.probabilities[action] < 0)
        throw std::runtime_error("invalid restricted artifact probability");
      sum += node.probabilities[action];
    }
    node.visits = sum;
    if (sum > 0) for (int action = 0; action < node.actions; ++action) node.probabilities[action] /= sum;
    if (!policy_.emplace(key, node).second) throw std::runtime_error("duplicate restricted infoset");
  }
}

Action RestrictedAgent::act(const Observation& observation, Pcg32& policy_rng) {
  const auto choices = restricted_actions(observation);
  const auto key = restricted_info_key(observation, choices, buckets_);
  const auto found = policy_.find(key);
  if (found == policy_.end() || found->second.actions != static_cast<int>(choices.size()) ||
      found->second.visits < 10.0) {
    ++fallbacks_;
    return fallback_.act(observation, policy_rng);
  }
  std::vector<WeightedAction> distribution;
  for (int i = 0; i < found->second.actions; ++i)
    distribution.push_back({choices[i].action, found->second.probabilities[i]});
  double sum = 0;
  for (const auto& entry : distribution) sum += entry.probability;
  if (sum == 0) {
    ++fallbacks_;
    return fallback_.act(observation, policy_rng);
  }
  return sample_action(observation, distribution, policy_rng);
}

}  // namespace pokerzero
