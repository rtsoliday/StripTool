# Command-line options

The Qt StripTool application syntax is:

```text
qtstriptool [--help] [--version] [-history=seconds] [-sampleCount=count] [configuration.stp]
```

| Argument | Behavior |
| --- | --- |
| `--help`, `-h` | Print usage and exit. |
| `--help-all` | Print usage including Qt's standard options and exit. |
| `--version`, `-v` | Print version information and exit. |
| `-history=seconds` | Set the initial time window and automatic archive backfill in seconds; integer from 1 to 2147483647. Default: 300 unless set by the configuration file. |
| `-sampleCount=count` | Set the maximum live samples retained per curve; integer from 1 to 65536. Default: 7200 unless set by the configuration file. |
| `configuration.stp` | Open one configuration file; a bare name is searched in the current directory and then `STRIP_FILE_SEARCH_PATH`. |

Use at most one configuration file. Timing options override settings loaded from that file or the startup fallback. Both single and double dashes are accepted, with `=` or a separate value (for example, `--history 3600`). Invalid values and unknown application options report an error and exit. Qt consumes its standard options, such as `-style adwaita`, before the application sees its arguments. The default style is Fusion.

An absolute path or path containing a directory component is used directly; it is not searched along `STRIP_FILE_SEARCH_PATH`. With no configuration argument, or if an explicit file cannot be loaded, Qt StripTool loads `StripTool.stp` from the current directory if it exists (this name is not searched along `STRIP_FILE_SEARCH_PATH`) and otherwise starts with compiled defaults. A loaded `StripTool.stp` becomes the current configuration: its curves connect at startup, the graph status bar shows `Loaded` and its path, and **File > Save** writes back to that file without asking for a name. Use **File > Save As** to save elsewhere.

Each load failure is reported on standard error with the file and line, for example `qtstriptool: /data/beam.stp:2: invalid value for Strip.Time.Timespan; trying StripTool.stp`. The message ends with `; trying StripTool.stp` only when a different `StripTool.stp` exists and will be read next; otherwise it ends with `; using defaults`.

```sh
qtstriptool beam.stp
qtstriptool -style adwaita beam.stp
qtstriptool -history=3600 -sampleCount=7200 beam.stp
```

For variable behavior, see [environment variables](./environment). For file contents, see [configuration files](./configuration).
