# Files, export, and printing

## Configuration files

**File > Open**, **Save**, and **Save As** read and write Motif-readable `StripConfig 1.2` `.stp` files. **Open Recent** uses Qt application settings. **Restore Defaults** in Controls replaces the current model with compiled defaults. Open and save work on the complete supported configuration; partial group selection is not available.

Use a `.stp` file to preserve curve definitions, timing, and appearance. Runtime annotations and sampled data are separate from the configuration. See [configuration files](../reference/configuration) for format details.

## Export samples

**File > Export Text** and **Export CSV** write samples in the visible time range. Qt StripTool writes one row per curve sample, with timestamp, curve, value, status, and severity. The legacy dump layout uses a separate curve column for each timestamp, so check consumers before changing an automated export workflow. SDDS export is not built.

## Capture and print the graph

**Save Snapshot** writes PNG or JPEG according to the chosen filename or filter. If the filename has no extension, the selected format adds `.png` or `.jpg`. **Print** and **Print Preview** render through Qt PrintSupport; available printers and PDF support depend on the platform backend.

The [appearance guide](../understand/appearance) explains why fonts, DPI, and native print output can differ from Motif.
