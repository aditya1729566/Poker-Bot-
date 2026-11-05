#include "pokerzero/agents.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace pokerzero {

namespace {
double result_for(const std::vector<Card>& hero, const std::vector<Card>& opponent) {
  const auto ours = evaluate(hero), theirs = evaluate(opponent);
  return ours > theirs ? 1.0 : ours == theirs ? 0.5 : 0.0;
}

Action passive_action(const Observation& observation) {
  return {observation.legal.check ? ActionKind::Check : ActionKind::Call};
}

Action sized_raise(const Observation& observation, double fraction) {
  if (!observation.legal.min_raise_to) return passive_action(observation);
  const Chips after_call = observation.pot + observation.to_call;
  const Chips raw = observation.street_contributions[observation.player] + observation.to_call +
                    static_cast<Chips>(std::llround(fraction * static_cast<double>(after_call)));
  const Chips target = std::clamp(raw, *observation.legal.min_raise_to, *observation.legal.max_raise_to);
  return {ActionKind::RaiseTo, target};
}
}  // namespace

EquityEstimate estimate_uniform_equity(const Observation& observation, Pcg32& rng,
                                       std::uint32_t samples, bool exact_when_feasible) {
  if (observation.board.size() != 0 && observation.board.size() != 3 &&
      observation.board.size() != 4 && observation.board.size() != 5)
    throw std::invalid_argument("invalid board size for equity");
  if (samples == 0) throw std::invalid_argument("equity samples must be positive");
  std::array<bool, 52> visible{};
  for (const Card card : observation.hole) {
    if (card.id >= 52 || visible[card.id]) throw std::invalid_argument("invalid visible cards");
    visible[card.id] = true;
  }
  for (const Card card : observation.board) {
    if (card.id >= 52 || visible[card.id]) throw std::invalid_argument("invalid visible cards");
    visible[card.id] = true;
  }
  std::vector<Card> remaining;
  for (int id = 0; id < 52; ++id) if (!visible[id]) remaining.push_back(Card(id));
  const int future = 5 - static_cast<int>(observation.board.size());
  double sum = 0, sum_squares = 0;
  std::uint64_t count = 0;
  if (future == 0 && exact_when_feasible) {
    std::vector<Card> hero = observation.board;
    hero.insert(hero.end(), observation.hole.begin(), observation.hole.end());
    for (std::size_t a = 0; a + 1 < remaining.size(); ++a)
      for (std::size_t b = a + 1; b < remaining.size(); ++b) {
        std::vector<Card> opponent = observation.board;
        opponent.push_back(remaining[a]); opponent.push_back(remaining[b]);
        sum += result_for(hero, opponent);
        ++count;
      }
    return {sum / static_cast<double>(count), 0, count, true};
  }
  for (std::uint32_t iteration = 0; iteration < samples; ++iteration) {
    auto pool = remaining;
    for (int i = 0; i < 2 + future; ++i) {
      const int offset = static_cast<int>(rng.bounded(static_cast<std::uint32_t>(pool.size() - i)));
      std::swap(pool[i], pool[i + offset]);
    }
    std::vector<Card> board = observation.board;
    for (int i = 0; i < future; ++i) board.push_back(pool[2 + i]);
    std::vector<Card> hero = board, opponent = board;
    hero.insert(hero.end(), observation.hole.begin(), observation.hole.end());
    opponent.push_back(pool[0]); opponent.push_back(pool[1]);
    const double value = result_for(hero, opponent);
    sum += value;
    sum_squares += value * value;
    ++count;
  }
  const double mean = sum / static_cast<double>(count);
  const double variance = std::max(0.0, sum_squares / static_cast<double>(count) - mean * mean);
  return {mean, std::sqrt(variance / static_cast<double>(count)), count, false};
}

Action BaselineAgent::act(const Observation& observation, Pcg32& policy_rng) {
  if (style_ == BaselineStyle::Passive) return passive_action(observation);
  const std::uint32_t samples = style_ == BaselineStyle::Equity ? 512 : 256;
  const auto estimate = estimate_uniform_equity(observation, policy_rng, samples);
  const double equity = estimate.equity;
  const double pot_odds = observation.to_call == 0 ? 0.0 :
      static_cast<double>(observation.to_call) / static_cast<double>(observation.pot + observation.to_call);
  double call_margin = 0.05, raise_threshold = 0.65, bet_threshold = 0.58, size = 0.75;
  if (style_ == BaselineStyle::TightAggressive) {
    call_margin = 0.10; raise_threshold = 0.72; bet_threshold = 0.64; size = 0.75;
  } else if (style_ == BaselineStyle::LooseAggressive) {
    call_margin = -0.05; raise_threshold = 0.58; bet_threshold = 0.52; size = 1.25;
  }
  if (observation.to_call > 0) {
    if (equity < pot_odds + call_margin) return {ActionKind::Fold};
    if (equity >= raise_threshold && observation.legal.min_raise_to) return sized_raise(observation, size);
    return {ActionKind::Call};
  }
  if (equity >= bet_threshold && observation.legal.min_raise_to) return sized_raise(observation, size);
  return {ActionKind::Check};
}

std::unique_ptr<IAgent> make_agent(const std::string& name) {
  if (name == "random") return std::make_unique<RandomAgent>();
  if (name == "passive") return std::make_unique<BaselineAgent>(BaselineStyle::Passive);
  if (name == "tight_aggressive") return std::make_unique<BaselineAgent>(BaselineStyle::TightAggressive);
  if (name == "loose_aggressive") return std::make_unique<BaselineAgent>(BaselineStyle::LooseAggressive);
  if (name == "equity") return std::make_unique<BaselineAgent>(BaselineStyle::Equity);
  throw std::invalid_argument("unknown agent: " + name);
}

}  // namespace pokerzero
