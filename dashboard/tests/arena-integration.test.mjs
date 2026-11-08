import { test } from "node:test";
import assert from "node:assert/strict";

const origin = process.env.ARENA_INTEGRATION_URL;
const run = origin ? test : test.skip;

run("whole human-versus-passive hand stays public, legal, and replayable through Next proxy", async () => {
  const base = new URL("/api/arena/v1", origin).toString();
  let cookie = "";
  async function request(method, path, body) {
    const response = await fetch(base + path, { method, headers: {
      Origin: new URL(origin).origin,
      ...(cookie ? { Cookie: cookie } : {}),
      ...(body !== undefined ? { "Content-Type": "application/json" } : {})
    }, body: body === undefined ? undefined : JSON.stringify(body), redirect: "manual" });
    const issued = response.headers.get("set-cookie");
    if (issued) {
      assert.match(issued, /HttpOnly/);
      assert.match(issued, /SameSite=Strict/);
      cookie = issued.split(";", 1)[0];
    }
    assert.equal(response.headers.get("cache-control"), "no-store");
    const data = await response.json();
    assert.equal(data.apiVersion, "arena.v1");
    assertNoPrivateFields(data);
    return { status: response.status, data };
  }

  function assertNoPrivateFields(value) {
    if (Array.isArray(value)) return value.forEach(assertNoPrivateFields);
    if (!value || typeof value !== "object") return;
    for (const [key, item] of Object.entries(value)) {
      assert.doesNotMatch(key, /seed|deck|rng|future|replayKey|trainingTarget|privateState/i);
      assertNoPrivateFields(item);
    }
    if ("opponentHole" in value && !value.terminal) assert.equal(value.opponentHole, null);
    if ("opponentHole" in value && value.terminal?.reason === "fold") assert.equal(value.opponentHole, null);
    if ("revealedHole" in value && value.terminal?.reason === "fold") assert.equal(value.revealedHole, null);
  }

  const inventory = await request("GET", "/checkpoints");
  assert.equal(inventory.status, 200);
  assert.equal(inventory.data.checkpoints.find(x => x.id === "passive")?.available, true);
  const wrongType = await fetch(base + "/sessions", { method: "POST", headers: { Origin: new URL(origin).origin, "Content-Type": "text/plain" }, body: JSON.stringify({ checkpointId: "passive" }) });
  assert.equal(wrongType.status, 422);
  const deniedSeed = await request("POST", "/sessions", { checkpointId: "passive", seed: 1 });
  assert.equal(deniedSeed.status, 422);
  const created = await request("POST", "/sessions", { checkpointId: "passive" });
  assert.equal(created.status, 200);
  const id = encodeURIComponent(created.data.sessionId);
  const first = await request("POST", `/sessions/${id}/hands`, {});
  assert.equal(first.status, 200);
  let state = first.data.state;
  const handId = state.handId;
  if (!state.legal.check) {
    const illegal = await request("POST", `/sessions/${id}/actions`, { handId, expectedActionIndex: state.actionIndex, action: { kind: "check" } });
    assert.equal(illegal.status, 422);
    assert.deepEqual(illegal.data.state, state);
  }
  let actions = 0;
  while (!state.terminal && actions < 30) {
    assert.equal(state.humanSeat, 0);
    assert.equal(state.actor, 0);
    assert.equal(state.stacks[0] + state.stacks[1] + state.pot, 20000);
    assert.equal(state.opponentHole, null);
    assert.equal(state.humanHole.length, 2);
    assert.equal(state.publicActions.length, state.actionIndex);
    const action = state.legal.check ? { kind: "check" } : { kind: "call" };
    assert.equal(state.legal.check || state.legal.call, true);
    const response = await request("POST", `/sessions/${id}/actions`, { handId, expectedActionIndex: state.actionIndex, action });
    assert.equal(response.status, 200);
    assert.ok(response.data.state.actionIndex > state.actionIndex);
    state = response.data.state;
    actions++;
  }
  assert.ok(state.terminal, "passive opponent should reach showdown");
  assert.equal(state.terminal.reason, "showdown");
  assert.equal(state.terminal.payoffs[0] + state.terminal.payoffs[1], 0);
  assert.equal(state.stacks[0] + state.stacks[1] + state.pot, 20000);
  const completed = await request("GET", `/sessions/${id}/hands/${encodeURIComponent(handId)}`);
  assert.equal(completed.status, 200);
  assert.equal(completed.data.revealedHole.length, 2);
  assert.equal(completed.data.publicActions.length, state.actionIndex);
  const replayed = await request("POST", "/replays", { replayId: completed.data.replayId });
  assert.equal(replayed.status, 200);
  assert.deepEqual(replayed.data, completed.data);
  const stale = await request("POST", `/sessions/${id}/actions`, { handId, expectedActionIndex: 0, action: { kind: "check" } });
  assert.equal(stale.status, 409);
  const nextHand = await request("POST", `/sessions/${id}/hands`, {});
  assert.equal(nextHand.status, 200);
  assert.equal(nextHand.data.state.button, 1 - state.button);
  assert.notEqual(nextHand.data.state.handId, handId);
});
