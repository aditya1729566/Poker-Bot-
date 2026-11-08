#include "pokerzero/arena_harness/session.hpp"

#include <cmath>
#include <sstream>
#include <stdexcept>

namespace pokerzero::arena_harness {
namespace {
constexpr std::uint64_t kArenaDealDomain = 0x4152454e41444541ULL;
constexpr std::uint64_t kArenaPolicyDomain = 0x4152454e41504f4cULL;
constexpr std::size_t kMaxHandsPerSession = 256;

bool valid_opaque_id(const std::string& value) {
  if (value.size() < 16 || value.size() > 128) return false;
  for (const unsigned char character : value)
    if (!(character >= 'A' && character <= 'Z') &&
        !(character >= 'a' && character <= 'z') &&
        !(character >= '0' && character <= '9') && character != '-' && character != '_')
      return false;
  return true;
}

const char* street_name(Street street) {
  switch (street) {
    case Street::Preflop: return "preflop";
    case Street::Flop: return "flop";
    case Street::Turn: return "turn";
    case Street::River: return "river";
    case Street::Terminal: return "terminal";
  }
  throw std::logic_error("unknown street");
}

const char* action_name(ActionKind kind) {
  switch (kind) {
    case ActionKind::Fold: return "fold";
    case ActionKind::Check: return "check";
    case ActionKind::Call: return "call";
    case ActionKind::RaiseTo: return "raiseTo";
  }
  throw std::logic_error("unknown action");
}

void cards_json(std::ostringstream& out, const std::vector<Card>& cards) {
  out << '[';
  for (std::size_t i = 0; i < cards.size(); ++i) {
    if (i) out << ',';
    out << '"' << card_string(cards[i]) << '"';
  }
  out << ']';
}

void hole_json(std::ostringstream& out, const std::array<Card, 2>& hole) {
  out << "[\"" << card_string(hole[0]) << "\",\"" << card_string(hole[1]) << "\"]";
}

void action_json(std::ostringstream& out, Action action) {
  out << "{\"kind\":\"" << action_name(action.kind) << '"';
  if (action.kind == ActionKind::RaiseTo) out << ",\"target\":" << action.target;
  out << '}';
}

void history_json(std::ostringstream& out, const std::vector<PublicAction>& actions) {
  out << '[';
  for (std::size_t i = 0; i < actions.size(); ++i) {
    if (i) out << ',';
    const auto& item = actions[i];
    out << "{\"street\":\"" << street_name(item.street) << "\",\"player\":" << item.player
        << ",\"action\":";
    action_json(out, item.action);
    out << ",\"committed\":" << item.committed << ",\"potAfter\":" << item.pot_after << '}';
  }
  out << ']';
}

void terminal_json(std::ostringstream& out, const TerminalView& view) {
  out << "{\"reason\":\"" << (view.showdown ? "showdown" : "fold")
      << "\",\"payoffs\":[" << view.payoffs[0] << ',' << view.payoffs[1] << "]}";
}

void legal_json(std::ostringstream& out, const Observation& observation) {
  const auto& legal = observation.legal;
  out << "{\"fold\":" << (legal.fold ? "true" : "false")
      << ",\"check\":" << (legal.check ? "true" : "false")
      << ",\"call\":" << (legal.call ? "true" : "false")
      << ",\"toCall\":" << observation.to_call << ",\"minRaiseTo\":";
  if (legal.min_raise_to) out << *legal.min_raise_to; else out << "null";
  out << ",\"maxRaiseTo\":";
  if (legal.max_raise_to) out << *legal.max_raise_to; else out << "null";
  out << '}';
}
}  // namespace

Session::Session(std::unique_ptr<IAgent> bot, std::uint64_t root_seed, GameConfig config)
    : bot_(std::move(bot)), root_seed_(root_seed), config_(config) {
  if (!bot_) throw std::invalid_argument("missing bot");
  if (config_.small_blind <= 0 || config_.big_blind <= config_.small_blind ||
      config_.initial_stack < config_.big_blind)
    throw std::invalid_argument("invalid game configuration");
  bot_->reset_match();
}

Result Session::error(int status, const char* message, const std::string& state) {
  std::string json = std::string("{\"apiVersion\":\"arena.v1\",\"message\":\"") + message + '"';
  if (!state.empty()) json += ",\"state\":" + state;
  json += '}';
  return {status, std::move(json)};
}

Result Session::start_hand(const std::string& hand_id, const std::string& replay_id) {
  if (game_ && !game_->terminal()) return error(409, "Current hand is unfinished.", state_json());
  if (!valid_opaque_id(hand_id) || !valid_opaque_id(replay_id))
    return error(422, "Invalid hand identifier.");
  if (completed_.size() >= kMaxHandsPerSession)
    return error(409, "Session hand limit reached.", game_ ? state_json() : std::string{});
  if (current_ && current_->hand_id == hand_id) return error(422, "Duplicate hand identifier.");
  for (const auto& previous : completed_)
    if (previous.hand_id == hand_id || previous.replay_id == replay_id)
      return error(422, "Duplicate hand identifier.");
  const auto deal_seed = derive_stream_seed(root_seed_, kArenaDealDomain, hand_count_);
  policy_rng_.emplace(derive_stream_seed(root_seed_, kArenaPolicyDomain, hand_count_));
  const int button = static_cast<int>(hand_count_ % 2);
  game_.emplace(Game::reset(deal_seed, config_, button));
  current_ = Hand{hand_id, replay_id, deal_seed, button, {}, {}, std::nullopt};
  ++hand_count_;
  advance_bot();
  return {200, std::string("{\"apiVersion\":\"arena.v1\",\"state\":") + state_json() + '}'};
}

void Session::advance_bot() {
  while (game_ && !game_->terminal() && game_->actor() == 1) {
    const auto observation = game_->observe(1);
    const Action action = bot_->act(observation, *policy_rng_);
    if (!observation.legal.contains(action)) throw std::logic_error("bot produced illegal action");
    const auto info = bot_->last_decision_info();
    game_->step(action);
    current_->actions.push_back(action);
    current_->decisions.push_back({1, info});
  }
  finish_if_terminal();
}

void Session::finish_if_terminal() {
  if (game_ && game_->terminal() && current_ && !current_->terminal) {
    current_->terminal = game_->terminal_view(0);
    completed_.push_back(*current_);
  }
}

std::string Session::state_json() const {
  if (!game_ || !current_) throw std::logic_error("hand not started");
  std::ostringstream out;
  out << "{\"handId\":\"" << current_->hand_id << "\",\"actionIndex\":"
      << current_->actions.size() << ",\"street\":\"" << street_name(game_->street())
      << "\",\"button\":" << current_->button << ",\"actor\":";
  if (game_->terminal()) out << "null"; else out << game_->actor();
  out << ",\"humanSeat\":0,\"bigBlind\":" << config_.big_blind;
  if (game_->terminal()) {
    const auto& view = *current_->terminal;
    out << ",\"stacks\":[" << view.stacks[0] << ',' << view.stacks[1]
        << "],\"streetContributions\":[" << view.street_contributions[0] << ','
        << view.street_contributions[1] << "],\"pot\":" << view.pot << ",\"board\":";
    cards_json(out, view.board);
    out << ",\"humanHole\":";
    hole_json(out, view.viewer_hole);
    out << ",\"opponentHole\":";
    if (view.opponent_hole) hole_json(out, *view.opponent_hole); else out << "null";
    out << ",\"publicActions\":";
    history_json(out, view.history);
    out << ",\"legal\":null,\"terminal\":";
    terminal_json(out, view);
  } else {
    // advance_bot() makes every nonterminal response a human decision.
    const auto observation = game_->observe(0);
    out << ",\"stacks\":[" << observation.stacks[0] << ',' << observation.stacks[1]
        << "],\"streetContributions\":[" << observation.street_contributions[0] << ','
        << observation.street_contributions[1] << "],\"pot\":" << observation.pot
        << ",\"board\":";
    cards_json(out, observation.board);
    out << ",\"humanHole\":";
    hole_json(out, observation.hole);
    out << ",\"opponentHole\":null,\"publicActions\":";
    history_json(out, observation.history);
    out << ",\"legal\":";
    legal_json(out, observation);
    out << ",\"terminal\":null";
  }
  out << '}';
  return out.str();
}

Result Session::state() const {
  if (!game_) return error(404, "No hand has started.");
  return {200, std::string("{\"apiVersion\":\"arena.v1\",\"state\":") + state_json() + '}'};
}

Result Session::act(const std::string& hand_id, std::size_t expected_index, Action action) {
  if (!game_) return error(404, "No hand has started.");
  if (hand_id != current_->hand_id || expected_index != current_->actions.size() || game_->terminal())
    return error(409, "Stale hand or action index.", state_json());
  if (game_->actor() != 0 || !game_->legal_actions().contains(action))
    return error(422, "Illegal action.", state_json());
  game_->step(action);
  current_->actions.push_back(action);
  current_->decisions.push_back({0, {}});
  finish_if_terminal();
  advance_bot();
  return state();
}

const Session::Hand* Session::find_completed(const std::string& hand_id) const {
  for (const auto& hand : completed_) if (hand.hand_id == hand_id) return &hand;
  return nullptr;
}

std::string Session::detail_json(const Hand& hand, const TerminalView& view) const {
  std::ostringstream out;
  out << "{\"apiVersion\":\"arena.v1\",\"replayId\":\"" << hand.replay_id
      << "\",\"publicActions\":";
  history_json(out, view.history);
  out << ",\"board\":";
  cards_json(out, view.board);
  out << ",\"revealedHole\":";
  if (view.opponent_hole) hole_json(out, *view.opponent_hole); else out << "null";
  out << ",\"terminal\":";
  terminal_json(out, view);
  out << ",\"analysis\":[";
  for (std::size_t i = 0; i < hand.decisions.size(); ++i) {
    if (i) out << ',';
    const auto& decision = hand.decisions[i];
    out << "{\"actor\":" << decision.actor;
    if (decision.actor == 1 && decision.info.available) {
      if (decision.info.table_visits && std::isfinite(*decision.info.table_visits))
        out << ",\"tableVisits\":" << *decision.info.table_visits;
      out << ",\"usedFallback\":" << (decision.info.used_fallback ? "true" : "false");
    }
    out << ",\"availability\":\""
        << (decision.actor == 0 ? "Human decision; no policy diagnostics."
            : decision.info.available ? "Policy metadata only; probabilities and equity unavailable."
                                      : "Policy diagnostics unavailable.") << "\"}";
  }
  out << "]}";
  return out.str();
}

Result Session::completed(const std::string& hand_id) const {
  const Hand* hand = find_completed(hand_id);
  if (!hand) return error(404, "Completed hand not found.");
  return {200, detail_json(*hand, *hand->terminal)};
}

Result Session::replay(const std::string& hand_id) const {
  const Hand* hand = find_completed(hand_id);
  if (!hand) return error(404, "Completed hand not found.");
  Game replayed = Game::reset(hand->deal_seed, config_, hand->button);
  for (const Action action : hand->actions) replayed.step(action);
  if (!replayed.terminal()) throw std::logic_error("replay did not terminate");
  const auto view = replayed.terminal_view(0);
  if (view.history != hand->terminal->history || view.board != hand->terminal->board ||
      view.payoffs != hand->terminal->payoffs || view.opponent_hole != hand->terminal->opponent_hole ||
      view.viewer_hole != hand->terminal->viewer_hole)
    throw std::logic_error("replay mismatch");
  return {200, detail_json(*hand, view)};
}

}  // namespace pokerzero::arena_harness
