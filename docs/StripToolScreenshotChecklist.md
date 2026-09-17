# Legacy StripTool Screenshot Checklist

Use this checklist to capture a stable visual reference before substantial Qt
UI work begins. Screenshots are evidence for layout and workflow review, not
pixel-perfect acceptance tests across different window managers and fonts.

## Capture Environment

Record the following alongside each capture set:

- Date, commit, operating system, desktop/window manager, display scaling, and
  color depth.
- Motif/X11 implementation and version.
- Executable build flags, especially history, SDDS, XPM, and clues support.
- Font and X resource overrides.
- IOC or replay-data version and the configuration fixture used.
- Window dimensions in pixels.

Store captures under:

```text
docs/screenshots/legacy/<platform>-<date>/
```

Include a `README.md` in that directory containing the environment record.
Do not commit screenshots containing sensitive PV names or operational data.

## Required Captures

| ID | State | Fixture/setup | Details to include |
|---|---|---|---|
| SHOT-001 | Initial graph | No configuration file | Full graph window and toolbar |
| SHOT-002 | Initial controls | No configuration file | Curves tab with all ten rows |
| SHOT-003 | Timing controls | No configuration file | Controls tab with defaults |
| SHOT-004 | Appearance controls | No configuration file | Colors grids colored axes and line width |
| SHOT-005 | Two live curves | `canonical-1.2.stp` with safe test PV substitutions | Legend axes grid and location readout |
| SHOT-006 | Linear and log curves | `canonical-1.2.stp` | Both scale modes and colored Y axes |
| SHOT-007 | Paused scrolling | Live fixture data | Visible paused-state affordance |
| SHOT-008 | Zoom and pan | Live fixture data | Range after one coarse zoom and pan |
| SHOT-009 | Auto-scale | Deliberately narrow initial limits | Resulting axes and data |
| SHOT-010 | Disconnected PV | One unavailable test PV | Curve row and graph indication |
| SHOT-011 | Annotation selected | Deterministic plotted data | Annotation and selection handles |
| SHOT-012 | Annotation editor | Existing annotation | All editable fields |
| SHOT-013 | From/to dialog | History-enabled build | Initial values and units |
| SHOT-014 | File load dialog | Any build | Attribute-group toggles and file filter |
| SHOT-015 | File save dialog | Any build | Attribute-group toggles and file filter |
| SHOT-016 | Data dump dialog | Any build | Available text CSV and optional SDDS types |
| SHOT-017 | Printer setup | Printing-enabled environment | Printer and device controls |
| SHOT-018 | Graph popup menu | Graph right-click | Every enabled and disabled command |
| SHOT-019 | Help/About | Any build | Version build date and EPICS version |
| SHOT-020 | History joined to live data | History test provider or safe archive data | Boundary between archived and live samples |

## Comparison Procedure

For each implemented Qt workflow:

1. Open the same fixture and deterministic data source in both applications.
2. Match window dimensions and visible time/value ranges.
3. Capture the Qt state using the same ID with a `qt-` filename prefix.
4. Compare control presence, labels, enabled state, ordering, plotted values,
   axes, and user feedback.
5. Record accepted intentional differences in the compatibility document.

Do not use production write-capable PVs to create the baseline. StripTool is
read-only for normal data acquisition, but test capture inputs should still be
isolated and reproducible.

The first automated capture is recorded in
`docs/screenshots/legacy/linux-x86_64-2026-09-17/README.md`. It uses the
built-in `CPU_Usage` curve to avoid requiring an IOC. The initial graph is
captured; states requiring user interaction or multiple reachable channels
remain on this checklist.
