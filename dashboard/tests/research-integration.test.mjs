import { test } from "node:test";
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { createHash } from "node:crypto";
import { join } from "node:path";

const url = process.env.RESEARCH_INTEGRATION_URL;
const dir = process.env.POKERZERO_REPORT_DIR;
const run = url && dir ? test : test.skip;

run("research page renders the configured report without editing it", async () => {
  const path = join(dir, "summary.json");
  const before = await readFile(path);
  const report = JSON.parse(before.toString("utf8"));
  const digest = createHash("sha256").update(before).digest("hex");
  const response = await fetch(new URL("/research", url));
  assert.equal(response.status, 200);
  const html = await response.text();
  assert.match(html, new RegExp(report.cohort.toUpperCase()));
  assert.ok(html.includes(digest), "page should show the source report digest");
  for (const opponent of Object.keys(report.results)) {
    assert.ok(html.includes(opponent.replaceAll("_", " ")), `missing opponent ${opponent}`);
  }
  assert.deepEqual(await readFile(path), before);
});
