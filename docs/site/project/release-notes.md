# Release notes

## Qt StripTool 1.1.0

Changes since Qt StripTool 1.0.0.

- Compatible SDDS libraries enable binary SDDS export from the File menu and
  the graph's context menu. Exports cover the visible time range, with a
  `Time` column and one column per configured PV. Missing samples and
  disconnect gaps are NaN.
- Configuration files are limited to 4 MiB, 64 KiB per line, and 10,000 lines.
  Open rejects oversized input transactionally, and Save validates these
  limits before replacing an existing file.
- CSV exports prefix curve names that begin with a spreadsheet formula
  character with a tab inside the quoted field.
- Archiver Appliance requests restrict redirects to the same origin.
- Shared Makefile rules and build recipes have been synchronized with the
  companion applications. The macOS build avoids linking the math library
  twice.

[Qt StripTool 1.0.0 on GitHub](https://github.com/rtsoliday/StripTool/releases/tag/v1.0.0)
