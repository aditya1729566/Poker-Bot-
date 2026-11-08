# PokerZero Arena frontend

This Next.js interface is part of the same PokerZero repository. It runs only
against the trusted local C++ Arena adapter proposed in [API v1](../arena/API_V1.md).
The browser never receives a deal seed, future deck, opponent's mucked cards,
or simulator state. It sends engine-defined actions and displays only values
returned by the adapter. Gameplay remains unavailable until the adapter exists.

The UI follows the supplied references: a quiet heads-up table, a dense hand
class index, and a read-only research ledger. The study grid is neutral until
real completed-hand diagnostics exist; it does not invent GTO frequencies.

The first dependency install and lockfile generation are pending the timed
research benchmark. Once `package-lock.json` is committed, use:

```sh
cd dashboard
npm ci
ARENA_ADAPTER_URL=http://127.0.0.1:8765 npm run dev
```

To connect a trusted local adapter, set `ARENA_ADAPTER_URL` to its loopback
origin before starting Next.js. The route at `/api/arena/v1/*` proxies browser
requests to that origin, forwards only the Arena cookie and browser Origin,
and returns 503 when the adapter is absent. The adapter must enforce the v1
cookie and Origin rules. Its default allowed Origin is
`http://127.0.0.1:3000`; use the same host spelling for the dashboard.
For read-only research, set `POKERZERO_REPORT_DIR` to an existing
verified report directory that contains `summary.json`. The report reader
projects selected metrics and provenance; it never edits source reports.

`npm run typecheck`, `npm test`, and `npm run build` are the frontend checks.
When the native harness has passed its C++ and HTTP tests, start it using its
documented `python/arena_http.py` command, then run:

```sh
ARENA_INTEGRATION_URL=http://127.0.0.1:3000 npm run test:integration
```

That test drives a complete hand and replay through Next's proxy. The C++
adapter integration and browser QA remain required before this can be called
a playable, verified human-versus-bot application.
