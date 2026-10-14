# Bike and Controls: documentation index

Working documents for the exercise-bike (FTMS) work and the remote-control
navigation pass. Start with the implementation plan for the status of each step.

| File | What it is |
| --- | --- |
| [`implementation-plan.md`](implementation-plan.md) | Phased plan for the bike, controls and bridge work, with the sequencing and per-task notes. |
| [`design.md`](design.md) | Gate G1 design: settings, CLI options, the bike core, the Bike menu and the bridge contract. Where it differs from the plan, it wins. |
| [`phase0-viability.md`](phase0-viability.md) | Phase 0 viability report (verdict: GO). |
| [`controls-probe.md`](controls-probe.md) | The `--input-probe` diagnostic (T3b.0), which logs every SDL event the game receives. |
| [`remote-navigation-audit.md`](remote-navigation-audit.md) | T3b.4 audit: which screens work with arrow keys, Enter, Esc, left/right click and media keys only. |
| [`phase0-spikes/`](phase0-spikes/) | Raw Phase 0 spike material: calibration analysis, Modern Tyrian and web routes, the route-A WebSocket client, and research notes on BLE and the sidecar. |

The launch helper for playing with a bike is in [`../../tools/ftms/`](../../tools/ftms/),
and the player-facing instructions are in the main
[README](../../README.md#playing-with-an-exercise-bike).
