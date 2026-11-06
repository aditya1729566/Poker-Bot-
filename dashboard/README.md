# PokerZero Arena frontend

This Next.js interface is part of the same PokerZero repository. It runs only
against the trusted local C++ Arena adapter proposed in [API v1](../arena/API_V1.md).
The browser never receives a deal seed, future deck, opponent's mucked cards,
or simulator state. It sends engine-defined actions and displays only values
returned by the adapter. Gameplay remains unavailable until the adapter exists.

The UI follows the supplied references: a quiet heads-up table, a dense hand
class index, and a read-only research ledger. The study grid is neutral until
real completed-hand diagnostics exist; it does not invent GTO frequencies.

```sh
cd dashboard
npm ci
npm run dev
```

To connect a trusted local adapter, set `ARENA_ADAPTER_URL` to its loopback
origin before starting Next.js. The adapter must enforce the v1 cookie and
Origin rules. For read-only research, set `POKERZERO_REPORT_DIR` to an existing
verified report directory that contains `summary.json`. The report reader
projects selected metrics and provenance; it never edits source reports.

`npm run typecheck`, `npm test`, and `npm run build` are the frontend checks.
The C++ adapter integration and end-to-end checks remain required before this
can be called a playable, verified human-versus-bot application.
