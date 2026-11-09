"""Complete real HTTP hand against the native Game and passive policy."""

import http.client
import json
import pathlib
import socket
import subprocess
import sys
import time


ROOT = pathlib.Path(__file__).resolve().parents[1]
API = "/api/arena/v1"
ORIGIN = "http://127.0.0.1:3000"


def request(port, method, path, body=None, cookie=None):
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
    headers = {"Origin": ORIGIN, "Content-Type": "application/json"}
    if cookie:
        headers["Cookie"] = cookie
    connection.request(method, API + path, json.dumps(body or {}), headers)
    response = connection.getresponse()
    payload = json.loads(response.read())
    set_cookie = response.getheader("Set-Cookie")
    status = response.status
    connection.close()
    assert payload["apiVersion"] == "arena.v1"
    assert not any(word in json.dumps(payload).lower() for word in ('"seed"', '"deck"', '"rng"'))
    return status, payload, set_cookie


def check_state(state):
    assert state["humanSeat"] == 0
    assert state["bigBlind"] == 100
    assert state["actionIndex"] == len(state["publicActions"])
    assert sum(state["stacks"]) + state["pot"] == 20000
    assert len(state["humanHole"]) == 2
    assert len(state["board"]) in (0, 3, 4, 5)
    if state["terminal"] is None:
        assert state["actor"] == 0
        assert state["opponentHole"] is None
        assert state["legal"] is not None
    else:
        assert state["actor"] is None
        assert state["legal"] is None
        assert sum(state["terminal"]["payoffs"]) == 0


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_arena_e2e.py /path/to/libpokerzero_arena")
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    process = subprocess.Popen(
        [sys.executable, str(ROOT / "python" / "arena_http.py"),
         "--library", sys.argv[1], "--port", str(port)],
        cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    try:
        deadline = time.monotonic() + 10
        while True:
            try:
                status, inventory, _ = request(port, "GET", "/checkpoints")
                assert status == 200
                assert any(item["id"] == "passive" and item["available"] for item in inventory["checkpoints"])
                break
            except (ConnectionRefusedError, TimeoutError):
                if time.monotonic() >= deadline or process.poll() is not None:
                    raise RuntimeError("Arena HTTP process did not start")
                time.sleep(0.05)
        status, session, set_cookie = request(port, "POST", "/sessions", {"checkpointId": "passive"})
        assert status == 200
        cookie = set_cookie.split(";", 1)[0]
        base = "/sessions/" + session["sessionId"]
        status, response, _ = request(port, "POST", base + "/hands", cookie=cookie)
        assert status == 200
        state = response["state"]
        check_state(state)
        assert state["button"] == 0
        hand_id = state["handId"]

        status, error, _ = request(port, "POST", base + "/actions", {
            "handId": hand_id, "expectedActionIndex": state["actionIndex"] + 1,
            "action": {"kind": "fold"}}, cookie)
        assert status == 409 and error["state"] == state
        status, error, _ = request(port, "POST", base + "/actions", {
            "handId": hand_id, "expectedActionIndex": state["actionIndex"],
            "action": {"kind": "check"}}, cookie)
        assert status == 422 and error["state"] == state

        for _ in range(12):
            if state["terminal"] is not None:
                break
            legal = state["legal"]
            action = {"kind": "check" if legal["check"] else "call"}
            assert legal[action["kind"]]
            status, response, _ = request(port, "POST", base + "/actions", {
                "handId": hand_id, "expectedActionIndex": state["actionIndex"],
                "action": action}, cookie)
            assert status == 200
            state = response["state"]
            check_state(state)
        assert state["terminal"]["reason"] == "showdown"
        assert len(state["board"]) == 5 and len(state["opponentHole"]) == 2

        status, completed, _ = request(port, "GET", base + "/hands/" + hand_id, cookie=cookie)
        assert status == 200
        assert completed["publicActions"] == state["publicActions"]
        assert completed["board"] == state["board"]
        assert completed["revealedHole"] == state["opponentHole"]
        assert completed["terminal"] == state["terminal"]
        assert len(completed["analysis"]) == state["actionIndex"]
        assert all("distribution" not in item and "equity" not in item for item in completed["analysis"])
        status, replayed, _ = request(port, "POST", "/replays", {"replayId": completed["replayId"]}, cookie)
        assert status == 200 and replayed == completed

        # A second hand has the other button and can end by fold without
        # revealing any bot card or future board.
        status, response, _ = request(port, "POST", base + "/hands", cookie=cookie)
        assert status == 200
        state = response["state"]
        check_state(state)
        assert state["button"] == 1
        # The passive button calls; the big blind can check. Raise once so the
        # passive bot calls, then fold is unavailable. A fresh session provides
        # a direct preflop fold case instead.
        status, second, second_cookie = request(port, "POST", "/sessions", {"checkpointId": "passive"})
        assert status == 200
        second_base = "/sessions/" + second["sessionId"]
        second_cookie = second_cookie.split(";", 1)[0]
        status, response, _ = request(port, "POST", second_base + "/hands", cookie=second_cookie)
        assert status == 200
        fold_state = response["state"]
        status, response, _ = request(port, "POST", second_base + "/actions", {
            "handId": fold_state["handId"], "expectedActionIndex": fold_state["actionIndex"],
            "action": {"kind": "fold"}}, second_cookie)
        assert status == 200
        folded = response["state"]
        assert folded["terminal"]["reason"] == "fold"
        assert folded["board"] == [] and folded["opponentHole"] is None
        status, fold_detail, _ = request(port, "GET", second_base + "/hands/" + folded["handId"], cookie=second_cookie)
        assert status == 200
        assert fold_detail["revealedHole"] is None and fold_detail["board"] == []
        status, fold_replay, _ = request(port, "POST", "/replays", {"replayId": fold_detail["replayId"]}, second_cookie)
        assert status == 200 and fold_replay == fold_detail
        print("arena e2e: showdown, fold, stale/illegal actions, privacy, and replay passed")
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)


if __name__ == "__main__":
    main()
