# StripTool Compatibility Baseline

This document defines the legacy behavior against which `qtstriptool` is
implemented and reviewed. It is based on the Motif source at version 2.5.18.0.
The accompanying feature matrix assigns each behavior to a release gate, and
the files under `tests/fixtures/config/` preserve representative configuration
inputs.

The baseline describes behavior; it does not require reproducing unsafe
implementation details or toolkit-specific defects.

## Release Gates

- **Preview**: needed for a useful developer preview.
- **Replacement**: needed before Qt StripTool can replace Motif StripTool in
  normal operation.
- **Optional**: useful, but not required for replacement.
- **Site decision**: current operational need must be confirmed before scope is
  committed.

The authoritative item-by-item classification is in
`docs/striptool-feature-matrix.csv`.

## Configuration File Contract

### Current format

The current writer emits `StripConfig 1.2`. The first line consists of that
identifier and version separated by whitespace. Attribute names are
case-sensitive and use dot-separated components.

```text
StripConfig                   1.2
Strip.Time.Timespan           300
Strip.Curve.0.Name            example:pv
```

Known attributes are:

| Group | Attributes | Value contract |
|---|---|---|
| `Strip.Time` | `Timespan`, `NumSamples`, `SampleInterval`, `RefreshInterval` | Unsigned seconds, integer samples, and floating-point seconds |
| `Strip.Color` | `Background`, `Foreground`, `Grid`, `Color1` through `Color10` | Three unsigned 16-bit RGB components |
| `Strip.Option` | `GridXon`, `GridYon`, `AxisYcolorStat`, `GraphLineWidth` | Integer values |
| `Strip.Curve.<index>` | `Name`, `Units`, `Comment`, `Precision`, `Min`, `Max`, `Scale`, `PlotStatus` | Curve index 0 through 9; field types described below |

Curve semantics:

- `Name` and `Units` are whitespace-delimited strings.
- `Comment` consumes the remainder of the line and trims trailing whitespace.
- `Precision` is intended to be clamped to 0 through 20.
- `Min` and `Max` are doubles.
- `Scale` is `0` for linear or `1` for base-10 logarithmic.
- `PlotStatus` uses integer false/true semantics.
- Curves are limited to ten by `STRIP_MAX_CURVES`.

The writer emits time, color, option, and curve attributes in that group
order. It writes curve `Units`, `Comment`, `Precision`, `Min`, and `Max` only
when those values have explicitly been set. It always writes `Name`, `Scale`,
and `PlotStatus` for active curves.

The loader ignores lines outside the `Strip.*` namespace and ignores unknown
groups or attributes. A syntactically known attribute with an invalid value
causes the complete load to fail and preserves the prior configuration.

When `NumSamples` is omitted, the loader derives it as:

```text
ceil(Timespan / SampleInterval)
```

Files whose version is newer than 1.2 are not supported. Qt StripTool must
read 1.2 and earlier supported forms, and should emit 1.2 until an explicitly
versioned format change is approved.

### Legacy pre-header format

If the first line is not a `StripConfig <major>.<minor>` header, the loader
uses its old-format reader. It recognizes:

- `SAMPLEFREQUENCY <seconds>`: sets both sample and refresh intervals.
- `TIMESPAN <seconds>`
- `CHANNEL <pv-name>`: adds the next curve in order.
- `MINIMUM <value>` and `MAXIMUM <value>`: apply to the most recent channel.

Qt StripTool must continue to read this format for replacement parity. It
does not need to write it.

### Parser defects that are not compatibility requirements

The Qt implementation must reject these cases safely and provide a useful
diagnostic:

- The current parser accepts curve index 10 even though valid storage indices
  are 0 through 9. This is an out-of-bounds defect, not a file-format feature.
- The old-format parser does not guard against more than ten `CHANNEL` lines.
- The current-format parser assumes every processed line contains whitespace;
  empty or malformed lines can overrun its token scan.
- Individual text fields are constrained by fixed legacy buffers. Qt should
  enforce the documented limits without overflowing: 63 characters for a
  curve name, 31 for units, and 255 for a comment.
- The legacy precision clamp assigns its second bound from the original value,
  so a negative precision can survive. Qt should enforce the intended 0 to 20
  range.

These differences are recorded in the known-differences document and covered
by negative tests.

## Defaults

| Setting | Legacy default |
|---|---|
| Title | `Untitled` |
| Time span | 300 seconds |
| Number of samples | 7200 |
| Sample interval | 1 second |
| Refresh interval | 1 second |
| Background | White |
| Foreground | Black |
| Grid | Grey75 |
| Curve colors 1-10 | Blue, OliveDrab, Brown, CadetBlue, Orange, Purple, Red, Gold, RosyBrown, YellowGreen |
| X grid | Some (`1`) |
| Y grid | Some (`1`) |
| Colored Y axes | Enabled (`1`) |
| Graph line width | 2 |
| Curve units | `Undefined` |
| Curve comment | Empty |
| Curve precision | 4 |
| Curve minimum | `1e-7` |
| Curve maximum | `1e+7` |
| Curve scale | Linear (`0`) |
| Curve plotted | Enabled |
| Maximum curves | 10 |
| Data cache ceiling | 8 MiB |
| Connection timeout | 5 seconds |
| Default configuration name | `StripTool.stp` |
| Configuration file pattern | `*.stp` |

The graph's nominal default physical size is 250 mm by 180 mm. Exact pixel
size depends on display metrics and is an appearance reference rather than a
portable geometry guarantee.

## Startup and File Search

After the X toolkit consumes its options, the legacy application treats the
first remaining positional argument as a configuration file. It does not
define an application-specific option parser.

For a relative bare filename, lookup is:

1. The filename in the current working directory.
2. Each directory in `STRIP_FILE_SEARCH_PATH`, using the platform path-list
   separator.

If the name is absolute or already contains a directory component, only that
path is tried. When compiled with `USE_OLD_FILE_SEARCH`, the environment name
is `EPICS_DISPLAY_PATH` instead.

If no explicit configuration loads successfully, the application tries
`StripTool.stp` in the current working directory. Failure to find a file is
non-fatal and leaves compiled defaults active.

The successfully loaded filename becomes the configuration filename, and its
basename becomes the displayed title.

## Resource and Environment Precedence

X resources and `.stp` configuration are distinct mechanisms.

Legacy X resources are merged in this order, with later values overriding
earlier values:

1. Compiled fallback resources in `StripFallback.h`.
2. The site resource file named by `STRIP_SITE_DEFAULTS`, or `./StripToolrc`
   when unset.
3. `$HOME/.StripToolrc` on Unix-like systems.

Qt does not need to reproduce arbitrary X resource processing. It must map
still-relevant site/user settings to documented Qt configuration, or list
them as intentional differences.

Environment variables used by the legacy program are:

| Variable | Purpose | Qt expectation |
|---|---|---|
| `STRIP_FILE_SEARCH_PATH` | Search list for a bare `.stp` filename | Preserve |
| `EPICS_DISPLAY_PATH` | Alternate search variable in old-search builds | Site decision |
| `STRIP_SITE_DEFAULTS` | Site X resource file | Replace or document |
| `STRIP_HELP_PATH` | Help URL override | Preserve |
| `STRIP_PRINTER_NAME` | Preferred printer name | Replace through Qt printing where possible |
| `STRIP_PRINTER_DEVICE` | Legacy print device/type | Site decision |
| `PSPRINTER` | Fallback printer name | Site decision |
| `STRIP_DUMP_TYPE_DEFAULT` | Default dump type in file dialog | Preserve equivalent behavior |
| `NETSCAPEPATH` | Legacy browser executable | Obsolete under Qt desktop services |
| `ComSpec` | Windows command interpreter used by browser helper | Obsolete under Qt desktop services |

Archive-specific builds additionally consult backend-specific variables in
the LANL/CAR implementation. Their names and support status must be recorded
when the active archive provider is selected.

## Acquisition and Runtime Behavior

- Channel Access is the default data source.
- Each configured curve occupies one of ten channel slots.
- Connection supplies the native value, engineering units, precision, and
  display limits when available.
- The description field is requested separately and becomes the curve
  comment.
- Sampling cadence and graph refresh cadence are independently configurable.
- Data is stored in bounded ring buffers and associated with timestamps and a
  status value.
- Connection loss is represented in curve state, and Channel Access
  automatically reconnects when the IOC returns.
- `CPU_Usage` is a special local curve and does not create a CA channel.
- Live and archived data can be joined over a requested time range when a
  history implementation is compiled in.

The Qt implementation must marshal EPICS callbacks safely to its GUI thread,
record every delivered monitor update with its workstation delivery timestamp
and alarm metadata, refresh the display independently, and make connection and
stale-data states visible and testable. The legacy sample interval remains applicable to
the local `CPU_Usage` source rather than resampling Channel Access values.

## Graph and Interaction Behavior

Replacement-level behavior includes:

- Up to ten simultaneously configured curves.
- Individual curve color, plotted state, linear/log scale, precision, and
  manual limits.
- Time-based X axis and colored per-curve Y scales.
- None/some/all grid modes and configurable line width.
- Incremental refresh and automatic scrolling.
- Pause/resume scrolling, pan, coarse/fine zoom, reset, replot, and
  auto-scaling.
- Pointer location/value feedback.
- Fixed from/to time-range selection.
- Creation, selection, editing, movement, and deletion of annotations.
- Graph and controls windows that can be shown, hidden, or raised.
- Clear data, dismiss, and quit workflows.

Exact Motif event routing and X11 drawing primitives are not compatibility
requirements. The same user outcomes are.

## Control Window Behavior

The controls window exposes:

- A PV name entry and connect action.
- One row per curve with name, color, plotted state, scale, precision, minimum,
  maximum, modify, and remove controls.
- Time span split into hours, minutes, and seconds.
- Number of samples, sample interval, and graph refresh interval.
- Foreground, background, grid, and curve colors.
- X/Y grid visibility, colored Y axes, and graph line width.
- Load, Save, and Save As operations with independently selectable Timing,
  Colors, Graph Attributes, and Curve Attributes groups.
- Window and Help menus, including web help, help-about-help, and version
  information.

The Qt UI may use native controls and layouts, but action availability,
validation, cancellation, and model updates must remain equivalent.

## Output and Integration Features

| Feature | Legacy behavior | Direction |
|---|---|---|
| Text dump | Writes visible/ranged buffered samples | Replacement parity |
| CSV dump | Writes comma-separated buffered samples | Replacement parity |
| SDDS dump | Compile-time optional | Site decision |
| Snapshot | Captures graph output | Replace with Qt image export |
| Print | Printer/device environment and platform commands | Replace with Qt PrintSupport; confirm site requirements |
| Help | Opens configured URL through legacy browser helper | Use Qt desktop URL handling |
| History | Compile-time selectable NULL, test, AR/Archive Record, CAR/AAPI, or LANL code paths | Native Archiver Appliance provider |
| CDEV | Alternate compile-time data source | Site decision |

## Baseline Evidence

Primary source evidence:

- `striptool/StripVersion.h`: application version.
- `striptool/StripDefines.h`: limits, defaults, filenames, and environment
  names.
- `striptool/StripConfig.c` and `striptool/StripConfig.h`: configuration
  grammar and model.
- `striptool/StripTool.c`: command-line file loading and search behavior.
- `striptool/Strip.c`: startup resources, main workflows, timers, graph commands,
  printing, snapshots, dumps, and history range UI.
- `striptool/StripDialog.c`: control window, partial save/load masks, and
  validation.
- `striptool/StripGraph.c`: rendering and pointer interaction.
- `striptool/StripCA.c` and `striptool/StripDAQ.h`: Channel Access behavior.
- `striptool/StripDataSource.c` and `striptool/StripDataSource.h`: buffering
  and rendering input.
- `striptool/StripHistory.h` and `striptool/StripHistory*.c*`: history provider
  contract and implementations.
- `striptool/Annotation.c` and `striptool/Annotation.h`: annotation behavior.
- `striptool/Makefile`: build-time feature selection.
