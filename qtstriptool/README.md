# Qt StripTool

This directory contains the Qt replacement. It currently provides a
C++17 application shell, an explicit `core`/`services`/`ui` boundary, embedded
resources, build-time Qt and EPICS version reporting, and offscreen tests.

The compatibility layer in `core/model.*` and `core/config.*` is independent
of Qt and X11. It defines typed graph, curve, timing, annotation, and runtime
metadata models; reads current `StripConfig 1.2` and pre-header legacy files;
writes Motif-readable 1.2 files; retains unknown attributes; and provides
layered-default and legacy file-search helpers. Plotting is intentionally
kept separate from configuration parsing and acquisition.

The acquisition layer records every EPICS Channel Access monitor update with
its value, status, severity, and workstation delivery time. Using the same
clock as the live graph keeps curves from different IOCs aligned even when IOC
clocks differ. Display refresh is independent of acquisition. The isolated
local `CPU_Usage` provider retains a periodic sample timer. Both paths use
bounded ring buffers, stale/reconnect
state, `.DESC` lookup for curve comments, and toolkit-neutral min/max
decimation that preserves transitions, spikes, and disconnected gaps.
Normal tests use a deterministic fake provider. To exercise a live IOC, run:

```sh
make test-ioc TEST_PV=some:readable:numeric:pv
```

`ui/plot_widget.*` implements the graph directly with `QPainter`. It supports
multiple colored linear and logarithmic step traces, a selectable Y axis, time
labels, grid modes, decimated live updates, auto-scroll and fixed ranges, pan and zoom,
reset/replot/auto-scale operations, cursor readout, annotations, and joining
historical samples with live data. Plot transforms and range selection remain
in `core/plot_data.*` so they can be tested without a display.

`ui/controls_window.*` provides the companion controls window using standard
Qt widgets. Its ten curve rows, PV entry, timing controls, graph appearance
controls, and menus edit the same `StripToolModel` used by the graph. Model
notifications repaint the graph, while PV and timing changes safely restart
acquisition.

The File menu now provides open/save/save-as with persistent recent files,
plain-text and CSV data export, PNG/JPEG plot snapshots, and native Qt print
and print-preview dialogs. Help honors `STRIP_HELP_PATH`, and the historical
range dialog targets a cancellable provider interface. The default
`NoHistoryProvider` reports that no archive is configured; a site-supported
archive can be added behind that interface without coupling it to the GUI.

Run `make test-visual` to create deterministic graph, main-window, and controls
renderings in `qtstriptool/O.<platform>-qt<version>/test-artifacts/` for human
comparison with the legacy screenshot checklist.

Build and test it from the repository root:

```sh
make qtstriptool
make test-qtstriptool
```

The full test target is split into independently runnable gates:

```sh
make test-qtstriptool-core
make test-qtstriptool-config
make test-qtstriptool-ui
make test-qtstriptool-performance
make test-qtstriptool-visual
make test-qtstriptool-ioc TEST_PV=some:readable:numeric:pv
make test-qt-versions
```

The parity status, cross-platform verification record, manual comparison
procedure, and soak-test criteria are maintained in
`docs/QtParityMatrix.md`.

Qt 5.15 or Qt 6 and a built EPICS Base are required. Override discovery with
`QT_VERSION=5|6` and `EPICS_BASE=/path/to/base`.
