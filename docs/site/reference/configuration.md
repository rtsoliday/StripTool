# Configuration files

Qt StripTool reads and writes Motif-readable `StripConfig 1.2` `.stp` files. It also reads supported older headers and the legacy pre-header format. Saving writes the current 1.2 format and the complete supported model.

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
| `Strip.Time` | `Timespan`, `NumSamples`, `SampleInterval`, `RefreshInterval` | History span, bounded samples, local CPU sample interval, and graph refresh interval. |
| `Strip.Color` | `Background`, `Foreground`, `Grid`, `Color1`–`Color10` | Graph and curve colors as three 16-bit red, green, and blue values (0–65535). |
| `Strip.Option` | `GridXon`, `GridYon`, `AxisYcolorStat`, `GraphLineWidth` | Grid modes (0 none, 1 some, 2 all), colored Y axes (0 or 1), and trace width (0–10). |
| `Strip.Curve.<index>` | `Name`, `Units`, `Comment`, `Precision`, `Min`, `Max`, `Scale`, `PlotStatus` | A curve at index 0 through 9. `Scale` is 0 for linear or 1 for base-10 log. |

`Name` and `Units` are single whitespace-delimited words; `Comment` extends to the end of its line. A line break or other control character in the title or a comment, such as from a multi-line paste or a channel `.DESC`, is saved as a space so it cannot start a new field. Motif StripTool saves the units Channel Access reported, so its files can contain an empty `Units` value or several words such as `deg C`. Qt StripTool ignores such a `Units` line and the channel supplies its units again when it connects; units of more than one word are not saved. Precision is limited to 0 through 20. Numbers are written with a `.` decimal point in every locale; a lone `,` is also read as the decimal point, as in files Motif StripTool saved in comma-decimal locales. If `NumSamples` is omitted, it is derived from the time span and sample interval. Unknown groups and fields are preserved across a load/save round trip; invalid known values cause the load to fail without replacing the current configuration. Versions newer than 1.2 are not supported.

## Legacy pre-header format

Older files without a `StripConfig` header can use `SAMPLEFREQUENCY`, `TIMESPAN`, `CHANNEL`, `MINIMUM`, and `MAXIMUM` records. Qt StripTool reads this form but writes 1.2 when saved.

## What the file does not hold

`.stp` stores supported configuration fields. Runtime annotations and captured samples are not saved in it. Use [data export](../operate/files) or a snapshot for those outputs.

The [compatibility guide](../understand/compatibility) covers differences in group load/save and text dump layout. The detailed [legacy compatibility baseline](https://github.com/rtsoliday/StripTool/blob/master/docs/StripToolCompatibilityBaseline.md) records the historical contract.
