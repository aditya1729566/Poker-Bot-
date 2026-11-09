# Arena acceptance result — 2026-11-09

Combined branch: `codex/pokerzero-arena-f1` in the shared Poker-Bot- repository.
Host: Darwin 25.6.0 arm64. Frontend: Next.js 16.4.5, Playwright 1.65.0.
Browser check: headless Google Chrome for Testing 156.0.8078.4 on macOS.

## Commands and outcomes

- `cmake -S . -B build-arena -DCMAKE_BUILD_TYPE=Release` and
  `cmake --build build-arena -j4`: passed.
- `ctest --test-dir build-arena --output-on-failure`: 5/5 passed, including native
  rules/harness, HTTP, and real Arena HTTP end-to-end tests.
- From `dashboard/`: `npm ci`, `npm run typecheck`, `npm run build`: passed.
- `npm test`: 8 passed; two live integration tests skip without their local URL
  variables. No failures.
- With the combined-branch adapter and production Next on loopback,
  `npm run test:integration`: 2/2 passed. The tests cover a full
  human-versus-passive showdown, legal/stale action responses, chip
  conservation, replay equality, hidden-field checks, and read-only final
  report rendering.
- With `ARENA_BROWSER_URL` pointed to the production Next origin,
  `npm run test:browser`: passed. A real browser completed a passive-bot hand,
  restored the same live hand after navigation, inspected replay, checked
  opponent muck after a fold, used arrow-key navigation in the 169-cell grid,
  and found no body-wide overflow at 375, 768, 1024, or 1440 px across Play,
  Study, and Research. No browser page errors were observed.

The final local report fixture was read without modification. Its `summary.json`
SHA-256 was `4f265d6aac1c496642f49cb9123782c74612ff013a2f89089da136c4135b994a`.
Screenshots from the browser check are under ignored local `artifacts/arena-qa-final/`;
they are not benchmark data or committed artifacts.

## Limits

The browser check used headless Chromium. Safari was inspected at desktop
width earlier, but its UI automation became unstable; Safari mobile layouts
were not independently exercised. Policy probability and equity values are
explicitly unavailable where the harness supplies no diagnostics. This is a
playable offline interface, not evidence of poker strength or GTO convergence.
No merge or deployment was performed.
