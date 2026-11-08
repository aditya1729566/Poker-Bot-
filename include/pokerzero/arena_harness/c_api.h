#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pz_arena_session pz_arena_session;
typedef struct pz_arena_result {
  int status;
  char* json;
} pz_arena_result;

// This ABI is trusted-process-only. The HTTP adapter validates all input and
// generates private seeds and opaque identifiers before calling it.
pz_arena_session* pz_arena_create(const char* bot_name, const char* checkpoint_path, uint64_t root_seed);
void pz_arena_destroy(pz_arena_session* session);
pz_arena_result pz_arena_start(pz_arena_session* session, const char* hand_id, const char* replay_id);
pz_arena_result pz_arena_state(const pz_arena_session* session);
pz_arena_result pz_arena_act(pz_arena_session* session, const char* hand_id,
                             uint64_t expected_index, int action_kind, int64_t target);
pz_arena_result pz_arena_completed(const pz_arena_session* session, const char* hand_id);
pz_arena_result pz_arena_replay(const pz_arena_session* session, const char* hand_id);
void pz_arena_free(char* json);

#ifdef __cplusplus
}
#endif
