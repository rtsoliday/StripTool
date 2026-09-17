# Legacy capture attempt: Linux x86_64, 2026-09-17

The installed legacy executable was inspected for baseline provenance:

- Executable: `/usr/local/epics/extensions/bin/linux-x86_64/StripTool`
- Initial fixture: `tests/fixtures/config/canonical-1.2.stp`
- Captured fixture: `tests/fixtures/config/visual-baseline-1.2.stp`, using the
  built-in `CPU_Usage` curve and no IOC
- Display: isolated Xvfb server, 1280x1024x24
- Observed graph geometry: 792x600
- Observed controls geometry: 609x529
- Observed titles: `visual-baseline-1.2.stp Graph` and
  `visual-baseline-1.2.stp Controls`
- Startup reported that both fixtures loaded successfully.

`SHOT-005-graph.png` records the initial graph with the local `CPU_Usage`
curve. The controls window was created but is not mapped at startup, so its
capture remains part of the checklist. Use reachable test IOC names (or a
replay/fake provider) for multi-curve captures without using production PVs.
