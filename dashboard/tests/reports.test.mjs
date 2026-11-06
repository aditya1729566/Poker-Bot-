import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtemp, writeFile, readFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { createHash } from "node:crypto";
import { loadReport } from "../lib/reports.ts";

test("read-only report projection preserves paired intervals and source bytes", async () => {
  const dir = await mkdtemp(join(tmpdir(), "pokerzero-report-"));
  const previous = process.env.POKERZERO_REPORT_DIR;
  const source = JSON.stringify({ cohort: "validation", candidate_sha256: "checkpoint-hash", opponent_versions: { passive: "v1" }, results: { passive: { hands: 20, paired_blocks: 10, mbb_per_hand: 4.5, ci95_mbb_per_hand: [-2,11], agent1_by_button_net_chips: [45,45], ci_method: "paired_block_normal_se", git_commit: "commit" } } });
  try {
    await writeFile(join(dir, "summary.json"), source);
    process.env.POKERZERO_REPORT_DIR = dir;
    const projected = await loadReport();
    assert.equal(projected.cohort, "validation");
    assert.deepEqual(projected.rows[0].ci, [-2,11]);
    assert.equal(projected.rows[0].blocks, 10);
    assert.equal(projected.sourceSha256, createHash("sha256").update(source).digest("hex"));
    assert.equal(await readFile(join(dir,"summary.json"),"utf8"), source);
  } finally {
    if (previous === undefined) delete process.env.POKERZERO_REPORT_DIR;
    else process.env.POKERZERO_REPORT_DIR = previous;
    await rm(dir,{ recursive:true, force:true });
  }
});

test("invalid cohort is rejected", async () => {
  const dir = await mkdtemp(join(tmpdir(), "pokerzero-report-"));
  const previous = process.env.POKERZERO_REPORT_DIR;
  try {
    await writeFile(join(dir,"summary.json"),JSON.stringify({cohort:"unknown",results:{}}));
    process.env.POKERZERO_REPORT_DIR = dir;
    assert.equal(await loadReport(), null);
  } finally {
    if (previous === undefined) delete process.env.POKERZERO_REPORT_DIR;
    else process.env.POKERZERO_REPORT_DIR = previous;
    await rm(dir,{ recursive:true, force:true });
  }
});
