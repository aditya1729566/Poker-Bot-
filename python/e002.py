"""Run and reconcile the predeclared E-002 development mirror calibration."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import statistics
import subprocess
import time
from pathlib import Path

from build_preflight import verify_build
ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "experiments/e002.json"
OUTPUT = ROOT / "artifacts/reports/e002"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_manifest() -> dict:
    data = json.loads(MANIFEST.read_text())
    if (data["id"] != "E-002-equity-mirror-calibration" or
            data["cohort"] != "development" or
            data["root_seeds"] != [54, 55, 56, 57, 58, 59] or
            data["hands_per_seed"] != 10000 or data["action_budget_ms"] != 25 or
            data["agent1"] != "equity" or data["agent2"] != "equity"):
        raise RuntimeError("E-002 predeclaration changed")
    return data


def provenance() -> dict:
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    if subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip():
        raise RuntimeError("E-002 execution requires clean committed source")
    build = verify_build(ROOT, ROOT / "build/pokerzero")
    return {"source_commit": commit, "manifest_sha256": sha256(MANIFEST),
            "binary_sha256": build["binary_sha256"], "build_info": build["build_info"],
            "game_config_sha256": sha256(ROOT / "configs/game.yaml"),
            "benchmark_manifest_sha256": sha256(ROOT / "benchmarks/manifest.yaml")}


def paths(seed: int) -> tuple[Path, Path, Path, Path]:
    base = OUTPUT / f"seed{seed}_equity_mirror"
    return (Path(str(base) + ".json"), Path(str(base) + ".blocks.jsonl"),
            Path(str(base) + ".actions.jsonl"), base)


def save_progress(data: dict) -> None:
    temp = OUTPUT / "progress.tmp"
    temp.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")
    temp.replace(OUTPUT / "progress.json")


def run_all() -> None:
    manifest = read_manifest()
    source = provenance()
    OUTPUT.mkdir(parents=True, exist_ok=True)
    progress_file = OUTPUT / "progress.json"
    if progress_file.exists():
        progress = json.loads(progress_file.read_text())
        if progress["provenance"] != source:
            raise RuntimeError("E-002 source or manifest changed during execution")
    else:
        progress = {"schema_version": 1, "experiment": manifest["id"],
                    "provenance": source, "runs": {}}
        save_progress(progress)
    for seed in manifest["root_seeds"]:
        name = f"seed{seed}"
        report, blocks, actions, prefix = paths(seed)
        if progress["runs"].get(name, {}).get("status") == "completed":
            if not all(p.is_file() for p in (report, blocks, actions)):
                raise RuntimeError(f"completed run missing output: {name}")
            continue
        if any(p.exists() for p in (report, blocks, actions)):
            raise RuntimeError(f"partial output for {name}; inspect before resuming")
        command = [str(ROOT / "build/pokerzero"), "match", "--agent1", "equity",
                   "--agent2", "equity", "--hands", "10000", "--seed", str(seed),
                   "--cohort", "development", "--output", str(report),
                   "--diagnostics-prefix", str(prefix)]
        verify_build(ROOT, ROOT / "build/pokerzero", source["binary_sha256"])
        progress["runs"][name] = {"status": "running", "command": command,
                                  "started_utc_epoch": time.time()}
        save_progress(progress)
        print(f"START {name}", flush=True)
        start = time.monotonic()
        try:
            completed = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                                       timeout=28800)
            (OUTPUT / f"{name}.stdout").write_text(completed.stdout)
            (OUTPUT / f"{name}.stderr").write_text(completed.stderr)
            if completed.returncode != 0 or not all(p.is_file() for p in (report, blocks, actions)):
                status = {"status": "failed", "returncode": completed.returncode,
                          "failure_reason": completed.stderr.strip() or "missing output file"}
            else:
                status = {"status": "completed", "report_sha256": sha256(report),
                          "blocks_sha256": sha256(blocks), "actions_sha256": sha256(actions),
                          "seconds": json.loads(report.read_text())["seconds"]}
        except subprocess.TimeoutExpired as error:
            status = {"status": "failed", "failure_reason": str(error)}
        status["wall_seconds"] = time.monotonic() - start
        progress["runs"][name].update(status)
        save_progress(progress)
        print(f"END {name}: {status['status']} ({status['wall_seconds']:.1f}s)", flush=True)
        if status["status"] != "completed":
            raise RuntimeError(f"E-002 run failed: {name}: {status['failure_reason']}")


def interval(values: list[int]) -> dict:
    mean = statistics.mean(values)
    half = 1.96 * statistics.stdev(values) / math.sqrt(len(values))
    return {"blocks": len(values), "mean_mbb_per_hand": mean,
            "ci95_mbb_per_hand": [mean - half, mean + half],
            "halfwidth_mbb_per_hand": half}


def analyze() -> None:
    manifest = read_manifest()
    progress = json.loads((OUTPUT / "progress.json").read_text())
    if set(progress["runs"]) != {f"seed{s}" for s in manifest["root_seeds"]} or any(
            row["status"] != "completed" for row in progress["runs"].values()):
        raise RuntimeError("six completed seeds required")
    all_blocks: list[int] = []
    all_seat = [0, 0]
    all_actions = [0, 0]
    all_timeouts = [0, 0]
    all_illegal = [0, 0]
    seed_rows: dict[str, dict] = {}
    for seed in manifest["root_seeds"]:
        report_path, block_path, action_path, _ = paths(seed)
        report = json.loads(report_path.read_text())
        if any((sha256(path) != progress["runs"][f"seed{seed}"][key]) for path, key in
               ((report_path, "report_sha256"), (block_path, "blocks_sha256"),
                (action_path, "actions_sha256"))):
            raise RuntimeError(f"output hash mismatch: {seed}")
        values = []
        seat = [0, 0]
        returns = []
        with block_path.open() as stream:
            for index, line in enumerate(stream):
                block = json.loads(line)
                if block["seed"] != seed or block["block"] != index:
                    raise RuntimeError(f"block index mismatch: {seed}")
                legs = block["candidate_leg_returns_chips"]
                if len(legs) != 2 or block["candidate_block_mbb_per_hand"] != 5 * sum(legs):
                    raise RuntimeError(f"block math mismatch: {seed}")
                seat[0] += legs[0]
                seat[1] += legs[1]
                values.append(5 * sum(legs))
                returns.append(legs)
        if len(values) != 5000 or seat != report["agent1_by_button_net_chips"] or sum(seat) != report["net_chips"][0] or sum(report["net_chips"]) != 0:
            raise RuntimeError(f"seat/net mismatch: {seed}")
        actors = [0, 0]
        timeouts = [0, 0]
        illegal = [0, 0]
        with action_path.open() as stream:
            for line in stream:
                action = json.loads(line)
                actor = action["actor"]
                if actor not in (0, 1) or action["seed"] != seed or action["button"] != action["leg"] or action["candidate_hand_return_chips"] != returns[action["block"]][action["leg"]]:
                    raise RuntimeError(f"action mismatch: {seed}")
                actors[actor] += 1
                timeouts[actor] += action["timeout"]
                illegal[actor] += action["illegal"]
        if sum(actors) != report["actions"] or sum(timeouts) != report["timeouts"] or sum(illegal) != report["illegal_actions"]:
            raise RuntimeError(f"action count mismatch: {seed}")
        exact_mean = sum(values) / len(values)
        if abs(exact_mean - report["mbb_per_hand"]) > 0.011:
            raise RuntimeError(f"summary rate mismatch: {seed}")
        seed_rows[str(seed)] = {"interval": interval(values), "seat_net_chips": seat,
                               "seat_mbb_per_hand": [10 * x / 5000 for x in seat],
                               "net_chips": report["net_chips"], "actions_by_player": actors,
                               "timeouts_by_player": timeouts, "illegal_by_player": illegal,
                               "seconds": report["seconds"], "peak_rss_bytes": report["peak_rss_bytes"],
                               "timeout_effect_bound_mbb_per_hand": sum(timeouts) * 200000 / 10000}
        all_blocks.extend(values)
        for i in (0, 1):
            all_seat[i] += seat[i]
            all_actions[i] += actors[i]
            all_timeouts[i] += timeouts[i]
            all_illegal[i] += illegal[i]
    pooled = interval(all_blocks)
    result = {"schema_version": 1, "experiment": manifest["id"],
              "provenance": progress["provenance"], "seeds": seed_rows,
              "pooled": {**pooled, "hands": 60000, "seat_net_chips": all_seat,
                         "seat_mbb_per_hand": [10 * x / 30000 for x in all_seat],
                         "net_chips": [sum(all_seat), -sum(all_seat)],
                         "actions_by_player": all_actions, "timeouts_by_player": all_timeouts,
                         "illegal_by_player": all_illegal,
                         "seconds": sum(row["seconds"] for row in seed_rows.values()),
                         "max_peak_rss_bytes": max(row["peak_rss_bytes"] for row in seed_rows.values()),
                         "timeout_effect_bound_mbb_per_hand": sum(all_timeouts) * 200000 / 60000,
                         "halfwidth_target_met": pooled["halfwidth_mbb_per_hand"] <= 500}}
    (OUTPUT / "analysis.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    lines = ["# E-002 equity mirror calibration", "", "Development diagnostic only. Both players use the unchanged equity agent. No candidate was promoted.", "",
             "## Read-only E-001 audit", "",
             "E-001 seed 51/52/53 mirror returns independently reconcile to +718.930, +1526.195, and +405.633 mbb/hand. Pooled: +883.586 mbb/hand, 95% paired-block interval [+302.383, +1464.789] over 15,000 blocks. Seat, net, action, timeout, and illegal counts agree with source logs; five timeouts split 2/3 by player. No report arithmetic mismatch was found. The duplicate protocol preserves card packets and board while changing the button. Independent policy streams mean equal agents do not have exactly canceling paired outcomes. This does not explain the positive offset.", "",
             "## Fresh calibration", "",
             "95% intervals use a normal standard error over independent duplicate deal blocks. Seed rows are descriptive; the prespecified pooled interval uses 30,000 blocks.", "",
             "| Seed | Player 0 mbb/hand | 95% CI | Player 0 button / big-blind mbb/hand | Actions P0 / P1 | Timeouts P0 / P1 | Illegal P0 / P1 | Seconds |",
             "|---|---:|---:|---:|---:|---:|---:|---:|"]
    def row(label: str, item: dict) -> str:
        ci = item["interval"] if "interval" in item else item
        return (f"| {label} | {ci['mean_mbb_per_hand']:+.2f} | [{ci['ci95_mbb_per_hand'][0]:+.2f}, {ci['ci95_mbb_per_hand'][1]:+.2f}] | "
                f"{item['seat_mbb_per_hand'][0]:+.2f} / {item['seat_mbb_per_hand'][1]:+.2f} | "
                f"{item['actions_by_player'][0]} / {item['actions_by_player'][1]} | "
                f"{item['timeouts_by_player'][0]} / {item['timeouts_by_player'][1]} | "
                f"{item['illegal_by_player'][0]} / {item['illegal_by_player'][1]} | {item['seconds']:.1f} |")
    lines += [row(seed, item) for seed, item in seed_rows.items()]
    lines += [row("Pooled", result["pooled"]), "",
              f"Pooled half-width: {pooled['halfwidth_mbb_per_hand']:.2f} mbb/hand; ≤500 target: {result['pooled']['halfwidth_target_met']}. Conservative timeout-effect bound: ±{result['pooled']['timeout_effect_bound_mbb_per_hand']:.2f} mbb/hand. This assumes every timeout could swing a 100bb hand from -100bb to +100bb.", "",
              "The frozen E-001 and Stage 4 artifacts were not modified. See `docs/e002_protocol.md` for deck mapping, RNG allocation, metamorphic transformation, and analysis definition. Machine-readable block and action data, run summaries, hashes, and failures are in this directory."]
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
