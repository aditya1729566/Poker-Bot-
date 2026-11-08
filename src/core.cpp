#include "pokerzero/core.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace pokerzero {

Card::Card(int value) {
  if (value < 0 || value >= 52) throw std::invalid_argument("card ID must be 0..51");
  id = static_cast<std::uint8_t>(value);
}
int Card::rank() const {
  if (id >= 52) throw std::logic_error("invalid card");
  return 2 + id % 13;
}
int Card::suit() const {
  if (id >= 52) throw std::logic_error("invalid card");
  return id / 13;
}

Pcg32::Pcg32(std::uint64_t seed, std::uint64_t sequence) : increment_((sequence << 1U) | 1U) {
  (void)next();
  state_ += seed;
  (void)next();
}
std::uint32_t Pcg32::next() {
  const auto old = state_;
  state_ = old * 6364136223846793005ULL + increment_;
  const auto shifted = static_cast<std::uint32_t>(((old >> 18U) ^ old) >> 27U);
  const auto rotation = static_cast<unsigned>(old >> 59U);
  return (shifted >> rotation) | (shifted << ((-rotation) & 31U));
}
std::uint32_t Pcg32::bounded(std::uint32_t bound) {
  if (bound == 0) throw std::invalid_argument("zero RNG bound");
  const auto threshold = static_cast<std::uint32_t>(-bound) % bound;
  for (;;) {
    const auto value = next();
    if (value >= threshold) return value % bound;
  }
}
std::uint64_t splitmix64(std::uint64_t value) {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}
std::uint64_t derive_stream_seed(std::uint64_t root, std::uint64_t domain, std::uint64_t index) {
  return splitmix64(splitmix64(root ^ domain) + index * 0x9e3779b97f4a7c15ULL);
}
std::array<Card, 52> shuffled_deck(std::uint64_t seed) {
  std::array<Card, 52> deck;
  for (int i = 0; i < 52; ++i) deck[i] = Card(i);
  Pcg32 rng(seed);
  for (int i = 51; i > 0; --i) std::swap(deck[i], deck[rng.bounded(static_cast<std::uint32_t>(i + 1))]);
  return deck;
}

namespace {
std::uint64_t pack(int category, const std::vector<int>& kickers) {
  std::uint64_t value = static_cast<std::uint64_t>(category);
  for (int i = 0; i < 5; ++i) value = (value << 4U) | static_cast<std::uint64_t>(i < static_cast<int>(kickers.size()) ? kickers[i] : 0);
  return value;
}
int straight_high(const std::array<int, 15>& counts) {
  for (int high = 14; high >= 5; --high) {
    bool found = true;
    for (int offset = 0; offset < 5; ++offset) {
      const int rank = high - offset == 1 ? 14 : high - offset;
      found = found && counts[rank] > 0;
    }
    if (found) return high;
  }
  return 0;
}
std::uint64_t evaluate_five(const std::array<Card, 5>& cards) {
  std::array<int, 15> counts{};
  std::array<int, 4> suits{};
  for (const auto card : cards) {
    ++counts[card.rank()];
    ++suits[card.suit()];
  }
  const bool flush = std::any_of(suits.begin(), suits.end(), [](int n) { return n == 5; });
  const int straight = straight_high(counts);
  if (flush && straight) return pack(8, {straight});
  std::vector<int> fours, trips, pairs, singles;
  for (int rank = 14; rank >= 2; --rank) {
    if (counts[rank] == 4) fours.push_back(rank);
    else if (counts[rank] == 3) trips.push_back(rank);
    else if (counts[rank] == 2) pairs.push_back(rank);
    else if (counts[rank] == 1) singles.push_back(rank);
  }
  if (!fours.empty()) return pack(7, {fours[0], singles[0]});
  if (!trips.empty() && !pairs.empty()) return pack(6, {trips[0], pairs[0]});
  std::vector<int> descending;
  for (int rank = 14; rank >= 2; --rank) for (int n = 0; n < counts[rank]; ++n) descending.push_back(rank);
  if (flush) return pack(5, descending);
  if (straight) return pack(4, {straight});
  if (!trips.empty()) return pack(3, {trips[0], singles[0], singles[1]});
  if (pairs.size() == 2) return pack(2, {pairs[0], pairs[1], singles[0]});
  if (pairs.size() == 1) return pack(1, {pairs[0], singles[0], singles[1], singles[2]});
  return pack(0, descending);
}
}  // namespace

std::uint64_t evaluate(const std::vector<Card>& cards) {
  if (cards.size() < 5 || cards.size() > 7) throw std::invalid_argument("evaluate requires 5 to 7 cards");
  std::array<bool, 52> seen{};
  for (const auto card : cards) {
    if (card.id >= 52 || seen[card.id]) throw std::invalid_argument("invalid or duplicate card");
    seen[card.id] = true;
  }
  std::uint64_t best = 0;
  for (std::size_t a = 0; a + 4 < cards.size(); ++a)
    for (std::size_t b = a + 1; b + 3 < cards.size(); ++b)
      for (std::size_t c = b + 1; c + 2 < cards.size(); ++c)
        for (std::size_t d = c + 1; d + 1 < cards.size(); ++d)
          for (std::size_t e = d + 1; e < cards.size(); ++e)
            best = std::max(best, evaluate_five({cards[a], cards[b], cards[c], cards[d], cards[e]}));
  return best;
}

bool LegalActions::contains(Action action) const {
  switch (action.kind) {
    case ActionKind::Fold: return fold && action.target == 0;
    case ActionKind::Check: return check && action.target == 0;
    case ActionKind::Call: return call && action.target == 0;
    case ActionKind::RaiseTo:
      return min_raise_to && max_raise_to && action.target >= *min_raise_to && action.target <= *max_raise_to;
  }
  return false;
}

Game Game::reset(std::uint64_t seed, GameConfig config, int button) {
  return reset_with_deck(shuffled_deck(seed), config, button);
}
Game Game::reset_with_deck(const std::array<Card, 52>& deck, GameConfig config, int button) {
  if (button != 0 && button != 1) throw std::invalid_argument("button must be 0 or 1");
  if (config.small_blind <= 0 || config.big_blind <= config.small_blind || config.initial_stack < config.big_blind)
    throw std::invalid_argument("invalid game configuration");
  std::array<bool, 52> seen{};
  for (const auto card : deck) {
    if (card.id >= 52 || seen[card.id]) throw std::invalid_argument("deck is not a permutation");
    seen[card.id] = true;
  }
  Game game;
  game.config_ = config;
  game.deck_ = deck;
  game.button_ = button;
  game.actor_ = button;
  game.stacks_ = {config.initial_stack, config.initial_stack};
  game.last_full_raise_ = config.big_blind;
  const int big_blind = 1 - button;
  game.hole_[button] = {deck[0], deck[2]};
  game.hole_[big_blind] = {deck[1], deck[3]};
  game.full_board_ = {deck[5], deck[6], deck[7], deck[9], deck[11]};
  game.commit(button, config.small_blind);
  game.commit(big_blind, config.big_blind);
  game.current_bet_ = config.big_blind;
  game.check_invariants();
  return game;
}

LegalActions Game::legal_actions() const {
  LegalActions legal;
  if (terminal()) return legal;
  const int other = 1 - actor_;
  const Chips to_call = std::max<Chips>(0, current_bet_ - street_contributions_[actor_]);
  legal.fold = to_call > 0;
  legal.check = to_call == 0;
  legal.call = to_call > 0;
  if (stacks_[actor_] > to_call && stacks_[other] > 0 && can_raise_[actor_]) {
    const Chips maximum = std::min(street_contributions_[actor_] + stacks_[actor_],
                                    street_contributions_[other] + stacks_[other]);
    if (maximum > current_bet_) {
      const Chips minimum = current_bet_ + last_full_raise_;
      if (maximum >= minimum) {
        legal.min_raise_to = minimum;
        legal.max_raise_to = maximum;
      } else if (maximum == street_contributions_[actor_] + stacks_[actor_]) {
        legal.min_raise_to = maximum;
        legal.max_raise_to = maximum;
      }
    }
  }
  return legal;
}

Observation Game::observe(int player) const {
  if (terminal() || player != actor_) throw std::invalid_argument("only the acting player may be observed");
  return Observation{player, hole_[player], board_, street_, button_, stacks_, street_contributions_, pot_,
                     std::max<Chips>(0, current_bet_ - street_contributions_[player]), legal_actions(), history_};
}

void Game::commit(int player, Chips amount) {
  if (amount < 0 || amount > stacks_[player]) throw std::logic_error("invalid chip commitment");
  stacks_[player] -= amount;
  street_contributions_[player] += amount;
  total_contributions_[player] += amount;
  pot_ += amount;
}

void Game::step(Action action) {
  if (!legal_actions().contains(action)) throw std::invalid_argument("illegal action");
  const int player = actor_;
  const int other = 1 - player;
  const Street old_street = street_;
  Chips committed = 0;
  if (action.kind == ActionKind::Fold) {
    history_.push_back({old_street, player, action, 0, pot_});
    award_fold(other);
    check_invariants();
    return;
  }
  if (action.kind == ActionKind::Call) {
    committed = std::min(stacks_[player], current_bet_ - street_contributions_[player]);
    commit(player, committed);
  } else if (action.kind == ActionKind::RaiseTo) {
    const Chips raise_size = action.target - current_bet_;
    committed = action.target - street_contributions_[player];
    commit(player, committed);
    const bool full_raise = raise_size >= last_full_raise_;
    if (full_raise) last_full_raise_ = raise_size;
    can_raise_[other] = full_raise;
    current_bet_ = action.target;
  }
  acted_[player] = true;
  history_.push_back({old_street, player, action, committed, pot_});
  if (action.kind == ActionKind::RaiseTo) {
    actor_ = other;
  } else if ((acted_[other] && (street_contributions_[player] == street_contributions_[other] || stacks_[player] == 0)) ||
             (stacks_[player] == 0 && stacks_[other] == 0)) {
    close_round();
  } else {
    actor_ = other;
  }
  check_invariants();
}

void Game::close_round() {
  if (street_ == Street::River) {
    showdown();
    return;
  }
  do {
    street_ = static_cast<Street>(static_cast<int>(street_) + 1);
    if (street_ == Street::Flop) board_.insert(board_.end(), full_board_.begin(), full_board_.begin() + 3);
    else if (street_ == Street::Turn) board_.push_back(full_board_[3]);
    else if (street_ == Street::River) board_.push_back(full_board_[4]);
    street_contributions_ = {0, 0};
    current_bet_ = 0;
    last_full_raise_ = config_.big_blind;
    acted_ = {false, false};
    can_raise_ = {true, true};
    actor_ = 1 - button_;
  } while ((stacks_[0] == 0 || stacks_[1] == 0) && street_ != Street::River);
  if (stacks_[0] == 0 || stacks_[1] == 0) showdown();
}

void Game::showdown() {
  if (board_.size() != 5) throw std::logic_error("showdown before full board");
  if (total_contributions_[0] != total_contributions_[1]) {
    const int larger = total_contributions_[0] > total_contributions_[1] ? 0 : 1;
    const Chips refund = total_contributions_[larger] - total_contributions_[1 - larger];
    total_contributions_[larger] -= refund;
    stacks_[larger] += refund;
    pot_ -= refund;
  }
  std::array<std::uint64_t, 2> scores{};
  for (int player = 0; player < 2; ++player) {
    std::vector<Card> cards = board_;
    cards.push_back(hole_[player][0]);
    cards.push_back(hole_[player][1]);
    scores[player] = evaluate(cards);
  }
  if (scores[0] == scores[1]) {
    stacks_[button_] += (pot_ + 1) / 2;
    stacks_[1 - button_] += pot_ / 2;
  } else {
    stacks_[scores[0] > scores[1] ? 0 : 1] += pot_;
  }
  pot_ = 0;
  showdown_ = true;
  street_ = Street::Terminal;
}

void Game::award_fold(int winner) {
  stacks_[winner] += pot_;
  pot_ = 0;
  street_ = Street::Terminal;
}

std::array<Chips, 2> Game::terminal_payoffs() const {
  if (!terminal()) throw std::logic_error("hand is not terminal");
  return {stacks_[0] - config_.initial_stack, stacks_[1] - config_.initial_stack};
}

TerminalView Game::terminal_view(int viewer) const {
  if (!terminal() || (viewer != 0 && viewer != 1))
    throw std::invalid_argument("terminal view requires a completed hand and valid viewer");
  return {button_, hole_[viewer], showdown_ ? std::optional(hole_[1 - viewer]) : std::nullopt,
          board_, history_, stacks_, street_contributions_, pot_, terminal_payoffs(), showdown_};
}

void Game::check_invariants() const {
  if (stacks_[0] < 0 || stacks_[1] < 0 || pot_ < 0 ||
      stacks_[0] + stacks_[1] + pot_ != 2 * config_.initial_stack)
    throw std::logic_error("chip conservation invariant failed");
  if (terminal()) {
    if (pot_ != 0 || terminal_payoffs()[0] + terminal_payoffs()[1] != 0)
      throw std::logic_error("terminal payoff invariant failed");
  } else if (actor_ != 0 && actor_ != 1) {
    throw std::logic_error("invalid actor");
  }
}

std::vector<WeightedAction> RandomAgent::distribution(const Observation& observation) const {
  std::vector<Action> choices;
  if (observation.legal.fold) choices.push_back({ActionKind::Fold});
  if (observation.legal.check) choices.push_back({ActionKind::Check});
  if (observation.legal.call) choices.push_back({ActionKind::Call});
  if (observation.legal.min_raise_to) {
    choices.push_back({ActionKind::RaiseTo, *observation.legal.min_raise_to});
    const auto maximum = *observation.legal.max_raise_to;
    if (maximum != *observation.legal.min_raise_to) choices.push_back({ActionKind::RaiseTo, maximum});
  }
  if (choices.empty()) throw std::logic_error("no legal action");
  std::vector<WeightedAction> result;
  for (const auto action : choices) result.push_back({action, 1.0 / static_cast<double>(choices.size())});
  return result;
}

Action sample_action(const Observation& observation, const std::vector<WeightedAction>& distribution, Pcg32& rng) {
  if (distribution.empty()) throw std::invalid_argument("empty strategy distribution");
  double total = 0;
  for (const auto& entry : distribution) {
    if (!observation.legal.contains(entry.action) || !std::isfinite(entry.probability) || entry.probability < 0)
      throw std::invalid_argument("invalid strategy action or probability");
    total += entry.probability;
  }
  if (!std::isfinite(total) || std::abs(total - 1.0) > 1e-9) throw std::invalid_argument("strategy probabilities do not sum to one");
  const double draw = (static_cast<double>(rng.next()) + 0.5) / 4294967296.0;
  double cumulative = 0;
  for (const auto& entry : distribution) {
    cumulative += entry.probability;
    if (draw < cumulative) return entry.action;
  }
  return distribution.back().action;
}

Action RandomAgent::act(const Observation& observation, Pcg32& policy_rng) {
  return sample_action(observation, distribution(observation), policy_rng);
}

Action random_legal_action(const Observation& observation, Pcg32& rng) {
  RandomAgent agent;
  return agent.act(observation, rng);
}

std::string card_string(Card card) {
  constexpr char ranks[] = "23456789TJQKA";
  constexpr char suits[] = "cdhs";
  return {ranks[card.rank() - 2], suits[card.suit()]};
}
std::string action_string(Action action) {
  switch (action.kind) {
    case ActionKind::Fold: return "fold";
    case ActionKind::Check: return "check";
    case ActionKind::Call: return "call";
    case ActionKind::RaiseTo: return "raise_to_" + std::to_string(action.target);
  }
  throw std::logic_error("invalid action");
}

}  // namespace pokerzero
