"""Predeclared E-004 development-only street-gated policy ablation."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import statistics
import subprocess
import time
from collections import defaultdict
from pathlib import Path

from build_preflight import verify_build

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "experiments/e004.json"
OUTPUT = ROOT / "artifacts/reports/e004"
STREETS = ("preflop", "flop", "turn", "river")
SEATS = ("button", "big_blind")
KINDS = ("fold", "check", "call", "raise_to")


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


def manifest() -> dict:
    data = json.loads(MANIFEST.read_text())
    if (data["schema_version"] != 1 or data["id"] != "E-004-street-gated-table-ablation" or
            data["cohort"] != "development" or data["root_seeds"] != [60, 61, 62] or
            data["opponents"] != ["random", "passive", "tight_aggressive", "loose_aggressive", "equity"] or
            data["opponent_versions"] != ["random_v1", "passive_v1", "tight_aggressive_v1", "loose_aggressive_v1", "equity_v1"] or
            data["action_abstraction"] != "hunl_abs_v1" or data["minimum_table_visits"] != 10 or
            data["hands_per_arm_opponent_seed"] != 10000 or data["action_budget_ms"] != 25 or
            data["experiment_cap_seconds"] != 28800 or data["peak_rss_cap_bytes"] != 12884901888 or
            data["primary_treatment"] != "postflop_only" or data["primary_control"] != "full" or
            data["primary_opponents"] != ["tight_aggressive", "loose_aggressive", "equity"] or
            list(data["arms"]) != ["full", "preflop_only", "postflop_only", "equity_only"]):
        raise RuntimeError("E-004 predeclared design changed")
    gates = [(v["preflop_table"], v["postflop_table"], v["agent_prefix"]) for v in data["arms"].values()]
    if gates != [(True, True, "restricted:"), (True, False, "restricted_preflop:"),
                 (False, True, "restricted_postflop:"), (False, False, "restricted_none:")]:
        raise RuntimeError("E-004 arm definitions changed")
    return data


def provenance(data: dict) -> dict:
    artifact = ROOT / data["artifact"]
    if not artifact.is_file() or sha256(artifact) != data["artifact_sha256"]:
        raise RuntimeError("E-004 frozen artifact hash mismatch")
    build = verify_build(ROOT, ROOT / "build/pokerzero")
    return {"source_commit": build["source_commit"], "source_tree_sha256": source_tree_sha256(),
            "binary_sha256": build["binary_sha256"], "build_info": build["build_info"],
            "artifact_sha256": sha256(artifact), "manifest_sha256": sha256(MANIFEST),
            "benchmark_manifest_sha256": sha256(ROOT / "benchmarks/manifest.yaml"),
            "game_config_sha256": sha256(ROOT / "configs/game.yaml"),
            "action_config_sha256": sha256(ROOT / "configs/action_abstraction.yaml")}


def name(seed: int, opponent: str, arm: str) -> str:
    return f"seed{seed}_{opponent}_{arm}"


def paths(run_name: str) -> tuple[Path, Path, Path, Path]:
    base = OUTPUT / run_name
    return (Path(str(base) + ".json"), Path(str(base) + ".blocks.jsonl"),
            Path(str(base) + ".actions.jsonl"), base)


def save_progress(progress: dict) -> None:
    temporary = OUTPUT / "progress.tmp"
    temporary.write_text(json.dumps(progress, indent=2, sort_keys=True) + "\n")
    temporary.replace(OUTPUT / "progress.json")


def run_all() -> None:
    data = manifest()
    frozen = provenance(data)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    progress_path = OUTPUT / "progress.json"
    if progress_path.exists():
        progress = json.loads(progress_path.read_text())
        if progress["provenance"] != frozen:
            raise RuntimeError("E-004 source, binary, artifact, or manifest changed since start")
    else:
        progress = {"schema_version": 1, "experiment": data["id"],
                    "provenance": frozen, "runs": {}}
        save_progress(progress)
    spent = sum(row.get("wall_seconds", 0) for row in progress["runs"].values()
                if row["status"] in ("completed", "failed"))
    for seed in data["root_seeds"]:
        for opponent in data["opponents"]:
            for arm, definition in data["arms"].items():
                run_name = name(seed, opponent, arm)
                report_path, blocks_path, actions_path, prefix = paths(run_name)
                previous = progress["runs"].get(run_name)
                if previous and previous["status"] == "completed":
                    if any(sha256(path) != previous[key] for path, key in
                           ((report_path, "report_sha256"), (blocks_path, "blocks_sha256"),
                            (actions_path, "actions_sha256"))):
                        raise RuntimeError(f"completed E-004 output changed: {run_name}")
                    print(f"SKIP completed {run_name}", flush=True)
                    continue
                if previous or any(path.exists() for path in (report_path, blocks_path, actions_path)):
                    raise RuntimeError(f"partial or failed E-004 run requires audit: {run_name}")
                remaining = data["experiment_cap_seconds"] - spent
                if remaining <= 0:
                    raise RuntimeError("E-004 eight-hour experiment cap reached before next match")
                verify_build(ROOT, ROOT / "build/pokerzero", frozen["binary_sha256"])
                if sha256(ROOT / data["artifact"]) != frozen["artifact_sha256"]:
                    raise RuntimeError("E-004 artifact changed before match")
                agent = definition["agent_prefix"] + data["artifact"]
                command = [str(ROOT / "build/pokerzero"), "match", "--agent1", agent,
                           "--agent2", opponent, "--hands", str(data["hands_per_arm_opponent_seed"]),
                           "--seed", str(seed), "--cohort", "development", "--output", str(report_path),
                           "--diagnostics-prefix", str(prefix)]
                progress["runs"][run_name] = {"status": "running", "command": command,
                                               "started_utc_epoch": time.time()}
                save_progress(progress)
                print(f"START {run_name}", flush=True)
                start = time.monotonic()
                try:
                    completed = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                                               timeout=remaining)
                    (OUTPUT / f"{run_name}.stdout").write_text(completed.stdout)
                    (OUTPUT / f"{run_name}.stderr").write_text(completed.stderr)
                    if completed.returncode != 0 or not all(path.is_file() for path in
                                                             (report_path, blocks_path, actions_path)):
                        status = {"status": "failed", "returncode": completed.returncode,
                                  "failure_reason": completed.stderr.strip() or "missing output file"}
                    else:
                        report = json.loads(report_path.read_text())
                        if (report["schema_version"] != 1 or report["git_commit"] != frozen["source_commit"] or
                                report["dirty_tree"] or report["cohort"] != "development" or
                                report["action_abstraction"] != "hunl_abs_v1" or
                                report["restricted_inference"] != "min_policy_visits_10_v2" or
                                report["hands"] != 10000 or report["seed"] != seed or
                                report["agent1"] != agent or report["agent2"] != opponent or
                                report["action_budget_ms"] != 25):
                            status = {"status": "failed", "failure_reason": "run provenance or design mismatch"}
                        elif report["peak_rss_bytes"] > data["peak_rss_cap_bytes"]:
                            status = {"status": "failed", "failure_reason": "12 GiB peak RSS cap exceeded"}
                        else:
                            status = {"status": "completed", "hands": report["hands"],
                                      "seconds": report["seconds"], "peak_rss_bytes": report["peak_rss_bytes"],
                                      "timeouts": report["timeouts"], "illegal_actions": report["illegal_actions"],
                                      "report_sha256": sha256(report_path), "blocks_sha256": sha256(blocks_path),
                                      "actions_sha256": sha256(actions_path)}
                except subprocess.TimeoutExpired as error:
                    status = {"status": "failed", "failure_reason": f"eight-hour cap: {error}"}
                status["wall_seconds"] = time.monotonic() - start
                spent += status["wall_seconds"]
                progress["runs"][run_name].update(status)
                save_progress(progress)
                print(f"END {run_name}: {status['status']} ({status['wall_seconds']:.1f}s)", flush=True)
                if status["status"] != "completed":
                    raise RuntimeError(f"E-004 run failed: {run_name}: {status['failure_reason']}")


def interval(values: list[float]) -> dict:
    if len(values) < 2:
        raise RuntimeError("insufficient paired blocks")
    mean = statistics.mean(values)
    half = 1.96 * statistics.stdev(values) / math.sqrt(len(values))
    return {"blocks": len(values), "mean_mbb_per_hand": mean,
            "ci95_mbb_per_hand": [mean - half, mean + half], "halfwidth_mbb_per_hand": half}


def empty_street_table() -> dict:
    return {seat: {street: {"actions": 0, "fallbacks": 0, "timeouts": 0, "illegal": 0,
                            "chosen_kinds": {kind: 0 for kind in KINDS}}
                   for street in STREETS} for seat in SEATS}


def analyze_one(seed: int, opponent: str, arm: str, expected: dict) -> dict:
    run_name = name(seed, opponent, arm)
    report_path, blocks_path, actions_path, _ = paths(run_name)
    for path, key in ((report_path, "report_sha256"), (blocks_path, "blocks_sha256"),
                      (actions_path, "actions_sha256")):
        if sha256(path) != expected[key]:
            raise RuntimeError(f"E-004 run hash mismatch: {run_name} {key}")
    report = json.loads(report_path.read_text())
    returns: list[list[int]] = []
    values: list[int] = []
    seat_net = [0, 0]
    with blocks_path.open() as stream:
        for index, line in enumerate(stream):
            block = json.loads(line)
            legs = block["candidate_leg_returns_chips"]
            if (block["seed"] != seed or block["block"] != index or len(legs) != 2 or
                    block["candidate_block_mbb_per_hand"] != 5 * sum(legs)):
                raise RuntimeError(f"E-004 block mismatch: {run_name} {index}")
            returns.append(legs)
            values.append(5 * sum(legs))
            seat_net[0] += legs[0]
            seat_net[1] += legs[1]
    if (len(returns) != 5000 or seat_net != report["agent1_by_button_net_chips"] or
            sum(seat_net) != report["net_chips"][0] or sum(report["net_chips"]) != 0 or
            abs(statistics.mean(values) - report["mbb_per_hand"]) > 0.011):
        raise RuntimeError(f"E-004 seat/net/rate mismatch: {run_name}")
    candidate = empty_street_table()
    opponent_table = empty_street_table()
    actor_counts = [0, 0]
    actor_timeouts = [0, 0]
    actor_illegal = [0, 0]
    with actions_path.open() as stream:
        for line in stream:
            action = json.loads(line)
            actor, leg, block = action["actor"], action["leg"], action["block"]
            if (actor not in (0, 1) or leg not in (0, 1) or block >= 5000 or
                    action["seed"] != seed or action["button"] != leg or
                    action["candidate_hand_return_chips"] != returns[block][leg] or
                    action["street"] not in range(4)):
                raise RuntimeError(f"E-004 action mismatch: {run_name}")
            seat = SEATS[leg if actor == 0 else 1 - leg]
            street = STREETS[action["street"]]
            table = candidate if actor == 0 else opponent_table
            item = table[seat][street]
            item["actions"] += 1
            item["chosen_kinds"][action["chosen"]["kind"]] += 1
            item["timeouts"] += action["timeout"]
            item["illegal"] += action["illegal"]
            if actor == 0:
                item["fallbacks"] += action["used_fallback"]
            actor_counts[actor] += 1
            actor_timeouts[actor] += action["timeout"]
            actor_illegal[actor] += action["illegal"]
    fallbacks = sum(candidate[seat][street]["fallbacks"] for seat in SEATS for street in STREETS)
    if (sum(actor_counts) != report["actions"] or sum(actor_timeouts) != report["timeouts"] or
            sum(actor_illegal) != report["illegal_actions"] or fallbacks != report["fallbacks"][0]):
        raise RuntimeError(f"E-004 action/fallback accounting mismatch: {run_name}")
    return {"blocks": values, "summary": {"seed": seed, "opponent": opponent, "arm": arm,
            "interval": interval(values), "seat_net_chips": seat_net,
            "seat_mbb_per_hand": [10 * value / 5000 for value in seat_net],
            "net_chips": report["net_chips"], "candidate": candidate, "opponent_actions": opponent_table,
            "actions_by_actor": actor_counts, "timeouts_by_actor": actor_timeouts,
            "illegal_by_actor": actor_illegal, "seconds": report["seconds"],
            "peak_rss_bytes": report["peak_rss_bytes"]}}


def analyze() -> None:
    data = manifest()
    progress = json.loads((OUTPUT / "progress.json").read_text())
    expected_names = {name(seed, opponent, arm) for seed in data["root_seeds"]
                      for opponent in data["opponents"] for arm in data["arms"]}
    if set(progress["runs"]) != expected_names or any(
            run["status"] != "completed" for run in progress["runs"].values()):
        raise RuntimeError("E-004 requires a complete, successful 60-run matrix")
    rows: dict[tuple[int, str, str], dict] = {}
    summaries: list[dict] = []
    total_seconds = 0.0
    max_rss = 0
    total_timeouts = 0
    total_illegal = 0
    for seed in data["root_seeds"]:
        for opponent in data["opponents"]:
            for arm in data["arms"]:
                result = analyze_one(seed, opponent, arm, progress["runs"][name(seed, opponent, arm)])
                rows[(seed, opponent, arm)] = result
                summary = result["summary"]
                summaries.append(summary)
                total_seconds += summary["seconds"]
                max_rss = max(max_rss, summary["peak_rss_bytes"])
                total_timeouts += sum(summary["timeouts_by_actor"])
                total_illegal += sum(summary["illegal_by_actor"])
    def arm_blocks(opponent: str, arm: str) -> list[int]:
        return [block for seed in data["root_seeds"] for block in rows[(seed, opponent, arm)]["blocks"]]
    opponents = {}
    for opponent in data["opponents"]:
        arm_values = {arm: arm_blocks(opponent, arm) for arm in data["arms"]}
        full = arm_values["full"]
        opponents[opponent] = {
            "arms": {arm: interval(values) for arm, values in arm_values.items()},
            "postflop_only_minus_full": interval([new - old for new, old in zip(arm_values["postflop_only"], full)]),
            "preflop_only_minus_full": interval([new - old for new, old in zip(arm_values["preflop_only"], full)]),
            "equity_only_minus_full": interval([new - old for new, old in zip(arm_values["equity_only"], full)]),
            "factorial_interaction": interval([arm_values["full"][i] - arm_values["preflop_only"][i] -
                                               arm_values["postflop_only"][i] + arm_values["equity_only"][i]
                                               for i in range(len(full))])}
    primary_values = []
    for seed in data["root_seeds"]:
        for block in range(5000):
            primary_values.append(statistics.mean(
                rows[(seed, opponent, "postflop_only")]["blocks"][block] -
                rows[(seed, opponent, "full")]["blocks"][block]
                for opponent in data["primary_opponents"]))
    primary = interval(primary_values)
    passive = opponents["passive"]["postflop_only_minus_full"]
    result = {"schema_version": 1, "experiment": data["id"], "provenance": progress["provenance"],
              "design": {"seeds": data["root_seeds"], "opponents": data["opponents"],
                         "arms": data["arms"], "hands": 600000, "paired_blocks_per_run": 5000},
              "primary": {**primary, "contrast": "postflop_only_minus_full_equal_weight_strong_opponents",
                          "opponents": data["primary_opponents"],
                          "supports_positive_development_effect": primary["ci95_mbb_per_hand"][0] > 0},
              "passive_regression": {**passive, "margin_mbb_per_hand": data["passive_regression_margin_mbb_per_hand"],
                                     "passes_margin": passive["ci95_mbb_per_hand"][0] > data["passive_regression_margin_mbb_per_hand"]},
              "opponents": opponents, "runs": summaries,
              "totals": {"runs": len(summaries), "hands": 600000, "blocks": 300000,
                         "timeouts": total_timeouts, "illegal_actions": total_illegal,
                         "match_seconds": total_seconds, "max_peak_rss_bytes": max_rss}}
    (OUTPUT / "analysis.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    lines = ["# E-004 street-gated learned-policy ablation", "",
             "Development diagnostic only; the same frozen 50k artifact and ten-visit fallback threshold were used in all arms. No candidate is promoted.", "",
             "## Predeclared primary contrast", "",
             f"Postflop-table only minus full hybrid, equal-weighted over TAG, LAG, and equity: **{primary['mean_mbb_per_hand']:+.2f} mbb/hand**, 95% paired interval [{primary['ci95_mbb_per_hand'][0]:+.2f}, {primary['ci95_mbb_per_hand'][1]:+.2f}] over {primary['blocks']:,} shared seed-and-deal-block clusters. The interval {'excludes' if primary['ci95_mbb_per_hand'][0] > 0 else 'does not exclude'} zero positively.", "",
             "## All predeclared opponents", "",
             "| Opponent | Full | Preflop only | Postflop only | Equity only | Postflop only − full 95% CI |",
             "|---|---:|---:|---:|---:|---:|"]
    for opponent in data["opponents"]:
        item = opponents[opponent]
        arms = item["arms"]
        contrast = item["postflop_only_minus_full"]
        lines.append(f"| {opponent} | " + " | ".join(
            f"{arms[arm]['mean_mbb_per_hand']:+.2f}" for arm in data["arms"]) +
            f" | {contrast['mean_mbb_per_hand']:+.2f} [{contrast['ci95_mbb_per_hand'][0]:+.2f}, {contrast['ci95_mbb_per_hand'][1]:+.2f}] |")
    lines += ["", f"Passive postflop-only minus full: {passive['mean_mbb_per_hand']:+.2f} mbb/hand, 95% interval [{passive['ci95_mbb_per_hand'][0]:+.2f}, {passive['ci95_mbb_per_hand'][1]:+.2f}]. Predeclared noninferiority margin {data['passive_regression_margin_mbb_per_hand']:+.0f}; margin {'met' if result['passive_regression']['passes_margin'] else 'not met'} by lower interval bound.", "",
              "Other factorial contrasts and interactions are exploratory; see `analysis.json`. Action frequencies alone do not establish a street-specific mechanism.", "",
              "## Execution", "",
              f"{len(summaries)} complete matches, 600,000 hands, {total_seconds:.1f} match seconds, peak RSS {max_rss} bytes, {total_timeouts} timeouts, {total_illegal} illegal actions. Each arm/opponent/seed has 5,000 two-leg blocks with exact seat/net reconciliation. No validation or final deal stream was used. Per-street/seat action kinds, fallbacks, timeouts, illegal actions, run hashes, and failures are in `analysis.json` and `progress.json`; raw block and action JSONL are retained.", "",
              "The E-002 identical-equity mirror offset remains unresolved. This development result cannot establish Gate E, full-game exploitability, or a promoted policy. A separate frozen validation and new held-out final test would be required."]
    (OUTPUT / "report.md").write_text("\n".join(lines) + "\n")
    print(OUTPUT / "report.md", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("run", "analyze"))
    command = parser.parse_args().command
    if command == "run":
        run_all()
    else:
        analyze()
