import { test } from "node:test";
import assert from "node:assert/strict";
import { handClasses, nextHandIndex } from "../lib/study.ts";

test("hand-class matrix has 169 unique labels and bounded arrow navigation", () => {
  assert.equal(handClasses.length, 169);
  assert.equal(new Set(handClasses).size, 169);
  assert.equal(handClasses[0], "AA");
  assert.equal(handClasses[168], "22");
  assert.equal(nextHandIndex(0,"ArrowLeft"),0);
  assert.equal(nextHandIndex(0,"ArrowDown"),13);
  assert.equal(nextHandIndex(13,"ArrowUp"),0);
  assert.equal(nextHandIndex(12,"ArrowRight"),12);
  assert.equal(nextHandIndex(168,"ArrowDown"),168);
  assert.equal(nextHandIndex(15,"Home"),13);
  assert.equal(nextHandIndex(15,"End"),25);
});
