"use client";
import { useEffect, useState } from "react";
import { arena, type CompletedHand } from "@/lib/arena";

const ranks = "AKQJT98765432";
const hands = Array.from({length: 13}, (_, row) => Array.from({length: 13}, (_, col) => row === col ? `${ranks[row]}${ranks[col]}` : row < col ? `${ranks[row]}${ranks[col]}s` : `${ranks[col]}${ranks[row]}o`));

export function StudyView() {
  const [selected, setSelected] = useState("AA");
  const [hand, setHand] = useState<CompletedHand | null>(null);
  const [replayError, setReplayError] = useState<string | null>(null);
  const [decisionIndex, setDecisionIndex] = useState(0);
  useEffect(() => { const replayId = sessionStorage.getItem("pokerzero.lastReplayId.v1"); if (replayId) arena.replay(replayId).then(setHand).catch((e: Error) => setReplayError(e.message)); }, []);
  const decision = hand?.analysis?.[decisionIndex];
  const actionName = (kind: string, target?: number) => kind === "raiseTo" && target !== undefined ? `Raise to ${target} chips` : kind;
  return <div className="study-page"><div className="page-heading"><span className="eyebrow">DECISION REVIEW</span><h1>Strategy inspector</h1><p>Explore a policy decision after a completed local hand. Values appear only when the engine provides them.</p></div>
    {replayError && <p className="error-banner" role="alert">{replayError}</p>}{hand && <section className="decision-strip" aria-label="Recorded decisions"><div><span className="eyebrow">LAST COMPLETED HAND</span><h2>Decision sequence</h2></div><div className="decision-list">{hand.analysis?.length ? hand.analysis.map((item,i) => <button key={i} className={i === decisionIndex ? "active" : ""} onClick={() => setDecisionIndex(i)} aria-pressed={i === decisionIndex}>{i+1}. {item.actor === 0 ? "Seat 0" : "Seat 1"}</button>) : <span>No decision diagnostics were recorded.</span>}</div></section>}
    <div className="study-layout"><section className="study-matrix-panel"><div className="section-top"><div><span className="eyebrow">STARTING HANDS</span><h2>169 hand classes</h2></div><span className="subtle-pill">NO SNAPSHOT LOADED</span></div><div className="matrix" role="group" aria-label="Starting hand classes">{hands.flat().map(hand => <button key={hand} type="button" aria-pressed={selected === hand} className={selected === hand ? "selected" : ""} onClick={() => setSelected(hand)}>{hand}</button>)}</div><div className="matrix-legend"><span><i className="legend-neutral"/> No strategy data</span><span>Class frequencies are unavailable in API v1</span></div></section>
      <aside className="study-detail"><span className="eyebrow">SELECTED HAND CLASS</span><strong className="selected-hand">{selected}</strong><div className="detail-rule"/><h2>{decision ? `Decision ${decisionIndex + 1}` : "Decision data unavailable"}</h2><p>{decision?.availability || "Complete a hand with an approved policy to inspect recorded diagnostics. Missing values remain unavailable."}</p><dl><div><dt>Action frequencies</dt><dd>{decision?.distribution?.length ? decision.distribution.map(x => `${actionName(x.action.kind,x.action.target)} ${(x.probability*100).toFixed(1)}%`).join(" · ") : "Unavailable"}</dd></div><div><dt>Equity estimate</dt><dd>{decision?.equity ? `${(decision.equity.estimate*100).toFixed(1)}% ± ${(decision.equity.standardError*100).toFixed(1)}% SE · ${decision.equity.samples} samples` : "Unavailable"}</dd></div><div><dt>Table visits</dt><dd>{decision?.tableVisits ?? "Unavailable"}</dd></div><div><dt>Fallback</dt><dd>{decision?.usedFallback === undefined ? "Unavailable" : decision.usedFallback ? "Used" : "No"}</dd></div></dl></aside></div>
    <div className="study-note">This grid is an index of possible starting hand classes. It does not display a solved strategy or claim GTO convergence.</div></div>;
}
