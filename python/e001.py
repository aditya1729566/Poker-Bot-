"""E-001 development-only equity ablation and paired diagnostic analysis."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import subprocess
import time
from collections import defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "experiments/e001.json"
OUTPUT = ROOT / "artifacts/reports/e001"
STREETS = ("preflop", "flop", "turn", "river")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def source_tree_sha256() -> str:
    digest = hashlib.sha256()
    for path in sorted(ROOT.rglob("*")):
        relative = path.relative_to(ROOT)
        if not path.is_file() or any(part in {".git", "build", "build-asan", "artifacts", "__pycache__"} for part in relative.parts):
            continue
        encoded = str(relative).encode()
        digest.update(len(encoded).to_bytes(4, "big"))
        digest.update(encoded)
        digest.update(bytes.fromhex(sha256(path)))
    return digest.hexdigest()


def git_state() -> tuple[str, bool]:
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip())
    return commit, dirty


def manifest() -> dict:
    data = json.loads(MANIFEST.read_text())
    if data["schema_version"] != 1 or data["id"] != "E-001-equity-only-ablation":
        raise RuntimeError("unsupported E-001 manifest")
    if data["cohort"] != "development" or data["root_seeds"] != [51, 52, 53]:
        raise RuntimeError("E-001 cohort or root seeds changed")
    if data["hands_per_arm_per_seed_opponent"] != 10000 or data["action_budget_ms"] != 25:
        raise RuntimeError("E-001 hand count or action budget changed")
    if data["opponents"] != ["random", "passive", "tight_aggressive", "loose_aggressive", "equity"]:
        raise RuntimeError("E-001 opponent matrix changed")
    if data["primary_opponents"] != ["tight_aggressive", "loose_aggressive", "equity"]:
        raise RuntimeError("E-001 primary opponents changed")
    return data


def verify_inputs(data: dict) -> dict:
    artifact = ROOT / "artifacts/strategies/hunl_50k.bin"
    if not artifact.is_file() or sha256(artifact) != data["frozen_artifact_sha256"]:
        raise RuntimeError("frozen artifact hash mismatch")
    commit, dirty = git_state()
    if dirty:
        raise RuntimeError("E-001 requires a clean committed source tree")
    return {"source_commit": commit, "source_tree_sha256": source_tree_sha256(),
            "artifact_sha256": sha256(artifact), "manifest_sha256": sha256(MANIFEST),
            "benchmark_manifest_sha256": sha256(ROOT / "benchmarks/manifest.yaml"),
            "game_config_sha256": sha256(ROOT / "configs/game.yaml"),
            "action_config_sha256": sha256(ROOT / "configs/action_abstraction.yaml")}


def run_paths(seed: int, opponent: str, arm: str) -> tuple[Path, Path]:
    base = OUTPUT / f"seed{seed}_{opponent}_{arm}"
    return Path(str(base) + ".json"), base


def save_progress(progress: dict) -> None:
    temporary = OUTPUT / "progress.tmp"
    temporary.write_text(json.dumps(progress, indent=2, sort_keys=True) + "\n")
    temporary.replace(OUTPUT / "progress.json")


def run_all() -> None:
    data = manifest()
    provenance = verify_inputs(data)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    progress_path = OUTPUT / "progress.json"
    if progress_path.exists():
        progress = json.loads(progress_path.read_text())
        if progress["provenance"] != provenance:
            raise RuntimeError("source, artifact, or manifest changed since E-001 began")
    else:
        progress = {"schema_version": 1, "experiment": data["id"], "provenance": provenance, "runs": {}}
        save_progress(progress)
    for seed in data["root_seeds"]:
        for opponent in data["opponents"]:
            for arm in ("control", "treatment"):
                name = f"seed{seed}_{opponent}_{arm}"
                report_path, prefix = run_paths(seed, opponent, arm)
                blocks_path = Path(str(prefix) + ".blocks.jsonl")
                actions_path = Path(str(prefix) + ".actions.jsonl")
                if name in progress["runs"] and progress["runs"][name]["status"] == "completed":
                    if not all(path.is_file() for path in (report_path, blocks_path, actions_path)):
                        raise RuntimeError(f"completed run missing output: {name}")
                    print(f"SKIP completed {name}", flush=True)
                    continue
                if any(path.exists() for path in (report_path, blocks_path, actions_path)):
                    raise RuntimeError(f"partial output exists for {name}; inspect before resuming")
                command = [str(ROOT / "build/pokerzero"), "match", "--agent1", data[arm],
                           "--agent2", opponent, "--hands", str(data["hands_per_arm_per_seed_opponent"]),
                           "--seed", str(seed), "--cohort", "development", "--output", str(report_path),
                           "--diagnostics-prefix", str(prefix)]
                progress["runs"][name] = {"status": "running", "command": command, "started_utc_epoch": time.time()}
                save_progress(progress)
                print(f"START {name}", flush=True)
                start = time.monotonic()
                try:
                    completed = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=28800)
                    (OUTPUT / f"{name}.stdout").write_text(completed.stdout)
                    (OUTPUT / f"{name}.stderr").write_text(completed.stderr)
                    if completed.returncode != 0:
                        status = {"status": "failed", "returncode": completed.returncode,
                                  "failure_reason": completed.stderr.strip() or "nonzero exit"}
                    elif not all(path.is_file() for path in (report_path, blocks_path, actions_path)):
                        status = {"status": "failed", "failure_reason": "missing output file"}
                    else:
                        report = json.loads(report_path.read_text())
                        status = {"status": "completed", "report_sha256": sha256(report_path),
                                  "blocks_sha256": sha256(blocks_path), "actions_sha256": sha256(actions_path),
                                  "hands": report["hands"], "timeouts": report["timeouts"],
                                  "illegal_actions": report["illegal_actions"], "seconds": report["seconds"]}
                except subprocess.TimeoutExpired as error:
                    status = {"status": "failed", "failure_reason": f"experiment exceeded 28800 seconds: {error}"}
                status["wall_seconds"] = time.monotonic() - start
                progress["runs"][name].update(status)
                save_progress(progress)
                print(f"END {name} {status['status']} {status['wall_seconds']:.1f}s", flush=True)
    print(OUTPUT / "progress.json", flush=True)


def interval(values: list[float]) -> dict:
    if len(values) < 2:
        raise RuntimeError("too few paired blocks for interval")
    n = len(values)
    mean = sum(values) / n
    variance = sum((value - mean) ** 2 for value in values) / (n - 1)
    half_width = 1.96 * math.sqrt(variance / n)
    return {"blocks": n, "mean_mbb_per_hand": mean,
            "ci95_mbb_per_hand": [mean - half_width, mean + half_width]}


def pot_band(pot_chips: int) -> str:
    if pot_chips < 500:
        return "lt5bb"
    if pot_chips < 2000:
        return "5to20bb"
    if pot_chips < 10000:
        return "20to100bb"
    return "ge100bb"


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, math.ceil(fraction * len(ordered)) - 1)]


def new_context() -> dict:
    return {"actions": 0, "fallbacks": 0, "timeouts": 0, "illegal_actions": 0,
            "proposed_kind": {}, "chosen_kind": {}, "table_visits_missing": 0,
            "table_visits_below_10": 0, "table_visits_at_least_10": 0}


def update_context(context: dict, action: dict) -> None:
    context["actions"] += 1
    context["fallbacks"] += int(action["used_fallback"])
    context["timeouts"] += int(action["timeout"])
    context["illegal_actions"] += int(action["illegal"])
    for field in ("proposed_kind", "chosen_kind"):
        kind = action[field.removesuffix("_kind")]["kind"]
        context[field][kind] = context[field].get(kind, 0) + 1
    visits = action["table_visits"]
    if visits is None:
        context["table_visits_missing"] += 1
    elif visits < 10:
        context["table_visits_below_10"] += 1
    else:
        context["table_visits_at_least_10"] += 1


def analyze_match(seed: int, opponent: str, arm: str, data: dict) -> tuple[list[int], dict, list[float]]:
    report_path, prefix = run_paths(seed, opponent, arm)
    blocks_path = Path(str(prefix) + ".blocks.jsonl")
    actions_path = Path(str(prefix) + ".actions.jsonl")
    report = json.loads(report_path.read_text())
    commit, dirty = git_state()
    if dirty or report["git_commit"] != commit or report["dirty_tree"] or report["build_type"] != "Release":
        raise RuntimeError(f"match was not built from the clean release source: {prefix}")
    if report["schema_version"] != 1 or report["diagnostics_schema"] != data["diagnostics_schema"]:
        raise RuntimeError(f"diagnostic schema mismatch: {prefix}")
    if report["agent1"] != data[arm] or report["agent2"] != opponent or report["seed"] != seed:
        raise RuntimeError(f"match identity mismatch: {prefix}")
    if report["cohort"] != "development" or report["hands"] != 10000 or report["paired_blocks"] != 5000:
        raise RuntimeError(f"match cohort or size mismatch: {prefix}")
    if report["action_budget_ms"] != 25 or report["failure_reason"] is not None:
        raise RuntimeError(f"match budget or failure mismatch: {prefix}")
    if report["seconds"] > 28800 or report["peak_rss_bytes"] > 12884901888:
        raise RuntimeError(f"match compute budget exceeded: {prefix}")
    if sum(report["net_chips"]) != 0 or sum(report["agent1_by_button_net_chips"]) != report["net_chips"][0]:
        raise RuntimeError(f"aggregate payoff mismatch: {prefix}")
    block_returns: list[int | None] = [None] * 5000
    leg_returns: list[list[int] | None] = [None] * 5000
    seat_net = [0, 0]
    block_rows = 0
    with blocks_path.open() as source:
        for line in source:
            record = json.loads(line)
            block = record["block"]
            if record["schema_version"] != 1 or record["type"] != "block" or record["seed"] != seed:
                raise RuntimeError(f"invalid block header: {prefix}")
            if not 0 <= block < 5000 or block_returns[block] is not None:
                raise RuntimeError(f"duplicate or out-of-range block: {prefix}")
            legs = record["candidate_leg_returns_chips"]
            if len(legs) != 2 or record["candidate_block_mbb_per_hand"] != 5 * sum(legs):
                raise RuntimeError(f"invalid block payoff: {prefix}")
            leg_returns[block] = legs
            block_returns[block] = 5 * sum(legs)
            seat_net[0] += legs[0]
            seat_net[1] += legs[1]
            block_rows += 1
    if block_rows != 5000 or any(value is None for value in block_returns):
        raise RuntimeError(f"missing blocks: {prefix}")
    if seat_net != report["agent1_by_button_net_chips"] or sum(seat_net) != report["net_chips"][0]:
        raise RuntimeError(f"block totals do not reconcile: {prefix}")
    if abs(sum(block_returns) / 5000 - report["mbb_per_hand"]) > 0.02:
        raise RuntimeError(f"reported mbb rate does not reconcile: {prefix}")

    action_rows = 0
    timeouts = illegal_actions = candidate_actions = candidate_fallbacks = 0
    expected_index: dict[tuple[int, int], int] = {}
    contexts: dict[str, dict] = defaultdict(new_context)
    pots: dict[str, dict] = defaultdict(new_context)
    reached_band: dict[str, set[tuple[int, int]]] = defaultdict(set)
    reached_return: dict[str, int] = defaultdict(int)
    candidate_latencies: list[float] = []
    forbidden = {"hole", "board_cards", "deck", "opponent_hole", "game_rng", "policy_rng"}
    with actions_path.open() as source:
        for line in source:
            action = json.loads(line)
            if forbidden.intersection(action):
                raise RuntimeError(f"private-state field in diagnostics: {prefix}")
            block, leg = action["block"], action["leg"]
            if action["schema_version"] != 1 or action["type"] != "action" or action["seed"] != seed:
                raise RuntimeError(f"invalid action header: {prefix}")
            if not 0 <= block < 5000 or leg not in (0, 1) or action["button"] != leg:
                raise RuntimeError(f"action deal/button mismatch: {prefix}")
            key = (block, leg)
            if action["action_index"] != expected_index.get(key, 0):
                raise RuntimeError(f"action sequence mismatch: {prefix}")
            expected_index[key] = action["action_index"] + 1
            if action["candidate_hand_return_chips"] != leg_returns[block][leg]:
                raise RuntimeError(f"action hand return mismatch: {prefix}")
            actor = action["actor"]
            if actor not in (0, 1) or action["street"] not in (0, 1, 2, 3):
                raise RuntimeError(f"invalid actor or street: {prefix}")
            action_rows += 1
            timeouts += int(action["timeout"])
            illegal_actions += int(action["illegal"])
            if actor == 0:
                candidate_actions += 1
                candidate_fallbacks += int(action["used_fallback"])
                candidate_latencies.append(action["latency_ms"])
                if action["policy_info_available"] != (arm == "control"):
                    raise RuntimeError(f"policy metadata availability mismatch: {prefix}")
                if arm == "treatment" and (action["used_fallback"] or action["table_visits"] is not None):
                    raise RuntimeError(f"equity treatment has table metadata: {prefix}")
                seat = "button" if leg == 0 else "big_blind"
                context = f"{STREETS[action['street']]}|{seat}"
                band = pot_band(action["pot_chips"])
                update_context(contexts[context], action)
                update_context(pots[band], action)
                if key not in reached_band[band]:
                    reached_band[band].add(key)
                    reached_return[band] += action["candidate_hand_return_chips"]
            elif action["policy_info_available"] or action["used_fallback"] or action["table_visits"] is not None:
                raise RuntimeError(f"opponent has restricted metadata unexpectedly: {prefix}")
    if action_rows != report["actions"] or timeouts != report["timeouts"] or illegal_actions != report["illegal_actions"]:
        raise RuntimeError(f"action failure totals do not reconcile: {prefix}")
    if candidate_fallbacks != report["fallbacks"][0] or report["fallbacks"][1] != 0:
        raise RuntimeError(f"fallback totals do not reconcile: {prefix}")
    if len(expected_index) != 10000 or candidate_actions == 0:
        raise RuntimeError(f"hand/action coverage incomplete: {prefix}")
    for band, context in pots.items():
        context["unique_hands_reaching_band"] = len(reached_band[band])
        context["descriptive_mean_return_mbb_per_reached_hand"] = (
            10 * reached_return[band] / len(reached_band[band]))
    details = {"report_sha256": sha256(report_path), "blocks_sha256": sha256(blocks_path),
               "actions_sha256": sha256(actions_path), "hands": report["hands"],
               "actions": report["actions"], "candidate_actions": candidate_actions,
               "fallbacks": candidate_fallbacks, "timeouts": timeouts,
               "illegal_actions": illegal_actions, "seconds": report["seconds"],
               "peak_rss_bytes": report["peak_rss_bytes"], "seat_net_chips": seat_net,
               "latency_ms_p50_p95_p99": [percentile(candidate_latencies, fraction) for fraction in (0.50, 0.95, 0.99)],
               "contexts": dict(contexts), "pot_bands": dict(pots)}
    return [int(value) for value in block_returns], details, candidate_latencies


def analyze_all() -> dict:
    data = manifest()
    provenance = verify_inputs(data)
    progress = json.loads((OUTPUT / "progress.json").read_text())
    if progress["provenance"] != provenance:
        raise RuntimeError("E-001 provenance changed after runs")
    expected_names = {f"seed{seed}_{opponent}_{arm}" for seed in data["root_seeds"]
                      for opponent in data["opponents"] for arm in ("control", "treatment")}
    if set(progress["runs"]) != expected_names or any(run["status"] != "completed" for run in progress["runs"].values()):
        raise RuntimeError("E-001 matrix has missing or failed runs; inspect progress.json")
    blocks: dict[tuple[int, str, str], list[int]] = {}
    details: dict[str, dict] = {}
    latencies: dict[str, list[float]] = {}
    for seed in data["root_seeds"]:
        for opponent in data["opponents"]:
            for arm in ("control", "treatment"):
                name = f"seed{seed}_{opponent}_{arm}"
                blocks[(seed, opponent, arm)], details[name], latencies[name] = analyze_match(seed, opponent, arm, data)
                if details[name]["report_sha256"] != progress["runs"][name]["report_sha256"] or \
                   details[name]["blocks_sha256"] != progress["runs"][name]["blocks_sha256"] or \
                   details[name]["actions_sha256"] != progress["runs"][name]["actions_sha256"]:
                    raise RuntimeError(f"run file changed since completion: {name}")
    comparisons = {}
    for opponent in data["opponents"]:
        control = [value for seed in data["root_seeds"] for value in blocks[(seed, opponent, "control")]]
        treatment = [value for seed in data["root_seeds"] for value in blocks[(seed, opponent, "treatment")]]
        paired = [treatment[index] - control[index] for index in range(len(control))]
        comparisons[opponent] = {"control": interval(control), "treatment": interval(treatment),
                                 "treatment_minus_control": interval(paired),
                                 "seat_mbb_per_hand": {
                                     arm: [10 * sum(details[f"seed{seed}_{opponent}_{arm}"]["seat_net_chips"][seat]
                                                    for seed in data["root_seeds"]) / 15000
                                           for seat in (0, 1)]
                                     for arm in ("control", "treatment")},
                                 "candidate_latency_ms_p50_p95_p99": {
                                     arm: [percentile([value for seed in data["root_seeds"]
                                                       for value in latencies[f"seed{seed}_{opponent}_{arm}"]], fraction)
                                           for fraction in (0.50, 0.95, 0.99)]
                                     for arm in ("control", "treatment")},
                                 "control_timeouts": sum(details[f"seed{seed}_{opponent}_control"]["timeouts"] for seed in data["root_seeds"]),
                                 "treatment_timeouts": sum(details[f"seed{seed}_{opponent}_treatment"]["timeouts"] for seed in data["root_seeds"]),
                                 "control_illegal_actions": sum(details[f"seed{seed}_{opponent}_control"]["illegal_actions"] for seed in data["root_seeds"]),
                                 "treatment_illegal_actions": sum(details[f"seed{seed}_{opponent}_treatment"]["illegal_actions"] for seed in data["root_seeds"])}
    primary_clusters = []
    for seed in data["root_seeds"]:
        for block in range(5000):
            delta = sum(blocks[(seed, opponent, "treatment")][block] -
                        blocks[(seed, opponent, "control")][block]
                        for opponent in data["primary_opponents"]) / len(data["primary_opponents"])
            primary_clusters.append(delta)
    primary = interval(primary_clusters)
    primary["supports_equity_only_outperformance"] = primary["ci95_mbb_per_hand"][0] > 0
    result = {"schema_version": 1, "experiment": data["id"], "cohort": "development",
              "valid": True, "provenance": provenance, "manifest": data,
              "primary": primary, "opponents": comparisons, "runs": details,
              "total_hands": sum(item["hands"] for item in details.values()),
              "total_actions": sum(item["actions"] for item in details.values()),
              "total_timeouts": sum(item["timeouts"] for item in details.values()),
              "total_illegal_actions": sum(item["illegal_actions"] for item in details.values()),
              "total_match_seconds": sum(item["seconds"] for item in details.values()),
              "max_peak_rss_bytes": max(item["peak_rss_bytes"] for item in details.values())}
    (OUTPUT / "analysis.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    (OUTPUT / "report.md").write_text(render_report(result))
    return result


def render_report(result: dict) -> str:
    primary = result["primary"]
    p_low, p_high = primary["ci95_mbb_per_hand"]
    lines = ["# PokerZero E-001: equity-only ablation", "",
             "Development cohort only. The frozen restricted policy is the control; the existing equity agent is the treatment.",
             "", f"Source commit: `{result['provenance']['source_commit']}`  ",
             f"Source tree SHA-256: `{result['provenance']['source_tree_sha256']}`  ",
             f"Frozen artifact SHA-256: `{result['provenance']['artifact_sha256']}`  ",
             f"Experiment manifest SHA-256: `{result['provenance']['manifest_sha256']}`  ",
             f"Benchmark manifest SHA-256: `{result['provenance']['benchmark_manifest_sha256']}`", "",
             "## Primary result", "",
             f"Equal-weight treatment-minus-control over tight-aggressive, loose-aggressive, and equity: "
             f"**{primary['mean_mbb_per_hand']:+.2f} mbb/hand**, 95% interval "
             f"[{p_low:+.2f}, {p_high:+.2f}] across {primary['blocks']:,} seed-and-deal-block clusters.", "",
             "A positive interval wholly above zero supports equity-only outperforming the hybrid in this development experiment. "
             "This experiment does not promote a replacement or satisfy Gate E.", "",
             "## All predeclared opponents", "",
             "| Opponent | Control mbb/hand | Treatment mbb/hand | Treatment − control | Paired 95% interval | Control / treatment timeouts | Illegal actions |",
             "|---|---:|---:|---:|---:|---:|---:|"]
    for opponent in result["manifest"]["opponents"]:
        row = result["opponents"][opponent]
        delta = row["treatment_minus_control"]
        low, high = delta["ci95_mbb_per_hand"]
        lines.append(f"| {opponent} | {row['control']['mean_mbb_per_hand']:+.2f} | "
                     f"{row['treatment']['mean_mbb_per_hand']:+.2f} | {delta['mean_mbb_per_hand']:+.2f} | "
                     f"[{low:+.2f}, {high:+.2f}] | {row['control_timeouts']} / {row['treatment_timeouts']} | "
                     f"{row['control_illegal_actions']} / {row['treatment_illegal_actions']} |")
    lines += ["", "## Seat outcomes and action latency", "",
              "Seat rates are descriptive mbb/hand for the candidate in each button position. "
              "Latency percentiles cover candidate actions only.", "",
              "| Opponent | Arm | Button mbb/hand | Big-blind mbb/hand | Candidate p50 / p95 / p99 ms | Match seconds |",
              "|---|---|---:|---:|---:|---:|"]
    for opponent in result["manifest"]["opponents"]:
        row = result["opponents"][opponent]
        for arm in ("control", "treatment"):
            seat = row["seat_mbb_per_hand"][arm]
            latency = row["candidate_latency_ms_p50_p95_p99"][arm]
            seconds = sum(result["runs"][f"seed{seed}_{opponent}_{arm}"]["seconds"]
                          for seed in result["manifest"]["root_seeds"])
            lines.append(f"| {opponent} | {arm} | {seat[0]:+.2f} | {seat[1]:+.2f} | "
                         f"{latency[0]:.3f} / {latency[1]:.3f} / {latency[2]:.3f} | {seconds:.1f} |")
    lines += ["", "## Candidate decisions by street and seat", "",
              "The action mix uses chosen actions after any timeout or illegal-action completion. "
              "Fallback rates apply only to the restricted control.", "",
              "| Opponent | Arm | Street | Seat | Actions | Fallbacks | Fold | Check | Call | Raise | Timeouts |",
              "|---|---|---|---|---:|---:|---:|---:|---:|---:|---:|"]
    for opponent in result["manifest"]["opponents"]:
        for arm in ("control", "treatment"):
            for street in STREETS:
                for seat in ("button", "big_blind"):
                    key = f"{street}|{seat}"
                    matching = [result["runs"][f"seed{seed}_{opponent}_{arm}"]["contexts"].get(key, new_context())
                                for seed in result["manifest"]["root_seeds"]]
                    count = sum(row["actions"] for row in matching)
                    if count == 0:
                        continue
                    mix = {kind: sum(row["chosen_kind"].get(kind, 0) for row in matching)
                           for kind in ("fold", "check", "call", "raise_to")}
                    lines.append(f"| {opponent} | {arm} | {street} | {seat} | {count} | "
                                 f"{sum(row['fallbacks'] for row in matching)} | {mix['fold']} | "
                                 f"{mix['check']} | {mix['call']} | {mix['raise_to']} | "
                                 f"{sum(row['timeouts'] for row in matching)} |")
    lines += ["", "## Public pot bands", "",
              "Bands use the pot before the decision. Mean return is descriptive for hands reaching the band; "
              "a hand may reach more than one band.", "",
              "| Opponent | Arm | Pot band | Candidate actions | Fallbacks | Timeouts | Reached hands | Descriptive mbb/reached hand |",
              "|---|---|---|---:|---:|---:|---:|---:|"]
    for opponent in result["manifest"]["opponents"]:
        for arm in ("control", "treatment"):
            for band in ("lt5bb", "5to20bb", "20to100bb", "ge100bb"):
                rows = [result["runs"][f"seed{seed}_{opponent}_{arm}"]["pot_bands"].get(band, new_context())
                        for seed in result["manifest"]["root_seeds"]]
                count = sum(row["actions"] for row in rows)
                if count:
                    reached = sum(row.get("unique_hands_reaching_band", 0) for row in rows)
                    weighted_return = sum(row.get("descriptive_mean_return_mbb_per_reached_hand", 0) *
                                          row.get("unique_hands_reaching_band", 0) for row in rows)
                    lines.append(f"| {opponent} | {arm} | {band} | {count} | "
                                 f"{sum(row['fallbacks'] for row in rows)} | "
                                 f"{sum(row['timeouts'] for row in rows)} | {reached} | "
                                 f"{weighted_return / reached:+.2f} |")
    lines += ["", "## Compute and validity", "",
              f"Total: {result['total_hands']:,} hands, {result['total_actions']:,} actions, "
              f"{result['total_timeouts']} reported timeouts, {result['total_illegal_actions']} illegal actions, "
              f"{result['total_match_seconds']:.1f} match seconds, maximum peak RSS "
              f"{result['max_peak_rss_bytes'] / 1048576:.1f} MiB. Each run used the same 25 ms action budget.", "",
              "Every JSONL block and action was checked against its match summary. The machine-readable "
              "`analysis.json` includes per-run seat nets, fallback counts, latency percentiles, context tables, "
              "pot bands, file hashes, and all 30 match identities. The block files preserve the two candidate "
              "returns separately for each duplicate pair. Opponent instances were created in separate "
              "processes for every arm. No final or validation seed was used.", ""]
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("run", "analyze"))
    args = parser.parse_args()
    if args.mode == "run":
        run_all()
    else:
        result = analyze_all()
        print(f"{OUTPUT / 'report.md'}: primary {result['primary']['mean_mbb_per_hand']:+.2f} "
              f"{result['primary']['ci95_mbb_per_hand']}")


if __name__ == "__main__":
    main()
