# Compatibility and release status

Qt StripTool 1.0.0 is the first normal release. It provides live Channel Access,
ten curves, controls, configuration load/save, annotations, export, printing,
and an EPICS Archiver Appliance provider. The legacy StripTool executable
remains available for workflows that depend on documented differences.

## Compatible workflows

- Read `StripConfig 1.2`, supported older headers, and the pre-header format; write Motif-readable 1.2 files.
- Preserve ten slots, timing, colors, grid modes, line width, limits, precision, plot state, and linear/log scale.
- Use the legacy bare-filename search order through `STRIP_FILE_SEARCH_PATH`, including the `StripTool.stp` fallback.
- Plot Channel Access monitor updates and local `CPU_Usage` in bounded buffers, preserving reconnect gaps.
- Open/save complete configurations, annotate the graph, export samples and images, and print.

## Differences to review

The Qt history provider uses an EPICS Archiver Appliance; the legacy CAR/AAPI/LANL providers are not ported. Text and CSV exports write one row per curve sample, whereas legacy dump files use one row per timestamp. Partial configuration-group load/save, CDEV, SDDS export, arbitrary X resources, and legacy printer commands are not available. Qt widgets, fonts, dialogs, and print backends follow the selected platform style.

The Qt executable is named `qtstriptool` and installs alongside `StripTool`. The project does not install a `StripTool` alias or a system-wide `.stp` MIME registration.

## Trial and cutover

Change one launcher at a time while retaining its prior `StripTool` command for rollback. Keep the `.stp` argument and relevant Channel Access and search-path environment. Validate a real site's IOC connections, archive behavior, exports, printing, and supported platforms before declaring replacement compatibility.

The detailed [compatibility inventory](https://github.com/rtsoliday/StripTool/blob/master/docs/qtstriptool-compatibility.md) and [parity matrix](https://github.com/rtsoliday/StripTool/blob/master/docs/QtParityMatrix.md) record compatibility and site validation evidence.
