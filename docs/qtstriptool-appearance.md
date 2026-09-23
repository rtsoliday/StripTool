# Qt StripTool appearance and visual review

Qt StripTool preserves graph meaning and control workflows while using normal
Qt widgets and painting. Exact Motif pixels are not a replacement requirement:
fonts, dialog controls, window decorations, focus indication, spacing, printer
metrics, and DPI scaling follow the active Qt platform integration.

## Appearance controls

The Controls window Appearance tab sets foreground, background, grid color,
X and Y grid density, colored Y-axis labels, and graph line width. Each curve
has its own color. Curve scale, precision, and bounds control Y-axis formatting
and transformation. These values round-trip through `.stp` files where the
legacy format defines them.

The application accepts standard Qt style options. For a relatively consistent
cross-desktop review baseline, launch with:

```sh
qtstriptool -style fusion configuration.stp
```

This does not emulate Motif and is not forced for users. Native platform styles
remain supported. Screenshots should record the OS, Qt version, style, font,
DPI/scaling, window size, and fixture used.

## Intentional visual differences

- The controls are tabbed and resize with layouts rather than reproducing
  fixed Motif geometry.
- Menus, file/color/print dialogs, keyboard focus, and accessibility feedback
  use Qt/platform conventions.
- The graph is painted with `QPainter`; line rasterization, font metrics, time
  labels, and image/print scaling can differ from X11.
- Window icons and desktop identity use the Qt StripTool artwork and name so
  both variants can be installed simultaneously.
- High-DPI scaling is delegated to Qt instead of relying on legacy X display
  millimeter geometry.

A visual difference is a defect when it hides data, changes scale or time
meaning, makes curve identity ambiguous, clips required controls, or breaks an
interaction—not merely because antialiasing or widget chrome differs.

## Automated fixture and human review

Generate the deterministic offscreen fixture with:

```sh
make test-qtstriptool-visual
```

The command prints the generated PNG path below the Qt object directory. It
checks that the fixture renders; it does not approve appearance automatically.
Follow `StripToolScreenshotChecklist.md` for the legacy reference capture and
compare both variants using the same data, ranges, colors, scale modes, grid
modes, line width, and window dimensions.

Review at least:

1. Empty/default graph and default controls.
2. Ten simultaneous curves with colored axes and overlapping ranges.
3. Linear and log curves, including invalid non-positive log samples.
4. None/some/all grid modes and line widths at normal and high DPI.
5. Live auto-scroll, paused/panned/zoomed states, cursor readout, and
   annotations.
6. Disconnected/stale curves and reconnect transitions.
7. Snapshot, print preview, and print-to-PDF legibility.
8. Keyboard navigation, focus visibility, contrast, resizing, and text
   clipping with the site's normal desktop theme.

Record accepted differences with the release candidate rather than replacing
the deterministic fixture merely to make a comparison pass.

## Local visual review, 2026-09-23

The macOS arm64 Qt 6 fixture renders readable Y-axis ticks and units, time
labels, a date, a graph toolbar, and the ten-row controls. The graph was
compared with the available Linux Motif `SHOT-005-graph.png`: both show colored
scales, a dashed grid, a white plotting area, and time context. The traces and
platforms differ, so this is a layout check rather than the required
same-data parity capture. The remaining checklist states and print output
still need side-by-side review.
