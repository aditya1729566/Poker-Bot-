import { test } from "node:test";
import assert from "node:assert/strict";
import { arena, ArenaError } from "../lib/arena.ts";

test("session and replay requests never carry a deal seed", async () => {
  const calls = [];
  const original = globalThis.fetch;
  globalThis.fetch = async (url, init) => {
    calls.push({ url, init });
    return new Response(JSON.stringify({ apiVersion: "arena.v1", sessionId: "opaque", checkpointId: "baseline" }), { status: 200 });
  };
  try {
    await arena.createSession("baseline");
    await arena.replay("opaque-replay");
    assert.deepEqual(JSON.parse(calls[0].init.body), { checkpointId: "baseline" });
    assert.deepEqual(JSON.parse(calls[1].init.body), { replayId: "opaque-replay" });
    assert.equal(JSON.stringify(calls).includes("seed"), false);
  } finally { globalThis.fetch = original; }
});

test("mismatched API versions are rejected", async () => {
  const original = globalThis.fetch;
  globalThis.fetch = async () => new Response(JSON.stringify({ apiVersion: "other.v1", checkpoints: [] }), { status: 200 });
  try { await assert.rejects(arena.checkpoints(), ArenaError); }
  finally { globalThis.fetch = original; }
});

test("restricted fields are rejected in success and error payloads", async () => {
  const original = globalThis.fetch;
  try {
    globalThis.fetch = async () => new Response(JSON.stringify({ apiVersion: "arena.v1", checkpoints: [], dealSeed: "123" }), { status: 200 });
    await assert.rejects(arena.checkpoints(), /restricted field/);
    globalThis.fetch = async () => new Response(JSON.stringify({ apiVersion: "arena.v1", message: "bad", rngState: 4 }), { status: 422 });
    await assert.rejects(arena.checkpoints(), /restricted field/);
    globalThis.fetch = async () => new Response(JSON.stringify({ apiVersion: "arena.v1", state: { opponentHole: ["Ah","As"], terminal: null } }), { status: 200 });
    await assert.rejects(arena.state("opaque"), /opponent card/);
  } finally { globalThis.fetch = original; }
});
