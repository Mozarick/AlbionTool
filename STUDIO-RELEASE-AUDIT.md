# Market Studio 0.6.0 — delivery audit

Verified on Windows, 2026-10-06. Release: `dist/MarketStudio-0.6.0/MarketStudio.exe`.

## Requested behavior

- Craft removed from runtime code, project and menus.
- Scan batches are absorbed incrementally; scanner waits service the UI and allow safe cancellation. Select all covers all 46 categories.
- Manual shopping adds the displayed row profit after each successful clipboard copy, including repeated copies. Undo removes the last addition; Reset clears progress.
- Shopping list and copy history survive restart. Search, Pending, keyboard actions and filtered CSV export are available.

## Verification

- Release x64 build and planner, shopping list, regression and product suites passed (`tools/studio-release.log`).
- Synthetic stress: 200,000 offers absorbed in 62 ms; planning 200,000 offers across 10,000 items took 102 ms on this machine. These measure data processing, not the time to scan every category in game.
- Native UI/clipboard tests cover repeated copies, Undo, reset, filtering, scrolling, close/cancel and durable session recovery. Rendered shopping, home and scan screens were visually inspected.
- Real loopback HTTP tests cover malformed data, partial requests, bounded shutdown and receiver restart.
- Packaged executable ran from an independent working directory with Windows-only PATH, received real HTTP market data, rendered the dashboard and exited successfully. All 71 manifest entries verified (`tools/studio-smoke.log`).
- Repackaging preserved existing config, scan profile, holdings, shopping state and CSV export (`tools/studio-preservation.log`).

## Delivery boundary

No full scan or automated trading was run against a logged-in Albion game during this verification. Capture and game interaction still use the existing installation and coordinate workflow. Profit is an estimate from the plan, not a confirmed trade result. Preview screenshots use test data.

The Vietnamese user guide is `STUDIO-GUIDE.vi.md` and is included in the release.
