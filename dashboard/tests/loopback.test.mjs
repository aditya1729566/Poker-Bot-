import { test } from "node:test";
import assert from "node:assert/strict";
import nextConfig from "../next.config.ts";

test("Arena adapter rewrite accepts only a loopback origin", async () => {
  const previous = process.env.ARENA_ADAPTER_URL;
  try {
    process.env.ARENA_ADAPTER_URL = "http://127.0.0.1:8765";
    const local = await nextConfig.rewrites();
    assert.equal(local.beforeFiles[0].destination, "http://127.0.0.1:8765/api/arena/v1/:path*");
    for (const value of ["https://example.com", "http://127.0.0.1:8765/other", "http://user:pass@localhost:8765"]) {
      process.env.ARENA_ADAPTER_URL = value;
      await assert.rejects(nextConfig.rewrites(), /loopback origin/);
    }
  } finally {
    if (previous === undefined) delete process.env.ARENA_ADAPTER_URL;
    else process.env.ARENA_ADAPTER_URL = previous;
  }
});
