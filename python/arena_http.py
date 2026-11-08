"""Trusted, process-local Arena HTTP boundary for offline human-versus-bot play.

Run only with a locally built libpokerzero_arena. The browser never supplies a
deal seed, checkpoint path, or replay transcript. This service writes no human
hands into training, validation, or benchmark artifacts.
"""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import http.cookies
import json
import pathlib
import secrets
import sys
import time
from dataclasses import dataclass, field
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import urlsplit


API = "/api/arena/v1"
LIFETIME = 30 * 60
MAX_SESSIONS = 4
MAX_BODY = 4096
ACTION_KINDS = {"fold": 0, "check": 1, "call": 2, "raiseTo": 3}


class NativeResult(ctypes.Structure):
    _fields_ = [("status", ctypes.c_int), ("json", ctypes.c_void_p)]


class NativeArena:
    def __init__(self, library: pathlib.Path):
        self.lib = ctypes.CDLL(str(library.resolve(strict=True)))
        pointer = ctypes.c_void_p
        chars = ctypes.c_char_p
        self.lib.pz_arena_create.argtypes = [chars, chars, ctypes.c_uint64]
        self.lib.pz_arena_create.restype = pointer
        self.lib.pz_arena_destroy.argtypes = [pointer]
        self.lib.pz_arena_free.argtypes = [pointer]
        for name, arguments in {
            "start": [pointer, chars, chars],
            "state": [pointer],
            "act": [pointer, chars, ctypes.c_uint64, ctypes.c_int, ctypes.c_int64],
            "completed": [pointer, chars],
            "replay": [pointer, chars],
        }.items():
            function = getattr(self.lib, "pz_arena_" + name)
            function.argtypes = arguments
            function.restype = NativeResult

    def create(self, bot_name: str, path: pathlib.Path | None):
        encoded_path = str(path).encode() if path else None
        handle = self.lib.pz_arena_create(bot_name.encode(), encoded_path, secrets.randbits(64))
        if not handle:
            raise RuntimeError("Arena engine could not load the selected policy.")
        return handle

    def destroy(self, handle):
        self.lib.pz_arena_destroy(handle)

    def call(self, name: str, *arguments):
        result = getattr(self.lib, "pz_arena_" + name)(*arguments)
        if not result.json:
            return 500, {"apiVersion": "arena.v1", "message": "Arena engine error."}
        try:
            return result.status, json.loads(ctypes.string_at(result.json).decode("utf-8"))
        finally:
            self.lib.pz_arena_free(result.json)


@dataclass
class SessionRecord:
    handle: int
    credential: str
    checkpoint_id: str
    accessed: float
    replays: dict[str, str] = field(default_factory=dict)


class ArenaStore:
    def __init__(self, native: NativeArena, origin: str,
                 restricted_path: pathlib.Path | None = None,
                 restricted_sha256: str | None = None):
        self.native = native
        self.origin = origin
        self.restricted_path = restricted_path
        self.restricted_sha256 = restricted_sha256.lower() if restricted_sha256 else None
        self.sessions: dict[str, SessionRecord] = {}
        self.expired: dict[str, float] = {}
        self.expired_replays: dict[str, tuple[float, str]] = {}

    def close(self):
        for record in self.sessions.values():
            self.native.destroy(record.handle)
        self.sessions.clear()

    def expire(self):
        now = time.monotonic()
        for session_id, record in list(self.sessions.items()):
            if now - record.accessed >= LIFETIME:
                self.native.destroy(record.handle)
                del self.sessions[session_id]
                self.expired[session_id] = now
                for replay_id in record.replays:
                    self.expired_replays[replay_id] = (now, record.credential)
        self.expired = {key: when for key, when in self.expired.items() if now - when < LIFETIME}
        self.expired_replays = {key: value for key, value in self.expired_replays.items()
                                if now - value[0] < LIFETIME}

    def restricted_verified(self) -> bool:
        if not self.restricted_path or not self.restricted_sha256:
            return False
        try:
            digest = hashlib.sha256()
            with self.restricted_path.open("rb") as stream:
                for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                    digest.update(chunk)
            return secrets.compare_digest(digest.hexdigest(), self.restricted_sha256)
        except OSError:
            return False

    def inventory(self):
        baseline = [
            ("equity", "Equity baseline", "Samples uniform opponent hands; no learned strategy."),
            ("passive", "Passive baseline", "Always checks or calls; for interface testing."),
        ]
        result = [{"id": key, "label": label, "sha256": None, "sourceCommit": None,
                   "kind": "baseline", "available": True, "limitations": limits}
                  for key, label, limits in baseline]
        result.append({"id": "restricted", "label": "Frozen restricted policy",
                       "sha256": self.restricted_sha256, "sourceCommit": None,
                       "kind": "restricted", "available": self.restricted_verified(),
                       "limitations": "Loads only the explicitly pinned local artifact; no strength claim."})
        return result

    def create(self, checkpoint_id: str):
        allowed = {entry["id"]: entry for entry in self.inventory()}
        if checkpoint_id not in allowed or not allowed[checkpoint_id]["available"]:
            return 422, {"apiVersion": "arena.v1", "message": "Checkpoint is unavailable."}, None
        if len(self.sessions) >= MAX_SESSIONS:
            return 503, {"apiVersion": "arena.v1", "message": "Local session limit reached."}, None
        path = self.restricted_path if checkpoint_id == "restricted" else None
        try:
            handle = self.native.create(checkpoint_id, path)
        except RuntimeError:
            return 503, {"apiVersion": "arena.v1", "message": "Arena engine could not load policy."}, None
        session_id = secrets.token_urlsafe(24)
        credential = secrets.token_urlsafe(32)
        self.sessions[session_id] = SessionRecord(handle, credential, checkpoint_id, time.monotonic())
        return 200, {"apiVersion": "arena.v1", "sessionId": session_id,
                     "checkpointId": checkpoint_id}, credential

    def lookup(self, session_id: str, credential: str | None):
        record = self.sessions.get(session_id)
        if record is None:
            status = 410 if session_id in self.expired else 404
            message = "Session expired." if status == 410 else "Session not found."
            return status, {"apiVersion": "arena.v1", "message": message}, None
        if credential is None or not secrets.compare_digest(record.credential, credential):
            return 403, {"apiVersion": "arena.v1", "message": "Session credential required."}, None
        record.accessed = time.monotonic()
        return 200, None, record


class ArenaServer(HTTPServer):
    def __init__(self, port: int, store: ArenaStore):
        super().__init__(("127.0.0.1", port), ArenaHandler)
        self.store = store


class ArenaHandler(BaseHTTPRequestHandler):
    server: ArenaServer

    def log_message(self, format_string, *arguments):
        # Request targets may contain attacker-controlled data; never log them.
        sys.stderr.write("arena_http: request handled\n")

    def reply(self, status: int, payload: dict, credential: str | None = None):
        encoded = json.dumps(payload, separators=(",", ":"), allow_nan=False).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(encoded)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        if credential:
            self.send_header("Set-Cookie", f"pz_arena={credential}; HttpOnly; SameSite=Strict; Path={API}; Max-Age={LIFETIME}")
        self.end_headers()
        self.wfile.write(encoded)

    def failure(self, status: int, message: str):
        self.reply(status, {"apiVersion": "arena.v1", "message": message})

    def credential(self) -> str | None:
        try:
            cookie = http.cookies.SimpleCookie(self.headers.get("Cookie", ""))
            return cookie["pz_arena"].value if "pz_arena" in cookie else None
        except http.cookies.CookieError:
            return None

    def body(self):
        if self.headers.get("Content-Type", "").split(";", 1)[0].strip().lower() != "application/json":
            raise ValueError("JSON content type required.")
        try:
            length = int(self.headers.get("Content-Length", ""))
        except ValueError as exc:
            raise ValueError("Invalid request length.") from exc
        if length < 0 or length > MAX_BODY:
            raise ValueError("Request body is too large.")
        try:
            parsed = json.loads(self.rfile.read(length))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise ValueError("Invalid JSON body.") from exc
        if not isinstance(parsed, dict):
            raise ValueError("JSON object required.")
        return parsed

    def session(self, session_id: str):
        status, error, record = self.server.store.lookup(session_id, self.credential())
        if record is None:
            self.reply(status, error)
        return record

    def do_GET(self):
        self.dispatch("GET")

    def do_POST(self):
        self.dispatch("POST")

    def dispatch(self, method: str):
        store = self.server.store
        store.expire()
        if len(self.path) > 512:
            self.failure(422, "Request target is too long.")
            return
        # A local Host and exact local Origin are both required. Next.js proxies
        # preserve the browser Origin; an unrelated site cannot mutate sessions.
        try:
            host_header = urlsplit("http://" + self.headers.get("Host", ""))
            local_host = host_header.hostname in ("127.0.0.1", "localhost") and not host_header.username and not host_header.password and not host_header.path
        except ValueError:
            local_host = False
        if not local_host:
            self.failure(403, "Local host required.")
            return
        origin = self.headers.get("Origin")
        if origin and origin != store.origin:
            self.failure(403, "Origin is not allowed.")
            return
        if method == "POST" and origin != store.origin:
            self.failure(403, "Local origin required.")
            return
        parsed_path = urlsplit(self.path)
        if parsed_path.query or parsed_path.fragment:
            self.failure(422, "Query parameters are not supported.")
            return
        segments = parsed_path.path.split("/")
        if segments[:4] != ["", "api", "arena", "v1"]:
            self.failure(404, "Endpoint not found.")
            return
        path = segments[4:]
        try:
            if method == "GET" and path == ["checkpoints"]:
                self.reply(200, {"apiVersion": "arena.v1", "checkpoints": store.inventory()})
                return
            if method == "POST" and path == ["sessions"]:
                body = self.body()
                if set(body) != {"checkpointId"} or not isinstance(body["checkpointId"], str):
                    raise ValueError("Invalid session request.")
                status, payload, credential = store.create(body["checkpointId"])
                self.reply(status, payload, credential)
                return
            if method == "POST" and path == ["replays"]:
                body = self.body()
                if set(body) != {"replayId"} or not isinstance(body["replayId"], str):
                    raise ValueError("Invalid replay request.")
                credential = self.credential()
                if credential is None:
                    self.failure(403, "Session credential required.")
                    return
                for record in store.sessions.values():
                    if body["replayId"] in record.replays:
                        if not secrets.compare_digest(credential, record.credential):
                            self.failure(403, "Session credential required.")
                            return
                        record.accessed = time.monotonic()
                        hand_id = record.replays[body["replayId"]]
                        status, payload = store.native.call("replay", record.handle, hand_id.encode())
                        self.reply(status, payload, record.credential)
                        return
                expired = store.expired_replays.get(body["replayId"])
                if expired and secrets.compare_digest(credential, expired[1]):
                    self.failure(410, "Replay expired.")
                else:
                    self.failure(404, "Replay not found.")
                return
            if len(path) >= 2 and path[0] == "sessions":
                session_id = path[1]
                record = self.session(session_id)
                if record is None:
                    return
                if method == "GET" and len(path) == 2:
                    status, payload = store.native.call("state", record.handle)
                elif method == "POST" and path[2:] == ["hands"]:
                    if self.body():
                        raise ValueError("Hand request must be empty.")
                    hand_id, replay_id = secrets.token_urlsafe(24), secrets.token_urlsafe(24)
                    status, payload = store.native.call("start", record.handle, hand_id.encode(), replay_id.encode())
                    if status == 200:
                        record.replays[replay_id] = hand_id
                elif method == "POST" and path[2:] == ["actions"]:
                    body = self.body()
                    if set(body) != {"handId", "expectedActionIndex", "action"}:
                        raise ValueError("Invalid action request.")
                    action = body["action"]
                    if not isinstance(body["handId"], str) or not isinstance(body["expectedActionIndex"], int) or isinstance(body["expectedActionIndex"], bool) or body["expectedActionIndex"] < 0 or body["expectedActionIndex"] > 1000 or not isinstance(action, dict) or not isinstance(action.get("kind"), str) or action["kind"] not in ACTION_KINDS:
                        raise ValueError("Invalid action request.")
                    kind = action["kind"]
                    if set(action) != ({"kind", "target"} if kind == "raiseTo" else {"kind"}):
                        raise ValueError("Invalid action request.")
                    target = action.get("target", 0)
                    if not isinstance(target, int) or isinstance(target, bool) or target < 0 or target > 10_000:
                        raise ValueError("Invalid action target.")
                    status, payload = store.native.call("act", record.handle, body["handId"].encode(),
                                                        body["expectedActionIndex"], ACTION_KINDS[kind], target)
                elif method == "GET" and len(path) == 4 and path[2] == "hands":
                    status, payload = store.native.call("completed", record.handle, path[3].encode())
                else:
                    self.failure(404, "Endpoint not found.")
                    return
                self.reply(status, payload, record.credential)
                return
            self.failure(404, "Endpoint not found.")
        except ValueError as exc:
            self.failure(422, str(exc))
        except Exception:
            self.failure(500, "Arena adapter error.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", type=pathlib.Path, required=True)
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--origin", default="http://127.0.0.1:3000")
    parser.add_argument("--restricted-artifact", type=pathlib.Path)
    parser.add_argument("--restricted-sha256")
    options = parser.parse_args()
    if not (1 <= options.port <= 65535):
        parser.error("port must be in 1..65535")
    parsed_origin = urlsplit(options.origin)
    if parsed_origin.scheme != "http" or parsed_origin.hostname not in ("127.0.0.1", "localhost") or parsed_origin.path not in ("", "/"):
        parser.error("origin must be a local HTTP origin")
    if bool(options.restricted_artifact) != bool(options.restricted_sha256):
        parser.error("restricted artifact and sha256 must be supplied together")
    if options.restricted_sha256 and (len(options.restricted_sha256) != 64 or any(c not in "0123456789abcdefABCDEF" for c in options.restricted_sha256)):
        parser.error("restricted sha256 must be 64 hexadecimal characters")
    store = ArenaStore(NativeArena(options.library), options.origin.rstrip("/"),
                       options.restricted_artifact, options.restricted_sha256)
    server = ArenaServer(options.port, store)
    try:
        print(f"Arena listening on http://127.0.0.1:{options.port}{API}", flush=True)
        server.serve_forever()
    finally:
        store.close()
        server.server_close()


if __name__ == "__main__":
    main()
