import { test } from "node:test";
import assert from "node:assert/strict";
import { localAdapterOrigin } from "../lib/adapter-url.ts";

test("Arena adapter proxy accepts only a loopback origin", () => {
  assert.equal(localAdapterOrigin(undefined), null);
  assert.equal(localAdapterOrigin("http://127.0.0.1:8765"), "http://127.0.0.1:8765");
  for (const value of ["https://example.com", "http://127.0.0.1:8765/other", "http://user:pass@localhost:8765"]) {
    assert.throws(() => localAdapterOrigin(value), /loopback origin/);
  }
});
