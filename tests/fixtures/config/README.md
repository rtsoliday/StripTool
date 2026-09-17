# StripTool Configuration Fixtures

These files preserve representative inputs for the legacy compatibility
contract. They are inputs for the Qt parser tests planned in Step 4; they are
not site configurations and intentionally use synthetic PV names.

- `canonical-1.2.stp`: every current-format attribute, all ten curve slots,
  both scale modes, comments, colors, and plotted states.
- `derived-samples-1.2.stp`: omits `NumSamples`; the expected derived value is
  `ceil(601 / 0.25) = 2404`.
- `unknown-fields-1.2.stp`: known data mixed with attributes the legacy loader
  ignores.
- `legacy-format.stp`: pre-header `SAMPLEFREQUENCY`, `TIMESPAN`, `CHANNEL`,
  `MINIMUM`, and `MAXIMUM` syntax.
- `visual-baseline-1.2.stp`: uses the built-in `CPU_Usage` pseudo-curve so the
  legacy UI can be captured safely without a Channel Access IOC.

Expected parser rules and intentional safety differences are documented in
`docs/StripToolCompatibilityBaseline.md`.

