# PokerZero Arena API v1 (proposal)

## Boundary and plan

The C++ `Game` remains the only authority for cards, turns, legal actions, pot,
stacks, showdown, and payoff. The adapter owns private state in process memory.
The browser receives an explicitly constructed player view, never a serialized
`GameState`. Arena sessions and their logs are excluded from benchmark cohorts.

Milestones: F1 contract and isolation; F2 one complete human/bot hand; F3 replay
and analysis where diagnostics exist; F4 read-only research reports; F5 access,
responsive behavior, and integration checks. Invariants: integer chip values,
engine-supplied legal actions, monotone action index, no preterminal private
opponent cards or future board, and reproducible seed/config/checkpoint keys.

## Transport

All JSON responses carry `apiVersion: "arena.v1"`. Local HTTP only, bound to
loopback. State-changing requests require an allowed local `Origin`; all
session-specific reads and writes require a session-scoped bearer credential
issued as an HttpOnly, SameSite=Strict cookie on `POST /sessions`. The cookie
expires with its 30 minute idle session; replay IDs are scoped to the same
credential. Reject missing or mismatched credentials with `403`, and never use
wildcard CORS. Requests use
`Content-Type: application/json`. Session and replay IDs are opaque random
tokens, held in memory, with a 30 minute idle expiry. `404` means unknown
session; `410` means expired session. No training/final benchmark endpoints.

### `GET /api/arena/v1/checkpoints`

Returns `{apiVersion, checkpoints:[{id,label,sha256,sourceCommit,kind,
available,limitations}]}`. The adapter enumerates an explicit allowlist, verifies
the hash before loading, and rejects arbitrary paths. `kind` is `baseline` or
`restricted`. A missing or invalid artifact is listed with `available:false` and
cannot be selected. This inventory must not infer a policy's strength.

### `POST /api/arena/v1/sessions`

Request `{checkpointId}`. Response `{apiVersion,sessionId,checkpointId}`. This
creates an empty session; `POST .../hands` starts the first hand. The adapter
generates and retains deal and policy seeds in trusted process memory. The
browser cannot choose seeds, deck positions, or future deals, and never receives
the seeds or a transcript capable of reconstructing unrevealed cards.

### `POST /api/arena/v1/sessions/{id}/hands`

Request `{}`. Starts the first or next hand and alternates the button. Response
`{apiVersion,state}`. `409` if the current hand is unfinished. A new hand gets
its own derived deal and policy streams; the client cannot choose a future deck.

### `GET /api/arena/v1/sessions/{id}`

Response `{apiVersion,state}`. `state` is:

```json
{
  "handId":"opaque", "actionIndex":0, "street":"preflop",
  "button":0, "actor":0, "humanSeat":0, "bigBlind":100,
  "stacks":[9950,9900], "streetContributions":[50,100], "pot":150,
  "board":[], "humanHole":["3h","3c"], "opponentHole":null,
  "publicActions":[], "legal":{"fold":true,"check":false,
  "call":true,"toCall":50,"minRaiseTo":200,"maxRaiseTo":10000},
  "terminal":null
}
```

`humanHole` is shown only after deal. `opponentHole` remains `null` unless
revealed at a completed showdown. `legal` is `null` while the bot acts or after
terminal. The adapter may advance bot actions before responding, but must return
the resulting human turn or terminal state atomically. The browser does not
derive legal action ranges.

### `POST /api/arena/v1/sessions/{id}/actions`

Request `{handId,expectedActionIndex,action:{kind:"fold"|"check"|"call"|
"raiseTo",target?:number}}`. `target` is integer chips committed on this street.
Response `{apiVersion,state}` after the bot has acted until the next human turn
or terminal. Reject illegal action with `422` and current `state`; stale index
with `409` and current `state`. Never silently translate a requested amount.

### `GET /api/arena/v1/sessions/{id}/hands/{handId}`

Only after terminal. Returns `{apiVersion,replayId,publicActions,board,
revealedHole,terminal:{reason,payoffs},analysis}`. `revealedHole` contains only
cards actually shown at showdown; fold-mucked cards stay hidden. `analysis` is
an array by action index with optional real diagnostics:
`{actor,distribution?:[{action,probability}],equity?:{estimate,standardError,
samples,method},tableVisits?:number,usedFallback?:boolean,availability}`.
Unknown quantities are omitted and `availability` explains why; no synthetic
probabilities, EV, equity, or GTO score. Training-only targets are never sent.

### `POST /api/arena/v1/replays`

Request `{replayId}`. Adapter looks up its server-held seed, rules/checkpoint
hashes, and action transcript, replays the same deal and policy streams, and
returns the same whitelisted completed hand view. `404` for an unknown ID and
`410` for an expired ID. A replay never shows fold-mucked cards. Raw seed
injection is available only in trusted C++ tests/research tools, never HTTP.

## Required adapter tests

Run a complete human/bot hand through HTTP and compare every action, legal
range, pot, stack, and payout to direct C++ replay. Compare restricted views for
states with changed opponent cards and future decks. Confirm no hidden fields
appear in JSON before terminal or after a fold. Test stale/illegal action errors,
session expiry, unknown checkpoint, and identical replay. Audit all success and
error JSON for leaked hidden cards, seeds, deck positions, or RNG state. Assert
that browser requests cannot choose a seed or replay arbitrary deal IDs.

## Current limitation

The E-003 engine exposes `Game::observe(int)` and `Game::step(Action)` but no
arena transport, completed hand reveal audit, or generic `IStrategy` diagnostics
for baseline policies. Until the adapter is merged, gameplay is disabled in the
frontend. Stage 4 benchmark reports are local ignored artifacts and need an
explicit read-only projection before the dashboard can show values.
