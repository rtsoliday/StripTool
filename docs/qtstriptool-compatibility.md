# Qt StripTool compatibility and cutover

Qt StripTool reads and writes the established StripTool configuration format
and recreates the principal live trending workflow with toolkit-neutral model,
acquisition, and plotting code. The detailed behavioral contract is in
`StripToolCompatibilityBaseline.md`; test and platform evidence is maintained
in `QtParityMatrix.md`.

## Current release status: alpha

The current code meets the planned alpha milestone: live Channel Access,
ten-curve connection and plotting controls, and `.stp` load/save are implemented. It also
contains annotations, text/CSV and image exports, Qt printing, and a cancellable
history-provider interface. Those additional features do not make the release
beta because production archive history and cross-platform build evidence are
still missing.

The legacy executable remains `StripTool`; the Qt executable remains
`qtstriptool`. The Qt program must not replace or alias `StripTool` at this
stage.

## Compatible behavior

- Reads `StripConfig 1.2`, supported older headers, and the pre-header legacy
  form; writes Motif-readable 1.2 files.
- Preserves ten curve slots, timing, colors, grid modes, line width, curve
  limits, precision, plot state, and linear/log scale.
- Uses the legacy bare-filename search order through
  `STRIP_FILE_SEARCH_PATH`, including the `StripTool.stp` fallback.
- Records Channel Access monitor updates with workstation timestamps and alarm
  metadata in bounded buffers, independently refreshes the display, preserves
  reconnect gaps, and periodically samples the local `CPU_Usage` curve.
- Provides the normal live plot operations, multiple colored axes,
  annotations, controls, full configuration open/save, text/CSV export,
  snapshots, and native print/preview.

Compatibility means equivalent user outcomes and data semantics. It does not
mean reproducing X11 drawing primitives, Motif event routing, unsafe parser
bugs, or exact pixels.

## Known differences

- **Archive history:** historical range requests use the EPICS Archiver
  Appliance retrieval service, defaulting to
  `http://asddtn03.aps4.anl.gov:17668/retrieval`. The endpoint can be
  overridden with `QTSTRIPTOOL_ARCHIVER_URL`. Legacy CAR/AAPI/LANL providers
  have not been ported. The native provider currently accepts scalar numeric
  samples; archived strings and waveforms are ignored.
- **CDEV:** the Qt build supports Channel Access and the local `CPU_Usage`
  provider; it has no CDEV provider.
- **SDDS:** plain text and CSV are supported; optional legacy SDDS export is
  not enabled.
- **Text/CSV layout:** Qt currently writes one row per curve sample with an ISO
  timestamp, curve name, value, status, and severity. Legacy StripTool writes
  one row per timestamp with a column for each curve. Consumers of legacy dump
  files need a format conversion until this output gate is resolved. Qt has
  separate Text and CSV actions and does not use `STRIP_DUMP_TYPE_DEFAULT`.
- **Configuration groups:** Qt open/save operates on the complete model. The
  legacy dialog's partial Timing/Colors/Graph/Curve group load and save is not
  present.
- **Curve metadata editing:** units and comments are read from `.stp` files,
  and Channel Access supplies units and `.DESC` comments when unset, but the
  controls window has no direct units or comment editor yet.
- **Resources and defaults:** arbitrary X resources, `STRIP_SITE_DEFAULTS`,
  `$HOME/.StripToolrc`, and exact site fallback resources are not interpreted.
  Relevant settings belong in `.stp` files or future documented Qt settings.
- **Printing:** Qt PrintSupport and platform printer discovery replace legacy
  shell/X11 printing. `STRIP_PRINTER_NAME`, `STRIP_PRINTER_DEVICE`, and
  `PSPRINTER` are not used.
- **Help:** Qt desktop services replace `NETSCAPEPATH` and platform-specific
  browser helper commands.
- **Appearance:** Qt widgets, fonts, metrics, DPI scaling, dialogs, and
  accessibility behavior are intentionally native. Exact Motif styling and
  geometry are not reproduced.
- **Platform evidence:** Linux x86_64 with Qt 5 was verified on 2026-09-17.
  macOS arm64 builds and non-IOC tests passed locally with Qt 5.15 and Qt 6 on
  2026-09-23. A Qt 6 Channel Access test passed against a local soft IOC.
  Windows remains unverified; site IOC, reconnect, and soak gates are open.
- **Sample count below 7200:** Qt accepts explicit counts down to 1 so a value
  entered in its controls survives save and reload. Legacy Motif clamps an
  explicit count to 7200 even though it can derive a smaller count when the
  field is omitted.
- **Safer parsing:** Qt rejects curve index 10, more than ten old-format
  channels, malformed known attributes, unsupported versions, and invalid
  model values transactionally. Precision is constrained to 0 through 20.
  Legacy acceptance caused by buffer, bounds, or clamp defects is not retained.
- **Executable and desktop identity:** Qt installs as `qtstriptool` and
  `org.epics.qtstriptool`; no `.stp` MIME registration or `StripTool` alias is
  installed.

Any site that depends on a listed difference must resolve it or formally
accept it before declaring replacement compatibility.

## Release progression and gates

1. **Developer preview** — configuration parsing, simulated data, and a basic
   plot. Completed.
2. **Alpha** — live CA, complete controls, and load/save. **Current stage.**
3. **Beta** — a selected history backend, annotations, exports, printing, and
   clean supported cross-platform builds. The existing feature implementations
   must be coupled with real archive and platform evidence.
4. **Compatibility release** — every mandatory automated parity test passes;
   the same configuration and IOC/archive data pass recorded side-by-side,
   output, reconnect, visual, and soak review; site-specific differences are
   resolved or accepted.
5. **Default-switch release** — launch documentation and managed desktop
   entries recommend `qtstriptool`, while `StripTool` remains available for a
   defined rollback interval.
6. **Optional rename/alias release** — only after the transition interval may
   Qt be exposed as `StripTool`; retain the Motif application under an explicit
   legacy name for the announced support period.

Promotion is evidence-based, not feature-count based. Update the platform
record and attach manual results for each candidate rather than carrying
forward assumptions from a previous host.

## Cutover checklist

Before compatibility or default-switch approval:

1. Select, implement, configure, and load-test the supported APS archive
   provider; document ownership, authentication, endpoints, and failure modes.
2. Run all automated suites on every supported Qt/OS/compiler combination and
   the IOC test against a representative numeric PV.
3. Complete the side-by-side procedure in `QtParityMatrix.md` with the site's
   representative ten-PV `.stp` set, including disconnect and reconnect.
4. Compare exports numerically and inspect snapshots and print-to-PDF output.
5. Complete the idle, high-rate, 24-hour, archive, and repeated-start soak
   gates in `qtstriptool-performance.md`.
6. Review every known difference with operations and application owners.
7. Stage packages on clean hosts and verify runtime Qt/EPICS dependencies,
   desktop launch, file arguments, help, printing, and uninstall/rollback.
8. Change launch scripts from `StripTool` to `qtstriptool` without changing
   `.stp` arguments; keep an explicit legacy launcher throughout the announced
   rollback period.

The optional alias decision is deliberately separate from technical parity so
that scripts cannot silently change application during alpha or beta testing.
