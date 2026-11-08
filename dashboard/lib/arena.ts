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

export function isValidRaiseTarget(legal: Legal | null, target: number): boolean {
  return legal?.minRaiseTo != null && legal.maxRaiseTo != null &&
    Number.isSafeInteger(target) && target >= legal.minRaiseTo && target <= legal.maxRaiseTo;
}

const base = "/api/arena/v1";

export class ArenaError extends Error {
  status: number;
  state?: ArenaState;
  constructor(message: string, status: number, state?: ArenaState) { super(message); this.status = status; this.state = state; }
}

function assertPublicPayload(value: unknown): void {
  if (!value || typeof value !== "object") return;
  if (Array.isArray(value)) { value.forEach(assertPublicPayload); return; }
  const object = value as Record<string, unknown>;
  for (const [key, field] of Object.entries(object)) {
    if (/seed|deck|rng|future|replayKey|trainingTarget|privateState/i.test(key)) {
      throw new ArenaError("The local engine returned a restricted field.", 502);
    }
    assertPublicPayload(field);
  }
  if ("opponentHole" in object && !object.terminal && object.opponentHole !== null) {
    throw new ArenaError("The local engine revealed an opponent card before hand completion.", 502);
  }
  if ("opponentHole" in object && object.terminal && typeof object.terminal === "object" &&
      (object.terminal as Record<string, unknown>).reason === "fold" && object.opponentHole !== null) {
    throw new ArenaError("The local engine revealed a folded opponent hand.", 502);
  }
  if ("revealedHole" in object && object.terminal && typeof object.terminal === "object" &&
      (object.terminal as Record<string, unknown>).reason === "fold" && object.revealedHole !== null) {
    throw new ArenaError("The replay revealed a folded opponent hand.", 502);
  }
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
  assertPublicPayload(data);
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
