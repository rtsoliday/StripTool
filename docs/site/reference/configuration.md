# Configuration files

Qt StripTool reads and writes Motif-readable `StripConfig 1.2` `.stp` files. It also reads supported older headers and the legacy pre-header format. Saving writes the current 1.2 format and the complete supported model. A UTF-8 byte order mark at the start of a file, as some Windows editors and PowerShell write, is ignored; saved files never have one.

## Current format

The header has an identifier and version. Attribute names are case-sensitive and use dot-separated groups.

```text
StripConfig                   1.2
Strip.Time.Timespan           300
Strip.Curve.0.Name            example:pv
```

| Group | Attributes | Meaning |
| --- | --- | --- |
| `Strip` | `Title` | Graph title, to the end of the line. Empty means no title. |
| `Strip.Time` | `Timespan`, `NumSamples`, `SampleInterval`, `RefreshInterval` | History span in whole seconds, bounded sample count, and the local CPU sample and graph refresh intervals in seconds. |
| `Strip.Color` | `Background`, `Foreground`, `Grid`, `Color1`–`Color10` | Graph and curve colors as three 16-bit red, green, and blue values (0–65535). |
| `Strip.Option` | `GridXon`, `GridYon`, `AxisYcolorStat`, `GraphLineWidth` | Grid modes (0 none, 1 some, 2 all), colored Y axes (0 or 1), and trace width (0–10). |
| `Strip.Curve.<index>` | `Name`, `Units`, `Comment`, `Precision`, `Min`, `Max`, `Scale`, `PlotStatus` | A curve at index 0 through 9. `Scale` is 0 for linear or 1 for base-10 log. |

`Name` and `Units` are single whitespace-delimited words; `Comment` extends to the end of its line. A line break or other control character in the title or a comment, such as from a multi-line paste or a channel `.DESC`, is saved as a space so it cannot start a new field. Motif StripTool saves the units Channel Access reported, so its files can contain an empty `Units` value or several words such as `deg C`. Qt StripTool ignores such a `Units` line and the channel supplies its units again when it connects. Numbers are written with a `.` decimal point in every locale; a lone `,` is also read as the decimal point, as in files Motif StripTool saved in comma-decimal locales. If `NumSamples` is omitted, it is derived from the time span and sample interval. Unknown groups and fields are preserved across a load/save round trip.

Some out-of-range values are adjusted, and the file still loads:

- `Timespan` 0 loads as 1. A value from 2147483648 through 4294967295, the most Motif StripTool reads, loads as 2147483647, the largest the Timing tab and `-history` accept.
- `NumSamples` is clamped to 1 through 65536.
- `SampleInterval` below 0.01 loads as 0.01, and `RefreshInterval` below 0.1 loads as 0.1.
- `GridXon` or `GridYon` outside 0 through 2 loads as 1 (some).
- `GraphLineWidth` is clamped to 0 through 10.
- Any nonzero `AxisYcolorStat` or `PlotStatus` counts as 1.

These values make the load fail without replacing the current configuration:

- A malformed version header, or a version newer than 1.2.
- Text where a number belongs, a fraction where a whole number belongs, a non-finite number, a `Timespan` that is negative or above 4294967295, or any other whole number outside the signed 32-bit range.
- A curve index outside 0 through 9.
- A `Name` that is empty, contains whitespace, or is longer than 63 bytes.
- A one-word `Units` value longer than 31 bytes.
- A `Comment` longer than 255 bytes.
- `Precision` outside 0 through 20, or `Scale` other than 0 or 1.
- A color that is not three whole numbers from 0 through 65535.

Lengths count UTF-8 bytes, so a non-ASCII character takes more than one byte.

Saving also writes the units, `.DESC` comment, precision, and display limits that a connected channel reported, not only values read from the file; units of more than one word are not saved. When the file is loaded again, these saved values are the curve's settings, so later changes to the channel's metadata no longer replace them.

## Legacy pre-header format

Older files without a `StripConfig` header can use `SAMPLEFREQUENCY`, `TIMESPAN`, `CHANNEL`, `MINIMUM`, and `MAXIMUM` records. Qt StripTool reads this form but writes 1.2 when saved. A `CHANNEL` name is checked like `Name`, `SAMPLEFREQUENCY` and `TIMESPAN` are adjusted like the intervals and `Timespan`, and an eleventh `CHANNEL`, or a `MINIMUM` or `MAXIMUM` before the first `CHANNEL`, makes the load fail.

## What the file does not hold

`.stp` stores supported configuration fields. Runtime annotations and captured samples are not saved in it. Use [data export](../operate/files) or a snapshot for those outputs.

The [compatibility guide](../understand/compatibility) covers differences in group load/save and text dump layout. The detailed [legacy compatibility baseline](https://github.com/rtsoliday/StripTool/blob/master/docs/StripToolCompatibilityBaseline.md) records the historical contract.
