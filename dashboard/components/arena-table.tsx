"use client";

import { useCallback, useEffect, useState } from "react";
import { AlertCircle, RotateCcw } from "lucide-react";
import Link from "next/link";
import { arena, ArenaError, isValidRaiseTarget, type Action, type ArenaState, type Checkpoint, type CompletedHand } from "@/lib/arena";
import { Button } from "@/components/ui/button";

function CardFace({ card, hidden = false }: { card?: string; hidden?: boolean }) {
  const suit = card?.slice(-1).toLowerCase();
  const red = suit === "h" || suit === "d";
  const symbols: Record<string, string> = { h: "♥", d: "♦", c: "♣", s: "♠" };
  const suits: Record<string, string> = { h: "hearts", d: "diamonds", c: "clubs", s: "spades" };
  const ranks: Record<string, string> = { A: "ace", K: "king", Q: "queen", J: "jack", T: "ten" };
  const rank = card?.slice(0,-1).toUpperCase() || "";
  const label = hidden ? "Hidden card" : card ? `${ranks[rank] || rank} of ${suits[suit || ""] || "unknown suit"}` : "Empty card slot";
  return <span className={`card ${hidden ? "card-back" : red ? "card-red" : "card-black"}`} role="img" aria-label={label}>{hidden ? <span className="card-back-mark" aria-hidden="true">P0</span> : card ? <><b aria-hidden="true">{rank}</b><span aria-hidden="true">{symbols[suit || ""]}</span></> : ""}</span>;
}

const chips = (amount: number | undefined, bigBlind: number) => amount === undefined ? "—" : `${(amount / bigBlind).toFixed(amount % bigBlind ? 1 : 0)} BB`;

export function ArenaTable() {
  const [checkpoints, setCheckpoints] = useState<Checkpoint[]>([]);
  const [selected, setSelected] = useState("");
  const [sessionId, setSessionId] = useState<string | null>(null);
  const [state, setState] = useState<ArenaState | null>(null);
  const [completed, setCompleted] = useState<CompletedHand | null>(null);
  const [raiseTo, setRaiseTo] = useState(0);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [adapterAvailable, setAdapterAvailable] = useState(false);

  const loadCheckpoints = useCallback(async () => {
    setError(null);
    try {
      const { checkpoints: list } = await arena.checkpoints();
      setAdapterAvailable(true); setCheckpoints(list);
      setSelected(list.find(c => c.available)?.id || "");
    } catch (e) { setAdapterAvailable(false); setError((e as Error).message); }
  }, []);

  useEffect(() => {
    void loadCheckpoints();
  }, [loadCheckpoints]);

  useEffect(() => {
    if (state?.legal?.minRaiseTo !== null && state?.legal?.minRaiseTo !== undefined) setRaiseTo(state.legal.minRaiseTo);
    if (state?.terminal && sessionId) arena.completed(sessionId, state.handId).then(hand => { setCompleted(hand); try { sessionStorage.setItem("pokerzero.lastReplayId.v1", hand.replayId); } catch {} }).catch(() => setCompleted(null));
  }, [state, sessionId]);

  async function start() {
    if (!selected || busy) return;
    setBusy(true); setError(null); setCompleted(null);
    try {
      const id = sessionId || (await arena.createSession(selected)).sessionId;
      setSessionId(id); setState((await arena.startHand(id)).state);
    } catch (e) {
      if (e instanceof ArenaError && (e.status === 404 || e.status === 410)) { setSessionId(null); setState(null); setCompleted(null); }
      setError((e as Error).message);
    }
    finally { setBusy(false); }
  }

  async function act(action: Action) {
    if (!sessionId || !state || busy || !state.legal) return;
    setBusy(true); setError(null);
    try { setState((await arena.act(sessionId, state, action)).state); }
    catch (e) {
      if (e instanceof ArenaError && e.state) setState(e.state);
      if (e instanceof ArenaError && (e.status === 404 || e.status === 410)) { setSessionId(null); setState(null); setCompleted(null); }
      setError((e as Error).message);
    } finally { setBusy(false); }
  }

  const legal = state?.legal;
  const bb = state?.bigBlind || 100;
  const opponent = state ? 1 - state.humanSeat : 1;
  const canRaise = legal?.minRaiseTo != null && legal.maxRaiseTo != null;
  const validRaise = isValidRaiseTarget(legal || null, raiseTo);
  return <div className="arena-page">
    <section className="arena-heading"><div><span className="eyebrow">HEADS-UP · NO-LIMIT HOLD’EM</span><h1>One table. Every decision.</h1><p>Play a local hand, then inspect what the policy actually knew and chose.</p></div><div className="session-status"><span className={`status-dot ${adapterAvailable ? "online" : ""}`} />{adapterAvailable ? "Local engine connected" : "Waiting for local engine"}</div></section>
    <div className="arena-layout"><section className="table-panel" aria-label="Poker table">
      <div className="table-meta"><span>100 BB EFFECTIVE</span><span>{state ? `HAND ${state.handId.slice(0, 8)}` : "NO ACTIVE HAND"}</span></div>
      <div className="felt-table">
        <div className="seat seat-opponent"><div className="seat-cards"><CardFace hidden={!state?.terminal || !state.opponentHole} card={state?.opponentHole?.[0]}/><CardFace hidden={!state?.terminal || !state.opponentHole} card={state?.opponentHole?.[1]}/></div><div className="seat-token"><span>BOT</span><strong>{state ? chips(state.stacks[opponent], bb) : "—"}</strong></div>{state?.button === opponent && <span className="dealer">D</span>}</div>
        <div className="table-center"><div className="pot-label">POT <strong>{state ? chips(state.pot, bb) : "—"}</strong></div><div className="board" aria-label="Community cards">{Array.from({length: 5}, (_, i) => <CardFace key={i} card={state?.board[i]}/>)}</div><div className="street-tag">{state ? state.street.toUpperCase() : "AWAITING DEAL"}</div></div>
        <div className="seat seat-human">{state?.button === state?.humanSeat && <span className="dealer">D</span>}<div className="seat-token human-token"><span>YOU</span><strong>{state ? chips(state.stacks[state.humanSeat], bb) : "—"}</strong></div><div className="seat-cards"><CardFace card={state?.humanHole[0]}/><CardFace card={state?.humanHole[1]}/></div></div>
      </div>
      <div className="table-footer" aria-live="polite"><span>{state?.terminal ? `Hand complete · ${state.terminal.reason}` : state ? `${state.actor === state.humanSeat ? "Your action" : "Bot acting"} · ${state.street}` : "Deal a hand to begin"}</span><span>BLINDS 0.5 / 1 BB</span></div>
    </section>
    <aside className="control-panel"><div className="panel-heading"><span className="eyebrow">LOCAL SESSION</span><h2>Action desk</h2></div>
      {!state && <div className="setup"><label htmlFor="checkpoint">Bot policy</label><select id="checkpoint" value={selected} onChange={e => setSelected(e.target.value)} disabled={!adapterAvailable || busy}><option value="">{adapterAvailable ? "No approved policies" : "Engine unavailable"}</option>{checkpoints.map(c => <option key={c.id} value={c.id} disabled={!c.available}>{c.label}{c.available ? "" : " · unavailable"}</option>)}</select><p>Policies are loaded from the adapter’s verified local allowlist.</p><Button onClick={start} disabled={!selected || busy}>{busy ? "Dealing…" : "Deal hand"}</Button>{!adapterAvailable && <Button variant="secondary" onClick={() => void loadCheckpoints()}>Reconnect engine</Button>}</div>}
      {state && !state.terminal && <div className="actions"><div className="decision-data"><span>TO CALL <strong>{chips(legal?.toCall,bb)}</strong></span><span>STREET COMMITTED <strong>{chips(state.streetContributions[state.humanSeat],bb)}</strong></span></div><div className="action-grid"><Button variant="danger" disabled={!legal?.fold || busy} onClick={() => act({kind:"fold"})}>Fold</Button><Button variant="secondary" disabled={!legal?.check || busy} onClick={() => act({kind:"check"})}>Check</Button><Button disabled={!legal?.call || busy} onClick={() => act({kind:"call"})}>Call {legal?.call ? chips(legal.toCall,bb) : ""}</Button></div><div className="raise-box"><label htmlFor="raise">Raise to <strong>{chips(raiseTo,bb)}</strong></label><input id="raise" type="range" min={legal?.minRaiseTo || 0} max={legal?.maxRaiseTo || 0} step="1" value={Math.min(legal?.maxRaiseTo || 0,Math.max(legal?.minRaiseTo || 0,raiseTo))} disabled={!canRaise || busy} onChange={e => setRaiseTo(Number(e.target.value))}/><div className="range-label"><span>{chips(legal?.minRaiseTo || undefined,bb)}</span><span>{chips(legal?.maxRaiseTo || undefined,bb)}</span></div><label className="raise-number-label" htmlFor="raise-chips">Exact target in chips</label><input id="raise-chips" className="raise-number" type="number" inputMode="numeric" min={legal?.minRaiseTo || 0} max={legal?.maxRaiseTo || 0} step="1" value={raiseTo} disabled={!canRaise || busy} onChange={e => setRaiseTo(Number(e.target.value))} aria-invalid={canRaise && !validRaise}/><div className="action-grid"><Button variant="secondary" disabled={!validRaise || busy} onClick={() => act({kind:"raiseTo",target:raiseTo})}>Raise to {chips(raiseTo,bb)}</Button><Button variant="secondary" disabled={!canRaise || busy} onClick={() => act({kind:"raiseTo",target:legal!.maxRaiseTo!})}>All in</Button></div></div></div>}
      {state?.terminal && <div className="terminal-panel"><span className="eyebrow">HAND COMPLETE</span><strong>{state.terminal.payoffs[state.humanSeat] >= 0 ? "+" : ""}{chips(state.terminal.payoffs[state.humanSeat],bb)}</strong><p>{completed ? `Replay ready · ${completed.publicActions.length} public actions` : "Completed-hand detail unavailable"}</p><Button onClick={start} disabled={busy}><RotateCcw size={16}/> Next hand</Button>{completed && <Button asChild variant="secondary"><Link href="/study">Inspect this hand</Link></Button>}</div>}
      {error && <p className="error-banner" role="alert"><AlertCircle size={17}/>{error}</p>}
      <div className="boundary-note"><span>INFORMATION BOUNDARY</span><p>Only your cards and public actions appear during a hand. Opponent cards remain hidden unless shown at showdown.</p></div>
    </aside></div>
    <section className="action-history"><div><span className="eyebrow">PUBLIC LEDGER</span><h2>Action history</h2></div>{!state?.publicActions.length ? <p>No actions yet.</p> : <ol>{state.publicActions.map((item,i) => <li key={i}><span>{item.street}</span><strong>{item.player === state.humanSeat ? "You" : "Bot"}</strong><span>{item.action.kind === "raiseTo" ? `Raise to ${chips(item.action.target,bb)}` : item.action.kind}</span><span>Pot {chips(item.potAfter,bb)}</span></li>)}</ol>}</section>
  </div>;
}
