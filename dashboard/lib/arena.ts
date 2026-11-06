export type Card = string;
export type Street = "preflop" | "flop" | "turn" | "river" | "terminal";
export type Action = { kind: "fold" | "check" | "call" | "raiseTo"; target?: number };
export type Legal = { fold: boolean; check: boolean; call: boolean; toCall: number; minRaiseTo: number | null; maxRaiseTo: number | null };
export type PublicAction = { street: Street; player: number; action: Action; committed: number; potAfter: number };
export type Terminal = { reason: string; payoffs: [number, number] };
export type ArenaState = {
  handId: string; actionIndex: number; street: Street; button: number; actor: number | null;
  humanSeat: number; bigBlind: number; stacks: [number, number]; streetContributions: [number, number];
  pot: number; board: Card[]; humanHole: [Card, Card]; opponentHole: [Card, Card] | null;
  publicActions: PublicAction[]; legal: Legal | null; terminal: Terminal | null;
};
export type Checkpoint = { id: string; label: string; sha256: string | null; sourceCommit: string | null; kind: "baseline" | "restricted"; available: boolean; limitations: string };
export type DecisionAnalysis = { actor: number; distribution?: { action: Action; probability: number }[]; equity?: { estimate: number; standardError: number; samples: number; method: string }; tableVisits?: number; usedFallback?: boolean; availability: string };
export type CompletedHand = { replayId: string; publicActions: PublicAction[]; board: Card[]; revealedHole: [Card, Card] | null; terminal: Terminal; analysis: DecisionAnalysis[] };

const base = process.env.NEXT_PUBLIC_ARENA_API_URL || "/api/arena/v1";

export class ArenaError extends Error {
  status: number;
  state?: ArenaState;
  constructor(message: string, status: number, state?: ArenaState) { super(message); this.status = status; this.state = state; }
}

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  let response: Response;
  try { response = await fetch(`${base}${path}`, { ...init, cache: "no-store", headers: { "Content-Type": "application/json", ...init?.headers } }); }
  catch { throw new ArenaError("Local engine is unavailable. Start the trusted Arena adapter to play.", 0); }
  let data: unknown;
  try { data = await response.json(); } catch { throw new ArenaError("The local engine returned an unreadable response.", response.status); }
  if (!data || typeof data !== "object" || !("apiVersion" in data) || data.apiVersion !== "arena.v1") {
    throw new ArenaError("The local engine uses a different API version.", response.status);
  }
  if (!response.ok) {
    const body = data as { message?: string; state?: ArenaState };
    throw new ArenaError(body.message || `Engine request failed (${response.status}).`, response.status, body.state);
  }
  return data as T;
}

export const arena = {
  checkpoints: () => request<{ apiVersion: string; checkpoints: Checkpoint[] }>("/checkpoints"),
  createSession: (checkpointId: string) => request<{ apiVersion: string; sessionId: string; checkpointId: string }>("/sessions", { method: "POST", body: JSON.stringify({ checkpointId }) }),
  startHand: (id: string) => request<{ apiVersion: string; state: ArenaState }>(`/sessions/${encodeURIComponent(id)}/hands`, { method: "POST", body: "{}" }),
  state: (id: string) => request<{ apiVersion: string; state: ArenaState }>(`/sessions/${encodeURIComponent(id)}`),
  act: (id: string, state: ArenaState, action: Action) => request<{ apiVersion: string; state: ArenaState }>(`/sessions/${encodeURIComponent(id)}/actions`, { method: "POST", body: JSON.stringify({ handId: state.handId, expectedActionIndex: state.actionIndex, action }) }),
  completed: (id: string, handId: string) => request<{ apiVersion: string } & CompletedHand>(`/sessions/${encodeURIComponent(id)}/hands/${encodeURIComponent(handId)}`),
  replay: (replayId: string) => request<{ apiVersion: string } & CompletedHand>("/replays", { method: "POST", body: JSON.stringify({ replayId }) })
};
