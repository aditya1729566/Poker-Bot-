#include "pokerzero/agents.hpp"
#include "pokerzero/match.hpp"
#include "pokerzero/small_games.hpp"
#include "pokerzero/restricted.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <sstream>
#include <unordered_set>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace pokerzero;

namespace {
int checks = 0;
void require(bool condition, const char* message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
Card card(const std::string& text) {
  const std::string ranks = "23456789TJQKA", suits = "cdhs";
  const auto rank = ranks.find(text.at(0)), suit = suits.find(text.at(1));
  if (rank == std::string::npos || suit == std::string::npos) throw std::invalid_argument("bad card text");
  return Card(static_cast<int>(suit * 13 + rank));
}
std::vector<Card> cards(std::initializer_list<const char*> texts) {
  std::vector<Card> result;
  for (const char* text : texts) result.push_back(card(text));
  return result;
}
void place(std::array<Card, 52>& deck, int position, Card desired) {
  for (int i = 0; i < 52; ++i) if (deck[i] == desired) { std::swap(deck[position], deck[i]); return; }
  throw std::logic_error("missing card");
}
void finish_passively(Game& game) {
  while (!game.terminal()) {
    const auto observation = game.observe(game.actor());
    game.step({observation.legal.check ? ActionKind::Check : ActionKind::Call});
  }
}
void test_cards_and_rng() {
  for (int i = 0; i < 52; ++i) {
    const Card c(i);
    require(c.rank() >= 2 && c.rank() <= 14 && c.suit() >= 0 && c.suit() < 4, "card encoding");
  }
  bool rejected = false;
  try { Card bad(52); (void)bad; } catch (const std::invalid_argument&) { rejected = true; }
  require(rejected, "card bounds");
  const auto deck = shuffled_deck(42);
  require(deck == shuffled_deck(42), "deck determinism");
  std::array<bool, 52> seen{};
  for (Card c : deck) { require(!seen[c.id], "duplicate shuffled card"); seen[c.id] = true; }
  require(deck != shuffled_deck(43), "distinct seeds");
  Pcg32 a(123), b(123);
  for (int i = 0; i < 1000; ++i) require(a.next() == b.next(), "RNG determinism");
  std::array<int, 52> first_card_counts{};
  for (std::uint64_t seed = 0; seed < 100000; ++seed) ++first_card_counts[shuffled_deck(seed)[0].id];
  // A deliberately loose deterministic smoke check; uniformity follows from rejection sampling.
  for (const int count : first_card_counts) require(count > 1500 && count < 2350, "shuffle first-card frequency");
  std::unordered_set<std::uint64_t> allocated_seeds;
  for (const std::uint64_t root : {42ULL, 43ULL, 44ULL, 51ULL, 52ULL, 53ULL})
    for (std::uint64_t block = 0; block < 100000; ++block)
      require(allocated_seeds.insert(derive_stream_seed(root, kMatchDealDomain, block)).second,
              "declared match cohorts have disjoint deal seeds");
  for (std::uint64_t iteration = 0; iteration < 50000; ++iteration)
    for (int player = 0; player < 2; ++player)
      require(allocated_seeds.insert(derive_stream_seed(42, kTrainingDealDomain + player, iteration)).second,
              "training and match deal seeds are disjoint");
}
void test_evaluator() {
  require((evaluate(cards({"Ac", "2d", "3h", "4s", "5c"})) >> 20) == 4, "wheel straight");
  require(evaluate(cards({"Ac", "2d", "3h", "4s", "5c"})) < evaluate(cards({"2c", "3d", "4h", "5s", "6c"})), "straight ordering");
  require((evaluate(cards({"Ac", "Kc", "Qc", "Jc", "Tc"})) >> 20) == 8, "royal flush");
  require((evaluate(cards({"Ac", "Ad", "Ah", "Ks", "Kc"})) >> 20) == 6, "full house");
  require((evaluate(cards({"Ac", "Ad", "Ah", "As", "Kc"})) >> 20) == 7, "quads");
  require(evaluate(cards({"Ac", "Ad", "Kh", "Qs", "Jc"})) > evaluate(cards({"Kc", "Kd", "Ah", "Qs", "Jc"})), "pair rank");
  require(evaluate(cards({"Ac", "Ad", "Kh", "Qs", "Jc"})) > evaluate(cards({"Ac", "Ad", "Kh", "Qs", "Tc"})), "pair kicker");
  require((evaluate(cards({"Ac", "Kc", "Qc", "Jc", "Tc", "2d", "3h"})) >> 20) == 8, "seven card best five");
  bool rejected = false;
  try { (void)evaluate(cards({"Ac", "Ac", "2h", "3d", "4s"})); } catch (const std::invalid_argument&) { rejected = true; }
  require(rejected, "duplicate evaluation card");
  constexpr std::array<std::uint64_t, 9> expected = {1302540, 1098240, 123552, 54912, 10200, 5108, 3744, 624, 40};
  std::array<std::uint64_t, 9> counts{};
  std::vector<Card> hand(5);
  for (int a = 0; a < 48; ++a) for (int b = a + 1; b < 49; ++b)
    for (int c = b + 1; c < 50; ++c) for (int d = c + 1; d < 51; ++d)
      for (int e = d + 1; e < 52; ++e) {
        hand = {Card(a), Card(b), Card(c), Card(d), Card(e)};
        ++counts[evaluate(hand) >> 20];
      }
  require(counts == expected, "exhaustive five-card category frequencies");
}
void test_betting() {
  Game game = Game::reset(1);
  require(game.actor() == 0 && game.pot() == 150, "blind posting and button order");
  auto o = game.observe(0);
  require(o.to_call == 50 && o.stacks[0] == 9950 && o.stacks[1] == 9900, "blind stacks");
  require(o.legal.min_raise_to == 200, "preflop minimum raise");
  game.step({ActionKind::Call});
  require(game.actor() == 1 && game.pot() == 200, "big blind option");
  game.step({ActionKind::Check});
  require(game.street() == Street::Flop && game.actor() == 1 && game.observe(1).board.size() == 3, "flop actor and board");
  finish_passively(game);
  const auto payoff = game.terminal_payoffs();
  require(payoff[0] + payoff[1] == 0, "showdown zero sum");
  Game fold = Game::reset(2);
  fold.step({ActionKind::Fold});
  require(fold.terminal_payoffs() == std::array<Chips, 2>{-50, 50}, "fold payout");
  Game raised = Game::reset(3);
  raised.step({ActionKind::RaiseTo, 200});
  require(raised.observe(1).to_call == 100 && raised.observe(1).legal.min_raise_to == 300, "full raise size");
  Game short_raise = Game::reset(4, {250, 50, 100});
  short_raise.step({ActionKind::RaiseTo, 200});
  require(short_raise.observe(1).legal.min_raise_to == 250 && short_raise.observe(1).legal.max_raise_to == 250, "short all-in raise");
  short_raise.step({ActionKind::RaiseTo, 250});
  require(!short_raise.observe(0).legal.min_raise_to, "short all-in does not reopen");
  short_raise.step({ActionKind::Call});
  require(short_raise.terminal() && short_raise.terminal_payoffs()[0] + short_raise.terminal_payoffs()[1] == 0, "all-in runout");
  Game allin = Game::reset(5);
  allin.step({ActionKind::RaiseTo, 10000});
  allin.step({ActionKind::Call});
  require(allin.terminal() && allin.terminal_payoffs()[0] + allin.terminal_payoffs()[1] == 0, "preflop all-in");
  bool rejected = false;
  try { Game bad = Game::reset(6); bad.step({ActionKind::RaiseTo, 199}); } catch (const std::invalid_argument&) { rejected = true; }
  require(rejected, "illegal raise rejected");
  Game other_button = Game::reset(7, {}, 1);
  require(other_button.actor() == 1 && other_button.observe(1).to_call == 50, "button one preflop order");
  other_button.step({ActionKind::Call});
  other_button.step({ActionKind::Check});
  require(other_button.actor() == 0 && other_button.street() == Street::Flop, "button one postflop order");

  auto tied_deck = shuffled_deck(8);
  place(tied_deck, 5, card("Ac"));
  place(tied_deck, 6, card("Kc"));
  place(tied_deck, 7, card("Qc"));
  place(tied_deck, 9, card("Jc"));
  place(tied_deck, 11, card("Tc"));
  Game tie = Game::reset_with_deck(tied_deck);
  finish_passively(tie);
  require(tie.terminal_payoffs() == std::array<Chips, 2>{0, 0}, "board-playing split pot");

  bool invalid_deck = false;
  tied_deck[0] = tied_deck[1];
  try { (void)Game::reset_with_deck(tied_deck); } catch (const std::invalid_argument&) { invalid_deck = true; }
  require(invalid_deck, "duplicate deck rejected");
}
void test_leakage_and_replay() {
  auto deck1 = shuffled_deck(100);
  auto deck2 = deck1;
  std::swap(deck2[1], deck2[12]);
  std::swap(deck2[3], deck2[13]);
  std::swap(deck2[9], deck2[14]);
  Game a = Game::reset_with_deck(deck1), b = Game::reset_with_deck(deck2);
  auto oa = a.observe(0), ob = b.observe(0);
  require(oa.hole == ob.hole && oa.board == ob.board && oa.pot == ob.pot && oa.legal.min_raise_to == ob.legal.min_raise_to, "same initial observation");
  Pcg32 rng1(55), rng2(55);
  require(random_legal_action(oa, rng1) == random_legal_action(ob, rng2), "private cards/future deck do not change action");
  RandomAgent agent;
  const auto da = agent.distribution(oa), db = agent.distribution(ob);
  require(da.size() == db.size(), "same distribution size");
  for (std::size_t i = 0; i < da.size(); ++i)
    require(da[i].action == db[i].action && da[i].probability == db[i].probability, "same distribution probabilities");
  a.step({ActionKind::Call}); b.step({ActionKind::Call});
  a.step({ActionKind::Check}); b.step({ActionKind::Check});
  require(a.observe(1).board == b.observe(1).board, "same current board");
  a.step({ActionKind::Check}); b.step({ActionKind::Check});
  oa = a.observe(0); ob = b.observe(0);
  require(oa.hole == ob.hole && oa.board == ob.board && oa.history == ob.history, "same flop observation");
  Pcg32 rng3(77), rng4(77);
  require(random_legal_action(oa, rng3) == random_legal_action(ob, rng4), "flop action independent of hidden cards");
  bool bad_distribution = false;
  try { Pcg32 rng(1); (void)sample_action(oa, {{{ActionKind::Check}, 0.4}}, rng); }
  catch (const std::invalid_argument&) { bad_distribution = true; }
  require(bad_distribution, "unnormalized strategy rejected");

  Game first = Game::reset(999), replay = Game::reset(999);
  Pcg32 policy(1234);
  while (!first.terminal()) {
    auto before = first.observe(first.actor());
    auto again = replay.observe(replay.actor());
    require(before.hole == again.hole && before.board == again.board && before.history == again.history && before.pot == again.pot, "replay observation");
    const auto action = random_legal_action(before, policy);
    first.step(action); replay.step(action);
  }
  require(first.terminal_payoffs() == replay.terminal_payoffs(), "replay payoff");
}
void test_random_hands() {
  for (std::uint64_t i = 0; i < 10000; ++i) {
    Game game = Game::reset(splitmix64(i), {}, static_cast<int>(i & 1));
    Pcg32 policy(splitmix64(i ^ 0xabcULL));
    int steps = 0;
    while (!game.terminal()) {
      require(++steps < 100, "hand termination");
      const auto observation = game.observe(game.actor());
      const auto action = random_legal_action(observation, policy);
      require(observation.legal.contains(action), "sampled action is legal");
      game.step(action);
      const auto stacks = game.stacks();
      require(stacks[0] >= 0 && stacks[1] >= 0 && stacks[0] + stacks[1] + game.pot() == 20000, "chip conservation");
    }
    require(game.terminal_payoffs()[0] + game.terminal_payoffs()[1] == 0, "random terminal zero sum");
  }
}
void test_equity_and_baselines() {
  auto deck = shuffled_deck(111);
  place(deck, 0, card("Ac")); place(deck, 2, card("Kc"));
  place(deck, 5, card("Qc")); place(deck, 6, card("Jc")); place(deck, 7, card("Tc"));
  place(deck, 9, card("2d")); place(deck, 11, card("3h"));
  Game game = Game::reset_with_deck(deck);
  game.step({ActionKind::Call}); game.step({ActionKind::Check});
  for (int street = 0; street < 3; ++street) {
    game.step({ActionKind::Check}); game.step({ActionKind::Check});
    if (street == 0) require(game.street() == Street::Turn, "turn reached");
    if (street == 1) require(game.street() == Street::River, "river reached");
    if (street == 2) break;
  }
  // Recreate a river decision because the passive line above has reached showdown.
  game = Game::reset_with_deck(deck);
  game.step({ActionKind::Call}); game.step({ActionKind::Check});
  game.step({ActionKind::Check}); game.step({ActionKind::Check});
  game.step({ActionKind::Check}); game.step({ActionKind::Check});
  require(game.street() == Street::River && game.actor() == 1, "river equity decision");
  game.step({ActionKind::Check});
  const auto observation = game.observe(0);
  Pcg32 rng(1);
  const auto estimate = estimate_uniform_equity(observation, rng);
  require(estimate.exact && estimate.outcomes == 990 && estimate.equity == 1.0 && estimate.standard_error == 0,
          "exact river nuts equity");
  for (const std::string name : {"random", "passive", "tight_aggressive", "loose_aggressive", "equity"}) {
    auto agent = make_agent(name);
    Pcg32 policy(456);
    const Action action = agent->act(observation, policy);
    require(observation.legal.contains(action), "baseline legal action");
  }
  Pcg32 zero(1);
  bool rejected = false;
  try { (void)estimate_uniform_equity(observation, zero, 0); } catch (const std::invalid_argument&) { rejected = true; }
  require(rejected, "zero equity samples rejected");
}
void test_pairing() {
  const auto [first, second] = paired_decks(123);
  for (int i = 4; i < 52; ++i) require(first[i] == second[i], "paired future deck identical");
  Game a = Game::reset_with_deck(first, {}, 0), b = Game::reset_with_deck(second, {}, 1);
  const auto a0 = a.observe(0).hole;
  const auto b1 = b.observe(1).hole;
  a.step({ActionKind::Call}); b.step({ActionKind::Call});
  require(a0 == b.observe(0).hole, "agent zero keeps hole cards after seat swap");
  require(b1 == a.observe(1).hole, "agent one keeps hole cards after seat swap");
  a.step({ActionKind::Check}); b.step({ActionKind::Check});
  require(a.observe(1).board == b.observe(0).board, "paired public board identical");
  RandomAgent x, y;
  const auto report = run_paired_match(x, y, 100, 42);
  require(report.hands == 200 && report.blocks == 100 && report.net_chips[0] + report.net_chips[1] == 0,
          "paired match accounting");
  require(report.timeouts == 0 && report.illegal_actions == 0, "paired match action validity");
}
struct CollectedDiagnostics final : MatchDiagnosticObserver {
  std::vector<ActionDiagnostic> actions;
  std::vector<BlockDiagnostic> blocks;
  void on_action(const ActionDiagnostic& action) override { actions.push_back(action); }
  void on_block(const BlockDiagnostic& block) override { blocks.push_back(block); }
};
std::string stable_trace(const CollectedDiagnostics& diagnostics) {
  std::ostringstream trace;
  for (const auto& block : diagnostics.blocks)
    trace << block.seed << ':' << block.block << ':' << block.candidate_leg_returns[0] << ':'
          << block.candidate_leg_returns[1] << ';';
  for (const auto& action : diagnostics.actions)
    trace << action.block << ':' << action.leg << ':' << action.action_index << ':' << action.actor
          << ':' << action.button << ':' << static_cast<int>(action.street) << ':' << action.pot
          << ':' << static_cast<int>(action.proposed.kind) << ':' << action.proposed.target
          << ':' << static_cast<int>(action.chosen.kind) << ':' << action.chosen.target
          << ':' << action.policy.used_fallback << ':' << action.candidate_hand_return << ';';
  return trace.str();
}
void test_match_diagnostics() {
  RandomAgent first_a, second_a, first_b, second_b, first_plain, second_plain;
  CollectedDiagnostics a, b;
  const auto report_a = run_paired_match(first_a, second_a, 50, 51, 25.0, &a);
  const auto report_b = run_paired_match(first_b, second_b, 50, 51, 25.0, &b);
  const auto report_plain = run_paired_match(first_plain, second_plain, 50, 51);
  require(stable_trace(a) == stable_trace(b), "deterministic block and action trace");
  require(a.blocks.size() == report_a.blocks && a.actions.size() == report_a.actions,
          "diagnostic row counts reconcile with report");
  require(report_a.net_chips == report_b.net_chips && report_a.agent0_by_button == report_b.agent0_by_button,
          "diagnostic mode preserves match outcomes");
  require(report_a.net_chips == report_plain.net_chips &&
          report_a.agent0_by_button == report_plain.agent0_by_button,
          "optional diagnostics leave uninstrumented outcomes unchanged");
  Chips net = 0;
  std::array<Chips, 2> seat_returns{};
  for (std::size_t index = 0; index < a.blocks.size(); ++index) {
    const auto& block = a.blocks[index];
    require(block.seed == 51 && block.block == index, "block seed/index trace");
    net += block.candidate_leg_returns[0] + block.candidate_leg_returns[1];
    seat_returns[0] += block.candidate_leg_returns[0];
    seat_returns[1] += block.candidate_leg_returns[1];
  }
  require(net == report_a.net_chips[0] && seat_returns == report_a.agent0_by_button,
          "block returns reconcile with aggregate net and seats");
  std::uint64_t timeouts = 0, illegal = 0;
  for (const auto& action : a.actions) {
    require(action.button == action.leg && action.actor >= 0 && action.actor <= 1,
            "diagnostic button and actor");
    require(action.candidate_hand_return == a.blocks[action.block].candidate_leg_returns[action.leg],
            "action hand return reconciles with block");
    timeouts += action.timeout;
    illegal += action.illegal;
    require(!action.policy.available && !action.policy.used_fallback && !action.policy.table_visits,
            "non-table policy has no table metadata");
  }
  require(timeouts == report_a.timeouts && illegal == report_a.illegal_actions,
          "diagnostic failures reconcile with aggregate failures");
  RestrictedSolver solver(51);
  solver.train(10);
  const std::string path = "/tmp/pokerzero_e001_test.strategy";
  solver.save(path);
  RestrictedAgent control(path);
  BaselineAgent passive(BaselineStyle::Passive);
  CollectedDiagnostics table_trace;
  const auto table_report = run_paired_match(control, passive, 20, 52, 25.0, &table_trace);
  std::uint64_t candidate_actions = 0, fallback_actions = 0;
  for (const auto& action : table_trace.actions) if (action.actor == 0) {
    ++candidate_actions;
    require(action.policy.available, "restricted policy metadata available");
    fallback_actions += action.policy.used_fallback;
    if (!action.policy.used_fallback) require(action.policy.table_visits && *action.policy.table_visits >= 10,
                                               "learned-table decision meets visit threshold");
  }
  require(candidate_actions > 0 && fallback_actions == table_report.fallbacks[0],
          "fallback decisions reconcile with aggregate count");
  std::filesystem::remove(path);
  std::ostringstream block_json, action_json;
  JsonlMatchDiagnostics writer(block_json, action_json);
  writer.on_block(a.blocks.front()); writer.on_action(a.actions.front());
  require(block_json.str().find("candidate_leg_returns_chips") != std::string::npos &&
          action_json.str().find("table_visits") != std::string::npos &&
          action_json.str().find("hole") == std::string::npos &&
          action_json.str().find("deck") == std::string::npos,
          "JSONL contains public diagnostics but no hidden cards or deck");
}
void test_small_solver() {
  SmallSolver kuhn(SmallGameKind::Kuhn);
  const double initial = kuhn.exploitability();
  kuhn.train(10000);
  const double trained = kuhn.exploitability();
  require(trained < initial && trained < 0.02, "Kuhn CFR exact best-response convergence");
  require(kuhn.infosets() > 0 && kuhn.iterations() == 10000, "Kuhn CFR training state");
  SmallSolver plus(SmallGameKind::Kuhn, CfrAlgorithm::Plus);
  plus.train(10000);
  require(plus.exploitability() < 0.02, "Kuhn CFR+ exact best-response convergence");
  const std::string path = "/tmp/pokerzero_kuhn_test.strategy";
  plus.save(path);
  const auto loaded = SmallSolver::load(path);
  require(std::abs(loaded.exploitability() - plus.exploitability()) < 1e-10, "small-game artifact round trip");
  std::filesystem::remove(path);
  SmallSolver leduc(SmallGameKind::Leduc);
  const double leduc_initial = leduc.exploitability();
  leduc.train(100);
  require(leduc.exploitability() < leduc_initial, "Leduc CFR best-response improvement");
  SmallSolver mc1(SmallGameKind::Kuhn, CfrAlgorithm::ExternalSampling, 42);
  SmallSolver mc2(SmallGameKind::Kuhn, CfrAlgorithm::ExternalSampling, 42);
  mc1.train(100000); mc2.train(100000);
  require(mc1.exploitability() < 0.02, "Kuhn external-sampling convergence");
  require(mc1.exploitability() == mc2.exploitability() && mc1.expected_value() == mc2.expected_value(),
          "fixed-seed MCCFR reproducibility");
}
void test_restricted_abstraction() {
  Game game = Game::reset(321);
  const auto initial = game.observe(0);
  const auto choices = restricted_actions(initial);
  require(choices.size() == 5, "preflop restricted action count");
  require(choices[0].label == 'f' && choices[1].label == 'c' &&
          choices[2].action.target == 250 && choices[3].action.target == 350 &&
          choices[4].action.target == 10000, "preflop sizing and all-in");
  EquityBuckets buckets;
  std::set<int> preflop_classes;
  for (int first = 0; first < 52; ++first)
    for (int second = first + 1; second < 52; ++second) {
      auto observation = initial;
      observation.hole = {Card(first), Card(second)};
      const int bucket = buckets.bucket(observation);
      require(bucket >= 0 && bucket < 169, "preflop bucket bounds");
      preflop_classes.insert(bucket);
    }
  require(preflop_classes.size() == 169, "169 preflop classes");
  game.step({ActionKind::RaiseTo, 275});
  require(abstract_public_history(game.observe(1)) == "m", "off-tree raise maps to nearest pot fraction");
  const auto key = restricted_info_key(initial, choices, buckets);
  require(!key.empty(), "restricted infoset key");
  RestrictedSolver solver1(42), solver2(42);
  solver1.train(10); solver2.train(10);
  require(solver1.infosets() == solver2.infosets(), "restricted fixed-seed reproducibility");
  const std::string path = "/tmp/pokerzero_hunl_test.strategy";
  solver1.save(path);
  RestrictedAgent agent(path);
  auto deck1 = shuffled_deck(1000), deck2 = deck1;
  std::swap(deck2[1], deck2[12]);
  std::swap(deck2[3], deck2[13]);
  std::swap(deck2[9], deck2[14]);
  const auto a = Game::reset_with_deck(deck1).observe(0);
  const auto b = Game::reset_with_deck(deck2).observe(0);
  Pcg32 first_rng(55), second_rng(55);
  require(agent.act(a, first_rng) == agent.act(b, second_rng), "restricted agent hidden-card leakage check");
  const auto first_info = agent.last_decision_info();
  Pcg32 repeated_rng(55);
  (void)agent.act(a, repeated_rng);
  const auto repeated_info = agent.last_decision_info();
  require(first_info.available == repeated_info.available &&
          first_info.used_fallback == repeated_info.used_fallback &&
          first_info.table_visits == repeated_info.table_visits,
          "restricted diagnostics independent of hidden cards and future deck");
  std::filesystem::remove(path);
}
}  // namespace

int main() {
  try {
    test_cards_and_rng();
    test_evaluator();
    test_betting();
    test_leakage_and_replay();
    test_random_hands();
    test_equity_and_baselines();
    test_pairing();
    test_match_diagnostics();
    test_small_solver();
    test_restricted_abstraction();
    std::cout << "PASS " << checks << " checks\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
    return 1;
  }
}
