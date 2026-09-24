# Qt StripTool user guide

Qt StripTool displays timestamped EPICS Channel Access values for as many as
ten process variables. The graph and controls windows share one configuration;
changes made in Controls immediately affect the graph and acquisition.

This guide describes the current **alpha**. Keep the legacy `StripTool`
available for operational rollback until the compatibility release gates are
closed.

## Start the application

```text
qtstriptool [--help] [--version] [configuration.stp]
```

Qt StripTool defaults to Qt's Fusion style for consistent, compact controls
across desktop environments. Qt also consumes its standard command-line
options; for example, `qtstriptool -style adwaita example.stp` selects an
installed native style instead. Pass at most one configuration file; other
application-specific switches are not defined.

For a bare relative configuration name, Qt StripTool checks the current
directory and then each directory in `STRIP_FILE_SEARCH_PATH`. Absolute paths
and paths containing a directory component are used directly. If the explicit
file cannot be loaded, the application tries `StripTool.stp` in the current
directory and otherwise starts with compiled defaults.

Useful environment variables are:

| Variable | Current Qt behavior |
|---|---|
| `EPICS_CA_ADDR_LIST`, `EPICS_CA_AUTO_ADDR_LIST` and other EPICS CA variables | Used by EPICS Channel Access |
| `STRIP_FILE_SEARCH_PATH` | Path-list used to find a bare startup `.stp` name |
| `STRIP_HELP_PATH` | Local file or URL opened by **Help > Help** |
| `QT_QPA_PLATFORM`, `QT_STYLE_OVERRIDE` and standard Qt variables | Interpreted by Qt |

Legacy X resource and printer environment variables are not consumed; see the
known differences in the compatibility guide.

## Configure curves

The Controls window opens at startup and is available later through
**Window > Show Controls**.

1. Enter a PV in the **PV** field and select **Connect**, or enter a name in an
   unused curve row.
2. Set Plot, Linear or Log 10 scale, precision, minimum, maximum, and color.
3. Select **Modify**, or press Return in that row's Name, Minimum, or Maximum
   field, to apply row edits. **Remove** frees the row and stops its acquisition.
4. Use the Timing tab to set history length, bounded sample count, the local
   `CPU_Usage` sampling interval, and the independent display-refresh interval.
5. Use Appearance for graph colors, grid density, colored Y axes, and line
   width.

Y-axis scaling is graph-wide. **Auto Scale** fits every plotted curve to its
visible data, including curves added while that mode is active. **Reset View**
returns every curve to the Minimum and Maximum shown in Controls; curves added
afterward also use their configured or provider-supplied limits.

`CPU_Usage` is a local pseudo-curve and does not open a Channel Access
subscription. Channel Access curves record monitor updates as they arrive,
using workstation delivery time together with the IOC value and alarm
metadata; the sampling interval does not resample them. This common clock
keeps curves aligned when IOC clocks differ. Traces use step lines on both
linear and logarithmic axes. For logarithmic curves, choose positive limits; non-positive
samples create a gap and cannot be plotted on a base-10 scale.

## Work with the graph

The View menu can pause time scrolling, enable or disable auto-scroll, pan in time or
value, zoom either axis, auto-scale configured curves, reset both axes, and
force a replot. Moving the pointer over the plot updates the location readout.
Each X-axis tick has two labels: minutes relative to the right edge above,
and local clock time below. The rightmost relative label is always 0; earlier
ticks are negative. With auto-scroll, the right edge tracks the latest time.
With auto-scroll off, it is the end of the fixed visible range.
The toolbar uses arrows for panning, X+/X− for time zoom, and Y+/Y− for value
zoom. Right click a toolbar Pan or Zoom button for a smaller step, as in the
legacy graph. Vertical zoom uses logarithmic units for Log 10 curves.
The legend beside the plot shows each visible curve's name and latest value.
Click a legend entry to read that curve's value at the pointer location.

Drag the empty plot with the left button to pan. Left click an annotation box
to select it; hold the middle button on the box to move it. Right click the
graph for controls, annotation, print, snapshot, export, and retry commands.
**Annotate Here** creates a note at the clicked time and value. Double click
an annotation to edit its text, or double click empty plot space to add one.
Delete or Backspace removes the selected annotation. Annotations are runtime
graph state and are not saved in `.stp` files.

**View > Historical Range** retrieves the selected range from the EPICS
Archiver Appliance and joins returned samples with live data. The default
retrieval root is `http://asddtn03.aps4.anl.gov:17668/retrieval`. Set
`QTSTRIPTOOL_ARCHIVER_URL` to another retrieval root, or directly to its
`data/getData.json` endpoint, before starting qtstriptool to override it.
Requests use automatic `lastSample` reduction to keep long ranges responsive.
Retrieval is asynchronous and implemented directly with Qt Network; no Python
runtime or external conversion command is required. Archived scalar numeric
values retain their archive timestamps, status, and severity. `CPU_Usage` is a
local live-only channel and is not sent to the Archiver.

Channel Access values are held as step lines until another monitor update or an
actual disconnect/error is received. A quiet PV may be marked **Stale** in the
Controls window, but silence alone does not end its plotted line because CA
monitors are not required to repeat unchanged values.

When a Channel Access PV is added, qtstriptool also attempts a background
Archiver request for the preceding five minutes. If the Archiver is unavailable
or has no usable data, this automatic backfill fails silently and live plotting
continues normally. Errors from an explicitly selected **Historical Range** are
still reported.

## Open, save, export, and print

- **File > Open**, **Save**, and **Save As** read and write Motif-readable
  `StripConfig 1.2` files. Open Recent is stored with Qt application settings.
- **Restore Defaults** in Controls replaces the current model with compiled
  defaults.
- **Export Text** and **Export CSV** write samples in the visible time range.
  Qt uses one row per curve sample; legacy StripTool's dump files use one row
  per timestamp with separate curve columns.
- **Save Snapshot** writes the graph as PNG or JPEG according to the chosen
  filename/filter.
- **Print** and **Print Preview** render the graph through Qt PrintSupport.
  Available printers and PDF support come from the platform's Qt print backend.

Saving always writes the complete supported model. Legacy partial group
load/save selection is not present. SDDS export is not built.

## Launch-script and desktop migration

Migrate one launcher at a time and preserve an explicit rollback command:

```sh
# old
exec /opt/epics/extensions/bin/$EPICS_HOST_ARCH/StripTool "$config"

# alpha trial
exec /opt/epics/extensions/bin/$EPICS_HOST_ARCH/qtstriptool "$config"
```

Keep the `.stp` argument and relevant CA/search-path environment unchanged.
Do not create a `StripTool` alias or replace the legacy binary during alpha.
For a fast rollback, retain the old launcher or add a site-owned selector that
names both executables explicitly.

`make install-qt-package` installs
`org.epics.qtstriptool.desktop` on Unix-like systems. Desktop databases may
need their normal administrator refresh after package installation. The
project does not register a system-wide `.stp` MIME type; sites that want file
association should define and review one in their native package rather than
changing the executable name.

## Troubleshooting

- **Build says EPICS Base is missing:** set `EPICS_BASE` to a built Base tree
  containing `include/cadef.h` and host libraries.
- **Build says Qt is missing:** install Qt Widgets/PrintSupport/Test development
  files or select an installed major with `QT_VERSION=5|6`.
- **PV remains disconnected:** verify the PV with site CA tools and check the
  EPICS CA address-list environment and network path.
- **Startup file is not found:** use an absolute path or inspect
  `STRIP_FILE_SEARCH_PATH`; directory-containing paths are not searched.
- **History reports no provider:** this is expected in the distributed alpha.
- **Help shows built-in guidance:** set `STRIP_HELP_PATH` to a reachable URL or
  an existing local help file if site documentation is required.
- **Visual or printer output differs from Motif:** Qt uses the active platform
  style, fonts, DPI, and print backend. Review the appearance guide before
  treating pixel differences as defects.
