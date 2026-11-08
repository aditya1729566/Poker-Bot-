#include "pokerzero/arena_harness/c_api.h"
#include "pokerzero/arena_harness/session.hpp"
#include "pokerzero/agents.hpp"
#include "pokerzero/restricted.hpp"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

struct pz_arena_session {
  pokerzero::arena_harness::Session value;
  pz_arena_session(std::unique_ptr<pokerzero::IAgent> bot, std::uint64_t seed)
      : value(std::move(bot), seed) {}
};

namespace {
pz_arena_result copy_result(const pokerzero::arena_harness::Result& result) {
  char* data = static_cast<char*>(std::malloc(result.json.size() + 1));
  if (!data) return {500, nullptr};
  std::memcpy(data, result.json.c_str(), result.json.size() + 1);
  return {result.status, data};
}

pz_arena_result internal_error() {
  return copy_result({500, "{\"apiVersion\":\"arena.v1\",\"message\":\"Arena engine error.\"}"});
}

template <typename Function>
pz_arena_result guarded(Function&& function) noexcept {
  try { return copy_result(function()); }
  catch (...) { return internal_error(); }
}
}  // namespace

extern "C" {
pz_arena_session* pz_arena_create(const char* bot_name, const char* checkpoint_path, uint64_t root_seed) {
  try {
    if (!bot_name) return nullptr;
    std::unique_ptr<pokerzero::IAgent> bot;
    if (std::string(bot_name) == "restricted") {
      if (!checkpoint_path) return nullptr;
      bot = std::make_unique<pokerzero::RestrictedAgent>(checkpoint_path);
    } else {
      bot = pokerzero::make_agent(bot_name);
    }
    return new pz_arena_session(std::move(bot), root_seed);
  } catch (...) { return nullptr; }
}

void pz_arena_destroy(pz_arena_session* session) { delete session; }

pz_arena_result pz_arena_start(pz_arena_session* session, const char* hand_id, const char* replay_id) {
  return guarded([&] {
    if (!session || !hand_id || !replay_id) throw std::invalid_argument("invalid input");
    return session->value.start_hand(hand_id, replay_id);
  });
}

pz_arena_result pz_arena_state(const pz_arena_session* session) {
  return guarded([&] {
    if (!session) throw std::invalid_argument("invalid input");
    return session->value.state();
  });
}

pz_arena_result pz_arena_act(pz_arena_session* session, const char* hand_id,
                             uint64_t expected_index, int action_kind, int64_t target) {
  return guarded([&] {
    if (!session || !hand_id || action_kind < 0 || action_kind > 3)
      throw std::invalid_argument("invalid input");
    return session->value.act(hand_id, expected_index,
                              {static_cast<pokerzero::ActionKind>(action_kind), target});
  });
}

pz_arena_result pz_arena_completed(const pz_arena_session* session, const char* hand_id) {
  return guarded([&] {
    if (!session || !hand_id) throw std::invalid_argument("invalid input");
    return session->value.completed(hand_id);
  });
}

pz_arena_result pz_arena_replay(const pz_arena_session* session, const char* hand_id) {
  return guarded([&] {
    if (!session || !hand_id) throw std::invalid_argument("invalid input");
    return session->value.replay(hand_id);
  });
}

void pz_arena_free(char* json) { std::free(json); }
}
