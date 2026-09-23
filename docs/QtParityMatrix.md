# Qt StripTool parity and release gates

This matrix tracks current evidence for the behavioral inventory in
`striptool-feature-matrix.csv`. The CSV's initial status labels are historical;
use this matrix for the current gate record. “Automated” means the repository contains a
repeatable test; “manual” means results must be recorded for each release
candidate. Platform entries describe what has actually been run, not merely
what the build files intend to support.

| Area | Motif reference | Qt implementation | Gate | Current evidence |
|---|---|---|---|---|
| Configuration | `.stp` 1.2 and legacy parser | Toolkit-neutral parser/writer | Automated | `make test-qtstriptool-config` |
| Defaults/search | Compiled defaults and search path | Model defaults and layered lookup | Automated | `make test-qtstriptool-config` |
| Buffering | Bounded StripDataSource storage | `SampleBuffer` | Automated | `make test-qtstriptool-core` |
| Sampling/refresh | Independent Motif timers | Independent acquisition timers | Automated | `make test-qtstriptool-core` |
| CA reconnect/metadata | `StripCA.c` | `ChannelAccessProvider` | Fake automated; IOC opt-in | `make test-qtstriptool-core`; `make test-qtstriptool-ioc TEST_PV=...` |
| Plot transforms | `StripGraph.c`/`jlAxis.c` | Toolkit-neutral selection plus `QPainter` | Automated | core, UI, and visual targets |
| Graph interaction | `Strip.c`/`Annotation.c` | Time and value pan/zoom, fine right-button toolbar steps, graph menu, annotation selection and middle-button drag | Automated offscreen and Cocoa event tests; physical mouse manual | `make test-qtstriptool-ui`; Qt 5/6 Cocoa interaction cases on 2026-09-23 |
| Controls | `StripDialog.c` | Shared-model Qt controls | Automated | `make test-qtstriptool-ui` |
| File/output | Motif dialogs and wide dump rows | Workflow/export services and long dump rows | Automated basics; format parity open | config, core, and UI targets; see known differences |
| History lifecycle | `StripHistory.h` | Cancellable provider interface | Automated | no-history and deterministic providers |
| APS archive backend | Site-selected legacy backend | Not selected | Site decision | Backend and endpoint must be supplied |
| Printing | X11/shell print path | Qt PrintSupport | Build/UI action automated; output manual | Print to PDF and inspect |
| SDDS output | Optional compile path | Not enabled | Site decision | Confirm operational requirement |

## Platform gate record

| Platform | Qt 5.15 | Qt 6 | Motif comparison | Status |
|---|---|---|---|---|
| Linux x86_64 | Build and tests available locally | Run when dependencies are installed | Builds locally | Qt 5 verified on 2026-09-17 |
| macOS arm64 | Full non-IOC suite and visual render passed locally on 2026-09-23 | Full non-IOC suite, visual render, and local soft IOC CA test passed on 2026-09-23 | Compared available legacy single-curve screenshot; full side-by-side pending | Local build/test verified; site IOC, reconnect, and soak gates pending |
| Windows MSVC | Optional Qt 5 | Required | Not applicable | Awaiting CI/host |

Use `make test-qt-versions` to run every locally installed Qt major version.
A skipped unavailable major is reported explicitly. Platform claims must only
be updated after a clean build and the full non-IOC test suite pass.

## Manual side-by-side procedure

1. Build both variants with `make all` and launch each with the same fixture or
   site `.stp` file.
2. Connect the same ten-PV set and compare values, units, precision, alarms,
   disconnect/reconnect behavior, and CPU use.
3. Exercise pause, auto-scroll, pan, zoom, autoscale, annotations, and fixed
   history ranges while comparing visible time and value ranges.
4. Compare text/CSV exports numerically and inspect snapshot and print-to-PDF
   output.
5. Record the operating system, Qt version, EPICS version, IOC/archive source,
   duration, and any accepted difference in this document or the release
   report.

## Performance gates

The automated performance target runs ten curves with 10,000 samples each,
renders the resulting large range, and exercises repeated window startup and
shutdown. The deliberately broad ten-second limits detect severe regressions
without making ordinary shared CI hosts flaky:

```sh
make test-qtstriptool-performance
```

Release candidates additionally require a manual soak because wall-clock and
resident-memory expectations depend on deployment hardware:

| Scenario | Measurement | Acceptance record |
|---|---|---|
| Idle, no configured curves | CPU over 10 minutes | Compare with Motif; record host and result |
| Ten curves at 10 ms sampling | CPU, lost updates, repaint latency for 30 minutes | Threshold agreed by site owner |
| Long duration | Resident memory over 24 hours | No sustained unbounded growth |
| Large archive range | Request and first-paint latency | Threshold agreed for selected backend |
| Startup/shutdown | Median of 20 runs | Compare against previous Qt release |

The current no-history provider cannot satisfy the archive latency gate. That
gate becomes applicable once the site selects and configures its supported APS
archive backend.
