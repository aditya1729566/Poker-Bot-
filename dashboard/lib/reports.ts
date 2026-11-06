import { readFile } from "node:fs/promises";
import { resolve } from "node:path";
import { createHash } from "node:crypto";

export type ResearchRow = { opponent: string; version: string; hands: number; blocks: number; mbb: number; ci: [number,number]; seatNet: [number,number]; timeouts: number; illegal: number; fallback: number; ciMethod: string };
export type ResearchReport = { cohort: string; sourceSha256: string; checkpointSha256: string; gitCommit: string; rulesSha256: string; actionSha256: string; manifestSha256: string; candidates: number; rows: ResearchRow[] };

export async function loadReport(): Promise<ResearchReport | null> {
  const dir = process.env.POKERZERO_REPORT_DIR;
  if (!dir) return null;
  // The path is an operator-supplied local directory, never an HTTP parameter.
  const source = await readFile(resolve(dir, "summary.json"));
  const sourceSha256 = createHash("sha256").update(source).digest("hex");
  const raw = JSON.parse(source.toString("utf8"));
  if (!raw || typeof raw !== "object" || !["development","validation","final"].includes(raw.cohort) || !raw.results || typeof raw.results !== "object") return null;
  const rows: ResearchRow[] = Object.entries(raw.results).map(([opponent, item]) => {
    const r = item as Record<string, unknown>;
    const interval = r.ci95_mbb_per_hand;
    const seat = r.agent1_by_button_net_chips;
    if (typeof r.mbb_per_hand !== "number" || !Number.isFinite(r.mbb_per_hand) || typeof r.hands !== "number" || !Number.isInteger(r.hands) || typeof r.paired_blocks !== "number" || !Number.isInteger(r.paired_blocks) || !Array.isArray(interval) || interval.length !== 2 || !interval.every(x => typeof x === "number" && Number.isFinite(x)) || !Array.isArray(seat) || seat.length !== 2 || !seat.every(x => typeof x === "number" && Number.isFinite(x))) throw new Error(`Invalid research row: ${opponent}`);
    return { opponent, version: String(raw.opponent_versions?.[opponent] || "unknown"), hands: r.hands as number, blocks: r.paired_blocks as number, mbb: r.mbb_per_hand as number, ci: interval as [number,number], seatNet: seat as [number,number], timeouts: Number(r.timeouts || 0), illegal: Number(r.illegal_actions || 0), fallback: Array.isArray(r.fallbacks) ? Number(r.fallbacks[0]) : 0, ciMethod: String(r.ci_method || "unavailable") };
  });
  return { cohort: raw.cohort, sourceSha256, checkpointSha256: String(raw.candidate_sha256 || "unavailable"), gitCommit: String(rows.length ? (raw.results[rows[0].opponent]?.git_commit || "unavailable") : "unavailable"), rulesSha256: String(raw.game_config_sha256 || "unavailable"), actionSha256: String(raw.action_config_sha256 || "unavailable"), manifestSha256: String(raw.manifest_sha256 || "unavailable"), candidates: Number(raw.candidate_trials_before_freeze || 0), rows };
}
