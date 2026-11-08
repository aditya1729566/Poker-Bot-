"""Transport and authorization checks with a stand-in native engine.

The C++ session test separately exercises real Game transitions and replay.
"""

import http.client
import importlib.util
import json
import pathlib
import threading
import unittest


MODULE = pathlib.Path(__file__).resolve().parents[1] / "python" / "arena_http.py"
SPEC = importlib.util.spec_from_file_location("arena_http", MODULE)
arena = importlib.util.module_from_spec(SPEC)
import sys
sys.modules[SPEC.name] = arena
SPEC.loader.exec_module(arena)


class NativeStandIn:
    def __init__(self):
        self.calls = []

    def create(self, bot_name, path):
        self.calls.append(("create", bot_name, path))
        return 1

    def destroy(self, handle):
        self.calls.append(("destroy", handle))

    def call(self, name, *arguments):
        self.calls.append((name, arguments))
        if name == "start":
            return 200, {"apiVersion": "arena.v1", "state": {"handId": arguments[1].decode()}}
        if name in ("completed", "replay"):
            return 200, {"apiVersion": "arena.v1", "replayId": "opaque", "revealedHole": None}
        return 200, {"apiVersion": "arena.v1", "state": {"opponentHole": None}}


class TransportTest(unittest.TestCase):
    def setUp(self):
        self.native = NativeStandIn()
        self.store = arena.ArenaStore(self.native, "http://127.0.0.1:3000")
        self.server = arena.ArenaServer(0, self.store)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port)

    def tearDown(self):
        self.connection.close()
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=5)
        self.store.close()

    def request(self, method, path, body=None, origin="http://127.0.0.1:3000", cookie=None):
        headers = {"Content-Type": "application/json"}
        if origin is not None:
            headers["Origin"] = origin
        if cookie:
            headers["Cookie"] = cookie
        self.connection.request(method, path, json.dumps(body or {}), headers)
        response = self.connection.getresponse()
        result = response.status, json.loads(response.read()), response.getheader("Set-Cookie")
        self.assertEqual(result[1]["apiVersion"], "arena.v1")
        return result

    def test_origin_credential_action_and_expiry(self):
        status, _, _ = self.request("POST", arena.API + "/sessions", {"checkpointId": "equity"}, origin="http://evil.example")
        self.assertEqual(status, 403)
        status, session, set_cookie = self.request("POST", arena.API + "/sessions", {"checkpointId": "equity"})
        self.assertEqual(status, 200)
        self.assertIn("HttpOnly", set_cookie)
        self.assertIn("SameSite=Strict", set_cookie)
        cookie = set_cookie.split(";", 1)[0]
        session_path = arena.API + "/sessions/" + session["sessionId"]
        self.assertEqual(self.request("GET", session_path)[0], 403)
        self.assertEqual(self.request("GET", session_path, cookie=cookie)[0], 200)
        self.assertEqual(self.request("POST", session_path + "/hands", cookie=cookie)[0], 200)
        replay_id = next(iter(self.store.sessions[session["sessionId"]].replays))
        self.assertEqual(self.request("POST", session_path + "/actions",
                                      {"handId": "x", "expectedActionIndex": 0,
                                       "action": {"kind": "raiseTo", "target": "bad"}}, cookie=cookie)[0], 422)
        self.assertEqual(self.request("POST", session_path + "/actions",
                                      {"handId": "x", "expectedActionIndex": 0,
                                       "action": {"kind": "check", "target": 0}}, cookie=cookie)[0], 422)
        self.assertEqual(self.request("POST", session_path + "/actions",
                                      {"handId": "x", "expectedActionIndex": 0,
                                       "action": {"kind": {"wrong": "type"}}}, cookie=cookie)[0], 422)
        self.assertEqual(self.request("POST", session_path + "/hands?seed=12", cookie=cookie)[0], 422)
        self.assertEqual(self.request("POST", arena.API + "/replays", {"replayId": "unknown"}, cookie=cookie)[0], 404)
        self.store.sessions[session["sessionId"]].accessed -= arena.LIFETIME + 1
        self.assertEqual(self.request("GET", session_path, cookie=cookie)[0], 410)
        self.assertEqual(self.request("POST", arena.API + "/replays", {"replayId": replay_id}, cookie=cookie)[0], 410)
        self.assertEqual(self.request("POST", arena.API + "/replays", {"replayId": "unknown"}, cookie=cookie)[0], 404)

    def test_no_client_seed_or_path_and_replay_scoping(self):
        status, _, _ = self.request("POST", arena.API + "/sessions",
                                    {"checkpointId": "equity", "seed": 5})
        self.assertEqual(status, 422)
        status, _, _ = self.request("POST", arena.API + "/sessions",
                                    {"checkpointId": "/tmp/foreign-checkpoint.bin"})
        self.assertEqual(status, 422)
        _, first, first_cookie = self.request("POST", arena.API + "/sessions", {"checkpointId": "passive"})
        _, second, second_cookie = self.request("POST", arena.API + "/sessions", {"checkpointId": "equity"})
        first_path = arena.API + "/sessions/" + first["sessionId"]
        self.assertEqual(self.request("GET", first_path, cookie=second_cookie.split(";", 1)[0])[0], 403)
        self.assertEqual(self.request("POST", first_path + "/hands", cookie=first_cookie.split(";", 1)[0])[0], 200)
        replay_id = next(iter(self.store.sessions[first["sessionId"]].replays))
        self.assertEqual(self.request("POST", arena.API + "/replays", {"replayId": replay_id},
                                      cookie=second_cookie.split(";", 1)[0])[0], 403)


if __name__ == "__main__":
    unittest.main()
