# Files, export, and printing

## Configuration files

Configuration and data-export files support non-ASCII file and folder names.

**File > Open**, **Save**, and **Save As** read and write Motif-readable `StripConfig 1.2` `.stp` files. **Open Recent** uses Qt application settings. **Restore Defaults** in Controls replaces the current model with compiled defaults. Open and save work on the complete supported configuration; partial group selection is not available. An Open error names the file and, for a malformed file, the failing line.

Use a `.stp` file to preserve curve definitions, timing, and appearance. Runtime annotations and sampled data are separate from the configuration. See [configuration files](../reference/configuration) for format details.

## Export samples

**File > Export Text** and **Export CSV** write samples in the visible time range. Qt StripTool writes one row per curve sample, with timestamp, curve, value, status, and severity. Timestamps are UTC in ISO 8601 form with milliseconds, such as `2026-10-16T14:05:09.250Z`. A row with value `nan` and severity 3 (INVALID) marks a gap from a disconnect, CA error, or lost updates; a genuine NaN reading carries its own alarm severity. Earlier builds omitted gap rows, so downstream parsers should treat them as missing data rather than readings. Legacy StripTool dump files instead have one row per timestamp and a column for each curve, so check consumers before changing an automated export workflow. SDDS export is not built.

## Capture and print the graph

**Save Snapshot** writes PNG or JPEG according to the chosen filename or filter. If the filename has no extension, the selected format adds `.png` or `.jpg`. **Print** and **Print Preview** render through Qt PrintSupport; available printers and PDF support depend on the platform backend.

The [appearance guide](../understand/appearance) explains why fonts, DPI, and native print output can differ from Motif.
