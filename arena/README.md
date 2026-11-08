# Local Arena adapter

The Arena adapter is an offline, loopback-only human-versus-bot research UI
transport. `Game` is the sole authority for rules, actions, cards, and payouts.
The Python standard-library server controls session credentials, opaque IDs,
private seed generation, and checkpoint allowlisting. It does not write hands
to benchmark or training artifacts.

After building `pokerzero_arena`, run the adapter with the local shared library:

```bash
python3 python/arena_http.py --library build/libpokerzero_arena.dylib
```

On Linux, the library suffix is `.so`. The server listens only on
`127.0.0.1:8765` and accepts mutations only from
`http://127.0.0.1:3000` by default. Configure the dashboard's
`ARENA_ADAPTER_URL=http://127.0.0.1:8765` for its local rewrite. The dashboard
and adapter should use the same loopback host spelling. The browser receives
an HttpOnly, SameSite=Strict cookie scoped to `/api/arena/v1`.

Two baseline policies are available: `equity` and `passive`. To expose one
frozen restricted checkpoint, the trusted local operator must supply both its
exact path and precomputed SHA-256 digest:

```bash
python3 python/arena_http.py --library build/libpokerzero_arena.dylib \
  --restricted-artifact /absolute/path/to/checkpoint.bin \
  --restricted-sha256 EXPECTED_64_CHARACTER_HEX_DIGEST
```

The adapter rechecks that digest before selection. It never accepts a
checkpoint path, seed, or replay transcript in an HTTP request. Each process
allows at most four live sessions, each idle session expires after 30 minutes,
and each session allows at most 256 completed hands. Policy diagnostics appear
only when the loaded policy actually reports them; probability and equity
diagnostics are currently unavailable.

The HTTP surface follows `arena.v1` in `API_V1.md` in the frontend worktree.
This adapter does not make claims about bot strength or use human play data for
official evaluation.
