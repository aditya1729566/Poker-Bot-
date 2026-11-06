"""End-to-end no-match checks for executable provenance preflight."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))
import benchmark  # noqa: E402
from build_preflight import sha256, verify_build  # noqa: E402


class BuildPreflightTest(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory(prefix="pokerzero_preflight_")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        (self.root / ".gitignore").write_text("build/\nartifacts/\n")
        subprocess.run(["git", "init", "-q"], cwd=self.root, check=True)
        subprocess.run(["git", "add", ".gitignore"], cwd=self.root, check=True)
        subprocess.run(["git", "-c", "user.name=PokerZero Test",
                        "-c", "user.email=test@example.invalid", "commit", "-qm", "fixture"],
                       cwd=self.root, check=True)
        self.commit = subprocess.check_output(["git", "rev-parse", "HEAD"],
                                              cwd=self.root, text=True).strip()
        self.binary = self.root / "build/pokerzero"
        self.binary.parent.mkdir()
        self.match_marker = self.root / "artifacts/match_started"

    def write_binary(self, *, commit: str, dirty: bool) -> None:
        info = {"schema_version": 1, "git_commit": commit, "dirty_tree": dirty,
                "build_type": "Release", "compiler": "fixture compiler", "platform": "fixture OS"}
        script = ("#!/usr/bin/env python3\n"
                  "import json, pathlib, sys\n"
                  f"info = {info!r}\n"
                  "if sys.argv[1:] == ['build-info']:\n"
                  "    print(json.dumps(info))\n"
                  "elif sys.argv[1:] and sys.argv[1] == 'match':\n"
                  f"    pathlib.Path({str(self.match_marker)!r}).parent.mkdir(parents=True, exist_ok=True)\n"
                  f"    pathlib.Path({str(self.match_marker)!r}).write_text('started')\n"
                  "else:\n"
                  "    raise SystemExit(2)\n")
        self.binary.write_text(script)
        os.chmod(self.binary, 0o755)

    def test_clean_matching_response_and_frozen_hash_accepted(self) -> None:
        self.write_binary(commit=self.commit, dirty=False)
        result = verify_build(self.root, self.binary, sha256(self.binary))
        self.assertEqual(result["source_commit"], self.commit)
        self.assertEqual(result["binary_sha256"], sha256(self.binary))
        self.assertEqual(result["build_info"]["build_type"], "Release")
        cli = subprocess.run([sys.executable,
                              str(Path(__file__).resolve().parents[1] / "python/build_preflight.py"),
                              "--root", self.root.name], cwd=self.root.parent,
                             capture_output=True, text=True, check=True)
        self.assertEqual(json.loads(cli.stdout)["binary_sha256"], sha256(self.binary))
        with self.assertRaisesRegex(RuntimeError, "frozen hash"):
            verify_build(self.root, self.binary, "0" * 64)

    def test_stale_and_dirty_responses_stop_before_match(self) -> None:
        for commit, dirty, reason in (("0" * 40, False, "stale executable"),
                                      (self.commit, True, "dirty executable")):
            with self.subTest(reason=reason):
                self.write_binary(commit=commit, dirty=dirty)
                with patch.object(benchmark, "ROOT", self.root), patch.object(
                        benchmark, "REPORTS", self.root / "artifacts/reports"):
                    with self.assertRaisesRegex(RuntimeError, reason):
                        benchmark.run_benchmark(self.root / "missing_candidate.bin",
                                                "validation", {}, None)
                self.assertFalse(self.match_marker.exists())
                self.assertFalse((self.root / "artifacts/reports").exists())


if __name__ == "__main__":
    unittest.main()
