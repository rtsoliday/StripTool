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
| `Strip.Time` | `Timespan`, `NumSamples`, `SampleInterval`, `RefreshInterval` | History span, bounded samples, local CPU sample interval, and graph refresh interval. |
| `Strip.Color` | `Background`, `Foreground`, `Grid`, `Color1`–`Color10` | Graph and curve RGB colors. |
| `Strip.Option` | `GridXon`, `GridYon`, `AxisYcolorStat`, `GraphLineWidth` | Grid modes, colored Y axes, and trace width. |
| `Strip.Curve.<index>` | `Name`, `Units`, `Comment`, `Precision`, `Min`, `Max`, `Scale`, `PlotStatus` | A curve at index 0 through 9. `Scale` is 0 for linear or 1 for base-10 log. |

`Name` and `Units` are whitespace-delimited; `Comment` extends to the end of its line. Precision is limited to 0 through 20. If `NumSamples` is omitted, it is derived from the time span and sample interval. Unknown groups and fields are preserved across a load/save round trip; invalid known values cause the load to fail without replacing the current configuration. Versions newer than 1.2 are not supported.

## Legacy pre-header format

Older files without a `StripConfig` header can use `SAMPLEFREQUENCY`, `TIMESPAN`, `CHANNEL`, `MINIMUM`, and `MAXIMUM` records. Qt StripTool reads this form but writes 1.2 when saved.

## What the file does not hold

`.stp` stores supported configuration fields. Runtime annotations and captured samples are not saved in it. Use [data export](../operate/files) or a snapshot for those outputs.

The [compatibility guide](../understand/compatibility) covers differences in group load/save and text dump layout. The detailed [legacy compatibility baseline](https://github.com/rtsoliday/StripTool/blob/master/docs/StripToolCompatibilityBaseline.md) records the historical contract.
