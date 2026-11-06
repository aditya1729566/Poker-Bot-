"""Reject stale or dirty PokerZero executables before timed experiments."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_build_info(info: object, commit: str) -> dict:
    if not isinstance(info, dict) or type(info.get("schema_version")) is not int or info["schema_version"] != 1:
        raise RuntimeError("unsupported or malformed build-info response")
    if not isinstance(info.get("git_commit"), str) or info["git_commit"] != commit:
        raise RuntimeError("stale executable: embedded commit differs from clean source commit")
    if type(info.get("dirty_tree")) is not bool or info["dirty_tree"]:
        raise RuntimeError("dirty executable: embedded dirty-tree flag is not false")
    for field in ("build_type", "compiler", "platform"):
        if not isinstance(info.get(field), str) or not info[field].strip():
            raise RuntimeError(f"malformed build-info field: {field}")
    return info


def verify_build(root: Path, binary: Path, expected_sha256: str | None = None) -> dict:
    """Return frozen provenance only for a clean commit and matching executable."""
    root = root.resolve()
    binary = binary.resolve()
    commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=root, capture_output=True,
                            text=True, check=True).stdout.strip()
    if not commit or commit == "unborn":
        raise RuntimeError("build preflight requires a source commit")
    status = subprocess.run(["git", "status", "--porcelain"], cwd=root, capture_output=True,
                            text=True, check=True).stdout.strip()
    if status:
        raise RuntimeError("build preflight requires a clean source tree")
    binary_hash = sha256(binary)
    if expected_sha256 is not None and binary_hash != expected_sha256:
        raise RuntimeError("executable SHA-256 differs from frozen hash")
    result = subprocess.run([str(binary), "build-info"], cwd=root, capture_output=True,
                            text=True, check=True, timeout=5)
    try:
        info = validate_build_info(json.loads(result.stdout), commit)
    except json.JSONDecodeError as error:
        raise RuntimeError("executable returned invalid build-info JSON") from error
    if sha256(binary) != binary_hash:
        raise RuntimeError("executable changed during build preflight")
    return {"source_commit": commit, "binary_sha256": binary_hash, "build_info": info}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="No-match executable provenance preflight")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--expected-sha256")
    args = parser.parse_args()
    root = args.root.resolve()
    executable = args.binary or root / "build/pokerzero"
    if not executable.is_absolute():
        executable = root / executable
    print(json.dumps(verify_build(root, executable, args.expected_sha256), sort_keys=True))
