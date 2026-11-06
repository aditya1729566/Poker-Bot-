"""Run predeclared paired benchmarks; freeze one candidate before final evaluation.

The manifest is a deliberately flat YAML subset so this script needs only stdlib.
Large artifacts and reports live under ignored artifacts/; failures are retained.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import subprocess
from pathlib import Path

from build_preflight import verify_build

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "benchmarks/manifest.yaml"
REPORTS = ROOT / "artifacts/reports"


def flat_yaml(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw in path.read_text().splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        if ":" not in line:
            raise ValueError(f"invalid flat YAML line in {path}: {line}")
        key, value = (part.strip() for part in line.split(":", 1))
        if not key or not value or key in values:
            raise ValueError(f"invalid or duplicate key in {path}: {key}")
        values[key] = value
    return values


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def source_tree_sha256() -> str:
    digest = hashlib.sha256()
    for path in sorted(ROOT.rglob("*")):
        if not path.is_file() or any(part in {".git", "build", "build-asan", "artifacts", "__pycache__"} for part in path.relative_to(ROOT).parts):
            continue
        relative = str(path.relative_to(ROOT)).encode()
        digest.update(len(relative).to_bytes(4, "big"))
        digest.update(relative)
        digest.update(bytes.fromhex(sha256(path)))
    return digest.hexdigest()


def git_state() -> tuple[str, bool]:
    commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True)
    status = subprocess.run(["git", "status", "--porcelain"], cwd=ROOT, capture_output=True, text=True, check=True)
    return (commit.stdout.strip() if commit.returncode == 0 else "unborn", bool(status.stdout.strip()))


def candidate_paths(candidate: Path) -> tuple[Path, Path]:
    candidate = candidate.resolve()
    if not candidate.is_file():
        raise FileNotFoundError(candidate)
    key = sha256(candidate)[:12] + "_" + source_tree_sha256()[:12]
    return REPORTS / f"validation_{key}", REPORTS / f"final_{key}"


def run_benchmark(candidate: Path, cohort: str, config: dict[str, str], frozen: dict | None) -> Path:
    build = verify_build(ROOT, ROOT / "build/pokerzero",
                         frozen["binary_sha256"] if frozen else None)
    validation_dir, final_dir = candidate_paths(candidate)
    directory = validation_dir if cohort == "validation" else final_dir
    if cohort == "final" and directory.exists():
        raise RuntimeError("final results already exist; a second final test requires a new held-out experiment")
    directory.mkdir(parents=True, exist_ok=False)
    opponents = config["opponents"].split(",")
    versions = config["opponent_versions"].split(",")
    if len(opponents) != len(versions):
        raise RuntimeError("opponent version list does not match opponents")
    seed = int(config["validation_seed" if cohort == "validation" else "stage4_final_seed"])
    results: dict[str, dict] = {}
    for opponent in opponents:
        verify_build(ROOT, ROOT / "build/pokerzero", build["binary_sha256"])
        hands = int(config["pilot_hands"]) if cohort == "validation" else int(frozen["planned_hands"][opponent])
        output = directory / f"{opponent}.json"
        command = [str(ROOT / "build/pokerzero"), "match", "--agent1", f"restricted:{candidate}",
                   "--agent2", opponent, "--hands", str(hands), "--seed", str(seed),
                   "--cohort", cohort, "--output", str(output)]
        try:
            completed = subprocess.run(command, cwd=ROOT, text=True, capture_output=True,
                                       timeout=int(config["max_experiment_seconds"]))
            if completed.returncode:
                results[opponent] = {"failure_reason": completed.stderr.strip(), "returncode": completed.returncode}
            else:
                result = json.loads(output.read_text())
                result["sha256"] = sha256(output)
                if result["peak_rss_bytes"] > int(config["max_peak_rss_bytes"]):
                    result["failure_reason"] = "peak RSS exceeded benchmark memory budget"
                results[opponent] = result
        except subprocess.TimeoutExpired:
            results[opponent] = {"failure_reason": "experiment time budget exceeded"}
        (directory / "summary.json").write_text(json.dumps({
            "cohort": cohort, "candidate_sha256": sha256(candidate),
            "binary_sha256": build["binary_sha256"], "build_info": build["build_info"],
            "source_tree_sha256": source_tree_sha256(), "manifest_sha256": sha256(MANIFEST),
            "game_config_sha256": sha256(ROOT / config["game_config"]),
            "action_config_sha256": sha256(ROOT / config["action_abstraction_config"]),
            "opponent_versions": dict(zip(opponents, versions)),
            "candidate_trials_before_freeze": int(config["candidate_trials_before_freeze"]),
            "precision_feasible": frozen["precision_feasible"] if frozen else None,
            "results": results}, indent=2, sort_keys=True) + "\n")
    return directory / "summary.json"


def freeze(candidate: Path, config: dict[str, str]) -> Path:
    build = verify_build(ROOT, ROOT / "build/pokerzero")
    validation_dir, _ = candidate_paths(candidate)
    summary = json.loads((validation_dir / "summary.json").read_text())
    opponents = config["opponents"].split(",")
    if set(summary["results"]) != set(opponents):
        raise RuntimeError("validation did not cover every predeclared opponent")
    if summary["source_tree_sha256"] != source_tree_sha256() or summary["manifest_sha256"] != sha256(MANIFEST):
        raise RuntimeError("source or manifest changed after validation")
    if summary["candidate_sha256"] != sha256(candidate):
        raise RuntimeError("candidate changed after validation")
    if summary.get("binary_sha256") != build["binary_sha256"]:
        raise RuntimeError("executable changed after validation")
    commit, dirty = git_state()
    if commit == "unborn" or dirty:
        raise RuntimeError("freeze requires a clean source commit")
    planned: dict[str, int] = {}
    precision_feasible: dict[str, bool] = {}
    for opponent in opponents:
        result = summary["results"][opponent]
        if result.get("failure_reason") or (opponent in config["required_weak_opponents"].split(",") and
                                            (result["timeouts"] or result["illegal_actions"])):
            raise RuntimeError(f"validation failure for {opponent}")
        blocks = result["paired_blocks"]
        half_width = (result["ci95_mbb_per_hand"][1] - result["ci95_mbb_per_hand"][0]) / 2
        sample_sd = half_width * math.sqrt(blocks) / 1.96
        target = int(config["target_ci_halfwidth_mbb"])
        needed_blocks = max(blocks, math.ceil((1.96 * sample_sd / target) ** 2))
        requested = 2 * needed_blocks
        runtime_cap = 2 * math.floor(int(config["max_experiment_seconds"]) * result["hands"] / result["seconds"] / 2)
        cap = min(int(config["max_confirmatory_hands"]), runtime_cap)
        planned[opponent] = max(4, min(requested, cap) // 2 * 2)
        precision_feasible[opponent] = planned[opponent] >= requested
    frozen = {"candidate_sha256": sha256(candidate), "source_tree_sha256": source_tree_sha256(),
              "binary_sha256": build["binary_sha256"], "build_info": build["build_info"],
              "source_commit": commit, "manifest_sha256": sha256(MANIFEST),
              "game_config_sha256": sha256(ROOT / config["game_config"]),
              "action_config_sha256": sha256(ROOT / config["action_abstraction_config"]),
              "planned_hands": planned, "precision_feasible": precision_feasible,
              "validation_summary": str(validation_dir / "summary.json")}
    path = REPORTS / f"frozen_{sha256(candidate)[:12]}_{source_tree_sha256()[:12]}.json"
    path.write_text(json.dumps(frozen, indent=2, sort_keys=True) + "\n")
    return path


def verify_frozen(candidate: Path, config: dict[str, str]) -> dict:
    path = REPORTS / f"frozen_{sha256(candidate)[:12]}_{source_tree_sha256()[:12]}.json"
    frozen = json.loads(path.read_text())
    if "binary_sha256" not in frozen:
        raise RuntimeError("frozen manifest predates executable provenance gate")
    verify_build(ROOT, ROOT / "build/pokerzero", frozen["binary_sha256"])
    commit, dirty = git_state()
    checks = {"candidate_sha256": sha256(candidate), "source_tree_sha256": source_tree_sha256(),
              "source_commit": commit, "manifest_sha256": sha256(MANIFEST),
              "game_config_sha256": sha256(ROOT / config["game_config"]),
              "action_config_sha256": sha256(ROOT / config["action_abstraction_config"])}
    if dirty or any(frozen[key] != value for key, value in checks.items()):
        raise RuntimeError("frozen candidate, source, or config does not match current state")
    return frozen


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=["validation", "freeze", "final"])
    parser.add_argument("--candidate", type=Path, required=True)
    arguments = parser.parse_args()
    config = flat_yaml(MANIFEST)
    if config["schema_version"] != "1" or config["id"] != "initial_hunl_v1":
        raise RuntimeError("unsupported benchmark manifest")
    candidate = arguments.candidate.resolve()
    if arguments.mode == "freeze":
        print(freeze(candidate, config))
    elif arguments.mode == "final":
        print(run_benchmark(candidate, "final", config, verify_frozen(candidate, config)))
    else:
        print(run_benchmark(candidate, "validation", config, None))


if __name__ == "__main__":
    main()
