#include "pokerzero/arena_harness/session.hpp"
#include "pokerzero/agents.hpp"
#include "pokerzero/core.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace pokerzero;
using namespace pokerzero::arena_harness;

namespace {
void check(bool condition) {
  if (!condition) throw std::runtime_error("arena harness assertion failed");
}

constexpr const char* kHandA = "hand_A_0123456789abcdef";
constexpr const char* kReplayA = "replay_A_0123456789abcdef";
constexpr const char* kHandB = "hand_B_0123456789abcdef";
constexpr const char* kReplayB = "replay_B_0123456789abcdef";

std::size_t action_index(const std::string& json) {
  constexpr const char* key = "\"actionIndex\":";
  const auto begin = json.find(key);
  check(begin != std::string::npos);
  return std::stoull(json.substr(begin + std::char_traits<char>::length(key)));
}

void terminal_boundary() {
  auto first = shuffled_deck(415);
  auto second = first;
  std::swap(second[1], second[11]);  // Opponent hole and undealt river differ.
  auto folded_a = Game::reset_with_deck(first);
  auto folded_b = Game::reset_with_deck(second);
  const auto public_a = folded_a.observe(0);
  const auto public_b = folded_b.observe(0);
  check(public_a.hole == public_b.hole && public_a.board == public_b.board);
  check(public_a.legal.contains({ActionKind::Fold}));
  folded_a.step({ActionKind::Fold});
  folded_b.step({ActionKind::Fold});
  const auto view_a = folded_a.terminal_view(0);
  const auto view_b = folded_b.terminal_view(0);
  check(!view_a.showdown && !view_b.showdown);
  check(!view_a.opponent_hole && !view_b.opponent_hole);
  check(view_a.board.empty() && view_b.board.empty());
  check(view_a.history == view_b.history && view_a.payoffs == view_b.payoffs);

  auto flop_fold = Game::reset_with_deck(first);
  flop_fold.step({ActionKind::Call});
  flop_fold.step({ActionKind::Check});
  const auto flop = flop_fold.observe(1).board;
  check(flop.size() == 3);
  flop_fold.step({ActionKind::RaiseTo, 100});
  flop_fold.step({ActionKind::Fold});
  const auto flop_view = flop_fold.terminal_view(0);
  check(flop_view.board == flop && !flop_view.opponent_hole);

  auto showdown = Game::reset_with_deck(first);
  showdown.step({ActionKind::RaiseTo, 10000});
  showdown.step({ActionKind::Call});
  const auto shown = showdown.terminal_view(0);
  check(shown.showdown && shown.opponent_hole && shown.board.size() == 5);
  check(shown.payoffs[0] + shown.payoffs[1] == 0);
}

void session_contract() {
  Session session(make_agent("passive"), 28491);
  check(session.state().status == 404);
  auto start = session.start_hand(kHandA, kReplayA);
  check(start.status == 200);
  check(start.json.find("\"actionIndex\":0") != std::string::npos);
  check(start.json.find("\"opponentHole\":null") != std::string::npos);
  check(start.json.find("\"minRaiseTo\":200") != std::string::npos);
  check(start.json.find("seed") == std::string::npos);
  check(session.start_hand(kHandB, kReplayB).status == 409);
  check(session.act(kHandA, 1, {ActionKind::Fold}).status == 409);
  check(session.act(kHandA, 0, {ActionKind::Check}).status == 422);
  check(session.act(kHandA, 0, {ActionKind::Fold}).status == 200);
  const auto first_detail = session.completed(kHandA);
  check(first_detail.status == 200);
  check(first_detail.json.find("\"revealedHole\":null") != std::string::npos);
  check(first_detail.json.find("\"board\":[]") != std::string::npos);
  check(first_detail.json == session.replay(kHandA).json);
  check(session.act(kHandA, 1, {ActionKind::Call}).status == 409);
  const auto quoted = session.start_hand("invalid\"id_0123456789abcdef", kReplayB);
  const auto control = session.start_hand("invalid\nid_0123456789abcdef", kReplayB);
  check(quoted.status == 422 && control.status == 422);
  check(quoted.json.find("invalid") == std::string::npos);
  check(control.json.find("invalid") == std::string::npos);

  start = session.start_hand(kHandB, kReplayB);
  check(start.status == 200);
  // Button alternates. The passive bot has already acted before the response.
  check(start.json.find("\"button\":1") != std::string::npos);
  check(start.json.find("\"actionIndex\":1") != std::string::npos);
  std::size_t index = action_index(start.json);
  while (session.completed(kHandB).status != 200) {
    const auto response = session.act(kHandB, index, {ActionKind::Check});
    check(response.status == 200);
    if (response.json.find("\"terminal\":null") == std::string::npos) break;
    index = action_index(response.json);
    check(index <= 10);
  }
  const auto second_detail = session.completed(kHandB);
  check(second_detail.status == 200);
  check(second_detail.json.find("\"revealedHole\":[") != std::string::npos);
  check(second_detail.json == session.replay(kHandB).json);
}
}  // namespace

int main() {
  terminal_boundary();
  session_contract();
  std::cout << "arena harness tests passed\n";
}
